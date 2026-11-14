#pragma once

// The texts of the Board page: what a pasted text holds (a FEN, a PGN, a bare list of moves), a game read from a PGN,
// what is wrong with a position, and a game written back as PGN.

#include <optional>
#include <string>
#include <string_view>

#include "pgn_store.hpp"

namespace fcg {

// What a text holds, as the Board page reads it.
struct BoardText {
    enum class Kind { Nothing, Position, Games };
    Kind kind = Kind::Nothing;
    std::string fen;  // Position: the position, as the board writes it
    bool chess960 = false;
    std::string pgn;    // Games: one or more games, as PGN (a bare list of moves made one)
    std::string error;  // Nothing: why
};

// A FEN (or the four fields of an EPD line), a PGN of one or more games, a list of moves in SAN with or without
// numbers ("1. e4 e5 2. Nf3"), or in UCI ("e2e4 e7e5"), from the initial position.
[[nodiscard]] BoardText readBoardText(std::string_view text);

// What is wrong with a position for a game to be played from it: a FEN the board cannot read, not one king of each
// colour, a pawn on the first or last rank, the side not to move in check; empty when nothing is.
[[nodiscard]] std::string positionProblem(const std::string& fen, bool chess960);

// Whether a position needs Chess960 castling: castling rights in file letters (Shredder-FEN), or rights whose king
// or rook is not on its usual square.
[[nodiscard]] bool needsChess960(const std::string& fen);

// One game of a PGN (the first, when it holds more): its moves up to the first that cannot be read. nullopt, with the
// reason in `error`, when it has no start position to play from.
[[nodiscard]] std::optional<ReplayGame> readPgnGame(const std::string& pgn, std::string& error);

// Whether a game is played from Chess960 positions (its Variant tag).
[[nodiscard]] bool gameIsChess960(const ReplayGame& game);

// The game as PGN: its tags (the Seven Tag Roster first, SetUp and FEN when it starts from another position), its
// moves in SAN, each with the engine's score, depth and time as a comment in fastchess' style when it has them.
[[nodiscard]] std::string gamePgn(const ReplayGame& game, bool chess960);

}  // namespace fcg
