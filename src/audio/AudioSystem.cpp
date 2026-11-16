#include "AudioSystem.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

// ===========================================================================
//  Audio engine constants
// ===========================================================================
constexpr float    kPi                = 3.14159265358979323846f;
constexpr float    kTwoPi             = 2.0f * kPi;
constexpr int      kSampleRate        = 44100;
constexpr int      kChannels          = 2;          // stereo output
constexpr int      kChunkFrames       = 512;        // frames synthesized per slice
constexpr int      kStreamBufferFrames= 8192;       // ~185ms buffer queued in SDL

// ===========================================================================
//  Fast 32-bit PRNG (Xorshift) — zero external libraries, fast & deterministic
// ===========================================================================
struct NoiseGen {
    uint32_t state = 0x853c49e6u;

    // Generates a floating point value in [-1.0, 1.0]
    float Next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(static_cast<int32_t>(state)) * (1.0f / 2147483648.0f);
    }
};

// ===========================================================================
//  Simple 1-pole Low-Pass Filter
// ===========================================================================
struct OnePoleLP {
    float state = 0.0f;

    float Process(float in, float coef) {
        state += coef * (in - state);
        return state;
    }
};

// ===========================================================================
//  Procedural Sci-Fi Synthesizer ("Mars: Quiet Swarm" menu atmosphere)
// ===========================================================================
struct ProceduralSynth {
    float sr = static_cast<float>(kSampleRate);
    float dt = 1.0f / static_cast<float>(kSampleRate);
    float t  = 0.0f;

    NoiseGen noise;

    // --- 1. Martian Wind (filtered stereo noise sweep) ---
    OnePoleLP windLPL;
    OnePoleLP windLPR;
    float windLfoPhase = 0.0f;

    // --- 2. Deep Sub-Bass Drone (A1 / 55 Hz with binaural stereo beating) ---
    float subPhaseL = 0.0f;
    float subPhaseR = 0.0f;
    float subPhaseHarm = 0.0f;

    // --- 3. Atmospheric Pad (4-voice chord with smooth frequency morphing) ---
    float padPhase[4]   = { 0.0f, 0.0f, 0.0f, 0.0f };
    float padFreq[4]    = { 110.0f, 130.81f, 164.81f, 196.0f };
    float targetFreq[4] = { 110.0f, 130.81f, 164.81f, 196.0f };
    OnePoleLP padFilterL;
    OnePoleLP padFilterR;
    int   chordIndex    = 0;
    float chordTimer    = 0.0f;

    // Chord table: Am9 -> Fmaj7 -> Dm9 -> Em7
    static constexpr float kChords[4][4] = {
        { 110.00f, 130.81f, 164.81f, 196.00f }, // Am9 (A2, C3, E3, G3)
        {  87.31f, 110.00f, 130.81f, 164.81f }, // Fmaj7 (F2, A2, C3, E3)
        {  73.42f,  87.31f, 110.00f, 130.81f }, // Dm9 (D2, F2, A2, C3)
        {  82.41f,  98.00f, 123.47f, 146.83f }  // Em7 (E2, G2, B2, D3)
    };

    // --- 4. Cosmic Arpeggio with Stereo Echo ---
    float arpPhase   = 0.0f;
    int   arpStep    = 0;
    float arpTimer   = 0.0f;
    float arpEnv     = 0.0f;

    static constexpr float kArpNotes[16] = {
        220.00f, 261.63f, 329.63f, 392.00f,
        440.00f, 392.00f, 329.63f, 261.63f,
        220.00f, 329.63f, 261.63f, 440.00f,
        392.00f, 329.63f, 261.63f, 196.00f
    };

    // Stereo delay line for arpeggio (~400ms echo)
    static constexpr int kDelayLen = 18000;
    std::vector<float> delayL = std::vector<float>(kDelayLen, 0.0f);
    std::vector<float> delayR = std::vector<float>(kDelayLen, 0.0f);
    int delayWriteIdx = 0;

    // --- 5. Subtle Sub-Pulse / Heartbeat ---
    float kickPhase = 0.0f;
    float kickFreq  = 0.0f;
    float kickAmp   = 0.0f;
    int   lastBeat  = -1;

    // --- 6. Soft Metallic Tick (Hi-hat) ---
    float hatAmp   = 0.0f;
    int   lastHalf = -1;

    // Tempo parameters: 72 BPM
    static constexpr float kBPM      = 72.0f;
    static constexpr float kBeatDur  = 60.0f / kBPM;          // ~0.833s
    static constexpr float kStepDur  = kBeatDur * 0.25f;       // 16th note ~0.208s
    static constexpr float kChordDur = kBeatDur * 8.0f;        // 2 bars per chord ~6.66s

    // Soft limiter (smooth 3rd-order saturation curve)
    static float SoftLimit(float x) {
        if (x >  1.2f) return 0.95f;
        if (x < -1.2f) return -0.95f;
        return x / (1.0f + 0.4f * std::abs(x));
    }

    void FillStereo(float* out, int frames, float volume) {
        for (int i = 0; i < frames; ++i) {

            // ===============================================================
            // 1. Martian Wind Sweep (stereo decorrelated filtered noise)
            // ===============================================================
            windLfoPhase += 0.08f * dt;
            if (windLfoPhase > 1.0f) windLfoPhase -= 1.0f;

            const float windCutoffL = 0.012f + 0.008f * std::sin(windLfoPhase * kTwoPi);
            const float windCutoffR = 0.012f + 0.008f * std::sin((windLfoPhase + 0.25f) * kTwoPi);

            const float rawNoiseL = noise.Next();
            const float rawNoiseR = noise.Next();
            const float windL = windLPL.Process(rawNoiseL, windCutoffL) * 0.12f;
            const float windR = windLPR.Process(rawNoiseR, windCutoffR) * 0.12f;

            // ===============================================================
            // 2. Deep Sub-Bass Drone (binaural beating in stereo)
            // ===============================================================
            // Left at 54.7 Hz, Right at 55.3 Hz -> 0.6 Hz slow stereo beat
            subPhaseL += 54.7f * dt;
            if (subPhaseL >= 1.0f) subPhaseL -= 1.0f;

            subPhaseR += 55.3f * dt;
            if (subPhaseR >= 1.0f) subPhaseR -= 1.0f;

            subPhaseHarm += 110.0f * dt;
            if (subPhaseHarm >= 1.0f) subPhaseHarm -= 1.0f;

            const float subBassL = std::sin(subPhaseL * kTwoPi) * 0.22f +
                                   std::sin(subPhaseHarm * kTwoPi) * 0.05f;
            const float subBassR = std::sin(subPhaseR * kTwoPi) * 0.22f +
                                   std::sin(subPhaseHarm * kTwoPi) * 0.05f;

            // ===============================================================
            // 3. Atmospheric Pad (smooth frequency glide & stereo panning)
            // ===============================================================
            chordTimer += dt;
            if (chordTimer >= kChordDur) {
                chordTimer -= kChordDur;
                chordIndex = (chordIndex + 1) % 4;
                for (int k = 0; k < 4; ++k) {
                    targetFreq[k] = kChords[chordIndex][k];
                }
            }

            // Smooth portamento frequency glide between chords (no clicks)
            for (int k = 0; k < 4; ++k) {
                padFreq[k] += (targetFreq[k] - padFreq[k]) * (dt * 2.5f);
                padPhase[k] += padFreq[k] * dt;
                if (padPhase[k] >= 1.0f) padPhase[k] -= 1.0f;
            }

            // Stereo panning of chord voices (voice 0 & 1 lean left, 2 & 3 lean right)
            const float s0 = std::sin(padPhase[0] * kTwoPi);
            const float s1 = std::sin(padPhase[1] * kTwoPi);
            const float s2 = std::sin(padPhase[2] * kTwoPi);
            const float s3 = std::sin(padPhase[3] * kTwoPi);

            float rawPadL = (s0 * 0.7f + s1 * 0.5f + s2 * 0.3f + s3 * 0.1f) * 0.065f;
            float rawPadR = (s0 * 0.1f + s1 * 0.3f + s2 * 0.5f + s3 * 0.7f) * 0.065f;

            // Gentle filter sweep
            const float padLfo = 0.045f + 0.025f * std::sin(t * 0.2f * kTwoPi);
            const float padL = padFilterL.Process(rawPadL, padLfo);
            const float padR = padFilterR.Process(rawPadR, padLfo);

            // ===============================================================
            // 4. Cosmic Arpeggio & Stereo Echo
            // ===============================================================
            arpTimer += dt;
            if (arpTimer >= kStepDur) {
                arpTimer -= kStepDur;
                arpStep = (arpStep + 1) % 16;
                arpEnv  = 0.75f;
            }

            arpPhase += kArpNotes[arpStep] * dt;
            if (arpPhase >= 1.0f) arpPhase -= 1.0f;

            // Triangle / sine hybrid oscillator
            const float tri = (arpPhase < 0.5f) ? (4.0f * arpPhase - 1.0f)
                                                : (3.0f - 4.0f * arpPhase);
            const float arpVoice = tri * arpEnv * 0.06f;
            arpEnv *= std::exp(-dt * 6.0f);   // note decay

            // Write to stereo delay line and read ping-pong taps
            const int tapL = (delayWriteIdx - 13230 + kDelayLen) % kDelayLen; // 300ms
            const int tapR = (delayWriteIdx - 17640 + kDelayLen) % kDelayLen; // 400ms

            const float echoL = delayL[tapL];
            const float echoR = delayR[tapR];

            delayL[delayWriteIdx] = arpVoice * 0.7f + echoR * 0.38f;
            delayR[delayWriteIdx] = arpVoice * 0.3f + echoL * 0.38f;
            delayWriteIdx = (delayWriteIdx + 1) % kDelayLen;

            const float finalArpL = arpVoice * 0.6f + echoL * 0.5f;
            const float finalArpR = arpVoice * 0.4f + echoR * 0.5f;

            // ===============================================================
            // 5. Subtle Sub-Pulse (heartbeat kick on beats 1 & 3)
            // ===============================================================
            const int beatIdx = static_cast<int>(t / kBeatDur);
            if (beatIdx != lastBeat) {
                lastBeat = beatIdx;
                if (beatIdx % 2 == 0) {
                    kickFreq = 88.0f;
                    kickAmp  = 0.55f;
                    kickPhase = 0.0f;
                }
            }
            kickPhase += kickFreq * dt;
            if (kickPhase >= 1.0f) kickPhase -= 1.0f;
            kickFreq *= std::exp(-dt * 22.0f);  // pitch drop
            kickAmp  *= std::exp(-dt * 12.0f);  // volume decay
            const float kick = std::sin(kickPhase * kTwoPi) * kickAmp;

            // ===============================================================
            // 6. Soft Metallic Tick (hi-hat on 8th notes)
            // ===============================================================
            const int halfBeat = static_cast<int>(t / (kBeatDur * 0.5f));
            if (halfBeat != lastHalf) {
                lastHalf = halfBeat;
                hatAmp   = 0.085f;
            }
            const float hatNoise = noise.Next();
            const float hat = hatNoise * hatAmp;
            hatAmp *= std::exp(-dt * 70.0f);

            // ===============================================================
            // 7. Master Mix & Soft Limiter
            // ===============================================================
            const float mixL = windL + subBassL + padL + finalArpL + kick + hat;
            const float mixR = windR + subBassR + padR + finalArpR + kick + hat;

            out[i * 2 + 0] = SoftLimit(mixL * 1.25f) * volume;
            out[i * 2 + 1] = SoftLimit(mixR * 1.25f) * volume;

            t += dt;
            if (t > 1.0e5f) t -= 1.0e5f;
        }
    }
};

} // namespace

// ===========================================================================
//  AudioSystem::Impl
// ===========================================================================
struct AudioSystem::Impl {
    SDL_AudioDeviceID  device = 0;
    SDL_AudioStream*   stream = nullptr;
    ProceduralSynth    synth;
    std::vector<float> buffer;
};

// ===========================================================================
//  AudioSystem public methods
// ===========================================================================

AudioSystem::~AudioSystem() {
    Shutdown();
}

bool AudioSystem::Init() {
    if (m_running) return true;

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_Log("[Audio] SDL_InitSubSystem(AUDIO) failed: %s", SDL_GetError());
        return false;
    }

    m_impl = new Impl{};
    m_running = true;
    SDL_Log("[Audio] Procedural audio subsystem ready.");
    return true;
}

bool AudioSystem::PlayProceduralMusic(float volume) {
    if (!m_running || !m_impl) return false;

    StopMusic();

    SDL_AudioSpec spec{};
    spec.format   = SDL_AUDIO_F32;
    spec.channels = kChannels;
    spec.freq     = kSampleRate;

    m_impl->device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (!m_impl->device) {
        SDL_Log("[Audio] SDL_OpenAudioDevice failed: %s", SDL_GetError());
        return false;
    }

    m_impl->stream = SDL_CreateAudioStream(&spec, &spec);
    if (!m_impl->stream) {
        SDL_Log("[Audio] SDL_CreateAudioStream failed: %s", SDL_GetError());
        SDL_CloseAudioDevice(m_impl->device);
        m_impl->device = 0;
        return false;
    }

    if (!SDL_BindAudioStream(m_impl->device, m_impl->stream)) {
        SDL_Log("[Audio] SDL_BindAudioStream failed: %s", SDL_GetError());
        SDL_DestroyAudioStream(m_impl->stream);
        m_impl->stream = nullptr;
        SDL_CloseAudioDevice(m_impl->device);
        m_impl->device = 0;
        return false;
    }

    m_volume = volume;
    m_paused = false;
    m_impl->synth = ProceduralSynth{};

    // Pre-fill buffer before unpausing device so audio starts immediately
    Tick();

    SDL_ResumeAudioDevice(m_impl->device);
    SDL_Log("[Audio] Procedural music active (Mars ambient soundtrack).");
    return true;
}

void AudioSystem::StopMusic() {
    if (!m_impl) return;
    if (m_impl->stream) {
        SDL_DestroyAudioStream(m_impl->stream);
        m_impl->stream = nullptr;
    }
    if (m_impl->device) {
        SDL_CloseAudioDevice(m_impl->device);
        m_impl->device = 0;
    }
}

void AudioSystem::SetPaused(bool paused) {
    if (!m_impl || !m_impl->device) return;
    m_paused = paused;
    if (paused) {
        SDL_PauseAudioDevice(m_impl->device);
    } else {
        SDL_ResumeAudioDevice(m_impl->device);
    }
}

void AudioSystem::SetVolume(float v) {
    m_volume = std::clamp(v, 0.0f, 1.0f);
}

bool AudioSystem::IsPlaying() const {
    return m_running && m_impl && m_impl->device != 0 && !m_paused;
}

void AudioSystem::Tick() {
    if (!m_impl || !m_impl->stream || m_paused) return;

    const int bytesPerFrame = kChannels * static_cast<int>(sizeof(float));
    const int targetBytes   = kStreamBufferFrames * bytesPerFrame;

    while (SDL_GetAudioStreamAvailable(m_impl->stream) < targetBytes) {
        m_impl->buffer.resize(kChunkFrames * kChannels);
        m_impl->synth.FillStereo(m_impl->buffer.data(), kChunkFrames, m_volume);
        SDL_PutAudioStreamData(m_impl->stream,
                               m_impl->buffer.data(),
                               kChunkFrames * bytesPerFrame);
    }
}

void AudioSystem::Shutdown() {
    if (!m_running) return;
    StopMusic();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    delete m_impl;
    m_impl = nullptr;
    m_running = false;
    SDL_Log("[Audio] Shutdown complete.");
}
