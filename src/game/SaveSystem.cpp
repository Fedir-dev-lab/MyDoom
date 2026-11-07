// MyDoomEngine — binary save/load with version migration.

#include "SaveSystem.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

#include "player/Player.h"

namespace {

std::string g_saveDirectory = "userdata/";   // fallback only; never shipped

constexpr uint32_t kSaveMagic          = 0x444F4F4D;
constexpr uint32_t kCurrentSaveVersion = 6;

// ---------------------------------------------------------------------------
// Legacy layouts — kept only to migrate existing saves.
// ---------------------------------------------------------------------------

struct SaveEnemyDataV4 {
    float posX, posY, posZ;
    int   health;
    float attackCooldown;
};

// v1: original vertical slice (no level ID).
struct SaveDataV1 {
    uint32_t magic;
    uint32_t version;
    float playerX, playerY, playerZ;
    float playerAngle, playerPitch;
    int playerHealth;
    int magazineAmmo, reserveAmmo;
    float weaponCooldown, reloadTime;
    float sunWorldAngle, sunElevation;
    int enemyCount;
    SaveEnemyDataV4 enemies[SaveData::kMaxEnemies];
    int pickupCount;
    SavePickupData pickups[SaveData::kMaxPickups];
};

// v2: added levelId[32]; identical to SaveData minus the LevelState arrays.
struct SaveDataV2 {
    uint32_t magic;
    uint32_t version;
    static constexpr int kLevelIdLength = 32;
    char levelId[kLevelIdLength];
    float playerX, playerY, playerZ;
    float playerAngle, playerPitch;
    int   playerHealth;
    int   magazineAmmo, reserveAmmo;
    float weaponCooldown, reloadTime;
    float sunWorldAngle, sunElevation;
    static constexpr int kMaxEnemies = 32;
    int  enemyCount;
    SaveEnemyDataV4 enemies[kMaxEnemies];
    static constexpr int kMaxPickups = 32;
    int  pickupCount;
    SavePickupData pickups[kMaxPickups];
};

// v3: added LevelState arrays (doors, keys, message triggers).
struct SaveDataV3 {
    uint32_t magic;
    uint32_t version;
    static constexpr int kLevelIdLength = 32;
    char levelId[kLevelIdLength];
    float playerX, playerY, playerZ;
    float playerAngle, playerPitch;
    int   playerHealth;
    int   magazineAmmo, reserveAmmo;
    float weaponCooldown, reloadTime;
    float sunWorldAngle, sunElevation;
    static constexpr int kMaxEnemies = 32;
    int  enemyCount;
    SaveEnemyDataV4 enemies[kMaxEnemies];
    static constexpr int kMaxPickups = 32;
    int  pickupCount;
    SavePickupData pickups[kMaxPickups];
    static constexpr int kMaxDoors    = 16;
    static constexpr int kMaxKeys     = 8;
    static constexpr int kMaxTriggers = 32;
    int          doorCount;
    SaveDoorData doors[kMaxDoors];
    int          keyCount;
    SaveKeyData  keys[kMaxKeys];
    int              triggerCount;
    SaveTriggerData  triggers[kMaxTriggers];
};

// v4: added multi-weapon inventory
struct SaveDataV4 {
    uint32_t magic;
    uint32_t version;
    static constexpr int kLevelIdLength = 32;
    char levelId[kLevelIdLength];
    float playerX, playerY, playerZ;
    float playerAngle, playerPitch;
    int   playerHealth;
    int   magazineAmmo, reserveAmmo;
    float weaponCooldown, reloadTime;
    int   activeWeaponIndex;
    int   shotgunMag, shotgunReserve;
    int   chaingunMag, chaingunReserve;
    float sunWorldAngle, sunElevation;
    static constexpr int kMaxEnemies = 32;
    int  enemyCount;
    SaveEnemyDataV4 enemies[kMaxEnemies];
    static constexpr int kMaxPickups = 32;
    int  pickupCount;
    SavePickupData pickups[kMaxPickups];
    static constexpr int kMaxDoors    = 16;
    static constexpr int kMaxKeys     = 8;
    static constexpr int kMaxTriggers = 32;
    int          doorCount;
    SaveDoorData doors[kMaxDoors];
    int          keyCount;
    SaveKeyData  keys[kMaxKeys];
    int              triggerCount;
    SaveTriggerData  triggers[kMaxTriggers];
};

// ---------------------------------------------------------------------------
// Validation helpers
// ---------------------------------------------------------------------------

bool IsValidSlot(int slot) {
    return slot >= 0 && slot < kSaveSlotCount;
}

bool IsValidLevelId(const char* levelId) {
    size_t length = 0;
    while (length < static_cast<size_t>(SaveData::kLevelIdLength) && levelId[length] != '\0') {
        const unsigned char character = static_cast<unsigned char>(levelId[length]);
        if (!std::isalnum(character) && character != '_' && character != '-') {
            return false;
        }
        ++length;
    }
    return length > 0 && length < static_cast<size_t>(SaveData::kLevelIdLength);
}

bool IsValidSaveData(const SaveData& data) {
    return data.magic == kSaveMagic && data.version == kCurrentSaveVersion &&
           IsValidLevelId(data.levelId) &&
           data.enemyCount >= 0 && data.enemyCount <= SaveData::kMaxEnemies &&
           data.pickupCount >= 0 && data.pickupCount <= SaveData::kMaxPickups &&
           data.doorCount >= 0 && data.doorCount <= SaveData::kMaxDoors &&
           data.keyCount >= 0 && data.keyCount <= SaveData::kMaxKeys &&
           data.triggerCount >= 0 && data.triggerCount <= SaveData::kMaxTriggers &&
           data.playerArmor >= 0 && data.playerArmor <= Player::kMaximumArmor &&
           std::isfinite(data.playerX) && std::isfinite(data.playerY) &&
           std::isfinite(data.playerZ) && std::isfinite(data.playerAngle) &&
           std::isfinite(data.playerPitch) && std::isfinite(data.sunWorldAngle) &&
           std::isfinite(data.sunElevation);
}

// ---------------------------------------------------------------------------
// Migration helpers
// ---------------------------------------------------------------------------

void MigrateV1(const SaveDataV1& src, SaveData& dst) {
    dst = {};
    std::snprintf(dst.levelId, sizeof(dst.levelId), "demo");
    dst.playerX = src.playerX; dst.playerY = src.playerY; dst.playerZ = src.playerZ;
    dst.playerAngle = src.playerAngle; dst.playerPitch = src.playerPitch;
    dst.playerHealth = src.playerHealth;
    dst.magazineAmmo = src.magazineAmmo; dst.reserveAmmo = src.reserveAmmo;
    dst.weaponCooldown = src.weaponCooldown; dst.reloadTime = src.reloadTime;
    dst.activeWeaponIndex = 0;
    dst.shotgunMag = 8; dst.shotgunReserve = 16;
    dst.chaingunMag = 30; dst.chaingunReserve = 60;
    dst.sunWorldAngle = src.sunWorldAngle; dst.sunElevation = src.sunElevation;
    dst.enemyCount = src.enemyCount;
    for (int i = 0; i < SaveData::kMaxEnemies; ++i) {
        dst.enemies[i].posX = src.enemies[i].posX;
        dst.enemies[i].posY = src.enemies[i].posY;
        dst.enemies[i].posZ = src.enemies[i].posZ;
        dst.enemies[i].health = src.enemies[i].health;
        dst.enemies[i].attackCooldown = src.enemies[i].attackCooldown;
        dst.enemies[i].type = 0;
        dst.enemies[i].state = src.enemies[i].health > 0 ? 3 : 6;
    }
    dst.pickupCount = src.pickupCount;
    for (int i = 0; i < SaveData::kMaxPickups; ++i) { dst.pickups[i] = src.pickups[i]; }
    // LevelState arrays: no interactive elements in v1 levels
    dst.doorCount = 0; dst.keyCount = 0; dst.triggerCount = 0;
}

void MigrateV2(const SaveDataV2& src, SaveData& dst) {
    dst = {};
    std::snprintf(dst.levelId, sizeof(dst.levelId), "%s", src.levelId);
    dst.playerX = src.playerX; dst.playerY = src.playerY; dst.playerZ = src.playerZ;
    dst.playerAngle = src.playerAngle; dst.playerPitch = src.playerPitch;
    dst.playerHealth = src.playerHealth;
    dst.magazineAmmo = src.magazineAmmo; dst.reserveAmmo = src.reserveAmmo;
    dst.weaponCooldown = src.weaponCooldown; dst.reloadTime = src.reloadTime;
    dst.activeWeaponIndex = 0;
    dst.shotgunMag = 8; dst.shotgunReserve = 16;
    dst.chaingunMag = 30; dst.chaingunReserve = 60;
    dst.sunWorldAngle = src.sunWorldAngle; dst.sunElevation = src.sunElevation;
    dst.enemyCount = src.enemyCount;
    for (int i = 0; i < SaveData::kMaxEnemies; ++i) {
        dst.enemies[i].posX = src.enemies[i].posX;
        dst.enemies[i].posY = src.enemies[i].posY;
        dst.enemies[i].posZ = src.enemies[i].posZ;
        dst.enemies[i].health = src.enemies[i].health;
        dst.enemies[i].attackCooldown = src.enemies[i].attackCooldown;
        dst.enemies[i].type = 0;
        dst.enemies[i].state = src.enemies[i].health > 0 ? 3 : 6;
    }
    dst.pickupCount = src.pickupCount;
    for (int i = 0; i < SaveData::kMaxPickups; ++i) { dst.pickups[i] = src.pickups[i]; }
    // LevelState arrays: v2 levels had no interactive elements
    dst.doorCount = 0; dst.keyCount = 0; dst.triggerCount = 0;
}

void MigrateV3(const SaveDataV3& src, SaveData& dst) {
    dst = {};
    std::snprintf(dst.levelId, sizeof(dst.levelId), "%s", src.levelId);
    dst.playerX = src.playerX; dst.playerY = src.playerY; dst.playerZ = src.playerZ;
    dst.playerAngle = src.playerAngle; dst.playerPitch = src.playerPitch;
    dst.playerHealth = src.playerHealth;
    dst.magazineAmmo = src.magazineAmmo; dst.reserveAmmo = src.reserveAmmo;
    dst.weaponCooldown = src.weaponCooldown; dst.reloadTime = src.reloadTime;
    dst.activeWeaponIndex = 0;
    dst.shotgunMag = 8; dst.shotgunReserve = 16;
    dst.chaingunMag = 30; dst.chaingunReserve = 60;
    dst.sunWorldAngle = src.sunWorldAngle; dst.sunElevation = src.sunElevation;
    dst.enemyCount = src.enemyCount;
    for (int i = 0; i < SaveData::kMaxEnemies; ++i) {
        dst.enemies[i].posX = src.enemies[i].posX;
        dst.enemies[i].posY = src.enemies[i].posY;
        dst.enemies[i].posZ = src.enemies[i].posZ;
        dst.enemies[i].health = src.enemies[i].health;
        dst.enemies[i].attackCooldown = src.enemies[i].attackCooldown;
        dst.enemies[i].type = 0;
        dst.enemies[i].state = src.enemies[i].health > 0 ? 3 : 6;
    }
    dst.pickupCount = src.pickupCount;
    for (int i = 0; i < SaveData::kMaxPickups; ++i) { dst.pickups[i] = src.pickups[i]; }
    dst.doorCount = src.doorCount;
    for (int i = 0; i < SaveData::kMaxDoors; ++i) { dst.doors[i] = src.doors[i]; }
    dst.keyCount = src.keyCount;
    for (int i = 0; i < SaveData::kMaxKeys; ++i) { dst.keys[i] = src.keys[i]; }
    dst.triggerCount = src.triggerCount;
    for (int i = 0; i < SaveData::kMaxTriggers; ++i) { dst.triggers[i] = src.triggers[i]; }
}

void MigrateV4(const SaveDataV4& src, SaveData& dst) {
    dst = {};
    std::snprintf(dst.levelId, sizeof(dst.levelId), "%s", src.levelId);
    dst.playerX = src.playerX; dst.playerY = src.playerY; dst.playerZ = src.playerZ;
    dst.playerAngle = src.playerAngle; dst.playerPitch = src.playerPitch;
    dst.playerHealth = src.playerHealth;
    dst.magazineAmmo = src.magazineAmmo; dst.reserveAmmo = src.reserveAmmo;
    dst.weaponCooldown = src.weaponCooldown; dst.reloadTime = src.reloadTime;
    dst.activeWeaponIndex = src.activeWeaponIndex;
    dst.shotgunMag = src.shotgunMag; dst.shotgunReserve = src.shotgunReserve;
    dst.chaingunMag = src.chaingunMag; dst.chaingunReserve = src.chaingunReserve;
    dst.sunWorldAngle = src.sunWorldAngle; dst.sunElevation = src.sunElevation;
    dst.enemyCount = src.enemyCount;
    for (int i = 0; i < SaveData::kMaxEnemies; ++i) {
        dst.enemies[i].posX = src.enemies[i].posX;
        dst.enemies[i].posY = src.enemies[i].posY;
        dst.enemies[i].posZ = src.enemies[i].posZ;
        dst.enemies[i].health = src.enemies[i].health;
        dst.enemies[i].attackCooldown = src.enemies[i].attackCooldown;
        dst.enemies[i].type = 0;
        dst.enemies[i].state = src.enemies[i].health > 0 ? 3 : 6;
    }
    dst.pickupCount = src.pickupCount;
    for (int i = 0; i < SaveData::kMaxPickups; ++i) { dst.pickups[i] = src.pickups[i]; }
    dst.doorCount = src.doorCount;
    for (int i = 0; i < SaveData::kMaxDoors; ++i) { dst.doors[i] = src.doors[i]; }
    dst.keyCount = src.keyCount;
    for (int i = 0; i < SaveData::kMaxKeys; ++i) { dst.keys[i] = src.keys[i]; }
    dst.triggerCount = src.triggerCount;
    for (int i = 0; i < SaveData::kMaxTriggers; ++i) { dst.triggers[i] = src.triggers[i]; }
}

// v5: добавлены поля shotgunMag/chaingunMag; без playerArmor и bitmask'ов аудиологов.
struct SaveDataV5 {
    uint32_t magic;
    uint32_t version;
    static constexpr int kLevelIdLength = 32;
    char levelId[kLevelIdLength];
    float playerX, playerY, playerZ;
    float playerAngle, playerPitch;
    int   playerHealth;
    int   magazineAmmo, reserveAmmo;
    float weaponCooldown, reloadTime;
    int   activeWeaponIndex;
    int   shotgunMag, shotgunReserve;
    int   chaingunMag, chaingunReserve;
    float sunWorldAngle, sunElevation;
    static constexpr int kMaxEnemies = 32;
    int  enemyCount;
    SaveEnemyDataV4 enemies[kMaxEnemies];
    static constexpr int kMaxPickups = 32;
    int  pickupCount;
    SavePickupData pickups[kMaxPickups];
    static constexpr int kMaxDoors    = 16;
    static constexpr int kMaxKeys     = 8;
    static constexpr int kMaxTriggers = 32;
    int          doorCount;
    SaveDoorData doors[kMaxDoors];
    int          keyCount;
    SaveKeyData  keys[kMaxKeys];
    int              triggerCount;
    SaveTriggerData  triggers[kMaxTriggers];
};

void MigrateV5(const SaveDataV5& src, SaveData& dst) {
    dst = {};
    std::snprintf(dst.levelId, sizeof(dst.levelId), "%s", src.levelId);
    dst.playerX = src.playerX; dst.playerY = src.playerY; dst.playerZ = src.playerZ;
    dst.playerAngle = src.playerAngle; dst.playerPitch = src.playerPitch;
    dst.playerHealth = src.playerHealth;
    dst.playerArmor = 0; // v5 had no armour
    dst.magazineAmmo = src.magazineAmmo; dst.reserveAmmo = src.reserveAmmo;
    dst.weaponCooldown = src.weaponCooldown; dst.reloadTime = src.reloadTime;
    dst.activeWeaponIndex = src.activeWeaponIndex;
    dst.shotgunMag = src.shotgunMag; dst.shotgunReserve = src.shotgunReserve;
    dst.chaingunMag = src.chaingunMag; dst.chaingunReserve = src.chaingunReserve;
    dst.sunWorldAngle = src.sunWorldAngle; dst.sunElevation = src.sunElevation;
    dst.enemyCount = src.enemyCount;
    for (int i = 0; i < SaveData::kMaxEnemies; ++i) {
        dst.enemies[i].posX = src.enemies[i].posX;
        dst.enemies[i].posY = src.enemies[i].posY;
        dst.enemies[i].posZ = src.enemies[i].posZ;
        dst.enemies[i].health = src.enemies[i].health;
        dst.enemies[i].attackCooldown = src.enemies[i].attackCooldown;
        dst.enemies[i].type = 0;
        dst.enemies[i].state = src.enemies[i].health > 0 ? 3 : 6;
    }
    dst.pickupCount = src.pickupCount;
    for (int i = 0; i < SaveData::kMaxPickups; ++i) { dst.pickups[i] = src.pickups[i]; }
    dst.doorCount = src.doorCount;
    for (int i = 0; i < SaveData::kMaxDoors; ++i) { dst.doors[i] = src.doors[i]; }
    dst.keyCount = src.keyCount;
    for (int i = 0; i < SaveData::kMaxKeys; ++i) { dst.keys[i] = src.keys[i]; }
    dst.triggerCount = src.triggerCount;
    for (int i = 0; i < SaveData::kMaxTriggers; ++i) { dst.triggers[i] = src.triggers[i]; }
    dst.audioCollectedMask = 0;
    dst.armorCollectedMask = 0;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void SaveSystem::SetSaveDirectory(std::string directory) {
    if (directory.empty()) { return; }
    const char last = directory.back();
    if (last != '/' && last != '\\') { directory += '/'; }
    g_saveDirectory = std::move(directory);
}

std::string SaveSystem::SlotPath(int slot) {
    if (!IsValidSlot(slot)) { return {}; }
    return g_saveDirectory + "save" + std::to_string(slot) + ".dat";
}

bool SaveSystem::Save(int slot, const SaveData& data) {
    if (!IsValidSlot(slot) || !IsValidSaveData(data)) { return false; }
    const std::string path = SlotPath(slot);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { return false; }
    const size_t written = std::fwrite(&data, sizeof(SaveData), 1, f);
    std::fclose(f);
    return written == 1;
}

bool SaveSystem::Load(int slot, SaveData& out) {
    if (!IsValidSlot(slot)) { return false; }
    const std::string path = SlotPath(slot);
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { return false; }

    uint32_t header[2] = {};
    const size_t headerRead = std::fread(header, sizeof(header), 1, f);
    if (headerRead != 1 || header[0] != kSaveMagic) {
        std::fclose(f);
        return false;
    }

    std::rewind(f);
    bool loaded = false;

    if (header[1] == kCurrentSaveVersion) {
        loaded = std::fread(&out, sizeof(SaveData), 1, f) == 1;
    } else if (header[1] == 5) {
        SaveDataV5 legacy{};
        if (std::fread(&legacy, sizeof(legacy), 1, f) == 1) {
            MigrateV5(legacy, out);
            loaded = true;
        }
    } else if (header[1] == 4) {
        SaveDataV4 legacy{};
        if (std::fread(&legacy, sizeof(legacy), 1, f) == 1) {
            MigrateV4(legacy, out);
            loaded = true;
        }
    } else if (header[1] == 3) {
        SaveDataV3 legacy{};
        if (std::fread(&legacy, sizeof(legacy), 1, f) == 1) {
            MigrateV3(legacy, out);
            loaded = true;
        }
    } else if (header[1] == 2) {
        SaveDataV2 legacy{};
        if (std::fread(&legacy, sizeof(legacy), 1, f) == 1) {
            MigrateV2(legacy, out);
            loaded = true;
        }
    } else if (header[1] == 1) {
        SaveDataV1 legacy{};
        if (std::fread(&legacy, sizeof(legacy), 1, f) == 1) {
            MigrateV1(legacy, out);
            loaded = true;
        }
    }

    std::fclose(f);
    return loaded && IsValidSaveData(out);
}

bool SaveSystem::HasSave(int slot) {
    if (!IsValidSlot(slot)) { return false; }
    SaveData data;
    return Load(slot, data);
}

SaveSlotInfo SaveSystem::GetSlotInfo(int slot) {
    SaveSlotInfo info;
    info.exists = false;
    if (!IsValidSlot(slot)) { return info; }

    SaveData data;
    if (!Load(slot, data)) { return info; }
    info.exists = true;

    // Derive approximate in-game time from sun angle (matches Phone.cpp).
    constexpr float kPi = 3.14159265f;
    float a = std::fmod(data.sunWorldAngle, 2.0f * kPi);
    if (a < 0.0f) { a += 2.0f * kPi; }
    const float totalHours = 6.0f + a / (2.0f * kPi) * 24.0f;
    const int hours   = static_cast<int>(totalHours) % 24;
    const int minutes = static_cast<int>((totalHours - std::floor(totalHours)) * 60.0f);

    std::snprintf(info.description, sizeof(info.description),
                  "%s HP:%d %d/%d %02d:%02d",
                  data.levelId,
                  data.playerHealth,
                  data.magazineAmmo, data.reserveAmmo,
                  hours, minutes);
    return info;
}
