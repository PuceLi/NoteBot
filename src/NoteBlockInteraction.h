#pragma once

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/world/level/BlockPos.h"
#include <cstdint>
#include <optional>

class LocalPlayer;
class BlockSource;
class NoteBlockActor;

namespace notebot {

class NoteBlockInteraction {
public:
    static Vec2 calculateRotation(const Vec3& playerPos, const BlockPos& blockPos);

    static void setBodyRotation(LocalPlayer* player, const Vec2& rotation);

    static bool tuneNoteBlock(LocalPlayer* player, const BlockPos& pos, bool swingArm = true);

    static bool playNoteBlock(LocalPlayer* player, const BlockPos& pos, bool swingArm = true);

    static int getNoteBlockPitch(BlockSource& blockSource, const BlockPos& pos);

    static bool isNoteBlock(BlockSource& blockSource, const BlockPos& pos);

    static void recordNoteEvent(const BlockPos& pos, int pitch);
    static uint64_t noteEventSerial();
    static std::optional<int> noteEventPitch(const BlockPos& pos, uint64_t afterSerial);
    static void clearNoteEvents();
};

} // namespace notebot
