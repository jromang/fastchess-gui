#include "profiles.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

#include <fmt/include/fmt/format.h>

#include <tour/polyglot.hpp>

#include "i18n.hpp"
#include "portable.hpp"
#include "run.hpp"
#include "storage.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

bool lessCaseInsensitive(const std::string& a, const std::string& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
    });
}

std::string joined(const std::vector<std::string>& parts, const char* separator = " · ") {
    std::string out;
    for (const auto& p : parts) {
        if (p.empty()) continue;
        if (!out.empty()) out += separator;
        out += p;
    }
    return out;
}

// 0.1 rather than 0.100000.
std::string number(double v) { return fmt::format("{:g}", v); }

struct Preset {
    const char* name;
    const char* values;  // JSON: the fields that differ from the default setup
};

// The program's profiles. The names of those that are words are translated where they are shown (N_), a time control
// is the same in every language.
const std::vector<Preset>& presets(SetupSection section) {
    static const std::vector<Preset> format{
        {N_("SPRT gainer [0, 3]"), R"({"format": 0, "mode": 2, "sprt_elo0": 0, "sprt_elo1": 3})"},
        {N_("SPRT non-regression [-5, 0]"), R"({"format": 0, "mode": 2, "sprt_elo0": -5, "sprt_elo1": 0})"},
        {N_("Match, 1000 games"), R"({"format": 0, "mode": 0, "fixed_games": 1000})"},
        {N_("Round robin"), R"({"format": 1, "cycles": 1})"},
        {N_("Gauntlet"), R"({"format": 2, "cycles": 1, "seeds": 1})"},
        {N_("Swiss, 9 rounds"), R"({"format": 3, "cycles": 9})"},
        {N_("Knockout"), R"({"format": 4, "cycles": 1})"},
        {N_("Team match"), R"({"format": 5, "cycles": 1, "team_boards": false})"},
    };
    // The time controls testers use: fishtest's short and long tests, and the rating lists' blitz and long ones.
    static const std::vector<Preset> time{
        {"10+0.1", R"({"limit": 0, "tc_base_s": 10, "tc_inc_s": 0.1, "tc_moves": 0})"},
        {"60+0.6", R"({"limit": 0, "tc_base_s": 60, "tc_inc_s": 0.6, "tc_moves": 0})"},
        {"2+1", R"({"limit": 0, "tc_base_s": 120, "tc_inc_s": 1, "tc_moves": 0})"},
        {"40/4", R"({"limit": 0, "tc_base_s": 240, "tc_inc_s": 0, "tc_moves": 40})"},
        {"40/15", R"({"limit": 0, "tc_base_s": 900, "tc_inc_s": 0, "tc_moves": 40})"},
        {"40/20", R"({"limit": 0, "tc_base_s": 1200, "tc_inc_s": 0, "tc_moves": 40})"},
        {N_("200k nodes"), R"({"limit": 1, "nodes": 200000})"},
    };
    static const std::vector<Preset> openings{
        {N_("No book"), R"({})"},
        {N_("Chess960, the 960 positions"), R"({"chess960": true})"},
    };
    static const std::vector<Preset> adjudication{
        {N_("None"), R"({})"},
        {N_("Standard"),
         R"({"draw_enabled": true, "draw_movenumber": 40, "draw_movecount": 8, "draw_score": 10,
             "resign_enabled": true, "resign_movecount": 3, "resign_score": 600})"},
        {N_("Standard with tablebases"),
         R"({"draw_enabled": true, "draw_movenumber": 40, "draw_movecount": 8, "draw_score": 10,
             "resign_enabled": true, "resign_movecount": 3, "resign_score": 600, "tb_enabled": true,
             "tb_pieces": 6})"},
    };
    static const std::vector<Preset> engines{
        {N_("Each engine's own"), R"({})"},
        {N_("Hash 16 MB, 1 thread"), R"({"common_hash": 16, "common_threads": 1, "engine_ownbook_off": true})"},
        {N_("Hash 64 MB, 1 thread"), R"({"common_hash": 64, "common_threads": 1, "engine_ownbook_off": true})"},
        {N_("Hash 256 MB, 1 thread"), R"({"common_hash": 256, "common_threads": 1, "engine_ownbook_off": true})"},
    };
    switch (section) {
        case SetupSection::Format:
            return format;
        case SetupSection::Time:
            return time;
        case SetupSection::Openings:
            return openings;
        case SetupSection::Adjudication:
            return adjudication;
        case SetupSection::Engines:
            break;
    }
    return engines;
}

std::size_t indexOf(SetupSection section) { return static_cast<std::size_t>(section); }

}  // namespace

const char* sectionKey(SetupSection section) {
    switch (section) {
        case SetupSection::Format:
            return "format";
        case SetupSection::Time:
            return "time";
        case SetupSection::Openings:
            return "openings";
        case SetupSection::Adjudication:
            return "adjudication";
        case SetupSection::Engines:
            break;
    }
    return "engines";
}

std::optional<SetupSection> sectionOfKey(std::string_view key) {
    for (const auto s : SETUP_SECTIONS) {
        if (key == sectionKey(s)) return s;
    }
    return std::nullopt;
}

const char* sectionTitle(SetupSection section) {
    switch (section) {
        case SetupSection::Format:
            return TR("Format");
        case SetupSection::Time:
            return TR("Time control");
        case SetupSection::Openings:
            return TR("Openings");
        case SetupSection::Adjudication:
            return TR("Adjudication");
        case SetupSection::Engines:
            break;
    }
    return TR("Engine settings");
}

const std::vector<std::string>& sectionFields(SetupSection section) {
    static const std::vector<std::string> format{"format",        "cycles",      "seeds",          "mode",
                                                 "fixed_games",   "sprt_elo0",   "sprt_elo1",      "sprt_alpha",
                                                 "sprt_beta",     "sprt_model",  "sprt_max_games", "sprt_draw_ratio",
                                                 "sprt_rms_bias", "team_boards", "sprt_bias",      "sprt_min_games",
                                                 "colours"};
    static const std::vector<std::string> time{"limit",        "tc_base_s", "tc_inc_s", "tc_moves",
                                               "tc_margin_ms", "nodes",     "move_s",   "depth"};
    static const std::vector<std::string> openings{
        "book", "book_random",     "book_plies",         "book_start",    "chess960",         "opening_per_round",
        "seed", "book_randomness", "unbalanced_enabled", "unbalanced_cp", "unbalanced_moves", "unbalanced_replace"};
    static const std::vector<std::string> adjudication{
        "draw_enabled",     "draw_movenumber", "draw_movecount",  "draw_score",       "resign_enabled",
        "resign_movecount", "resign_score",    "resign_twosided", "maxmoves_enabled", "maxmoves",
        "tb_enabled",       "tb_pieces",       "tb_ignore50",     "tb_result"};
    static const std::vector<std::string> engines{"common_hash", "common_threads", "engine_syzygy",
                                                  "engine_ownbook_off"};
    switch (section) {
        case SetupSection::Format:
            return format;
        case SetupSection::Time:
            return time;
        case SetupSection::Openings:
            return openings;
        case SetupSection::Adjudication:
            return adjudication;
        case SetupSection::Engines:
            break;
    }
    return engines;
}

nlohmann::json sectionValues(const MatchSetup& setup, SetupSection section) {
    const nlohmann::json all = setup;
    auto out                 = nlohmann::json::object();
    for (const auto& key : sectionFields(section)) {
        if (all.contains(key)) out[key] = all[key];
    }
    return out;
}

std::string SetupProfile::label() const { return preset ? i18n::tr(name) : name; }

SetupProfile makeProfile(std::string name, SetupSection section, const nlohmann::json& values, bool preset) {
    SetupProfile p;
    p.name   = std::move(name);
    p.preset = preset;
    // The default setup's values for the fields `values` lacks (a field added since the profile was saved), and
    // nothing of another section.
    nlohmann::json all = MatchSetup{};
    if (values.is_object()) {
        for (const auto& key : sectionFields(section)) {
            if (values.contains(key)) all[key] = values[key];
        }
    }
    try {
        p.setup = all.get<MatchSetup>();
    } catch (const nlohmann::json::exception&) {
        p.setup = MatchSetup{};  // a value of another type, written by hand: the defaults
    }
    p.values = sectionValues(p.setup, section);
    return p;
}

bool sameSection(const MatchSetup& a, const MatchSetup& b, SetupSection section) {
    switch (section) {
        case SetupSection::Format:
            if (a.format != b.format) return false;
            switch (a.format) {
                case TournamentFormat::Match:
                    if (a.mode != b.mode || a.colours != b.colours) return false;
                    if (a.mode == MatchMode::Fixed) return a.fixed_games == b.fixed_games;
                    if (a.mode == MatchMode::Sprt) {
                        // The draw ratio and the RMS bias are the calculator's guesses: they change no game.
                        return a.sprt_elo0 == b.sprt_elo0 && a.sprt_elo1 == b.sprt_elo1 &&
                               a.sprt_alpha == b.sprt_alpha && a.sprt_beta == b.sprt_beta &&
                               a.sprt_model == b.sprt_model && a.sprt_max_games == b.sprt_max_games &&
                               a.sprt_bias == b.sprt_bias && a.sprt_min_games == b.sprt_min_games;
                    }
                    return true;
                case TournamentFormat::Gauntlet:
                    return a.cycles == b.cycles && a.seeds == b.seeds;
                case TournamentFormat::TeamMatch:
                    return a.cycles == b.cycles && a.team_boards == b.team_boards;
                default:
                    return a.cycles == b.cycles;
            }
        case SetupSection::Time:
            if (a.limit != b.limit) return false;
            switch (a.limit) {
                case LimitKind::Time:
                    return a.tc_base_s == b.tc_base_s && a.tc_inc_s == b.tc_inc_s && a.tc_moves == b.tc_moves &&
                           a.tc_margin_ms == b.tc_margin_ms;
                case LimitKind::MoveTime:
                    return a.move_s == b.move_s && a.tc_margin_ms == b.tc_margin_ms;
                case LimitKind::Nodes:
                    return a.nodes == b.nodes;
                case LimitKind::Depth:
                    return a.depth == b.depth;
            }
            return true;
        case SetupSection::Openings:
            // The plies of a book only matter with a book. The randomness also chooses the moves of the
            // participants' own books, which are not in the section: it always counts.
            return a.book == b.book && (a.book.empty() || a.book_plies == b.book_plies) &&
                   a.book_random == b.book_random && a.book_start == b.book_start && a.chess960 == b.chess960 &&
                   a.opening_per_round == b.opening_per_round && a.seed == b.seed &&
                   a.book_randomness == b.book_randomness && a.unbalanced_enabled == b.unbalanced_enabled &&
                   (!a.unbalanced_enabled ||
                    (a.unbalanced_cp == b.unbalanced_cp && a.unbalanced_moves == b.unbalanced_moves &&
                     a.unbalanced_replace == b.unbalanced_replace));
        case SetupSection::Adjudication:
            if (a.draw_enabled != b.draw_enabled || a.resign_enabled != b.resign_enabled ||
                a.maxmoves_enabled != b.maxmoves_enabled || a.tb_enabled != b.tb_enabled) {
                return false;
            }
            if (a.draw_enabled && (a.draw_movenumber != b.draw_movenumber || a.draw_movecount != b.draw_movecount ||
                                   a.draw_score != b.draw_score)) {
                return false;
            }
            if (a.resign_enabled && (a.resign_movecount != b.resign_movecount || a.resign_score != b.resign_score ||
                                     a.resign_twosided != b.resign_twosided)) {
                return false;
            }
            if (a.maxmoves_enabled && a.maxmoves != b.maxmoves) return false;
            return !a.tb_enabled ||
                   (a.tb_pieces == b.tb_pieces && a.tb_ignore50 == b.tb_ignore50 && a.tb_result == b.tb_result);
        case SetupSection::Engines:
            break;
    }
    return a.common_hash == b.common_hash && a.common_threads == b.common_threads &&
           a.engine_syzygy == b.engine_syzygy && a.engine_ownbook_off == b.engine_ownbook_off;
}

void copySection(MatchSetup& to, const MatchSetup& from, SetupSection section) {
    switch (section) {
        case SetupSection::Format:
            to.format          = from.format;
            to.cycles          = from.cycles;
            to.seeds           = from.seeds;
            to.team_boards     = from.team_boards;
            to.mode            = from.mode;
            to.fixed_games     = from.fixed_games;
            to.sprt_elo0       = from.sprt_elo0;
            to.sprt_elo1       = from.sprt_elo1;
            to.sprt_alpha      = from.sprt_alpha;
            to.sprt_beta       = from.sprt_beta;
            to.sprt_model      = from.sprt_model;
            to.sprt_max_games  = from.sprt_max_games;
            to.sprt_draw_ratio = from.sprt_draw_ratio;
            to.sprt_rms_bias   = from.sprt_rms_bias;
            to.sprt_bias       = from.sprt_bias;
            to.sprt_min_games  = from.sprt_min_games;
            to.colours         = from.colours;
            return;
        case SetupSection::Time:
            to.limit        = from.limit;
            to.tc_base_s    = from.tc_base_s;
            to.tc_inc_s     = from.tc_inc_s;
            to.tc_moves     = from.tc_moves;
            to.tc_margin_ms = from.tc_margin_ms;
            to.nodes        = from.nodes;
            to.move_s       = from.move_s;
            to.depth        = from.depth;
            return;
        case SetupSection::Openings:
            to.book              = from.book;
            to.book_random       = from.book_random;
            to.book_plies        = from.book_plies;
            to.book_start        = from.book_start;
            to.chess960          = from.chess960;
            to.opening_per_round = from.opening_per_round;
            to.seed              = from.seed;
            to.book_randomness    = from.book_randomness;
            to.unbalanced_enabled = from.unbalanced_enabled;
            to.unbalanced_cp      = from.unbalanced_cp;
            to.unbalanced_moves   = from.unbalanced_moves;
            to.unbalanced_replace = from.unbalanced_replace;
            return;
        case SetupSection::Adjudication:
            to.draw_enabled     = from.draw_enabled;
            to.draw_movenumber  = from.draw_movenumber;
            to.draw_movecount   = from.draw_movecount;
            to.draw_score       = from.draw_score;
            to.resign_enabled   = from.resign_enabled;
            to.resign_movecount = from.resign_movecount;
            to.resign_score     = from.resign_score;
            to.resign_twosided  = from.resign_twosided;
            to.maxmoves_enabled = from.maxmoves_enabled;
            to.maxmoves         = from.maxmoves;
            to.tb_enabled       = from.tb_enabled;
            to.tb_pieces        = from.tb_pieces;
            to.tb_ignore50      = from.tb_ignore50;
            to.tb_result        = from.tb_result;
            return;
        case SetupSection::Engines:
            to.common_hash        = from.common_hash;
            to.common_threads     = from.common_threads;
            to.engine_syzygy      = from.engine_syzygy;
            to.engine_ownbook_off = from.engine_ownbook_off;
            return;
    }
}

void applyProfile(MatchSetup& setup, SetupSection section, const SetupProfile& profile) {
    // Field by field: the rest of the setup is left exactly as it is.
    copySection(setup, profile.setup, section);
    setup.profiles[sectionKey(section)] = profile.name;
}

ProfileStore::ProfileStore() {
    for (const auto s : SETUP_SECTIONS) {
        for (const auto& p : presets(s)) {
            presets_[indexOf(s)].push_back(makeProfile(p.name, s, nlohmann::json::parse(p.values), true));
        }
        relist(s);
    }
}

void ProfileStore::relist(SetupSection section) {
    auto& users = users_[indexOf(section)];
    std::stable_sort(users.begin(), users.end(),
                     [](const auto& a, const auto& b) { return lessCaseInsensitive(a.name, b.name); });
    auto& listed = listed_[indexOf(section)];
    listed       = users;
    for (const auto& p : presets_[indexOf(section)]) {
        if (!isUsers(section, p.name)) listed.push_back(p);
    }
}

const std::vector<SetupProfile>& ProfileStore::list(SetupSection section) const { return listed_[indexOf(section)]; }

const SetupProfile* ProfileStore::find(SetupSection section, const std::string& name) const {
    for (const auto& p : list(section)) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

bool ProfileStore::isUsers(SetupSection section, const std::string& name) const {
    const auto& users = users_[indexOf(section)];
    return std::any_of(users.begin(), users.end(), [&](const auto& p) { return p.name == name; });
}

const SetupProfile& ProfileStore::save(SetupSection section, const std::string& name, const MatchSetup& setup) {
    auto& users  = users_[indexOf(section)];
    auto profile = makeProfile(name, section, sectionValues(setup, section));
    if (auto it = std::find_if(users.begin(), users.end(), [&](const auto& p) { return p.name == name; });
        it != users.end()) {
        *it = std::move(profile);
    } else {
        users.push_back(std::move(profile));
    }
    relist(section);
    return *find(section, name);
}

bool ProfileStore::remove(SetupSection section, const std::string& name) {
    auto& users = users_[indexOf(section)];
    if (std::erase_if(users, [&](const auto& p) { return p.name == name; }) == 0) return false;
    relist(section);
    return true;
}

nlohmann::json ProfileStore::toJson() const {
    auto j = nlohmann::json::object();
    for (const auto s : SETUP_SECTIONS) {
        const auto& users = users_[indexOf(s)];
        if (users.empty()) continue;
        auto& list = j[sectionKey(s)] = nlohmann::json::array();
        for (const auto& p : users) list.push_back({{"name", p.name}, {"values", p.values}});
    }
    return j;
}

void ProfileStore::fromJson(const nlohmann::json& j) {
    for (const auto s : SETUP_SECTIONS) {
        auto& users = users_[indexOf(s)];
        users.clear();
        if (j.is_object() && j.contains(sectionKey(s)) && j[sectionKey(s)].is_array()) {
            for (const auto& item : j[sectionKey(s)]) {
                if (!item.is_object() || !item.contains("name") || !item["name"].is_string()) continue;
                const auto name = item["name"].get<std::string>();
                if (name.empty()) continue;
                // The last of two of the same name, as a save would have left it.
                std::erase_if(users, [&](const auto& p) { return p.name == name; });
                users.push_back(makeProfile(name, s, item.value("values", nlohmann::json::object())));
            }
        }
        relist(s);
    }
}

std::string sectionProfile(const MatchSetup& setup, SetupSection section) {
    const auto it = setup.profiles.find(sectionKey(section));
    return it == setup.profiles.end() ? std::string() : it->second;
}

ProfileState profileState(const MatchSetup& setup, SetupSection section, const ProfileStore& store) {
    const auto name = sectionProfile(setup, section);
    if (name.empty()) return ProfileState::None;
    const auto* profile = store.find(section, name);
    if (!profile) return ProfileState::Missing;
    return sameSection(setup, profile->setup, section) ? ProfileState::Same : ProfileState::Changed;
}

void recognizeProfiles(MatchSetup& setup, const ProfileStore& store) {
    for (const auto s : SETUP_SECTIONS) {
        if (!sectionProfile(setup, s).empty()) continue;
        for (const auto& p : store.list(s)) {
            if (sameSection(setup, p.setup, s)) {
                setup.profiles[sectionKey(s)] = p.name;
                break;
            }
        }
    }
}

nlohmann::json templateJson(const MatchSetup& setup, const ProfileStore& store) {
    nlohmann::json j = setup;
    auto follow      = nlohmann::json::array();
    for (const auto s : SETUP_SECTIONS) {
        if (profileState(setup, s, store) == ProfileState::Same) follow.push_back(sectionKey(s));
    }
    j["follow"] = follow;
    return j;
}

MatchSetup templateSetup(const nlohmann::json& j, const ProfileStore& store) {
    auto setup = j.get<MatchSetup>();
    upgradeSavedSetup(j, setup);
    portable::resolve(setup);  // in the portable mode, the paths of the program's folder are kept relative to it
    if (j.contains("follow") && j["follow"].is_array()) {
        for (const auto& key : j["follow"]) {
            const auto section = key.is_string() ? sectionOfKey(key.get<std::string>()) : std::nullopt;
            if (!section) continue;
            if (const auto* profile = store.find(*section, sectionProfile(setup, *section)))
                applyProfile(setup, *section, *profile);
        }
    }
    recognizeProfiles(setup, store);
    return setup;
}

std::optional<MatchSetup> readTemplate(const std::filesystem::path& file, const ProfileStore& store) {
    const auto j = readJson<nlohmann::json>(file);
    if (!j) return std::nullopt;
    try {
        return templateSetup(*j, store);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string randomnessText(int randomness) {
    randomness = std::clamp(randomness, -tour::RANDOMNESS_LIMIT, tour::RANDOMNESS_LIMIT);
    if (randomness == 0) return TR("moves by their weights");
    if (randomness == -tour::RANDOMNESS_LIMIT) return TR("the best moves only");
    if (randomness == tour::RANDOMNESS_LIMIT) return TR("every move alike");
    return trf("randomness {}", fmt::format("{:+}", randomness));
}

std::string sprtText(double elo0, double elo1, double bias, uint64_t min_games) {
    auto text = fmt::format("SPRT [{}, {}]", number(elo0), number(elo1));
    if (bias != 0) text += fmt::format(" {} {}", bias > 0 ? "+" : "-", number(std::abs(bias)));
    if (min_games > 0) {
        text += ", " + trn("no decision before {} game", "no decision before {} games", min_games, min_games);
    }
    return text;
}

const char* coloursText(tour::Colours colours) {
    switch (colours) {
        case tour::Colours::AWhite:
            return TR("A always White");
        case tour::Colours::BWhite:
            return TR("B always White");
        default:
            return TR("Alternate");
    }
}

std::string unbalancedText(const MatchSetup& s) {
    if (!s.unbalanced_enabled) return {};
    const int cp     = std::max(1, s.unbalanced_cp);
    const auto moves = static_cast<uint64_t>(std::max(1, s.unbalanced_moves));
    return s.unbalanced_replace ? trn("unbalanced openings replaced (±{} cp for {} move)",
                                      "unbalanced openings replaced (±{} cp for {} moves)", moves, cp, moves)
                                : trn("unbalanced openings flagged (±{} cp for {} move)",
                                      "unbalanced openings flagged (±{} cp for {} moves)", moves, cp, moves);
}

std::string durationText(double seconds) {
    if (!(seconds > 0)) return trf("{} s", 0);
    const double whole = std::round(seconds);
    if (seconds < 60 || std::abs(seconds - whole) > 1e-9) return trf("{} s", number(seconds));
    const auto total = static_cast<int64_t>(whole);
    std::vector<std::string> parts;
    if (total >= 3600) parts.push_back(trf("{} h", total / 3600));
    if (total % 3600 >= 60) parts.push_back(trf("{} min", total % 3600 / 60));
    if (total % 60 > 0) parts.push_back(trf("{} s", total % 60));
    return joined(parts, " ");
}

namespace {

std::string bookLabel(const std::string& path, const std::vector<BookEntry>& books) {
    for (const auto& b : books) {
        if (b.path == path) return b.name;
    }
    return std::filesystem::path(path).filename().string();
}

std::string timeSummary(const MatchSetup& s) {
    std::string text;
    switch (s.limit) {
        case LimitKind::Time:
            if (s.tc_moves > 0) {
                text = trf("{} moves in {}, repeated", s.tc_moves, durationText(s.tc_base_s));
                if (s.tc_inc_s > 0) text += " " + trf("+ {} per move", durationText(s.tc_inc_s));
            } else if (s.tc_inc_s > 0) {
                text = trf("{} + {} per move", durationText(s.tc_base_s), durationText(s.tc_inc_s));
            } else {
                text = trf("{} per game", durationText(s.tc_base_s));
            }
            break;
        case LimitKind::MoveTime:
            text = trf("{} per move", durationText(s.move_s));
            break;
        case LimitKind::Nodes:
            return trn("{} node per move", "{} nodes per move", s.nodes, ui::formatCount(s.nodes));
        case LimitKind::Depth:
            return trn("depth {} ply", "depth {} plies", static_cast<uint64_t>(std::max(0, s.depth)), s.depth);
    }
    if (s.tc_margin_ms > 0) text += " · " + trf("margin {} ms", s.tc_margin_ms);
    return text;
}

std::string formatSummary(const MatchSetup& s) {
    const auto cycles = static_cast<uint64_t>(std::max(1, s.cycles));
    switch (s.format) {
        case TournamentFormat::Match: {
            std::string summary;
            if (s.mode == MatchMode::Sprt) {
                std::vector<std::string> parts{TR("Match"),
                                               fmt::format("SPRT [{}, {}]", number(s.sprt_elo0), number(s.sprt_elo1))};
                // The bias as the testers write it: "SPRT [0, 3] + 5", the bounds given being [5, 8].
                if (s.sprt_bias != 0) {
                    parts.back() += fmt::format(" {} {}", s.sprt_bias > 0 ? "+" : "-", number(std::abs(s.sprt_bias)));
                }
                if (s.sprt_model != "normalized") parts.back() += " " + s.sprt_model;
                if (s.sprt_alpha != 0.05 || s.sprt_beta != 0.05) {
                    parts.back() += fmt::format(" α {} β {}", number(s.sprt_alpha), number(s.sprt_beta));
                }
                if (s.sprt_min_games > 0) {
                    parts.push_back(trn("no decision before {} game", "no decision before {} games",
                                        static_cast<uint64_t>(s.sprt_min_games), s.sprt_min_games));
                }
                if (s.sprt_max_games > 0) {
                    parts.push_back(trn("at most {} game", "at most {} games", static_cast<uint64_t>(s.sprt_max_games),
                                        s.sprt_max_games));
                }
                summary = joined(parts);
            } else if (s.mode == MatchMode::Infinite) {
                summary = joined({TR("Match"), TR("until stopped")});
            } else {
                summary = joined({TR("Match"), trn("{} game", "{} games",
                                                   static_cast<uint64_t>(std::max(0, s.fixed_games)), s.fixed_games)});
            }
            if (s.colours != tour::Colours::Alternate) summary = joined({summary, coloursText(s.colours)});
            return summary;
        }
        case TournamentFormat::RoundRobin:
            return joined({TR("Round robin"), trn("{} cycle of 2 games", "{} cycles of 2 games", cycles, cycles)});
        case TournamentFormat::Gauntlet: {
            const auto seeds = static_cast<uint64_t>(std::max(1, s.seeds));
            return joined({TR("Gauntlet"), trn("{} seed", "{} seeds", seeds, seeds),
                           trn("{} cycle of 2 games", "{} cycles of 2 games", cycles, cycles)});
        }
        case TournamentFormat::Swiss:
            return joined({TR("Swiss"), trn("{} round", "{} rounds", cycles, cycles)});
        case TournamentFormat::Knockout:
            return joined({TR("Knockout"), trn("{} game pair per match", "{} game pairs per match", cycles, cycles)});
        case TournamentFormat::TeamMatch:
            return joined({TR("Team match"), s.team_boards ? TR("board by board") : TR("everyone against everyone"),
                           trn("{} cycle of 2 games", "{} cycles of 2 games", cycles, cycles)});
    }
    return "";
}

std::string openingsSummary(const MatchSetup& s, const std::vector<BookEntry>& books) {
    // The participants' own books choose their moves as the randomness says too.
    const auto people = setupParticipants(s);
    const bool own    = std::any_of(people.begin(), people.end(), [](const auto& p) { return !p.book.empty(); });
    const auto own_text =
        own && s.book_randomness != 0 ? trf("own books: {}", randomnessText(s.book_randomness)) : std::string();
    const auto unbalanced = unbalancedText(s);
    if (s.book.empty()) {
        return joined({s.chess960 ? std::string(TR("Chess960: the 960 starting positions"))
                                  : std::string(TR("every game from the initial position")),
                       own_text, unbalanced});
    }
    const bool polyglot = tour::isPolyglot(s.book);
    std::vector<std::string> parts{bookLabel(s.book, books)};
    // A Polyglot book is always followed to some depth: the default one when none is set.
    const int plies = polyglot && s.book_plies <= 0 ? POLYGLOT_PLIES : s.book_plies;
    if (plies > 0) parts.push_back(trn("{} ply", "{} plies", static_cast<uint64_t>(plies), plies));
    parts.push_back(s.book_random ? TR("random") : TR("in order"));
    if (polyglot) parts.push_back(randomnessText(s.book_randomness));
    if (s.book_start > 1) parts.push_back(trf("from opening {}", s.book_start));
    if (s.seed > 0) parts.push_back(trf("seed {}", s.seed));
    if (s.chess960) parts.push_back("Chess960");
    if (s.opening_per_round) parts.push_back(TR("one opening per round"));
    if (!polyglot) parts.push_back(own_text);
    parts.push_back(unbalanced);
    return joined(parts);
}

std::string adjudicationSummary(const MatchSetup& s) {
    std::vector<std::string> parts;
    if (s.draw_enabled) parts.push_back(trf("draw {}/{}/{} cp", s.draw_movenumber, s.draw_movecount, s.draw_score));
    if (s.resign_enabled) {
        parts.push_back(s.resign_twosided ? trf("resign {}/{} cp, both engines", s.resign_movecount, s.resign_score)
                                          : trf("resign {}/{} cp", s.resign_movecount, s.resign_score));
    }
    if (s.maxmoves_enabled) parts.push_back(trf("at most {} moves", s.maxmoves));
    if (s.tb_enabled) {
        parts.push_back(s.tb_pieces > 0
                            ? trn("TB {} piece", "TB {} pieces", static_cast<uint64_t>(s.tb_pieces), s.tb_pieces)
                            : std::string(TR("TB")));
    }
    if (parts.empty()) return TR("none: every game is played to its end");
    return joined(parts);
}

std::string enginesSummary(const MatchSetup& s) {
    std::vector<std::string> parts;
    if (s.common_hash > 0) parts.push_back(trf("Hash {} MB", s.common_hash));
    if (s.common_threads > 0) {
        parts.push_back(trn("{} thread", "{} threads", static_cast<uint64_t>(s.common_threads), s.common_threads));
    }
    if (s.engine_syzygy) parts.push_back("Syzygy");
    if (s.engine_ownbook_off) parts.push_back(TR("own books off"));
    if (parts.empty()) return TR("each engine's own settings");
    return joined(parts);
}

}  // namespace

std::string sectionSummary(const MatchSetup& setup, SetupSection section, const std::vector<BookEntry>& books) {
    switch (section) {
        case SetupSection::Format:
            return formatSummary(setup);
        case SetupSection::Time:
            return timeSummary(setup);
        case SetupSection::Openings:
            return openingsSummary(setup, books);
        case SetupSection::Adjudication:
            return adjudicationSummary(setup);
        case SetupSection::Engines:
            break;
    }
    return enginesSummary(setup);
}

std::string sectionCommandLine(const MatchSetup& s, SetupSection section) {
    switch (section) {
        case SetupSection::Format: {
            const auto rounds = std::max(1, s.cycles);
            switch (s.format) {
                case TournamentFormat::Match: {
                    // Locked colours: fastchess' -noswap (-reverse giving White to the second engine), each game
                    // from an opening of its own (-games 1).
                    const bool locked         = s.colours != tour::Colours::Alternate;
                    const std::string colours = s.colours == tour::Colours::AWhite   ? " -games 1 -noswap"
                                                : s.colours == tour::Colours::BWhite ? " -games 1 -noswap -reverse"
                                                                                     : "";
                    if (s.mode == MatchMode::Sprt) {
                        // The bounds fastchess would be given, the bias on them; it has no minimum of games.
                        return fmt::format("-sprt elo0={} elo1={} alpha={} beta={} model={}{}",
                                           number(s.sprt_elo0 + s.sprt_bias), number(s.sprt_elo1 + s.sprt_bias),
                                           number(s.sprt_alpha), number(s.sprt_beta), s.sprt_model, colours);
                    }
                    if (s.mode == MatchMode::Fixed && locked)
                        return fmt::format("-rounds {}{}", std::max(2, (s.fixed_games + 1) / 2 * 2), colours);
                    if (s.mode == MatchMode::Fixed)
                        return fmt::format("-rounds {} -games 2 -repeat", std::max(1, s.fixed_games / 2));
                    return colours.empty() ? std::string() : colours.substr(1);
                }
                case TournamentFormat::RoundRobin:
                    return fmt::format("-tournament round-robin -rounds {} -games 2 -repeat", rounds);
                case TournamentFormat::Gauntlet:
                    return fmt::format("-tournament gauntlet -seeds {} -rounds {} -games 2 -repeat",
                                       std::max(1, s.seeds), rounds);
                case TournamentFormat::Knockout:
                    return fmt::format("-tournament knockout -games {}", 2 * rounds);
                case TournamentFormat::Swiss:
                case TournamentFormat::TeamMatch:  // neither has one
                    return "";
            }
            return "";
        }
        case SetupSection::Time: {
            std::string text;
            switch (s.limit) {
                case LimitKind::Time:
                    text = fmt::format("tc={}{}{}", s.tc_moves > 0 ? fmt::format("{}/", s.tc_moves) : "",
                                       number(s.tc_base_s), s.tc_inc_s > 0 ? "+" + number(s.tc_inc_s) : "");
                    break;
                case LimitKind::MoveTime:
                    text = "st=" + number(s.move_s);
                    break;
                case LimitKind::Nodes:
                    return fmt::format("nodes={}", s.nodes);
                case LimitKind::Depth:
                    return fmt::format("depth={} (fastchess: plies={})", s.depth, s.depth);
            }
            if (s.tc_margin_ms > 0) text += fmt::format(" timemargin={}", s.tc_margin_ms);
            return text;
        }
        case SetupSection::Openings: {
            if (s.book.empty()) return s.chess960 ? "-variant fischerandom" : "";
            const auto ext     = std::filesystem::path(s.book).extension().string();
            std::string format = ext.size() > 1 ? ext.substr(1) : "epd";
            for (auto& c : format) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            auto text =
                fmt::format("-openings file={} format={} order={}", std::filesystem::path(s.book).filename().string(),
                            format, s.book_random ? "random" : "sequential");
            if (s.book_plies > 0) text += fmt::format(" plies={}", s.book_plies);
            if (s.book_start > 1) text += fmt::format(" start={}", s.book_start);
            if (s.seed > 0) text += fmt::format(" -srand {}", s.seed);
            if (s.chess960) text += " -variant fischerandom";
            return text;
        }
        case SetupSection::Adjudication: {
            std::vector<std::string> parts;
            if (s.draw_enabled) {
                parts.push_back(fmt::format("-draw movenumber={} movecount={} score={}", s.draw_movenumber,
                                            s.draw_movecount, s.draw_score));
            }
            if (s.resign_enabled) {
                parts.push_back(fmt::format("-resign movecount={} score={}{}", s.resign_movecount, s.resign_score,
                                            s.resign_twosided ? " twosided=true" : ""));
            }
            if (s.maxmoves_enabled) parts.push_back(fmt::format("-maxmoves {}", s.maxmoves));
            if (s.tb_enabled) {
                std::string tb = "-tb …";
                if (s.tb_pieces > 0) tb += fmt::format(" -tbpieces {}", s.tb_pieces);
                if (s.tb_ignore50) tb += " -tbignore50";
                if (s.tb_result != 3) tb += s.tb_result == 1 ? " -tbadjudicate WIN_LOSS" : " -tbadjudicate DRAW";
                parts.push_back(tb);
            }
            return joined(parts, " ");
        }
        case SetupSection::Engines:
            break;
    }
    std::vector<std::string> parts;
    if (s.common_hash > 0) parts.push_back(fmt::format("option.Hash={}", s.common_hash));
    if (s.common_threads > 0) parts.push_back(fmt::format("option.Threads={}", s.common_threads));
    if (s.engine_syzygy) parts.push_back("option.SyzygyPath=…");
    if (s.engine_ownbook_off) parts.push_back("option.OwnBook=false");
    return parts.empty() ? std::string() : "-each " + joined(parts, " ");
}

}  // namespace fcg
