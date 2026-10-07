#pragma once

#include "NoteBlockManager.h"
#include "Config.h"
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace notebot {

class NoteBlockRenderer {
public:
    NoteBlockRenderer();
    ~NoteBlockRenderer();

    void render(const NoteBlockManager& manager, const NoteBotConfig& config, bool playing);

private:
    void renderBox(const BlockPos& pos, const float* sideColor, const float* lineColor,
                   const glm::mat4& viewProjection, const glm::vec3& cameraWorld);
    void renderText(const BlockPos& pos, int pitch, int remainingHits, const NoteBotConfig& config,
                    const glm::mat4& viewProjection, const glm::vec3& cameraWorld);

    const char* getNoteNameFromPitch(int pitch);
};

} // namespace notebot
