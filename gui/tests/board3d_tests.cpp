// The 3D board's work on the processor: the camera's rays, framing the board, the square under the mouse, what a
// BoardDecor lays over the board, the lightings by id. The rendering itself needs a graphics card: tools/board3d's
// viewer shows it.

#include <doctest/doctest.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>

#include "board3d/camera.hpp"
#include "board3d/renderer.hpp"
#include "board_3d.hpp"

using namespace fcg;
using namespace fcg::board3d;

namespace {

constexpr float PI = 3.14159265f;

// The bounding cylinders of the six pieces, about as the baked pack has them.
const std::vector<PieceShape> SHAPES = {{0.30f, 0.80f}, {0.33f, 1.05f}, {0.33f, 1.20f},
                                        {0.34f, 0.95f}, {0.37f, 1.45f}, {0.37f, 1.65f}};

std::array<chess::Piece, 64> piecesOf(const chess::Board& board) {
    std::array<chess::Piece, 64> out;
    for (int i = 0; i < 64; ++i) out[static_cast<std::size_t>(i)] = board.at(chess::Square(i));
    return out;
}

}  // namespace

TEST_CASE("board 3D: the camera's rays and the points they show agree") {
    Camera c;
    c.yaw = 0, c.pitch = 0.9f, c.distance = 15, c.target = {0, 0, 0};
    const View v = viewOf(c, 1.5f);
    // Behind White, above the board, looking at the target.
    CHECK(v.eye.z > 0);
    CHECK(v.eye.y > 0);
    CHECK(std::fabs(v.eye.x) < 1e-4f);
    CHECK(std::fabs(dot(v.forward, v.right)) < 1e-5f);
    CHECK(std::fabs(dot(v.forward, v.up)) < 1e-5f);
    CHECK(v.right.x > 0.99f);  // the h-file on the right

    for (const auto& [x, y] : {std::pair{0.0f, 0.0f}, {0.5f, -0.3f}, {-0.9f, 0.8f}, {1.0f, 1.0f}}) {
        const Ray r = rayAt(v, x, y);
        CHECK(std::fabs(dot(r.dir, r.dir) - 1) < 1e-5f);
        const auto back = project(v, r.origin + r.dir * 7.0f);
        REQUIRE(back.has_value());
        CHECK((*back)[0] == doctest::Approx(x).epsilon(1e-4));
        CHECK((*back)[1] == doctest::Approx(y).epsilon(1e-4));
    }
    CHECK_FALSE(project(v, v.eye - v.forward).has_value());  // behind the camera
}

TEST_CASE("board 3D: framed, the board and its pieces fill the image, centred, from any side and height") {
    for (const float yaw : {0.0f, PI, 0.7f, -2.0f}) {
        for (const float pitch : {0.3f, 0.95f, 1.5f}) {
            for (const float aspect : {1.0f, 1.6f}) {
                CAPTURE(yaw);
                CAPTURE(pitch);
                CAPTURE(aspect);
                Camera c;
                c.yaw = yaw, c.pitch = pitch, c.distance = 40;
                const float margin = 0.03f, height = 1.6f;
                const Camera f = framed(c, aspect, height, margin);
                CHECK(f.yaw == yaw);
                CHECK(f.pitch == pitch);
                CHECK(f.target[1] == c.target[1]);  // the target slides along the board only
                const View v     = viewOf(f, aspect);
                const float edge = kHalfBoard + kFrame;
                float x0 = 9, x1 = -9, y0 = 9, y1 = -9;
                for (const Vec3 p :
                     {Vec3{-edge, 0, -edge}, Vec3{edge, 0, -edge}, Vec3{-edge, 0, edge}, Vec3{edge, 0, edge},
                      Vec3{-edge, -kThickness, edge}, Vec3{edge, -kThickness, edge}, Vec3{-edge, -kThickness, -edge},
                      Vec3{edge, -kThickness, -edge}, Vec3{-3.5f, height, -3.5f}, Vec3{3.5f, height, 3.5f},
                      Vec3{-3.5f, height, 3.5f}, Vec3{3.5f, height, -3.5f}}) {
                    const auto q = project(v, p);
                    REQUIRE(q.has_value());
                    x0 = std::min(x0, (*q)[0]), x1 = std::max(x1, (*q)[0]);
                    y0 = std::min(y0, (*q)[1]), y1 = std::max(y1, (*q)[1]);
                }
                // Inside, with the margin; touching it on the tighter side; centred.
                const float roomY = 1 - 2 * margin, roomX = 1 - 2 * margin / aspect;
                CHECK(x1 <= roomX + 1e-3f);
                CHECK(y1 <= roomY + 1e-3f);
                CHECK(std::max((x1 - x0) / 2 / roomX, (y1 - y0) / 2 / roomY) == doctest::Approx(1).epsilon(1e-3));
                CHECK(std::fabs(x0 + x1) < 2e-3f);
                CHECK(std::fabs(y0 + y1) < 2e-3f);
            }
        }
    }
}

TEST_CASE("board 3D: the square under the mouse, a piece before the board behind it, from either side") {
    for (const float yaw : {0.0f, PI, 0.6f}) {
        CAPTURE(yaw);
        Camera c;
        c.yaw = yaw, c.pitch = 0.95f;
        c            = framed(c, 1, 1.65f);
        const View v = viewOf(c, 1);
        auto at      = [&](Vec3 p) {
            const auto q = project(v, p);
            REQUIRE(q.has_value());
            return rayAt(v, (*q)[0], (*q)[1]);
        };
        // An empty board: the square under each point of each square.
        for (int square = 0; square < 64; ++square) {
            const Vec3 centre = squareCentre(square);
            CHECK(squareAt(at(centre), {}, SHAPES) == square);
            CHECK(squareAt(at(centre + Vec3{0.4f, 0, -0.4f}), {}, SHAPES) == square);
        }
        // Off the board, and the sky.
        CHECK(squareAt(at({4.3f, 0, 0}), {}, SHAPES) == -1);
        CHECK(squareAt(rayAt(v, 0, 5), {}, SHAPES) == -1);

        // A king on e1 and a queen on d8: their heads are theirs, whatever square lies behind them on the board.
        const std::vector<PlacedPiece> pieces = {{4, Kind::King, false}, {59, Kind::Queen, true}};
        CHECK(squareAt(at(squareCentre(4) + Vec3{0, 1.4f, 0}), pieces, SHAPES) == 4);
        CHECK(squareAt(at(squareCentre(59) + Vec3{0, 1.2f, 0}), pieces, SHAPES) == 59);
        CHECK(squareAt(at(squareCentre(36)), pieces, SHAPES) == 36);  // e5, nothing on it
    }
    // Seen from White, the e2 square's far half lies behind a king on e1.
    Camera c;
    c.pitch      = 0.5f;
    c            = framed(c, 1, 1.65f);
    const View v = viewOf(c, 1);
    const auto q = project(v, squareCentre(12) + Vec3{0, 0, -0.45f});
    REQUIRE(q.has_value());
    const std::vector<PlacedPiece> king = {{4, Kind::King, false}};
    const Ray r                         = rayAt(v, (*q)[0], (*q)[1]);
    CHECK(squareAt(r, {}, SHAPES) == 12);
    CHECK(squareAt(r, king, SHAPES) == 4);
}

TEST_CASE("board 3D: the pieces of a position, and what a decor lays over them") {
    const chess::Board start;
    const auto pieces = piecesOf(start);
    const auto placed = placedPieces(pieces);
    CHECK(placed.size() == 32);
    const auto find = [&](int square) {
        return *std::find_if(placed.begin(), placed.end(), [&](const PlacedPiece& p) { return p.square == square; });
    };
    CHECK(find(4).kind == Kind::King);
    CHECK_FALSE(find(4).black);
    CHECK(find(59).kind == Kind::Queen);
    CHECK(find(59).black);
    CHECK(find(57).kind == Kind::Knight);
    CHECK(find(8).kind == Kind::Pawn);

    // Colours composed as one is laid over the other.
    Overlay o;
    o.addTint(0, IM_COL32(255, 0, 0, 255));
    CHECK(o.tint[0] == IM_COL32(255, 0, 0, 255));
    o.addTint(0, IM_COL32(0, 0, 255, 128));
    CHECK(o.tint[0] == IM_COL32(127, 0, 128, 255));
    o.addTint(1, IM_COL32(0, 255, 0, 128));
    o.addTint(1, IM_COL32(0, 255, 0, 128));
    CHECK(o.tint[1] == IM_COL32(0, 255, 0, 192));
    o.addTint(2, 0);
    CHECK(o.tint[2] == 0);

    // White castled short, the king picked up, an arrow for each line: the squares the 2D board marks.
    chess::Board board("r3k2r/pppq1ppp/8/8/8/8/PPPQ1PPP/R4RK1 b kq - 1 1");
    const auto castling = chess::Move::make<chess::Move::CASTLING>(chess::Square::SQ_E1, chess::Square::SQ_H1);
    BoardDecor decor;
    decor.last_move = castling;
    decor.selected  = chess::Square::SQ_E8;
    decor.targets   = {chess::Square::SQ_F8, chess::Square::SQ_D7, chess::Square::SQ_E7};
    for (int i = 0; i < 10; ++i)
        decor.arrows.push_back({chess::Move::make(chess::Square::SQ_A7, chess::Square::SQ_A5), IM_COL32(1, 2, 3, 4)});
    appearance::BoardColours colours;
    colours.highlight  = IM_COL32(200, 100, 0, 255);
    const auto checked = chess::Square::SQ_G1;
    const Overlay d    = overlayOf(decor, piecesOf(board), checked, colours);
    CHECK(d.tint[4] == colours.highlight);  // e1
    CHECK(d.tint[6] == colours.highlight);  // g1, where the king went, not h1
    CHECK(d.tint[7] == 0);
    CHECK(d.tint[60] == BOARD_SELECTED);
    CHECK(d.mark[61] == Overlay::Mark::Dot);   // f8, empty
    CHECK(d.mark[51] == Overlay::Mark::Ring);  // d7, the black queen's
    CHECK(d.mark[52] == Overlay::Mark::Dot);
    CHECK(d.mark[6] == Overlay::Mark::Check);
    CHECK(d.markColour == BOARD_TARGET);
    CHECK(d.checkColour == BOARD_CHECK);
    REQUIRE(d.arrows.size() == Overlay::kMaxArrows);
    CHECK(d.arrows[0].from == 48);
    CHECK(d.arrows[0].to == 32);
    CHECK(d.arrows[0].colour == IM_COL32(1, 2, 3, 4));
}

TEST_CASE("board 3D: the varnish and its gloss, from satin to a mirror") {
    appearance::Board3d s;
    CHECK(finishOf(s).varnish == 1.0f);
    CHECK(finishOf(s).varnishRough == doctest::Approx(0.07).epsilon(0.03));  // the prototype's, by default
    s.gloss = 0;
    CHECK(finishOf(s).varnishRough == doctest::Approx(0.4));
    s.gloss = 1;
    CHECK(finishOf(s).varnishRough == doctest::Approx(0.02));
    s.gloss   = 7;
    s.varnish = -1;
    CHECK(finishOf(s).varnishRough == doctest::Approx(0.02));
    CHECK(finishOf(s).varnish == 0.0f);
}

TEST_CASE("board 3D: the lightings, by an id of their own, named") {
    const auto all = studios();
    REQUIRE(all.size() >= 2);
    std::set<std::string> ids;
    for (int i = 0; i < static_cast<int>(all.size()); ++i) {
        const auto& s = all[static_cast<std::size_t>(i)];
        CHECK(ids.insert(s.id).second);
        CHECK(studioIndex(s.id) == i);
        CHECK(std::string(studioName(i)) == s.name);  // English, as the tests run
        CHECK(s.lightCount <= kMaxLights);
        CHECK(s.exposure > 0);
    }
    CHECK(studioIndex("no such lighting") == 0);
    CHECK(std::string(studioName(-1)).empty());
    CHECK(std::string(studioName(static_cast<int>(all.size()))).empty());
    CHECK(studioIndex(appearance::Board3d{}.lighting) == 0);
}
