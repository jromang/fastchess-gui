#include "board3d/camera.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fcg::board3d {

Vec3 normalize(Vec3 a) {
    const float l = std::sqrt(dot(a, a));
    return l > 0 ? a * (1.0f / l) : a;
}

View viewOf(const Camera& c, float aspect) {
    const Vec3 target{c.target[0], c.target[1], c.target[2]};
    const Vec3 eye =
        target + Vec3{std::sin(c.yaw) * std::cos(c.pitch), std::sin(c.pitch), std::cos(c.yaw) * std::cos(c.pitch)} *
                     c.distance;
    View v;
    v.eye        = eye;
    v.forward    = normalize(target - eye);
    v.right      = normalize(cross(v.forward, {0, 1, 0}));
    v.up         = cross(v.right, v.forward);
    v.tanHalfFov = std::tan(c.fovY / 2);
    v.aspect     = aspect > 0 ? aspect : 1;
    return v;
}

Ray rayAt(const View& v, float x, float y) {
    const Vec3 dir = v.forward + v.right * (x * v.tanHalfFov * v.aspect) + v.up * (y * v.tanHalfFov);
    return {v.eye, normalize(dir)};
}

std::optional<std::array<float, 2>> project(const View& v, Vec3 point) {
    const Vec3 d    = point - v.eye;
    const float far = dot(d, v.forward);
    if (far <= 1e-4f) return std::nullopt;
    return std::array<float, 2>{dot(d, v.right) / (far * v.tanHalfFov * v.aspect), dot(d, v.up) / (far * v.tanHalfFov)};
}

Camera framed(Camera camera, float aspect, float pieceHeight, float margin) {
    if (aspect <= 0) aspect = 1;
    const float edge = kHalfBoard + kFrame, inner = kHalfBoard - 0.5f;
    std::vector<Vec3> points;
    for (float x : {-edge, edge})
        for (float z : {-edge, edge}) points.push_back({x, 0, z}), points.push_back({x, -kThickness, z});
    if (pieceHeight > 0)
        for (float x : {-inner, inner})
            for (float z : {-inner, inner}) points.push_back({x, pieceHeight, z});

    // The room left in the image, as much of it on each side.
    const float roomY = std::max(0.1f, 1 - 2 * margin), roomX = std::max(0.1f, 1 - 2 * margin / aspect);
    const float lift  = std::max(std::sin(camera.pitch), 0.2f);
    const Vec3 along{-std::sin(camera.yaw), 0, -std::cos(camera.yaw)};  // away from the camera, level
    for (int i = 0; i < 64; ++i) {
        const View v = viewOf(camera, aspect);
        float x0 = std::numeric_limits<float>::max(), x1 = -x0, y0 = x0, y1 = -x0;
        bool behind = false;
        for (const Vec3& p : points) {
            const auto q = project(v, p);
            if (!q) {
                behind = true;
                break;
            }
            x0 = std::min(x0, (*q)[0]), x1 = std::max(x1, (*q)[0]);
            y0 = std::min(y0, (*q)[1]), y1 = std::max(y1, (*q)[1]);
        }
        if (behind) {
            camera.distance = std::min(camera.distance * 1.5f, 200.0f);
            continue;
        }
        // Centred: the target slid across and along the board; then nearer or farther for the size.
        const float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
        const float h  = camera.distance * v.tanHalfFov;
        const Vec3 t   = Vec3{camera.target[0], camera.target[1], camera.target[2]} + v.right * (cx * h * aspect) +
                       along * (cy * h / lift);
        camera.target = {t.x, t.y, t.z};
        const float ratio = std::max((y1 - y0) / 2 / roomY, (x1 - x0) / 2 / roomX);
        camera.distance   = std::clamp(camera.distance * ratio, 1.0f, 200.0f);
        if (std::fabs(ratio - 1) < 1e-4f && std::fabs(cx) < 1e-4f && std::fabs(cy) < 1e-4f) break;
    }
    return camera;
}

Vec3 squareCentre(int square) {
    const int file = square % 8, rank = square / 8;
    return {float(file) - 3.5f, 0, 3.5f - float(rank)};
}

namespace {

// Where a ray enters a standing cylinder of radius r from y = 0 to y = h round (cx, cz), by its side or its top;
// infinity if it does not.
float enterCylinder(const Ray& ray, float cx, float cz, float r, float h) {
    float best    = std::numeric_limits<float>::infinity();
    const float ox = ray.origin.x - cx, oz = ray.origin.z - cz;
    const float a = ray.dir.x * ray.dir.x + ray.dir.z * ray.dir.z;
    if (a > 1e-9f) {
        const float b = ox * ray.dir.x + oz * ray.dir.z, c = ox * ox + oz * oz - r * r;
        const float disc = b * b - a * c;
        if (disc >= 0) {
            const float t = (-b - std::sqrt(disc)) / a;
            const float y = ray.origin.y + t * ray.dir.y;
            if (t > 0 && y >= 0 && y <= h) best = t;
        }
    }
    if (std::fabs(ray.dir.y) > 1e-9f) {
        const float t = (h - ray.origin.y) / ray.dir.y;
        const float x = ox + t * ray.dir.x, z = oz + t * ray.dir.z;
        if (t > 0 && x * x + z * z <= r * r) best = std::min(best, t);
    }
    return best;
}

}  // namespace

int squareAt(const Ray& ray, std::span<const PlacedPiece> pieces, std::span<const PieceShape> shapes) {
    float nearest = std::numeric_limits<float>::infinity();
    int square    = -1;
    for (const PlacedPiece& p : pieces) {
        const auto kind = std::size_t(p.kind);
        if (kind >= shapes.size() || p.square < 0 || p.square > 63) continue;
        const Vec3 c  = squareCentre(p.square);
        const float t = enterCylinder(ray, c.x, c.z, shapes[kind].radius, shapes[kind].height);
        if (t < nearest) nearest = t, square = p.square;
    }
    if (square >= 0) return square;
    if (ray.dir.y >= -1e-9f) return -1;
    const float t = -ray.origin.y / ray.dir.y;
    if (t <= 0) return -1;
    const float x = ray.origin.x + t * ray.dir.x, z = ray.origin.z + t * ray.dir.z;
    const int file = int(std::floor(x + kHalfBoard)), rank = int(std::floor(kHalfBoard - z));
    if (file < 0 || file > 7 || rank < 0 || rank > 7) return -1;
    return rank * 8 + file;
}

}  // namespace fcg::board3d
