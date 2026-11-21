#include "top_bar.h"
#include "translation.h"
#include "meteor_widgets.h"
#include <imgui.h>
#include <algorithm>
#include <array>
#include <string>

namespace meteor {
void drawTopBar(int& selectedTab) {
    struct Tab { const char* key; const char* label; };
    static constexpr std::array<Tab, 3> tabs{{
        {"notebot.ui.tab.notebot", "NoteBot"},
        {"notebot.ui.render", "Render"},
        {"notebot.ui.tab.settings", "Mod Settings"}
    }};
    const float padding = theme::px(6);
    std::array<std::string, tabs.size()> labels;
    std::array<float, tabs.size()> widths;
    float totalWidth = 0;
    for (size_t i = 0; i < tabs.size(); ++i) {
        labels[i] = notebot::ui::translate(tabs[i].key, tabs[i].label);
        widths[i] = ImGui::CalcTextSize(labels[i].c_str()).x + padding * 2;
        totalWidth += widths[i];
    }
    const auto* viewport = ImGui::GetMainViewport();
    const float width = std::min(totalWidth, viewport->Size.x);
    const float height = ImGui::GetTextLineHeight() + padding * 2;
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y),
                            ImGuiCond_Always, ImVec2(0.5f, 0));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoCollapse;
    flags |= ImGuiWindowFlags_NoScrollbar;
    if (ImGui::Begin("##meteor-top-bar", nullptr, flags)) {
        for (size_t i = 0; i < tabs.size(); ++i) {
            if (i) ImGui::SameLine(0, 0);
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::PushID(static_cast<int>(i));
            ImGui::InvisibleButton("##tab", ImVec2(widths[i], height),
                ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            if (widgets::clicked()) selectedTab = static_cast<int>(i);
            const bool selected = selectedTab == static_cast<int>(i);
            auto* draw = ImGui::GetWindowDrawList();
            // Meteor's top tabs form a continuous strip without ordinary button outlines.
            const auto background = selected ? theme::UiColor::ActiveTab
                : ImGui::IsItemActive() ? theme::UiColor::BackgroundPressed
                : ImGui::IsItemHovered() ? theme::UiColor::BackgroundHovered : theme::UiColor::BackgroundNormal;
            draw->AddRectFilled(pos, ImVec2(pos.x + widths[i], pos.y + height), theme::color(background));
            draw->AddText(ImVec2(pos.x + padding, pos.y + padding), theme::color(theme::UiColor::Text), labels[i].c_str());
            ImGui::PopID();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}
} // namespace meteor
