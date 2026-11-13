#pragma once

#include <cstdint>
#include <string>

class Framebuffer;
class LevelState;

class Hud {
public:
    void ShowMessage(std::string message, float duration = 2.5f);
    void Update(float deltaSeconds);

    void Draw(Framebuffer& framebuffer, int health, int maximumHealth,
              int magazineAmmo, int reserveAmmo,
              bool weaponFiring, bool weaponReloading,
              int activeWeaponIndex = 0,
              const char* weaponName = nullptr,
              const LevelState* levelState = nullptr) const;

private:
    void DrawAmmoCounter(Framebuffer& framebuffer, int x, int y,
                         int magazineAmmo, int reserveAmmo, float scale) const;
    void DrawKeys(Framebuffer& framebuffer, int x, int y,
                  const LevelState* levelState, float scale) const;
    void DrawMessageBanner(Framebuffer& framebuffer, float scale) const;
    void DrawGlyph(Framebuffer& framebuffer, int x, int y,
                   char glyph, uint32_t color, int scale = 2) const;
    void DrawString(Framebuffer& framebuffer, int x, int y,
                    const std::string& text, uint32_t color, int scale = 2) const;

    std::string m_message;
    float       m_messageTimer = 0.0f;
};
