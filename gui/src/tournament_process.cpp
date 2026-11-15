#include "tournament_process.hpp"

#include <chrono>

namespace fcg {

namespace {

// A started process's pipes as a link.
class PipeLink final : public Link {
   public:
    explicit PipeLink(ChildProcess& child) : child_(child) {}
    long read(char* buffer, std::size_t size) override { return child_.read(buffer, size); }
    bool write(const std::string& data) override { return child_.write(data); }
    // Its input closed, the process stops and closes its output, which ends the read: a pipe's read waiting on
    // another thread does not return when its end is closed.
    void close() override { child_.closeInput(); }

   private:
    ChildProcess& child_;
};

}  // namespace

TournamentProcess::~TournamentProcess() {
    // A process started here with pipes reads the end of its input: it stops, resumable (and is reaped below).
    disconnect();
    if (child_) (void)child_->running();
}

bool TournamentProcess::start(const std::string& program, const std::vector<std::string>& args, std::string& error) {
    child_ = std::make_unique<ChildProcess>();
    if (!child_->start(program, args, error)) {
        child_.reset();
        return false;
    }
    link_   = std::make_unique<PipeLink>(*child_);
    reader_ = std::thread([this] { readLoop(); });
    return true;
}

bool TournamentProcess::spawnDetached(const std::string& program, const std::vector<std::string>& args,
                                      std::string& error) {
    child_ = std::make_unique<ChildProcess>();
    if (!child_->start(program, args, error, true)) {
        child_.reset();
        return false;
    }
    // Its standard input and output are not the channel: closed here, it reads its end, as a detached process.
    child_->closeInput();
    child_->closeOutput();
    detached_ = true;
    return true;
}

bool TournamentProcess::childRunning() { return child_ && child_->running(); }

void TournamentProcess::attach(std::unique_ptr<Link> link) {
    if (reader_.joinable()) reader_.join();  // the connection before, ended
    link_   = std::move(link);
    closed_ = false;
    reader_ = std::thread([this] { readLoop(); });
}

void TournamentProcess::readLoop() {
    control::LineSplitter split;
    std::vector<std::string> lines;
    std::vector<nlohmann::json> parsed;
    char buffer[65536];
    for (;;) {
        const long n = link_->read(buffer, sizeof buffer);
        if (n <= 0) break;
        lines.clear();
        split.feed(std::string_view(buffer, static_cast<std::size_t>(n)), lines);
        parsed.clear();
        for (const auto& line : lines) {
            if (line.empty()) continue;
            try {
                auto j = nlohmann::json::parse(line);
                if (j.is_object()) parsed.push_back(std::move(j));
            } catch (const nlohmann::json::exception&) {
                // Not a message (a stray line): ignored.
            }
        }
        if (parsed.empty()) continue;
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& j : parsed) messages_.push_back(std::move(j));
    }
    closed_ = true;
}

std::vector<nlohmann::json> TournamentProcess::take() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::exchange(messages_, {});
}

bool TournamentProcess::send(const nlohmann::json& message) {
    if (!link_ || closed_) return false;
    return link_->write(message.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "\n");
}

std::optional<int> TournamentProcess::exitCode() {
    if (!child_ || child_->running()) return std::nullopt;
    return child_->wait();
}

void TournamentProcess::disconnect() {
    if (link_) link_->close();
    if (child_ && !detached_ && reader_.joinable()) {
        // A process started with pipes stops when its input closes: it is given time to stop its engines, then ended.
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (!closed_ && std::chrono::steady_clock::now() < until)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (!closed_) child_->kill();
    }
    if (reader_.joinable()) reader_.join();
    closed_ = true;
}

void TournamentProcess::kill() {
    if (child_) child_->kill();
}

}  // namespace fcg
