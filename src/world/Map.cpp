#include "Map.h"

#include <algorithm>
#include <utility>

#include "player/Player.h"
#include "weapons/Weapon.h"

namespace {

float Cross2D(float firstX, float firstZ, float secondX, float secondZ) {
    return firstX * secondZ - firstZ * secondX;
}

} // namespace

Map::Map(LevelData levelData)
    : m_levelData(levelData),                        // keep a copy for LevelState
      m_sectors(std::move(levelData.sectors)),
      m_sprites(std::move(levelData.sprites)),
      m_pickups(std::move(levelData.pickups)),
      m_pointLights(std::move(levelData.pointLights)),
      m_exitTriggers(std::move(levelData.exitTriggers)),
      m_meshObjects(std::move(levelData.meshObjects)),
      m_armorPickups(m_levelData.armorPickups),      // mutable copy for collected state
      m_audiologs(m_levelData.audiologs) {            // mutable copy for collected state
    for (Sector& sector : m_sectors) {
        sector.ComputeBounds();
    }
}

int Map::WallCount() const {
    int wallCount = 0;
    for (const Sector& sector : m_sectors) {
        wallCount += static_cast<int>(sector.walls.size());
    }
    return wallCount;
}

bool Map::IsPositionBlocked(const Vector3& position, float radius) const {
    const float radiusSquared = radius * radius;

    for (const Sector& sector : m_sectors) {
        for (const Wall& wall : sector.walls) {
            if (!wall.blocksMovement) {
                continue;
            }

            const float wallX = wall.end.x - wall.start.x;
            const float wallZ = wall.end.z - wall.start.z;
            const float wallLengthSquared = wallX * wallX + wallZ * wallZ;

            const float fromStartX = position.x - wall.start.x;
            const float fromStartZ = position.z - wall.start.z;
            const float projection = (fromStartX * wallX + fromStartZ * wallZ) / wallLengthSquared;
            const float t = std::clamp(projection, 0.0f, 1.0f);

            const float closestX = wall.start.x + wallX * t;
            const float closestZ = wall.start.z + wallZ * t;
            const float distanceX = position.x - closestX;
            const float distanceZ = position.z - closestZ;

            if (distanceX * distanceX + distanceZ * distanceZ < radiusSquared) {
                return true;
            }
        }
    }

    return false;
}

bool Map::IsLineBlocked(const Vector3& start, const Vector3& end) const {
    const float rayX = end.x - start.x;
    const float rayZ = end.z - start.z;

    for (const Sector& sector : m_sectors) {
        for (const Wall& wall : sector.walls) {
            if (!wall.blocksMovement) {
                continue;
            }

            const float wallX = wall.end.x - wall.start.x;
            const float wallZ = wall.end.z - wall.start.z;
            const float denominator = Cross2D(rayX, rayZ, wallX, wallZ);
            if (std::abs(denominator) < 1e-6f) {
                continue;
            }

            const float fromStartX = wall.start.x - start.x;
            const float fromStartZ = wall.start.z - start.z;
            const float rayT = Cross2D(fromStartX, fromStartZ, wallX, wallZ) / denominator;
            const float wallT = Cross2D(fromStartX, fromStartZ, rayX, rayZ) / denominator;

            if (rayT > 0.0f && rayT < 1.0f && wallT >= 0.0f && wallT <= 1.0f) {
                return true;
            }
        }
    }

    return false;
}

void Map::CollectPickups(Player& player, Weapon& weapon) {
    constexpr float kPickupRadius = 0.8f;

    if (!player.IsAlive()) {
        return;
    }

    for (Pickup& pickup : m_pickups) {
        if (pickup.collected) {
            continue;
        }

        Vector3 toPickup = pickup.position - player.Position();
        toPickup.y = 0.0f;
        if (toPickup.LengthSquared() <= kPickupRadius * kPickupRadius) {
            player.Heal(pickup.healthAmount);
            weapon.AddReserveAmmo(pickup.ammoAmount);
            pickup.collected = true;
        }
    }
}

void Map::ResetPickups() {
    for (Pickup& pickup : m_pickups) {
        pickup.collected = false;
    }
    // Reset v2 pickups too.
    for (ArmorPickupDef& a : m_armorPickups) { a.collected = false; }
    for (AudiologDef& al : m_audiologs)      { al.audioId = al.audioId; } // ids unchanged
    // Audiolog "collected" state lives in LevelState (SaveData); Map just holds
    // the definitions.  Reset is done via LevelState::Reset().
}

bool Map::CollectArmor(Player& player) {
    constexpr float kArmorRadius = 0.8f;
    bool any = false;
    if (!player.IsAlive()) { return false; }
    for (ArmorPickupDef& a : m_armorPickups) {
        if (a.collected) { continue; }
        const float dx = player.Position().x - a.position.x;
        const float dz = player.Position().z - a.position.z;
        if (dx * dx + dz * dz <= kArmorRadius * kArmorRadius) {
            player.AddArmor(a.armorAmount);
            a.collected = true;
            any = true;
        }
    }
    return any;
}

const AudiologDef* Map::CollectAudiolog(Player& player) {
    constexpr float kAudioRadius = 0.9f;
    if (!player.IsAlive()) { return nullptr; }
    for (AudiologDef& al : m_audiologs) {
        if (al.audioId < 0) { continue; } // negative = already collected (sentinel)
        const float dx = player.Position().x - al.position.x;
        const float dz = player.Position().z - al.position.z;
        if (dx * dx + dz * dz <= kAudioRadius * kAudioRadius) {
            al.audioId = -1; // mark collected
            return &al;
        }
    }
    return nullptr;
}

void Map::ApplyHazards(Player& player, float deltaSeconds) const {
    if (!player.IsAlive()) { return; }
    const float px = player.Position().x;
    const float pz = player.Position().z;
    for (const HazardSurface& h : m_levelData.hazards) {
        if (px >= h.minX && px <= h.maxX && pz >= h.minZ && pz <= h.maxZ) {
            player.TakeDamage(static_cast<int>(h.damagePerSecond * deltaSeconds + 0.5f));
        }
    }
}

std::optional<KeyColour> Map::CollectKeys(Player& player, LevelState& state) const {
    constexpr float kKeyRadius = 0.7f;
    const Vector3& pos = player.Position();
    for (const KeyPickupDef& def : m_levelData.keys) {
        KeyState* ks = state.FindKey(def.keyId);
        if (!ks || ks->collected) { continue; }
        const float dx = pos.x - def.position.x;
        const float dz = pos.z - def.position.z;
        if (dx * dx + dz * dz <= kKeyRadius * kKeyRadius) {
            ks->collected = true;
            return def.colour;
        }
    }
    return std::nullopt;
}

// Returns true if a door should currently block movement.
// A door is solid when Closed, or when its openFraction is below 0.5
// (i.e. still more than half closed during Opening/Closing animation).
static bool IsDoorSolid(const DoorState& state) {
    return state.phase == DoorPhase::Closed ||
           ((state.phase == DoorPhase::Opening || state.phase == DoorPhase::Closing) &&
            state.openFraction < 0.5f);
}

bool Map::IsDoorPositionBlocked(const Vector3& position, float radius,
                                const LevelState& state) const {
    const float radiusSquared = radius * radius;

    for (const DoorDef& door : m_levelData.doors) {
        const DoorState* doorState = state.FindDoor(door.doorId);
        if (!doorState || !IsDoorSolid(*doorState)) { continue; }

        // Same segment-distance test as IsPositionBlocked.
        const float wallX = door.wallEnd.x - door.wallStart.x;
        const float wallZ = door.wallEnd.z - door.wallStart.z;
        const float wallLenSq = wallX * wallX + wallZ * wallZ;
        if (wallLenSq < 1e-9f) { continue; }

        const float fromX = position.x - door.wallStart.x;
        const float fromZ = position.z - door.wallStart.z;
        const float t = std::clamp((fromX * wallX + fromZ * wallZ) / wallLenSq, 0.0f, 1.0f);
        const float dx = position.x - (door.wallStart.x + wallX * t);
        const float dz = position.z - (door.wallStart.z + wallZ * t);
        if (dx * dx + dz * dz < radiusSquared) { return true; }
    }
    return false;
}

bool Map::IsDoorLineBlocked(const Vector3& start, const Vector3& end,
                             const LevelState& state) const {
    const float rayX = end.x - start.x;
    const float rayZ = end.z - start.z;

    for (const DoorDef& door : m_levelData.doors) {
        const DoorState* doorState = state.FindDoor(door.doorId);
        if (!doorState || !IsDoorSolid(*doorState)) { continue; }

        const float wallX = door.wallEnd.x - door.wallStart.x;
        const float wallZ = door.wallEnd.z - door.wallStart.z;
        const float denom = rayX * wallZ - rayZ * wallX; // Cross2D(ray, wall)
        if (std::abs(denom) < 1e-6f) { continue; }

        const float fromX = door.wallStart.x - start.x;
        const float fromZ = door.wallStart.z - start.z;
        const float rayT  = (fromX * wallZ - fromZ * wallX) / denom;
        const float wallT = (fromX * rayZ  - fromZ * rayX)  / denom;

        if (rayT > 0.0f && rayT < 1.0f && wallT >= 0.0f && wallT <= 1.0f) {
            return true;
        }
    }
    return false;
}
