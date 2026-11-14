#pragma once

// An opening book read for inspection: every opening, the position it gives the engines, and what is wrong with
// it (an illegal position, a duplicate, the same position with colours reversed, a game already over). Read with
// fastchess' own readers, so the openings are exactly those a tournament plays.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

#include <game/book/epd_reader.hpp>
#include <game/book/pgn_reader.hpp>

namespace fcg::tour {

enum BookFlag : uint8_t {
    BOOK_ILLEGAL   = 1,  // not a legal position: kings, pawns on the last ranks, the side not to move in check
    BOOK_DUPLICATE = 2,  // the same position as an earlier opening
    BOOK_MIRRORED  = 4,  // an earlier opening with colours reversed
    BOOK_OVER      = 8,  // no legal move, or not enough material to mate
};

struct BookItem {
    std::string fen;               // the position the engines start from
    std::vector<std::string> san;  // the book moves leading to it (PGN), in SAN
    uint8_t flags = 0;
};

class BookScan {
   public:
    enum class Kind { Epd, Pgn, Polyglot };

    BookScan() = default;

    // Reads `file` (.epd, .pgn, or a Polyglot .bin followed to `plies`); `plies` also cuts the games of a PGN book
    // (-1: all of them, 8 for Polyglot). `done` counts the openings checked, for a progress display; the scan stops
    // early, incomplete, when `cancel` is set. Throws std::runtime_error with a readable message.
    BookScan(const std::filesystem::path& file, int plies, const std::atomic<bool>* cancel = nullptr,
             std::atomic<std::size_t>* done = nullptr);

    [[nodiscard]] Kind kind() const noexcept { return kind_; }
    [[nodiscard]] bool chess960() const noexcept { return chess960_; }
    [[nodiscard]] std::size_t size() const noexcept { return flags_.size(); }
    [[nodiscard]] uint8_t flags(std::size_t i) const { return flags_.at(i); }
    // How many openings have any of `flag`; the openings fastchess' readers leave out, their position invalid, count
    // as BOOK_ILLEGAL (they are not among the openings, nor in size()).
    [[nodiscard]] std::size_t count(uint8_t flag) const;

    // The escape lines of a PGN book (a '%' in the first column): ignored, as the PGN standard says, but read as
    // moves by fastchess-gui 0.4.13 and earlier, which could then play some of its openings from a wrong position.
    [[nodiscard]] std::size_t escapeLines() const noexcept { return escape_lines_; }

    // Opening `i`, in the book's order.
    [[nodiscard]] BookItem item(std::size_t i) const;

    // Writes the positions of the openings without any of `drop` flags, one EPD line each. Returns how many.
    std::size_t writeEpd(const std::filesystem::path& file, uint8_t drop) const;

   private:
    [[nodiscard]] std::string position(std::size_t i) const;  // the starting position, FEN

    Kind kind_                = Kind::Epd;
    bool chess960_            = false;
    std::size_t escape_lines_ = 0;
    std::size_t left_out_     = 0;  // by fastchess' reader, the position invalid
    std::variant<fastchess::book::EpdReader, fastchess::book::PgnReader, std::vector<std::string>> book_;
    std::vector<uint8_t> flags_;
};

}  // namespace fcg::tour
