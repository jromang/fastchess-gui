#pragma once

// The portable mode: with a file named "portable" beside the program, the profile is the folder "data" beside it, and
// nothing is written elsewhere (but where the user puts tournaments or exports). The paths of the profile's files
// that lie in the program's folder (engines, books, tablebases, PGN files) are kept relative to it, so the folder can
// be moved, copied to another machine or run from a USB stick.

#include <filesystem>
#include <string>
#include <vector>

#include "model.hpp"

namespace fcg::portable {

// The file whose presence turns the mode on, and the profile's folder then, both beside the program.
inline constexpr const char* MARKER      = "portable";
inline constexpr const char* DATA_FOLDER = "data";

// The folder the marker is looked for in: the executable's, or the one holding the application bundle on macOS
// (beside fastchess-gui.app, rather than inside it).
[[nodiscard]] std::filesystem::path programFolder(const std::filesystem::path& executable);

// The program folder when the marker is in it, empty otherwise.
[[nodiscard]] std::filesystem::path detect(const std::filesystem::path& executable);

// The folder relative paths are kept from: the program folder in the portable mode, empty otherwise (the paths are
// then kept as they are). Set once at startup.
void setBase(const std::filesystem::path& folder);
[[nodiscard]] const std::filesystem::path& base();
[[nodiscard]] inline bool active() { return !base().empty(); }

// A path as a profile file keeps it: relative to `base`, with forward slashes, when it lies inside `base`; as it is
// otherwise, or when `base` is empty.
[[nodiscard]] std::string stored(const std::string& path, const std::filesystem::path& base);
// A path read from a profile file: a relative one is made absolute from `base`; others are returned as they are.
[[nodiscard]] std::string resolved(const std::string& path, const std::filesystem::path& base);
// The same for a list of folders separated as tablebase directories are (';' on Windows, ':' elsewhere).
[[nodiscard]] std::string storedList(const std::string& paths, const std::filesystem::path& base);
[[nodiscard]] std::string resolvedList(const std::string& paths, const std::filesystem::path& base);

// Every path of these, kept or read, from base().
[[nodiscard]] std::string stored(const std::string& path);
[[nodiscard]] std::string resolved(const std::string& path);
[[nodiscard]] EngineEntry stored(EngineEntry engine);
void resolve(EngineEntry& engine);
[[nodiscard]] BookEntry stored(BookEntry book);
void resolve(BookEntry& book);
// The book, the participants' own books, the tablebases, the PGN file every game goes to and the broadcast folder.
[[nodiscard]] MatchSetup stored(MatchSetup setup);
void resolve(MatchSetup& setup);

template <typename T>
[[nodiscard]] std::vector<T> stored(std::vector<T> items) {
    for (auto& item : items) item = stored(std::move(item));
    return items;
}
template <typename T>
void resolve(std::vector<T>& items) {
    for (auto& item : items) resolve(item);
}

}  // namespace fcg::portable
