#include "AssetManager.h"

#include <filesystem>
#include <utility>

#include "game/Logger.h"
#include "renderer/Framebuffer.h"

AssetManager::AssetManager(std::string assetDirectory)
    : m_assetDirectory(std::move(assetDirectory)) {
    m_textures.reserve(static_cast<size_t>(TextureId::Count));

    // Helper: спробувати завантажити PPM-файл, при невдачі повернути fallback.
    auto loadOrFallback = [&](const std::string& filename, Texture fallback) -> Texture {
        const std::string path = m_assetDirectory + "/textures/" + filename;
        if (auto t = Texture::LoadPpm(path)) {
            Logger::Info("Loaded texture: " + path);
            return std::move(*t);
        }
        if (std::filesystem::exists(path))
            Logger::Error("Could not read texture; using procedural fallback: " + path);
        return fallback;
    };

    // ---- Wall (TextureId::Wall) ----
    m_textures.push_back(loadOrFallback("wall.ppm",
        Texture::CreateCheckerboard(64, 64, 8,
            Framebuffer::MakeColor(196, 82, 48),
            Framebuffer::MakeColor(72, 28, 20))));

    // ---- Floors ----
    m_textures.push_back(loadOrFallback("floor1.ppm",
        Texture::CreateCheckerboard(64, 64, 8,
            Framebuffer::MakeColor(72, 62, 52),
            Framebuffer::MakeColor(42, 36, 32))));

    m_textures.push_back(loadOrFallback("floor2.ppm",
        Texture::CreateCheckerboard(64, 64, 8,
            Framebuffer::MakeColor(122, 58, 38),
            Framebuffer::MakeColor(76, 34, 26))));

    m_textures.push_back(loadOrFallback("floor_outdoor.ppm",
        Texture::CreateCheckerboard(64, 64, 8,
            Framebuffer::MakeColor(62, 102, 55),
            Framebuffer::MakeColor(38, 70, 35))));

    // ---- Ceilings ----
    m_textures.push_back(loadOrFallback("ceiling1.ppm",
        Texture::CreateCheckerboard(64, 64, 16,
            Framebuffer::MakeColor(78, 84, 96),
            Framebuffer::MakeColor(58, 64, 76))));

    m_textures.push_back(loadOrFallback("ceiling2.ppm",
        Texture::CreateCheckerboard(64, 64, 16,
            Framebuffer::MakeColor(86, 76, 100),
            Framebuffer::MakeColor(62, 54, 76))));

    // ---- Sprites (процедурні, поки немає файлів) ----
    m_textures.push_back(loadOrFallback("lamp.ppm",
        Texture::CreateLamp(32, 64,
            Framebuffer::MakeColor(52, 48, 42),
            Framebuffer::MakeColor(255, 214, 82))));

    m_textures.push_back(loadOrFallback("ceiling_lamp.ppm",
        Texture::CreateCeilingLamp(32, 48,
            Framebuffer::MakeColor(58, 54, 48),
            Framebuffer::MakeColor(255, 240, 160))));

    m_textures.push_back(loadOrFallback("barrel.ppm",
        Texture::CreateBarrel(32, 40,
            Framebuffer::MakeColor(100, 68, 38),
            Framebuffer::MakeColor(52, 36, 20))));

    m_textures.push_back(loadOrFallback("column.ppm",
        Texture::CreateColumn(32, 64,
            Framebuffer::MakeColor(148, 136, 118),
            Framebuffer::MakeColor(80, 72, 62))));

    m_textures.push_back(loadOrFallback("bookshelf.ppm",
        Texture::CreateBookshelf(64, 64)));

    // ---- Enemies ----
    m_textures.push_back(loadOrFallback("enemy_scout.ppm",
        Texture::CreateEnemy(32, 48,
            Framebuffer::MakeColor(100, 180, 80),
            Framebuffer::MakeColor(230, 200, 30))));

    m_textures.push_back(loadOrFallback("enemy_scout_corpse.ppm",
        Texture::CreateEnemyCorpse(32, 24,
            Framebuffer::MakeColor(60, 110, 45),
            Framebuffer::MakeColor(140, 200, 40))));

    m_textures.push_back(loadOrFallback("enemy_shooter.ppm",
        Texture::CreateEnemyShooter(32, 48,
            Framebuffer::MakeColor(42, 94, 76),
            Framebuffer::MakeColor(60, 240, 255))));

    m_textures.push_back(loadOrFallback("enemy_shooter_corpse.ppm",
        Texture::CreateEnemyCorpse(32, 24,
            Framebuffer::MakeColor(34, 70, 58),
            Framebuffer::MakeColor(130, 40, 140))));

    m_textures.push_back(loadOrFallback("enemy_tank.ppm",
        Texture::CreateEnemyTank(48, 54,
            Framebuffer::MakeColor(84, 88, 96),
            Framebuffer::MakeColor(255, 110, 20))));

    m_textures.push_back(loadOrFallback("enemy_tank_corpse.ppm",
        Texture::CreateEnemyCorpse(48, 28,
            Framebuffer::MakeColor(60, 62, 68),
            Framebuffer::MakeColor(140, 16, 20))));

    // ---- Pickups / UI ----
    m_textures.push_back(loadOrFallback("projectile.ppm",
        Texture::CreateProjectile(16, 16,
            Framebuffer::MakeColor(255, 255, 200),
            Framebuffer::MakeColor(255, 120, 10))));

    m_textures.push_back(loadOrFallback("medkit.ppm",
        Texture::CreateMedkit(32, 32,
            Framebuffer::MakeColor(232, 224, 208),
            Framebuffer::MakeColor(202, 38, 36))));

    m_textures.push_back(loadOrFallback("ammo.ppm",
        Texture::CreateAmmoBox(32, 32,
            Framebuffer::MakeColor(56, 80, 136),
            Framebuffer::MakeColor(222, 190, 62))));

    m_textures.push_back(loadOrFallback("exit_gate.ppm",
        Texture::CreateExitGate(32, 64,
            Framebuffer::MakeColor(48, 170, 90),
            Framebuffer::MakeColor(160, 255, 190))));

    m_textures.push_back(loadOrFallback("key_red.ppm",
        Texture::CreateKey(24, 40, Framebuffer::MakeColor(220, 52, 40))));

    m_textures.push_back(loadOrFallback("key_blue.ppm",
        Texture::CreateKey(24, 40, Framebuffer::MakeColor(52, 120, 220))));

    m_textures.push_back(loadOrFallback("key_yellow.ppm",
        Texture::CreateKey(24, 40, Framebuffer::MakeColor(210, 185, 30))));
}

const Texture& AssetManager::GetTexture(TextureId id) const {
    return m_textures[static_cast<size_t>(id)];
}
