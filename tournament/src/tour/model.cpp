#include "model.hpp"

#include <filesystem>

#include <core/crc32.hpp>

namespace fcg::tour {

void to_json(nlohmann::json& j, const Participant& p) {
    j = {{"id", p.id}, {"engine", p.engine}};
    if (!p.active) j["active"] = false;
    if (p.replaced_by) j["replaced_by"] = *p.replaced_by;
    if (p.program.size > 0) j["program"] = p.program;
}

void from_json(const nlohmann::json& j, Participant& p) {
    j.at("id").get_to(p.id);
    j.at("engine").get_to(p.engine);
    p.active = j.value("active", true);
    if (j.contains("replaced_by")) p.replaced_by = j.at("replaced_by").get<int>();
    if (j.contains("program")) j.at("program").get_to(p.program);
}

FileFingerprint fingerprint(const std::string& file) {
    namespace fs = std::filesystem;
    FileFingerprint fp;
    std::error_code ec;
    if (file.empty() || !fs::is_regular_file(file, ec)) return fp;

    const auto size  = fs::file_size(file, ec);
    const auto mtime = fs::last_write_time(file, ec);
    if (ec) return fp;
    fp.size  = size;
    fp.mtime = mtime.time_since_epoch().count();
    fp.crc32 = fastchess::crc::calculate_crc32(file).value_or(0);
    return fp;
}

bool changedSince(const std::string& file, const FileFingerprint& recorded) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return true;
    const auto size  = fs::file_size(file, ec);
    const auto mtime = fs::last_write_time(file, ec);
    if (ec || size != recorded.size) return true;
    if (recorded.mtime != 0 && mtime.time_since_epoch().count() == recorded.mtime) return false;
    return fastchess::crc::calculate_crc32(file).value_or(0) != recorded.crc32;
}

void to_json(nlohmann::json& j, const FileFingerprint& f) {
    j = {{"size", f.size}, {"crc32", f.crc32}};
    if (f.mtime != 0) j["mtime"] = f.mtime;
}

void from_json(const nlohmann::json& j, FileFingerprint& f) {
    f.size  = j.value("size", uint64_t{0});
    f.crc32 = j.value("crc32", uint32_t{0});
    f.mtime = j.value("mtime", int64_t{0});
}

void to_json(nlohmann::json& j, const TournamentSpec& s) {
    j = {{"version", s.version},
         {"name", s.name},
         {"format", s.format},
         {"cycles", s.cycles},
         {"seeds", s.seeds},
         {"max_games", s.max_games},
         {"rounds", s.rounds},
         {"conditions", s.conditions},
         {"participants", s.participants},
         {"book", s.book}};
}

void from_json(const nlohmann::json& j, TournamentSpec& s) {
    s.version = j.value("version", 1);
    if (s.version > 1) throw std::runtime_error("this tournament was written by a newer version of fastchess-gui");

    s.name      = j.value("name", std::string{});
    s.format    = j.value("format", Format::Match);
    s.cycles    = j.value("cycles", 1);
    s.seeds     = j.value("seeds", 1);
    s.max_games = j.value("max_games", uint64_t{0});
    s.rounds    = j.value("rounds", 0);
    j.at("conditions").get_to(s.conditions);
    j.at("participants").get_to(s.participants);
    if (j.contains("book")) j.at("book").get_to(s.book);
}

}  // namespace fcg::tour
