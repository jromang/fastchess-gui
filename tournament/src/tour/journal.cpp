#include "journal.hpp"

#include <fstream>
#include <stdexcept>
#include <string>

#include "files.hpp"

namespace fcg::tour {

namespace fs = std::filesystem;

std::vector<nlohmann::json> Journal::read(const fs::path& file, bool repair, std::vector<std::string>* damaged) {
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
            const auto what = file.string() + ", line " + std::to_string(line) + ": not a valid event";
            if (!damaged) throw std::runtime_error(what);
            damaged->push_back(what);
        }
    }

    return events;
}

Journal::Journal(const fs::path& file) {
    // A partial last line that could not be cut (read() without repair, or the cut failed) becomes a line of its
    // own, which read() reports, instead of spoiling the next one.
    bool cut_short = false;
    {
        std::ifstream in(file, std::ios::binary | std::ios::ate);
        if (in && in.tellg() > 0) {
            in.seekg(-1, std::ios::end);
            cut_short = in.get() != '\n';
        }
    }

    file_ = openFile(file, "ab");
    if (!file_) throw std::runtime_error("cannot write " + file.string());
    if (cut_short && (std::fputc('\n', file_) == EOF || !syncFile(file_))) {
        std::fclose(file_);
        file_ = nullptr;
        throw std::runtime_error("cannot write " + file.string());
    }
}

Journal::~Journal() {
    if (file_) std::fclose(file_);
}

void Journal::append(const nlohmann::json& event) {
    const auto line = event.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + '\n';
    std::lock_guard<std::mutex> lock(mutex_);
    if (failed_) throw std::runtime_error("cannot write the tournament journal");
    if (std::fwrite(line.data(), 1, line.size(), file_) != line.size() || !syncFile(file_)) {
        failed_ = true;
        throw std::runtime_error("cannot write the tournament journal (is the disk full?)");
    }
}

}  // namespace fcg::tour
