#include "game_db.hpp"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <map>

#include "journal.hpp"
#include "ratinglist.hpp"
#include "state.hpp"

namespace fcg::tour {

namespace fs = std::filesystem;

namespace {

// A PGN date, "2024.01.31", as unix milliseconds at midnight UTC; 0 when it is not one.
int64_t pgnDate(const std::string& date) {
    int y = 0, m = 0, d = 0;
    if (std::sscanf(date.c_str(), "%d.%d.%d", &y, &m, &d) != 3 || y < 1970 || m < 1 || m > 12 || d < 1 || d > 31) {
        return 0;
    }
    // Days since 1970-01-01, by the civil calendar.
    y -= m <= 2;
    const int era      = y / 400;
    const int yoe      = y - era * 400;
    const int doy      = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe      = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = static_cast<int64_t>(era) * 146097 + doe - 719468;
    return days * 86'400'000;
}

class Builder {
   public:
    explicit Builder(GameDb& db) : db_(db) {}

    uint32_t engine(const std::string& name) {
        const auto [it, added] = engines_.emplace(name, static_cast<uint32_t>(db_.engines.size()));
        if (added) db_.engines.push_back(name);
        return it->second;
    }

    uint16_t timeControl(const std::string& tc) {
        const auto [it, added] = tcs_.emplace(tc, static_cast<uint16_t>(db_.time_controls.size()));
        if (added) db_.time_controls.push_back(tc);
        return it->second;
    }

   private:
    GameDb& db_;
    std::map<std::string, uint32_t> engines_;
    std::map<std::string, uint16_t> tcs_;
};

void readTournament(const fs::path& dir, GameDb& db, Builder& builder) {
    TournamentSpec spec;
    try {
        std::ifstream in(dir / "tournament.json", std::ios::binary);
        if (!in) return;
        spec = nlohmann::json::parse(in).get<TournamentSpec>();
    } catch (const std::exception&) {
        return;
    }
    if (spec.participants.empty()) return;

    TournamentState::Replayed replayed;
    try {
        replayed = TournamentState::replay(Journal::read(dir / "journal.jsonl", false), spec);
    } catch (const std::exception&) {
        return;
    }
    if (replayed.results.empty()) return;

    const auto source = static_cast<uint32_t>(db.sources.size());
    db.sources.push_back({dir, spec.name.empty() ? dir.filename().string() : spec.name, false});
    const auto tc = builder.timeControl(timeControlText(spec.participants.front().engine.limit));

    for (const auto& r : replayed.results) {
        const auto* w = spec.participant(r.white);
        const auto* b = spec.participant(r.black);
        if (!w || !b || w->engine.name == b->engine.name) continue;
        DbGame g;
        g.source       = source;
        g.white        = builder.engine(w->engine.name);
        g.black        = builder.engine(b->engine.name);
        g.time_control = tc;
        g.white_score  = static_cast<float>(r.scoreOf(r.white));
        g.termination  = r.termination;
        g.plies        = r.plies;
        g.played_at    = r.finished_at;
        g.number       = r.number() + 1;
        g.encounter    = r.encounter;
        g.pgn          = r.pgn;
        db.games.push_back(g);
    }
}

// The games of a PGN file, from their headers only: fast enough for the big files of the rating lists.
void readPgn(const fs::path& file, GameDb& db, Builder& builder) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return;
    const auto source = static_cast<uint32_t>(db.sources.size());
    db.sources.push_back({file, file.filename().string(), true});

    std::map<std::string, std::string> tags;
    const auto flush = [&] {
        const auto w = tags["White"], b = tags["Black"], result = tags["Result"];
        if (!w.empty() && !b.empty() && w != b && (result == "1-0" || result == "0-1" || result == "1/2-1/2")) {
            DbGame g;
            g.source         = source;
            g.white          = builder.engine(w);
            g.black          = builder.engine(b);
            g.time_control   = builder.timeControl(tags.count("TimeControl") ? tags["TimeControl"] : "?");
            g.white_score    = result == "1-0" ? 1.0f : result == "0-1" ? 0.0f : 0.5f;
            g.played_at      = pgnDate(tags.count("Date") ? tags["Date"] : "");
            const auto plies = tags.find("PlyCount");
            if (plies != tags.end()) g.plies = static_cast<uint32_t>(std::strtoul(plies->second.c_str(), nullptr, 10));
            db.games.push_back(g);
        }
        tags.clear();
    };

    bool in_header = false;
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) {
            in_header = false;  // a header is followed by a blank line, before the moves or the next game
            continue;
        }
        if (line.front() == '[') {
            // [Tag "value"]
            const auto space = line.find(' ');
            const auto open  = line.find('"');
            const auto close = line.rfind('"');
            if (space == std::string::npos || open == std::string::npos || close <= open) continue;
            if (!in_header && !tags.empty()) flush();
            in_header                       = true;
            tags[line.substr(1, space - 1)] = line.substr(open + 1, close - open - 1);
        } else if (in_header) {
            in_header = false;  // the moves: the header is complete
        }
    }
    if (!tags.empty()) flush();
}

}  // namespace

std::optional<uint32_t> GameDb::engine(const std::string& name) const {
    for (uint32_t i = 0; i < engines.size(); ++i) {
        if (engines[i] == name) return i;
    }
    return std::nullopt;
}

GameDb readGameDb(const std::vector<fs::path>& dirs, const std::vector<fs::path>& pgns) {
    GameDb db;
    Builder builder(db);
    for (const auto& dir : dirs) readTournament(dir, db, builder);
    for (const auto& file : pgns) readPgn(file, db, builder);
    return db;
}

bool GameFilter::accepts(const GameDb& db, const DbGame& game) const {
    if (!external && db.sources[game.source].external) return false;
    if (!time_control.empty() && db.time_controls[game.time_control] != time_control) return false;
    if (from > 0 && game.played_at < from) return false;
    if (to > 0 && game.played_at > to) return false;
    if (!engines.empty() && (!engines.count(db.engines[game.white]) || !engines.count(db.engines[game.black]))) {
        return false;
    }
    return true;
}

namespace {

// A game's result for `stats`, from the point of view of `engine`.
void add(fastchess::Stats& stats, const DbGame& g, uint32_t engine) {
    const float score = g.white == engine ? g.white_score : 1 - g.white_score;
    if (score == 1) stats.wins++;
    if (score == 0) stats.losses++;
    if (score == 0.5f) stats.draws++;
}

template <typename Key>
fastchess::Stats& slot(std::vector<std::pair<Key, fastchess::Stats>>& list, const Key& key) {
    for (auto& [k, s] : list) {
        if (k == key) return s;
    }
    return list.emplace_back(key, fastchess::Stats{}).second;
}

}  // namespace

HeadToHead headToHead(const GameDb& db, const GameFilter& filter, const std::string& a, const std::string& b) {
    HeadToHead out;
    const auto ia = db.engine(a), ib = db.engine(b);
    if (!ia || !ib) return out;

    std::map<std::pair<uint32_t, uint32_t>, std::vector<float>> pairs;  // (source, encounter) -> a's scores
    for (std::size_t i = 0; i < db.games.size(); ++i) {
        const auto& g = db.games[i];
        if (!((g.white == *ia && g.black == *ib) || (g.white == *ib && g.black == *ia))) continue;
        if (!filter.accepts(db, g)) continue;
        out.games.push_back(i);
        add(out.total, g, *ia);
        add(slot(out.by_time_control, db.time_controls[g.time_control]), g, *ia);
        add(slot(out.by_source, g.source), g, *ia);
        if (g.encounter != ~0u)
            pairs[{g.source, g.encounter}].push_back(g.white == *ia ? g.white_score : 1 - g.white_score);
    }
    for (const auto& [key, scores] : pairs) {
        if (scores.size() != 2) continue;
        const float sum = scores[0] + scores[1];
        auto& s         = out.total;
        if (sum == 0) s.penta_LL++;
        if (sum == 0.5f) s.penta_LD++;
        if (sum == 1) (scores[0] == 0.5f ? s.penta_DD : s.penta_WL)++;
        if (sum == 1.5f) s.penta_WD++;
        if (sum == 2) s.penta_WW++;
    }
    return out;
}

EngineRecord engineRecord(const GameDb& db, const GameFilter& filter, const std::string& engine) {
    EngineRecord out;
    const auto id = db.engine(engine);
    if (!id) return out;

    using fastchess::MatchTermination;
    for (std::size_t i = 0; i < db.games.size(); ++i) {
        const auto& g = db.games[i];
        if ((g.white != *id && g.black != *id) || !filter.accepts(db, g)) continue;
        add(out.total, g, *id);
        add(slot(out.by_time_control, db.time_controls[g.time_control]), g, *id);

        auto it =
            std::find_if(out.sources.begin(), out.sources.end(), [&](const auto& s) { return s.source == g.source; });
        if (it == out.sources.end()) it = out.sources.insert(out.sources.end(), EngineRecord::Source{g.source});
        add(it->stats, g, *id);
        it->last_game = std::max(it->last_game, g.played_at);

        if (g.termination == MatchTermination::ADJUDICATION) out.adjudicated++;
        const bool lost = (g.white == *id ? g.white_score : 1 - g.white_score) == 0;
        if (!lost) continue;
        switch (g.termination) {
            case MatchTermination::TIMEOUT:
                out.timeouts++;
                break;
            case MatchTermination::DISCONNECT:
            case MatchTermination::STALL:
                out.crashes++;
                break;
            case MatchTermination::ILLEGAL_MOVE:
                out.illegal++;
                break;
            default:
                continue;
        }
        out.problem_games.push_back(i);
    }
    std::sort(out.sources.begin(), out.sources.end(),
              [](const auto& a, const auto& b) { return a.last_game < b.last_game; });
    return out;
}

}  // namespace fcg::tour
