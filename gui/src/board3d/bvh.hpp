#pragma once

// Bounding volume hierarchies of the pieces, laid out for the path tracer's texture buffers (shaders/trace.frag).
//
// A node is 4 texels: the box of its left child (min.xyz, ref), (max.xyz, count), then the same for its right child.
// A child with count > 0 is a leaf of `count` triangles from triangle `ref`; one with count 0 is the node `ref`. The
// box of a missing child lies far away, where no ray reaches. A triangle is 3 texels (v0, v1 - v0, v2 - v0) and its
// three corner normals 3 more, in another buffer.

#include "board3d/piece_pack.hpp"

#include <vector>

namespace fcg::board3d {

struct GpuBvh {
    std::vector<float> nodes;    // 16 floats per node
    std::vector<float> triangles;  // 12 floats per triangle
    std::vector<float> normals;    // 12 floats per triangle
    std::vector<int> roots;        // the root node of each mesh
};

[[nodiscard]] GpuBvh buildBvh(const std::vector<PieceMesh>& meshes);

}  // namespace fcg::board3d
