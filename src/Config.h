#pragma once

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
};

struct NoteBotConfig {
    int version = 5;

    enum class NotebotMode {
        Manual = 0,
        Auto = 1
    };

    enum class PlayingMode {
        Sequential = 0,
        Polyphonic = 1
    };

    GeneralConfig general;
    NotebotMode mode = NotebotMode::Manual;
    PlayingMode playingMode = PlayingMode::Polyphonic;
    int checkNoteblocksDelay = 3;

    bool renderBoxes = true;
    bool renderBoxLines = true;
    bool renderBoxFaces = false;
    bool showUnmappedBoxes = true;
    bool renderText = true;
    bool showPlaybackHud = true;
    float noteTextScale = 1.0f;
    float noteTextHeight = 1.3f;
    float boxExpansion = 0.01f;

    // 播放配置
    int tickDelay = 0;
    int tuneIntervalTicks = 0;
    bool polyphonic = true;
    bool autoRotate = true;
    bool autoPlay = false;
    bool transposeOutOfRange = true;
    bool swingArm = true;

    ColorConfig colors;
    std::array<float, 4> pitchTextColor = {0.0f, 0.8f, 0.0f, 1.0f};
    std::array<float, 4> remainingTextColor = {0.8f, 0.0f, 0.0f, 1.0f};
    KeybindsConfig keybinds;
};

NoteBotConfig& getConfig();
std::filesystem::path getConfigPath();
std::filesystem::path getDataDir();
bool sanitizeConfig(NoteBotConfig& config);
void loadConfig();
void saveConfig();

} // namespace notebot
