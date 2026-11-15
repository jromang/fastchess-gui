#pragma once

// The piece sets built into the program: gui/assets/pieces/<set>/*.svg, embedded by cmake/embed_pieces.cmake. Their
// authors and licenses are in gui/assets/ATTRIBUTION.md.

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace fcg::assets {

struct PieceSetSvgs {
    std::string_view id;                    // its folder's name
    std::array<std::string_view, 12> svgs;  // white K Q R B N P, then black K Q R B N P
};

// The default set first.
extern const PieceSetSvgs piece_sets[];
extern const std::size_t piece_set_count;

[[nodiscard]] inline std::span<const PieceSetSvgs> pieceSets() { return {piece_sets, piece_set_count}; }

}  // namespace fcg::assets
