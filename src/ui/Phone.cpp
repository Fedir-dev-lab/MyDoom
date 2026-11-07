#include "Phone.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "renderer/Framebuffer.h"

// ---------------------------------------------------------------------------
// Pixel font — same 3×5 bitmap as DebugOverlay, but supports digits, ':', 'M', 'A', 'P'
// and a few extra letters needed for clock labels.
// ---------------------------------------------------------------------------
namespace {

using Glyph = std::array<uint8_t, 5>;

Glyph GetGlyph(char c) {
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
        case ':': return {0b000, 0b010, 0b000, 0b010, 0b000};
        case 'A': return {0b010, 0b101, 0b111, 0b101, 0b101};
        case 'D': return {0b110, 0b101, 0b101, 0b101, 0b110};
        case 'E': return {0b111, 0b100, 0b110, 0b100, 0b111};
        case 'F': return {0b111, 0b100, 0b110, 0b100, 0b100};
        case 'I': return {0b111, 0b010, 0b010, 0b010, 0b111};
        case 'L': return {0b100, 0b100, 0b100, 0b100, 0b111};
        case 'M': return {0b101, 0b111, 0b101, 0b101, 0b101};
        case 'N': return {0b101, 0b111, 0b111, 0b101, 0b101};
        case 'O': return {0b111, 0b101, 0b101, 0b101, 0b111};
        case 'P': return {0b111, 0b101, 0b111, 0b100, 0b100};
        case 'R': return {0b110, 0b101, 0b110, 0b101, 0b101};
        case 'S': return {0b111, 0b100, 0b111, 0b001, 0b111};
        case 'T': return {0b111, 0b010, 0b010, 0b010, 0b010};
        case 'U': return {0b101, 0b101, 0b101, 0b101, 0b111};
        case 'Y': return {0b101, 0b101, 0b010, 0b010, 0b010};
        default:  return {0b000, 0b000, 0b000, 0b000, 0b000};
    }
}

// Smooth easing: ease-out quad so the phone decelerates into place.
float EaseOut(float t) {
    return 1.0f - (1.0f - t) * (1.0f - t);
}

} // namespace

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------
void Phone::Update(float deltaSeconds) {
    const float target = m_open ? 1.0f : 0.0f;
    if (m_animProgress < target) {
        m_animProgress += kAnimSpeed * deltaSeconds;
        if (m_animProgress > 1.0f) { m_animProgress = 1.0f; }
    } else if (m_animProgress > target) {
        m_animProgress -= kAnimSpeed * deltaSeconds;
        if (m_animProgress < 0.0f) { m_animProgress = 0.0f; }
    }
}

void Phone::Toggle() {
    m_open = !m_open;
}

// ---------------------------------------------------------------------------
// DrawChar / DrawText
// ---------------------------------------------------------------------------
void Phone::DrawChar(Framebuffer& fb, int x, int y,
                     char c, uint32_t color, int scale) const {
    const Glyph g = GetGlyph(c);
    for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 3; ++col) {
            if (g[row] & (1 << (2 - col))) {
                fb.DrawRect(x + col * scale, y + row * scale, scale, scale, color, true);
            }
        }
    }
}

void Phone::DrawText(Framebuffer& fb, int x, int y,
                     const char* text, uint32_t color, int scale) const {
    const int advance = 3 * scale + scale; // glyph width + 1px gap
    for (int i = 0; text[i] != '\0'; ++i) {
        DrawChar(fb, x + i * advance, y, text[i], color, scale);
    }
}

// ---------------------------------------------------------------------------
// DrawClock — digital HH:MM clock derived from sunWorldAngle.
// One full day = 2π radians, mapped to 0–24 hours, starting at 06:00.
// ---------------------------------------------------------------------------
void Phone::DrawClock(Framebuffer& fb, int phoneX, int phoneY,
                      int phoneW, float sunWorldAngle) const {
    constexpr float kPi = 3.14159265f;
    // Normalize angle to [0, 2π)
    float a = std::fmod(sunWorldAngle, 2.0f * kPi);
    if (a < 0.0f) { a += 2.0f * kPi; }
    // Map [0, 2π) → [6, 30) hours (so 0 rad = 06:00)
    const float totalHours = 6.0f + a / (2.0f * kPi) * 24.0f;
    const int hours   = static_cast<int>(totalHours) % 24;
    const int minutes = static_cast<int>((totalHours - std::floor(totalHours)) * 60.0f);

    // Format "HH:MM"
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", hours, minutes);

    // Clock drawn large (scale 4) centered in the upper portion of the phone screen.
    constexpr int kClockScale = 4;
    const int glyphW  = 3 * kClockScale + kClockScale; // advance per char
    const int textW   = 5 * glyphW - kClockScale;      // 5 chars ("HH:MM")
    const int clockX  = phoneX + phoneW / 2 - textW / 2;
    const int clockY  = phoneY + 28;

    // Shadow
    DrawText(fb, clockX + 1, clockY + 1, buf,
             Framebuffer::MakeColor(20, 20, 30), kClockScale);
    // Main text — bright cyan-white
    DrawText(fb, clockX, clockY, buf,
             Framebuffer::MakeColor(200, 240, 255), kClockScale);

    // Thin separator line below clock
    fb.DrawLine(phoneX + 12, clockY + 5 * kClockScale + 6,
                phoneX + phoneW - 12, clockY + 5 * kClockScale + 6,
                Framebuffer::MakeColor(60, 80, 100));

    // "MAP" label above the minimap area
    const int labelY = clockY + 5 * kClockScale + 12;
    DrawText(fb, phoneX + phoneW / 2 - 7, labelY, "MAP",
             Framebuffer::MakeColor(120, 160, 190), 2);
}

// ---------------------------------------------------------------------------
// DrawMiniMap — draws all walls and a player arrow inside the phone.
// ---------------------------------------------------------------------------
void Phone::DrawMiniMap(Framebuffer& fb, int phoneX, int phoneY,
                        int phoneW, int phoneH,
                        const Vector3& playerPos, float playerAngle,
                        const std::vector<std::pair<Vector2, Vector2>>& walls) const {
    // Map area: a square region in the lower part of the phone screen.
    const int mapMargin = 14;
    const int mapX = phoneX + mapMargin;
    const int mapY = phoneY + phoneH / 2 - 4;          // starts a bit above the midpoint
    const int mapW = phoneW - 2 * mapMargin;
    const int mapH = phoneH - (mapY - phoneY) - mapMargin;

    // Dark background for the map
    fb.DrawRect(mapX, mapY, mapW, mapH,
                Framebuffer::MakeColor(18, 28, 38), true);
    // Border
    fb.DrawRect(mapX, mapY, mapW, mapH,
                Framebuffer::MakeColor(50, 90, 130), false);

    if (walls.empty()) { return; }

    // Find world AABB so we can fit everything in the map area.
    float worldMinX = walls[0].first.x,  worldMaxX = walls[0].first.x;
    float worldMinZ = walls[0].first.y,  worldMaxZ = walls[0].first.y;
    for (const auto& [a, b] : walls) {
        worldMinX = std::min({worldMinX, a.x, b.x});
        worldMaxX = std::max({worldMaxX, a.x, b.x});
        worldMinZ = std::min({worldMinZ, a.y, b.y});
        worldMaxZ = std::max({worldMaxZ, a.y, b.y});
    }
    const float worldW = worldMaxX - worldMinX;
    const float worldD = worldMaxZ - worldMinZ;
    if (worldW < 0.01f || worldD < 0.01f) { return; }

    // Scale to fit with a small padding inside the map rectangle.
    const int pad = 4;
    const float scaleX = static_cast<float>(mapW - 2 * pad) / worldW;
    const float scaleZ = static_cast<float>(mapH - 2 * pad) / worldD;
    const float scale  = std::min(scaleX, scaleZ);

    // Center the map content
    const float offsetX = mapX + pad + (mapW - 2 * pad - worldW * scale) * 0.5f;
    const float offsetZ = mapY + pad + (mapH - 2 * pad - worldD * scale) * 0.5f;

    // Helper: world XZ → screen XY
    auto toScreen = [&](float wx, float wz) -> std::pair<int, int> {
        return {
            static_cast<int>(offsetX + (wx - worldMinX) * scale),
            static_cast<int>(offsetZ + (wz - worldMinZ) * scale)
        };
    };

    // Draw walls
    const uint32_t wallColor = Framebuffer::MakeColor(80, 140, 200);
    for (const auto& [a, b] : walls) {
        auto [ax, ay] = toScreen(a.x, a.y);
        auto [bx, by] = toScreen(b.x, b.y);
        fb.DrawLine(ax, ay, bx, by, wallColor);
    }

    // Draw player — a filled dot with a direction line
    auto [px, pz] = toScreen(playerPos.x, playerPos.z);

    const uint32_t playerColor  = Framebuffer::MakeColor(80, 220, 120);
    const uint32_t arrowColor   = Framebuffer::MakeColor(180, 255, 180);

    // Dot (3×3 square)
    fb.DrawRect(px - 2, pz - 2, 5, 5, playerColor, true);

    // Direction arrow: 7 px long
    constexpr float kArrowLen = 7.0f;
    const int arrowX = static_cast<int>(px + std::sin(playerAngle) * kArrowLen);
    const int arrowZ = static_cast<int>(pz + std::cos(playerAngle) * kArrowLen);
    fb.DrawLine(px, pz, arrowX, arrowZ, arrowColor);
}

// ---------------------------------------------------------------------------
// Draw — main entry: phone body + screen + content
// ---------------------------------------------------------------------------
void Phone::Draw(Framebuffer& framebuffer,
                 float sunWorldAngle,
                 const Vector3& playerPos, float playerAngle,
                 const std::vector<Vector2>& /*mapVerticesMin*/,
                 const std::vector<Vector2>& /*mapVerticesMax*/,
                 const std::vector<std::pair<Vector2, Vector2>>& walls) const {
    if (m_animProgress <= 0.001f) { return; }

    const int scrW = framebuffer.Width();
    const int scrH = framebuffer.Height();

    // Phone body dimensions — about 1/3 screen width, 2/3 screen height.
    const int phoneW = scrW * 10 / 32;   // ~40% of render width (looks right at 640px)
    const int phoneH = scrH * 17 / 24;

    // Horizontal centre; vertical: fully visible when anim=1, fully below screen when anim=0.
    const int phoneX = (scrW - phoneW) / 2;
    const int fullyVisibleY = scrH / 2 - phoneH / 2 + scrH / 14; // slightly below centre
    const float eased = EaseOut(m_animProgress);
    // Start position: bottom edge just below screen bottom
    const int startY = scrH;
    const int phoneY = startY + static_cast<int>((fullyVisibleY - startY) * eased);

    // --- Phone body (outer shell) ---
    const uint32_t shellColor   = Framebuffer::MakeColor(28, 30, 36);
    const uint32_t shellEdge    = Framebuffer::MakeColor(60, 65, 80);
    const uint32_t screenBg     = Framebuffer::MakeColor(10, 18, 28);

    // Drop shadow (slightly offset darker rect)
    framebuffer.DrawRect(phoneX + 4, phoneY + 4, phoneW, phoneH,
                         Framebuffer::MakeColor(0, 0, 0), true);

    // Body fill
    framebuffer.DrawRect(phoneX,     phoneY,     phoneW,     phoneH,     shellColor, true);
    // Edge highlight (1px lighter border)
    framebuffer.DrawRect(phoneX,     phoneY,     phoneW,     phoneH,     shellEdge,  false);
    // Inner bevel (1px inset, darker)
    framebuffer.DrawRect(phoneX + 2, phoneY + 2, phoneW - 4, phoneH - 4,
                         Framebuffer::MakeColor(20, 22, 28), false);

    // Screen area (inset 8px from body edges, starts 18px from top for camera/speaker notch)
    const int screenMargin = 8;
    const int screenTopOffset = 18;
    const int screenX = phoneX + screenMargin;
    const int screenY = phoneY + screenTopOffset;
    const int screenW = phoneW - 2 * screenMargin;
    const int screenH = phoneH - screenTopOffset - screenMargin;
    framebuffer.DrawRect(screenX, screenY, screenW, screenH, screenBg, true);
    // Screen border glow
    framebuffer.DrawRect(screenX, screenY, screenW, screenH,
                         Framebuffer::MakeColor(40, 60, 90), false);

    // Speaker slot (top centre, above screen)
    const int speakerW = phoneW / 5;
    framebuffer.DrawRect(phoneX + phoneW / 2 - speakerW / 2,
                         phoneY + 6, speakerW, 4,
                         Framebuffer::MakeColor(18, 18, 24), true);

    // Front camera dot
    framebuffer.DrawRect(phoneX + phoneW / 2 + speakerW / 2 + 4,
                         phoneY + 6, 4, 4,
                         Framebuffer::MakeColor(30, 40, 50), true);

    // Home button (bottom centre, below screen)
    const int btnY  = phoneY + phoneH - 11;
    framebuffer.DrawRect(phoneX + phoneW / 2 - 8, btnY, 16, 6,
                         Framebuffer::MakeColor(40, 44, 54), true);
    framebuffer.DrawRect(phoneX + phoneW / 2 - 8, btnY, 16, 6,
                         Framebuffer::MakeColor(55, 60, 75), false);

    // --- Screen content ---
    DrawClock(framebuffer, screenX, screenY, screenW, sunWorldAngle);
    DrawMiniMap(framebuffer, screenX, screenY, screenW, screenH,
                playerPos, playerAngle, walls);
}
