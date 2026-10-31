#include "NoteBotUI.h"
#include "NoteBotPlayer.h"
#include "Config.h"
#include "Main.h"
#include "ui/notebot_ui.h"
#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/shared_types/v1_26_20/block/NoteBlockInstrumentEnum.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <windows.h>

namespace fs = std::filesystem;

namespace notebot {

namespace {
std::string formatSongTime(int tick) {
    const int64_t milliseconds = static_cast<int64_t>(std::max(0, tick)) * 50;
    char text[32];
    std::snprintf(text, sizeof(text), "%02lld:%02lld:%03lld",
                  static_cast<long long>(milliseconds / 60000),
                  static_cast<long long>(milliseconds / 1000 % 60),
                  static_cast<long long>(milliseconds % 1000));
    return text;
}

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

fs::path fromUtf8(std::string_view text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

std::string instrumentName(int value) {
    if (value < 0 || value >= static_cast<int>(Instrument::Size))
        return "Unrecognized instrument ID " + std::to_string(value);
    return SharedTypes::v1_26_20::NoteBlockInstrumentEnum::noteBlockInstrumentToString(
        static_cast<Instrument>(value));
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
    auto& actions = mMeteorUI->actions;
    actions.loadSong = [this](const fs::path& path) { mPlayer->loadSong(path); };
    actions.previewSong = [this](const fs::path& path) { mPlayer->previewSong(path); };
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
    actions.resetNowPlayingPosition = [this] { mResetNowPlayingRequested = true; };
    actions.nowPlayingVisibilityChanged = [this](bool visible) {
        mPlayer->getConfig().showNowPlaying = visible;
        markConfigEdited();
    };
    actions.settingsChanged = [this](const meteor::NotebotSettings&) { applyMeteorSettings(); };
    actions.animatedColorsChanged = [this](const meteor::NotebotSettings& settings) {
        if (!mPlayer) return;
        auto& config = mPlayer->getConfig();
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
    };
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
    mMeteorUI->showNowPlaying = config.showNowPlaying;
    settings.tickDelay = config.tickDelay;
    settings.tuneIntervalTicks = config.tuneIntervalTicks;
    settings.mode = config.mode == NoteBotConfig::NotebotMode::Manual ? 1 : 0;
    settings.playingMode = static_cast<int>(config.playingMode);
    settings.noteMap = config.noteMap;
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
    config.noteMap = settings.noteMap;
    config.polyphonic = settings.polyphonic;
    config.autoRotate = settings.autoRotate;
    config.autoPlay = settings.autoPlay;
    config.transposeOutOfRange = settings.transposeOutOfRange;
    config.swingArm = settings.swingArm;
    config.checkNoteblocksDelay = settings.checkNoteblocksAgainDelay;
    config.renderText = settings.renderText;
    config.renderBoxes = settings.renderBoxes;
    config.renderBoxLines = settings.shapeMode == 0 || settings.shapeMode == 2;
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
    if (mMeteorUI) mMeteorUI->updateRainbowColors();
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
            mMeteorUI->statusOverride = display.status;
            mMeteorUI->foundNoteBlocks = manager.getNoteBlockCount();
            mMeteorUI->requiredNotes = diagnostics.required;
            mMeteorUI->missingNotes.clear();
            mMeteorUI->missingNotes.reserve(diagnostics.missing.size());
            for (const auto& note : diagnostics.missing) {
                if (note.customInstrument) {
                    mMeteorUI->missingNotes.push_back("Custom instrument ID " +
                        std::to_string(note.instrument) + ", pitch " + std::to_string(note.pitch));
                } else if (mPlayer->getConfig().mode == NoteBotConfig::NotebotMode::Auto)
                    mMeteorUI->missingNotes.push_back("Pitch " + std::to_string(note.pitch));
                else
                    mMeteorUI->missingNotes.push_back(instrumentName(note.instrument) +
                        ", pitch " + std::to_string(note.pitch) + ", NBS ID " +
                        std::to_string(note.sourceInstrumentId));
            }
        }
        mMeteorUI->draw();
        mVisible.store(mMeteorUI->showModule || mMeteorUI->showSongs,
                       std::memory_order_release);
    }
    if (mPlayer) renderNowPlaying();
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

void NoteBotUI::renderNowPlaying() {
    if (!mPlayer->getConfig().showNowPlaying || (!mPlayer->isPlaying() && !isVisible())) return;
    const auto display = mPlayer->getDisplayState();
    const auto* viewport = ImGui::GetMainViewport();
    const auto defaultPosition = ImVec2(viewport->GetCenter().x, viewport->Pos.y + 72.0f);
    if (mResetNowPlayingRequested) {
        ImGui::SetNextWindowPos(defaultPosition, ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        mResetNowPlayingRequested = false;
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
        const char* title = display.songTitle.empty() ? "No song" : display.songTitle.c_str();
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
        const std::string progressText = display.hasSong
            ? formatSongTime(display.currentTick) + " / " + formatSongTime(display.totalTicks)
            : "No song";
        ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f), progressText.c_str());
    }
    ImGui::End();
    ImGui::PopStyleColor();
    if (hideRequested) {
        mPlayer->getConfig().showNowPlaying = false;
        if (mMeteorUI) mMeteorUI->showNowPlaying = false;
        markConfigEdited();
    }
}

void NoteBotUI::markConfigEdited() {
    mConfigDirty = true;
    mLastConfigEdit = std::chrono::steady_clock::now();
}

} // namespace notebot
