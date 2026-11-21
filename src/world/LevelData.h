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

    // Cached AABB bounds (world units)
    Vector3 boundsMin{0.0f, 0.0f, 0.0f};
    Vector3 boundsMax{0.0f, 0.0f, 0.0f};

    void ComputeBounds() {
        float minX = 1e9f, maxX = -1e9f;
        float minZ = 1e9f, maxZ = -1e9f;
        for (const Vector2& v : vertices) {
            if (v.x < minX) minX = v.x;
            if (v.x > maxX) maxX = v.x;
            if (v.y < minZ) minZ = v.y;
            if (v.y > maxZ) maxZ = v.y;
        }

        // Align baseline vertical bounds with the renderer's fallback rules:
        // floor defaults to floorHeight; ceiling defaults to ceilingHeight or 3.0f.
        const float defaultCeil = ceilingHeight.value_or(3.0f);
        float minY = floorHeight;
        float maxY = std::max(floorHeight, defaultCeil);

        for (const Wall& w : walls) {
            if (w.start.x < minX) minX = w.start.x;
            if (w.start.x > maxX) maxX = w.start.x;
            if (w.start.z < minZ) minZ = w.start.z;
            if (w.start.z > maxZ) maxZ = w.start.z;

            if (w.end.x < minX) minX = w.end.x;
            if (w.end.x > maxX) maxX = w.end.x;
            if (w.end.z < minZ) minZ = w.end.z;
            if (w.end.z > maxZ) maxZ = w.end.z;

            // Follow the exact fallback logic used in WallRenderer calls:
            const float wallBottom = w.bottomHeight.value_or(floorHeight);
            const float wallTop    = w.topHeight.value_or(defaultCeil);

            if (wallBottom < minY) minY = wallBottom;
            if (wallTop > maxY)    maxY = wallTop;
        }
        if (minX > maxX) {
            minX = maxX = 0.0f;
            minZ = maxZ = 0.0f;
        }
        boundsMin = {minX, minY, minZ};
        boundsMax = {maxX, maxY, maxZ};
    }
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

// ---------------------------------------------------------------------------
// v3 — true 3D mesh objects
// ---------------------------------------------------------------------------

// Identifies which procedural mesh to build. Only the cube for now; extending
// this enum is how new solid shapes become placeable in levels.
enum class MeshType : uint8_t { Cube = 0 };

// A solid 3D object placed in a level and rendered by MeshRenderer as
// flat-shaded, z-buffered triangles (no texture). meshId must be unique.
struct MeshObjectDef {
    int       meshId    = 0;
    MeshType  type      = MeshType::Cube;
    Vector3   position;        // world-space centre of the object
    float     yaw       = 0.0f; // rotation about the world Y axis, radians
    float     scale     = 1.0f; // edge length in world units (for a cube)
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

    // True 3D solid objects rendered by MeshRenderer (cubes, etc.). No texture:
    // flat-shaded from the face normal so the solid reads as 3D.
    std::vector<MeshObjectDef>     meshObjects;
};
