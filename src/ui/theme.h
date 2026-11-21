#pragma once

#include "../UiAppearance.h"
#include <imgui.h>
#include <imgui_internal.h>

namespace meteor::theme {

using notebot::UiColor;
inline notebot::UiAppearance current;
inline float px(float value) { return value * current.scale; }
inline ImVec2 point(float x, float y) { return ImVec2(px(x), px(y)); }
inline float windowHeightLimit() {
    return std::max(50.f, ImGui::GetMainViewport()->Size.y - std::max(128.f, px(30) + 48));
}
inline ImVec2 windowCenter() {
    const auto center = ImGui::GetMainViewport()->GetCenter();
    return ImVec2(center.x, center.y + std::max(0.f, px(30) - 32) / 2);
}
inline ImVec4 rgba(const notebot::UiAppearance& appearance, UiColor key) {
    const auto& c = appearance.color(key);
    ImVec4 result(c[0], c[1], c[2], c[3]);
    if (appearance.rainbowColors[static_cast<size_t>(key)])
        ImGui::ColorConvertHSVtoRGB(static_cast<float>(std::fmod(ImGui::GetTime() * .15, 1.0)),
                                    1.f, 1.f, result.x, result.y, result.z);
    return result;
}
inline ImVec4 rgba(UiColor key) { return rgba(current, key); }
inline ImU32 color(UiColor key) { return ImGui::GetColorU32(rgba(key)); }
inline float titleX(float x, float width, float textWidth) {
    const float available = std::max(0.f, width - px(36) - textWidth);
    return x + px(18) + available * current.titleAlignment / 2;
}
inline void tooltip(const char* text) {
    if (current.helpText && text) ImGui::SetTooltip("%s", text);
}

inline void apply(const notebot::UiAppearance& appearance) {
    auto& style = ImGui::GetStyle();
    const float ratio = appearance.scale / style.FontScaleMain;
    current = appearance;
    notebot::uiLanguageOverride.store(appearance.language, std::memory_order_relaxed);
    auto* context = ImGui::GetCurrentContext();
    if (context->FrameCount > 0 && std::abs(ratio - 1.f) > .0001f) {
        const auto* viewport = ImGui::GetMainViewport();
        for (auto* window : context->Windows) {
            if (window->Flags & (ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_Popup |
                                ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoSavedSettings)) continue;
            const ImVec2 size(std::min(window->SizeFull.x * ratio, viewport->Size.x - 16),
                              std::min(window->SizeFull.y * ratio, windowHeightLimit()));
            ImGui::SetWindowSize(window, size, ImGuiCond_Always);
            const ImVec2 pos(std::clamp(window->Pos.x, viewport->Pos.x, viewport->Pos.x + viewport->Size.x - size.x),
                             std::clamp(window->Pos.y, viewport->Pos.y + std::min(px(30) + 8, viewport->Size.y - size.y),
                                        viewport->Pos.y + viewport->Size.y - size.y));
            ImGui::SetWindowPos(window, pos, ImGuiCond_Always);
        }
    }
    style.FontScaleMain = current.scale;
    style.WindowPadding = point(8, 8);
    style.FramePadding = point(6, 6);
    style.ItemSpacing = point(3, 3);
    style.ItemInnerSpacing = point(4, 4);
    style.WindowMinSize = point(32, 32);
    style.FrameBorderSize = px(2);
    style.IndentSpacing = px(21);
    style.GrabMinSize = px(12);
    style.WindowRounding = style.FrameRounding = style.ChildRounding = 0;
    const auto set = [&](ImGuiCol slot, UiColor key) { style.Colors[slot] = rgba(key); };
    set(ImGuiCol_Text, UiColor::Text);
    set(ImGuiCol_TextDisabled, UiColor::Placeholder);
    for (auto slot : {ImGuiCol_WindowBg, ImGuiCol_PopupBg, ImGuiCol_FrameBg, ImGuiCol_Button})
        set(slot, UiColor::BackgroundNormal);
    for (auto slot : {ImGuiCol_FrameBgHovered, ImGuiCol_ButtonHovered}) set(slot, UiColor::BackgroundHovered);
    for (auto slot : {ImGuiCol_FrameBgActive, ImGuiCol_ButtonActive}) set(slot, UiColor::BackgroundPressed);
    for (auto slot : {ImGuiCol_Header, ImGuiCol_HeaderHovered, ImGuiCol_HeaderActive,
                      ImGuiCol_ResizeGrip, ImGuiCol_ResizeGripHovered, ImGuiCol_ResizeGripActive,
                      ImGuiCol_NavCursor, ImGuiCol_PlotHistogram}) set(slot, UiColor::Accent);
    set(ImGuiCol_CheckMark, UiColor::Checkbox);
    set(ImGuiCol_Border, UiColor::OutlineNormal);
    set(ImGuiCol_Separator, UiColor::SeparatorLine);
    set(ImGuiCol_TextSelectedBg, UiColor::Highlight);
    set(ImGuiCol_SliderGrab, UiColor::SliderHandle);
    set(ImGuiCol_SliderGrabActive, UiColor::SliderHandlePressed);
}

} // namespace meteor::theme
