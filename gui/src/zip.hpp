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

// `data` as a gzip member (RFC 1952), deflated by the same compressor: what a web server sends with
// "Content-Encoding: gzip". Empty when it cannot be compressed. A higher `quality` (stb's hash chain length) finds
// longer matches, slower.
[[nodiscard]] std::string gzipped(const std::string& data, int quality = 8);

// Writes `zip`, the archive of one file `name` holding `data`, dated now. False with the reason in `error` when it
// cannot be written.
bool writeZip(const std::filesystem::path& zip, const std::string& name, const std::string& data,
              std::string* error = nullptr);

}  // namespace fcg
