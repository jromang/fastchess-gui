#include "pgn_file.hpp"

#include <fstream>
#include <stdexcept>

#include "files.hpp"

namespace fcg::tour {

namespace fs = std::filesystem;

PgnFile::PgnFile(fs::path path, std::optional<uint64_t> keep) : path_(std::move(path)) {
    std::error_code ec;
    if (path_.has_parent_path()) fs::create_directories(path_.parent_path(), ec);

    if (keep && fs::exists(path_, ec) && fs::file_size(path_, ec) > *keep) fs::resize_file(path_, *keep, ec);

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

void appendShared(const fs::path& file, const std::string& text) {
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);

    std::error_code ec;
    if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
    std::FILE* out = openFile(file, "ab");
    if (!out) throw std::runtime_error("cannot write " + file.string());
    const bool written = std::fwrite(text.data(), 1, text.size(), out) == text.size() && syncFile(out);
    if (std::fclose(out) != 0 || !written) throw std::runtime_error("cannot write " + file.string());
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

}  // namespace fcg::tour
