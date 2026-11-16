#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include <chess.hpp>

#include "appearance.hpp"

namespace fcg {

// The twelve piece images, rasterized once into a texture atlas: mipmapped with OpenGL; with SDL's renderer, which has
// no mipmaps, also rasterized at half, a quarter and an eighth of the size, so that small boards stay smooth.
class PieceAtlas {
   public:
    PieceAtlas()                             = default;
    PieceAtlas(const PieceAtlas&)            = delete;
    PieceAtlas& operator=(const PieceAtlas&) = delete;
    ~PieceAtlas();

    // Needs the window's renderer (render::open). Returns false if the SVGs could not be rendered. Made again, from
    // other pieces, it frees the textures it had: between frames, since the last frame's drawing may use them.
    bool create(int pixels_per_piece, const appearance::PieceSvgs& svgs);
    bool create(int pixels_per_piece) { return create(pixels_per_piece, appearance::builtInPieces({})); }
    void clear();

    [[nodiscard]] bool ready() const noexcept { return !levels_.empty(); }
    // The atlas for pieces drawn `pixels` wide on the screen: the smallest image at least that large.
    [[nodiscard]] ImTextureID texture(float pixels) const noexcept;

    // UV rectangle of a piece in the atlas.
    void uv(chess::Piece piece, ImVec2& uv0, ImVec2& uv1) const;

   private:
    struct Level {
        int pixels = 0;  // per piece
        ImTextureID texture{};
    };
    std::vector<Level> levels_;  // largest first
};

// The twelve pieces side by side, `pixels` x `pixels` each, RGBA rows from the top: the atlas' image. Empty if an SVG
// could not be rendered. Without `svgs`, the default set.
[[nodiscard]] std::vector<unsigned char> rasterizePieces(int pixels, const appearance::PieceSvgs& svgs);
[[nodiscard]] inline std::vector<unsigned char> rasterizePieces(int pixels) {
    return rasterizePieces(pixels, appearance::builtInPieces({}));
}

// An arrow over the board, for a move (castling drawn to the king's square).
struct BoardArrow {
    chess::Move move;
    ImU32 colour = 0;
    bool dashed  = false;  // a move expected rather than found: a shaft in dashes
};

// A mark on a square's corner: a move's judgement (?!, ?, ??) on the square it went to.
struct BoardBadge {
    chess::Square square;
    std::string text;
    ImU32 colour = 0;
};

struct BoardDecor {
    bool flipped = false;
    std::optional<chess::Move> last_move;
    // A frame this many pixels thick around the two squares of the last move, in last_move_colour (the big screen,
    // read from afar); 0: none.
    float last_move_frame  = 0.0f;
    ImU32 last_move_colour = IM_COL32(255, 210, 70, 255);
    float eval_white       = 0.5f;   // fraction of the eval bar that is white, < 0 hides the bar
    std::vector<BoardArrow> arrows;  // drawn in order, over the pieces
    std::optional<BoardBadge> badge;        // over everything, at the square's top right corner
    std::optional<ImU32> frame;             // a frame around the board: a line shown on it rather than the game
    std::optional<chess::Square> selected;  // a piece picked up, to move it
    std::vector<chess::Square> targets;     // where it may go, a dot on each
    // A piece dragged with the mouse from this square: not drawn there on the 2D board (it follows the pointer, see
    // drawDraggedPiece()); on the 3D board a ghost of it follows the pointer, the piece staying on its square.
    std::optional<chess::Square> lifted;
    std::optional<chess::Square> drop;  // the square under the piece dragged, where it would land: tinted
    // The squares' colours; by default those of the theme in use (appearance::boardColours()), as on every board.
    std::optional<appearance::BoardColours> colours;
};

// A move's judgement as a disc with its mark, centred on `centre`, `radius` wide; the 2D board puts it on the corner
// of the square, the 3D board over it.
void drawBoardBadge(ImDrawList* dl, ImVec2 centre, float radius, const BoardBadge& badge);

// The room the eval bar takes left of a board `size` high, the gap after it included.
[[nodiscard]] float evalBarSpace(float size);
// The bar at `top`, `size` high, `eval_white` of it White's: from the bottom, or from the top when flipped.
void drawEvalBar(ImDrawList* dl, ImVec2 top, float size, float eval_white, bool flipped);

// The colours of the marks drawBoard() lays over the squares, for a board drawn otherwise (board_3d.hpp).
inline constexpr ImU32 BOARD_CHECK    = IM_COL32(220, 40, 40, 150);
inline constexpr ImU32 BOARD_SELECTED = IM_COL32(20, 85, 30, 110);
inline constexpr ImU32 BOARD_TARGET   = IM_COL32(20, 85, 30, 90);
inline constexpr ImU32 BOARD_DROP     = IM_COL32(20, 85, 30, 70);

// The square a move visibly lands on: castling is stored as king takes rook.
[[nodiscard]] chess::Square displayedTarget(const chess::Move& move);

// Draws the position at the cursor and reserves `size` x `size` (plus the eval bar if shown).
void drawBoard(const PieceAtlas& atlas, const chess::Board& board, float size, const BoardDecor& decor);
// The same for any placement of pieces (a1 = 0 ... h8 = 63), even one without kings, as the position editor has;
// `check`: a king's square to mark.
void drawBoard(const PieceAtlas& atlas, const std::array<chess::Piece, 64>& pieces, float size,
               const BoardDecor& decor, std::optional<chess::Square> check = std::nullopt);

// The piece dragged with the mouse on a board `size` wide, drawn over everything at the pointer.
void drawDraggedPiece(const PieceAtlas& atlas, chess::Piece piece, float size);

// The square under `point` of a board drawBoard() drew with its top left corner (its eval bar's, if shown) at `top`;
// nullopt off the squares.
[[nodiscard]] std::optional<chess::Square> boardSquareAt(ImVec2 point, ImVec2 top, float size, const BoardDecor& decor);

// The centre of `square` on a board whose a8 corner (h1 when flipped) is at `origin`, `size` wide.
[[nodiscard]] ImVec2 squareCentre(chess::Square square, ImVec2 origin, float size, bool flipped);

// The first `max_moves` moves of a PV in UCI notation, played from `board`; it stops at the first move that is not
// legal there.
[[nodiscard]] std::vector<chess::Move> legalPrefix(chess::Board board, const std::vector<std::string>& pv,
                                                   std::size_t max_moves);

// What one looks for near an adjudication: the 50-move counter, the material, the pieces left.
struct BoardInfo {
    int halfmoves = 0;             // plies since the last capture or pawn move; drawn at 100
    int pieces    = 0;             // kings included
    int balance   = 0;             // material in pawns (1, 3, 3, 5, 9), White's point of view
    std::array<int, 5> surplus{};  // pawns, knights, bishops, rooks, queens: White's count minus Black's
};
[[nodiscard]] BoardInfo boardInfo(const chess::Board& board);

// A line under a board, `width` wide: the BoardInfo, the pieces marked when they are few enough for the tablebase
// adjudication (tb_pieces > 0). A narrow line leaves out the labels.
void drawBoardInfo(const PieceAtlas& atlas, const chess::Board& board, float width, int tb_pieces);

// Win expectancy shown by the eval bar, from White's point of view.
[[nodiscard]] float evalBarFraction(int cp_white, bool mate, int mate_white);

}  // namespace fcg
