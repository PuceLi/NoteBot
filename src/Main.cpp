#include "Main.h"
#include "Config.h"
#include "NoteBotPlayer.h"
#include "NoteBotUI.h"
#include "NoteBlockRenderer.h"
#include "ImGuiManager.h"

#include "ll/api/memory/Hook.h"
#include "ll/api/mod/RegisterHelper.h"

#include <imgui.h>
#include <memory>
#include <filesystem>

namespace notebot {

std::unique_ptr<NoteBotPlayer> gPlayer;
std::unique_ptr<NoteBotUI>     gUI;
std::unique_ptr<NoteBlockRenderer> gRenderer;

NoteBot& NoteBot::getInstance() {
    static NoteBot instance;
    return instance;
}

bool NoteBot::load() {
    auto& logger = getSelf().getLogger();
    logger.info("NoteBot Loading...");
	logger.info("Author: PuceLi");

    try {
        loadConfig();
        logger.info("Config loaded from: {}", getConfigPath().string());
    } catch (const std::exception& e) {
        logger.error("Failed to load config: {}", e.what());
        return false;
    }

    namespace fs = std::filesystem;
    auto dataDir = getDataDir();
    auto& cfg = getConfig();
    auto songDir = dataDir / cfg.general.songFolder;

    if (!fs::exists(dataDir)) {
        fs::create_directories(dataDir);
        logger.info("Created data directory: {}", dataDir.string());
    }

    if (!fs::exists(songDir)) {
        fs::create_directories(songDir);
        logger.info("Created song directory: {}", songDir.string());
    }

    gPlayer = std::make_unique<NoteBotPlayer>();
    gUI     = std::make_unique<NoteBotUI>();
    gRenderer = std::make_unique<NoteBlockRenderer>();
    gUI->setPlayer(gPlayer.get());

    logger.info("NoteBot loaded successfully!");
    logger.info("Press 'L' to open the UI");

    return true;
}

bool NoteBot::enable() {
    auto& logger = getSelf().getLogger();
    logger.info("NoteBot Enabled");

    if (!ImGuiManager::getInstance().initialize()) {
        logger.error("Failed to initialize ImGui");
        return false;
    }

    installInputHooks();

    try {
        saveConfig();
    } catch (const std::exception& e) {
        logger.warn("Failed to save config: {}", e.what());
    }

    return true;
}

bool NoteBot::disable() {
    auto& logger = getSelf().getLogger();
    logger.info("NoteBot Disabling...");

    uninstallInputHooks();

    try {
        saveConfig();
    } catch (const std::exception& e) {
        logger.warn("Failed to save config: {}", e.what());
    }

    gRenderer.reset();
    gUI.reset();
    gPlayer.reset();

    ImGuiManager::getInstance().shutdown();

    logger.info("NoteBot Disabled");
    return true;
}

} // namespace notebot

LL_REGISTER_MOD(notebot::NoteBot, notebot::NoteBot::getInstance());
