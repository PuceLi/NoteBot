#include "NoteBotPlayer.h"
#include "NBSDecoder.h"
#include "NoteBlockInteraction.h"
#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace notebot {

NoteBotPlayer::NoteBotPlayer() : mConfig(notebot::getConfig()) { publishDisplayState(); }
NoteBotPlayer::~NoteBotPlayer() = default;

bool NoteBotPlayer::loadSong(const std::filesystem::path& filePath) {
    auto song = NBSDecoder::decode(filePath);
    if (!song) return false;
    std::lock_guard lock(mPendingSongMutex);
    mPendingSong = std::move(song);
    return true;
}

void NoteBotPlayer::play() {
    mPendingControl.store(ControlRequest::Play, std::memory_order_release);
}

void NoteBotPlayer::pause() {
    mPendingControl.store(ControlRequest::Pause, std::memory_order_release);
}

void NoteBotPlayer::stop() {
    mPendingControl.store(ControlRequest::Stop, std::memory_order_release);
}

void NoteBotPlayer::stopInternal() {
    mIsPlaying   = false;
    mCurrentTick = 0;
    mTickCounter = 0;
    mStage = Stage::Ready;
    mTuneTargets.clear();
    mAwaitingTuneUpdate = false;
    mTuneDelay = 0;
    mTuneRetryCount = 0;
    mError.clear();
    mWorldPaused.store(false, std::memory_order_release);
    mHasSimulationTick = false;
}

void NoteBotPlayer::beginTuning() {
    if (!mSong) return;
    if (mStage == Stage::Tuning || mStage == Stage::Verifying) return;
    if (mSong->notesMap.empty()) {
        mStage = Stage::Error;
        mError = "Song has no playable notes";
        return;
    }
    auto clientRef = ll::service::getClientInstance();
    auto* player = clientRef.has_value() ? clientRef.value().getLocalPlayer() : nullptr;
    if (!player) {
        mStage = Stage::Error;
        mError = "No active world for playing";
        return;
    }
    scanAndMapNoteBlocks(player->getDimensionBlockSource(), BlockPos(player->getPosition()),
                         player->getEyePos(), mConfig.general.scanRadius);
    prepareTuneTargets();
    if (mTuneTargets.empty()) {
        mStage = Stage::Playing;
        mIsPlaying = true;
    }
}

void NoteBotPlayer::reset() {
    mSong.reset();
    mIsPlaying   = false;
    mCurrentTick = 0;
    mTickCounter = 0;
    mNextReadyScanTime = {};
    mAutoRefreshNearby = false;
    mNoteBlockManager.clear();
    mTuneTargets.clear();
    mAwaitingTuneUpdate = false;
    mTuneDelay = 0;
    mTuneRetryCount = 0;
    mStage = Stage::Ready;
    mError.clear();
    mWorldPaused.store(false, std::memory_order_release);
    mHasSimulationTick = false;
}

void NoteBotPlayer::prepareTuneTargets() {
    mTuneTargets.clear();
    std::unordered_set<BlockPos, BlockPosHash> assigned;
    for (const auto& [tick, mappings] : mNoteBlockManager.getNoteMapping()) {
        for (const auto& mapping : mappings) {
            if (assigned.insert(mapping.blockPos).second) {
                mTuneTargets.push_back({mapping.blockPos, mapping.requiredPitch, 0});
            }
        }
    }
    mTuneIndex = 0;
    mTunePasses = 0;
    mTunePlanReady = false;
    mAwaitingTuneUpdate = false;
    mTuneDelay = 0;
    mTuneRetryCount = 0;
    mIsPlaying = false;
    mError.clear();
    mStage = Stage::Tuning;
}

bool NoteBotPlayer::refreshTuneTarget(TuneTarget& target, BlockSource& blockSource) {
    if (!NoteBlockInteraction::isNoteBlock(blockSource, target.pos)) {
        mError = "A mapped note block is no longer available";
        return false;
    }
    const int current = NoteBlockInteraction::getNoteBlockPitch(blockSource, target.pos);
    if (current < 0 || current >= 25) {
        mError = "A mapped note block is no longer available";
        return false;
    }
    mNoteBlockManager.updateNoteBlockPitch(target.pos, current);
    target.remainingHits = (target.targetPitch - current + 25) % 25;
    return true;
}

bool NoteBotPlayer::updateTuneTargets() {
    auto clientRef = ll::service::getClientInstance();
    if (!clientRef.has_value()) {
        mError = "No active world for tuning";
        return false;
    }
    auto* player = clientRef.value().getLocalPlayer();
    if (!player) {
        mError = "No local player for tuning";
        return false;
    }
    auto& blockSource = player->getDimensionBlockSource();

    for (auto& target : mTuneTargets) {
        if (!refreshTuneTarget(target, blockSource)) return false;
    }
    return true;
}

void NoteBotPlayer::tickTuning() {
    if (mAwaitingTuneUpdate) {
        auto clientRef = ll::service::getClientInstance();
        auto* player = clientRef.has_value() ? clientRef.value().getLocalPlayer() : nullptr;
        if (!player) {
            mStage = Stage::Error;
            mError = "No local player while verifying tuning";
            return;
        }
        int pitch = NoteBlockInteraction::getNoteBlockPitch(
            player->getDimensionBlockSource(), mTuneTargets[mTuneIndex].pos);
        if (pitch == mTunePitchBefore) {
            if (const auto eventPitch = NoteBlockInteraction::noteEventPitch(
                    mTuneTargets[mTuneIndex].pos, mTuneEventSerialBefore)) {
                pitch = *eventPitch;
            }
        }
        if (pitch < 0) {
            mStage = Stage::Error;
            mError = "Note block disappeared while tuning";
            return;
        }
        if (pitch == mTunePitchBefore) {
            if (++mTuneVerifyTicks >= 40) {
                if (mTuneRetryCount++ < 2) {
                    mAwaitingTuneUpdate = false;
                    mTuneDelay = 1;
                    return;
                }
                mStage = Stage::Error;
                const auto& pos = mTuneTargets[mTuneIndex].pos;
                const auto delta = pos.center() - player->getPosition();
                const auto distance = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
                mError = "Tuning ignored at (" + std::to_string(pos.x) + "," +
                    std::to_string(pos.y) + "," + std::to_string(pos.z) +
                    "), distance " + std::to_string(distance) +
                    ", event " + (NoteBlockInteraction::noteEventPitch(pos, mTuneEventSerialBefore)
                        ? "unchanged" : "none");
            }
            return;
        }
        mTuneTargets[mTuneIndex].remainingHits =
            (mTuneTargets[mTuneIndex].targetPitch - pitch + 25) % 25;
        mTuneRetryCount = 0;
        mNoteBlockManager.updateNoteBlockPitch(mTuneTargets[mTuneIndex].pos, pitch);
        mNoteBlockManager.markNoteBlockHit(mTuneTargets[mTuneIndex].pos);
        mAwaitingTuneUpdate = false;
        mTuneDelay = mConfig.tuneIntervalTicks;
    }

    if (mStage == Stage::Verifying) {
        if (--mVerifyDelay > 0) return;
        if (!updateTuneTargets()) {
            mStage = Stage::Error;
            return;
        }
        mTuneIndex = 0;
        const bool done = std::all_of(mTuneTargets.begin(), mTuneTargets.end(),
                                     [](const TuneTarget& target) { return target.remainingHits == 0; });
        if (done) {
            auto clientRef = ll::service::getClientInstance();
            auto* player = clientRef.has_value() ? clientRef.value().getLocalPlayer() : nullptr;
            if (player && player->getPlayerGameType() == GameType::Survival) {
                mStage = Stage::Playing;
                mIsPlaying = true;
            } else {
                mStage = Stage::Ready;
                mError = "Tuned. Switch to survival and press Play";
            }
            return;
        }
        if (++mTunePasses >= 3) {
            mStage = Stage::Error;
            mError = "Tuning did not update the note blocks";
            return;
        }
        mStage = Stage::Tuning;
    }

    if (mTuneDelay > 0) {
        --mTuneDelay;
        return;
    }

    if (!mTunePlanReady) {
        if (!updateTuneTargets()) {
            mStage = Stage::Error;
            return;
        }
        mTunePlanReady = true;
    }
    auto clientRef = ll::service::getClientInstance();
    auto* player = clientRef.has_value() ? clientRef.value().getLocalPlayer() : nullptr;
    if (!player) {
        mStage = Stage::Error;
        mError = "No local player for tuning";
        return;
    }
    auto& blockSource = player->getDimensionBlockSource();
    while (mTuneIndex < mTuneTargets.size()) {
        auto& target = mTuneTargets[mTuneIndex];
        if (!refreshTuneTarget(target, blockSource)) {
            mStage = Stage::Error;
            return;
        }
        if (target.remainingHits != 0) break;
        ++mTuneIndex;
    }
    if (mTuneIndex == mTuneTargets.size()) {
        mStage = Stage::Verifying;
        mVerifyDelay = std::max(1, mConfig.checkNoteblocksDelay);
        return;
    }

    mTunePitchBefore = mNoteBlockManager.getCachedPitch(mTuneTargets[mTuneIndex].pos);
    mTuneEventSerialBefore = NoteBlockInteraction::noteEventSerial();
    if (mTunePitchBefore < 0 ||
        !NoteBlockInteraction::tuneNoteBlock(player, mTuneTargets[mTuneIndex].pos, false)) {
        mStage = Stage::Error;
        mError = "Could not interact with a note block for tuning";
        return;
    }
    mAwaitingTuneUpdate = true;
    mTuneVerifyTicks = 0;
}

void NoteBotPlayer::tick() {
    tickInternal();
    publishDisplayState();
}

void NoteBotPlayer::tickInternal() {
    std::optional<Song> nextSong;
    {
        std::lock_guard lock(mPendingSongMutex);
        nextSong.swap(mPendingSong);
    }
    if (nextSong) {
        reset();
        mSong = std::move(nextSong);
        mNextReadyScanTime = {};
        mAutoRefreshNearby = true;
    }

    if (mClearNoteBlocksRequested.exchange(false, std::memory_order_acq_rel)) {
        stopInternal();
        mNoteBlockManager.clear();
        mAutoRefreshNearby = false;
    }

    const int scanRadius = mPendingScanRadius.exchange(-1, std::memory_order_acq_rel);
    if (scanRadius >= 0) {
        auto clientRef = ll::service::getClientInstance();
        if (clientRef.has_value()) {
            auto& client = clientRef.value();
            if (auto* player = client.getLocalPlayer()) {
                auto& blockSource = player->getDimensionBlockSource();
                scanAndMapNoteBlocks(blockSource, BlockPos(player->getPosition()),
                                     player->getEyePos(), scanRadius);
            }
        }
    }

    switch (mPendingControl.exchange(ControlRequest::None, std::memory_order_acq_rel)) {
    case ControlRequest::Play: beginTuning(); break;
    case ControlRequest::Pause:
        if (mStage == Stage::Playing) mIsPlaying = !mIsPlaying;
        break;
    case ControlRequest::Stop: stopInternal(); break;
    case ControlRequest::None: break;
    }

    if (mStage == Stage::Playing && mIsPlaying) {
        auto clientRef = ll::service::getClientInstance();
        auto* player = clientRef.has_value() ? clientRef.value().getLocalPlayer() : nullptr;
        if (player && player->getPlayerGameType() != GameType::Survival) {
            mIsPlaying = false;
            mStage = Stage::Error;
            mError = "Playing note blocks requires survival mode";
            mWorldPaused.store(false, std::memory_order_release);
            return;
        }
    }

    const auto now = std::chrono::steady_clock::now();
    if (mAutoRefreshNearby && mSong && mStage == Stage::Ready && now >= mNextReadyScanTime) {
        mNextReadyScanTime = now + std::chrono::milliseconds(500);
        auto clientRef = ll::service::getClientInstance();
        auto* player = clientRef.has_value() ? clientRef.value().getLocalPlayer() : nullptr;
        if (player) {
            auto& blockSource = player->getDimensionBlockSource();
            mNoteBlockManager.scanNearbyNoteBlocks(blockSource, BlockPos(player->getPosition()),
                                                    player->getEyePos(), mConfig.general.scanRadius);
            mNoteBlockManager.mapSongToNoteBlocks(*mSong, mConfig.mode == NoteBotConfig::NotebotMode::Auto);
        }
    }

    if (auto clientRef = ll::service::getClientInstance(); clientRef.has_value()) {
        if (auto* player = clientRef.value().getLocalPlayer()) {
            mNoteBlockManager.refreshPitches(player->getDimensionBlockSource());
        }
    }

    if (mStage == Stage::Tuning || mStage == Stage::Verifying || mStage == Stage::Playing) {
        auto clientRef = ll::service::getClientInstance();
        auto* client = clientRef.has_value() ? &clientRef.value() : nullptr;
        auto* player = client ? client->getLocalPlayer() : nullptr;
        if (!client || !player) {
            mWorldPaused.store(true, std::memory_order_release);
            return;
        }

        bool paused = client->isShowingPauseScreen() || player->getLevel().getSimPaused();
        bool sameSimulationTick = false;
        if (auto levelRef = ll::service::getLevel(); levelRef.has_value()) {
            paused = paused || levelRef.value().getSimPaused();
            const uint64_t worldTick = levelRef.value().getCurrentTick().tickID;
            sameSimulationTick = mHasSimulationTick && worldTick == mLastSimulationTick;
            if (!sameSimulationTick) {
                mLastSimulationTick = worldTick;
                mHasSimulationTick = true;
                mLastSimulationAdvance = std::chrono::steady_clock::now();
            }
        }
        const bool stalled = sameSimulationTick &&
            std::chrono::steady_clock::now() - mLastSimulationAdvance > std::chrono::milliseconds(500);
        mWorldPaused.store(paused || stalled, std::memory_order_release);
        if (paused || sameSimulationTick) return;
    } else {
        mWorldPaused.store(false, std::memory_order_release);
    }

    if (mStage == Stage::Tuning || mStage == Stage::Verifying) {
        tickTuning();
        mNoteBlockManager.tick();
        return;
    }

    if (mStage != Stage::Playing || !mIsPlaying || !mSong) return;

    mTickCounter++;
    if (mTickCounter < mConfig.tickDelay) return;
    mTickCounter = 0;

    if (mCurrentTick > mSong->lastTick) {
        if (mConfig.autoPlay) {
            mCurrentTick = 0;
        } else {
            stopInternal();
        }
        return;
    }

    playNotesAtTick(mCurrentTick);
    mCurrentTick++;

    mNoteBlockManager.tick();
}

void NoteBotPlayer::playNotesAtTick(int tick) {
    if (!mSong) return;

    const auto mappings = mNoteBlockManager.getMappingsAtTick(tick);
    if (mappings.empty()) return;

    auto clientRef = ll::service::getClientInstance();
    if (!clientRef.has_value()) return;

    auto& client = clientRef.value();
    auto* player = client.getLocalPlayer();
    if (!player) return;

    if (player->getPlayerGameType() != GameType::Survival) {
        mIsPlaying = false;
        mStage = Stage::Error;
        mError = "Playing note blocks requires survival mode";
        return;
    }

    auto& blockSource = player->getDimensionBlockSource();

    for (const auto& mapping : mappings) {
        const BlockPos& pos = mapping.blockPos;

        if (!NoteBlockInteraction::isNoteBlock(blockSource, pos)) continue;
        int currentPitch = mNoteBlockManager.getCachedPitch(pos);
        if (currentPitch != mapping.requiredPitch) continue;
        if (!NoteBlockInteraction::playNoteBlock(player, pos, mConfig.swingArm)) continue;

        mNoteBlockManager.markNoteBlockHit(pos);
    }
}

void NoteBotPlayer::scanAndMapNoteBlocks(BlockSource& blockSource, const BlockPos& playerPos,
                                        const Vec3& eyePos, int radius) {
    mIsPlaying = false;
    mCurrentTick = 0;
    mNextReadyScanTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    mAutoRefreshNearby = true;
    mStage = Stage::Ready;
    mError.clear();
    mTuneTargets.clear();
    mNoteBlockManager.scanNearbyNoteBlocks(blockSource, playerPos, eyePos, radius);
    if (mSong) {
        mNoteBlockManager.mapSongToNoteBlocks(*mSong, mConfig.mode == NoteBotConfig::NotebotMode::Auto);
    }
}

NoteBotPlayer::DisplayState NoteBotPlayer::getDisplayState() const {
    const auto state = mPublishedDisplay.load(std::memory_order_acquire);
    return state ? *state : DisplayState{};
}

std::string NoteBotPlayer::getStatus() const {
    return getDisplayState().status;
}

void NoteBotPlayer::publishDisplayState() {
    auto state = std::make_shared<DisplayState>();
    state->hasSong = mSong.has_value();
    state->isPlaying = mIsPlaying;
    state->currentTick = mCurrentTick;
    if (mSong) {
        state->totalTicks = mSong->lastTick;
        state->songTitle = mSong->title;
        state->songAuthor = mSong->author;
    }
    state->status = composeStatus();
    mPublishedDisplay.store(std::move(state), std::memory_order_release);
}

std::string NoteBotPlayer::composeStatus() const {
    if (!mSong) return "No song loaded";
    if (mStage == Stage::Error || !mError.empty()) return mError;
    if (mWorldPaused.load(std::memory_order_acquire)) return "World paused; waiting to resume";
    if (mStage == Stage::Tuning && mTuneIndex < mTuneTargets.size()) {
        const auto& target = mTuneTargets[mTuneIndex];
        const int current = mNoteBlockManager.getCachedPitch(target.pos);
        return "Tuning " + std::to_string(mTuneIndex + 1) + "/" +
            std::to_string(mTuneTargets.size()) + ": " + std::to_string(current) +
            " -> " + std::to_string(target.targetPitch) +
            " (" + std::to_string(target.remainingHits) + " clicks left)";
    }
    if (mStage == Stage::Tuning || mStage == Stage::Verifying) return "Checking tuned note blocks";
    if (mIsPlaying) {
        const auto diagnostics = mNoteBlockManager.getMappingDiagnostics();
        if (diagnostics.mapped == 0) return "Playing silently: no notes mapped";
        if (!diagnostics.missing.empty()) {
            return "Playing (skipping " + std::to_string(diagnostics.missing.size()) +
                   " missing notes)";
        }
        return "Playing";
    }
    if (mCurrentTick > 0) return "Paused";
    return "Ready";
}

} // namespace notebot
