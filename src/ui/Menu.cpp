#include "Menu.h"

#include <array>
#include <cstdio>

#include "renderer/Framebuffer.h"

// ---------------------------------------------------------------------------
// Pixel font — full uppercase alphabet + digits + punctuation
// ---------------------------------------------------------------------------
namespace {

using Glyph = std::array<uint8_t, 5>;

Glyph GetGlyph(char c) {
    switch (c) {
        case '0': return {0b111,0b101,0b101,0b101,0b111};
        case '1': return {0b010,0b110,0b010,0b010,0b111};
        case '2': return {0b111,0b001,0b111,0b100,0b111};
        case '3': return {0b111,0b001,0b111,0b001,0b111};
        case '4': return {0b101,0b101,0b111,0b001,0b001};
        case '5': return {0b111,0b100,0b111,0b001,0b111};
        case '6': return {0b111,0b100,0b111,0b101,0b111};
        case '7': return {0b111,0b001,0b010,0b010,0b010};
        case '8': return {0b111,0b101,0b111,0b101,0b111};
        case '9': return {0b111,0b101,0b111,0b001,0b111};
        case ':': return {0b000,0b010,0b000,0b010,0b000};
        case '.': return {0b000,0b000,0b000,0b000,0b010};
        case '-': return {0b000,0b000,0b111,0b000,0b000};
        case ' ': return {0b000,0b000,0b000,0b000,0b000};
        case '/': return {0b001,0b001,0b010,0b100,0b100};
        case '>': return {0b100,0b010,0b001,0b010,0b100};
        case 'A': return {0b010,0b101,0b111,0b101,0b101};
        case 'B': return {0b110,0b101,0b110,0b101,0b110};
        case 'C': return {0b111,0b100,0b100,0b100,0b111};
        case 'D': return {0b110,0b101,0b101,0b101,0b110};
        case 'E': return {0b111,0b100,0b110,0b100,0b111};
        case 'F': return {0b111,0b100,0b110,0b100,0b100};
        case 'G': return {0b011,0b100,0b101,0b101,0b011};
        case 'H': return {0b101,0b101,0b111,0b101,0b101};
        case 'I': return {0b111,0b010,0b010,0b010,0b111};
        case 'J': return {0b001,0b001,0b001,0b101,0b111};
        case 'K': return {0b101,0b110,0b100,0b110,0b101};
        case 'L': return {0b100,0b100,0b100,0b100,0b111};
        case 'M': return {0b101,0b111,0b111,0b101,0b101};
        case 'N': return {0b101,0b111,0b111,0b111,0b101};
        case 'O': return {0b111,0b101,0b101,0b101,0b111};
        case 'P': return {0b111,0b101,0b111,0b100,0b100};
        case 'Q': return {0b111,0b101,0b101,0b110,0b011};
        case 'R': return {0b110,0b101,0b110,0b101,0b101};
        case 'S': return {0b111,0b100,0b111,0b001,0b111};
        case 'T': return {0b111,0b010,0b010,0b010,0b010};
        case 'U': return {0b101,0b101,0b101,0b101,0b111};
        case 'V': return {0b101,0b101,0b101,0b101,0b010};
        case 'W': return {0b101,0b101,0b101,0b111,0b101};
        case 'X': return {0b101,0b101,0b010,0b101,0b101};
        case 'Y': return {0b101,0b101,0b010,0b010,0b010};
        case 'Z': return {0b111,0b001,0b010,0b100,0b111};
        default:  return {0b000,0b000,0b000,0b000,0b000};
    }
}

int TextWidth(const char* t, int scale) {
    int n = 0;
    while (t[n]) ++n;
    return n > 0 ? n * (3 * scale + scale) - scale : 0;
}

} // namespace

// ---------------------------------------------------------------------------
// DrawChar / DrawText
// ---------------------------------------------------------------------------
void Menu::DrawChar(Framebuffer& fb, int x, int y,
                    char c, uint32_t color, int scale) const {
    const Glyph g = GetGlyph(c);
    for (int row = 0; row < 5; ++row)
        for (int col = 0; col < 3; ++col)
            if (g[row] & (1 << (2 - col)))
                fb.DrawRect(x + col*scale, y + row*scale, scale, scale, color, true);
}

void Menu::DrawText(Framebuffer& fb, int x, int y,
                    const char* text, uint32_t color, int scale) const {
    const int adv = 3*scale + scale;
    for (int i = 0; text[i]; ++i)
        DrawChar(fb, x + i*adv, y, text[i], color, scale);
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------
void Menu::Show() {
    m_visible       = true;
    m_screen        = Screen::Main;
    m_selectedIndex = 0;
}

void Menu::ShowSave() {
    m_visible       = true;
    m_screen        = Screen::Save;
    m_selectedIndex = 0;
}

void Menu::Hide() {
    m_visible = false;
}

void Menu::NavigateUp() {
    int items = kMainItems;
    if (m_screen == Screen::Load || m_screen == Screen::Save) items = kSlotRows;
    else if (m_screen == Screen::Settings) items = kSettingsItems + 1; // +1 for BACK
    m_selectedIndex = (m_selectedIndex + items - 1) % items;
}

void Menu::NavigateDown() {
    int items = kMainItems;
    if (m_screen == Screen::Load || m_screen == Screen::Save) items = kSlotRows;
    else if (m_screen == Screen::Settings) items = kSettingsItems + 1;
    m_selectedIndex = (m_selectedIndex + 1) % items;
}

Menu::MainAction Menu::Confirm() {
    if (m_screen == Screen::Main) {
        switch (m_selectedIndex) {
            case 0: m_visible = false; return MainAction::NewGame;
            case 1: m_screen = Screen::Load; m_selectedIndex = 0; return MainAction::None;
            case 2: m_screen = Screen::Settings; m_selectedIndex = 0; return MainAction::None;
            case 3: return MainAction::Exit;
        }
    }

    if (m_screen == Screen::Load) {
        if (m_selectedIndex < kSaveSlotCount) {
            // Only load if the slot actually has data
            if (SaveSystem::HasSave(m_selectedIndex)) {
                m_selectedSlot = m_selectedIndex;
                m_visible = false;
                m_screen  = Screen::Main;
                return MainAction::LoadGame;
            }
            // Empty slot — do nothing (cursor stays)
        } else {
            // BACK row
            m_screen = Screen::Main;
            m_selectedIndex = 1; // re-select "LOAD GAME"
        }
    }

    if (m_screen == Screen::Save) {
        if (m_selectedIndex < kSaveSlotCount) {
            m_selectedSlot = m_selectedIndex;
            m_visible = false;
            m_screen  = Screen::Main;
            return MainAction::SaveGame;
        } else {
            // BACK row
            m_screen = Screen::Main;
            m_selectedIndex = 0;
        }
    }

    if (m_screen == Screen::Settings) {
        if (m_selectedIndex < kSettingsItems) {
            switch (m_selectedIndex) {
                case 0: m_settingBob        = !m_settingBob;        break;
                case 1: m_settingFps        = !m_settingFps;        break;
                case 2: m_settingShadows    = !m_settingShadows;    break;
                case 3:
                    m_settingFullscreen = !m_settingFullscreen;
                    return MainAction::FullscreenToggled;
                case 4:
                    // Cycle through resolutions
                    m_resolutionIndex = (m_resolutionIndex + 1) % kResolutionCount;
                    return MainAction::ResolutionChanged;
            }
        } else {
            m_screen = Screen::Main;
            m_selectedIndex = 2;
        }
    }

    return MainAction::None;
}

void Menu::Back() {
    if (m_screen != Screen::Main) {
        m_screen = Screen::Main;
        m_selectedIndex = 0;
    }
    // On Main screen Esc does nothing — game stays paused in menu
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------
void Menu::Draw(Framebuffer& fb) const {
    if (!m_visible) return;

    // Dark overlay
    fb.DrawRect(0, 0, fb.Width(), fb.Height(),
                Framebuffer::MakeColor(8, 6, 12), true);

    switch (m_screen) {
        case Screen::Main:     DrawMain(fb);               break;
        case Screen::Load:     DrawSlotScreen(fb, false);  break;
        case Screen::Save:     DrawSlotScreen(fb, true);   break;
        case Screen::Settings: DrawSettings(fb);           break;
    }
}

// ---------------------------------------------------------------------------
// DrawMain
// ---------------------------------------------------------------------------
void Menu::DrawMain(Framebuffer& fb) const {
    const int W = fb.Width(), H = fb.Height(), cx = W / 2;

    const uint32_t kTitle   = Framebuffer::MakeColor(180, 30, 30);
    const uint32_t kSel     = Framebuffer::MakeColor(220, 200, 60);
    const uint32_t kNormal  = Framebuffer::MakeColor(120, 100, 80);
    const uint32_t kShadow  = Framebuffer::MakeColor(30, 20, 20);

    // Title
    constexpr int kTS = 4;
    const char* title = "MYDOOMENGINE";
    const int tx = cx - TextWidth(title, kTS) / 2;
    const int ty = H / 6;
    DrawText(fb, tx+2, ty+2, title, kShadow, kTS);
    DrawText(fb, tx,   ty,   title, kTitle,  kTS);

    // Separator
    const int lineY = ty + 5*kTS + 8;
    fb.DrawLine(cx-120, lineY, cx+120, lineY, Framebuffer::MakeColor(80, 30, 30));

    const char* items[kMainItems] = { "NEW GAME", "LOAD GAME", "SETTINGS", "EXIT" };
    constexpr int kIS = 3;
    const int startY = lineY + 20;
    const int step   = 5*kIS + 10;

    for (int i = 0; i < kMainItems; ++i) {
        const int iw = TextWidth(items[i], kIS);
        const int ix = cx - iw / 2;
        const int iy = startY + i * step;
        const bool sel = (i == m_selectedIndex);
        DrawText(fb, ix+1, iy+1, items[i], kShadow, kIS);
        if (sel) {
            DrawText(fb, ix - 4*kIS, iy, ">", kSel, kIS);
            DrawText(fb, ix, iy, items[i], kSel, kIS);
        } else {
            DrawText(fb, ix, iy, items[i], kNormal, kIS);
        }
    }

    // Footer
    const char* hint = "W S  MOVE   ENTER  SELECT   ESC  BACK";
    DrawText(fb, cx - TextWidth(hint,1)/2, H-16, hint,
             Framebuffer::MakeColor(60,50,50), 1);
}

// ---------------------------------------------------------------------------
// DrawSlotScreen — shared by Load and Save screens
// ---------------------------------------------------------------------------
void Menu::DrawSlotScreen(Framebuffer& fb, bool isSave) const {
    const int W = fb.Width(), H = fb.Height(), cx = W / 2;

    const uint32_t kTitle  = Framebuffer::MakeColor(180, 30, 30);
    const uint32_t kSel    = Framebuffer::MakeColor(220, 200, 60);
    const uint32_t kNormal = Framebuffer::MakeColor(120, 100, 80);
    const uint32_t kEmpty  = Framebuffer::MakeColor(55,  44, 38);
    const uint32_t kShadow = Framebuffer::MakeColor(20,  15, 15);
    const uint32_t kOver   = Framebuffer::MakeColor(180, 60, 60); // overwrite warning

    constexpr int kTS = 3;
    const char* title = isSave ? "SAVE GAME" : "LOAD GAME";
    const int tx = cx - TextWidth(title, kTS) / 2;
    const int ty = H / 8;
    DrawText(fb, tx+1, ty+1, title, kShadow, kTS);
    DrawText(fb, tx,   ty,   title, kTitle,  kTS);

    constexpr int kSS = 2;
    const int startY = ty + 5*kTS + 24;
    const int step   = 5*kSS + 14;

    for (int slot = 0; slot < kSaveSlotCount; ++slot) {
        const int iy  = startY + slot * step;
        const bool sel = (slot == m_selectedIndex);
        const SaveSlotInfo info = SaveSystem::GetSlotInfo(slot);

        char label[10];
        std::snprintf(label, sizeof(label), "SLOT %d", slot + 1);

        const int lx = cx - 100;
        const uint32_t lc = sel ? kSel : kNormal;

        if (sel) DrawText(fb, lx - 4*kSS, iy, ">", kSel, kSS);
        DrawText(fb, lx+1, iy+1, label, kShadow, kSS);
        DrawText(fb, lx,   iy,   label, lc,      kSS);

        const int ix = lx + TextWidth(label, kSS) + 16;

        if (info.exists) {
            // On Save screen, warn that this slot will be overwritten
            const uint32_t infoColor = isSave && sel ? kOver
                                     : sel           ? kSel
                                                     : kNormal;
            DrawText(fb, ix+1, iy+1, info.description, kShadow, kSS);
            DrawText(fb, ix,   iy,   info.description, infoColor, kSS);
            if (isSave && sel) {
                const char* warn = "OVERWRITE?";
                const int wx = lx + TextWidth(label, kSS) + 10;
                DrawText(fb, wx, iy + 5*kSS + 4, warn, kOver, 1);
            }
        } else {
            const char* empty = isSave ? "--- NEW SAVE ---" : "--- EMPTY ---";
            const uint32_t ec = sel ? kSel : kEmpty;
            DrawText(fb, ix, iy, empty, ec, kSS);
        }
    }

    // BACK row
    const int backY  = startY + kSaveSlotCount * step + 8;
    const bool backSel = (m_selectedIndex == kSaveSlotCount);
    const char* backText = "BACK";
    const int bx = cx - TextWidth(backText, kSS) / 2;
    if (backSel) DrawText(fb, bx - 4*kSS, backY, ">", kSel, kSS);
    DrawText(fb, bx+1, backY+1, backText, kShadow, kSS);
    DrawText(fb, bx,   backY,   backText, backSel ? kSel : kNormal, kSS);

    // Footer
    const char* hint = isSave ? "ENTER  SAVE   ESC  BACK"
                               : "ENTER  LOAD   ESC  BACK";
    DrawText(fb, cx - TextWidth(hint,1)/2, H-16, hint,
             Framebuffer::MakeColor(60,50,50), 1);
}

// ---------------------------------------------------------------------------
// DrawSettings
// ---------------------------------------------------------------------------
void Menu::DrawSettings(Framebuffer& fb) const {
    const int W = fb.Width(), H = fb.Height(), cx = W / 2;

    const uint32_t kTitle  = Framebuffer::MakeColor(180, 30, 30);
    const uint32_t kSel    = Framebuffer::MakeColor(220, 200, 60);
    const uint32_t kNormal = Framebuffer::MakeColor(120, 100, 80);
    const uint32_t kOn     = Framebuffer::MakeColor(80,  200, 80);
    const uint32_t kOff    = Framebuffer::MakeColor(160,  60, 60);
    const uint32_t kShadow = Framebuffer::MakeColor(20,   15, 15);

    constexpr int kTS = 3;
    const char* title = "SETTINGS";
    const int tx = cx - TextWidth(title, kTS) / 2;
    const int ty = H / 8;
    DrawText(fb, tx+1, ty+1, title, kShadow, kTS);
    DrawText(fb, tx,   ty,   title, kTitle,  kTS);

    constexpr int kSS = 2;
    const int startY = ty + 5*kTS + 24;
    const int step   = 5*kSS + 14;

    // First 4 items are ON/OFF toggles, 5th is resolution cycle.
    struct BoolItem { const char* label; bool value; };
    const BoolItem boolItems[4] = {
        {"HEAD BOB",   m_settingBob},
        {"SHOW FPS",   m_settingFps},
        {"SHADOWS",    m_settingShadows},
        {"FULLSCREEN", m_settingFullscreen},
    };

    // Draw the 4 bool toggles
    for (int i = 0; i < 4; ++i) {
        const int iy  = startY + i * step;
        const bool sel = (i == m_selectedIndex);
        const int ix  = cx - 80;

        if (sel) DrawText(fb, ix - 4*kSS, iy, ">", kSel, kSS);
        DrawText(fb, ix+1, iy+1, boolItems[i].label, kShadow, kSS);
        DrawText(fb, ix,   iy,   boolItems[i].label, sel ? kSel : kNormal, kSS);

        const char* val = boolItems[i].value ? "ON" : "OFF";
        const int vx = ix + TextWidth(boolItems[i].label, kSS) + 20;
        DrawText(fb, vx+1, iy+1, val, kShadow, kSS);
        DrawText(fb, vx,   iy,   val, sel ? kSel : (boolItems[i].value ? kOn : kOff), kSS);
    }

    // Resolution item (index 4)
    {
        const int i   = 4;
        const int iy  = startY + i * step;
        const bool sel = (i == m_selectedIndex);
        const int ix  = cx - 80;

        if (sel) DrawText(fb, ix - 4*kSS, iy, ">", kSel, kSS);
        DrawText(fb, ix+1, iy+1, "RESOLUTION", kShadow, kSS);
        DrawText(fb, ix,   iy,   "RESOLUTION", sel ? kSel : kNormal, kSS);

        // Show current resolution as "320x180" / "640x360" / "1280x720"
        char resBuf[16];
        const int rw = kResolutions[m_resolutionIndex];
        const int rh = rw * 9 / 16;
        std::snprintf(resBuf, sizeof(resBuf), "%dx%d", rw, rh);
        const int vx = ix + TextWidth("RESOLUTION", kSS) + 20;
        DrawText(fb, vx+1, iy+1, resBuf, kShadow, kSS);
        // Resolution uses a blue-ish colour to distinguish from ON/OFF
        const uint32_t resColor = sel ? kSel : Framebuffer::MakeColor(80, 160, 220);
        DrawText(fb, vx,   iy,   resBuf, resColor, kSS);

        // Small hint under selected resolution item
        if (sel) {
            const char* hint2 = "ENTER TO CYCLE";
            DrawText(fb, ix, iy + 5*kSS + 3, hint2,
                     Framebuffer::MakeColor(80, 70, 60), 1);
        }
    }

    const int backY  = startY + kSettingsItems * step + 8;
    const bool backSel = (m_selectedIndex == kSettingsItems);
    const char* backText = "BACK";
    const int bx = cx - TextWidth(backText, kSS) / 2;
    if (backSel) DrawText(fb, bx - 4*kSS, backY, ">", kSel, kSS);
    DrawText(fb, bx+1, backY+1, backText, kShadow, kSS);
    DrawText(fb, bx,   backY,   backText, backSel ? kSel : kNormal, kSS);

    const char* hint = "ENTER  TOGGLE   ESC  BACK";
    DrawText(fb, cx - TextWidth(hint,1)/2, H-16, hint,
             Framebuffer::MakeColor(60,50,50), 1);
}
