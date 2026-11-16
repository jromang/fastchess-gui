#include "engine_client.hpp"

#include <algorithm>
#include <atomic>
#include <ctime>
#include <filesystem>
#include <iterator>

#include <core/globals/globals.hpp>
#include <core/threading/thread_vector.hpp>
#include <engine/uci_engine.hpp>

#include <fmt/include/fmt/format.h>

#include "engines.hpp"
#include "proc_tree.hpp"

namespace fastchess {
extern util::ThreadVector<ProcessInformation> process_list;
}

namespace fcg {

namespace {

std::atomic<int> g_board_threads{0};

using fastchess::engine::process::Status;

}  // namespace

EngineLaunch engineLaunch(const EngineEntry& entry, bool raw) {
    EngineLaunch launch;
    const auto start = engineStart(entry);
    launch.name      = entry.name;
    launch.dir       = start.dir;
    launch.program   = start.cmd;
    launch.args      = start.args;
    // As fastchess starts it in a tournament (UciEngine::start), the program quoted for the adapter.
    if (!raw && entry.protocol == "xboard" && !fastchess::engine::xboard_adapter.empty()) {
        launch.program = fastchess::engine::xboard_adapter;
        launch.args    = fmt::format("--wb2uci \"{}\"{}{}", start.cmd, start.args.empty() ? "" : " ", start.args);
    }
    return launch;
}

struct EngineProcess::Impl {
    fastchess::engine::process::Process process;
    // Writes, and the checks of whether the process lives (which reap it), from one thread at a time: the reading
    // thread checks too, on Windows.
    std::mutex io;
};

EngineProcess::EngineProcess() = default;

EngineProcess::~EngineProcess() { stop(); }

void EngineProcess::push(Line::Kind kind, std::string text) {
    {
        std::lock_guard lock(mutex_);
        pending_.push_back(Line{kind, std::move(text), std::chrono::system_clock::now()});
        // Nobody collects them (a console hidden for long): the oldest go.
        if (pending_.size() > 100000) pending_.pop_front();
    }
    cv_.notify_all();
}

bool EngineProcess::start(const EngineLaunch& launch, std::string& error) {
    stop();
    {
        std::lock_guard lock(mutex_);
        pending_.clear();
        ended_ = false;
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(launch.program, ec)) {
        error = fmt::format("File not found: {}", launch.program);
        return false;
    }

    impl_         = std::make_unique<Impl>();
    auto& process = impl_->process;
    process.setRealtimeLogging(false);
    process.setErrorLineCallback([this](const std::string& line) { push(Line::Kind::Err, toUtf8(line)); });
    if (const auto r = process.init(launch.dir, launch.program, launch.args, launch.name); !r) {
        error = r.message.empty() ? std::string("cannot start the program") : r.message;
        impl_.reset();
        return false;
    }

    // Off fastchess' list: the end of a tournament ends every process on it, and a start interrupts them.
    if (const auto pid = process.pid(); pid != 0) {
        fastchess::process_list.remove_if([pid](const fastchess::ProcessInformation& info) {
#ifdef _WIN64
            return static_cast<int64_t>(GetProcessId(info.identifier)) == pid;
#else
            return static_cast<int64_t>(info.identifier) == pid;
#endif
        });
    }

    {
        std::lock_guard lock(mutex_);
        started_ = true;
    }
    reader_ = std::thread([this] { readLoop(); });
    return true;
}

void EngineProcess::readLoop() {
    // One read without an end word nor a time limit, each line passed on by the callbacks as it comes: it lasts until
    // the engine's output ends. The lines it also collects are dropped as they come.
    std::vector<fastchess::engine::process::Line> lines;
    auto& process = impl_->process;
    process.setLineCallback([this, &lines](const std::string& line) {
        push(Line::Kind::Out, toUtf8(line));
        lines.clear();
    });
    while (true) {
        const auto r = process.readOutput(lines, "", std::chrono::milliseconds(0));
        lines.clear();
        if (r.code != Status::ERR) continue;
        // On Windows, the read gives up while fastchess' stop flag is set (a tournament ending): the engine goes on.
        bool alive = false;
        {
            std::lock_guard lock(impl_->io);
#ifdef _WIN64
            alive = static_cast<bool>(process.alive());
#else
            alive = r.message == "poll failed" && static_cast<bool>(process.alive());
#endif
        }
        if (!alive) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    {
        std::lock_guard lock(mutex_);
        ended_ = true;
    }
    cv_.notify_all();
}

bool EngineProcess::send(const std::string& line) {
    if (!running()) return false;
    std::lock_guard lock(impl_->io);
    // Written down before it is written: an engine quick to answer had its answer read, and shown, before the line it
    // answered (issue #175).
    push(Line::Kind::Sent, line);
    if (impl_->process.writeInput(line + "\n")) return true;
    // Not sent after all: taken back, unless already collected.
    std::lock_guard pending(mutex_);
    const auto it = std::find_if(pending_.rbegin(), pending_.rend(),
                                 [&](const Line& l) { return l.kind == Line::Kind::Sent && l.text == line; });
    if (it != pending_.rend()) pending_.erase(std::next(it).base());
    return false;
}

std::vector<EngineProcess::Line> EngineProcess::take() {
    std::lock_guard lock(mutex_);
    std::vector<Line> out(std::make_move_iterator(pending_.begin()), std::make_move_iterator(pending_.end()));
    pending_.clear();
    return out;
}

bool EngineProcess::wait(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    cv_.wait_for(lock, timeout, [this] { return !pending_.empty() || ended_; });
    return !pending_.empty();
}

bool EngineProcess::running() const {
    std::lock_guard lock(mutex_);
    return started_ && !ended_;
}

void EngineProcess::stop(std::chrono::milliseconds grace, const std::string& quit) {
    if (!started_) return;
    if (!quit.empty()) (void)send(quit);
    {
        std::unique_lock lock(mutex_);
        cv_.wait_for(lock, grace, [this] { return ended_; });
    }
    if (running()) {
        std::lock_guard lock(impl_->io);
#ifdef _WIN32
        // Its job ends what it started when the process is closed.
        impl_->process.forceKill();
#else
        // With what it started: the engine behind a wrapper script, or behind the WinBoard adapter.
        if (!killProcessTree(static_cast<int>(impl_->process.pid()))) impl_->process.forceKill();
#endif
    }
    if (reader_.joinable()) reader_.join();
    impl_.reset();  // reaps it
    std::lock_guard lock(mutex_);
    started_ = false;
}

int boardThreads() { return g_board_threads.load(); }

void CoreClaim::set(int threads) {
    threads = std::max(0, threads);
    g_board_threads += threads - threads_;
    threads_ = threads;
}

std::string clockTime(std::chrono::system_clock::time_point when) {
    const auto t  = std::chrono::system_clock::to_time_t(when);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(when.time_since_epoch()).count() % 1000;
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    return fmt::format("{:02}:{:02}:{:02}.{:03}", local.tm_hour, local.tm_min, local.tm_sec, ms);
}

std::string consoleTranscript(const std::vector<EngineProcess::Line>& log) {
    std::string out;
    for (const auto& line : log) {
        out += clockTime(line.when);
        switch (line.kind) {
            case EngineProcess::Line::Kind::Sent:
                out += " > ";
                break;
            case EngineProcess::Line::Kind::Err:
                out += " ! ";
                break;
            case EngineProcess::Line::Kind::Note:
                out += " # ";
                break;
            case EngineProcess::Line::Kind::Out:
                out += "   ";
                break;
        }
        out += line.text;
        out += '\n';
    }
    return out;
}

void rememberCommand(std::vector<std::string>& history, const std::string& command) {
    if (command.empty() || (!history.empty() && history.back() == command)) return;
    history.push_back(command);
    if (history.size() > 200) history.erase(history.begin());
}

}  // namespace fcg
