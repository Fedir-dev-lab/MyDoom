#include "Hud.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

#include "renderer/Framebuffer.h"
#include "world/LevelState.h"

void Hud::ShowMessage(std::string message, float duration) {
    m_message = std::move(message);
    m_messageTimer = duration;
}

void Hud::Update(float deltaSeconds) {
    if (m_messageTimer > 0.0f) {
        m_messageTimer = std::max(0.0f, m_messageTimer - deltaSeconds);
    }
}

void Hud::Draw(Framebuffer& framebuffer, int health, int maximumHealth,
               int magazineAmmo, int reserveAmmo,
               bool weaponFiring, bool weaponReloading,
               int activeWeaponIndex,
               const char* weaponName,
               const LevelState* levelState) const {
    const int width = framebuffer.Width();
    const int height = framebuffer.Height();
    const uint32_t outlineColor = Framebuffer::MakeColor(18, 18, 18);
    const uint32_t emptyColor = Framebuffer::MakeColor(82, 22, 22);
    const uint32_t healthColor = Framebuffer::MakeColor(198, 48, 42);
    const uint32_t crosshairColor = Framebuffer::MakeColor(238, 238, 220);

    constexpr int kBarWidth = 180;
    constexpr int kBarHeight = 16;
    const int barX = 16;
    const int barY = height - kBarHeight - 16;
    const int filledWidth = std::clamp(health, 0, maximumHealth) * (kBarWidth - 4) / maximumHealth;

    framebuffer.DrawRect(barX - 2, barY - 2, kBarWidth + 4, kBarHeight + 4, outlineColor, true);
    framebuffer.DrawRect(barX, barY, kBarWidth, kBarHeight, emptyColor, true);
    framebuffer.DrawRect(barX + 2, barY + 2, filledWidth, kBarHeight - 4, healthColor, true);

    DrawAmmoCounter(framebuffer, width - 100, height - 34, magazineAmmo, reserveAmmo);

    if (weaponName) {
        DrawString(framebuffer, width - 100, height - 52, weaponName, Framebuffer::MakeColor(120, 190, 255), 2);
    }

    const int centerX = width / 2;
    const int centerY = height / 2;
    framebuffer.DrawLine(centerX - 8, centerY, centerX - 3, centerY, crosshairColor);
    framebuffer.DrawLine(centerX + 3, centerY, centerX + 8, centerY, crosshairColor);
    framebuffer.DrawLine(centerX, centerY - 8, centerX, centerY - 3, crosshairColor);
    framebuffer.DrawLine(centerX, centerY + 3, centerX, centerY + 8, crosshairColor);

    const uint32_t gunDarkColor = Framebuffer::MakeColor(46, 50, 56);
    const uint32_t gunLightColor = Framebuffer::MakeColor(94, 100, 110);
    const uint32_t woodColor = Framebuffer::MakeColor(120, 72, 40);

    if (activeWeaponIndex == 1) {
        // Shotgun: double barrels with wood pump
        const int gunWidth = 140;
        const int gunHeight = 52;
        const int gunX = centerX - gunWidth / 2;
        const int gunY = height - gunHeight;
        framebuffer.DrawRect(gunX + 15, gunY + 20, gunWidth - 30, gunHeight - 20, gunDarkColor, true);
        framebuffer.DrawRect(centerX - 24, gunY + 14, 48, 16, woodColor, true);
        framebuffer.DrawRect(centerX - 24, gunY + 18, 48, 2, Framebuffer::MakeColor(70, 40, 20), true);
        framebuffer.DrawRect(centerX - 18, gunY - 14, 16, 30, gunLightColor, true);
        framebuffer.DrawRect(centerX + 2,  gunY - 14, 16, 30, gunLightColor, true);
        framebuffer.DrawRect(centerX - 14, gunY - 18, 8, 6, Framebuffer::MakeColor(30, 30, 30), true);
        framebuffer.DrawRect(centerX + 6,  gunY - 18, 8, 6, Framebuffer::MakeColor(30, 30, 30), true);

        if (weaponFiring) {
            const uint32_t flashColor = Framebuffer::MakeColor(255, 220, 90);
            const uint32_t flashCore = Framebuffer::MakeColor(255, 255, 200);
            framebuffer.DrawLine(centerX - 28, gunY - 20, centerX - 10, gunY - 55, flashColor);
            framebuffer.DrawLine(centerX + 28, gunY - 20, centerX + 10, gunY - 55, flashColor);
            framebuffer.DrawLine(centerX - 10, gunY - 22, centerX - 10, gunY - 60, flashCore);
            framebuffer.DrawLine(centerX + 10, gunY - 22, centerX + 10, gunY - 60, flashCore);
            framebuffer.DrawLine(centerX - 35, gunY - 35, centerX + 35, gunY - 35, flashColor);
        }
    } else if (activeWeaponIndex == 2) {
        // Chaingun: rotating multibarrel
        const int gunWidth = 120;
        const int gunHeight = 50;
        const int gunX = centerX - gunWidth / 2;
        const int gunY = height - gunHeight;
        framebuffer.DrawRect(gunX + 10, gunY + 18, gunWidth - 20, gunHeight - 18, gunDarkColor, true);
        framebuffer.DrawRect(gunX + 8, gunY + 22, 18, 14, Framebuffer::MakeColor(210, 180, 50), true);
        framebuffer.DrawRect(centerX - 20, gunY - 6, 40, 26, gunLightColor, true);
        framebuffer.DrawRect(centerX - 14, gunY - 22, 8, 18, Framebuffer::MakeColor(35, 38, 42), true);
        framebuffer.DrawRect(centerX + 6,  gunY - 22, 8, 18, Framebuffer::MakeColor(35, 38, 42), true);
        framebuffer.DrawRect(centerX - 4,  gunY - 26, 8, 22, Framebuffer::MakeColor(25, 28, 32), true);

        if (weaponFiring) {
            const uint32_t flashColor = Framebuffer::MakeColor(255, 200, 70);
            framebuffer.DrawLine(centerX - 14, gunY - 24, centerX - 6, gunY - 52, flashColor);
            framebuffer.DrawLine(centerX + 14, gunY - 24, centerX + 6, gunY - 52, flashColor);
            framebuffer.DrawLine(centerX, gunY - 26, centerX, gunY - 56, Framebuffer::MakeColor(255, 255, 180));
        }
    } else {
        // Pistol: compact slide
        const int gunWidth = 110;
        const int gunHeight = 48;
        const int gunX = centerX - gunWidth / 2;
        const int gunY = height - gunHeight;
        framebuffer.DrawRect(gunX, gunY + 16, gunWidth, gunHeight - 16, gunDarkColor, true);
        framebuffer.DrawRect(centerX - 16, gunY, 32, 28, gunLightColor, true);
        framebuffer.DrawRect(centerX - 8, gunY - 16, 16, 20, gunLightColor, true);

        if (weaponFiring) {
            const uint32_t flashColor = Framebuffer::MakeColor(255, 218, 96);
            framebuffer.DrawLine(centerX - 16, gunY - 42, centerX, gunY - 16, flashColor);
            framebuffer.DrawLine(centerX + 16, gunY - 42, centerX, gunY - 16, flashColor);
            framebuffer.DrawLine(centerX, gunY - 48, centerX, gunY - 18, flashColor);
        }
    }

    if (weaponReloading) {
        const uint32_t reloadColor = Framebuffer::MakeColor(244, 182, 62);
        framebuffer.DrawRect(centerX - 34, height - 64, 68, 5, reloadColor, true);
    }

    DrawKeys(framebuffer, barX, barY - 20, levelState);
    DrawMessageBanner(framebuffer);
}

void Hud::DrawAmmoCounter(Framebuffer& framebuffer, int x, int y,
                          int magazineAmmo, int reserveAmmo) const {
    const std::string text = std::to_string(magazineAmmo) + "/" + std::to_string(reserveAmmo);
    const uint32_t color = Framebuffer::MakeColor(102, 180, 250);

    for (size_t i = 0; i < text.size(); ++i) {
        DrawGlyph(framebuffer, x + static_cast<int>(i) * 10, y, text[i], color);
    }
}

void Hud::DrawKeys(Framebuffer& framebuffer, int x, int y,
                   const LevelState* levelState) const {
    if (!levelState) { return; }

    struct KeyBadge {
        KeyColour colour;
        char      letter;
        uint32_t  activeColor;
        uint32_t  dimColor;
        uint32_t  textColor;
    };

    const KeyBadge badges[3] = {
        {KeyColour::Red,    'R', Framebuffer::MakeColor(200, 36, 36),
                                 Framebuffer::MakeColor(45, 18, 18),
                                 Framebuffer::MakeColor(255, 255, 255)},
        {KeyColour::Blue,   'B', Framebuffer::MakeColor(36, 80, 210),
                                 Framebuffer::MakeColor(18, 25, 55),
                                 Framebuffer::MakeColor(255, 255, 255)},
        {KeyColour::Yellow, 'Y', Framebuffer::MakeColor(220, 195, 30),
                                 Framebuffer::MakeColor(50, 45, 16),
                                 Framebuffer::MakeColor(20, 20, 20)},
    };

    constexpr int kBadgeW = 14;
    constexpr int kBadgeH = 16;
    constexpr int kSpacing = 4;

    for (int i = 0; i < 3; ++i) {
        const int bx = x + i * (kBadgeW + kSpacing);
        const bool owned = levelState->HasKey(badges[i].colour);
        const uint32_t bg = owned ? badges[i].activeColor : badges[i].dimColor;
        const uint32_t fg = owned ? badges[i].textColor : Framebuffer::MakeColor(70, 70, 70);

        framebuffer.DrawRect(bx - 1, y - 1, kBadgeW + 2, kBadgeH + 2,
                             Framebuffer::MakeColor(18, 18, 18), true);
        framebuffer.DrawRect(bx, y, kBadgeW, kBadgeH, bg, true);
        DrawGlyph(framebuffer, bx + 4, y + 3, badges[i].letter, fg, 2);
    }
}

void Hud::DrawMessageBanner(Framebuffer& framebuffer) const {
    if (m_messageTimer <= 0.0f || m_message.empty()) { return; }

    constexpr int kScale = 2;
    constexpr int kCharWidth = 3 * kScale + kScale; // 8 px
    const int textWidth = static_cast<int>(m_message.size()) * kCharWidth - kScale;
    const int textX = std::max(8, (framebuffer.Width() - textWidth) / 2);
    constexpr int textY = 28;

    const int padX = 8;
    const int padY = 4;
    const int boxX = textX - padX;
    const int boxY = textY - padY;
    const int boxW = textWidth + padX * 2;
    const int boxH = 5 * kScale + padY * 2;

    framebuffer.DrawRect(boxX, boxY, boxW, boxH, Framebuffer::MakeColor(18, 18, 22), true);
    framebuffer.DrawRect(boxX - 1, boxY - 1, boxW + 2, boxH + 2, Framebuffer::MakeColor(120, 110, 95), false);

    DrawString(framebuffer, textX, textY, m_message, Framebuffer::MakeColor(255, 224, 70), kScale);
}

void Hud::DrawString(Framebuffer& framebuffer, int x, int y,
                     const std::string& text, uint32_t color, int scale) const {
    const int advance = 3 * scale + scale;
    for (size_t i = 0; i < text.size(); ++i) {
        DrawGlyph(framebuffer, x + static_cast<int>(i) * advance, y, text[i], color, scale);
    }
}

void Hud::DrawGlyph(Framebuffer& framebuffer, int x, int y, char glyph, uint32_t color, int scale) const {
    using Glyph = std::array<uint8_t, 5>;
    Glyph rows{};

    const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(glyph)));
    switch (c) {
        case '0': rows = {0b111, 0b101, 0b101, 0b101, 0b111}; break;
        case '1': rows = {0b010, 0b110, 0b010, 0b010, 0b111}; break;
        case '2': rows = {0b111, 0b001, 0b111, 0b100, 0b111}; break;
        case '3': rows = {0b111, 0b001, 0b111, 0b001, 0b111}; break;
        case '4': rows = {0b101, 0b101, 0b111, 0b001, 0b001}; break;
        case '5': rows = {0b111, 0b100, 0b111, 0b001, 0b111}; break;
        case '6': rows = {0b111, 0b100, 0b111, 0b101, 0b111}; break;
        case '7': rows = {0b111, 0b001, 0b010, 0b010, 0b010}; break;
        case '8': rows = {0b111, 0b101, 0b111, 0b101, 0b111}; break;
        case '9': rows = {0b111, 0b101, 0b111, 0b001, 0b111}; break;
        case '/': rows = {0b001, 0b001, 0b010, 0b100, 0b100}; break;
        case ':': rows = {0b000, 0b010, 0b000, 0b010, 0b000}; break;
        case '.': rows = {0b000, 0b000, 0b000, 0b000, 0b010}; break;
        case '-': rows = {0b000, 0b000, 0b111, 0b000, 0b000}; break;
        case '!': rows = {0b010, 0b010, 0b010, 0b000, 0b010}; break;
        case ' ': rows = {0b000, 0b000, 0b000, 0b000, 0b000}; break;
        case 'A': rows = {0b010, 0b101, 0b111, 0b101, 0b101}; break;
        case 'B': rows = {0b110, 0b101, 0b110, 0b101, 0b110}; break;
        case 'C': rows = {0b111, 0b100, 0b100, 0b100, 0b111}; break;
        case 'D': rows = {0b110, 0b101, 0b101, 0b101, 0b110}; break;
        case 'E': rows = {0b111, 0b100, 0b110, 0b100, 0b111}; break;
        case 'F': rows = {0b111, 0b100, 0b110, 0b100, 0b100}; break;
        case 'G': rows = {0b011, 0b100, 0b101, 0b101, 0b011}; break;
        case 'H': rows = {0b101, 0b101, 0b111, 0b101, 0b101}; break;
        case 'I': rows = {0b111, 0b010, 0b010, 0b010, 0b111}; break;
        case 'J': rows = {0b001, 0b001, 0b001, 0b101, 0b111}; break;
        case 'K': rows = {0b101, 0b110, 0b100, 0b110, 0b101}; break;
        case 'L': rows = {0b100, 0b100, 0b100, 0b100, 0b111}; break;
        case 'M': rows = {0b101, 0b111, 0b111, 0b101, 0b101}; break;
        case 'N': rows = {0b101, 0b111, 0b111, 0b111, 0b101}; break;
        case 'O': rows = {0b111, 0b101, 0b101, 0b101, 0b111}; break;
        case 'P': rows = {0b111, 0b101, 0b111, 0b100, 0b100}; break;
        case 'Q': rows = {0b111, 0b101, 0b101, 0b110, 0b011}; break;
        case 'R': rows = {0b110, 0b101, 0b110, 0b101, 0b101}; break;
        case 'S': rows = {0b111, 0b100, 0b111, 0b001, 0b111}; break;
        case 'T': rows = {0b111, 0b010, 0b010, 0b010, 0b010}; break;
        case 'U': rows = {0b101, 0b101, 0b101, 0b101, 0b111}; break;
        case 'V': rows = {0b101, 0b101, 0b101, 0b101, 0b010}; break;
        case 'W': rows = {0b101, 0b101, 0b101, 0b111, 0b101}; break;
        case 'X': rows = {0b101, 0b101, 0b010, 0b101, 0b101}; break;
        case 'Y': rows = {0b101, 0b101, 0b010, 0b010, 0b010}; break;
        case 'Z': rows = {0b111, 0b001, 0b010, 0b100, 0b111}; break;
        default: return;
    }

    for (int row = 0; row < 5; ++row) {
        for (int column = 0; column < 3; ++column) {
            if ((rows[row] & (1 << (2 - column))) != 0) {
                framebuffer.DrawRect(x + column * scale, y + row * scale,
                                     scale, scale, color, true);
            }
        }
    }
}
