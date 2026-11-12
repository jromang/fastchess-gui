#pragma once

// A zip archive of one file, as the rating list testers send their games: deflated with stb_image_write's compressor
// (the one of the PNG screenshots), stored as is when that does not make it smaller.

#include <cstdint>
#include <filesystem>
#include <string>

namespace fcg {

// The bytes of a zip archive holding one file, `name` (UTF-8), with `data` and the given modification time
// (MS-DOS format).
[[nodiscard]] std::string zipArchive(const std::string& name, const std::string& data, uint16_t dos_time,
                                     uint16_t dos_date);

// Writes `zip`, the archive of one file `name` holding `data`, dated now. False with the reason in `error` when it
// cannot be written.
bool writeZip(const std::filesystem::path& zip, const std::string& name, const std::string& data,
              std::string* error = nullptr);

}  // namespace fcg
