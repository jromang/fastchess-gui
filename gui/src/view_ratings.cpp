// "Ratings": every game of every tournament (and of imported PGN files): the rating list, an engine's card, two
// engines head to head, and the progress of engines over time.

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>

#include <imgui.h>
#include <implot.h>

#include <fmt/include/fmt/format.h>

#include <tour/summary.hpp>

#include "app.hpp"
#include "i18n.hpp"
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

void App::refreshRatings() {
    if (game_db_dirty_) {
        game_db_       = tour::readGameDb(tour::tournamentDirs(storage_.runsDir()), storage_.ratingPgns());
        game_db_dirty_ = false;
        ratings_dirty_ = true;
    }
    if (!ratings_dirty_) return;

    // The default anchor: the first engine of the library with a reference rating.
    if (rating_anchor_.empty()) {
        for (const auto& e : engines_) {
            if (e.elo > 0 && game_db_.engine(e.name)) {
                rating_anchor_     = e.name;
                rating_anchor_elo_ = e.elo;
                break;
            }
        }
    }
    tour::NamedAnchor anchor;
    if (!rating_anchor_.empty()) anchor = std::make_pair(rating_anchor_, rating_anchor_elo_);

    const auto filter = ratingFilter();
    rating_list_      = tour::ratingList(game_db_, filter, anchor, rating_min_games_);

    // The card, the head to head and the progress start with the leaders of the list, the anchor (whose rating
    // does not move) last.
    std::vector<std::string> top;
    for (const auto& e : rating_list_.entries) {
        if (e.name != rating_anchor_) top.push_back(e.name);
    }
    if (!rating_anchor_.empty() && game_db_.engine(rating_anchor_)) top.push_back(rating_anchor_);
    if (card_engine_.empty() && !top.empty()) card_engine_ = top[0];
    if (h2h_a_.empty() && h2h_b_.empty() && top.size() >= 2) h2h_a_ = top[0], h2h_b_ = top[1];
    if (progress_engines_.empty() && !top.empty()) progress_engines_.insert(top[0]);
    if (!card_engine_.empty()) {
        card_         = tour::engineRecord(game_db_, filter, card_engine_);
        card_history_ = tour::ratingHistory(game_db_, filter, anchor, card_engine_);
        card_search_  = {};
    }
    if (!h2h_a_.empty() && !h2h_b_.empty()) h2h_ = tour::headToHead(game_db_, filter, h2h_a_, h2h_b_);
    progress_.clear();
    for (const auto& name : progress_engines_) progress_[name] = tour::ratingHistory(game_db_, filter, anchor, name);
    ratings_dirty_ = false;
}

void App::showEngineCard(const std::string& name) {
    card_engine_         = name;
    ratings_dirty_       = true;
    ratings_tab_request_ = 1;
    page_                = Page::Ratings;
}

void App::openGame(const tour::DbGame& game) {
    const auto& source = game_db_.sources[game.source];
    if (source.external) return;
    openTournament(source.path);
    if (!start_error_.empty()) return;
    page_           = Page::Tournament;
    tab_request_    = Tab::Games;
    select_game_id_ = game.number;
}

void App::drawRatings() {
    ui::pageTitle(TR("Ratings"));
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

    const auto pgns = storage_.ratingPgns();
    ui::sameLineOrWrap(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
                       ImGui::CalcTextSize(TR("Imported games")).x);
    if (ImGui::Checkbox(TR("Imported games"), &rating_external_)) ratings_dirty_ = true;
    ui::sameLineOrWrap(TR("PGN files…"));
    if (ImGui::Button(TR("PGN files…"))) ImGui::OpenPopup("rating_pgns");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("Games of other programs or rating lists (CCRL, CEGT…) counted with the "
                                   "tournaments'. Engines are known by their name, as in the PGN."));
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
        if (remove >= 0) {
            auto list = pgns;
            list.erase(list.begin() + remove);
            storage_.setRatingPgns(list);
            game_db_dirty_ = true;
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
        if (ImGui::InputDouble("##rating_anchor_elo", &rating_anchor_elo_, 0, 0, "%.0f")) ratings_dirty_ = true;
    }
    ImGui::SameLine(0, px(24));
    if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(TR("Refresh")).x + ImGui::GetStyle().FramePadding.x * 2)
        ImGui::NewLine();
    if (ImGui::Button(TR("Refresh"))) game_db_dirty_ = true;
}

void App::drawRatingList() {
    ImGui::SetNextItemWidth(px(110));
    if (ImGui::InputInt(TR("games at least"), &rating_min_games_, 0, 0)) {
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
    if (rating_list_.entries.empty()) {
        ImGui::TextDisabled("%s", TR("No game yet."));
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

void App::drawEngineCard() {
    if (engineCombo("##card_engine", card_engine_, game_db_.engines)) ratings_dirty_ = true;
    if (card_engine_.empty() || !game_db_.engine(card_engine_)) {
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
    health(card_.timeouts == 0, trf("{} losses on time", card_.timeouts));
    ImGui::SameLine(0, px(24));
    health(card_.crashes == 0, trf("{} crashes or stalls", card_.crashes));
    ImGui::SameLine(0, px(24));
    health(card_.illegal == 0, trf("{} illegal moves", card_.illegal));
    ImGui::SameLine(0, px(24));
    ImGui::TextDisabled("%s", trf("{} games adjudicated", card_.adjudicated).c_str());

    // Search statistics, read from the move comments of its games when asked: thousands of games take a moment.
    if (!card_search_.read) {
        if (ImGui::SmallButton(TR("Read its search statistics"))) {
            const auto id = *game_db_.engine(card_engine_);
            SearchAverages avg;
            avg.read = true;
            std::map<uint32_t, std::ifstream> files;
            uint64_t nps_samples = 0;
            const auto filter    = ratingFilter();
            for (const auto& g : game_db_.games) {
                if ((g.white != id && g.black != id) || !filter.accepts(game_db_, g)) continue;
                const auto& src = game_db_.sources[g.source];
                if (src.external) continue;
                const auto text = tour::readSpan(src.path / "games.pgn", g.pgn);
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
            const auto& src = game_db_.sources[it->source];
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
            const auto& g   = game_db_.games[i];
            const auto& src = game_db_.sources[g.source];
            const auto label =
                fmt::format("{}  {} – {}  {}##pg{}", src.name, game_db_.engines[g.white], game_db_.engines[g.black],
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
    bool changed = engineCombo("##h2h_a", h2h_a_, game_db_.engines);
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("against"));
    ImGui::SameLine();
    changed |= engineCombo("##h2h_b", h2h_b_, game_db_.engines);
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
        dialogs_.saveFile(Dialogs::Purpose::ExportHeadToHead, {{"PGN", "pgn"}},
                          (storage_.root() / fmt::format("{}-vs-{}.pgn", h2h_a_, h2h_b_)).string());
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
            ImGui::TextUnformatted(game_db_.sources[source].name.c_str());
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
                const auto& g = game_db_.games[h2h_.games[h2h_.games.size() - 1 - static_cast<std::size_t>(row)]];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", date(g.played_at).c_str());
                ImGui::TableNextColumn();
                const auto label =
                    fmt::format("{} – {}##hg{}", game_db_.engines[g.white], game_db_.engines[g.black], row);
                if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) openGame(g);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(g.white_score == 1 ? "1-0" : g.white_score == 0 ? "0-1" : "½-½");
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", game_db_.sources[g.source].name.c_str());
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
