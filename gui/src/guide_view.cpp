#include "guide_view.hpp"

#include <algorithm>
#include <cfloat>

#include <imgui_internal.h>
#include <stb_image.h>  // compiled in engine_logos.cpp

#include <fmt/include/fmt/format.h>

#include "i18n.hpp"
#include "renderer.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float em() { return ImGui::GetFontSize(); }

constexpr float DEFAULT_EM = 28.0f;
constexpr float MIN_EM     = 18.0f;

ImFont* fontFor(uint8_t style, bool heading) {
    if (style & md::Code) return ui::monoFont();
    if (heading || (style & md::Bold)) return ui::titleFont() ? ui::titleFont() : ImGui::GetIO().FontDefault;
    if (style & md::Italic) return ui::italicFont();
    return ImGui::GetIO().FontDefault;
}

float sizeFor(uint8_t style, float size) { return (style & md::Code) ? size * 0.92f : size; }

float keyPad(float size) { return size * 0.3f; }

float measureText(std::string_view text, uint8_t style, float size, bool heading) {
    ImFont* font = fontFor(style, heading);
    float w      = font->CalcTextSizeA(sizeFor(style, size), FLT_MAX, 0.0f, text.data(), text.data() + text.size()).x;
    if (style & md::Key) w += keyPad(size) * 2.0f;
    return w;
}

float lineHeight(float size, bool heading) { return heading ? size * 1.3f : size * 1.5f; }

float headingSize(int level) {
    const float base = ImGui::GetStyle().FontSizeBase;
    return level <= 1 ? base * 1.6f : level == 2 ? base * 1.35f : level == 3 ? base * 1.12f : base;
}

// The text of a section's Markdown as the search reads it: without the marks of emphasis, code and links.
std::string searchText(std::string_view markdown) {
    std::string out;
    out.reserve(markdown.size());
    for (std::size_t i = 0; i < markdown.size(); ++i) {
        const char c = markdown[i];
        if (c == '*' || c == '`' || c == '#' || c == '|') continue;
        if (c == '<') {
            // <kbd>, </kbd>, <!-- … -->: left out.
            const auto end = markdown.find('>', i);
            if (end != std::string_view::npos && end - i < 200) {
                i = end;
                continue;
            }
        }
        if (c == ']' && i + 1 < markdown.size() && markdown[i + 1] == '(') {
            const auto end = markdown.find(')', i);
            if (end != std::string_view::npos) {
                i = end;
                continue;
            }
        }
        if (c == '[') continue;
        out += (c == '\n' || c == '\t') ? ' ' : c;
    }
    return out;
}

// `text` cut to about `before` bytes before `at` and `after` bytes after `end`, on character boundaries.
std::string snippet(const std::string& text, std::size_t at, std::size_t end, std::size_t before, std::size_t after) {
    std::size_t b = at > before ? at - before : 0;
    std::size_t e = std::min(text.size(), end + after);
    while (b > 0 && (static_cast<unsigned char>(text[b]) & 0xC0) == 0x80) --b;
    while (e < text.size() && (static_cast<unsigned char>(text[e]) & 0xC0) == 0x80) ++e;
    std::string out = text.substr(b, e - b);
    // Spaces collapsed.
    std::string tidy;
    for (const char c : out) {
        if (c == ' ' && !tidy.empty() && tidy.back() == ' ') continue;
        tidy += c;
    }
    while (!tidy.empty() && tidy.front() == ' ') tidy.erase(tidy.begin());
    return (b > 0 ? "\xE2\x80\xA6" : "") + tidy + (e < text.size() ? "\xE2\x80\xA6" : "");
}

}  // namespace

GuideView::~GuideView() {
    for (auto& [src, p] : pictures_)
        if (p.texture != ImTextureID_Invalid) render::destroyTexture(p.texture);
}

void GuideView::open(const std::string& id) {
    open_ = true;
    navigate(id, true);
}

void GuideView::toggle(const std::string& id) {
    if (open_ && id == id_ && !searching_) {
        close();
    } else {
        open(id);
    }
}

void GuideView::close() { open_ = false; }

void GuideView::search(const std::string& query) {
    open_ = true;
    std::snprintf(query_, sizeof(query_), "%s", query.c_str());
    searching_    = query_[0] != '\0';
    focus_search_ = true;
    runSearch();
}

float GuideView::width(float available) const {
    if (!open_) return 0.0f;
    const float wanted = (width_em > 0 ? width_em : DEFAULT_EM) * em();
    const float most   = available * 0.6f;
    return std::max(std::min(wanted, most), std::min(MIN_EM * em(), most));
}

void GuideView::navigate(const std::string& id, bool record) {
    if (record && !id_.empty() && id_ != id) {
        back_.push_back(id_);
        forward_.clear();
    }
    id_        = id;
    scroll_    = 3;
    flash_     = ImGui::GetTime() + 1.6;
    searching_ = false;
}

void GuideView::refreshLanguage() {
    const auto code = guide::currentCode();
    if (code == code_) return;
    code_ = code;
    sections_.clear();
    entries_.clear();
    layouts_.clear();
    results_for_.clear();
    runSearch();
}

const GuideView::Section& GuideView::section(std::size_t english_index) {
    if (const auto it = sections_.find(english_index); it != sections_.end()) return it->second;
    Section s;
    const auto shown = guide::shown(code_, english_index);
    if (shown.doc && shown.heading) {
        s.fallback = shown.fallback;
        s.doc      = shown.doc;
        s.title    = shown.heading->title;
        if (const auto* en = guide::doc("en")) s.level = en->headings[english_index].level;
        s.blocks = md::parse(std::string_view(shown.doc->text).substr(shown.heading->body,
                                                                      shown.heading->end - shown.heading->body));
        for (auto& b : s.blocks) {
            // A key's name is one key cap: no line break inside it.
            const auto keys = [](std::vector<md::Run>& runs) {
                for (auto& r : runs) {
                    if (!(r.style & md::Key)) continue;
                    for (std::size_t at = r.text.find(' '); at != std::string::npos; at = r.text.find(' ', at))
                        r.text.replace(at, 1, "\xC2\xA0");
                }
            };
            keys(b.runs);
            for (std::size_t row = 0; row < b.rows.size(); ++row) {
                for (auto& cell : b.rows[row]) {
                    keys(cell);
                    if (row == 0)
                        for (auto& r : cell) r.style |= md::Bold;
                }
            }
        }
    }
    return sections_.emplace(english_index, std::move(s)).first->second;
}

void GuideView::runSearch() {
    const std::string query = query_;
    if (query == results_for_) return;
    results_for_ = query;
    results_.clear();
    layouts_.clear();
    highlight_ = md::queryWords(query);
    if (highlight_.empty()) return;

    const auto* en = guide::doc("en");
    if (!en) return;
    if (entries_.empty()) {
        for (std::size_t i = 0; i < en->headings.size(); ++i) {
            const auto s = guide::shown(code_, i);
            if (!s.doc || !s.heading) continue;
            entries_.push_back({i, s.heading->title,
                                searchText(std::string_view(s.doc->text).substr(s.heading->body,
                                                                                s.heading->end - s.heading->body))});
        }
    }
    for (const auto& e : entries_) {
        const auto title = md::fold(e.title).text;
        const auto text  = md::fold(e.text).text;
        int score        = 0;
        bool all         = true;
        for (const auto& w : highlight_) {
            const bool in_title = title.find(w) != std::u32string::npos;
            int count           = 0;
            for (auto at = text.find(w); at != std::u32string::npos && count < 20; at = text.find(w, at + w.size()))
                ++count;
            if (!in_title && count == 0) {
                all = false;
                break;
            }
            score += (in_title ? 100 : 0) + count;
        }
        if (!all) continue;
        Result r;
        r.index = e.index;
        r.title = e.title;
        r.score = score;
        const auto found = md::matches(e.text, highlight_);
        r.snippet        = found.empty() ? snippet(e.text, 0, 0, 0, 160)
                                         : snippet(e.text, found.front().first, found.front().second, 60, 140);
        results_.push_back(std::move(r));
    }
    std::stable_sort(results_.begin(), results_.end(),
                     [](const Result& a, const Result& b) { return a.score > b.score; });
}

void GuideView::draw(float width, float height) {
    refreshLanguage();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em() * 0.9f, em() * 0.7f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    ImGui::BeginChild("##guide_panel", ImVec2(width, height), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    hovered_           = hovered;
    auto* dl           = ImGui::GetWindowDrawList();
    dl->AddLine(origin, ImVec2(origin.x, origin.y + height), ui::CARD_BD, std::max(1.0f, em() * 0.06f));

    // Its left edge widens or narrows it.
    {
        const ImVec2 keep = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(origin);
        ImGui::InvisibleButton("##guide_edge", ImVec2(em() * 0.35f, height));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive()) {
            const float right = origin.x + width;
            width_em          = std::max(MIN_EM, (right - ImGui::GetIO().MousePos.x) / em());
            dragging_         = true;
        } else if (dragging_) {
            dragging_ = false;
            if (on_width) on_width(width_em);
        }
        ImGui::SetCursorScreenPos(keep);
    }

    const float inner = ImGui::GetContentRegionAvail().x;
    drawHeader(inner);
    ImGui::Separator();

    ImGui::BeginChild("##guide_body", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_None);
    const float body = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize * 0.25f;
    if (searching_ && query_[0] != '\0') {
        drawResults(body);
    } else {
        drawPage(body);
    }
    ImGui::EndChild();

    // Its keys, while it has the keyboard; the mouse's side buttons while it is under the mouse.
    const auto back = [&] {
        if (back_.empty()) return;
        forward_.push_back(id_);
        id_ = back_.back();
        back_.pop_back();
        scroll_    = 3;
        searching_ = false;
    };
    const auto forward = [&] {
        if (forward_.empty()) return;
        back_.push_back(id_);
        id_ = forward_.back();
        forward_.pop_back();
        scroll_    = 3;
        searching_ = false;
    };
    if ((focused && ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiKey_LeftArrow)) ||
        (hovered && ImGui::IsMouseClicked(3)))
        back();
    if ((focused && ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiKey_RightArrow)) ||
        (hovered && ImGui::IsMouseClicked(4)))
        forward();
    if (focused && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F)) focus_search_ = true;
    if (focused && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) close();

    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void GuideView::drawHeader(float width) {
    // The title and the close button, then the history, the contents, the search and the browser.
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.15f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("Guide"));
    ImGui::PopFont();
    const float side = ImGui::GetFrameHeight();
    ImGui::SameLine(ImGui::GetStyle().WindowPadding.x + width - side);
    if (ui::iconOnlyButton("##guide_close", ui::Icon::Close, TR("Close (F1)"))) close();

    const float spacing = ImGui::GetStyle().ItemSpacing.x * 0.5f;
    if (ui::iconOnlyButton("##guide_back", ui::Icon::Back, TR("Back (Alt+Left)"), !back_.empty())) {
        forward_.push_back(id_);
        id_ = back_.back();
        back_.pop_back();
        scroll_    = 3;
        searching_ = false;
    }
    ImGui::SameLine(0, spacing);
    if (ui::iconOnlyButton("##guide_forward", ui::Icon::Forward, TR("Forward (Alt+Right)"), !forward_.empty())) {
        back_.push_back(id_);
        id_ = forward_.back();
        forward_.pop_back();
        scroll_    = 3;
        searching_ = false;
    }
    ImGui::SameLine(0, spacing);
    if (ui::iconOnlyButton("##guide_contents", ui::Icon::Contents, TR("Contents"))) ImGui::OpenPopup("##guide_toc");
    drawContents();
    ImGui::SameLine(0, spacing);
    ImGui::SetNextItemWidth(std::max(em() * 4.0f, ImGui::GetContentRegionAvail().x - side - spacing));
    if (focus_search_) {
        ImGui::SetKeyboardFocusHere();
        focus_search_ = false;
    }
    if (ImGui::InputTextWithHint("##guide_search", TR("Search the guide"), query_, sizeof(query_))) {
        searching_ = query_[0] != '\0';
        runSearch();
    }
    if (ImGui::IsItemActive()) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            query_[0]  = '\0';
            searching_ = false;
            runSearch();
            ImGui::ClearActiveID();
        } else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !results_.empty()) {
            if (const auto* en = guide::doc("en")) navigate(en->headings[results_.front().index].id, true);
        }
    }
    ImGui::SameLine(0, spacing);
    if (ui::iconOnlyButton("##guide_web", ui::Icon::External, TR("Open in the browser")) && open_url)
        open_url(guide::url(code_, id_.empty() ? guide::topics::Intro : id_));
}

void GuideView::drawContents() {
    const auto* en = guide::doc("en");
    if (!en) return;
    const float w = std::max(em() * 16.0f, ImGui::GetContentRegionAvail().x);
    ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0), ImVec2(w, ImGui::GetMainViewport()->WorkSize.y * 0.7f));
    if (!ImGui::BeginPopup("##guide_toc")) return;
    const int current = en->byId(id_);
    const auto page   = guide::page(current < 0 ? 0 : static_cast<std::size_t>(current));
    for (std::size_t i = 0; i < en->headings.size(); ++i) {
        const auto& h = en->headings[i];
        if (h.level > 3) continue;
        const auto s           = guide::shown(code_, i);
        const std::string text = (s.heading ? s.heading->title : h.title) + "##toc" + std::to_string(i);
        if (h.level == 3) ImGui::Indent(em());
        if (h.level <= 2) ImGui::PushFont(ui::titleFont(), 0.0f);
        const bool here = i >= page.first && i < page.second && (h.level <= 2 || static_cast<int>(i) == current);
        if (ImGui::Selectable(text.c_str(), here)) {
            navigate(h.id, true);
            ImGui::CloseCurrentPopup();
        }
        if (here && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY(0.3f);
        if (h.level <= 2) ImGui::PopFont();
        if (h.level == 3) ImGui::Unindent(em());
    }
    ImGui::EndPopup();
}

void GuideView::drawResults(float width) {
    const auto* en = guide::doc("en");
    if (!en) return;
    if (results_.empty()) {
        ImGui::TextDisabled("%s", TR("No section matches."));
        return;
    }
    ImGui::TextDisabled("%s", trn("{} section found", "{} sections found", results_.size(), results_.size()).c_str());
    ImGui::Spacing();
    const float size = ImGui::GetStyle().FontSizeBase;
    Section none;
    for (std::size_t i = 0; i < results_.size(); ++i) {
        const auto& r = results_[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::PushFont(ui::titleFont(), 0.0f);
        if (ImGui::Selectable(r.title.c_str())) navigate(en->headings[r.index].id, true);
        ImGui::PopFont();
        // Its snippet's layout, kept under the result's address (the layouts are dropped with the results).
        const std::vector<md::Run> runs{md::Run{r.snippet, md::Plain, {}}};
        auto& l = layouts_[&r];
        if (l.width != width || l.size != size) {
            l.lines  = md::wrap(runs, width, [&](std::string_view t, uint8_t st) {
                return measureText(t, st, size, false);
            });
            l.width  = width;
            l.size   = size;
            l.height = static_cast<float>(l.lines.size()) * lineHeight(size, false);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        const float h = drawText(runs, l.lines, ImGui::GetCursorScreenPos(), size, false, none);
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(width, h));
        ImGui::Spacing();
        ImGui::PopID();
    }
}

const GuideView::Layout& GuideView::layout(const md::Block& block, float width, float size) {
    Layout& l = layouts_[&block];
    if (l.width == width && l.size == size) return l;
    l       = Layout{};
    l.width = width;
    l.size  = size;
    const bool heading = block.kind == md::BlockKind::Heading;
    const auto measure = [&](std::string_view t, uint8_t st) { return measureText(t, st, size, heading); };
    switch (block.kind) {
        case md::BlockKind::Heading:
        case md::BlockKind::Paragraph:
        case md::BlockKind::ListItem: {
            const float indent = block.kind == md::BlockKind::ListItem ? em() * 1.4f * static_cast<float>(block.level)
                                                                       : 0.0f;
            l.lines  = md::wrap(block.runs, std::max(em() * 4.0f, width - indent), measure);
            l.height = static_cast<float>(l.lines.size()) * lineHeight(size, heading);
            break;
        }
        case md::BlockKind::Table: {
            // Each column at its natural width when they all fit; else at least its longest word, the room left shared
            // by what each needs more.
            const std::size_t cols = block.rows.empty() ? 0 : block.rows.front().size();
            const float pad        = em() * 0.45f;
            std::vector<float> natural(cols, 0.0f), least(cols, 0.0f);
            for (const auto& row : block.rows) {
                for (std::size_t c = 0; c < row.size() && c < cols; ++c) {
                    float total = 0;
                    for (const auto& r : row[c]) {
                        total += measure(r.text, r.style);
                        std::size_t at = 0;
                        while (at <= r.text.size()) {
                            auto end = r.text.find(' ', at);
                            if (end == std::string::npos) end = r.text.size();
                            least[c] = std::max(least[c],
                                                measure(std::string_view(r.text).substr(at, end - at), r.style));
                            at = end + 1;
                        }
                    }
                    natural[c] = std::max(natural[c], total);
                }
            }
            float sum_natural = 0, sum_least = 0;
            for (std::size_t c = 0; c < cols; ++c) {
                least[c] = std::min(least[c], em() * 12.0f) + pad * 2;
                natural[c] += pad * 2;
                least[c] = std::min(least[c], natural[c]);
                sum_natural += natural[c];
                sum_least += least[c];
            }
            l.columns = natural;
            if (sum_natural > width) {
                const float room = std::max(0.0f, width - sum_least);
                const float more = std::max(1.0f, sum_natural - sum_least);
                for (std::size_t c = 0; c < cols; ++c) l.columns[c] = least[c] + room * (natural[c] - least[c]) / more;
            }
            for (const auto& row : block.rows) {
                std::vector<std::vector<md::Line>> cells;
                float h = lineHeight(size, false);
                for (std::size_t c = 0; c < cols; ++c) {
                    const std::vector<md::Run> empty;
                    const auto& runs = c < row.size() ? row[c] : empty;
                    cells.push_back(md::wrap(runs, std::max(1.0f, l.columns[c] - pad * 2), measure));
                    h = std::max(h, static_cast<float>(cells.back().size()) * lineHeight(size, false));
                }
                l.cells.push_back(std::move(cells));
                l.row_heights.push_back(h + pad * 1.2f);
                l.height += l.row_heights.back();
            }
            break;
        }
        default:
            break;
    }
    return l;
}

float GuideView::drawText(const std::vector<md::Run>& runs, const std::vector<md::Line>& lines, ImVec2 origin,
                          float size, bool heading, const Section& from) {
    auto* dl              = ImGui::GetWindowDrawList();
    const float line_h    = lineHeight(size, heading);
    const bool window_hot = ImGui::IsWindowHovered();
    const ImU32 text_col  = ImGui::GetColorU32(ImGuiCol_Text);
    for (std::size_t k = 0; k < lines.size(); ++k) {
        const float y = origin.y + static_cast<float>(k) * line_h;
        for (const auto& p : lines[k].pieces) {
            if (p.run >= runs.size() || p.end > runs[p.run].text.size() || p.begin > p.end) continue;
            const auto& run       = runs[p.run];
            const std::string_view text(run.text.data() + p.begin, p.end - p.begin);
            ImFont* font          = fontFor(run.style, heading);
            const float sz        = sizeFor(run.style, size);
            const float x         = origin.x + p.x;
            const float text_y    = y + (line_h - sz) * 0.5f;
            const float pad       = (run.style & md::Key) ? keyPad(size) : 0.0f;
            const ImVec2 min(x, y + line_h * 0.08f), max(x + p.width, y + line_h * 0.92f);

            if (run.style & md::Key) {
                dl->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg), size * 0.2f);
                dl->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Border), size * 0.2f);
            } else if (run.style & md::Code) {
                dl->AddRectFilled(ImVec2(min.x - size * 0.1f, min.y), ImVec2(max.x + size * 0.1f, max.y),
                                  ImGui::GetColorU32(ImGuiCol_FrameBg), size * 0.18f);
            }
            if (!highlight_.empty()) {
                for (const auto& [a, b] : md::matches(text, highlight_)) {
                    const float xa = x + pad + font->CalcTextSizeA(sz, FLT_MAX, 0, text.data(), text.data() + a).x;
                    const float xb = x + pad + font->CalcTextSizeA(sz, FLT_MAX, 0, text.data(), text.data() + b).x;
                    dl->AddRectFilled(ImVec2(xa, min.y), ImVec2(xb, max.y), IM_COL32(255, 205, 0, 110),
                                      size * 0.12f);
                }
            }
            ImU32 col = text_col;
            if (run.style & md::Link) {
                col               = ui::ACCENT;
                const bool hot    = window_hot && ImGui::IsMouseHoveringRect(min, max);
                if (hot) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    dl->AddLine(ImVec2(x, text_y + sz), ImVec2(x + p.width, text_y + sz), col,
                                std::max(1.0f, size * 0.06f));
                    if (run.link.rfind("http", 0) == 0) ImGui::SetTooltip("%s", run.link.c_str());
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) link(run.link, from);
                }
            }
            dl->AddText(font, sz, ImVec2(x + pad, text_y), col, text.data(), text.data() + text.size());
        }
    }
    return static_cast<float>(lines.size()) * line_h;
}

void GuideView::link(const std::string& target, const Section& from) {
    if (target.empty()) return;
    if (target[0] == '#') {
        const std::string anchor = target.substr(1);
        for (const guide::Doc* d : {from.doc, guide::doc(code_), guide::doc("en")}) {
            if (!d) continue;
            if (const int h = d->byAnchor(anchor); h >= 0 && !d->headings[static_cast<std::size_t>(h)].id.empty()) {
                navigate(d->headings[static_cast<std::size_t>(h)].id, true);
                return;
            }
        }
        if (const auto* en = guide::doc("en"); en && en->byId(anchor) >= 0) navigate(anchor, true);
        return;
    }
    if (!open_url) return;
    if (target.rfind("http://", 0) == 0 || target.rfind("https://", 0) == 0) {
        open_url(target);
        return;
    }
    // A file of the repository, relative to docs/: on GitHub, at the version that runs.
    std::string path = "docs/" + target;
    for (std::size_t at = path.find("/../"); at != std::string::npos; at = path.find("/../")) {
        const auto start = path.rfind('/', at == 0 ? 0 : at - 1);
        path.erase(start == std::string::npos ? 0 : start + 1, at + 4 - (start == std::string::npos ? 0 : start + 1));
    }
    open_url(fmt::format("https://github.com/jromang/fastchess-gui/blob/v{}/{}", FCG_VERSION, path));
}

ImTextureID GuideView::picture(const std::string& src, int* w, int* h) {
    auto it = pictures_.find(src);
    if (it == pictures_.end()) {
        Picture p;
        if (const auto bytes = guide::picture(src)) {
            int channels       = 0;
            stbi_uc* pixels    = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes->data()),
                                                       static_cast<int>(bytes->size()), &p.w, &p.h, &channels, 4);
            if (pixels) {
                p.texture = render::createTexture(p.w, p.h, pixels);
                stbi_image_free(pixels);
            }
        }
        it = pictures_.emplace(src, p).first;
    }
    *w = it->second.w;
    *h = it->second.h;
    return it->second.texture;
}

float GuideView::drawBlock(const md::Block& block, const Section& from, float width) {
    const float size  = ImGui::GetStyle().FontSizeBase;
    const ImVec2 pos  = ImGui::GetCursorScreenPos();
    const float after = size * 0.55f;
    auto* dl          = ImGui::GetWindowDrawList();

    switch (block.kind) {
        case md::BlockKind::Heading:
        case md::BlockKind::Paragraph:
        case md::BlockKind::ListItem: {
            const bool heading = block.kind == md::BlockKind::Heading;
            const float bsize  = heading ? headingSize(block.level) : size;
            const auto& l      = layout(block, width, bsize);
            const bool item    = block.kind == md::BlockKind::ListItem;
            const float indent = item ? em() * 1.4f * static_cast<float>(block.level) : 0.0f;
            // Items of a list sit close together; the space after the list is the next block's.
            const float space = item ? size * 0.25f : after;
            if (ImGui::IsRectVisible(pos, ImVec2(pos.x + width, pos.y + l.height))) {
                if (item && !block.marker.empty()) {
                    const ImVec2 ms = ImGui::CalcTextSize(block.marker.c_str());
                    dl->AddText(ImVec2(pos.x + indent - em() * 0.5f - ms.x, pos.y + (lineHeight(size, false) - ms.y) * 0.5f),
                                ImGui::GetColorU32(ImGuiCol_TextDisabled), block.marker.c_str());
                }
                drawText(block.runs, l.lines, ImVec2(pos.x + indent, pos.y), bsize, heading, from);
            }
            ImGui::Dummy(ImVec2(width, l.height + space));
            return l.height + space;
        }
        case md::BlockKind::Code: {
            ImFont* mono     = ui::monoFont();
            const float sz   = size * 0.9f;
            const float lh   = sz * 1.35f;
            const float pad  = size * 0.6f;
            std::vector<std::string_view> lines;
            float longest = 0;
            for (std::size_t at = 0; at <= block.text.size();) {
                auto end = block.text.find('\n', at);
                if (end == std::string::npos) end = block.text.size();
                lines.push_back(std::string_view(block.text).substr(at, end - at));
                longest = std::max(longest, mono->CalcTextSizeA(sz, FLT_MAX, 0, lines.back().data(),
                                                                lines.back().data() + lines.back().size())
                                                .x);
                at = end + 1;
            }
            const float inner_h = static_cast<float>(lines.size()) * lh + pad * 2;
            const bool scrolls  = longest + pad * 2 > width;
            const float h       = inner_h + (scrolls ? ImGui::GetStyle().ScrollbarSize : 0.0f);
            dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + h), ImGui::GetColorU32(ImGuiCol_FrameBg), size * 0.3f);
            ImGui::PushID(&block);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
            ImGui::BeginChild("##code", ImVec2(width, h), ImGuiChildFlags_None,
                              ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            const ImVec2 start = ImGui::GetCursorScreenPos();
            auto* cdl          = ImGui::GetWindowDrawList();
            for (std::size_t k = 0; k < lines.size(); ++k) {
                cdl->AddText(mono, sz, ImVec2(start.x + pad, start.y + pad + static_cast<float>(k) * lh),
                             ImGui::GetColorU32(ImGuiCol_Text), lines[k].data(), lines[k].data() + lines[k].size());
            }
            ImGui::Dummy(ImVec2(longest + pad * 2, inner_h));
            ImGui::EndChild();
            ImGui::PopStyleColor();
            // Copy, at its top right.
            const ImVec2 below = ImGui::GetCursorScreenPos();
            const float side   = ImGui::GetFrameHeight();
            ImGui::SetCursorScreenPos(ImVec2(pos.x + width - side - size * 0.2f, pos.y + size * 0.2f));
            if (ui::iconOnlyButton("##copy_code", ui::Icon::Copy, TR("Copy"))) ImGui::SetClipboardText(block.text.c_str());
            ImGui::SetCursorScreenPos(below);
            ImGui::PopID();
            ImGui::Dummy(ImVec2(width, after));
            return h + after;
        }
        case md::BlockKind::Table: {
            const auto& l   = layout(block, width, size);
            const float pad = em() * 0.45f;
            if (ImGui::IsRectVisible(pos, ImVec2(pos.x + width, pos.y + l.height))) {
                float total_w = 0;
                for (const float c : l.columns) total_w += c;
                float y = pos.y;
                for (std::size_t r = 0; r < l.cells.size() && r < block.rows.size(); ++r) {
                    const float rh = l.row_heights[r];
                    if (r == 0)
                        dl->AddRectFilled(ImVec2(pos.x, y), ImVec2(pos.x + total_w, y + rh),
                                          ImGui::GetColorU32(ImGuiCol_TableHeaderBg));
                    float x = pos.x;
                    for (std::size_t c = 0; c < l.cells[r].size() && c < block.rows[r].size(); ++c) {
                        drawText(block.rows[r][c], l.cells[r][c], ImVec2(x + pad, y + pad * 0.6f), size, false, from);
                        x += l.columns[c];
                    }
                    dl->AddLine(ImVec2(pos.x, y + rh), ImVec2(pos.x + total_w, y + rh),
                                ImGui::GetColorU32(ImGuiCol_TableBorderLight));
                    y += rh;
                }
            }
            ImGui::Dummy(ImVec2(width, l.height + after));
            return l.height + after;
        }
        case md::BlockKind::Image: {
            int w = 0, h = 0;
            const ImTextureID texture = picture(block.src, &w, &h);
            if (texture == ImTextureID_Invalid || w <= 0 || h <= 0) {
                ImGui::TextDisabled("%s", block.text.c_str());
                return ImGui::GetTextLineHeightWithSpacing();
            }
            const float shown_w = std::min(width, static_cast<float>(w) / std::max(1.0f, ImGui::GetIO().DisplayFramebufferScale.x));
            const float shown_h = shown_w * static_cast<float>(h) / static_cast<float>(w);
            if (ImGui::IsRectVisible(pos, ImVec2(pos.x + shown_w, pos.y + shown_h)))
                dl->AddImage(ImTextureRef(texture), pos, ImVec2(pos.x + shown_w, pos.y + shown_h));
            ImGui::Dummy(ImVec2(shown_w, shown_h));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", block.text.c_str());
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
            ImGui::TextDisabled("%s", block.text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Dummy(ImVec2(width, after));
            return shown_h + after;
        }
        case md::BlockKind::Rule: {
            ImGui::Separator();
            return ImGui::GetStyle().ItemSpacing.y;
        }
    }
    return 0.0f;
}

void GuideView::drawPage(float width) {
    const auto* en = guide::doc("en");
    if (!en || en->headings.empty()) {
        ImGui::TextDisabled("%s", TR("No section matches."));
        return;
    }
    int target = en->byId(id_);
    if (target < 0) target = 0;
    const auto [first, last] = guide::page(static_cast<std::size_t>(target));
    auto* dl                 = ImGui::GetWindowDrawList();
    const double now         = ImGui::GetTime();

    for (std::size_t i = first; i < last; ++i) {
        const Section& s = section(i);
        if (static_cast<int>(i) == target && scroll_ > 0) {
            ImGui::SetScrollHereY(0.0f);
            --scroll_;
        }
        if (i != first) ImGui::Dummy(ImVec2(width, em() * 0.4f));
        md::Block heading;
        heading.kind  = md::BlockKind::Heading;
        heading.level = s.level;
        heading.runs  = {md::Run{s.title, md::Bold, {}}};
        const ImVec2 top = ImGui::GetCursorScreenPos();
        // The heading's block lives in the section, so that its layout is kept: drawn from a copy here, laid out
        // under the section's address.
        const float hsize = headingSize(s.level);
        auto& l           = layouts_[&s];
        if (l.width != width || l.size != hsize) {
            l        = Layout{};
            l.width  = width;
            l.size   = hsize;
            l.lines  = md::wrap(heading.runs, width, [&](std::string_view t, uint8_t st) {
                return measureText(t, st, hsize, true);
            });
            l.height = static_cast<float>(l.lines.size()) * lineHeight(hsize, true);
        }
        if (static_cast<int>(i) == target && now < flash_) {
            const float alpha = static_cast<float>(std::min(1.0, (flash_ - now) / 0.8));
            ImVec4 c          = ImGui::ColorConvertU32ToFloat4(ui::ACCENT);
            c.w               = 0.28f * alpha;
            dl->AddRectFilled(ImVec2(top.x - em() * 0.3f, top.y - em() * 0.15f),
                              ImVec2(top.x + width, top.y + l.height + em() * 0.15f), ImGui::ColorConvertFloat4ToU32(c),
                              em() * 0.25f);
        }
        drawText(heading.runs, l.lines, top, hsize, true, s);
        ImGui::Dummy(ImVec2(width, l.height + em() * 0.35f));
        if (s.fallback != guide::Fallback::None) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
            ImGui::TextDisabled("%s", s.fallback == guide::Fallback::Missing
                                          ? TR("This section is not translated yet; here it is in English.")
                                          : TR("This section changed after it was translated; here it is in English."));
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
        }
        for (const auto& b : s.blocks) drawBlock(b, s, width);
    }

    // The pages before and after.
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (first > 0) {
        const auto prev = guide::page(first - 1).first;
        const auto s    = guide::shown(code_, prev);
        if (s.heading && ImGui::TextLink(trf("Previous: {}", s.heading->title).c_str()))
            navigate(en->headings[prev].id, true);
    }
    if (last < en->headings.size()) {
        const auto s = guide::shown(code_, last);
        if (first > 0) ImGui::SameLine(0, em() * 1.5f);
        if (s.heading && ImGui::TextLink(trf("Next: {}", s.heading->title).c_str()))
            navigate(en->headings[last].id, true);
    }
    ImGui::Dummy(ImVec2(width, em()));
}

}  // namespace fcg
