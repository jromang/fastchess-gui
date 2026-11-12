#include "submission.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <set>

#include <fmt/include/fmt/format.h>

#include "pgn_file.hpp"

namespace fcg::tour {

namespace {

// The plies of a game's movetext: its moves, without move numbers, comments, variations, NAGs and the result.
std::size_t movetextPlies(std::string_view game) {
    const auto start = game.find("\n\n");
    if (start == std::string_view::npos) return 0;
    std::size_t plies = 0;
    int depth         = 0;  // inside ( ) variations
    for (std::size_t i = start; i < game.size();) {
        const char c = game[i];
        if (c == '{') {
            const auto end = game.find('}', i);
            i              = end == std::string_view::npos ? game.size() : end + 1;
        } else if (c == ';') {
            const auto end = game.find('\n', i);
            i              = end == std::string_view::npos ? game.size() : end + 1;
        } else if (c == '(' || c == ')') {
            depth += c == '(' ? 1 : -1;
            ++i;
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
        } else {
            auto end = i;
            while (end < game.size() && !std::isspace(static_cast<unsigned char>(game[end])) &&
                   std::string_view("{};()").find(game[end]) == std::string_view::npos) {
                ++end;
            }
            auto token = game.substr(i, end - i);
            i          = end;
            // "12." or "12..." before a move, possibly glued to it.
            const auto digits = token.find_first_not_of("0123456789");
            if (digits != std::string_view::npos && digits > 0 && token[digits] == '.') {
                token.remove_prefix(token.find_first_not_of('.', digits) == std::string_view::npos
                                        ? token.size()
                                        : token.find_first_not_of('.', digits));
            }
            if (token.empty() || token[0] == '$' || depth > 0) continue;
            if (token == "1-0" || token == "0-1" || token == "1/2-1/2" || token == "*") continue;
            ++plies;
        }
    }
    return plies;
}

bool endsWithNoCase(std::string_view s, std::string_view end) {
    if (s.size() < end.size()) return false;
    for (std::size_t i = 0; i < end.size(); ++i) {
        const auto a = std::tolower(static_cast<unsigned char>(s[s.size() - end.size() + i]));
        if (a != std::tolower(static_cast<unsigned char>(end[i]))) return false;
    }
    return true;
}

std::string trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

// The line of a tag in the tags of a game: [begin, end) without its line ending; npos when it has none.
std::pair<std::size_t, std::size_t> tagLine(std::string_view game, std::string_view tag) {
    const auto prefix = "[" + std::string(tag) + " \"";
    for (std::size_t at = 0; at < game.size();) {
        auto end = game.find('\n', at);
        if (end == std::string_view::npos) end = game.size();
        auto line = game.substr(at, end - at);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() && at > 0) break;  // the end of the tags
        if (!line.empty() && line.front() != '[') break;
        if (line.rfind(prefix, 0) == 0) return {at, at + line.size()};
        at = end + 1;
    }
    return {std::string_view::npos, std::string_view::npos};
}

void setTag(std::string& game, std::string_view tag, const std::string& value) {
    const auto [begin, end] = tagLine(game, tag);
    if (begin == std::string_view::npos) return;
    game.replace(begin, end - begin, fmt::format("[{} \"{}\"]", tag, pgnTagValue(value)));
}

}  // namespace

std::string listName(const std::string& name, int threads) {
    std::string n = trim(name);
    // A "4CPU" at the end is the threads of another export: the threads given here replace it.
    if (const auto space = n.find_last_of(' '); space != std::string::npos && endsWithNoCase(n, "CPU")) {
        const auto count = std::string_view(n).substr(space + 1, n.size() - space - 4);
        if (!count.empty() && std::all_of(count.begin(), count.end(), [](char c) { return std::isdigit(c); })) {
            n = trim(n.substr(0, space));
        }
    }
    if (!endsWithNoCase(n, "64-bit")) n += " 64-bit";
    if (threads > 1) n += fmt::format(" {}CPU", threads);
    return n;
}

int optionValue(const fastchess::EngineConfiguration& engine, std::string_view option, int fallback) {
    for (const auto& [name, value] : engine.options) {
        if (name != option) continue;
        try {
            return std::stoi(value);
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

std::string submissionFileStem(const SubmissionExport& e) {
    return fmt::format("[{} {}] {} (hash {}MB) (book {}) (egtb {}-man)", e.tester, e.date, e.event, e.hash_mb, e.book,
                       e.egtb);
}

std::string submissionZipName(const std::string& stem) {
    std::string out;
    for (const char c : stem) {
        const bool blank = std::isspace(static_cast<unsigned char>(c)) || c == '[' || c == ']' || c == '(' || c == ')';
        if (!blank) {
            out += c;
        } else if (!out.empty() && out.back() != '_') {
            out += '_';
        }
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out + ".zip";
}

std::filesystem::path submissionZipPath(const std::filesystem::path& pgn) {
    const auto name  = submissionZipName(pgn.stem().string());
    const auto lower = [](std::string s) {
        for (auto& c : s) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        return s;
    };
    if (lower(name) == lower(pgn.filename().string())) return {};
    return pgn.parent_path() / name;
}

std::string submissionEvent(const std::string& seed, int64_t first_game_ms) {
    static constexpr const char* MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                             "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const auto t                          = static_cast<std::time_t>(first_game_ms / 1000);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return fmt::format("{} - {} {}", seed, MONTHS[std::clamp(tm.tm_mon, 0, 11)], tm.tm_mday);
}

std::string pgnTag(std::string_view game, std::string_view tag) {
    const auto [begin, end] = tagLine(game, tag);
    if (begin == std::string_view::npos) return {};
    const auto line = game.substr(begin, end - begin);
    const auto open = line.find('"');
    const auto shut = line.rfind('"');
    if (open == std::string_view::npos || shut <= open) return {};
    std::string out;
    for (std::size_t i = open + 1; i < shut; ++i) {
        if (line[i] == '\\' && i + 1 < shut) ++i;
        out += line[i];
    }
    return out;
}

SubmissionGames submissionGames(const std::filesystem::path& pgn, const std::vector<GameResult>& results,
                                const TournamentSpec& spec, const SubmissionExport& e) {
    SubmissionGames out;

    // The games that count, each game of the schedule once (the first recorded), in the order they ended.
    std::vector<const GameResult*> kept;
    std::set<uint64_t> seen;
    for (const auto& r : results) {
        if (r.voided) {
            out.voided++;
        } else if (!seen.insert(r.number()).second) {
            out.duplicates++;
        } else if (e.leave_out_crashes && (r.termination == fastchess::MatchTermination::DISCONNECT ||
                                           r.termination == fastchess::MatchTermination::STALL)) {
            out.crashes++;
        } else {
            kept.push_back(&r);
        }
    }
    std::stable_sort(kept.begin(), kept.end(),
                     [](const GameResult* a, const GameResult* b) { return a->finished_at < b->finished_at; });

    const auto name = [&](int id) {
        if (const auto it = e.names.find(id); it != e.names.end() && !trim(it->second).empty()) {
            return trim(it->second);
        }
        const auto* p = spec.participant(id);
        return p ? p->engine.name : std::string();
    };

    std::ifstream in(pgn, std::ios::binary);
    std::vector<std::string> games;
    for (const auto* r : kept) {
        std::string text(r->pgn.length, '\0');
        in.clear();
        in.seekg(static_cast<std::streamoff>(r->pgn.offset));
        in.read(text.data(), r->pgn.length);
        if (!in || in.gcount() != static_cast<std::streamsize>(r->pgn.length) || text.find('[') != 0) {
            out.unreadable++;
            continue;
        }
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();

        setTag(text, "Event", e.event);
        if (!trim(e.site).empty()) setTag(text, "Site", trim(e.site));
        setTag(text, "Round", fmt::format("{}", games.size() + 1));
        setTag(text, "White", name(r->white));
        setTag(text, "Black", name(r->black));
        // fastchess-gui 0.4.13 and earlier counted the illegal move that ends a game, which is not in the movetext: the
        // count of the moves written, whichever version wrote the game.
        if (r->termination == fastchess::MatchTermination::ILLEGAL_MOVE && !pgnTag(text, "PlyCount").empty()) {
            setTag(text, "PlyCount", fmt::format("{}", movetextPlies(text)));
        }
        games.push_back(std::move(text));
    }

    for (const auto& g : games) out.pgn += g + "\n\n";
    out.games = games.size();
    return out;
}

SubmissionFacts submissionFacts(const TournamentSpec& spec, const std::vector<GameResult>& results,
                                std::optional<uint64_t> planned) {
    SubmissionFacts f;
    f.planned = planned;

    std::set<uint64_t> seen;
    std::map<uint32_t, std::pair<const GameResult*, int>> encounters;  // first game, games that count
    for (const auto& r : results) {
        if (r.voided) {
            f.voided++;
            continue;
        }
        if (!seen.insert(r.number()).second) {
            f.duplicates++;
            continue;
        }
        f.played++;
        auto& e = encounters[r.encounter];
        if (!e.first) e.first = &r;
        e.second++;
        switch (r.termination) {
            case fastchess::MatchTermination::DISCONNECT:
            case fastchess::MatchTermination::STALL:
                f.crashes.push_back(r.number());
                break;
            case fastchess::MatchTermination::TIMEOUT:
                f.timeouts.push_back(r.number());
                break;
            case fastchess::MatchTermination::ILLEGAL_MOVE:
                f.illegal.push_back(r.number());
                break;
            default:
                break;
        }
    }
    for (const auto& [number, e] : encounters) {
        if (e.second == 1) f.unpaired.emplace_back(e.first->white, e.first->black);
    }

    for (const auto& p : spec.participants) {
        if (!p.active) continue;
        SubmissionFacts::Engine e;
        e.id      = p.id;
        e.name    = p.engine.name;
        e.hash    = optionValue(p.engine, "Hash", 0);
        e.threads = std::max(1, optionValue(p.engine, "Threads", 1));
        for (const auto& [name, value] : p.engine.options) {
            if (name == "Ponder") e.ponder = endsWithNoCase(value, "true") && value.size() == 4;
            if (name == "SyzygyPath" && value != "<empty>") e.syzygy = value;
        }
        f.engines.push_back(e);
    }
    if (!spec.conditions.opening.file.empty()) {
        f.book = std::filesystem::path(spec.conditions.opening.file).filename().string();
    }
    if (spec.conditions.tb_adjudication.enabled) f.adjudication_tb = spec.conditions.tb_adjudication.syzygy_dirs;
    return f;
}

}  // namespace fcg::tour
