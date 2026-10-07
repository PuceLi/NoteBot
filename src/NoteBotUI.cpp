#include "NoteBotUI.h"
#include "NoteBotPlayer.h"
#include "Config.h"
#include "Main.h"
#include "ui/notebot_ui.h"
#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <iterator>
#include <string_view>
#include <windows.h>

namespace fs = std::filesystem;

namespace notebot {

namespace {
bool clickedOutsideImGui() {
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left) || !ImGui::IsMousePosValid())
        return false;
    const ImVec2 mouse = ImGui::GetMousePos();
    for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows) {
        if (!window->Active || window->Hidden || (window->Flags & ImGuiWindowFlags_NoInputs))
            continue;
        const ImVec2 min = window->Pos;
        const ImVec2 max(min.x + window->Size.x, min.y + window->Size.y);
        if (mouse.x >= min.x && mouse.x < max.x && mouse.y >= min.y && mouse.y < max.y)
            return false;
    }
    return true;
}

std::string toUtf8(const fs::path& path) {
    const auto bytes = path.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

fs::path fromUtf8(std::string_view text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

const char* instrumentName(int value) {
    static constexpr const char* names[] = {
        "Harp", "Bass", "Bass Drum", "Snare", "Hat", "Guitar", "Flute", "Bell",
        "Chime", "Xylophone", "Iron Xylophone", "Cow Bell", "Didgeridoo", "Bit", "Banjo", "Pling"
    };
    return value >= 0 && value < static_cast<int>(std::size(names)) ? names[value] : "Unknown";
}

int keyCodeFromName(std::string_view name) {
    if (name == "None") return 0;
    if (name.size() == 1 && std::isalnum(static_cast<unsigned char>(name[0])))
        return std::toupper(static_cast<unsigned char>(name[0]));
    if (name.size() >= 2 && name[0] == 'F') {
        int number = 0;
        for (size_t index = 1; index < name.size(); ++index) {
            if (!std::isdigit(static_cast<unsigned char>(name[index]))) return -1;
            number = number * 10 + name[index] - '0';
        }
        if (number >= 1 && number <= 24) return VK_F1 + number - 1;
    }
    if (name == "Space") return VK_SPACE;
    if (name == "Tab") return VK_TAB;
    if (name == "Enter") return VK_RETURN;
    if (name == "Insert") return VK_INSERT;
    if (name == "Delete") return VK_DELETE;
    if (name == "Home") return VK_HOME;
    if (name == "End") return VK_END;
    return -1;
}

std::string keyNameFromCode(int code) {
    if (code == 0) return "None";
    if (code >= 'A' && code <= 'Z') return std::string(1, static_cast<char>(code));
    if (code >= '0' && code <= '9') return std::string(1, static_cast<char>(code));
    if (code >= VK_F1 && code <= VK_F24) return "F" + std::to_string(code - VK_F1 + 1);
    if (code == VK_SPACE) return "Space";
    if (code == VK_TAB) return "Tab";
    if (code == VK_RETURN) return "Enter";
    if (code == VK_INSERT) return "Insert";
    if (code == VK_DELETE) return "Delete";
    if (code == VK_HOME) return "Home";
    if (code == VK_END) return "End";
    return std::to_string(code);
}
}

NoteBotUI::NoteBotUI() {
    auto folder = fromUtf8(getConfig().general.songFolder);
    const auto dataSongs = getDataDir() / "songs";
    if (folder.empty() || folder.is_relative()) folder = getDataDir() / folder;
    if (folder.filename() == "songs" && !fs::exists(folder) && fs::exists(dataSongs)) folder = dataSongs;
    const auto folderString = toUtf8(folder.lexically_normal());
    strncpy_s(mSongFolderPath, folderString.c_str(), sizeof(mSongFolderPath) - 1);
    scanSongFolder();
    mMeteorUI = std::make_unique<meteor::NotebotUi>(folder);
}

NoteBotUI::~NoteBotUI() = default;

void NoteBotUI::setIconTextures(std::uintptr_t reset, std::uintptr_t copy, std::uintptr_t paste) {
    if (!mMeteorUI) return;
    mMeteorUI->resetIcon = reset;
    mMeteorUI->copyIcon = copy;
    mMeteorUI->pasteIcon = paste;
}

void NoteBotUI::toggle() { setVisible(!isVisible()); }

void NoteBotUI::setVisible(bool visible) {
    mVisible.store(visible, std::memory_order_release);
    if (visible) mOpenModuleRequested.store(true, std::memory_order_release);
}

void NoteBotUI::setPlayer(NoteBotPlayer* player) {
    mPlayer = player;
    if (!mMeteorUI || !mPlayer) return;
    syncMeteorSettingsFromConfig();
    mMeteorUI->keybind = keyNameFromCode(mPlayer->getConfig().keybinds.hotkey);
    mMeteorUI->active = true;
    auto& actions = mMeteorUI->actions;
    actions.loadSong = [this](const fs::path& path) { mPlayer->loadSong(path); };
    actions.pauseResume = [this] {
        const auto state = mPlayer->getDisplayState();
        if (!state.hasSong) return;
        if (state.isPlaying) mPlayer->pause(); else mPlayer->play();
    };
    actions.stop = [this] { mPlayer->stop(); };
    actions.scanNoteBlocks = [this] {
        mPlayer->requestScanNearbyNoteBlocks(mPlayer->getConfig().general.scanRadius);
    };
    actions.clearNoteBlocks = [this] { mPlayer->requestClearNoteBlocks(); };
    actions.alignCenter = [] {
        const auto* viewport = ImGui::GetMainViewport();
        ImGui::SetWindowPos("Notebot##module", ImVec2(viewport->GetCenter().x - 325.0f,
                                                      viewport->GetCenter().y - 330.0f));
    };
    actions.resetPlaybackHudPosition = [this] { mResetPlaybackHudRequested = true; };
    actions.playbackHudVisibilityChanged = [this](bool visible) {
        mPlayer->getConfig().showPlaybackHud = visible;
        markConfigEdited();
    };
    actions.settingsChanged = [this](const meteor::NotebotSettings&) { applyMeteorSettings(); };
    actions.bindChanged = [this](const std::string& name) {
        const int code = keyCodeFromName(name);
        if (code < 0) {
            mMeteorUI->keybind = keyNameFromCode(mPlayer->getConfig().keybinds.hotkey);
            return;
        }
        mPlayer->getConfig().keybinds.hotkey = code;
        markConfigEdited();
    };
}

void NoteBotUI::syncMeteorSettingsFromConfig() {
    if (!mPlayer || !mMeteorUI) return;
    const auto& config = mPlayer->getConfig();
    auto& settings = mMeteorUI->settings;
    mMeteorUI->showPlaybackHud = config.showPlaybackHud;
    settings.tickDelay = config.tickDelay;
    settings.tuneIntervalTicks = config.tuneIntervalTicks;
    settings.mode = config.mode == NoteBotConfig::NotebotMode::Manual ? 1 : 0;
    settings.playingMode = static_cast<int>(config.playingMode);
    settings.polyphonic = config.polyphonic;
    settings.autoRotate = config.autoRotate;
    settings.autoPlay = config.autoPlay;
    settings.transposeOutOfRange = config.transposeOutOfRange;
    settings.swingArm = config.swingArm;
    settings.checkNoteblocksAgainDelay = config.checkNoteblocksDelay;
    settings.renderText = config.renderText;
    settings.renderBoxes = config.renderBoxes;
    settings.shapeMode = config.renderBoxLines && config.renderBoxFaces ? 2
                       : config.renderBoxFaces ? 1 : 0;
    settings.noteTextScale = config.noteTextScale;
    settings.noteTextHeight = config.noteTextHeight;
    settings.boxExpansion = config.boxExpansion;
    settings.showScannedNoteblocks = config.showUnmappedBoxes;
    settings.renderColors = {{
        config.colors.untunedSide, config.colors.untunedLine,
        config.colors.tunedSide, config.colors.tunedLine,
        config.colors.tuneHitSide, config.colors.tuneHitLine,
        config.colors.scannedSide, config.colors.scannedLine,
        config.colors.playingSide, config.colors.playingLine,
        config.pitchTextColor, config.remainingTextColor
    }};
}

void NoteBotUI::applyMeteorSettings() {
    if (!mPlayer || !mMeteorUI) return;
    const auto& settings = mMeteorUI->settings;
    auto& config = mPlayer->getConfig();
    config.tickDelay = settings.tickDelay;
    config.tuneIntervalTicks = settings.tuneIntervalTicks;
    config.mode = settings.mode == 1 ? NoteBotConfig::NotebotMode::Manual
                                      : NoteBotConfig::NotebotMode::Auto;
    config.playingMode = static_cast<NoteBotConfig::PlayingMode>(settings.playingMode);
    config.polyphonic = settings.polyphonic;
    config.autoRotate = settings.autoRotate;
    config.autoPlay = settings.autoPlay;
    config.transposeOutOfRange = settings.transposeOutOfRange;
    config.swingArm = settings.swingArm;
    config.checkNoteblocksDelay = settings.checkNoteblocksAgainDelay;
    config.renderText = settings.renderText;
    config.renderBoxes = settings.renderBoxes;
    config.renderBoxLines = settings.shapeMode != 1;
    config.renderBoxFaces = settings.shapeMode != 0;
    config.noteTextScale = settings.noteTextScale;
    config.noteTextHeight = settings.noteTextHeight;
    config.boxExpansion = settings.boxExpansion;
    config.showUnmappedBoxes = settings.showScannedNoteblocks;
    config.colors.untunedSide = settings.renderColors[0];
    config.colors.untunedLine = settings.renderColors[1];
    config.colors.tunedSide = settings.renderColors[2];
    config.colors.tunedLine = settings.renderColors[3];
    config.colors.tuneHitSide = settings.renderColors[4];
    config.colors.tuneHitLine = settings.renderColors[5];
    config.colors.scannedSide = settings.renderColors[6];
    config.colors.scannedLine = settings.renderColors[7];
    config.colors.playingSide = settings.renderColors[8];
    config.colors.playingLine = settings.renderColors[9];
    config.pitchTextColor = settings.renderColors[10];
    config.remainingTextColor = settings.renderColors[11];
    if (sanitizeConfig(config)) syncMeteorSettingsFromConfig();
    markConfigEdited();
}

void NoteBotUI::render() {
    if (!ImGui::GetCurrentContext()) return;
    if (isVisible() && mMeteorUI) {
        if (mOpenModuleRequested.exchange(false, std::memory_order_acq_rel) &&
            !mMeteorUI->showModule && !mMeteorUI->showSongs)
            mMeteorUI->showModule = true;
        if (mPlayer && sanitizeConfig(mPlayer->getConfig())) {
            NoteBot::getInstance().getSelf().getLogger().warn("Invalid UI config values were repaired");
            syncMeteorSettingsFromConfig();
            markConfigEdited();
        }
        if (mPlayer) {
            const auto display = mPlayer->getDisplayState();
            auto& manager = mPlayer->getNoteBlockManager();
            const auto diagnostics = manager.getMappingDiagnostics();
            mMeteorUI->songLoaded = display.hasSong;
            mMeteorUI->playing = display.isPlaying;
            mMeteorUI->currentTick = display.currentTick;
            mMeteorUI->lastTick = display.totalTicks;
            mMeteorUI->songTitle = display.songTitle;
            mMeteorUI->songAuthor = display.songAuthor;
            mMeteorUI->statusOverride = display.status;
            mMeteorUI->foundNoteBlocks = manager.getNoteBlockCount();
            mMeteorUI->requiredNotes = diagnostics.required;
            mMeteorUI->missingNotes.clear();
            mMeteorUI->missingNotes.reserve(diagnostics.missing.size());
            for (const auto& note : diagnostics.missing) {
                if (mPlayer->getConfig().mode == NoteBotConfig::NotebotMode::Auto)
                    mMeteorUI->missingNotes.push_back("Pitch " + std::to_string(note.pitch));
                else
                    mMeteorUI->missingNotes.push_back(std::string(instrumentName(note.instrument)) +
                                                       ", pitch " + std::to_string(note.pitch));
            }
        }
        mMeteorUI->draw();
        mVisible.store(mMeteorUI->showModule || mMeteorUI->showSongs,
                       std::memory_order_release);
    }
    if (mPlayer) renderPlaybackHud(mPlayer->getDisplayState());
    if (isVisible() && clickedOutsideImGui())
        setVisible(false);
    if (mConfigDirty && std::chrono::steady_clock::now() - mLastConfigEdit >=
                            std::chrono::milliseconds(750)) {
        try {
            saveConfig();
            mConfigDirty = false;
        } catch (const std::exception& error) {
            NoteBot::getInstance().getSelf().getLogger().warn("Failed to save UI config: {}", error.what());
            mLastConfigEdit = std::chrono::steady_clock::now();
        }
    }
}

void NoteBotUI::renderPlaybackHud(const NoteBotPlayer::DisplayState& display) {
    if (!display.isPlaying || !mPlayer->getConfig().showPlaybackHud) return;
    const auto* viewport = ImGui::GetMainViewport();
    const auto defaultPosition = ImVec2(viewport->GetCenter().x, viewport->Pos.y + 72.0f);
    if (mResetPlaybackHudRequested) {
        ImGui::SetNextWindowPos(defaultPosition, ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        mResetPlaybackHudRequested = false;
    } else {
        ImGui::SetNextWindowPos(defaultPosition, ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.0f));
    }
    const auto& style = ImGui::GetStyle();
    const float hudHeight = std::max(72.0f, style.WindowPadding.y * 2.0f +
        ImGui::GetFrameHeight() * 2.0f + style.ItemSpacing.y + 4.0f);
    ImGui::SetNextWindowSize(ImVec2(360.0f, hudHeight), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(240.0f, hudHeight), ImVec2(FLT_MAX, hudHeight));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoScrollbar;
    if (!isVisible()) flags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                               ImGuiWindowFlags_NoFocusOnAppearing;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(20/255.f, 20/255.f, 20/255.f, 0.55f));
    bool hideRequested = false;
    if (ImGui::Begin("Now Playing##notebot-hud", nullptr, flags)) {
        const bool editable = isVisible();
        const float closeWidth = editable ? ImGui::CalcTextSize("X").x +
            style.FramePadding.x * 2.0f + style.ItemSpacing.x : 0.0f;
        const ImVec2 titlePos = ImGui::GetCursorScreenPos();
        const float titleWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x - closeWidth);
        ImGui::InvisibleButton("##playback-hud-drag", ImVec2(titleWidth, ImGui::GetFrameHeight()));
        if (editable && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
            const ImVec2 pos = ImGui::GetWindowPos();
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            ImGui::SetWindowPos(ImVec2(pos.x + delta.x, pos.y + delta.y));
        }
        const char* title = display.songTitle.empty() ? "NoteBot" : display.songTitle.c_str();
        const float titleY = titlePos.y + (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight()) * 0.5f;
        ImGui::GetWindowDrawList()->PushClipRect(titlePos,
            ImVec2(titlePos.x + titleWidth, titlePos.y + ImGui::GetFrameHeight()), true);
        ImGui::GetWindowDrawList()->AddText(ImVec2(titlePos.x, titleY),
                                             ImGui::GetColorU32(ImGuiCol_Text), title);
        ImGui::GetWindowDrawList()->PopClipRect();
        if (editable) {
            ImGui::SameLine();
            hideRequested = ImGui::SmallButton("X##hide-playback-hud");
        }
        const float progress = display.totalTicks > 0
            ? std::clamp(static_cast<float>(display.currentTick) /
                         static_cast<float>(display.totalTicks), 0.0f, 1.0f)
            : 0.0f;
        const std::string progressText = std::to_string(display.currentTick) + " / " +
                                         std::to_string(display.totalTicks);
        ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f), progressText.c_str());
    }
    ImGui::End();
    ImGui::PopStyleColor();
    if (hideRequested) {
        mPlayer->getConfig().showPlaybackHud = false;
        if (mMeteorUI) mMeteorUI->showPlaybackHud = false;
        markConfigEdited();
    }
}

void NoteBotUI::markConfigEdited() {
    mConfigDirty = true;
    mLastConfigEdit = std::chrono::steady_clock::now();
}

void NoteBotUI::renderMainWindow() {
    ImGui::SetNextWindowSize(ImVec2(600, 550), ImGuiCond_FirstUseEver);

    bool windowOpen = isVisible();
    if (!ImGui::Begin("NoteBot", &windowOpen)) {
        if (!windowOpen) setVisible(false);
        ImGui::End();
        return;
    }
    if (!windowOpen) setVisible(false);

    if (ImGui::BeginTabBar("NoteBotTabs")) {
        if (ImGui::BeginTabItem("Player")) {
            renderPlayerTab();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Settings")) {
            renderSettingsTab();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Render")) {
            renderRenderTab();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

void NoteBotUI::renderPlayerTab() {
    if (!mPlayer) {
        ImGui::TextDisabled("Player not initialized");
        return;
    }

    ImGui::SeparatorText("Player Status");

    const auto display = mPlayer->getDisplayState();
    if (display.hasSong) {
        ImGui::Text("Song: %s", display.songTitle.c_str());
        ImGui::Text("Author: %s", display.songAuthor.c_str());
        ImGui::Text("Status: %s", display.status.c_str());

        if (display.totalTicks > 0) {
            float progress = static_cast<float>(display.currentTick) /
                           static_cast<float>(display.totalTicks);
            ImGui::ProgressBar(progress, ImVec2(-1, 0), "");
            ImGui::Text("Tick: %d / %d", display.currentTick, display.totalTicks);
        }

        ImGui::Spacing();

        if (display.isPlaying) {
            if (ImGui::Button("Pause", ImVec2(80, 0))) {
                mPlayer->pause();
            }
        } else {
            if (ImGui::Button("Play", ImVec2(80, 0))) {
                mPlayer->play();
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Stop", ImVec2(80, 0))) {
            mPlayer->stop();
        }

        ImGui::Spacing();

        if (ImGui::Button("Scan NoteBlocks", ImVec2(150, 0))) {
            mPlayer->requestScanNearbyNoteBlocks(32);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Scan nearby noteblocks and map them to the song");
        }

        ImGui::SameLine();
        if (ImGui::Button("Clear NoteBlocks", ImVec2(150, 0))) {
            mPlayer->requestClearNoteBlocks();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Clear all scanned noteblocks and stop rendering");
        }

        const auto& manager = mPlayer->getNoteBlockManager();
        ImGui::Text("Found %zu noteblocks", manager.getNoteBlockCount());
        const auto diagnostics = manager.getMappingDiagnostics();
        if (diagnostics.required > 0 && diagnostics.missing.empty()) {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "All %zu required notes mapped", diagnostics.required);
        } else if (!diagnostics.missing.empty()) {
            ImGui::TextColored(ImVec4(1, 0.75f, 0, 1), "Missing %zu of %zu required note blocks",
                               diagnostics.missing.size(), diagnostics.required);
            if (ImGui::TreeNode("Missing notes")) {
                for (const auto& note : diagnostics.missing) {
                    if (mPlayer->getConfig().mode == NoteBotConfig::NotebotMode::Auto) {
                        ImGui::BulletText("Pitch %d", note.pitch);
                    } else {
                        ImGui::BulletText("%s, pitch %d", instrumentName(note.instrument), note.pitch);
                    }
                }
                ImGui::TreePop();
            }
        }
    } else {
        ImGui::TextDisabled("No song loaded");
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Song Library");

    ImGui::Text("Song Folder:");
    ImGui::InputText("##folder", mSongFolderPath, sizeof(mSongFolderPath));

    ImGui::SameLine();
    if (ImGui::Button("Scan")) {
        scanSongFolder();
    }

    ImGui::Spacing();
    ImGui::Text("Songs (%zu):", mSongFiles.size());

    if (mSongFiles.empty()) {
        ImGui::TextDisabled("No .nbs files found in this folder");
    }

    ImGui::BeginChild("SongList", ImVec2(0, 200), true);

    for (size_t i = 0; i < mSongFiles.size(); ++i) {
        const auto& songPath = mSongFiles[i];
        std::string filename = toUtf8(songPath.filename());

        bool isSelected = (static_cast<int>(i) == mSelectedSongIndex);
        if (ImGui::Selectable(filename.c_str(), isSelected)) {
            mSelectedSongIndex = static_cast<int>(i);
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            loadSelectedSong();
        }
    }

    ImGui::EndChild();

    if (mSelectedSongIndex >= 0 && mSelectedSongIndex < static_cast<int>(mSongFiles.size())) {
        if (ImGui::Button("Load Selected Song")) {
            loadSelectedSong();
        }
    }
}

void NoteBotUI::renderSettingsTab() {
    if (!mPlayer) return;

    auto& config = mPlayer->getConfig();
    bool edited = false;

    ImGui::SeparatorText("Playback Settings");

    edited |= ImGui::SliderInt("Tick Delay", &config.tickDelay, 0, 20);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Delay between each tick (0 = fastest)");
    }

    edited |= ImGui::SliderInt("Tune Interval (ticks)", &config.tuneIntervalTicks, 0, 20);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Extra world ticks between confirmed tuning clicks (0 = fastest)");
    }

    edited |= ImGui::Checkbox("Polyphonic", &config.polyphonic);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Play multiple notes at the same time");
    }

    edited |= ImGui::Checkbox("Auto Rotate", &config.autoRotate);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Automatically rotate to face noteblocks");
    }

    edited |= ImGui::Checkbox("Auto Play", &config.autoPlay);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Automatically loop the song");
    }

    edited |= ImGui::Checkbox("Transpose Out of Range", &config.transposeOutOfRange);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Transpose notes that are out of range");
    }

    edited |= ImGui::Checkbox("Swing Arm", &config.swingArm);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Swing arm when clicking noteblocks");
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Mode Settings");

    const char* notebotModes[] = {"Exact Instruments", "Any Instrument"};
    int currentMode = static_cast<int>(config.mode);
    if (ImGui::Combo("Notebot Mode", &currentMode, notebotModes, IM_ARRAYSIZE(notebotModes))) {
        config.mode = static_cast<NoteBotConfig::NotebotMode>(currentMode);
        edited = true;
    }

    const char* playingModes[] = {"Sequential", "Polyphonic"};
    int currentPlayMode = static_cast<int>(config.playingMode);
    if (ImGui::Combo("Playing Mode", &currentPlayMode, playingModes, IM_ARRAYSIZE(playingModes))) {
        config.playingMode = static_cast<NoteBotConfig::PlayingMode>(currentPlayMode);
        edited = true;
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Advanced");

    edited |= ImGui::SliderInt("Final Verify Delay (ticks)", &config.checkNoteblocksDelay, 1, 100);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Wait after tuning before checking all note blocks (default: 3)");
    }
    if (edited) markConfigEdited();
}

void NoteBotUI::renderRenderTab() {
    if (!mPlayer) return;

    auto& config = mPlayer->getConfig();
    bool edited = false;
    constexpr auto colorFlags = ImGuiColorEditFlags_Float |
                                ImGuiColorEditFlags_DisplayRGB |
                                ImGuiColorEditFlags_InputRGB;
    const auto editColor = [&edited](const char* label, std::array<float, 4>& color) {
        edited |= ImGui::ColorEdit4(label, color.data(), colorFlags);
    };

    ImGui::SeparatorText("Render Options");

    edited |= ImGui::Checkbox("Render Text", &config.renderText);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Show note names above noteblocks");
    }

    edited |= ImGui::Checkbox("Render Boxes", &config.renderBoxes);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Show boxes around noteblocks");
    }
    edited |= ImGui::Checkbox("Box Lines", &config.renderBoxLines);
    edited |= ImGui::Checkbox("Box Faces", &config.renderBoxFaces);
    edited |= ImGui::Checkbox("Show Unmapped Boxes", &config.showUnmappedBoxes);
    edited |= ImGui::SliderFloat("Box Expansion", &config.boxExpansion, 0.001f, 0.08f, "%.3f");

    edited |= ImGui::SliderFloat("Note Text Scale", &config.noteTextScale, 0.5f, 3.0f);
    edited |= ImGui::SliderFloat("Note Text Height", &config.noteTextHeight, 0.5f, 2.5f, "%.2f");
    editColor("Pitch Text Color", config.pitchTextColor);
    editColor("Remaining Text Color", config.remainingTextColor);

    ImGui::Spacing();
    ImGui::SeparatorText("Colors");

    ImGui::Text("Scanned Noteblock");
    editColor("Side##scanned", config.colors.scannedSide);
    editColor("Line##scanned", config.colors.scannedLine);

    ImGui::Spacing();
    ImGui::Text("Untuned Noteblock");
    editColor("Side##untuned", config.colors.untunedSide);
    editColor("Line##untuned", config.colors.untunedLine);

    ImGui::Spacing();
    ImGui::Text("Tuned Noteblock");
    editColor("Side##tuned", config.colors.tunedSide);
    editColor("Line##tuned", config.colors.tunedLine);

    ImGui::Spacing();
    ImGui::Text("Tune Hit Noteblock");
    editColor("Side##hit", config.colors.tuneHitSide);
    editColor("Line##hit", config.colors.tuneHitLine);

    ImGui::Spacing();
    ImGui::Text("Playing Noteblock");
    editColor("Side##playing", config.colors.playingSide);
    editColor("Line##playing", config.colors.playingLine);
    if (edited) markConfigEdited();
}

void NoteBotUI::scanSongFolder() {
    mSongFiles.clear();
    mSelectedSongIndex = -1;

    fs::path folder = fromUtf8(mSongFolderPath);
    if (folder.empty()) return;

    std::error_code ec;
    if (folder.is_relative()) folder = fs::absolute(folder, ec);
    if (ec || !fs::is_directory(folder, ec)) return;

    for (fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
         it != end && !ec; it.increment(ec)) {
        const auto& entry = *it;
        if (!entry.is_regular_file(ec)) continue;

        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (ext == ".nbs") mSongFiles.push_back(entry.path());
    }

    std::sort(mSongFiles.begin(), mSongFiles.end(), [](const fs::path& lhs, const fs::path& rhs) {
        return lhs.filename().u8string() < rhs.filename().u8string();
    });
}

void NoteBotUI::loadSelectedSong() {
    if (mSelectedSongIndex >= 0 && mSelectedSongIndex < static_cast<int>(mSongFiles.size()) && mPlayer) {
        mPlayer->loadSong(mSongFiles[mSelectedSongIndex]);
    }
}

} // namespace notebot
