#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "core/Vector2.h"
#include "core/Vector3.h"

class Framebuffer;

// ---------------------------------------------------------------------------
// Phone — a phone UI opened with F key.
//
// Animation: the phone slides in from below when opened and slides out when
// closed. m_animProgress goes 0→1 (opening) or 1→0 (closing).
// The phone body is drawn centered horizontally, positioned vertically based
// on the animation so at progress=0 it is fully off the bottom of the screen.
//
// Screen contents:
//   - Digital clock (in-game time driven by sunWorldAngle)
//   - Mini-map showing sector walls and player position/direction
// ---------------------------------------------------------------------------
class Phone {
public:
    // Call every frame whether the phone is open or not (drives animation).
    // deltaSeconds — frame time.
    void Update(float deltaSeconds);

    // Toggle open/close.
    void Toggle();

    bool IsOpen()    const { return m_open; }
    bool IsVisible() const { return m_animProgress > 0.001f; }

    // Draw the phone onto the framebuffer.
    // sunWorldAngle — used to derive in-game hours/minutes.
    // playerPos / playerAngle — for the mini-map dot and direction arrow.
    // walls — flat list of wall start/end pairs for the mini-map.
    void Draw(Framebuffer& framebuffer,
              float sunWorldAngle,
              const Vector3& playerPos, float playerAngle,
              const std::vector<Vector2>& mapVerticesMin,
              const std::vector<Vector2>& mapVerticesMax,
              const std::vector<std::pair<Vector2, Vector2>>& walls) const;

private:
    // ---- pixel-font helpers (same 3×5 bitmap style as DebugOverlay) ------
    void DrawChar(Framebuffer& fb, int x, int y,
                  char c, uint32_t color, int scale) const;
    void DrawText(Framebuffer& fb, int x, int y,
                  const char* text, uint32_t color, int scale) const;

    // ---- sub-sections of Draw --------------------------------------------
    void DrawClock(Framebuffer& fb, int phoneX, int phoneY,
                   int phoneW, float sunWorldAngle) const;
    void DrawMiniMap(Framebuffer& fb, int phoneX, int phoneY,
                     int phoneW, int phoneH,
                     const Vector3& playerPos, float playerAngle,
                     const std::vector<std::pair<Vector2, Vector2>>& walls) const;

    bool  m_open          = false;
    float m_animProgress  = 0.0f;   // 0 = hidden, 1 = fully visible
    // Animation speed: full open/close in 0.18 seconds.
    static constexpr float kAnimSpeed = 1.0f / 0.18f;
};
