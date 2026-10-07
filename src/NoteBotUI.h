#pragma once

#include "NoteBotPlayer.h"
#include <chrono>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace meteor { class NotebotUi; }

namespace notebot {

class NoteBotUI {
public:
    NoteBotUI();
    ~NoteBotUI();

    void render();
    void toggle();
    void setVisible(bool visible);
    bool isVisible() const { return mVisible.load(std::memory_order_acquire); }

    void setPlayer(NoteBotPlayer* player);
    void setIconTextures(std::uintptr_t reset, std::uintptr_t copy, std::uintptr_t paste);

private:
    std::atomic<bool> mVisible{false};
    std::atomic<bool> mOpenModuleRequested{false};
    NoteBotPlayer*  mPlayer  = nullptr;
    std::unique_ptr<meteor::NotebotUi> mMeteorUI;
    std::vector<std::filesystem::path> mSongFiles;
    int             mSelectedSongIndex = -1;
    char            mSongFolderPath[256];
    bool            mConfigDirty = false;
    bool            mResetPlaybackHudRequested = false;
    std::chrono::steady_clock::time_point mLastConfigEdit{};

    void renderMainWindow();
    void renderPlaybackHud(const NoteBotPlayer::DisplayState& display);
    void renderPlayerTab();
    void renderSettingsTab();
    void renderRenderTab();
    void scanSongFolder();
    void loadSelectedSong();
    void markConfigEdited();
    void syncMeteorSettingsFromConfig();
    void applyMeteorSettings();
};

} // namespace notebot
