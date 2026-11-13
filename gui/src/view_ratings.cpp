// "Ratings": every game of every tournament (and of imported PGN files): the rating list, an engine's card, two
// engines head to head, and the progress of engines over time.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <future>
#include <optional>
#include <set>

#include <imgui.h>
#include <implot.h>

#include <fmt/include/fmt/format.h>

#include <tour/summary.hpp>

#include "app.hpp"
#include "i18n.hpp"
#include "run.hpp"
#include "stats.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

std::string date(int64_t ms) {
    if (ms <= 0) return "?";
    const auto t = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return fmt::format("{:04}-{:02}-{:02}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

std::string score(const fastchess::Stats& s) {
    const int n = s.wins + s.draws + s.losses;
    return fmt::format("+{} ={} -{}  ({:.1f} %)", s.wins, s.draws, s.losses,
                       n ? 100.0 * (s.wins + 0.5 * s.draws) / n : 0);
}

// An engine chooser over the engines of the database.
bool engineCombo(const char* id, std::string& value, const std::vector<std::string>& names) {
    bool changed = false;
    ImGui::SetNextItemWidth(px(240));
    if (ImGui::BeginCombo(id, value.empty() ? TR("(choose)") : value.c_str(), ImGuiComboFlags_HeightLarge)) {
        for (const auto& n : names) {
            if (ImGui::Selectable(n.c_str(), n == value)) {
                value   = n;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

}  // namespace

tour::GameFilter App::ratingFilter() const {
    tour::GameFilter f;
    f.time_control = rating_tc_;
    f.external     = rating_external_;
    if (!rating_group_.empty()) {
        for (const auto& e : engines_) {
            if (std::find(e.groups.begin(), e.groups.end(), rating_group_) != e.groups.end()) f.engines.insert(e.name);
        }
    }
    if (rating_period_ > 0) {
        const auto now =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();
        f.from = now - int64_t{rating_period_ == 1 ? 30 : 365} * 86'400'000;
    }
    return f;
}

const std::vector<std::filesystem::path>& App::ratingPgns() {
    if (!rating_pgns_) rating_pgns_ = storage_.ratingPgns();
    return *rating_pgns_;
}

void App::setRatingPgns(const std::vector<std::filesystem::path>& files) {
    storage_.setRatingPgns(files);
    rating_pgns_   = files;
    game_db_dirty_ = true;
}

std::vector<std::string>& App::separatePlayers() {
    if (!separate_players_) separate_players_ = storage_.separatePlayers();
    return *separate_players_;
}

bool reviewPgnPlayers(const std::vector<tour::GameDb::Player>& players, const std::set<std::string>& before,
                      const std::vector<std::string>& separate) {
    return std::any_of(players.begin(), players.end(), [&](const tour::GameDb::Player& p) {
        return p.engine.empty() && !p.engines.empty() && !isSeparatePlayer(separate, p.name) &&
               !before.count(tour::foldedName(p.name));
    });
}

void App::refreshRatings() {
    if (ratings_job_.valid()) {
        if (ratings_job_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        auto r       = ratings_job_.get();
        game_db_     = std::move(r.db);
        rating_list_ = std::move(r.list);
        // What was chosen meanwhile stays; the defaults fill what is still empty.
        if (rating_anchor_.empty()) rating_anchor_ = r.anchor, rating_anchor_elo_ = r.anchor_elo;
        if (card_engine_.empty()) card_engine_ = r.card_engine;
        if (h2h_a_.empty() && h2h_b_.empty()) h2h_a_ = r.h2h_a, h2h_b_ = r.h2h_b;
        if (progress_engines_.empty()) progress_engines_ = r.progress_engines;
        card_         = std::move(r.card);
        card_history_ = std::move(r.card_history);
        card_search_  = {};
        h2h_          = std::move(r.h2h);
        progress_     = std::move(r.progress);
        // A PGN file just added: the players the user may want to say which engine they are.
        if (review_pgn_players_ == 2) {
            review_pgn_players_ = 0;
            open_pgn_players_   = reviewPgnPlayers(game_db_->players, pgn_players_before_, separatePlayers());
        }
    }
    // The players of the PGN files are matched to the library's engines when the games are read: again when the
    // library's names change, or the names the user gave them, those once the PGN players window is closed (reading
    // a large PGN file again at each choice would keep the user waiting).
    auto aliases = libraryAliases(engines_);
    if (!std::equal(engines_.begin(), engines_.end(), game_db_library_.begin(), game_db_library_.end(),
                    [](const EngineEntry& e, const std::string& name) { return e.name == name; }) ||
        (aliases != game_db_aliases_ && !pgn_players_shown_)) {
        game_db_dirty_ = true;
    }
    if (!game_db_dirty_ && !ratings_dirty_) return;

    std::vector<std::pair<std::string, double>> library;  // the engines with a reference rating, for the anchor
    for (const auto& e : engines_) {
        if (e.elo > 0) library.emplace_back(e.name, e.elo);
    }
    RatingsResult in;
    if (!game_db_dirty_) in.db = game_db_;
    in.anchor           = rating_anchor_;
    in.anchor_elo       = rating_anchor_elo_;
    in.card_engine      = card_engine_;
    in.h2h_a            = h2h_a_;
    in.h2h_b            = h2h_b_;
    in.progress_engines = progress_engines_;
    const auto dirs = game_db_dirty_ ? tour::tournamentDirs(storage_.runsDir()) : std::vector<std::filesystem::path>{};
    if (game_db_dirty_) {
        game_db_library_.clear();
        for (const auto& e : engines_) game_db_library_.push_back(e.name);
        game_db_aliases_ = std::move(aliases);
        if (review_pgn_players_ == 1) {
            review_pgn_players_ = 2;
            pgn_players_before_.clear();
            for (const auto& p : game_db_->players) pgn_players_before_.insert(tour::foldedName(p.name));
        }
    }
    auto work = [in = std::move(in), dirs, pgns = ratingPgns(), filter = ratingFilter(), library = std::move(library),
                 names = game_db_library_, aliases = game_db_aliases_, min_games = rating_min_games_]() mutable {
        if (!in.db) in.db = std::make_shared<tour::GameDb>(tour::readGameDb(dirs, pgns, names, aliases));
        const auto& db = *in.db;

        // The default anchor: the first engine of the library with a reference rating.
        if (in.anchor.empty()) {
            for (const auto& [name, elo] : library) {
                if (db.engine(name)) {
                    in.anchor     = name;
                    in.anchor_elo = elo;
                    break;
                }
            }
        }
        tour::NamedAnchor anchor;
        if (!in.anchor.empty()) anchor = std::make_pair(in.anchor, in.anchor_elo);
        in.list = tour::ratingList(db, filter, anchor, min_games);

        // The card, the head to head and the progress start with the leaders of the list, the anchor (whose
        // rating does not move) last.
        std::vector<std::string> top;
        for (const auto& e : in.list.entries) {
            if (e.name != in.anchor) top.push_back(e.name);
        }
        if (!in.anchor.empty() && db.engine(in.anchor)) top.push_back(in.anchor);
        if (in.card_engine.empty() && !top.empty()) in.card_engine = top[0];
        if (in.h2h_a.empty() && in.h2h_b.empty() && top.size() >= 2) in.h2h_a = top[0], in.h2h_b = top[1];
        if (in.progress_engines.empty() && !top.empty()) in.progress_engines.insert(top[0]);
        if (!in.card_engine.empty()) {
            in.card         = tour::engineRecord(db, filter, in.card_engine);
            in.card_history = tour::ratingHistory(db, filter, anchor, in.card_engine);
        }
        if (!in.h2h_a.empty() && !in.h2h_b.empty()) in.h2h = tour::headToHead(db, filter, in.h2h_a, in.h2h_b);
        for (const auto& name : in.progress_engines) in.progress[name] = tour::ratingHistory(db, filter, anchor, name);
        return std::move(in);
    };
    ratings_job_   = std::async(std::launch::async, std::move(work));
    game_db_dirty_ = false;
    ratings_dirty_ = false;
}

void App::showEngineCard(const std::string& name) {
    card_engine_         = name;
    ratings_dirty_       = true;
    ratings_tab_request_ = 1;
    page_                = Page::Ratings;
}

void App::openGame(const tour::DbGame& game) {
    const auto& source = game_db_->sources[game.source];
    if (source.external) return;
    openTournament(source.path);
    if (!start_error_.empty()) return;
    page_           = Page::Tournament;
    tab_request_    = Tab::Games;
    select_game_id_ = game.number;
}

void App::drawRatings() {
    ui::pageTitle(TR("Ratings"), guideSection());
    ImGui::TextDisabled("%s", TR("Every game of every tournament, and of the PGN files imported, engines known by "
                                 "name; maximum likelihood ratings with 95 % intervals."));
    refreshRatings();
    drawRatingFilters();

    if (!ImGui::BeginTabBar("ratings_tabs")) return;
    const auto tab = [&](const char* label, int index) {
        ImGuiTabItemFlags flags = 0;
        if (ratings_tab_request_ == index) {
            flags = ImGuiTabItemFlags_SetSelected;
            ratings_tab_request_.reset();
        }
        return ImGui::BeginTabItem(label, nullptr, flags);
    };
    if (tab(TR("Rating list###rt_list"), 0)) {
        drawRatingList();
        ImGui::EndTabItem();
    }
    if (tab(TR("Engine###rt_engine"), 1)) {
        drawEngineCard();
        ImGui::EndTabItem();
    }
    if (tab(TR("Head to head###rt_h2h"), 2)) {
        drawHeadToHead();
        ImGui::EndTabItem();
    }
    if (tab(TR("Progress###rt_progress"), 3)) {
        drawProgress();
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

void App::drawRatingFilters() {
    // Which games count: time control, group, period, imported files.
    ImGui::SetNextItemWidth(px(170));
    if (ImGui::BeginCombo("##rating_tc", rating_tc_.empty() ? TR("Every time control") : rating_tc_.c_str())) {
        if (ImGui::Selectable(TR("Every time control"), rating_tc_.empty())) rating_tc_.clear(), ratings_dirty_ = true;
        for (const auto& tc : rating_list_.time_controls) {
            if (ImGui::Selectable(tc.c_str(), tc == rating_tc_)) rating_tc_ = tc, ratings_dirty_ = true;
        }
        ImGui::EndCombo();
    }

    std::vector<std::string> groups;
    for (const auto& e : engines_) {
        for (const auto& g : e.groups) {
            if (std::find(groups.begin(), groups.end(), g) == groups.end()) groups.push_back(g);
        }
    }
    std::sort(groups.begin(), groups.end());
    if (!groups.empty()) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(px(170));
        if (ImGui::BeginCombo("##rating_group", rating_group_.empty() ? TR("All groups") : rating_group_.c_str())) {
            if (ImGui::Selectable(TR("All groups"), rating_group_.empty()))
                rating_group_.clear(), ratings_dirty_ = true;
            for (const auto& g : groups) {
                if (ImGui::Selectable(g.c_str(), g == rating_group_)) rating_group_ = g, ratings_dirty_ = true;
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", TR("Only the games between engines of a group of the library."));
    }

    ui::sameLineOrWrap(px(160));
    ImGui::SetNextItemWidth(px(160));
    const char* periods[] = {TR("Every game"), TR("The last 30 days"), TR("The last 365 days")};
    if (ImGui::Combo("##rating_period", &rating_period_, periods, 3)) ratings_dirty_ = true;

    const auto& pgns = ratingPgns();
    ui::sameLineOrWrap(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
                       ImGui::CalcTextSize(TR("Imported games")).x);
    if (ImGui::Checkbox(TR("Imported games"), &rating_external_)) ratings_dirty_ = true;
    ui::sameLineOrWrap(TR("PGN files…"));
    if (ImGui::Button(TR("PGN files…"))) ImGui::OpenPopup("rating_pgns");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("Games of other programs or rating lists counted with the "
                                   "tournaments'. Engines are known by their name, as in the PGN; a player whose "
                                   "name is a library engine's, letter case and spaces aside, is that engine, and "
                                   "you can say which engine another player is."));
    }
    if (ImGui::BeginPopup("rating_pgns")) {
        int remove = -1;
        for (std::size_t i = 0; i < pgns.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::SmallButton(TR("Remove"))) remove = static_cast<int>(i);
            ImGui::SameLine();
            ImGui::TextUnformatted(pgns[i].string().c_str());
            ImGui::PopID();
        }
        if (pgns.empty()) ImGui::TextDisabled("%s", TR("No PGN file imported."));
        if (ImGui::Button(TR("Add a PGN file…"))) {
            dialogs_.openFile(Dialogs::Purpose::ImportRatingPgn, {{"PGN", "pgn"}, {TR("All files"), "*"}});
            ImGui::CloseCurrentPopup();
        }
        if (!game_db_->players.empty()) {
            ImGui::SameLine();
            if (ImGui::Button(TR("Players and library engines…"))) {
                open_pgn_players_ = true;
                ImGui::CloseCurrentPopup();
            }
        }
        if (remove >= 0) {
            auto list = pgns;
            list.erase(list.begin() + remove);
            setRatingPgns(list);
        }
        ImGui::EndPopup();
    }

    // The anchor, on a line of its own if the window is narrow.
    ui::sameLineOrWrap(ImGui::CalcTextSize(TR("Anchor:")).x + px(200) + ImGui::GetStyle().ItemSpacing.x, px(24));
    ImGui::TextUnformatted(TR("Anchor:"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(200));
    if (ImGui::BeginCombo("##rating_anchor", rating_anchor_.empty() ? TR("Average") : rating_anchor_.c_str())) {
        if (ImGui::Selectable(TR("Average"), rating_anchor_.empty())) rating_anchor_.clear(), ratings_dirty_ = true;
        for (const auto& e : rating_list_.entries) {
            if (ImGui::Selectable(e.name.c_str(), e.name == rating_anchor_)) {
                rating_anchor_ = e.name;
                for (const auto& lib : engines_) {
                    if (lib.name == e.name && lib.elo > 0) rating_anchor_elo_ = lib.elo;
                }
                ratings_dirty_ = true;
            }
        }
        ImGui::EndCombo();
    }
    if (!rating_anchor_.empty()) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(px(90));
        if (ui::inputDouble("##rating_anchor_elo", &rating_anchor_elo_, "%.0f")) ratings_dirty_ = true;
    }
    ImGui::SameLine(0, px(24));
    if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(TR("Refresh")).x + ImGui::GetStyle().FramePadding.x * 2)
        ImGui::NewLine();
    if (ImGui::Button(TR("Refresh"))) game_db_dirty_ = true;
    if (ratings_job_.valid()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("Computing…"));
    }
}

void App::drawRatingList() {
    ImGui::SetNextItemWidth(px(110));
    if (ui::inputInt(TR("games at least"), &rating_min_games_)) {
        rating_min_games_ = std::max(0, rating_min_games_);
        ratings_dirty_    = true;
    }
    ImGui::SameLine(0, px(24));
    if (const auto as = ui::copyButton("ratings"); as != ui::CopyAs::None) {
        const auto text = tour::ratingListText(rating_list_);
        ImGui::SetClipboardText(
            (as == ui::CopyAs::Text ? text : tour::bbcode("Rating list", rating_tc_, text)).c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Export CSV…"))) {
        dialogs_.saveFile(Dialogs::Purpose::ExportRatings, {{"CSV", "csv"}},
                          (storage_.root() / "ratings.csv").string());
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Export for Ordo…"))) {
        dialogs_.saveFile(Dialogs::Purpose::ExportRatingPgn, {{"PGN", "pgn"}},
                          (storage_.root() / "ratings.pgn").string());
    }
    ui::help(
        TR("The games counted, as a PGN of headers, which Ordo and BayesElo read: ordo -p ratings.pgn -o "
           "ratings.txt"));

    ImGui::TextDisabled("%s", trf("{} games, {} tournaments", rating_list_.games, rating_list_.tournaments).c_str());
    drawLibraryMatches();
    if (rating_list_.entries.empty()) {
        if (!ratings_job_.valid()) ImGui::TextDisabled("%s", TR("No game yet."));
        return;
    }

    constexpr auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                           ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;
    if (!ImGui::BeginTable("##ratings", 7, flags)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("#");
    ImGui::TableSetupColumn(TR("Engine"), ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Elo");
    ImGui::TableSetupColumn("±");
    ImGui::TableSetupColumn(TR("Games"));
    ImGui::TableSetupColumn("%");
    ImGui::TableSetupColumn(TR("Tournaments"));
    ImGui::TableHeadersRow();
    for (std::size_t i = 0; i < rating_list_.entries.size(); ++i) {
        const auto& e = rating_list_.entries[i];
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%d", static_cast<int>(i + 1));
        ImGui::TableNextColumn();
        if (ImGui::Selectable(fmt::format("{}##re{}", e.name, i).c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
            showEngineCard(e.name);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("Click for its card."));
        ImGui::TableNextColumn();
        ImGui::Text("%ld", std::lround(e.rating.elo));
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%.0f", e.rating.error);
        ImGui::TableNextColumn();
        ImGui::Text("%d", e.games);
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%.1f", 100.0 * e.score());
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%d", e.tournaments);
    }
    ImGui::EndTable();
}

void App::drawLibraryMatches() {
    // The players of the PGN files taken for library engines: their games are the engine's, and its rating on a
    // list can be the anchor.
    const auto& matched = game_db_->matched;
    const auto& players = game_db_->players;
    if (!rating_external_) return;
    if (!matched.empty()) {
        std::set<std::string> engines;
        for (const auto& m : matched) engines.insert(m.second);
        ImGui::SameLine();
        ImGui::TextDisabled("·");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", trn("{} engine of the library in the PGN files",
                                      "{} engines of the library in the PGN files", engines.size(), engines.size())
                                      .c_str());
        if (ImGui::BeginItemTooltip()) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
            ImGui::TextUnformatted(
                TR("The players of the PGN files taken for engines of the library, by their name, "
                   "letter case and spaces aside, or by your choice: their games are the engine's, "
                   "and its settings apply (its rating on a list, as the anchor)."));
            ImGui::PopTextWrapPos();
            constexpr std::size_t SHOWN = 30;
            for (std::size_t i = 0; i < matched.size() && i < SHOWN; ++i) {
                const auto& [player, engine] = matched[i];
                if (player == engine) {
                    ImGui::BulletText("%s", engine.c_str());
                } else {
                    ImGui::BulletText("%s → %s", player.c_str(), engine.c_str());
                }
            }
            if (matched.size() > SHOWN) ImGui::TextDisabled("%s", trf("and {} more", matched.size() - SHOWN).c_str());
            ImGui::EndTooltip();
        }
    }

    // The others, for the user to say which engine they are; in the warning colour when two library engines may be
    // one of them.
    if (players.empty()) return;
    const auto& separate = separatePlayers();
    const auto ambiguous = static_cast<int>(std::count_if(players.begin(), players.end(), [&](const auto& p) {
        return p.ambiguous && !isSeparatePlayer(separate, p.name);
    }));
    ImGui::SameLine();
    if (ambiguous > 0) ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
    if (ImGui::SmallButton(TR("PGN players…"))) open_pgn_players_ = true;
    if (ambiguous > 0) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", ambiguous > 0 ? trn("{} player that two library engines may be: say which one.",
                                                    "{} players that two library engines may be: say which one.",
                                                    ambiguous, ambiguous)
                                                    .c_str()
                                              : TR("Say which library engine a player of the PGN files is."));
    }
}

void App::drawPgnPlayers() {
    const char* title = TR("PGN players and library engines###pgn_players");
    if (open_pgn_players_) {
        ImGui::OpenPopup(title);
        open_pgn_players_ = false;
    }
    ImGui::SetNextWindowSize(ImVec2(px(820), 0), ImGuiCond_Appearing);
    pgn_players_shown_ = ImGui::BeginPopupModal(title, nullptr);
    if (!pgn_players_shown_) return;
    // The games are read again with the choices once the window is closed; the rows stay where they are meanwhile.
    const auto& players = game_db_->players;
    auto& separate      = separatePlayers();

    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s", TR("The players of the PGN files that are no library engine by their name, letter case "
                                 "and spaces aside. Say which library engine a player is, and its games are the "
                                 "engine's; the choice is kept for every PGN file that names the player. A name that "
                                 "two library engines have waits for your choice; a hint is a guess, never applied."));
    ImGui::PopTextWrapPos();

    char buffer[128];
    const auto n = std::min(pgn_players_filter_.size(), sizeof(buffer) - 1);
    std::copy_n(pgn_players_filter_.data(), n, buffer);
    buffer[n] = 0;
    ImGui::SetNextItemWidth(px(260));
    if (ImGui::InputTextWithHint("##pgn_players_filter", TR("Search"), buffer, sizeof(buffer)))
        pgn_players_filter_ = buffer;
    const auto lower = [](std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const auto needle = lower(pgn_players_filter_);
    std::vector<int> shown;
    for (int i = 0; i < static_cast<int>(players.size()); ++i) {
        if (needle.empty() || lower(players[i].name).find(needle) != std::string::npos) shown.push_back(i);
    }

    // The library's engines, in alphabetical order, for the combos.
    std::vector<std::string> names;
    for (const auto& e : engines_) names.push_back(e.name);
    std::sort(names.begin(), names.end(), [&](const auto& a, const auto& b) { return lower(a) < lower(b); });

    std::optional<std::pair<std::string, std::string>> choice;  // player, engine (empty: a separate engine)
    // Of the same height whatever is shown: the window is sized once, when it opens, maybe before the games are read.
    const ImVec2 size(0, ImGui::GetFrameHeightWithSpacing() * 12);
    if (!shown.empty() &&
        ImGui::BeginTable("##pgn_players", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp, size)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(TR("Player"), ImGuiTableColumnFlags_WidthStretch, 0.35f);
        ImGui::TableSetupColumn(TR("Library engine"), ImGuiTableColumnFlags_WidthStretch, 0.35f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.3f);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(shown.size()));
        while (clipper.Step()) {
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                const auto& p = players[shown[r]];
                // The user's choice as it is now, while the games may still be read with the one before.
                const auto engine = libraryAlias(engines_, p.name);
                const bool apart  = engine.empty() && isSeparatePlayer(separate, p.name);
                ImGui::TableNextRow();
                ImGui::PushID(shown[r]);
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(p.name.c_str());
                ImGui::TableNextColumn();
                const bool waiting = engine.empty() && !apart && p.ambiguous;
                if (waiting) ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
                ImGui::SetNextItemWidth(-FLT_MIN);
                const char* preview = !engine.empty() ? engine.c_str()
                                      : waiting       ? TR("Choose…")
                                                      : TR("A separate engine");
                const bool open     = ImGui::BeginCombo("##engine", preview, ImGuiComboFlags_HeightLarge);
                if (waiting) ImGui::PopStyleColor();
                if (open) {
                    if (ImGui::Selectable(TR("A separate engine"), engine.empty())) choice.emplace(p.name, "");
                    // The engines of its name, or that of the hint, first.
                    for (const auto& e : p.engines) {
                        if (ImGui::Selectable(e.c_str(), e == engine)) choice.emplace(p.name, e);
                    }
                    ImGui::Separator();
                    if (const int i = ui::searchComboItems(names, engine); i >= 0) choice.emplace(p.name, names[i]);
                    ImGui::EndCombo();
                }
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                if (!engine.empty() || apart) {
                    ImGui::TextDisabled("%s", TR("your choice"));
                } else if (p.ambiguous) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
                    ImGui::TextUnformatted(trn("{} library engine has this name", "{} library engines have this name",
                                               p.engines.size(), p.engines.size())
                                               .c_str());
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("Letter case and spaces aside."));
                } else if (!p.engines.empty()) {
                    ImGui::TextDisabled("%s", trf("Perhaps {}?", p.engines.front()).c_str());
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", TR("Its name begins with this library engine's: choose it if it is "
                                                   "the same engine, version included."));
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    } else if (shown.empty()) {
        ImGui::BeginChild("##pgn_players_none", size);
        ImGui::TextDisabled("%s", !players.empty()       ? TR("Nothing found.")
                                  : ratings_job_.valid() ? TR("Computing…")
                                                         : TR("Every player of the PGN files is a library engine by "
                                                              "its name: nothing to choose."));
        ImGui::EndChild();
    }
    if (choice) {
        // refreshRatings() reads the games again with it once the window is closed.
        if (setLibraryAlias(engines_, choice->first, choice->second)) storage_.saveEngines(engines_);
        // A separate engine is a decision too: the player is no longer warned about nor offered again.
        if (setSeparatePlayer(separate, choice->first, choice->second.empty())) storage_.setSeparatePlayers(separate);
    }

    const auto ambiguous = std::count_if(players.begin(), players.end(), [](const auto& p) { return p.ambiguous; });
    ImGui::TextDisabled("%s", trn("{} player", "{} players", players.size(), players.size()).c_str());
    if (ambiguous > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("· %s", trn("{} that two library engines may be", "{} that two library engines may be",
                                        ambiguous, ambiguous)
                                        .c_str());
    }
    if (ImGui::Button(TR("Close"))) {
        pgn_players_filter_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void App::drawEngineCard() {
    if (engineCombo("##card_engine", card_engine_, game_db_->engines)) ratings_dirty_ = true;
    if (card_engine_.empty() || !game_db_->engine(card_engine_)) {
        ImGui::TextDisabled("%s",
                            TR("Choose an engine: its results, its health and its rating over every tournament."));
        return;
    }

    const auto& t    = card_.total;
    const auto entry = std::find_if(rating_list_.entries.begin(), rating_list_.entries.end(),
                                    [&](const auto& e) { return e.name == card_engine_; });
    ImGui::SameLine(0, px(24));
    if (entry != rating_list_.entries.end()) {
        ImGui::Text("%s", trf("Elo {} ± {:.0f}", std::lround(entry->rating.elo), entry->rating.error).c_str());
        ImGui::SameLine();
    }
    ImGui::TextDisabled("%s", trf("{} games · {}", t.wins + t.draws + t.losses, score(t)).c_str());

    // Health.
    const auto health = [](bool ok, const std::string& text) {
        ImGui::PushStyleColor(ImGuiCol_Text, ok ? ui::GOOD : ui::WARN);
        ImGui::TextUnformatted(ok ? "✓" : "!");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextUnformatted(text.c_str());
    };
    health(card_.timeouts == 0, trn("{} loss on time", "{} losses on time", card_.timeouts, card_.timeouts));
    ImGui::SameLine(0, px(24));
    health(card_.crashes == 0, trn("{} crash or stall", "{} crashes or stalls", card_.crashes, card_.crashes));
    ImGui::SameLine(0, px(24));
    health(card_.illegal == 0, trn("{} illegal move", "{} illegal moves", card_.illegal, card_.illegal));
    ImGui::SameLine(0, px(24));
    ImGui::TextDisabled(
        "%s", trn("{} game adjudicated", "{} games adjudicated", card_.adjudicated, card_.adjudicated).c_str());

    // Search statistics, read from the move comments of its games when asked: thousands of games take a moment.
    if (!card_search_.read) {
        if (ImGui::SmallButton(TR("Read its search statistics"))) {
            const auto id = *game_db_->engine(card_engine_);
            SearchAverages avg;
            avg.read = true;
            std::map<uint32_t, std::ifstream> files;
            uint64_t nps_samples = 0;
            const auto filter    = ratingFilter();
            for (const auto& g : game_db_->games) {
                if ((g.white != id && g.black != id) || !filter.accepts(*game_db_, g)) continue;
                const auto& src = game_db_->sources[g.source];
                if (src.external) continue;
                const auto text = tour::readSpan(src.pgn, g.pgn);
                if (!text) continue;
                forEachMoveComment(*text, [&](bool white, const ReplayMove& m) {
                    if (m.book || white != (g.white == id)) return;
                    avg.moves++;
                    avg.depth += m.depth;
                    avg.time_ms += std::max<int64_t>(0, m.time_ms);
                    if (m.nps > 0) avg.nps += static_cast<double>(m.nps), nps_samples++;
                });
            }
            if (avg.moves) avg.depth /= avg.moves, avg.time_ms /= avg.moves;
            if (nps_samples) avg.nps /= nps_samples;
            card_search_ = avg;
        }
    } else if (card_search_.moves > 0) {
        ImGui::TextDisabled(
            "%s",
            trf("{} moves · depth {:.1f} · {:.0f} ms per move{}", card_search_.moves, card_search_.depth,
                card_search_.time_ms,
                card_search_.nps > 0 ? " · " + ui::formatCount(static_cast<uint64_t>(card_search_.nps)) + " nps" : "")
                .c_str());
    }

    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::BeginChild("##card_left", ImVec2(half, 0));

    // By time control.
    constexpr auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit;
    ImGui::SeparatorText(TR("By time control"));
    if (ImGui::BeginTable("##card_tc", 2, flags)) {
        for (const auto& [tc, s] : card_.by_time_control) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(tc.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(score(s).c_str());
        }
        ImGui::EndTable();
    }

    // Its tournaments, the last first.
    ImGui::SeparatorText(TR("Tournaments"));
    if (ImGui::BeginTable("##card_sources", 3, flags | ImGuiTableFlags_ScrollY, ImVec2(0, px(260)))) {
        for (auto it = card_.sources.rbegin(); it != card_.sources.rend(); ++it) {
            const auto& src = game_db_->sources[it->source];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", date(it->last_game).c_str());
            ImGui::TableNextColumn();
            if (!src.external) {
                if (ImGui::Selectable(fmt::format("{}##cs{}", src.name, it->source).c_str())) openTournament(src.path);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("Open the tournament."));
            } else {
                ImGui::TextUnformatted(src.name.c_str());
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(score(it->stats).c_str());
        }
        ImGui::EndTable();
    }

    // The games lost on time, by a crash or an illegal move.
    if (!card_.problem_games.empty()) {
        ImGui::SeparatorText(TR("Games lost on time, by a crash or an illegal move"));
        for (const auto i : card_.problem_games) {
            const auto& g   = game_db_->games[i];
            const auto& src = game_db_->sources[g.source];
            const auto label =
                fmt::format("{}  {} – {}  {}##pg{}", src.name, game_db_->engines[g.white], game_db_->engines[g.black],
                            g.number ? fmt::format("#{}", g.number) : std::string(), i);
            if (ImGui::Selectable(label.c_str())) openGame(g);
        }
    }
    ImGui::EndChild();

    // Its rating over time.
    ImGui::SameLine();
    ImGui::BeginChild("##card_right");
    ImGui::SeparatorText(TR("Rating after each tournament"));
    if (card_history_.size() >= 2 && ImPlot::BeginPlot("##card_history", ImVec2(-1, px(320)))) {
        std::vector<double> x, y, lo, hi;
        for (std::size_t i = 0; i < card_history_.size(); ++i) {
            x.push_back(static_cast<double>(card_history_[i].time) / 1000.0);
            y.push_back(card_history_[i].rating.elo);
            lo.push_back(card_history_[i].rating.elo - card_history_[i].rating.error);
            hi.push_back(card_history_[i].rating.elo + card_history_[i].rating.error);
        }
        ImPlot::SetupAxes(TR("Date"), "Elo");
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
        ImPlotSpec band;
        band.FillColor = ImVec4(0.43f, 0.66f, 0.87f, 0.25f);
        ImPlot::PlotShaded("##band", x.data(), lo.data(), hi.data(), static_cast<int>(x.size()), band);
        ImPlotSpec line;
        line.LineColor  = ImVec4(0.43f, 0.66f, 0.87f, 1.0f);
        line.LineWeight = 1.5f;
        line.Marker     = ImPlotMarker_Circle;
        ImPlot::PlotLine(card_engine_.c_str(), x.data(), y.data(), static_cast<int>(x.size()), line);
        ImPlot::EndPlot();
    } else {
        ImGui::TextDisabled("%s", TR("Once it has played two tournaments."));
    }
    ImGui::EndChild();
}

void App::drawHeadToHead() {
    bool changed = engineCombo("##h2h_a", h2h_a_, game_db_->engines);
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("against"));
    ImGui::SameLine();
    changed |= engineCombo("##h2h_b", h2h_b_, game_db_->engines);
    if (changed) ratings_dirty_ = true;
    if (h2h_a_.empty() || h2h_b_.empty() || h2h_a_ == h2h_b_) {
        ImGui::TextDisabled("%s", TR("Choose two engines: their games against each other, over every tournament."));
        return;
    }
    if (h2h_.games.empty()) {
        ImGui::TextDisabled("%s", TR("They have not played each other."));
        return;
    }

    const auto& s    = h2h_.total;
    const bool pairs = s.penta_WW + s.penta_WD + s.penta_WL + s.penta_DD + s.penta_LD + s.penta_LL > 0;
    const auto stats = computeStats(s, false, nullptr);
    ImGui::Spacing();
    ImGui::Text("%s", trf("{}: {}", h2h_a_, score(s)).c_str());
    ImGui::SameLine(0, px(24));
    ImGui::TextUnformatted(
        stats.valid ? trf("Elo {}  ·  LOS {:.1f} %", formatElo(stats.elo, stats.elo_err), stats.los).c_str() : "Elo —");
    if (pairs) {
        ImGui::TextDisabled(TR("· pairs [0-2]: %d  %d  %d  %d  %d"), s.penta_LL, s.penta_LD, s.penta_WL + s.penta_DD,
                            s.penta_WD, s.penta_WW);
        ui::help(TR("Results per game pair (same opening, colours reversed), in points for A: 0, 0.5, 1, 1.5, 2."));
    }
    ImGui::SameLine(0, px(24));
    if (ImGui::Button(TR("Export the games…"))) {
        // Names of imported PGNs may hold any bytes, and characters a file name cannot have.
        const auto name = fmt::format("{}-vs-{}.pgn", cleanName(h2h_a_), cleanName(h2h_b_));
        dialogs_.saveFile(Dialogs::Purpose::ExportHeadToHead, {{"PGN", "pgn"}}, (storage_.root() / name).string());
    }

    const float third    = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3;
    constexpr auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit;
    ImGui::BeginChild("##h2h_left", ImVec2(third, 0));
    ImGui::SeparatorText(TR("By time control"));
    if (ImGui::BeginTable("##h2h_tc", 2, flags)) {
        for (const auto& [tc, st] : h2h_.by_time_control) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(tc.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(score(st).c_str());
        }
        ImGui::EndTable();
    }
    ImGui::SeparatorText(TR("Tournaments"));
    if (ImGui::BeginTable("##h2h_src", 2, flags)) {
        for (const auto& [source, st] : h2h_.by_source) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(game_db_->sources[source].name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(score(st).c_str());
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##h2h_games");
    ImGui::SeparatorText(TR("Games"));
    if (ImGui::BeginTable("##h2h_list", 4, flags | ImGuiTableFlags_ScrollY)) {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(h2h_.games.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto& g = game_db_->games[h2h_.games[h2h_.games.size() - 1 - static_cast<std::size_t>(row)]];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", date(g.played_at).c_str());
                ImGui::TableNextColumn();
                const auto label =
                    fmt::format("{} – {}##hg{}", game_db_->engines[g.white], game_db_->engines[g.black], row);
                if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) openGame(g);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(g.white_score == 1 ? "1-0" : g.white_score == 0 ? "0-1" : "½-½");
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", game_db_->sources[g.source].name.c_str());
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

void App::drawProgress() {
    ImGui::TextDisabled("%s", TR("The rating of the engines chosen after each tournament they played: the versions "
                                 "of an engine, side by side."));
    const float page = ImGui::GetContentRegionAvail().x;
    ImGui::BeginChild("##progress_pick", ImVec2(ui::paneWidth(setup_.panes["progress"], page, px(220), px(480)), 0),
                      ImGuiChildFlags_Borders);
    for (const auto& e : rating_list_.entries) {
        bool on = progress_engines_.count(e.name) > 0;
        if (ImGui::Checkbox(e.name.c_str(), &on)) {
            if (on) {
                progress_engines_.insert(e.name);
            } else {
                progress_engines_.erase(e.name);
            }
            ratings_dirty_ = true;
        }
    }
    ImGui::EndChild();
    if (ui::splitter("##progress_split", setup_.panes["progress"], page)) saveSetupSoon();
    ImGui::BeginChild("##progress_plot");
    if (progress_.empty()) {
        ui::emptyState(TR("Tick engines on the left."));
    } else if (ImPlot::BeginPlot("##progress", ImVec2(-1, -1))) {
        ImPlot::SetupAxes(TR("Date"), "Elo");
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
        for (const auto& [name, points] : progress_) {
            std::vector<double> x, y, err;
            for (const auto& p : points) {
                x.push_back(static_cast<double>(p.time) / 1000.0);
                y.push_back(p.rating.elo);
                err.push_back(p.rating.error);
            }
            ImPlot::PlotErrorBars(name.c_str(), x.data(), y.data(), err.data(), static_cast<int>(x.size()));
            ImPlotSpec line;
            line.Marker = ImPlotMarker_Circle;
            ImPlot::PlotLine(name.c_str(), x.data(), y.data(), static_cast<int>(x.size()), line);
        }
        ImPlot::EndPlot();
    }
    ImGui::EndChild();
}

}  // namespace fcg
