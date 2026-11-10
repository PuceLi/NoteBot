#include "NoteBlockInteraction.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/actor/Actor.h"
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
std::mutex renderRotationMutex;
LocalPlayer* renderRotationPlayer = nullptr;
Vec2 renderRotation{};
std::chrono::steady_clock::time_point renderRotationExpires{};
std::chrono::steady_clock::time_point lastRotationUpdate{};
float smoothedBodyYaw = 0.0f;
float smoothedHeadYaw = 0.0f;
bool hasSmoothedRotation = false;

float wrapYaw(float yaw) {
    while (yaw <= -180.0f) yaw += 360.0f;
    while (yaw > 180.0f) yaw -= 360.0f;
    return yaw;
}

float approachYaw(float current, float target, float maxStep) {
    const float difference = wrapYaw(target - current);
    if (std::abs(difference) <= maxStep) return wrapYaw(target);
    return wrapYaw(current + std::copysign(maxStep, difference));
}

void applyMobYaw(LocalPlayer& player, float bodyYaw, float headYaw) {
    // Update only the live values. The paired setters also overwrite Bedrock's
    // previous-frame values and disable the renderer's normal interpolation.
    player.setYBodyRotation(bodyYaw);
    player.setYHeadRot(headYaw);
}
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

void NoteBlockInteraction::setRenderRotation(LocalPlayer* player, const Vec2& rotation) {
    if (!player) return;
    std::lock_guard lock(renderRotationMutex);
    if (renderRotationPlayer != player) {
        hasSmoothedRotation = false;
    }
    renderRotationPlayer = player;
    renderRotation = rotation;
    // Keep a note's visible turn for five game ticks at 20 TPS, even when
    // playback advances every tick or several notes share one tick.
    renderRotationExpires = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
}

void NoteBlockInteraction::updateAutoRotation(LocalPlayer* player) {
    std::lock_guard lock(renderRotationMutex);
    if (!renderRotationPlayer) return;
    if (player != renderRotationPlayer) {
        renderRotationPlayer = nullptr;
        hasSmoothedRotation = false;
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    const float nativeBodyYaw = player->getYBodyRotation();
    const float nativeHeadYaw = player->getYHeadRot();
    if (!hasSmoothedRotation) {
        smoothedBodyYaw = nativeBodyYaw;
        smoothedHeadYaw = nativeHeadYaw;
        lastRotationUpdate = now;
        hasSmoothedRotation = true;
    }

    const float elapsed = std::clamp(
        std::chrono::duration<float>(now - lastRotationUpdate).count(), 0.0f, 0.1f);
    lastRotationUpdate = now;

    float bodyTarget = renderRotation.y;
    float headTarget = renderRotation.y;
    if (now >= renderRotationExpires) {
        bodyTarget = nativeBodyYaw;
        headTarget = nativeHeadYaw;
    }

    // Limit angular speed and always take the shortest path, so rapid changes
    // between note blocks cannot snap the model back and forth.
    smoothedBodyYaw = approachYaw(smoothedBodyYaw, bodyTarget, 360.0f * elapsed);
    smoothedHeadYaw = approachYaw(smoothedHeadYaw, headTarget, 540.0f * elapsed);
    if (now >= renderRotationExpires &&
        std::abs(wrapYaw(nativeBodyYaw - smoothedBodyYaw)) < 1.0f &&
        std::abs(wrapYaw(nativeHeadYaw - smoothedHeadYaw)) < 1.0f) {
        // The input tick already installed the live camera angles; relinquish
        // control only after the smoothed model has caught up with them.
        renderRotationPlayer = nullptr;
        hasSmoothedRotation = false;
        return;
    }
    applyMobYaw(*player, smoothedBodyYaw, smoothedHeadYaw);
}

void NoteBlockInteraction::clearRenderRotation() {
    std::lock_guard lock(renderRotationMutex);
    renderRotationPlayer = nullptr;
    renderRotationExpires = {};
    hasSmoothedRotation = false;
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
