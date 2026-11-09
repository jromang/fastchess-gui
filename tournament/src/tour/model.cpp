#include "model.hpp"

namespace fcg::tour {

void to_json(nlohmann::json& j, const Participant& p) {
    j = {{"id", p.id}, {"engine", p.engine}};
    if (!p.active) j["active"] = false;
    if (p.replaced_by) j["replaced_by"] = *p.replaced_by;
}

void from_json(const nlohmann::json& j, Participant& p) {
    j.at("id").get_to(p.id);
    j.at("engine").get_to(p.engine);
    p.active = j.value("active", true);
    if (j.contains("replaced_by")) p.replaced_by = j.at("replaced_by").get<int>();
}

void to_json(nlohmann::json& j, const BookFingerprint& b) { j = {{"size", b.size}, {"crc32", b.crc32}}; }

void from_json(const nlohmann::json& j, BookFingerprint& b) {
    b.size  = j.value("size", uint64_t{0});
    b.crc32 = j.value("crc32", uint32_t{0});
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
