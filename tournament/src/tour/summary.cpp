#include "summary.hpp"

#include <algorithm>
#include <fstream>

#include "journal.hpp"
#include "schedule.hpp"
#include "state.hpp"

namespace fcg::tour {

namespace fs = std::filesystem;

std::optional<Summary> summarize(const fs::path& dir) {
    try {
        std::ifstream in(dir / "tournament.json", std::ios::binary);
        if (!in) return std::nullopt;

        Summary s;
        s.dir  = dir;
        s.spec = nlohmann::json::parse(in).get<TournamentSpec>();

        const auto replayed = TournamentState::replay(Journal::read(dir / "journal.jsonl", false), s.spec);
        s.played            = replayed.played;
        s.total             = replayed.total;
        s.finished          = replayed.finished;
        s.finish_reason     = replayed.finish_reason;
        return s;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::vector<fs::path> tournamentDirs(const fs::path& root) {
    std::vector<fs::path> dirs;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (entry.is_directory(ec) && fs::is_regular_file(entry.path() / "tournament.json", ec)) {
            dirs.push_back(entry.path());
        }
    }
    std::sort(dirs.begin(), dirs.end(), [](const auto& a, const auto& b) { return a.filename() > b.filename(); });
    return dirs;
}

std::vector<Summary> listTournaments(const fs::path& root) {
    std::vector<Summary> list;
    for (const auto& dir : tournamentDirs(root)) {
        if (auto s = summarize(dir)) list.push_back(std::move(*s));
    }
    return list;
}

}  // namespace fcg::tour
