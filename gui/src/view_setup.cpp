// The "Setup" window: everything a run is started with.

#include <imgui.h>
#include <imgui_internal.h>  // ImHashStr: a combo opened for a screenshot
#include <implot.h>

#include <fmt/include/fmt/format.h>

#include <tour/pgn_book.hpp>
#include <tour/polyglot.hpp>
#include <tour/sprt_rules.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>

#include "app.hpp"
#include "i18n.hpp"
#include "memory.hpp"
#include "participants.hpp"
#include "run.hpp"
#include "setup_checks.hpp"
#include "stats.hpp"
#include "storage.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

constexpr float LABEL_WIDTH = 150.0f;

float labelWidth() { return LABEL_WIDTH * ImGui::GetFontSize() / 16.0f; }
float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

bool inputInt(const char* label, const char* id, int& value, int min, int max, float width = 110.0f) {
    ui::label(label, labelWidth());
    ImGui::SetNextItemWidth(width * ImGui::GetFontSize() / 16.0f);
    const bool changed = ui::inputInt(id, &value);
    if (changed) value = std::clamp(value, min, max);
    return changed;
}

bool inputDouble(const char* label, const char* id, double& value, double min, double max, const char* fmt = "%.2f",
                 float width = 110.0f) {
    ui::label(label, labelWidth());
    ImGui::SetNextItemWidth(width * ImGui::GetFontSize() / 16.0f);
    const bool changed = ui::inputDouble(id, &value, fmt);
    if (changed) value = std::clamp(value, min, max);
    return changed;
}

bool inputText(const char* id, std::string& value, float width, const char* hint = nullptr) {
    char buffer[2048];
    const auto n = std::min(value.size(), sizeof(buffer) - 1);
    std::copy_n(value.data(), n, buffer);
    buffer[n] = 0;

    ImGui::SetNextItemWidth(width);
    if (hint ? ImGui::InputTextWithHint(id, hint, buffer, sizeof(buffer))
             : ImGui::InputText(id, buffer, sizeof(buffer))) {
        value = buffer;
        return true;
    }
    return false;
}

// A tooltip at the cursor for the item just drawn, wrapped: the help that used to be a grey line under it.
void itemHelp(const std::string& text) {
    if (text.empty() || !ImGui::BeginItemTooltip()) return;
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

// How the moves of the Polyglot books are chosen, from -100 to 100, its meaning beside the number.
bool randomnessSlider(const char* id, int& value, float width) {
    const int v        = value;
    const char* format = v == -tour::RANDOMNESS_LIMIT ? TR("%d · the best moves only")
                         : v < 0                      ? TR("%d · toward the best moves")
                         : v == 0                     ? TR("%d · moves by their weights")
                         : v < tour::RANDOMNESS_LIMIT ? TR("%d · toward every move alike")
                                                      : TR("%d · every move alike");
    ImGui::SetNextItemWidth(width);
    return ImGui::SliderInt(id, &value, -tour::RANDOMNESS_LIMIT, tour::RANDOMNESS_LIMIT, format,
                            ImGuiSliderFlags_AlwaysClamp);
}

std::string randomnessHelp() {
    return std::string(TR("How the moves of a Polyglot book are chosen, for the tournament's openings and for the "
                          "participants' own books: -100 the best move only, 0 in proportion to their weights, as "
                          "Polyglot and Cute Chess choose, 100 every move alike. In between, -50 squares the weights "
                          "and 50 takes their square roots.")) +
           "\n" + TR("Ctrl+click to type a value.");
}

// The Syzygy directories of the setup, typed or added with the folder dialog. This machine's: in no profile, the same
// for the adjudication and for the engines.
bool tablebaseDirs(const char* id, std::string& paths, Dialogs& dialogs) {
    const float browse = ImGui::CalcTextSize("…").x + ImGui::GetStyle().FramePadding.x * 4;
#ifdef _WIN32
    const char* how = TR("Directories separated by ';' (WDL files are enough).");
#else
    const char* how = TR("Directories separated by ':' (WDL files are enough).");
#endif
    const bool changed =
        inputText(id, paths, ImGui::GetContentRegionAvail().x - browse - ImGui::GetStyle().ItemSpacing.x, how);
    itemHelp(paths.empty() ? std::string(how)
                           : fmt::format("{}\n\n{}\n{}", paths, how,
                                         TR("The directories are this computer's: no profile holds them.")));
    ImGui::SameLine();
    ImGui::PushID(id);
    if (ImGui::Button("…")) dialogs.openFolder(Dialogs::Purpose::Tablebases);
    ImGui::PopID();
    return changed;
}

// An engine of the list, `width` wide (-FLT_MIN: the width left), with a search field. The inactive engines are not
// offered.
bool engineCombo(const char* id, std::string& value, const std::vector<EngineEntry>& engines, float width = -FLT_MIN) {
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(id, value.empty() ? TR("(choose)") : value.c_str(), ImGuiComboFlags_HeightLargest)) {
        std::vector<std::string> names, more;
        for (const auto& e : engines) {
            if (e.inactive && e.name != value) continue;
            names.push_back(e.name);
            more.push_back(engineSearchText(e));
        }
        if (const int i = ui::searchComboItems(names, value, &more); i >= 0) {
            value   = names[i];
            changed = true;
        }
        if (engines.empty()) ImGui::TextDisabled("%s", TR("Add engines in the Engines window."));
        ImGui::EndCombo();
    }
    return changed;
}

std::string ownBookText(const OwnBook& book) { return fcg::ownBookText(ownBookName(book), book.depth); }

using BookState = PgnBookJobs::Status::State;

int percent(double share) { return static_cast<int>(std::clamp(share, 0.0, 1.0) * 100); }

// What a PGN book's conversion found: "Prepared from 1200 games: 5400 positions."
std::string pgnBookStatsText(const tour::PgnBookStats& stats) {
    return trn("Prepared from {} game: {} positions.", "Prepared from {} games: {} positions.", stats.games,
               stats.games, stats.positions);
}

std::string skippedText(uint64_t skipped) {
    return trn("{} game could not be read: it is left out.", "{} games could not be read: they are left out.", skipped,
               skipped);
}

}  // namespace

void App::pollOwnBooks() {
    // The setup's PGN books, prepared as soon as they are chosen or loaded (a template, the queue).
    for (const auto& need : pgnBookNeeds(setup_)) (void)pgn_books_.want(need.path, need.depth, need.chess960);
    for (const auto& ended : pgn_books_.poll()) {
        const auto name = std::filesystem::path(ended.pgn).filename().string();
        if (ended.status.state == BookState::Failed) {
            Console::instance().add(trf("This book cannot play for an engine: {}", ended.status.error), true);
            continue;
        }
        const auto& stats = ended.status.stats;
        Console::instance().add(fmt::format("{}: {}", name, pgnBookStatsText(stats)));
        if (stats.skipped > 0) Console::instance().add(fmt::format("{}: {}", name, skippedText(stats.skipped)), true);
        if (!stats.stopped.empty()) {
            Console::instance().add(
                fmt::format("{}: {}", name, trf("The reading stopped before the end of the file: {}", stats.stopped)),
                true);
        }
    }
}

const std::string& App::ownBookCheck(const std::string& path) {
    static const std::string none;
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) return none;
    const auto time = std::filesystem::last_write_time(path, ec);
    if (ec) return none;
    auto [it, added] = own_book_checks_.try_emplace(path);
    auto& check      = it->second;
    if (added || check.size != size || check.time != time) check = {size, time, fcg::ownBookProblem(path)};
    return check.problem;
}

bool App::ownBookButton(const char* id, OwnBook& book, float width, bool* again, bool open) {
    bool changed = false;
    ImGui::PushID(id);

    // A PGN book: where its conversion into a Polyglot book stands.
    const bool pgn = !book.empty() && tour::isPgnBook(book.path);
    PgnBookJobs::Status status;
    status.state = BookState::Ready;
    if (pgn) status = pgn_books_.want(book.path, book.depth, setup_.chess960);
    const bool preparing = status.state == BookState::Preparing || status.state == BookState::Queued;

    // A book icon, then the book's name and depth, cut to the button.
    std::string label = book.empty() ? std::string("—") : ownBookName(book);
    if (!book.empty() && book.depth > 0) label += fmt::format(" · {}", book.depth);
    if (preparing) label += fmt::format(" · {} %", percent(status.progress));
    if (ImGui::Button("##own_book", ImVec2(width, 0)) || open) {
        ImGui::OpenPopup("own_book_popup");
    }
    {
        const auto& style   = ImGui::GetStyle();
        const float h       = ImGui::GetFrameHeight();
        const float icon_sz = h * 0.42f;
        const ImVec2 p0     = ImGui::GetItemRectMin();
        const ImVec2 p1     = ImGui::GetItemRectMax();
        auto* dl            = ImGui::GetWindowDrawList();
        const ImU32 col = status.state == BookState::Failed
                              ? ImGui::GetColorU32(ui::WARN)
                              : ImGui::GetColorU32(book.empty() || preparing ? ImGuiCol_TextDisabled : ImGuiCol_Text);
        ui::drawIcon(dl, ui::Icon::Book, ImVec2(p0.x + style.FramePadding.x + icon_sz * 0.5f, p0.y + h * 0.5f), icon_sz,
                     col);
        const float text_x = p0.x + style.FramePadding.x + icon_sz + style.ItemInnerSpacing.x;
        dl->PushClipRect(ImVec2(text_x, p0.y), ImVec2(p1.x - style.FramePadding.x, p1.y), true);
        dl->AddText(ImVec2(text_x, p0.y + style.FramePadding.y), col, label.c_str());
        dl->PopClipRect();
        // The conversion's progress along the bottom, seen even when the label is cut.
        if (preparing) {
            const float y = p1.y - std::max(2.0f, h * 0.08f);
            dl->AddRectFilled(ImVec2(p0.x, y), ImVec2(p0.x + (p1.x - p0.x) * static_cast<float>(status.progress), p1.y),
                              ImGui::GetColorU32(ImGuiCol_PlotHistogram));
        }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        std::string state;
        if (preparing) state = "\n" + trf("Preparing the book… {} %", percent(status.progress));
        if (status.state == BookState::Failed)
            state = "\n" + trf("This book cannot play for an engine: {}", status.error);
        ImGui::SetTooltip("%s", book.empty() ? TR("No book of its own: the engine plays every move. Click to give it "
                                                  "one.")
                                             : fmt::format("{}\n{}{}", ownBookText(book), book.path, state).c_str());
    }

    // Under the button, also when it was opened without a click.
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y), ImGuiCond_Appearing);
    if (ImGui::BeginPopup("own_book_popup")) {
        ImGui::TextDisabled("%s", TR("Its own opening book"));
        if (std::exchange(open_own_book_list_, false)) {
            ImGui::OpenPopup(ImHashStr("##ComboPopup", 0, ImGui::GetID("##own_book_combo")));
        }
        ImGui::SetNextItemWidth(px(260));
        if (ImGui::BeginCombo("##own_book_combo", book.empty() ? TR("None") : ownBookName(book).c_str())) {
            if (ImGui::Selectable(TR("None"), book.empty())) {
                book    = {};
                changed = true;
            }
            for (const auto& b : books_) {
                // Polyglot books give moves, and PGN books too, once turned into Polyglot books; EPD books hold
                // only positions, openings for the tournament.
                const bool polyglot = tour::isPolyglot(b.path);
                const bool moves    = polyglot || tour::isPgnBook(b.path);
                const auto& problem = polyglot ? ownBookCheck(b.path) : std::string();
                const bool can_play = moves && problem.empty();
                ImGui::BeginDisabled(!can_play);
                if (ImGui::Selectable(b.name.c_str(), b.path == book.path) && can_play) {
                    book.path = b.path;
                    book.name = b.name;
                    changed   = true;
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("%s",
                                      !moves ? TR("An EPD book holds positions, not moves: it gives the tournament "
                                                  "its openings, it cannot play for an engine.")
                                      : !problem.empty() ? trf("It cannot play for an engine: {}", problem).c_str()
                                                         : (b.description.empty() ? b.path : b.description).c_str());
                }
            }
            if (std::none_of(books_.begin(), books_.end(),
                             [](const auto& b) { return tour::isPolyglot(b.path) || tour::isPgnBook(b.path); })) {
                ImGui::TextDisabled("%s", TR("No Polyglot (.bin) or PGN book in the library: add one in the Books "
                                             "window."));
            }
            ImGui::EndCombo();
        }
        itemHelp(
            TR("Like Cute Chess' book=: while the position is in this book, the engine plays the book's move at "
               "once, then searches. A PGN book is first turned into a Polyglot book, in the background."));
        ui::topic(guide::topics::OwnBooks);
        // The book chosen before it changed, or one of a template; a PGN book that cannot be prepared.
        const auto warn = [&](const std::string& text) {
            ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + px(260));
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        };
        const auto& problem = book.empty() || pgn ? std::string() : ownBookCheck(book.path);
        if (!problem.empty()) warn(trf("This book cannot play for an engine: {}", problem));
        if (pgn) {
            // The PGN book being turned into a Polyglot book, or what that found. Asked again: the depth may have
            // changed above.
            const auto now = pgn_books_.want(book.path, book.depth, setup_.chess960);
            if (now.state == BookState::Preparing || now.state == BookState::Queued) {
                ImGui::ProgressBar(static_cast<float>(now.progress), ImVec2(px(260), 0),
                                   trf("Preparing the book… {} %", percent(now.progress)).c_str());
            } else if (now.state == BookState::Failed) {
                warn(trf("This book cannot play for an engine: {}", now.error));
                if (ImGui::Button(TR("Try again"))) pgn_books_.retry(book.path, book.depth, setup_.chess960);
            } else {
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + px(260));
                ImGui::TextDisabled("%s", pgnBookStatsText(now.stats).c_str());
                ImGui::PopTextWrapPos();
                if (now.stats.skipped > 0) warn(skippedText(now.stats.skipped));
                if (!now.stats.stopped.empty()) {
                    warn(trf("The reading stopped before the end of the file: {}", now.stats.stopped));
                }
            }
        }

        ImGui::BeginDisabled(book.empty());
        ImGui::SetNextItemWidth(px(260));
        if (ImGui::SliderInt(TR("Depth"), &book.depth, 0, 60, book.depth > 0 ? TR("%d plies") : TR("all"),
                             ImGuiSliderFlags_AlwaysClamp)) {
            changed = true;
        }
        ImGui::EndDisabled();
        // What the depth counts, at the cursor rather than in grey lines under it.
        {
            std::string depth_help = TR("Plies from the start of the game, the tournament's opening included.");
            if (pgn) {
                depth_help += "\n" + trf("A PGN book plays the moves of its games, as often as they play them; "
                                         "\"all\" is their first {} plies.",
                                         tour::PGN_BOOK_ALL_PLIES);
            }
            depth_help += "\n" + std::string(TR("Its book moves are played at once, without a search."));
            itemHelp(depth_help);
        }
        // The setting of every book of the tournament (Openings), here too since it chooses this book's moves.
        ImGui::BeginDisabled(book.empty());
        const auto randomness_label = fmt::format("{}##own_randomness", TR("Randomness"));
        if (randomnessSlider(randomness_label.c_str(), setup_.book_randomness, px(260))) changed = true;
        ImGui::EndDisabled();
        itemHelp(randomnessHelp() + "\n" +
                 TR("The same for every book of the tournament: also in the Openings section."));

        if (again) {
            ImGui::Separator();
            if (ImGui::Button(TR("This engine once more, with another book"))) {
                *again = true;
                ImGui::CloseCurrentPopup();
            }
            itemHelp(
                TR("A second participant with the same engine, to give it another book: each copy plays as "
                   "engine (book), its results and rating apart."));
            ui::topic(guide::topics::OwnBooks);
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

void App::drawSetup(bool two_columns, bool with_summary) {
    if (!ui::beginPanel(TR("Setup###setup"))) {
        ui::endPanel();
        return;
    }

    // How the games are played, one folded row per section; then who plays them.
    ui::beginCard("settings_card");
    drawSetupRows();
    ui::endCard();
    ImGui::Spacing();

    // Who plays, the library beside them on a wide window, under them otherwise. Both lists as tall as the
    // participants need, within reason.
    const bool match = setup_.format == TournamentFormat::Match;
    const float row  = ImGui::GetFrameHeight();
    players_list_h_ =
        std::clamp((static_cast<float>(match ? 0 : setup_.participants.size()) + 1.5f) * row, px(240), px(480));
    const auto players = [&] {
        ui::TopicRegion topic(guide::topics::Participants);
        ui::beginCard("players_card");
        if (match)
            setupEngines();
        else
            setupParticipants();
        ui::endCard();
    };
    const auto library = [&] {
        ui::TopicRegion topic(guide::topics::Participants);
        ui::beginCard("library_card");
        participantPicker();
        ui::endCard();
    };
    if (two_columns && ImGui::BeginTable("##players", 2, ImGuiTableFlags_SizingStretchProp)) {
        // The names need the room: the participants a little wider than the library.
        ImGui::TableSetupColumn("players", ImGuiTableColumnFlags_WidthStretch, 1.15f);
        ImGui::TableSetupColumn("library", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableNextColumn();
        players();
        ImGui::TableNextColumn();
        library();
        ImGui::EndTable();
    } else {
        players();
        ImGui::Spacing();
        library();
    }

    // A narrow window: the summary under the form, in the same scroll.
    if (with_summary) {
        ImGui::Spacing();
        drawSetupSummary();
    }
    ui::endPanel();
}

void App::drawSetupRows() {
    if (std::exchange(open_pgn_setup_, false)) setup_open_[RowPgn] = true;
    // The titles' column as wide as the longest, in this language.
    setup_title_w_ = px(110);
    for (const auto s : SETUP_SECTIONS)
        setup_title_w_ = std::max(setup_title_w_, ImGui::CalcTextSize(sectionTitle(s)).x);
    for (const char* t : {TR("Concurrency"), TR("PGN and misc")})
        setup_title_w_ = std::max(setup_title_w_, ImGui::CalcTextSize(t).x);
    setup_title_w_ += ImGui::GetStyle().ItemSpacing.x * 2;
    // Each row, its details included, tells F1 its section of the guide.
    const auto row = [&](const char* topic, auto&& draw) {
        ui::TopicRegion region(topic);
        draw();
    };
    row(guide::topics::Format, [&] { sectionRow(RowFormat, SetupSection::Format, &App::setupFormat); });
    row(guide::topics::TimeControl, [&] { sectionRow(RowTime, SetupSection::Time, &App::setupLimit); });
    row(guide::topics::Openings, [&] { sectionRow(RowOpenings, SetupSection::Openings, &App::setupOpenings); });
    row(guide::topics::Adjudication,
        [&] { sectionRow(RowAdjudication, SetupSection::Adjudication, &App::setupAdjudication); });
    row(guide::topics::EngineSettings,
        [&] { sectionRow(RowEngines, SetupSection::Engines, &App::setupEngineOptions); });
    // What no profile holds: this tournament's.
    ImGui::Separator();
    row(guide::topics::Concurrency, [&] {
        setupRow(RowConcurrency, TR("Concurrency"), concurrencySummary(),
                 TR("Other programs call it concurrency too: the games played at the same time (-concurrency)."),
                 std::nullopt, &App::setupConcurrency);
    });
    row(guide::topics::PgnMisc,
        [&] { setupRow(RowPgn, TR("PGN and misc"), pgnSummary(), std::string(), std::nullopt, &App::setupPgn); });
    drawProfilePopups();
}

std::string App::concurrencyRule(bool brief) const {
    const bool clock_free = clockFree(setup_.limit);
    if (!setup_.concurrency_auto) {
        return brief ? std::string(TR("set by hand"))
                     : trf("Set by hand; the automatic choice would be {}.", topology_.defaultConcurrency(clock_free));
    }
    if (clock_free) {
        return brief ? std::string(TR("one per hardware thread, for a node count or a depth"))
                     : trf("One game per hardware thread ({} on this machine): a node count or a depth plays the "
                           "same moves however the cores are shared.",
                           topology_.logical);
    }
    return brief ? std::string(TR("physical cores − 1, for a clock"))
                 : trf("Physical cores − 1 ({} physical cores on this machine): with a clock, two engines on the "
                       "same core would slow each other down.",
                       topology_.physical);
}

std::string App::concurrencySummary() const {
    const int games  = setup_.concurrency_auto ? topology_.defaultConcurrency(clockFree(setup_.limit))
                                               : std::clamp(setup_.concurrency, 1, topology_.logical);
    std::string text = trn("{} game at a time", "{} games at a time", static_cast<uint64_t>(games), games) + " · " +
                       concurrencyRule(true);
    // On a remote machine: said first.
    if (!setup_.machine.empty()) {
        const auto m = std::find_if(machines_.begin(), machines_.end(),
                                     [&](const Machine& x) { return x.id == setup_.machine; });
        text = trf("On {}", m != machines_.end() ? (m->name.empty() ? m->host : m->name)
                                                 : std::string(TR("a machine removed"))) +
               " · " + text;
    }
    // Fewer, by the memory and the GPUs of the budget.
    const auto limit = budgetLimit(setup_, engines_, machineBudget(), games);
    if (limit.games < games) text += " · " + trf("{} allowed by the memory", limit.games);
    if (limit.gpu_games < games) text += " · " + trf("{} with a GPU engine at a time", limit.gpu_games);
    if (AFFINITY_SUPPORTED && setup_.affinity) text += " · " + std::string(TR("CPU affinity"));
    return text;
}

std::string App::pgnSummary() const {
    std::vector<std::string> parts;
    if (!setup_.pgn_event.empty()) parts.push_back(trf("event {}", setup_.pgn_event));
    if (!setup_.pgn_out.empty())
        parts.push_back(trf("also to {}", std::filesystem::path(setup_.pgn_out).filename().string()));
    if (setup_.log_every_game) parts.push_back(TR("every game's engine log"));
    if (!setup_.recover) parts.push_back(TR("a crash stops the tournament"));
    if (setup_.check_mate_pvs) parts.push_back(TR("mate PVs checked"));
    std::string text;
    for (const auto& p : parts) text += (text.empty() ? "" : " · ") + p;
    return text;
}

void App::setupRow(SetupRow row, const char* title, const std::string& summary, const std::string& more,
                   std::optional<SetupSection> section, void (App::*details)()) {
    ImGui::PushID(static_cast<int>(row));
    const auto& style     = ImGui::GetStyle();
    const ImVec2 start    = ImGui::GetCursorScreenPos();
    const float width     = ImGui::GetContentRegionAvail().x;
    const float frame     = ImGui::GetFrameHeight();
    const float title_w   = std::min(setup_title_w_, width * 0.3f);
    const float profile_w = std::min(px(190), width * 0.3f);
    const float dot_w     = px(16);
    const float edit_w    = frame;
    bool& open            = setup_open_[row];

    // The title, then the profile (or the scope, for the rows without one).
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(title);
    ImGui::SetCursorScreenPos(ImVec2(start.x + title_w, start.y));
    const auto state = section ? profileState(setup_, *section, profiles_) : ProfileState::None;
    if (section) {
        profileCombo(*section, profile_w);
    } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", TR("this tournament"));
        itemHelp(TR("No profile holds this: it is saved with the setup and in templates."));
    }

    // A dot when the section was changed for this tournament only.
    if (state == ProfileState::Changed || state == ProfileState::Missing) {
        ImGui::SetCursorScreenPos(ImVec2(start.x + title_w + profile_w, start.y));
        ImGui::InvisibleButton("##changed", ImVec2(dot_w, frame));
        const ImVec2 c((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f,
                       (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f);
        ImGui::GetWindowDrawList()->AddCircleFilled(c, px(3.5f),
                                                    state == ProfileState::Changed ? ui::ACCENT : ui::WARN);
        itemHelp(state == ProfileState::Changed
                     ? TR("Changed for this tournament only. Its list, or ✎, saves it as a profile, updates the "
                          "profile or reverts to it.")
                     : trf("The profile {} is not there any more: these are the values it had. Save them as a profile "
                           "to use them again.",
                           sectionProfile(setup_, *section)));
    }

    // The summary, which opens the details as ✎ does.
    const float summary_x = start.x + title_w + profile_w + dot_w;
    const float summary_w = std::max(px(40), start.x + width - edit_w - style.ItemSpacing.x - summary_x);
    ImGui::SetCursorScreenPos(ImVec2(summary_x, start.y));
    if (ImGui::Selectable("##summary", false, ImGuiSelectableFlags_None, ImVec2(summary_w, frame))) open = !open;
    {
        const ImVec2 p0 = ImGui::GetItemRectMin(), p1 = ImGui::GetItemRectMax();
        auto* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(p0, ImVec2(p1.x - style.FramePadding.x, p1.y), true);
        dl->AddText(ImVec2(p0.x + style.FramePadding.x * 0.5f, start.y + style.FramePadding.y),
                    ImGui::GetColorU32(open ? ImGuiCol_TextDisabled : ImGuiCol_Text), summary.c_str());
        dl->PopClipRect();
    }
    itemHelp(more.empty() ? summary : summary + "\n\n" + more);

    // ✎, at the right end.
    ImGui::SetCursorScreenPos(ImVec2(start.x + width - edit_w, start.y));
    if (open) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::Button("##edit", ImVec2(edit_w, frame))) open = !open;
    ui::drawIcon(ImGui::GetWindowDrawList(), ui::Icon::Pencil,
                 ImVec2((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f,
                        (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f),
                 frame * 0.45f, ImGui::GetColorU32(ImGuiCol_Text));
    if (open) ImGui::PopStyleColor();
    itemHelp(open ? TR("Fold the details") : TR("Edit in place"));

    // The details, under the row, on a background of their own.
    if (open) {
        auto* dl         = ImGui::GetWindowDrawList();
        const ImVec2 top = ImGui::GetCursorScreenPos();
        if (setup_details_h_[row] > 0) {
            dl->AddRectFilled(ImVec2(start.x, top.y - style.ItemSpacing.y * 0.5f),
                              ImVec2(start.x + width, top.y + setup_details_h_[row]),
                              ImGui::GetColorU32(ImGuiCol_FrameBg, 0.45f), style.FrameRounding);
        }
        ImGui::Indent(px(10));
        ImGui::BeginGroup();
        (this->*details)();
        if (section) profileActions(*section);
        ImGui::EndGroup();
        ImGui::Unindent(px(10));
        setup_details_h_[row] = ImGui::GetItemRectMax().y - top.y + style.ItemSpacing.y * 0.5f;
        ImGui::Spacing();
    }
    ImGui::PopID();
}

std::optional<App::SetupRow> App::setupRowOf(const std::string& key) {
    static constexpr std::array<const char*, ROW_COUNT> keys{"format",  "time",        "openings", "adjudication",
                                                             "engines", "concurrency", "pgn"};
    for (int i = 0; i < ROW_COUNT; ++i) {
        if (key == keys[i]) return static_cast<SetupRow>(i);
    }
    return std::nullopt;
}

void App::sectionRow(SetupRow row, SetupSection section, void (App::*details)()) {
    const auto line = sectionCommandLine(setup_, section);
    setupRow(row, sectionTitle(section), sectionSummary(setup_, section, books_),
             line.empty() ? std::string() : trf("As cutechess and fastchess write it: {}", line), section, details);
}

void App::applySetupProfile(SetupSection section, const SetupProfile& profile) {
    applyProfile(setup_, section, profile);
    saveSetupSoon();
}

void App::saveSetupProfile(SetupSection section, const std::string& name) {
    profiles_.save(section, name, setup_);
    setup_.profiles[sectionKey(section)] = name;
    saveProfiles();
    saveSetupSoon();
}

void App::profileCombo(SetupSection section, float width) {
    const auto name     = sectionProfile(setup_, section);
    const auto* current = profiles_.find(section, name);
    const auto state    = profileState(setup_, section, profiles_);
    const auto preview  = current ? current->label() : name.empty() ? std::string(TR("No profile")) : name;

    // Opened for a screenshot: the popup of the combo, as BeginCombo names it. The rows of the sections come first, in
    // their order.
    if (open_profile_list_ == static_cast<int>(section)) {
        open_profile_list_ = -1;
        ImGui::OpenPopup(ImHashStr("##ComboPopup", 0, ImGui::GetID("##profile")));
    }
    ImGui::SetNextItemWidth(width);
    if (state == ProfileState::None || state == ProfileState::Missing)
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    const bool opened = ImGui::BeginCombo("##profile", preview.c_str(), ImGuiComboFlags_HeightLarge);
    if (state == ProfileState::None || state == ProfileState::Missing) ImGui::PopStyleColor();
    if (!opened) {
        itemHelp(state == ProfileState::None ? TR("No profile: these settings are this tournament's. Choose a profile, "
                                                  "or save these as one.")
                                             : TR("The profile these settings come from: choose another one, or save, "
                                                  "update or revert them."));
        return;
    }
    const auto& list = profiles_.list(section);
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto& p = list[i];
        ImGui::PushID(static_cast<int>(i));
        // The user's profiles, then the program's presets, marked.
        if (i > 0 && p.preset && !list[i - 1].preset) ImGui::Separator();
        if (ImGui::Selectable(p.label().c_str(), p.name == name)) applySetupProfile(section, p);
        if (p.name == name && ImGui::IsWindowAppearing()) ImGui::SetItemDefaultFocus();
        itemHelp(sectionSummary(p.setup, section, books_) +
                 (p.preset ? std::string("\n") + TR("A preset of the program.") : std::string()));
        if (p.preset) {
            ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(TR("preset")).x);
            ImGui::TextDisabled("%s", TR("preset"));
        }
        ImGui::PopID();
    }
    ImGui::Separator();
    if (ImGui::Selectable(TR("Save as profile…"))) {
        profile_section_   = section;
        profile_name_      = state == ProfileState::Missing ? name : std::string();
        open_save_profile_ = true;
    }
    if (current && state == ProfileState::Changed) {
        if (ImGui::Selectable(trf("Update {}", current->label()).c_str())) saveSetupProfile(section, name);
        itemHelp(trf("{} takes these settings, for every tournament and template that uses it.", current->label()));
        if (ImGui::Selectable(trf("Revert to {}", current->label()).c_str())) applySetupProfile(section, *current);
    }
    if (current && profiles_.isUsers(section, name)) {
        if (ImGui::Selectable(trf("Delete {}…", current->label()).c_str())) {
            profile_section_ = section;
            profile_delete_  = name;
        }
    }
    ImGui::EndCombo();
}

void App::profileActions(SetupSection section) {
    const auto name     = sectionProfile(setup_, section);
    const auto* current = profiles_.find(section, name);
    const auto state    = profileState(setup_, section, profiles_);
    ImGui::Spacing();
    if (state == ProfileState::Changed && current) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::ACCENT);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("•");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextUnformatted(TR("Changed for this tournament only."));
        ImGui::SameLine();
    }
    if (ImGui::Button(TR("Save as profile…"))) {
        profile_section_   = section;
        profile_name_      = state == ProfileState::Missing ? name : std::string();
        open_save_profile_ = true;
    }
    itemHelp(TR("These settings under a name, to choose them again in other tournaments and templates."));
    if (state == ProfileState::Changed && current) {
        ImGui::SameLine();
        if (ImGui::Button(trf("Update {}", current->label()).c_str())) saveSetupProfile(section, name);
        itemHelp(trf("{} takes these settings, for every tournament and template that uses it.", current->label()));
        ImGui::SameLine();
        if (ImGui::Button(TR("Revert"))) applySetupProfile(section, *current);
        itemHelp(trf("Back to the settings of {}.", current->label()));
    }
}

void App::drawProfilePopups() {
    if (std::exchange(open_save_profile_, false)) ImGui::OpenPopup("save_profile");
    if (ImGui::BeginPopup("save_profile")) {
        const auto section = profile_section_;
        ImGui::TextDisabled("%s", trf("{}: save as profile", sectionTitle(section)).c_str());
        char buffer[128];
        const auto n = std::min(profile_name_.size(), sizeof(buffer) - 1);
        std::copy_n(profile_name_.data(), n, buffer);
        buffer[n] = 0;
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(px(260));
        const bool enter = ImGui::InputTextWithHint("##profile_name", TR("Profile name"), buffer, sizeof(buffer),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        profile_name_    = buffer;
        // A name is trimmed of its spaces; one already taken is replaced, once said.
        auto name = profile_name_;
        name.erase(0, name.find_first_not_of(' '));
        name.erase(name.find_last_not_of(' ') + 1);
        const auto* taken = profiles_.find(section, name);
        if (taken) {
            ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
            ImGui::TextUnformatted(taken->preset ? TR("A preset of this name: your profile takes its place.")
                                                 : TR("A profile of this name: it is replaced."));
            ImGui::PopStyleColor();
        }
        ImGui::BeginDisabled(name.empty());
        if (ImGui::Button(taken ? TR("Replace") : TR("Save")) || (enter && !name.empty())) {
            saveSetupProfile(section, name);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(TR("Cancel"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (!profile_delete_.empty() && !ImGui::IsPopupOpen("delete_profile")) ImGui::OpenPopup("delete_profile");
    if (ImGui::BeginPopup("delete_profile")) {
        const auto section = profile_section_;
        ImGui::TextUnformatted(trf("Delete the profile {}?", profile_delete_).c_str());
        ImGui::TextDisabled("%s", TR("The setup and the templates that use it keep its values."));
        if (ImGui::Button(TR("Delete"))) {
            profiles_.remove(section, profile_delete_);
            saveProfiles();
            profile_delete_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(TR("Cancel"))) {
            profile_delete_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else {
        profile_delete_.clear();  // closed by a click elsewhere or Escape
    }
}

void App::setupEngines() {
    ImGui::TextDisabled("%s", TR("Engines"));

    // Each with its book, if it has one of its own: the same engine may then play both sides.
    const float swap_w  = ImGui::GetFrameHeight() * 1.6f;
    const float book_w  = px(150);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    // An engine of the library dropped on A or B takes its place.
    const auto dropped = [&](std::string& slot) {
        if (!ImGui::BeginDragDropTarget()) return;
        if (ImGui::AcceptDragDropPayload("FCG_LIBRARY") && !library_drag_.empty()) {
            slot = library_drag_.front();
            saveSetupSoon();
        }
        ImGui::EndDragDropTarget();
    };
    ui::label(TR("A (tested)"), labelWidth() * 0.75f);
    itemHelp(TR("Statistics are given from A's point of view."));
    if (engineCombo("##engine_a", setup_.engine_a, engines_,
                    ImGui::GetContentRegionAvail().x - book_w - swap_w - 2 * spacing))
        saveSetupSoon();
    dropped(setup_.engine_a);
    ImGui::SameLine();
    if (ownBookButton("book_a", setup_.book_a, book_w, nullptr, std::exchange(open_own_book_, -1) == 0))
        saveSetupSoon();

    ui::label(TR("B (baseline)"), labelWidth() * 0.75f);
    if (engineCombo("##engine_b", setup_.engine_b, engines_,
                    ImGui::GetContentRegionAvail().x - book_w - swap_w - 2 * spacing))
        saveSetupSoon();
    dropped(setup_.engine_b);
    ImGui::SameLine();
    if (ownBookButton("book_b", setup_.book_b, book_w)) saveSetupSoon();
    ImGui::SameLine();
    if (ui::iconButton("swap", ui::Icon::Swap, "")) {
        std::swap(setup_.engine_a, setup_.engine_b);
        std::swap(setup_.book_a, setup_.book_b);
        saveSetupSoon();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("Swap A and B"));

    // An engine chosen with too many threads (its own default perhaps), flagged here too.
    std::vector<Notice> threads;
    for (std::size_t i = 0; i < 2; ++i) {
        if (auto n = threadNotice(setup_, i, engines_, topology_)) threads.push_back(std::move(*n));
    }
    if (threads.size() == 2 && threads[0].text == threads[1].text) threads.pop_back();
    drawSetupNotices(threads, "engine_threads");
}

void App::setupEngineOptions() {
    bool changed = false;
    changed |= inputInt(TR("Hash (MB)"), "##common_hash", setup_.common_hash, 0, 1 << 20);
    itemHelp(TR("Common settings, forced on every engine (0 = each engine's own):") + std::string("\n") +
             TR("A participant's own, from its ⋯ menu, comes first."));
    changed |= inputInt(TR("Threads"), "##common_threads", setup_.common_threads, 0, 1024);
    itemHelp(TR("Common settings, forced on every engine (0 = each engine's own):") + std::string("\n") +
             TR("A participant's own, from its ⋯ menu, comes first."));

    changed |= ImGui::Checkbox(TR("Syzygy tablebases for the engines"), &setup_.engine_syzygy);
    ui::help(
        TR("The engines that have a SyzygyPath option get these directories, those of the adjudication: "
           "otherwise each engine keeps its own setting."));
    if (setup_.engine_syzygy) {
        ImGui::Indent();
        changed |= tablebaseDirs("##engine_tb_paths", setup_.tb_paths, dialogs_);
        ImGui::Unindent();
    }
    changed |= ImGui::Checkbox(TR("Engines' own books off"), &setup_.engine_ownbook_off);
    ui::help(
        TR("OwnBook is set to false on the engines that have it, so that every game is played from the "
           "openings of the tournament."));

    if (changed) saveSetupSoon();
}

void App::setupMatch() {
    int mode     = static_cast<int>(setup_.mode);
    bool changed = false;
    changed |= ImGui::RadioButton(TR("Fixed games"), &mode, 0);
    ImGui::SameLine();
    changed |= ImGui::RadioButton(TR("Infinite"), &mode, 1);
    itemHelp(TR("Plays until you stop the match."));
    ImGui::SameLine();
    changed |= ImGui::RadioButton("SPRT", &mode, 2);
    itemHelp(
        TR("Sequential probability ratio test, as fishtest and OpenBench run it: plays until it can tell, with "
           "the error rates alpha and beta, whether A is worth elo1 more than B (H1 accepted) or only elo0."));
    setup_.mode = static_cast<MatchMode>(mode);

    // Who has White: A and B in turn, or always the same one (odds games, an opening tried from one side).
    ui::label(TR("Colours"), labelWidth());
    ImGui::SetNextItemWidth(px(170));
    int colours                  = static_cast<int>(setup_.colours);
    const char* colour_choices[] = {coloursText(tour::Colours::Alternate), coloursText(tour::Colours::AWhite),
                                    coloursText(tour::Colours::BWhite)};
    if (ImGui::Combo("##colours", &colours, colour_choices, 3)) {
        setup_.colours = static_cast<tour::Colours>(colours);
        changed        = true;
    }
    itemHelp(
        TR("Alternate: each opening is played twice, colours reversed, which cancels out what the opening gives "
           "a side. Always White: for odds games, or an opening tried from one side; each game then has an opening "
           "of its own, and the score includes White's advantage."));
    const bool locked = setup_.colours != tour::Colours::Alternate;

    switch (setup_.mode) {
        case MatchMode::Fixed:
            changed |= inputInt(TR("Games"), "##games", setup_.fixed_games, 2, 100'000'000);
            ui::help(locked ? TR("Played in pairs: two games with the same colours, each from an opening of its own.")
                            : TR("Played in pairs: each opening is played twice, with colours reversed."));
            break;
        case MatchMode::Infinite:
            break;
        case MatchMode::Sprt: {
            changed |= inputDouble("elo0", "##elo0", setup_.sprt_elo0, -1000, 1000);
            changed |= inputDouble("elo1", "##elo1", setup_.sprt_elo1, -1000, 1000);
            if (setup_.sprt_elo1 <= setup_.sprt_elo0) {  // Start waits until they are right
                ui::label("", labelWidth());
                ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
                ImGui::TextUnformatted(TR("elo1 must be larger than elo0."));
                ImGui::PopStyleColor();
            }
            changed |= inputDouble("alpha", "##alpha", setup_.sprt_alpha, 0.001, 0.5, "%.3f");
            changed |= inputDouble("beta", "##beta", setup_.sprt_beta, 0.001, 0.5, "%.3f");

            ui::label(TR("Model"), labelWidth());
            ImGui::SetNextItemWidth(140.0f * ImGui::GetFontSize() / 16.0f);
            const char* models[] = {"normalized", "logistic", "bayesian"};
            int model            = 0;
            for (int i = 0; i < 3; ++i) {
                if (setup_.sprt_model == models[i]) model = i;
            }
            if (ImGui::Combo("##model", &model, models, 3)) {
                setup_.sprt_model = models[model];
                changed           = true;
            }
            ui::help(
                TR("normalized: bounds in nElo (fishtest, OpenBench). logistic: classic Elo. bayesian: BayesElo, "
                   "without pentanomial."));

            changed |= inputInt(TR("Max games"), "##sprt_max", setup_.sprt_max_games, 0, 100'000'000);
            ui::help(TR("0 = no limit."));
            changed |= inputDouble(TR("Elo bias"), "##sprt_bias", setup_.sprt_bias, -1000, 1000);
            itemHelp(
                TR("Added to elo0 and elo1, for a test against a version known to be stronger or weaker by that "
                   "much: the SPRT is given [elo0 + bias, elo1 + bias]."));
            changed |= inputInt(TR("Min games"), "##sprt_min", setup_.sprt_min_games, 0, 100'000'000);
            itemHelp(
                TR("No decision before this number of games, the LLR still shown: it guards a short test "
                   "against an early lucky stop. 0 = none."));

            // What these bounds are likely to cost, from the calculator's guesses (the bias on them). The usual
            // bounds are profiles (SPRT gainer, non-regression).
            const auto given = tour::biasedBounds(setup_.sprt_elo0, setup_.sprt_elo1, setup_.sprt_bias);
            const auto plan  = sprtPlan(setup_.sprt_alpha, setup_.sprt_beta, given.elo0, given.elo1, setup_.sprt_model,
                                        setup_.sprt_draw_ratio, setup_.sprt_rms_bias);
            if (plan.valid) {
                ImGui::PushTextWrapPos(0);
                ImGui::TextDisabled("%s", trf("About {} games if the change is worth elo0, {} if it is worth elo1.",
                                              ui::formatCount(static_cast<uint64_t>(plan.at(plan.elo0).games)),
                                              ui::formatCount(static_cast<uint64_t>(plan.at(plan.elo1).games)))
                                              .c_str());
                ImGui::PopTextWrapPos();
            }
            if (ImGui::SmallButton(TR("Calculator…"))) {
                sprt_calc_      = setup_;
                sprt_calc_open_ = true;
                ImGui::SetWindowFocus(TR("SPRT calculator###sprt_calc"));
            }
            itemHelp(
                TR("As fishtest's SPRT calculator: the probability that H1 is accepted and the expected number of "
                   "games against the true Elo difference, from a draw ratio and the openings' RMS bias."));
            break;
        }
    }

    if (changed) saveSetupSoon();
}

void App::drawSprtCalculator() {
    if (!sprt_calc_open_) return;
    ImGui::SetNextWindowSize(ImVec2(px(860), px(560)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin(TR("SPRT calculator###sprt_calc"), &sprt_calc_open_, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }
    auto& c = sprt_calc_;

    // The bounds and the guesses on the left, the curves on the right.
    ImGui::BeginChild("##sprt_inputs", ImVec2(px(300), 0));
    inputDouble("elo0", "##calc_elo0", c.sprt_elo0, -1000, 1000);
    inputDouble("elo1", "##calc_elo1", c.sprt_elo1, -1000, 1000);
    inputDouble(TR("Elo bias"), "##calc_sprt_bias", c.sprt_bias, -1000, 1000);
    itemHelp(
        TR("Added to elo0 and elo1, for a test against a version known to be stronger or weaker by that much: "
           "the SPRT is given [elo0 + bias, elo1 + bias]."));
    inputDouble("alpha", "##calc_alpha", c.sprt_alpha, 0.001, 0.5, "%.3f");
    inputDouble("beta", "##calc_beta", c.sprt_beta, 0.001, 0.5, "%.3f");
    ui::label(TR("Model"), labelWidth());
    ImGui::SetNextItemWidth(px(110));
    const char* models[] = {"normalized", "logistic", "bayesian"};
    int model            = 0;
    for (int i = 0; i < 3; ++i) {
        if (c.sprt_model == models[i]) model = i;
    }
    if (ImGui::Combo("##calc_model", &model, models, 3)) c.sprt_model = models[model];
    ImGui::Spacing();
    inputDouble(TR("Draw ratio"), "##calc_draws", c.sprt_draw_ratio, 0.01, 0.99, "%.2f");
    ui::help(TR("The share of drawn games expected between the two engines, at this time control."));
    ImGui::BeginDisabled(c.sprt_model == "bayesian");
    inputDouble(TR("RMS bias (Elo)"), "##calc_bias", c.sprt_rms_bias, 0, 1000, "%.0f");
    ImGui::EndDisabled();
    ui::help(
        TR("How unbalanced the openings are: the spread of the advantage they give, in Elo. Pairs of games on the "
           "same opening cancel it out, so a larger bias makes the test shorter. fishtest's book: about 191."));

    const auto given = tour::biasedBounds(c.sprt_elo0, c.sprt_elo1, c.sprt_bias);
    const auto plan =
        sprtPlan(c.sprt_alpha, c.sprt_beta, given.elo0, given.elo1, c.sprt_model, c.sprt_draw_ratio, c.sprt_rms_bias);
    ImGui::Spacing();
    if (!plan.valid) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                           c.sprt_elo1 <= c.sprt_elo0 ? TR("elo1 must be larger than elo0.")
                                                      : TR("This draw ratio and this RMS bias are not compatible."));
        ImGui::PopTextWrapPos();
    } else {
        for (const auto& [name, elo] : {std::pair{"elo0", plan.elo0}, std::pair{"elo1", plan.elo1}}) {
            const auto o = plan.at(elo);
            ImGui::TextUnformatted(name);
            if (c.sprt_model != "logistic") {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", trf("= {:.2f} logistic Elo", std::abs(elo) < 0.005 ? 0.0 : elo).c_str());
            }
            ImGui::BulletText("%s", trf("accepted: {:.1f} %", 100 * o.pass).c_str());
            ImGui::BulletText("%s", trn("about {} game", "about {} games", static_cast<uint64_t>(o.games),
                                        ui::formatCount(static_cast<uint64_t>(o.games)))
                                        .c_str());
        }
    }
    ImGui::Spacing();
    if (ImGui::Button(TR("Use these bounds"))) {
        setup_.mode            = MatchMode::Sprt;
        setup_.sprt_elo0       = c.sprt_elo0;
        setup_.sprt_elo1       = c.sprt_elo1;
        setup_.sprt_bias       = c.sprt_bias;
        setup_.sprt_alpha      = c.sprt_alpha;
        setup_.sprt_beta       = c.sprt_beta;
        setup_.sprt_model      = c.sprt_model;
        setup_.sprt_draw_ratio = c.sprt_draw_ratio;
        setup_.sprt_rms_bias   = c.sprt_rms_bias;
        saveSetupSoon();
    }
    itemHelp(TR("Makes the match an SPRT with these values: the bounds, the bias, alpha, beta and the model."));
    ui::topic(guide::topics::Format);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##sprt_curves");
    if (plan.valid) {
        // fishtest's range: a third of the bounds' width on each side.
        const double width = plan.elo1 - plan.elo0;
        const double from = std::floor(plan.elo0 - width / 3), to = std::ceil(plan.elo1 + width / 3);
        const int steps = 200;
        std::vector<double> xs, pass, games;
        for (int i = 0; i <= steps; ++i) {
            const double elo = from + (to - from) * i / steps;
            const auto o     = plan.at(elo);
            xs.push_back(elo);
            pass.push_back(100 * o.pass);
            games.push_back(o.games);
        }
        const double bounds[2] = {plan.elo0, plan.elo1};
        const float height     = (ImGui::GetContentRegionAvail().y - ImGui::GetStyle().ItemSpacing.y) / 2;
        const auto curve = [&](const char* title, const char* y_label, const std::vector<double>& ys, double y_max) {
            if (!ImPlot::BeginPlot(title, ImVec2(-1, height), ImPlotFlags_NoLegend | ImPlotFlags_NoMenus)) return;
            ImPlot::SetupAxes(TR("true difference (Elo)"), y_label);
            ImPlot::SetupAxisLimits(ImAxis_X1, from, to, ImPlotCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0, y_max, ImPlotCond_Always);
            ImPlotSpec marks;
            marks.LineColor = ImVec4(0.95f, 0.75f, 0.3f, 0.8f);
            ImPlot::PlotInfLines("##bounds", bounds, 2, marks);
            ImPlotSpec line;
            line.LineColor  = ImGui::ColorConvertU32ToFloat4(ui::ACCENT);
            line.LineWeight = 2.0f;
            ImPlot::PlotLine("##curve", xs.data(), ys.data(), static_cast<int>(xs.size()), line);
            ImPlot::EndPlot();
        };
        curve(TR("Probability that H1 is accepted (%)"), "%", pass, 100);
        curve(TR("Expected number of games"), TR("games"), games, *std::max_element(games.begin(), games.end()) * 1.1);
    }
    ImGui::EndChild();
    ImGui::End();
}

void App::setupLimit() {
    int limit    = static_cast<int>(setup_.limit);
    bool changed = false;
    changed |= ImGui::RadioButton(TR("Time"), &limit, static_cast<int>(LimitKind::Time));
    itemHelp(
        TR("A clock: a base time and an increment, the base given again every N moves if you like (tc=40/900, "
           "tc=10+0.1)."));
    ImGui::SameLine();
    changed |= ImGui::RadioButton(TR("Time per move"), &limit, static_cast<int>(LimitKind::MoveTime));
    itemHelp(TR("Every move gets this time, whatever was used before (go movetime)."));
    ImGui::SameLine();
    changed |= ImGui::RadioButton(TR("Fixed nodes"), &limit, static_cast<int>(LimitKind::Nodes));
    itemHelp(
        TR("Every move searches this many nodes: the same moves however fast the machine, and as many games at "
           "once as it has hardware threads."));
    ImGui::SameLine();
    changed |= ImGui::RadioButton(TR("Depth"), &limit, static_cast<int>(LimitKind::Depth));
    itemHelp(TR("Every move is searched to this depth (go depth), however long it takes: no time loss."));
    setup_.limit = static_cast<LimitKind>(limit);

    const auto margin = [&] {
        changed |= inputInt(TR("Margin (ms)"), "##tc_margin", setup_.tc_margin_ms, 0, 100000);
        ui::help(TR("Overrun tolerated before a game is lost on time."));
    };
    switch (setup_.limit) {
        case LimitKind::Time:
            changed |= inputDouble(TR("Base (s)"), "##tc_base", setup_.tc_base_s, 0, 36000, "%.2f");
            changed |= inputDouble(TR("Increment (s)"), "##tc_inc", setup_.tc_inc_s, 0, 3600, "%.3f");
            changed |= inputInt(TR("Moves / period"), "##tc_moves", setup_.tc_moves, 0, 1000);
            ui::help(TR("0 = the base time covers the whole game. Otherwise it is given again every N moves."));
            margin();
            break;
        case LimitKind::MoveTime:
            changed |= inputDouble(TR("Seconds per move"), "##move_s", setup_.move_s, 0.001, 3600, "%.3f");
            ui::help(TR("Every move gets this time, whatever was used before (go movetime)."));
            margin();
            break;
        case LimitKind::Nodes: {
            int64_t nodes = static_cast<int64_t>(setup_.nodes);
            ui::label(TR("Nodes per move"), labelWidth());
            ImGui::SetNextItemWidth(140.0f * ImGui::GetFontSize() / 16.0f);
            if (ui::inputInt64("##nodes", &nodes)) {
                setup_.nodes = static_cast<uint64_t>(std::max<int64_t>(1, nodes));
                changed      = true;
            }
            itemHelp(
                TR("Every move searches this many nodes: the same moves however fast the machine, and as many games "
                   "at once as it has hardware threads."));
            break;
        }
        case LimitKind::Depth:
            changed |= inputInt(TR("Plies"), "##depth", setup_.depth, 1, 200);
            ui::help(TR("Every move is searched to this depth (go depth), however long it takes: no time loss."));
            break;
    }

    if (changed) saveSetupSoon();
}

void App::setupConcurrency() {
    // Where it plays: this computer, or a remote machine (view_machine_runs.cpp).
    drawRunOn(labelWidth());

    bool changed           = false;
    const bool fixed_nodes = clockFree(setup_.limit);
    const int auto_value   = topology_.defaultConcurrency(fixed_nodes);

    changed |= ImGui::Checkbox(TR("Automatic"), &setup_.concurrency_auto);
    ui::help(
        TR("Fixed nodes or depth: one game per hardware thread (Hyper-Threading does not change the moves played). "
           "With a clock: physical cores − 1, so the engines do not slow each other down."));

    int value = setup_.concurrency_auto ? auto_value : setup_.concurrency;
    ui::label(TR("Concurrent games"), labelWidth());
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::BeginDisabled(setup_.concurrency_auto);
    if (ImGui::SliderInt("##concurrency", &value, 1, topology_.logical)) {
        setup_.concurrency = value;
        changed            = true;
    }
    ImGui::EndDisabled();
    itemHelp(TR("Other programs call it concurrency too: the games played at the same time (-concurrency)."));
    if (!setup_.concurrency_auto) setup_.concurrency = std::clamp(setup_.concurrency, 1, topology_.logical);
    // The rule that chose the number, and the machine it measured: without it a number above the cores looks wrong.
    ui::label("", labelWidth());
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s", concurrencyRule(false).c_str());
    ImGui::PopTextWrapPos();
    itemHelp(trf("Machine: {} physical cores, {} threads", topology_.physical, topology_.logical));

    // macOS cannot pin a process to a processor.
    if constexpr (AFFINITY_SUPPORTED) {
        changed |= ImGui::Checkbox(TR("CPU affinity"), &setup_.affinity);
        ui::help(
            TR("Pins every game to its own processor, shared by its two engines since they never think at the "
               "same time: 2 games keep 2 cores busy and leave the others idle. Useful with a clock, irrelevant to "
               "a node count."));
    }

    // Warn about oversubscription, which distorts timed games. The summary names the engine, with Lower Threads.
    int threads = 1;
    for (const auto& p : fcg::setupParticipants(setup_)) {
        for (const auto& e : engines_) {
            if (e.name == p.name) threads = std::max(threads, participantThreads(setup_, p, e).threads);
        }
    }

    if (AFFINITY_SUPPORTED && setup_.affinity && threads > 1) {
        ImGui::TextColored(ui::MUTED4, TR("Affinity ignored: an engine uses %d threads."), threads);
    }
    if (value * threads > topology_.logical) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.3f, 1), TR("%d games × %d threads > %d available threads."), value,
                           threads, topology_.logical);
        ImGui::PopTextWrapPos();
    } else if (!fixed_nodes && value * threads > topology_.physical) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.3f, 1),
                           TR("With a clock, %d games × %d threads exceed the %d physical cores."), value, threads,
                           topology_.physical);
        ImGui::PopTextWrapPos();
    }

    if (changed) saveSetupSoon();
}

void App::setupOpenings() {
    bool changed       = false;
    const float browse = ImGui::CalcTextSize("…").x + ImGui::GetStyle().FramePadding.x * 4;

    // A book of the library, or any file.
    if (!books_.empty()) {
        const auto current =
            std::find_if(books_.begin(), books_.end(), [&](const auto& b) { return b.path == setup_.book; });
        ImGui::SetNextItemWidth(-FLT_MIN);
        const auto preview = current != books_.end() ? current->name
                             : setup_.book.empty()   ? std::string(TR("No book"))
                                                     : std::string(TR("A file of your choice"));
        if (ImGui::BeginCombo("##book_library", preview.c_str())) {
            if (ImGui::Selectable(TR("No book"), setup_.book.empty())) {
                setup_.book.clear();
                changed = true;
            }
            for (const auto& b : books_) {
                const auto label =
                    b.positions > 0 ? trf("{}  ({} openings)", b.name, ui::formatCount(b.positions)) : b.name;
                if (ImGui::Selectable(label.c_str(), b.path == setup_.book)) {
                    setup_.book       = b.path;
                    setup_.book_plies = b.plies;
                    if (b.chess960) setup_.chess960 = true;
                    changed = true;
                }
                if (ImGui::IsItemHovered() && !b.description.empty()) ImGui::SetTooltip("%s", b.description.c_str());
            }
            ImGui::EndCombo();
        }
    }

    // Without a book, the field says what is played instead.
    const char* without = setup_.chess960 ? TR("Without a book, the 960 starting positions, in random order.")
                                          : TR("Without a book, every game starts from the initial position.");
    changed |= inputText("##book", setup_.book,
                         ImGui::GetContentRegionAvail().x - browse - ImGui::GetStyle().ItemSpacing.x, without);
    itemHelp(setup_.book.empty() ? std::string(TR("An opening book, or opening suite: EPD, PGN or Polyglot (.bin)."))
                                 : setup_.book);
    ImGui::SameLine();
    if (ImGui::Button("…##book_browse")) {
        dialogs_.openFile(Dialogs::Purpose::Book,
                          {{TR("Books (EPD, PGN, Polyglot)"), "epd;pgn;bin"}, {TR("All files"), "*"}}, setup_.book);
    }

    changed |= ImGui::Checkbox(TR("Chess960"), &setup_.chess960);
    ui::help(
        TR("Fischer random chess. The engines must play it (the UCI_Chess960 option, or the fischerandom "
           "variant of a WinBoard engine). A book of Chess960 positions, or none for all 960."));

    const bool polyglot = tour::isPolyglot(setup_.book);
    changed |= ImGui::Checkbox(TR("Random order"), &setup_.book_random);
    itemHelp(polyglot ? TR("The positions of the Polyglot book in the order they are drawn, each once, the likelier "
                           "lines (by the weights of their moves, as the randomness makes them) the sooner; or in the "
                           "order of the book, from Start at.")
                      : TR("The openings in a random order (order=random), or one after the other from Start at "
                           "(order=sequential)."));
    if (polyglot) {
        changed |= inputInt(TR("Depth (plies)"), "##plies", setup_.book_plies, -1, 40);
        ui::help(trf("A Polyglot book is followed from the initial position to this depth (-1 = {}), leaving out the "
                     "moves weighing less than a tenth of the best one; the positions reached are the openings.",
                     POLYGLOT_PLIES)
                     .c_str());
    }
    // The participants' own books choose their moves with it too: shown for them, the book being of another kind.
    const auto people   = fcg::setupParticipants(setup_);
    const bool own_book = std::any_of(people.begin(), people.end(), [](const auto& p) { return !p.book.empty(); });
    if (polyglot || own_book) {
        ui::label(TR("Randomness"), labelWidth());
        changed |= randomnessSlider("##book_randomness", setup_.book_randomness, px(260));
        itemHelp(randomnessHelp());
    }
    if (!polyglot) {
        changed |= inputInt(TR("Plies (PGN)"), "##plies", setup_.book_plies, -1, 1000);
        ui::help(TR("For a PGN book: number of plies kept. -1 = all."));
    }
    changed |= inputInt(TR("Start at"), "##book_start", setup_.book_start, 1, 100'000'000);
    if (setup_.format == TournamentFormat::RoundRobin || setup_.format == TournamentFormat::Gauntlet ||
        setup_.format == TournamentFormat::Swiss || setup_.format == TournamentFormat::TeamMatch) {
        changed |= ImGui::Checkbox(TR("The same opening for every pair of a round"), &setup_.opening_per_round);
        ui::help(
            TR("Every game pair of a round plays the same opening, the next of the book for each round: no engine "
               "gets easier openings than the others of its round. Otherwise each pair takes the next one."));
    }

    // The openings that decide the game by themselves, seen as the games end.
    changed |= ImGui::Checkbox(TR("Flag unbalanced openings"), &setup_.unbalanced_enabled);
    itemHelp(
        TR("A game is flagged when both engines see the same side ahead by the score below for each of their "
           "first moves after the book: the Games tab can show those games, the Openings tab marks and counts "
           "their openings, the overview counts them. The results are not changed."));
    if (setup_.unbalanced_enabled) {
        ImGui::Indent();
        changed |= inputInt(TR("Score (cp)"), "##unbalanced_cp", setup_.unbalanced_cp, 1, 100000);
        itemHelp(TR("200 cp: both engines think a side is two pawns ahead, or more."));
        changed |= inputInt(TR("Moves"), "##unbalanced_moves", setup_.unbalanced_moves, 1, 100);
        itemHelp(TR("The number of moves each engine plays after the book, all of them beyond the score."));
        changed |= ImGui::Checkbox(TR("Play the pair again from another opening"), &setup_.unbalanced_replace);
        itemHelp(
            trf("Once its two games are played, a pair with a game flagged is voided (its games stay in the PGN "
                "files and the journal) and played again from an opening at the end of the book, which the "
                "tournament would play last; at most {} times for one pair.",
                tour::MAX_OPENING_REPLACEMENTS));
        ImGui::Unindent();
    }

    int64_t seed = static_cast<int64_t>(setup_.seed);
    ui::label(TR("Seed"), labelWidth());
    ImGui::SetNextItemWidth(160.0f * ImGui::GetFontSize() / 16.0f);
    if (ui::inputInt64("##seed", &seed)) {
        setup_.seed = static_cast<uint64_t>(std::max<int64_t>(0, seed));
        changed     = true;
    }
    ui::help(TR("0 = a new random order for every match."));

    if (changed) saveSetupSoon();
}

void App::setupAdjudication() {
    bool changed = false;

    changed |= ImGui::Checkbox(TR("Draw"), &setup_.draw_enabled);
    ui::help(
        TR("Draw when both engines report |score| ≤ threshold for N consecutive moves, after a given move number."));
    if (setup_.draw_enabled) {
        ImGui::Indent();
        changed |= inputInt(TR("From move"), "##draw_mn", setup_.draw_movenumber, 0, 1000);
        changed |= inputInt(TR("Consecutive moves"), "##draw_mc", setup_.draw_movecount, 1, 1000);
        changed |= inputInt(TR("Max score (cp)"), "##draw_sc", setup_.draw_score, 0, 10000);
        ImGui::Unindent();
    }

    changed |= ImGui::Checkbox(TR("Resign"), &setup_.resign_enabled);
    ui::help(TR("Loss when an engine reports a score ≤ -threshold for N consecutive moves."));
    if (setup_.resign_enabled) {
        ImGui::Indent();
        changed |= inputInt(TR("Consecutive moves"), "##res_mc", setup_.resign_movecount, 1, 1000);
        changed |= inputInt(TR("Score (cp)"), "##res_sc", setup_.resign_score, 0, 100000);
        changed |= ImGui::Checkbox(TR("Both engines must agree"), &setup_.resign_twosided);
        itemHelp(
            TR("Both engines' scores must be beyond the threshold for those moves, the winner's as well as the "
               "loser's (twosided=true for fastchess and cutechess)."));
        ImGui::Unindent();
    }

    changed |= ImGui::Checkbox(TR("Maximum number of moves"), &setup_.maxmoves_enabled);
    itemHelp(TR("A game still going after this many moves is adjudicated a draw (-maxmoves)."));
    if (setup_.maxmoves_enabled) {
        ImGui::Indent();
        changed |= inputInt(TR("Moves"), "##maxmoves", setup_.maxmoves, 1, 100000);
        ImGui::Unindent();
    }

    changed |= ImGui::Checkbox(TR("Syzygy tablebases"), &setup_.tb_enabled);
    if (setup_.tb_enabled) {
        ImGui::Indent();
        changed |= tablebaseDirs("##tb_paths", setup_.tb_paths, dialogs_);
        changed |= inputInt(TR("Max pieces"), "##tb_pieces", setup_.tb_pieces, 0, 7);
        ui::help(TR("0 = everything the tables cover."));
        changed |= ImGui::Checkbox(TR("Ignore the 50-move rule"), &setup_.tb_ignore50);
        itemHelp(
            TR("A position the tablebases win only past the 50-move rule (a cursed win) is adjudicated a win "
               "rather than a draw (-tbignore50)."));

        ui::label(TR("Adjudicate"), labelWidth());
        ImGui::SetNextItemWidth(160.0f * ImGui::GetFontSize() / 16.0f);
        const char* modes[] = {TR("wins and losses"), TR("draws"), TR("both")};
        int mode            = std::clamp(setup_.tb_result, 1, 3) - 1;
        if (ImGui::Combo("##tb_mode", &mode, modes, 3)) {
            setup_.tb_result = mode + 1;
            changed          = true;
        }
        itemHelp(TR("The results the tablebases decide; the games in other positions play on (-tbadjudicate)."));
        ImGui::Unindent();
    }

    if (changed) saveSetupSoon();
}

void App::setupPgn() {
    bool changed = false;

    ui::label(TR("Event"), labelWidth());
    changed |= inputText("##event", setup_.pgn_event, -FLT_MIN);
    {
        // The files every game is written to, named after the tournament as TournamentState::create() names them.
        tour::TournamentSpec files;
        files.name = setup_.name.empty() ? defaultTournamentName(setup_) : setup_.name;
        tour::namePgnFiles(files);
        itemHelp(fmt::format(
            "{}\n• {} : {{+0.35/18 1.234s}} (cutechess, fastchess)\n• {} : {{[%eval 0.35,18] "
            "[%emt …]}}",
            TR("Every game is written to two files in the match folder:"), files.pgn_file, files.pgn_chessbase_file));
    }

    ImGui::TextUnformatted(TR("Also append every game to:"));
    const float browse = ImGui::CalcTextSize("…").x + ImGui::GetStyle().FramePadding.x * 4;
    changed |= inputText("##pgn_out", setup_.pgn_out,
                         ImGui::GetContentRegionAvail().x - browse - ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::IsItemHovered() && !setup_.pgn_out.empty()) ImGui::SetTooltip("%s", setup_.pgn_out.c_str());
    ImGui::SameLine();
    if (ImGui::Button("…##pgn_out_browse")) {
        dialogs_.saveFile(Dialogs::Purpose::PgnOut, {{"PGN", "pgn"}}, setup_.pgn_out);
    }
    if (!setup_.pgn_out.empty()) {
        int style          = setup_.export_style == PgnStyle::ChessBase ? 1 : 0;
        const char* styles = TR(
            "ChessBase / Lichess: {[%eval 0.35,18] [%emt 0:00:01.234]}, eval from White's point of view.\ncutechess: "
            "{+0.35/18 1.234s}, score from the point of view of the side that moved.");
        bool style_changed = ImGui::RadioButton("ChessBase / Lichess##pgn_out", &style, 1);
        itemHelp(styles);
        ImGui::SameLine();
        style_changed |= ImGui::RadioButton("cutechess##pgn_out", &style, 0);
        itemHelp(styles);
        if (style_changed) {
            setup_.export_style = style == 1 ? PgnStyle::ChessBase : PgnStyle::Fastchess;
            changed             = true;
        }
    }
    ui::help(
        TR("Like cutechess' -pgnout: the games of every tournament go on after those already in the file, which is "
           "never overwritten, in the order of the schedule: the two games of an opening side by side. Empty: "
           "none."));

    ImGui::TextUnformatted(TR("Extra fields in the comments:"));
    changed |= ImGui::Checkbox(TR("nodes"), &setup_.pgn_nodes);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("seldepth", &setup_.pgn_seldepth);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("nps", &setup_.pgn_nps);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("pv", &setup_.pgn_pv);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("tbhits", &setup_.pgn_tbhits);
    ImGui::SameLine();
    changed |= ImGui::Checkbox(TR("decision time"), &setup_.pgn_decision);
    ui::help(
        TR("dt=: when, during its search, the engine settled on the move it played. The Games tab draws these "
           "fields, move by move, for both engines."));

    ImGui::Spacing();
    changed |= ImGui::Checkbox(TR("Restart an engine that crashes"), &setup_.recover);
    ui::help(TR("Otherwise an engine that crashes or hangs stops the whole match."));
    changed |= ImGui::Checkbox(TR("Stop the tournament when an engine cannot start"), &setup_.stop_on_start_failure);
    ui::help(trf("Otherwise an engine still not started after {} tries, {} s apart, has its games set aside while the "
                 "others go on; it is tried again every {} minutes, and the tournament stops only when nothing else "
                 "is left to play.",
                 tour::StartFailures::TRIES, tour::StartFailures::RETRY_PAUSE.count(),
                 tour::StartFailures::RETRY_EVERY.count())
                 .c_str());
    changed |= ImGui::Checkbox(TR("Keep the engine log of every game"), &setup_.log_every_game);
    ui::help(
        TR("Every line exchanged with the engines, in logs/ in the tournament folder, opened from the Games tab. "
           "Always kept for the games lost on time, by a crash, a stall or an illegal move."));
    changed |= ImGui::Checkbox(TR("Check the mate PVs"), &setup_.check_mate_pvs);
    ui::help(
        TR("A mate score's PV must have the right length and end in mate. PVs are always checked for illegal moves; "
           "the moves whose PV is wrong are marked in the Games tab."));
    if (changed) saveSetupSoon();
}

void App::setupFormat() {
    bool changed = false;
    int format   = static_cast<int>(setup_.format);
    // What each format is, at the cursor: a grey line under them was cut at the column's edge.
    const auto radio = [&](const char* label, int value, const char* what) {
        changed |= ImGui::RadioButton(label, &format, value);
        itemHelp(what);
    };
    radio(TR("Match"), 0, TR("Two engines: is the new version stronger? Statistics from A's point of view."));
    ImGui::SameLine();
    radio(TR("Round robin"), 1,
          TR("Every engine meets every other one, each time on two games with the same opening, colours reversed."));
    ImGui::SameLine();
    radio(TR("Gauntlet"), 2, TR("The first engines of the list (the tested ones) meet every other engine."));
    ImGui::SameLine();
    radio(TR("Swiss"), 3,
          TR("Each round pairs engines with similar scores, never twice the same; for large fields. The list order is "
             "the seeding."));
    ImGui::SameLine();
    radio(TR("Knockout"), 4,
          TR("Matches in a bracket, the winner goes through; tiebreak pairs of games when a match is tied. The list "
             "order is the seeding."));
    ImGui::SameLine();
    radio(TR("Team match"), 5,
          TR("Two teams of engines, one family or version against another: every engine of a team meets every "
             "engine of the other, or board by board. The teams are scored as a whole, and each engine too."));
    setup_.format = static_cast<TournamentFormat>(format);

    if (setup_.format == TournamentFormat::Swiss) {
        changed |= inputInt(TR("Rounds"), "##rounds", setup_.cycles, 1, 1000);
        ui::help(TR("Each round is two games, colours reversed, against an engine with a similar score."));
    } else if (setup_.format == TournamentFormat::Knockout) {
        changed |= inputInt(TR("Game pairs per match"), "##ko_cycles", setup_.cycles, 1, 100);
        ui::help(TR("Each pair is two games on the same opening, colours reversed."));
    } else if (setup_.format != TournamentFormat::Match) {
        changed |= inputInt(TR("Cycles"), "##cycles", setup_.cycles, 1, 10000);
        ui::help(
            TR("How many times every pairing is played; each time is two games, colours reversed. Other programs "
               "call a cycle a round, of 2 games per encounter (-rounds N -games 2)."));
        if (setup_.format == TournamentFormat::Gauntlet) {
            const int most = std::max(1, static_cast<int>(setup_.participants.size()) - 1);
            changed |= inputInt(TR("Tested engines"), "##seeds", setup_.seeds, 1, most);
            ui::help(
                TR("The first engines of the list play every other engine, not each other: the gauntlet's seeds "
                   "(-seeds N)."));
        }
        if (setup_.format == TournamentFormat::TeamMatch) changed |= setupTeamOptions();
    }
    if (changed) saveSetupSoon();

    // A match: how long it lasts, the SPRT's bounds.
    if (setup_.format == TournamentFormat::Match) {
        ImGui::Spacing();
        setupMatch();
    }
}

bool App::setupTeamOptions() {
    bool changed = false;
    int boards   = setup_.team_boards ? 1 : 0;
    ui::label(TR("Pairings"), labelWidth());
    changed |= ImGui::RadioButton(TR("Everyone against everyone"), &boards, 0);
    itemHelp(
        TR("Every engine of a team meets every engine of the other team, one opponent per round (a Scheveningen "
           "system); the teams may differ in size."));
    ImGui::SameLine();
    changed |= ImGui::RadioButton(TR("Board by board"), &boards, 1);
    itemHelp(
        TR("The first engine of each team against the first of the other, the second against the second, and so on: "
           "two teams of the same size, in the order of the list."));
    setup_.team_boards = boards == 1;

    // The names, and how many engines each team has: the teams are made in the list of participants.
    const auto [a, b]  = teamSizes(setup_);
    const float name_w = px(200);
    ui::label(TR("Team A"), labelWidth());
    changed |= inputText("##team_a", setup_.team_a, name_w, TR("Team A"));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", trn("{} engine", "{} engines", static_cast<uint64_t>(a), a).c_str());
    ui::label(TR("Team B"), labelWidth());
    changed |= inputText("##team_b", setup_.team_b, name_w, TR("Team B"));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", trn("{} engine", "{} engines", static_cast<uint64_t>(b), b).c_str());
    itemHelp(
        TR("Without a choice, the upper half of the list plays the lower half. A participant's ⋯, or the Team A and "
           "Team B buttons under the list, move engines from one team to the other."));
    return changed;
}

void App::drawSetupSummary() {
    ui::beginCard("summary_card");
    ImGui::TextDisabled("%s", TR("Summary"));

    const auto n     = static_cast<int>(setup_.participants.size());
    const int cycles = std::max(1, setup_.cycles);
    const int seeds  = std::clamp(setup_.seeds, 1, std::max(1, n - 1));
    const auto games = plannedGames(setup_);
    std::string formula;
    std::string per_engine;

    switch (setup_.format) {
        case TournamentFormat::Match:
            formula = setup_.mode == MatchMode::Sprt       ? std::string(TR("until the SPRT decides"))
                      : setup_.mode == MatchMode::Infinite ? std::string(TR("until stopped"))
                                                           : trn("{} game pair", "{} game pairs", games / 2, games / 2);
            break;
        case TournamentFormat::RoundRobin: {
            const uint64_t pairs = n >= 2 ? static_cast<uint64_t>(n) * (n - 1) / 2 : 0;
            formula              = trf("{} pairings × 2 games × {} cycles", pairs, cycles);
            const uint64_t each  = n >= 2 ? static_cast<uint64_t>(n - 1) * 2 * cycles : 0;
            per_engine           = trn("{} game per engine", "{} games per engine", each, each);
            break;
        }
        case TournamentFormat::Gauntlet: {
            const uint64_t pairs = n >= 2 ? static_cast<uint64_t>(seeds) * (n - seeds) : 0;
            formula              = trf("{} pairings × 2 games × {} cycles", pairs, cycles);
            const uint64_t each  = n >= 2 ? static_cast<uint64_t>(n - seeds) * 2 * cycles : 0;
            per_engine           = trn("{} game per tested engine", "{} games per tested engine", each, each);
            break;
        }
        case TournamentFormat::Swiss: {
            const uint64_t pairs = static_cast<uint64_t>(n / 2);
            formula              = trf("{} rounds × {} pairings × 2 games", cycles, pairs);
            const uint64_t each  = uint64_t{2} * cycles;
            per_engine           = trn("{} game per engine", "{} games per engine", each, each);
            break;
        }
        case TournamentFormat::Knockout: {
            const uint64_t matches = n >= 2 ? static_cast<uint64_t>(n - 1) : 0;
            formula                = trf("{} matches × {} pairs of games, plus tiebreaks", matches, cycles);
            break;
        }
        case TournamentFormat::TeamMatch: {
            const auto [a, b] = teamSizes(setup_);
            if (setup_.team_boards) {
                formula    = trf("{} boards × 2 games × {} cycles", std::min(a, b), cycles);
                per_engine = trn("{} game per engine", "{} games per engine", 2 * cycles, 2 * cycles);
            } else {
                formula    = trf("{} × {} pairings × 2 games × {} cycles", a, b, cycles);
                per_engine =
                    a == b ? trn("{} game per engine", "{} games per engine", uint64_t{2} * cycles * b, 2 * cycles * b)
                           : trf("{} games per engine of team A, {} of team B", 2 * cycles * b, 2 * cycles * a);
            }
            break;
        }
    }

    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.8f);
    ImGui::TextUnformatted(games ? trn("{} game", "{} games", games, games).c_str() : "∞");
    ImGui::PopFont();
    // Wrapped: a team match's line is longer than the card is wide.
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", formula.c_str());
    if (!per_engine.empty()) ImGui::TextWrapped("%s", per_engine.c_str());
    // The SPRT's bounds, with the bias and the minimum of games when there are some.
    if (setup_.format == TournamentFormat::Match && setup_.mode == MatchMode::Sprt &&
        (setup_.sprt_bias != 0 || setup_.sprt_min_games > 0)) {
        ImGui::TextWrapped("%s", sprtText(setup_.sprt_elo0, setup_.sprt_elo1, setup_.sprt_bias,
                                          static_cast<uint64_t>(std::max(0, setup_.sprt_min_games)))
                                     .c_str());
    }
    ImGui::PopStyleColor();

    // A rough duration, with a clock.
    const bool fixed_nodes = clockFree(setup_.limit);
    const int concurrency  = setup_.concurrency_auto ? topology_.defaultConcurrency(fixed_nodes) : setup_.concurrency;
    if (const auto seconds = plannedSeconds(setup_, topology_)) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s",
                           trf("About {} with {} games at a time", ui::formatDuration(*seconds), concurrency).c_str());
        itemHelp(concurrencyRule(false));
    }

    // What could go wrong.
    ImGui::Spacing();
    const auto check = [](bool ok, const std::string& text) {
        ImGui::PushStyleColor(ImGuiCol_Text, ok ? ui::GOOD : ui::WARN);
        ImGui::TextUnformatted(ok ? "✓" : "!");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextWrapped("%s", text.c_str());
    };
    const auto names_played = participantNames(setup_);
    if (setup_.format == TournamentFormat::Match) {
        // The same engine on both sides is two participants when a book of its own tells one from the other.
        const bool apart = names_played[0] != names_played[1];
        check(!setup_.engine_a.empty() && !setup_.engine_b.empty() && apart,
              setup_.engine_a == setup_.engine_b && apart ? std::string(TR("The same engine, with different books"))
                                                          : std::string(TR("Two different engines chosen")));
        if (setup_.colours != tour::Colours::Alternate) {
            const bool a     = setup_.colours == tour::Colours::AWhite;
            const auto white = names_played[a ? 0 : 1];
            check(true, trf("{} has White in every game", white.empty() ? std::string(a ? "A" : "B") : white));
        }
    } else if (setup_.format == TournamentFormat::TeamMatch) {
        // Each team needs an engine, and board by board the same number.
        const auto [a, b] = teamSizes(setup_);
        const auto name   = [&](const std::string& given, const char* fallback) {
            return given.empty() ? std::string(fallback) : given;
        };
        if (a == 0 || b == 0) {
            check(false, TR("A team match needs an engine in each team: put one in the other team (the participant's "
                            "⋯)."));
        } else if (setup_.team_boards && a != b) {
            check(false, trf("Board by board needs two teams of the same size, not {} and {}.", a, b));
        } else {
            check(true, trf("{} ({}) against {} ({})", name(setup_.team_a, TR("Team A")), a,
                            name(setup_.team_b, TR("Team B")), b));
        }
    } else {
        check(n >= 2, trn("{} engine", "{} engines", n, n));
    }
    std::vector<std::string> names;
    if (setup_.format == TournamentFormat::Match) {
        names = {setup_.engine_a, setup_.engine_b};
    } else {
        for (const auto& p : setup_.participants) names.push_back(p.name);
    }
    const auto chosen = [&](const EngineEntry& e) {
        return std::find(names.begin(), names.end(), e.name) != names.end();
    };
    // A program or a working directory not found, with Open the engine and Locate…; the opening book not found.
    drawSetupNotices(programNotices(setup_, engines_), "programs");
    // The engines on machines: their machine, its round trip against the clock, a passphrase to type.
    drawSetupNotices(remoteNotices(setup_, engines_, machines_), "remote");
    if (setup_.machine.empty()) {
        std::vector<Notice> locked;
        std::vector<std::string> asked;
        for (const auto& name : names) {
            const auto i = engineIndex(name);
            if (i < 0 || engines_[i].machine.empty()) continue;
            const auto* m = machineById(engines_[i].machine);
            if (!m || std::find(asked.begin(), asked.end(), m->id) != asked.end()) continue;
            asked.push_back(m->id);
            bool missing = false;
            (void)machinePassphrase(*m, missing);
            if (missing) {
                locked.push_back({Notice::Level::Warning,
                                  trf("The key of {} needs its passphrase: Start asks for it once this session.",
                                      m->name.empty() ? m->host : m->name),
                                  {},
                                  {enterPassphraseFix(m->id)}});
            }
        }
        drawSetupNotices(std::move(locked), "passphrases");
    }
    const auto book_missing = bookNotice(setup_);
    if (book_missing) drawSetupNotices({*book_missing}, "book");
    drawRebuiltSincePlayed(names);
    if (setup_.chess960) {
        if (!book_missing) {
            check(true, setup_.book.empty()
                            ? std::string(TR("Chess960: the 960 starting positions, in random order"))
                            : trf("Chess960: {}", std::filesystem::path(setup_.book).filename().string()));
        }
        for (const auto& e : engines_) {
            const bool plays =
                e.uci_options.empty() || std::any_of(e.uci_options.begin(), e.uci_options.end(),
                                                     [](const auto& o) { return o.name == "UCI_Chess960"; });
            if (chosen(e) && !plays) {
                drawSetupNotices({{Notice::Level::Warning,
                                   trf("{} does not play Chess960.", e.name),
                                   TR("Its options have no UCI_Chess960 (a WinBoard engine: no fischerandom variant)."),
                                   {openEngineFix(e.name)}}},
                                 e.name.c_str());
            }
        }
    } else if (!book_missing) {
        // The book's name, the plies kept of a PGN book, and how a Polyglot book's lines are followed.
        auto text = setup_.book.empty()
                        ? std::string(TR("No opening book: every game starts from the initial position"))
                        : trf("Openings: {}", std::filesystem::path(setup_.book).filename().string());
        if (tour::isPgnBook(setup_.book) && setup_.book_plies > 0) {
            text += ", " + trn("{} ply", "{} plies", static_cast<uint64_t>(setup_.book_plies), setup_.book_plies);
        }
        check(!setup_.book.empty(), text);
        if (tour::isPolyglot(setup_.book)) {
            check(true, trf("Polyglot book: its lines to {} plies, as positions; {}",
                            setup_.book_plies > 0 ? setup_.book_plies : POLYGLOT_PLIES,
                            randomnessText(setup_.book_randomness)));
        }
    }
    const bool per_round = setup_.format == TournamentFormat::RoundRobin ||
                           setup_.format == TournamentFormat::Gauntlet || setup_.format == TournamentFormat::Swiss ||
                           setup_.format == TournamentFormat::TeamMatch;
    if (per_round && setup_.opening_per_round) check(true, TR("The same opening for every pair of a round"));
    if (setup_.unbalanced_enabled) {
        const auto moves = static_cast<uint64_t>(std::max(1, setup_.unbalanced_moves));
        const int cp     = std::max(1, setup_.unbalanced_cp);
        check(true, setup_.unbalanced_replace
                        ? trn("Unbalanced openings replaced: both engines beyond ±{} cp for {} move after the book",
                              "Unbalanced openings replaced: both engines beyond ±{} cp for {} moves after the book",
                              moves, cp, moves)
                        : trn("Unbalanced openings flagged: both engines beyond ±{} cp for {} move after the book",
                              "Unbalanced openings flagged: both engines beyond ±{} cp for {} moves after the book",
                              moves, cp, moves));
    }
    if (!setup_.pgn_out.empty()) {
        check(true, trf("Games also appended to {}", std::filesystem::path(setup_.pgn_out).filename().string()));
    }
    if (setup_.common_hash > 0 || setup_.common_threads > 0) {
        std::string parts;
        if (setup_.common_hash > 0) parts += trf("Hash {} MB", setup_.common_hash);
        if (setup_.common_threads > 0) parts += (parts.empty() ? "" : ", ") + trf("Threads {}", setup_.common_threads);
        check(true, trf("Common settings: {}", parts));
    }
    if (setup_.engine_syzygy) {
        check(!setup_.tb_paths.empty(), setup_.tb_paths.empty()
                                            ? std::string(TR("Syzygy tablebases for the engines: no directory given"))
                                            : trf("Syzygy tablebases for the engines: {}", setup_.tb_paths));
    }
    if (setup_.engine_ownbook_off) check(true, TR("The engines' own books are off"));
    // The hash tables against the memory: a warning above the free memory, an error above the installed one.
    if (const auto memory = setupMemory(setup_, engines_, topology_); memory.hash_mb > 0) {
        drawSetupNotices({memoryNotice(setup_, engines_, memory)}, "memory");
    }
    // What the machine's budget lets it play at once: the memory, the GPUs (Settings).
    if (const auto limit = budgetLimit(setup_, engines_, machineBudget(), concurrency); !limit.why.empty()) {
        check(false, limit.why);
    }
    // The threads, the option values the engines refuse, the tablebases.
    drawSetupNotices(setupNotices(), "checks");
    for (const auto& p : setup_.participants) {
        if (setup_.format != TournamentFormat::Match && !fixed_nodes && p.time_percent != 100) {
            check(true, trf("{} plays with {} % of the time", p.name, p.time_percent));
        }
        if (setup_.format != TournamentFormat::Match && setup_.limit == LimitKind::Depth && p.depth > 0) {
            check(true, trf("{} searches to depth {}", p.name, p.depth));
        }
    }
    // The participants with a book of their own: "Alpha: own book A, 12 plies", and how their moves are chosen.
    const auto people = fcg::setupParticipants(setup_);
    if (setup_.book_randomness != 0 &&
        std::any_of(people.begin(), people.end(), [](const auto& p) { return !p.book.empty(); })) {
        check(true, trf("Own books: {}", randomnessText(setup_.book_randomness)));
    }
    for (std::size_t i = 0; i < people.size(); ++i) {
        const auto& book = people[i].book;
        if (book.empty()) continue;
        const bool found = std::filesystem::is_regular_file(book.path);
        if (found && tour::isPgnBook(book.path)) {
            // A PGN book: being turned into a Polyglot book, or what that found.
            const auto status = pgn_books_.want(book.path, book.depth, setup_.chess960);
            const auto text   = fmt::format("{}: {}", names_played[i], ownBookText(book));
            if (status.state == BookState::Failed) {
                check(false, trf("{}: its book cannot be used: {}", names_played[i], status.error));
            } else if (status.state != BookState::Ready) {
                check(true, trf("{} (preparing the book: {} %)", text, percent(status.progress)));
            } else {
                check(status.stats.skipped == 0, status.stats.skipped == 0
                                                     ? text
                                                     : fmt::format("{} · {}", text, skippedText(status.stats.skipped)));
            }
            continue;
        }
        const auto& problem = found ? ownBookCheck(book.path) : std::string();
        check(found && problem.empty(), !found ? trf("{}: its book is not found: {}", names_played[i], book.path)
                                        : !problem.empty()
                                            ? trf("{}: its book cannot be used: {}", names_played[i], problem)
                                            : fmt::format("{}: {}", names_played[i], ownBookText(book)));
    }
    ui::endCard();
}

void App::listTemplates() {
    // Listed every few seconds rather than every frame; a template copied into the folder shows up soon enough.
    const auto now = std::chrono::steady_clock::now();
    if (now - templates_listed_ <= std::chrono::seconds(3)) return;
    templates_listed_ = now;
    template_names_.clear();
    std::error_code ec;
    const auto dir = storage_.root() / "templates";
    for (std::filesystem::directory_iterator it(dir, ec), last; !ec && it != last; it.increment(ec)) {
        if (it->path().extension() == ".json") template_names_.push_back(it->path().stem().string());
    }
    std::sort(template_names_.begin(), template_names_.end());
}

bool App::loadTemplate(const std::string& name) {
    // The sections that followed their profile take its values of today; the look of the program is not part of a
    // template.
    auto loaded = readTemplate(storage_.root() / "templates" / (name + ".json"), profiles_);
    if (!loaded) {
        template_message_ = trf("Cannot read the template {}.", name);
        return false;
    }
    keepProgramSettings(*loaded, setup_);
    setup_           = std::move(*loaded);
    template_loaded_ = name;
    template_message_.clear();
    saveSetupSoon();
    return true;
}

bool App::cloneTournament(const std::filesystem::path& dir) {
    auto loaded = readTournamentSetup(dir, profiles_);
    if (!loaded) return false;
    keepProgramSettings(*loaded, setup_);
    setup_ = std::move(*loaded);
    template_loaded_.clear();
    template_message_.clear();
    start_error_.clear();
    saveSetupSoon();
    page_ = Page::NewMatch;
    return true;
}

bool App::canClone(const std::filesystem::path& dir) {
    const auto [it, added] = clone_sources_.try_emplace(dir.string(), false);
    if (added) {
        std::error_code ec;
        it->second = std::filesystem::is_regular_file(dir / TOURNAMENT_SETUP_FILE, ec);
    }
    return it->second;
}

void App::drawClone(const std::filesystem::path& dir, bool compact) {
    const bool can = canClone(dir);
    ImGui::BeginDisabled(!can);
    const bool clicked =
        compact ? ImGui::SmallButton(TR("Clone")) : ui::iconButton("clone", ui::Icon::Copy, TR("Clone"));
    ImGui::EndDisabled();
    if (clicked) cloneTournament(dir);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", can ? TR("A new tournament with this one's participants and settings, to change "
                                         "before it starts: another time control, the next version of an engine.")
                                    : TR("Started by a version that did not keep its setup: it cannot be cloned."));
    }
    ui::topic(guide::topics::Queue);
}

void App::drawTemplates() {
    listTemplates();
    const auto dir = storage_.root() / "templates";
    std::error_code ec;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("Template"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(200));
    const auto preview = template_loaded_.empty() ? std::string(TR("None")) : template_loaded_;
    bool open_save     = std::exchange(open_save_template_, false);  // or from the command palette
    if (ImGui::BeginCombo("##template", preview.c_str(), ImGuiComboFlags_HeightLarge)) {
        for (const auto& name : template_names_) {
            if (ImGui::Selectable(name.c_str(), name == template_loaded_)) loadTemplate(name);
        }
        if (template_names_.empty()) ImGui::TextDisabled("%s", TR("No template yet."));
        ImGui::Separator();
        if (ImGui::Selectable(TR("Save the setup as a template…"))) open_save = true;
        ImGui::EndCombo();
    } else {
        itemHelp(
            TR("A template is the whole setup: a profile for each section, and what no profile holds (the "
               "name, the participants, the concurrency, the PGN files, the broadcast). A section that played "
               "as its profile takes the profile's values of the day."));
    }
    if (open_save) {
        template_name_ = template_loaded_;
        ImGui::OpenPopup("save_template");
    }

    const auto save = [&](const std::string& file) {
        std::filesystem::create_directories(dir, ec);
        if (writeJson(dir / (file + ".json"), templateJson(portable::stored(setup_), profiles_))) {
            template_loaded_ = file;
            template_message_.clear();
        } else {
            template_message_ = trf("Cannot write {}", (dir / (file + ".json")).string());
        }
        templates_listed_ = {};
    };
    if (ImGui::BeginPopup("save_template")) {
        ImGui::TextDisabled("%s", TR("Save the setup as a template…"));
        char buffer[128];
        const auto n = std::min(template_name_.size(), sizeof(buffer) - 1);
        std::copy_n(template_name_.data(), n, buffer);
        buffer[n] = 0;
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(px(260));
        const bool enter = ImGui::InputTextWithHint("##template_name", TR("Template name"), buffer, sizeof(buffer),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        template_name_   = buffer;
        const auto file  = fileNameOf(template_name_);
        const bool taken = !template_name_.empty() &&
                           (std::find(template_names_.begin(), template_names_.end(), file) != template_names_.end() ||
                            std::filesystem::exists(dir / (file + ".json"), ec));
        if (taken) {
            ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
            ImGui::TextUnformatted(trf("Replace the template {}?", file).c_str());
            ImGui::PopStyleColor();
        }
        ImGui::BeginDisabled(template_name_.empty());
        if (ImGui::Button(taken ? TR("Replace") : TR("Save")) || (enter && !template_name_.empty())) {
            save(file);
            template_name_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(TR("Cancel"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (!template_message_.empty()) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
        ImGui::TextUnformatted(template_message_.c_str());
        ImGui::PopStyleColor();
    }
}

}  // namespace fcg
