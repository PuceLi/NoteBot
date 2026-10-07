#include "NoteBlockInteraction.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/NoteBlock.h"
#include "mc/world/level/block/actor/NoteBlockActor.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/ChunkPos.h"
#include "mc/world/actor/ActorSwingSource.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/item/HandSlot.h"
#include <cmath>
#include <mutex>
#include <unordered_map>

namespace notebot {

namespace {
struct NoteEvent {
    int pitch = -1;
    uint64_t serial = 0;
};
struct PosKey {
    int x, y, z;
    bool operator==(const PosKey&) const = default;
};
struct PosKeyHash {
    size_t operator()(const PosKey& pos) const {
        return std::hash<int>{}(pos.x) ^ (std::hash<int>{}(pos.y) << 1)
            ^ (std::hash<int>{}(pos.z) << 2);
    }
};
PosKey key(const BlockPos& pos) { return {pos.x, pos.y, pos.z}; }
std::mutex noteEventsMutex;
std::unordered_map<PosKey, NoteEvent, PosKeyHash> noteEvents;
uint64_t lastNoteEventSerial = 0;
}

void NoteBlockInteraction::recordNoteEvent(const BlockPos& pos, int pitch) {
    if (pitch < 0 || pitch >= 25) return;
    std::lock_guard lock(noteEventsMutex);
    ++lastNoteEventSerial;
    if (noteEvents.size() > 4096) noteEvents.clear();
    noteEvents[key(pos)] = {pitch, lastNoteEventSerial};
}

uint64_t NoteBlockInteraction::noteEventSerial() {
    std::lock_guard lock(noteEventsMutex);
    return lastNoteEventSerial;
}

std::optional<int> NoteBlockInteraction::noteEventPitch(const BlockPos& pos, uint64_t afterSerial) {
    std::lock_guard lock(noteEventsMutex);
    const auto found = noteEvents.find(key(pos));
    if (found == noteEvents.end() || found->second.serial <= afterSerial) return std::nullopt;
    return found->second.pitch;
}

void NoteBlockInteraction::clearNoteEvents() {
    std::lock_guard lock(noteEventsMutex);
    noteEvents.clear();
    ++lastNoteEventSerial;
}

Vec2 NoteBlockInteraction::calculateRotation(const Vec3& playerPos, const BlockPos& blockPos) {
    Vec3 blockCenter = blockPos.center();
    Vec3 diff = blockCenter - playerPos;

    float horizontalDist = std::sqrt(diff.x * diff.x + diff.z * diff.z);
    float yaw = std::atan2(diff.z, diff.x) * 180.0f / 3.14159265f - 90.0f;
    float pitch = -std::atan2(diff.y, horizontalDist) * 180.0f / 3.14159265f;

    while (yaw < -180.0f) yaw += 360.0f;
    while (yaw > 180.0f) yaw -= 360.0f;

    pitch = std::clamp(pitch, -90.0f, 90.0f);

    return Vec2{pitch, yaw};
}

void NoteBlockInteraction::setBodyRotation(LocalPlayer* player, const Vec2& rotation) {
    if (!player) return;

    // 设置旋转
    player->setRotationWrapped(rotation);
}

bool NoteBlockInteraction::tuneNoteBlock(LocalPlayer* player, const BlockPos& pos, bool swingArm) {
    if (!player) return false;

    BlockSource& blockSource = player->getDimensionBlockSource();

    // 检查是否是音符盒
    if (!isNoteBlock(blockSource, pos)) return false;

    if (!player->mGameMode) return false;
    if (!player->mGameMode->buildBlock(pos, 1, HandSlot::Mainhand, false)) return false;

    // 挥手
    if (swingArm) {
        player->swing(ActorSwingSource::Interact, HandSlot::Mainhand);
    }

    return true;
}

bool NoteBlockInteraction::playNoteBlock(LocalPlayer* player, const BlockPos& pos, bool swingArm) {
    if (!player) return false;

    BlockSource& blockSource = player->getDimensionBlockSource();

    // 检查是否是音符盒
    if (!isNoteBlock(blockSource, pos)) return false;

    // 获取音符盒的音高
    int note = getNoteBlockPitch(blockSource, pos);
    if (note < 0) return false;

    if (!player->mGameMode || !player->mGameMode->mMessenger) return false;
    auto* messenger = player->mGameMode->mMessenger.get();
    messenger->sendStartDestroyBlock(pos, 0);
    messenger->sendStopDestroyBlock(pos, 0.0f);

    // 挥手
    if (swingArm) {
        player->swing(ActorSwingSource::Attack, HandSlot::Mainhand);
    }

    return true;
}

int NoteBlockInteraction::getNoteBlockPitch(BlockSource& blockSource, const BlockPos& pos) {
    if (!blockSource.hasChunk(ChunkPos(pos), false)) return -1;
    auto* blockActor = blockSource.getBlockEntity(pos);
    if (!blockActor || blockActor->getType() != BlockActorType::Music) return -1;

    if (const auto eventPitch = noteEventPitch(pos, 0)) return *eventPitch;
    auto* noteBlockActor = static_cast<const NoteBlockActor*>(blockActor);
    return noteBlockActor->mNote;
}

bool NoteBlockInteraction::isNoteBlock(BlockSource& blockSource, const BlockPos& pos) {
    if (!blockSource.hasChunk(ChunkPos(pos), false)) return false;
    const Block& block = blockSource.getBlock(pos);
    const auto& name = block.getTypeName();
    return name == "minecraft:noteblock" || name == "minecraft:note_block";
}

} // namespace notebot
