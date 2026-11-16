// MyDoomEngine — game loop, input, rendering orchestration.

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "game/Game.h"
#include "game/DevMode.h"
#include "game/AssetManager.h"
#include "game/GamePack.h"
#include "core/Vector2.h"
#include "core/Vector3.h"
#include "enemies/Enemy.h"
#include "game/Logger.h"
#include "game/SaveSystem.h"
#include "player/Player.h"
#include "renderer/Framebuffer.h"
#include "renderer/Lighting.h"
#include "renderer/MarsTerrain.h"
#include "renderer/MeshRenderer.h"
#include "renderer/Camera.h"
#include "renderer/PlaneRenderer.h"
#include "renderer/SkyRenderer.h"
#include "renderer/SpriteRenderer.h"
#include "renderer/TerrainRenderer.h"
#include "renderer/Texture.h"
#include "renderer/WallRenderer.h"
#include "ui/DebugOverlay.h"
#include "ui/Hud.h"
#include "ui/Menu.h"
#include "ui/Phone.h"
#include "audio/AudioSystem.h"
#include "weapons/Weapon.h"
#include "world/Map.h"
#include "world/DemoLevel.h"
#include "world/LevelLoader.h"
#include "world/OpenWorldManager.h"
#include "world/LevelState.h"

constexpr int   kScreenWidth  = 1280;
constexpr int   kScreenHeight = 720;
constexpr float kPi           = 3.14159265f;
constexpr float kFieldOfView  = 90.0f * kPi / 180.0f;

// ---------------------------------------------------------------------------
// GameState — controls which systems may process input and advance time.
// Loading performs a safe map replacement between playable levels.
// ---------------------------------------------------------------------------
enum class GameState { Menu, Loading, Playing, Paused, Dead, LevelComplete };

// ---------------------------------------------------------------------------
// RenderContext — everything that depends on render resolution.
// Rebuilt whenever the player changes resolution in Settings.
// ---------------------------------------------------------------------------
struct RenderContext {
    int renderWidth;
    int renderHeight;

    Framebuffer    framebuffer;
    SkyRenderer    skyRenderer;
    WallRenderer   wallRenderer;
    SpriteRenderer spriteRenderer;
    PlaneRenderer  planeRenderer;
    TerrainRenderer terrainRenderer;
    MeshRenderer    meshRenderer;

    explicit RenderContext(int rw, int rh)
        : renderWidth(rw), renderHeight(rh),
          framebuffer(rw, rh),
          skyRenderer(rw, rh),
          wallRenderer(rw, rh, kFieldOfView),
          spriteRenderer(rw, rh, kFieldOfView),
          planeRenderer(rw, rh, kFieldOfView),
          terrainRenderer(rw, rh, kFieldOfView),
          meshRenderer(rw, rh, kFieldOfView) {}
};

static std::vector<Enemy> CreateEnemies(const std::vector<EnemySpawn>& spawns) {
    std::vector<Enemy> enemies;
    enemies.reserve(spawns.size());
    for (const EnemySpawn& spawn : spawns) {
        enemies.emplace_back(spawn.position, spawn.type);
    }
    return enemies;
}

static const ExitTrigger* FindExitTrigger(const Vector3& position,
                                          const std::vector<ExitTrigger>& triggers) {
    for (const ExitTrigger& trigger : triggers) {
        if (position.x >= trigger.minX && position.x <= trigger.maxX &&
            position.z >= trigger.minZ && position.z <= trigger.maxZ) {
            return &trigger;
        }
    }
    return nullptr;
}

struct Projectile {
    Vector3 position;
    Vector3 velocity;
    float   radius = 0.25f;
    int     damage = 1;
};

// ---------------------------------------------------------------------------
// AppState — game objects + SDL handles + render context pointer.
// ---------------------------------------------------------------------------
struct AppState {
    AppState(LevelData levelData, std::vector<EnemySpawn> enemySpawns,
             std::string assetDirectory)
        : assets(std::move(assetDirectory)),
          map(std::move(levelData)),
          enemies(CreateEnemies(enemySpawns)) {
        levelState.Reset(map.GetLevelData());
        RebuildPhoneWalls();
    }

    void RebuildPhoneWalls() {
        phoneWalls.clear();
        for (const Sector& s : map.Sectors()) {
            for (const Wall& w : s.walls) {
                phoneWalls.push_back({{w.start.x, w.start.z}, {w.end.x, w.end.z}});
            }
        }
    }

    // SDL
    SDL_Window*   window        = nullptr;
    SDL_Renderer* sdlRenderer   = nullptr;
    SDL_Texture*  screenTexture = nullptr;

    // Audio
    AudioSystem audio;

    // Render context (heap-allocated so it can be replaced without moving AppState)
    std::unique_ptr<RenderContext> rc;

    // Assets are resolution-independent. They are procedural for now, but
    // callers use stable IDs so file loading can replace them later.
    AssetManager assets;

    // --- UI ---
    DebugOverlay debugOverlay;
    Hud          hud;
    Menu         menu;
    Phone        phone;

    // --- Game objects ---
    Player                  player;
    Map                     map;
    LevelState              levelState;
    std::vector<Enemy>      enemies;
    std::vector<Projectile> projectiles;
    Weapon                  weapon;
    std::vector<std::pair<Vector2, Vector2>> phoneWalls;

    // --- State ---
    bool      running          = true;
    bool      restartRequested = false;
    bool      showDebugOverlay = false;
    float     framesPerSecond  = 0.0f;
    float     sunWorldAngle    = 0.3f;
    float     sunElevation     = 0.4f;
    GameState gameState        = GameState::Menu;
    int       activeSaveSlot   = 0;
    std::string assetDirectory;
    std::string currentLevelId = "demo";
    std::string requestedLevelId;
    std::optional<SaveData> pendingLoad;
    int  pendingLoadSlot = -1;
    bool isLevelTransition = false;
    bool isFullscreen = false;
    bool isOpenWorld  = false;
    OpenWorldManager openWorld;

#ifdef DEV_MODE
    DevModeState devMode;
#endif
};

// ---------------------------------------------------------------------------
// RebuildRenderContext — called once at startup and on every resolution change.
// Destroys the old SDL streaming texture, creates a new one matching the
// new render size, then replaces the RenderContext.
// ---------------------------------------------------------------------------
static bool RebuildRenderContext(AppState& app, int renderWidth) {
    const int renderHeight = renderWidth * 9 / 16;

    // Destroy old SDL texture (framebuffer upload target)
    if (app.screenTexture) {
        SDL_DestroyTexture(app.screenTexture);
        app.screenTexture = nullptr;
    }

    // Create new streaming texture at the new size
    app.screenTexture = SDL_CreateTexture(
        app.sdlRenderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING,
        renderWidth, renderHeight);
    if (!app.screenTexture) {
        SDL_Log("SDL_CreateTexture failed: %s", SDL_GetError());
        return false;
    }
    // NEAREST so pixels look sharp when upscaled to the window
    SDL_SetTextureScaleMode(app.screenTexture, SDL_SCALEMODE_NEAREST);

    // Replace the render context
    app.rc = std::make_unique<RenderContext>(renderWidth, renderHeight);

    SDL_Log("Resolution changed to %dx%d", renderWidth, renderHeight);
    return true;
}

// ---------------------------------------------------------------------------
// DestroyApp — releases every SDL object owned by AppState.
// Safe to call after a partial initialization, so all early error paths use
// the same cleanup sequence as normal shutdown.
// ---------------------------------------------------------------------------
static void DestroyApp(AppState& app) {
    app.audio.Shutdown();
    if (app.screenTexture) {
        SDL_DestroyTexture(app.screenTexture);
        app.screenTexture = nullptr;
    }
    if (app.sdlRenderer) {
        SDL_DestroyRenderer(app.sdlRenderer);
        app.sdlRenderer = nullptr;
    }
    if (app.window) {
        SDL_DestroyWindow(app.window);
        app.window = nullptr;
    }
}

// ---------------------------------------------------------------------------
// SyncMusic — plays music in menu/paused states, stops it during gameplay.
// Call after every gameState change.
// ---------------------------------------------------------------------------
static void SyncMusic(AppState& app) {
    const bool inMenu = (app.gameState == GameState::Menu ||
                         app.gameState == GameState::Paused ||
                         app.gameState == GameState::Dead    ||
                         app.gameState == GameState::LevelComplete);
    if (inMenu) {
        if (!app.audio.IsPlaying())
            app.audio.PlayProceduralMusic(0.7f);
    } else {
        app.audio.StopMusic();
    }
}

// ---------------------------------------------------------------------------
// StartNewGame — resets every state that belongs to the current level.
// ---------------------------------------------------------------------------
static void StartNewGame(AppState& app) {
    app.pendingLoad.reset();
    app.pendingLoadSlot = -1;
    app.isLevelTransition = false;
    app.requestedLevelId = "demo";
    app.menu.Hide();
    app.gameState = GameState::Loading;
}

static void RestoreSaveData(AppState& app, const SaveData& data) {
    app.player.LoadState({data.playerX, data.playerY, data.playerZ},
                         data.playerAngle, data.playerPitch, data.playerHealth,
                         data.playerArmor);
    app.weapon.LoadStateV4(data.activeWeaponIndex,
                           data.magazineAmmo, data.reserveAmmo,
                           data.shotgunMag, data.shotgunReserve,
                           data.chaingunMag, data.chaingunReserve,
                           data.weaponCooldown, data.reloadTime);
    app.sunWorldAngle = data.sunWorldAngle;
    app.sunElevation = data.sunElevation;

    for (int index = 0;
         index < data.enemyCount && index < static_cast<int>(app.enemies.size()); ++index) {
        app.enemies[index].LoadState(
            {data.enemies[index].posX, data.enemies[index].posY, data.enemies[index].posZ},
            data.enemies[index].health, data.enemies[index].attackCooldown,
            static_cast<EnemyType>(data.enemies[index].type),
            static_cast<EnemyState>(data.enemies[index].state));
    }
    app.projectiles.clear();

    app.map.ResetPickups();
    const auto& pickups = app.map.Pickups();
    for (int index = 0;
         index < data.pickupCount && index < static_cast<int>(pickups.size()); ++index) {
        if (data.pickups[index].collected) {
            const_cast<Pickup&>(pickups[index]).collected = true;
        }
    }

    // Restore LevelState: doors, keys, message triggers.
    auto& doors = app.levelState.Doors();
    for (int i = 0; i < data.doorCount && i < static_cast<int>(doors.size()); ++i) {
        doors[i].phase        = static_cast<DoorPhase>(data.doors[i].phase);
        doors[i].openFraction = data.doors[i].openFraction;
    }
    auto& keys = app.levelState.Keys();
    for (int i = 0; i < data.keyCount && i < static_cast<int>(keys.size()); ++i) {
        keys[i].collected = data.keys[i].collected;
    }
    auto& triggers = app.levelState.MessageTriggers();
    for (int i = 0; i < data.triggerCount && i < static_cast<int>(triggers.size()); ++i) {
        triggers[i].fired = data.triggers[i].fired;
    }
}

// Replaces all state that belongs to a level only after its data has passed
// LevelLoader validation. A bad next-level file therefore cannot leave Map in
// a half-updated state or terminate the process.
static void LoadRequestedLevel(AppState& app) {
    // --- Open world: synthetic level, no .level file needed ---
    if (app.requestedLevelId == "openworld") {
        app.isOpenWorld = true;

        // Empty LevelData: no sectors, no walls, no Mars terrain.
        // OpenWorldManager owns all geometry for this level.
        LevelData owData;
        owData.marsTerrain = false;
        app.map = Map(std::move(owData));
        app.levelState.Reset(app.map.GetLevelData());
        app.RebuildPhoneWalls();
        app.enemies.clear();
        app.projectiles.clear();

        const Vector3 spawn = OpenWorldManager::SpawnPosition();
        const bool wasTransition = app.isLevelTransition;
        app.isLevelTransition = false;
        const int carryHealth = wasTransition
            ? std::clamp(std::max(app.player.Health(),
                                  Player::kMaximumHealth * 4 / 10),
                         1, Player::kMaximumHealth)
            : Player::kMaximumHealth;
        app.player.LoadState(spawn, 0.0f, 0.0f, carryHealth);
        if (!wasTransition) app.weapon.Reset();

        app.phone           = Phone{};
        app.restartRequested = false;
        app.sunWorldAngle   = 0.3f;
        app.sunElevation    = 0.4f;
        app.currentLevelId  = "openworld";
        app.requestedLevelId.clear();
        app.pendingLoad.reset();
        app.pendingLoadSlot = -1;
        app.hud.ShowMessage("OPEN WORLD");
        app.gameState = GameState::Playing;
        SyncMusic(app);

        // Prime the first chunk load.
        app.openWorld.Update(spawn.x, spawn.z);
        return;
    }

    const std::string path = app.assetDirectory + "/levels/" +
                             app.requestedLevelId + ".level";
    std::string error;
    std::optional<LoadedLevel> loaded = LevelLoader::Load(path, error);
    if (!loaded) {
        const std::string message = "Level transition failed (" +
                                    app.requestedLevelId + "): " + error;
        SDL_Log("%s", message.c_str());
        Logger::Error(message);
        app.pendingLoad.reset();
        app.pendingLoadSlot = -1;
        app.isLevelTransition = false;
        app.requestedLevelId.clear();
        app.gameState = GameState::Menu;
        SyncMusic(app);
        app.menu.Show();
        return;
    }

    const bool wasTransition = app.isLevelTransition;
    app.isLevelTransition = false;
    app.isOpenWorld = false;

    app.map = Map(std::move(loaded->data));
    app.levelState.Reset(app.map.GetLevelData());
    app.RebuildPhoneWalls();
    app.enemies = CreateEnemies(loaded->enemySpawns);
    app.projectiles.clear();

    if (wasTransition) {
        // Carry over health (at least 40% of max so the player has a fighting
        // chance, never above the maximum) and weapons/ammo.
        const int minimumCarry = Player::kMaximumHealth * 4 / 10;
        const int carryHealth = std::clamp(
            std::max(app.player.Health(), minimumCarry), 1, Player::kMaximumHealth);
        app.player.LoadState(loaded->spawnPosition, loaded->spawnYaw,
                             loaded->spawnPitch, carryHealth);
        app.hud.ShowMessage("ENTERING: " + loaded->name);
    } else {
        app.player.LoadState(loaded->spawnPosition, loaded->spawnYaw,
                             loaded->spawnPitch, Player::kMaximumHealth);
        app.weapon.Reset();
    }
    app.player.SnapToTerrain(app.map);

    app.phone = Phone{};
    app.restartRequested = false;
    app.sunWorldAngle = 0.3f;
    app.sunElevation = 0.4f;
    app.currentLevelId = app.requestedLevelId;
    app.requestedLevelId.clear();
    if (app.pendingLoad) {
        RestoreSaveData(app, *app.pendingLoad);
        app.activeSaveSlot = app.pendingLoadSlot;
        Logger::Info("Loaded slot " + std::to_string(app.activeSaveSlot) +
                     " on level: " + app.currentLevelId);
        app.pendingLoad.reset();
        app.pendingLoadSlot = -1;
    } else {
        Logger::Info("Loaded level: " + app.currentLevelId);
    }
    app.gameState = GameState::Playing;
    SyncMusic(app);
}

// ---------------------------------------------------------------------------
// Save / Load helpers
// ---------------------------------------------------------------------------
static void DoSave(AppState& app, int slot) {
    SaveData data;
    std::snprintf(data.levelId, sizeof(data.levelId), "%s", app.currentLevelId.c_str());
    data.playerX      = app.player.Position().x;
    data.playerY      = app.player.Position().y;
    data.playerZ      = app.player.Position().z;
    data.playerAngle  = app.player.Angle();
    data.playerPitch  = app.player.Pitch();
    data.playerHealth = app.player.Health();
    data.playerArmor  = app.player.Armor();

    data.activeWeaponIndex = app.weapon.ActiveWeaponIndex();
    data.magazineAmmo      = app.weapon.SlotMagazineAmmo(0);
    data.reserveAmmo       = app.weapon.SlotReserveAmmo(0);
    data.shotgunMag        = app.weapon.SlotMagazineAmmo(1);
    data.shotgunReserve    = app.weapon.SlotReserveAmmo(1);
    data.chaingunMag       = app.weapon.SlotMagazineAmmo(2);
    data.chaingunReserve   = app.weapon.SlotReserveAmmo(2);
    data.weaponCooldown    = 0.0f;
    data.reloadTime        = 0.0f;

    data.sunWorldAngle = app.sunWorldAngle;
    data.sunElevation  = app.sunElevation;

    data.enemyCount = static_cast<int>(
        std::min(app.enemies.size(), static_cast<size_t>(SaveData::kMaxEnemies)));
    for (int i = 0; i < data.enemyCount; ++i) {
        data.enemies[i] = {
            app.enemies[i].Position().x,
            app.enemies[i].Position().y,
            app.enemies[i].Position().z,
            app.enemies[i].Health(),
            app.enemies[i].AttackCooldown(),
            static_cast<uint8_t>(app.enemies[i].Type()),
            static_cast<uint8_t>(app.enemies[i].State()),
            {0, 0}
        };
    }

    const auto& pickups = app.map.Pickups();
    data.pickupCount = static_cast<int>(
        std::min(pickups.size(), static_cast<size_t>(SaveData::kMaxPickups)));
    for (int i = 0; i < data.pickupCount; ++i) {
        data.pickups[i].collected = pickups[i].collected;
    }

    // Serialise LevelState.
    const auto& stateDoors = app.levelState.Doors();
    data.doorCount = static_cast<int>(
        std::min(stateDoors.size(), static_cast<size_t>(SaveData::kMaxDoors)));
    for (int i = 0; i < data.doorCount; ++i) {
        data.doors[i].doorId       = stateDoors[i].doorId;
        data.doors[i].phase        = static_cast<uint8_t>(stateDoors[i].phase);
        data.doors[i].openFraction = stateDoors[i].openFraction;
    }
    const auto& stateKeys = app.levelState.Keys();
    data.keyCount = static_cast<int>(
        std::min(stateKeys.size(), static_cast<size_t>(SaveData::kMaxKeys)));
    for (int i = 0; i < data.keyCount; ++i) {
        data.keys[i].keyId     = stateKeys[i].keyId;
        data.keys[i].collected = stateKeys[i].collected;
    }
    const auto& stateTriggers = app.levelState.MessageTriggers();
    data.triggerCount = static_cast<int>(
        std::min(stateTriggers.size(), static_cast<size_t>(SaveData::kMaxTriggers)));
    for (int i = 0; i < data.triggerCount; ++i) {
        data.triggers[i].triggerId = stateTriggers[i].triggerId;
        data.triggers[i].fired     = stateTriggers[i].fired;
    }

    if (!SaveSystem::Save(slot, data)) {
        SDL_Log("Save failed: slot %d", slot);
        Logger::Error("Save failed for slot " + std::to_string(slot) + ".");
        return;
    }

    app.activeSaveSlot = slot;
    SDL_Log("Saved to slot %d", slot);
    Logger::Info("Saved slot " + std::to_string(slot) + ".");
}

static void DoLoad(AppState& app, int slot) {
    SaveData data;
    if (!SaveSystem::Load(slot, data)) {
        SDL_Log("Load failed: slot %d", slot);
        Logger::Error("Load failed for slot " + std::to_string(slot) + ".");
        return;
    }

    app.pendingLoad = data;
    app.pendingLoadSlot = slot;
    app.requestedLevelId = data.levelId;
    app.gameState = GameState::Loading;
    SDL_Log("Loading slot %d on level %s", slot, data.levelId);
}

static void SetFullscreen(AppState& app, bool fullscreen) {
    app.isFullscreen = fullscreen;
    if (app.window) {
        SDL_SetWindowFullscreen(app.window, fullscreen);
    }
    app.menu.SetFullscreen(fullscreen);
}

static void ToggleFullscreen(AppState& app) {
    SetFullscreen(app, !app.isFullscreen);
}

// ---------------------------------------------------------------------------
// HandleInput
// ---------------------------------------------------------------------------
static bool HandleInput(AppState& app) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) { return false; }

        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            const SDL_Keycode key = event.key.key;

            // Global fullscreen toggle: F11 or Alt+Enter (works in all screens)
            if (key == SDLK_F11 ||
                ((event.key.mod & SDL_KMOD_ALT) && (key == SDLK_RETURN || key == SDLK_KP_ENTER))) {
                ToggleFullscreen(app);
                return true;
            }

            // ---- Main menu or paused game --------------------------------
            if (app.gameState == GameState::Menu || app.gameState == GameState::Paused) {
                if (key == SDLK_ESCAPE) {
                    if (app.menu.CurrentScreen() != Menu::Screen::Main) {
                        app.menu.Back();
                    } else if (app.gameState == GameState::Paused) {
                        app.menu.Hide();
                        app.gameState = GameState::Playing;
                        SyncMusic(app);
                    }
                } else if (key == SDLK_W || key == SDLK_UP) {
                    app.menu.NavigateUp();
                } else if (key == SDLK_S || key == SDLK_DOWN) {
                    app.menu.NavigateDown();
                } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                    const Menu::MainAction action = app.menu.Confirm();
                    if (action == Menu::MainAction::NewGame) {
                        StartNewGame(app);
                    } else if (action == Menu::MainAction::LoadGame) {
                        DoLoad(app, app.menu.LoadSlot());
                    } else if (action == Menu::MainAction::SaveGame) {
                        DoSave(app, app.menu.SaveSlot());
                        app.gameState = GameState::Playing;
                        SyncMusic(app);
                    } else if (action == Menu::MainAction::ResolutionChanged) {
                        // Rebuild everything at the new render resolution.
                        RebuildRenderContext(app, app.menu.SelectedResolution());
                    } else if (action == Menu::MainAction::FullscreenToggled) {
                        SetFullscreen(app, app.menu.FullscreenEnabled());
                    } else if (action == Menu::MainAction::ConvertMusicProject) {
                        const std::string path = app.assetDirectory + "/music/MusicProject.wav";
                        if (app.audio.ConvertAndApplyWav(path)) {
                            const auto& prof = app.audio.GetSoundtrackProfile();
                            app.menu.SetSoundtrackInfo(prof.name, static_cast<int>(prof.bpm), true, prof.statusMessage);
                        } else {
                            app.menu.SetSoundtrackInfo("CONVERSION FAILED", 0, false, "FILE NOT FOUND OR INVALID WAV");
                        }
                    } else if (action == Menu::MainAction::ConvertCustomWav) {
                        const std::string path = app.assetDirectory + "/music/custom.wav";
                        if (app.audio.ConvertAndApplyWav(path)) {
                            const auto& prof = app.audio.GetSoundtrackProfile();
                            app.menu.SetSoundtrackInfo(prof.name, static_cast<int>(prof.bpm), true, prof.statusMessage);
                        } else {
                            app.menu.SetSoundtrackInfo("CONVERSION FAILED", 0, false, "DROP WAV TO assets/music/custom.wav");
                        }
                    } else if (action == Menu::MainAction::ResetDefaultMusic) {
                        app.audio.ResetToDefaultSoundtrack();
                        const auto& prof = app.audio.GetSoundtrackProfile();
                        app.menu.SetSoundtrackInfo(prof.name, static_cast<int>(prof.bpm), false, "RESET TO MARS AMBIENT");
                    } else if (action == Menu::MainAction::Exit) {
                        return false;
                    }
                }
                return true;
            }

            // ---- Death screen --------------------------------------------
            if (app.gameState == GameState::Dead) {
                if (key == SDLK_R) {
                    StartNewGame(app);
                } else if (key == SDLK_ESCAPE) {
                    app.menu.Show();
                    app.gameState = GameState::Paused;
                    SyncMusic(app);
                }
                return true;
            }

            // ---- Level-complete screen ----------------------------------
            if (app.gameState == GameState::LevelComplete) {
                if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_ESCAPE) {
                    app.menu.Show();
                    app.gameState = GameState::Menu;
                    SyncMusic(app);
                }
                return true;
            }

            // ---- Playing mode --------------------------------------------
            if (app.gameState != GameState::Playing) {
                return true;
            }

            if (key == SDLK_ESCAPE) {
                app.menu.Show();
                app.gameState = GameState::Paused;
                SyncMusic(app);
            } else if (key == SDLK_F3) {
                app.showDebugOverlay = !app.showDebugOverlay;
            } else if (key == SDLK_F) {
                app.phone.Toggle();
            } else if (key == SDLK_R) {
                app.weapon.RequestReload();
            } else if (key == SDLK_1) {
                app.weapon.SelectWeapon(0);
            } else if (key == SDLK_2) {
                app.weapon.SelectWeapon(1);
            } else if (key == SDLK_3) {
                app.weapon.SelectWeapon(2);
            } else if (key == SDLK_Q) {
                app.weapon.CycleWeapon(-1);
            } else if (key == SDLK_E) {
                // Use interaction: open/close the nearest door within reach.
                constexpr float kUseDistance = 2.0f;
                const Vector3& pos = app.player.Position();
                const float fwd_x = std::sin(app.player.Angle());
                const float fwd_z = std::cos(app.player.Angle());
                const Vector3 useTarget{pos.x + fwd_x * kUseDistance,
                                        pos.y,
                                        pos.z + fwd_z * kUseDistance};
                for (const DoorDef& def : app.map.GetLevelData().doors) {
                    // Find closest point on door segment to use-target.
                    const float wx = def.wallEnd.x - def.wallStart.x;
                    const float wz = def.wallEnd.z - def.wallStart.z;
                    const float lenSq = wx * wx + wz * wz;
                    const float t = lenSq > 1e-9f
                        ? std::clamp(((useTarget.x - def.wallStart.x) * wx +
                                      (useTarget.z - def.wallStart.z) * wz) / lenSq, 0.0f, 1.0f)
                        : 0.0f;
                    const float cx = def.wallStart.x + wx * t;
                    const float cz = def.wallStart.z + wz * t;
                    const float dx = pos.x - cx;
                    const float dz = pos.z - cz;
                    if (dx * dx + dz * dz > kUseDistance * kUseDistance) { continue; }
                    // Toggle door phase
                    DoorState* state = app.levelState.FindDoor(def.doorId);
                    if (!state) { continue; }
                    if (state->phase == DoorPhase::Closed || state->phase == DoorPhase::Closing) {
                        if (def.requiredKey != KeyColour::None && !app.levelState.HasKey(def.requiredKey)) {
                            std::string keyName;
                            switch (def.requiredKey) {
                                case KeyColour::Red:    keyName = "RED"; break;
                                case KeyColour::Blue:   keyName = "BLUE"; break;
                                case KeyColour::Yellow: keyName = "YELLOW"; break;
                                default: break;
                            }
                            app.hud.ShowMessage("YOU NEED THE " + keyName + " KEY");
                            break;
                        }
                        state->phase = DoorPhase::Opening;
                    } else {
                        state->phase = DoorPhase::Closing;
                    }
                    break; // interact with at most one door per press
                }
            } else if (key == SDLK_F5) {
                app.menu.ShowSave();
                app.gameState = GameState::Paused;
                SyncMusic(app);
            } else if (key == SDLK_F9) {
                app.menu.Show();
                app.menu.NavigateDown();
                app.menu.Confirm();
                app.gameState = GameState::Paused;
                SyncMusic(app);
            }
        } else if (event.type == SDL_EVENT_MOUSE_WHEEL && app.gameState == GameState::Playing) {
            if (event.wheel.y > 0) {
                app.weapon.CycleWeapon(-1);
            } else if (event.wheel.y < 0) {
                app.weapon.CycleWeapon(1);
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------
static void Update(AppState& app, float deltaSeconds) {
    const float instant = 1.0f / std::max(deltaSeconds, 0.0001f);
    app.framesPerSecond = (app.framesPerSecond == 0.0f)
        ? instant
        : app.framesPerSecond + (instant - app.framesPerSecond) *
          std::min(deltaSeconds * 8.0f, 1.0f);

    if (app.gameState == GameState::Loading) {
        LoadRequestedLevel(app);
        return;
    }

    if (app.gameState != GameState::Playing) { return; }

    // ---- Playing --------------------------------------------------------
    constexpr float kDayDuration = 120.0f;
    app.sunWorldAngle += deltaSeconds * (2.0f * kPi / kDayDuration) * 0.4f;
    app.sunElevation   = 0.22f + 0.55f * std::sin(app.sunWorldAngle * 0.7f);
    if (app.sunElevation < 0.0f) { app.sunElevation = 0.0f; }

    if (app.restartRequested) {
        app.weapon.RequestReload();
        app.restartRequested = false;
    }

    const bool* kb = SDL_GetKeyboardState(nullptr);
    float mdx = 0.0f, mdy = 0.0f;
    const SDL_MouseButtonFlags mb = SDL_GetRelativeMouseState(&mdx, &mdy);
    const bool mouseShoot = (mb & SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) != 0;

    // ---- Door animation ------------------------------------------------
    // Speed: full travel in 1 second (fraction units per second).
    constexpr float kDoorSpeed = 1.0f;
    for (DoorState& ds : app.levelState.Doors()) {
        if (ds.phase == DoorPhase::Opening) {
            ds.openFraction = std::min(1.0f, ds.openFraction + kDoorSpeed * deltaSeconds);
            if (ds.openFraction >= 1.0f) { ds.phase = DoorPhase::Open; }
        } else if (ds.phase == DoorPhase::Closing) {
            ds.openFraction = std::max(0.0f, ds.openFraction - kDoorSpeed * deltaSeconds);
            if (ds.openFraction <= 0.0f) { ds.phase = DoorPhase::Closed; }
        }
    }

    app.player.Update({kb[SDL_SCANCODE_W], kb[SDL_SCANCODE_S],
                       kb[SDL_SCANCODE_A], kb[SDL_SCANCODE_D], mdx, mdy},
                      deltaSeconds, app.map, app.levelState, app.menu.HeadBobEnabled());

    // Player footstep audio cadence
    const bool playerIsMoving = kb[SDL_SCANCODE_W] || kb[SDL_SCANCODE_S] ||
                                kb[SDL_SCANCODE_A] || kb[SDL_SCANCODE_D];
    if (playerIsMoving && app.player.IsAlive()) {
        static float playerStepTimer = 0.0f;
        playerStepTimer += deltaSeconds;
        if (playerStepTimer >= 0.42f) {
            playerStepTimer = 0.0f;
            app.audio.PlaySound(SoundId::PlayerStep, 0.45f);
        }
    }

    // Open world chunk streaming + physics.
    if (app.isOpenWorld) {
        app.openWorld.Update(app.player.Position().x, app.player.Position().z);

        // Build camera forward direction from player yaw+pitch.
        const float yr = app.player.Angle();
        const float pr = app.player.Pitch();
        const Vector3 owFwd = {
             std::sin(yr) * std::cos(pr),
            -std::sin(pr),
             std::cos(yr) * std::cos(pr)
        };
        app.openWorld.UpdatePhysics(deltaSeconds, app.player.EyePosition(), owFwd);

        // ---- Physics interaction keys (only in open world) ----
        phys::PhysicsWorld& pw = app.openWorld.Physics();

        // E — grab / release
        static bool ePrev = false;
        const bool  eNow  = kb[SDL_SCANCODE_E];
        if (eNow && !ePrev) {
            if (pw.Held()) {
                pw.Release();
                app.hud.ShowMessage("RELEASED");
            } else {
                int r = pw.Grab();
                if      (r == 0) app.hud.ShowMessage("GRABBED  (LMB=throw  RMB=strike)");
                else if (r == 1) app.hud.ShowMessage("TOO HEAVY");
                else             app.hud.ShowMessage("NOTHING IN RANGE");
            }
        }
        ePrev = eNow;

        // LMB — throw held object
        const bool lmbNow = (mb & SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) != 0;
        static bool lmbPrev = false;
        if (lmbNow && !lmbPrev && pw.Held()) {
            pw.ThrowHeld({ 0.f, 0.f, 0.f });
            app.hud.ShowMessage("THROWN");
        }
        lmbPrev = lmbNow;

        // RMB — hammer strike
        const bool rmbNow = (mb & SDL_BUTTON_MASK(SDL_BUTTON_RIGHT)) != 0;
        static bool rmbPrev = false;
        if (rmbNow && !rmbPrev) {
            if (pw.Held()) pw.Release();
            int hit = pw.Strike();
            app.hud.ShowMessage(hit >= 0 ? "STRIKE!" : "MISS");
        }
        rmbPrev = rmbNow;

        // 1/2/3 — spawn crate / glass / stone ahead
        auto spawnAhead = [&](int mat) {
            const phys::Vec3 p = {
                pw.eye.x + pw.fwd.x * 2.5f,
                pw.eye.y + pw.fwd.y * 2.5f,
                pw.eye.z + pw.fwd.z * 2.5f
            };
            pw.Spawn(mat, p, phys::quatY(phys::frand() * 6.f), pw.fwd * 2.f);
        };
        static bool k1p=false,k2p=false,k3p=false,kTp=false;
        const bool k1n=kb[SDL_SCANCODE_1],k2n=kb[SDL_SCANCODE_2];
        const bool k3n=kb[SDL_SCANCODE_3],kTn=kb[SDL_SCANCODE_T];
        if (k1n && !k1p) { spawnAhead(0); app.hud.ShowMessage("SPAWNED CRATE"); }
        if (k2n && !k2p) { spawnAhead(1); app.hud.ShowMessage("SPAWNED GLASS"); }
        if (k3n && !k3p) { spawnAhead(2); app.hud.ShowMessage("SPAWNED STONE"); }
        if (kTn && !kTp) {
            const phys::Vec3 base = {
                pw.eye.x + pw.fwd.x * 5.f,
                pw.eye.y,
                pw.eye.z + pw.fwd.z * 5.f
            };
            pw.SpawnWall(base, app.player.Angle());
            app.hud.ShowMessage("WALL BUILT");
        }
        k1p=k1n; k2p=k2n; k3p=k3n; kTp=kTn;
    }

#ifdef DEV_MODE
    // ---- Developer noclip / flight -------------------------------------
    // F1 toggles noclip. Speed: [ = slower, ] = faster.
    {
        static bool f1Prev     = false;
        static bool bracketLPrev = false;
        static bool bracketRPrev = false;
        static bool f2Prev     = false;

        const bool f1Now       = kb[SDL_SCANCODE_F1];
        const bool bracketLNow = kb[SDL_SCANCODE_LEFTBRACKET];
        const bool bracketRNow = kb[SDL_SCANCODE_RIGHTBRACKET];
        const bool f2Now       = kb[SDL_SCANCODE_F2];

        if (f1Now && !f1Prev) {
            app.devMode.active = !app.devMode.active;
            if (app.devMode.active) {
                // Sync tracked Y with current player position on activation.
                app.devMode.posY = app.player.Position().y;
            }
            app.hud.ShowMessage(app.devMode.StatusLine());
        }
        if (bracketLNow && !bracketLPrev && app.devMode.active) {
            app.devMode.SpeedDown();
            app.hud.ShowMessage(app.devMode.StatusLine());
        }
        if (bracketRNow && !bracketRPrev && app.devMode.active) {
            app.devMode.SpeedUp();
            app.hud.ShowMessage(app.devMode.StatusLine());
        }
        if (f2Now && !f2Prev && app.devMode.active) {
            // Teleport to level spawn — reload the current level state without
            // resetting progress (just reposition the player).
            app.hud.ShowMessage("[DEV] TELEPORTED TO SPAWN");
        }

        f1Prev       = f1Now;
        bracketLPrev = bracketLNow;
        bracketRPrev = bracketRNow;
        f2Prev       = f2Now;

        if (app.devMode.active) {
            // Fly up/down: Space = up, LShift = down.
            const bool flyUp   = kb[SDL_SCANCODE_SPACE];
            const bool flyDown = kb[SDL_SCANCODE_LSHIFT];
            DevModeUpdate(app.devMode, app.player,
                          kb[SDL_SCANCODE_W], kb[SDL_SCANCODE_S],
                          kb[SDL_SCANCODE_A], kb[SDL_SCANCODE_D],
                          flyUp, flyDown,
                          mdx, mdy,
                          deltaSeconds);
        }
    }
#endif // DEV_MODE
    app.map.CollectPickups(app.player, app.weapon);
    if (const auto keyColour = app.map.CollectKeys(app.player, app.levelState)) {
        std::string keyName;
        switch (*keyColour) {
            case KeyColour::Red:    keyName = "RED"; break;
            case KeyColour::Blue:   keyName = "BLUE"; break;
            case KeyColour::Yellow: keyName = "YELLOW"; break;
            default: break;
        }
        app.hud.ShowMessage(keyName + " KEY ACQUIRED");
    }
    if (app.map.CollectArmor(app.player)) {
        app.hud.ShowMessage("ARMOR ACQUIRED");
    }
    if (const AudiologDef* log = app.map.CollectAudiolog(app.player)) {
        app.hud.ShowMessage("[AUDIOLOG] " + log->title + ": " + log->text, 6.0f);
    }
    app.map.ApplyHazards(app.player, deltaSeconds);

    for (const MessageTriggerDef& trig : app.map.GetLevelData().messageTriggers) {
        MessageTriggerState* ts = app.levelState.FindTrigger(trig.triggerId);
        if (!ts || ts->fired) { continue; }
        const Vector3& pos = app.player.Position();
        if (pos.x >= trig.minX && pos.x <= trig.maxX &&
            pos.z >= trig.minZ && pos.z <= trig.maxZ) {
            ts->fired = true;
            app.hud.ShowMessage(trig.message);
        }
    }

    app.hud.Update(deltaSeconds);
    if (app.player.IsAlive()) {
        app.weapon.Update(mouseShoot || kb[SDL_SCANCODE_SPACE], deltaSeconds,
                          app.player.Position(), app.player.Angle(),
                          app.map, app.enemies);
        if (app.weapon.JustFired()) {
            switch (app.weapon.ActiveType()) {
                case WeaponType::Pistol:
                    app.audio.PlaySound(SoundId::Pistol, 0.75f);
                    break;
                case WeaponType::Shotgun:
                    app.audio.PlaySound(SoundId::Shotgun, 0.95f);
                    break;
                case WeaponType::Chaingun:
                    app.audio.PlaySound(SoundId::Chaingun, 0.80f);
                    break;
                default:
                    app.audio.PlaySound(SoundId::Pistol, 0.75f);
                    break;
            }
            for (Enemy& e : app.enemies) {
                if (e.IsAlive() && (e.Position() - app.player.Position()).Length() < 22.0f) {
                    e.Alert();
                }
            }
        }
    }

    for (size_t i = 0; i < app.enemies.size(); ++i) {
        std::optional<ProjectileSpawn> projSpawn;
        app.enemies[i].Update(deltaSeconds, app.player, app.map, app.levelState,
                              app.enemies, i, projSpawn);

        const Vector3& enemyPos  = app.enemies[i].Position();
        const Vector3& playerPos = app.player.Position();
        const float    playerYaw = app.player.Angle();

        // 1. Monster alert screech
        if (app.enemies[i].JustAlerted()) {
            app.audio.PlaySpatialSound(SoundId::MonsterAlert, enemyPos, playerPos, playerYaw, 0.90f);
        }
        // 2. Monster attacks
        if (app.enemies[i].JustAttacked()) {
            switch (app.enemies[i].Type()) {
                case EnemyType::MantisScout:
                    app.audio.PlaySpatialSound(SoundId::MonsterAttackScout, enemyPos, playerPos, playerYaw, 0.85f);
                    break;
                case EnemyType::Tank:
                    app.audio.PlaySpatialSound(SoundId::MonsterAttackTank, enemyPos, playerPos, playerYaw, 1.05f);
                    break;
                case EnemyType::Shooter:
                    app.audio.PlaySpatialSound(SoundId::MonsterAttackShooter, enemyPos, playerPos, playerYaw, 0.85f);
                    break;
            }
        }
        // 3. Monster hurt screech
        if (app.enemies[i].JustHurt()) {
            app.audio.PlaySpatialSound(SoundId::MonsterHurt, enemyPos, playerPos, playerYaw, 0.85f);
        }
        // 4. Monster death shriek
        if (app.enemies[i].JustDied()) {
            app.audio.PlaySpatialSound(SoundId::MonsterDeath, enemyPos, playerPos, playerYaw, 1.0f);
        }
        // 5. Monster footsteps / scuttles
        if (app.enemies[i].JustStepped()) {
            const SoundId stepSnd = (app.enemies[i].Type() == EnemyType::Tank)
                                  ? SoundId::MonsterStepHeavy
                                  : SoundId::MonsterStep;
            app.audio.PlaySpatialSound(stepSnd, enemyPos, playerPos, playerYaw, 0.45f);
        }
        if (projSpawn) {
            Vector3 dir = (projSpawn->target - projSpawn->position);
            const float len = dir.Length();
            if (len > 0.001f) {
                dir = dir * (1.0f / len);
            } else {
                dir = Vector3{0.0f, 0.0f, 1.0f};
            }
            constexpr float kProjectileSpeed = 8.0f;
            app.projectiles.push_back(Projectile{
                .position = projSpawn->position,
                .velocity = dir * kProjectileSpeed,
                .radius   = 0.25f,
                .damage   = projSpawn->damage,
            });
        }
    }

    // Update active projectiles
    for (size_t i = 0; i < app.projectiles.size();) {
        Projectile& proj = app.projectiles[i];
        proj.position = proj.position + proj.velocity * deltaSeconds;

        if (app.map.IsPositionBlocked(proj.position, proj.radius) ||
            app.map.IsDoorPositionBlocked(proj.position, proj.radius, app.levelState)) {
            app.projectiles[i] = app.projectiles.back();
            app.projectiles.pop_back();
            continue;
        }

        Vector3 toPlayer = app.player.Position() - proj.position;
        toPlayer.y = (app.player.Position().y + 0.9f) - proj.position.y;
        if (toPlayer.Length() < 0.6f + proj.radius) {
            app.player.TakeDamage(proj.damage);
            app.projectiles[i] = app.projectiles.back();
            app.projectiles.pop_back();
            continue;
        }

        if (proj.position.y < -5.0f || proj.position.y > 20.0f ||
            (proj.position - app.player.Position()).Length() > 50.0f) {
            app.projectiles[i] = app.projectiles.back();
            app.projectiles.pop_back();
            continue;
        }

        ++i;
    }

    if (!app.player.IsAlive()) {
        app.gameState = GameState::Dead;
        SyncMusic(app);
    } else if (const ExitTrigger* exit =
                   FindExitTrigger(app.player.Position(), app.map.ExitTriggers())) {
        if (exit->targetLevel == "complete") {
            app.gameState = GameState::LevelComplete;
            SyncMusic(app);
        } else {
            app.requestedLevelId = exit->targetLevel;
            app.isLevelTransition = true;
            app.gameState = GameState::Loading;
        }
    }

    app.phone.Update(deltaSeconds);
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------
static void Render(AppState& app) {
    RenderContext& rc = *app.rc;
    Framebuffer&   fb = rc.framebuffer;

    const int rw = rc.renderWidth;
    const int rh = rc.renderHeight;

    const Vector3 eyePos = app.player.EyePosition();
    const float   angle  = app.player.Angle();
    const float   pitch  = app.player.Pitch();

    const float focalLen = (static_cast<float>(rw) * 0.5f) /
                           std::tan(kFieldOfView * 0.5f);
    // True 3D camera (yaw + pitch): a real rotation rather than the old
    // horizon-line shift, so floors/ceilings/solids stay correct when looking
    // up/down. At pitch 0 this is identical to the previous yaw-only math.
    const Camera cam = Camera::Make(eyePos, angle, pitch, focalLen, rw, rh);

    rc.skyRenderer.Draw(fb, app.sunWorldAngle, app.sunElevation, angle, pitch);
    fb.ClearDepth();
    const bool isMarsSurface = app.map.HasMarsTerrain();
    if (isMarsSurface) {
        rc.terrainRenderer.Draw(fb, eyePos.x, eyePos.y, eyePos.z, angle, pitch);
    }
    const auto GroundedPosition = [isMarsSurface](Vector3 position) {
        if (isMarsSurface) {
            position.y = MarsTerrain::HeightAt(position.x, position.z);
        }
        return position;
    };

    const std::vector<PointLight>& lights = app.map.PointLights();
    const DirectionalLight sunLight = DirectionalLight::FromAngles(
        app.sunWorldAngle, app.sunElevation);
    const DirectionalLight noSun;

    // When shadows/lighting is disabled in settings, replace dynamic lights
    // with empty lists so everything renders at full ambient brightness.
    const bool shadowsOn = app.menu.ShadowsEnabled();
    static const std::vector<PointLight> kNoLights;
    const std::vector<PointLight>& activeLights = shadowsOn ? lights : kNoLights;

    for (const Sector& sector : app.map.Sectors()) {
        const Texture& floorTex =
            sector.floorMaterial == 0 ? app.assets.GetTexture(TextureId::FirstFloor) :
            sector.floorMaterial == 1 ? app.assets.GetTexture(TextureId::SecondFloor) :
                                        app.assets.GetTexture(TextureId::OutdoorFloor);
        const Texture& ceilTex =
            sector.ceilingMaterial == 0 ? app.assets.GetTexture(TextureId::FirstCeiling) :
                                          app.assets.GetTexture(TextureId::SecondCeiling);
        const float amb = sector.ambientLight;
        const DirectionalLight& secSun = shadowsOn
            ? (sector.ceilingHeight ? noSun : sunLight)
            : noSun;

        if (!isMarsSurface) {
            rc.planeRenderer.DrawPlane(fb, cam,
                sector.floorHeight, sector.vertices, activeLights, amb, secSun, floorTex);
        }
        if (sector.ceilingHeight) {
            rc.planeRenderer.DrawPlane(fb, cam,
                *sector.ceilingHeight, sector.vertices, activeLights, amb, noSun, ceilTex);
        }
        if (!isMarsSurface) {
            for (const Wall& wall : sector.walls) {
                rc.wallRenderer.DrawWall(fb, cam,
                    wall.start, wall.end,
                    wall.bottomHeight.value_or(sector.floorHeight),
                    wall.topHeight.value_or(sector.ceilingHeight.value_or(3.0f)),
                    activeLights, amb, secSun,
                    app.assets.GetTexture(TextureId::Wall));
            }
        }
    }

    // --- Open world chunk geometry ------------------------------------
    if (app.isOpenWorld) {
        const auto& owData = app.openWorld.GetRenderData();
        const Texture& floorTex = app.assets.GetTexture(TextureId::OutdoorFloor);
        const Texture& wallTex  = app.assets.GetTexture(TextureId::Wall);
        for (const Sector& sector : owData.sectors) {
            rc.planeRenderer.DrawPlane(fb, cam,
                sector.floorHeight, sector.vertices,
                activeLights, sector.ambientLight, sunLight, floorTex);
        }
        for (const auto& [wall, amb] : owData.walls) {
            rc.wallRenderer.DrawWall(fb, cam,
                wall.start, wall.end,
                wall.bottomHeight.value_or(0.0f),
                wall.topHeight.value_or(3.0f),
                activeLights, amb, sunLight, wallTex);
        }

        // --- Physics bodies (OBB rigid bodies) ---
        const phys::PhysicsWorld& pw = app.openWorld.Physics();
        for (const phys::Body& body : pw.bodies) {
            if (!body.alive) continue;
            // Colour from body material, brightened if held.
            const bool held = (body.id == pw.heldId);
            const float boost = held ? 1.4f : 1.0f;
            const uint32_t col = Framebuffer::MakeColor(
                static_cast<uint8_t>(std::min(255.f, body.cr * 255.f * boost)),
                static_cast<uint8_t>(std::min(255.f, body.cg * 255.f * boost)),
                static_cast<uint8_t>(std::min(255.f, body.cb * 255.f * boost)));
            const Vector3 pos{ body.pos.x, body.pos.y, body.pos.z };
            const Vector3 half{ body.half.x, body.half.y, body.half.z };
            rc.meshRenderer.DrawMeshOBB(fb, cam, pos, body.R.m, half,
                col, activeLights, 0.4f, sunLight);
        }
    }

    // Render interactive doors: draw as a wall that slides upward as it opens.
    for (const DoorDef& door : app.map.GetLevelData().doors) {
        const DoorState* ds = app.levelState.FindDoor(door.doorId);
        if (!ds || ds->openFraction >= 1.0f) { continue; } // fully open = invisible
        const float doorHeight = door.topHeight - door.bottomHeight;
        const float currentBottom = door.bottomHeight + ds->openFraction * doorHeight;
        if (currentBottom >= door.topHeight) { continue; }
        rc.wallRenderer.DrawWall(fb, cam,
            door.wallStart, door.wallEnd,
            currentBottom, door.topHeight,
            activeLights, 0.15f, noSun,
            app.assets.GetTexture(TextureId::Wall));
    }

    // --- 3D mesh objects ---------------------------------------------------
    // Real polygon-rendered solids (no texture): flat-shaded from the face
    // normal and z-buffered against the rest of the world. Meshes are always
    // lit by the sun so their faces read as 3D regardless of the shadows
    // setting; point lights still respect the toggle via activeLights.
    if (!app.map.MeshObjects().empty()) {
        static const Mesh kCube = MakeCube(1.0f);  // unit cube, scaled per object
        const uint32_t kMeshColor = Framebuffer::MakeColor(190, 140, 100);
        for (const MeshObjectDef& obj : app.map.MeshObjects()) {
            rc.meshRenderer.DrawMesh(fb, cam,
                obj.position, obj.yaw, obj.scale, kCube, kMeshColor,
                activeLights, 0.15f, sunLight);
        }
    }

    constexpr float kOutAmb = 0.4f, kInAmb = 0.15f;

    for (const WorldSprite& sprite : app.map.Sprites()) {
        const Texture* tex = &app.assets.GetTexture(TextureId::Lamp);
        float amb          = kOutAmb;
        const DirectionalLight* spriteSun = &sunLight;
        switch (sprite.spriteType) {
            case SpriteType::Lamp:       tex = &app.assets.GetTexture(TextureId::Lamp);        amb = kOutAmb; spriteSun = shadowsOn ? &sunLight : &noSun; break;
            case SpriteType::CeilingLamp:tex = &app.assets.GetTexture(TextureId::CeilingLamp); amb = 1.0f;    spriteSun = &noSun;    break;
            case SpriteType::Barrel:
                tex = &app.assets.GetTexture(TextureId::Barrel);
                amb = (sprite.position.z < 18.0f) ? kInAmb : kOutAmb;
                spriteSun = (shadowsOn && sprite.position.z >= 18.0f) ? &sunLight : &noSun;
                break;
            case SpriteType::Column:    tex = &app.assets.GetTexture(TextureId::Column);    amb = kInAmb; spriteSun = &noSun; break;
            case SpriteType::Bookshelf: tex = &app.assets.GetTexture(TextureId::Bookshelf); amb = kInAmb; spriteSun = &noSun; break;
        }
        rc.spriteRenderer.DrawSprite(fb, cam,
            GroundedPosition(sprite.position), sprite.width, sprite.height,
            activeLights, amb, *spriteSun, *tex);
    }

    for (const Pickup& p : app.map.Pickups()) {
        if (!p.collected) {
            rc.spriteRenderer.DrawSprite(fb, cam,
                GroundedPosition(p.position), 0.8f, 0.8f, activeLights, kOutAmb,
                shadowsOn ? sunLight : noSun,
                p.healthAmount > 0 ? app.assets.GetTexture(TextureId::Medkit)
                                   : app.assets.GetTexture(TextureId::Ammo));
        }
    }

    for (const Enemy& e : app.enemies) {
        TextureId tex = TextureId::Enemy;
        TextureId corpseTex = TextureId::EnemyCorpse;
        float width = e.Width();
        float height = e.Height();
        float corpseWidth = e.Width() * 1.2f;
        float corpseHeight = 0.45f;

        switch (e.Type()) {
            case EnemyType::MantisScout:
                tex = TextureId::Enemy;
                corpseTex = TextureId::EnemyCorpse;
                break;
            case EnemyType::Shooter:
                tex = TextureId::EnemyShooter;
                corpseTex = TextureId::EnemyShooterCorpse;
                break;
            case EnemyType::Tank:
                tex = TextureId::EnemyTank;
                corpseTex = TextureId::EnemyTankCorpse;
                corpseHeight = 0.55f;
                break;
        }

        const Vector3 groundedEnemyPosition = GroundedPosition(e.Position());
        if (!e.IsAlive()) {
            rc.spriteRenderer.DrawSprite(fb, cam,
                groundedEnemyPosition, corpseWidth, corpseHeight, activeLights, kInAmb, noSun,
                app.assets.GetTexture(corpseTex));
            continue;
        }
        rc.spriteRenderer.DrawSprite(fb, cam,
            groundedEnemyPosition, width, height, activeLights, kInAmb, noSun,
            app.assets.GetTexture(tex), e.IsHurt());
    }

    for (const Projectile& p : app.projectiles) {
        rc.spriteRenderer.DrawSprite(fb, cam,
            p.position, 0.45f, 0.45f, activeLights, 1.0f, noSun,
            app.assets.GetTexture(TextureId::Projectile));
    }

    // Exit triggers are gameplay-only rectangles, so render a bright gate at
    // each centre. This gives the player a visible destination instead of an
    // invisible completion zone at the far end of the map.
    for (const ExitTrigger& trigger : app.map.ExitTriggers()) {
        Vector3 gatePosition{
            (trigger.minX + trigger.maxX) * 0.5f, 0.0f,
            (trigger.minZ + trigger.maxZ) * 0.5f};
        rc.spriteRenderer.DrawSprite(fb, cam,
            GroundedPosition(gatePosition), trigger.maxX - trigger.minX, 4.0f,
            activeLights, 1.0f, noSun,
            app.assets.GetTexture(TextureId::ExitGate));
    }

    for (const KeyPickupDef& keyDef : app.map.GetLevelData().keys) {
        const KeyState* ks = app.levelState.FindKey(keyDef.keyId);
        if (ks && ks->collected) { continue; }
        TextureId texId = TextureId::KeyRed;
        switch (keyDef.colour) {
            case KeyColour::Red:    texId = TextureId::KeyRed; break;
            case KeyColour::Blue:   texId = TextureId::KeyBlue; break;
            case KeyColour::Yellow: texId = TextureId::KeyYellow; break;
            default: break;
        }
        rc.spriteRenderer.DrawSprite(fb, cam,
            GroundedPosition(keyDef.position), 0.5f, 0.7f, activeLights,
            kInAmb, noSun, app.assets.GetTexture(texId));
    }

    // Damage flash & death effect: tint the world before HUD drawing
    if (app.player.DamageFlash() > 0.0f) {
        const float alpha = std::min(0.55f, app.player.DamageFlash() * 1.8f);
        fb.ApplyTint(210, 20, 20, alpha);
    } else if (!app.player.IsAlive()) {
        fb.ApplyTint(180, 0, 0, 0.45f);
    }

    app.hud.Draw(fb, app.player.Health(), Player::kMaximumHealth,
                 app.weapon.MagazineAmmo(), app.weapon.ReserveAmmo(),
                 app.weapon.IsFiring(), app.weapon.IsReloading(),
                 app.weapon.ActiveWeaponIndex(),
                 app.weapon.ActiveWeaponName(),
                 &app.levelState);

    if (app.phone.IsVisible()) {
        app.phone.Draw(fb, app.sunWorldAngle,
                       app.player.Position(), app.player.Angle(), {}, {}, app.phoneWalls);
    }

    if (app.gameState == GameState::Dead) { app.debugOverlay.DrawRestartPrompt(fb); }
    if (app.gameState == GameState::LevelComplete) { app.debugOverlay.DrawLevelComplete(fb); }

    if (app.showDebugOverlay || app.menu.ShowFpsEnabled()) {
        app.debugOverlay.Draw(fb, app.framesPerSecond,
            app.player.Position(), app.player.Angle(),
            app.player.Health(),
            static_cast<int>(app.map.Sectors().size()), app.map.WallCount());
    }

    if (app.gameState == GameState::Menu || app.gameState == GameState::Paused) {
        app.menu.Draw(fb);
    }

    SDL_UpdateTexture(app.screenTexture, nullptr, fb.Data(),
                      rw * static_cast<int>(sizeof(uint32_t)));
    SDL_RenderClear(app.sdlRenderer);
    SDL_RenderTexture(app.sdlRenderer, app.screenTexture, nullptr, nullptr);
    SDL_RenderPresent(app.sdlRenderer);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int Game::Run(int argc, char* argv[]) {
    bool startFullscreen = false;
    std::string gameArg;   // --game <folder|file.mdpk> or a bare file path
    for (int i = 1; i < argc; ++i) {
        if (!argv || !argv[i]) { continue; }
        const std::string_view arg = argv[i];
        if (arg == "-f" || arg == "--fullscreen") {
            startFullscreen = true;
        } else if (arg == "-w" || arg == "--windowed") {
            startFullscreen = false;
        } else if (arg == "--game") {
            if (i + 1 >= argc || !argv[i + 1]) {
                SDL_Log("--game requires a folder or .mdpk file");
                return 1;
            }
            gameArg = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            SDL_Log("MyDoomEngine options:\n"
                    "  -f, --fullscreen  Launch in fullscreen mode\n"
                    "  -w, --windowed    Launch in windowed mode (default)\n"
                    "  --game <path>     Game to run: a folder, or a .mdpk game pack\n"
                    "                    (also: MyDoomEngine mygame.mdpk)\n"
                    "                    default: assets/ or game.mdpk next to the exe\n"
                    "  -h, --help        Show this help message\n\n"
                    "In-game controls:\n"
                    "  F11 / Alt+Enter   Toggle fullscreen on/off\n"
                    "  ESC               Pause / Options menu\n");
            return 0;
        } else if (!arg.empty() && arg.front() != '-') {
            gameArg = std::string(arg);   // drag a .mdpk onto the executable
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    std::string prefPath;
    char* saveDirectory = SDL_GetPrefPath("Fedir", "MyDoomEngine");
    if (!saveDirectory) {
        SDL_Log("SDL_GetPrefPath failed, using relative saves directory: %s", SDL_GetError());
    } else {
        SaveSystem::SetSaveDirectory(saveDirectory);
        Logger::Initialize(saveDirectory);
        prefPath = saveDirectory;
        SDL_free(saveDirectory);
    }
    Logger::Info("SDL video subsystem initialized.");

    std::string assetDirectory = "assets";
    const char* basePath = SDL_GetBasePath();
    if (!basePath) {
        Logger::Error("SDL_GetBasePath failed; using relative assets directory: " +
                      std::string(SDL_GetError()));
    } else {
        assetDirectory = std::string(basePath) + "assets";
    }
    {
        // Folder or .mdpk pack (unpacked into the cache) -> asset directory.
        const std::filesystem::path cacheRoot = prefPath.empty()
            ? std::filesystem::temp_directory_path() / "MyDoomEngine" / "games"
            : std::filesystem::path(prefPath) / "games";
        std::string resolvedDir, gameError;
        if (GamePack::ResolveGameDir(gameArg, basePath ? basePath : "", cacheRoot,
                                     resolvedDir, gameError)) {
            assetDirectory = resolvedDir;
            Logger::Info("Game data: " + assetDirectory);
        } else if (!gameArg.empty()) {
            Logger::Error("Cannot load game: " + gameError);
            SDL_Log("Cannot load game: %s", gameError.c_str());
            SDL_Quit();
            return 1;
        } else {
            Logger::Error("No game data found, using built-in fallback: " + gameError);
        }
    }

    std::string levelError;
    std::optional<LoadedLevel> loadedLevel = LevelLoader::Load(
        assetDirectory + "/levels/demo.level", levelError);
    if (!loadedLevel) {
        Logger::Error("Demo level load failed; using fallback: " + levelError);
    } else {
        Logger::Info("Loaded level: " + loadedLevel->name);
    }

    const Vector3 spawnPosition = loadedLevel
        ? loadedLevel->spawnPosition : Vector3{0.0f, Player::kEyeHeight, 0.0f};
    const float spawnYaw = loadedLevel ? loadedLevel->spawnYaw : 0.0f;
    const float spawnPitch = loadedLevel ? loadedLevel->spawnPitch : 0.0f;
    std::vector<EnemySpawn> enemySpawns = loadedLevel
        ? std::move(loadedLevel->enemySpawns)
        : std::vector<EnemySpawn>{{{0.0f, 0.0f, 36.0f}}};
    AppState app(loadedLevel ? std::move(loadedLevel->data) : CreateDemoLevel(),
                 std::move(enemySpawns), assetDirectory);
    app.assetDirectory = assetDirectory;
    app.player.LoadState(spawnPosition, spawnYaw, spawnPitch, Player::kMaximumHealth);
    app.player.SnapToTerrain(app.map);

    app.window = SDL_CreateWindow(
        "Mars: Quiet Swarm — ESC menu  F5 save  F9 load",
        kScreenWidth, kScreenHeight, SDL_WINDOW_RESIZABLE);
    if (!app.window) {
        SDL_Log("CreateWindow: %s", SDL_GetError());
        Logger::Error("SDL_CreateWindow failed: " + std::string(SDL_GetError()));
        DestroyApp(app);
        Logger::Shutdown();
        SDL_Quit();
        return 1;
    }

    app.sdlRenderer = SDL_CreateRenderer(app.window, nullptr);
    if (!app.sdlRenderer) {
        SDL_Log("CreateRenderer: %s", SDL_GetError());
        Logger::Error("SDL_CreateRenderer failed: " + std::string(SDL_GetError()));
        DestroyApp(app);
        Logger::Shutdown();
        SDL_Quit();
        return 1;
    }

    SDL_SetRenderLogicalPresentation(
        app.sdlRenderer, kScreenWidth, kScreenHeight, SDL_LOGICAL_PRESENTATION_LETTERBOX);

    if (startFullscreen) {
        SetFullscreen(app, true);
    }

    // Build the initial render context at the resolution chosen in Menu defaults (640).
    if (!RebuildRenderContext(app, app.menu.SelectedResolution())) {
        Logger::Error("Initial render-context creation failed.");
        DestroyApp(app);
        Logger::Shutdown();
        SDL_Quit();
        return 1;
    }

    SDL_SetWindowRelativeMouseMode(app.window, true);

    // --- Audio -----------------------------------------------------------
    // Procedural synthesizer: no external files or audio libraries required.
    // SyncMusic starts procedural soundtrack if gameState == Menu.
    app.audio.Init();
    SyncMusic(app);

    Uint64 lastTicks = SDL_GetTicksNS();
    while (app.running) {
        const Uint64 now = SDL_GetTicksNS();
        const float dt   = static_cast<float>(now - lastTicks) / 1e9f;
        lastTicks = now;

        app.audio.Tick();   // keep looping stream filled
        app.running = HandleInput(app);
        Update(app, dt);
        Render(app);
    }

    DestroyApp(app);
    Logger::Info("Application shut down normally.");
    Logger::Shutdown();
    SDL_Quit();
    std::puts("MyDoomEngine exited cleanly.");
    return 0;
}
