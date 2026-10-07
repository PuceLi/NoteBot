#include "Config.h"
#include "Main.h"

#include "ll/api/Config.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace notebot {

NoteBotConfig& getConfig() {
    static NoteBotConfig cfg;
    return cfg;
}

std::filesystem::path getConfigPath() {
    return NoteBot::getInstance().getSelf().getConfigDir() / "config.json";
}

std::filesystem::path getDataDir() {
    return NoteBot::getInstance().getSelf().getDataDir();
}

static std::filesystem::path getPersistentConfigPath() {
    return getDataDir() / "config-backup.json";
}

bool sanitizeConfig(NoteBotConfig& cfg) {
    const NoteBotConfig defaults;
    bool changed = false;
    const auto clampInt = [&changed](int& value, int minimum, int maximum) {
        const int valid = std::clamp(value, minimum, maximum);
        if (value != valid) {
            value = valid;
            changed = true;
        }
    };
    const auto clampFloat = [&changed](float& value, float minimum, float maximum, float fallback) {
        const float valid = std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
        if (value != valid) {
            value = valid;
            changed = true;
        }
    };
    const auto clampColor = [&clampFloat](std::array<float, 4>& color,
                                          const std::array<float, 4>& fallback) {
        for (size_t index = 0; index < color.size(); ++index) {
            clampFloat(color[index], 0.0f, 1.0f, fallback[index]);
        }
    };

    if (cfg.version != defaults.version) {
        cfg.version = defaults.version;
        changed = true;
    }
    if (cfg.general.songFolder.empty()) {
        cfg.general.songFolder = defaults.general.songFolder;
        changed = true;
    }
    clampInt(cfg.general.scanRadius, 1, 32);
    if (cfg.mode != NoteBotConfig::NotebotMode::Manual &&
        cfg.mode != NoteBotConfig::NotebotMode::Auto) {
        cfg.mode = defaults.mode;
        changed = true;
    }
    if (cfg.playingMode != NoteBotConfig::PlayingMode::Sequential &&
        cfg.playingMode != NoteBotConfig::PlayingMode::Polyphonic) {
        cfg.playingMode = defaults.playingMode;
        changed = true;
    }
    clampInt(cfg.checkNoteblocksDelay, 1, 100);
    clampInt(cfg.tickDelay, 0, 20);
    clampInt(cfg.tuneIntervalTicks, 0, 20);
    clampInt(cfg.keybinds.hotkey, 0, 255);
    clampFloat(cfg.noteTextScale, 0.5f, 3.0f, defaults.noteTextScale);
    clampFloat(cfg.noteTextHeight, 0.5f, 2.5f, defaults.noteTextHeight);
    clampFloat(cfg.boxExpansion, 0.001f, 0.08f, defaults.boxExpansion);

    clampColor(cfg.colors.scannedSide, defaults.colors.scannedSide);
    clampColor(cfg.colors.scannedLine, defaults.colors.scannedLine);
    clampColor(cfg.colors.tuneHitSide, defaults.colors.tuneHitSide);
    clampColor(cfg.colors.tuneHitLine, defaults.colors.tuneHitLine);
    clampColor(cfg.colors.tunedSide, defaults.colors.tunedSide);
    clampColor(cfg.colors.tunedLine, defaults.colors.tunedLine);
    clampColor(cfg.colors.untunedSide, defaults.colors.untunedSide);
    clampColor(cfg.colors.untunedLine, defaults.colors.untunedLine);
    clampColor(cfg.colors.playingSide, defaults.colors.playingSide);
    clampColor(cfg.colors.playingLine, defaults.colors.playingLine);
    clampColor(cfg.pitchTextColor, defaults.pitchTextColor);
    clampColor(cfg.remainingTextColor, defaults.remainingTextColor);
    return changed;
}

void loadConfig() {
    namespace fs = std::filesystem;

    const auto path = getConfigPath();
    const auto mirror = getPersistentConfigPath();
    const bool hasPrimary = fs::exists(path);
    const bool hasMirror = fs::exists(mirror);
    if (!hasPrimary && !hasMirror) {
        saveConfig();
        return;
    }

    std::vector<fs::path> candidates;
    if (hasPrimary) candidates.push_back(path);
    if (hasMirror) candidates.push_back(mirror);
    if (hasPrimary && hasMirror && fs::last_write_time(mirror) > fs::last_write_time(path)) {
        std::swap(candidates[0], candidates[1]);
    }

    for (const auto& candidate : candidates) {
        NoteBotConfig loaded;
        int sourceVersion = loaded.version;
        bool updaterCalled = false;
        bool currentVersion = false;
        try {
            currentVersion = ll::config::loadConfig(loaded, candidate,
                [&](NoteBotConfig& defaults, auto& data) {
                    updaterCalled = true;
                    const auto version = data.find("version");
                    sourceVersion = version != data.end() && version->is_number_integer()
                        ? version->template get<int>() : 0;
                    data.erase("concurrentTuneBlocks");
                    return ll::config::defaultConfigUpdater(defaults, data);
                });
            if (!currentVersion && !updaterCalled) {
                throw std::runtime_error("Config is empty");
            }
            if (sourceVersion < 4 && loaded.checkNoteblocksDelay == 100) {
                loaded.checkNoteblocksDelay = 3;
            }
        } catch (const std::exception& error) {
            auto backup = candidate;
            backup += ".invalid.bak";
            std::error_code ec;
            fs::copy_file(candidate, backup, fs::copy_options::overwrite_existing, ec);
            NoteBot::getInstance().getSelf().getLogger().warn(
                "Invalid config ({}); backup: {}", error.what(), backup.string());
            continue;
        }

        const bool repaired = sanitizeConfig(loaded);
        getConfig() = std::move(loaded);
        if (candidate == mirror || !currentVersion || repaired) {
            NoteBot::getInstance().getSelf().getLogger().warn(
                "Config restored, migrated, or out-of-range values were repaired");
            saveConfig();
        }
        return;
    }

    NoteBot::getInstance().getSelf().getLogger().warn("No valid config found; using defaults");
    getConfig() = NoteBotConfig{};
    saveConfig();
}

void saveConfig() {
    namespace fs = std::filesystem;
    auto path = getConfigPath();
    auto mirror = getPersistentConfigPath();

    if (!fs::exists(path.parent_path())) {
        fs::create_directories(path.parent_path());
    }
    if (!fs::exists(mirror.parent_path())) {
        fs::create_directories(mirror.parent_path());
    }

    sanitizeConfig(getConfig());
    if (!ll::config::saveConfig(getConfig(), mirror)) {
        throw std::runtime_error("Failed to save persistent config: " + mirror.string());
    }
    if (!ll::config::saveConfig(getConfig(), path)) {
        throw std::runtime_error("Failed to save config: " + path.string());
    }
}

} // namespace notebot
