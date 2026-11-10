#pragma once

// Engine lists of other programs, turned into entries of ours: ChessGUI (ChsGUI_Eng.TXT, with the options of
// ChsGUI_Cfg.ini next to it), cutechess and Banksia (engines.json), Arena and other INI style lists, and the
// executables of a folder.

#include <filesystem>
#include <string>
#include <vector>

#include "model.hpp"

namespace fcg {

struct ImportResult {
    std::vector<EngineEntry> engines;
    std::vector<std::string> notes;  // engines skipped and why (missing files)
};

// Reads an engine list, whatever program wrote it. Relative paths are taken from the list's folder.
[[nodiscard]] ImportResult importEngineList(const std::filesystem::path& file);

// The programs of a folder and its subfolders (two levels), as engines to check.
[[nodiscard]] ImportResult findEngines(const std::filesystem::path& folder);

}  // namespace fcg
