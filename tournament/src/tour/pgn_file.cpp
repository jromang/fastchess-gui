#include "pgn_file.hpp"

#include <stdexcept>

namespace fcg::tour {

namespace fs = std::filesystem;

PgnFile::PgnFile(fs::path path, std::optional<uint64_t> keep) : path_(std::move(path)) {
    std::error_code ec;
    if (path_.has_parent_path()) fs::create_directories(path_.parent_path(), ec);

    if (keep && fs::exists(path_, ec) && fs::file_size(path_, ec) > *keep) fs::resize_file(path_, *keep, ec);

    out_.open(path_, std::ios::binary | std::ios::app);
    size_ = fs::exists(path_, ec) ? fs::file_size(path_, ec) : 0;
}

PgnSpan PgnFile::append(const std::string& pgn) {
    std::lock_guard<std::mutex> lock(mutex_);

    PgnSpan span{size_, static_cast<uint32_t>(pgn.size())};
    out_.write(pgn.data(), static_cast<std::streamsize>(pgn.size()));
    out_.flush();
    size_ += pgn.size();
    return span;
}

void appendShared(const fs::path& file, const std::string& text) {
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);

    std::error_code ec;
    if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::app);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.close();
    if (!out) throw std::runtime_error("cannot write " + file.string());
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
