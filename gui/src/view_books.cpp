// "Books": the library of opening books, and the openings of the book chosen, with what is wrong with them.

#include <algorithm>
#include <filesystem>
#include <utility>

#include <imgui.h>

#include <fmt/include/fmt/format.h>

#include <tour/chess324.hpp>
#include <tour/pgn_book.hpp>
#include <tour/chess_text.hpp>
#include <tour/polyglot.hpp>

#include "app.hpp"
#include "i18n.hpp"
#include "run.hpp"
#include "ui.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

bool editText(const char* id, std::string& value, float width = -FLT_MIN) {
    char buffer[4096];
    const auto n = std::min(value.size(), sizeof(buffer) - 1);
    std::copy_n(value.data(), n, buffer);
    buffer[n] = 0;
    ImGui::SetNextItemWidth(width);
    if (ImGui::InputText(id, buffer, sizeof(buffer))) {
        value = buffer;
        return true;
    }
    return false;
}

// What is wrong with an opening, in words.
std::string problems(uint8_t flags) {
    std::string out;
    const auto add = [&](const char* text) { out += (out.empty() ? "" : ", ") + std::string(text); };
    if (flags & tour::BOOK_ILLEGAL) add(TR("illegal position"));
    if (flags & tour::BOOK_DUPLICATE) add(TR("duplicate"));
    if (flags & tour::BOOK_MIRRORED) add(TR("colours reversed of an earlier one"));
    if (flags & tour::BOOK_OVER) add(TR("game already over"));
    return out;
}

constexpr uint8_t ANY_PROBLEM = tour::BOOK_ILLEGAL | tour::BOOK_DUPLICATE | tour::BOOK_MIRRORED | tour::BOOK_OVER;

// A tooltip for the item just drawn, also when it is disabled, its lines wrapped.
void tip(const char* text) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

int percent(double share) { return static_cast<int>(std::clamp(share, 0.0, 1.0) * 100); }

// The options of a Polyglot book built from a PGN file, in words: its description in the library.
std::string buildSummary(const std::string& pgn, const tour::PgnBookOptions& o) {
    std::string out = trn("{} ply", "{} plies", o.plies, o.plies);
    if (o.min_games > 1)
        out += ", " + trn("moves of {} game or more", "moves of {} games or more", o.min_games, o.min_games);
    out += ", ";
    out += o.weight == tour::PgnBookOptions::Weight::Score ? TR("weighted by score") : TR("weighted by count");
    if (o.min_elo > 0) out += ", " + trf("players rated {} or more", o.min_elo);
    return trf("From {}: {}", fs::path(pgn).filename().string(), out);
}

}  // namespace

void App::addBook(const std::string& path) {
    BookEntry book;
    book.path = path;
    book.name = fs::path(path).stem().string();
    books_.push_back(std::move(book));
    selected_book_ = static_cast<int>(books_.size()) - 1;
    storage_.saveBooks(books_);
    startBookScan(selected_book_);
}

std::string App::newBookPath(const std::string& name) {
    std::error_code ec;
    const auto folder = [&](int i) {
        if (i < 0 || i >= static_cast<int>(books_.size())) return fs::path();
        const auto dir = fs::path(books_[i].path).parent_path();
        return !dir.empty() && fs::is_directory(dir, ec) ? dir : fs::path();
    };
    auto dir = folder(selected_book_);
    if (dir.empty()) dir = folder(static_cast<int>(books_.size()) - 1);
    if (dir.empty()) {
        dir = storage_.root() / "books";
        fs::create_directories(dir, ec);
    }
    return (dir / name).string();
}

void App::addWrittenBook(const std::string& path, const std::string& description) {
    for (int i = 0; i < static_cast<int>(books_.size()); ++i) {
        std::error_code ec;
        if (books_[i].path != path && !fs::equivalent(books_[i].path, path, ec)) continue;
        selected_book_ = i;
        book_item_     = -1;
        if (books_[i].description.empty()) books_[i].description = description;
        storage_.saveBooks(books_);
        startBookScan(i);  // the file changed
        return;
    }
    addBook(path);
    books_.back().description = description;
    storage_.saveBooks(books_);
}

void App::askChess324(bool symmetrical) {
    dialogs_.saveFile(symmetrical ? Dialogs::Purpose::Chess324SymmetricalBook : Dialogs::Purpose::Chess324Book,
                      {{"EPD", "epd"}}, newBookPath(symmetrical ? "chess324-symmetrical.epd" : "chess324.epd"));
}

void App::writeChess324Book(std::string path, bool symmetrical) {
    // A book is read by its extension.
    if (!fs::path(path).has_extension()) path += ".epd";
    try {
        const auto n = tour::writeChess324(path, symmetrical);
        Console::instance().add(trn("{} position written to {}", "{} positions written to {}", n, n, path));
        addWrittenBook(path, symmetrical ? TR("Chess324: the 18 symmetrical starting positions")
                                         : TR("Chess324: the 324 starting positions"));
    } catch (const std::exception& e) {
        Console::instance().add(fmt::format("{} ({})", trf("Cannot write {}", path), e.what()), true);
    }
}

void App::drawBookCreateMenu() {
    if (!ImGui::BeginPopup("##book_create")) return;
    if (ImGui::BeginMenu(TR("Generate Chess324 book"))) {
        if (ImGui::MenuItem(TR("All 324 positions…"))) askChess324(false);
        tip(
            TR("Chess324: the kings and the rooks on their usual squares, so castling is the usual one; the "
               "queen, the bishops and the knights of each side shuffled on the other squares of its back rank, the "
               "bishops on opposite colours. 18 arrangements a side, White's and Black's chosen independently: 324 "
               "starting positions, an EPD book any engine plays, added to the library."));
        if (ImGui::MenuItem(TR("Only the 18 symmetrical ones…"))) askChess324(true);
        tip(TR("Black's pieces on the same files as White's: 18 starting positions, added to the library."));
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem(TR("Build a Polyglot book…"))) openBookBuild();
    tip(
        TR("From the games of a PGN file: their moves up to a depth, those played by enough games, weighted by their "
           "number or their score; written as a Polyglot book (.bin) and added to the library."));
    ImGui::EndPopup();
}

tour::PgnBookOptions App::bookBuildOptions() const {
    const auto& f = book_build_form_;
    tour::PgnBookOptions o;
    o.plies     = f.plies;
    o.min_games = f.min_games;
    o.weight    = f.by_score ? tour::PgnBookOptions::Weight::Score : tour::PgnBookOptions::Weight::Count;
    o.min_elo   = f.rated ? f.min_elo : 0;
    return o;
}

void App::openBookBuild() {
    book_build_shown_ = true;
    if (book_build_form_.pgn.empty() && selected_book_ >= 0 && selected_book_ < static_cast<int>(books_.size()) &&
        tour::isPgnBook(books_[selected_book_].path)) {
        book_build_form_.pgn = books_[selected_book_].path;
    }
}

void App::startBookBuild(std::string bin) {
    // Read as a Polyglot book by its extension; and never the PGN file itself.
    if (!tour::isPolyglot(bin)) bin += ".bin";
    book_build_error_.clear();
    book_build_shown_ = true;
    if (!book_build_.start(book_build_form_.pgn, bin, bookBuildOptions())) {
        book_build_error_ = TR("A book is being built already: wait for it, or cancel it.");
    }
}

void App::pollBookBuild() {
    const auto ended = book_build_.poll();
    if (!ended) return;
    const auto name  = fs::path(ended->bin).filename().string();
    const auto& read = ended->stats;
    if (ended->cancelled) {
        Console::instance().add(trf("{}: cancelled, nothing written.", name));
        return;
    }
    if (!ended->error.empty()) {
        const auto pgn = fs::path(ended->pgn).filename().string();
        if (!ended->empty) {
            book_build_error_ = trf("The book cannot be built: {}", ended->error);
        } else if (read.games == 0 && read.left_out > 0) {
            book_build_error_ =
                trn("No move is left: {} game of {} is left out by the rating, or by its missing result with weights "
                    "by score. Lower the rating, or weigh the moves by count.",
                    "No move is left: {} games of {} are left out by the rating, or by their missing result with "
                    "weights by score. Lower the rating, or weigh the moves by count.",
                    read.left_out, read.left_out, pgn);
        } else if (read.games == 0) {
            book_build_error_ = trf("No game of {} gives a move: is it a PGN file?", pgn);
        } else if (ended->options.min_games > 1) {
            book_build_error_ =
                trf("No move of {} is played by {} games or more: lower the minimum.", pgn, ended->options.min_games);
        } else {
            book_build_error_ = trf("No move of {} is left with these options.", pgn);
        }
        Console::instance().add(fmt::format("{}: {}", name, book_build_error_), true);
        return;
    }

    book_build_error_.clear();
    Console::instance().add(trn("{} written: {} positions, from {} game.", "{} written: {} positions, from {} games.",
                                read.games, name, read.positions, read.games));
    if (read.left_out > 0) {
        Console::instance().add(
            fmt::format("{}: {}", name,
                        trn("{} game left out by the options (a rating below the minimum, or no result).",
                            "{} games left out by the options (a rating below the minimum, or no result).",
                            read.left_out, read.left_out)));
    }
    if (read.skipped > 0) {
        Console::instance().add(
            fmt::format("{}: {}", name,
                        trn("{} game could not be read: it is left out.",
                            "{} games could not be read: they are left out.", read.skipped, read.skipped)),
            true);
    }
    if (!read.stopped.empty()) {
        Console::instance().add(
            fmt::format("{}: {}", name, trf("The reading stopped before the end of the file: {}", read.stopped)), true);
    }
    // The new book shown in place of the form.
    book_build_shown_ = false;
    addWrittenBook(ended->bin, buildSummary(ended->pgn, ended->options));
}

void App::drawBookBuild() {
    auto& form         = book_build_form_;
    const float label  = px(170);
    const float browse = ImGui::CalcTextSize("…").x + ImGui::GetStyle().FramePadding.x * 4;
    const bool busy    = book_build_.busy();

    ImGui::SeparatorText(TR("Build a Polyglot book"));
    ImGui::BeginDisabled(busy);
    ui::label(TR("PGN file"), label);
    editText("##build_pgn", form.pgn, ImGui::GetContentRegionAvail().x - browse - ImGui::GetStyle().ItemSpacing.x);
    ImGui::SameLine();
    if (ImGui::Button("…##build_pgn_file")) {
        dialogs_.openFile(Dialogs::Purpose::BuildBookPgn, {{"PGN", "pgn"}, {TR("All files"), "*"}}, form.pgn);
    }

    ui::label(TR("Depth (plies)"), label);
    ImGui::SetNextItemWidth(px(120));
    if (ui::inputInt("##build_plies", &form.plies)) form.plies = std::clamp(form.plies, 1, 100);
    tip(TR("How far each game goes into the book, in plies (a move of one side) from its start."));

    ui::label(TR("Minimum games"), label);
    ImGui::SetNextItemWidth(px(120));
    if (ui::inputInt("##build_min_games", &form.min_games)) form.min_games = std::clamp(form.min_games, 1, 1000000);
    tip(
        TR("A move is kept when at least this many games play it in its position; the others are left out as "
           "played too rarely (Polyglot's -min-game)."));

    ui::label(TR("Weights"), label);
    if (ImGui::RadioButton(TR("By score"), form.by_score)) form.by_score = true;
    tip(
        TR("A move weighs the points the side that played it made in its games: a win 2, a draw 1, a loss 0, as "
           "Polyglot counts. The games without a result are left out, and so is a move whose games were all lost."));
    ImGui::SameLine();
    if (ImGui::RadioButton(TR("By count"), !form.by_score)) form.by_score = false;
    tip(TR("A move weighs the number of games that play it, whatever their results."));

    ui::label(TR("Rating"), label);
    ImGui::Checkbox(TR("Only the moves of players rated at least"), &form.rated);
    tip(
        TR("A move counts when the player who made it is rated this or more, by the game's WhiteElo or BlackElo tag; "
           "a player without a rating is left out."));
    ImGui::SameLine();
    ImGui::BeginDisabled(!form.rated);
    ImGui::SetNextItemWidth(px(90));
    if (ui::inputInt("##build_min_elo", &form.min_elo)) form.min_elo = std::clamp(form.min_elo, 1, 4000);
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    ImGui::Spacing();
    if (busy) {
        const auto share = book_build_.progress();
        ImGui::ProgressBar(
            static_cast<float>(share), ImVec2(px(320), 0),
            book_build_.cancelling() ? TR("Cancelling…") : trf("Reading the games… {} %", percent(share)).c_str());
        ImGui::SameLine();
        ImGui::BeginDisabled(book_build_.cancelling());
        if (ImGui::Button(TR("Cancel"))) book_build_.cancel();
        ImGui::EndDisabled();
        ImGui::TextDisabled("%s", trf("Writing {}", book_build_.bin()).c_str());
    } else {
        std::error_code ec;
        const bool found = fs::is_regular_file(form.pgn, ec);
        ImGui::BeginDisabled(!found);
        if (ImGui::Button(TR("Build…"))) {
            // Next to the PGN file.
            const auto pgn  = fs::path(form.pgn);
            const auto name = pgn.stem().string() + ".bin";
            dialogs_.saveFile(Dialogs::Purpose::BuildBookBin, {{TR("Polyglot book"), "bin"}},
                              pgn.has_parent_path() ? (pgn.parent_path() / name).string() : newBookPath(name));
        }
        ImGui::EndDisabled();
        tip(found ? TR("Asks where to write the book, then reads the games in the background; the book is added to "
                       "the library.")
            : form.pgn.empty() ? TR("Choose a PGN file first.")
                               : trf("File not found: {}", form.pgn).c_str());
    }
    if (!book_build_error_.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
        ImGui::TextWrapped("%s", book_build_error_.c_str());
        ImGui::PopStyleColor();
    }
}

void App::cancelBookScan() {
    if (!book_reading_) return;
    // Reading a big file cannot be interrupted: it ends in the background, and is forgotten.
    *book_reading_->cancel = true;
    abandoned_scans_.push_back(std::move(book_reading_->result));
    book_reading_.reset();
}

void App::startBookScan(int index) {
    if (index < 0 || index >= static_cast<int>(books_.size())) return;
    cancelBookScan();

    BookReading reading;
    reading.index  = index;
    reading.path   = books_[index].path;
    reading.cancel = std::make_shared<std::atomic<bool>>(false);
    reading.done   = std::make_shared<std::atomic<std::size_t>>(0);
    reading.result = std::async(std::launch::async, [path = reading.path, plies = books_[index].plies,
                                                     cancel = reading.cancel, done = reading.done] {
        return std::make_shared<tour::BookScan>(path, plies, cancel.get(), done.get());
    });
    book_reading_  = std::move(reading);
    book_scan_.reset();
    book_scan_index_ = index;
    book_scan_error_.clear();
    book_item_       = -1;
    book_rows_dirty_ = true;
}

void App::pollBookScan() {
    std::erase_if(abandoned_scans_,
                  [](const auto& f) { return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready; });
    if (!book_reading_ || book_reading_->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;

    auto reading = std::move(*book_reading_);
    book_reading_.reset();
    try {
        book_scan_ = reading.result.get();
    } catch (const std::exception& e) {
        book_scan_error_ = e.what();
        return;
    }
    book_rows_dirty_ = true;

    // The book's summary is kept in the library, for the list and the setup.
    for (auto& b : books_) {
        if (b.path != reading.path) continue;
        b.positions = book_scan_->size();
        b.problems  = book_scan_->count(ANY_PROBLEM);
        b.chess960  = book_scan_->chess960();
        b.file      = tour::fingerprint(b.path);
    }
    storage_.saveBooks(books_);
}

void App::drawBooks() {
    if (!ui::beginPanel(TR("Books###books"))) {
        ui::endPanel();
        return;
    }
    pollBookScan();

    // The library on the left.
    const float page = ImGui::GetContentRegionAvail().x;
    ImGui::BeginChild("##book_list", ImVec2(ui::paneWidth(setup_.panes["books"], page, px(260), px(520)), 0),
                      ImGuiChildFlags_Borders);
    if (ImGui::Button(TR("Add…"))) {
        dialogs_.openFile(Dialogs::Purpose::AddBook,
                          {{TR("Books (EPD, PGN, Polyglot)"), "epd;pgn;bin"}, {TR("All files"), "*"}});
    }
    tip(
        TR("Adds an opening book of yours, EPD, PGN or Polyglot (.bin), to the library: the setup's Openings then "
           "offer it by name. The file stays where it is."));
    ui::topic(guide::topics::Books);
    ui::sameLineOrWrap(TR("Create…"));
    const bool create = ImGui::Button(TR("Create…"));
    const ImVec2 below(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y);  // the menu drops down from it
    tip(TR("Books the program writes, added to the library."));
    if (create || std::exchange(open_book_create_, false)) ImGui::OpenPopup("##book_create");
    ImGui::SetNextWindowPos(below, ImGuiCond_Appearing);
    drawBookCreateMenu();
    ui::sameLineOrWrap(TR("Remove"));
    ImGui::BeginDisabled(selected_book_ < 0 || book_build_shown_);
    if (ImGui::Button(TR("Remove")) && selected_book_ >= 0) {
        if (book_scan_index_ == selected_book_) {
            cancelBookScan();
            book_scan_.reset();
            book_scan_index_ = -1;
        }
        books_.erase(books_.begin() + selected_book_);
        if (book_scan_index_ > selected_book_) book_scan_index_--;
        selected_book_ = std::min(selected_book_, static_cast<int>(books_.size()) - 1);
        storage_.saveBooks(books_);
    }
    ImGui::EndDisabled();
    ui::help(TR("Removes the book from the library; the file stays where it is."));

    ImGui::Separator();
    for (int i = 0; i < static_cast<int>(books_.size()); ++i) {
        const auto& b = books_[i];
        std::error_code ec;
        const bool missing = !fs::is_regular_file(b.path, ec);
        ImGui::PushStyleColor(ImGuiCol_Text, missing ? ui::BAD : b.problems > 0 ? ui::WARN : ui::MUTED);
        ImGui::TextUnformatted(missing || b.problems > 0 ? "!" : "·");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Selectable(fmt::format("{}##book{}", b.name, i).c_str(),
                              selected_book_ == i && !book_build_shown_)) {
            selected_book_    = i;
            book_item_        = -1;
            book_build_shown_ = false;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", missing ? trf("File not found: {}", b.path).c_str() : b.path.c_str());
        if (b.positions > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", ui::formatCount(b.positions).c_str());
        }
    }
    // The book being built, until it joins the library.
    if (book_build_.busy()) {
        ImGui::TextDisabled("…");
        ImGui::SameLine();
        if (ImGui::Selectable(fmt::format("{}##book_building", fs::path(book_build_.bin()).stem().string()).c_str(),
                              book_build_shown_)) {
            book_build_shown_ = true;
        }
        tip(trf("Being built from {}", book_build_.pgn()).c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%d %%", percent(book_build_.progress()));
    }
    if (books_.empty() && !book_build_.busy()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("%s", TR("No book yet. Click Add."));
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
    if (ui::splitter("##book_split", setup_.panes["books"], page)) saveSetupSoon();

    ImGui::BeginChild("##book_detail");
    if (book_build_shown_) {
        drawBookBuild();
    } else if (selected_book_ < 0 || selected_book_ >= static_cast<int>(books_.size())) {
        ui::emptyState(TR("Choose a book in the list."));
    } else {
        drawBookDetail(books_[selected_book_]);
    }
    ImGui::EndChild();
    ui::endPanel();
}

void App::drawBookDetail(BookEntry& book) {
    const float label  = px(150);
    const float browse = ImGui::CalcTextSize("…").x + ImGui::GetStyle().FramePadding.x * 4;
    bool changed       = false;

    // The book shown is read once, in the background.
    if (book_reading_ ? book_reading_->index != selected_book_ : book_scan_index_ != selected_book_) {
        startBookScan(selected_book_);
    }

    ui::label(TR("Name"), label);
    changed |= editText("##book_name", book.name);

    ui::label(TR("File"), label);
    changed |=
        editText("##book_path", book.path, ImGui::GetContentRegionAvail().x - browse - ImGui::GetStyle().ItemSpacing.x);
    ImGui::SameLine();
    if (ImGui::Button("…##book_file")) {
        dialogs_.openFile(Dialogs::Purpose::BookFile,
                          {{TR("Books (EPD, PGN, Polyglot)"), "epd;pgn;bin"}, {TR("All files"), "*"}}, book.path);
    }

    ui::label(TR("Description"), label);
    changed |= editText("##book_description", book.description);

    const bool polyglot = tour::isPolyglot(book.path);
    const auto ext      = fs::path(book.path).extension().string();
    if (polyglot || ext == ".pgn" || ext == ".PGN") {
        ui::label(polyglot ? TR("Depth (plies)") : TR("Plies (PGN)"), label);
        ImGui::SetNextItemWidth(px(120));
        if (ui::inputInt("##book_plies", &book.plies)) {
            book.plies = std::clamp(book.plies, -1, polyglot ? 40 : 1000);
            changed    = true;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) startBookScan(selected_book_);  // other openings
        ui::help(polyglot ? trf("How deep the lines of the book are followed; -1 = {}.", POLYGLOT_PLIES).c_str()
                          : TR("For a PGN book: number of plies kept. -1 = all."));
    }

    // What the reading found.
    ImGui::Spacing();
    if (book_reading_ && book_reading_->index == selected_book_) {
        ImGui::TextDisabled("%s", trn("Reading… {} opening", "Reading… {} openings", book_reading_->done->load(),
                                      ui::formatCount(book_reading_->done->load()))
                                      .c_str());
    } else if (!book_scan_error_.empty() && book_scan_index_ == selected_book_) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
        ImGui::TextWrapped("%s", book_scan_error_.c_str());
        ImGui::PopStyleColor();
    } else if (book_scan_ && book_scan_index_ == selected_book_) {
        const auto& scan    = *book_scan_;
        std::string summary = trn("{} opening", "{} openings", scan.size(), scan.size());
        if (scan.kind() == tour::BookScan::Kind::Polyglot) summary += " · " + std::string(TR("from a Polyglot book"));
        if (scan.chess960()) summary += " · Chess960";
        ImGui::TextUnformatted(summary.c_str());

        const std::pair<uint8_t, const char*> kinds[] = {
            {tour::BOOK_ILLEGAL, TR("illegal positions")},
            {tour::BOOK_DUPLICATE, TR("duplicates")},
            {tour::BOOK_MIRRORED, TR("colours reversed of an earlier one")},
            {tour::BOOK_OVER, TR("games already over")}};
        for (const auto& [flag, text] : kinds) {
            const auto n = scan.count(flag);
            ImGui::PushStyleColor(ImGuiCol_Text, n ? ui::WARN : ui::GOOD);
            ImGui::TextUnformatted(n ? "!" : "✓");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextUnformatted(trf("{}: {}", text, ui::formatCount(n)).c_str());
        }
        // Escape lines are skipped now, but earlier versions read them as moves: worth knowing for past results.
        if (const auto n = scan.escapeLines()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
            ImGui::TextUnformatted("!");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextUnformatted(trf("{}: {}", TR("lines starting with %, ignored"), ui::formatCount(n)).c_str());
            ui::help(
                TR("The PGN standard makes them comments. fastchess-gui 0.4.13 and earlier read them as moves, "
                   "and could play some openings of this book from a wrong position, often the starting one."));
        }

        ImGui::Spacing();
        if (ImGui::Button(TR("Use it for the next tournament"))) {
            setup_.book       = book.path;
            setup_.book_plies = book.plies;
            if (scan.chess960()) setup_.chess960 = true;
            saveSetupSoon();
            Console::instance().add(trf("{} will be the book of the next tournament.", book.name));
        }
        tip(
            TR("Puts it in the Openings of the New tournament page, with its plies, and Chess960 for a book of "
               "Chess960 positions."));
        if (fs::path(book.path).extension() == ".epd") {
            ImGui::SameLine();
            if (ImGui::Button(TR("Run as a test suite…"))) openSuite(book.path);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", TR("Its positions with a best move (bm), moves to avoid (am) or STS points, "
                                           "in the Suites page."));
            }
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(scan.count(ANY_PROBLEM) == 0);
        if (ImGui::Button(TR("Save a cleaned copy…"))) {
            dialogs_.saveFile(
                Dialogs::Purpose::SaveCleanBook, {{"EPD", "epd"}},
                (fs::path(book.path).parent_path() / (fs::path(book.path).stem().string() + "-clean.epd")).string());
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Checkbox(TR("without mirrored positions"), &drop_mirrored_);
        ui::help(
            TR("An EPD book without the illegal positions, the duplicates and the games already over (and the "
               "positions with colours reversed of an earlier one, if ticked), added to the library."));
    }
    if (changed) storage_.saveBooks(books_);

    ImGui::Separator();
    if (book_scan_ && book_scan_index_ == selected_book_) drawBookPositions();
}

void App::drawBookPositions() {
    const auto& scan = *book_scan_;

    ImGui::SetNextItemWidth(px(220));
    const char* show[] = {TR("Every opening"), TR("Those with a problem")};
    if (ImGui::Combo("##book_show", &book_show_, show, 2)) book_rows_dirty_ = true;
    ImGui::SameLine();
    {
        char buffer[256];
        const auto n = std::min(book_search_.size(), sizeof(buffer) - 1);
        std::copy_n(book_search_.data(), n, buffer);
        buffer[n] = 0;
        ImGui::SetNextItemWidth(px(260));
        if (ImGui::InputTextWithHint("##book_search", TR("Search"), buffer, sizeof(buffer))) {
            book_search_     = buffer;
            book_rows_dirty_ = true;
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("Part of a FEN, or moves of a PGN book (e4 e5 Nf3)."));

    if (book_rows_dirty_) {
        book_rows_.clear();
        for (std::size_t i = 0; i < scan.size(); ++i) {
            if (book_show_ == 1 && (scan.flags(i) & ANY_PROBLEM) == 0) continue;
            if (!book_search_.empty()) {
                const auto item  = scan.item(i);
                std::string text = item.fen;
                for (const auto& m : item.san) text += " " + m;
                if (text.find(book_search_) == std::string::npos) continue;
            }
            book_rows_.push_back(static_cast<int>(i));
        }
        book_rows_dirty_ = false;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", trf("{} shown", ui::formatCount(book_rows_.size())).c_str());

    // The list, and the opening chosen on a board.
    const float board = std::min(px(300), ImGui::GetContentRegionAvail().x * 0.4f);
    ImGui::BeginChild("##book_rows", ImVec2(ImGui::GetContentRegionAvail().x - board - px(16), 0));
    constexpr auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV |
                           ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("##book_table", 3, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", 0, ImGui::CalcTextSize("0000000").x);
        ImGui::TableSetupColumn(scan.kind() == tour::BookScan::Kind::Pgn ? TR("Moves") : TR("Position"),
                                ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(TR("Problem"), ImGuiTableColumnFlags_WidthStretch, 0.5f);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(book_rows_.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const int i     = book_rows_[row];
                const auto item = scan.item(static_cast<std::size_t>(i));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Selectable(fmt::format("{}##bi{}", i + 1, i).c_str(), book_item_ == i,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    book_item_ = i;
                }
                ImGui::TableNextColumn();
                std::string text;
                for (const auto& m : item.san) text += (text.empty() ? "" : " ") + m;
                ImGui::TextUnformatted(text.empty() ? item.fen.c_str() : text.c_str());
                ImGui::TableNextColumn();
                if (item.flags) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
                    ImGui::TextUnformatted(problems(item.flags).c_str());
                    ImGui::PopStyleColor();
                }
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##book_board");
    if (book_item_request_ >= 0 && book_item_request_ < static_cast<int>(scan.size())) {
        book_item_         = book_item_request_;
        book_item_request_ = -1;
    }
    if (book_item_ >= 0 && book_item_ < static_cast<int>(scan.size())) {
        const auto item = scan.item(static_cast<std::size_t>(book_item_));
        chess::Board position;
        position.set960(scan.chess960());
        BoardDecor decor;
        decor.eval_white = -1;
        try {
            position.setFen(item.fen);
            drawBoard(atlas_, position, board, decor);
            // A: the opening on the Board page, analysed; not an illegal one.
            const bool legal = tour::legalPosition(position);
            if (legal && (ImGui::IsItemHovered() || ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) &&
                boardKey()) {
                const auto& book = books_[static_cast<std::size_t>(selected_book_)];
                openPositionOnBoard(item.fen, scan.chess960(), trf("{} · opening {}", book.name, book_item_ + 1),
                                    true);
            }
            if (legal && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip("%s", TR("A: analyse it on the Board page"));
        } catch (...) {
        }
        ImGui::TextWrapped("%s", item.fen.c_str());
        if (item.flags) {
            ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
            ImGui::TextWrapped("%s", problems(item.flags).c_str());
            ImGui::PopStyleColor();
        }
    } else {
        ImGui::TextDisabled("%s", TR("Choose an opening to see it."));
    }
    ImGui::EndChild();
}

}  // namespace fcg
