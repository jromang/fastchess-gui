#pragma once

// The 3D board: a path tracer in a fragment shader (OpenGL 3.3), progressive. Every frame adds samples to an
// accumulation buffer within a time budget, the image converging while the scene stays still; once it has the samples
// asked for, nothing more is drawn and the GPU rests. Any change (a move, the camera, the light) starts it over.
//
// While samples are few, an edge-aware wavelet filter (à-trous, guided by the first hit's normal, distance and albedo,
// and by the variance of each pixel) shows a clean image; it fades out as the samples make it needless.
//
// The scene is small and mostly analytic: the board is a box, the studio round it a seamless sweep, the lights
// rectangles, and only the pieces are meshes, six of them instanced on the squares (bvh.hpp).

#include "board3d/camera.hpp"
#include "board3d/piece_pack.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fcg::board3d {

// A light of a studio: a rectangle at centre, width by height squares, facing target, its colour a temperature. Or,
// with sun > 0, a sun far away beyond centre as seen from target, sun degrees wide, its radiance then the light it
// gives a surface facing it.
struct StudioLight {
    std::array<float, 3> centre, target;
    float width, height;
    float kelvin;
    float radiance;  // luminance
    float sun = 0;
};

inline constexpr int kMaxLights = 4;

// A way of lighting the board, as photographers light a still life: its lights, the glow of the studio's ceiling, the
// colour and finish of its floor and walls, the exposure it is made for. Coordinates in squares, y up, White at +z,
// the playing surface at y = 0.
struct Studio {
    const char* id;    // as the settings keep it
    const char* name;
    int lightCount;
    std::array<StudioLight, kMaxLights> lights;
    float skyKelvin, sky;           // the ceiling's colour and radiance
    float backdrop = 0;             // the glow of the wall and of the floor away from the board, of the ceiling's
                                    // colour: a backdrop lit evenly from hiding
    std::array<float, 3> floor;     // sRGB, 0 to 255
    float weave;                    // how much the floor's cloth shows
    float rough, coat, coatRough;   // its finish: a satin base under a clear coat
    float exposure;
};

[[nodiscard]] std::span<const Studio> studios();

struct Lighting {
    int studio     = 0;     // one of studios()
    float exposure = 1.3f;  // the studio's own, unless changed
    float lights   = 1.0f;  // its lights' power
    float ambient  = 1.0f;  // its ceiling's glow
    int bounces    = 3;
};

// The board's finish: its clear varnish, how much of it (0: waxed, matt) and how rough (0.02: a mirror, 0.4: satin).
struct Finish {
    float varnish      = 1.0f;
    float varnishRough = 0.07f;
};

// What is laid over the board, as the 2D board draws it: tints over squares, a dot or a ring on a square, a disc under
// a king in check, arrows. Colours as Dear ImGui packs them (IM_COL32: red in the low byte), with their opacity. Drawn
// when the image is shown, over the surface of the board; where a piece hides it, the arrows, dots and rings show
// faintly through.
struct Overlay {
    enum class Mark : std::uint8_t { None, Dot, Ring, Check };
    struct Arrow {
        int from = 0, to = 0;  // squares, 0 = a1 ... 63 = h8
        std::uint32_t colour = 0;
        bool dashed                         = false;  // a shaft in dashes, as the 2D board draws an expected move
        bool operator==(const Arrow&) const = default;
    };
    static constexpr std::size_t kMaxArrows = 8;

    std::array<std::uint32_t, 64> tint{};  // the colours laid over each square, composed into one
    std::array<Mark, 64> mark{};
    std::uint32_t markColour  = 0;  // of the dots and rings
    std::uint32_t checkColour = 0;
    std::vector<Arrow> arrows;  // drawn in order, the first kMaxArrows

    // `colour` laid over what the square has already.
    void addTint(int square, std::uint32_t colour);
    bool operator==(const Overlay&) const = default;
};

// A piece dragged with the mouse: drawn by rasterization over the traced image, translucent, kGhostLift above the board
// where the pointer is, and hidden wherever the image shows something nearer the camera (its first hits' distance),
// so that it passes behind the pieces in front of it. The image is not traced again while it moves.
struct Ghost {
    Kind kind  = Kind::Pawn;
    bool black = false;
    float x = 0, z = 0;                       // where its base stands, in squares (ghostAt())
    int square                          = 0;  // the square it was taken from, for its turn as it stands there
    bool operator==(const Ghost&) const = default;
};

// The pieces of a FEN's placement field (the rest ignored); empty if it is not one.
[[nodiscard]] std::vector<PlacedPiece> piecesFromFen(const std::string& fen);

// The fragment shaders, from gui/src/board3d/shaders.
struct ShaderSources {
    std::string trace, prepare, atrous, display;
};

class Renderer {
public:
    Renderer() = default;
    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;
    ~Renderer();

    // With a current OpenGL context. False with why otherwise.
    bool init(const ShaderSources& shaders, const unsigned char* pack, std::size_t packSize, std::string& why);
    // New shader sources, the old ones kept if these do not compile (why says so).
    bool reloadShaders(const ShaderSources& shaders, std::string& why);
    void shutdown();

    void setPieces(const std::vector<PlacedPiece>& pieces);
    void setCamera(const Camera& camera);
    void setLighting(const Lighting& lighting);
    void setFinish(const Finish& finish);
    void setTargetSamples(int samples);
    void setDenoise(bool on);
    void setSeed(int seed);  // another series of random numbers: for measuring the noise against a reference
    void setOverlay(const Overlay& overlay);  // shown at once: the samples stay good
    void setGhost(const std::optional<Ghost>& ghost);  // shown at once: the samples stay good

    // A step of the image of that size within about budgetMs of GPU time. True if the image changed.
    // With scale > 1 the image is traced at 1/scale of that size and shown stretched, a whole sample each frame: the
    // preview while the camera moves. Back at scale 1, the preview stays shown until the image has a whole sample.
    bool render(int width, int height, double budgetMs, int scale = 1);

    [[nodiscard]] unsigned texture() const { return target_[kDisplay].texture; }  // RGBA8, rows bottom up
    [[nodiscard]] int width() const { return outWidth_; }
    [[nodiscard]] int height() const { return outHeight_; }
    [[nodiscard]] int scale() const { return scale_; }
    [[nodiscard]] int samples() const { return samples_; }
    [[nodiscard]] int targetSamples() const { return targetSamples_; }
    [[nodiscard]] double passMs() const { return passMs_; }  // GPU time of a sample over the whole image
    [[nodiscard]] int bands() const { return bands_; }
    [[nodiscard]] bool converged() const { return samples_ >= targetSamples_; }
    [[nodiscard]] bool denoise() const { return denoise_; }
    [[nodiscard]] const Camera& camera() const { return camera_; }
    [[nodiscard]] const std::vector<PlacedPiece>& pieces() const { return pieces_; }
    // The pieces' bounding cylinders, in the order of Kind (for squareAt()); empty before init().
    [[nodiscard]] const std::vector<PieceShape>& shapes() const { return shapes_; }

    // Waits for the GPU to finish what was asked of it: when nothing else paces the frames (no swap, no vsync).
    void finish() const;

    // The image as RGBA, rows top down.
    void readPixels(std::vector<std::uint8_t>& rgba) const;
    // The sums of the samples, alpha their count, rows bottom up: for measuring.
    void readAccumulation(std::vector<float>& rgba) const;

private:
    // The render targets: the accumulation (radiance, guide, geometry), the denoiser's, the image shown.
    enum Target { kRadiance, kGuide, kGeometry, kFilterA, kFilterB, kGeometryMean, kDisplay, kTargets };
    struct Texture {
        unsigned texture = 0;
    };

    void resize(int width, int height);  // the traced image
    void resizeDisplay(int width, int height);
    void restart() { samples_ = 0, band_ = 0, dirty_ = true, filtered_ = -1; }
    void uploadInstances();
    void tracePass(int y0, int y1);
    void filterPasses();
    void displayPass();
    void ghostPass();  // over the image the display pass made

    std::vector<PieceMesh> meshes_;
    std::vector<PieceShape> shapes_;
    Overlay overlay_;
    std::optional<Ghost> ghost_;
    std::vector<int> roots_;
    std::vector<PlacedPiece> pieces_;
    Camera camera_;
    Lighting lighting_;
    Finish finish_;

    unsigned traceProgram_ = 0, prepareProgram_ = 0, atrousProgram_ = 0, displayProgram_ = 0, vao_ = 0;
    // The ghost: its program, a vertex array per kind of piece over its positions, normals and indices, the depth
    // buffer it is drawn with into the image shown.
    unsigned ghostProgram_ = 0, ghostDepth_ = 0;
    std::array<unsigned, 6> ghostVaos_{};
    std::array<unsigned, 18> ghostBuffers_{};
    static constexpr std::size_t kMaxInstances = 40;  // the 32 of a game, and a few to spare
    unsigned buffers_[3]  = {};  // nodes, triangles, normals
    unsigned textures_[3] = {};
    std::vector<float> instances_;  // 16 floats per piece
    Texture target_[kTargets];
    unsigned accumFbo_ = 0, prepareFbo_ = 0, filterFbo_[2] = {}, displayFbo_ = 0;
    int filterResult_ = kFilterA;  // where the last filter pass wrote
    unsigned queries_[2]  = {};
    bool queryPending_[2] = {};
    int querySteps_[2]    = {};
    int queryBands_[2]    = {1, 1};
    int queryNext_        = 0;
    int width_ = 0, height_ = 0;        // the traced image
    int outWidth_ = 0, outHeight_ = 0;  // the image shown
    int scale_ = 1;
    int samples_ = 0, targetSamples_ = 1024;
    int bands_ = 1, band_ = 0;
    int filtered_  = -1;  // the samples the filter last ran on
    double passMs_ = 0;
    bool dirty_    = true;
    bool denoise_  = true;
    int seed_      = 0;
    bool instancesDirty_ = true;
};

}  // namespace fcg::board3d
