#pragma once

// The camera's geometry on the processor: the rays the trace shader casts (trace.frag's main(), the same formulas),
// framing the board in a view, and which square is under the mouse. No OpenGL here: the tests run it.

#include "board3d/piece_pack.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace fcg::board3d {

struct Vec3 {
    float x = 0, y = 0, z = 0;
};
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec3 normalize(Vec3 a);

struct Camera {
    float yaw      = 0.0f;   // radians around the board, 0 behind White
    float pitch    = 0.72f;  // radians above the board's plane
    float distance = 13.0f;  // squares from the target
    std::array<float, 3> target{0.0f, 0.0f, 0.6f};
    float fovY     = 0.52f;  // vertical field of view, radians
    float aperture = 0.0f;   // lens radius in squares; 0: everything sharp
};

// Where the camera is and where it looks, for an image of that width over height.
struct View {
    Vec3 eye, forward, right, up;
    float tanHalfFov = 0, aspect = 1;
};
[[nodiscard]] View viewOf(const Camera& camera, float aspect);

struct Ray {
    Vec3 origin, dir;  // dir of length 1
};
// The ray through a point of the image, in normalized coordinates: -1 to 1, y up.
[[nodiscard]] Ray rayAt(const View& view, float x, float y);
// A point's place in the image, normalized as rayAt() takes it; nullopt behind the camera.
[[nodiscard]] std::optional<std::array<float, 2>> project(const View& view, Vec3 point);

// The board's geometry, in squares (trace.frag's): the playing surface at y = 0, 8 squares wide, its frame round it.
inline constexpr float kHalfBoard = 4.0f, kFrame = 0.62f, kThickness = 0.32f;

// The camera moved along its line of sight and its target slid along the board, so that the board, its frame and
// pieces up to `pieceHeight` fill the image of that aspect but for `margin` (a fraction of its height) all round.
// Its yaw, pitch and field of view are kept.
[[nodiscard]] Camera framed(Camera camera, float aspect, float pieceHeight, float margin = 0.03f);

// The centre of a square on the playing surface (0 = a1 ... 63 = h8): White at +z, the a-file at -x.
[[nodiscard]] Vec3 squareCentre(int square);

// The slices of a piece's height its profile has.
inline constexpr int PROFILE_SLICES = 48;

struct PieceShape {
    float radius = 0, height = 0;  // the bounding cylinder of a piece standing on its square
    // Its radius in each of PROFILE_SLICES slices of its height, from the base: how far from its axis its surface
    // gets there. A ray meets the piece where it meets this stack of thin cylinders, so that the neck of a pawn or
    // the space beside a king's cross is not taken for the piece. Empty: the bounding cylinder.
    std::vector<float> profile;
};

// The profile of a mesh standing on y = 0, `height` tall (see PieceShape::profile): each triangle clipped to each
// slice it crosses, its farthest point from the axis there. Whichever way the piece is turned, it is the same.
[[nodiscard]] std::vector<float> profileOf(std::span<const float> positions, std::span<const std::uint32_t> indices,
                                           float height);
struct PlacedPiece {
    int square = 0;  // 0 = a1 ... 63 = h8
    Kind kind  = Kind::Pawn;
    bool black = false;
    bool operator==(const PlacedPiece&) const = default;
};

// The square a ray points at: the nearest piece it meets (its profile, or its bounding cylinder), else the square of
// the playing surface it reaches; -1 off the board. `shapes` in the order of Kind.
[[nodiscard]] int squareAt(const Ray& ray, std::span<const PlacedPiece> pieces, std::span<const PieceShape> shapes);

}  // namespace fcg::board3d
