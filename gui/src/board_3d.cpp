#include "board_3d.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <imgui.h>

#include "console.hpp"
#include "i18n.hpp"
#include "renderer.hpp"

namespace fcg::assets {
extern const unsigned char board3d_trace[];
extern const std::size_t board3d_trace_size;
extern const unsigned char board3d_prepare[];
extern const std::size_t board3d_prepare_size;
extern const unsigned char board3d_atrous[];
extern const std::size_t board3d_atrous_size;
extern const unsigned char board3d_display[];
extern const std::size_t board3d_display_size;
extern const unsigned char board3d_pieces[];
extern const std::size_t board3d_pieces_size;
}  // namespace fcg::assets

namespace fcg {

namespace {

struct StudioName {
    const char* id;
    const char* name;
};
constexpr StudioName STUDIO_NAMES[] = {
    {"studio", N_("Studio")},        {"low_key", N_("Low key")},     {"high_key", N_("High key")},
    {"showroom", N_("Showroom")},    {"window", N_("Window light")}, {"golden_hour", N_("Golden hour")},
    {"hall", N_("Tournament hall")},
};

constexpr float PI            = 3.14159265f;
constexpr float DEFAULT_PITCH = 0.95f;  // radians above the board: steeper than a photograph, to play on
constexpr float MAX_DISTANCE  = 24.0f;  // inside the studio (trace.frag: 36 squares round)
constexpr double BUDGET_MS    = 8.0;    // of the graphics card each frame while the image refines
constexpr auto TURN_TIME      = std::chrono::milliseconds(600);
constexpr auto STILL_AFTER    = std::chrono::milliseconds(150);  // the camera still for that long: the full image

std::string text(const unsigned char* data, std::size_t size) { return {reinterpret_cast<const char*>(data), size}; }

// The preview's scale while the camera moves: a whole sample in about 12 ms, from the time of one at full size.
int previewScale(double full_pass_ms) {
    if (full_pass_ms <= 0) return 2;
    return std::clamp(static_cast<int>(std::ceil(std::sqrt(full_pass_ms / 12.0))), 2, 6);
}

}  // namespace

int studioIndex(std::string_view id) {
    const auto all = board3d::studios();
    for (std::size_t i = 0; i < all.size(); ++i)
        if (id == all[i].id) return static_cast<int>(i);
    return 0;
}

const char* studioName(int index) {
    const auto all = board3d::studios();
    if (index < 0 || index >= static_cast<int>(all.size())) return "";
    const auto& studio = all[static_cast<std::size_t>(index)];
    for (const auto& n : STUDIO_NAMES)
        if (std::strcmp(n.id, studio.id) == 0) return TR(n.name);
    return studio.name;
}

board3d::Finish finishOf(const appearance::Board3d& settings) {
    board3d::Finish finish;
    finish.varnish = std::clamp(settings.varnish, 0.0f, 1.0f);
    // The gloss as the eye sees it, on a roughness from 0.4 (satin) to 0.02 (a mirror): even in its logarithm.
    finish.varnishRough = 0.4f * std::pow(0.05f, std::clamp(settings.gloss, 0.0f, 1.0f));
    return finish;
}

std::vector<board3d::PlacedPiece> placedPieces(const std::array<chess::Piece, 64>& pieces) {
    std::vector<board3d::PlacedPiece> out;
    for (int i = 0; i < 64; ++i) {
        const auto piece = pieces[static_cast<std::size_t>(i)];
        if (piece == chess::Piece::NONE) continue;
        // PieceType's order is board3d::Kind's: pawn, knight, bishop, rook, queen, king.
        const int type = static_cast<int>(piece.type());
        if (type < 0 || type > 5) continue;
        out.push_back({i, static_cast<board3d::Kind>(type), piece.color() == chess::Color::BLACK});
    }
    return out;
}

board3d::Overlay overlayOf(const BoardDecor& decor, const std::array<chess::Piece, 64>& pieces,
                           std::optional<chess::Square> check, const appearance::BoardColours& colours) {
    board3d::Overlay o;
    o.markColour  = BOARD_TARGET;
    o.checkColour = BOARD_CHECK;
    if (decor.last_move) {
        o.addTint(decor.last_move->from().index(), colours.highlight);
        o.addTint(displayedTarget(*decor.last_move).index(), colours.highlight);
    }
    if (decor.selected) o.addTint(decor.selected->index(), BOARD_SELECTED);
    if (decor.drop) o.addTint(decor.drop->index(), BOARD_DROP);
    using Mark = board3d::Overlay::Mark;
    if (check) o.mark[static_cast<std::size_t>(check->index())] = Mark::Check;
    for (const auto& square : decor.targets) {
        const auto i = static_cast<std::size_t>(square.index());
        if (i < 64) o.mark[i] = pieces[i] != chess::Piece::NONE ? Mark::Ring : Mark::Dot;
    }
    for (const auto& arrow : decor.arrows) {
        if (o.arrows.size() == board3d::Overlay::kMaxArrows) break;
        o.arrows.push_back(
            {arrow.move.from().index(), displayedTarget(arrow.move).index(), arrow.colour, arrow.dashed});
    }
    return o;
}

Board3d::Board3d() : pitch_(DEFAULT_PITCH) {}

Board3d::~Board3d() = default;

bool Board3d::available() {
    if (renderer_) return true;
    if (tried_) return false;
    tried_ = true;
    if (render::path() != render::Path::OpenGL) {
        why_ =
            TR("The window is drawn without OpenGL (the software renderer of the Settings page, or a graphics "
               "driver without OpenGL 3.2): the boards stay flat.");
        return false;
    }
    board3d::ShaderSources shaders{text(assets::board3d_trace, assets::board3d_trace_size),
                                   text(assets::board3d_prepare, assets::board3d_prepare_size),
                                   text(assets::board3d_atrous, assets::board3d_atrous_size),
                                   text(assets::board3d_display, assets::board3d_display_size)};
    auto renderer = std::make_unique<board3d::Renderer>();
    std::string reason;
    if (!renderer->init(shaders, assets::board3d_pieces, assets::board3d_pieces_size, reason)) {
        renderer->shutdown();
        why_ = trf("The graphics driver cannot draw the 3D board ({}): the boards stay flat.", reason);
        Console::instance().add(why_, true);
        return false;
    }
    renderer_ = std::move(renderer);
    why_.clear();
    return true;
}

void Board3d::release() {
    renderer_.reset();
    tried_       = false;
    last_pixels_ = 0;
    full_pass_   = 0;
}

void Board3d::setView(float yaw, float pitch) {
    yaw_offset_ = yaw;
    pitch_      = std::clamp(pitch, 0.25f, 1.55f);
}

bool Board3d::moving() const {
    const auto now = std::chrono::steady_clock::now();
    return renderer_ && (now - last_move_ < STILL_AFTER || now - turn_start_ < TURN_TIME || right_down_);
}

Board3d::Input Board3d::draw(const chess::Board& board, float size, const BoardDecor& decor) {
    std::array<chess::Piece, 64> pieces;
    for (int i = 0; i < 64; ++i) pieces[static_cast<std::size_t>(i)] = board.at(chess::Square(i));
    std::optional<chess::Square> check;
    if (board.inCheck()) check = board.kingSq(board.sideToMove());
    return draw(pieces, size, decor, check);
}

Board3d::Input Board3d::draw(const std::array<chess::Piece, 64>& pieces, float size, const BoardDecor& decor,
                             std::optional<chess::Square> check) {
    Input input;
    const ImVec2 top  = ImGui::GetCursorScreenPos();
    const bool bar    = decor.eval_white >= 0.0f;
    const float space = bar ? evalBarSpace(size) : 0.0f;
    const ImVec2 p0(top.x + space, top.y), p1(p0.x + size, p0.y + size);
    ImGui::InvisibleButton(
        "##board3d", ImVec2(size + space, size),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    if (!available()) return input;
    auto& r        = *renderer_;
    const auto now = std::chrono::steady_clock::now();
    ImGuiIO& io    = ImGui::GetIO();

    // The mouse: the wheel nearer or farther, the right button held turning round, the middle one back to the start.
    if (ImGui::IsItemHovered() && ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY) && io.MouseWheel != 0.0f)
        zoom_ = std::clamp(zoom_ * std::pow(0.9f, io.MouseWheel), 0.4f, 1.6f);
    if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) right_down_ = true, dragged_ = false;
    if (right_down_ && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        dragged_ = true;
        yaw_offset_ -= io.MouseDelta.x * 0.006f;
        pitch_ = std::clamp(pitch_ + io.MouseDelta.y * 0.004f, 0.25f, 1.55f);
    }
    if (right_down_ && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        input.right_click = !dragged_ && ImGui::IsItemHovered();
        right_down_       = false;
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Middle)) yaw_offset_ = 0, pitch_ = DEFAULT_PITCH, zoom_ = 1;

    // Flipped: half a turn, always the same way round.
    if (!placed_ || decor.flipped != flipped_) {
        const float target = yaw_to_ + (placed_ ? PI : (decor.flipped ? PI : 0.0f));
        yaw_from_          = placed_ ? yaw_ : target;
        yaw_to_            = target;
        turn_start_        = placed_ ? now : now - TURN_TIME;
        flipped_           = decor.flipped;
        placed_            = true;
    }
    const float t = std::clamp(
        std::chrono::duration<float>(now - turn_start_).count() / std::chrono::duration<float>(TURN_TIME).count(), 0.0f,
        1.0f);
    yaw_ = yaw_from_ + (yaw_to_ - yaw_from_) * t * t * (3 - 2 * t);

    board3d::Camera camera;
    camera.yaw    = yaw_ + yaw_offset_;
    camera.pitch  = pitch_;
    float tallest = 0;
    for (const auto& s : r.shapes()) tallest = std::max(tallest, s.height);
    camera          = board3d::framed(camera, 1.0f, tallest);
    camera.distance = std::min(camera.distance * zoom_, MAX_DISTANCE);

    const auto& settings = appearance::current().board3d;
    board3d::Lighting lighting;
    lighting.studio = studioIndex(settings.lighting);
    lighting.exposure =
        board3d::studios()[static_cast<std::size_t>(lighting.studio)].exposure * std::exp2(settings.exposure);

    const auto colours = decor.colours ? *decor.colours : appearance::boardColours();
    r.setPieces(placedPieces(pieces));
    r.setCamera(camera);
    r.setLighting(lighting);
    r.setFinish(finishOf(settings));
    r.setTargetSamples(settings.samples);
    r.setOverlay(overlayOf(decor, pieces, check, colours));

    // The pointer's ray; a piece dragged (decor.lifted) has its ghost where it points, floating over the board.
    const ImVec2 m     = io.MousePos;
    const auto pointer = [&] {
        return board3d::rayAt(board3d::viewOf(r.camera(), 1.0f), (m.x - p0.x) / size * 2 - 1,
                              1 - (m.y - p0.y) / size * 2);
    };
    std::optional<board3d::Ghost> ghost;
    if (decor.lifted) {
        for (const auto& placed : r.pieces()) {
            if (placed.square != decor.lifted->index()) continue;
            if (const auto at = board3d::ghostAt(pointer()))
                ghost = board3d::Ghost{placed.kind, placed.black, (*at)[0], (*at)[1], placed.square};
        }
    }
    r.setGhost(ghost);
    if (m.x >= p0.x && m.x < p1.x && m.y >= p0.y && m.y < p1.y) {
        if (const int square = board3d::squareAt(pointer(), r.pieces(), r.shapes()); square >= 0)
            input.hovered = chess::Square(square);
    }

    // The pixels: a new size is shown as the camera moving is, a whole preview at once, then refined.
    const int pixels = std::max(1, static_cast<int>(std::lround(size * io.DisplayFramebufferScale.x)));
    if (std::memcmp(&camera, &shown_, sizeof camera) != 0 || pixels != last_pixels_) last_move_ = now;
    shown_           = camera;
    last_pixels_     = pixels;
    const bool still = now - last_move_ >= STILL_AFTER;
    if (still && r.scale() == 1 && r.passMs() > 0) full_pass_ = r.passMs();
    // Minimized, the window shows nothing: nothing drawn.
    if (io.DisplaySize.x > 0 && io.DisplaySize.y > 0)
        r.render(pixels, pixels, BUDGET_MS, still ? 1 : previewScale(full_pass_));

    auto* dl = ImGui::GetWindowDrawList();
    if (bar) drawEvalBar(dl, top, size, decor.eval_white, decor.flipped);
    if (r.width() > 0)
        dl->AddImage(ImTextureRef(static_cast<ImTextureID>(r.texture())), p0, p1, ImVec2(0, 1), ImVec2(1, 0));

    // The move's judgement over its square, above the piece standing there, a disc facing the viewer as on the 2D
    // board; the frame of a line shown instead of the game round the view.
    if (decor.badge && r.width() > 0) {
        const int square = decor.badge->square.index();
        auto point       = board3d::squareCentre(square);
        const auto piece = pieces[static_cast<std::size_t>(square)];
        const int type   = piece == chess::Piece::NONE ? -1 : static_cast<int>(piece.type());
        if (type >= 0 && type < static_cast<int>(r.shapes().size()))
            point.y += r.shapes()[static_cast<std::size_t>(type)].height;
        if (const auto at = board3d::project(board3d::viewOf(r.camera(), 1.0f), point)) {
            const float radius = size / 8.0f * 0.15f;
            const ImVec2 c(p0.x + ((*at)[0] + 1) / 2 * size + radius * 0.9f,
                           p0.y + (1 - (*at)[1]) / 2 * size - radius * 0.9f);
            drawBoardBadge(
                dl,
                ImVec2(std::clamp(c.x, p0.x + radius, p1.x - radius), std::clamp(c.y, p0.y + radius, p1.y - radius)),
                radius, *decor.badge);
        }
    }
    if (decor.frame) dl->AddRect(ImVec2(p0.x - 2, p0.y - 2), ImVec2(p1.x + 2, p1.y + 2), *decor.frame, 0, 0, 3.0f);

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && m.x >= p0.x && m.x < p1.x && m.y >= p0.y && m.y < p1.y) {
        const float x = (m.x - p0.x) / size * 2 - 1, y = 1 - (m.y - p0.y) / size * 2;
        const int square =
            board3d::squareAt(board3d::rayAt(board3d::viewOf(r.camera(), 1.0f), x, y), r.pieces(), r.shapes());
        if (square >= 0) input.clicked = chess::Square(square);
    }
    return input;
}

}  // namespace fcg
