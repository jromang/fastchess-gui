#include "pgn_file.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>

#include "files.hpp"

namespace fcg::tour {

namespace fs = std::filesystem;

namespace {

// The lock of appendShared() and settleShared().
std::mutex& sharedMutex() {
    static std::mutex mutex;
    return mutex;
}

bool seekTo(std::FILE* file, uint64_t offset) {
#ifdef _WIN32
    return _fseeki64(file, static_cast<__int64>(offset), SEEK_SET) == 0;
#else
    return fseeko(file, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
}

// Overwrites [from, to) of `file`, open for update, with newlines, unless it holds blank space only already (the
// newlines of a PGN file filled up after a power cut, or of a game blanked before). Sets `wrote` when it writes.
// False when it cannot.
bool blankRange(std::FILE* file, uint64_t from, uint64_t to, bool& wrote) {
    constexpr uint64_t CHUNK = 64 * 1024;
    std::string text;
    for (uint64_t at = from; at < to; at += CHUNK) {
        const auto n = static_cast<std::size_t>(std::min(CHUNK, to - at));
        text.resize(n);
        if (!seekTo(file, at) || std::fread(text.data(), 1, n, file) != n) return false;
        if (text.find_first_not_of(" \t\r\n") == std::string::npos) continue;
        text.assign(n, '\n');
        if (!seekTo(file, at) || std::fwrite(text.data(), 1, n, file) != n) return false;
        wrote = true;
    }
    return true;
}

}  // namespace

PgnFile::PgnFile(fs::path path, std::optional<std::vector<PgnSpan>> kept, bool between) : path_(std::move(path)) {
    std::error_code ec;
    if (path_.has_parent_path()) fs::create_directories(path_.parent_path(), ec);

    std::optional<uint64_t> keep;
    if (kept) {
        std::sort(kept->begin(), kept->end(), [](const PgnSpan& a, const PgnSpan& b) { return a.offset < b.offset; });
        keep = 0;
        for (const auto& span : *kept) keep = std::max(*keep, span.end());
    }
    if (keep && fs::exists(path_, ec) && fs::file_size(path_, ec) > *keep) fs::resize_file(path_, *keep, ec);

    // Text between the journal's games. The games are read the same whether it is blanked or not: a failure here is
    // no reason to stop, and the next opening tries again.
    if (between && kept && !kept->empty() && fs::exists(path_, ec)) {
        const auto size = fs::file_size(path_, ec);
        if (std::FILE* file = ec ? nullptr : openFile(path_, "r+b")) {
            bool wrote    = false;
            uint64_t from = 0;
            for (const auto& span : *kept) {
                if (from >= size) break;
                if (span.offset > from && !blankRange(file, from, std::min(span.offset, size), wrote)) break;
                from = std::max(from, span.end());
            }
            if (wrote) (void)syncFile(file);
            std::fclose(file);
        }
    }

    file_ = openFile(path_, "ab");
    if (!file_) error_ = "cannot write " + path_.string();
    size_ = fs::exists(path_, ec) ? fs::file_size(path_, ec) : 0;

    if (file_ && keep && size_ < *keep) {
        missing_ = *keep - size_;
        const std::string blank(missing_, '\n');
        if (std::fwrite(blank.data(), 1, blank.size(), file_) == blank.size() && syncFile(file_)) {
            size_ = *keep;
        } else {
            error_ = "cannot write " + path_.string();
        }
    }
}

PgnFile::~PgnFile() {
    if (file_) std::fclose(file_);
}

PgnSpan PgnFile::append(const std::string& pgn) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!error_.empty()) throw std::runtime_error(error_);

    // On the disk before the journal points to it: after a power cut, no journaled game lies past the end.
    PgnSpan span{size_, static_cast<uint32_t>(pgn.size())};
    if (std::fwrite(pgn.data(), 1, pgn.size(), file_) != pgn.size() || !syncFile(file_)) {
        error_ = "cannot write " + path_.string() + " (is the disk full?)";
        throw std::runtime_error(error_);
    }
    size_ += pgn.size();
    return span;
}

bool PgnFile::blank(const PgnSpan& span) {
    // Through another handle than the one games are appended with, which writes at the end only; the games appended
    // meanwhile do not overlap this one.
    std::FILE* file = openFile(path_, "r+b");
    if (!file) return false;
    const std::string text(span.length, '\n');
    const bool written =
        seekTo(file, span.offset) && std::fwrite(text.data(), 1, text.size(), file) == text.size() && syncFile(file);
    return std::fclose(file) == 0 && written;
}

void appendShared(const fs::path& file, const std::string& text, const std::function<void(uint64_t at)>& before) {
    std::lock_guard<std::mutex> lock(sharedMutex());

    std::error_code ec;
    if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
    std::FILE* out = openFile(file, "ab");
    if (!out) throw std::runtime_error("cannot write " + file.string());
    if (before) {
        const auto size = fs::file_size(file, ec);
        try {
            before(ec ? 0 : size);
        } catch (...) {
            std::fclose(out);
            throw;
        }
    }
    const auto size    = appendSharedCut ? std::min(text.size(), appendSharedCut(text.size())) : text.size();
    const bool written = std::fwrite(text.data(), 1, size, out) == text.size() && syncFile(out);
    if (std::fclose(out) != 0 || !written) throw std::runtime_error("cannot write " + file.string());
}

bool settleShared(const fs::path& file, uint64_t at, const std::string& text) {
    // How far past `at` the text is looked for: what other programs wrote between before() and the write is small.
    constexpr uint64_t SLACK = 16 * 1024 * 1024;

    std::lock_guard<std::mutex> lock(sharedMutex());
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (ec || size <= at || text.empty()) return false;

    std::string there;
    {
        std::ifstream in(file, std::ios::binary);
        if (!in) return false;
        in.seekg(static_cast<std::streamoff>(at));
        there.resize(static_cast<std::size_t>(std::min<uint64_t>(size - at, text.size() + SLACK)));
        in.read(there.data(), static_cast<std::streamsize>(there.size()));
        if (in.gcount() != static_cast<std::streamsize>(there.size())) return false;
    }

    if (there.compare(0, text.size(), text) == 0) return true;
    if (size - at < text.size() && text.compare(0, there.size(), there) == 0) {
        // The write was cut short and nothing came after it: the rest goes after what is there.
        std::FILE* out = openFile(file, "ab");
        if (!out) throw std::runtime_error("cannot write " + file.string());
        const auto rest    = std::string_view(text).substr(there.size());
        const bool written = std::fwrite(rest.data(), 1, rest.size(), out) == rest.size() && syncFile(out);
        if (std::fclose(out) != 0 || !written) throw std::runtime_error("cannot write " + file.string());
        return true;
    }
    return there.find(text) != std::string::npos;
}

std::optional<std::string> readSpan(const fs::path& file, const PgnSpan& span) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;

    in.seekg(static_cast<std::streamoff>(span.offset));
    std::string text(span.length, '\0');
    in.read(text.data(), span.length);
    if (in.gcount() != static_cast<std::streamsize>(span.length)) return std::nullopt;
    return text;
}

std::string pgnTagValue(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

}  // namespace fcg::tour
