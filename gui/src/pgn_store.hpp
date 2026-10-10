#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <chess.hpp>
#include <types/score.hpp>

namespace fcg {

// A byte range inside a PGN file.
struct PgnSpan {
    uint64_t offset = 0;
    uint32_t length = 0;
};

// Append-only PGN file shared by the tournament threads. Remembers nothing itself:
// append() returns where the game went, and the caller keeps that.
class PgnFile {
   public:
    explicit PgnFile(std::filesystem::path path);

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    // Thread safe.
    PgnSpan append(const std::string& pgn);

    // Reads one game back.
    [[nodiscard]] std::optional<std::string> read(const PgnSpan& span) const;

   private:
    std::filesystem::path path_;
    std::ofstream out_;
    uint64_t size_ = 0;
    mutable std::mutex mutex_;
};

// Copies the given games, in order, into a new file. Returns an error message, empty on success.
std::string exportSpans(const std::filesystem::path& source, const std::vector<PgnSpan>& spans,
                        const std::filesystem::path& target);

// A game read back from a PGN written in the fastchess comment style.
struct ReplayMove {
    std::string san;
    chess::Move move;
    chess::Color mover = chess::Color::WHITE;
    bool book          = false;

    std::optional<fastchess::Score> score;  // from the mover's point of view
    int depth       = 0;
    int64_t time_ms = -1;
};

struct ReplayGame {
    std::vector<std::pair<std::string, std::string>> headers;
    std::string start_fen;
    std::vector<ReplayMove> moves;
    std::string result;
    std::string final_comment;

    [[nodiscard]] std::string header(const std::string& key) const;
};

[[nodiscard]] std::optional<ReplayGame> parseReplay(const std::string& pgn);

// "{+0.35/18 1.234s, ...}" (the body without braces) into score, depth and time.
void parseFastchessComment(std::string_view comment, ReplayMove& move);

}  // namespace fcg
