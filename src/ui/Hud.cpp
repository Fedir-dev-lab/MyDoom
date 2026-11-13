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
    const int W = framebuffer.Width();
    const int H = framebuffer.Height();

    // UI scale relative to the reference render width (640). At 320 → 0.5×,
    // at 640 → 1×, at 1280 → 2×. All pixel coordinates multiply by S so the
    // UI occupies the same screen-fraction at every resolution.
    const float Sf = static_cast<float>(W) / 640.0f;
    // Integer scale — round down but keep at least 1 so nothing vanishes.
    const auto S = [&](int v) { return std::max(1, static_cast<int>(v * Sf)); };

    const uint32_t outlineColor  = Framebuffer::MakeColor(18,  18,  18);
    const uint32_t emptyColor    = Framebuffer::MakeColor(82,  22,  22);
    const uint32_t healthColor   = Framebuffer::MakeColor(198, 48,  42);
    const uint32_t crosshairColor= Framebuffer::MakeColor(238, 238, 220);

    // ---- Health bar -------------------------------------------------------
    const int kBarW  = S(180);
    const int kBarH  = S(16);
    const int barX   = S(16);
    const int barY   = H - kBarH - S(16);
    const int filled = std::clamp(health, 0, maximumHealth) * (kBarW - S(4)) / maximumHealth;

    framebuffer.DrawRect(barX - S(2), barY - S(2), kBarW + S(4), kBarH + S(4), outlineColor, true);
    framebuffer.DrawRect(barX, barY, kBarW, kBarH, emptyColor, true);
    framebuffer.DrawRect(barX + S(2), barY + S(2), filled, kBarH - S(4), healthColor, true);

    // ---- Ammo + weapon name -----------------------------------------------
    DrawAmmoCounter(framebuffer, W - S(100), H - S(34), magazineAmmo, reserveAmmo, Sf);

    if (weaponName) {
        DrawString(framebuffer, W - S(100), H - S(52), weaponName,
                   Framebuffer::MakeColor(120, 190, 255), std::max(1, static_cast<int>(2 * Sf)));
    }

    // ---- Crosshair --------------------------------------------------------
    const int cx = W / 2;
    const int cy = H / 2;
    const int ch = S(8);  // arm length
    const int cg = S(3);  // gap
    framebuffer.DrawLine(cx - ch, cy, cx - cg, cy, crosshairColor);
    framebuffer.DrawLine(cx + cg, cy, cx + ch, cy, crosshairColor);
    framebuffer.DrawLine(cx, cy - ch, cx, cy - cg, crosshairColor);
    framebuffer.DrawLine(cx, cy + cg, cx, cy + ch, crosshairColor);

    // ---- Weapon sprite ----------------------------------------------------
    const uint32_t gunDark  = Framebuffer::MakeColor(46,  50,  56);
    const uint32_t gunLight = Framebuffer::MakeColor(94,  100, 110);
    const uint32_t wood     = Framebuffer::MakeColor(120, 72,  40);

    if (activeWeaponIndex == 1) {
        const int gW = S(140), gH = S(52);
        const int gX = cx - gW / 2;
        const int gY = H - gH;
        framebuffer.DrawRect(gX + S(15), gY + S(20), gW - S(30), gH - S(20), gunDark, true);
        framebuffer.DrawRect(cx - S(24), gY + S(14), S(48), S(16), wood, true);
        framebuffer.DrawRect(cx - S(24), gY + S(18), S(48), S(2),  Framebuffer::MakeColor(70,40,20), true);
        framebuffer.DrawRect(cx - S(18), gY - S(14), S(16), S(30), gunLight, true);
        framebuffer.DrawRect(cx + S(2),  gY - S(14), S(16), S(30), gunLight, true);
        framebuffer.DrawRect(cx - S(14), gY - S(18), S(8),  S(6),  Framebuffer::MakeColor(30,30,30), true);
        framebuffer.DrawRect(cx + S(6),  gY - S(18), S(8),  S(6),  Framebuffer::MakeColor(30,30,30), true);
        if (weaponFiring) {
            const uint32_t fl = Framebuffer::MakeColor(255, 220, 90);
            const uint32_t fc = Framebuffer::MakeColor(255, 255, 200);
            framebuffer.DrawLine(cx-S(28), gY-S(20), cx-S(10), gY-S(55), fl);
            framebuffer.DrawLine(cx+S(28), gY-S(20), cx+S(10), gY-S(55), fl);
            framebuffer.DrawLine(cx-S(10), gY-S(22), cx-S(10), gY-S(60), fc);
            framebuffer.DrawLine(cx+S(10), gY-S(22), cx+S(10), gY-S(60), fc);
            framebuffer.DrawLine(cx-S(35), gY-S(35), cx+S(35), gY-S(35), fl);
        }
    } else if (activeWeaponIndex == 2) {
        const int gW = S(120), gH = S(50);
        const int gX = cx - gW / 2;
        const int gY = H - gH;
        framebuffer.DrawRect(gX + S(10), gY + S(18), gW - S(20), gH - S(18), gunDark, true);
        framebuffer.DrawRect(gX + S(8),  gY + S(22), S(18), S(14), Framebuffer::MakeColor(210,180,50), true);
        framebuffer.DrawRect(cx - S(20), gY - S(6),  S(40), S(26), gunLight, true);
        framebuffer.DrawRect(cx - S(14), gY - S(22), S(8),  S(18), Framebuffer::MakeColor(35,38,42), true);
        framebuffer.DrawRect(cx + S(6),  gY - S(22), S(8),  S(18), Framebuffer::MakeColor(35,38,42), true);
        framebuffer.DrawRect(cx - S(4),  gY - S(26), S(8),  S(22), Framebuffer::MakeColor(25,28,32), true);
        if (weaponFiring) {
            const uint32_t fl = Framebuffer::MakeColor(255, 200, 70);
            framebuffer.DrawLine(cx-S(14), gY-S(24), cx-S(6), gY-S(52), fl);
            framebuffer.DrawLine(cx+S(14), gY-S(24), cx+S(6), gY-S(52), fl);
            framebuffer.DrawLine(cx, gY-S(26), cx, gY-S(56), Framebuffer::MakeColor(255,255,180));
        }
    } else {
        const int gW = S(110), gH = S(48);
        const int gX = cx - gW / 2;
        const int gY = H - gH;
        framebuffer.DrawRect(gX,        gY + S(16), gW,     gH - S(16), gunDark,  true);
        framebuffer.DrawRect(cx - S(16), gY,         S(32),  S(28),      gunLight, true);
        framebuffer.DrawRect(cx - S(8),  gY - S(16), S(16),  S(20),      gunLight, true);
        if (weaponFiring) {
            const uint32_t fl = Framebuffer::MakeColor(255, 218, 96);
            framebuffer.DrawLine(cx-S(16), gY-S(42), cx, gY-S(16), fl);
            framebuffer.DrawLine(cx+S(16), gY-S(42), cx, gY-S(16), fl);
            framebuffer.DrawLine(cx,       gY-S(48), cx, gY-S(18), fl);
        }
    }

    // ---- Reload bar -------------------------------------------------------
    if (weaponReloading) {
        framebuffer.DrawRect(cx - S(34), H - S(64), S(68), S(5),
                             Framebuffer::MakeColor(244, 182, 62), true);
    }

    // ---- Keys + message ---------------------------------------------------
    DrawKeys(framebuffer, barX, barY - S(20), levelState, Sf);
    DrawMessageBanner(framebuffer, Sf);
}

void Hud::DrawAmmoCounter(Framebuffer& framebuffer, int x, int y,
                          int magazineAmmo, int reserveAmmo, float scale) const {
    const std::string text = std::to_string(magazineAmmo) + "/" + std::to_string(reserveAmmo);
    const uint32_t color = Framebuffer::MakeColor(102, 180, 250);
    const int glyphScale = std::max(1, static_cast<int>(2 * scale));
    const int advance    = 3 * glyphScale + glyphScale; // glyph width + gap
    for (size_t i = 0; i < text.size(); ++i) {
        DrawGlyph(framebuffer, x + static_cast<int>(i) * advance, y,
                  text[i], color, glyphScale);
    }
}

void Hud::DrawKeys(Framebuffer& framebuffer, int x, int y,
                   const LevelState* levelState, float scale) const {
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

    const auto S = [&](int v) { return std::max(1, static_cast<int>(v * scale)); };
    const int kBadgeW = S(14);
    const int kBadgeH = S(16);
    const int kSpacing = S(4);
    const int glyphScale = std::max(1, static_cast<int>(2 * scale));

    for (int i = 0; i < 3; ++i) {
        const int bx = x + i * (kBadgeW + kSpacing);
        const bool owned = levelState->HasKey(badges[i].colour);
        const uint32_t bg = owned ? badges[i].activeColor : badges[i].dimColor;
        const uint32_t fg = owned ? badges[i].textColor : Framebuffer::MakeColor(70, 70, 70);

        framebuffer.DrawRect(bx - S(1), y - S(1), kBadgeW + S(2), kBadgeH + S(2),
                             Framebuffer::MakeColor(18, 18, 18), true);
        framebuffer.DrawRect(bx, y, kBadgeW, kBadgeH, bg, true);
        DrawGlyph(framebuffer, bx + S(4), y + S(3), badges[i].letter, fg, glyphScale);
    }
}

void Hud::DrawMessageBanner(Framebuffer& framebuffer, float scale) const {
    if (m_messageTimer <= 0.0f || m_message.empty()) { return; }

    const int kScale = std::max(1, static_cast<int>(2 * scale));
    const int kCharWidth = 3 * kScale + kScale;
    const int textWidth  = static_cast<int>(m_message.size()) * kCharWidth - kScale;
    const int textX      = std::max(8, (framebuffer.Width() - textWidth) / 2);
    const int textY      = static_cast<int>(28 * scale);

    const int padX = static_cast<int>(8 * scale);
    const int padY = static_cast<int>(4 * scale);
    const int boxX = textX - padX;
    const int boxY = textY - padY;
    const int boxW = textWidth + padX * 2;
    const int boxH = 5 * kScale + padY * 2;

    framebuffer.DrawRect(boxX, boxY, boxW, boxH, Framebuffer::MakeColor(18, 18, 22), true);
    framebuffer.DrawRect(boxX - 1, boxY - 1, boxW + 2, boxH + 2,
                         Framebuffer::MakeColor(120, 110, 95), false);
    DrawString(framebuffer, textX, textY, m_message,
               Framebuffer::MakeColor(255, 224, 70), kScale);
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
