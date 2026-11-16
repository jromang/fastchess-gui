#include "board3d/piece_pack.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>

// stb_image's decoder of a raw deflate stream, compiled with it in engine_logos.cpp.
extern "C" char* stbi_zlib_decode_noheader_malloc(const char* buffer, int len, int* outlen);

namespace fcg::board3d {

namespace {

struct Reader {
    const unsigned char* at;
    const unsigned char* end;
    bool ok = true;

    std::uint32_t u32() {
        if (end - at < 4) return ok = false, 0;
        std::uint32_t v = at[0] | at[1] << 8 | at[2] << 16 | std::uint32_t(at[3]) << 24;
        at += 4;
        return v;
    }
    std::uint8_t u8() {
        if (at >= end) return ok = false, 0;
        return *at++;
    }
    std::uint32_t varint() {
        std::uint32_t v = 0;
        for (int shift = 0; shift < 35; shift += 7) {
            std::uint8_t b = u8();
            v |= std::uint32_t(b & 0x7f) << shift;
            if (!(b & 0x80)) return v;
        }
        return ok = false, 0;
    }
};

int unzigzag(std::uint16_t v) { return int(v >> 1) ^ -int(v & 1); }

// Values delta coded and stored as a plane of low bytes, then one of high bytes.
void readPlanes(Reader& r, std::size_t count, std::vector<int>& out) {
    out.assign(count, 0);
    if (std::size_t(r.end - r.at) < 2 * count) {
        r.ok = false;
        return;
    }
    int prev = 0;
    for (std::size_t i = 0; i < count; ++i) {
        prev += unzigzag(std::uint16_t(r.at[i] | r.at[count + i] << 8));
        out[i] = prev;
    }
    r.at += 2 * count;
}

}  // namespace

std::vector<PieceMesh> decodePieces(const unsigned char* data, std::size_t size) {
    Reader h{data, data + size};
    if (size < 16 || std::memcmp(data, "FCG3", 4) != 0) return {};
    h.at += 4;
    if (h.u32() != 1) return {};
    const std::uint32_t count = h.u32();
    const float unit          = 1.0f / float(h.u32());
    if (count != 6 || !h.ok) return {};
    std::uint32_t verts[6], indices[6];
    for (std::uint32_t p = 0; p < count; ++p) verts[p] = h.u32(), indices[p] = h.u32();
    const std::uint32_t payloadSize = h.u32();
    if (!h.ok) return {};

    int length = 0;
    std::unique_ptr<char, decltype(&std::free)> raw(
        stbi_zlib_decode_noheader_malloc(reinterpret_cast<const char*>(h.at), int(h.end - h.at), &length), &std::free);
    if (!raw || std::uint32_t(length) != payloadSize) return {};

    Reader r{reinterpret_cast<const unsigned char*>(raw.get()),
             reinterpret_cast<const unsigned char*>(raw.get()) + length};
    std::vector<PieceMesh> pieces(count);
    std::vector<int> comp[5];
    for (std::uint32_t p = 0; p < count && r.ok; ++p) {
        PieceMesh& m = pieces[p];
        for (auto& c : comp) readPlanes(r, verts[p], c);
        if (!r.ok) break;
        m.positions.resize(3 * verts[p]);
        m.normals.resize(3 * verts[p]);
        for (std::uint32_t v = 0; v < verts[p]; ++v) {
            float x = comp[0][v] * unit, y = comp[1][v] * unit, z = comp[2][v] * unit;
            m.positions[3 * v] = x, m.positions[3 * v + 1] = y, m.positions[3 * v + 2] = z;
            m.height = std::max(m.height, y);
            m.radius = std::max(m.radius, std::sqrt(x * x + z * z));
            // Octahedral normal.
            float nx = comp[3][v] / 4095.0f * 2 - 1, ny = comp[4][v] / 4095.0f * 2 - 1;
            float nz = 1 - std::fabs(nx) - std::fabs(ny);
            if (nz < 0) {
                float ox = nx;
                nx       = (1 - std::fabs(ny)) * (ox >= 0 ? 1 : -1);
                ny       = (1 - std::fabs(ox)) * (ny >= 0 ? 1 : -1);
            }
            float l = std::sqrt(nx * nx + ny * ny + nz * nz);
            m.normals[3 * v] = nx / l, m.normals[3 * v + 1] = ny / l, m.normals[3 * v + 2] = nz / l;
        }
        m.indices.resize(indices[p]);
        std::uint32_t next = 0;
        for (auto& i : m.indices) {
            std::uint32_t d = r.varint();
            if (d > next) r.ok = false;
            i = next - d;
            if (d == 0) ++next;
        }
        if (next > verts[p]) r.ok = false;
    }
    if (!r.ok) return {};
    return pieces;
}

}  // namespace fcg::board3d
