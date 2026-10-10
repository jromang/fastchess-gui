#pragma once

#include <optional>

#include <imgui.h>

#include <chess.hpp>

namespace fcg {

// The twelve piece images, rasterized once into a mipmapped texture atlas.
class PieceAtlas {
   public:
    ~PieceAtlas();

    // Needs a current GL context. Returns false if the SVGs could not be rendered.
    bool create(int pixels_per_piece);

    [[nodiscard]] bool ready() const noexcept { return texture_ != 0; }
    [[nodiscard]] ImTextureID texture() const noexcept { return static_cast<ImTextureID>(texture_); }

    // UV rectangle of a piece in the atlas.
    void uv(chess::Piece piece, ImVec2& uv0, ImVec2& uv1) const;

   private:
    unsigned int texture_ = 0;
};

struct BoardDecor {
    bool flipped = false;
    std::optional<chess::Move> last_move;
    float eval_white = 0.5f;  // fraction of the eval bar that is white, < 0 hides the bar
};

// Draws the position at the cursor and reserves `size` x `size` (plus the eval bar if shown).
void drawBoard(const PieceAtlas& atlas, const chess::Board& board, float size, const BoardDecor& decor);

// Win expectancy shown by the eval bar, from White's point of view.
[[nodiscard]] float evalBarFraction(int cp_white, bool mate, int mate_white);

}  // namespace fcg
