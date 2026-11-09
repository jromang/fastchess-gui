#pragma once

// Matches played by fastchess-gui before the tournament layer (0.2.x) are folders with fastchess' config.json and
// the two PGN files. They are turned into finished tournaments, so they appear in the list like the others.

#include <filesystem>
#include <vector>

namespace fcg::tour {

// A folder of fastchess-gui 0.2.x: config.json and games.pgn, no tournament.json.
[[nodiscard]] bool isLegacyRun(const std::filesystem::path& dir);

// The legacy folders directly inside `root`.
[[nodiscard]] std::vector<std::filesystem::path> legacyRuns(const std::filesystem::path& root);

// Writes tournament.json and a journal of finished games, read from the PGN files; the PGN files are not touched.
// The match is marked finished ("imported"). Throws with a readable message when the folder cannot be read.
void importLegacyRun(const std::filesystem::path& dir);

}  // namespace fcg::tour
