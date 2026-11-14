#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include <chess.hpp>

namespace fcg {

// The twelve piece images, rasterized once into a texture atlas: mipmapped with OpenGL; with SDL's renderer, which has
// no mipmaps, also rasterized at half, a quarter and an eighth of the size, so that small boards stay smooth.
class PieceAtlas {
   public:
    PieceAtlas()                             = default;
    PieceAtlas(const PieceAtlas&)            = delete;
    PieceAtlas& operator=(const PieceAtlas&) = delete;
    ~PieceAtlas();

    // Needs the window's renderer (render::open). Returns false if the SVGs could not be rendered.
    bool create(int pixels_per_piece);

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
// could not be rendered.
[[nodiscard]] std::vector<unsigned char> rasterizePieces(int pixels);

// An arrow over the board, for a move (castling drawn to the king's square).
struct BoardArrow {
    chess::Move move;
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
};

// Draws the position at the cursor and reserves `size` x `size` (plus the eval bar if shown).
void drawBoard(const PieceAtlas& atlas, const chess::Board& board, float size, const BoardDecor& decor);

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
