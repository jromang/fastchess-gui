#include "board3d/renderer.hpp"

#include "board3d/bvh.hpp"
#include "board3d/gl3.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace fcg::board3d {

namespace {

using namespace gl;

// A triangle covering the viewport, from gl_VertexID alone.
constexpr const char* kFullscreenVertex = R"(#version 330 core
void main() {
    vec2 p = vec2(float((gl_VertexID & 1) << 2) - 1.0, float((gl_VertexID & 2) << 1) - 1.0);
    gl_Position = vec4(p, 0.0, 1.0);
}
)";

GLuint compile(GLenum type, const std::string& source, std::string& why) {
    const auto& f    = fn();
    GLuint shader    = f.CreateShader(type);
    const char* text = source.c_str();
    f.ShaderSource(shader, 1, &text, nullptr);
    f.CompileShader(shader);
    GLint ok = 0;
    f.GetShaderiv(shader, COMPILE_STATUS, &ok);
    if (!ok) {
        GLint length = 0;
        f.GetShaderiv(shader, INFO_LOG_LENGTH, &length);
        std::string log(std::size_t(std::max(length, 1)), '\0');
        f.GetShaderInfoLog(shader, length, nullptr, log.data());
        why += log;
        f.DeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint link(const std::string& fragment, std::string& why) {
    const auto& f = fn();
    GLuint vs     = compile(VERTEX_SHADER, kFullscreenVertex, why);
    GLuint fs     = compile(FRAGMENT_SHADER, fragment, why);
    if (!vs || !fs) {
        if (vs) f.DeleteShader(vs);
        if (fs) f.DeleteShader(fs);
        return 0;
    }
    GLuint program = f.CreateProgram();
    f.AttachShader(program, vs);
    f.AttachShader(program, fs);
    f.LinkProgram(program);
    f.DeleteShader(vs);
    f.DeleteShader(fs);
    GLint ok = 0;
    f.GetProgramiv(program, LINK_STATUS, &ok);
    if (!ok) {
        GLint length = 0;
        f.GetProgramiv(program, INFO_LOG_LENGTH, &length);
        std::string log(std::size_t(std::max(length, 1)), '\0');
        f.GetProgramInfoLog(program, length, nullptr, log.data());
        why += log;
        f.DeleteProgram(program);
        return 0;
    }
    return program;
}

GLint loc(GLuint program, const char* name) { return fn().GetUniformLocation(program, name); }

using V3 = Vec3;

// A rectangular light at `centre`, w by h squares, facing `target`.
struct RectLight {
    V3 centre, u, v;
    V3 radiance;
    float area;
};
RectLight rectLight(V3 centre, V3 target, float w, float h, V3 radiance) {
    V3 n = normalize(target - centre);
    // Its width level; along x for one that faces straight down or up.
    V3 u = std::fabs(n.y) > 0.999f ? V3{1, 0, 0} : normalize(cross(n, {0, 1, 0}));
    V3 v = cross(n, u);  // u x v = n: the side that shines
    return {centre, u * w, v * h, radiance, w * h};
}

// The colour of a black body at that temperature, in linear sRGB of luminance 1: Kim et al.'s fit of the Planckian
// locus (2002), 1667 K to 25000 K.
V3 blackbody(float kelvin) {
    const double t = std::clamp(double(kelvin), 1667.0, 25000.0), k = 1000.0 / t;
    const double x = t <= 4000.0 ? -0.2661239 * k * k * k - 0.2343589 * k * k + 0.8776956 * k + 0.179910
                                 : -3.0258469 * k * k * k + 2.1070379 * k * k + 0.2226347 * k + 0.240390;
    const double y = t <= 2222.0   ? -1.1063814 * x * x * x - 1.34811020 * x * x + 2.18555832 * x - 0.20219683
                     : t <= 4000.0 ? -0.9549476 * x * x * x - 1.37418593 * x * x + 2.09137015 * x - 0.16748867
                                   : 3.0817580 * x * x * x - 5.87338670 * x * x + 3.75112997 * x - 0.37001483;
    const double X = x / y, Z = (1.0 - x - y) / y;
    const double r = std::max(0.0, 3.2406 * X - 1.5372 - 0.4986 * Z);
    const double g = std::max(0.0, -0.9689 * X + 1.8758 + 0.0415 * Z);
    const double b = std::max(0.0, 0.0557 * X - 0.2040 + 1.0570 * Z);
    const double l = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    return {float(r / l), float(g / l), float(b / l)};
}

float linear(float srgb) { return std::pow(srgb / 255.0f, 2.2f); }

// The lightings, as photographers light a still life. Each light's radiance is set for about the same light on the
// board from the main one, so that one exposure fits them all roughly; each has its own.
constexpr Studio kStudios[] = {
    // Three-point lighting: a warm soft box above left, a dim cool fill on the right, a strip behind for the outlines.
    {.id         = "studio",
     .name       = "Studio",
     .lightCount = 3,
     .lights     = {{{{-6.5f, 7.0f, 5.0f}, {0, 0, 0}, 3, 3, 4800, 18.5f},
                     {{9.0f, 6.0f, 7.0f}, {0, 0, 0}, 8, 8, 8000, 0.28f},
                     {{0.0f, 4.0f, -14.0f}, {0, 0.6f, 0}, 14, 2, 6800, 2.6f}}},
     .skyKelvin  = 7000,
     .sky        = 0.05f,
     .floor      = {34, 36, 40},
     .weave      = 0.3f,
     .rough      = 0.9f,
     .coat       = 0,
     .coatRough  = 1,
     .exposure   = 1.3f},
    // Low key: one small tungsten source close, front left, no fill, the light falling off across the board towards
    // Black and the pieces carved out of the dark (its reflection falls off the near edge); a cool kicker behind on
    // the right draws the far edges.
    {.id         = "low_key",
     .name       = "Low key",
     .lightCount = 2,
     .lights     = {{{{-5.0f, 6.0f, 3.0f}, {0, 0.3f, 0}, 1.6f, 1.6f, 3200, 38},
                     {{7.5f, 2.5f, -8.0f}, {0, 0.8f, 0}, 0.8f, 4, 6500, 6}}},
     .skyKelvin  = 6500,
     .sky        = 0.004f,
     .floor      = {12, 12, 13},
     .weave      = 0.2f,
     .rough      = 0.85f,
     .coat       = 0,
     .coatRough  = 1,
     .exposure   = 1.3f},
    // High key: a white sweep lit evenly all over, a large soft box overhead and two fills: bright, soft, hardly a
    // shadow.
    {.id         = "high_key",
     .name       = "High key",
     .lightCount = 3,
     .lights     = {{{{0.0f, 12.0f, 2.5f}, {0, 0, 0}, 11, 11, 5600, 2.4f},
                     {{-11.0f, 5.0f, 6.0f}, {0, 0.5f, 0}, 6, 6, 5600, 3.5f},
                     {{11.0f, 5.0f, 6.0f}, {0, 0.5f, 0}, 6, 6, 5600, 3.5f}}},
     .skyKelvin  = 6000,
     .sky        = 0.8f,
     .backdrop   = 0.6f,
     .floor      = {236, 236, 234},
     .weave      = 0.08f,
     .rough      = 0.95f,
     .coat       = 0,
     .coatRough  = 1,
     .exposure   = 0.9f},
    // Showroom: black acrylic that mirrors the board (its satin base matt: the coat alone gives the mirror), two tall
    // strip boxes behind on either side for the highlights along the pieces' curves, a soft box above.
    {.id         = "showroom",
     .name       = "Showroom",
     .lightCount = 3,
     .lights     = {{{{-8.5f, 3.5f, -6.0f}, {0, 1, 0}, 1.2f, 7, 6500, 9},
                     {{8.5f, 3.5f, -6.0f}, {0, 1, 0}, 1.2f, 7, 6500, 9},
                     {{0.0f, 11.0f, 4.0f}, {0, 0, 0}, 6, 6, 5600, 2.5f}}},
     .skyKelvin  = 6500,
     .sky        = 0.01f,
     .floor      = {6, 6, 7},
     .weave      = 0,
     .rough      = 1,
     .coat       = 1,
     .coatRough  = 0.03f,
     .exposure   = 1.4f},
    // A north window: a tall pane of daylight on the left, a white card on the right giving a little back, the sky's
    // blue in the shadows.
    {.id         = "window",
     .name       = "Window light",
     .lightCount = 2,
     .lights     = {{{{-24.0f, 7.5f, -3.0f}, {0, 1.5f, 0}, 7, 9, 6500, 26},
                     {{12.0f, 3.0f, 5.0f}, {0, 0.5f, 0}, 6, 5, 5000, 0.25f}}},
     .skyKelvin  = 9000,
     .sky        = 0.08f,
     .floor      = {96, 86, 74},
     .weave      = 0.25f,
     .rough      = 0.8f,
     .coat       = 0,
     .coatRough  = 1,
     .exposure   = 1.35f},
    // Golden hour: a low orange sun, 12 degrees up, raking the board, the pieces' shadows long across it, a blue sky
    // above.
    {.id         = "golden_hour",
     .name       = "Golden hour",
     .lightCount = 1,
     .lights     = {{{{-25.0f, 6.0f, -12.0f}, {0, 0, 0}, 0, 0, 3000, 4.0f, 1.5f}}},
     .skyKelvin  = 11000,
     .sky        = 0.12f,
     .floor      = {110, 98, 84},
     .weave      = 0.2f,
     .rough      = 0.85f,
     .coat       = 0,
     .coatRough  = 1,
     .exposure   = 1.2f},
    // A tournament hall: one large even panel overhead, neutral, a grey carpet; plain and easy to read.
    {.id         = "hall",
     .name       = "Tournament hall",
     .lightCount = 1,
     .lights     = {{{{0.0f, 10.0f, 0.0f}, {0, 0, 0}, 16, 16, 4300, 0.55f}}},
     .skyKelvin  = 5000,
     .sky        = 0.25f,
     .floor      = {70, 72, 76},
     .weave      = 0.45f,
     .rough      = 1,
     .coat       = 0,
     .coatRough  = 1,
     .exposure   = 1.2f},
};

// A small, fixed turn of each piece, as hands leave them: the same for a square every time.
float jitter(int square) {
    unsigned h = unsigned(square) * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return (float(h & 0xffff) / 65535.0f - 0.5f) * 0.16f;
}

}  // namespace

std::span<const Studio> studios() { return kStudios; }

std::vector<PlacedPiece> piecesFromFen(const std::string& fen) {
    std::vector<PlacedPiece> out;
    int file = 0, rank = 7;
    for (char c : fen) {
        if (c == ' ') break;
        if (c == '/') {
            if (--rank < 0) return {};
            file = 0;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
        } else {
            static const char* letters = "pnbrqk";
            const char* at             = std::strchr(letters, c | 0x20);
            if (!at || file > 7) return {};
            out.push_back({rank * 8 + file, Kind(at - letters), (c & 0x20) != 0});
            ++file;
        }
        if (file > 8) return {};
    }
    return out;
}

Renderer::~Renderer() { shutdown(); }

bool Renderer::init(const ShaderSources& shaders, const unsigned char* pack, std::size_t packSize, std::string& why) {
    if (!gl::load()) {
        why = "OpenGL 3.3 is missing";
        return false;
    }
    meshes_ = decodePieces(pack, packSize);
    if (meshes_.empty()) {
        why = "the pieces' data is damaged";
        return false;
    }
    if (!reloadShaders(shaders, why)) return false;
    shapes_.clear();
    for (const PieceMesh& m : meshes_)
        shapes_.push_back({m.radius, m.height, profileOf(m.positions, m.indices, m.height)});

    const auto& f    = fn();
    const GpuBvh bvh = buildBvh(meshes_);
    roots_           = bvh.roots;
    f.GenBuffers(3, buffers_);
    f.GenTextures(3, textures_);
    const std::vector<float>* data[3] = {&bvh.nodes, &bvh.triangles, &bvh.normals};
    for (int i = 0; i < 3; ++i) {
        f.BindBuffer(TEXTURE_BUFFER, buffers_[i]);
        f.BufferData(TEXTURE_BUFFER, GLsizeiptr(data[i]->size() * sizeof(float)), data[i]->data(), STATIC_DRAW);
        f.BindTexture(TEXTURE_BUFFER, textures_[i]);
        f.TexBuffer(TEXTURE_BUFFER, RGBA32F, buffers_[i]);
    }
    f.BindTexture(TEXTURE_BUFFER, 0);
    f.BindBuffer(TEXTURE_BUFFER, 0);
    f.GenVertexArrays(1, &vao_);
    f.GenQueries(2, queries_);
    return true;
}

bool Renderer::reloadShaders(const ShaderSources& shaders, std::string& why) {
    std::string log;
    GLuint programs[4]           = {};
    const std::string* source[4] = {&shaders.trace, &shaders.prepare, &shaders.atrous, &shaders.display};
    bool ok                      = true;
    for (int i = 0; i < 4 && ok; ++i) ok = (programs[i] = link(*source[i], log)) != 0;
    if (!ok) {
        for (GLuint p : programs)
            if (p) fn().DeleteProgram(p);
        why = log;
        return false;
    }
    unsigned* mine[4] = {&traceProgram_, &prepareProgram_, &atrousProgram_, &displayProgram_};
    for (int i = 0; i < 4; ++i) {
        if (*mine[i]) fn().DeleteProgram(*mine[i]);
        *mine[i] = programs[i];
    }
    restart();
    return true;
}

void Renderer::shutdown() {
    if (!vao_) return;
    const auto& f = fn();
    for (unsigned p : {traceProgram_, prepareProgram_, atrousProgram_, displayProgram_}) f.DeleteProgram(p);
    traceProgram_ = prepareProgram_ = atrousProgram_ = displayProgram_ = 0;
    f.DeleteVertexArrays(1, &vao_);
    f.DeleteBuffers(3, buffers_);
    f.DeleteTextures(3, textures_);
    f.DeleteQueries(2, queries_);
    if (accumFbo_) {
        unsigned fbos[] = {accumFbo_, prepareFbo_, filterFbo_[0], filterFbo_[1], displayFbo_};
        f.DeleteFramebuffers(5, fbos);
        for (Texture& t : target_) f.DeleteTextures(1, &t.texture), t.texture = 0;
    }
    vao_ = accumFbo_ = 0;
}

void Renderer::setPieces(const std::vector<PlacedPiece>& pieces) {
    if (pieces == pieces_) return;
    pieces_         = pieces;
    instancesDirty_ = true;
    restart();
}

void Renderer::setCamera(const Camera& camera) {
    if (std::memcmp(&camera, &camera_, sizeof camera) == 0) return;
    camera_ = camera;
    restart();
}

void Renderer::setLighting(const Lighting& lighting) {
    if (std::memcmp(&lighting, &lighting_, sizeof lighting) == 0) return;
    // The exposure is applied when the image is shown: the samples stay good.
    const bool exposureOnly = lighting.studio == lighting_.studio && lighting.lights == lighting_.lights &&
                              lighting.ambient == lighting_.ambient && lighting.bounces == lighting_.bounces;
    lighting_ = lighting;
    if (exposureOnly) dirty_ = true;
    else restart();
}

void Renderer::setFinish(const Finish& finish) {
    if (std::memcmp(&finish, &finish_, sizeof finish) == 0) return;
    finish_ = finish;
    restart();
}

void Renderer::setTargetSamples(int samples) {
    samples = std::max(1, samples);
    if (samples == targetSamples_) return;
    targetSamples_ = samples;
    dirty_         = true;
}

void Renderer::setOverlay(const Overlay& overlay) {
    if (overlay == overlay_) return;
    overlay_ = overlay;
    dirty_   = true;
}

void Overlay::addTint(int square, std::uint32_t colour) {
    if (square < 0 || square > 63) return;
    auto channel = [](std::uint32_t c, int i) { return float((c >> (8 * i)) & 0xFF) / 255.0f; };
    const std::uint32_t under = tint[std::size_t(square)];
    const float a2 = channel(colour, 3), a1 = channel(under, 3), a = a2 + a1 * (1 - a2);
    if (a <= 0) return;
    std::uint32_t out = std::uint32_t(std::lround(a * 255)) << 24;
    for (int i = 0; i < 3; ++i) {
        const float c = (channel(colour, i) * a2 + channel(under, i) * a1 * (1 - a2)) / a;
        out |= std::uint32_t(std::lround(std::clamp(c, 0.0f, 1.0f) * 255)) << (8 * i);
    }
    tint[std::size_t(square)] = out;
}

void Renderer::setSeed(int seed) {
    if (seed == seed_) return;
    seed_ = seed;
    restart();
}

void Renderer::setDenoise(bool on) {
    if (on == denoise_) return;
    denoise_  = on;
    dirty_    = true;
    filtered_ = -1;
}

// Each piece as 4 vec4 uniforms of the trace shader: the same for every pixel, read from the uniforms' registers.
void Renderer::uploadInstances() {
    std::vector<float>& data = instances_;
    data.clear();
    for (const PlacedPiece& p : pieces_) {
        const int file = p.square % 8, rank = p.square / 8;
        const float x = float(file) - 3.5f, z = 3.5f - float(rank);
        // The knight looks towards -z, the opponent of a white piece; a black piece turned round.
        const float angle  = (p.black ? 3.14159265f : 0.0f) + jitter(p.square);
        const PieceMesh& m = meshes_[std::size_t(p.kind)];
        const float r      = m.radius;
        data.insert(data.end(), {x, z, std::cos(angle), std::sin(angle)});
        data.insert(data.end(), {float(int(p.kind)), p.black ? 1.0f : 0.0f, 1.0f, float(roots_[std::size_t(p.kind)])});
        data.insert(data.end(), {x - r, 0.0f, z - r, 0.0f});
        data.insert(data.end(), {x + r, m.height, z + r, 0.0f});
        if (data.size() >= kMaxInstances * 16) break;
    }
    instancesDirty_ = false;
}

void Renderer::resize(int width, int height) {
    const auto& f = fn();
    if (!accumFbo_) {
        unsigned fbos[5];
        f.GenFramebuffers(5, fbos);
        accumFbo_ = fbos[0], prepareFbo_ = fbos[1], filterFbo_[0] = fbos[2], filterFbo_[1] = fbos[3];
        displayFbo_ = fbos[4];
        for (Texture& t : target_) f.GenTextures(1, &t.texture);
    }
    width_ = width, height_ = height;
    // The timings were of another size: measured again, from a band of a few rows.
    passMs_ = 0;
    bands_  = std::max(1, height / 32);
    // Sums over many samples need 32-bit floats; the filter's working images do with 16.
    const GLint formats[kTargets] = {GLint(RGBA32F), GLint(RGBA32F), GLint(RGBA32F), GLint(RGBA16F),
                                     GLint(RGBA16F), GLint(RGBA16F), GLint(RGBA8)};
    for (int i = 0; i < kDisplay; ++i) {
        f.BindTexture(TEXTURE_2D, target_[i].texture);
        f.TexImage2D(TEXTURE_2D, 0, formats[i], width, height, 0, RGBA, i == kDisplay ? UNSIGNED_BYTE : FLOAT, nullptr);
        f.TexParameteri(TEXTURE_2D, TEXTURE_MIN_FILTER, GLint(i == kDisplay ? LINEAR : NEAREST));
        f.TexParameteri(TEXTURE_2D, TEXTURE_MAG_FILTER, GLint(i == kDisplay ? LINEAR : NEAREST));
        f.TexParameteri(TEXTURE_2D, TEXTURE_WRAP_S, GLint(CLAMP_TO_EDGE));
        f.TexParameteri(TEXTURE_2D, TEXTURE_WRAP_T, GLint(CLAMP_TO_EDGE));
    }
    f.BindTexture(TEXTURE_2D, 0);

    GLint previous = 0;
    f.GetIntegerv(FRAMEBUFFER_BINDING, &previous);
    auto attach = [&](unsigned fbo, std::initializer_list<int> targets) {
        f.BindFramebuffer(FRAMEBUFFER, fbo);
        GLenum buffers[4];
        int n = 0;
        for (int t : targets) {
            f.FramebufferTexture2D(FRAMEBUFFER, COLOR_ATTACHMENT0 + GLenum(n), TEXTURE_2D, target_[t].texture, 0);
            buffers[n] = COLOR_ATTACHMENT0 + GLenum(n);
            ++n;
        }
        f.DrawBuffers(n, buffers);
    };
    attach(accumFbo_, {kRadiance, kGuide, kGeometry});
    attach(prepareFbo_, {kFilterA, kGeometryMean});
    attach(filterFbo_[0], {kFilterA});
    attach(filterFbo_[1], {kFilterB});
    f.BindFramebuffer(FRAMEBUFFER, GLuint(previous));
    restart();
}

void Renderer::resizeDisplay(int width, int height) {
    const auto& f = fn();
    outWidth_ = width, outHeight_ = height;
    f.BindTexture(TEXTURE_2D, target_[kDisplay].texture);
    f.TexImage2D(TEXTURE_2D, 0, GLint(RGBA8), width, height, 0, RGBA, UNSIGNED_BYTE, nullptr);
    f.BindTexture(TEXTURE_2D, 0);
    GLint previous = 0;
    f.GetIntegerv(FRAMEBUFFER_BINDING, &previous);
    f.BindFramebuffer(FRAMEBUFFER, displayFbo_);
    f.FramebufferTexture2D(FRAMEBUFFER, COLOR_ATTACHMENT0, TEXTURE_2D, target_[kDisplay].texture, 0);
    f.BindFramebuffer(FRAMEBUFFER, GLuint(previous));
    dirty_ = true;
}

void Renderer::tracePass(int y0, int y1) {
    const auto& f  = fn();
    const GLuint p = traceProgram_;
    f.UseProgram(p);
    for (int i = 0; i < 3; ++i) {
        f.ActiveTexture(TEXTURE0 + GLenum(i));
        f.BindTexture(TEXTURE_BUFFER, textures_[i]);
    }
    f.Uniform1i(loc(p, "uNodes"), 0);
    f.Uniform1i(loc(p, "uTris"), 1);
    f.Uniform1i(loc(p, "uNorms"), 2);
    const int count = int(instances_.size() / 16);
    f.Uniform1i(loc(p, "uInstCount"), count);
    if (count > 0) {
        // Stored as count blocks of 4 vec4, sent as the 4 arrays, one vec4 of each per piece.
        std::vector<float> column(std::size_t(count) * 4);
        const char* names[4] = {"uInstXf", "uInstInfo", "uInstMin", "uInstMax"};
        for (int k = 0; k < 4; ++k) {
            for (int i = 0; i < count; ++i)
                std::memcpy(&column[std::size_t(i) * 4], &instances_[std::size_t(i) * 16 + std::size_t(k) * 4], 16);
            f.Uniform4fv(loc(p, names[k]), count, column.data());
        }
    }
    // The squares' pieces, for walking the board square by square; the tallest piece's top.
    GLint squares[64];
    std::fill(std::begin(squares), std::end(squares), -1);
    for (int i = 0; i < count && i < int(pieces_.size()); ++i)
        if (pieces_[std::size_t(i)].square >= 0 && pieces_[std::size_t(i)].square < 64) squares[pieces_[std::size_t(i)].square] = i;
    f.Uniform1iv(loc(p, "uSquare"), 64, squares);
    float top = 0;
    for (const PieceShape& s : shapes_) top = std::max(top, s.height);
    f.Uniform1f(loc(p, "uPieceTop"), top + 1e-3f);
    f.Uniform2f(loc(p, "uResolution"), float(width_), float(height_));
    f.Uniform1i(loc(p, "uSample"), samples_);
    f.Uniform1i(loc(p, "uSeed"), seed_);
    // A preview carries one bounce: less to compute, and far less noise for one sample.
    f.Uniform1i(loc(p, "uBounces"), scale_ > 1 ? std::min(lighting_.bounces, 1) : lighting_.bounces);

    // The camera: on a sphere round the target, a thin lens focused on it.
    const Camera& c = camera_;
    const View view = viewOf(c, float(width_) / float(height_));
    f.Uniform3f(loc(p, "uCamPos"), view.eye.x, view.eye.y, view.eye.z);
    f.Uniform3f(loc(p, "uCamRight"), view.right.x, view.right.y, view.right.z);
    f.Uniform3f(loc(p, "uCamUp"), view.up.x, view.up.y, view.up.z);
    f.Uniform3f(loc(p, "uCamForward"), view.forward.x, view.forward.y, view.forward.z);
    f.Uniform1f(loc(p, "uTanHalfFov"), std::tan(c.fovY / 2));
    f.Uniform1f(loc(p, "uAperture"), c.aperture);
    f.Uniform1f(loc(p, "uFocusDist"), c.distance);

    // The studio's lights, a light picked by its power; its ceiling's glow; its floor.
    const Lighting& l = lighting_;
    const Studio& s   = kStudios[std::clamp(l.studio, 0, int(std::size(kStudios)) - 1)];
    const int lightCount = std::clamp(s.lightCount, 0, kMaxLights);
    RectLight lights[kMaxLights];
    float power[kMaxLights], sun[kMaxLights], total = 0;
    for (int i = 0; i < lightCount; ++i) {
        const StudioLight& sl = s.lights[std::size_t(i)];
        const V3 centre{sl.centre[0], sl.centre[1], sl.centre[2]}, target{sl.target[0], sl.target[1], sl.target[2]};
        const V3 colour = blackbody(sl.kelvin) * (sl.radiance * l.lights);
        if (sl.sun > 0) {
            // Its direction; the radiance of a disc of that size giving that light.
            const float cosMax = std::cos(sl.sun * 0.5f * 3.14159265f / 180.0f);
            lights[i] = {normalize(centre - target), {0, 0, 0}, {0, 0, 0}, colour * (1.0f / (6.2831853f * (1 - cosMax))), 0};
            sun[i]    = cosMax;
            power[i]  = (colour.x + colour.y + colour.z) * 100.0f;  // as a large light would bring to the board
        } else {
            lights[i]   = rectLight(centre, target, sl.width, sl.height, colour);
            sun[i]      = 0;
            const V3& L = lights[i].radiance;
            power[i]    = (L.x + L.y + L.z) * lights[i].area + 1e-6f;
        }
        total += power[i];
    }
    float centre[3 * kMaxLights], u[3 * kMaxLights], v[3 * kMaxLights], radiance[3 * kMaxLights], cdf[kMaxLights];
    float acc = 0;
    for (int i = 0; i < lightCount; ++i) {
        const RectLight& r = lights[i];
        std::memcpy(centre + 3 * i, &r.centre, 12);
        std::memcpy(u + 3 * i, &r.u, 12);
        std::memcpy(v + 3 * i, &r.v, 12);
        std::memcpy(radiance + 3 * i, &r.radiance, 12);
        acc += power[i] / total;
        cdf[i] = acc;
    }
    f.Uniform1i(loc(p, "uLightCount"), lightCount);
    if (lightCount > 0) {
        cdf[lightCount - 1] = 1.0f;
        f.Uniform3fv(loc(p, "uLightC"), lightCount, centre);
        f.Uniform3fv(loc(p, "uLightU"), lightCount, u);
        f.Uniform3fv(loc(p, "uLightV"), lightCount, v);
        f.Uniform3fv(loc(p, "uLightL"), lightCount, radiance);
        f.Uniform1fv(loc(p, "uLightCdf"), lightCount, cdf);
        f.Uniform1fv(loc(p, "uLightSun"), lightCount, sun);
    }
    const V3 sky = blackbody(s.skyKelvin) * (s.sky * l.ambient), backdrop = blackbody(s.skyKelvin) * (s.backdrop * l.ambient);
    f.Uniform3f(loc(p, "uSky"), sky.x, sky.y, sky.z);
    f.Uniform3f(loc(p, "uBackdrop"), backdrop.x, backdrop.y, backdrop.z);
    f.Uniform4f(loc(p, "uFloor"), linear(s.floor[0]), linear(s.floor[1]), linear(s.floor[2]), s.weave);
    f.Uniform3f(loc(p, "uFloorSurface"), s.rough, s.coat, s.coatRough);
    f.Uniform2f(loc(p, "uVarnish"), std::clamp(finish_.varnish, 0.0f, 1.0f), std::clamp(finish_.varnishRough, 0.01f, 1.0f));

    f.Scissor(0, y0, width_, y1 - y0);
    f.DrawArrays(TRIANGLES, 0, 3);
}

// The demodulated mean and its variance, then five à-trous steps of growing reach, edge-aware.
void Renderer::filterPasses() {
    const auto& f = fn();
    f.Disable(BLEND);
    f.Disable(SCISSOR_TEST);
    auto bind = [&](int unit, int target) {
        f.ActiveTexture(TEXTURE0 + GLenum(unit));
        f.BindTexture(TEXTURE_2D, target_[target].texture);
    };

    f.BindFramebuffer(FRAMEBUFFER, prepareFbo_);
    f.UseProgram(prepareProgram_);
    bind(0, kRadiance), bind(1, kGuide), bind(2, kGeometry);
    f.Uniform1i(loc(prepareProgram_, "uRadiance"), 0);
    f.Uniform1i(loc(prepareProgram_, "uGuide"), 1);
    f.Uniform1i(loc(prepareProgram_, "uGeometry"), 2);
    f.DrawArrays(TRIANGLES, 0, 3);

    f.UseProgram(atrousProgram_);
    f.Uniform1i(loc(atrousProgram_, "uIn"), 0);
    f.Uniform1i(loc(atrousProgram_, "uGeometry"), 1);
    bind(1, kGeometryMean);
    int in = kFilterA;
    for (int step = 0; step < 5; ++step) {
        const int out = in == kFilterA ? kFilterB : kFilterA;
        f.BindFramebuffer(FRAMEBUFFER, filterFbo_[out == kFilterA ? 0 : 1]);
        bind(0, in);
        f.Uniform1i(loc(atrousProgram_, "uStep"), 1 << step);
        f.DrawArrays(TRIANGLES, 0, 3);
        in = out;
    }
    filterResult_ = in;
    filtered_     = samples_;
}

void Renderer::displayPass() {
    const auto& f = fn();
    f.BindFramebuffer(FRAMEBUFFER, displayFbo_);
    f.Disable(BLEND);
    f.Disable(SCISSOR_TEST);
    const GLuint p = displayProgram_;
    f.UseProgram(p);
    const int inputs[4]  = {kRadiance, kGuide, filterResult_, kGeometry};
    const char* names[4] = {"uRadiance", "uGuide", "uFiltered", "uGeometry"};
    for (int i = 0; i < 4; ++i) {
        f.ActiveTexture(TEXTURE0 + GLenum(i));
        f.BindTexture(TEXTURE_2D, target_[inputs[i]].texture);
        f.Uniform1i(loc(p, names[i]), i);
    }
    f.Uniform1i(loc(p, "uDenoise"), denoise_ && filtered_ >= 0 ? 1 : 0);
    f.Uniform1f(loc(p, "uExposure"), lighting_.exposure);
    f.Uniform2f(loc(p, "uScale"), float(width_) / float(outWidth_), float(height_) / float(outHeight_));
    f.Uniform2f(loc(p, "uOutSize"), float(outWidth_), float(outHeight_));

    // The overlay: the camera's rays again, to find the board under each pixel.
    const View view = viewOf(camera_, float(outWidth_) / float(outHeight_));
    f.Uniform3f(loc(p, "uCamPos"), view.eye.x, view.eye.y, view.eye.z);
    f.Uniform3f(loc(p, "uCamRight"), view.right.x, view.right.y, view.right.z);
    f.Uniform3f(loc(p, "uCamUp"), view.up.x, view.up.y, view.up.z);
    f.Uniform3f(loc(p, "uCamForward"), view.forward.x, view.forward.y, view.forward.z);
    f.Uniform1f(loc(p, "uTanHalfFov"), view.tanHalfFov);
    auto rgba = [](std::uint32_t c, float* out) {
        for (int i = 0; i < 4; ++i) out[i] = float((c >> (8 * i)) & 0xFF) / 255.0f;
    };
    float tint[64 * 4];
    GLint marks[64];
    for (int i = 0; i < 64; ++i) {
        rgba(overlay_.tint[std::size_t(i)], tint + 4 * i);
        marks[i] = GLint(overlay_.mark[std::size_t(i)]);
    }
    f.Uniform4fv(loc(p, "uTint"), 64, tint);
    f.Uniform1iv(loc(p, "uMark"), 64, marks);
    float colour[4];
    rgba(overlay_.markColour, colour);
    f.Uniform4f(loc(p, "uMarkColour"), colour[0], colour[1], colour[2], colour[3]);
    rgba(overlay_.checkColour, colour);
    f.Uniform4f(loc(p, "uCheckColour"), colour[0], colour[1], colour[2], colour[3]);
    const int arrows = int(std::min(overlay_.arrows.size(), Overlay::kMaxArrows));
    float ends[Overlay::kMaxArrows * 4], colours[Overlay::kMaxArrows * 4];
    int dashed = 0;  // a bit per arrow
    for (int i = 0; i < arrows; ++i) {
        const Overlay::Arrow& a = overlay_.arrows[std::size_t(i)];
        const Vec3 from = squareCentre(a.from), to = squareCentre(a.to);
        ends[4 * i] = from.x, ends[4 * i + 1] = from.z, ends[4 * i + 2] = to.x, ends[4 * i + 3] = to.z;
        rgba(a.colour, colours + 4 * i);
        if (a.dashed) dashed |= 1 << i;
    }
    f.Uniform1i(loc(p, "uArrowCount"), arrows);
    f.Uniform1i(loc(p, "uArrowDashed"), dashed);
    if (arrows > 0) {
        f.Uniform4fv(loc(p, "uArrow"), arrows, ends);
        f.Uniform4fv(loc(p, "uArrowColour"), arrows, colours);
    }

    f.Viewport(0, 0, outWidth_, outHeight_);
    f.DrawArrays(TRIANGLES, 0, 3);
}

bool Renderer::render(int width, int height, double budgetMs, int scale) {
    if (width <= 0 || height <= 0 || !vao_) return false;
    scale        = std::clamp(scale, 1, 8);
    const int iw = (width + scale - 1) / scale, ih = (height + scale - 1) / scale;
    if (iw != width_ || ih != height_) resize(iw, ih);  // creates the targets the first time
    if (width != outWidth_ || height != outHeight_) resizeDisplay(width, height);
    scale_ = scale;
    if (!dirty_ && converged()) return false;

    const auto& f = fn();
    // Read the timings of the frames before, without waiting for them.
    for (int q = 0; q < 2; ++q) {
        if (!queryPending_[q]) continue;
        GLint ready = 0;
        f.GetQueryObjectiv(queries_[q], QUERY_RESULT_AVAILABLE, &ready);
        if (!ready) continue;
        GLuint64 ns = 0;
        f.GetQueryObjectui64v(queries_[q], QUERY_RESULT, &ns);
        queryPending_[q] = false;
        // A full pass over the image, from the bands that query timed.
        passMs_ = double(ns) / 1e6 / std::max(querySteps_[q], 1) * queryBands_[q];
    }

    // What the program had bound, given back at the end: the GUI draws next.
    GLint previousFbo = 0, previousProgram = 0, viewport[4];
    f.GetIntegerv(FRAMEBUFFER_BINDING, &previousFbo);
    f.GetIntegerv(CURRENT_PROGRAM, &previousProgram);
    f.GetIntegerv(VIEWPORT, viewport);
    const bool blend = f.IsEnabled(BLEND), scissor = f.IsEnabled(SCISSOR_TEST);

    f.BindVertexArray(vao_);
    f.Viewport(0, 0, width_, height_);
    if (instancesDirty_) uploadInstances();

    if (!converged()) {
        f.BindFramebuffer(FRAMEBUFFER, accumFbo_);
        if (samples_ == 0 && band_ == 0) {
            f.Disable(SCISSOR_TEST);
            f.ClearColor(0, 0, 0, 0);
            f.Clear(COLOR_BUFFER_BIT);
        }
        f.Enable(BLEND);
        f.BlendEquation(FUNC_ADD);
        f.BlendFunc(ONE, ONE);
        f.Enable(SCISSOR_TEST);

        // Bands of rows, as many as keep each within the budget; as many of them this frame as fit in it.
        if (band_ == 0 && passMs_ > 0) bands_ = std::clamp(int(std::ceil(passMs_ / budgetMs)), 1, 64);
        const double perBand = passMs_ > 0 ? passMs_ / bands_ : budgetMs;
        // Until a pass is timed, one band; never more than a few samples in a frame, whatever the timings say. A
        // preview has its whole first sample at once.
        int steps = passMs_ > 0 ? std::clamp(int(budgetMs / std::max(perBand, 0.05)), 1, 4 * bands_) : 1;
        if (scale_ > 1 && samples_ == 0) steps = std::max(steps, bands_ - band_);
        const int q          = queryNext_;
        const bool timing    = !queryPending_[q];
        if (timing) f.BeginQuery(TIME_ELAPSED, queries_[q]);
        int timedSteps = 0;
        for (; steps > 0 && !converged(); --steps) {
            const int y0 = height_ * band_ / bands_, y1 = height_ * (band_ + 1) / bands_;
            tracePass(y0, y1);
            ++timedSteps;
            if (++band_ == bands_) {
                band_ = 0;
                ++samples_;
            }
        }
        if (timing) {
            f.EndQuery(TIME_ELAPSED);
            queryPending_[q] = true;
            querySteps_[q]   = timedSteps;
            queryBands_[q]   = bands_;
            queryNext_       = 1 - q;
        }
    }

    // The filter once per whole sample, while it is of use.
    if (denoise_ && samples_ > 0 && filtered_ != samples_ && samples_ < 4096) filterPasses();
    // What is shown stays until the new image has a whole sample.
    const bool show = samples_ > 0;
    if (show) displayPass();
    dirty_ = !show;

    f.BindFramebuffer(FRAMEBUFFER, GLuint(previousFbo));
    f.UseProgram(GLuint(previousProgram));
    f.Viewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    if (blend) f.Enable(BLEND);
    else f.Disable(BLEND);
    if (scissor) f.Enable(SCISSOR_TEST);
    else f.Disable(SCISSOR_TEST);
    f.BindVertexArray(0);
    f.ActiveTexture(TEXTURE0);
    return true;
}

void Renderer::finish() const { fn().Finish(); }

void Renderer::readPixels(std::vector<std::uint8_t>& rgba) const {
    const auto& f = fn();
    const int w = outWidth_, h = outHeight_;
    rgba.assign(std::size_t(w) * h * 4, 0);
    GLint previous = 0;
    f.GetIntegerv(FRAMEBUFFER_BINDING, &previous);
    f.BindFramebuffer(FRAMEBUFFER, displayFbo_);
    f.PixelStorei(PACK_ALIGNMENT, 1);
    f.ReadPixels(0, 0, w, h, RGBA, UNSIGNED_BYTE, rgba.data());
    f.BindFramebuffer(FRAMEBUFFER, GLuint(previous));
    const std::size_t row = std::size_t(w) * 4;
    std::vector<std::uint8_t> tmp(row);
    for (int y = 0; y < h / 2; ++y) {
        std::memcpy(tmp.data(), &rgba[y * row], row);
        std::memcpy(&rgba[y * row], &rgba[(h - 1 - y) * row], row);
        std::memcpy(&rgba[(h - 1 - y) * row], tmp.data(), row);
    }
}

void Renderer::readAccumulation(std::vector<float>& rgba) const {
    const auto& f = fn();
    rgba.assign(std::size_t(width_) * height_ * 4, 0);
    GLint previous = 0;
    f.GetIntegerv(FRAMEBUFFER_BINDING, &previous);
    f.BindFramebuffer(READ_FRAMEBUFFER, accumFbo_);
    f.PixelStorei(PACK_ALIGNMENT, 4);
    f.ReadPixels(0, 0, width_, height_, RGBA, FLOAT, rgba.data());
    f.BindFramebuffer(FRAMEBUFFER, GLuint(previous));
}

}  // namespace fcg::board3d
