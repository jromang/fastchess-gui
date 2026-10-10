#pragma once

// Small formatting and widget helpers shared by the views.

#include <cstdint>
#include <optional>
#include <string>

#include <imgui.h>

#include <chess.hpp>
#include <types/score.hpp>

namespace fcg::ui {

// Colours of the sober dark theme, reused for plots and highlights.
inline constexpr ImU32 ACCENT  = IM_COL32(110, 168, 222, 255);
inline constexpr ImU32 GOOD    = IM_COL32(120, 200, 120, 255);
inline constexpr ImU32 BAD     = IM_COL32(230, 110, 100, 255);
inline constexpr ImU32 MUTED   = IM_COL32(150, 150, 150, 255);
inline constexpr ImVec4 MUTED4 = ImVec4(0.59f, 0.59f, 0.59f, 1.0f);

void applyTheme(float scale);

// "1:02:03", "2:03.4" or "4.2s".
std::string formatClock(int64_t ms);

// 12345678 -> "12.3M"
std::string formatCount(uint64_t n);

// Engine score from the mover's point of view, as "+0.35" or "#5".
std::string formatScore(const std::optional<fastchess::Score>& score);

// White point of view, in pawns, clamped to +-limit; mates map to +-limit.
float whitePawns(const std::optional<fastchess::Score>& score, chess::Color mover, float limit = 8.0f);

// Duration "1 h 02 min", "3 min 05 s", "12 s".
std::string formatDuration(double seconds);

enum class Icon { Play, Pause, Stop, Swap };

// A button with a small drawn icon, so no icon font is needed.
bool iconButton(const char* id, Icon icon, const char* label, bool enabled = true);

// "(?)" with a tooltip.
void help(const char* text);

// Label in the left column, widget in the right one (for the setup panel).
void label(const char* text, float width);

}  // namespace fcg::ui
