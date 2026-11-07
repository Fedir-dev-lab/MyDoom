#pragma once

#include <optional>
#include <vector>

#include "world/LevelData.h"
#include "world/LevelState.h"

class Player;
class Weapon;

// Owns static world geometry and exposes its collision/pickup queries.
class Map {
public:
    explicit Map(LevelData levelData);

    // Returns the original LevelData this map was built from.
    // Used by LevelState::Reset() to initialise interactive element states.
    const ::LevelData& GetLevelData() const { return m_levelData; }

    const std::vector<Sector>&      Sectors()      const { return m_sectors; }
    const std::vector<WorldSprite>& Sprites()      const { return m_sprites; }
    const std::vector<Pickup>&      Pickups()      const { return m_pickups; }
    const std::vector<PointLight>&  PointLights()  const { return m_pointLights; }
    const std::vector<ExitTrigger>& ExitTriggers() const { return m_exitTriggers; }
    bool HasMarsTerrain() const { return m_levelData.marsTerrain; }
    int  WallCount() const;
    bool IsPositionBlocked(const Vector3& position, float radius) const;
    bool IsLineBlocked(const Vector3& start, const Vector3& end) const;
    // Door-aware collision queries — check DoorDef geometry against LevelState.
    bool IsDoorPositionBlocked(const Vector3& position, float radius,
                               const LevelState& state) const;
    bool IsDoorLineBlocked(const Vector3& start, const Vector3& end,
                           const LevelState& state) const;
    void CollectPickups(Player& player, Weapon& weapon);
    // Marks any uncollected KeyPickupDef as collected when the player is within radius.
    // Returns the colour of the collected key if one was acquired.
    std::optional<KeyColour> CollectKeys(Player& player, LevelState& state) const;
    // Collect armour pickups; returns true if any were collected.
    bool CollectArmor(Player& player);
    // Collect audiologs; returns the first uncollected audiolog within radius,
    // marking it as collected. Returns nullptr if none in range.
    const AudiologDef* CollectAudiolog(Player& player);
    // Apply hazard damage-per-second to the player if inside any hazard zone.
    void ApplyHazards(Player& player, float deltaSeconds) const;
    void ResetPickups();

private:
    ::LevelData               m_levelData;  // kept for LevelState access
    std::vector<Sector>       m_sectors;
    std::vector<WorldSprite>  m_sprites;
    std::vector<Pickup>       m_pickups;
    std::vector<PointLight>   m_pointLights;
    std::vector<ExitTrigger>  m_exitTriggers;
    // v2 mutable pickup state (collected flag lives here, not in LevelData)
    std::vector<ArmorPickupDef> m_armorPickups;
    std::vector<AudiologDef>    m_audiologs;
};
