#include "summary.hpp"

#include <algorithm>
#include <fstream>

#include "journal.hpp"
#include "schedule.hpp"
#include "standings.hpp"
#include "state.hpp"

namespace fcg::tour {

namespace fs = std::filesystem;

namespace {

std::vector<Summary::Score> scoresOf(const TournamentSpec& spec, const TournamentState::Replayed& replayed) {
    std::vector<Summary::Score> out;
    if (spec.format == Format::Match) {
        for (const auto& p : spec.participants) {
            Summary::Score score{p.engine.name, 0, 0};
            for (const auto& r : replayed.results) {
                if (r.white != p.id && r.black != p.id) continue;
                score.points += r.scoreOf(p.id);
                score.games++;
            }
            out.push_back(std::move(score));
        }
        return out;
    }

    const auto table = computeTable(spec, replayed.results, std::nullopt, replayed.byes);
    for (const auto& row : table.rows) out.push_back({row.name, row.points, row.games});
    // A knockout is won in its final, not on points: its winner comes first.
    const auto& last = replayed.last_bracket;
    if (spec.format == Format::Knockout && replayed.finished && last.size() == 1 && last[0].size() == 2) {
        const auto pair  = table.pair(last[0][0], last[0][1]);
        const int winner = pair.points() * 2 > pair.games() ? last[0][0] : last[0][1];
        const auto* p    = spec.participant(winner);
        const auto it    = std::find_if(out.begin(), out.end(),
                                        [&](const Summary::Score& s) { return p && s.name == p->engine.name; });
        if (it != out.end()) std::rotate(out.begin(), it, it + 1);
    }
    return out;
}

}  // namespace

std::optional<Summary> summarize(const fs::path& dir) {
    try {
        std::ifstream in(dir / "tournament.json", std::ios::binary);
        if (!in) return std::nullopt;

        Summary s;
        s.dir  = dir;
        s.spec = nlohmann::json::parse(in).get<TournamentSpec>();

        // A damaged line does not hide the tournament from the list; opening it reports the line.
        std::vector<std::string> damaged;
        const auto replayed = TournamentState::replay(Journal::read(dir / "journal.jsonl", false, &damaged), s.spec);
        s.played            = replayed.played;
        s.total             = replayed.total;
        s.finished          = replayed.finished;
        s.finish_reason     = replayed.finish_reason;
        s.scores            = scoresOf(s.spec, replayed);
        return s;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::vector<fs::path> tournamentDirs(const fs::path& root) {
    std::vector<fs::path> dirs;
    std::error_code ec;
    // increment(ec): a folder that cannot be read further (a network drive gone...) ends the list, it does not throw.
    for (fs::directory_iterator it(root, ec), last; !ec && it != last; it.increment(ec)) {
        if (it->is_directory(ec) && fs::is_regular_file(it->path() / "tournament.json", ec)) dirs.push_back(it->path());
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

std::vector<Summary> SummaryCache::list(const fs::path& root) {
    std::map<fs::path, Entry> kept;
    std::vector<Summary> list;
    for (const auto& dir : tournamentDirs(root)) {
        Entry now;
        std::error_code ec;
        now.journal_size = fs::file_size(dir / "journal.jsonl", ec);
        if (ec) now.journal_size = 0;
        now.journal_time = fs::last_write_time(dir / "journal.jsonl", ec);
        now.spec_time    = fs::last_write_time(dir / "tournament.json", ec);

        const auto it = entries_.find(dir);
        if (it != entries_.end() && it->second.journal_size == now.journal_size &&
            it->second.journal_time == now.journal_time && it->second.spec_time == now.spec_time) {
            now.summary = std::move(it->second.summary);
        } else {
            now.summary = summarize(dir);
        }
        if (now.summary) list.push_back(*now.summary);
        kept.emplace(dir, std::move(now));
    }
    entries_ = std::move(kept);  // folders gone are forgotten
    return list;
}

}  // namespace fcg::tour
