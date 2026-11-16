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

std::array<float, 16> viewProjection(const View& v, float depth_near, float depth_far) {
    // Clip x and y are the point's distances along right and up over tanHalfFov (times the aspect for x), clip w its
    // distance along forward: x / w is what project() gives. Clip z from depth_near to depth_far, as glFrustum maps it.
    const float sx = 1.0f / (v.tanHalfFov * v.aspect), sy = 1.0f / v.tanHalfFov;
    const float a = (depth_far + depth_near) / (depth_far - depth_near);
    const float b = -2.0f * depth_far * depth_near / (depth_far - depth_near);
    const Vec3 rows[3] = {v.right * sx, v.up * sy, v.forward};
    std::array<float, 16> m{};
    // Row r of the matrix: rows[r] . (p - eye); the third row's depth a * w + b.
    for (int c = 0; c < 3; ++c) {
        const float axis[3][3] = {
            {rows[0].x, rows[0].y, rows[0].z}, {rows[1].x, rows[1].y, rows[1].z}, {rows[2].x, rows[2].y, rows[2].z}};
        m[std::size_t(c * 4 + 0)] = axis[0][c];
        m[std::size_t(c * 4 + 1)] = axis[1][c];
        m[std::size_t(c * 4 + 2)] = a * axis[2][c];
        m[std::size_t(c * 4 + 3)] = axis[2][c];
    }
    m[12] = -dot(rows[0], v.eye);
    m[13] = -dot(rows[1], v.eye);
    m[14] = -a * dot(rows[2], v.eye) + b;
    m[15] = -dot(rows[2], v.eye);
    return m;
}

std::optional<std::array<float, 2>> ghostAt(const Ray& ray) {
    if (ray.dir.y >= -1e-6f) return std::nullopt;
    const float t = (kGhostLift - ray.origin.y) / ray.dir.y;
    if (t <= 0) return std::nullopt;
    const float edge = kHalfBoard + kFrame * 0.5f;
    return std::array<float, 2>{std::clamp(ray.origin.x + t * ray.dir.x, -edge, edge),
                                std::clamp(ray.origin.z + t * ray.dir.z, -edge, edge)};
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

// Where a ray enters a standing cylinder of radius r from y = y0 to y = y1 round (cx, cz), by its side, its top or
// its bottom; infinity if it does not.
float enterBand(const Ray& ray, float cx, float cz, float r, float y0, float y1) {
    float best     = std::numeric_limits<float>::infinity();
    const float ox = ray.origin.x - cx, oz = ray.origin.z - cz;
    const float a = ray.dir.x * ray.dir.x + ray.dir.z * ray.dir.z;
    if (a > 1e-9f) {
        const float b = ox * ray.dir.x + oz * ray.dir.z, c = ox * ox + oz * oz - r * r;
        const float disc = b * b - a * c;
        if (disc >= 0) {
            const float t = (-b - std::sqrt(disc)) / a;
            const float y = ray.origin.y + t * ray.dir.y;
            if (t > 0 && y >= y0 && y <= y1) best = t;
        }
    }
    if (std::fabs(ray.dir.y) > 1e-9f) {
        for (const float y : {y0, y1}) {
            const float t = (y - ray.origin.y) / ray.dir.y;
            const float x = ox + t * ray.dir.x, z = oz + t * ray.dir.z;
            if (t > 0 && x * x + z * z <= r * r) best = std::min(best, t);
        }
    }
    return best;
}

// Where a ray enters a piece standing on (cx, cz): its bounding cylinder first, then the slices of its profile.
float enterPiece(const Ray& ray, float cx, float cz, const PieceShape& shape) {
    const float bound = enterBand(ray, cx, cz, shape.radius, 0, shape.height);
    if (shape.profile.empty() || std::isinf(bound)) return bound;
    float best        = std::numeric_limits<float>::infinity();
    const auto slices = shape.profile.size();
    for (std::size_t i = 0; i < slices; ++i) {
        if (shape.profile[i] <= 0) continue;
        const float y0 = shape.height * float(i) / float(slices), y1 = shape.height * float(i + 1) / float(slices);
        best = std::min(best, enterBand(ray, cx, cz, shape.profile[i], y0, y1));
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
        const float t = enterPiece(ray, c.x, c.z, shapes[kind]);
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

std::vector<float> profileOf(std::span<const float> positions, std::span<const std::uint32_t> indices, float height) {
    std::vector<float> profile(PROFILE_SLICES, 0.0f);
    if (height <= 0) return profile;
    const std::size_t vertices = positions.size() / 3;
    const float slice          = height / PROFILE_SLICES;
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
        std::array<Vec3, 3> tri;
        bool inside = true;
        for (std::size_t k = 0; k < 3; ++k) {
            const auto v = indices[i + k];
            if (v >= vertices) inside = false;
            if (inside) tri[k] = {positions[3 * v], positions[3 * v + 1], positions[3 * v + 2]};
        }
        if (!inside) continue;
        const float low  = std::min({tri[0].y, tri[1].y, tri[2].y});
        const float high = std::max({tri[0].y, tri[1].y, tri[2].y});
        const int first  = std::clamp(int(std::floor(low / slice)), 0, PROFILE_SLICES - 1);
        const int last   = std::clamp(int(std::floor(high / slice)), 0, PROFILE_SLICES - 1);
        for (int s = first; s <= last; ++s) {
            // The triangle clipped to the slice (above y0, then below y1): the farthest of its corners from the axis
            // is the farthest of its points, the distance to the axis being convex.
            const float y0 = s * slice, y1 = (s + 1) * slice;
            std::vector<Vec3> polygon(tri.begin(), tri.end());
            for (const int side : {0, 1}) {
                std::vector<Vec3> kept;
                const auto in = [&](const Vec3& p) { return side == 0 ? p.y >= y0 : p.y <= y1; };
                const float y = side == 0 ? y0 : y1;
                for (std::size_t k = 0; k < polygon.size(); ++k) {
                    const Vec3 a = polygon[k], b = polygon[(k + 1) % polygon.size()];
                    if (in(a)) kept.push_back(a);
                    if (in(a) != in(b)) kept.push_back(a + (b - a) * ((y - a.y) / (b.y - a.y)));
                }
                polygon = std::move(kept);
            }
            for (const Vec3& p : polygon)
                profile[std::size_t(s)] = std::max(profile[std::size_t(s)], std::sqrt(p.x * p.x + p.z * p.z));
        }
    }
    return profile;
}

}  // namespace fcg::board3d
