#include "AudioSystem.h"

#include <SDL3/SDL.h>

#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Internal implementation — holds every SDL audio object.
// ---------------------------------------------------------------------------
struct AudioSystem::Impl {
    SDL_AudioDeviceID device  = 0;
    SDL_AudioStream*  stream  = nullptr;

    // Raw decoded WAV samples (kept for looping).
    std::vector<Uint8> wavData;
    SDL_AudioSpec      wavSpec{};

    // Looping callback bookkeeping (driven by SDL_GetAudioStreamAvailable).
    // We refill the stream whenever it runs low.
    bool looping = false;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static constexpr int kStreamBufferMs = 200;   // ms of audio to keep queued

// ---------------------------------------------------------------------------
AudioSystem::~AudioSystem() { Shutdown(); }

// ---------------------------------------------------------------------------
bool AudioSystem::Init() {
    if (m_running) return true;

    // SDL audio subsystem (safe to call even if SDL_INIT_VIDEO is already up).
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_Log("[Audio] SDL_InitSubSystem(AUDIO) failed: %s", SDL_GetError());
        return false;
    }

    m_impl    = new Impl{};
    m_running = true;
    SDL_Log("[Audio] Subsystem ready.");
    return true;
}

// ---------------------------------------------------------------------------
bool AudioSystem::PlayMusic(const std::string& wavPath, float volume) {
    if (!m_running || !m_impl) return false;

    StopMusic();   // stop & clean up previous track

    // --- Load WAV --------------------------------------------------------
    SDL_AudioSpec spec{};
    Uint8*        buf  = nullptr;
    Uint32        len  = 0;
    if (!SDL_LoadWAV(wavPath.c_str(), &spec, &buf, &len)) {
        SDL_Log("[Audio] SDL_LoadWAV('%s') failed: %s", wavPath.c_str(), SDL_GetError());
        return false;
    }
    m_impl->wavData.assign(buf, buf + len);
    m_impl->wavSpec = spec;
    SDL_free(buf);

    // --- Open device in the WAV's native format --------------------------
    m_impl->device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (!m_impl->device) {
        SDL_Log("[Audio] SDL_OpenAudioDevice failed: %s", SDL_GetError());
        m_impl->wavData.clear();
        return false;
    }

    // --- Create stream ---------------------------------------------------
    m_impl->stream = SDL_CreateAudioStream(&spec, &spec);
    if (!m_impl->stream) {
        SDL_Log("[Audio] SDL_CreateAudioStream failed: %s", SDL_GetError());
        SDL_CloseAudioDevice(m_impl->device);
        m_impl->device = 0;
        m_impl->wavData.clear();
        return false;
    }

    if (!SDL_BindAudioStream(m_impl->device, m_impl->stream)) {
        SDL_Log("[Audio] SDL_BindAudioStream failed: %s", SDL_GetError());
        SDL_DestroyAudioStream(m_impl->stream);
        m_impl->stream = nullptr;
        SDL_CloseAudioDevice(m_impl->device);
        m_impl->device = 0;
        m_impl->wavData.clear();
        return false;
    }

    m_impl->looping = true;
    m_volume        = volume;
    m_paused        = false;

    // Prime the stream with two full copies so playback starts immediately
    // without an audible gap at the first loop boundary.
    SDL_PutAudioStreamData(m_impl->stream,
                           m_impl->wavData.data(),
                           static_cast<int>(m_impl->wavData.size()));
    SDL_PutAudioStreamData(m_impl->stream,
                           m_impl->wavData.data(),
                           static_cast<int>(m_impl->wavData.size()));

    SDL_SetAudioStreamGain(m_impl->stream, m_volume);
    SDL_ResumeAudioDevice(m_impl->device);

    SDL_Log("[Audio] Playing '%s' (looping, vol=%.2f)", wavPath.c_str(), m_volume);
    return true;
}

// ---------------------------------------------------------------------------
void AudioSystem::StopMusic() {
    if (!m_impl) return;
    m_impl->looping = false;
    if (m_impl->stream) {
        SDL_DestroyAudioStream(m_impl->stream);  // also unbinds from device
        m_impl->stream = nullptr;
    }
    if (m_impl->device) {
        SDL_CloseAudioDevice(m_impl->device);
        m_impl->device = 0;
    }
    m_impl->wavData.clear();
}

// ---------------------------------------------------------------------------
void AudioSystem::SetPaused(bool paused) {
    if (!m_impl || !m_impl->device) return;
    m_paused = paused;
    if (paused)
        SDL_PauseAudioDevice(m_impl->device);
    else
        SDL_ResumeAudioDevice(m_impl->device);
}

// ---------------------------------------------------------------------------
void AudioSystem::SetVolume(float v) {
    m_volume = (v < 0.f) ? 0.f : (v > 1.f) ? 1.f : v;
    if (m_impl && m_impl->stream)
        SDL_SetAudioStreamGain(m_impl->stream, m_volume);
}

// ---------------------------------------------------------------------------
bool AudioSystem::IsPlaying() const {
    return m_running && m_impl && m_impl->device != 0 && !m_paused;
}

// ---------------------------------------------------------------------------
void AudioSystem::Shutdown() {
    if (!m_running) return;
    StopMusic();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    delete m_impl;
    m_impl    = nullptr;
    m_running = false;
    SDL_Log("[Audio] Shutdown.");
}

// ---------------------------------------------------------------------------
// Tick — call once per frame to keep the loop stream filled.
// ---------------------------------------------------------------------------
void AudioSystem::Tick() {
    if (!m_impl || !m_impl->stream || !m_impl->looping || m_impl->wavData.empty()) return;

    const SDL_AudioSpec& s = m_impl->wavSpec;
    const int bytesPerSec  = s.freq * SDL_AUDIO_BYTESIZE(s.format) * s.channels;
    const int threshold    = bytesPerSec * kStreamBufferMs / 1000;

    while (SDL_GetAudioStreamAvailable(m_impl->stream) < threshold) {
        SDL_PutAudioStreamData(m_impl->stream,
                               m_impl->wavData.data(),
                               static_cast<int>(m_impl->wavData.size()));
    }
}
