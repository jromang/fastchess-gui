#include "model.hpp"

#include <algorithm>
#include <filesystem>

#include <core/crc32.hpp>

#include "files.hpp"

namespace fcg::tour {

void to_json(nlohmann::json& j, const Participant& p) {
    j = {{"id", p.id}, {"engine", p.engine}};
    if (!p.active) j["active"] = false;
    if (p.replaced_by) j["replaced_by"] = *p.replaced_by;
    if (p.program.size > 0) j["program"] = p.program;
    if (p.elo > 0) j["elo"] = p.elo;
}

void from_json(const nlohmann::json& j, Participant& p) {
    j.at("id").get_to(p.id);
    j.at("engine").get_to(p.engine);
    p.active = j.value("active", true);
    if (j.contains("replaced_by")) p.replaced_by = j.at("replaced_by").get<int>();
    if (j.contains("program")) j.at("program").get_to(p.program);
    p.elo = j.value("elo", 0);
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

namespace {

// File names compared as Windows does, letter case aside (ASCII only, enough for ".pgn").
std::string asciiLower(std::string s) {
    for (auto& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

}  // namespace

void to_json(nlohmann::json& j, const TournamentSpec& s) {
    // Version 2: PGN files with other names than games.pgn. Versions before 0.4.13 would read and write a
    // games.pgn of their own there (and append its blank games to the file of "Also append every game to"), so
    // they refuse such a folder instead.
    const bool named = s.pgnFile() != "games.pgn" || s.chessbasePgnFile() != "games-chessbase.pgn";
    j = {{"version", std::max(s.version, named ? 2 : 1)},
         {"name", s.name},
         {"format", s.format},
         {"cycles", s.cycles},
         {"seeds", s.seeds},
         {"max_games", s.max_games},
         {"rounds", s.rounds},
         {"conditions", s.conditions},
         {"participants", s.participants},
         {"book", s.book}};
    if (s.log_every_game) j["log_every_game"] = true;
    if (s.opening_per_round) j["opening_per_round"] = true;
    if (!s.pgn_out.empty()) {
        j["pgn_out"]           = s.pgn_out;
        j["pgn_out_chessbase"] = s.pgn_out_chessbase;
    }
    if (!s.pgn_file.empty()) j["pgn_file"] = s.pgn_file;
    if (!s.pgn_chessbase_file.empty()) j["pgn_chessbase_file"] = s.pgn_chessbase_file;
    if (!s.forced_options.empty()) {
        auto& forced = j["forced_options"] = nlohmann::json::object();
        for (const auto& [name, value] : s.forced_options) forced[name] = value;
    }
}

void from_json(const nlohmann::json& j, TournamentSpec& s) {
    s.version = j.value("version", 1);
    if (s.version > 2) throw std::runtime_error("this tournament was written by a newer version of fastchess-gui");

    s.name      = j.value("name", std::string{});
    s.format    = j.value("format", Format::Match);
    s.cycles    = j.value("cycles", 1);
    s.seeds     = j.value("seeds", 1);
    s.max_games = j.value("max_games", uint64_t{0});
    s.rounds    = j.value("rounds", 0);
    j.at("conditions").get_to(s.conditions);
    j.at("participants").get_to(s.participants);
    if (j.contains("book")) j.at("book").get_to(s.book);
    s.log_every_game    = j.value("log_every_game", false);
    s.opening_per_round = j.value("opening_per_round", false);
    s.pgn_out           = j.value("pgn_out", std::string{});
    s.pgn_out_chessbase = j.value("pgn_out_chessbase", false);
    s.forced_options.clear();
    if (const auto it = j.find("forced_options"); it != j.end() && it->is_object()) {
        for (const auto& [name, value] : it->items()) s.forced_options.emplace_back(name, value.get<std::string>());
    }

    // A file of the folder, nothing else: the games are written to it.
    s.pgn_file           = j.value("pgn_file", std::string{});
    s.pgn_chessbase_file = j.value("pgn_chessbase_file", std::string{});
    // A .pgn file, so that it can never be the journal or tournament.json, and two different files.
    for (const auto& file : {s.pgn_file, s.pgn_chessbase_file}) {
        const bool pgn = file.size() > 4 && asciiLower(file.substr(file.size() - 4)) == ".pgn";
        if (!file.empty() && (fileNameOf(file) != file || !pgn))
            throw std::runtime_error("invalid PGN file name \"" + file + "\"");
    }
    if (asciiLower(s.pgnFile()) == asciiLower(s.chessbasePgnFile()))
        throw std::runtime_error("the two PGN files have the same name \"" + s.pgnFile() + "\"");
}

std::string fileStemOf(const std::string& name) {
    const auto first = name.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    std::string stem = name.substr(first, name.find_last_not_of(" \t") + 1 - first);

    // Cut at the first byte of a character, not inside one (UTF-8).
    constexpr std::size_t MAX_BYTES = 64;
    if (stem.size() > MAX_BYTES) {
        std::size_t end = MAX_BYTES;
        while (end > 0 && (static_cast<unsigned char>(stem[end]) & 0xC0) == 0x80) --end;
        stem.resize(end);
    }
    return fileNameOf(stem);
}

void namePgnFiles(TournamentSpec& spec) {
    auto stem = fileStemOf(spec.name);
    if (stem.empty()) stem = "games";
    spec.pgn_file           = stem + ".pgn";
    spec.pgn_chessbase_file = stem + "-chessbase.pgn";
}

}  // namespace fcg::tour
