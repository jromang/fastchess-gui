#include "control.hpp"

#include <chrono>

namespace fcg::control {

namespace {

template <typename T>
T get(const nlohmann::json& j, const char* key, T fallback) {
    const auto it = j.find(key);
    if (it == j.end() || it->is_null()) return fallback;
    try {
        return it->get<T>();
    } catch (const nlohmann::json::exception&) {
        return fallback;
    }
}

const char* phaseName(Status::Phase phase) {
    switch (phase) {
        case Status::Phase::Starting:
            return "starting";
        case Status::Phase::Running:
            return "running";
        case Status::Phase::Finished:
            return "finished";
    }
    return "starting";
}

Status::Phase phaseFrom(const std::string& name) {
    if (name == "running") return Status::Phase::Running;
    if (name == "finished") return Status::Phase::Finished;
    return Status::Phase::Starting;
}

bool sameEngine(const tour::StartFailures::Engine& a, const tour::StartFailures::Engine& b) {
    return a.participant == b.participant && a.name == b.name && a.error == b.error && a.tries == b.tries &&
           a.failed == b.failed && a.trying == b.trying && a.log == b.log && a.last_chance == b.last_chance;
}

}  // namespace

nlohmann::json helloJson(const Hello& h) {
    nlohmann::json j = {{"protocol", h.protocol}, {"version", h.version}};
    if (!h.dir.empty()) j["dir"] = h.dir;
    if (h.pid) j["pid"] = h.pid;
    if (h.playing_ms) j["playing_ms"] = h.playing_ms;
    if (h.journal) j["journal"] = h.journal;
    return {{"hello", j}};
}

std::optional<Hello> parseHello(const nlohmann::json& line) {
    const auto it = line.find("hello");
    if (it == line.end() || !it->is_object()) return std::nullopt;
    Hello h;
    h.protocol   = get(*it, "protocol", 0);
    h.version    = get(*it, "version", std::string{});
    h.dir        = get(*it, "dir", std::string{});
    h.pid        = get(*it, "pid", int64_t{0});
    h.playing_ms = get(*it, "playing_ms", int64_t{0});
    h.journal    = get(*it, "journal", uint64_t{0});
    return h;
}

bool Status::operator==(const Status& o) const {
    if (start_failures.size() != o.start_failures.size()) return false;
    for (std::size_t i = 0; i < start_failures.size(); ++i) {
        if (!sameEngine(start_failures[i], o.start_failures[i])) return false;
    }
    return phase == o.phase && paused == o.paused && stop_idle == o.stop_idle && stopping == o.stopping &&
           running_games == o.running_games && open_pairs == o.open_pairs && running == o.running &&
           can_undo == o.can_undo && undo_lasts == o.undo_lasts && pgn_out_error == o.pgn_out_error &&
           stalled == o.stalled && in_flight == o.in_flight && allowance == o.allowance && waiting == o.waiting &&
           machines == o.machines;
}

nlohmann::json statusJson(const Status& s) {
    nlohmann::json failures = nlohmann::json::array();
    for (const auto& e : s.start_failures) failures.push_back(startFailureJson(e));
    nlohmann::json j = {{"phase", phaseName(s.phase)},
                        {"paused", s.paused},
                        {"stop_idle", s.stop_idle},
                        {"stopping", s.stopping},
                        {"running_games", s.running_games},
                        {"open_pairs", s.open_pairs},
                        {"running", s.running},
                        {"can_undo", s.can_undo},
                        {"undo_lasts", s.undo_lasts},
                        {"in_flight", costJson(s.in_flight)},
                        {"allowance", allowanceJson(s.allowance)},
                        {"waiting", waitName(s.waiting)}};
    if (!s.pgn_out_error.empty()) j["pgn_out_error"] = s.pgn_out_error;
    if (!failures.empty()) j["start_failures"] = failures;
    if (s.stalled) j["stalled"] = true;
    if (!s.machines.empty()) {
        auto machines = nlohmann::json::array();
        for (const auto& m : s.machines) {
            machines.push_back({{"id", m.id},
                                {"name", m.name},
                                {"connections", m.connections},
                                {"engines", m.engines},
                                {"error", m.error},
                                {"since", m.since}});
        }
        j["machines"] = std::move(machines);
    }
    return j;
}

Status statusFromJson(const nlohmann::json& j) {
    Status s;
    s.phase         = phaseFrom(get(j, "phase", std::string{}));
    s.paused        = get(j, "paused", false);
    s.stop_idle     = get(j, "stop_idle", false);
    s.stopping      = get(j, "stopping", false);
    s.running_games = get(j, "running_games", 0);
    s.open_pairs    = get(j, "open_pairs", 0);
    s.running       = get(j, "running", std::vector<uint64_t>{});
    s.can_undo      = get(j, "can_undo", false);
    s.undo_lasts    = get(j, "undo_lasts", false);
    s.pgn_out_error = get(j, "pgn_out_error", std::string{});
    if (const auto it = j.find("start_failures"); it != j.end() && it->is_array()) {
        for (const auto& e : *it) s.start_failures.push_back(startFailureFromJson(e));
    }
    s.stalled = get(j, "stalled", false);
    if (const auto it = j.find("in_flight"); it != j.end()) s.in_flight = costFromJson(*it);
    if (const auto it = j.find("allowance"); it != j.end()) s.allowance = allowanceFromJson(*it);
    s.waiting = waitFromName(get(j, "waiting", std::string{}));
    if (const auto it = j.find("machines"); it != j.end() && it->is_array()) {
        for (const auto& m : *it) {
            if (!m.is_object()) continue;
            Status::Machine machine;
            machine.id          = get(m, "id", std::string{});
            machine.name        = get(m, "name", std::string{});
            machine.connections = get(m, "connections", 0);
            machine.engines     = get(m, "engines", 0);
            machine.error       = get(m, "error", std::string{});
            machine.since       = get(m, "since", int64_t{0});
            s.machines.push_back(std::move(machine));
        }
    }
    return s;
}

nlohmann::json endJson(const End& e) {
    nlohmann::json j = {{"error", e.error},       {"abnormal", e.abnormal}, {"stopped_by_user", e.stopped_by_user},
                        {"finished", e.finished}, {"stalled", e.stalled},   {"exit_code", e.exit_code}};
    if (!e.failed_engine.empty()) {
        j["failed_engine"] = e.failed_engine;
        j["failed_reason"] = e.failed_reason;
    }
    return j;
}

End endFromJson(const nlohmann::json& j) {
    End e;
    e.error           = get(j, "error", std::string{});
    e.abnormal        = get(j, "abnormal", false);
    e.stopped_by_user = get(j, "stopped_by_user", false);
    e.finished        = get(j, "finished", false);
    e.stalled         = get(j, "stalled", false);
    e.failed_engine   = get(j, "failed_engine", std::string{});
    e.failed_reason   = get(j, "failed_reason", std::string{});
    e.exit_code       = get(j, "exit_code", 0);
    return e;
}

namespace {

nlohmann::json scoreJson(const std::optional<fastchess::Score>& score) {
    if (!score) return nullptr;
    return {score->type == fastchess::ScoreType::MATE ? "mate"
            : score->type == fastchess::ScoreType::CP ? "cp"
                                                      : "err",
            score->value};
}

std::optional<fastchess::Score> scoreFrom(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() != 2) return std::nullopt;
    fastchess::Score s;
    s.type  = fastchess::ScoreType::fromString(it->at(0).get<std::string>());
    s.value = it->at(1).get<int64_t>();
    return s;
}

nlohmann::json wdlJson(const std::optional<std::array<int64_t, 3>>& wdl) {
    if (!wdl) return nullptr;
    return {(*wdl)[0], (*wdl)[1], (*wdl)[2]};
}

std::optional<std::array<int64_t, 3>> wdlFrom(const nlohmann::json& j) {
    const auto it = j.find("wdl");
    if (it == j.end() || !it->is_array() || it->size() != 3) return std::nullopt;
    return std::array<int64_t, 3>{it->at(0).get<int64_t>(), it->at(1).get<int64_t>(), it->at(2).get<int64_t>()};
}

int colorIndex(chess::Color c) { return c == chess::Color::WHITE ? 0 : 1; }
chess::Color colorOf(int i) { return i == 0 ? chess::Color::WHITE : chess::Color::BLACK; }

}  // namespace

nlohmann::json liveMoveJson(const LiveMove& m, bool frc) {
    nlohmann::json j = {{"san", m.san}, {"uci", chess::uci::moveToUci(m.move, frc)}, {"mover", colorIndex(m.mover)}};
    if (m.book) {
        j["book"] = true;
        return j;
    }
    j["score"] = scoreJson(m.score);
    if (m.wdl) j["wdl"] = wdlJson(m.wdl);
    j["depth"]    = m.depth;
    j["seldepth"] = m.seldepth;
    j["time"]     = m.time_ms;
    j["nodes"]    = m.nodes;
    j["nps"]      = m.nps;
    j["tbhits"]   = m.tbhits;
    if (m.decision_ms) j["decision"] = *m.decision_ms;
    if (!m.pv.empty()) j["pv"] = m.pv;
    return j;
}

LiveMove liveMoveFromJson(const nlohmann::json& j) {
    LiveMove m;
    m.san      = get(j, "san", std::string{});
    m.mover    = colorOf(get(j, "mover", 0));
    m.book     = get(j, "book", false);
    m.score    = scoreFrom(j, "score");
    m.wdl      = wdlFrom(j);
    m.depth    = get(j, "depth", int64_t{0});
    m.seldepth = get(j, "seldepth", int64_t{0});
    m.time_ms  = get(j, "time", int64_t{0});
    m.nodes    = get(j, "nodes", uint64_t{0});
    m.nps      = get(j, "nps", uint64_t{0});
    m.tbhits   = get(j, "tbhits", uint64_t{0});
    if (j.contains("decision")) m.decision_ms = get(j, "decision", int64_t{0});
    m.pv = get(j, "pv", std::string{});
    return m;
}

nlohmann::json slotJson(const SlotState& s, bool frc) {
    nlohmann::json moves = nlohmann::json::array();
    for (const auto& m : s.moves) moves.push_back(liveMoveJson(m, frc));
    return {{"frc", frc},          {"active", s.active},       {"has_game", s.has_game}, {"game", s.game_id},
            {"round", s.round_id}, {"white", s.white},         {"black", s.black},       {"fen", s.start_fen},
            {"moves", moves},      {"stm", colorIndex(s.stm)}, {"thinking", s.thinking}, {"wtime", s.white_ms},
            {"btime", s.black_ms}, {"clocks", s.clocks},       {"info", s.info},         {"result", s.result},
            {"reason", s.reason}};
}

SlotState slotFromJson(const nlohmann::json& j) {
    SlotState s;
    s.active    = get(j, "active", false);
    s.has_game  = get(j, "has_game", false);
    s.game_id   = get(j, "game", std::size_t{0});
    s.round_id  = get(j, "round", std::size_t{0});
    s.white     = get(j, "white", std::string{});
    s.black     = get(j, "black", std::string{});
    s.start_fen = get(j, "fen", std::string{});
    s.stm       = colorOf(get(j, "stm", 0));
    s.thinking  = get(j, "thinking", false);
    s.white_ms  = get(j, "wtime", int64_t{0});
    s.black_ms  = get(j, "btime", int64_t{0});
    s.clocks    = get(j, "clocks", false);
    s.info      = get(j, "info", std::array<std::string, 2>{});
    s.result    = get(j, "result", std::string{});
    s.reason    = get(j, "reason", std::string{});
    // The moves, played on the board from the start position, so that each has its move.
    chess::Board board;
    board.set960(get(j, "frc", false));
    if (!s.start_fen.empty()) board.setFen(s.start_fen);
    if (const auto it = j.find("moves"); it != j.end() && it->is_array()) {
        for (const auto& mj : *it) {
            auto m = liveMoveFromJson(mj);
            m.move = chess::uci::uciToMove(board, get(mj, "uci", std::string{}));
            if (m.move == chess::Move::NO_MOVE) break;
            board.makeMove<true>(m.move);
            s.moves.push_back(std::move(m));
        }
    }
    s.think_start = Clock::now();
    return s;
}

nlohmann::json moveDataJson(const fastchess::MoveData& m) {
    nlohmann::json j = {{"move", m.move},   {"depth", m.depth}, {"seldepth", m.seldepth}, {"time", m.elapsed_millis},
                        {"nodes", m.nodes}, {"nps", m.nps},     {"tbhits", m.tbhits},     {"score", scoreJson(m.score)},
                        {"legal", m.legal}};
    if (m.wdl) j["wdl"] = wdlJson(m.wdl);
    if (m.decision_millis) j["decision"] = *m.decision_millis;
    if (!m.pv.empty()) j["pv"] = m.pv;
    if (m.book) j["book"] = true;
    return j;
}

fastchess::MoveData moveDataFromJson(const nlohmann::json& j) {
    fastchess::MoveData m(get(j, "move", std::string{}));
    m.depth          = get(j, "depth", int64_t{0});
    m.seldepth       = get(j, "seldepth", int64_t{0});
    m.elapsed_millis = get(j, "time", int64_t{0});
    m.nodes          = get(j, "nodes", uint64_t{0});
    m.nps            = get(j, "nps", uint64_t{0});
    m.tbhits         = get(j, "tbhits", uint64_t{0});
    m.score          = scoreFrom(j, "score");
    m.wdl            = wdlFrom(j);
    if (j.contains("decision")) m.decision_millis = get(j, "decision", int64_t{0});
    m.pv    = get(j, "pv", std::string{});
    m.legal = get(j, "legal", true);
    m.book  = get(j, "book", false);
    return m;
}

nlohmann::json gameStartJson(const fastchess::observer::GameStart& g) {
    return {{"slot", g.slot},       {"game", g.game_id},     {"round", g.round_id},
            {"white", g.white},     {"black", g.black},      {"fen", g.start_fen},
            {"book", g.book_moves}, {"wtime", g.white_time}, {"btime", g.black_time}};
}

fastchess::observer::GameStart gameStartFromJson(const nlohmann::json& j) {
    fastchess::observer::GameStart g;
    g.slot       = get(j, "slot", -1);
    g.game_id    = get(j, "game", std::size_t{0});
    g.round_id   = get(j, "round", std::size_t{0});
    g.white      = get(j, "white", std::string{});
    g.black      = get(j, "black", std::string{});
    g.start_fen  = get(j, "fen", std::string{});
    g.book_moves = get(j, "book", std::vector<std::string>{});
    g.white_time = get(j, "wtime", int64_t{0});
    g.black_time = get(j, "btime", int64_t{0});
    return g;
}

nlohmann::json totalsJson(const EngineTotals& t) {
    return {{"moves", t.moves},       {"depth", t.depth_sum},      {"nps", t.nps_sum},
            {"nps_n", t.nps_samples}, {"nodes", t.nodes_sum},      {"nodes_n", t.nodes_samples},
            {"time", t.time_ms},      {"decision", t.decision_ms}, {"decision_n", t.decision_samples}};
}

EngineTotals totalsFromJson(const nlohmann::json& j) {
    EngineTotals t;
    t.moves            = get(j, "moves", uint64_t{0});
    t.depth_sum        = get(j, "depth", uint64_t{0});
    t.nps_sum          = get(j, "nps", 0.0);
    t.nps_samples      = get(j, "nps_n", uint64_t{0});
    t.nodes_sum        = get(j, "nodes", uint64_t{0});
    t.nodes_samples    = get(j, "nodes_n", uint64_t{0});
    t.time_ms          = get(j, "time", uint64_t{0});
    t.decision_ms      = get(j, "decision", uint64_t{0});
    t.decision_samples = get(j, "decision_n", uint64_t{0});
    return t;
}

nlohmann::json statsJson(const fastchess::Stats& s) {
    return {s.wins, s.losses, s.draws, s.penta_LL, s.penta_LD, s.penta_WL, s.penta_DD, s.penta_WD, s.penta_WW};
}

fastchess::Stats statsFromJson(const nlohmann::json& j) {
    fastchess::Stats s;
    if (!j.is_array() || j.size() != 9) return s;
    s.wins     = j[0].get<int>();
    s.losses   = j[1].get<int>();
    s.draws    = j[2].get<int>();
    s.penta_LL = j[3].get<int>();
    s.penta_LD = j[4].get<int>();
    s.penta_WL = j[5].get<int>();
    s.penta_DD = j[6].get<int>();
    s.penta_WD = j[7].get<int>();
    s.penta_WW = j[8].get<int>();
    return s;
}

nlohmann::json allowanceJson(const tour::Allowance& a) {
    return {{"threads", a.threads}, {"memory", a.memory_mb}, {"gpus", a.gpus}, {"one", a.one_game_always}};
}

tour::Allowance allowanceFromJson(const nlohmann::json& j) {
    tour::Allowance a;
    a.threads         = get(j, "threads", -1);
    a.memory_mb       = get(j, "memory", int64_t{-1});
    a.gpus            = get(j, "gpus", -1);
    a.one_game_always = get(j, "one", true);
    return a;
}

nlohmann::json costJson(const tour::GameCost& c) { return {c.threads, c.memory_mb, c.gpus}; }

tour::GameCost costFromJson(const nlohmann::json& j) {
    tour::GameCost c;
    if (!j.is_array() || j.size() != 3) return c;
    c.threads   = j[0].get<int>();
    c.memory_mb = j[1].get<uint64_t>();
    c.gpus      = j[2].get<int>();
    return c;
}

nlohmann::json startFailureJson(const tour::StartFailures::Engine& e) {
    using namespace std::chrono;
    const auto next = duration_cast<seconds>(e.next_try - tour::StartFailures::Clock::now()).count();
    return {{"participant", e.participant},
            {"name", e.name},
            {"error", e.error},
            {"tries", e.tries},
            {"failed", e.failed},
            {"trying", e.trying},
            {"trying_game", e.trying_game},
            {"next_try", std::max<int64_t>(0, next)},
            {"since", duration_cast<seconds>(e.since.time_since_epoch()).count()},
            {"log", e.log.string()},
            {"last_chance", e.last_chance}};
}

tour::StartFailures::Engine startFailureFromJson(const nlohmann::json& j) {
    using namespace std::chrono;
    tour::StartFailures::Engine e;
    e.participant = get(j, "participant", -1);
    e.name        = get(j, "name", std::string{});
    e.error       = get(j, "error", std::string{});
    e.tries       = get(j, "tries", 0);
    e.failed      = get(j, "failed", false);
    e.trying      = get(j, "trying", false);
    e.trying_game = get(j, "trying_game", uint64_t{0});
    e.next_try    = tour::StartFailures::Clock::now() + seconds(get(j, "next_try", int64_t{0}));
    e.since       = system_clock::time_point(seconds(get(j, "since", int64_t{0})));
    e.log         = get(j, "log", std::string{});
    e.last_chance = get(j, "last_chance", false);
    return e;
}

const char* waitName(tour::Wait wait) {
    switch (wait) {
        case tour::Wait::None:
            return "";
        case tour::Wait::Cores:
            return "cores";
        case tour::Wait::Memory:
            return "memory";
        case tour::Wait::Gpus:
            return "gpus";
    }
    return "";
}

tour::Wait waitFromName(std::string_view name) {
    if (name == "cores") return tour::Wait::Cores;
    if (name == "memory") return tour::Wait::Memory;
    if (name == "gpus") return tour::Wait::Gpus;
    return tour::Wait::None;
}

nlohmann::json command(std::string_view kind, uint64_t id, nlohmann::json fields) {
    if (!fields.is_object()) fields = nlohmann::json::object();
    fields["c"] = kind;
    if (id) fields["id"] = id;
    return fields;
}

void LineSplitter::feed(std::string_view data, std::vector<std::string>& lines, std::size_t max) {
    while (!data.empty()) {
        const auto end = data.find('\n');
        if (end == std::string_view::npos) {
            if (!skipping_) partial_.append(data);
            if (partial_.size() > max) {
                partial_.clear();
                skipping_ = true;
            }
            return;
        }
        if (!skipping_) {
            partial_.append(data.substr(0, end));
            if (!partial_.empty() && partial_.back() == '\r') partial_.pop_back();
            if (partial_.size() <= max) lines.push_back(std::move(partial_));
        }
        partial_.clear();
        skipping_ = false;
        data.remove_prefix(end + 1);
    }
}

}  // namespace fcg::control
