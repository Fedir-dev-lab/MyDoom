#pragma once

#include <cstdint>

#include "core/Vector3.h"

class Framebuffer;

// Frustum culling statistics for a single frame.
struct CullStats {
    int total   = 0;
    int visible = 0;
    int culled  = 0;
};

class DebugOverlay {
public:
    void Draw(Framebuffer& framebuffer, float framesPerSecond,
              const Vector3& playerPosition, float playerAngleRadians,
              int health, int sectorCount, int wallCount,
              const CullStats& cullStats = {}) const;
    void DrawRestartPrompt(Framebuffer& framebuffer) const;
    void DrawLevelComplete(Framebuffer& framebuffer) const;

private:
    void DrawText(Framebuffer& framebuffer, int x, int y,
                  const char* text, uint32_t color) const;
    void DrawCharacter(Framebuffer& framebuffer, int x, int y,
                       char character, uint32_t color) const;
};
