// Settings > Appearance: the boards' colours for each theme, the pieces and the move sounds, beside a small board that
// shows them. The engines' logos: chosen on the engine's card, drawn beside its name. The sound of the moves of the
// game followed.

#include <algorithm>
#include <cmath>
#include <system_error>

#include <imgui.h>

#include <fmt/include/fmt/format.h>

#include "app.hpp"
#include "appearance.hpp"
#include "console.hpp"
#include "engine_logos.hpp"
#include "i18n.hpp"
#include "renderer.hpp"
#include "sound.hpp"
#include "ui.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

// The small logo, for names drawn in the text's size.
constexpr int SMALL_LOGO = 48;

// The preview: a Ruy Lopez after 3...a6, the pieces of both sides and the last move on the board.
const chess::Board& previewBoard() {
    static const chess::Board board = [] {
        chess::Board b;
        b.setFen("r1bqkbnr/1ppp1ppp/p1n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 4");
        return b;
    }();
    return board;
}

}  // namespace

ImTextureID App::LogoTexture::texture(float pixels) const {
    return pixels <= SMALL_LOGO && small != ImTextureID_Invalid ? small : large;
}

void App::loadAppearance() {
    const auto settings = storage_.appearance();
    appearance::set(settings);
    // Not in scripted screenshots: nothing listens to them, and no sound device is to be looked for.
    sound::configure(settings.sounds && !options_.scripted, settings.volume);
}

void App::applyAppearance(const appearance::Settings& settings) {
    const auto before = appearance::current();
    if (settings == before) return;
    appearance::set(settings);
    if (settings.pieces != before.pieces ||
        (settings.pieces == appearance::FOLDER && settings.piece_folder != before.piece_folder)) {
        pieces_dirty_ = true;
    }
    if (settings.sounds != before.sounds || settings.volume != before.volume)
        sound::configure(settings.sounds && !options_.scripted, settings.volume);
    appearance_save_due_ = std::chrono::steady_clock::now() + std::chrono::seconds(1);
}

void App::rebuildPieces() {
    pieces_dirty_ = false;
    if (!initGraphics()) Console::instance().add(TR("Cannot draw the pieces."), true);
}

void App::drawBoardPreview(ui::Theme theme, float size) {
    BoardDecor decor;
    decor.eval_white = -1.0f;
    decor.last_move  = chess::Move::make<chess::Move::NORMAL>(chess::Square(chess::File(0), chess::Rank(6)),
                                                              chess::Square(chess::File(0), chess::Rank(5)));
    decor.colours    = appearance::boardColours(theme);
    drawBoard(atlas_, previewBoard(), size, decor);
}

void App::drawAppearanceSettings(float label) {
    auto s       = appearance::current();
    bool changed = false;
    if (!appearance_theme_set_) {
        appearance_theme_     = ui::currentTheme();
        appearance_theme_set_ = true;
    }

    ImGui::PushID("appearance");
    if (scroll_to_appearance_) {
        ImGui::SetScrollHereY(0.0f);
        scroll_to_appearance_ = false;
    }
    ui::pageTitle(TR("Appearance"), guide::topics::Appearance);
    const float preview = std::floor(px(200));

    // In the group, the widgets' column is placed from the group's left: where the other rows have it.
    label -= ImGui::GetCursorPosX();
    ImGui::BeginGroup();
    // The board of one theme at a time: each has its own colours, those of the theme in use on every board.
    ui::label(TR("Board"), label);
    ImGui::BeginGroup();
    if (ImGui::RadioButton(TR("Light theme"), appearance_theme_ == ui::Theme::Light))
        appearance_theme_ = ui::Theme::Light;
    ImGui::SetItemTooltip("%s", TR("The colours of the boards while the theme is light."));
    ImGui::SameLine();
    if (ImGui::RadioButton(TR("Dark theme"), appearance_theme_ == ui::Theme::Dark)) appearance_theme_ = ui::Theme::Dark;
    ImGui::SetItemTooltip("%s", TR("The colours of the boards while the theme is dark."));

    auto& board        = s.board(appearance_theme_);
    const auto colours = appearance::colours(board);
    const auto* preset = appearance::boardPreset(board.preset);
    const auto presets = appearance::boardPresets();
    ImGui::SetNextItemWidth(px(220));
    if (ImGui::BeginCombo("##board_preset", board.preset == appearance::CUSTOM
                                                ? TR("Custom")
                                                : TR(preset ? preset->name : presets.front().name))) {
        for (const auto& p : presets) {
            // Each preset with its two squares, at the right of its row.
            const ImVec2 at   = ImGui::GetCursorScreenPos();
            const float sq    = ImGui::GetTextLineHeight();
            const float right = at.x + ImGui::GetContentRegionAvail().x;
            if (ImGui::Selectable(fmt::format("{}##{}", TR(p.name), p.id).c_str(), board.preset == p.id)) {
                board.preset = p.id;
                board.custom = p.colours;
                changed      = true;
            }
            auto* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(ImVec2(right - 2 * sq, at.y), ImVec2(right - sq, at.y + sq), p.colours.light);
            dl->AddRectFilled(ImVec2(right - sq, at.y), ImVec2(right, at.y + sq), p.colours.dark);
        }
        if (ImGui::Selectable(TR("Custom"), board.preset == appearance::CUSTOM) && board.preset != appearance::CUSTOM) {
            board.custom = colours;
            board.preset = appearance::CUSTOM;
            changed      = true;
        }
        ImGui::EndCombo();
    }
    // A colour changed makes the board a custom one, from the colours it had.
    const auto edit = [&](const char* name, ImU32 value, bool alpha, ImU32 appearance::BoardColours::* field) {
        ImVec4 c   = ImGui::ColorConvertU32ToFloat4(value);
        float f[4] = {c.x, c.y, c.z, c.w};
        const auto flags =
            ImGuiColorEditFlags_NoInputs |
            (alpha ? ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf : ImGuiColorEditFlags_None);
        if (alpha ? ImGui::ColorEdit4(name, f, flags) : ImGui::ColorEdit3(name, f, flags)) {
            board.custom        = colours;
            board.custom.*field = ImGui::ColorConvertFloat4ToU32(ImVec4(f[0], f[1], f[2], alpha ? f[3] : 1.0f));
            board.preset        = appearance::CUSTOM;
            changed             = true;
        }
    };
    edit(TR("Light squares"), colours.light, false, &appearance::BoardColours::light);
    ImGui::SameLine(0, px(16));
    edit(TR("Dark squares"), colours.dark, false, &appearance::BoardColours::dark);
    ImGui::SameLine(0, px(16));
    edit(TR("Last move"), colours.highlight, true, &appearance::BoardColours::highlight);
    ImGui::SetItemTooltip("%s", TR("Laid over the two squares of the last move: its transparency lets the square's "
                                   "colour through."));
    ImGui::EndGroup();

    // The pieces: a set built in, or the user's folder.
    ImGui::Spacing();
    ui::label(TR("Pieces"), label);
    ImGui::BeginGroup();
    const auto sets      = appearance::pieceSets();
    const auto* set      = appearance::pieceSet(s.pieces);
    const char* set_name = s.pieces == appearance::FOLDER ? TR("Your own folder") : set ? set->name : sets.front().name;
    ImGui::SetNextItemWidth(px(220));
    if (ImGui::BeginCombo("##pieces", set_name)) {
        for (const auto& p : sets) {
            if (ImGui::Selectable(p.name, s.pieces == p.id)) {
                s.pieces = p.id;
                changed  = true;
            }
            ImGui::SetItemTooltip("%s", trf("By {}, under the {} license.", p.author, p.license).c_str());
        }
        if (ImGui::Selectable(TR("Your own folder…"), s.pieces == appearance::FOLDER)) {
            if (s.piece_folder.empty()) {
                dialogs_.openFolder(Dialogs::Purpose::PieceFolder);
            } else {
                s.pieces = appearance::FOLDER;
                changed  = true;
            }
        }
        ImGui::SetItemTooltip("%s", TR("A folder of 12 SVG pictures named wK.svg, wQ.svg, wR.svg, wB.svg, wN.svg, "
                                       "wP.svg and bK.svg … bP.svg: the white and black king, queen, rook, bishop, "
                                       "knight and pawn."));
        ImGui::EndCombo();
    }
    if (set && s.pieces != appearance::FOLDER) {
        ImGui::SetItemTooltip("%s", trf("By {}, under the {} license.", set->author, set->license).c_str());
    }
    if (s.pieces == appearance::FOLDER) {
        ImGui::SameLine();
        if (ImGui::Button(TR("Choose…##pieces"))) dialogs_.openFolder(Dialogs::Purpose::PieceFolder, s.piece_folder);
        ImGui::SameLine();
        if (ImGui::Button(TR("Read again##pieces"))) pieces_dirty_ = true;
        ImGui::SetItemTooltip("%s", TR("After the folder's files were changed."));
        ImGui::TextDisabled("%s", s.piece_folder.c_str());
        if (!pieces_error_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + px(440));
            ImGui::TextUnformatted(trf("{} The default pieces are drawn meanwhile.", pieces_error_).c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndGroup();

    // The move sounds.
    ImGui::Spacing();
    ui::label(TR("Move sounds"), label);
    ImGui::BeginGroup();
    changed |= ImGui::Checkbox(TR("A sound for each move"), &s.sounds);
    ImGui::SetItemTooltip("%s", TR("A short knock for each move of the game followed, in Live and on the big screen, "
                                   "a heavier one for a capture; not for every game of the mosaic."));
    if (s.sounds) {
        ImGui::SameLine(0, px(16));
        ImGui::SetNextItemWidth(px(150));
        changed |= ImGui::SliderInt("##volume", &s.volume, 0, 100, TR("Volume %d %%"));
        ImGui::SameLine();
        ImGui::BeginDisabled(!sound::ready());
        if (ImGui::SmallButton(TR("Try a move"))) sound::play(sound::Kind::Move);
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Try a capture"))) sound::play(sound::Kind::Capture);
        ImGui::EndDisabled();
        if (!sound::ready() && appearance::current().sounds && !options_.scripted) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                               TR("No sound device could be opened: the log says why."));
        }
    }
    ImGui::EndGroup();

    // The buttons under the boards that show a game.
    ImGui::Spacing();
    ui::label(TR("Boards"), label);
    if (ImGui::Checkbox(TR("Navigation buttons under the boards"), &setup_.nav_buttons)) saveSetupSoon();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("Start position, previous move, next move, last move and Flip, under the boards "
                                   "that show a game: Games, Live, the detached boards and the Board page. Without "
                                   "them, the keys do the same: the left and right arrows, Home, End, and F to flip."));
    }
    ImGui::EndGroup();

    // The board of the theme chosen above, as every board will show it.
    ui::sameLineOrWrap(preview, px(32));
    drawBoardPreview(appearance_theme_, preview);
    ImGui::PopID();

    if (changed) applyAppearance(s);
}

void App::hearMoves(int slot) {
    if (slot < 0 || slot >= static_cast<int>(slot_views_.size())) return;
    const auto& v      = slot_views_[static_cast<std::size_t>(slot)];
    const int frame    = ImGui::GetFrameCount();
    const auto plies   = v.state.has_game ? v.state.moves.size() : std::size_t{0};
    auto& heard        = heard_[slot];
    const bool follows = heard.frame >= frame - 1 && heard.game == v.state.game_id;
    if (follows && plies > heard.plies && heard_frame_ != frame && sound::ready()) {
        sound::play(v.last_capture ? sound::Kind::Capture : sound::Kind::Move);
        heard_frame_ = frame;
    }
    heard = {v.state.game_id, plies, frame};
}

// ---------------------------------------------------------------------------
// Engine logos

const App::LogoTexture* App::engineLogo(const std::string& engine) {
    const auto e =
        std::find_if(engines_.begin(), engines_.end(), [&](const EngineEntry& x) { return x.name == engine; });
    if (e == engines_.end() || e->logo.empty()) return nullptr;
    const auto [it, made] = logo_textures_.try_emplace(e->logo);
    auto& logo            = it->second;
    if (made) {
        std::string error;
        if (!logos::plainName(e->logo)) {
            error = trf("{} is not a file of the logos folder.", e->logo);
        } else if (const auto image = logos::load(storage_.logosDir() / e->logo, logos::TEXTURE_SIDE, error)) {
            logo.large  = render::createTexture(image->width, image->height, image->rgba.data());
            logo.width  = image->width;
            logo.height = image->height;
            if (logo.large == ImTextureID_Invalid) error = TR("The picture could not be made a texture.");
            if (!render::mipmapped()) {
                if (const auto small = logos::load(storage_.logosDir() / e->logo, SMALL_LOGO, error))
                    logo.small = render::createTexture(small->width, small->height, small->rgba.data());
            }
        }
        logo.error = error;
        if (!error.empty()) Console::instance().add(trf("The logo of {} cannot be shown: {}", engine, error), true);
    }
    return logo.large != ImTextureID_Invalid ? &logo : nullptr;
}

float App::drawEngineLogo(const std::string& engine, float height) {
    const auto* logo = engineLogo(engine);
    if (!logo || logo->width <= 0 || logo->height <= 0) return 0.0f;
    const float w   = std::min(height * logo->width / logo->height, height * 2.5f);
    const float h   = w * logo->height / logo->width;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, height));
    ImGui::GetWindowDrawList()->AddImage(logo->texture(h * ImGui::GetIO().DisplayFramebufferScale.y),
                                         ImVec2(at.x, at.y + (height - h) / 2),
                                         ImVec2(at.x + w, at.y + (height + h) / 2));
    return w;
}

void App::drawLogoRow(int index, float label) {
    if (index < 0 || index >= static_cast<int>(engines_.size())) return;
    const auto& e = engines_[static_cast<std::size_t>(index)];
    ui::label(TR("Logo"), label);
    if (drawEngineLogo(e.name, ImGui::GetFrameHeight()) > 0) {
        ImGui::SameLine();
    } else if (e.logo.empty()) {
        ImGui::TextDisabled("%s", TR("None"));
        ImGui::SameLine();
    }
    if (ImGui::Button(TR("Choose…##logo"))) {
        dialogs_.openFile(Dialogs::Purpose::EngineLogo,
                          {{TR("Pictures (PNG, JPEG, SVG)"), "png;jpg;jpeg;svg"}, {TR("All files"), "*"}});
    }
    ImGui::SetItemTooltip("%s",
                          TR("A PNG, JPEG or SVG picture of 2 MB at most, shown beside the engine's name in Live, "
                             "on the big screen and on the broadcast web page. A copy is kept in the profile's "
                             "folder logos: the picture chosen can be moved or deleted."));
    if (!e.logo.empty()) {
        ImGui::SameLine();
        if (ImGui::Button(TR("Remove##logo"))) {
            removeEngineLogo(index);
            return;
        }
    }
    // Why it is not shown: the copy in the logos folder gone or damaged, or the picture chosen refused.
    std::string problem;
    if (const auto it = logo_textures_.find(e.logo); !e.logo.empty() && it != logo_textures_.end())
        problem = it->second.error;
    if (logo_error_for_ == e.name && !logo_error_.empty()) problem = logo_error_;
    if (!problem.empty()) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + label);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(problem.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
}

void App::setEngineLogo(int index, const std::string& picture) {
    if (index < 0 || index >= static_cast<int>(engines_.size())) return;
    auto& e = engines_[static_cast<std::size_t>(index)];
    std::string error;
    const auto name = logos::import(fs::path(picture), storage_.logosDir(), e.name, error);
    logo_error_for_ = e.name;
    logo_error_     = error.empty() ? std::string() : trf("The logo was not changed: {}", error);
    if (name.empty()) return;
    const auto old = e.logo;
    e.logo         = name;
    // The same picture chosen again, or one whose copy could not be read before: read anew.
    if (const auto it = logo_textures_.find(name); it != logo_textures_.end()) {
        stale_textures_.push_back(it->second.large);
        stale_textures_.push_back(it->second.small);
        logo_textures_.erase(it);
    }
    storage_.saveEngines(engines_);
    if (old != name) forgetLogoFile(old);
    ++logos_version_;
}

void App::removeEngineLogo(int index) {
    if (index < 0 || index >= static_cast<int>(engines_.size())) return;
    auto& e        = engines_[static_cast<std::size_t>(index)];
    const auto old = e.logo;
    e.logo.clear();
    logo_error_.clear();
    storage_.saveEngines(engines_);
    forgetLogoFile(old);
    ++logos_version_;
}

void App::forgetLogoFile(const std::string& file) {
    if (file.empty()) return;
    // A duplicated engine has the same file.
    if (std::any_of(engines_.begin(), engines_.end(), [&](const EngineEntry& e) { return e.logo == file; })) return;
    if (const auto it = logo_textures_.find(file); it != logo_textures_.end()) {
        stale_textures_.push_back(it->second.large);
        stale_textures_.push_back(it->second.small);
        logo_textures_.erase(it);
    }
    std::error_code ec;
    if (logos::plainName(file)) fs::remove(storage_.logosDir() / file, ec);
}

}  // namespace fcg
