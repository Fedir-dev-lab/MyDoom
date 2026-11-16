#pragma once
// ---------------------------------------------------------------------------
// AudioSystem — procedural synthesizer & 3D sound effects engine using SDL3.
//
// 100% generated in real-time from mathematical algorithms (DSP).
// Zero external files (.wav, .mp3) and zero third-party audio libraries.
//
// Features:
//   - Dark sci-fi ambient menu music (pads, binaural drone, wind, arpeggio)
//   - Weapon fire sounds (Pistol, Shotgun, Chaingun)
//   - Player & monster footsteps
//   - Monster roars, attacks (Scout slash, Tank smash, Shooter acid spit),
//     hurt screeches, and death sounds
//   - 3D spatial audio with distance attenuation and stereo panning
// ---------------------------------------------------------------------------
#include <string>
#include "core/Vector3.h"

enum class SoundId {
    Pistol,
    Shotgun,
    Chaingun,
    PlayerStep,
    MonsterStep,
    MonsterStepHeavy,
    MonsterAlert,
    MonsterAttackScout,
    MonsterAttackTank,
    MonsterAttackShooter,
    MonsterHurt,
    MonsterDeath,
};

class AudioSystem {
public:
    AudioSystem() = default;
    ~AudioSystem();

    // Opens SDL audio subsystem and stream. Call once after SDL_Init.
    bool Init();

    // Starts the procedural ambient music (used in Menu/Paused states).
    bool PlayProceduralMusic(float volume = 0.7f);

    // Stops background music (sound effects continue playing).
    void StopMusic();

    // Convenience aliases
    bool PlayMusic(float volume = 0.7f) {
        return PlayProceduralMusic(volume);
    }
    bool PlayMusic(const std::string& /*unusedPath*/, float volume = 0.7f) {
        return PlayProceduralMusic(volume);
    }

    // Pause / resume entire audio output.
    void SetPaused(bool paused);

    // 0.0 = silent, 1.0 = full volume.
    void SetVolume(float v);

    float Volume()    const { return m_volume; }
    bool  IsPlaying() const { return m_musicActive; }

    // Play a procedural sound effect.
    // pan: -1.0 (full left), 0.0 (center), +1.0 (full right)
    void PlaySound(SoundId id, float volume = 1.0f, float pan = 0.0f);

    // Play a sound positioned in 3D world space relative to the listener.
    void PlaySpatialSound(SoundId id, const Vector3& emitterPos,
                          const Vector3& listenerPos, float listenerAngle,
                          float baseVolume = 1.0f);

    // Call once per frame in the main game loop to keep audio stream fed.
    void Tick();

    // Closes audio device and releases all resources.
    void Shutdown();

private:
    struct Impl;
    Impl* m_impl        = nullptr;
    float m_volume      = 0.7f;
    bool  m_musicActive = false;
    bool  m_paused      = false;
    bool  m_running     = false;
};
