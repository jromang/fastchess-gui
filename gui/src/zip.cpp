#include "zip.hpp"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <fstream>

#include <core/crc32.hpp>

// The zlib compressor of stb_image_write (compiled in screenshot.cpp), which its header does not declare.
extern "C" unsigned char* stbi_zlib_compress(unsigned char* data, int data_len, int* out_len, int quality);

namespace fcg {

namespace {

void put16(std::string& out, uint32_t v) {
    out += static_cast<char>(v & 0xFF);
    out += static_cast<char>((v >> 8) & 0xFF);
}

void put32(std::string& out, uint32_t v) {
    put16(out, v & 0xFFFF);
    put16(out, v >> 16);
}

}  // namespace

std::string gzipped(const std::string& data, int quality) {
    if (data.empty() || data.size() >= 0x7FFFFFFF) return {};
    int length = 0;
    auto* z    = stbi_zlib_compress(reinterpret_cast<unsigned char*>(const_cast<char*>(data.data())),
                                    static_cast<int>(data.size()), &length, quality);
    if (!z || length <= 6) {
        std::free(z);
        return {};
    }
    // The header: deflate, no name nor time, an unknown system; then the zlib stream without its two bytes of header
    // and its four of checksum; then the CRC-32 and the length.
    std::string out("\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\xff", 10);
    out.append(reinterpret_cast<const char*>(z) + 2, static_cast<std::size_t>(length - 6));
    std::free(z);
    put32(out, fastchess::crc::finalize_crc32(
                   fastchess::crc::incremental_crc32(fastchess::crc::initial_crc32(), data)));
    put32(out, static_cast<uint32_t>(data.size()));
    return out;
}

std::string zipArchive(const std::string& name, const std::string& data, uint16_t dos_time, uint16_t dos_date) {
    const uint32_t crc =
        fastchess::crc::finalize_crc32(fastchess::crc::incremental_crc32(fastchess::crc::initial_crc32(), data));

    // Deflated: the zlib stream without its two bytes of header and its four of checksum.
    std::string body;
    uint16_t method = 0;
    if (!data.empty() && data.size() < 0x7FFFFFFF) {
        int length = 0;
        auto* z    = stbi_zlib_compress(reinterpret_cast<unsigned char*>(const_cast<char*>(data.data())),
                                        static_cast<int>(data.size()), &length, 8);
        if (z && length > 6 && static_cast<std::size_t>(length - 6) < data.size()) {
            body.assign(reinterpret_cast<const char*>(z) + 2, static_cast<std::size_t>(length - 6));
            method = 8;
        }
        std::free(z);
    }
    if (method == 0) body = data;

    // The fields both headers share, from "version needed" to the name's length. Bit 11: the name is UTF-8.
    const auto common = [&](std::string& out) {
        put16(out, 20);
        put16(out, 0x0800);
        put16(out, method);
        put16(out, dos_time);
        put16(out, dos_date);
        put32(out, crc);
        put32(out, static_cast<uint32_t>(body.size()));
        put32(out, static_cast<uint32_t>(data.size()));
        put16(out, static_cast<uint32_t>(name.size()));
        put16(out, 0);  // extra field
    };

    std::string out;
    put32(out, 0x04034b50);  // local file header
    common(out);
    out += name;
    out += body;

    const auto directory = static_cast<uint32_t>(out.size());
    put32(out, 0x02014b50);  // central directory
    put16(out, 20);          // made by: MS-DOS / FAT, zip 2.0
    common(out);
    put16(out, 0);  // comment
    put16(out, 0);  // disk
    put16(out, 0);  // internal attributes
    put32(out, 0);  // external attributes
    put32(out, 0);  // where the local header is
    out += name;

    const auto directory_size = static_cast<uint32_t>(out.size()) - directory;
    put32(out, 0x06054b50);  // end of central directory
    put16(out, 0);
    put16(out, 0);
    put16(out, 1);
    put16(out, 1);
    put32(out, directory_size);
    put32(out, directory);
    put16(out, 0);
    return out;
}

bool writeZip(const std::filesystem::path& zip, const std::string& name, const std::string& data, std::string* error) {
    const auto now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    const auto time = static_cast<uint16_t>((tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec / 2));
    const auto date = static_cast<uint16_t>((std::max(0, tm.tm_year - 80) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday);

    std::ofstream out(zip, std::ios::binary | std::ios::trunc);
    out << zipArchive(name, data, time, date);
    out.close();
    if (!out && error) *error = "cannot write " + zip.string();
    return static_cast<bool>(out);
}

}  // namespace fcg
