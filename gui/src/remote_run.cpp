#include "remote_run.hpp"

#include <mbedtls/platform_util.h>

#include <chrono>
#include <set>
#include <thread>

#include "console.hpp"
#include "control_child.hpp"
#include "i18n.hpp"

namespace fcg {

namespace {

chess::Color colorOf(int i) { return i == 0 ? chess::Color::WHITE : chess::Color::BLACK; }

// How long a command waits for its reply: an edit is journaled and answered at once.
constexpr auto REPLY_WAIT = std::chrono::seconds(10);

}  // namespace

std::vector<std::string> remoteArgs(const RemoteStart& s) {
    std::vector<std::string> args = {"--profile",    s.profile.string(), "--run",
                                     s.dir.string(), "--control",        s.detached ? "socket" : "stdio"};
    if (s.allowance) {
        args.push_back("--allowance");
        args.push_back(allowanceArg(*s.allowance));
    }
    args.insert(args.end(), s.extra.begin(), s.extra.end());
    return args;
}

RemoteRun::~RemoteRun() {
    // The process started here with pipes reads the end of its input and stops, resumable.
    process_.disconnect();
    for (auto& [file, secret] : secrets_) {
        if (!secret.empty()) mbedtls_platform_zeroize(secret.data(), secret.size());
    }
}

void RemoteRun::setSecrets(std::map<std::string, std::string> secrets) {
    for (auto& [file, secret] : secrets_) {
        if (!secret.empty()) mbedtls_platform_zeroize(secret.data(), secret.size());
    }
    secrets_      = std::move(secrets);
    secrets_sent_ = secrets_.empty();
}

std::string RemoteRun::start(const RemoteStart& s) {
    dir_ = s.dir;
    if (s.allowance) {
        allowance_      = *s.allowance;
        allowance_sent_ = true;
    }
    std::string error;
    if (s.detached) {
        // It makes its socket, then the window connects (update()); its copy is shown meanwhile.
        if (!process_.spawnDetached(s.program, remoteArgs(s), error))
            return trf("Cannot start the tournament: {}", error);
        pid_           = process_.pid();
        detached_      = true;
        connecting_    = true;
        connect_until_ = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        return open(s.dir);
    }
    if (!process_.start(s.program, remoteArgs(s), error)) return trf("Cannot start the tournament: {}", error);
    pid_ = process_.pid();
    control::Hello hello;
    hello.version = FCG_VERSION;
    (void)process_.send(control::helloJson(hello));
    return open(s.dir);
}

void RemoteRun::tryConnect() {
    if (reconnect_) {
        // On another machine: through its SSH, the link connecting on its own thread.
        std::string gone;
        auto link = reconnect_(gone);
        if (!link) {
            if (!gone.empty()) {
                connecting_     = false;
                error_          = gone;
                state_          = State::Finished;
                info_.finished  = Clock::now();
                finished_event_ = true;
            }
            return;
        }
        attachLink(std::move(link));
        return;
    }
    const auto file     = local::readControlFile(dir_);
    const auto endpoint = file ? file->endpoint : local::endpointFor(dir_);
    std::string error;
    auto link = local::connect(endpoint, error);
    if (!link) return;
    if (file && file->pid) pid_ = file->pid;
    attachLink(std::move(link));
}

void RemoteRun::attachLink(std::unique_ptr<Link> link) {
    process_.attach(std::move(link));
    // Where its copy's journal ends: the process sends the lines after it, then the games in progress as they stand.
    control::Hello hello;
    hello.version = FCG_VERSION;
    hello.journal = replica_ ? replica_->journalSize() : 0;
    (void)process_.send(control::helloJson(hello));
    allowance_sent_ = false;  // told again: it may have another one meanwhile
    secrets_sent_   = secrets_.empty();
    connecting_     = false;
}

std::string RemoteRun::attach(const std::filesystem::path& dir, std::unique_ptr<Link> link) {
    dir_      = dir;
    detached_ = true;
    if (auto error = open(dir); !error.empty()) return error;
    if (const auto file = local::readControlFile(dir); file && file->pid) pid_ = file->pid;
    process_.attach(std::move(link));
    // Where its copy's journal ends: the process sends the lines after it, then the games in progress as they stand.
    control::Hello hello;
    hello.version = FCG_VERSION;
    hello.journal = replica_->journalSize();
    (void)process_.send(control::helloJson(hello));
    return {};
}

std::string RemoteRun::open(const std::filesystem::path& dir) {
    try {
        replica_ = tour::TournamentState::open(dir, true);
    } catch (const std::exception& e) {
        process_.kill();
        return trf("Cannot open the tournament: {}", e.what());
    }
    const auto& spec  = replica_->spec();
    info_             = runInfo(spec, dir);
    info_.total_games = replica_->totalGames().value_or(0);
    info_.started     = Clock::now();
    observer_         = std::make_unique<GuiObserver>(spec.conditions.concurrency, *replica_, false);
    state_            = State::Running;
    return {};
}

void RemoteRun::reopen() {
    try {
        replica_ = tour::TournamentState::open(dir_, true);
    } catch (const std::exception& e) {
        Console::instance().add(trf("Cannot open the tournament again: {}", e.what()), true, info_.name);
        return;
    }
    observer_->updateParticipants(replica_->spec());
    info_.total_games  = replica_->totalGames().value_or(0);
    info_.participants = static_cast<int>(replica_->spec().participants.size());
    reopened_          = true;
}

void RemoteRun::apply(const nlohmann::json& m) {
    // Another window talks with it already: this one lets go.
    if (m.value("busy", false)) {
        elsewhere_ = true;
        error_     = TR("This tournament is shown in another window.");
        process_.disconnect();
        return;
    }
    if (const auto hello = control::parseHello(m)) {
        if (hello->protocol != control::PROTOCOL) {
            error_ = trf("The tournament's process speaks another version of the program ({}): restart the window.",
                         hello->version);
            process_.disconnect();
        }
        if (hello->pid) pid_ = hello->pid;
        // Playing since before this window connected: its time counts from its start, not from the connection.
        if (hello->playing_ms > 0) info_.started = Clock::now() - std::chrono::milliseconds(hello->playing_ms);
        return;
    }
    const auto kind = m.value("e", std::string{});
    try {
        if (kind == "journal") {
            if (!replica_) return;
            const auto at   = m.at("at").get<uint64_t>();
            const auto size = replica_->journalSize();
            if (at < size) return;  // in the copy already
            if (at > size || !replica_->follow(m.at("event"), m.at("end").get<uint64_t>())) {
                // A line missed, or an edit undone: the copy is opened again from the files.
                reopen();
                return;
            }
            const auto type = m.at("event").value("t", std::string{});
            if (tour::isEditEvent(type) || type == "round_paired" || type == "tiebreak_added") {
                observer_->updateParticipants(replica_->spec());
                info_.total_games  = replica_->totalGames().value_or(0);
                info_.participants = static_cast<int>(replica_->spec().participants.size());
            }
        } else if (kind == "state") {
            status_ = control::statusFromJson(m);
            if (replica_) {
                tour::TournamentState::ReplicaStatus rs;
                rs.running       = std::set<uint64_t>(status_.running.begin(), status_.running.end());
                rs.can_undo      = status_.can_undo;
                rs.undo_lasts    = status_.undo_lasts;
                rs.pgn_out_error = status_.pgn_out_error;
                replica_->setReplicaStatus(std::move(rs));
                replica_->startFailures().assign(status_.start_failures, status_.stalled);
            }
        } else if (kind == "game_start") {
            observer_->onGameStart(control::gameStartFromJson(m));
        } else if (kind == "thinking") {
            observer_->onThinking(m.at("slot").get<int>(), colorOf(m.at("stm").get<int>()),
                                  m.at("left").get<int64_t>());
        } else if (kind == "info") {
            const auto line = m.at("line").get<std::string>();
            observer_->onEngineLine(m.at("slot").get<int>(), colorOf(m.at("side").get<int>()), line);
        } else if (kind == "move") {
            const int slot   = m.at("slot").get<int>();
            const auto mover = colorOf(m.at("mover").get<int>());
            observer_->onMove(slot, mover, control::moveDataFromJson(m.at("data")), m.value("san", std::string{}),
                              m.value("wtime", int64_t{0}), m.value("btime", int64_t{0}));
            if (const auto it = m.find("cpu"); it != m.end() && it->is_array() && it->size() == 3) {
                CpuMeter::Used used;
                used.cpu_ms  = {it->at(0).get<uint64_t>(), it->at(1).get<uint64_t>()};
                used.wall_ms = it->at(2).get<uint64_t>();
                observer_->addCpu(slot, mover, used);
            }
        } else if (kind == "game_end") {
            observer_->gameEnded(m.at("slot").get<int>(), m.value("counted", false), m.value("result", std::string{}),
                                 m.value("reason", std::string{}));
        } else if (kind == "recorded") {
            const auto result = tour::gameResultFromJson(m.at("result"));
            const auto& side  = m.at("side");
            std::optional<fastchess::Stats> pair;
            if (m.contains("pair")) pair = control::statsFromJson(m.at("pair"));
            observer_->recorded(result, {control::totalsFromJson(side.at(0)), control::totalsFromJson(side.at(1))},
                                pair);
        } else if (kind == "slot") {
            observer_->restoreSlot(m.at("slot").get<int>(), control::slotFromJson(m));
        } else if (kind == "log") {
            Console::instance().add(m.value("text", std::string{}), m.value("warning", false), info_.name);
        } else if (kind == "reply") {
            replies_[m.value("id", uint64_t{0})] = m.value("error", std::string{});
        } else if (kind == "end") {
            end_   = control::endFromJson(m);
            ended_ = true;
            if (!end_.error.empty()) error_ = end_.error;
        }
    } catch (const std::exception& e) {
        // A message this window cannot read: said once in the log, the others are read.
        Console::instance().add(trf("A message of the tournament's process could not be read: {}", e.what()), true,
                                info_.name);
    }
}

void RemoteRun::update() {
    if (state_ != State::Running) return;
    if (connecting_) {
        tryConnect();
        if (connecting_) {
            // Its process ended before it listened (it said why in its log), or does not answer.
            const bool gone = pid_ && process_.pid() && !process_.childRunning();
            if (gone || std::chrono::steady_clock::now() > connect_until_) {
                connecting_     = false;
                error_          = gone ? std::string(TR("The tournament's process ended as it started: see its log "
                                                        "(fastchess-gui.log in its folder)."))
                                       : std::string(TR("The connection with the tournament's process was lost."));
                state_          = State::Finished;
                info_.finished  = Clock::now();
                finished_event_ = true;
            }
            return;
        }
    }
    for (const auto& m : process_.take()) apply(m);
    // A move the copy of a game could not play: the games in progress are asked again, at most once a second.
    if (observer_ && observer_->takeResync() && Clock::now() - resync_asked_ > std::chrono::seconds(1)) {
        resync_asked_ = Clock::now();
        process_.send(control::command("snapshot", 0, nlohmann::json::object()));
    }
    if (!allowance_sent_) {
        allowance_sent_ =
            process_.send(control::command("allowance", 0, {{"allowance", control::allowanceJson(allowance_)}}));
    }
    if (!secrets_sent_) {
        bool sent = true;
        for (const auto& [file, secret] : secrets_) {
            sent &= process_.send(control::command("passphrase", 0, {{"key_file", file}, {"passphrase", secret}}));
        }
        secrets_sent_ = sent;
    }
    if (!process_.closed()) return;

    // Its last words, then its end.
    for (const auto& m : process_.take()) apply(m);
    // Detached, the connection may end while the tournament plays on (the process was busy, a broken connection): it
    // is connected again while its socket is there. On another machine, as long as the network takes to come back:
    // the reconnection says when its process is gone.
    if (detached_ && !ended_ && !elsewhere_ && (reconnect_ || local::readControlFile(dir_))) {
        connecting_    = true;
        connect_until_ = std::chrono::steady_clock::now() + std::chrono::seconds(reconnect_ ? 365 * 24 * 3600 : 10);
        return;
    }
    if (!ended_ && error_.empty()) {
        const auto code = process_.exitCode();
        error_          = code ? trf("The tournament's process ended unexpectedly (exit code {}): it resumes exactly "
                                     "where it stopped.",
                                     *code)
                               : std::string(TR("The connection with the tournament's process was lost."));
    }
    state_          = State::Finished;
    info_.finished  = Clock::now();
    finished_event_ = true;
}

std::string RemoteRun::ask(const char* kind, nlohmann::json fields) {
    if (state_ != State::Running || process_.closed()) return TR("The tournament is not playing.");
    const auto id = ++last_id_;
    if (!process_.send(control::command(kind, id, std::move(fields)))) return TR("The tournament's process is gone.");
    // The events before the reply (the journal line of an edit) are applied first: the copy has the change.
    const auto until = std::chrono::steady_clock::now() + REPLY_WAIT;
    while (std::chrono::steady_clock::now() < until) {
        for (const auto& m : process_.take()) apply(m);
        if (const auto it = replies_.find(id); it != replies_.end()) {
            auto error = it->second;
            replies_.erase(it);
            return error;
        }
        if (process_.closed()) return TR("The tournament's process is gone.");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return TR("The tournament's process did not answer.");
}

std::string RemoteRun::localEdit(const std::function<void(tour::TournamentState&)>& change) {
    try {
        if (!writer_) writer_ = tour::TournamentState::open(dir_);
        change(*writer_);
    } catch (const std::exception& e) {
        return e.what();
    }
    observer_->updateParticipants(writer_->spec());
    info_.total_games  = writer_->totalGames().value_or(0);
    info_.participants = static_cast<int>(writer_->spec().participants.size());
    return {};
}

std::string RemoteRun::applyEdit(const tour::Edit& edit) {
    if (!replica_) return TR("No tournament is open.");
    if (state_ != State::Running) return localEdit([&](tour::TournamentState& t) { t.applyEdit(edit); });
    // What cannot be done is said here, without asking the process.
    if (const auto impact = replica_->preview(edit); !impact.error.empty()) return impact.error;
    return ask("edit", {{"edit", edit}});
}

std::string RemoteRun::undoEdit() {
    if (state_ != State::Running) {
        if (!writer_) return TR("There is no change to undo.");
        return localEdit([](tour::TournamentState& t) { t.undoEdit(); });
    }
    return ask("undo", nlohmann::json::object());
}

std::string RemoteRun::endGame(int slot, uint64_t number, const std::string& result, const std::string& why) {
    if (state_ != State::Running) return TR("No game is being played.");
    if (replica_ && !replica_->spec().conditions.recover) {
        return TR(
            "This tournament was started without \"Restart an engine that crashes\" (setup, PGN and misc): "
            "ending a game stops the engine on move, which would stop the tournament. Set the result once the "
            "game is over, in the Games tab.");
    }
    return ask("end_game", {{"slot", slot}, {"number", number}, {"result", result}, {"why", why}});
}

void RemoteRun::setPaused(bool paused) {
    if (state_ != State::Running) return;
    if (process_.send(control::command(paused ? "pause" : "resume", 0))) status_.paused = paused;
}

bool RemoteRun::tryEngineAgain(int participant) {
    if (state_ != State::Running || stop_requested_ || !replica_) return false;
    const auto engine = replica_->startFailures().engine(participant);
    if (!engine || engine->trying) return false;
    return process_.send(control::command("try_engine", 0, {{"participant", participant}}));
}

void RemoteRun::stopNow() {
    if (state_ != State::Running) return;
    stop_requested_ = true;
    (void)process_.send(control::command("stop", 0, {{"now", true}}));
}

void RemoteRun::stopWhenIdle() {
    if (state_ != State::Running) return;
    stop_when_idle_ = true;
    (void)process_.send(control::command("stop", 0, {{"now", false}}));
}

void RemoteRun::setAllowance(const tour::Allowance& allowance) {
    if (allowance == allowance_ && allowance_sent_) return;
    allowance_ = allowance;
    allowance_sent_ =
        state_ == State::Running &&
        process_.send(control::command("allowance", 0, {{"allowance", control::allowanceJson(allowance)}}));
}

std::optional<StartupFailure> RemoteRun::startupFailed() const {
    if (end_.failed_engine.empty()) return std::nullopt;
    return StartupFailure{end_.failed_engine, end_.failed_reason};
}

bool RemoteRun::takeFinishedEvent() { return std::exchange(finished_event_, false); }

bool RemoteRun::takeReopened() { return std::exchange(reopened_, false); }

void RemoteRun::disconnect() {
    process_.disconnect();
    state_ = State::Finished;
}

}  // namespace fcg
