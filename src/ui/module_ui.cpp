#include "notebot_ui.h"
#include "translation.h"
#include "mc/deps/shared_types/v1_26_20/block/NoteBlockInstrumentEnum.h"

#include <imgui.h>
#include <fmt/format.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace meteor {
namespace {

constexpr ImU32 col(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }
constexpr float pad = 6.0f;
constexpr float rowHeight = 33.0f;
constexpr float rowControlX = 330.0f;
constexpr float resetButtonWidth = 30.0f;
constexpr float rowControlGap = 6.0f;
std::array<std::uintptr_t,3> iconTextures{};

using notebot::ui::translate;

std::string instrumentLabel(notebot::Instrument instrument) {
    const int index = static_cast<int>(instrument);
    const std::string fallback = SharedTypes::v1_26_20::NoteBlockInstrumentEnum::noteBlockInstrumentToString(instrument);
    std::string label = translate("notebot.instrument." + std::to_string(index), fallback);
    std::replace(label.begin(), label.end(), '_', ' ');
    return label;
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
        if (keybind == name) return translate(key, name);
    return std::string(keybind);
}

void box(ImDrawList* draw, ImVec2 p, ImVec2 size, bool hovered, bool held) {
    draw->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y),
        held ? col(20,20,20) : hovered ? col(10,10,10) : col(0,0,0));
    draw->AddRectFilled(ImVec2(p.x + 2, p.y + 2), ImVec2(p.x + size.x - 2, p.y + size.y - 2),
        held ? col(40,40,40,190) : hovered ? col(30,30,30,175) : col(20,20,20,160));
}

bool button(const char* text, float width) {
    const ImVec2 t = ImGui::CalcTextSize(text);
    if (width <= 0) width = t.x + pad * 2;
    const ImVec2 size(width, t.y + pad * 2);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(text, size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    box(ImGui::GetWindowDrawList(), p, size, ImGui::IsItemHovered(), ImGui::IsItemActive());
    ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + (size.x - t.x)/2, p.y + pad), col(255,255,255), text);
    return ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right);
}

bool iconButton(const char* id, const char* tooltip, int kind) {
    const ImVec2 size(30,30), p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    box(draw, p, size, hovered, ImGui::IsItemActive());
    const ImU32 ink = col(255,255,255);
    if (kind>=0 && kind<int(iconTextures.size()) && iconTextures[size_t(kind)]!=0) {
        draw->AddImage(static_cast<ImTextureID>(iconTextures[size_t(kind)]),
            ImVec2(p.x+6,p.y+6),ImVec2(p.x+24,p.y+24));
    } else if (kind == 0) {
        std::array<ImVec2, 19> points{};
        for (int i = 0; i < 19; ++i) {
            const float angle = (-2.4f + float(i) * 0.27f);
            points[size_t(i)] = ImVec2(p.x + 15 + std::cos(angle)*8, p.y + 15 + std::sin(angle)*8);
        }
        draw->AddPolyline(points.data(), int(points.size()), ink, false, 2.5f);
        draw->AddTriangleFilled(ImVec2(p.x+22,p.y+7), ImVec2(p.x+26,p.y+12), ImVec2(p.x+19,p.y+12), ink);
    } else if (kind == 1) {
        draw->AddRect(ImVec2(p.x+8,p.y+10), ImVec2(p.x+19,p.y+22), ink, 0.0f, ImDrawFlags_None, 2.0f);
        draw->AddRect(ImVec2(p.x+12,p.y+7), ImVec2(p.x+23,p.y+19), ink, 0.0f, ImDrawFlags_None, 2.0f);
    } else {
        draw->AddRect(ImVec2(p.x+8,p.y+9), ImVec2(p.x+22,p.y+24), ink, 0.0f, ImDrawFlags_None, 2.0f);
        draw->AddRectFilled(ImVec2(p.x+12,p.y+6), ImVec2(p.x+18,p.y+11), ink);
        draw->AddLine(ImVec2(p.x+11,p.y+15), ImVec2(p.x+19,p.y+15), ink);
        draw->AddLine(ImVec2(p.x+11,p.y+19), ImVec2(p.x+19,p.y+19), ink);
    }
    if (hovered && tooltip) ImGui::SetTooltip("%s", tooltip);
    return clicked;
}

bool checkBox(const char* id, bool& value) {
    const ImVec2 size(30,30), p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool changed = ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right);
    if (changed) value = !value;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    box(draw, p, size, ImGui::IsItemHovered(), ImGui::IsItemActive());
    if (value) draw->AddRectFilled(ImVec2(p.x+7,p.y+7), ImVec2(p.x+23,p.y+23), col(145,61,226));
    return changed;
}

void horizontalSeparator(float width) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x,p.y+1),ImVec2(p.x+width,p.y+1),col(255,255,255,170),1.0f);
    ImGui::Dummy(ImVec2(width,3));
}

bool section(const char* name, bool& expanded, float width) {
    ImGui::PushID(name);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFontSize() + 6;
    ImGui::InvisibleButton("##section", ImVec2(width,h), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) expanded = !expanded;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 text = ImGui::CalcTextSize(name);
    const float textX = p.x + (width-text.x)/2;
    const float centerY = p.y+h/2;
    draw->AddLine(ImVec2(p.x,centerY),ImVec2(textX-4,centerY),col(255,255,255,170),1.0f);
    draw->AddLine(ImVec2(textX+text.x+4,centerY),ImVec2(p.x+width-22,centerY),col(255,255,255,170),1.0f);
    draw->AddText(ImVec2(textX,p.y+3),col(255,255,255),name);
    if (expanded) draw->AddTriangleFilled(ImVec2(p.x+width-16,centerY-3),ImVec2(p.x+width-4,centerY-3),ImVec2(p.x+width-10,centerY+4),col(255,255,255));
    else draw->AddTriangleFilled(ImVec2(p.x+width-14,centerY-6),ImVec2(p.x+width-14,centerY+6),ImVec2(p.x+width-6,centerY),col(255,255,255));
    ImGui::PopID();
    return expanded;
}

struct Row {
    ImVec2 origin;
    float width;
    float controlX;
};

Row beginRow(const char* label, const char* description, float width) {
    const ImVec2 cursor = ImGui::GetCursorPos();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddText(ImVec2(p.x,p.y+6),col(255,255,255),label);
    if (description && ImGui::IsMouseHoveringRect(ImVec2(p.x,p.y),ImVec2(p.x+ImGui::CalcTextSize(label).x,p.y+30)))
        ImGui::SetTooltip("%s",description);
    ImGui::PushID(label);
    const float controlX = std::min(rowControlX,
        std::max(190.0f, (width-resetButtonWidth-rowControlGap)*0.55f));
    ImGui::SetCursorPos(ImVec2(cursor.x+controlX,cursor.y));
    return {cursor,width,controlX};
}

float rowControlWidth(const Row& row) {
    return std::max(1.0f,row.width-row.controlX-resetButtonWidth-rowControlGap);
}

void finishRow(const Row& row) {
    ImGui::PopID();
    const float usedHeight = ImGui::GetCursorPosY()-row.origin.y;
    ImGui::Dummy(ImVec2(0.0f,std::max(0.0f,rowHeight-usedHeight)));
}

bool resetAt(const Row& row) {
    ImGui::SetCursorPos(ImVec2(row.origin.x+row.width-resetButtonWidth,row.origin.y));
    const auto tooltip = translate("notebot.ui.reset", "Reset");
    return iconButton("##reset",tooltip.c_str(),0);
}

bool boolRow(const char* label, const char* description, bool& value, bool defaultValue, float width) {
    Row row=beginRow(label,description,width);
    bool changed=checkBox("##value",value);
    if (resetAt(row)) { value=defaultValue; changed=true; }
    finishRow(row); return changed;
}

bool slider(const char* id, int& value, int minValue, int maxValue, float width=200.0f) {
    const ImVec2 p=ImGui::GetCursorScreenPos(), size(width,30);
    ImGui::InvisibleButton(id,size);
    const bool hovered=ImGui::IsItemHovered(), held=ImGui::IsItemActive();
    bool changed=false;
    if (held && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const float fraction=std::clamp((ImGui::GetIO().MousePos.x-p.x-9)/(size.x-18),0.0f,1.0f);
        const int next=minValue+int(std::round(fraction*(maxValue-minValue)));
        changed=next!=value; value=next;
    }
    const float fraction=std::clamp(float(value-minValue)/std::max(1,maxValue-minValue),0.0f,1.0f);
    const float handleX=p.x+9+fraction*(size.x-18);
    ImDrawList* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(ImVec2(p.x+9,p.y+13),ImVec2(handleX,p.y+16),col(100,35,170));
    draw->AddRectFilled(ImVec2(handleX,p.y+13),ImVec2(p.x+size.x-9,p.y+16),col(50,50,50));
    draw->AddCircleFilled(ImVec2(handleX,p.y+15),9,held?col(150,60,255):hovered?col(140,30,255):col(130,0,255));
    return changed;
}

bool intRow(const char* label, const char* description, int& value, int def, int minValue, int maxValue, float width) {
    Row row=beginRow(label,description,width);
    ImGui::SetNextItemWidth(75);
    bool changed=ImGui::InputInt("##text",&value,0,0);
    value=std::max(value,minValue);
    ImGui::SameLine(0,3);
    changed |= slider("##slider",value,minValue,maxValue,rowControlWidth(row)-75.0f-3.0f);
    if (resetAt(row)) { value=def; changed=true; }
    finishRow(row); return changed;
}

bool floatRow(const char* label, const char* description, float& value, float def, float minValue, float maxValue, float width) {
    Row row=beginRow(label,description,width);
    ImGui::SetNextItemWidth(75);
    bool changed=ImGui::InputFloat("##text",&value,0,0,"%.2f");
    value=std::max(value,minValue);
    ImGui::SameLine(0,3);
    const float sliderWidth=rowControlWidth(row)-75.0f-3.0f;
    const float trackWidth=sliderWidth-18.0f;
    const ImVec2 p=ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##slider",ImVec2(sliderWidth,30));
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const float next=std::clamp((ImGui::GetIO().MousePos.x-p.x-9)/trackWidth,0.0f,1.0f)*(maxValue-minValue)+minValue;
        changed |= std::abs(next-value)>0.001f; value=next;
    }
    const float frac=std::clamp((value-minValue)/std::max(0.001f,maxValue-minValue),0.0f,1.0f);
    const float hx=p.x+9+frac*trackWidth;
    ImDrawList* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(ImVec2(p.x+9,p.y+13),ImVec2(hx,p.y+16),col(100,35,170));
    draw->AddRectFilled(ImVec2(hx,p.y+13),ImVec2(p.x+sliderWidth-9,p.y+16),col(50,50,50));
    draw->AddCircleFilled(ImVec2(hx,p.y+15),9,col(130,0,255));
    if (resetAt(row)) { value=def; changed=true; }
    finishRow(row); return changed;
}

bool enumRow(const char* label, const char* description, int& value, int def, const char* const* options, int count, float width) {
    Row row=beginRow(label,description,width);
    ImGui::SetNextItemWidth(rowControlWidth(row));
    bool changed=ImGui::Combo("##value",&value,options,count);
    if (resetAt(row)) { value=def; changed=true; }
    finishRow(row); return changed;
}

bool colorRow(const char* label, const char* description, std::array<float,4>& value,
              const std::array<float,4>& def, bool& rainbowEnabled, float width, bool& openEditor) {
    Row row=beginRow(label,description,width);
    const ImVec2 p=ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##swatch",ImVec2(32,32));
    ImGui::GetWindowDrawList()->AddRectFilled(p,ImVec2(p.x+32,p.y+32),
        ImGui::ColorConvertFloat4ToU32(ImVec4(value[0],value[1],value[2],value[3])));
    if (ImGui::IsItemClicked()) openEditor=true;
    ImGui::SameLine(0,3);
    const auto editLabel = translate("notebot.ui.edit", "Edit");
    if (button(editLabel.c_str(),46)) openEditor=true;
    bool changed=false;
    if (resetAt(row)) { value=def; rainbowEnabled=false; changed=true; }
    finishRow(row); return changed;
}

void moduleHeader(bool& expanded, float width) {
    const ImVec2 p=ImGui::GetCursorScreenPos();
    const float h=ImGui::GetFontSize()*1.25f+8;
    ImGui::InvisibleButton("##module-header",ImVec2(width,h),ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonRight);
    ImDrawList* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(p,ImVec2(p.x+width,p.y+h),col(145,61,226));
    const ImVec2 t=ImGui::CalcTextSize("Notebot");
    draw->AddText(ImVec2(p.x+(width-t.x)/2,p.y+(h-t.y)/2),col(255,255,255),"Notebot");
    const float tx=p.x+width-15,ty=p.y+h/2;
    if (expanded) draw->AddTriangleFilled(ImVec2(tx-5,ty-2),ImVec2(tx+5,ty-2),ImVec2(tx,ty+4),col(20,20,20,200));
    else draw->AddTriangleFilled(ImVec2(tx-2,ty-5),ImVec2(tx-2,ty+5),ImVec2(tx+4,ty),col(20,20,20,200));
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) expanded=!expanded;
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left,2)) {
        const ImVec2 pos=ImGui::GetWindowPos(), delta=ImGui::GetIO().MouseDelta;
        ImGui::SetWindowPos(ImVec2(pos.x+delta.x,pos.y+delta.y));
    }
}

} // namespace

void NotebotUi::updateRainbowColors() {
    if (std::none_of(settings.rainbowColors.begin(), settings.rainbowColors.end(),
                     [](bool enabled) { return enabled; })) return;

    const auto now = std::chrono::steady_clock::now();
    if (lastRainbowUpdate_ != std::chrono::steady_clock::time_point{} &&
        now - lastRainbowUpdate_ < std::chrono::milliseconds(50)) return;
    lastRainbowUpdate_ = now;

    float red, green, blue;
    ImGui::ColorConvertHSVtoRGB(std::fmod(static_cast<float>(ImGui::GetTime()) * 0.15f, 1.0f),
                                1.0f, 1.0f, red, green, blue);
    for (size_t index = 0; index < settings.rainbowColors.size(); ++index) {
        if (!settings.rainbowColors[index]) continue;
        auto& color = settings.renderColors[index];
        color[0] = red;
        color[1] = green;
        color[2] = blue;
    }
    if (actions.animatedColorsChanged) actions.animatedColorsChanged(settings);
}

void NotebotUi::drawModule() {
    iconTextures={resetIcon,copyIcon,pasteIcon};
    if (binding_) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) binding_=false;
        else for (int key=ImGuiKey_NamedKey_BEGIN;key<ImGuiKey_NamedKey_END;++key) {
            if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(key))) {
                keybind=ImGui::GetKeyName(static_cast<ImGuiKey>(key));
                binding_=false;
                if (actions.bindChanged) actions.bindChanged(keybind);
                break;
            }
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { showModule=false; return; }

    const ImGuiViewport* viewport=ImGui::GetMainViewport();
    const float height=std::min(660.0f,std::max(200.0f,viewport->Size.y-128.0f));
    const float headerHeight=ImGui::GetFontSize()*1.25f+8;
    ImGui::SetNextWindowPos(viewport->GetCenter(),ImGuiCond_FirstUseEver,ImVec2(0.5f,0.5f));
    ImGui::SetNextWindowSize(ImVec2(650,height),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(420,headerHeight),ImVec2(FLT_MAX,FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(3,3));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(6,6));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,2);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(20/255.f,20/255.f,20/255.f,160/255.f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,ImVec4(30/255.f,30/255.f,30/255.f,175/255.f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,ImVec4(40/255.f,40/255.f,40/255.f,190/255.f));
    ImGui::PushStyleColor(ImGuiCol_Border,ImVec4(0,0,0,1));
    ImGui::PushStyleColor(ImGuiCol_PopupBg,ImVec4(20/255.f,20/255.f,20/255.f,0.76f));
    if (ImGui::Begin("Notebot##module",&showModule,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoScrollbar)) {
        if (!moduleLayoutLoaded_) {
            moduleExpanded_ = ImGui::GetWindowSize().y > headerHeight + 10.0f;
            moduleLayoutLoaded_ = true;
        }
        const bool wasExpanded = moduleExpanded_;
        if (wasExpanded) {
            moduleExpandedWidth_ = ImGui::GetWindowSize().x;
            moduleExpandedHeight_ = ImGui::GetWindowSize().y;
        }
        moduleHeader(moduleExpanded_,ImGui::GetContentRegionAvail().x);
        if (wasExpanded && !moduleExpanded_)
            ImGui::SetWindowSize(ImVec2(moduleExpandedWidth_,headerHeight));
        else if (!wasExpanded && moduleExpanded_)
            ImGui::SetWindowSize(ImVec2(moduleExpandedWidth_,moduleExpandedHeight_));
        if (moduleExpanded_) {
            ImGui::BeginChild("##config-view",ImVec2(0,ImGui::GetContentRegionAvail().y),false,ImGuiWindowFlags_NoScrollbar);
            constexpr float contentPadding=8.0f;
            ImGui::SetCursorPosX(contentPadding);
            ImGui::BeginGroup();
            const float width=std::max(1.0f,ImGui::GetContentRegionAvail().x-contentPadding);
            const auto description = translate("notebot.ui.description",
                "Play songs with nearby note blocks");
            ImGui::TextUnformatted(description.c_str());
            const auto github = translate("notebot.ui.github", "GitHub");
            ImGui::Text("%s: https://github.com/PuceLI/NoteBot", github.c_str());
            bool changed=false;
            const NotebotSettings defaults{};
            const auto general = translate("notebot.ui.general", "General");
            if (section(general.c_str(),generalExpanded_,width)) {
                const auto tickDelay = translate("notebot.ui.tick_delay", "Tick Delay");
                const auto tickDelayTip = translate("notebot.ui.tick_delay_tip",
                    "Extra delay between song ticks (0 = fastest).");
                changed |= intRow(tickDelay.c_str(),tickDelayTip.c_str(),settings.tickDelay,defaults.tickDelay,0,20,width);
                const auto tuneInterval = translate("notebot.ui.tune_interval", "Tune Interval (ticks)");
                const auto tuneIntervalTip = translate("notebot.ui.tune_interval_tip",
                    "Extra world ticks between confirmed tuning clicks (0 = fastest).");
                changed |= intRow(tuneInterval.c_str(),tuneIntervalTip.c_str(),settings.tuneIntervalTicks,defaults.tuneIntervalTicks,0,20,width);
                const auto anyInstrument = translate("notebot.ui.mode_any_instrument", "AnyInstrument");
                const auto exactInstruments = translate("notebot.ui.mode_exact_instruments", "ExactInstruments");
                const char* modes[]={anyInstrument.c_str(),exactInstruments.c_str()};
                const auto mode = translate("notebot.ui.mode", "Mode");
                const auto modeTip = translate("notebot.ui.mode_tip", "Select mode of notebot");
                changed |= enumRow(mode.c_str(),modeTip.c_str(),settings.mode,defaults.mode,modes,2,width);
                const auto sequential = translate("notebot.ui.sequential", "Sequential");
                const auto polyphonicMode = translate("notebot.ui.polyphonic", "Polyphonic");
                const char* playingModes[]={sequential.c_str(),polyphonicMode.c_str()};
                const auto playingMode = translate("notebot.ui.playing_mode", "Playing Mode");
                const auto playingModeTip = translate("notebot.ui.playing_mode_tip",
                    "How notes on the same song tick are played");
                changed |= enumRow(playingMode.c_str(),playingModeTip.c_str(),settings.playingMode,defaults.playingMode,playingModes,2,width);
                const auto polyphonic = translate("notebot.ui.polyphonic", "Polyphonic");
                const auto polyphonicTip = translate("notebot.ui.polyphonic_tip",
                    "Whether or not to allow multiple notes at the same time");
                changed |= boolRow(polyphonic.c_str(),polyphonicTip.c_str(),settings.polyphonic,defaults.polyphonic,width);
                const auto autoRotate = translate("notebot.ui.auto_rotate", "Auto Rotate");
                const auto autoRotateTip = translate("notebot.ui.auto_rotate_tip",
                    "Should client look at note block when it wants to hit it");
                changed |= boolRow(autoRotate.c_str(),autoRotateTip.c_str(),settings.autoRotate,defaults.autoRotate,width);
                const auto autoPlay = translate("notebot.ui.auto_play", "Auto Play");
                const auto autoPlayTip = translate("notebot.ui.auto_play_tip", "Auto plays random songs");
                changed |= boolRow(autoPlay.c_str(),autoPlayTip.c_str(),settings.autoPlay,defaults.autoPlay,width);
                const auto transpose = translate("notebot.ui.transpose", "Transpose Out Of Range");
                const auto transposeTip = translate("notebot.ui.transpose_tip", "Transposes out of range notes");
                changed |= boolRow(transpose.c_str(),transposeTip.c_str(),settings.transposeOutOfRange,defaults.transposeOutOfRange,width);
                const auto swingArm = translate("notebot.ui.swing_arm", "Swing Arm");
                const auto swingArmTip = translate("notebot.ui.swing_arm_tip", "Should swing arm on hit");
                changed |= boolRow(swingArm.c_str(),swingArmTip.c_str(),settings.swingArm,defaults.swingArm,width);
                const auto verifyDelay = translate("notebot.ui.verify_delay", "Final Verify Delay");
                const auto verifyDelayTip = translate("notebot.ui.verify_delay_tip",
                    "Ticks to wait after tuning before checking note blocks");
                changed |= intRow(verifyDelay.c_str(),verifyDelayTip.c_str(),settings.checkNoteblocksAgainDelay,defaults.checkNoteblocksAgainDelay,1,100,width);
            }
            const std::string noteMapSection = translate("notebot.notemap.section", "Note Map");
            if (settings.mode==1 && section(noteMapSection.c_str(),noteMapExpanded_,width)) {
                std::vector<std::string> choices;
                choices.reserve(notebot::kMappedInstrumentCount + 1);
                choices.push_back(translate("notebot.notemap.original", "Original"));
                for (size_t index = 0; index < notebot::kMappedInstrumentCount; ++index)
                    choices.push_back(instrumentLabel(static_cast<notebot::Instrument>(index)));
                std::vector<const char*> choicePointers;
                choicePointers.reserve(choices.size());
                for (const auto& choice : choices) choicePointers.push_back(choice.c_str());
                const std::string mappingTooltip = translate("notebot.notemap.tooltip",
                    "Choose the note-block instrument used for this song instrument.");
                for (size_t index=0;index<settings.noteMap.size();++index) {
                    const auto sourceInstrument = index < 16
                        ? notebot::nbsInstrument(static_cast<uint8_t>(index))
                        : static_cast<notebot::Instrument>(index);
                    const std::string label = instrumentLabel(sourceInstrument);
                    changed |= enumRow(label.c_str(),mappingTooltip.c_str(),
                        settings.noteMap[index],defaults.noteMap[index],choicePointers.data(),
                        static_cast<int>(choices.size()),width);
                }
            }
            const auto renderSection = translate("notebot.ui.render", "Render");
            if (section(renderSection.c_str(),renderExpanded_,width)) {
                const auto hideNowPlaying = translate("notebot.ui.hide_now_playing", "Hide Now Playing");
                const auto showNowPlayingLabel = translate("notebot.ui.show_now_playing", "Show Now Playing");
                if (button(showNowPlaying ? hideNowPlaying.c_str() : showNowPlayingLabel.c_str(), width)) {
                    showNowPlaying = !showNowPlaying;
                    if (actions.nowPlayingVisibilityChanged)
                        actions.nowPlayingVisibilityChanged(showNowPlaying);
                }
                const auto resetNowPlaying = translate("notebot.ui.reset_now_playing", "Reset Now Playing Position");
                if (button(resetNowPlaying.c_str(), width) && actions.resetNowPlayingPosition)
                    actions.resetNowPlayingPosition();
                const auto renderText = translate("notebot.ui.render_text", "Render Text");
                const auto renderTextTip = translate("notebot.ui.render_text_tip",
                    "Whether to render text above noteblocks");
                changed |= boolRow(renderText.c_str(),renderTextTip.c_str(),settings.renderText,defaults.renderText,width);
                const auto renderBoxes = translate("notebot.ui.render_boxes", "Render Boxes");
                const auto renderBoxesTip = translate("notebot.ui.render_boxes_tip",
                    "Whether to render noteblock outlines");
                changed |= boolRow(renderBoxes.c_str(),renderBoxesTip.c_str(),settings.renderBoxes,defaults.renderBoxes,width);
                const auto lines = translate("notebot.ui.lines", "Lines");
                const auto sides = translate("notebot.ui.sides", "Sides");
                const auto both = translate("notebot.ui.both", "Both");
                const char* shapes[]={lines.c_str(),sides.c_str(),both.c_str()};
                const auto shapeMode = translate("notebot.ui.shape_mode", "Shape Mode");
                changed |= enumRow(shapeMode.c_str(),nullptr,settings.shapeMode,defaults.shapeMode,shapes,3,width);
                static constexpr const char* colorKeys[]={
                    "untuned_side_color","untuned_line_color","tuned_side_color","tuned_line_color",
                    "hit_side_color","hit_line_color","scanned_side_color","scanned_line_color",
                    "playing_side_color","playing_line_color","pitch_text_color","remaining_text_color"};
                static constexpr const char* colorFallbacks[]={
                    "Untuned Side Color","Untuned Line Color","Tuned Side Color","Tuned Line Color",
                    "Hit Side Color","Hit Line Color","Scanned Side Color","Scanned Line Color",
                    "Playing Side Color","Playing Line Color","Pitch Text Color","Remaining Text Color"};
                for (int i=0;i<12;++i) {
                    bool openEditor=false;
                    const auto label = translate(std::string("notebot.ui.") + colorKeys[i], colorFallbacks[i]);
                    const auto colorTip = translate("notebot.ui.render_color_tip", "Noteblock render color");
                    changed |= colorRow(label.c_str(),colorTip.c_str(),settings.renderColors[size_t(i)],defaults.renderColors[size_t(i)],settings.rainbowColors[size_t(i)],width,openEditor);
                    if (openEditor) editingColor_=i;
                }
                const auto noteScale = translate("notebot.ui.note_text_scale", "Note Text Scale");
                const auto noteScaleTip = translate("notebot.ui.note_text_scale_tip", "Scale of note text.");
                changed |= floatRow(noteScale.c_str(),noteScaleTip.c_str(),settings.noteTextScale,defaults.noteTextScale,0.5f,3.0f,width);
                const auto noteHeight = translate("notebot.ui.note_text_height", "Note Text Height");
                const auto noteHeightTip = translate("notebot.ui.note_text_height_tip",
                    "Height of note text above blocks.");
                changed |= floatRow(noteHeight.c_str(),noteHeightTip.c_str(),settings.noteTextHeight,defaults.noteTextHeight,0.5f,2.5f,width);
                const auto boxExpansion = translate("notebot.ui.box_expansion", "Box Expansion");
                const auto boxExpansionTip = translate("notebot.ui.box_expansion_tip",
                    "Distance between outline and block face.");
                changed |= floatRow(boxExpansion.c_str(),boxExpansionTip.c_str(),settings.boxExpansion,defaults.boxExpansion,0.0f,0.08f,width);
                const auto showUnmapped = translate("notebot.ui.show_unmapped", "Show Unmapped Boxes");
                const auto showUnmappedTip = translate("notebot.ui.show_unmapped_tip",
                    "Show scanned blocks without a mapped song note");
                changed |= boolRow(showUnmapped.c_str(),showUnmappedTip.c_str(),settings.showScannedNoteblocks,defaults.showScannedNoteblocks,width);
            }
            if (changed && actions.settingsChanged) actions.settingsChanged(settings);
            horizontalSeparator(width);
            const auto openSongGui = translate("notebot.ui.open_song_gui", "Open Song GUI");
            if (button(openSongGui.c_str(),width)) { showSongs=true; showModule=false; focusSearch_=true; }
            const auto alignCenter = translate("notebot.ui.align_center", "Align Center");
            if (button(alignCenter.c_str(),width) && actions.alignCenter) actions.alignCenter();
            const std::string playbackStatus=getStatus();
            ImGui::TextWrapped("%s", playbackStatus.c_str());
            const auto pause = translate("notebot.ui.pause", "Pause");
            const auto play = translate("notebot.ui.play", "Play");
            if (button(playing?pause.c_str():play.c_str(),68) && actions.pauseResume) actions.pauseResume();
            ImGui::SameLine(0,3);
            const auto stop = translate("notebot.ui.stop", "Stop");
            if (button(stop.c_str(),48) && actions.stop) actions.stop();
            if (songLoaded) {
                const auto songLabel = translate("notebot.ui.song", "Song: {0}");
                const auto songText = fmt::format(fmt::runtime(songLabel), songTitle);
                ImGui::TextUnformatted(songText.c_str());
                const auto noteBlocksLabel = translate("notebot.ui.note_blocks", "Note blocks: {0}");
                const auto noteBlocksText = fmt::format(fmt::runtime(noteBlocksLabel), foundNoteBlocks);
                ImGui::TextUnformatted(noteBlocksText.c_str());
                if (requiredNotes > 0 && missingNotes.empty()) {
                    const auto text = fmt::format(fmt::runtime(translate(
                        "notebot.missing.all_mapped", "All {0} required notes mapped")), requiredNotes);
                    ImGui::TextUnformatted(text.c_str());
                }
                else if (!missingNotes.empty()) {
                    const auto text = fmt::format(fmt::runtime(translate(
                        "notebot.missing.summary", "Missing {0} of {1} required notes")),
                        missingNotes.size(), requiredNotes);
                    ImGui::TextUnformatted(text.c_str());
                    const auto title = translate("notebot.missing.title", "Missing notes");
                    if (ImGui::TreeNode("##missing-notes", "%s", title.c_str())) {
                        for (const auto& note : missingNotes) ImGui::BulletText("%s", note.c_str());
                        ImGui::TreePop();
                    }
                }
            }
            const auto scanNoteBlocks = translate("notebot.ui.scan_note_blocks", "Scan NoteBlocks");
            if (button(scanNoteBlocks.c_str(),(width-3)/2) && actions.scanNoteBlocks) actions.scanNoteBlocks();
            ImGui::SameLine(0,3);
            const auto clearNoteBlocks = translate("notebot.ui.clear_note_blocks", "Clear NoteBlocks");
            if (button(clearNoteBlocks.c_str(),(width-3)/2) && actions.clearNoteBlocks) actions.clearNoteBlocks();
            const auto bindSection = translate("notebot.ui.bind", "Bind");
            if (section(bindSection.c_str(),bindExpanded_,width)) {
                const auto bindLabel = translate("notebot.ui.bind_label", "Bind:");
                Row row=beginRow(bindLabel.c_str(),nullptr,width);
                const auto bindingLabel = binding_ ? std::string("...") : keybindLabel(keybind);
                if (button(bindingLabel.c_str(),rowControlWidth(row))) binding_=true;
                if (resetAt(row)) { keybind="L"; binding_=false; if(actions.bindChanged) actions.bindChanged(keybind); }
                finishRow(row);
            }
            ImGui::EndGroup();
            ImGui::Dummy(ImVec2(1,8));
            ImGui::EndChild();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(5);
}

void NotebotUi::drawColorEditor() {
    iconTextures={resetIcon,copyIcon,pasteIcon};
    if (editingColor_<0 || editingColor_>=int(settings.renderColors.size())) { editingColor_=-1; return; }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { editingColor_=-1; return; }
    auto& rgba=settings.renderColors[size_t(editingColor_)];

    const ImGuiViewport* viewport=ImGui::GetMainViewport();
    const float windowHeight=std::min(650.0f,std::max(200.0f,viewport->Size.y-128.0f));
    const float headerHeight=ImGui::GetFontSize()*1.25f+8;
    ImGui::SetNextWindowPos(viewport->GetCenter(),ImGuiCond_FirstUseEver,ImVec2(0.5f,0.5f));
    ImGui::SetNextWindowSize(ImVec2(416,windowHeight),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(300,headerHeight),ImVec2(FLT_MAX,FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(3,3));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(6,6));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,2);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(20/255.f,20/255.f,20/255.f,160/255.f));
    ImGui::PushStyleColor(ImGuiCol_Border,ImVec4(0,0,0,1));
    const auto selectColor = translate("notebot.ui.select_color", "Select Color");
    if (ImGui::Begin("Select Color##meteor",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoScrollbar)) {
        if (!colorLayoutLoaded_) {
            colorExpanded_ = ImGui::GetWindowSize().y > headerHeight + 10.0f;
            colorLayoutLoaded_ = true;
        }
        const bool wasExpanded=colorExpanded_;
        if (wasExpanded) {
            colorExpandedWidth_=ImGui::GetWindowSize().x;
            colorExpandedHeight_=ImGui::GetWindowSize().y;
        }
        const float headerWidth=ImGui::GetContentRegionAvail().x;
        const ImVec2 hp=ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##color-header",ImVec2(headerWidth,headerHeight));
        ImDrawList* draw=ImGui::GetWindowDrawList();
        draw->AddRectFilled(hp,ImVec2(hp.x+headerWidth,hp.y+headerHeight),col(145,61,226));
        const ImVec2 title=ImGui::CalcTextSize(selectColor.c_str());
        draw->AddText(ImVec2(hp.x+(headerWidth-title.x)/2,hp.y+(headerHeight-title.y)/2),col(255,255,255),selectColor.c_str());
        const float tx=hp.x+headerWidth-13,ty=hp.y+headerHeight/2;
        if (colorExpanded_) draw->AddTriangleFilled(ImVec2(tx-5,ty-2),ImVec2(tx+5,ty-2),ImVec2(tx,ty+4),col(20,20,20,200));
        else draw->AddTriangleFilled(ImVec2(tx-2,ty-5),ImVec2(tx-2,ty+5),ImVec2(tx+4,ty),col(20,20,20,200));
        static bool wasDragged=false;
        if (ImGui::IsItemActivated()) wasDragged=false;
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) colorExpanded_=!colorExpanded_;
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left,2.0f)) {
            wasDragged=true;
            const ImVec2 pos=ImGui::GetWindowPos(), delta=ImGui::GetIO().MouseDelta;
            ImGui::SetWindowPos(ImVec2(pos.x+delta.x,pos.y+delta.y));
        }
        if (ImGui::IsItemDeactivated() && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !wasDragged)
            colorExpanded_=!colorExpanded_;
        if (wasExpanded && !colorExpanded_)
            ImGui::SetWindowSize(ImVec2(colorExpandedWidth_,headerHeight));
        else if (!wasExpanded && colorExpanded_)
            ImGui::SetWindowSize(ImVec2(colorExpandedWidth_,colorExpandedHeight_));
        if (colorExpanded_) {
            ImGui::BeginChild("##color-view",ImVec2(0,ImGui::GetContentRegionAvail().y),false,
                              ImGuiWindowFlags_NoScrollbar);
            ImGui::SetCursorPosX(8);
            ImGui::BeginGroup();
            const float width=std::max(260.0f,ImGui::GetContentRegionAvail().x-8.0f);
            bool changed=false;
            const ImVec2 preview=ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##display",ImVec2(width,32));
            draw=ImGui::GetWindowDrawList();
            draw->AddRectFilled(preview,ImVec2(preview.x+width,preview.y+32),
                ImGui::ColorConvertFloat4ToU32(ImVec4(rgba[0],rgba[1],rgba[2],rgba[3])));

            float hue,saturation,value;
            ImGui::ColorConvertRGBtoHSV(rgba[0],rgba[1],rgba[2],hue,saturation,value);
            float hr,hg,hb;
            ImGui::ColorConvertHSVtoRGB(hue,1.0f,1.0f,hr,hg,hb);
            const ImVec2 square=ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##brightness",ImVec2(width,width));
            if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                saturation=std::clamp((ImGui::GetIO().MousePos.x-square.x)/width,0.0f,1.0f);
                value=1.0f-std::clamp((ImGui::GetIO().MousePos.y-square.y)/width,0.0f,1.0f);
                ImGui::ColorConvertHSVtoRGB(hue,saturation,value,rgba[0],rgba[1],rgba[2]);
                changed=true;
            }
            draw=ImGui::GetWindowDrawList();
            draw->AddRectFilledMultiColor(square,ImVec2(square.x+width,square.y+width),
                col(255,255,255),col(int(hr*255),int(hg*255),int(hb*255)),col(0,0,0),col(0,0,0));
            draw->AddRect(ImVec2(square.x+saturation*width-3,square.y+(1-value)*width-3),
                          ImVec2(square.x+saturation*width+3,square.y+(1-value)*width+3),col(255,255,255));

            const ImVec2 strip=ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##hue",ImVec2(width,10));
            if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                hue=std::clamp((ImGui::GetIO().MousePos.x-strip.x)/width,0.0f,1.0f);
                ImGui::ColorConvertHSVtoRGB(hue,saturation,value,rgba[0],rgba[1],rgba[2]);
                changed=true;
            }
            draw=ImGui::GetWindowDrawList();
            for (int i=0;i<6;++i) {
                float r1,g1,b1,r2,g2,b2;
                ImGui::ColorConvertHSVtoRGB(i/6.0f,1,1,r1,g1,b1);
                ImGui::ColorConvertHSVtoRGB((i+1)/6.0f,1,1,r2,g2,b2);
                draw->AddRectFilledMultiColor(ImVec2(strip.x+width*i/6,strip.y),ImVec2(strip.x+width*(i+1)/6,strip.y+10),
                    col(int(r1*255),int(g1*255),int(b1*255)),col(int(r2*255),int(g2*255),int(b2*255)),
                    col(int(r2*255),int(g2*255),int(b2*255)),col(int(r1*255),int(g1*255),int(b1*255)));
            }
            draw->AddRectFilled(ImVec2(strip.x+hue*width-1,strip.y),ImVec2(strip.x+hue*width+1,strip.y+10),col(255,255,255));

            static constexpr const char* labels[]={"R:","G:","B:","A:"};
            for (int i=0;i<4;++i) {
                ImGui::PushID(i);
                int channel=int(std::round(rgba[size_t(i)]*255));
                ImGui::TextUnformatted(labels[i]);
                ImGui::SameLine(42);
                ImGui::SetNextItemWidth(75);
                if (ImGui::InputInt("##channel",&channel,0,0)) { rgba[size_t(i)]=std::clamp(channel,0,255)/255.0f; changed=true; }
                ImGui::SameLine(0,3);
                if (slider("##channel-slider",channel,0,255)) { rgba[size_t(i)]=channel/255.0f; changed=true; }
                ImGui::PopID();
            }
            const auto rainbowLabel = translate("notebot.ui.rainbow", "Rainbow:");
            ImGui::TextUnformatted(rainbowLabel.c_str());
            ImGui::SameLine(width-30);
            if (checkBox("##rainbow",settings.rainbowColors[size_t(editingColor_)])) changed=true;
            if (changed && actions.settingsChanged) actions.settingsChanged(settings);
            const float backWidth=width-99;
            const auto back = translate("notebot.ui.back", "Back");
            if (button(back.c_str(),backWidth)) editingColor_=-1;
            ImGui::SameLine(0,3);
            const auto copyColor = translate("notebot.ui.copy_color", "Copy color");
            if (iconButton("##copy",copyColor.c_str(),1)) {
                std::ostringstream text;
                text << '#' << std::uppercase << std::hex << std::setfill('0');
                for (float channel:rgba) text << std::setw(2) << std::clamp(int(std::round(channel*255)),0,255);
                ImGui::SetClipboardText(text.str().c_str());
            }
            ImGui::SameLine(0,3);
            const auto pasteColor = translate("notebot.ui.paste_color", "Paste color");
            if (iconButton("##paste",pasteColor.c_str(),2)) {
                const char* clipboard=ImGui::GetClipboardText();
                if (clipboard) {
                    const std::string value(clipboard);
                    if (value.size()==9 && value[0]=='#') {
                        try {
                            for (int i=0;i<4;++i) rgba[size_t(i)]=std::stoi(value.substr(size_t(i)*2+1,2),nullptr,16)/255.0f;
                            if (actions.settingsChanged) actions.settingsChanged(settings);
                        } catch (const std::exception&) {}
                    }
                }
            }
            ImGui::SameLine(0,3);
            const auto resetColor = translate("notebot.ui.reset_color", "Reset color");
            if (iconButton("##reset",resetColor.c_str(),0)) {
                rgba=NotebotSettings{}.renderColors[size_t(editingColor_)];
                settings.rainbowColors[size_t(editingColor_)]=false;
                if (actions.settingsChanged) actions.settingsChanged(settings);
            }
            ImGui::EndGroup();
            ImGui::Dummy(ImVec2(1,8));
            ImGui::EndChild();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(5);
}

} // namespace meteor
