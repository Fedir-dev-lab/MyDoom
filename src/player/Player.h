#pragma once

#include <cmath>

#include "core/Vector3.h"

class Map;
class LevelState;

struct PlayerInput {
    bool moveForward = false;
    bool moveBackward = false;
    bool moveLeft = false;
    bool moveRight = false;
    float mouseDeltaX = 0.0f; // pixels moved since last frame; positive = turn right
    float mouseDeltaY = 0.0f; // pixels moved since last frame; positive = look down
};

// Holds the camera state used by the renderer. Collision is intentionally not
// part of this first movement step; it will be added once walls belong to a map.
class Player {
public:
    static constexpr int kMaximumHealth = 100;
    static constexpr int kMaximumArmor  = 100; // scenario.md: standard vest +50, cap at 100

    // Base height of the player's eyes above the floor.
    static constexpr float kEyeHeight = 1.0f;

    Player() = default;

    // Raw position on the X/Z plane (Y = kEyeHeight always).
    const Vector3& Position() const { return m_position; }

    // Camera position with head-bob vertical offset applied.
    Vector3 EyePosition() const {
        return {m_position.x, m_position.y + m_bobOffsetY, m_position.z};
    }

    float Angle() const { return m_angle; }

    // Pitch with a subtle head-bob tilt layered on top.
    float Pitch() const { return m_pitch + m_bobOffsetPitch; }

    int Health() const { return m_health; }
    int Armor()  const { return m_armor; }
    bool IsAlive() const { return m_health > 0; }
    float DamageFlash() const { return m_damageFlash; }

    void Update(const PlayerInput& input, float deltaSeconds, const Map& map,
                const LevelState& levelState, bool enableBob = true);
    void TakeDamage(int damage);
    void Heal(int amount);
    void AddArmor(int amount);
    void Reset();
    // Align the player's eye base with a procedural terrain surface.
    void SnapToTerrain(const Map& map);

    // Restore full state from a save. Used by SaveSystem.
    void LoadState(const Vector3& pos, float angle, float pitch, int health,
                   int armor = 0);

private:
    Vector3 m_position{0.0f, kEyeHeight, 0.0f};
    float m_angle = 0.0f;
    // Pitch in radians: positive = look down, negative = look up.
    // Clamped to ±kMaxPitch so the horizon never flips past the screen edge.
    float m_pitch = 0.0f;
    int m_health = kMaximumHealth;
    int m_armor  = 0;
    float m_damageFlash = 0.0f;

    // Head-bob state — accumulated phase and current offsets.
    float m_bobPhase = 0.0f;    // advances while moving
    float m_bobOffsetY = 0.0f;  // vertical camera shift (world units)
    float m_bobOffsetPitch = 0.0f; // tiny pitch tilt (radians)
};
