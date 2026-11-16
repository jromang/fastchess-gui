#pragma once

// The 3D pieces, as tools/board3d/bake.cpp packed them (gui/assets/board3d/pieces.bin): six meshes in squares, the
// base of each centred on the origin, y up, the knight looking towards -z.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fcg::board3d {

enum class Kind : int { Pawn, Knight, Bishop, Rook, Queen, King };

struct PieceMesh {
    std::vector<float> positions;  // x, y, z per vertex
    std::vector<float> normals;    // x, y, z per vertex, unit
    std::vector<std::uint32_t> indices;
    float height = 0;  // top of the piece, in squares
    float radius = 0;  // the farthest vertex from the vertical axis
};

// The six pieces in the order of Kind; empty if the data is not a pack of this version.
[[nodiscard]] std::vector<PieceMesh> decodePieces(const unsigned char* data, std::size_t size);

}  // namespace fcg::board3d
