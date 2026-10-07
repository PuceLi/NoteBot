#pragma once

#include "NoteData.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/deps/core/math/Vec3.h"
#include <vector>
#include <unordered_map>
#include <optional>
#include <mutex>

namespace notebot {

struct BlockPosHash {
    std::size_t operator()(const BlockPos& pos) const {
        return std::hash<int>()(pos.x) ^ (std::hash<int>()(pos.y) << 1) ^ (std::hash<int>()(pos.z) << 2);
    }
};

struct NoteBlockInfo {
    BlockPos pos;
    int      instrument;
    int      pitch;
    bool     isTuned = false;
    bool     isHit   = false;
    int      hitTimer = 0;
    bool     awaitingActorSync = false;
};

class NoteBlockManager {
public:
    NoteBlockManager();
    ~NoteBlockManager();

    void scanNearbyNoteBlocks(BlockSource& blockSource, const BlockPos& playerPos,
                              const Vec3& eyePos, int radius);
    void mapSongToNoteBlocks(const Song& song, bool anyInstrument = false);
    void clear();

    void tick();
    void refreshPitches(BlockSource& blockSource);
    void markNoteBlockHit(const BlockPos& pos);
    void updateNoteBlockPitch(const BlockPos& pos, int pitch);
    int getCachedPitch(const BlockPos& pos) const;

    struct NoteMapping {
        BlockPos blockPos;
        int      requiredPitch;
        int      requiredInstrument;
    };

    struct RenderSnapshot {
        std::vector<NoteBlockInfo> noteBlocks;
        std::unordered_map<BlockPos, int, BlockPosHash> targetPitches;
    };

    struct RequiredNote {
        int instrument;
        int pitch;
    };
    struct MappingDiagnostics {
        size_t required = 0;
        size_t mapped = 0;
        std::vector<RequiredNote> missing;
    };

    RenderSnapshot getRenderSnapshot() const;
    MappingDiagnostics getMappingDiagnostics() const;
    size_t getNoteBlockCount() const;
    bool hasMapping() const;
    std::unordered_map<int, std::vector<NoteMapping>> getNoteMapping() const;
    std::vector<NoteMapping> getMappingsAtTick(int tick) const;

private:
    mutable std::mutex mMutex;
    std::vector<NoteBlockInfo> mNoteBlocks;
    std::unordered_map<int, std::vector<NoteMapping>> mNoteMapping;
    std::unordered_map<BlockPos, int, BlockPosHash> mTargetPitches;
    MappingDiagnostics mMappingDiagnostics;
    uint64_t mLastNoteEventSerial = 0;

    bool isNoteBlockAt(BlockSource& blockSource, const BlockPos& pos);
    std::optional<NoteBlockInfo> getNoteBlockInfo(BlockSource& blockSource, const BlockPos& pos);
};

} // namespace notebot
