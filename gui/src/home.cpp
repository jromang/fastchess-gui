#include "home.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>

#include "i18n.hpp"
#include "memory.hpp"
#include "ui.hpp"

namespace fcg::home {

namespace fs = std::filesystem;

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

void tooltip(const std::string& text) {
    if (text.empty() || !ImGui::BeginTooltip()) return;
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

// Text on the accent colour, as the New tournament button has it.
ImU32 onColour() { return ui::currentTheme() == ui::Theme::Dark ? IM_COL32(11, 18, 32, 255) : IM_COL32_WHITE; }

}  // namespace

std::string formatLabel(tour::Format format) {
    switch (format) {
        case tour::Format::Match:
            return TR("Match");
        case tour::Format::RoundRobin:
            return TR("Round robin");
        case tour::Format::Gauntlet:
            return TR("Gauntlet");
        case tour::Format::Swiss:
            return TR("Swiss");
        case tour::Format::Knockout:
            return TR("Knockout");
    }
    return "";
}

std::string statusLabel(const tour::Summary& s, bool running) {
    if (running) return TR("Running");
    if (!s.finished) return TR("Interrupted");
    if (s.finish_reason == "sprt_h1") return TR("H1 accepted");
    if (s.finish_reason == "sprt_h0") return TR("H0 accepted");
    if (s.finish_reason == "imported") return TR("Imported");
    return TR("Finished");
}

std::string endTime(double seconds) {
    const auto now = std::time(nullptr);
    const auto end = now + static_cast<std::time_t>(seconds);
    std::tm today{}, then{};
#ifdef _WIN32
    localtime_s(&today, &now);
    localtime_s(&then, &end);
#else
    localtime_r(&now, &today);
    localtime_r(&end, &then);
#endif
    char text[32];
    const bool same_day = today.tm_year == then.tm_year && today.tm_yday == then.tm_yday;
    std::strftime(text, sizeof(text), same_day ? "%H:%M" : "%Y-%m-%d %H:%M", &then);
    return text;
}

void sectionTitle(const char* text) {
    ImGui::Spacing();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.1f);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

std::string halfPoints(double points) {
    const auto whole = static_cast<long>(std::floor(points + 1e-9));
    const bool half  = points - static_cast<double>(whole) > 0.25;
    if (whole == 0 && half) return "½";
    return fmt::format("{}{}", whole, half ? "½" : "");
}

std::string resultText(const tour::Summary& s) {
    if (s.played == 0 || s.scores.empty()) return TR("No game played");
    if (s.spec.format == tour::Format::Match && s.scores.size() >= 2) {
        const auto& a    = s.scores[0];
        const auto& b    = s.scores[1];
        const auto score = fmt::format("{} {} – {} {}", a.name, halfPoints(a.points), halfPoints(b.points), b.name);
        if (s.finish_reason == "sprt_h1" || s.finish_reason == "sprt_h0")
            return fmt::format("{} · {}", statusLabel(s, false), score);
        return score;
    }
    const auto& first = s.scores.front();
    if (s.spec.format == tour::Format::Knockout && s.finished) return trf("Winner: {}", first.name);
    return trf("First: {} {}/{}", first.name, halfPoints(first.points), first.games);
}

std::vector<const tour::Summary*> lastFinished(const std::vector<tour::Summary>& list, std::size_t count,
                                               const fs::path& running) {
    std::vector<const tour::Summary*> out;
    for (const auto& s : list) {
        if (out.size() >= count) break;
        if (!s.finished || (!running.empty() && s.dir == running)) continue;
        out.push_back(&s);
    }
    return out;
}

QueueEnd queueEnd(const std::vector<std::optional<double>>& lengths) {
    QueueEnd end;
    for (const auto& length : lengths) {
        end.count++;
        if (length)
            end.seconds += *length;
        else
            end.unknown++;
    }
    return end;
}

std::optional<DiskSpace> diskSpace(const fs::path& dir) {
    if (dir.empty()) return std::nullopt;
    std::error_code ec;
    auto path = fs::absolute(dir, ec);
    if (ec) return std::nullopt;
    // A folder not made yet is written on the drive of the closest one that exists.
    while (!fs::exists(path, ec)) {
        const auto parent = path.parent_path();
        if (parent.empty() || parent == path) return std::nullopt;
        path = parent;
    }
    const auto space = fs::space(path, ec);
    if (ec || space.capacity == 0) return std::nullopt;
    return DiskSpace{dir, space.available, space.capacity};
}

bool diskLow(std::uintmax_t available, std::uintmax_t capacity) {
    constexpr std::uintmax_t GB = std::uintmax_t{1} << 30;
    return available < GB || (capacity < 500 * GB && available < capacity / 50);
}

Notice diskNotice(const DiskSpace& disk) {
    Notice n;
    n.level = disk.available < (std::uintmax_t{1} << 28) ? Notice::Level::Error : Notice::Level::Warning;
    n.text  = trf("Only {} free of {} on the drive of {}: a tournament stops when its games cannot be written.",
                  formatMemory(disk.available >> 20), formatMemory(disk.capacity >> 20), disk.dir.string());
    n.fixes = {openFolderFix(disk.dir.string()), changeTournamentsFolderFix()};
    return n;
}

void drawResourceBars(const std::vector<ResourceBar>& bars) {
    const auto& style = ImGui::GetStyle();
    float title_w = 0, caption_w = 0;
    for (const auto& b : bars) {
        title_w   = std::max(title_w, ImGui::CalcTextSize(b.title.c_str()).x);
        caption_w = std::max(caption_w, ImGui::CalcTextSize(b.caption.c_str()).x);
    }
    const float h        = ImGui::GetFrameHeight();
    const float rounding = std::min(style.FrameRounding, h * 0.5f);
    const float pad      = style.FramePadding.x;

    for (std::size_t i = 0; i < bars.size(); ++i) {
        const auto& b = bars[i];
        ImGui::PushID(static_cast<int>(i));
        const float x0 = ImGui::GetCursorPosX();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(b.title.c_str());
        if (ImGui::IsItemHovered()) tooltip(b.tooltip);
        ImGui::SameLine(x0 + title_w + style.ItemSpacing.x * 2);

        const float width = std::max(px(120), ImGui::GetContentRegionAvail().x - caption_w - style.ItemSpacing.x);
        const ImVec2 p0   = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##bar", ImVec2(width, h));
        const bool hovered = ImGui::IsItemHovered();
        const float mouse  = ImGui::GetIO().MousePos.x;
        auto* dl           = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p0, ImVec2(p0.x + width, p0.y + h), ImGui::GetColorU32(ImGuiCol_FrameBg), rounding);

        // Each share from the left, in proportion; what is beyond the capacity is cut at the end.
        const double capacity = std::max(1e-9, b.capacity);
        float x               = p0.x;
        const float end       = p0.x + width;
        const auto label      = [&](const std::string& text, float from, float to, ImU32 col) {
            const auto size = ImGui::CalcTextSize(text.c_str());
            if (text.empty() || size.x + pad * 2 > to - from) return;
            dl->AddText(ImVec2(from + pad, p0.y + (h - size.y) * 0.5f), col, text.c_str());
        };
        for (std::size_t k = 0; k < b.shares.size() && x < end - 0.5f; ++k) {
            const auto& s  = b.shares[k];
            const float to = std::min(end, x + static_cast<float>(std::max(0.0, s.amount) / capacity * width));
            if (to - x < 0.5f) continue;
            ImDrawFlags flags = (x <= p0.x + 0.5f ? ImDrawFlags_RoundCornersLeft : 0) |
                                (to >= end - 0.5f ? ImDrawFlags_RoundCornersRight : 0);
            if (flags == 0) flags = ImDrawFlags_RoundCornersNone;
            dl->AddRectFilled(ImVec2(x, p0.y), ImVec2(to, p0.y + h), s.colour, rounding, flags);
            label(s.label, x, to, onColour());
            if (hovered && mouse >= x && mouse < to) tooltip(s.tooltip.empty() ? s.label : s.tooltip);
            x = to;
        }
        if (x < end - 0.5f) {
            label(b.free, x, end, ImGui::GetColorU32(ImGuiCol_TextDisabled));
            if (hovered && mouse >= x) tooltip(b.free);
        }

        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", b.caption.c_str());
        if (ImGui::IsItemHovered()) tooltip(b.tooltip);
        ImGui::PopID();
    }
}

CardAction drawRunCard(const RunCard& c, float width) {
    CardAction action = CardAction::None;
    ImGui::PushID(c.id.c_str());
    ui::beginCard("run_card", ImVec2(width, 0));

    // Its name, and its state at the right.
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.1f);
    ImGui::TextUnformatted(c.name.c_str());
    ImGui::PopFont();
    {
        const float w = ImGui::CalcTextSize(c.state.c_str()).x;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - w));
        ImGui::PushStyleColor(ImGuiCol_Text, c.warn_state ? ui::WARN : ui::ACCENT);
        ImGui::TextUnformatted(c.state.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::TextDisabled("%s", c.format.c_str());
    if (c.weight) {
        const auto weight = trf("weight {}", *c.weight);
        const float w     = ImGui::CalcTextSize(weight.c_str()).x;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - w));
        ImGui::TextDisabled("%s", weight.c_str());
    }

    // The games played of those planned, and when it ends.
    if (c.total > 0) {
        ImGui::ProgressBar(std::min(1.0f, static_cast<float>(c.played) / static_cast<float>(c.total)),
                           ImVec2(-FLT_MIN, px(6)), "");
    }
    auto games = c.total > 0 ? trf("{} / {} games", c.played, c.total) : trn("{} game", "{} games", c.played, c.played);
    if (c.playing > 0) games += " · " + trn("{} game being played", "{} games being played", c.playing, c.playing);
    ImGui::TextUnformatted(games.c_str());
    {
        const float w = ImGui::CalcTextSize(c.end.c_str()).x;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(16), ImGui::GetContentRegionMax().x - w));
        ImGui::TextDisabled("%s", c.end.c_str());
    }

    // Who leads, or where the SPRT stands.
    if (!c.leader.empty()) ImGui::TextUnformatted(c.leader.c_str());
    if (!c.detail.empty()) ImGui::TextDisabled("%s", c.detail.c_str());

    ImGui::Spacing();
    if (ImGui::Button(TR("Open"))) action = CardAction::Open;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("Its workspace: standings, live games, schedule"));
    ImGui::SameLine();
    if (c.paused) {
        if (ui::iconButton("resume", ui::Icon::Play, TR("Resume"), c.can_resume)) action = CardAction::Resume;
    } else {
        if (ui::iconButton("pause", ui::Icon::Pause, TR("Pause"), c.can_pause)) action = CardAction::Pause;
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", TR("Pause: no new pair starts. Running games, and the second game of their "
                                       "pair,\nplay to the end (pair statistics stay complete)."));
        }
    }

    ui::endCard();
    ImGui::PopID();
    return action;
}

}  // namespace fcg::home
