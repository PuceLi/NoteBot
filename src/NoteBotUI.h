#pragma once

#include "NoteBotPlayer.h"
#include <chrono>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>

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
    bool            mConfigDirty = false;
    bool            mResetNowPlayingRequested = false;
    std::chrono::steady_clock::time_point mLastConfigEdit{};

    void renderNowPlaying();
    void markConfigEdited();
    void syncMeteorSettingsFromConfig();
    void applyMeteorSettings();
};

} // namespace notebot
