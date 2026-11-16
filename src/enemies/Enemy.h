#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "core/Vector3.h"

class Map;
class Player;
class LevelState;

enum class EnemyType : uint8_t {
    // First scenario-facing archetype. Its behaviour and procedural sprite are
    // still the prototype implementation until final Mantis assets arrive.
    MantisScout = 0,
    // Kept so old levels and save data that used the prototype name stay valid.
    Chaser = MantisScout,
    Shooter = 1,
    Tank = 2,
};

enum class EnemyState : uint8_t {
    Idle = 0,
    Patrol = 1,
    Alert = 2,
    Chase = 3,
    Attack = 4,
    Hurt = 5,
    Dead = 6,
};

struct ProjectileSpawn {
    Vector3 position;
    Vector3 target;
    int damage = 1;
};

class Enemy {
public:
    explicit Enemy(Vector3 position, EnemyType type = EnemyType::MantisScout);

    const Vector3& Position() const { return m_position; }
    EnemyType Type() const { return m_type; }
    EnemyState State() const { return m_state; }
    bool IsAlive() const { return m_health > 0; }
    bool IsHurt() const { return m_hurtTimer > 0.0f; }
    int Health() const { return m_health; }
    int MaxHealth() const { return m_maxHealth; }
    float Radius() const { return m_radius; }
    float Width() const { return m_width; }
    float Height() const { return m_height; }
    float AttackCooldown() const { return m_attackCooldown; }

    bool JustAlerted()  const { return m_justAlerted; }
    bool JustAttacked() const { return m_justAttacked; }
    bool JustHurt()     const { return m_justHurt; }
    bool JustDied()     const { return m_justDied; }
    bool JustStepped()  const { return m_justStepped; }

    void Alert();
    void TakeDamage(int damage);
    void Reset();

    // Updates AI state and movement.
    // If a ranged attack fires, returns projectile spawn info in outProjectile.
    void Update(float deltaSeconds, Player& player, const Map& map,
                const LevelState& levelState,
                const std::vector<Enemy>& allEnemies, size_t selfIndex,
                std::optional<ProjectileSpawn>& outProjectile);

    // Restore state from a save. Used by SaveSystem.
    void LoadState(const Vector3& pos, int health, float attackCooldown,
                   EnemyType type = EnemyType::MantisScout,
                   EnemyState state = EnemyState::Chase);

private:
    void ConfigureArchetype();

    Vector3 m_spawnPosition;
    Vector3 m_position;
    EnemyType m_type = EnemyType::MantisScout;
    EnemyState m_state = EnemyState::Idle;

    int m_health = 3;
    int m_maxHealth = 3;
    float m_radius = 0.35f;
    float m_width = 1.1f;
    float m_height = 1.8f;
    float m_moveSpeed = 1.4f;

    float m_attackCooldown = 0.0f;
    float m_hurtTimer = 0.0f;
    float m_stateTimer = 0.0f;
    float m_patrolTimer = 0.0f;
    float m_lostSightTimer = 0.0f;
    float m_repathTimer = 0.0f;
    std::vector<Vector3> m_path;
    Vector3 m_patrolTarget{0.0f, 0.0f, 0.0f};
    bool m_returningToSpawn = false;
    int   m_patrolDirection = 1;

    // Transient audio / event triggers
    bool m_justAlerted  = false;
    bool m_justAttacked = false;
    bool m_justHurt     = false;
    bool m_justDied     = false;
    bool m_justStepped  = false;
    float m_stepTimer   = 0.0f;
};
