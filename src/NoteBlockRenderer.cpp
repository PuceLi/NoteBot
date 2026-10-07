#include "NoteBlockRenderer.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/memory/Hook.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/options/IOptionRegistry.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/RenderMaterialGroup.h"
#include "mc/client/renderer/SupplementaryFieldAutoGenerationMode.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/deps/renderer/Camera.h"
#include "mc/deps/core_graphics/enums/PrimitiveMode.h"
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"
#include "mc/deps/minecraft_renderer/renderer/MaterialPtr.h"
#include "mc/deps/minecraft_renderer/resources/ClientTexture.h"
#include "mc/deps/minecraft_renderer/resources/ServerTexture.h"
#include "mc/deps/minecraft_renderer/resources/OffscreenCaptureDescription.h"
#include "mc/deps/core/string/HashedString.h"
#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace notebot {

namespace {
struct CameraSnapshot {
    glm::mat4 viewProjection{1.0f};
    glm::vec3 worldPosition{};
    bool hasView = false;
    bool hasWorldPosition = false;
};

std::mutex cameraMutex;
CameraSnapshot cameraSnapshot;
struct WorldOutline {
    BlockPos pos;
    std::array<float, 4> sideColor;
    std::array<float, 4> lineColor;
};
struct WorldOverlayState {
    std::vector<WorldOutline> blocks;
    bool drawFaces = false;
    bool drawLines = true;
    float expansion = 0.01f;
    uint64_t revision = 0;
};
std::mutex outlineMutex;
WorldOverlayState worldOverlay;

bool isGameplayScreen() {
    auto clientRef = ll::service::getClientInstance();
    return clientRef.has_value() && clientRef.value().getLocalPlayer() &&
           clientRef.value().getScreenName().starts_with("hud_screen");
}

bool sameOverlay(const WorldOverlayState& lhs, const WorldOverlayState& rhs) {
    if (lhs.drawFaces != rhs.drawFaces || lhs.drawLines != rhs.drawLines ||
        lhs.expansion != rhs.expansion || lhs.blocks.size() != rhs.blocks.size()) return false;
    for (size_t index = 0; index < lhs.blocks.size(); ++index) {
        const auto& a = lhs.blocks[index];
        const auto& b = rhs.blocks[index];
        if (a.pos != b.pos || a.sideColor != b.sideColor || a.lineColor != b.lineColor) return false;
    }
    return true;
}

struct OverlayMeshCache {
    uint64_t revision = ~uint64_t{0};
    BlockPos origin{0, 0, 0};
    std::optional<mce::Mesh> faces;
    std::optional<mce::Mesh> lines;
    uint32_t faceVertices = 0;
    uint32_t lineVertices = 0;
};

void buildOverlayMesh(ScreenContext& screen, const WorldOverlayState& overlay, OverlayMeshCache& cache) {
    cache.faces.reset();
    cache.lines.reset();
    cache.faceVertices = cache.lineVertices = 0;
    cache.revision = overlay.revision;
    if (overlay.blocks.empty()) return;
    cache.origin = overlay.blocks.front().pos;
    const float expansion = std::clamp(overlay.expansion, 0.001f, 0.08f);
    const auto vertex = [&](Tessellator& batch, const BlockPos& pos, int corner) {
        const float x = float(pos.x - cache.origin.x) + ((corner & 1) ? 1.0f + expansion : -expansion);
        const float y = float(pos.y - cache.origin.y) + ((corner & 2) ? 1.0f + expansion : -expansion);
        const float z = float(pos.z - cache.origin.z) + ((corner & 4) ? 1.0f + expansion : -expansion);
        batch.vertex(x, y, z);
    };

    if (overlay.drawFaces && std::any_of(overlay.blocks.begin(), overlay.blocks.end(),
                                         [](const WorldOutline& block) { return block.sideColor[3] > 0.0f; })) {
        static constexpr std::array<std::array<int, 4>, 6> faces{{
            {{0, 2, 6, 4}}, {{1, 5, 7, 3}}, {{0, 4, 5, 1}},
            {{2, 3, 7, 6}}, {{0, 1, 3, 2}}, {{4, 6, 7, 5}}
        }};
        const std::unordered_set<BlockPos, BlockPosHash> positions = [&] {
            std::unordered_set<BlockPos, BlockPosHash> result;
            result.reserve(overlay.blocks.size());
            for (const auto& block : overlay.blocks) result.insert(block.pos);
            return result;
        }();
        Tessellator batch(screen.tessellator.mBufferResourceService);
        batch.begin({}, mce::PrimitiveMode::QuadList, static_cast<int>(overlay.blocks.size() * 24), false);
        for (const auto& block : overlay.blocks) {
            const auto& color = block.sideColor;
            if (color[3] <= 0.0f) continue;
            batch.color(color[0], color[1], color[2], color[3]);
            const auto& pos = block.pos;
            const std::array<BlockPos, 6> neighbors{{
                {pos.x - 1, pos.y, pos.z}, {pos.x + 1, pos.y, pos.z},
                {pos.x, pos.y - 1, pos.z}, {pos.x, pos.y + 1, pos.z},
                {pos.x, pos.y, pos.z - 1}, {pos.x, pos.y, pos.z + 1}
            }};
            for (size_t face = 0; face < faces.size(); ++face) {
                if (positions.contains(neighbors[face])) continue;
                for (auto corner = faces[face].rbegin(); corner != faces[face].rend(); ++corner) {
                    vertex(batch, pos, *corner);
                }
                cache.faceVertices += 4;
            }
        }
        if (cache.faceVertices > 0) {
            cache.faces.emplace(batch.end(Tessellator::UploadMode::Buffered, "NoteBot block faces",
                                          SupplementaryFieldAutoGenerationMode{}));
        }
    }

    if (overlay.drawLines && std::any_of(overlay.blocks.begin(), overlay.blocks.end(),
                                         [](const WorldOutline& block) { return block.lineColor[3] > 0.0f; })) {
        Tessellator batch(screen.tessellator.mBufferResourceService);
        batch.begin({}, mce::PrimitiveMode::LineList, static_cast<int>(overlay.blocks.size() * 24), false);
        for (const auto& block : overlay.blocks) {
            const auto& color = block.lineColor;
            if (color[3] <= 0.0f) continue;
            batch.color(color[0], color[1], color[2], color[3]);
            for (int corner = 0; corner < 8; ++corner) {
                for (int axis = 0; axis < 3; ++axis) {
                    if (corner & (1 << axis)) continue;
                    vertex(batch, block.pos, corner);
                    vertex(batch, block.pos, corner | (1 << axis));
                }
            }
        }
        cache.lineVertices = static_cast<uint32_t>(
            std::count_if(overlay.blocks.begin(), overlay.blocks.end(),
                          [](const WorldOutline& block) { return block.lineColor[3] > 0.0f; }) * 24);
        cache.lines.emplace(batch.end(Tessellator::UploadMode::Buffered, "NoteBot block outlines",
                                      SupplementaryFieldAutoGenerationMode{}));
    }
}

void renderWorldBoxes(BaseActorRenderContext& context) {
    if (!context.mImpl || !isGameplayScreen()) return;
    WorldOverlayState overlay;
    {
        std::lock_guard lock(outlineMutex);
        overlay = worldOverlay;
    }
    static OverlayMeshCache cache;
    if (overlay.blocks.empty()) {
        cache.faces.reset();
        cache.lines.reset();
        cache.revision = ~uint64_t{0};
        return;
    }

    ScreenContext& screen = context.mScreenContext;
    if (cache.revision != overlay.revision ||
        (cache.faces && !cache.faces->isValid()) || (cache.lines && !cache.lines->isValid())) {
        buildOverlayMesh(screen, overlay, cache);
    }

    auto clientRef = ll::service::getClientInstance();
    const bool fancy = clientRef.has_value() &&
                       clientRef.value().getOptions().getGraphicsMode() == GraphicsMode::Fancy;
    mce::MaterialPtr faceMaterial(fancy ? mce::RenderMaterialGroup::switchable()
                                        : mce::RenderMaterialGroup::common(),
                                  HashedString{fancy ? "holo_hand_pointer" : "lightning"});
    if (!faceMaterial.mRenderMaterialInfoPtr) {
        faceMaterial = mce::MaterialPtr(mce::RenderMaterialGroup::common(), HashedString{"lightning"});
    }
    mce::MaterialPtr lineMaterial(mce::RenderMaterialGroup::common(), HashedString{"debug"});

    const Vec3 camera = context.mImpl->mCameraPosition;
    auto ref = screen.camera.worldMatrixStack->push(false);
    ref.stack->_isDirty = true;
    constexpr float towardEye = 0.997f;
    const glm::vec3 offset{float(cache.origin.x) - camera.x,
                           float(cache.origin.y) - camera.y,
                           float(cache.origin.z) - camera.z};
    ref.mat->_m = glm::scale(glm::translate(ref.mat->_m.get(), offset * towardEye),
                             glm::vec3{towardEye});
    if (cache.faces && faceMaterial.mRenderMaterialInfoPtr) {
        cache.faces->renderMesh(screen, faceMaterial, gsl::span<mce::ClientTexture const*>{}, 0,
                                cache.faceVertices, OffscreenCaptureDescription{}, nullptr);
    }
    if (cache.lines && lineMaterial.mRenderMaterialInfoPtr) {
        cache.lines->renderMesh(screen, lineMaterial, gsl::span<mce::ClientTexture const*>{}, 0,
                                cache.lineVertices, OffscreenCaptureDescription{}, nullptr);
    }
    ref.stack->_isDirty = true;
    if (ref.stack->sortOrigin->has_value() &&
        (ref.stack->stack->size() - 1) <= ref.stack->sortOrigin->value()) {
        ref.stack->sortOrigin->reset();
    }
    ref.stack->stack->pop_back();
    ref.mat = nullptr;
    ref.stack = nullptr;
}

bool project(const glm::mat4& viewProjection, const glm::vec3& cameraWorld,
             const glm::vec3& world, ImVec2& screen) {
    const glm::vec4 clip = viewProjection * glm::vec4(world - cameraWorld, 1.0f);
    if (clip.w <= 0.05f || !std::isfinite(clip.w)) return false;
    const auto size = ImGui::GetIO().DisplaySize;
    screen.x = (clip.x / clip.w * 0.5f + 0.5f) * size.x;
    screen.y = (0.5f - clip.y / clip.w * 0.5f) * size.y;
    return std::isfinite(screen.x) && std::isfinite(screen.y);
}

bool projectEdge(glm::vec4 a, glm::vec4 b, ImVec2& from, ImVec2& to) {
    constexpr float nearW = 0.05f;
    if (a.w <= nearW && b.w <= nearW) return false;
    if (a.w <= nearW) a = glm::mix(a, b, (nearW - a.w) / (b.w - a.w));
    else if (b.w <= nearW) b = glm::mix(b, a, (nearW - b.w) / (a.w - b.w));
    const auto size = ImGui::GetIO().DisplaySize;
    from = ImVec2((a.x / a.w * 0.5f + 0.5f) * size.x, (0.5f - a.y / a.w * 0.5f) * size.y);
    to = ImVec2((b.x / b.w * 0.5f + 0.5f) * size.x, (0.5f - b.y / b.w * 0.5f) * size.y);
    return std::isfinite(from.x) && std::isfinite(from.y) &&
           std::isfinite(to.x) && std::isfinite(to.y);
}
}

LL_AUTO_TYPE_INSTANCE_HOOK(
    NoteBotCameraViewHook,
    ll::memory::HookPriority::Highest,
    LevelRendererPlayer,
    &LevelRendererPlayer::setupCamera,
    void,
    mce::Camera& camera,
    float alpha
) {
    origin(camera, alpha);
    if (camera.viewMatrixStack->stack->empty() || camera.projectionMatrixStack->stack->empty()) return;
    std::lock_guard lock(cameraMutex);
    cameraSnapshot.viewProjection = camera.getProjectionMatrix() * *camera.viewMatrixStack->top()._m;
    cameraSnapshot.hasView = true;
}

LL_AUTO_TYPE_INSTANCE_HOOK(
    NoteBotCameraPositionHook,
    ll::memory::HookPriority::Normal,
    LevelRendererPlayer,
    &LevelRendererPlayer::$renderEntityEffects,
    void,
    BaseActorRenderContext& context
) {
    origin(context);
    if (!context.mImpl) return;
    const Vec3 position = context.mImpl->mCameraPosition;
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) return;
    {
        std::lock_guard lock(cameraMutex);
        cameraSnapshot.worldPosition = glm::vec3(position.x, position.y, position.z);
        cameraSnapshot.hasWorldPosition = true;
    }
    renderWorldBoxes(context);
}

NoteBlockRenderer::NoteBlockRenderer() = default;
NoteBlockRenderer::~NoteBlockRenderer() = default;

void NoteBlockRenderer::render(const NoteBlockManager& manager, const NoteBotConfig& config, bool playing) {
    if (!isGameplayScreen()) {
        std::lock_guard lock(outlineMutex);
        if (!worldOverlay.blocks.empty()) {
            WorldOverlayState empty;
            empty.revision = worldOverlay.revision + 1;
            worldOverlay = std::move(empty);
        }
        return;
    }
    const auto snapshot = manager.getRenderSnapshot();
    const auto& noteBlocks = snapshot.noteBlocks;
    const auto& targetPitches = snapshot.targetPitches;

    WorldOverlayState overlay;
    overlay.drawFaces = config.renderBoxFaces;
    overlay.drawLines = config.renderBoxLines;
    overlay.expansion = config.boxExpansion;
    if (config.renderBoxes) {
        overlay.blocks.reserve(noteBlocks.size());
        for (const auto& block : noteBlocks) {
            const auto target = targetPitches.find(block.pos);
            if (target == targetPitches.end() && !config.showUnmappedBoxes) continue;
            const auto& sideColor = target == targetPitches.end() ? config.colors.scannedSide
                : block.isHit ? (playing ? config.colors.playingSide : config.colors.tuneHitSide)
                : block.pitch == target->second ? config.colors.tunedSide
                : config.colors.untunedSide;
            const auto& lineColor = target == targetPitches.end() ? config.colors.scannedLine
                : block.isHit ? (playing ? config.colors.playingLine : config.colors.tuneHitLine)
                : block.pitch == target->second ? config.colors.tunedLine
                : config.colors.untunedLine;
            overlay.blocks.push_back({block.pos, sideColor, lineColor});
        }
    }
    {
        std::lock_guard lock(outlineMutex);
        if (!sameOverlay(worldOverlay, overlay)) {
            overlay.revision = worldOverlay.revision + 1;
            worldOverlay = std::move(overlay);
        }
    }
    if (noteBlocks.empty() || !config.renderText) return;
    auto clientRef = ll::service::getClientInstance();
    if (!clientRef.has_value() || !clientRef.value().getLocalPlayer()) return;
    CameraSnapshot camera;
    {
        std::lock_guard lock(cameraMutex);
        camera = cameraSnapshot;
    }
    if (!camera.hasView || !camera.hasWorldPosition) return;

    for (const auto& block : noteBlocks) {
        const auto target = targetPitches.find(block.pos);
        if (target == targetPitches.end() && !config.showUnmappedBoxes) continue;
        if (config.renderText) {
            const int remaining = target != targetPitches.end()
                ? (target->second - block.pitch + 25) % 25 : 0;
            renderText(block.pos, block.pitch, remaining, config,
                       camera.viewProjection, camera.worldPosition);
        }
    }
}

void NoteBlockRenderer::renderBox(const BlockPos& pos, const float* sideColor, const float* lineColor,
                                  const glm::mat4& viewProjection, const glm::vec3& cameraWorld) {
    std::array<glm::vec4, 8> corners;
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 world(float(pos.x + (i & 1)), float(pos.y + ((i >> 1) & 1)),
                              float(pos.z + ((i >> 2) & 1)));
        corners[i] = viewProjection * glm::vec4(world - cameraWorld, 1.0f);
    }

    auto* draw = ImGui::GetBackgroundDrawList();
    const ImU32 fill = ImGui::ColorConvertFloat4ToU32(
        ImVec4(sideColor[0], sideColor[1], sideColor[2], sideColor[3]));
    const ImU32 line = ImGui::ColorConvertFloat4ToU32(
        ImVec4(lineColor[0], lineColor[1], lineColor[2], lineColor[3]));
    if (corners[2].w > 0.05f && corners[3].w > 0.05f &&
        corners[7].w > 0.05f && corners[6].w > 0.05f) {
        const auto size = ImGui::GetIO().DisplaySize;
        auto screen = [size](const glm::vec4& clip) {
            return ImVec2((clip.x / clip.w * 0.5f + 0.5f) * size.x,
                          (0.5f - clip.y / clip.w * 0.5f) * size.y);
        };
        draw->AddQuadFilled(screen(corners[2]), screen(corners[3]),
                            screen(corners[7]), screen(corners[6]), fill);
    }
    for (int i = 0; i < 8; ++i) {
        for (int axis = 0; axis < 3; ++axis) {
            if ((i & (1 << axis)) == 0) {
                ImVec2 from, to;
                if (projectEdge(corners[i], corners[i | (1 << axis)], from, to)) {
                    draw->AddLine(from, to, line, 2.0f);
                }
            }
        }
    }
}

void NoteBlockRenderer::renderText(const BlockPos& pos, int pitch, int remainingHits,
                                   const NoteBotConfig& config,
                                   const glm::mat4& viewProjection, const glm::vec3& cameraWorld) {
    ImVec2 screen;
    if (!project(viewProjection, cameraWorld, glm::vec3(float(pos.x) + 0.5f,
                                            float(pos.y) + std::clamp(config.noteTextHeight, 0.5f, 2.5f),
                                            float(pos.z) + 0.5f), screen)) return;
    const float size = ImGui::GetFontSize() * std::clamp(config.noteTextScale, 0.5f, 3.0f);
    const std::string levelText = std::to_string(pitch);
    const std::string hitsText = remainingHits > 0 ? " -" + std::to_string(remainingHits) : "";
    const float levelWidth = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, levelText.c_str()).x;
    const float hitsWidth = hitsText.empty() ? 0.0f
        : ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, hitsText.c_str()).x;
    screen.x -= (levelWidth + hitsWidth) * 0.5f;
    auto* draw = ImGui::GetBackgroundDrawList();
    const auto& pitchColor = config.pitchTextColor;
    const auto& remainingColor = config.remainingTextColor;
    const ImU32 pitchTint = ImGui::ColorConvertFloat4ToU32(
        ImVec4(pitchColor[0], pitchColor[1], pitchColor[2], pitchColor[3]));
    const ImU32 remainingTint = ImGui::ColorConvertFloat4ToU32(
        ImVec4(remainingColor[0], remainingColor[1], remainingColor[2], remainingColor[3]));
    draw->AddText(ImGui::GetFont(), size, screen, pitchTint, levelText.c_str());
    if (!hitsText.empty()) {
        screen.x += levelWidth;
        draw->AddText(ImGui::GetFont(), size, screen, remainingTint, hitsText.c_str());
    }
}

const char* NoteBlockRenderer::getNoteNameFromPitch(int pitch) {
    static const char* noteNames[] = {
        "F#3", "G3", "G#3", "A3", "A#3", "B3",
        "C4", "C#4", "D4", "D#4", "E4", "F4",
        "F#4", "G4", "G#4", "A4", "A#4", "B4",
        "C5", "C#5", "D5", "D#5", "E5", "F5",
        "F#5"
    };

    if (pitch >= 0 && pitch < 25) {
        return noteNames[pitch];
    }
    return "?";
}

} // namespace notebot
