#pragma once

// ---------------------------------------------------------------------------
// DevMode — developer flight / noclip mode.
//
// Compiled in ONLY when CMake option DEV_MODE=ON is set.
// The entire feature is stripped from release builds automatically.
//
// Usage in the editor or test session:
//   F1            — toggle noclip / flight on/off
//   [ / ]         — decrease / increase fly speed (0.5x / 2x step)
//   W A S D       — move horizontally (same as normal)
//   Space / LShift — fly up / fly down
//   Mouse         — look around (same as normal)
//   F2            — teleport to level spawn point
//
// The mode is intentionally NOT saved to SaveData so it never leaks into
// release save files.
// ---------------------------------------------------------------------------

#ifdef DEV_MODE

#include <algorithm>
#include <cmath>
#include <string>

#include "core/Vector3.h"
#include "player/Player.h"

struct DevModeState {
    bool  active        = false;   // noclip/flight on?
    float speedMultiplier = 1.0f;  // 0.25 … 16.0 in power-of-two steps
    float posY          = 1.0f;    // vertical position while flying

    static constexpr float kMinSpeed =  0.25f;
    static constexpr float kMaxSpeed = 16.0f;
    static constexpr float kFlySpeed =  6.0f;  // world-units/second at ×1

    void SpeedDown() {
        speedMultiplier = std::max(kMinSpeed, speedMultiplier * 0.5f);
    }
    void SpeedUp() {
        speedMultiplier = std::min(kMaxSpeed, speedMultiplier * 2.0f);
    }

    // Returns a human-readable status line for the HUD.
    std::string StatusLine() const {
        if (!active) { return "[DEV] NOCLIP OFF"; }
        return "[DEV] NOCLIP  SPD x" + SpeedLabel();
    }

private:
    std::string SpeedLabel() const {
        if (speedMultiplier < 0.5f)  return "0.25";
        if (speedMultiplier < 0.75f) return "0.5";
        if (speedMultiplier < 1.5f)  return "1";
        if (speedMultiplier < 3.0f)  return "2";
        if (speedMultiplier < 6.0f)  return "4";
        if (speedMultiplier < 12.0f) return "8";
        return "16";
    }
};

// ---------------------------------------------------------------------------
// DevMode::Update — called INSTEAD of player.Update when noclip is active.
// When noclip is off this function is not called at all — the normal
// player.Update handles everything.
// ---------------------------------------------------------------------------
inline void DevModeUpdate(DevModeState& dev, Player& player,
                          bool fwd, bool back, bool left, bool right,
                          bool up, bool down,
                          float mdx, float mdy,
                          float deltaSeconds,
                          float mouseSensitivity = 0.002f) {
    // --- look ---
    const float angle = player.Angle() + mdx * mouseSensitivity;
    const float pitch = std::clamp(player.Pitch() + mdy * mouseSensitivity,
                                   -1.0f, 1.0f);

    // --- move ---
    const float speed = DevModeState::kFlySpeed * dev.speedMultiplier * deltaSeconds;
    const float cosA  = std::cos(angle);
    const float sinA  = std::sin(angle);

    Vector3 pos = player.Position();
    pos.y = dev.posY; // use our tracked Y, not terrain-snapped Y

    Vector3 move{0.f, 0.f, 0.f};
    if (fwd)   move = move + Vector3{ sinA, 0.f,  cosA};
    if (back)  move = move + Vector3{-sinA, 0.f, -cosA};
    if (right) move = move + Vector3{ cosA, 0.f, -sinA};
    if (left)  move = move + Vector3{-cosA, 0.f,  sinA};
    if (move.LengthSquared() > 0.f) {
        move = move.Normalized() * speed;
    }
    if (up)   move.y += speed;
    if (down) move.y -= speed;

    pos = pos + move;
    dev.posY = pos.y;

    // Write back through LoadState so we bypass collision and terrain snap.
    player.LoadState(pos, angle, pitch, player.Health(), player.Armor());
}

#endif // DEV_MODE
