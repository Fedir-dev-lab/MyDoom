#include "Enemy.h"

#include <algorithm>
#include <cmath>

#include "player/Player.h"
#include "world/Map.h"
#include "world/LevelState.h"

Enemy::Enemy(Vector3 position, EnemyType type)
    : m_spawnPosition(position), m_position(position), m_type(type) {
    ConfigureArchetype();
}

void Enemy::ConfigureArchetype() {
    switch (m_type) {
        case EnemyType::MantisScout:
            // scenario.md: 1.8m tall, 40 HP, fast melee runner that jumps
            // at the player from shadows. moveSpeed raised to feel aggressive.
            m_maxHealth   = 40;
            m_health      = 40;
            m_radius      = 0.35f;
            m_width       = 1.1f;
            m_height      = 1.8f;
            m_moveSpeed   = 2.6f;
            break;
        case EnemyType::Shooter:
            // scenario.md: Mantis-shooter, 60 HP, keeps distance, acid spit.
            m_maxHealth   = 60;
            m_health      = 60;
            m_radius      = 0.35f;
            m_width       = 1.1f;
            m_height      = 2.0f;
            m_moveSpeed   = 1.4f;
            break;
        case EnemyType::Tank:
            // scenario.md: Mantis-heavy, 180 HP, slow, massive claws.
            m_maxHealth   = 180;
            m_health      = 180;
            m_radius      = 0.55f;
            m_width       = 1.6f;
            m_height      = 2.6f;
            m_moveSpeed   = 0.75f;
            break;
    }
}

void Enemy::Alert() {
    if (IsAlive() && (m_state == EnemyState::Idle || m_state == EnemyState::Patrol)) {
        m_state = EnemyState::Alert;
        m_stateTimer = 0.35f;
    }
}

void Enemy::Update(float deltaSeconds, Player& player, const Map& map,
                   const LevelState& levelState,
                   const std::vector<Enemy>& allEnemies, size_t selfIndex,
                   std::optional<ProjectileSpawn>& outProjectile) {
    outProjectile.reset();

    if (!IsAlive()) {
        m_state = EnemyState::Dead;
        return;
    }

    if (m_hurtTimer > 0.0f) {
        m_hurtTimer = std::max(0.0f, m_hurtTimer - deltaSeconds);
    }
    m_attackCooldown = std::max(0.0f, m_attackCooldown - deltaSeconds);

    const Vector3& playerPosition = player.Position();
    Vector3 toPlayer = playerPosition - m_position;
    toPlayer.y = 0.0f;
    const float distance = toPlayer.Length();

    const bool hasLoS = (distance <= 22.0f) &&
                        !map.IsLineBlocked(m_position, playerPosition) &&
                        !map.IsDoorLineBlocked(m_position, playerPosition, levelState);

    switch (m_state) {
        case EnemyState::Idle: {
            if (hasLoS && distance <= 14.0f) {
                m_state = EnemyState::Alert;
                m_stateTimer = 0.35f;
            } else {
                m_patrolTimer += deltaSeconds;
                if (m_patrolTimer >= 4.0f) {
                    m_patrolTimer = 0.0f;
                    m_state = EnemyState::Patrol;
                    m_stateTimer = 2.0f;
                }
            }
            return;
        }

        case EnemyState::Patrol: {
            if (hasLoS && distance <= 14.0f) {
                m_state = EnemyState::Alert;
                m_stateTimer = 0.35f;
                return;
            }
            m_stateTimer -= deltaSeconds;
            if (m_stateTimer <= 0.0f) {
                m_patrolDirection = -m_patrolDirection;
                m_state = EnemyState::Idle;
                return;
            }
            const Vector3 patrolDir{static_cast<float>(m_patrolDirection), 0.0f, 0.0f};
            const Vector3 step = patrolDir * (m_moveSpeed * 0.4f * deltaSeconds);
            const Vector3 candidate = m_position + step;
            if (!map.IsPositionBlocked(candidate, m_radius) &&
                !map.IsDoorPositionBlocked(candidate, m_radius, levelState)) {
                m_position = candidate;
            } else {
                m_patrolDirection = -m_patrolDirection;
                m_state = EnemyState::Idle;
            }
            return;
        }

        case EnemyState::Alert: {
            m_stateTimer -= deltaSeconds;
            if (m_stateTimer <= 0.0f) {
                m_state = EnemyState::Chase;
            }
            return;
        }

        case EnemyState::Hurt: {
            m_stateTimer -= deltaSeconds;
            if (m_stateTimer <= 0.0f) {
                m_state = EnemyState::Chase;
            }
            return;
        }

        case EnemyState::Attack: {
            if (m_type == EnemyType::Shooter) {
                m_stateTimer -= deltaSeconds;
                if (m_stateTimer <= 0.0f) {
                    // Fire ranged projectile towards player's torso
                    const Vector3 spawnPos = m_position + Vector3{0.0f, 0.9f, 0.0f};
                    const Vector3 targetPos = playerPosition + Vector3{0.0f, 0.8f, 0.0f};
                    outProjectile = ProjectileSpawn{
                        .position = spawnPos,
                        .target   = targetPos,
                        .damage   = 15, // scenario.md: 15 damage per acid spit
                    };
                    m_attackCooldown = 1.8f;
                    m_state = EnemyState::Chase;
                }
            } else {
                m_state = EnemyState::Chase;
            }
            return;
        }

        case EnemyState::Chase: {
            if (distance > 26.0f) {
                m_state = EnemyState::Idle;
                m_lostSightTimer = 0.0f;
                return;
            }

            // Re-check line of sight: if a wall or closed door blocks vision,
            // start a lost-sight timer.  After 3 seconds without LoS the enemy
            // gives up and returns to Idle instead of pressing against the
            // wall forever (so opening a door later doesn't release a camping
            // monster that has been waiting right behind it).
            if (hasLoS) {
                m_lostSightTimer = 0.0f;
            } else {
                m_lostSightTimer += deltaSeconds;
                if (m_lostSightTimer >= 3.0f) {
                    m_state = EnemyState::Idle;
                    m_lostSightTimer = 0.0f;
                    m_patrolTimer = 0.0f;
                    return;
                }
            }

            if (m_type == EnemyType::MantisScout) {
                constexpr float kStopDistance = 1.3f;
                constexpr int kAttackDamage = 10; // scenario.md: 10 damage per slash
                constexpr float kAttackInterval = 1.0f;

                if (distance < kStopDistance) {
                    if (m_attackCooldown == 0.0f) {
                        player.TakeDamage(kAttackDamage);
                        m_attackCooldown = kAttackInterval;
                        m_state = EnemyState::Attack;
                    }
                    return;
                }
            } else if (m_type == EnemyType::Tank) {
                constexpr float kStopDistance = 1.5f;
                constexpr int kAttackDamage = 30; // scenario.md: 30 damage per claw
                constexpr float kAttackInterval = 1.2f;

                if (distance < kStopDistance) {
                    if (m_attackCooldown == 0.0f) {
                        player.TakeDamage(kAttackDamage);
                        m_attackCooldown = kAttackInterval;
                        m_state = EnemyState::Attack;
                    }
                    return;
                }
            } else if (m_type == EnemyType::Shooter) {
                if (hasLoS && distance <= 10.0f) {
                    if (m_attackCooldown == 0.0f) {
                        m_state = EnemyState::Attack;
                        m_stateTimer = 0.25f; // brief telegraph windup
                        return;
                    }
                    if (distance < 2.5f) {
                        const Vector3 retreatDir = -toPlayer.Normalized();
                        const Vector3 retreatStep = retreatDir * (m_moveSpeed * 0.7f * deltaSeconds);
                        const Vector3 retreatCand = m_position + retreatStep;
                        if (!map.IsPositionBlocked(retreatCand, m_radius) &&
                            !map.IsDoorPositionBlocked(retreatCand, m_radius, levelState)) {
                            m_position = retreatCand;
                        }
                    }
                    return;
                }
            }

            // Normal chase movement
            Vector3 moveDir = toPlayer.Normalized();

            // Separation from other active enemies
            Vector3 separation{0.0f, 0.0f, 0.0f};
            for (size_t i = 0; i < allEnemies.size(); ++i) {
                if (i == selfIndex || !allEnemies[i].IsAlive()) { continue; }
                Vector3 toOther = m_position - allEnemies[i].Position();
                toOther.y = 0.0f;
                const float dist = toOther.Length();
                const float minDist = m_radius + allEnemies[i].Radius();
                if (dist > 0.001f && dist < minDist) {
                    separation = separation + (toOther / dist) * ((minDist - dist) / minDist);
                }
            }
            if (separation.Length() > 0.001f) {
                moveDir = (moveDir + separation * 0.8f).Normalized();
            }

            const Vector3 step = moveDir * (m_moveSpeed * deltaSeconds);
            const Vector3 candidate = m_position + step;
            if (!map.IsPositionBlocked(candidate, m_radius) &&
                !map.IsDoorPositionBlocked(candidate, m_radius, levelState)) {
                m_position = candidate;
            } else {
                // Sliding collision against perpendicular walls
                const Vector3 stepX{step.x, 0.0f, 0.0f};
                const Vector3 candX = m_position + stepX;
                if (!map.IsPositionBlocked(candX, m_radius) &&
                    !map.IsDoorPositionBlocked(candX, m_radius, levelState)) {
                    m_position = candX;
                } else {
                    const Vector3 stepZ{0.0f, 0.0f, step.z};
                    const Vector3 candZ = m_position + stepZ;
                    if (!map.IsPositionBlocked(candZ, m_radius) &&
                        !map.IsDoorPositionBlocked(candZ, m_radius, levelState)) {
                        m_position = candZ;
                    }
                }
            }
            break;
        }

        case EnemyState::Dead:
            break;
    }
}

void Enemy::TakeDamage(int damage) {
    if (damage <= 0 || !IsAlive()) { return; }
    m_health -= damage;
    if (m_health <= 0) {
        m_health = 0;
        m_state = EnemyState::Dead;
        m_hurtTimer = 0.0f;
        return;
    }
    m_hurtTimer = 0.15f;
    m_state = EnemyState::Hurt;
    m_stateTimer = 0.15f;
}

void Enemy::LoadState(const Vector3& pos, int health, float attackCooldown,
                      EnemyType type, EnemyState state) {
    m_position        = pos;
    m_health          = health;
    m_attackCooldown  = attackCooldown;
    m_type            = type;
    m_state           = state;
    m_hurtTimer       = 0.0f;
    m_stateTimer      = 0.0f;
    ConfigureArchetype();
    m_health          = health; // restore exact saved health after archetype configuration
}

void Enemy::Reset() {
    m_position = m_spawnPosition;
    ConfigureArchetype();
    m_state = EnemyState::Idle;
    m_attackCooldown = 0.0f;
    m_hurtTimer = 0.0f;
    m_stateTimer = 0.0f;
    m_patrolTimer = 0.0f;
    m_lostSightTimer = 0.0f;
}
