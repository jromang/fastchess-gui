#include "state.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <stdexcept>

#include <game/pgn/pgn_builder.hpp>

#include <fmt/include/fmt/format.h>

#include "engine_text.hpp"
#include "pairing.hpp"

namespace fcg::tour {

namespace fs = std::filesystem;

namespace {

constexpr const char* SPEC_FILE    = "tournament.json";
constexpr const char* JOURNAL_FILE = "journal.jsonl";

// The events without the edits an "edit_undone" cancels, nor the end of tournament such an edit caused.
std::vector<nlohmann::json> withoutUndoneEdits(const std::vector<nlohmann::json>& events) {
    std::vector<nlohmann::json> out;
    out.reserve(events.size());
    for (const auto& e : events) {
        if (e.value("t", std::string{}) != "edit_undone") {
            out.push_back(e);
            continue;
        }
        for (auto i = out.size(); i-- > 0;) {
            if (!isEditEvent(out[i].value("t", std::string{}))) continue;
            const auto completed = [](const nlohmann::json& x) {
                return x.value("t", std::string{}) == "finished" && x.value("why", std::string{}) == "complete";
            };
            out.erase(std::remove_if(out.begin() + static_cast<std::ptrdiff_t>(i) + 1, out.end(), completed),
                      out.end());
            out.erase(out.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    }
    return out;
}

nlohmann::json toJson(const GameResult& r) {
    nlohmann::json j = {{"t", "game_finished"},
                        {"encounter", r.encounter},
                        {"game", r.game},
                        {"white", r.white},
                        {"black", r.black},
                        {"result", r.result},
                        {"termination", static_cast<int>(r.termination)},
                        {"reason", r.reason},
                        {"plies", r.plies},
                        {"ms", r.duration_ms},
                        {"pgn", {r.pgn.offset, r.pgn.length}},
                        {"cb", {r.pgn_chessbase.offset, r.pgn_chessbase.length}},
                        {"at", r.finished_at}};
    if (!r.pv_warnings.empty()) j["pv"] = r.pv_warnings;
    return j;
}

GameResult resultFromJson(const nlohmann::json& j) {
    GameResult r;
    r.encounter     = j.at("encounter").get<uint32_t>();
    r.game          = j.at("game").get<int>();
    r.white         = j.at("white").get<int>();
    r.black         = j.at("black").get<int>();
    r.result        = j.at("result").get<std::string>();
    r.termination   = static_cast<fastchess::MatchTermination>(j.value("termination", 0));
    r.reason        = j.value("reason", std::string{});
    r.plies         = j.value("plies", 0u);
    r.duration_ms   = j.value("ms", 0u);
    r.pgn           = {j.at("pgn").at(0).get<uint64_t>(), j.at("pgn").at(1).get<uint32_t>()};
    r.pgn_chessbase = {j.at("cb").at(0).get<uint64_t>(), j.at("cb").at(1).get<uint32_t>()};
    r.finished_at   = j.value("at", int64_t{0});
    if (j.contains("pv")) j.at("pv").get_to(r.pv_warnings);
    if (r.game != 0 && r.game != 1) throw std::runtime_error("bad game number");
    return r;
}

// fastchess gives the duration as "HH:MM:SS".
uint32_t parseDuration(const std::string& text) {
    int h = 0, m = 0, s = 0;
    if (std::sscanf(text.c_str(), "%d:%d:%d", &h, &m, &s) != 3) return 0;
    const int64_t ms = (static_cast<int64_t>(h) * 3600 + m * 60 + s) * 1000;
    return static_cast<uint32_t>(std::clamp<int64_t>(ms, 0, UINT32_MAX));
}

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void writeFileAtomically(const fs::path& file, const std::string& text) {
    const auto tmp = fs::path(file).concat(".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << text;
        if (!out) throw std::runtime_error("cannot write " + tmp.string());
    }
    std::error_code ec;
    fs::rename(tmp, file, ec);
    if (ec) throw std::runtime_error("cannot write " + file.string());
}

// The reference ratings after the Result tag, where rating lists and databases read them.
std::string withElo(std::string pgn, int white_elo, int black_elo) {
    std::string tags;
    if (white_elo > 0) tags += fmt::format("[WhiteElo \"{}\"]\n", white_elo);
    if (black_elo > 0) tags += fmt::format("[BlackElo \"{}\"]\n", black_elo);
    const auto result = pgn.find("\n[Result ");
    const auto end    = result == std::string::npos ? result : pgn.find('\n', result + 1);
    if (!tags.empty() && end != std::string::npos) pgn.insert(end + 1, tags);
    return pgn;
}

// Games the tournament will have; Swiss and knockout schedules grow round by round, so their plan counts too.
uint64_t plannedGames(const TournamentSpec& spec, const Schedule& schedule, std::size_t dropped) {
    const auto size = schedule.size();
    if (!size) return 0;
    const auto n = spec.participants.size();
    if (spec.format == Format::Swiss) {
        return std::max<uint64_t>(*size, uint64_t(std::max(1, spec.rounds)) * (n / 2)) * 2 - dropped;
    }
    if (spec.format == Format::Knockout) {
        // Every match eliminates one player; tiebreaks come on top.
        const uint64_t planned = (n > 0 ? n - 1 : 0) * uint64_t(std::max(1, spec.cycles));
        return std::max<uint64_t>(*size, planned) * 2 - dropped;
    }
    return uint64_t{*size} * 2 - dropped;
}

}  // namespace

fastchess::Stats encounterStats(const GameResult& a, const GameResult& b, int pov) {
    int w = 0, d = 0, l = 0;
    for (const double score : {a.scoreOf(pov), b.scoreOf(pov)}) {
        if (score == 1.0) {
            ++w;
        } else if (score == 0.0) {
            ++l;
        } else {
            ++d;
        }
    }

    fastchess::Stats stats(w, l, d);
    stats.penta_WW = w == 2;
    stats.penta_WD = w == 1 && d == 1;
    stats.penta_WL = w == 1 && l == 1;
    stats.penta_DD = d == 2;
    stats.penta_LD = l == 1 && d == 1;
    stats.penta_LL = l == 2;
    return stats;
}

double GameResult::scoreOf(int id) const noexcept {
    if (result == "1/2-1/2") return 0.5;
    const bool white_won = result == "1-0";
    if (id == white) return white_won ? 1.0 : 0.0;
    if (id == black) return white_won ? 0.0 : 1.0;
    return 0.0;
}

TournamentState::Replayed TournamentState::replay(const std::vector<nlohmann::json>& events, TournamentSpec& spec) {
    Replayed out;
    Schedule schedule(spec);
    std::vector<GameResult> results;
    std::set<uint64_t> done, dropped;

    for (const auto& e : withoutUndoneEdits(events)) {
        const auto type = e.value("t", std::string{});
        if (type == "game_finished") {
            const auto r = resultFromJson(e);
            if (done.insert(r.number()).second) results.push_back(r);
        } else if (isEditEvent(type)) {
            effects(e, spec, schedule, results, done, dropped);
            if (out.finished && out.finish_reason == "complete") out.finished = false;
        } else if (type == "round_paired" || type == "tiebreak_added") {
            roundEventTo(e, spec, schedule);
        } else if (type == "finished") {
            out.finished      = true;
            out.finish_reason = e.value("why", std::string{});
        }
    }

    for (const auto& r : results) {
        if (r.voided) continue;
        out.played++;
        out.results.push_back(r);
    }
    if (const auto size = schedule.size()) {
        const auto n     = spec.participants.size();
        uint64_t planned = *size;
        if (spec.format == Format::Swiss)
            planned = std::max<uint64_t>(planned, uint64_t(std::max(1, spec.rounds)) * (n / 2));
        if (spec.format == Format::Knockout) {
            planned = std::max<uint64_t>(planned, (n > 0 ? n - 1 : 0) * uint64_t(std::max(1, spec.cycles)));
        }
        out.total = planned * 2 - dropped.size();
    }
    return out;
}

bool TournamentState::isTournament(const fs::path& dir) {
    std::error_code ec;
    return fs::is_regular_file(dir / SPEC_FILE, ec);
}

std::unique_ptr<TournamentState> TournamentState::create(const fs::path& dir, TournamentSpec spec) {
    if (isTournament(dir)) throw std::runtime_error("a tournament already exists in " + dir.string());

    // Validates the participants against the format before anything is written.
    (void)Schedule(spec);

    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) throw std::runtime_error("cannot create the folder " + dir.string());

    spec.book = fingerprint(spec.conditions.opening.file);
    for (auto& p : spec.participants) p.program = fingerprint(p.engine.cmd);
    writeFileAtomically(dir / SPEC_FILE,
                        nlohmann::json(spec).dump(4, ' ', false, nlohmann::json::error_handler_t::replace) + "\n");

    auto state = open(dir);

    // A missing or unreadable book is reported now, not when the first game starts.
    std::lock_guard<std::mutex> lock(state->mutex_);
    (void)state->openings();
    return state;
}

std::unique_ptr<TournamentState> TournamentState::open(const fs::path& dir) {
    std::ifstream in(dir / SPEC_FILE, std::ios::binary);
    if (!in) throw std::runtime_error("no tournament in " + dir.string());

    TournamentSpec spec;
    try {
        spec = nlohmann::json::parse(in).get<TournamentSpec>();
    } catch (const std::exception& e) {
        throw std::runtime_error((dir / SPEC_FILE).string() + ": " + e.what());
    }

    std::unique_ptr<TournamentState> state(new TournamentState(dir, std::move(spec)));
    state->load();
    return state;
}

TournamentState::TournamentState(fs::path dir, TournamentSpec spec)
    : dir_(std::move(dir)), spec_(std::move(spec)), schedule_(spec_) {
    const auto& s = spec_.conditions.sprt;
    if (spec_.format == Format::Match && s.enabled) {
        sprt_ = fastchess::SPRT(s.alpha, s.beta, s.elo0, s.elo1, s.model, true);
    }
}

bool TournamentState::frc() const noexcept { return spec_.conditions.variant == fastchess::VariantType::FRC; }

const OpeningSource& TournamentState::openings() {
    if (!openings_) {
        openings_ = std::make_unique<OpeningSource>(spec_.conditions.opening, frc(), spec_.conditions.seed);
    }
    return *openings_;
}

bool TournamentState::openingPerRound() const noexcept {
    return spec_.opening_per_round && spec_.format != Format::Match && spec_.format != Format::Knockout;
}

fastchess::book::Opening TournamentState::openingFor(const Encounter& encounter) {
    if (!openingPerRound()) return openings().at(encounter.index);
    const auto key = std::pair{encounter.cycle, encounter.round};
    if (const auto it = round_openings_.find(key); it != round_openings_.end()) return it->second;
    const auto opening = openings().at(round_openings_.size());
    round_openings_.emplace(key, opening);
    return opening;
}

void TournamentState::load() {
    const auto events = withoutUndoneEdits(Journal::read(dir_ / JOURNAL_FILE));
    std::set<uint64_t> written_out;  // games.pgn offsets of the games written to spec_.pgn_out

    for (std::size_t i = 0; i < events.size(); ++i) {
        const auto& e = events[i];
        try {
            const auto type = e.at("t").get<std::string>();
            if (type == "encounter_started") {
                OpeningText text{e.at("fen").get<std::string>(), e.value("moves", std::vector<std::string>{})};
                const auto encounter = e.at("encounter").get<uint32_t>();
                started_[encounter]  = fromText(text, frc());
                if (const auto enc = schedule_.at(encounter); enc && openingPerRound()) {
                    round_openings_.emplace(std::pair{enc->cycle, enc->round}, started_[encounter]);
                }
                next_encounter_ = std::max(next_encounter_, encounter + 1);
            } else if (isEditEvent(type)) {
                applyEvent(e);
            } else if (type == "round_paired" || type == "tiebreak_added") {
                applyRoundEvent(e);
            } else if (type == "game_finished") {
                apply(resultFromJson(e));
            } else if (type == "finished") {
                finished_      = true;
                finish_reason_ = e.value("why", std::string{});
            } else if (type == "pgn_out") {
                for (const auto& offset : e.at("pgn")) written_out.insert(offset.get<uint64_t>());
            } else {
                throw std::runtime_error("unknown event \"" + type + "\"");
            }
        } catch (const std::exception& ex) {
            throw std::runtime_error((dir_ / JOURNAL_FILE).string() + ", event " + std::to_string(i + 1) + ": " +
                                     ex.what());
        }
    }

    rebuildOpen();
    for (std::size_t i = 0; i < results_.size(); ++i) {
        if (written_out.count(results_[i].pgn.offset)) {
            out_written_.insert(results_[i].number());
        } else {
            out_held_.push_back(i);
        }
    }

    // Text written after the last game the journal knows about belongs to no game.
    uint64_t keep = 0, keep_cb = 0;
    for (const auto& r : results_) {
        keep    = std::max(keep, r.pgn.end());
        keep_cb = std::max(keep_cb, r.pgn_chessbase.end());
    }
    pgn_           = std::make_unique<PgnFile>(pgnPath(), keep);
    pgn_chessbase_ = std::make_unique<PgnFile>(chessbasePgnPath(), keep_cb);
    journal_       = std::make_unique<Journal>(dir_ / JOURNAL_FILE);

    // A round may have ended right before a stop, before the next one was paired.
    pairIfReady();

    // Every game played but no end recorded: a stop right after the last game, or an undone edit had ended it.
    if (!finished_ && schedule_.size() && !gamesLeft()) {
        finished_      = true;
        finish_reason_ = "complete";
        journal_->append({{"t", "finished"}, {"why", finish_reason_}});
    }

    if (spec_.book.size > 0 && changedSince(spec_.conditions.opening.file, spec_.book)) {
        warnings_.push_back("the opening book changed since the tournament started: " + spec_.conditions.opening.file);
    }
    for (const auto& p : spec_.participants) {
        if (!p.active || p.program.size == 0 || !changedSince(p.engine.cmd, p.program)) continue;
        warnings_.push_back(std::filesystem::exists(p.engine.cmd)
                                ? "the program of " + p.engine.name +
                                      " changed since it joined the tournament: " + p.engine.cmd
                                : "the program of " + p.engine.name + " is missing: " + p.engine.cmd);
    }

    // Games held when the tournament was stopped, which may now be written (the tournament is over...).
    writeOut();
}

bool TournamentState::apply(const GameResult& result) {
    if (done(result.encounter, result.game)) return false;

    results_.push_back(result);
    done_.insert(result.number());
    next_encounter_ = std::max(next_encounter_, result.encounter + 1);
    return count(results_.size() - 1);
}

bool TournamentState::count(std::size_t index) {
    const auto& result = results_[index];
    const auto it      = half_.find(result.encounter);
    if (it == half_.end()) {
        half_[result.encounter] = index;
        return false;
    }

    // The encounter is complete: count its two games, from the lower id's point of view.
    const int lo = std::min(result.white, result.black);
    const int hi = std::max(result.white, result.black);
    pairs_[{lo, hi}] += encounterStats(results_[it->second], result, lo);

    half_.erase(it);
    return true;
}

void TournamentState::rebuildStats() {
    pairs_.clear();
    half_.clear();
    for (std::size_t i = 0; i < results_.size(); ++i) {
        if (!results_[i].voided) count(i);
    }
}

void TournamentState::rebuildOpen() {
    open_.clear();
    const auto size = schedule_.size();
    const auto end  = size ? std::min(next_encounter_, *size) : next_encounter_;
    for (uint32_t e = 0; e < end; ++e) {
        for (int g = 0; g < 2; ++g) {
            const auto n = uint64_t{e} * 2 + g;
            if (!done_.count(n) && !dropped_.count(n) && !running_.count(n)) open_.emplace_back(e, g);
        }
    }
}

void TournamentState::effects(const nlohmann::json& event, TournamentSpec& spec, Schedule& schedule,
                              std::vector<GameResult>& results, std::set<uint64_t>& done, std::set<uint64_t>& dropped) {
    const auto type = event.at("t").get<std::string>();
    applyEditEvent(event, spec, schedule);

    const auto voidWhere = [&](auto involves, bool replay, const std::string& why) {
        for (auto& r : results) {
            if (r.voided || !involves(r)) continue;
            r.voided      = true;
            r.void_reason = why;
            done.erase(r.number());
            if (!replay) dropped.insert(r.number());
        }
    };

    if (type == "participant_replaced") {
        // The new engine plays again every game of the old one.
        const int old = event.at("old").get<int>();
        voidWhere([&](const GameResult& r) { return r.white == old || r.black == old; }, true, "replaced");
    } else if (type == "participant_withdrawn") {
        const int id = event.at("participant").get<int>();
        // Games of started encounters it has not played will not be played.
        const auto at = std::min(event.at("at").get<uint32_t>(), schedule.size().value_or(0));
        for (uint32_t e = 0; e < at; ++e) {
            const auto enc = schedule.at(e);
            if (!enc || (enc->first != id && enc->second != id)) continue;
            for (int g = 0; g < 2; ++g) {
                if (!done.count(uint64_t{e} * 2 + g)) dropped.insert(uint64_t{e} * 2 + g);
            }
        }
        if (event.value("void_played", false)) {
            voidWhere([&](const GameResult& r) { return r.white == id || r.black == id; }, false, "withdrawn");
        }
    } else if (type == "games_voided") {
        const bool replay = event.value("replay", true);
        const auto why    = event.value("why", std::string{});
        for (const auto& g : event.at("games")) {
            const auto n = g.at(0).get<uint64_t>() * 2 + g.at(1).get<uint64_t>();
            voidWhere([&](const GameResult& r) { return r.number() == n; }, replay, why);
            if (!replay && !done.count(n)) dropped.insert(n);
        }
    }
}

void TournamentState::applyEvent(const nlohmann::json& event) {
    effects(event, spec_, schedule_, results_, done_, dropped_);
    rebuildStats();
    rebuildOpen();
    out_floor_ = 0;  // voided games are to be played again

    // Games added to a finished tournament reopen it.
    if (finished_ && finish_reason_ == "complete" && gamesLeft()) {
        finished_ = false;
        finish_reason_.clear();
    }
}

uint64_t TournamentState::totalLocked() const { return plannedGames(spec_, schedule_, dropped_.size()); }

void TournamentState::roundEventTo(const nlohmann::json& event, const TournamentSpec& spec, Schedule& schedule) {
    const auto type  = event.at("t").get<std::string>();
    const auto round = event.at("round").get<uint32_t>();
    const int cycles = spec.format == Format::Knockout ? std::max(1, spec.cycles) : 1;

    std::vector<Encounter> encounters;
    const auto add = [&](int a, int b, uint32_t cycle) {
        Encounter e;
        e.round  = round;
        e.cycle  = cycle;
        e.first  = cycle % 2 == 1 ? a : b;
        e.second = cycle % 2 == 1 ? b : a;
        encounters.push_back(e);
    };
    if (type == "round_paired") {
        for (const auto& p : event.at("pairs")) {
            for (int c = 1; c <= cycles; ++c) add(p.at(0).get<int>(), p.at(1).get<int>(), static_cast<uint32_t>(c));
        }
    } else {
        const auto& p = event.at("pair");
        add(p.at(0).get<int>(), p.at(1).get<int>(), static_cast<uint32_t>(cycles + event.at("index").get<int>()));
    }
    schedule.append(encounters);
}

void TournamentState::applyRoundEvent(const nlohmann::json& event) {
    roundEventTo(event, spec_, schedule_);
    if (event.at("t") == "round_paired") {
        round_ = event.at("round").get<uint32_t>();
        for (const auto& id : event.value("byes", std::vector<int>{})) byes_[id]++;
        if (event.contains("bracket")) brackets_[round_] = event.at("bracket").get<std::vector<std::vector<int>>>();
    }
    rebuildOpen();
}

double TournamentState::pairPoints(int a, int b, uint32_t round) const {
    double points = 0;
    for (const auto& r : results_) {
        if (r.voided) continue;
        const bool between = (r.white == a && r.black == b) || (r.white == b && r.black == a);
        if (between && schedule_.at(r.encounter) && schedule_.at(r.encounter)->round == round) points += r.scoreOf(a);
    }
    return points;
}

void TournamentState::pairIfReady() {
    if (!pairedAsItGoes(spec_.format) || finished_ || !journal_) return;

    // The round being played must be over: every game of it played (or dropped).
    const auto size = schedule_.size().value_or(0);
    for (uint32_t e = 0; e < size; ++e) {
        if (schedule_.at(e)->round != round_) continue;
        for (int g = 0; g < 2; ++g) {
            const auto n = uint64_t{e} * 2 + g;
            if (!done_.count(n) && !dropped_.count(n)) return;
        }
    }

    std::vector<int> players;
    for (const auto& p : spec_.participants) {
        if (p.active) players.push_back(p.id);
    }

    nlohmann::json event = {{"t", "round_paired"}, {"round", round_ + 1}};
    const auto pairsJson = [](const RoundPairing& p) {
        nlohmann::json pairs = nlohmann::json::array();
        for (const auto& [a, b] : p.pairs) pairs.push_back({a, b});
        return pairs;
    };

    if (spec_.format == Format::Swiss) {
        if (round_ >= static_cast<uint32_t>(spec_.rounds)) return;
        std::map<int, double> score;
        for (const auto& r : results_) {
            if (r.voided) continue;
            score[r.white] += r.scoreOf(r.white);
            score[r.black] += r.scoreOf(r.black);
        }
        std::set<int> had_bye;
        for (const auto& [id, n] : byes_) {
            score[id] += n;
            had_bye.insert(id);
        }
        std::set<std::pair<int, int>> played;
        for (uint32_t e = 0; e < size; ++e) {
            const auto enc = schedule_.at(e);
            played.insert({std::min(enc->first, enc->second), std::max(enc->first, enc->second)});
        }
        const auto pairing = swissPairing(players, score, played, had_bye);
        event["pairs"]     = pairsJson(pairing);
        event["byes"]      = pairing.byes;
    } else if (round_ == 0) {
        const auto pairing = knockoutFirstRound(players);
        event["pairs"]     = pairsJson(pairing);
        event["byes"]      = pairing.byes;
        event["bracket"]   = pairing.bracket;
    } else {
        // Who won each place of the round; tied matches get another pair of games first, all at once.
        const auto seed = [&](int id) {
            const auto& ps = spec_.participants;
            return std::find_if(ps.begin(), ps.end(), [&](const Participant& p) { return p.id == id; }) - ps.begin();
        };
        std::vector<int> winners;
        bool tiebreak_added = false;
        for (const auto& place : brackets_[round_]) {
            if (place.size() == 1) {
                winners.push_back(place[0]);
                continue;
            }
            const int a = place[0], b = place[1];
            const double sa = pairPoints(a, b, round_), sb = pairPoints(b, a, round_);
            if (sa == sb) {
                int tiebreaks = 0;
                for (uint32_t e = 0; e < size; ++e) {
                    const auto enc     = schedule_.at(e);
                    const bool between = (enc->first == a && enc->second == b) || (enc->first == b && enc->second == a);
                    if (enc->round == round_ && between &&
                        enc->cycle > static_cast<uint32_t>(std::max(1, spec_.cycles))) {
                        ++tiebreaks;
                    }
                }
                // Ten tiebreaks without a winner: the higher seed goes through.
                if (tiebreaks < 10) {
                    const nlohmann::json tiebreak = {
                        {"t", "tiebreak_added"}, {"round", round_}, {"pair", {a, b}}, {"index", tiebreaks + 1}};
                    journal_->append(tiebreak);
                    applyRoundEvent(tiebreak);
                    tiebreak_added = true;
                    continue;
                }
                winners.push_back(seed(a) <= seed(b) ? a : b);
                continue;
            }
            winners.push_back(sa > sb ? a : b);
        }
        if (tiebreak_added) return;      // the round goes on
        if (winners.size() < 2) return;  // the final is over

        std::vector<std::vector<int>> bracket;
        nlohmann::json pairs = nlohmann::json::array();
        for (std::size_t i = 0; i + 1 < winners.size(); i += 2) {
            bracket.push_back({winners[i], winners[i + 1]});
            pairs.push_back({winners[i], winners[i + 1]});
        }
        event["pairs"]   = pairs;
        event["bracket"] = bracket;
    }

    journal_->append(event);
    applyRoundEvent(event);
}

std::map<int, int> TournamentState::byes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return byes_;
}

std::map<uint32_t, std::vector<std::vector<int>>> TournamentState::brackets() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return brackets_;
}

Impact TournamentState::preview(const Edit& edit) const {
    std::lock_guard<std::mutex> lock(mutex_);

    Impact impact;
    impact.games_before = totalLocked();
    if (edit.kind != Edit::Kind::VoidGames && !schedule_.editable()) {
        impact.error = "a match cannot be edited this way";
        return impact;
    }

    int new_id = 0;
    for (const auto& p : spec_.participants) new_id = std::max(new_id, p.id + 1);

    auto spec     = spec_;
    auto schedule = schedule_;
    auto results  = results_;
    auto done     = done_;
    auto dropped  = dropped_;
    try {
        effects(editEvent(edit, next_encounter_, new_id, spec_), spec, schedule, results, done, dropped);
    } catch (const std::exception& e) {
        impact.error = e.what();
        return impact;
    }

    impact.games_after = plannedGames(spec, schedule, dropped.size());
    for (std::size_t i = 0; i < results.size(); ++i) {
        if (!results[i].voided || results_[i].voided) continue;
        (dropped.count(results[i].number()) ? impact.voided : impact.replayed)++;
    }
    return impact;
}

void TournamentState::applyEdit(const Edit& edit) {
    const auto impact = preview(edit);
    if (!impact.error.empty()) throw std::runtime_error(impact.error);

    std::lock_guard<std::mutex> lock(mutex_);
    int new_id = 0;
    for (const auto& p : spec_.participants) new_id = std::max(new_id, p.id + 1);

    auto event = editEvent(edit, next_encounter_, new_id, spec_);
    // The newcomer's program is read here rather than in editEvent(), which every preview calls.
    if (auto it = event.find("participant"); it != event.end() && it->is_object()) {
        auto p    = it->get<Participant>();
        p.program = fingerprint(p.engine.cmd);
        *it       = p;
    }
    journal_->append(event);

    UndoPoint undo{spec_, schedule_, {}, done_, dropped_, claims_};
    undo.voided.reserve(results_.size());
    for (const auto& r : results_) undo.voided.emplace_back(r.voided, r.void_reason);
    undo_ = std::move(undo);

    applyEvent(event);
    // The games of a withdrawn participant may have been the last ones to play. A Swiss or a knockout would pair
    // its next round first, which only a game's end does.
    if (!pairedAsItGoes(spec_.format)) finishIfComplete();
    writeOut();  // games of a withdrawn participant no longer hold the others
}

bool TournamentState::canUndo() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return undo_ && undo_->claims == claims_;
}

void TournamentState::undoEdit() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!undo_ || undo_->claims != claims_) {
        throw std::runtime_error("a game started since the last change, which can no longer be undone");
    }
    journal_->append({{"t", "edit_undone"}});

    auto& u   = *undo_;
    spec_     = std::move(u.spec);
    schedule_ = std::move(u.schedule);
    done_     = std::move(u.done);
    dropped_  = std::move(u.dropped);
    for (std::size_t i = 0; i < results_.size(); ++i) {
        if (i < u.voided.size()) {
            results_[i].voided      = u.voided[i].first;
            results_[i].void_reason = u.voided[i].second;
        } else {
            done_.insert(results_[i].number());  // running when the edit was made, finished since
        }
    }
    undo_.reset();

    rebuildStats();
    rebuildOpen();
    out_floor_ = 0;
    if (finished_ && finish_reason_ == "complete" && gamesLeft()) {
        finished_ = false;
        finish_reason_.clear();
    } else if (!finished_ && schedule_.size() && !gamesLeft() && running_.empty()) {
        // The edit had reopened a finished tournament.
        finished_      = true;
        finish_reason_ = "complete";
        journal_->append({{"t", "finished"}, {"why", finish_reason_}});
    }
    writeOut();
}

std::vector<ScheduleRow> TournamentState::scheduleRows() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ScheduleRow> rows;
    if (spec_.format == Format::Match) return rows;

    const auto size = schedule_.size().value_or(0);
    rows.reserve(size);
    for (uint32_t e = 0; e < size; ++e) {
        ScheduleRow row;
        row.encounter = *schedule_.at(e);
        for (int g = 0; g < 2; ++g) {
            const auto n  = uint64_t{e} * 2 + g;
            row.status[g] = done_.count(n)      ? GameStatus::Done
                            : dropped_.count(n) ? GameStatus::Dropped
                            : running_.count(n) ? GameStatus::Running
                                                : GameStatus::Pending;
        }
        rows.push_back(row);
    }
    return rows;
}

bool TournamentState::done(uint32_t encounter, int game) const {
    return done_.count(uint64_t{encounter} * 2 + game) > 0;
}

std::optional<uint64_t> TournamentState::totalGames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!schedule_.size()) return std::nullopt;
    return totalLocked();
}

GameTask TournamentState::makeTask(uint32_t encounter, int game) {
    const auto enc = schedule_.at(encounter);
    if (!enc) throw std::logic_error("encounter outside the schedule");

    auto it = started_.find(encounter);
    if (it == started_.end()) {
        const auto opening = openingFor(*enc);
        const auto text    = toText(opening, frc());
        journal_->append(
            {{"t", "encounter_started"}, {"encounter", encounter}, {"fen", text.fen}, {"moves", text.moves}});
        it = started_.emplace(encounter, opening).first;
    }

    const auto* first  = spec_.participant(enc->first);
    const auto* second = spec_.participant(enc->second);
    if (!first || !second) throw std::logic_error("unknown participant in the schedule");

    GameTask task;
    task.encounter = *enc;
    task.game      = game;
    task.opening   = it->second;
    task.white     = game == 0 ? first->engine : second->engine;
    task.black     = game == 0 ? second->engine : first->engine;
    // As the engines read them; the spec keeps the UTF-8.
    task.white.options = forEngine(std::move(task.white.options));
    task.black.options = forEngine(std::move(task.black.options));
    return task;
}

std::optional<GameTask> TournamentState::claimNext() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (finished_) return std::nullopt;

    while (!open_.empty()) {
        const auto [encounter, game] = open_.front();
        open_.pop_front();
        const auto n = uint64_t{encounter} * 2 + game;
        if (done(encounter, game) || dropped_.count(n) || running_.count(n)) continue;
        auto task = makeTask(encounter, game);
        running_.insert(n);
        ++claims_;
        return task;
    }

    if (!schedule_.at(next_encounter_)) return std::nullopt;

    const auto encounter = next_encounter_++;
    open_.emplace_front(encounter, 1);  // its second game comes next
    auto task = makeTask(encounter, 0);
    running_.insert(task.number());
    ++claims_;
    return task;
}

void TournamentState::release(const GameTask& task) {
    std::lock_guard<std::mutex> lock(mutex_);
    running_.erase(task.number());
    if (!done(task.encounter.index, task.game) && !dropped_.count(task.number())) {
        open_.emplace_front(task.encounter.index, task.game);
    }
}

Recorded TournamentState::complete(const GameTask& task, const fastchess::MatchData& data) {
    using fastchess::pgn::CommentStyle;
    using fastchess::pgn::PgnBuilder;

    const auto& pgn_config = spec_.conditions.pgn;
    const auto round       = std::size_t{task.encounter.index} + 1;

    GameResult result;
    result.encounter   = task.encounter.index;
    result.game        = task.game;
    result.white       = task.game == 0 ? task.encounter.first : task.encounter.second;
    result.black       = task.game == 0 ? task.encounter.second : task.encounter.first;
    result.result      = PgnBuilder::getResultFromWhiteMatch(data.players.white);
    result.termination = data.termination;
    result.reason      = data.reason;
    result.plies       = static_cast<uint32_t>(data.moves.size());
    result.duration_ms = parseDuration(data.duration);
    result.finished_at = nowMs();
    for (std::size_t ply = 0; ply < data.moves.size(); ++ply) {
        for (const auto& w : data.moves[ply].pv_warnings) result.pv_warnings.emplace_back(ply, w);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto elo     = [&](int id) { return spec_.participant(id) ? spec_.participant(id)->elo : 0; };
    const auto text    = withElo(PgnBuilder(pgn_config, data, round, CommentStyle::Fastchess).get(), elo(result.white),
                                 elo(result.black));
    const auto text_cb = withElo(PgnBuilder(pgn_config, data, round, CommentStyle::ChessBase).get(), elo(result.white),
                                 elo(result.black));
    running_.erase(task.number());
    // Already played, or no longer to be played (its participant was withdrawn meanwhile). It may have been the
    // last game the tournament was waiting for.
    if (done(task.encounter.index, task.game) || dropped_.count(task.number())) {
        pairIfReady();
        finishIfComplete();
        writeOut();
        return {result, std::nullopt, false};
    }
    // One of its engines was replaced meanwhile: the game is played again, by the new engine.
    const auto now = schedule_.at(task.encounter.index);
    if (!now || now->first != task.encounter.first || now->second != task.encounter.second) {
        open_.emplace_front(task.encounter.index, task.game);
        return {result, std::nullopt, false};
    }

    result.pgn           = pgn_->append(text);
    result.pgn_chessbase = pgn_chessbase_->append(text_cb);
    journal_->append(toJson(result));

    Recorded recorded{result, std::nullopt};
    if (apply(result)) {
        recorded.pair = pairs_[{std::min(result.white, result.black), std::max(result.white, result.black)}];
    }
    out_held_.push_back(results_.size() - 1);
    checkSprt();
    pairIfReady();
    finishIfComplete();
    writeOut();

    return recorded;
}

void TournamentState::finishIfComplete() {
    if (finished_ || !schedule_.size() || hasWorkLocked() || !running_.empty()) return;
    finished_      = true;
    finish_reason_ = "complete";
    journal_->append({{"t", "finished"}, {"why", finish_reason_}});
}

void TournamentState::writeOut() {
    if (spec_.pgn_out.empty() || !journal_) return;

    // The first game that holds the others: neither done, nor dropped, nor written once already (its replay).
    uint64_t until = UINT64_MAX;
    if (!finished_) {
        while (done_.count(out_floor_) || dropped_.count(out_floor_) || out_written_.count(out_floor_)) ++out_floor_;
        until = out_floor_;
    }

    std::vector<std::size_t> ready;
    for (const auto i : out_held_) {
        if (!results_[i].voided && results_[i].number() < until) ready.push_back(i);
    }
    if (ready.empty()) return;
    std::stable_sort(ready.begin(), ready.end(),
                     [&](std::size_t a, std::size_t b) { return results_[a].number() < results_[b].number(); });

    const bool chessbase = spec_.pgn_out_chessbase;
    std::string text;
    nlohmann::json offsets = nlohmann::json::array();
    for (const auto i : ready) {
        const auto& r   = results_[i];
        const auto game = readSpan(chessbase ? chessbasePgnPath() : pgnPath(), chessbase ? r.pgn_chessbase : r.pgn);
        if (!game) {
            out_error_ = "cannot read game " + std::to_string(r.number() + 1) + " back from the tournament folder";
            return;
        }
        text += *game;
        offsets.push_back(r.pgn.offset);
    }

    // Written, then journaled: a crash in between writes these games twice, rather than not at all.
    try {
        appendShared(spec_.pgn_out, text);
    } catch (const std::exception& e) {
        out_error_ = e.what();
        return;
    }
    out_error_.clear();
    journal_->append({{"t", "pgn_out"}, {"pgn", offsets}});

    for (const auto i : ready) out_written_.insert(results_[i].number());
    const std::set<std::size_t> gone(ready.begin(), ready.end());
    out_held_.erase(std::remove_if(out_held_.begin(), out_held_.end(), [&](std::size_t i) { return gone.count(i); }),
                    out_held_.end());
}

std::string TournamentState::pgnOutError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return out_error_;
}

void TournamentState::checkSprt() {
    if (finished_ || !sprt_.isEnabled()) return;

    const int a   = spec_.participants[0].id;
    const int b   = spec_.participants[1].id;
    const auto it = pairs_.find({std::min(a, b), std::max(a, b)});
    if (it == pairs_.end()) return;
    const auto stats = a < b ? it->second : ~it->second;

    const auto verdict = sprt_.getResult(sprt_.getLLR(stats, true));
    if (verdict == fastchess::SPRT_CONTINUE) return;

    finished_      = true;
    finish_reason_ = verdict == fastchess::SPRT_H1 ? "sprt_h1" : "sprt_h0";
    journal_->append({{"t", "finished"}, {"why", finish_reason_}});
}

bool TournamentState::hasWork() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return hasWorkLocked();
}

bool TournamentState::settled(uint32_t encounter) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int g = 0; g < 2; ++g) {
        const auto n = uint64_t{encounter} * 2 + g;
        if (!done_.count(n) && !dropped_.count(n)) return false;
    }
    return true;
}

bool TournamentState::hasWorkLocked() const { return !finished_ && gamesLeft(); }

bool TournamentState::gamesLeft() const {
    for (const auto& [encounter, game] : open_) {
        const auto n = uint64_t{encounter} * 2 + game;
        if (!done(encounter, game) && !dropped_.count(n) && !running_.count(n)) return true;
    }
    return schedule_.at(next_encounter_).has_value();
}

bool TournamentState::finished() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return finished_;
}

std::string TournamentState::finishReason() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return finish_reason_;
}

std::vector<GameResult> TournamentState::results() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return results_;
}

std::map<uint32_t, fastchess::book::Opening> TournamentState::encounterOpenings() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return started_;
}

fastchess::Stats TournamentState::pairStats(int a, int b) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = pairs_.find({std::min(a, b), std::max(a, b)});
    if (it == pairs_.end()) return {};
    return a < b ? it->second : ~it->second;
}

std::vector<fastchess::Stats> TournamentState::pairHistory(int a, int b) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<fastchess::Stats> history;
    std::map<uint32_t, const GameResult*> first;
    fastchess::Stats total;

    for (const auto& r : results_) {
        const bool between = (r.white == a && r.black == b) || (r.white == b && r.black == a);
        if (!between || r.voided) continue;

        const auto it = first.find(r.encounter);
        if (it == first.end()) {
            first.emplace(r.encounter, &r);
            continue;
        }
        total += encounterStats(*it->second, r, a);
        history.push_back(total);
        first.erase(it);
    }
    return history;
}

}  // namespace fcg::tour
