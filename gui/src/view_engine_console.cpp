// The engine console of the Engines page: lines typed to an engine of the library and what it answers, in the
// engine's card.

#include <algorithm>
#include <fstream>

#include <imgui.h>

#include <tour/engine_text.hpp>

#include "app.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

int historyCallback(ImGuiInputTextCallbackData* data) {
    auto& c = *static_cast<EngineConsole*>(data->UserData);
    if (data->EventFlag != ImGuiInputTextFlags_CallbackHistory || c.history.empty()) return 0;
    const int n = static_cast<int>(c.history.size());
    if (data->EventKey == ImGuiKey_UpArrow) {
        c.history_pos = c.history_pos < 0 ? n - 1 : std::max(0, c.history_pos - 1);
    } else if (data->EventKey == ImGuiKey_DownArrow) {
        c.history_pos = c.history_pos < 0 || c.history_pos + 1 >= n ? -1 : c.history_pos + 1;
    }
    data->DeleteChars(0, data->BufTextLen);
    if (c.history_pos >= 0) data->InsertChars(0, c.history[static_cast<std::size_t>(c.history_pos)].c_str());
    return 0;
}

}  // namespace

const std::vector<const char*>& consoleCommands(bool winboard) {
    static const std::vector<const char*> uci{"uci",         "isready",          "ucinewgame",  "position startpos",
                                              "go depth 10", "go movetime 1000", "go infinite", "stop",
                                              "quit"};
    static const std::vector<const char*> xboard{"xboard", "protover 2", "new",     "force", "post",
                                                 "go",     "?",          "analyze", "exit",  "quit"};
    return winboard ? xboard : uci;
}

void App::openEngineConsole(int index) {
    if (index < 0 || index >= static_cast<int>(engines_.size())) return;
    const auto& e = engines_[static_cast<std::size_t>(index)];
    // The same engine's console, shown again; another one replaces it (its engine ended).
    if (engine_console_ && engine_console_->engine == e.name && engine_console_->process.running()) {
        engine_console_->shown = true;
        engine_console_->focus = true;
        return;
    }
    const bool raw    = engine_console_ && engine_console_->engine == e.name ? engine_console_->raw : isWinboard(e);
    auto history      = engine_console_ ? std::move(engine_console_->history) : std::vector<std::string>{};
    engine_console_   = std::make_unique<EngineConsole>();
    auto& c           = *engine_console_;
    c.engine          = e.name;
    c.raw             = raw && e.protocol == "xboard";
    c.history         = std::move(history);
    const auto launch = engineLaunch(e, c.raw);
    std::string error;
    const auto note = [&](std::string text) {
        c.log.push_back({EngineProcess::Line::Kind::Note, std::move(text), std::chrono::system_clock::now()});
    };
    note(fmt::format("{} {}", launch.program, launch.args));
    if (!c.process.start(launch, error)) {
        note(trf("Cannot start {}: {}", e.name, toUtf8(error)));
        return;
    }
    // A UCI engine is started as a game starts it: uci, then the library's option values once it answered.
    if (!c.raw) {
        c.options     = tour::forEngine(e.options);
        c.options_due = !c.options.empty();
        (void)c.process.send("uci");
    }
}

void App::closeEngineConsole() {
    if (!engine_console_) return;
    engine_console_->process.stop(std::chrono::milliseconds(500));
    engine_console_.reset();
}

void App::pollEngineConsole() {
    if (!engine_console_) return;
    auto& c          = *engine_console_;
    const bool alive = c.process.running();
    for (auto& line : c.process.take()) {
        const bool uciok = line.kind == EngineProcess::Line::Kind::Out && line.text.rfind("uciok", 0) == 0;
        c.log.push_back(std::move(line));
        if (uciok && c.options_due) {
            c.options_due = false;
            for (const auto& [name, value] : c.options) {
                const auto* entry = [&]() -> const EngineEntry* {
                    for (const auto& e : engines_)
                        if (e.name == c.engine) return &e;
                    return nullptr;
                }();
                // A button is pressed only when its value says so.
                bool button = false;
                if (entry) {
                    for (const auto& o : entry->uci_options) button |= o.name == name && o.type == "button";
                }
                if (button && value != "true") continue;
                (void)c.process.send(button ? "setoption name " + name : "setoption name " + name + " value " + value);
            }
        }
    }
    if (!alive && c.process.started()) {
        c.process.stop(std::chrono::milliseconds(0), "");
        c.log.push_back({EngineProcess::Line::Kind::Note, TR("The engine ended."), std::chrono::system_clock::now()});
    }
    // A very long session keeps its last lines.
    if (c.log.size() > 50000) c.log.erase(c.log.begin(), c.log.begin() + 10000);
}

void App::drawEngineConsole(const EngineEntry& e) {
    auto& c          = *engine_console_;
    const bool alive = c.process.running();

    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.1f);
    ImGui::TextUnformatted(trf("Console · {}", e.name).c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", c.raw ? TR("WinBoard, its own protocol") : "UCI");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", c.raw ? TR("The engine's own protocol (xboard), without the translation to UCI a "
                                           "tournament uses.")
                                      : TR("Started with the library's program, arguments, working directory and "
                                           "option values (sent once it answers uciok)."));
    }
    // The buttons on the right.
    const char* card    = TR("Settings");
    const char* restart = alive ? TR("Restart") : TR("Start");
    const char* close   = TR("Close");
    const auto width    = [](const char* label) {
        return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x;
    };
    float right = width(card) + width(restart) + width(close);
    if (e.protocol == "xboard")
        right += ImGui::GetFrameHeight() + ImGui::CalcTextSize(TR("Through the adapter")).x +
                 ImGui::GetStyle().ItemSpacing.x * 2;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(12), ImGui::GetContentRegionMax().x - right));
    if (e.protocol == "xboard") {
        bool adapter = !c.raw;
        if (ImGui::Checkbox(TR("Through the adapter"), &adapter)) {
            c.raw = !adapter;
            c.process.stop(std::chrono::milliseconds(300));
            openEngineConsole(selected_engine_);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", TR("Speak UCI to it through the translation a tournament uses, rather than its "
                                       "own protocol. The engine starts again."));
        }
        ImGui::SameLine();
    }
    if (ImGui::Button(card)) c.shown = false;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("The engine's settings; the console stays open."));
    ImGui::SameLine();
    if (ImGui::Button(restart)) {
        c.process.stop(std::chrono::milliseconds(300));
        openEngineConsole(selected_engine_);
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button(close)) {
        closeEngineConsole();
        return;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("Ends the engine."));

    if (run_->running()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
        ImGui::TextWrapped("%s", TR("A tournament is running: what this engine searches takes cores from its games."));
        ImGui::PopStyleColor();
    }

    // The usual commands, sent as they are.
    ImGui::BeginDisabled(!alive);
    for (const char* command : consoleCommands(c.raw)) {
        ui::sameLineOrWrap(command);
        if (ImGui::SmallButton(command)) {
            (void)c.process.send(command);
            c.follow = true;
        }
    }
    // The position of the Board page.
    {
        const char* label = TR("Board position");
        ui::sameLineOrWrap(label);
        if (ImGui::SmallButton(label)) {
            (void)c.process.send(c.raw ? "setboard " + boardFen() : "position fen " + boardFen());
            c.follow = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", boardFen().c_str());
    }
    ImGui::EndDisabled();

    // The log, newest at the bottom.
    const float bottom = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    ImGui::BeginChild("##console_log", ImVec2(0, -bottom), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    {
        const float time_w = ImGui::CalcTextSize("00:00:00.000").x + px(10);
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(c.log.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const auto& line  = c.log[static_cast<std::size_t>(i)];
                const float start = ImGui::GetCursorPosX();
                ImGui::TextDisabled("%s", clockTime(line.when).c_str());
                ImGui::SameLine(start + time_w);
                using Kind = EngineProcess::Line::Kind;
                switch (line.kind) {
                    case Kind::Sent:
                        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "> %s", line.text.c_str());
                        break;
                    case Kind::Err:
                        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "! %s", line.text.c_str());
                        break;
                    case Kind::Note:
                        ImGui::TextDisabled("# %s", line.text.c_str());
                        break;
                    case Kind::Out:
                        ImGui::TextUnformatted(line.text.c_str());
                        break;
                }
            }
        }
        if (c.follow) ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();

    // The line typed, Enter sends it; Up and Down go through those sent before.
    const char* send  = TR("Send");
    const float tools = ImGui::CalcTextSize(send).x + ImGui::CalcTextSize(TR("Copy")).x +
                        ImGui::CalcTextSize(TR("Save…")).x + ImGui::CalcTextSize(TR("Clear")).x +
                        ImGui::CalcTextSize(TR("Follow")).x + ImGui::GetFrameHeight() +
                        ImGui::GetStyle().FramePadding.x * 8 + ImGui::GetStyle().ItemSpacing.x * 6;
    char buffer[4096];
    const auto n = std::min(c.input.size(), sizeof(buffer) - 1);
    std::copy_n(c.input.data(), n, buffer);
    buffer[n] = 0;
    ImGui::BeginDisabled(!alive);
    if (c.focus && alive) {
        ImGui::SetKeyboardFocusHere();
        c.focus = false;
    }
    ImGui::SetNextItemWidth(std::max(px(120), ImGui::GetContentRegionAvail().x - tools));
    bool entered = ImGui::InputTextWithHint("##console_input", TR("A command for the engine"), buffer, sizeof(buffer),
                                            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory,
                                            historyCallback, &c);
    c.input      = buffer;
    ImGui::SameLine();
    entered |= ImGui::Button(send);
    ImGui::EndDisabled();
    if (entered && alive) {
        (void)c.process.send(c.input);
        rememberCommand(c.history, c.input);
        c.input.clear();
        c.history_pos = -1;
        c.follow      = true;
        c.focus       = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Copy"))) ImGui::SetClipboardText(consoleTranscript(c.log).c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("The whole log, to the clipboard."));
    ImGui::SameLine();
    if (ImGui::Button(TR("Save…"))) {
        dialogs_.saveFile(Dialogs::Purpose::ConsoleLog, {{TR("Text"), "txt;log"}},
                          (storage_.root() / (tour::fileStemOf(e.name) + "-console.txt")).string());
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Clear"))) c.log.clear();
    ImGui::SameLine();
    ImGui::Checkbox(TR("Follow"), &c.follow);
}

void App::saveConsoleLog(const std::string& path) {
    if (!engine_console_) return;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << consoleTranscript(engine_console_->log);
    Console::instance().add(out ? trf("Console log written to {}", path) : trf("Cannot write {}", path), !out);
}

}  // namespace fcg
