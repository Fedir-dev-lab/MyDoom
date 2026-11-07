#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/Vector2.h"
#include "core/Vector3.h"
#include "renderer/Lighting.h"

// Static data that describes one playable level. It deliberately has no game
// logic: Map consumes it for collision/render queries, while LevelLoader
// populates the same structure from a level file.
struct Wall {
    Vector3 start;
    Vector3 end;
    std::optional<float> bottomHeight;
    std::optional<float> topHeight;
    bool blocksMovement = true;

    Wall(Vector3 start_, Vector3 end_)
        : start(start_), end(end_) {
    }

    Wall(Vector3 start_, Vector3 end_, float bottomHeight_, float topHeight_,
         bool blocksMovement_)
        : start(start_), end(end_), bottomHeight(bottomHeight_), topHeight(topHeight_),
          blocksMovement(blocksMovement_) {
    }
};

struct Sector {
    float floorHeight = 0.0f;
    std::optional<float> ceilingHeight = 2.0f;
    int floorMaterial = 0;
    int ceilingMaterial = 0;
    float ambientLight = 0.15f;
    std::vector<Vector2> vertices;
    std::vector<Wall> walls;
};

enum class SpriteType {
    Lamp,
    CeilingLamp,
    Barrel,
    Column,
    Bookshelf,
};

struct WorldSprite {
    Vector3    position;
    float      width      = 1.0f;
    float      height     = 1.0f;
    SpriteType spriteType = SpriteType::Lamp;
};

struct Pickup {
    Vector3 position;
    int healthAmount = 0;
    int ammoAmount = 0;
    bool collected = false;
};

// Axis-aligned area on the X/Z plane that transfers the player to another
// level. The reserved target "complete" ends the episode.
struct ExitTrigger {
    std::string targetLevel;
    float minX;
    float minZ;
    float maxX;
    float maxZ;
};

// ---------------------------------------------------------------------------
// Interactive element definitions — static geometry/position data only.
// Their mutable runtime state lives in LevelState, not here.
// ---------------------------------------------------------------------------

// Colour token shared by DoorDef and KeyPickupDef.
enum class KeyColour { None, Red, Blue, Yellow };

// A door defined by a wall segment in X/Z that blocks movement when closed and
// slides upward when opened. Optionally locked to a key colour.
// doorId must be unique within the level.
struct DoorDef {
    int       doorId       = 0;
    Vector3   wallStart;
    Vector3   wallEnd;
    float     bottomHeight = 0.0f;
    float     topHeight    = 2.0f;
    KeyColour requiredKey  = KeyColour::None;
};

// A key pickup that grants access to doors whose requiredKey matches this
// colour. keyId must be unique within the level.
struct KeyPickupDef {
    int       keyId  = 0;
    KeyColour colour = KeyColour::None;
    Vector3   position;
};

// A rectangular trigger on the X/Z plane that shows a one-shot HUD message
// the first time the player steps into it.
// triggerId must be unique within the level.
struct MessageTriggerDef {
    int         triggerId = 0;
    float       minX = 0.0f;
    float       minZ = 0.0f;
    float       maxX = 0.0f;
    float       maxZ = 0.0f;
    std::string message;
};

// ---------------------------------------------------------------------------
// v2 content structures
// ---------------------------------------------------------------------------

// A collectible audiolog tablet.  Collecting it displays the full text in
// the HUD, then marks the audioId as collected for the rest of the session.
// audioId must be unique within the level.
struct AudiologDef {
    int         audioId  = 0;
    Vector3     position;
    std::string title;    // shown in the HUD banner (short, e.g. "DR. KORVIN - LOG 1")
    std::string text;     // full message shown on the Phone/audiolog screen
};

// An armour pickup.  armorId must be unique within the level.
struct ArmorPickupDef {
    int     armorId    = 0;
    Vector3 position;
    int     armorAmount = 50; // scenario.md: standard vest gives +50
    bool    collected  = false;
};

// An axis-aligned hazard surface on the X/Z plane.  While the player's
// X/Z position is inside [minX,maxX] × [minZ,maxZ] the engine applies
// damagePerSecond continuously.  type is a hint for rendering/sound (unused
// until the audio system is implemented).
enum class HazardType : uint8_t { Acid = 0, Coolant = 1 };

struct HazardSurface {
    int         hazardId       = 0;
    float       minX = 0.0f, minZ = 0.0f;
    float       maxX = 0.0f, maxZ = 0.0f;
    float       damagePerSecond = 5.0f;
    HazardType  type           = HazardType::Acid;
};

struct LevelData {
    std::vector<Sector>             sectors;
    std::vector<WorldSprite>        sprites;
    std::vector<Pickup>             pickups;
    std::vector<PointLight>         pointLights;
    std::vector<ExitTrigger>        exitTriggers;
    // Interactive elements — populated by LevelLoader; empty vectors until then
    std::vector<DoorDef>            doors;
    std::vector<KeyPickupDef>       keys;
    std::vector<MessageTriggerDef>  messageTriggers;
    bool                            marsTerrain = false;

    // ---------------------------------------------------------------------------
    // v2 additions (scenario.md content layer)
    // ---------------------------------------------------------------------------

    // An audiolog is a collectible tablet that shows a text message once and
    // stays collected for the rest of the session.  audioId must be unique.
    std::vector<AudiologDef>        audiologs;

    // Armour pickup: grants the player armour points.  armorId must be unique.
    std::vector<ArmorPickupDef>     armorPickups;

    // Hazard surfaces: axis-aligned rectangles on the X/Z plane that deal
    // damage-per-second while the player stands on them (acid pools, coolant).
    std::vector<HazardSurface>      hazards;
};
