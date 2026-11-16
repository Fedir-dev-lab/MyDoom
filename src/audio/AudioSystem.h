#pragma once
// ---------------------------------------------------------------------------
// AudioSystem — simple looping music player using SDL3 audio streams.
//
// Usage:
//   AudioSystem audio;
//   audio.Init();
//   audio.PlayMusic("assets/music/MusicProject.wav");  // loops forever
//   audio.SetVolume(0.6f);
//   audio.StopMusic();
//   audio.Shutdown();
// ---------------------------------------------------------------------------
#include <string>

class AudioSystem {
public:
    AudioSystem() = default;
    ~AudioSystem();

    // Opens SDL audio device. Must be called once after SDL_Init.
    bool Init();

    // Loads and starts looping a WAV file. Stops any currently playing music.
    // Returns false on error (non-fatal — game continues without music).
    bool PlayMusic(const std::string& wavPath, float volume = 0.7f);

    // Stop the current music immediately.
    void StopMusic();

    // Pause / resume without unloading.
    void SetPaused(bool paused);

    // 0.0 = silent, 1.0 = full volume.
    void SetVolume(float v);

    float Volume() const { return m_volume; }
    bool  IsPlaying() const;

    // Call once per frame to keep the loop stream filled (seamless repeat).
    void Tick();

    // Closes audio device and releases all resources.
    void Shutdown();

private:
    // Pimpl via raw pointers to avoid leaking SDL headers into every TU.
    struct Impl;
    Impl* m_impl = nullptr;

    float m_volume  = 0.7f;
    bool  m_paused  = false;
    bool  m_running = false;
};
