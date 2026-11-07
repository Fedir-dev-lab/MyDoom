#include "Weapon.h"

#include <algorithm>
#include <cmath>

#include "enemies/Enemy.h"
#include "world/Map.h"

namespace {

constexpr float kHitRadius = 0.6f;

std::vector<WeaponSlot> CreateDefaultSlots() {
    return {
        // Sterh — compact semi-automatic pistol (scenario.md: 9mm, accurate, weak damage)
        {
            WeaponType::Pistol,
            "STERH",
            12,   // magazineCapacity  (standard 9mm mag)
            48,   // maxReserveAmmo
            12,   // magazineAmmo
            36,   // reserveAmmo
            0.25f,// fireInterval     (semi-auto, slightly faster than before)
            0.06f,// flashDuration
            0.80f,// reloadDuration
            30.0f,// range            (accurate at distance)
            8,    // damagePerPellet  (scenario: weak damage)
            1,    // pelletCount
            0.0f, // spreadRadians    (precise — zero spread)
            false // automatic
        },
        // Shotgun
        // Karatel — scenario.md §6: pump-action shotgun, powerful at close range,
        // reloads after every shot. "Каратель" = Punisher.
        {
            WeaponType::Shotgun,
            "KARATEL",
            8,    // magazineCapacity  (pump, 8-shell tube)
            32,   // maxReserveAmmo
            8,    // magazineAmmo
            16,   // reserveAmmo
            0.75f,// fireInterval      (pump action — slow)
            0.12f,// flashDuration
            1.10f,// reloadDuration
            14.0f,// range             (devastating up close, falls off quickly)
            4,    // damagePerPellet   (×7 pellets = 28 total, brutal)
            7,    // pelletCount
            0.11f,// spreadRadians     (wide cone)
            false // automatic
        },
        // Reindjer — scenario.md §6: assault rifle, fast burst, moderate damage.
        // "Рейнджер" = Ranger.
        {
            WeaponType::Chaingun,
            "REINDJER",
            25,   // magazineCapacity  (5.56 mag)
            100,  // maxReserveAmmo
            25,   // magazineAmmo
            75,   // reserveAmmo
            0.09f,// fireInterval      (automatic, ~11 RPS)
            0.05f,// flashDuration
            1.20f,// reloadDuration
            28.0f,// range
            6,    // damagePerPellet   (scenario: medium damage)
            1,    // pelletCount
            0.025f,// spreadRadians    (tight, accurate)
            true  // automatic
        }
    };
}

} // namespace

Weapon::Weapon()
    : m_slots(CreateDefaultSlots()), m_activeWeaponIndex(0) {
}

int Weapon::MagazineAmmo() const {
    if (m_activeWeaponIndex >= 0 && m_activeWeaponIndex < static_cast<int>(m_slots.size())) {
        return m_slots[m_activeWeaponIndex].magazineAmmo;
    }
    return 0;
}

int Weapon::ReserveAmmo() const {
    if (m_activeWeaponIndex >= 0 && m_activeWeaponIndex < static_cast<int>(m_slots.size())) {
        return m_slots[m_activeWeaponIndex].reserveAmmo;
    }
    return 0;
}

const char* Weapon::ActiveWeaponName() const {
    if (m_activeWeaponIndex >= 0 && m_activeWeaponIndex < static_cast<int>(m_slots.size())) {
        return m_slots[m_activeWeaponIndex].name;
    }
    return "WEAPON";
}

int Weapon::SlotMagazineAmmo(int slot) const {
    if (slot >= 0 && slot < static_cast<int>(m_slots.size())) {
        return m_slots[slot].magazineAmmo;
    }
    return 0;
}

int Weapon::SlotReserveAmmo(int slot) const {
    if (slot >= 0 && slot < static_cast<int>(m_slots.size())) {
        return m_slots[slot].reserveAmmo;
    }
    return 0;
}

void Weapon::SelectWeapon(int index) {
    if (index < 0 || index >= static_cast<int>(m_slots.size()) || index == m_activeWeaponIndex) {
        return;
    }
    m_activeWeaponIndex = index;
    m_reloadTime = 0.0f; // cancel reload on switch
    m_cooldown = 0.15f;   // brief equip delay
    m_flashTime = 0.0f;
}

void Weapon::CycleWeapon(int direction) {
    const int count = static_cast<int>(m_slots.size());
    int next = (m_activeWeaponIndex + direction) % count;
    if (next < 0) next += count;
    SelectWeapon(next);
}

void Weapon::AddReserveAmmo(int amount) {
    if (m_slots.size() >= 3) {
        m_slots[0].reserveAmmo = std::min(m_slots[0].maxReserveAmmo, m_slots[0].reserveAmmo + amount);
        m_slots[1].reserveAmmo = std::min(m_slots[1].maxReserveAmmo, m_slots[1].reserveAmmo + std::max(2, amount / 2));
        m_slots[2].reserveAmmo = std::min(m_slots[2].maxReserveAmmo, m_slots[2].reserveAmmo + amount * 2);
    }
}

void Weapon::RequestReload() {
    if (m_activeWeaponIndex < 0 || m_activeWeaponIndex >= static_cast<int>(m_slots.size())) {
        return;
    }
    WeaponSlot& slot = m_slots[m_activeWeaponIndex];
    if (m_reloadTime == 0.0f && slot.magazineAmmo < slot.magazineCapacity && slot.reserveAmmo > 0) {
        m_reloadTime = slot.reloadDuration;
    }
}

void Weapon::Update(bool firePressed, float deltaSeconds, const Vector3& origin,
                    float angle, const Map& map, std::vector<Enemy>& enemies) {
    m_justFired = false;
    m_cooldown = std::max(0.0f, m_cooldown - deltaSeconds);
    m_flashTime = std::max(0.0f, m_flashTime - deltaSeconds);

    if (m_activeWeaponIndex < 0 || m_activeWeaponIndex >= static_cast<int>(m_slots.size())) {
        return;
    }
    WeaponSlot& slot = m_slots[m_activeWeaponIndex];

    if (m_reloadTime > 0.0f) {
        m_reloadTime = std::max(0.0f, m_reloadTime - deltaSeconds);
        if (m_reloadTime == 0.0f) {
            const int neededAmmo = slot.magazineCapacity - slot.magazineAmmo;
            const int loadedAmmo = std::min(neededAmmo, slot.reserveAmmo);
            slot.magazineAmmo += loadedAmmo;
            slot.reserveAmmo  -= loadedAmmo;
        }
        return;
    }

    if (!firePressed || m_cooldown > 0.0f) {
        return;
    }

    // Auto-reload on empty trigger
    if (slot.magazineAmmo == 0) {
        if (slot.reserveAmmo > 0) {
            RequestReload();
        }
        return;
    }

    m_cooldown = slot.fireInterval;
    m_flashTime = slot.flashDuration;
    m_justFired = true;
    --slot.magazineAmmo;

    for (int p = 0; p < slot.pelletCount; ++p) {
        float pelletAngle = angle;
        if (slot.pelletCount > 1) {
            const float t = static_cast<float>(p) / static_cast<float>(slot.pelletCount - 1);
            pelletAngle += (t - 0.5f) * slot.spreadRadians;
        } else if (slot.spreadRadians > 0.0f) {
            static int counter = 0;
            const float jitter = (((counter++) % 5) - 2) * (slot.spreadRadians * 0.4f);
            pelletAngle += jitter;
        }

        const Vector3 direction{std::sin(pelletAngle), 0.0f, std::cos(pelletAngle)};
        Enemy* hitEnemy = nullptr;
        float closestDistance = slot.range;

        for (Enemy& enemy : enemies) {
            if (!enemy.IsAlive()) {
                continue;
            }

            Vector3 toEnemy = enemy.Position() - origin;
            toEnemy.y = 0.0f;
            const float forwardDistance = Vector3::Dot(toEnemy, direction);
            if (forwardDistance < 0.0f || forwardDistance >= closestDistance) {
                continue;
            }

            const Vector3 closestPoint = origin + direction * forwardDistance;
            Vector3 fromRay = enemy.Position() - closestPoint;
            fromRay.y = 0.0f;
            const float enemyHitRadius = std::max(kHitRadius, enemy.Radius() * 1.2f);
            if (fromRay.LengthSquared() > enemyHitRadius * enemyHitRadius) {
                continue;
            }

            if (map.IsLineBlocked(origin, enemy.Position())) {
                continue;
            }

            hitEnemy = &enemy;
            closestDistance = forwardDistance;
        }

        if (hitEnemy) {
            hitEnemy->TakeDamage(slot.damagePerPellet);
        }
    }
}

void Weapon::LoadState(int mag, int reserve, float cooldown, float reloadTime) {
    m_slots = CreateDefaultSlots();
    m_activeWeaponIndex = 0;
    m_slots[0].magazineAmmo = mag;
    m_slots[0].reserveAmmo  = reserve;
    m_cooldown   = cooldown;
    m_reloadTime = reloadTime;
    m_flashTime  = 0.0f;
}

void Weapon::LoadStateV4(int activeIndex, int pMag, int pRes, int sMag, int sRes,
                         int cMag, int cRes, float cooldown, float reloadTime) {
    m_slots = CreateDefaultSlots();
    m_activeWeaponIndex = std::clamp(activeIndex, 0, static_cast<int>(m_slots.size()) - 1);
    m_slots[0].magazineAmmo = pMag;
    m_slots[0].reserveAmmo  = pRes;
    m_slots[1].magazineAmmo = sMag;
    m_slots[1].reserveAmmo  = sRes;
    m_slots[2].magazineAmmo = cMag;
    m_slots[2].reserveAmmo  = cRes;
    m_cooldown   = cooldown;
    m_reloadTime = reloadTime;
    m_flashTime  = 0.0f;
}

void Weapon::Reset() {
    m_slots = CreateDefaultSlots();
    m_activeWeaponIndex = 0;
    m_cooldown = 0.0f;
    m_flashTime = 0.0f;
    m_reloadTime = 0.0f;
}
