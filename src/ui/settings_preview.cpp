#include "notebot_ui.h"
#include "translation.h"
#include "meteor_widgets.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace meteor {
namespace {
std::string text(const char* english, const char*) {
    return notebot::ui::translate(std::string("preview.") + english, english);
}
std::string keybindLabel(std::string_view keybind) {
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 8> keyNames{{
        {"None", "notebot.ui.key_unbound"},
        {"Space", "notebot.ui.key_space"},
        {"Tab", "notebot.ui.key_tab"},
        {"Enter", "notebot.ui.key_enter"},
        {"Insert", "notebot.ui.key_insert"},
        {"Delete", "notebot.ui.key_delete"},
        {"Home", "notebot.ui.key_home"},
        {"End", "notebot.ui.key_end"}
    }};
    for (const auto& [name, key] : keyNames)
        if (keybind == name) return notebot::ui::translate(key, name);
    return std::string(keybind);
}

std::uintptr_t previewResetIcon = 0;
using widgets::section;
using widgets::separator;

bool action(const char* english, const char* chinese, float width = 0) {
    return widgets::button(text(english, chinese).c_str(), width);
}
struct Row {
    ImVec2 origin;
    float width;
    float controlX;
    float controlWidth() const { return std::max(1.f, width - controlX - theme::px(36)); }
};
Row beginRow(const std::string& label) {
    const auto origin = ImGui::GetCursorPos();
    const auto p = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float controlX = std::min(theme::px(330), std::max(0.f, (width - theme::px(36)) * .55f));
    widgets::rowLabel(label.c_str(), p, controlX - widgets::gap());
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + controlX);
    return {origin, width, controlX};
}
bool finishRow(const Row& row) {
    ImGui::SetCursorPos(ImVec2(row.origin.x + row.width - theme::px(30), row.origin.y));
    const bool reset = widgets::iconButton("##reset", previewResetIcon, 0, text("Reset", "重置").c_str());
    const float usedHeight = ImGui::GetCursorPosY() - row.origin.y;
    ImGui::Dummy(ImVec2(0, std::max(0.f, theme::px(33) - usedHeight)));
    return reset;
}
void toggle(const char* english, bool& value, bool initial) {
    ImGui::PushID(english);
    const auto row = beginRow(text(english, english));
    widgets::checkbox("##value", value);
    if (finishRow(row)) value = initial;
    ImGui::PopID();
}
void color(const char* english, notebot::UiAppearance& appearance, notebot::UiColor key) {
    ImGui::PushID(static_cast<int>(key));
    const auto row = beginRow(text(english, english));
    auto& value = appearance.color(key);
    auto& rainbow = appearance.rainbowColors[static_cast<size_t>(key)];
    const auto p = ImGui::GetCursorScreenPos();
    const float size = theme::px(32);
    ImGui::InvisibleButton("##color", ImVec2(size, size));
    const bool swatchClicked = widgets::clicked();
    ImGui::RenderColorRectWithAlphaCheckerboard(ImGui::GetWindowDrawList(), p, ImVec2(p.x + size, p.y + size),
        ImGui::ColorConvertFloat4ToU32(theme::rgba(appearance, key)), 1.f, theme::px(8), ImVec2(0, 0));
    ImGui::SameLine(0, widgets::gap());
    if (action("Edit", "编辑", theme::px(46)) || swatchClicked) ImGui::OpenPopup("##picker");
    if (ImGui::BeginPopup("##picker", ImGuiWindowFlags_NoScrollbar)) {
        ImGui::SetNextItemWidth(theme::px(220));
        ImGui::ColorPicker4("##rgba", value.data(), ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
        widgets::checkbox("##rainbow", rainbow);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(notebot::ui::translate("notebot.ui.rainbow", "Rainbow:").c_str());
        ImGui::EndPopup();
    }
    if (finishRow(row)) { value = notebot::UiAppearance{}.color(key); rainbow = false; }
    ImGui::PopID();
}
bool group(const char* english, const char* chinese) {
    return section(english, text(english, chinese));
}
}

void NotebotUi::drawModSettings() {
    if (isBindingKey()) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) binding_=BindTarget::None;
        else for (int key=ImGuiKey_NamedKey_BEGIN;key<ImGuiKey_NamedKey_END;++key) {
            if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(key), false)) {
                auto& value = binding_ == BindTarget::Playback ? playbackKeybind : keybind;
                auto& callback = binding_ == BindTarget::Playback ? actions.playbackBindChanged : actions.bindChanged;
                value=ImGui::GetKeyName(static_cast<ImGuiKey>(key));
                binding_=BindTarget::None;
                if (callback) callback(value);
                break;
            }
        }
    }
    const auto before = appearance;
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(theme::windowCenter(), ImGuiCond_FirstUseEver, ImVec2(.5f,.5f));
    const float headerHeight = widgets::headerHeight();
    const float maxHeight = theme::windowHeightLimit();
    ImGui::SetNextWindowSize(ImVec2(std::min(theme::px(700), viewport->Size.x - 16), std::min(theme::px(620), maxHeight)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(theme::px(620), viewport->Size.x - 16), headerHeight), ImVec2(FLT_MAX, FLT_MAX));
    const auto title = notebot::ui::translate("notebot.ui.tab.settings", "Mod Settings");
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(widgets::gap(), widgets::gap()));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(widgets::padding(), widgets::padding()));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, theme::px(2));
    previewResetIcon = resetIcon;
    if (ImGui::Begin((title + "###mod-settings").c_str(), nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar)) {
        auto& state = settingsWindow_;
        if (!state.loaded) {
            state.expanded = ImGui::GetWindowHeight() > headerHeight + 10;
            state.loaded = true;
        }
        const bool wasExpanded = state.expanded;
        if (wasExpanded) state.expandedHeight = ImGui::GetWindowHeight();
        widgets::windowHeader(title, state.expanded);
        if (wasExpanded != state.expanded)
            ImGui::SetWindowSize(ImVec2(ImGui::GetWindowWidth(), state.expanded ? state.expandedHeight : headerHeight));
        if (state.expanded) {
            ImGui::SetCursorPosX(theme::px(8));
            const float pageWidth = std::max(1.f, (ImGui::GetContentRegionAvail().x - theme::px(8) - widgets::gap() * 2) / 3);
            const int previousPage = selectedSettingsPage_;
            ImGui::BeginDisabled(dataTransferBusy);
            if (widgets::button(text("UI Settings", "UI 设置").c_str(), pageWidth, selectedSettingsPage_ == 0))
                selectedSettingsPage_ = 0;
            ImGui::SameLine(0, widgets::gap());
            if (widgets::button(notebot::ui::translate("notebot.ui.bind", "Keybindings").c_str(), pageWidth,
                selectedSettingsPage_ == 2))
                selectedSettingsPage_ = 2;
            ImGui::SameLine(0, widgets::gap());
            if (widgets::button(notebot::ui::translate("notebot.ui.tab.data", "Mod Data").c_str(), pageWidth,
                selectedSettingsPage_ == 1))
                selectedSettingsPage_ = 1;
            ImGui::EndDisabled();
            if (previousPage != selectedSettingsPage_) binding_ = BindTarget::None;
            const bool gui = selectedSettingsPage_ == 0;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, theme::point(8, 8));
            ImGui::BeginChild(gui ? "##ui-settings-content" : selectedSettingsPage_ == 2 ? "##keybindings-content" : "##mod-data-content", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding,
                              ImGuiWindowFlags_NoScrollbar);
            if (gui) {
                using enum notebot::UiColor;
                const float toolbarWidth = ImGui::GetContentRegionAvail().x;
                const float iconsWidth = (theme::px(30) + widgets::gap()) * 2;
                const float resetColorsWidth = ImGui::CalcTextSize(text("Reset Colors", "重置颜色").c_str()).x + widgets::padding() * 2;
                const float resetLayoutWidth = ImGui::CalcTextSize(text("Reset Layout", "重置布局").c_str()).x + widgets::padding() * 2;
                const bool wrapButtons = resetLayoutWidth + resetColorsWidth + iconsWidth + widgets::gap() > ImGui::GetContentRegionAvail().x;
                if (action("Reset Layout", "重置布局", wrapButtons ? toolbarWidth :
                    std::max(1.f, ImGui::GetContentRegionAvail().x - iconsWidth - resetColorsWidth - widgets::gap())))
                    resetLayoutRequested_ = true;
                if (!wrapButtons) ImGui::SameLine();
                if (action("Reset Colors", "重置颜色", wrapButtons ? std::max(1.f, toolbarWidth - iconsWidth) : resetColorsWidth)) {
                    appearance.colors = notebot::UiAppearance{}.colors;
                    appearance.rainbowColors.fill(false);
                }
                ImGui::SameLine();
                if (widgets::iconButton("##copy", copyIcon, 1, text("Copy", "复制").c_str())) {
                    ImGui::SetClipboardText(notebot::serializeUiAppearance(appearance).c_str());
                    appearanceStatus = notebot::ui::translate("notebot.ui.appearance_copied", "UI settings copied.");
                    appearanceStatusFailed = false;
                }
                ImGui::SameLine();
                if (widgets::iconButton("##paste", pasteIcon, 2, text("Paste", "粘贴").c_str())) {
                    const char* clipboard = ImGui::GetClipboardText();
                    appearanceStatusFailed = !clipboard || !notebot::deserializeUiAppearance(clipboard, appearance);
                    scaleInput_ = appearance.scale;
                    scaleInputEditing_ = false;
                    appearanceStatus = notebot::ui::translate(appearanceStatusFailed ? "notebot.ui.appearance_invalid" : "notebot.ui.appearance_pasted",
                        appearanceStatusFailed ? "Clipboard does not contain valid NoteBot UI settings." : "UI settings applied.");
                }
                if (!appearanceStatus.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, appearanceStatusFailed ? ImVec4(1,.4f,.4f,1) : theme::rgba(SecondaryText));
                    ImGui::TextWrapped("%s", appearanceStatus.c_str());
                    ImGui::PopStyleColor();
                }
                const notebot::UiAppearance defaults;
                if (group("General", "常规")) {
                    ImGui::PushID("language-row");
                    const auto languageRow = beginRow(text("Language", "语言"));
                    const auto automatic = text("Follow Game", "跟随游戏");
                    const char* languages[]{automatic.c_str(), "简体中文", "English"};
                    widgets::dropdown("##language", appearance.language, languages, 3, languageRow.controlWidth());
                    if (finishRow(languageRow)) appearance.language = defaults.language;
                    ImGui::PopID();
                    ImGui::PushID("scale-row");
                    if (!scaleInputEditing_) scaleInput_ = appearance.scale;
                    const auto scaleRow = beginRow(text("Scale", "缩放"));
                    ImGui::SetNextItemWidth(theme::px(75));
                    ImGui::InputFloat("##scale-input", &scaleInput_, 0, 0, "%.2f");
                    const bool textActive = ImGui::IsItemActive();
                    bool commitScale = ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::SameLine();
                    scaleInput_ = std::isfinite(scaleInput_) ? std::clamp(scaleInput_, .1f, 4.f) : 1.f;
                    widgets::slider("##scale", scaleInput_, .1f, 4.f,
                        std::max(theme::px(20), scaleRow.controlWidth() - theme::px(75) - widgets::gap()));
                    commitScale |= ImGui::IsItemDeactivated();
                    scaleInputEditing_ = textActive || ImGui::IsItemActive();
                    if (commitScale) appearance.scale = scaleInput_;
                    if (finishRow(scaleRow)) { appearance.scale = defaults.scale; scaleInputEditing_ = false; }
                    ImGui::PopID();
                    ImGui::PushID("alignment-row");
                    const auto left=text("Left", "左"), center=text("Center", "中"), right=text("Right", "右");
                    const char* alignments[]{left.c_str(),center.c_str(),right.c_str()};
                    const auto alignmentRow = beginRow(text("Module Alignment", "模块标题对齐"));
                    widgets::dropdown("##alignment", appearance.titleAlignment, alignments, 3, alignmentRow.controlWidth());
                    if (finishRow(alignmentRow)) appearance.titleAlignment = defaults.titleAlignment;
                    ImGui::PopID();
                    toggle("Modules Help Text", appearance.helpText, defaults.helpText);
                    toggle("Hide HUD", appearance.hideHud, defaults.hideHud);
                }
                if (group("Now Playing", "Now Playing")) {
                    color("Now Playing Background", appearance, NowPlayingBackground);
                    color("Now Playing Title", appearance, NowPlayingTitle);
                    color("Now Playing Time", appearance, NowPlayingTime);
                    color("Now Playing Progress", appearance, NowPlayingProgress);
                    color("Now Playing Track", appearance, NowPlayingTrack);
                    color("Now Playing Outline", appearance, NowPlayingOutline);
                }
                if (group("Colors", "颜色")) {
                    color("Accent", appearance, Accent); color("Checkbox", appearance, Checkbox);
                }
                if (group("Text", "文字")) {
                    color("Text Color", appearance, Text); color("Secondary Text", appearance, SecondaryText);
                    color("Highlight", appearance, Highlight); color("Title Text", appearance, TitleText);
                    color("Placeholder", appearance, Placeholder);
                }
                if (group("Background", "背景")) {
                    color("Background Normal", appearance, BackgroundNormal);
                    color("Background Hovered", appearance, BackgroundHovered);
                    color("Background Pressed", appearance, BackgroundPressed);
                    color("Active Module", appearance, ActiveTab);
                }
                if (group("Outline", "边框")) {
                    color("Normal", appearance, OutlineNormal); color("Hovered", appearance, OutlineHovered); color("Pressed", appearance, OutlinePressed);
                }
                if (group("Separator", "分隔线")) {
                    color("Separator Text", appearance, SeparatorText); color("Separator Line", appearance, SeparatorLine);
                }
                if (group("Slider Handle", "滑块")) {
                    color("Normal", appearance, SliderHandle); color("Hovered", appearance, SliderHandleHovered); color("Pressed", appearance, SliderHandlePressed);
                }
                if (group("Slider", "滑条")) { color("Slider Left", appearance, SliderLeft); color("Slider Right", appearance, SliderRight); }
            } else if (selectedSettingsPage_ == 2) {
                using notebot::ui::translate;
                if (section("keybindings", translate("notebot.ui.bind", "Keybindings"))) {
                    const auto bindingRow = [&](const char* key, const char* fallback, BindTarget target,
                                                std::string& value, const char* initial, auto& callback) {
                        ImGui::PushID(key);
                        const auto row = beginRow(translate(key, fallback));
                        const auto label = binding_ == target ? std::string("...") : keybindLabel(value);
                        if (widgets::button(label.c_str(), row.controlWidth())) binding_ = target;
                        if (finishRow(row)) {
                            value = initial;
                            binding_ = BindTarget::None;
                            if (callback) callback(value);
                        }
                        ImGui::PopID();
                    };
                    bindingRow("notebot.ui.bind_label", "Open UI:", BindTarget::Interface,
                               keybind, "L", actions.bindChanged);
                    bindingRow("notebot.ui.playback_bind_label", "Play / Pause:", BindTarget::Playback,
                               playbackKeybind, "P", actions.playbackBindChanged);
                }
            } else {
                using notebot::ui::translate;
                const float buttonWidth = std::max(1.f, (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2);
                if (section("config", translate("notebot.data.config", "Configuration"))) {
                    ImGui::TextWrapped("%s", translate("notebot.data.config.description",
                        "Back up the entire config folder and ImGui layout to ZIP.").c_str());
                    ImGui::BeginDisabled(dataTransferBusy);
                    if (widgets::button(translate("notebot.data.backup_config", "Back Up Configuration").c_str(), buttonWidth) && actions.backupConfig)
                        actions.backupConfig();
                    ImGui::SameLine();
                    if (widgets::button(translate("notebot.data.import_config", "Import Configuration").c_str(), buttonWidth) && actions.importConfig)
                        actions.importConfig();
                    ImGui::EndDisabled();
                }
                ImGui::Spacing();
                if (section("songs", translate("notebot.data.songs", "Songs"))) {
                    ImGui::TextWrapped("%s", translate("notebot.data.songs.description",
                        "Back up the entire current songs folder, including subfolders, to ZIP.").c_str());
                    ImGui::BeginDisabled(dataTransferBusy);
                    if (widgets::button(translate("notebot.data.backup_songs", "Back Up Songs Folder").c_str(), buttonWidth) && actions.backupSongs)
                        actions.backupSongs();
                    ImGui::SameLine();
                    if (widgets::button(translate("notebot.data.import_songs", "Import Songs Folder").c_str(), buttonWidth) && actions.importSongs)
                        actions.importSongs();
                    ImGui::EndDisabled();
                }
                ImGui::Spacing();
                separator();
                ImGui::BeginDisabled(dataTransferBusy);
                if (widgets::button(translate("notebot.data.open_backup_folder", "Open Backup Folder").c_str(),
                    ImGui::GetContentRegionAvail().x) && actions.openBackupFolder) actions.openBackupFolder();
                ImGui::EndDisabled();
                ImGui::TextWrapped("%s", translate("notebot.data.import.description",
                    "Import merges files and replaces matching names. Other existing files are kept.").c_str());
                if (!dataTransferStatus.empty()) {
                    separator();
                    ImGui::PushStyleColor(ImGuiCol_Text, dataTransferFailed
                        ? ImVec4(1.f, .4f, .4f, 1.f) : theme::rgba(theme::UiColor::SecondaryText));
                    ImGui::TextWrapped("%s", dataTransferStatus.c_str());
                    ImGui::PopStyleColor();
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
        }
    }
    ImGui::End(); ImGui::PopStyleVar(7); notebot::sanitizeUiAppearance(appearance);
    if (appearance != before && actions.appearanceChanged) actions.appearanceChanged(appearance);
}
} // namespace meteor
