#include "Player.h"

#include <algorithm>
#include <cmath>

#include "world/Map.h"
#include "world/LevelState.h"
#include "renderer/MarsTerrain.h"

namespace {

constexpr float kMoveSpeed = 3.0f;
// How many radians the camera turns/pitches per pixel of mouse movement.
constexpr float kMouseSensitivity = 0.002f;
// Maximum pitch angle (radians). Keeps the horizon well within the screen.
constexpr float kMaxPitch = 1.0f;
constexpr float kPlayerRadius = 0.3f;
constexpr float kMaxMoveStep = 0.05f;

// Head-bob: phase advances at this rate (radians per world unit travelled).
// Two full sine cycles per ~4 units = one bob per ~2 units ≈ one step.
constexpr float kBobFrequency = 3.2f;
constexpr float kBobAmplitudeY     = 0.045f;  // world-unit vertical swing
constexpr float kBobAmplitudePitch = 0.018f;  // radian tilt swing
// Smoothing factor: how fast the bob decays to zero when standing still.
constexpr float kBobDecay = 12.0f;

} // namespace

void Player::Update(const PlayerInput& input, float deltaSeconds, const Map& map,
                    const LevelState& levelState, bool enableBob) {
    if (!IsAlive()) {
        return;
    }

    SnapToTerrain(map);

    m_angle += input.mouseDeltaX * kMouseSensitivity;
    m_pitch += input.mouseDeltaY * kMouseSensitivity;
    if (m_pitch > kMaxPitch) { m_pitch = kMaxPitch; }
    if (m_pitch < -kMaxPitch) { m_pitch = -kMaxPitch; }

    const Vector3 forward{std::sin(m_angle), 0.0f, std::cos(m_angle)};
    const Vector3 right{std::cos(m_angle), 0.0f, -std::sin(m_angle)};
    Vector3 movement;

    if (input.moveForward) {
        movement += forward;
    }
    if (input.moveBackward) {
        movement -= forward;
    }
    if (input.moveRight) {
        movement += right;
    }
    if (input.moveLeft) {
        movement -= right;
    }

    const bool isMoving = movement.LengthSquared() > 0.0f;

    if (isMoving) {
        const Vector3 displacement = movement.Normalized() * (kMoveSpeed * deltaSeconds);
        const int stepCount = static_cast<int>(std::ceil(displacement.Length() / kMaxMoveStep));
        const Vector3 step = displacement * (1.0f / static_cast<float>(stepCount));

        for (int i = 0; i < stepCount; ++i) {
            Vector3 moveAlongX = m_position;
            moveAlongX.x += step.x;
            if (!map.IsPositionBlocked(moveAlongX, kPlayerRadius) &&
                !map.IsDoorPositionBlocked(moveAlongX, kPlayerRadius, levelState)) {
                m_position.x = moveAlongX.x;
            }

            Vector3 moveAlongZ = m_position;
            moveAlongZ.z += step.z;
            if (!map.IsPositionBlocked(moveAlongZ, kPlayerRadius) &&
                !map.IsDoorPositionBlocked(moveAlongZ, kPlayerRadius, levelState)) {
                m_position.z = moveAlongZ.z;
            }
        }

        // Advance bob phase proportional to distance actually requested to move.
        const float distanceMoved = displacement.Length();
        if (enableBob) {
            m_bobPhase += distanceMoved * kBobFrequency;
            m_bobOffsetY     = std::sin(m_bobPhase) * kBobAmplitudeY;
            m_bobOffsetPitch = std::sin(m_bobPhase * 0.5f) * kBobAmplitudePitch;
        } else {
            m_bobOffsetY     = 0.0f;
            m_bobOffsetPitch = 0.0f;
        }
    } else {
        if (enableBob) {
            // Smoothly decay bob offsets back to zero while standing still.
            const float decay = std::exp(-kBobDecay * deltaSeconds);
            m_bobOffsetY     *= decay;
            m_bobOffsetPitch *= decay;
        } else {
            m_bobOffsetY     = 0.0f;
            m_bobOffsetPitch = 0.0f;
        }
    }

    if (m_damageFlash > 0.0f) {
        m_damageFlash = std::max(0.0f, m_damageFlash - deltaSeconds);
    }
    SnapToTerrain(map);
}

void Player::SnapToTerrain(const Map& map) {
    if (map.HasMarsTerrain()) {
        // m_position is the base camera position, not the player's feet.
        m_position.y = MarsTerrain::HeightAt(m_position.x, m_position.z) + kEyeHeight;
    }
}

void Player::TakeDamage(int damage) {
    if (damage <= 0) { return; }
    // Armour absorbs 1/3 of each hit; the rest goes to health.
    // (Classic DOOM formula: armour factor 0.333)
    if (m_armor > 0) {
        const int armorAbsorb = std::min(m_armor, damage / 3 + 1);
        m_armor  = std::max(0, m_armor - armorAbsorb);
        damage   = std::max(1, damage - armorAbsorb);
    }
    m_health = std::max(0, m_health - damage);
    m_damageFlash = 0.35f;
}

void Player::AddArmor(int amount) {
    m_armor = std::min(kMaximumArmor, m_armor + amount);
}

void Player::Heal(int amount) {
    m_health = std::min(kMaximumHealth, m_health + amount);
}

void Player::LoadState(const Vector3& pos, float angle, float pitch, int health,
                       int armor) {
    m_position        = pos;
    m_angle           = angle;
    m_pitch           = pitch;
    m_health          = std::clamp(health, 0, kMaximumHealth);
    m_armor           = std::clamp(armor,  0, kMaximumArmor);
    m_damageFlash     = 0.0f;
    m_bobPhase        = 0.0f;
    m_bobOffsetY      = 0.0f;
    m_bobOffsetPitch  = 0.0f;
}

void Player::Reset() {
    m_position = {0.0f, kEyeHeight, 0.0f};
    m_angle = 0.0f;
    m_pitch = 0.0f;
    m_damageFlash = 0.0f;
    m_bobPhase = 0.0f;
    m_bobOffsetY = 0.0f;
    m_bobOffsetPitch = 0.0f;
    m_health = kMaximumHealth;
    m_armor  = 0;
}
