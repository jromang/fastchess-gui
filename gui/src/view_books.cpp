// "Books": the library of opening books, and the openings of the book chosen, with what is wrong with them.

#include <algorithm>
#include <filesystem>

#include <imgui.h>

#include <fmt/include/fmt/format.h>

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
    ImGui::BeginChild("##book_list", ImVec2(px(320), 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    if (ImGui::Button(TR("Add…"))) {
        dialogs_.openFile(Dialogs::Purpose::AddBook,
                          {{TR("Books (EPD, PGN, Polyglot)"), "epd;pgn;bin"}, {TR("All files"), "*"}});
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(selected_book_ < 0);
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
        if (ImGui::Selectable(fmt::format("{}##book{}", b.name, i).c_str(), selected_book_ == i)) {
            selected_book_ = i;
            book_item_     = -1;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", missing ? trf("File not found: {}", b.path).c_str() : b.path.c_str());
        if (b.positions > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", ui::formatCount(b.positions).c_str());
        }
    }
    if (books_.empty()) ImGui::TextDisabled("%s", TR("No book yet. Click Add."));
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##book_detail");
    if (selected_book_ < 0 || selected_book_ >= static_cast<int>(books_.size())) {
        ImGui::TextDisabled("%s", TR("Choose a book in the list."));
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
        if (ImGui::InputInt("##book_plies", &book.plies, 0, 0)) {
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
        ImGui::TextDisabled("%s", trf("Reading… {} openings", ui::formatCount(book_reading_->done->load())).c_str());
    } else if (!book_scan_error_.empty() && book_scan_index_ == selected_book_) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
        ImGui::TextWrapped("%s", book_scan_error_.c_str());
        ImGui::PopStyleColor();
    } else if (book_scan_ && book_scan_index_ == selected_book_) {
        const auto& scan    = *book_scan_;
        std::string summary = trf("{} openings", scan.size());
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

        ImGui::Spacing();
        if (ImGui::Button(TR("Use it for the next tournament"))) {
            setup_.book       = book.path;
            setup_.book_plies = book.plies;
            if (scan.chess960()) setup_.chess960 = true;
            saveSetupSoon();
            Console::instance().add(trf("{} will be the book of the next tournament.", book.name));
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
    if (book_item_ >= 0 && book_item_ < static_cast<int>(scan.size())) {
        const auto item = scan.item(static_cast<std::size_t>(book_item_));
        chess::Board position;
        position.set960(scan.chess960());
        BoardDecor decor;
        decor.eval_white = -1;
        try {
            position.setFen(item.fen);
            drawBoard(atlas_, position, board, decor);
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
