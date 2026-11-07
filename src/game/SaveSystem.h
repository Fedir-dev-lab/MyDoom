#pragma once

#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
// SaveSystem — binary save/load for 3 save slots.
//
// File format (SaveData, packed struct), version history:
//   v1 — original vertical slice (no level ID)
//   v2 — added levelId[32]; v1 saves migrate to the legacy "demo" level
//   v3 — added LevelState arrays (doors, keys, message triggers);
//         v2 saves migrated with empty arrays (no interactive elements)
//
// Save files are written to a configured writable user-data directory.
// ---------------------------------------------------------------------------

static constexpr int kSaveSlotCount = 3;

struct SaveEnemyData {
    float   posX, posY, posZ;
    int     health;
    float   attackCooldown;
    uint8_t type;  // 0=Chaser, 1=Shooter, 2=Tank
    uint8_t state; // 0=Idle, 1=Patrol, 2=Alert, 3=Chase, 4=Attack, 5=Hurt, 6=Dead
    uint8_t reserved[2];
};

struct SavePickupData {
    bool collected;
};

// ---------------------------------------------------------------------------
// LevelState serialisation records — one entry per interactive element.
// Fixed-size arrays keep the binary struct self-contained without heap alloc.
// ---------------------------------------------------------------------------
struct SaveDoorData {
    int   doorId        = 0;
    uint8_t phase       = 0; // 0=Closed 1=Opening 2=Open 3=Closing
    float openFraction  = 0.0f;
};

struct SaveKeyData {
    int     keyId     = 0;
    bool    collected = false;
};

struct SaveTriggerData {
    int  triggerId = 0;
    bool fired     = false;
};

struct SaveData {
    // Header
    uint32_t magic   = 0x444F4F4D; // "DOOM"
    uint32_t version = 6;

    // The authored level file to load before applying the saved entity state.
    static constexpr int kLevelIdLength = 32;
    char levelId[kLevelIdLength] = "demo";

    // Player
    float playerX, playerY, playerZ;
    float playerAngle, playerPitch;
    int   playerHealth;
    int   playerArmor = 0; // v6: armour points

    // Weapon
    int   magazineAmmo = 6, reserveAmmo = 24; // Sterh
    float weaponCooldown = 0.0f, reloadTime = 0.0f;
    int   activeWeaponIndex = 0;
    int   shotgunMag = 8, shotgunReserve = 16;
    int   chaingunMag = 30, chaingunReserve = 60;

    // Sun
    float sunWorldAngle, sunElevation;

    // Enemies — fixed max of 32
    static constexpr int kMaxEnemies = 32;
    int  enemyCount = 0;
    SaveEnemyData enemies[kMaxEnemies] = {};

    // Pickups — fixed max of 32
    static constexpr int kMaxPickups = 32;
    int  pickupCount = 0;
    SavePickupData pickups[kMaxPickups] = {};

    // LevelState — doors, keys, message triggers (max 16 each)
    static constexpr int kMaxDoors    = 16;
    static constexpr int kMaxKeys     = 8;
    static constexpr int kMaxTriggers = 32;

    int          doorCount    = 0;
    SaveDoorData doors[kMaxDoors] = {};

    int          keyCount = 0;
    SaveKeyData  keys[kMaxKeys] = {};

    int              triggerCount = 0;
    SaveTriggerData  triggers[kMaxTriggers] = {};

    // v2 pickups — audiolog and armour collected bitmasks.
    // Bit N = 1 means item with index N (0-based order in level file) is collected.
    static constexpr int kMaxAudiologs   = 64;
    static constexpr int kMaxArmorItems  = 32;
    uint64_t audioCollectedMask = 0; // one bit per audiolog (up to 64)
    uint32_t armorCollectedMask = 0; // one bit per armor pickup (up to 32)
};

// Human-readable one-line description shown in the Load menu.
struct SaveSlotInfo {
    bool  exists       = false;
    char  description[48] = {}; // e.g. "FINALE  HP:8  Ammo:4/12  12:34"
};

class SaveSystem {
public:
    // Sets the directory used for save files. Called once at startup with
    // SDL's per-user preference path.
    static void SetSaveDirectory(std::string directory);

    // Returns the path for a given slot index (0-based).
    static std::string SlotPath(int slot);

    // Write SaveData to disk. Returns true on success.
    static bool Save(int slot, const SaveData& data);

    // Read SaveData from disk. Returns true on success.
    static bool Load(int slot, SaveData& out);

    // Returns true if a save file for this slot exists and is valid.
    static bool HasSave(int slot);

    // Fill a SaveSlotInfo for display in the menu.
    static SaveSlotInfo GetSlotInfo(int slot);
};
