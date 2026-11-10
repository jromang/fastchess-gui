#include "engine_log.hpp"

#include <fmt/include/fmt/format.h>

#include <matchmaking/game_events.hpp>

namespace fcg::tour {

void EngineLog::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    start_ = std::chrono::steady_clock::now();
    head_.clear();
    tail_.clear();
    dropped_ = 0;
}

void EngineLog::add(chess::Color side, bool sent, std::string_view line) {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    const double seconds = std::chrono::duration<double>(now - start_).count();
    auto text =
        fmt::format("{:9.3f} {}{} {}", seconds, side == chess::Color::WHITE ? 'W' : 'B', sent ? '>' : '<', line);

    if (head_.size() < HEAD_LINES) {
        head_.push_back(std::move(text));
        return;
    }
    tail_.push_back(std::move(text));
    if (tail_.size() > TAIL_LINES) {
        tail_.pop_front();
        ++dropped_;
    }
}

std::string EngineLog::text(uint64_t game_number, const fastchess::MatchData& data) const {
    const auto& white = data.players.white;
    const auto& black = data.players.black;
    const auto engine = [](const fastchess::MatchData::PlayerInfo& p) {
        return fmt::format("{} ({}{}{})", p.config.name, p.config.cmd, p.config.args.empty() ? "" : " ", p.config.args);
    };

    std::string out = fmt::format("fastchess-gui engine log, game {}\n", game_number);
    out += fmt::format("White: {}\nBlack: {}\n", engine(white), engine(black));
    out += fmt::format("Start: {}  Duration: {}\n", data.start_time, data.duration);
    out += fmt::format("Position: {}\n", data.fen);
    out += fmt::format("End: {} ({})\n", data.reason, fastchess::events::terminationName(data.termination));
    out += "Seconds since the game started; W/B: White's or Black's engine; > sent to it, < read from it.\n\n";

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& line : head_) out += line + '\n';
    if (dropped_ > 0) out += fmt::format("          ... {} lines left out ...\n", dropped_);
    for (const auto& line : tail_) out += line + '\n';
    return out;
}

bool endsBadly(fastchess::MatchTermination termination) noexcept {
    using fastchess::MatchTermination;
    return termination == MatchTermination::TIMEOUT || termination == MatchTermination::DISCONNECT ||
           termination == MatchTermination::STALL || termination == MatchTermination::ILLEGAL_MOVE;
}

std::filesystem::path engineLogPath(const std::filesystem::path& tournament_dir, uint64_t game_number) {
    return tournament_dir / "logs" / fmt::format("game-{}.log", game_number);
}

}  // namespace fcg::tour
