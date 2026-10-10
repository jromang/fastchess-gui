// "Engines": the engine list and the UCI option editor; "Console": fastchess output.

#include <filesystem>

#include <imgui.h>

#include <fmt/include/fmt/core.h>

#include "app.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

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

bool lessCaseInsensitive(const std::string& a, const std::string& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
    });
}

bool containsCaseInsensitive(const std::string& text, const std::string& needle) {
    if (needle.empty()) return true;
    auto lower = [](std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    return lower(text).find(lower(needle)) != std::string::npos;
}

}  // namespace

std::string App::uniqueEngineName(const std::string& base, int ignore_index) const {
    auto taken = [&](const std::string& name) {
        for (int i = 0; i < static_cast<int>(engines_.size()); ++i) {
            if (i != ignore_index && engines_[i].name == name) return true;
        }
        return false;
    };

    const auto root = base.empty() ? std::string("engine") : base;
    if (!taken(root)) return root;
    for (int n = 2;; ++n) {
        const auto candidate = fmt::format("{} ({})", root, n);
        if (!taken(candidate)) return candidate;
    }
}

void App::addEngine(const std::string& path) {
    EngineEntry entry;
    entry.cmd  = path;
    entry.name = uniqueEngineName(fs::path(path).stem().string(), -1);

    engines_.push_back(std::move(entry));
    selected_engine_ = static_cast<int>(engines_.size()) - 1;
    storage_.saveEngines(engines_);

    startProbe(selected_engine_);
}

void App::startProbe(int index) {
    if (index < 0 || index >= static_cast<int>(engines_.size())) return;
    if (probe_.valid()) return;

    probe_index_   = index;
    probe_message_ = TR("Reading the engine…");
    g_probes_in_flight++;

    probe_ = std::async(std::launch::async, [entry = engines_[index]] {
        auto result = probeEngine(entry);
        g_probes_in_flight--;
        return result;
    });
}

void App::pollProbe() {
    // Start the next queued probe when idle, but never while a run is being set up.
    if (!probe_.valid() && !probe_queue_.empty() && !run_.running()) {
        const auto name = probe_queue_.front();
        probe_queue_.erase(probe_queue_.begin());
        for (int i = 0; i < static_cast<int>(engines_.size()); ++i) {
            if (engines_[i].name == name) {
                startProbe(i);
                break;
            }
        }
    }

    if (!probe_.valid()) return;
    if (probe_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;

    const auto result = probe_.get();
    const int index   = probe_index_;
    probe_index_      = -1;

    if (index < 0 || index >= static_cast<int>(engines_.size())) return;
    auto& e = engines_[index];

    if (!result.ok) {
        probe_message_ = trf("Failed: {}", result.error);
        return;
    }

    const bool first_probe = e.id_name.empty();

    e.id_name     = result.id_name;
    e.id_author   = result.id_author;
    e.uci_options = result.options;
    pruneOptions(e);

    // A freshly added engine takes the name it reports.
    if (first_probe && !e.id_name.empty() && e.name == uniqueEngineName(fs::path(e.cmd).stem().string(), index)) {
        const auto old = e.name;
        e.name         = uniqueEngineName(e.id_name, index);
        if (setup_.engine_a == old) setup_.engine_a = e.name;
        if (setup_.engine_b == old) setup_.engine_b = e.name;
    }

    probe_message_ = trf("{} options read.", e.uci_options.size());
    storage_.saveEngines(engines_);
}

void App::drawEngines() {
    if (!ImGui::Begin(TR("Engines###engines"))) {
        ImGui::End();
        return;
    }

    // List on the left.
    const float list_w = 220.0f * ImGui::GetFontSize() / 16.0f;
    ImGui::BeginChild("##engine_list", ImVec2(list_w, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);

    if (ImGui::Button(TR("Add…"))) {
#ifdef _WIN32
        dialogs_.openFile(Dialogs::Purpose::AddEngine, {{TR("Programs"), "exe"}, {TR("All files"), "*"}});
#else
        dialogs_.openFile(Dialogs::Purpose::AddEngine);
#endif
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(selected_engine_ < 0);
    if (ImGui::Button(TR("Duplicate")) && selected_engine_ >= 0) {
        auto copy = engines_[selected_engine_];
        copy.name = uniqueEngineName(copy.name, -1);
        engines_.push_back(copy);
        selected_engine_ = static_cast<int>(engines_.size()) - 1;
        storage_.saveEngines(engines_);
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Remove")) && selected_engine_ >= 0) ImGui::OpenPopup(TR("Remove the engine?###remove"));
    ImGui::EndDisabled();

    if (ImGui::BeginPopupModal(TR("Remove the engine?###remove"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (selected_engine_ >= 0) {
            ImGui::Text(TR("Remove “%s” from the list? The file is not deleted."),
                        engines_[selected_engine_].name.c_str());
        }
        if (ImGui::Button(TR("Remove##confirm")) && selected_engine_ >= 0) {
            engines_.erase(engines_.begin() + selected_engine_);
            selected_engine_ = std::min(selected_engine_, static_cast<int>(engines_.size()) - 1);
            storage_.saveEngines(engines_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(TR("Cancel"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::Separator();
    for (int i = 0; i < static_cast<int>(engines_.size()); ++i) {
        const auto& e = engines_[i];
        std::string tag;
        if (e.name == setup_.engine_a) tag = "  [A]";
        if (e.name == setup_.engine_b) tag += "  [B]";
        const auto label = fmt::format("{}{}##engine{}", e.name, tag, i);
        if (ImGui::Selectable(label.c_str(), selected_engine_ == i)) selected_engine_ = i;
    }
    if (engines_.empty()) ImGui::TextDisabled("%s", TR("No engine yet. Click Add."));

    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("##engine_detail");
    if (selected_engine_ < 0 || selected_engine_ >= static_cast<int>(engines_.size())) {
        ImGui::TextDisabled("%s", TR("Choose an engine in the list."));
        ImGui::EndChild();
        ImGui::End();
        return;
    }

    auto& e            = engines_[selected_engine_];
    bool changed       = false;
    const bool in_use  = run_.running() && (e.name == run_.info().engine_a || e.name == run_.info().engine_b);
    const float label  = 150.0f * ImGui::GetFontSize() / 16.0f;
    const float browse = ImGui::CalcTextSize("…").x + ImGui::GetStyle().FramePadding.x * 4;

    ImGui::BeginDisabled(in_use);

    ui::label(TR("Name"), label);
    {
        auto name = e.name;
        if (editText("##name", name)) {
            const auto unique = uniqueEngineName(name, selected_engine_);
            if (setup_.engine_a == e.name) setup_.engine_a = unique;
            if (setup_.engine_b == e.name) setup_.engine_b = unique;
            e.name  = unique;
            changed = true;
            saveSetupSoon();
        }
    }

    ui::label(TR("Program"), label);
    changed |= editText("##cmd", e.cmd, ImGui::GetContentRegionAvail().x - browse - ImGui::GetStyle().ItemSpacing.x);
    ImGui::SameLine();
    if (ImGui::Button("…##cmd_browse")) dialogs_.openFile(Dialogs::Purpose::EngineCommand, {}, e.cmd);

    ui::label(TR("Arguments"), label);
    changed |= editText("##args", e.args);

    ui::label(TR("Working directory"), label);
    changed |= editText("##dir", e.dir, ImGui::GetContentRegionAvail().x - browse - ImGui::GetStyle().ItemSpacing.x);
    ImGui::SameLine();
    if (ImGui::Button("…##dir_browse")) dialogs_.openFolder(Dialogs::Purpose::EngineDir, engineWorkingDir(e));
    if (e.dir.empty()) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + label);
        ImGui::TextDisabled("%s", TR("Empty: the program's directory."));
    }

    changed |= ImGui::Checkbox(TR("Restart the engine for every game"), &e.restart);

    ImGui::EndDisabled();
    if (in_use) ImGui::TextDisabled("%s", TR("Engine used by the running match: changes are locked."));

    ImGui::Separator();
    if (!e.id_name.empty()) {
        ImGui::Text("%s", e.id_name.c_str());
        if (!e.id_author.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled(TR("by %s"), e.id_author.c_str());
        }
    }

    ImGui::BeginDisabled(probe_.valid());
    if (ImGui::Button(probe_.valid() ? TR("Reading…") : TR("Read the UCI options again"))) startProbe(selected_engine_);
    ImGui::EndDisabled();
    if (!probe_message_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", probe_message_.c_str());
    }

    if (!e.uci_options.empty()) {
        ImGui::SameLine();
        char filter[256];
        const auto n = std::min(option_filter_.size(), sizeof(filter) - 1);
        std::copy_n(option_filter_.data(), n, filter);
        filter[n] = 0;
        ImGui::SetNextItemWidth(160.0f * ImGui::GetFontSize() / 16.0f);
        if (ImGui::InputTextWithHint("##option_filter", "filtrer", filter, sizeof(filter))) option_filter_ = filter;
        ImGui::SameLine();
        if (ImGui::Button(TR("All to default"))) {
            e.options.clear();
            changed = true;
        }
    }

    // Options: Threads and Hash first, then alphabetical.
    std::vector<int> order;
    for (int i = 0; i < static_cast<int>(e.uci_options.size()); ++i) {
        if (containsCaseInsensitive(e.uci_options[i].name, option_filter_)) order.push_back(i);
    }
    auto rank = [&](int i) {
        const auto& n = e.uci_options[i].name;
        return n == "Threads" ? 0 : n == "Hash" ? 1 : 2;
    };
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        if (rank(a) != rank(b)) return rank(a) < rank(b);
        return lessCaseInsensitive(e.uci_options[a].name, e.uci_options[b].name);
    });

    ImGui::BeginDisabled(in_use);
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV |
                                      ImGuiTableFlags_SizingStretchProp;
    if (!e.uci_options.empty() && ImGui::BeginTable("##options", 4, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(TR("Option"), ImGuiTableColumnFlags_WidthStretch, 0.35f);
        ImGui::TableSetupColumn(TR("Value"), ImGuiTableColumnFlags_WidthStretch, 0.40f);
        ImGui::TableSetupColumn(TR("Default"), ImGuiTableColumnFlags_WidthStretch, 0.20f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
        ImGui::TableHeadersRow();

        for (int i : order) {
            const auto& opt     = e.uci_options[i];
            const auto* current = e.option(opt.name);
            const bool modified = current != nullptr;
            auto value          = modified ? *current : opt.default_value;

            ImGui::TableNextRow();
            ImGui::PushID(i);

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            if (modified) ImGui::PushStyleColor(ImGuiCol_Text, ui::ACCENT);
            ImGui::TextUnformatted(opt.name.c_str());
            if (modified) ImGui::PopStyleColor();

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            bool edited = false;

            if (opt.type == "spin") {
                long long v = 0, lo = 0, hi = 0;
                try {
                    v  = std::stoll(value);
                    lo = opt.min.empty() ? LLONG_MIN : std::stoll(opt.min);
                    hi = opt.max.empty() ? LLONG_MAX : std::stoll(opt.max);
                } catch (...) {
                }
                if (ImGui::InputScalar("##v", ImGuiDataType_S64, &v)) {
                    v      = std::clamp(v, lo, hi);
                    value  = std::to_string(v);
                    edited = true;
                }
                if (ImGui::IsItemHovered() && (!opt.min.empty() || !opt.max.empty())) {
                    ImGui::SetTooltip("%s … %s", opt.min.c_str(), opt.max.c_str());
                }
            } else if (opt.type == "check") {
                bool v = value == "true";
                if (ImGui::Checkbox("##v", &v)) {
                    value  = v ? "true" : "false";
                    edited = true;
                }
            } else if (opt.type == "combo") {
                if (ImGui::BeginCombo("##v", value.c_str())) {
                    for (const auto& var : opt.vars) {
                        if (ImGui::Selectable(var.c_str(), var == value)) {
                            value  = var;
                            edited = true;
                        }
                    }
                    ImGui::EndCombo();
                }
            } else if (opt.type == "button") {
                bool v = value == "true";
                if (ImGui::Checkbox(TR("send at startup##v"), &v)) {
                    value  = v ? "true" : "false";
                    edited = true;
                }
            } else {
                edited = editText("##v", value);
            }

            if (edited) {
                if (opt.type == "button" ? value != "true" : value == opt.default_value) {
                    e.clearOption(opt.name);
                } else {
                    e.setOption(opt.name, value);
                }
                changed = true;
            }

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", opt.type == "button" ? TR("button") : opt.default_value.c_str());

            ImGui::TableNextColumn();
            if (modified && ImGui::SmallButton("x")) {
                e.clearOption(opt.name);
                changed = true;
            }
            if (modified && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("Back to the default value"));

            ImGui::PopID();
        }
        ImGui::EndTable();
    } else if (e.uci_options.empty()) {
        ImGui::TextDisabled("%s", TR("No options yet: click Read the UCI options again."));
    }
    ImGui::EndDisabled();

    // Options set on an engine that no longer reports them would make fastchess warn at every game.
    if (!e.options.empty() && !e.uci_options.empty()) {
        for (const auto& [k, v] : e.options) {
            bool known = false;
            for (const auto& o : e.uci_options) known |= o.name == k;
            if (!known)
                ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.3f, 1), TR("Option unknown to the engine: %s = %s"),
                                   k.c_str(), v.c_str());
        }
    }

    if (changed) storage_.saveEngines(engines_);

    ImGui::EndChild();
    ImGui::End();
}

void App::drawConsole() {
    if (!ImGui::Begin(TR("Console###console"))) {
        ImGui::End();
        return;
    }

    Console::instance().copy(console_lines_, console_version_);

    if (ImGui::SmallButton(TR("Clear"))) Console::instance().clear();
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Copy"))) {
        std::string all;
        for (const auto& l : console_lines_) all += l.text + "\n";
        ImGui::SetClipboardText(all.c_str());
    }
    ImGui::SameLine();
    ImGui::Checkbox(TR("Follow"), &console_follow_);

    ImGui::BeginChild("##console", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(console_lines_.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto& l = console_lines_[i];
            if (l.warning) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.65f, 0.3f, 1));
            ImGui::TextUnformatted(l.text.c_str());
            if (l.warning) ImGui::PopStyleColor();
        }
    }
    if (console_follow_) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    ImGui::End();
}

}  // namespace fcg
