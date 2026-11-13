#include "engine_log.hpp"

#include <algorithm>

#include <fmt/include/fmt/format.h>

#include <matchmaking/game_events.hpp>

namespace fcg::tour {

void EngineLog::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    start_ = std::chrono::steady_clock::now();
    for (auto& head : heads_) head.clear();
    tail_.clear();
    added_ = 0;
}

void EngineLog::add(chess::Color side, Stream stream, std::string_view line) {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    const double seconds = std::chrono::duration<double>(now - start_).count();
    const char mark      = stream == Stream::Input ? '>' : stream == Stream::Output ? '<' : '!';
    auto text            = fmt::format("{:9.3f} {}{} {}", seconds, side == chess::Color::WHITE ? 'W' : 'B', mark, line);

    Line entry{added_++, std::move(text)};
    auto& head = heads_[side == chess::Color::WHITE ? 0 : 1];
    if (head.size() < HEAD_LINES) {
        head.push_back(std::move(entry));
        return;
    }
    tail_.push_back(std::move(entry));
    if (tail_.size() > TAIL_LINES) tail_.pop_front();
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
    out +=
        "Seconds since the game started; W/B: White's or Black's engine; > sent to it, < read from its output, "
        "! read from its error output.\n\n";

    std::lock_guard<std::mutex> lock(mutex_);
    // The two heads and the tail, each in the order of its lines, merged into the order of the game. The last line
    // added is always kept, so the dropped lines are gaps between kept ones.
    std::vector<const Line*> lines;
    lines.reserve(heads_[0].size() + heads_[1].size() + tail_.size());
    for (const auto& head : heads_) {
        for (const auto& line : head) lines.push_back(&line);
    }
    for (const auto& line : tail_) lines.push_back(&line);
    std::sort(lines.begin(), lines.end(), [](const Line* a, const Line* b) { return a->number < b->number; });

    uint64_t next = 0;
    for (const auto* line : lines) {
        if (const auto dropped = line->number - next; dropped > 0) {
            out += fmt::format("          ... {} line{} left out ...\n", dropped, dropped == 1 ? "" : "s");
        }
        out += line->text;
        out += '\n';
        next = line->number + 1;
    }
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
