#pragma once

// The 3D board (board3d/renderer.hpp) where a 2D board would be: an image of the same size, the eval bar left of it,
// the BoardDecor laid over its squares as drawBoard() lays it. A left click picks a square as on the 2D board; the
// right button held turns round the board, the wheel comes closer, a click of the wheel brings the view back.
// Flipping the board turns the camera half way round.
//
// The image refines while nothing changes, a few milliseconds of the graphics card each frame, then rests. It is drawn
// only where the window is drawn with OpenGL 3.3 or later (renderer.hpp): elsewhere available() is false, why() says
// why, and the caller draws in 2D.

#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <chess.hpp>

#include "appearance.hpp"
#include "board3d/renderer.hpp"
#include "board_view.hpp"

namespace fcg {

class Board3d {
   public:
    Board3d();
    Board3d(const Board3d&)            = delete;
    Board3d& operator=(const Board3d&) = delete;
    ~Board3d();  // with the OpenGL context still current

    // Whether it can be drawn: the renderer is made at the first call (the shaders compiled, the pieces read).
    [[nodiscard]] bool available();
    [[nodiscard]] const std::string& why() const { return why_; }

    struct Input {
        std::optional<chess::Square> clicked;  // a left click on a square, a piece's or the board's
        bool right_click = false;              // a right click that did not turn the board
    };
    // Draws the position at the cursor and reserves `size` x `size`, plus the eval bar if shown, as drawBoard().
    Input draw(const chess::Board& board, float size, const BoardDecor& decor);
    Input draw(const std::array<chess::Piece, 64>& pieces, float size, const BoardDecor& decor,
               std::optional<chess::Square> check = std::nullopt);

    // While the camera moves: frames wanted at a steadier pace.
    [[nodiscard]] bool moving() const;
    // The graphics card's memory given back while the 3D board is not wanted; made again when drawn.
    void release();

   private:
    std::unique_ptr<board3d::Renderer> renderer_;
    std::string why_;
    bool tried_ = false;

    // The view: half turns for flipping, animated; the user's turn, height and closeness on top.
    float yaw_      = 0;  // the half turns' angle shown
    float yaw_from_ = 0, yaw_to_ = 0;
    std::chrono::steady_clock::time_point turn_start_{};
    bool flipped_     = false;
    bool placed_      = false;  // the first draw sets the yaw without turning
    float yaw_offset_ = 0;
    float pitch_      = 0;
    float zoom_       = 1;
    bool right_down_ = false, dragged_ = false;
    board3d::Camera shown_{};
    std::chrono::steady_clock::time_point last_move_{};
    int last_pixels_  = 0;
    double full_pass_ = 0;  // ms of a whole sample at full size, measured while still
};

// The lightings (board3d::studios()), by the id the settings keep; the first for an unknown one.
[[nodiscard]] int studioIndex(std::string_view id);
// A lighting's name, translated.
[[nodiscard]] const char* studioName(int index);

// The board's finish for the settings' varnish and gloss.
[[nodiscard]] board3d::Finish finishOf(const appearance::Board3d& settings);
// The pieces of a position as the renderer places them.
[[nodiscard]] std::vector<board3d::PlacedPiece> placedPieces(const std::array<chess::Piece, 64>& pieces);
// What a BoardDecor lays over the 3D board, as drawBoard() lays it over the 2D one.
[[nodiscard]] board3d::Overlay overlayOf(const BoardDecor& decor, const std::array<chess::Piece, 64>& pieces,
                                         std::optional<chess::Square> check, const appearance::BoardColours& colours);

}  // namespace fcg
