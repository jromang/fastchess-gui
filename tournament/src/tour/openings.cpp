#include "openings.hpp"

#include <algorithm>
#include <filesystem>
#include <random>
#include <stdexcept>

#include <core/crc32.hpp>

namespace fcg::tour {

namespace {

// Fisher-Yates, as fastchess' Openings::shuffle, on a generator of our own.
template <typename T>
void shuffle(std::vector<T>& v, uint64_t seed) {
    std::mt19937_64 rng(seed);
    for (std::size_t i = 0; i + 2 <= v.size(); ++i) {
        const std::size_t j = i + static_cast<std::size_t>(rng() % (v.size() - i));
        std::swap(v[i], v[j]);
    }
}

template <typename T>
void arrange(std::vector<T>& v, const fastchess::config::Opening& book, uint64_t seed) {
    if (v.empty()) throw std::runtime_error("the opening book is empty: " + book.file);
    if (book.order == fastchess::OrderType::RANDOM) shuffle(v, seed);
    const auto offset = static_cast<std::size_t>(std::max(1, book.start) - 1) % v.size();
    std::rotate(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(offset), v.end());
}

}  // namespace

OpeningSource::OpeningSource(const fastchess::config::Opening& book, bool frc, uint64_t seed) {
    if (book.file.empty() || book.format == fastchess::FormatType::NONE) return;
    if (!std::filesystem::is_regular_file(book.file)) throw std::runtime_error("opening book not found: " + book.file);

    if (book.format == fastchess::FormatType::PGN) {
        auto& reader = book_.emplace<fastchess::book::PgnReader>(book.file, book.plies, frc);
        arrange(reader.get(), book, seed);
    } else {
        auto& reader = book_.emplace<fastchess::book::EpdReader>(book.file);
        arrange(reader.get(), book, seed);
    }
}

std::size_t OpeningSource::size() const noexcept {
    if (const auto* epd = std::get_if<fastchess::book::EpdReader>(&book_)) return epd->get().size();
    if (const auto* pgn = std::get_if<fastchess::book::PgnReader>(&book_)) return pgn->get().size();
    return 0;
}

fastchess::book::Opening OpeningSource::at(std::size_t index) const {
    const auto n = size();
    if (n == 0) return {};

    if (const auto* epd = std::get_if<fastchess::book::EpdReader>(&book_)) {
        return {std::string(epd->get()[index % n].view()), {}};
    }
    return std::get<fastchess::book::PgnReader>(book_).get()[index % n];
}

BookFingerprint fingerprint(const std::string& file) {
    BookFingerprint fp;
    std::error_code ec;
    if (file.empty() || !std::filesystem::is_regular_file(file, ec)) return fp;

    fp.size  = std::filesystem::file_size(file, ec);
    fp.crc32 = fastchess::crc::calculate_crc32(file).value_or(0);
    return fp;
}

OpeningText toText(const fastchess::book::Opening& opening, bool frc) {
    OpeningText text;
    text.fen = opening.fen_epd;
    for (const auto& move : opening.moves) text.moves.push_back(chess::uci::moveToUci(move, frc));
    return text;
}

fastchess::book::Opening fromText(const OpeningText& text, bool frc) {
    chess::Board board;
    board.set960(frc);
    board.setFen(text.fen);

    fastchess::book::Opening opening;
    opening.fen_epd = text.fen;
    for (const auto& uci : text.moves) {
        const auto move = chess::uci::uciToMove(board, uci);
        if (move == chess::Move::NO_MOVE) throw std::runtime_error("illegal opening move in the journal: " + uci);
        opening.moves.push_back(move);
        board.makeMove<true>(move);
    }
    return opening;
}

}  // namespace fcg::tour
