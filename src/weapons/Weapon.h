#pragma once

#include <vector>

#include "core/Vector3.h"

class Enemy;
class Map;

enum class WeaponType {
    Pistol = 0,
    Shotgun = 1,
    Chaingun = 2,
    Count = 3
};

struct WeaponSlot {
    WeaponType  type;
    const char* name;
    int         magazineCapacity;
    int         maxReserveAmmo;
    int         magazineAmmo;
    int         reserveAmmo;
    float       fireInterval;
    float       flashDuration;
    float       reloadDuration;
    float       range;
    int         damagePerPellet;
    int         pelletCount;
    float       spreadRadians;
    bool        automatic;
};

class Weapon {
public:
    Weapon();

    void Update(bool firePressed, float deltaSeconds, const Vector3& origin,
                float angle, const Map& map, std::vector<Enemy>& enemies);

    bool IsFiring() const { return m_flashTime > 0.0f; }
    bool IsReloading() const { return m_reloadTime > 0.0f; }
    bool JustFired() const { return m_justFired; }
    int MagazineAmmo() const;
    int ReserveAmmo() const;
    int ActiveWeaponIndex() const { return m_activeWeaponIndex; }
    WeaponType ActiveType() const { return static_cast<WeaponType>(m_activeWeaponIndex); }
    const char* ActiveWeaponName() const;

    void SelectWeapon(int index);
    void CycleWeapon(int direction);
    void AddReserveAmmo(int amount);
    void RequestReload();
    void Reset();

    int SlotMagazineAmmo(int slot) const;
    int SlotReserveAmmo(int slot) const;

    // Restore full state from a save. Used by SaveSystem.
    void LoadState(int mag, int reserve, float cooldown, float reloadTime);
    void LoadStateV4(int activeIndex, int pMag, int pRes, int sMag, int sRes,
                     int cMag, int cRes, float cooldown, float reloadTime);

private:
    std::vector<WeaponSlot> m_slots;
    int   m_activeWeaponIndex = 0;
    float m_cooldown = 0.0f;
    float m_flashTime = 0.0f;
    float m_reloadTime = 0.0f;
    bool  m_justFired = false;
};
