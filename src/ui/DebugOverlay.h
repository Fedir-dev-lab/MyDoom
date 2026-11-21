#pragma once

#include <cstdint>

#include "core/Vector3.h"

class Framebuffer;

// Frustum culling statistics for a single frame.
struct CullStats {
    // Objects (Mesh objects, physics bodies, sprites, enemies, pickups, projectiles, etc.)
    int objectsTotal   = 0;
    int objectsVisible = 0;
    int objectsCulled  = 0;

    // Sectors
    int sectorsTotal   = 0;
    int sectorsVisible = 0;
    int sectorsCulled  = 0;

    // Open World Chunks
    int chunksTotal    = 0;
    int chunksVisible  = 0;
    int chunksCulled   = 0;

    // Draw call counters (actual planes and walls submitted to rasterizer)
    int planesDrawn    = 0;
    int wallsDrawn     = 0;
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
