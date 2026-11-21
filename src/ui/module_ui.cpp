#include "notebot_ui.h"
#include "translation.h"
#include "meteor_widgets.h"
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


bool button(const char* text, float width) { return widgets::button(text, width); }

bool iconButton(const char* id, const char* tooltip, int kind) {
    return widgets::iconButton(id, kind >= 0 && kind < int(iconTextures.size()) ? iconTextures[size_t(kind)] : 0, kind, tooltip);
}

bool checkBox(const char* id, bool& value) {
    const bool before = value;
    widgets::checkbox(id, value);
    return value != before;
}

void horizontalSeparator(float width) {
    const auto p = ImGui::GetCursorScreenPos();
    widgets::line(p.x, p.y + theme::px(1), width);
    ImGui::Dummy(ImVec2(width, theme::px(3)));
}

bool section(const char* name, bool& expanded, float width) {
    return widgets::section(name, name, &expanded, width);
}

struct Row {
    ImVec2 origin;
    float width;
    float controlX;
};

Row beginRow(const char* label, const char* description, float width) {
    const ImVec2 cursor = ImGui::GetCursorPos();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    const float controlX = std::min(theme::px(rowControlX), std::max(0.f, (width-theme::px(resetButtonWidth)-theme::px(rowControlGap))*0.55f));
    widgets::rowLabel(label, p, controlX - theme::px(rowControlGap), description);
    ImGui::SetCursorPos(ImVec2(cursor.x+controlX,cursor.y));
    return {cursor,width,controlX};
}

float rowControlWidth(const Row& row) {
    return std::max(1.0f,row.width-row.controlX-theme::px(resetButtonWidth)-theme::px(rowControlGap));
}

void finishRow(const Row& row) {
    ImGui::PopID();
    const float usedHeight = ImGui::GetCursorPosY()-row.origin.y;
    ImGui::Dummy(ImVec2(0.0f,std::max(0.0f,theme::px(rowHeight)-usedHeight)));
}

bool resetAt(const Row& row) {
    ImGui::SetCursorPos(ImVec2(row.origin.x+row.width-theme::px(resetButtonWidth),row.origin.y));
    const auto tooltip = translate("notebot.ui.reset", "Reset");
    return iconButton("##reset",tooltip.c_str(),0);
}

bool boolRow(const char* label, const char* description, bool& value, bool defaultValue, float width) {
    Row row=beginRow(label,description,width);
    bool changed=checkBox("##value",value);
    if (resetAt(row)) { value=defaultValue; changed=true; }
    finishRow(row); return changed;
}

bool slider(const char* id, int& value, int minValue, int maxValue, float width=theme::px(200)) {
    const ImVec2 p=ImGui::GetCursorScreenPos(), size(width,theme::px(30));
    ImGui::InvisibleButton(id,size);
    const bool hovered=ImGui::IsItemHovered(), held=ImGui::IsItemActive();
    bool changed=false;
    if (held && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const float fraction=std::clamp((ImGui::GetIO().MousePos.x-p.x-theme::px(9))/(size.x-theme::px(18)),0.0f,1.0f);
        const int next=minValue+int(std::round(fraction*(maxValue-minValue)));
        changed=next!=value; value=next;
    }
    const float fraction=std::clamp(float(value-minValue)/std::max(1,maxValue-minValue),0.0f,1.0f);
    const float handleX=p.x+theme::px(9)+fraction*(size.x-theme::px(18));
    ImDrawList* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(ImVec2(p.x+theme::px(9),p.y+theme::px(13)),ImVec2(handleX,p.y+theme::px(16)),theme::color(theme::UiColor::SliderLeft));
    draw->AddRectFilled(ImVec2(handleX,p.y+theme::px(13)),ImVec2(p.x+size.x-theme::px(9),p.y+theme::px(16)),theme::color(theme::UiColor::SliderRight));
    draw->AddCircleFilled(ImVec2(handleX,p.y+theme::px(15)),theme::px(9),theme::color(held ? theme::UiColor::SliderHandlePressed : hovered ? theme::UiColor::SliderHandleHovered : theme::UiColor::SliderHandle));
    return changed;
}

bool intRow(const char* label, const char* description, int& value, int def, int minValue, int maxValue, float width) {
    Row row=beginRow(label,description,width);
    ImGui::SetNextItemWidth(theme::px(75));
    bool changed=ImGui::InputInt("##text",&value,0,0);
    value=std::max(value,minValue);
    ImGui::SameLine(0,theme::px(3));
    changed |= slider("##slider",value,minValue,maxValue,rowControlWidth(row)-theme::px(78));
    if (resetAt(row)) { value=def; changed=true; }
    finishRow(row); return changed;
}

bool floatRow(const char* label, const char* description, float& value, float def, float minValue, float maxValue, float width) {
    Row row = beginRow(label, description, width);
    ImGui::SetNextItemWidth(theme::px(75));
    bool changed = ImGui::InputFloat("##text", &value, 0, 0, "%.2f");
    value = std::max(value, minValue);
    ImGui::SameLine(0, theme::px(3));
    const float before = value;
    widgets::slider("##slider", value, minValue, maxValue, std::max(theme::px(20), rowControlWidth(row) - theme::px(78)));
    changed |= before != value;
    if (resetAt(row)) { value = def; changed = true; }
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
    ImGui::InvisibleButton("##swatch",ImVec2(theme::px(32),theme::px(32)));
    ImGui::GetWindowDrawList()->AddRectFilled(p,ImVec2(p.x+theme::px(32),p.y+theme::px(32)),
        ImGui::ColorConvertFloat4ToU32(ImVec4(value[0],value[1],value[2],value[3])));
    if (ImGui::IsItemClicked()) openEditor=true;
    ImGui::SameLine(0,theme::px(3));
    const auto editLabel = translate("notebot.ui.edit", "Edit");
    if (button(editLabel.c_str(),theme::px(46))) openEditor=true;
    bool changed=false;
    if (resetAt(row)) { value=def; rainbowEnabled=false; changed=true; }
    finishRow(row); return changed;
}

void moduleHeader(bool& expanded, float) { widgets::windowHeader("Notebot", expanded); }

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
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { showModule=false; return; }

    const ImGuiViewport* viewport=ImGui::GetMainViewport();
    const float height=std::min(theme::px(660),theme::windowHeightLimit());
    const float headerHeight=ImGui::GetFontSize()*1.25f+theme::px(8);
    ImGui::SetNextWindowPos(theme::windowCenter(),ImGuiCond_FirstUseEver,ImVec2(0.5f,0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(theme::px(650), viewport->Size.x-16),height),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(theme::px(420), viewport->Size.x-16),headerHeight),ImVec2(FLT_MAX,FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(theme::px(3),theme::px(3)));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(theme::px(6),theme::px(6)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,theme::px(2));
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
            const float contentPadding=theme::px(8);
            ImGui::SetCursorPosX(contentPadding);
            ImGui::BeginGroup();
            const float width=std::max(1.0f,ImGui::GetContentRegionAvail().x-contentPadding);
            const auto description = translate("notebot.ui.description",
                "Play songs with nearby note blocks");
            ImGui::TextUnformatted(description.c_str());
            const auto github = translate("notebot.ui.github", "GitHub");
            ImGui::Text("%s: https://github.com/PuceLi/NoteBot", github.c_str());
            bool changed=false;
            const NotebotSettings defaults{};
            const auto general = translate("notebot.ui.general", "General");
            if (section(general.c_str(),generalExpanded_,width)) {
                const auto playbackSpeed = translate("notebot.ui.playback_speed", "Playback Speed");
                const auto playbackSpeedTip = translate("notebot.ui.playback_speed_tip",
                    "Playback speed during playing or previewing");
                changed |= floatRow(playbackSpeed.c_str(),playbackSpeedTip.c_str(),settings.playbackSpeed,defaults.playbackSpeed,0.02f,2.0f,width);
                settings.playbackSpeed = std::round(settings.playbackSpeed * 100.0f) / 100.0f;
                const auto tuneInterval = translate("notebot.ui.tune_interval", "Tune Interval (ticks)");
                const auto tuneIntervalTip = translate("notebot.ui.tune_interval_tip",
                    "Extra world ticks between confirmed tuning clicks (0 = fastest).");
                changed |= intRow(tuneInterval.c_str(),tuneIntervalTip.c_str(),settings.tuneIntervalTicks,defaults.tuneIntervalTicks,0,20,width);
                const auto concurrentTune = translate("notebot.ui.concurrent_tune", "Concurrent Tune Blocks");
                const auto concurrentTuneTip = translate("notebot.ui.concurrent_tune_tip",
                    "Number of distinct note blocks adjusted simultaneously; overly high values may trigger anti-cheat.");
                changed |= intRow(concurrentTune.c_str(),concurrentTuneTip.c_str(),settings.concurrentTuneBlocks,defaults.concurrentTuneBlocks,1,20,width);
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
                const auto transpose = translate("notebot.ui.transpose", "Octave Conversion");
                const auto transposeTip = translate("notebot.ui.transpose_tip", "Move only out-of-range notes by whole octaves into the playable range. Preview keeps the original pitches. Changing this setting remaps blocks and restarts playback.");
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
            if (button(playing?pause.c_str():play.c_str(),theme::px(68)) && actions.pauseResume) actions.pauseResume();
            ImGui::SameLine(0,theme::px(3));
            const auto stop = translate("notebot.ui.stop", "Stop");
            if (button(stop.c_str(),theme::px(48)) && actions.stop) actions.stop();
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
            if (button(scanNoteBlocks.c_str(),(width-theme::px(3))/2) && actions.scanNoteBlocks) actions.scanNoteBlocks();
            ImGui::SameLine(0,theme::px(3));
            const auto clearNoteBlocks = translate("notebot.ui.clear_note_blocks", "Clear NoteBlocks");
            if (button(clearNoteBlocks.c_str(),(width-theme::px(3))/2) && actions.clearNoteBlocks) actions.clearNoteBlocks();
            ImGui::EndGroup();
            ImGui::Dummy(ImVec2(1,theme::px(8)));
            ImGui::EndChild();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(5);
}

void NotebotUi::drawRender() {
    iconTextures = {resetIcon, copyIcon, pasteIcon};
    const auto* viewport = ImGui::GetMainViewport();
    const float headerHeight = widgets::headerHeight();
    const float height = std::min(theme::px(660),theme::windowHeightLimit());
    ImGui::SetNextWindowPos(theme::windowCenter(), ImGuiCond_FirstUseEver, ImVec2(.5f, .5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(theme::px(650), viewport->Size.x-16), height), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(theme::px(420), viewport->Size.x-16), headerHeight), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(theme::px(3),theme::px( 3)));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(theme::px(6),theme::px( 6)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, theme::px(2));
    const auto title = translate("notebot.ui.render", "Render");
    if (ImGui::Begin((title + "###notebot-render").c_str(), nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar)) {
        auto& state = renderWindow_;
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
            ImGui::BeginChild("##render-content", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
            ImGui::SetCursorPosX(theme::px(8));
            ImGui::BeginGroup();
            const float width = std::max(1.f, ImGui::GetContentRegionAvail().x - 8);
            const NotebotSettings defaults;
            bool changed = false;
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
            const auto throughWalls = translate("notebot.ui.render_through_walls", "Render Through Walls");
            const auto throughWallsTip = translate("notebot.ui.render_through_walls_tip",
                "Show note-block outlines and filled faces through walls");
            changed |= boolRow(throughWalls.c_str(),throughWallsTip.c_str(),settings.renderBoxThroughWalls,defaults.renderBoxThroughWalls,width);
            const auto hitFade = translate("notebot.ui.hit_fade", "Hit Fade Time (seconds)");
            const auto hitFadeTip = translate("notebot.ui.hit_fade_tip",
                "Time for playback highlights to fade back to the normal block color");
            changed |= floatRow(hitFade.c_str(),hitFadeTip.c_str(),settings.hitFadeSeconds,defaults.hitFadeSeconds,0.0f,5.0f,width);
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
            if (changed && actions.settingsChanged) actions.settingsChanged(settings);
            ImGui::EndGroup();
            ImGui::Dummy(ImVec2(1,theme::px( 8)));
            ImGui::EndChild();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(5);
}

void NotebotUi::drawColorEditor() {
    iconTextures={resetIcon,copyIcon,pasteIcon};
    if (editingColor_<0 || editingColor_>=int(settings.renderColors.size())) { editingColor_=-1; return; }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { editingColor_=-1; return; }
    auto& rgba=settings.renderColors[size_t(editingColor_)];

    const ImGuiViewport* viewport=ImGui::GetMainViewport();
    const float windowHeight=std::min(theme::px(650),theme::windowHeightLimit());
    const float headerHeight=ImGui::GetFontSize()*1.25f+theme::px(8);
    ImGui::SetNextWindowPos(theme::windowCenter(),ImGuiCond_FirstUseEver,ImVec2(0.5f,0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(theme::px(416), viewport->Size.x-16),windowHeight),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(theme::px(300), viewport->Size.x-16),headerHeight),ImVec2(FLT_MAX,FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(theme::px(3),theme::px(3)));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(theme::px(6),theme::px(6)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,theme::px(2));
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
        widgets::windowHeader(selectColor, colorExpanded_);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        if (wasExpanded && !colorExpanded_)
            ImGui::SetWindowSize(ImVec2(colorExpandedWidth_,headerHeight));
        else if (!wasExpanded && colorExpanded_)
            ImGui::SetWindowSize(ImVec2(colorExpandedWidth_,colorExpandedHeight_));
        if (colorExpanded_) {
            ImGui::BeginChild("##color-view",ImVec2(0,ImGui::GetContentRegionAvail().y),false,
                              ImGuiWindowFlags_NoScrollbar);
            ImGui::SetCursorPosX(theme::px(8));
            ImGui::BeginGroup();
            const float width=std::max(theme::px(260),ImGui::GetContentRegionAvail().x-theme::px(8));
            bool changed=false;
            const ImVec2 preview=ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##display",ImVec2(width,theme::px(32)));
            draw=ImGui::GetWindowDrawList();
            draw->AddRectFilled(preview,ImVec2(preview.x+width,preview.y+theme::px(32)),
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
            draw->AddRect(ImVec2(square.x+saturation*width-theme::px(3),square.y+(1-value)*width-theme::px(3)),
                          ImVec2(square.x+saturation*width+theme::px(3),square.y+(1-value)*width+theme::px(3)),col(255,255,255));

            const ImVec2 strip=ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##hue",ImVec2(width,theme::px(10)));
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
                draw->AddRectFilledMultiColor(ImVec2(strip.x+width*i/6,strip.y),ImVec2(strip.x+width*(i+1)/6,strip.y+theme::px(10)),
                    col(int(r1*255),int(g1*255),int(b1*255)),col(int(r2*255),int(g2*255),int(b2*255)),
                    col(int(r2*255),int(g2*255),int(b2*255)),col(int(r1*255),int(g1*255),int(b1*255)));
            }
            draw->AddRectFilled(ImVec2(strip.x+hue*width-theme::px(1),strip.y),ImVec2(strip.x+hue*width+theme::px(1),strip.y+theme::px(10)),col(255,255,255));

            static constexpr const char* labels[]={"R:","G:","B:","A:"};
            for (int i=0;i<4;++i) {
                ImGui::PushID(i);
                int channel=int(std::round(rgba[size_t(i)]*255));
                ImGui::TextUnformatted(labels[i]);
                ImGui::SameLine(theme::px(42));
                ImGui::SetNextItemWidth(theme::px(75));
                if (ImGui::InputInt("##channel",&channel,0,0)) { rgba[size_t(i)]=std::clamp(channel,0,255)/255.0f; changed=true; }
                ImGui::SameLine(0,theme::px(3));
                if (slider("##channel-slider",channel,0,255)) { rgba[size_t(i)]=channel/255.0f; changed=true; }
                ImGui::PopID();
            }
            const auto rainbowLabel = translate("notebot.ui.rainbow", "Rainbow:");
            ImGui::TextUnformatted(rainbowLabel.c_str());
            ImGui::SameLine(width-theme::px(30));
            if (checkBox("##rainbow",settings.rainbowColors[size_t(editingColor_)])) changed=true;
            if (changed && actions.settingsChanged) actions.settingsChanged(settings);
            const float backWidth=width-theme::px(99);
            const auto back = translate("notebot.ui.back", "Back");
            if (button(back.c_str(),backWidth)) editingColor_=-1;
            ImGui::SameLine(0,theme::px(3));
            const auto copyColor = translate("notebot.ui.copy_color", "Copy color");
            if (iconButton("##copy",copyColor.c_str(),1)) {
                std::ostringstream text;
                text << '#' << std::uppercase << std::hex << std::setfill('0');
                for (float channel:rgba) text << std::setw(2) << std::clamp(int(std::round(channel*255)),0,255);
                ImGui::SetClipboardText(text.str().c_str());
            }
            ImGui::SameLine(0,theme::px(3));
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
            ImGui::SameLine(0,theme::px(3));
            const auto resetColor = translate("notebot.ui.reset_color", "Reset color");
            if (iconButton("##reset",resetColor.c_str(),0)) {
                rgba=NotebotSettings{}.renderColors[size_t(editingColor_)];
                settings.rainbowColors[size_t(editingColor_)]=false;
                if (actions.settingsChanged) actions.settingsChanged(settings);
            }
            ImGui::EndGroup();
            ImGui::Dummy(ImVec2(1,theme::px(8)));
            ImGui::EndChild();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(5);
}

} // namespace meteor
