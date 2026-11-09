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

#include <tour/pgn_file.hpp>

namespace fcg {

// The PGN files are written by the tournament layer.
using tour::PgnSpan;

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
