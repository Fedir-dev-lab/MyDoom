#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "renderer/Texture.h"

// Stable IDs keep world and rendering code independent from how a texture is
// produced. Assets are procedural for now; file loading will replace the
// constructor implementation without changing its callers.
enum class TextureId : size_t {
    Wall,
    Door,
    FirstFloor,
    SecondFloor,
    OutdoorFloor,
    FirstCeiling,
    SecondCeiling,
    Lamp,
    CeilingLamp,
    Barrel,
    Column,
    Bookshelf,
    Enemy,              // Chaser
    EnemyCorpse,        // Chaser corpse
    EnemyShooter,
    EnemyShooterCorpse,
    EnemyTank,
    EnemyTankCorpse,
    Projectile,
    Medkit,
    Ammo,
    ExitGate,
    KeyRed,
    KeyBlue,
    KeyYellow,
    CubeImpostor,       // LOD 2 Impostor billboard texture for 3D cubes / crates
    Count,
};

// Aliases for readability
inline constexpr TextureId TextureId_EnemyChaser = TextureId::Enemy;
inline constexpr TextureId TextureId_EnemyChaserCorpse = TextureId::EnemyCorpse;

class AssetManager {
public:
    explicit AssetManager(std::string assetDirectory);

    const Texture& GetTexture(TextureId id) const;

private:
    std::string m_assetDirectory;
    std::vector<Texture> m_textures;
};
