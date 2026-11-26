#pragma once

#include <SDL3/SDL.h>
#include <cstdint>
#include <string>

class Framebuffer;

enum class TouchButton {
    None = -1,
    Fire = 0,
    Interact,
    Reload,
    Weapon,
    Menu,
    Count
};

class TouchControls {
public:
    TouchControls();

    // Call at the start of each frame before polling SDL events
    void BeginFrame();

    // Process incoming SDL touch and mouse events
    // isPlaying keeps menu navigation hit areas out of the gameplay look zone.
    void HandleEvent(const SDL_Event& event, int screenWidth, int screenHeight,
                     bool isPlaying);

    // Render virtual touch controls onto the framebuffer
    void Draw(Framebuffer& framebuffer, bool isPlaying) const;

    // Movement axes (-1.0f .. +1.0f)
    float MoveX() const { return m_moveX; }
    float MoveY() const { return m_moveY; }

    // Camera look offsets (pixels)
    float LookDx() const { return m_lookDx; }
    float LookDy() const { return m_lookDy; }

    // Action button states
    bool IsShootHeld() const { return m_shootHeld; }
    bool WasShootPressed() const { return m_shootPressed; }
    bool WasInteractPressed() const { return m_interactPressed; }
    bool WasReloadPressed() const { return m_reloadPressed; }
    bool WasWeaponCyclePressed() const { return m_weaponCyclePressed; }
    bool WasMenuPressed() const { return m_menuPressed; }

    // Menu navigation states for phone UI
    bool WasMenuUpPressed() const { return m_menuUpPressed; }
    bool WasMenuDownPressed() const { return m_menuDownPressed; }
    bool WasMenuConfirmPressed() const { return m_menuConfirmPressed; }
    bool WasMenuBackPressed() const { return m_menuBackPressed; }

    bool IsEnabled() const { return m_enabled; }
    void SetEnabled(bool enabled) { m_enabled = enabled; }

private:
    struct TouchPoint {
        SDL_FingerID id = 0;
        float x = 0.0f; // Normalized 0..1
        float y = 0.0f; // Normalized 0..1
        float prevX = 0.0f;
        float prevY = 0.0f;
        enum class Role { None, Joystick, Look, Button } role = Role::None;
        TouchButton button = TouchButton::None;
        bool active = false;
    };

    bool m_enabled = false;

    // Movement output
    float m_moveX = 0.0f; // Strafe: -1 (A) to +1 (D)
    float m_moveY = 0.0f; // Forward/Back: -1 (S) to +1 (W)

    // Look output
    float m_lookDx = 0.0f;
    float m_lookDy = 0.0f;

    // Action output
    bool m_shootHeld = false;
    bool m_shootPressed = false;
    bool m_interactPressed = false;
    bool m_reloadPressed = false;
    bool m_weaponCyclePressed = false;
    bool m_menuPressed = false;

    // Menu navigation output
    bool m_menuUpPressed = false;
    bool m_menuDownPressed = false;
    bool m_menuConfirmPressed = false;
    bool m_menuBackPressed = false;

    // Joystick screen position (normalized coords 0..1)
    float m_joyBaseX = 0.16f;
    float m_joyBaseY = 0.75f;
    float m_joyRadius = 0.12f;
    float m_joyKnobX = 0.16f;
    float m_joyKnobY = 0.75f;
    bool  m_joyActive = false;

    // Active multi-touch slots
    static constexpr int kMaxTouches = 10;
    TouchPoint m_touches[kMaxTouches];

    TouchPoint* FindTouch(SDL_FingerID id);
    TouchPoint* AllocTouch(SDL_FingerID id);
    void FreeTouch(SDL_FingerID id);

    TouchButton HitTestButton(float x, float y, int screenWidth, int screenHeight) const;
    void DrawString(Framebuffer& framebuffer, int x, int y, const std::string& text, uint32_t color, int scale) const;
};
