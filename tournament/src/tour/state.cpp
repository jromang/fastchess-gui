#include "state.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <stdexcept>

#include <game/pgn/pgn_builder.hpp>

namespace fcg::tour {

namespace fs = std::filesystem;

namespace {

constexpr const char* SPEC_FILE    = "tournament.json";
constexpr const char* JOURNAL_FILE = "journal.jsonl";

nlohmann::json toJson(const GameResult& r) {
    return {{"t", "game_finished"},
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
    writeFileAtomically(dir / SPEC_FILE, nlohmann::json(spec).dump(4) + "\n");

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

void TournamentState::load() {
    const auto events = Journal::read(dir_ / JOURNAL_FILE);

    for (std::size_t i = 0; i < events.size(); ++i) {
        const auto& e = events[i];
        try {
            const auto type = e.at("t").get<std::string>();
            if (type == "encounter_started") {
                OpeningText text{e.at("fen").get<std::string>(), e.value("moves", std::vector<std::string>{})};
                started_[e.at("encounter").get<uint32_t>()] = fromText(text, frc());
            } else if (type == "game_finished") {
                apply(resultFromJson(e));
            } else if (type == "finished") {
                finished_      = true;
                finish_reason_ = e.value("why", std::string{});
            } else {
                throw std::runtime_error("unknown event \"" + type + "\"");
            }
        } catch (const std::exception& ex) {
            throw std::runtime_error((dir_ / JOURNAL_FILE).string() + ", event " + std::to_string(i + 1) + ": " +
                                     ex.what());
        }
    }

    for (const auto& [encounter, opening] : started_) next_encounter_ = std::max(next_encounter_, encounter + 1);
    for (uint32_t e = 0; e < next_encounter_; ++e) {
        for (int g = 0; g < 2; ++g) {
            if (!done(e, g)) open_.emplace_back(e, g);
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

    if (spec_.book.size > 0 && fingerprint(spec_.conditions.opening.file) != spec_.book) {
        warnings_.push_back("the opening book changed since the tournament started: " + spec_.conditions.opening.file);
    }
}

bool TournamentState::apply(const GameResult& result) {
    if (done(result.encounter, result.game)) return false;

    results_.push_back(result);
    done_.insert(result.number());
    next_encounter_ = std::max(next_encounter_, result.encounter + 1);

    const auto it = half_.find(result.encounter);
    if (it == half_.end()) {
        half_[result.encounter] = results_.size() - 1;
        return false;
    }

    // The encounter is complete: count its two games, from the lower id's point of view.
    const int lo = std::min(result.white, result.black);
    const int hi = std::max(result.white, result.black);
    pairs_[{lo, hi}] += encounterStats(results_[it->second], result, lo);

    half_.erase(it);
    return true;
}

bool TournamentState::done(uint32_t encounter, int game) const {
    return done_.count(uint64_t{encounter} * 2 + game) > 0;
}

std::optional<uint64_t> TournamentState::totalGames() const {
    const auto size = schedule_.size();
    if (!size) return std::nullopt;
    return uint64_t{*size} * 2;
}

GameTask TournamentState::makeTask(uint32_t encounter, int game) {
    const auto enc = schedule_.at(encounter);
    if (!enc) throw std::logic_error("encounter outside the schedule");

    auto it = started_.find(encounter);
    if (it == started_.end()) {
        const auto opening = openings().at(encounter);
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
    return task;
}

std::optional<GameTask> TournamentState::claimNext() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (finished_) return std::nullopt;

    while (!open_.empty()) {
        const auto [encounter, game] = open_.front();
        open_.pop_front();
        if (!done(encounter, game)) return makeTask(encounter, game);
    }

    if (!schedule_.at(next_encounter_)) return std::nullopt;

    const auto encounter = next_encounter_++;
    open_.emplace_front(encounter, 1);  // its second game comes next
    return makeTask(encounter, 0);
}

void TournamentState::release(const GameTask& task) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!done(task.encounter.index, task.game)) open_.emplace_front(task.encounter.index, task.game);
}

Recorded TournamentState::complete(const GameTask& task, const fastchess::MatchData& data) {
    using fastchess::pgn::CommentStyle;
    using fastchess::pgn::PgnBuilder;

    const auto& pgn_config = spec_.conditions.pgn;
    const auto round       = std::size_t{task.encounter.index} + 1;
    const auto text        = PgnBuilder(pgn_config, data, round, CommentStyle::Fastchess).get();
    const auto text_cb     = PgnBuilder(pgn_config, data, round, CommentStyle::ChessBase).get();

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

    std::lock_guard<std::mutex> lock(mutex_);
    if (done(task.encounter.index, task.game)) return {result, std::nullopt};

    result.pgn           = pgn_->append(text);
    result.pgn_chessbase = pgn_chessbase_->append(text_cb);
    journal_->append(toJson(result));

    Recorded recorded{result, std::nullopt};
    if (apply(result)) {
        recorded.pair = pairs_[{std::min(result.white, result.black), std::max(result.white, result.black)}];
    }
    checkSprt();

    if (!finished_ && schedule_.size() && done_.size() >= uint64_t{*schedule_.size()} * 2) {
        finished_      = true;
        finish_reason_ = "complete";
        journal_->append({{"t", "finished"}, {"why", finish_reason_}});
    }

    return recorded;
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
    if (finished_) return false;
    for (const auto& [encounter, game] : open_) {
        if (!done(encounter, game)) return true;
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
        if (!between) continue;

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
