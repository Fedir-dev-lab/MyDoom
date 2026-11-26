#include "TouchControls.h"

#include <algorithm>
#include <cmath>

#include "renderer/Framebuffer.h"

namespace {

constexpr float kGameAspect = 16.0f / 9.0f;

// SDL reports finger positions across the whole window.  The renderer presents
// the fixed 16:9 framebuffer with letterboxing, so remap to that visible area.
// This keeps controls under the finger on 18:9, 19.5:9 and tablet displays.
void MapToGameArea(float& x, float& y, int screenWidth, int screenHeight) {
    const float windowAspect = static_cast<float>(screenWidth) /
                               static_cast<float>(std::max(screenHeight, 1));
    if (windowAspect > kGameAspect) {
        const float usedWidth = kGameAspect / windowAspect;
        x = (x - (1.0f - usedWidth) * 0.5f) / usedWidth;
    } else if (windowAspect < kGameAspect) {
        const float usedHeight = windowAspect / kGameAspect;
        y = (y - (1.0f - usedHeight) * 0.5f) / usedHeight;
    }
}

void MapDeltaToGameArea(float& dx, float& dy, int screenWidth, int screenHeight) {
    const float windowAspect = static_cast<float>(screenWidth) /
                               static_cast<float>(std::max(screenHeight, 1));
    if (windowAspect > kGameAspect) {
        dx /= (kGameAspect / windowAspect);
    } else if (windowAspect < kGameAspect) {
        dy /= (windowAspect / kGameAspect);
    }
}

} // namespace

TouchControls::TouchControls() {
#ifdef __ANDROID__
    m_enabled = true;
#else
    m_enabled = false;
#endif
}

void TouchControls::BeginFrame() {
    m_lookDx = 0.0f;
    m_lookDy = 0.0f;
    m_shootPressed = false;
    m_interactPressed = false;
    m_reloadPressed = false;
    m_weaponCyclePressed = false;
    m_menuPressed = false;

    m_menuUpPressed = false;
    m_menuDownPressed = false;
    m_menuConfirmPressed = false;
    m_menuBackPressed = false;
}

TouchControls::TouchPoint* TouchControls::FindTouch(SDL_FingerID id) {
    for (int i = 0; i < kMaxTouches; ++i) {
        if (m_touches[i].active && m_touches[i].id == id) {
            return &m_touches[i];
        }
    }
    return nullptr;
}

TouchControls::TouchPoint* TouchControls::AllocTouch(SDL_FingerID id) {
    for (int i = 0; i < kMaxTouches; ++i) {
        if (!m_touches[i].active) {
            m_touches[i].active = true;
            m_touches[i].id = id;
            m_touches[i].role = TouchPoint::Role::None;
            m_touches[i].button = TouchButton::None;
            return &m_touches[i];
        }
    }
    return nullptr;
}

void TouchControls::FreeTouch(SDL_FingerID id) {
    for (int i = 0; i < kMaxTouches; ++i) {
        if (m_touches[i].active && m_touches[i].id == id) {
            if (m_touches[i].role == TouchPoint::Role::Joystick) {
                m_joyActive = false;
                m_joyKnobX = m_joyBaseX;
                m_joyKnobY = m_joyBaseY;
                m_moveX = 0.0f;
                m_moveY = 0.0f;
            } else if (m_touches[i].role == TouchPoint::Role::Button) {
                if (m_touches[i].button == TouchButton::Fire) {
                    m_shootHeld = false;
                }
            }
            m_touches[i].active = false;
            break;
        }
    }
}

TouchButton TouchControls::HitTestButton(float x, float y,
                                         int screenWidth, int screenHeight) const {
    // Coordinates received from SDL are normalized independently on each axis.
    // The controls themselves use radii based on screen height, so compensate
    // horizontally to keep both the image and its touch target circular.
    (void)screenWidth;
    (void)screenHeight;
    const float aspect = kGameAspect;
    const auto hitsCircle = [aspect, x, y](float cx, float cy, float radius) {
        const float dx = (x - cx) * aspect;
        const float dy = y - cy;
        return dx * dx + dy * dy <= radius * radius;
    };

    // Menu (Pause) button — top right
    if (hitsCircle(0.92f, 0.08f, 0.055f)) {
        return TouchButton::Menu;
    }

    // Fire button — bottom right
    if (hitsCircle(0.88f, 0.75f, 0.105f)) {
        return TouchButton::Fire;
    }

    // Use (Interact) button — middle right
    if (hitsCircle(0.88f, 0.54f, 0.080f)) {
        return TouchButton::Interact;
    }

    // Reload button — lower middle right
    if (hitsCircle(0.73f, 0.82f, 0.072f)) {
        return TouchButton::Reload;
    }

    // Weapon cycle button — upper middle right
    if (hitsCircle(0.73f, 0.63f, 0.072f)) {
        return TouchButton::Weapon;
    }

    return TouchButton::None;
}

void TouchControls::HandleEvent(const SDL_Event& event, int screenWidth, int screenHeight,
                                bool isPlaying) {
    if (event.type == SDL_EVENT_FINGER_DOWN) {
        m_enabled = true; // Auto-enable touch UI when a touch is received
        float x = event.tfinger.x;
        float y = event.tfinger.y;
        MapToGameArea(x, y, screenWidth, screenHeight);
        if (x < 0.0f || x > 1.0f || y < 0.0f || y > 1.0f) { return; }
        TouchPoint* tp = AllocTouch(event.tfinger.fingerID);
        if (!tp) { return; }

        tp->x = x;
        tp->y = y;
        tp->prevX = x;
        tp->prevY = y;

        // Menu buttons exist only while a menu is visible.  Previously these
        // broad rectangles also captured the right side during gameplay,
        // including the fire control and the look area.
        if (!isPlaying && x >= 0.78f && y >= 0.32f && y < 0.50f) {
            m_menuUpPressed = true;
            tp->role = TouchPoint::Role::Button;
            return;
        } else if (!isPlaying && x >= 0.78f && y >= 0.50f && y < 0.68f) {
            m_menuConfirmPressed = true;
            tp->role = TouchPoint::Role::Button;
            return;
        } else if (!isPlaying && x >= 0.78f && y >= 0.68f) {
            m_menuDownPressed = true;
            tp->role = TouchPoint::Role::Button;
            return;
        } else if (!isPlaying && x >= 0.60f && x < 0.78f && y >= 0.50f && y < 0.70f) {
            m_menuBackPressed = true;
            tp->role = TouchPoint::Role::Button;
            return;
        }

        const TouchButton btn = isPlaying
            ? HitTestButton(x, y, screenWidth, screenHeight)
            : TouchButton::None;
        if (btn != TouchButton::None) {
            tp->role = TouchPoint::Role::Button;
            tp->button = btn;

            if (btn == TouchButton::Fire) {
                m_shootHeld = true;
                m_shootPressed = true;
            } else if (btn == TouchButton::Interact) {
                m_interactPressed = true;
            } else if (btn == TouchButton::Reload) {
                m_reloadPressed = true;
            } else if (btn == TouchButton::Weapon) {
                m_weaponCyclePressed = true;
            } else if (btn == TouchButton::Menu) {
                m_menuPressed = true;
                m_menuBackPressed = true;
            }
        } else if (x < 0.45f) {
            // Left half of screen -> virtual joystick (in-game) or tap up/down (menu)
            tp->role = TouchPoint::Role::Joystick;
            m_joyActive = true;
            m_joyKnobX = x;
            m_joyKnobY = y;

            const float aspect = kGameAspect;
            const float dx = (x - m_joyBaseX) * aspect;
            const float dy = y - m_joyBaseY;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist > 0.0001f) {
                const float clampedDist = std::min(dist, m_joyRadius);
                m_joyKnobX = m_joyBaseX + (dx / dist) * clampedDist / aspect;
                m_joyKnobY = m_joyBaseY + (dy / dist) * clampedDist;
                m_moveX = (dx / dist) * (clampedDist / m_joyRadius);
                m_moveY = -(dy / dist) * (clampedDist / m_joyRadius);
            }

            if (!isPlaying && y < 0.45f) {
                m_menuUpPressed = true;
            } else if (!isPlaying && y > 0.65f) {
                m_menuDownPressed = true;
            } else if (!isPlaying) {
                m_menuConfirmPressed = true;
            }
        } else {
            // Right half of screen -> look control or confirm
            tp->role = TouchPoint::Role::Look;
            if (!isPlaying && y < 0.45f) {
                m_menuUpPressed = true;
            } else if (!isPlaying && y > 0.65f) {
                m_menuDownPressed = true;
            } else if (!isPlaying) {
                m_menuConfirmPressed = true;
            }
        }
    } else if (event.type == SDL_EVENT_FINGER_MOTION) {
        TouchPoint* tp = FindTouch(event.tfinger.fingerID);
        if (!tp) { return; }

        float x = event.tfinger.x;
        float y = event.tfinger.y;
        float dxNorm = event.tfinger.dx;
        float dyNorm = event.tfinger.dy;
        MapToGameArea(x, y, screenWidth, screenHeight);
        MapDeltaToGameArea(dxNorm, dyNorm, screenWidth, screenHeight);

        tp->prevX = tp->x;
        tp->prevY = tp->y;
        tp->x = x;
        tp->y = y;

        if (tp->role == TouchPoint::Role::Joystick) {
            const float aspect = kGameAspect;
            const float dx = (x - m_joyBaseX) * aspect;
            const float dy = y - m_joyBaseY;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist > 0.0001f) {
                const float clampedDist = std::min(dist, m_joyRadius);
                m_joyKnobX = m_joyBaseX + (dx / dist) * clampedDist / aspect;
                m_joyKnobY = m_joyBaseY + (dy / dist) * clampedDist;
                m_moveX = (dx / dist) * (clampedDist / m_joyRadius);
                m_moveY = -(dy / dist) * (clampedDist / m_joyRadius);
            } else {
                m_joyKnobX = m_joyBaseX;
                m_joyKnobY = m_joyBaseY;
                m_moveX = 0.0f;
                m_moveY = 0.0f;
            }
        } else if (tp->role == TouchPoint::Role::Look) {
            // Use the presented 16:9 area's pixel size, not black bars or a
            // phone's wider surface, for stable camera sensitivity.
            const float windowAspect = static_cast<float>(screenWidth) /
                                       static_cast<float>(std::max(screenHeight, 1));
            const float gameWidth = windowAspect > kGameAspect
                ? static_cast<float>(screenHeight) * kGameAspect
                : static_cast<float>(screenWidth);
            const float gameHeight = windowAspect > kGameAspect
                ? static_cast<float>(screenHeight)
                : static_cast<float>(screenWidth) / kGameAspect;
            m_lookDx += dxNorm * gameWidth * 1.8f;
            m_lookDy += dyNorm * gameHeight * 1.8f;
        }
    } else if (event.type == SDL_EVENT_FINGER_UP) {
        FreeTouch(event.tfinger.fingerID);
    }
}

void TouchControls::DrawString(Framebuffer& framebuffer, int x, int y,
                               const std::string& text, uint32_t color, int scale) const {
    static const uint8_t kFont[][5] = {
        {' ', 0x00, 0x00, 0x00, 0x00},
        {'A', 0x7E, 0x09, 0x09, 0x7E},
        {'B', 0x7F, 0x49, 0x49, 0x36},
        {'C', 0x3E, 0x41, 0x41, 0x22},
        {'D', 0x7F, 0x41, 0x41, 0x3E},
        {'E', 0x7F, 0x49, 0x49, 0x41},
        {'F', 0x7F, 0x09, 0x09, 0x01},
        {'G', 0x3E, 0x41, 0x49, 0x7A},
        {'H', 0x7F, 0x08, 0x08, 0x7F},
        {'I', 0x00, 0x41, 0x7F, 0x41},
        {'J', 0x20, 0x40, 0x41, 0x3F},
        {'K', 0x7F, 0x08, 0x14, 0x63},
        {'L', 0x7F, 0x40, 0x40, 0x40},
        {'M', 0x7F, 0x02, 0x0C, 0x7F},
        {'N', 0x7F, 0x04, 0x08, 0x7F},
        {'O', 0x3E, 0x41, 0x41, 0x3E},
        {'P', 0x7F, 0x09, 0x09, 0x06},
        {'Q', 0x3E, 0x41, 0x51, 0x7E},
        {'R', 0x7F, 0x09, 0x19, 0x66},
        {'S', 0x26, 0x49, 0x49, 0x32},
        {'T', 0x01, 0x01, 0x7F, 0x01},
        {'U', 0x3F, 0x40, 0x40, 0x3F},
        {'V', 0x1F, 0x20, 0x40, 0x1F},
        {'W', 0x7F, 0x20, 0x18, 0x7F},
        {'X', 0x63, 0x14, 0x08, 0x63},
        {'Y', 0x07, 0x08, 0x70, 0x0F},
        {'Z', 0x61, 0x51, 0x49, 0x43},
    };

    int curX = x;
    for (char c : text) {
        char uppercaseChar = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        const uint8_t* glyphData = kFont[0];
        for (const auto& entry : kFont) {
            if (entry[0] == uppercaseChar) {
                glyphData = entry;
                break;
            }
        }

        for (int col = 1; col < 5; ++col) {
            uint8_t columnBits = glyphData[col];
            for (int bit = 0; bit < 7; ++bit) {
                if ((columnBits >> bit) & 1) {
                    for (int dy = 0; dy < scale; ++dy) {
                        for (int dx = 0; dx < scale; ++dx) {
                            framebuffer.SetPixel(curX + col * scale + dx, y + bit * scale + dy, color);
                        }
                    }
                }
            }
        }
        curX += 6 * scale;
    }
}

void TouchControls::Draw(Framebuffer& framebuffer, bool isPlaying) const {
    if (!m_enabled) { return; }

    const int W = framebuffer.Width();
    const int H = framebuffer.Height();
    const float scale = static_cast<float>(H) / 480.0f;

    if (!isPlaying) {
        // ---- Menu Navigation Overlay -------------------------------------
        // UP Arrow Button
        const int upX = static_cast<int>(0.85f * W);
        const int upY = static_cast<int>(0.42f * H);
        const int upR = static_cast<int>(26.0f * scale);
        framebuffer.DrawBlendCircle(upX, upY, upR, 50, 150, 240, 0.65f, true);
        framebuffer.DrawBlendCircle(upX, upY, upR, 255, 255, 255, 0.90f, false);
        DrawString(framebuffer, upX - static_cast<int>(6 * scale), upY - static_cast<int>(5 * scale),
                   "UP", Framebuffer::MakeColor(255, 255, 255), std::max(1, static_cast<int>(scale * 1.1f)));

        // OK Confirm Button
        const int okX = static_cast<int>(0.85f * W);
        const int okY = static_cast<int>(0.60f * H);
        const int okR = static_cast<int>(28.0f * scale);
        framebuffer.DrawBlendCircle(okX, okY, okR, 60, 200, 100, 0.65f, true);
        framebuffer.DrawBlendCircle(okX, okY, okR, 255, 255, 255, 0.90f, false);
        DrawString(framebuffer, okX - static_cast<int>(6 * scale), okY - static_cast<int>(5 * scale),
                   "OK", Framebuffer::MakeColor(255, 255, 255), std::max(1, static_cast<int>(scale * 1.2f)));

        // DOWN Arrow Button
        const int dnX = static_cast<int>(0.85f * W);
        const int dnY = static_cast<int>(0.78f * H);
        const int dnR = static_cast<int>(26.0f * scale);
        framebuffer.DrawBlendCircle(dnX, dnY, dnR, 50, 150, 240, 0.65f, true);
        framebuffer.DrawBlendCircle(dnX, dnY, dnR, 255, 255, 255, 0.90f, false);
        DrawString(framebuffer, dnX - static_cast<int>(6 * scale), dnY - static_cast<int>(5 * scale),
                   "DN", Framebuffer::MakeColor(255, 255, 255), std::max(1, static_cast<int>(scale * 1.1f)));

        // BACK Button
        const int backX = static_cast<int>(0.68f * W);
        const int backY = static_cast<int>(0.60f * H);
        const int backR = static_cast<int>(24.0f * scale);
        framebuffer.DrawBlendCircle(backX, backY, backR, 200, 80, 80, 0.65f, true);
        framebuffer.DrawBlendCircle(backX, backY, backR, 255, 255, 255, 0.90f, false);
        DrawString(framebuffer, backX - static_cast<int>(12 * scale), backY - static_cast<int>(5 * scale),
                   "BACK", Framebuffer::MakeColor(255, 255, 255), std::max(1, static_cast<int>(scale * 1.0f)));

        return;
    }

    // ---- In-Game Virtual Joystick (Left) ----------------------------------
    const int joyBaseX = static_cast<int>(m_joyBaseX * W);
    const int joyBaseY = static_cast<int>(m_joyBaseY * H);
    const int joyRadius = static_cast<int>(m_joyRadius * H);

    const int joyKnobX = static_cast<int>(m_joyKnobX * W);
    const int joyKnobY = static_cast<int>(m_joyKnobY * H);
    const int knobRadius = static_cast<int>(22.0f * scale);

    // Draw joystick outer ring (translucent cyan/white)
    framebuffer.DrawBlendCircle(joyBaseX, joyBaseY, joyRadius, 200, 220, 255, 0.35f, false);
    framebuffer.DrawBlendCircle(joyBaseX, joyBaseY, joyRadius - 2, 200, 220, 255, 0.15f, true);

    // Draw inner knob
    const uint8_t knobR = m_joyActive ? 255 : 180;
    const uint8_t knobG = m_joyActive ? 220 : 200;
    const uint8_t knobB = m_joyActive ? 80  : 220;
    framebuffer.DrawBlendCircle(joyKnobX, joyKnobY, knobRadius, knobR, knobG, knobB, 0.55f, true);
    framebuffer.DrawBlendCircle(joyKnobX, joyKnobY, knobRadius, 255, 255, 255, 0.70f, false);

    // ---- Action Buttons (Right) -------------------------------------------

    // 1. FIRE Button
    const int fireX = static_cast<int>(0.88f * W);
    const int fireY = static_cast<int>(0.75f * H);
    const int fireR = static_cast<int>(50.0f * scale);
    const bool fireActive = m_shootHeld;
    framebuffer.DrawBlendCircle(fireX, fireY, fireR,
                                fireActive ? 255 : 220,
                                fireActive ? 60  : 40,
                                fireActive ? 40  : 40,
                                fireActive ? 0.75f : 0.40f, true);
    framebuffer.DrawBlendCircle(fireX, fireY, fireR, 255, 255, 255, 0.80f, false);
    DrawString(framebuffer, fireX - static_cast<int>(14 * scale), fireY - static_cast<int>(6 * scale),
               "FIRE", Framebuffer::MakeColor(255, 255, 255), std::max(1, static_cast<int>(scale * 1.2f)));

    // 2. USE (Interact) Button
    const int useX = static_cast<int>(0.88f * W);
    const int useY = static_cast<int>(0.54f * H);
    const int useR = static_cast<int>(38.0f * scale);
    framebuffer.DrawBlendCircle(useX, useY, useR, 40, 180, 240, 0.40f, true);
    framebuffer.DrawBlendCircle(useX, useY, useR, 255, 255, 255, 0.70f, false);
    DrawString(framebuffer, useX - static_cast<int>(10 * scale), useY - static_cast<int>(5 * scale),
               "USE", Framebuffer::MakeColor(255, 255, 255), std::max(1, static_cast<int>(scale * 1.1f)));

    // 3. RELOAD Button
    const int relX = static_cast<int>(0.73f * W);
    const int relY = static_cast<int>(0.82f * H);
    const int relR = static_cast<int>(35.0f * scale);
    framebuffer.DrawBlendCircle(relX, relY, relR, 230, 160, 40, 0.40f, true);
    framebuffer.DrawBlendCircle(relX, relY, relR, 255, 255, 255, 0.70f, false);
    DrawString(framebuffer, relX - static_cast<int>(10 * scale), relY - static_cast<int>(5 * scale),
               "REL", Framebuffer::MakeColor(255, 255, 255), std::max(1, static_cast<int>(scale * 1.0f)));

    // 4. WEAPON CYCLE Button
    const int wpnX = static_cast<int>(0.73f * W);
    const int wpnY = static_cast<int>(0.63f * H);
    const int wpnR = static_cast<int>(35.0f * scale);
    framebuffer.DrawBlendCircle(wpnX, wpnY, wpnR, 60, 200, 100, 0.40f, true);
    framebuffer.DrawBlendCircle(wpnX, wpnY, wpnR, 255, 255, 255, 0.70f, false);
    DrawString(framebuffer, wpnX - static_cast<int>(10 * scale), wpnY - static_cast<int>(5 * scale),
               "GUN", Framebuffer::MakeColor(255, 255, 255), std::max(1, static_cast<int>(scale * 1.0f)));

    // 5. PAUSE / MENU Button (Top Right)
    const int menuX = static_cast<int>(0.92f * W);
    const int menuY = static_cast<int>(0.08f * H);
    const int menuR = static_cast<int>(26.0f * scale);
    framebuffer.DrawBlendCircle(menuX, menuY, menuR, 100, 100, 120, 0.50f, true);
    framebuffer.DrawBlendCircle(menuX, menuY, menuR, 255, 255, 255, 0.80f, false);
    DrawString(framebuffer, menuX - static_cast<int>(10 * scale), menuY - static_cast<int>(5 * scale),
               "ESC", Framebuffer::MakeColor(255, 255, 255), std::max(1, static_cast<int>(scale * 1.0f)));
}
