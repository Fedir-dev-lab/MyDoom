#include "AssetManager.h"

#include <filesystem>
#include <utility>

#include "game/Logger.h"
#include "renderer/Framebuffer.h"

AssetManager::AssetManager(std::string assetDirectory)
    : m_assetDirectory(std::move(assetDirectory)) {
    m_textures.reserve(static_cast<size_t>(TextureId::Count));

    m_textures.push_back(Texture::CreateCheckerboard(
        64, 64, 8,
        Framebuffer::MakeColor(196, 82, 48),
        Framebuffer::MakeColor(72, 28, 20)));
    m_textures.push_back(Texture::CreateCheckerboard(
        64, 64, 8,
        Framebuffer::MakeColor(72, 62, 52),
        Framebuffer::MakeColor(42, 36, 32)));
    m_textures.push_back(Texture::CreateCheckerboard(
        64, 64, 8,
        // Temporary Martian regolith palette. This remains procedural until
        // authored terrain textures are supplied.
        Framebuffer::MakeColor(122, 58, 38),
        Framebuffer::MakeColor(76, 34, 26)));
    m_textures.push_back(Texture::CreateCheckerboard(
        64, 64, 8,
        Framebuffer::MakeColor(62, 102, 55),
        Framebuffer::MakeColor(38, 70, 35)));
    m_textures.push_back(Texture::CreateCheckerboard(
        64, 64, 16,
        Framebuffer::MakeColor(78, 84, 96),
        Framebuffer::MakeColor(58, 64, 76)));
    m_textures.push_back(Texture::CreateCheckerboard(
        64, 64, 16,
        Framebuffer::MakeColor(86, 76, 100),
        Framebuffer::MakeColor(62, 54, 76)));
    m_textures.push_back(Texture::CreateLamp(
        32, 64,
        Framebuffer::MakeColor(52, 48, 42),
        Framebuffer::MakeColor(255, 214, 82)));
    m_textures.push_back(Texture::CreateCeilingLamp(
        32, 48,
        Framebuffer::MakeColor(58, 54, 48),
        Framebuffer::MakeColor(255, 240, 160)));
    m_textures.push_back(Texture::CreateBarrel(
        32, 40,
        Framebuffer::MakeColor(100, 68, 38),
        Framebuffer::MakeColor(52, 36, 20)));
    m_textures.push_back(Texture::CreateColumn(
        32, 64,
        Framebuffer::MakeColor(148, 136, 118),
        Framebuffer::MakeColor(80, 72, 62)));
    m_textures.push_back(Texture::CreateBookshelf(64, 64));
    // Mantis Scout (scenario.md §4.1): light-green chitin, yellow vein markings
    m_textures.push_back(Texture::CreateEnemy(
        32, 48,
        Framebuffer::MakeColor(100, 180, 80),   // light-green chitin
        Framebuffer::MakeColor(230, 200, 30)));  // yellow compound eyes / veins
    // Mantis Scout corpse: darkened green with yellow-green slime pool
    m_textures.push_back(Texture::CreateEnemyCorpse(
        32, 24,
        Framebuffer::MakeColor(60, 110, 45),    // dark chitin pieces
        Framebuffer::MakeColor(140, 200, 40)));  // green slime pool
    m_textures.push_back(Texture::CreateEnemyShooter(
        32, 48,
        Framebuffer::MakeColor(42, 94, 76),
        Framebuffer::MakeColor(60, 240, 255)));
    m_textures.push_back(Texture::CreateEnemyCorpse(
        32, 24,
        Framebuffer::MakeColor(34, 70, 58),
        Framebuffer::MakeColor(130, 40, 140)));
    m_textures.push_back(Texture::CreateEnemyTank(
        48, 54,
        Framebuffer::MakeColor(84, 88, 96),
        Framebuffer::MakeColor(255, 110, 20)));
    m_textures.push_back(Texture::CreateEnemyCorpse(
        48, 28,
        Framebuffer::MakeColor(60, 62, 68),
        Framebuffer::MakeColor(140, 16, 20)));
    m_textures.push_back(Texture::CreateProjectile(
        16, 16,
        Framebuffer::MakeColor(255, 255, 200),
        Framebuffer::MakeColor(255, 120, 10)));
    m_textures.push_back(Texture::CreateMedkit(
        32, 32,
        Framebuffer::MakeColor(232, 224, 208),
        Framebuffer::MakeColor(202, 38, 36)));
    m_textures.push_back(Texture::CreateAmmoBox(
        32, 32,
        Framebuffer::MakeColor(56, 80, 136),
        Framebuffer::MakeColor(222, 190, 62)));
    m_textures.push_back(Texture::CreateExitGate(
        32, 64,
        Framebuffer::MakeColor(48, 170, 90),
        Framebuffer::MakeColor(160, 255, 190)));
    m_textures.push_back(Texture::CreateKey(24, 40,
        Framebuffer::MakeColor(220, 52, 40)));   // KeyRed
    m_textures.push_back(Texture::CreateKey(24, 40,
        Framebuffer::MakeColor(52, 120, 220)));  // KeyBlue
    m_textures.push_back(Texture::CreateKey(24, 40,
        Framebuffer::MakeColor(210, 185, 30)));  // KeyYellow

    // The first file-backed asset proves the loading path. If it is missing or
    // malformed, keep the procedural wall so the game remains runnable.
    const std::string wallPath = m_assetDirectory + "/textures/wall.ppm";
    if (std::optional<Texture> wall = Texture::LoadPpm(wallPath)) {
        m_textures[static_cast<size_t>(TextureId::Wall)] = std::move(*wall);
        Logger::Info("Loaded texture: " + wallPath);
    } else if (std::filesystem::exists(wallPath)) {
        Logger::Error("Could not read texture; using procedural fallback: " + wallPath);
    } else {
        Logger::Info("Texture is absent; using procedural fallback: " + wallPath);
    }
}

const Texture& AssetManager::GetTexture(TextureId id) const {
    return m_textures[static_cast<size_t>(id)];
}
