#include "board3d/bvh.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cfloat>

namespace fcg::board3d {

namespace {

struct Box {
    std::array<float, 3> lo{FLT_MAX, FLT_MAX, FLT_MAX};
    std::array<float, 3> hi{-FLT_MAX, -FLT_MAX, -FLT_MAX};

    void grow(const std::array<float, 3>& p) {
        for (int a = 0; a < 3; ++a) lo[a] = std::min(lo[a], p[a]), hi[a] = std::max(hi[a], p[a]);
    }
    void grow(const Box& b) {
        for (int a = 0; a < 3; ++a) lo[a] = std::min(lo[a], b.lo[a]), hi[a] = std::max(hi[a], b.hi[a]);
    }
    [[nodiscard]] float area() const {
        if (lo[0] > hi[0]) return 0;
        float x = hi[0] - lo[0], y = hi[1] - lo[1], z = hi[2] - lo[2];
        return 2 * (x * y + y * z + z * x);
    }
};

struct Node {
    Box box;
    int left = -1, right = -1;  // children, for an inner node
    int first = 0, count = 0;   // triangles, for a leaf
};

struct Builder {
    std::vector<Box> triBox;
    std::vector<std::array<float, 3>> centre;
    std::vector<int> order;
    std::vector<Node> nodes;

    int build(int first, int count) {
        Node n;
        Box centres;
        for (int i = first; i < first + count; ++i) n.box.grow(triBox[order[i]]), centres.grow(centre[order[i]]);
        n.first = first, n.count = count;
        const int index = int(nodes.size());
        nodes.push_back(n);
        if (count <= 2) return index;

        // Binned surface area heuristic on each axis.
        constexpr int bins = 16;
        float bestCost      = FLT_MAX;
        int bestAxis = -1, bestSplit = 0;
        for (int a = 0; a < 3; ++a) {
            float extent = centres.hi[a] - centres.lo[a];
            if (extent <= 0) continue;
            Box binBox[bins];
            int binCount[bins] = {};
            for (int i = first; i < first + count; ++i) {
                int b = std::min(bins - 1, int((centre[order[i]][a] - centres.lo[a]) / extent * bins));
                binBox[b].grow(triBox[order[i]]);
                ++binCount[b];
            }
            float rightArea[bins];
            int rightCount[bins];
            Box acc;
            int c = 0;
            for (int b = bins - 1; b > 0; --b) {
                acc.grow(binBox[b]);
                c += binCount[b];
                rightArea[b] = acc.area(), rightCount[b] = c;
            }
            acc = Box{};
            c   = 0;
            for (int b = 0; b < bins - 1; ++b) {
                acc.grow(binBox[b]);
                c += binCount[b];
                float cost = acc.area() * c + rightArea[b + 1] * rightCount[b + 1];
                if (c > 0 && rightCount[b + 1] > 0 && cost < bestCost) bestCost = cost, bestAxis = a, bestSplit = b;
            }
        }
        // A leaf when splitting costs more than intersecting every triangle (traversal step counted as one).
        const float leafCost = n.box.area() * count;
        if (bestAxis < 0 || (count <= 4 && bestCost + n.box.area() >= leafCost)) return index;

        const float extent = centres.hi[bestAxis] - centres.lo[bestAxis];
        auto* mid          = std::partition(order.data() + first, order.data() + first + count, [&](int t) {
            return std::min(bins - 1, int((centre[t][bestAxis] - centres.lo[bestAxis]) / extent * bins)) <= bestSplit;
        });
        int leftCount = int(mid - (order.data() + first));
        if (leftCount == 0 || leftCount == count) leftCount = count / 2;

        int left  = build(first, leftCount);
        int right = build(first + leftCount, count - leftCount);
        nodes[index].left = left, nodes[index].right = right, nodes[index].count = 0;
        return index;
    }
};

void putTexel(std::vector<float>& out, const std::array<float, 3>& v, float w) {
    out.insert(out.end(), {v[0], v[1], v[2], w});
}

}  // namespace

GpuBvh buildBvh(const std::vector<PieceMesh>& meshes) {
    GpuBvh out;
    for (const PieceMesh& m : meshes) {
        const int triCount = int(m.indices.size() / 3);
        Builder b;
        b.triBox.resize(triCount);
        b.centre.resize(triCount);
        b.order.resize(triCount);
        auto vertex = [&](std::uint32_t i) {
            return std::array<float, 3>{m.positions[3 * i], m.positions[3 * i + 1], m.positions[3 * i + 2]};
        };
        for (int t = 0; t < triCount; ++t) {
            for (int k = 0; k < 3; ++k) b.triBox[t].grow(vertex(m.indices[3 * t + k]));
            for (int a = 0; a < 3; ++a) b.centre[t][a] = (b.triBox[t].lo[a] + b.triBox[t].hi[a]) / 2;
            b.order[t] = t;
        }
        b.build(0, triCount);

        // The triangles in the order of the leaves.
        const int triBase = int(out.triangles.size() / 12);
        for (int t : b.order) {
            auto v0 = vertex(m.indices[3 * t]), v1 = vertex(m.indices[3 * t + 1]), v2 = vertex(m.indices[3 * t + 2]);
            putTexel(out.triangles, v0, 0);
            putTexel(out.triangles, {v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2]}, 0);
            putTexel(out.triangles, {v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2]}, 0);
            for (int k = 0; k < 3; ++k) {
                std::uint32_t i = m.indices[3 * t + k];
                putTexel(out.normals, {m.normals[3 * i], m.normals[3 * i + 1], m.normals[3 * i + 2]}, 0);
            }
        }

        // The inner nodes, numbered in the order met; the root first even when it is a leaf.
        std::vector<int> gpuIndex(b.nodes.size(), -1);
        const int nodeBase = int(out.nodes.size() / 16);
        int next           = nodeBase;
        for (std::size_t i = 0; i < b.nodes.size(); ++i)
            if (b.nodes[i].count == 0 || i == 0) gpuIndex[i] = next++;
        out.nodes.resize(std::size_t(next) * 16);
        const Box far{{1e30f, 1e30f, 1e30f}, {1e30f, 1e30f, 1e30f}};
        auto child = [&](float* at, int c) {
            const Box& box = c < 0 ? far : b.nodes[c].box;
            int ref = 0, count = 0;
            if (c >= 0 && b.nodes[c].count > 0) ref = triBase + b.nodes[c].first, count = b.nodes[c].count;
            else if (c >= 0) ref = gpuIndex[c];
            at[0] = box.lo[0], at[1] = box.lo[1], at[2] = box.lo[2], at[3] = std::bit_cast<float>(ref);
            at[4] = box.hi[0], at[5] = box.hi[1], at[6] = box.hi[2], at[7] = std::bit_cast<float>(count);
        };
        for (std::size_t i = 0; i < b.nodes.size(); ++i) {
            if (gpuIndex[i] < 0) continue;
            float* at = &out.nodes[std::size_t(gpuIndex[i]) * 16];
            if (b.nodes[i].count > 0) {  // a root that is a leaf
                child(at, int(i));
                child(at + 8, -1);
            } else {
                child(at, b.nodes[i].left);
                child(at + 8, b.nodes[i].right);
            }
        }
        out.roots.push_back(nodeBase);
    }
    return out;
}

}  // namespace fcg::board3d
