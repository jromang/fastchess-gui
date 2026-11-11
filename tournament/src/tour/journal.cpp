#include "journal.hpp"

#include <fstream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#    include <io.h>
#else
#    include <unistd.h>
#endif

namespace fcg::tour {

namespace fs = std::filesystem;

namespace {

void syncToDisk(std::FILE* file) {
#ifdef _WIN32
    _commit(_fileno(file));
#else
    fsync(fileno(file));
#endif
}

}  // namespace

std::vector<nlohmann::json> Journal::read(const fs::path& file, bool repair) {
    std::vector<nlohmann::json> events;

    std::ifstream in(file, std::ios::binary);
    if (!in) return events;

    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    std::size_t pos  = 0;
    std::size_t line = 0;
    while (pos < text.size()) {
        const auto end = text.find('\n', pos);
        if (end == std::string::npos) {
            // Cut short by a crash: forget it, and remove it so that the next line does not get glued to it.
            if (repair) {
                std::error_code ec;
                fs::resize_file(file, pos, ec);
            }
            break;
        }

        ++line;
        const std::string_view content(text.data() + pos, end - pos);
        pos = end + 1;
        if (content.find_first_not_of(" \t\r") == std::string_view::npos) continue;

        try {
            events.push_back(nlohmann::json::parse(content));
        } catch (const nlohmann::json::exception&) {
            throw std::runtime_error(file.string() + ", line " + std::to_string(line) + ": not a valid event");
        }
    }

    return events;
}

Journal::Journal(const fs::path& file) {
#ifdef _WIN32
    file_ = _wfopen(file.c_str(), L"ab");
#else
    file_ = std::fopen(file.c_str(), "ab");
#endif
    if (!file_) throw std::runtime_error("cannot write " + file.string());
}

Journal::~Journal() {
    if (file_) std::fclose(file_);
}

void Journal::append(const nlohmann::json& event) {
    const auto line = event.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + '\n';
    if (std::fwrite(line.data(), 1, line.size(), file_) != line.size() || std::fflush(file_) != 0) {
        throw std::runtime_error("cannot write the tournament journal");
    }
    syncToDisk(file_);
}

}  // namespace fcg::tour
