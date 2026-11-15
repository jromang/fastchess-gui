#include "control_child.hpp"

#include <cstdio>
#include <cstring>
#include <future>
#include <fstream>

#include <fcntl.h>

#ifdef _WIN32
#    include <io.h>
#    include <windows.h>
#else
#    include <unistd.h>
#endif

#include <tour/files.hpp>

namespace fcg {

namespace {

constexpr auto SEARCH_EVERY = std::chrono::milliseconds(250);

int colorIndex(chess::Color c) { return c == chess::Color::WHITE ? 0 : 1; }

// A search line worth showing: "info ... score ...", not "info string".
bool searchLine(std::string_view line) {
    return line.size() >= 12 && line.compare(0, 5, "info ") == 0 && line.compare(5, 7, "string ") != 0 &&
           line.find(" score ") != std::string_view::npos;
}

}  // namespace

std::optional<tour::Allowance> parseAllowance(const std::string& text) {
    tour::Allowance a;
    long long t = 0, m = 0, one = 1;
    int g = 0;
    if (std::sscanf(text.c_str(), "%lld:%lld:%d:%lld", &t, &m, &g, &one) != 4) return std::nullopt;
    a.threads         = static_cast<int>(t);
    a.memory_mb       = m;
    a.gpus            = g;
    a.one_game_always = one != 0;
    return a;
}

std::string allowanceArg(const tour::Allowance& a) {
    return std::to_string(a.threads) + ":" + std::to_string(a.memory_mb) + ":" + std::to_string(a.gpus) + ":" +
           (a.one_game_always ? "1" : "0");
}

// --- channel
// -----------------------------------------------------------------------------------------------------------

void ChildChannel::send(const char* kind, nlohmann::json fields) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    sendLocked(kind, std::move(fields));
}

void ChildChannel::sendLocked(const char* kind, nlohmann::json fields) {
    if (!fields.is_object()) fields = nlohmann::json::object();
    fields["seq"] = ++seq_;
    fields["e"]   = kind;
    (void)write(fields.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "\n");
}

void ChildChannel::hello(const control::Hello& hello) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    helloLocked(hello);
    setConnected();
}

void ChildChannel::helloLocked(const control::Hello& hello) {
    (void)write(control::helloJson(hello).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "\n");
}

std::vector<nlohmann::json> ChildChannel::takeCommands() {
    std::lock_guard<std::mutex> lock(commands_mutex_);
    return std::exchange(commands_, {});
}

void ChildChannel::received(const std::string& line) {
    if (line.empty()) return;
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(line);
    } catch (const nlohmann::json::exception&) {
        return;  // not a message: ignored
    }
    if (!j.is_object()) return;
    std::lock_guard<std::mutex> lock(commands_mutex_);
    commands_.push_back(std::move(j));
}

StdioChannel::StdioChannel(const std::string& log) {
#ifdef _WIN32
    _setmode(0, _O_BINARY);
    _setmode(1, _O_BINARY);
    out_ = _dup(1);
    in_  = _dup(0);
    // Stray writes to the standard output and error go to the log, not into the channel.
    const int log_fd = _open(log.c_str(), _O_WRONLY | _O_CREAT | _O_APPEND | _O_BINARY, 0600);
    if (log_fd >= 0) {
        _dup2(log_fd, 1);
        _dup2(log_fd, 2);
        _close(log_fd);
    }
    // The engines started from now on must not inherit the channel's handles: the window would not see the end of a
    // tournament whose engine is still alive.
    SetHandleInformation(reinterpret_cast<HANDLE>(_get_osfhandle(out_)), HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(reinterpret_cast<HANDLE>(_get_osfhandle(in_)), HANDLE_FLAG_INHERIT, 0);
#else
    out_             = ::fcntl(1, F_DUPFD_CLOEXEC, 3);
    in_              = ::fcntl(0, F_DUPFD_CLOEXEC, 3);
    const int log_fd = ::open(log.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (log_fd >= 0) {
        ::dup2(log_fd, 1);
        ::dup2(log_fd, 2);
        ::close(log_fd);
    }
    const int null_fd = ::open("/dev/null", O_RDONLY);
    if (null_fd >= 0) {
        ::dup2(null_fd, 0);
        ::close(null_fd);
    }
#endif
    link_->channel = this;
    std::thread([in = in_, link = link_] { readLoop(in, link); }).detach();
}

StdioChannel::~StdioChannel() {
    // The reader may still be blocked on the window's pipe: it ends with the process, and no longer reaches this.
    {
        std::lock_guard<std::mutex> lock(link_->mutex);
        link_->channel = nullptr;
    }
#ifdef _WIN32
    if (out_ >= 0) _close(out_);
#else
    if (out_ >= 0) ::close(out_);
#endif
}

bool StdioChannel::write(const std::string& line) {
    if (closed() || out_ < 0) return false;
    const char* data = line.data();
    std::size_t left = line.size();
    while (left > 0) {
#ifdef _WIN32
        const int n = _write(out_, data, static_cast<unsigned>(std::min<std::size_t>(left, 1 << 20)));
#else
        const auto n = ::write(out_, data, left);
        if (n < 0 && errno == EINTR) continue;
#endif
        if (n <= 0) {
            setClosed();
            return false;
        }
        data += n;
        left -= static_cast<std::size_t>(n);
    }
    return true;
}

void StdioChannel::readLoop(int in, const std::shared_ptr<ReaderLink>& link) {
    control::LineSplitter split;
    std::vector<std::string> lines;
    char buffer[65536];
    for (;;) {
#ifdef _WIN32
        const int n = _read(in, buffer, sizeof buffer);
#else
        const auto n = ::read(in, buffer, sizeof buffer);
        if (n < 0 && errno == EINTR) continue;
#endif
        if (n <= 0) break;
        lines.clear();
        split.feed(std::string_view(buffer, static_cast<std::size_t>(n)), lines);
        std::lock_guard<std::mutex> lock(link->mutex);
        if (!link->channel) return;
        for (const auto& line : lines) link->channel->received(line);
    }
    std::lock_guard<std::mutex> lock(link->mutex);
    if (link->channel) link->channel->setClosed();
}

SocketChannel::SocketChannel(const std::string& log, std::filesystem::path dir, control::Hello hello)
    : dir_(std::move(dir)), hello_(std::move(hello)) {
    // As the standard channel: stray writes to the standard output and error go to the log.
#ifdef _WIN32
    const int log_fd = _open(log.c_str(), _O_WRONLY | _O_CREAT | _O_APPEND | _O_BINARY, 0600);
    if (log_fd >= 0) {
        _dup2(log_fd, 1);
        _dup2(log_fd, 2);
        _close(log_fd);
    }
#else
    const int log_fd = ::open(log.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (log_fd >= 0) {
        ::dup2(log_fd, 1);
        ::dup2(log_fd, 2);
        ::close(log_fd);
    }
    const int null_fd = ::open("/dev/null", O_RDONLY);
    if (null_fd >= 0) {
        ::dup2(null_fd, 0);
        ::close(null_fd);
    }
#endif
}

SocketChannel::~SocketChannel() {
    // control.json first: a window that sees the connection end finds nothing to connect to again.
    local::removeControlFile(dir_);
    server_.close();
    std::shared_ptr<Link> client;
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        client = client_;
    }
    if (client) {
        // Its last lines (the tournament's end) read before the connection goes, two seconds at most: a named pipe let
        // go at once drops what its window has not read yet, and a window that reads no more must not keep the process.
        auto done     = std::make_shared<std::promise<void>>();
        auto finished = done->get_future();
        std::thread([client, done] {
            client->finish();
            done->set_value();
        }).detach();
        if (finished.wait_for(std::chrono::seconds(2)) != std::future_status::ready) client->close();
    }
    if (accept_thread_.joinable()) accept_thread_.join();
}

bool SocketChannel::listen(std::string& error) {
    const auto endpoint = local::endpointFor(dir_);
    if (!server_.listen(endpoint, error)) return false;
    local::ControlFile file;
    file.endpoint = endpoint;
    file.pid      = hello_.pid;
    file.version  = hello_.version;
    if (!local::writeControlFile(dir_, file)) {
        error = "cannot write " + local::controlFilePath(dir_).string();
        server_.close();
        return false;
    }
    accept_thread_ = std::thread([this] { acceptLoop(); });
    return true;
}

bool SocketChannel::write(const std::string& line) {
    if (!client_ || !ready_) return false;
    if (client_->write(line)) return true;
    client_.reset();  // gone: the events wait for the next window
    ready_ = false;
    return false;
}

void SocketChannel::replayJournal(uint64_t from) {
    std::ifstream in(dir_ / "journal.jsonl", std::ios::binary);
    if (!in || !in.seekg(static_cast<std::streamoff>(from))) return;
    uint64_t at = from;
    for (std::string line; std::getline(in, line);) {
        if (in.eof()) break;  // a last line without its end: not written yet, it comes as an event
        const uint64_t end = at + line.size() + 1;
        try {
            if (line.find_first_not_of(" \t\r") != std::string::npos)
                sendLocked("journal", {{"at", at}, {"end", end}, {"event", nlohmann::json::parse(line)}});
        } catch (const nlohmann::json::exception&) {
            // A damaged line: the window's copy reads the files again when it misses one.
        }
        at = end;
    }
}

void SocketChannel::acceptLoop() {
    while (auto link = server_.accept()) {
        std::shared_ptr<Link> client(std::move(link));
        {
            // One window at a time: another one is told, and let go.
            std::lock_guard<std::mutex> lock(write_mutex_);
            if (client_) {
                (void)client->write("{\"busy\":true}\n");
                // Let go once it has the line, on a thread of its own: it may take as long as that window reads.
                std::thread([client] { client->finish(); }).detach();
                continue;
            }
            client_ = client;  // taken, though its hello is still to come: no event goes to it before
            ready_  = false;
        }
        if (serving_.joinable()) serving_.join();  // the window before, gone
        serving_ = std::thread([this, client] { serve(client); });
    }
    if (serving_.joinable()) serving_.join();
}

void SocketChannel::serve(std::shared_ptr<Link> client) {
    // Its first line: its hello, with where its copy's journal ends; else a command (a script through --attach), and
    // no journal line is sent again.
    control::LineSplitter split;
    std::vector<std::string> lines;
    char buffer[65536];
    while (lines.empty()) {
        const long n = client->read(buffer, sizeof buffer);
        if (n <= 0) break;
        split.feed(std::string_view(buffer, static_cast<std::size_t>(n)), lines);
    }
    if (!lines.empty()) {
        std::optional<uint64_t> from;
        try {
            if (const auto hello = control::parseHello(nlohmann::json::parse(lines.front()))) from = hello->journal;
        } catch (const nlohmann::json::exception&) {
        }
        {
            std::lock_guard<std::mutex> lock(write_mutex_);
            ready_     = true;
            auto hello = hello_;
            hello.playing_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_)
                    .count();
            helloLocked(hello);
            if (from) replayJournal(*from);
        }
        for (std::size_t i = from ? 1 : 0; i < lines.size(); ++i) received(lines[i]);
        setConnected();

        // Its commands, until it goes away.
        for (;;) {
            const long n = client->read(buffer, sizeof buffer);
            if (n <= 0) break;
            lines.clear();
            split.feed(std::string_view(buffer, static_cast<std::size_t>(n)), lines);
            for (const auto& line : lines) received(line);
        }
    }
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (client_ == client) {
        client_.reset();
        ready_ = false;
    }
}

// --- observer
// ----------------------------------------------------------------------------------------------------------

ControlObserver::ControlObserver(ChildChannel& channel, const tour::TournamentState& state)
    : channel_(channel),
      frc_(state.spec().conditions.variant == fastchess::VariantType::FRC),
      meter_(state.spec().conditions.concurrency, state.dir() / "processor.tsv"),
      live_(state.spec().conditions.concurrency, state, false),
      searches_(static_cast<std::size_t>(std::max(1, state.spec().conditions.concurrency))) {}

void ControlObserver::onGameStart(const fastchess::observer::GameStart& info) {
    meter_.gameStart(info.slot);
    live_.onGameStart(info);
    {
        std::lock_guard<std::mutex> lock(search_mutex_);
        if (info.slot >= 0 && info.slot < static_cast<int>(searches_.size())) searches_[info.slot] = {};
    }
    channel_.send("game_start", control::gameStartJson(info));
}

void ControlObserver::onEngineProcesses(int slot, int64_t white_pid, int64_t black_pid) {
    meter_.engineProcesses(slot, white_pid, black_pid);
    live_.onEngineProcesses(slot, white_pid, black_pid);
}

void ControlObserver::onThinking(int slot, chess::Color stm, int64_t time_left_ms) {
    meter_.thinking(slot);
    live_.onThinking(slot, stm, time_left_ms);
    channel_.send("thinking", {{"slot", slot}, {"stm", colorIndex(stm)}, {"left", time_left_ms}});
}

void ControlObserver::onEngineLine(int slot, chess::Color side, std::string_view line) {
    if (!searchLine(line)) return;
    live_.onEngineLine(slot, side, line);
    std::unique_lock<std::mutex> lock(search_mutex_);
    if (slot < 0 || slot >= static_cast<int>(searches_.size())) return;
    auto& s = searches_[slot][colorIndex(side)];
    s.line.assign(line.data(), line.size());
    // A few updates per second: an engine prints thousands of lines.
    if (Clock::now() - s.sent >= SEARCH_EVERY) sendSearch(slot, colorIndex(side), lock);
}

void ControlObserver::sendSearch(int slot, int side, std::unique_lock<std::mutex>& lock) {
    auto& s = searches_[slot][side];
    if (s.line.empty()) return;
    auto line = std::move(s.line);
    s.line.clear();
    s.sent = Clock::now();
    lock.unlock();
    channel_.send("info", {{"slot", slot}, {"side", side}, {"line", line}});
    lock.lock();
}

void ControlObserver::flush() {
    std::unique_lock<std::mutex> lock(search_mutex_);
    const auto now = Clock::now();
    for (int slot = 0; slot < static_cast<int>(searches_.size()); ++slot) {
        for (int side = 0; side < 2; ++side) {
            if (!searches_[slot][side].line.empty() && now - searches_[slot][side].sent >= SEARCH_EVERY)
                sendSearch(slot, side, lock);
        }
    }
}

void ControlObserver::sendSlots() {
    for (int i = 0; i < live_.slotCount(); ++i) {
        SlotState slot;
        if (!live_.copySlot(i, slot) || !slot.has_game) continue;
        auto j    = control::slotJson(slot, frc_);
        j["slot"] = i;
        channel_.send("slot", std::move(j));
    }
}

void ControlObserver::onMove(int slot, chess::Color mover, const fastchess::MoveData& move, const std::string& san,
                             int64_t white_time_left, int64_t black_time_left) {
    const auto used = meter_.moved(slot, mover);
    live_.onMove(slot, mover, move, san, white_time_left, black_time_left);
    if (used) live_.addCpu(slot, mover, *used);
    {
        // The search is over: what was held back of it is old news.
        std::lock_guard<std::mutex> lock(search_mutex_);
        if (slot >= 0 && slot < static_cast<int>(searches_.size())) searches_[slot][colorIndex(mover)].line.clear();
    }
    nlohmann::json j = {{"slot", slot}, {"mover", colorIndex(mover)}, {"data", control::moveDataJson(move)},
                        {"san", san},   {"wtime", white_time_left},   {"btime", black_time_left}};
    if (used) j["cpu"] = {used->cpu_ms[0], used->cpu_ms[1], used->wall_ms};
    channel_.send("move", std::move(j));
}

void ControlObserver::onGameEnd(int slot, std::size_t game_id, std::size_t round_id, const fastchess::MatchData& data,
                                bool counted) {
    // Not live_.onGameEnd(): its own meter (none) would forget what this one keeps of the game for onRecorded().
    (void)game_id;
    (void)round_id;
    meter_.gameEnd(slot, counted);
    const std::string result = counted ? fastchess::pgn::PgnBuilder::getResultFromWhiteMatch(data.players.white) : "*";
    live_.gameEnded(slot, counted, result, data.reason);
    channel_.send("game_end", {{"slot", slot}, {"counted", counted}, {"result", result}, {"reason", data.reason}});
}

void ControlObserver::onRecorded(const tour::GameResult& result, const fastchess::MatchData& data,
                                 const std::optional<fastchess::Stats>& pair) {
    // processor.tsv first: the window reads a game's line there when it opens the tournament.
    meter_.recorded(result);
    const auto side = sideTotals(data);
    live_.recorded(result, side, pair);
    nlohmann::json j = {{"result", tour::gameResultJson(result)},
                        {"side", {control::totalsJson(side[0]), control::totalsJson(side[1])}}};
    if (pair) j["pair"] = control::statsJson(*pair);
    channel_.send("recorded", std::move(j));
}

void ControlObserver::onStartFailure(const tour::StartFailureEvent& event) {
    nlohmann::json engines = nlohmann::json::array();
    for (const auto& e : event.engines) engines.push_back(control::startFailureJson(e));
    channel_.send("start_failure", {{"kind", static_cast<int>(event.kind)}, {"engines", engines}});
}

}  // namespace fcg
