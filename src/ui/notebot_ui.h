#pragma once

#include "../NoteData.h"
#include "../UiAppearance.h"
#include <filesystem>
#include <functional>
#include <string>
#include <array>
#include <chrono>
#include <vector>

namespace meteor {

struct NotebotSettings {
    float playbackSpeed = 1.0f;
    int tuneIntervalTicks = 0;
    int concurrentTuneBlocks = 1;
    int mode = 1;                       // 0 AnyInstrument, 1 ExactInstruments
    int playingMode = 1;                // 0 Sequential, 1 Polyphonic
    bool polyphonic = true;
    bool autoRotate = true;
    bool autoPlay = false;
    bool transposeOutOfRange = true;
    bool ignoreDistanceLimit = false;
    bool swingArm = true;
    int checkNoteblocksAgainDelay = 1;
    bool renderText = true;
    bool renderBoxes = true;
    bool renderBoxThroughWalls = false;
    int shapeMode = 1;                  // 0 Lines, 1 Sides, 2 Both
    notebot::NoteMap noteMap = notebot::makeDefaultNoteMap();
    std::array<bool, 12> rainbowColors{};
    std::array<std::array<float, 4>, 12> renderColors{{
        {{0.8f,0.0f,0.0f,0.12f}}, {{0.8f,0.0f,0.0f,1.0f}},
        {{0.0f,1.0f,0.0f,0.2f}}, {{0.0f,1.0f,0.0f,0.8f}},
        {{1.0f,0.8f,0.0f,0.3f}}, {{1.0f,0.8f,0.0f,1.0f}},
        {{1.0f,1.0f,0.0f,0.12f}}, {{1.0f,1.0f,0.0f,1.0f}},
        {{0.0f,0.5f,1.0f,0.4f}}, {{0.0f,0.7f,1.0f,1.0f}},
        {{0.0f,0.8f,0.0f,1.0f}}, {{0.8f,0.0f,0.0f,1.0f}}
    }};
    float noteTextScale = 1.0f;
    float noteTextHeight = 1.3f;
    float boxExpansion = 0.0f;
    float hitFadeSeconds = 0.5f;
    bool showScannedNoteblocks = false;
};

struct NotebotActions {
    std::function<void(const std::filesystem::path&)> loadSong;
    std::function<void(const std::filesystem::path&)> previewSong;
    std::function<void()> alignCenter;
    std::function<void()> resetNowPlayingPosition;
    std::function<void(bool)> nowPlayingVisibilityChanged;
    std::function<void()> pauseResume;
    std::function<void()> stop;
    std::function<void()> scanNoteBlocks;
    std::function<void()> clearNoteBlocks;
    std::function<void(const std::string&)> bindChanged;
    std::function<void(const std::string&)> playbackBindChanged;
    std::function<void(const NotebotSettings&)> settingsChanged;
    std::function<void(const NotebotSettings&)> animatedColorsChanged;
    std::function<void()> backupConfig;
    std::function<void()> importConfig;
    std::function<void()> backupSongs;
    std::function<void()> importSongs;
    std::function<void()> openBackupFolder;
    std::function<void(const notebot::UiAppearance&)> appearanceChanged;
    std::function<void()> resetLayout;
};

class NotebotUi {
public:
    explicit NotebotUi(std::filesystem::path songsDirectory);
    NotebotSettings settings;
    notebot::UiAppearance appearance;
    NotebotActions actions;
    bool songLoaded = false;
    std::string songTitle;
    std::string statusOverride;
    size_t foundNoteBlocks = 0;
    size_t requiredNotes = 0;
    std::vector<std::string> missingNotes;
    int currentTick = 0;
    int lastTick = 0;
    bool playing = false;
    bool showNowPlaying = true;
    std::string keybind = "None";
    std::string playbackKeybind = "P";
    bool showSongs = false;
    bool showModule = true;
    std::uintptr_t resetIcon = 0;
    std::uintptr_t copyIcon = 0;
    std::uintptr_t pasteIcon = 0;
    bool dataTransferBusy = false;
    bool dataTransferFailed = false;
    std::string dataTransferStatus;
    bool appearanceStatusFailed = false;
    std::string appearanceStatus;

    void draw();
    void updateRainbowColors();
    std::string getStatus() const;
    void refreshSongs();
    void setSongsDirectory(std::filesystem::path directory);
    void reloadLayout();
    void resetLayout();
    bool isBindingKey() const { return binding_ != BindTarget::None; }

private:
    int selectedTopTab_ = 0;
    struct SettingsWindowState {
        bool expanded = true;
        bool loaded = false;
        float expandedHeight = 620.f;
    };
    SettingsWindowState settingsWindow_;
    SettingsWindowState renderWindow_;
    int selectedSettingsPage_ = 0;
    bool resetLayoutRequested_ = false;
    float layoutScale_ = 1.f;
    float scaleInput_ = 1.f;
    bool scaleInputEditing_ = false;
    std::filesystem::path songsDirectory_;
    std::vector<std::filesystem::path> songFiles_;
    bool songFolderAvailable_ = false;
    std::string filter_;
    char searchBuffer_[256]{};
    bool songsExpanded_ = true;
    bool songsLayoutLoaded_ = false;
    float songsExpandedWidth_ = 416.0f;
    float songsExpandedHeight_ = 650.0f;
    bool moduleExpanded_ = true;
    bool moduleLayoutLoaded_ = false;
    float moduleExpandedWidth_ = 650.0f;
    float moduleExpandedHeight_ = 660.0f;
    bool generalExpanded_ = true;
    bool noteMapExpanded_ = false;
    enum class BindTarget { None, Interface, Playback };
    BindTarget binding_ = BindTarget::None;
    int editingColor_ = -1;
    bool colorExpanded_ = true;
    bool colorLayoutLoaded_ = false;
    float colorExpandedWidth_ = 416.0f;
    float colorExpandedHeight_ = 650.0f;
    std::chrono::steady_clock::time_point lastRainbowUpdate_{};
    bool focusSearch_ = true;
    void drawModSettings();
    void drawRender();
    void drawSongs();
    void drawModule();
    void drawColorEditor();
};

} // namespace meteor
