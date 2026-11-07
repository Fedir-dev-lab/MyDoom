#include "DebugOverlay.h"

#include <array>
#include <cctype>
#include <cmath>
#include <numbers>
#include <cstdio>

#include "renderer/Framebuffer.h"

namespace {

constexpr int kGlyphWidth = 3;
constexpr int kGlyphHeight = 5;
constexpr int kGlyphScale = 2;
constexpr int kCharacterSpacing = 2;

using Glyph = std::array<uint8_t, kGlyphHeight>;

Glyph GetGlyph(char character) {
    const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    switch (c) {
        case '0': return {0b111, 0b101, 0b101, 0b101, 0b111};
        case '1': return {0b010, 0b110, 0b010, 0b010, 0b111};
        case '2': return {0b111, 0b001, 0b111, 0b100, 0b111};
        case '3': return {0b111, 0b001, 0b111, 0b001, 0b111};
        case '4': return {0b101, 0b101, 0b111, 0b001, 0b001};
        case '5': return {0b111, 0b100, 0b111, 0b001, 0b111};
        case '6': return {0b111, 0b100, 0b111, 0b101, 0b111};
        case '7': return {0b111, 0b001, 0b010, 0b010, 0b010};
        case '8': return {0b111, 0b101, 0b111, 0b101, 0b111};
        case '9': return {0b111, 0b101, 0b111, 0b001, 0b111};
        case 'A': return {0b010, 0b101, 0b111, 0b101, 0b101};
        case 'B': return {0b110, 0b101, 0b110, 0b101, 0b110};
        case 'C': return {0b111, 0b100, 0b100, 0b100, 0b111};
        case 'D': return {0b110, 0b101, 0b101, 0b101, 0b110};
        case 'E': return {0b111, 0b100, 0b110, 0b100, 0b111};
        case 'F': return {0b111, 0b100, 0b110, 0b100, 0b100};
        case 'G': return {0b011, 0b100, 0b101, 0b101, 0b011};
        case 'H': return {0b101, 0b101, 0b111, 0b101, 0b101};
        case 'I': return {0b111, 0b010, 0b010, 0b010, 0b111};
        case 'J': return {0b001, 0b001, 0b001, 0b101, 0b111};
        case 'K': return {0b101, 0b110, 0b100, 0b110, 0b101};
        case 'L': return {0b100, 0b100, 0b100, 0b100, 0b111};
        case 'M': return {0b101, 0b111, 0b111, 0b101, 0b101};
        case 'N': return {0b101, 0b111, 0b111, 0b111, 0b101};
        case 'O': return {0b111, 0b101, 0b101, 0b101, 0b111};
        case 'P': return {0b111, 0b101, 0b111, 0b100, 0b100};
        case 'Q': return {0b111, 0b101, 0b101, 0b110, 0b011};
        case 'R': return {0b110, 0b101, 0b110, 0b101, 0b101};
        case 'S': return {0b111, 0b100, 0b111, 0b001, 0b111};
        case 'T': return {0b111, 0b010, 0b010, 0b010, 0b010};
        case 'U': return {0b101, 0b101, 0b101, 0b101, 0b111};
        case 'V': return {0b101, 0b101, 0b101, 0b101, 0b010};
        case 'W': return {0b101, 0b101, 0b101, 0b111, 0b101};
        case 'X': return {0b101, 0b101, 0b010, 0b101, 0b101};
        case 'Y': return {0b101, 0b101, 0b010, 0b010, 0b010};
        case 'Z': return {0b111, 0b001, 0b010, 0b100, 0b111};
        case ':': return {0b000, 0b010, 0b000, 0b010, 0b000};
        case '.': return {0b000, 0b000, 0b000, 0b000, 0b010};
        case '-': return {0b000, 0b000, 0b111, 0b000, 0b000};
        case '/': return {0b001, 0b001, 0b010, 0b100, 0b100};
        case '!': return {0b010, 0b010, 0b010, 0b000, 0b010};
        case ' ': return {0b000, 0b000, 0b000, 0b000, 0b000};
        default:  return {0b000, 0b000, 0b000, 0b000, 0b000};
    }
}

} // namespace

void DebugOverlay::Draw(Framebuffer& framebuffer, float framesPerSecond,
                        const Vector3& playerPosition, float playerAngleRadians,
                        int health, int sectorCount, int wallCount) const {
    char line[64];
    const uint32_t textColor = Framebuffer::MakeColor(220, 240, 220);

    framebuffer.DrawRect(8, 8, 300, 84, Framebuffer::MakeColor(12, 16, 18), true);

    std::snprintf(line, sizeof(line), "FPS: %.1f", framesPerSecond);
    DrawText(framebuffer, 14, 14, line, textColor);

    std::snprintf(line, sizeof(line), "POS: %.2f %.2f %.2f",
                  playerPosition.x, playerPosition.y, playerPosition.z);
    DrawText(framebuffer, 14, 28, line, textColor);

    const float angleDegrees = playerAngleRadians * 180.0f / std::numbers::pi_v<float>;
    std::snprintf(line, sizeof(line), "ANGLE: %.1f", angleDegrees);
    DrawText(framebuffer, 14, 42, line, textColor);

    std::snprintf(line, sizeof(line), "HEALTH: %d", health);
    DrawText(framebuffer, 14, 56, line, textColor);

    std::snprintf(line, sizeof(line), "SECTORS: %d WALLS: %d", sectorCount, wallCount);
    DrawText(framebuffer, 14, 70, line, textColor);
}

void DebugOverlay::DrawRestartPrompt(Framebuffer& framebuffer) const {
    constexpr int kPanelWidth = 144;
    constexpr int kPanelHeight = 34;
    const int x = framebuffer.Width() / 2 - kPanelWidth / 2;
    const int y = framebuffer.Height() / 2 - kPanelHeight / 2;

    framebuffer.DrawRect(x, y, kPanelWidth, kPanelHeight,
                         Framebuffer::MakeColor(26, 12, 14), true);
    DrawText(framebuffer, x + 16, y + 12, "PRESS R", Framebuffer::MakeColor(255, 208, 100));
}

void DebugOverlay::DrawLevelComplete(Framebuffer& framebuffer) const {
    constexpr int kPanelWidth = 160;
    constexpr int kPanelHeight = 48;
    const int x = framebuffer.Width() / 2 - kPanelWidth / 2;
    const int y = framebuffer.Height() / 2 - kPanelHeight / 2;

    framebuffer.DrawRect(x, y, kPanelWidth, kPanelHeight,
                         Framebuffer::MakeColor(12, 26, 18), true);
    framebuffer.DrawRect(x, y, kPanelWidth, kPanelHeight,
                         Framebuffer::MakeColor(90, 190, 110), false);
    DrawText(framebuffer, x + 48, y + 12, "COMPLETE",
             Framebuffer::MakeColor(170, 255, 180));
    DrawText(framebuffer, x + 62, y + 28, "ENTER",
             Framebuffer::MakeColor(255, 224, 120));
}

void DebugOverlay::DrawText(Framebuffer& framebuffer, int x, int y,
                            const char* text, uint32_t color) const {
    const int characterWidth = kGlyphWidth * kGlyphScale + kCharacterSpacing;
    for (int i = 0; text[i] != '\0'; ++i) {
        DrawCharacter(framebuffer, x + i * characterWidth, y, text[i], color);
    }
}

void DebugOverlay::DrawCharacter(Framebuffer& framebuffer, int x, int y,
                                 char character, uint32_t color) const {
    const Glyph glyph = GetGlyph(character);

    for (int row = 0; row < kGlyphHeight; ++row) {
        for (int column = 0; column < kGlyphWidth; ++column) {
            if ((glyph[row] & (1 << (kGlyphWidth - 1 - column))) != 0) {
                framebuffer.DrawRect(x + column * kGlyphScale,
                                     y + row * kGlyphScale,
                                     kGlyphScale, kGlyphScale, color, true);
            }
        }
    }
}
