#include "NoteBlockManager.h"
#include "NoteBlockInteraction.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/NoteBlock.h"
#include "mc/world/level/block/actor/NoteBlockActor.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/ChunkPos.h"
#include <algorithm>
#include <set>
#include <tuple>
#include <unordered_set>

namespace notebot {

NoteBlockManager::NoteBlockManager() = default;
NoteBlockManager::~NoteBlockManager() = default;

void NoteBlockManager::scanNearbyNoteBlocks(BlockSource& blockSource, const BlockPos& playerPos,
                                           const Vec3& eyePos, int radius) {
    std::lock_guard lock(mMutex);
    mNoteBlocks.clear();
    mNoteMapping.clear();
    mTargetPitches.clear();
    mMappingDiagnostics = {};
    mLastNoteEventSerial = NoteBlockInteraction::noteEventSerial();

    radius = std::clamp(radius, 1, 6);
    const int minY = std::max<int>(blockSource.getMinHeight(), playerPos.y - radius);
    const int maxY = std::min<int>(blockSource.getMaxHeight() - 1, playerPos.y + radius);
    std::unordered_set<ChunkPos> loadedChunks;

    for (int x = -radius; x <= radius; ++x) {
        for (int y = minY; y <= maxY; ++y) {
            for (int z = -radius; z <= radius; ++z) {
                BlockPos pos(playerPos.x + x, y, playerPos.z + z);
                const float dx = std::max({float(pos.x) - eyePos.x, 0.0f, eyePos.x - float(pos.x + 1)});
                const float dy = std::max({float(pos.y) - eyePos.y, 0.0f, eyePos.y - float(pos.y + 1)});
                const float dz = std::max({float(pos.z) - eyePos.z, 0.0f, eyePos.z - float(pos.z + 1)});
                if (dx * dx + dy * dy + dz * dz > 25.0f) continue;

                const ChunkPos chunk(pos);
                if (!loadedChunks.contains(chunk)) {
                    if (!blockSource.hasChunk(chunk, false)) continue;
                    loadedChunks.insert(chunk);
                }

                if (isNoteBlockAt(blockSource, pos)) {
                    auto info = getNoteBlockInfo(blockSource, pos);
                    if (info) {
                        mNoteBlocks.push_back(*info);
                    }
                }
            }
        }
    }

    std::sort(mNoteBlocks.begin(), mNoteBlocks.end(), [&playerPos](const NoteBlockInfo& a, const NoteBlockInfo& b) {
        const auto distanceSquared = [&playerPos](const BlockPos& pos) {
            const int64_t dx = static_cast<int64_t>(pos.x) - playerPos.x;
            const int64_t dy = static_cast<int64_t>(pos.y) - playerPos.y;
            const int64_t dz = static_cast<int64_t>(pos.z) - playerPos.z;
            return dx * dx + dy * dy + dz * dz;
        };
        return distanceSquared(a.pos) < distanceSquared(b.pos);
    });
}

void NoteBlockManager::mapSongToNoteBlocks(const Song& song, bool anyInstrument,
                                           const NoteMap& noteMap) {
    std::lock_guard lock(mMutex);
    mNoteMapping.clear();
    mTargetPitches.clear();
    mMappingDiagnostics = {};
    std::map<std::pair<int, int>, size_t> assigned;
    std::map<std::tuple<bool, int, int>, int> required;
    std::vector<bool> used(mNoteBlocks.size(), false);

    for (const auto& [tick, note] : song.notesMap) {
        const int instrument = static_cast<int>(anyInstrument || note.customInstrument
            ? note.instrument : mapInstrument(note.sourceInstrumentId, note.instrument, noteMap));
        const int pitch = note.noteLevel;
        const int requiredInstrument = note.customInstrument
            ? note.sourceInstrumentId
            : (anyInstrument ? -1 : instrument);
        const auto key = std::tuple{note.customInstrument, requiredInstrument, pitch};
        required.try_emplace(key, note.sourceInstrumentId);
        // Custom NBS instruments and pitches outside the note-block range cannot be
        // represented in-game. Keep them in diagnostics rather than silently dropping them.
        if (note.customInstrument || pitch < 0 || pitch >= 25 || instrument < 0 ||
            instrument >= static_cast<int>(Instrument::Zombie)) continue;

        const auto mappingKey = std::pair{anyInstrument ? -1 : instrument, pitch};
        auto found = assigned.find(mappingKey);
        if (found == assigned.end()) {
            size_t index = mNoteBlocks.size();
            for (size_t i = 0; i < mNoteBlocks.size(); ++i) {
                if (!used[i] && (anyInstrument || mNoteBlocks[i].instrument == instrument)
                    && mNoteBlocks[i].pitch == pitch) {
                    index = i;
                    break;
                }
            }
            if (index == mNoteBlocks.size()) {
                int fewestClicks = 25;
                for (size_t i = 0; i < mNoteBlocks.size(); ++i) {
                    if (!used[i] && (anyInstrument || mNoteBlocks[i].instrument == instrument)) {
                        const int clicks = (pitch - mNoteBlocks[i].pitch + 25) % 25;
                        if (clicks < fewestClicks) {
                            fewestClicks = clicks;
                            index = i;
                        }
                    }
                }
            }
            if (index == mNoteBlocks.size()) continue;
            used[index] = true;
            found = assigned.emplace(mappingKey, index).first;
        }

        auto& block = mNoteBlocks[found->second];
        block.isTuned = block.pitch == pitch;
        mNoteMapping[tick].push_back({block.pos, pitch, instrument});
        mTargetPitches.emplace(block.pos, pitch);
    }
    mMappingDiagnostics.required = required.size();
    mMappingDiagnostics.mapped = assigned.size();
    for (const auto& [key, sourceInstrumentId] : required) {
        const auto& [isCustomInstrument, instrument, pitch] = key;
        const auto mappingKey = std::pair{isCustomInstrument || anyInstrument ? -1 : instrument, pitch};
        if (isCustomInstrument || !assigned.contains(mappingKey)) {
            mMappingDiagnostics.missing.push_back(
                {instrument, pitch, isCustomInstrument, sourceInstrumentId});
        }
    }
}

void NoteBlockManager::clear() {
    std::lock_guard lock(mMutex);
    mNoteBlocks.clear();
    mNoteMapping.clear();
    mTargetPitches.clear();
    mMappingDiagnostics = {};
    mLastNoteEventSerial = NoteBlockInteraction::noteEventSerial();
}

void NoteBlockManager::refreshPitches(BlockSource& blockSource) {
    std::lock_guard lock(mMutex);
    if (mNoteBlocks.empty()) return;
    const uint64_t currentSerial = NoteBlockInteraction::noteEventSerial();
    for (auto& block : mNoteBlocks) {
        const int actorPitch = NoteBlockInteraction::getNoteBlockPitch(blockSource, block.pos);
        const auto eventPitch = NoteBlockInteraction::noteEventPitch(block.pos, mLastNoteEventSerial);
        if (eventPitch) {
            if (block.pitch != *eventPitch) {
                block.pitch = *eventPitch;
                block.isHit = true;
                block.hitTimer = 10;
            }
            block.awaitingActorSync = actorPitch != *eventPitch;
        } else if (actorPitch >= 0 && actorPitch < 25) {
            if (block.awaitingActorSync) {
                if (actorPitch == block.pitch) block.awaitingActorSync = false;
            } else if (block.pitch != actorPitch) {
                block.pitch = actorPitch;
                block.isHit = true;
                block.hitTimer = 10;
            }
        }
    }
    mLastNoteEventSerial = currentSerial;
}

void NoteBlockManager::tick() {
    std::lock_guard lock(mMutex);
    for (auto& block : mNoteBlocks) {
        if (block.isHit && block.hitTimer > 0) {
            block.hitTimer--;
            if (block.hitTimer <= 0) {
                block.isHit = false;
            }
        }
    }
}

void NoteBlockManager::markNoteBlockHit(const BlockPos& pos) {
    std::lock_guard lock(mMutex);
    for (auto& block : mNoteBlocks) {
        if (block.pos == pos) {
            block.isHit = true;
            block.hitTimer = 10;
            break;
        }
    }
}

void NoteBlockManager::updateNoteBlockPitch(const BlockPos& pos, int pitch) {
    std::lock_guard lock(mMutex);
    for (auto& block : mNoteBlocks) {
        if (block.pos == pos) {
            block.pitch = pitch;
            break;
        }
    }
}

int NoteBlockManager::getCachedPitch(const BlockPos& pos) const {
    std::lock_guard lock(mMutex);
    for (const auto& block : mNoteBlocks) {
        if (block.pos == pos) return block.pitch;
    }
    return -1;
}

NoteBlockManager::RenderSnapshot NoteBlockManager::getRenderSnapshot() const {
    std::lock_guard lock(mMutex);
    return {mNoteBlocks, mTargetPitches};
}

NoteBlockManager::MappingDiagnostics NoteBlockManager::getMappingDiagnostics() const {
    std::lock_guard lock(mMutex);
    return mMappingDiagnostics;
}

size_t NoteBlockManager::getNoteBlockCount() const {
    std::lock_guard lock(mMutex);
    return mNoteBlocks.size();
}

bool NoteBlockManager::hasMapping() const {
    std::lock_guard lock(mMutex);
    return !mNoteMapping.empty();
}

std::unordered_map<int, std::vector<NoteBlockManager::NoteMapping>> NoteBlockManager::getNoteMapping() const {
    std::lock_guard lock(mMutex);
    return mNoteMapping;
}

void NoteBlockManager::getMappingsAtTick(int tick, std::vector<NoteMapping>& output) const {
    std::lock_guard lock(mMutex);
    const auto found = mNoteMapping.find(tick);
    output.clear();
    if (found != mNoteMapping.end()) output.insert(output.end(), found->second.begin(), found->second.end());
}

bool NoteBlockManager::isNoteBlockAt(BlockSource& blockSource, const BlockPos& pos) {
    return NoteBlockInteraction::isNoteBlock(blockSource, pos);
}

std::optional<NoteBlockInfo> NoteBlockManager::getNoteBlockInfo(BlockSource& blockSource, const BlockPos& pos) {
    if (!isNoteBlockAt(blockSource, pos)) {
        return std::nullopt;
    }

    auto* blockActor = blockSource.getBlockEntity(pos);
    if (!blockActor || blockActor->getType() != BlockActorType::Music) {
        return std::nullopt;
    }

    NoteBlockInfo info;
    info.pos = pos;
    info.pitch = NoteBlockInteraction::getNoteBlockPitch(blockSource, pos);
    if (info.pitch < 0 || info.pitch >= 25) return std::nullopt;

    // 获取乐器类型
    auto instrument = NoteBlockActor::getInstrument(blockSource, pos);
    info.instrument = instrument ? static_cast<int>(*instrument) : -1;

    info.isTuned = false;
    info.isHit = false;
    info.hitTimer = 0;

    return info;
}

} // namespace notebot
