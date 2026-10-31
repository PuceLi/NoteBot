#include "NoteBotPlayer.h"
#include "NBSDecoder.h"
#include "NoteBlockInteraction.h"
#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/sound/SoundEngine.h"
#include "mc/deps/audio/ServerSoundHandle.h"
#include "mc/deps/shared_types/v1_26_20/block/NoteBlockInstrumentEnum.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include <algorithm>
#include <cmath>
#include <string_view>
#include <unordered_set>

namespace notebot {
namespace {
std::string pathToUtf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

std::string_view previewSoundName(Instrument instrument) {
    switch (instrument) {
    case Instrument::Harp: return "note.harp";
    case Instrument::Bass: return "note.bass";
    case Instrument::BassDrum: return "note.bd";
    case Instrument::Snare: return "note.snare";
    case Instrument::Hat: return "note.hat";
    case Instrument::Guitar: return "note.guitar";
    case Instrument::Flute: return "note.flute";
    case Instrument::Bell: return "note.bell";
    case Instrument::Chime: return "note.chime";
    case Instrument::Xylophone: return "note.xylobone";
    case Instrument::IronXylophone: return "note.iron_xylophone";
    case Instrument::CowBell: return "note.cow_bell";
    case Instrument::Didgeridoo: return "note.didgeridoo";
    case Instrument::Bit: return "note.bit";
    case Instrument::Banjo: return "note.banjo";
    case Instrument::Pling: return "note.pling";
    case Instrument::Trumpet: return "note.trumpet";
    case Instrument::TrumpetExposed: return "note.trumpet_exposed";
    case Instrument::TrumpetWeathered: return "note.trumpet_weathered";
    case Instrument::TrumpetOxidized: return "note.trumpet_oxidized";
    case Instrument::Zombie:
    case Instrument::Skeleton:
    case Instrument::Creeper:
    case Instrument::Dragon:
    case Instrument::WitherSkeleton:
    case Instrument::Piglin:
    case Instrument::None:
    case Instrument::Size: return {};
    }
    return {};
}

std::string instrumentName(int instrument) {
    if (instrument < 0 || instrument >= static_cast<int>(Instrument::Size))
        return "Unrecognized instrument ID " + std::to_string(instrument);
    return SharedTypes::v1_26_20::NoteBlockInstrumentEnum::noteBlockInstrumentToString(
        static_cast<Instrument>(instrument));
}

std::string describeMissingNote(const NoteBlockManager::RequiredNote& missing) {
    std::string description = "pitch " + std::to_string(missing.pitch);
    if (missing.pitch < 0 || missing.pitch > 24) {
        description += " is outside the in-game note-block range 0-24";
    }
    if (missing.customInstrument) {
        description += "; NBS custom instrument ID " + std::to_string(missing.instrument) +
            " cannot be played because its custom sound is not loaded by NoteBot";
    } else if (missing.instrument >= static_cast<int>(Instrument::Zombie)) {
        description += "; " + std::string(instrumentName(missing.instrument)) + " (ID " +
            std::to_string(missing.instrument) + ") is not a melodic note-block instrument";
    } else if (missing.instrument >= 0) {
        description += " (" + std::string(instrumentName(missing.instrument)) + ", NBS ID " +
            std::to_string(missing.sourceInstrumentId) + ", game instrument ID " +
            std::to_string(missing.instrument) + ")";
    }
    return description;
}
}

NoteBotPlayer::NoteBotPlayer() : mConfig(notebot::getConfig()) { publishDisplayState(); }
NoteBotPlayer::~NoteBotPlayer() = default;

bool NoteBotPlayer::loadSong(const std::filesystem::path& filePath) {
    return queueSong(filePath, false);
}

bool NoteBotPlayer::previewSong(const std::filesystem::path& filePath) {
    return queueSong(filePath, true);
}

bool NoteBotPlayer::queueSong(const std::filesystem::path& filePath, bool preview) {
    auto result = NBSDecoder::decodeWithError(filePath);
    std::lock_guard lock(mPendingSongMutex);
    if (!result.song) {
        const auto filename = pathToUtf8(filePath.filename());
        mPendingSong.reset();
        mPendingSongError = "Could not load \"" + filename + "\": " + result.error;
        return false;
    }
    if (result.song->notesMap.empty()) {
        const auto filename = pathToUtf8(filePath.filename());
        mPendingSong.reset();
        mPendingSongError = "Could not load \"" + filename + "\": no note events were found";
        return false;
    }

    std::string warning;
    size_t unsupportedPitchCount = 0;
    size_t unsupportedInstrumentCount = 0;
    int firstPitchTick = -1;
    int firstPitch = 0;
    int firstCustomInstrumentTick = -1;
    int firstCustomInstrument = -1;
    for (const auto& [tick, note] : result.song->notesMap) {
        const int pitch = note.noteLevel;
        if (!preview && (pitch < 0 || pitch > 24)) {
            if (unsupportedPitchCount == 0) {
                firstPitchTick = tick;
                firstPitch = pitch;
            }
            ++unsupportedPitchCount;
        }
        if (note.customInstrument) {
            if (unsupportedInstrumentCount == 0) {
                firstCustomInstrumentTick = tick;
                firstCustomInstrument = note.sourceInstrumentId;
            }
            ++unsupportedInstrumentCount;
        }
    }
    if (unsupportedPitchCount > 0) {
        warning = std::to_string(unsupportedPitchCount) +
            " note(s) are outside the in-game note-block pitch range 0-24; first at tick " +
            std::to_string(firstPitchTick) + " has pitch " + std::to_string(firstPitch);
    }
    if (unsupportedInstrumentCount > 0) {
        if (!warning.empty()) warning += "; ";
        warning += std::to_string(unsupportedInstrumentCount) + " note(s) use " +
            "NBS custom instrument ID " + std::to_string(firstCustomInstrument) +
            "; its custom sound is not loaded by NoteBot (first at tick " +
            std::to_string(firstCustomInstrumentTick) + ")";
    }

    mPendingSongError.reset();
    mPendingSong = PendingSong{std::move(*result.song), preview, std::move(warning)};
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
    mPreviewMode = false;
    mCurrentTick = 0;
    mPlaybackClock.reset();
    mNextPitchRefreshTime = {};
    mStage = Stage::Ready;
    mTuneTargets.clear();
    mAwaitingTuneUpdate = false;
    mTuneDelay = 0;
    mTuneRetryCount = 0;
    mError.clear();
    mSongWarning.clear();
    mWorldPaused.store(false, std::memory_order_release);
    mHasSimulationTick = false;
    mHasWorldPlaybackTick = false;
}

void NoteBotPlayer::beginTuning() {
    if (!mSong) return;
    if (mStage == Stage::Tuning || mStage == Stage::Verifying) return;
    if (mSong->notesMap.empty()) {
        mStage = Stage::Error;
        mError = "Song has no playable notes";
        return;
    }
    if (mPreviewMode) {
        mStage = Stage::Playing;
        mIsPlaying = true;
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
    mPreviewMode = false;
    mCurrentTick = 0;
    mPlaybackClock.reset();
    mNextPitchRefreshTime = {};
    mNextReadyScanTime = {};
    mAutoRefreshNearby = false;
    mNoteBlockManager.clear();
    mTuneTargets.clear();
    mAwaitingTuneUpdate = false;
    mTuneDelay = 0;
    mTuneRetryCount = 0;
    mStage = Stage::Ready;
    mError.clear();
    mSongWarning.clear();
    mWorldPaused.store(false, std::memory_order_release);
    mHasSimulationTick = false;
    mHasWorldPlaybackTick = false;
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
    std::optional<PendingSong> nextSong;
    std::optional<std::string> loadError;
    {
        std::lock_guard lock(mPendingSongMutex);
        nextSong.swap(mPendingSong);
        loadError.swap(mPendingSongError);
    }
    if (loadError) {
        mError = std::move(*loadError);
        if (!mSong) mStage = Stage::Error;
    }
    if (nextSong) {
        reset();
        mSong = std::move(nextSong->song);
        mPreviewMode = nextSong->preview;
        mSongWarning = std::move(nextSong->warning);
        mNextReadyScanTime = {};
        mAutoRefreshNearby = !mPreviewMode;
        if (mPreviewMode) {
            if (mSong->notesMap.empty()) {
                mStage = Stage::Error;
                mError = "Song has no playable notes";
            } else {
                mStage = Stage::Playing;
                mIsPlaying = true;
            }
        }
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
    case ControlRequest::Play:
        if (mStage == Stage::Playing && mSong) {
            if (!mIsPlaying) {
                mPlaybackClock.reset();
                mHasWorldPlaybackTick = false;
            }
            mIsPlaying = true;
        } else {
            beginTuning();
        }
        break;
    case ControlRequest::Pause:
        if (mStage == Stage::Playing) {
            mIsPlaying = !mIsPlaying;
            mPlaybackClock.reset();
            mHasWorldPlaybackTick = false;
        }
        break;
    case ControlRequest::Stop: stopInternal(); break;
    case ControlRequest::None: break;
    }

    if (mStage == Stage::Playing && mIsPlaying && !mPreviewMode) {
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
            mNoteBlockManager.mapSongToNoteBlocks(*mSong,
                mConfig.mode == NoteBotConfig::NotebotMode::Auto, mConfig.noteMap);
        }
    }

    const auto pitchRefreshNow = std::chrono::steady_clock::now();
    const bool activeBlockPlayback = mStage == Stage::Playing && mIsPlaying && !mPreviewMode;
    if (!mPreviewMode && mStage != Stage::Error &&
        (!activeBlockPlayback || pitchRefreshNow >= mNextPitchRefreshTime)) {
        if (auto clientRef = ll::service::getClientInstance(); clientRef.has_value()) {
            if (auto* player = clientRef.value().getLocalPlayer();
                player && !clientRef.value().isShowingPauseScreen()) {
                mNoteBlockManager.refreshPitches(player->getDimensionBlockSource());
                if (activeBlockPlayback)
                    mNextPitchRefreshTime = pitchRefreshNow + std::chrono::milliseconds(100);
            }
        }
    }

    bool worldTickAvailable = false;
    if (mStage == Stage::Tuning || mStage == Stage::Verifying || mStage == Stage::Playing) {
        auto clientRef = ll::service::getClientInstance();
        auto* client = clientRef.has_value() ? &clientRef.value() : nullptr;
        auto* player = client ? client->getLocalPlayer() : nullptr;
        if (!client || (!mPreviewMode && !player)) {
            mWorldPaused.store(true, std::memory_order_release);
            mPlaybackClock.reset();
            mHasWorldPlaybackTick = false;
            return;
        }

        if (mPreviewMode) {
            mWorldPaused.store(false, std::memory_order_release);
            mHasSimulationTick = false;
        } else {
            bool paused = client->isShowingPauseScreen() || player->getLevel().getSimPaused();
            bool sameSimulationTick = false;
            if (auto levelRef = ll::service::getLevel(); levelRef.has_value()) {
                worldTickAvailable = true;
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
            if (paused || stalled) {
                mPlaybackClock.reset();
                mHasWorldPlaybackTick = false;
                return;
            }
            if (sameSimulationTick) return;
        }
    } else {
        mWorldPaused.store(false, std::memory_order_release);
    }

    if (mStage == Stage::Tuning || mStage == Stage::Verifying) {
        tickTuning();
        mNoteBlockManager.tick();
        return;
    }

    if (mStage != Stage::Playing || !mIsPlaying || !mSong) {
        mPlaybackClock.reset();
        mHasWorldPlaybackTick = false;
        return;
    }
    if (mPreviewMode || !worldTickAvailable) {
        if (!worldTickAvailable) mHasWorldPlaybackTick = false;
        if (!mPlaybackClock.advance(std::chrono::steady_clock::now(), mConfig.tickDelay)) return;
    } else {
        // In-world playback already has a canonical 20 Hz clock. Using the real-time
        // clock here as well would add another 50 ms gate on top of each world tick.
        if (!mHasWorldPlaybackTick) {
            mNextWorldPlaybackTick = mLastSimulationTick;
            mHasWorldPlaybackTick = true;
        }
        if (mLastSimulationTick < mNextWorldPlaybackTick) return;
        mNextWorldPlaybackTick = mLastSimulationTick +
            static_cast<uint64_t>(std::clamp(mConfig.tickDelay, 0, 20) + 1);
    }

    if (mCurrentTick > mSong->lastTick) {
        if (mConfig.autoPlay && !mPreviewMode) {
            mCurrentTick = 0;
        } else {
            stopInternal();
        }
        return;
    }

    if (mPreviewMode) {
        playPreviewNotesAtTick(mCurrentTick);
    } else {
        playNotesAtTick(mCurrentTick);
    }
    mCurrentTick++;

    if (!mPreviewMode) mNoteBlockManager.tick();
}

void NoteBotPlayer::playPreviewNotesAtTick(int tick) {
    if (!mSong) return;

    auto clientRef = ll::service::getClientInstance();
    auto* client = clientRef.has_value() ? &clientRef.value() : nullptr;
    auto* player = client ? client->getLocalPlayer() : nullptr;
    if (!client) return;

    auto& soundEngine = *client->getSoundEngine();
    const Vec3 position = player ? player->getPosition() : Vec3{};
    const float volume = 2.0f;
    const auto range = mSong->notesMap.equal_range(tick);
    for (auto it = range.first; it != range.second; ++it) {
        const Note& note = it->second;
        if (note.customInstrument) continue;
        Instrument instrument = note.instrument;
        if (mConfig.mode == NoteBotConfig::NotebotMode::Manual) {
            instrument = mapInstrument(note.sourceInstrumentId, note.instrument, mConfig.noteMap);
        }
        const auto sound = previewSoundName(instrument);
        if (sound.empty()) continue;
        const float pitch = std::pow(2.0f, (static_cast<float>(note.noteLevel) - 12.0f) / 12.0f);
        const std::string soundName(sound);
        if (player) {
            soundEngine.play(soundName, position, volume, pitch, std::nullopt);
        } else {
            soundEngine.playUI(soundName, volume, pitch);
        }
    }
}

void NoteBotPlayer::playNotesAtTick(int tick) {
    if (!mSong) return;

    mNoteBlockManager.getMappingsAtTick(tick, mTickMappings);
    if (mTickMappings.empty()) return;

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

    for (const auto& mapping : mTickMappings) {
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
    mPreviewMode = false;
    mCurrentTick = 0;
    mPlaybackClock.reset();
    mNextReadyScanTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    mAutoRefreshNearby = true;
    mStage = Stage::Ready;
    mError.clear();
    mTuneTargets.clear();
    mNoteBlockManager.scanNearbyNoteBlocks(blockSource, playerPos, eyePos, radius);
    if (mSong) {
        mNoteBlockManager.mapSongToNoteBlocks(*mSong,
            mConfig.mode == NoteBotConfig::NotebotMode::Auto, mConfig.noteMap);
    }
}

NoteBotPlayer::DisplayState NoteBotPlayer::getDisplayState() const {
    std::lock_guard lock(mDisplayMutex);
    return mPublishedDisplay;
}

bool NoteBotPlayer::isPlaying() const {
    std::lock_guard lock(mDisplayMutex);
    return mPublishedDisplay.isPlaying;
}

bool NoteBotPlayer::hasSong() const {
    std::lock_guard lock(mDisplayMutex);
    return mPublishedDisplay.hasSong;
}

std::string NoteBotPlayer::getStatus() const {
    return getDisplayState().status;
}

void NoteBotPlayer::publishDisplayState() {
    std::lock_guard lock(mDisplayMutex);
    mPublishedDisplay.hasSong = mSong.has_value();
    mPublishedDisplay.isPlaying = mIsPlaying;
    mPublishedDisplay.isPreview = mPreviewMode;
    mPublishedDisplay.currentTick = mCurrentTick;
    if (mSong) {
        mPublishedDisplay.totalTicks = mSong->lastTick;
        if (mPublishedDisplay.songTitle != mSong->title) mPublishedDisplay.songTitle = mSong->title;
        if (mPublishedDisplay.songAuthor != mSong->author) mPublishedDisplay.songAuthor = mSong->author;
    } else {
        mPublishedDisplay.totalTicks = 0;
        mPublishedDisplay.songTitle.clear();
        mPublishedDisplay.songAuthor.clear();
    }
    if (mStage == Stage::Error || !mError.empty()) {
        if (mPublishedDisplay.status != mError) mPublishedDisplay.status = mError;
    } else {
        auto status = composeStatus();
        if (mPublishedDisplay.status != status) mPublishedDisplay.status = std::move(status);
    }
}

std::string NoteBotPlayer::composeStatus() const {
    if (mStage == Stage::Error || !mError.empty()) return mError;
    if (!mSong) return "No song loaded";
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
        if (mPreviewMode) {
            return mSongWarning.empty() ? "Previewing" : "Previewing; " + mSongWarning;
        }
        const auto diagnostics = mNoteBlockManager.getMappingDiagnostics();
        if (diagnostics.mapped == 0 && diagnostics.required > 0) {
            const auto& missing = diagnostics.missing.front();
            std::string status = "No matching note blocks: 0/" +
                std::to_string(diagnostics.required) + " note types mapped; " +
                describeMissingNote(missing);
            if (!mSongWarning.empty()) status += "; " + mSongWarning;
            return status;
        }
        if (diagnostics.mapped == 0 && !mSongWarning.empty()) return mSongWarning;
        if (!diagnostics.missing.empty()) {
            const auto& missing = diagnostics.missing.front();
            std::string status = "Playing; " + std::to_string(diagnostics.missing.size()) +
                " note type(s) have no matching note block; " + describeMissingNote(missing);
            if (!mSongWarning.empty()) status += "; " + mSongWarning;
            return status;
        }
        if (!mSongWarning.empty()) return "Playing; " + mSongWarning;
        return "Playing";
    }
    if (mCurrentTick > 0) return mSongWarning.empty() ? "Paused" : "Paused; " + mSongWarning;
    if (!mSongWarning.empty()) return "Ready; " + mSongWarning;
    return "Ready";
}

} // namespace notebot
