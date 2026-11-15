#include "analysis.hpp"

#include <algorithm>

#include <engine/uci_engine.hpp>
#include <tour/engine_text.hpp>

#include "engines.hpp"
#include "i18n.hpp"

namespace fcg {

namespace {

using Clock = std::chrono::steady_clock;

// How long an engine has to give its move once told to stop, and to answer isready once its options are sent (a
// large hash table takes a moment to clear).
constexpr std::chrono::seconds STOP_TIMEOUT{5};

bool startsWith(const std::string& line, std::string_view word) {
    return line.size() >= word.size() && line.compare(0, word.size(), word) == 0 &&
           (line.size() == word.size() || line[word.size()] == ' ');
}

}  // namespace

bool takeInfoLine(std::vector<AnalysisLine>& lines, const std::string& info, int multipv) {
    if (!startsWith(info, "info") || info.find(" string ") != std::string::npos) return false;
    const auto parsed = fastchess::engine::UciEngine::parseInfo(info);
    if (!parsed.score) return false;
    const int rank = static_cast<int>(parsed.multipv.value_or(1));
    if (rank < 1 || rank > std::max(1, multipv)) return false;
    if (static_cast<int>(lines.size()) < rank) lines.resize(static_cast<std::size_t>(rank));

    auto& line      = lines[static_cast<std::size_t>(rank - 1)];
    line.multipv    = rank;
    line.score      = *parsed.score;
    line.lowerbound = parsed.lowerbound;
    line.upperbound = parsed.upperbound;
    if (parsed.depth) line.depth = *parsed.depth;
    if (parsed.seldepth) line.seldepth = *parsed.seldepth;
    if (parsed.wdl) line.wdl = *parsed.wdl;
    if (parsed.nodes) line.nodes = *parsed.nodes;
    if (parsed.nps) line.nps = *parsed.nps;
    if (parsed.time) line.time_ms = *parsed.time;
    // A bound often comes without its PV: the last one stays.
    if (!parsed.pv.empty()) line.pv = parsed.pv;
    return true;
}

std::string goCommand(const SearchLimit& limit) {
    if (limit.depth > 0) return "go depth " + std::to_string(limit.depth);
    if (limit.movetime_ms > 0) return "go movetime " + std::to_string(limit.movetime_ms);
    return "go infinite";
}

std::string positionCommand(const AnalysisPosition& position) {
    std::string command = "position fen " + position.fen;
    if (!position.moves.empty()) {
        command += " moves";
        for (const auto& m : position.moves) command += " " + m;
    }
    return command;
}

bool Analyser::hasOption(const std::string& name) const {
    return std::any_of(options_.begin(), options_.end(), [&](const UciOptionInfo& o) { return o.name == name; });
}

int Analyser::effectiveThreads() const { return hasOption("Threads") ? std::max(1, settings_.threads) : 1; }

double Analyser::searchSeconds() const {
    if (state_ != State::Searching) return 0;
    return std::chrono::duration<double>(Clock::now() - search_start_).count();
}

void Analyser::start(const EngineEntry& entry, const AnalysisSettings& settings) {
    stop();
    engine_          = entry.name;
    id_name_         = entry.id_name;
    settings_        = settings;
    sent_            = {};
    library_options_ = tour::forEngine(entry.options);
    options_.clear();
    last_words_.clear();
    ready_sent_    = false;
    options_due_   = false;
    chess960_      = false;
    new_game_due_  = true;
    start_timeout_ = entry.start_timeout > 0
                         ? std::chrono::milliseconds(entry.start_timeout * 1000LL)
                         : std::chrono::duration_cast<std::chrono::milliseconds>(ENGINE_STARTUP_TIME);
    std::string why;
    if (!process_.start(engineLaunch(entry), why)) {
        fail(trf("Cannot start {}: {}", entry.name, toUtf8(why)));
        return;
    }
    state_    = State::Starting;
    deadline_ = Clock::now() + start_timeout_;
    (void)process_.send("uci");
}

void Analyser::stop() {
    claim_.set(0);
    if (process_.started()) process_.stop(std::chrono::milliseconds(500));
    state_ = State::Off;
    error_.clear();
    wanted_.reset();
    lines_.clear();
    finished_ = false;
}

void Analyser::fail(std::string message) {
    claim_.set(0);
    error_ = std::move(message);
    // What the engine wrote last tells why, most often.
    std::string words;
    for (const auto& w : last_words_) words += "\n" + w;
    if (!words.empty()) error_ += "\n" + std::string(TR("The engine wrote:")) + words;
    if (process_.started()) process_.stop(std::chrono::milliseconds(200));
    state_ = State::Failed;
}

void Analyser::update(const AnalysisSettings& settings) {
    const bool changed = settings.threads != settings_.threads || settings.hash != settings_.hash ||
                         settings.multipv != settings_.multipv;
    settings_          = settings;
    if (!changed || state_ == State::Off || state_ == State::Failed) return;
    options_due_ = true;
    // The search starts again with them.
    if (state_ == State::Searching && !wanted_) {
        wanted_       = searched_;
        wanted_limit_ = limit_;
    }
    if (state_ == State::Searching) {
        (void)process_.send("stop");
        state_    = State::Stopping;
        deadline_ = Clock::now() + STOP_TIMEOUT;
    } else if (state_ == State::Ready) {
        beginSearch();
    }
}

void Analyser::analyse(const AnalysisPosition& position, SearchLimit limit, bool new_game) {
    if (position.empty()) return;
    new_game_due_ |= new_game;
    // The same search already running: nothing to do.
    if (state_ == State::Searching && !wanted_ && position == searched_ && !finished_ &&
        limit.movetime_ms == limit_.movetime_ms && limit.depth == limit_.depth && !new_game) {
        return;
    }
    wanted_       = position;
    wanted_limit_ = limit;
    if (state_ == State::Searching) {
        (void)process_.send("stop");
        state_    = State::Stopping;
        deadline_ = Clock::now() + STOP_TIMEOUT;
    } else if (state_ == State::Ready) {
        beginSearch();
    }
}

void Analyser::halt() {
    wanted_.reset();
    if (state_ == State::Searching) {
        (void)process_.send("stop");
        state_    = State::Stopping;
        deadline_ = Clock::now() + STOP_TIMEOUT;
    }
}

void Analyser::sendOptions(bool all) {
    const auto set = [&](const std::string& name, const std::string& value) {
        if (hasOption(name)) (void)process_.send("setoption name " + name + " value " + value);
    };
    if (all) {
        // The library's values first, as a game sends them, buttons aside; those the analysis sets come after.
        for (const auto& [name, value] : library_options_) {
            if (name == "Threads" || name == "Hash" || name == "MultiPV" || name == "UCI_AnalyseMode") continue;
            const auto it =
                std::find_if(options_.begin(), options_.end(), [&](const UciOptionInfo& o) { return o.name == name; });
            if (it == options_.end() || it->type == "button") continue;
            set(name, value);
        }
        set("UCI_ShowWDL", "true");
        set("UCI_AnalyseMode", "true");
    }
    if (all || settings_.threads != sent_.threads) set("Threads", std::to_string(std::max(1, settings_.threads)));
    if (settings_.hash > 0 && (all || settings_.hash != sent_.hash)) set("Hash", std::to_string(settings_.hash));
    if (all || settings_.multipv != sent_.multipv) set("MultiPV", std::to_string(std::clamp(settings_.multipv, 1, 64)));
    sent_ = settings_;
}

void Analyser::beginSearch() {
    if (!wanted_) return;
    if (options_due_) {
        sendOptions(false);
        options_due_ = false;
    }
    if (wanted_->chess960 != chess960_ && hasOption("UCI_Chess960")) {
        (void)process_.send(std::string("setoption name UCI_Chess960 value ") + (wanted_->chess960 ? "true" : "false"));
        chess960_ = wanted_->chess960;
    }
    if (new_game_due_) {
        (void)process_.send("ucinewgame");
        new_game_due_ = false;
    }
    searched_ = *wanted_;
    limit_    = wanted_limit_;
    wanted_.reset();
    lines_.clear();
    bestmove_.clear();
    finished_ = false;
    (void)process_.send(positionCommand(searched_));
    (void)process_.send(goCommand(limit_));
    state_        = State::Searching;
    search_start_ = Clock::now();
    // A limited search gets its time and some more; a depth, a while.
    if (limit_.movetime_ms > 0)
        deadline_ = search_start_ + std::chrono::milliseconds(limit_.movetime_ms) + std::chrono::seconds(3);
    else if (limit_.depth > 0)
        deadline_ = search_start_ + std::chrono::minutes(5);
    claim_.set(effectiveThreads());
}

void Analyser::handle(const std::string& line) {
    last_words_.push_back(line);
    if (last_words_.size() > 8) last_words_.erase(last_words_.begin());

    switch (state_) {
        case State::Starting:
            if (startsWith(line, "option")) {
                if (auto option = parseUciOption(line)) options_.push_back(std::move(*option));
            } else if (startsWith(line, "id") && line.rfind("id name ", 0) == 0) {
                id_name_ = line.substr(8);
            } else if (line == "uciok" || startsWith(line, "uciok")) {
                sendOptions(true);
                (void)process_.send("isready");
                ready_sent_ = true;
                deadline_   = Clock::now() + start_timeout_;
            } else if (ready_sent_ && startsWith(line, "readyok")) {
                state_ = State::Ready;
                beginSearch();
            }
            break;
        case State::Searching:
            if (startsWith(line, "info")) {
                (void)takeInfoLine(lines_, line, hasOption("MultiPV") ? settings_.multipv : 1);
            } else if (startsWith(line, "bestmove")) {
                // It ended on its own: a limited search, a mate found, or an engine that does not search forever.
                const auto space = line.find(' ', 9);
                bestmove_ = line.size() > 9 ? line.substr(9, space == std::string::npos ? space : space - 9) : "";
                finished_ = true;
                state_    = State::Ready;
                claim_.set(0);
                beginSearch();
            }
            break;
        case State::Stopping:
            if (startsWith(line, "bestmove")) {
                state_ = State::Ready;
                claim_.set(0);
                beginSearch();
            }
            break;
        default:
            break;
    }
}

void Analyser::poll() {
    if (state_ == State::Off || state_ == State::Failed) return;
    for (const auto& line : process_.take()) {
        if (line.kind == EngineProcess::Line::Kind::Out || line.kind == EngineProcess::Line::Kind::Err)
            handle(line.text);
        if (state_ == State::Failed || state_ == State::Off) return;
    }
    if (!process_.running()) {
        fail(trf("{} stopped.", engine_));
        return;
    }
    const auto now = Clock::now();
    if (now < deadline_) return;
    switch (state_) {
        case State::Starting:
            fail(ready_sent_ ? trf("{} does not answer isready.", engine_)
                             : trf("{} does not answer uci: is it a UCI engine?", engine_));
            break;
        case State::Stopping:
            fail(trf("{} does not give its move when told to stop.", engine_));
            break;
        case State::Searching:
            // A limited search that runs over: it is told to stop.
            if (!limit_.infinite()) {
                (void)process_.send("stop");
                state_    = State::Stopping;
                deadline_ = now + STOP_TIMEOUT;
            }
            break;
        default:
            break;
    }
}

}  // namespace fcg
