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

// The participant (id, name) a journal or tournament.json line describes, if it does.
void addName(const nlohmann::json& participant, std::map<int, std::string>& names) {
    if (!participant.is_object() || !participant.contains("id")) return;
    const auto engine = participant.value("engine", nlohmann::json::object());
    if (engine.is_object() && engine.contains("name")) {
        names[participant.at("id").get<int>()] = engine.at("name").get<std::string>();
    }
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

        // The participants' names, once: those added later are in the journal.
        if (!journal.named) {
            journal.named = true;
            try {
                std::ifstream in(dir / "tournament.json", std::ios::binary);
                const auto spec = nlohmann::json::parse(in);
                for (const auto& p : spec.value("participants", nlohmann::json::array())) addName(p, journal.names);
            } catch (const std::exception&) {
            }
        }

        // What was appended since the last scan, up to its last complete line.
        if (size > journal.read) {
            std::ifstream in(file, std::ios::binary);
            in.seekg(static_cast<std::streamoff>(journal.read));
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            std::size_t pos = 0;
            for (auto end = text.find('\n'); end != std::string::npos; pos = end + 1, end = text.find('\n', pos)) {
                const std::string_view line(text.data() + pos, end - pos);
                if (line.find("\"participant_added\"") != std::string_view::npos ||
                    line.find("\"participant_replaced\"") != std::string_view::npos) {
                    try {
                        addName(nlohmann::json::parse(line).value("participant", nlohmann::json()), journal.names);
                    } catch (const std::exception&) {
                    }
                    continue;
                }
                // Most lines are games that ended normally: told by their termination, without parsing them.
                if (line.find("\"game_finished\"") == std::string_view::npos) continue;
                const auto key = line.find("\"termination\":");
                if (key == std::string_view::npos) continue;
                const auto how =
                    static_cast<MatchTermination>(std::atoi(std::string(line.substr(key + 14, 3)).c_str()));
                if (!incident(how)) continue;
                try {
                    const auto event = nlohmann::json::parse(line);
                    Found found;
                    found.at    = event.value("at", int64_t{0});
                    found.how   = how;
                    found.plies = event.value("plies", 0u);
                    // The side that lost; a draw (a timeout against a bare king) has no loser.
                    const auto result = event.value("result", std::string());
                    if (result == "1-0") found.loser = event.value("black", -1);
                    if (result == "0-1") found.loser = event.value("white", -1);
                    journal.found.push_back(found);
                } catch (const std::exception&) {
                }
            }
            journal.read += pos;
        }

        std::erase_if(journal.found, [&](const auto& f) { return f.at < since_ms; });
        kept.emplace(file, std::move(journal));
    }
    journals_ = std::move(kept);  // journals gone, or too old, are forgotten

    Incidents out;
    std::map<std::string, EngineIncidents> by_engine;
    std::map<std::string, int64_t> last;  // the time of each engine's last incident
    for (const auto& [file, journal] : journals_) {
        for (const auto& f : journal.found) {
            const bool time  = f.how == MatchTermination::TIMEOUT;
            const bool crash = f.how == MatchTermination::DISCONNECT || f.how == MatchTermination::STALL;
            const bool move  = f.how == MatchTermination::ILLEGAL_MOVE;
            out.time_losses += time;
            out.crashes += crash;
            out.illegal_moves += move;

            const auto name = journal.names.find(f.loser);
            if (name == journal.names.end()) continue;
            auto& e  = by_engine[name->second];
            e.engine = name->second;
            e.time_losses += time;
            e.early_time_losses += time && f.plies <= EARLY_TIME_LOSS_PLIES;
            e.crashes += crash;
            e.illegal_moves += move;
            if (auto& when = last[e.engine]; e.dir.empty() || f.at >= when) {
                when  = f.at;
                e.dir = file.parent_path();
            }
        }
    }
    for (auto& [name, e] : by_engine) out.engines.push_back(std::move(e));
    std::stable_sort(out.engines.begin(), out.engines.end(),
                     [](const EngineIncidents& a, const EngineIncidents& b) { return a.total() > b.total(); });
    return out;
}

}  // namespace fcg::tour
