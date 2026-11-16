// Bakes the 3D chess pieces of the board3d view from STL files into gui/assets/board3d/pieces.bin.
//
// Offline only, never part of the GUI: the GUI reads the baked file (gui/src/board3d/piece_pack.cpp) with the deflate
// decoder it already has. Each piece is welded, given normals on its full-resolution mesh (smooth within a crease
// angle), simplified with meshoptimizer (its vertices stay original ones, so those normals carry over), optimised for
// the vertex cache, quantised (positions to 1/2048 of a square, normals octahedral on 12 bits), delta coded, split
// into byte planes and deflated.
//
//   c++ -O2 -std=c++20 tools/board3d/bake.cpp <meshoptimizer>/src/*.cpp -I<meshoptimizer>/src -lz -o bake
//   ./bake <Staunton-Pieces>/Source/Staunton gui/assets/board3d/pieces.bin [error=0.0004] [square_mm=44]
//
// The pieces: clarkerubber's Staunton-Pieces (MIT), https://github.com/clarkerubber/Staunton-Pieces.

#include <meshoptimizer.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

struct V3 {
    float x = 0, y = 0, z = 0;
};
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float length(V3 a) { return std::sqrt(dot(a, a)); }
V3 normalize(V3 a) {
    float l = length(a);
    return l > 0 ? a * (1.0f / l) : V3{0, 1, 0};
}

struct Mesh {
    std::vector<V3> pos;
    std::vector<uint32_t> idx;
};

Mesh readStl(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<char> d((std::istreambuf_iterator<char>(in)), {});
    if (d.size() < 84) throw std::runtime_error("short STL: " + path);
    uint32_t n;
    std::memcpy(&n, d.data() + 80, 4);
    if (d.size() < 84 + 50ull * n) throw std::runtime_error("binary STL expected: " + path);
    Mesh m;
    std::map<std::array<float, 3>, uint32_t> seen;
    for (uint32_t t = 0; t < n; ++t) {
        float f[12];
        std::memcpy(f, d.data() + 84 + 50ull * t, 48);
        uint32_t tri[3];
        for (int k = 0; k < 3; ++k) {
            std::array<float, 3> p{f[3 + 3 * k], f[4 + 3 * k], f[5 + 3 * k]};
            auto [it, inserted] = seen.try_emplace(p, uint32_t(m.pos.size()));
            if (inserted) m.pos.push_back({p[0], p[1], p[2]});
            tri[k] = it->second;
        }
        if (tri[0] == tri[1] || tri[1] == tri[2] || tri[0] == tri[2]) continue;
        m.idx.insert(m.idx.end(), tri, tri + 3);
    }
    return m;
}

V3 faceNormal(const Mesh& m, size_t f) {
    V3 a = m.pos[m.idx[3 * f]], b = m.pos[m.idx[3 * f + 1]], c = m.pos[m.idx[3 * f + 2]];
    return normalize(cross(b - a, c - a));
}

// The angle of face f at its corner k: the weight of its normal in the vertex normal.
float cornerAngle(const Mesh& m, size_t f, int k) {
    V3 p  = m.pos[m.idx[3 * f + k]];
    V3 e1 = normalize(m.pos[m.idx[3 * f + (k + 1) % 3]] - p);
    V3 e2 = normalize(m.pos[m.idx[3 * f + (k + 2) % 3]] - p);
    return std::acos(std::clamp(dot(e1, e2), -1.0f, 1.0f));
}

uint16_t zigzag(int v) { return uint16_t((v << 1) ^ (v >> 31)); }

// Octahedral encoding of a unit vector on two 12-bit unsigned values.
std::array<uint16_t, 2> octEncode(V3 n) {
    float s = std::fabs(n.x) + std::fabs(n.y) + std::fabs(n.z);
    float u = n.x / s, v = n.y / s;
    if (n.z < 0) {
        float ou = u;
        u        = (1 - std::fabs(v)) * (ou >= 0 ? 1 : -1);
        v        = (1 - std::fabs(ou)) * (v >= 0 ? 1 : -1);
    }
    auto q = [](float x) { return uint16_t(std::lround((std::clamp(x, -1.0f, 1.0f) * 0.5f + 0.5f) * 4095.0f)); };
    return {q(u), q(v)};
}

void putPlanes(std::vector<uint8_t>& out, const std::vector<uint16_t>& values) {
    for (uint16_t v : values) out.push_back(uint8_t(v & 0xff));
    for (uint16_t v : values) out.push_back(uint8_t(v >> 8));
}

void putVarint(std::vector<uint8_t>& out, uint32_t v) {
    while (v >= 0x80) {
        out.push_back(uint8_t(v | 0x80));
        v >>= 7;
    }
    out.push_back(uint8_t(v));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <Source/Staunton> <out.bin> [error=0.0004] [square_mm=44]\n", argv[0]);
        return 2;
    }
    const std::string dir    = argv[1];
    const float targetError  = argc > 3 ? std::stof(argv[3]) : 0.0004f;
    const float squareMm     = argc > 4 ? std::stof(argv[4]) : 44.0f;
    const float creaseCos    = std::cos(35.0f * 3.14159265f / 180.0f);
    const float quantPerUnit = 2048.0f;  // positions: 1/2048 of a square, 0.02 mm

    // In the order of the GUI's piece kinds: pawn, knight, bishop, rook, queen, king.
    const char* names[] = {"Pawn", "Knight", "Bishop", "Rook", "Queen", "King"};

    std::vector<uint8_t> header, payload;
    header.insert(header.end(), {'F', 'C', 'G', '3'});
    auto put32 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i) header.push_back(uint8_t(v >> (8 * i)));
    };
    put32(1);  // version
    put32(6);
    put32(uint32_t(quantPerUnit));

    size_t totalTris = 0;
    for (int p = 0; p < 6; ++p) {
        Mesh m = readStl(dir + "/" + names[p] + "/" + names[p] + ".STL");

        // Centred on its base, in squares.
        V3 lo{1e9, 1e9, 1e9}, hi{-1e9, -1e9, -1e9};
        for (V3 v : m.pos) {
            lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)};
            hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)};
        }
        V3 centre{(lo.x + hi.x) / 2, lo.y, (lo.z + hi.z) / 2};
        for (V3& v : m.pos) v = (v - centre) * (1.0f / squareMm);

        const size_t faces = m.idx.size() / 3;
        std::vector<V3> fn(faces);
        for (size_t f = 0; f < faces; ++f) fn[f] = faceNormal(m, f);
        std::vector<std::vector<std::pair<uint32_t, float>>> around(m.pos.size());  // (face, angle) at each vertex
        for (size_t f = 0; f < faces; ++f)
            for (int k = 0; k < 3; ++k) around[m.idx[3 * f + k]].push_back({uint32_t(f), cornerAngle(m, f, k)});

        // Where the head of the knight looks, for the GUI to turn it.
        if (p == 1) {
            V3 head{};
            int n = 0;
            for (V3 v : m.pos)
                if (v.y > 1.0f) head = head + v, ++n;
            head = head * (1.0f / n);
            std::printf("knight head centroid x=%.3f z=%.3f\n", head.x, head.z);
        }

        std::vector<uint32_t> simplified(m.idx.size());
        float error = 0;
        size_t count = meshopt_simplify(simplified.data(), m.idx.data(), m.idx.size(), &m.pos[0].x, m.pos.size(),
                                        sizeof(V3), 0, targetError, 0, &error);
        simplified.resize(count);

        // The normal of each corner: the faces of the full mesh around its vertex that lie within the crease angle
        // of the simplified face, weighted by their angle at the vertex.
        struct Out {
            uint32_t orig;
            std::array<uint16_t, 2> n;
            bool operator<(const Out& o) const { return std::tie(orig, n) < std::tie(o.orig, o.n); }
        };
        std::map<Out, uint32_t> unique;
        std::vector<Out> verts;
        std::vector<uint32_t> idx(count);
        for (size_t c = 0; c < count; ++c) {
            size_t f  = c / 3;
            V3 a = m.pos[simplified[3 * f]], b = m.pos[simplified[3 * f + 1]], d = m.pos[simplified[3 * f + 2]];
            V3 face = normalize(cross(b - a, d - a));
            V3 sum{};
            for (auto [g, angle] : around[simplified[c]])
                if (dot(fn[g], face) > creaseCos) sum = sum + fn[g] * angle;
            V3 n  = length(sum) > 0 ? normalize(sum) : face;
            Out o{simplified[c], octEncode(n)};
            auto [it, inserted] = unique.try_emplace(o, uint32_t(verts.size()));
            if (inserted) verts.push_back(o);
            idx[c] = it->second;
        }

        meshopt_optimizeVertexCache(idx.data(), idx.data(), idx.size(), verts.size());
        std::vector<uint32_t> remap(verts.size());
        size_t nv = meshopt_optimizeVertexFetchRemap(remap.data(), idx.data(), idx.size(), verts.size());
        std::vector<Out> ordered(nv);
        for (size_t i = 0; i < verts.size(); ++i)
            if (remap[i] != ~0u) ordered[remap[i]] = verts[i];
        for (uint32_t& i : idx) i = remap[i];

        // Positions and normals, delta coded per component, each component's values as two byte planes.
        for (int comp = 0; comp < 3; ++comp) {
            std::vector<uint16_t> deltas;
            int prev = 0;
            for (const Out& o : ordered) {
                V3 v  = m.pos[o.orig];
                float x = comp == 0 ? v.x : comp == 1 ? v.y : v.z;
                int q = int(std::lround(x * quantPerUnit));
                deltas.push_back(zigzag(q - prev));
                prev = q;
            }
            putPlanes(payload, deltas);
        }
        for (int comp = 0; comp < 2; ++comp) {
            std::vector<uint16_t> deltas;
            int prev = 0;
            for (const Out& o : ordered) {
                deltas.push_back(zigzag(int(o.n[comp]) - prev));
                prev = o.n[comp];
            }
            putPlanes(payload, deltas);
        }
        // Indices: the distance below the next new vertex, which the vertex fetch order makes small (0: new).
        uint32_t next = 0;
        for (uint32_t i : idx) {
            putVarint(payload, next - i);
            if (i == next) ++next;
        }

        put32(uint32_t(nv));
        put32(uint32_t(idx.size()));
        totalTris += idx.size() / 3;
        std::printf("%-6s %6zu -> %6zu tris, %5zu verts, error %.5f (%.3f mm)\n", names[p], faces, idx.size() / 3, nv,
                    error, error * (hi.y - lo.y));
    }

    // Raw deflate (no zlib header), what stb_image's decoder in the GUI reads.
    std::vector<uint8_t> packed(compressBound(payload.size()) + 64);
    z_stream zs{};
    deflateInit2(&zs, 9, Z_DEFLATED, -15, 9, Z_DEFAULT_STRATEGY);
    zs.next_in   = payload.data();
    zs.avail_in  = uInt(payload.size());
    zs.next_out  = packed.data();
    zs.avail_out = uInt(packed.size());
    deflate(&zs, Z_FINISH);
    packed.resize(zs.total_out);
    deflateEnd(&zs);

    put32(uint32_t(payload.size()));
    std::ofstream out(argv[2], std::ios::binary);
    out.write(reinterpret_cast<const char*>(header.data()), std::streamsize(header.size()));
    out.write(reinterpret_cast<const char*>(packed.data()), std::streamsize(packed.size()));
    std::printf("%zu tris, payload %zu bytes, deflated %zu, file %zu bytes\n", totalTris, payload.size(), packed.size(),
                header.size() + packed.size());
}
