#pragma once

#include <cstdint>

#include "game/SaveSystem.h"

class Framebuffer;

class Menu {
public:
    enum class Screen {
        Main,
        Load,   // choose a slot to load from
        Save,   // choose a slot to save into
        Settings,
        Soundtrack,
    };

    enum class MainAction {
        None,
        NewGame,
        LoadGame,
        SaveGame,
        ResolutionChanged,  // settings: resolution was cycled — rebuild renderers
        FullscreenToggled,
        ConvertMusicProject,
        ConvertCustomWav,
        ResetDefaultMusic,
        Exit,
    };

    void Show();   // open on Main screen
    void Hide();

    // Open directly on the Save screen (called from in-game F5)
    void ShowSave();

    bool IsVisible()        const { return m_visible; }
    Screen CurrentScreen()  const { return m_screen; }

    void NavigateUp();
    void NavigateDown();

    // Returns the relevant action; for sub-screens handles navigation internally.
    MainAction Confirm();

    // Back from sub-screen → Main, or close if already on Main (only from game pause)
    void Back();

    int LoadSlot() const { return m_selectedSlot; }
    int SaveSlot()  const { return m_selectedSlot; }

    // Returns current render width chosen in settings (320, 640, or 1280).
    int  SelectedResolution() const { return kResolutions[m_resolutionIndex]; }

    // Current values of the settings toggles — read by main.cpp every frame.
    bool HeadBobEnabled()     const { return m_settingBob;        }
    bool ShowFpsEnabled()     const { return m_settingFps;        }
    bool ShadowsEnabled()     const { return m_settingShadows;    }
    bool FullscreenEnabled()  const { return m_settingFullscreen; }
    void SetFullscreen(bool enabled) { m_settingFullscreen = enabled; }

    void SetSoundtrackInfo(const std::string& name, int bpm, bool isCustom, const std::string& status) {
        m_soundtrackName   = name;
        m_soundtrackBpm    = bpm;
        m_soundtrackCustom = isCustom;
        m_soundtrackStatus = status;
    }

    void Draw(Framebuffer& fb) const;

private:
    void DrawMain(Framebuffer& fb) const;
    void DrawSlotScreen(Framebuffer& fb, bool isSave) const;
    void DrawSettings(Framebuffer& fb) const;
    void DrawSoundtrack(Framebuffer& fb) const;

    void DrawText(Framebuffer& fb, int x, int y,
                  const char* text, uint32_t color, int scale) const;
    void DrawChar(Framebuffer& fb, int x, int y,
                  char c, uint32_t color, int scale) const;

    bool   m_visible       = true;
    Screen m_screen        = Screen::Main;
    int    m_selectedIndex = 0;  // current cursor row
    int    m_selectedSlot  = 0;  // last confirmed slot

    // Slot screen has kSaveSlotCount slots + 1 BACK row
    static constexpr int kSlotRows        = kSaveSlotCount + 1;
    static constexpr int kMainItems       = 5;
    // Settings items: HEAD BOB, SHOW FPS, SHADOWS, FULLSCREEN, RESOLUTION, BACK
    static constexpr int kSettingsItems   = 5;  // toggles/cycles before BACK
    static constexpr int kSoundtrackItems = 4;  // MusicProject, custom.wav, Reset, BACK

    // Available render widths (height = width * 9/16).
    static constexpr int kResolutionCount = 3;
    static constexpr int kResolutions[kResolutionCount] = {320, 640, 1280};

    bool m_settingBob        = true;
    bool m_settingFps        = false;
    bool m_settingShadows    = true;
    bool m_settingFullscreen = false;
    int  m_resolutionIndex   = 1;  // default: 640

    std::string m_soundtrackName   = "MARS AMBIENT";
    std::string m_soundtrackStatus = "DEFAULT PROCEDURAL THEME";
    int         m_soundtrackBpm    = 72;
    bool        m_soundtrackCustom = false;
};
