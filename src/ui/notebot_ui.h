#pragma once

#include "../NoteData.h"
#include <filesystem>
#include <functional>
#include <string>
#include <array>
#include <chrono>
#include <cstdint>
#include <vector>

namespace meteor {

struct NotebotSettings {
    int tickDelay = 0;
    int tuneIntervalTicks = 0;
    int mode = 1;                       // 0 AnyInstrument, 1 ExactInstruments
    int playingMode = 1;                // 0 Sequential, 1 Polyphonic
    bool polyphonic = true;
    bool autoRotate = true;
    bool autoPlay = false;
    bool transposeOutOfRange = true;
    bool swingArm = true;
    int checkNoteblocksAgainDelay = 1;
    bool renderText = true;
    bool renderBoxes = true;
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
    std::function<void(const NotebotSettings&)> settingsChanged;
    std::function<void(const NotebotSettings&)> animatedColorsChanged;
};

class NotebotUi {
public:
    explicit NotebotUi(std::filesystem::path songsDirectory);
    NotebotSettings settings;
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
    bool showSongs = false;
    bool showModule = true;
    std::uintptr_t resetIcon = 0;
    std::uintptr_t copyIcon = 0;
    std::uintptr_t pasteIcon = 0;

    void draw();
    void updateRainbowColors();
    std::string getStatus() const;
    void refreshSongs();

private:
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
    bool renderExpanded_ = true;
    bool bindExpanded_ = true;
    bool binding_ = false;
    int editingColor_ = -1;
    bool colorExpanded_ = true;
    bool colorLayoutLoaded_ = false;
    float colorExpandedWidth_ = 416.0f;
    float colorExpandedHeight_ = 650.0f;
    std::chrono::steady_clock::time_point lastRainbowUpdate_{};
    bool focusSearch_ = true;
    void drawSongs();
    void drawModule();
    void drawColorEditor();
};

} // namespace meteor
