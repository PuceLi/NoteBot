#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include "theme.h"

// Shared controls for the navigation and auxiliary pages, matching the main NoteBot panel.
namespace meteor::widgets {

inline float padding() { return theme::px(6); }
inline float gap() { return theme::px(3); }

inline ImU32 ink(int r, int g, int b, int a = 255) {
    return ImGui::GetColorU32(ImVec4(r / 255.f, g / 255.f, b / 255.f, a / 255.f));
}

inline void background(ImVec2 p, ImVec2 size, bool held, bool hovered, bool selected = false) {
    auto* draw = ImGui::GetWindowDrawList();
    using enum theme::UiColor;
    const float border = theme::px(2);
    draw->AddRectFilled(ImVec2(p.x + border, p.y + border), ImVec2(p.x + size.x - border, p.y + size.y - border),
        theme::color(selected ? ActiveTab : held ? BackgroundPressed : hovered ? BackgroundHovered : BackgroundNormal));
    // Keep the outline outside the fill so translucent backgrounds reveal the panel below.
    const float inset = border * .5f;
    draw->AddRect(ImVec2(p.x + inset, p.y + inset), ImVec2(p.x + size.x - inset, p.y + size.y - inset),
        theme::color(held ? OutlinePressed : hovered ? OutlineHovered : OutlineNormal),
        0.f, border, ImDrawFlags_None);
}

inline bool clicked() {
    return ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right);
}

inline void rowLabel(const char* label, ImVec2 p, float width, const char* description = nullptr) {
    width = std::max(1.f, width);
    auto* draw = ImGui::GetWindowDrawList();
    const float textWidth = ImGui::CalcTextSize(label).x;
    const float height = theme::px(30);
    draw->PushClipRect(p, ImVec2(p.x + width, p.y + height), true);
    draw->AddText(ImVec2(p.x, p.y + padding()), theme::color(theme::UiColor::Text), label);
    draw->PopClipRect();
    if (ImGui::IsMouseHoveringRect(p, ImVec2(p.x + std::min(textWidth, width), p.y + height))) {
        if (textWidth > width) {
            const auto tooltip = description ? std::string(label) + "\n" + description : std::string(label);
            theme::tooltip(tooltip.c_str());
        } else if (description) theme::tooltip(description);
    }
}

inline bool button(const char* label, float width = 0, bool selected = false) {
    const ImVec2 text = ImGui::CalcTextSize(label);
    const ImVec2 size(width > 0 ? width : text.x + padding() * 2, text.y + padding() * 2);
    const auto p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(label, size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    background(p, size, ImGui::IsItemActive(), ImGui::IsItemHovered(), selected);
    ImGui::GetWindowDrawList()->PushClipRect(p, ImVec2(p.x + size.x, p.y + size.y), true);
    ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + (size.x - text.x) / 2, p.y + padding()),
                                      theme::color(theme::UiColor::Text), label);
    ImGui::GetWindowDrawList()->PopClipRect();
    return clicked();
}

inline bool iconButton(const char* id, std::uintptr_t texture, int kind, const char* tooltip) {
    const float size = theme::px(30);
    const auto p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(size, size),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    background(p, ImVec2(size, size), ImGui::IsItemActive(), ImGui::IsItemHovered());
    auto* draw = ImGui::GetWindowDrawList();
    if (texture) draw->AddImage(static_cast<ImTextureID>(texture),
        ImVec2(p.x + padding(), p.y + padding()), ImVec2(p.x + size - padding(), p.y + size - padding()),
        ImVec2(0, 0), ImVec2(1, 1), theme::color(theme::UiColor::Text));
    else if (kind == 0) {
        std::array<ImVec2, 19> points{};
        for (int i = 0; i < 19; ++i) {
            const float angle = -2.4f + i * .27f;
            points[size_t(i)] = ImVec2(p.x + theme::px(15) + std::cos(angle) * theme::px(8), p.y + theme::px(15) + std::sin(angle) * theme::px(8));
        }
        draw->AddPolyline(points.data(), int(points.size()), theme::color(theme::UiColor::Text), false, theme::px(2.5f));
        draw->AddTriangleFilled(ImVec2(p.x + theme::px(22), p.y + theme::px(7)), ImVec2(p.x + theme::px(26), p.y + theme::px(12)),
            ImVec2(p.x + theme::px(19), p.y + theme::px(12)), theme::color(theme::UiColor::Text));
    } else if (kind == 1) {
        draw->AddRect(ImVec2(p.x + theme::px(8), p.y + theme::px(10)), ImVec2(p.x + theme::px(19), p.y + theme::px(22)), theme::color(theme::UiColor::Text), 0.f, theme::px(2), ImDrawFlags_None);
        draw->AddRect(ImVec2(p.x + theme::px(12), p.y + theme::px(7)), ImVec2(p.x + theme::px(23), p.y + theme::px(19)), theme::color(theme::UiColor::Text), 0.f, theme::px(2), ImDrawFlags_None);
    } else {
        draw->AddRect(ImVec2(p.x + theme::px(8), p.y + theme::px(9)), ImVec2(p.x + theme::px(22), p.y + theme::px(24)), theme::color(theme::UiColor::Text), 0.f, theme::px(2), ImDrawFlags_None);
        draw->AddRectFilled(ImVec2(p.x + theme::px(12), p.y + theme::px(6)), ImVec2(p.x + theme::px(18), p.y + theme::px(11)), theme::color(theme::UiColor::Text));
        draw->AddLine(ImVec2(p.x + theme::px(11), p.y + theme::px(15)), ImVec2(p.x + theme::px(19), p.y + theme::px(15)), theme::color(theme::UiColor::Text));
        draw->AddLine(ImVec2(p.x + theme::px(11), p.y + theme::px(19)), ImVec2(p.x + theme::px(19), p.y + theme::px(19)), theme::color(theme::UiColor::Text));
    }
    if (ImGui::IsItemHovered()) theme::tooltip(tooltip);
    return clicked();
}

inline void line(float x, float y, float width) {
    if (width <= 0) return;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(x, y), ImVec2(x + width, y), theme::color(theme::UiColor::SeparatorLine), theme::px(1));
}

inline void separator() {
    const auto p = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    line(p.x, p.y + theme::px(1), width);
    ImGui::Dummy(ImVec2(width, theme::px(3)));
}

inline bool section(const char* idLabel, const std::string& label, bool* state = nullptr, float widthOverride = 0) {
    ImGui::PushID(idLabel);
    auto* storage = ImGui::GetStateStorage();
    const auto id = ImGui::GetID("##expanded");
    bool expanded = state ? *state : storage->GetBool(id, true);
    const auto p = ImGui::GetCursorScreenPos();
    const float width = widthOverride > 0 ? widthOverride : ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetTextLineHeight() + theme::px(6);
    ImGui::InvisibleButton("##section", ImVec2(width, height),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    if (clicked()) {
        expanded = !expanded;
        storage->SetBool(id, expanded);
    }
    auto* draw = ImGui::GetWindowDrawList();
    const auto textSize = ImGui::CalcTextSize(label.c_str());
    const float textX = p.x + (width - textSize.x) / 2;
    const float centerY = p.y + height / 2;
    line(p.x, centerY, textX - p.x - theme::px(4));
    line(textX + textSize.x + theme::px(4), centerY, p.x + width - theme::px(22) - textX - textSize.x - theme::px(4));
    draw->AddText(ImVec2(textX, p.y + theme::px(3)), theme::color(theme::UiColor::SeparatorText), label.c_str());
    if (expanded) draw->AddTriangleFilled(ImVec2(p.x + width - theme::px(16), centerY - theme::px(3)),
        ImVec2(p.x + width - theme::px(4), centerY - theme::px(3)), ImVec2(p.x + width - theme::px(10), centerY + theme::px(4)), theme::color(theme::UiColor::Text));
    else draw->AddTriangleFilled(ImVec2(p.x + width - theme::px(14), centerY - theme::px(6)),
        ImVec2(p.x + width - theme::px(14), centerY + theme::px(6)), ImVec2(p.x + width - theme::px(6), centerY), theme::color(theme::UiColor::Text));
    ImGui::PopID();
    if (state) *state = expanded;
    return expanded;
}

inline float headerHeight() { return ImGui::GetFontSize() * 1.25f + theme::px(8); }

inline void windowHeader(const std::string& title, bool& expanded) {
    const auto p = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x, height = headerHeight();
    ImGui::InvisibleButton("##window-header", ImVec2(width, height),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    if (clicked()) expanded = !expanded;
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.f)) {
        const auto pos = ImGui::GetWindowPos(), delta = ImGui::GetIO().MouseDelta;
        ImGui::SetWindowPos(ImVec2(pos.x + delta.x, pos.y + delta.y));
    }
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(p, ImVec2(p.x + width, p.y + height), theme::color(theme::UiColor::Accent));
    const auto textSize = ImGui::CalcTextSize(title.c_str());
    draw->AddText(
        ImVec2(theme::titleX(p.x, width, textSize.x), p.y + (height - textSize.y) / 2),
        theme::color(theme::UiColor::TitleText), title.c_str());
    const float tx = p.x + width - theme::px(15), ty = p.y + height / 2;
    if (expanded) draw->AddTriangleFilled(ImVec2(tx - theme::px(5), ty - theme::px(2)), ImVec2(tx + theme::px(5), ty - theme::px(2)),
        ImVec2(tx, ty + theme::px(4)), theme::color(theme::UiColor::OutlinePressed));
    else draw->AddTriangleFilled(ImVec2(tx - theme::px(2), ty - theme::px(5)), ImVec2(tx - theme::px(2), ty + theme::px(5)),
        ImVec2(tx + theme::px(4), ty), theme::color(theme::UiColor::OutlinePressed));
}

inline void checkbox(const char* id, bool& value) {
    const auto p = ImGui::GetCursorScreenPos();
    const float size = theme::px(30);
    ImGui::InvisibleButton(id, ImVec2(size, size),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    if (clicked()) value = !value;
    background(p, ImVec2(size, size), ImGui::IsItemActive(), ImGui::IsItemHovered());
    if (value) {
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x + theme::px(7), p.y + theme::px(7)),
            ImVec2(p.x + theme::px(23), p.y + theme::px(23)), theme::color(theme::UiColor::Checkbox));
    }
}

inline void slider(const char* id, float& value, float minimum, float maximum, float width) {
    const auto p = ImGui::GetCursorScreenPos();
    const float height = theme::px(30), radius = theme::px(9);
    ImGui::InvisibleButton(id, ImVec2(width, height));
    const bool held = ImGui::IsItemActive();
    if (held && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        value = minimum + std::clamp((ImGui::GetIO().MousePos.x - p.x - radius) /
            std::max(1.f, width - radius * 2), 0.f, 1.f) * (maximum - minimum);
    const float fraction = std::clamp((value - minimum) / (maximum - minimum), 0.f, 1.f);
    const float x = p.x + radius + fraction * std::max(1.f, width - radius * 2);
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(ImVec2(p.x + radius, p.y + theme::px(13)), ImVec2(x, p.y + theme::px(16)), theme::color(theme::UiColor::SliderLeft));
    draw->AddRectFilled(ImVec2(x, p.y + theme::px(13)), ImVec2(p.x + width - radius, p.y + theme::px(16)), theme::color(theme::UiColor::SliderRight));
    draw->AddCircleFilled(ImVec2(x, p.y + theme::px(15)), radius, theme::color(held ? theme::UiColor::SliderHandlePressed : ImGui::IsItemHovered() ? theme::UiColor::SliderHandleHovered : theme::UiColor::SliderHandle));
}

inline void dropdown(const char* id, int& selected, const char* const* values, int count, float width) {
    ImGui::SetNextItemWidth(width);
    ImGui::Combo(id, &selected, values, count);
}

} // namespace meteor::widgets
