#pragma once

// The opening book of a tournament in its playing order: read once with fastchess' readers, shuffled with the
// tournament seed (independently of fastchess' global generator), so the order is the same every time the
// tournament is opened.

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include <game/book/epd_reader.hpp>
#include <game/book/opening.hpp>
#include <game/book/pgn_reader.hpp>
#include <types/opening.hpp>

#include "model.hpp"

namespace fcg::tour {

class OpeningSource {
   public:
    OpeningSource() = default;  // no book: every game starts from the initial position

    // Throws when the book cannot be read.
    OpeningSource(const fastchess::config::Opening& book, bool frc, uint64_t seed);

    [[nodiscard]] std::size_t size() const noexcept;

    // The opening of encounter `index` (modulo the book size).
    [[nodiscard]] fastchess::book::Opening at(std::size_t index) const;

   private:
    std::variant<std::monostate, fastchess::book::EpdReader, fastchess::book::PgnReader> book_;
};

// An opening as text (FEN and UCI moves), for the journal.
struct OpeningText {
    std::string fen;
    std::vector<std::string> moves;
};

[[nodiscard]] OpeningText toText(const fastchess::book::Opening& opening, bool frc);
[[nodiscard]] fastchess::book::Opening fromText(const OpeningText& text, bool frc);

}  // namespace fcg::tour
