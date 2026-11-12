#include "incidents.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>

#include <json.hpp>

#include "summary.hpp"

namespace fcg::tour {

namespace fs = std::filesystem;
using fastchess::MatchTermination;

namespace {

bool incident(MatchTermination termination) {
    return termination == MatchTermination::TIMEOUT || termination == MatchTermination::DISCONNECT ||
           termination == MatchTermination::STALL || termination == MatchTermination::ILLEGAL_MOVE;
}

}  // namespace

Incidents IncidentScanner::scan(const fs::path& root, int64_t since_ms, std::chrono::seconds window) {
    std::map<fs::path, Journal> kept;
    const auto now = fs::file_time_type::clock::now();
    for (const auto& dir : tournamentDirs(root)) {
        const auto file = dir / "journal.jsonl";
        std::error_code ec;
        const auto written = fs::last_write_time(file, ec);
        if (ec || now - written > window) continue;
        const auto size = fs::file_size(file, ec);
        if (ec) continue;

        Journal journal;
        if (const auto it = journals_.find(file); it != journals_.end()) journal = std::move(it->second);
        if (size < journal.read) journal = {};  // not the file read before

        // What was appended since the last scan, up to its last complete line.
        if (size > journal.read) {
            std::ifstream in(file, std::ios::binary);
            in.seekg(static_cast<std::streamoff>(journal.read));
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            std::size_t pos = 0;
            for (auto end = text.find('\n'); end != std::string::npos; pos = end + 1, end = text.find('\n', pos)) {
                const std::string_view line(text.data() + pos, end - pos);
                // Most lines are games that ended normally: told by their termination, without parsing them.
                if (line.find("\"game_finished\"") == std::string_view::npos) continue;
                const auto key = line.find("\"termination\":");
                if (key == std::string_view::npos) continue;
                const auto how =
                    static_cast<MatchTermination>(std::atoi(std::string(line.substr(key + 14, 3)).c_str()));
                if (!incident(how)) continue;
                try {
                    const auto event = nlohmann::json::parse(line);
                    journal.found.emplace_back(event.value("at", int64_t{0}), how);
                } catch (const std::exception&) {
                }
            }
            journal.read += pos;
        }

        std::erase_if(journal.found, [&](const auto& f) { return f.first < since_ms; });
        kept.emplace(file, std::move(journal));
    }
    journals_ = std::move(kept);  // journals gone, or too old, are forgotten

    Incidents out;
    for (const auto& [file, journal] : journals_) {
        for (const auto& [at, how] : journal.found) {
            if (how == MatchTermination::TIMEOUT) ++out.time_losses;
            if (how == MatchTermination::DISCONNECT || how == MatchTermination::STALL) ++out.crashes;
            if (how == MatchTermination::ILLEGAL_MOVE) ++out.illegal_moves;
        }
    }
    return out;
}

}  // namespace fcg::tour
