#pragma once

#include <cstdint>

#include "core/Vector3.h"

class Framebuffer;

class DebugOverlay {
public:
    void Draw(Framebuffer& framebuffer, float framesPerSecond,
              const Vector3& playerPosition, float playerAngleRadians,
              int health, int sectorCount, int wallCount) const;
    void DrawRestartPrompt(Framebuffer& framebuffer) const;
    void DrawLevelComplete(Framebuffer& framebuffer) const;

private:
    void DrawText(Framebuffer& framebuffer, int x, int y,
                  const char* text, uint32_t color) const;
    void DrawCharacter(Framebuffer& framebuffer, int x, int y,
                       char character, uint32_t color) const;
};
