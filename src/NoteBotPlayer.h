#pragma once

#include "NoteData.h"
#include "Config.h"
#include "NoteBlockManager.h"
#include <memory>
#include <filesystem>
#include <atomic>
#include <chrono>
#include <optional>
#include <string>
#include <mutex>

namespace notebot {

class NoteBotPlayer {
public:
    struct DisplayState {
        bool hasSong = false;
        bool isPlaying = false;
        int currentTick = 0;
        int totalTicks = 0;
        std::string songTitle;
        std::string songAuthor;
        std::string status = "No song loaded";
    };

    NoteBotPlayer();
    ~NoteBotPlayer();

    bool loadSong(const std::filesystem::path& filePath);
    void play();
    void pause();
    void stop();
    void requestClearNoteBlocks() noexcept {
        mPendingScanRadius.store(-1, std::memory_order_release);
        mPendingControl.store(ControlRequest::None, std::memory_order_release);
        mClearNoteBlocksRequested.store(true, std::memory_order_release);
    }
    void tick();

    DisplayState getDisplayState() const;
    bool isPlaying() const { return getDisplayState().isPlaying; }
    bool hasSong() const { return getDisplayState().hasSong; }
    std::string getStatus() const;
    int getCurrentTick() const { return getDisplayState().currentTick; }
    int getTotalTicks() const { return getDisplayState().totalTicks; }
    std::string getSongTitle() const { return getDisplayState().songTitle; }
    std::string getSongAuthor() const { return getDisplayState().songAuthor; }

    NoteBotConfig& getConfig() { return mConfig; }
    const NoteBotConfig& getConfig() const { return mConfig; }

    NoteBlockManager& getNoteBlockManager() { return mNoteBlockManager; }
    const NoteBlockManager& getNoteBlockManager() const { return mNoteBlockManager; }

    void scanAndMapNoteBlocks(class BlockSource& blockSource, const BlockPos& playerPos,
                              const Vec3& eyePos, int radius);
    void requestScanNearbyNoteBlocks(int radius = 32) noexcept { mPendingScanRadius.store(radius, std::memory_order_release); }

private:
    struct TuneTarget {
        BlockPos pos;
        int targetPitch;
        int remainingHits;
    };
    enum class Stage { Ready, Tuning, Verifying, Playing, Error };
    enum class ControlRequest { None, Play, Pause, Stop };

    std::optional<Song> mSong;
    std::mutex mPendingSongMutex;
    std::optional<Song> mPendingSong;
    std::atomic<std::shared_ptr<const DisplayState>> mPublishedDisplay;
    bool                mIsPlaying   = false;
    int                 mCurrentTick = 0;
    int                 mTickCounter = 0;
    std::chrono::steady_clock::time_point mNextReadyScanTime{};
    bool mAutoRefreshNearby = false;
    NoteBotConfig&      mConfig;
    NoteBlockManager    mNoteBlockManager;
    std::atomic<int>    mPendingScanRadius{-1};
    std::atomic<ControlRequest> mPendingControl{ControlRequest::None};
    std::atomic<bool> mClearNoteBlocksRequested{false};
    std::atomic<bool> mWorldPaused{false};
    uint64_t mLastSimulationTick = 0;
    bool mHasSimulationTick = false;
    std::chrono::steady_clock::time_point mLastSimulationAdvance;
    std::vector<TuneTarget> mTuneTargets;
    Stage mStage = Stage::Ready;
    size_t mTuneIndex = 0;
    int mVerifyDelay = 0;
    int mTunePasses = 0;
    bool mTunePlanReady = false;
    bool mAwaitingTuneUpdate = false;
    int mTunePitchBefore = -1;
    uint64_t mTuneEventSerialBefore = 0;
    int mTuneVerifyTicks = 0;
    int mTuneDelay = 0;
    int mTuneRetryCount = 0;
    std::string mError;

    void playNotesAtTick(int tick);
    void tickInternal();
    void publishDisplayState();
    std::string composeStatus() const;
    void stopInternal();
    void beginTuning();
    void prepareTuneTargets();
    void tickTuning();
    bool refreshTuneTarget(TuneTarget& target, class BlockSource& blockSource);
    bool updateTuneTargets();
    void reset();
};

} // namespace notebot
