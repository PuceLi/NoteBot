#pragma once

#include "NoteData.h"
#include "UiAppearance.h"
#include <array>
#include <filesystem>
#include <string>

namespace notebot {

struct GeneralConfig {
    std::string songFolder = "songs";
    bool autoScan = false;
    int scanRadius = 32;
};

struct ColorConfig {
    std::array<float, 4> scannedSide = {1.0f, 1.0f, 0.0f, 0.12f};
    std::array<float, 4> scannedLine = {1.0f, 1.0f, 0.0f, 1.0f};
    std::array<float, 4> tuneHitSide = {1.0f, 0.8f, 0.0f, 0.3f};
    std::array<float, 4> tuneHitLine = {1.0f, 0.8f, 0.0f, 1.0f};
    std::array<float, 4> tunedSide = {0.0f, 1.0f, 0.0f, 0.2f};
    std::array<float, 4> tunedLine = {0.0f, 1.0f, 0.0f, 0.8f};
    std::array<float, 4> untunedSide = {0.8f, 0.0f, 0.0f, 0.12f};
    std::array<float, 4> untunedLine = {0.8f, 0.0f, 0.0f, 1.0f};
    std::array<float, 4> playingSide = {0.0f, 0.5f, 1.0f, 0.4f};
    std::array<float, 4> playingLine = {0.0f, 0.7f, 1.0f, 1.0f};
};

struct KeybindsConfig {
    int hotkey = 0x4C; // L 键
    int playPause = 0x50; // P 键
};

struct NoteBotConfig {
    int version = 18;

    enum class NotebotMode {
        Manual = 0,
        Auto = 1
    };

    enum class PlayingMode {
        Sequential = 0,
        Polyphonic = 1
    };

    GeneralConfig general;
    UiAppearance ui;
    NotebotMode mode = NotebotMode::Manual;
    PlayingMode playingMode = PlayingMode::Polyphonic;
    NoteMap noteMap = makeDefaultNoteMap();
    int checkNoteblocksDelay = 1;

    bool renderBoxes = true;
    bool renderBoxThroughWalls = false;
    bool renderBoxLines = false;
    bool renderBoxFaces = true;
    bool showUnmappedBoxes = false;
    bool renderText = true;
    bool showNowPlaying = true;
    float noteTextScale = 1.0f;
    float noteTextHeight = 1.3f;
    float boxExpansion = 0.0f;
    float hitFadeSeconds = 0.5f;

    // 播放配置
    float playbackSpeed = 1.0f;
    int tuneIntervalTicks = 0;
    int concurrentTuneBlocks = 1;
    bool polyphonic = true;
    bool autoRotate = true;
    bool autoPlay = false;
    bool transposeOutOfRange = true;
    bool ignoreDistanceLimit = false;
    bool swingArm = true;
    bool debugBlockEvents = false;

    ColorConfig colors;
    std::array<bool, 12> renderRainbowColors{};
    std::array<float, 4> pitchTextColor = {0.0f, 0.8f, 0.0f, 1.0f};
    std::array<float, 4> remainingTextColor = {0.8f, 0.0f, 0.0f, 1.0f};
    KeybindsConfig keybinds;
};

NoteBotConfig& getConfig();
std::filesystem::path getConfigPath();
std::filesystem::path getDataDir();
std::filesystem::path getSongDir();
bool sanitizeConfig(NoteBotConfig& config);
void loadConfig();
void saveConfig();
NoteBotConfig readConfigForImport(const std::filesystem::path& path);

} // namespace notebot
