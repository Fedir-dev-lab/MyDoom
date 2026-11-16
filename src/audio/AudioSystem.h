#pragma once
// ---------------------------------------------------------------------------
// AudioSystem — procedural dark-ambient music synthesizer using SDL3 audio.
//
// No external files or audio libraries are needed. All audio is synthesized
// in real-time from scratch (analog-style pads, sub-bass drone, Martian wind,
// cosmic arpeggios, and subtle rhythmic pulses).
//
// Usage:
//   AudioSystem audio;
//   audio.Init();
//   audio.PlayProceduralMusic();   // starts procedural soundtrack
//   audio.SetVolume(0.7f);
//   // Call Tick() once per frame to keep the stream buffer fed.
//   audio.StopMusic();
//   audio.Shutdown();
// ---------------------------------------------------------------------------
#include <string>

class AudioSystem {
public:
    AudioSystem() = default;
    ~AudioSystem();

    // Opens SDL audio subsystem. Call once after SDL_Init.
    bool Init();

    // Starts the procedural music synthesizer (loops indefinitely).
    bool PlayProceduralMusic(float volume = 0.7f);

    // Convenience overloads (file-free procedural music).
    bool PlayMusic(float volume = 0.7f) {
        return PlayProceduralMusic(volume);
    }
    bool PlayMusic(const std::string& /*unusedPath*/, float volume = 0.7f) {
        return PlayProceduralMusic(volume);
    }

    // Stop all audio output immediately.
    void StopMusic();

    // Pause / resume without resetting synth state.
    void SetPaused(bool paused);

    // 0.0 = silent, 1.0 = full volume.
    void SetVolume(float v);

    float Volume()    const { return m_volume; }
    bool  IsPlaying() const;

    // Call once per frame to keep the audio stream fed.
    void Tick();

    // Closes audio device and releases all resources.
    void Shutdown();

private:
    struct Impl;
    Impl* m_impl    = nullptr;
    float m_volume  = 0.7f;
    bool  m_paused  = false;
    bool  m_running = false;
};
