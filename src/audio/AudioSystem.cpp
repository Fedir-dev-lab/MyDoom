#include "AudioSystem.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

// ===========================================================================
//  Audio constants
// ===========================================================================
constexpr float    kPi                = 3.14159265358979323846f;
constexpr float    kTwoPi             = 2.0f * kPi;
constexpr int      kSampleRate        = 44100;
constexpr int      kChannels          = 2;          // stereo
constexpr int      kChunkFrames       = 512;        // frames per update slice
constexpr int      kStreamBufferFrames= 8192;       // ~185ms buffer
constexpr int      kMaxVoices         = 24;         // polyphonic SFX voice pool

// ===========================================================================
//  Fast 32-bit PRNG (Xorshift) — zero external libraries, fast & deterministic
// ===========================================================================
struct NoiseGen {
    uint32_t state = 0x853c49e6u;

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
//  Active SFX Voice
// ===========================================================================
struct SoundVoice {
    bool    active      = false;
    SoundId id          = SoundId::Pistol;
    float   time        = 0.0f;
    float   duration    = 0.0f;
    float   volume      = 1.0f;
    float   panL        = 0.7f;
    float   panR        = 0.7f;
    float   phase1      = 0.0f;
    float   phase2      = 0.0f;
    float   filterState = 0.0f;
    float   customParam = 0.0f;
};

// ===========================================================================
//  Procedural Sci-Fi Synthesizer ("Mars: Quiet Swarm" menu atmosphere)
// ===========================================================================
struct ProceduralSynth {
    float dt = 1.0f / static_cast<float>(kSampleRate);
    float t  = 0.0f;

    NoiseGen noise;

    // --- 1. Martian Wind ---
    OnePoleLP windLPL;
    OnePoleLP windLPR;
    float windLfoPhase = 0.0f;

    // --- 2. Deep Sub-Bass Drone ---
    float subPhaseL = 0.0f;
    float subPhaseR = 0.0f;
    float subPhaseHarm = 0.0f;

    // --- 3. Atmospheric Pad ---
    float padPhase[4]   = { 0.0f, 0.0f, 0.0f, 0.0f };
    float padFreq[4]    = { 110.0f, 130.81f, 164.81f, 196.0f };
    float targetFreq[4] = { 110.0f, 130.81f, 164.81f, 196.0f };
    OnePoleLP padFilterL;
    OnePoleLP padFilterR;
    int   chordIndex    = 0;
    float chordTimer    = 0.0f;

    static constexpr float kChords[4][4] = {
        { 110.00f, 130.81f, 164.81f, 196.00f }, // Am9
        {  87.31f, 110.00f, 130.81f, 164.81f }, // Fmaj7
        {  73.42f,  87.31f, 110.00f, 130.81f }, // Dm9
        {  82.41f,  98.00f, 123.47f, 146.83f }  // Em7
    };

    // --- 4. Cosmic Arpeggio & Stereo Echo ---
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

    static constexpr int kDelayLen = 18000;
    std::vector<float> delayL = std::vector<float>(kDelayLen, 0.0f);
    std::vector<float> delayR = std::vector<float>(kDelayLen, 0.0f);
    int delayWriteIdx = 0;

    // --- 5. Subtle Sub-Pulse ---
    float kickPhase = 0.0f;
    float kickFreq  = 0.0f;
    float kickAmp   = 0.0f;
    int   lastBeat  = -1;

    // --- 6. Soft Metallic Tick ---
    float hatAmp   = 0.0f;
    int   lastHalf = -1;

    static constexpr float kBPM      = 72.0f;
    static constexpr float kBeatDur  = 60.0f / kBPM;
    static constexpr float kStepDur  = kBeatDur * 0.25f;
    static constexpr float kChordDur = kBeatDur * 8.0f;

    void ProcessSample(float& outL, float& outR) {
        // 1. Wind
        windLfoPhase += 0.08f * dt;
        if (windLfoPhase > 1.0f) windLfoPhase -= 1.0f;
        const float windCutoffL = 0.012f + 0.008f * std::sin(windLfoPhase * kTwoPi);
        const float windCutoffR = 0.012f + 0.008f * std::sin((windLfoPhase + 0.25f) * kTwoPi);
        const float windL = windLPL.Process(noise.Next(), windCutoffL) * 0.12f;
        const float windR = windLPR.Process(noise.Next(), windCutoffR) * 0.12f;

        // 2. Sub-Bass
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

        // 3. Pad
        chordTimer += dt;
        if (chordTimer >= kChordDur) {
            chordTimer -= kChordDur;
            chordIndex = (chordIndex + 1) % 4;
            for (int k = 0; k < 4; ++k) {
                targetFreq[k] = kChords[chordIndex][k];
            }
        }
        for (int k = 0; k < 4; ++k) {
            padFreq[k] += (targetFreq[k] - padFreq[k]) * (dt * 2.5f);
            padPhase[k] += padFreq[k] * dt;
            if (padPhase[k] >= 1.0f) padPhase[k] -= 1.0f;
        }
        const float s0 = std::sin(padPhase[0] * kTwoPi);
        const float s1 = std::sin(padPhase[1] * kTwoPi);
        const float s2 = std::sin(padPhase[2] * kTwoPi);
        const float s3 = std::sin(padPhase[3] * kTwoPi);
        const float rawPadL = (s0 * 0.7f + s1 * 0.5f + s2 * 0.3f + s3 * 0.1f) * 0.065f;
        const float rawPadR = (s0 * 0.1f + s1 * 0.3f + s2 * 0.5f + s3 * 0.7f) * 0.065f;
        const float padLfo = 0.045f + 0.025f * std::sin(t * 0.2f * kTwoPi);
        const float padL = padFilterL.Process(rawPadL, padLfo);
        const float padR = padFilterR.Process(rawPadR, padLfo);

        // 4. Arpeggio & Delay
        arpTimer += dt;
        if (arpTimer >= kStepDur) {
            arpTimer -= kStepDur;
            arpStep = (arpStep + 1) % 16;
            arpEnv  = 0.75f;
        }
        arpPhase += kArpNotes[arpStep] * dt;
        if (arpPhase >= 1.0f) arpPhase -= 1.0f;
        const float tri = (arpPhase < 0.5f) ? (4.0f * arpPhase - 1.0f)
                                            : (3.0f - 4.0f * arpPhase);
        const float arpVoice = tri * arpEnv * 0.06f;
        arpEnv *= std::exp(-dt * 6.0f);

        const int tapL = (delayWriteIdx - 13230 + kDelayLen) % kDelayLen;
        const int tapR = (delayWriteIdx - 17640 + kDelayLen) % kDelayLen;
        const float echoL = delayL[tapL];
        const float echoR = delayR[tapR];
        delayL[delayWriteIdx] = arpVoice * 0.7f + echoR * 0.38f;
        delayR[delayWriteIdx] = arpVoice * 0.3f + echoL * 0.38f;
        delayWriteIdx = (delayWriteIdx + 1) % kDelayLen;
        const float finalArpL = arpVoice * 0.6f + echoL * 0.5f;
        const float finalArpR = arpVoice * 0.4f + echoR * 0.5f;

        // 5. Kick
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
        kickFreq *= std::exp(-dt * 22.0f);
        kickAmp  *= std::exp(-dt * 12.0f);
        const float kick = std::sin(kickPhase * kTwoPi) * kickAmp;

        // 6. Hi-hat
        const int halfBeat = static_cast<int>(t / (kBeatDur * 0.5f));
        if (halfBeat != lastHalf) {
            lastHalf = halfBeat;
            hatAmp   = 0.085f;
        }
        const float hat = noise.Next() * hatAmp;
        hatAmp *= std::exp(-dt * 70.0f);

        outL = windL + subBassL + padL + finalArpL + kick + hat;
        outR = windR + subBassR + padR + finalArpR + kick + hat;

        t += dt;
        if (t > 1.0e5f) t -= 1.0e5f;
    }
};

static float SoftLimit(float x) {
    if (x >  1.2f) return 0.95f;
    if (x < -1.2f) return -0.95f;
    return x / (1.0f + 0.4f * std::abs(x));
}

} // namespace

// ===========================================================================
//  AudioSystem::Impl
// ===========================================================================
struct AudioSystem::Impl {
    SDL_AudioDeviceID        device = 0;
    SDL_AudioStream*         stream = nullptr;
    ProceduralSynth          musicSynth;
    NoiseGen                 sfxNoise;
    std::vector<SoundVoice>  voices = std::vector<SoundVoice>(kMaxVoices);
    std::vector<float>       buffer;

    float ProcessVoice(SoundVoice& v, float dt) {
        float sample = 0.0f;
        const float curT = v.time;

        switch (v.id) {
            case SoundId::Pistol: {
                // Punchy tactical shot: pitch plunge + transient pop
                const float freq = 55.0f + 340.0f * std::exp(-curT * 45.0f);
                v.phase1 += freq * dt;
                if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;
                const float body = std::sin(v.phase1 * kTwoPi) * std::exp(-curT * 26.0f);
                const float pop  = sfxNoise.Next() * std::exp(-curT * 55.0f) * 0.45f;
                sample = (body * 0.7f + pop) * 1.5f;
                break;
            }

            case SoundId::Shotgun: {
                // Heavy explosive blast with sub boom & noise tail
                const float freq = 35.0f + 185.0f * std::exp(-curT * 11.0f);
                v.phase1 += freq * dt;
                if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;
                const float sub = std::sin(v.phase1 * kTwoPi) * std::exp(-curT * 7.5f);
                const float n   = sfxNoise.Next() * std::exp(-curT * 12.0f);
                v.filterState += 0.22f * (n - v.filterState);
                sample = (sub * 0.85f + v.filterState * 0.95f) * 1.6f;
                break;
            }

            case SoundId::Chaingun: {
                // Rapid metallic crack
                const float freq = 90.0f + 420.0f * std::exp(-curT * 60.0f);
                v.phase1 += freq * dt;
                if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;
                const float env  = std::exp(-curT * 32.0f);
                const float square = (v.phase1 < 0.5f ? 1.0f : -1.0f) * 0.35f;
                const float sine   = std::sin(v.phase1 * kTwoPi) * 0.65f;
                const float crack  = sfxNoise.Next() * std::exp(-curT * 50.0f) * 0.45f;
                sample = ((square + sine) * env + crack) * 1.5f;
                break;
            }

            case SoundId::PlayerStep: {
                // Boot impact on Martian surface (gravel/metal thud)
                const float baseF = 85.0f + v.customParam * 14.0f;
                const float freq  = baseF * std::exp(-curT * 25.0f);
                v.phase1 += freq * dt;
                if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;
                const float thud   = std::sin(v.phase1 * kTwoPi) * std::exp(-curT * 35.0f);
                const float crunch = sfxNoise.Next() * std::exp(-curT * 50.0f) * 0.25f;
                sample = (thud * 0.75f + crunch) * 1.1f;
                break;
            }

            case SoundId::MonsterStep: {
                // Chitinous leg tap/scrape
                const float freq = 220.0f + 550.0f * std::exp(-curT * 65.0f);
                v.phase1 += freq * dt;
                if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;
                const float tap    = std::sin(v.phase1 * kTwoPi) * std::exp(-curT * 40.0f);
                const float scrape = sfxNoise.Next() * std::exp(-curT * 55.0f) * 0.25f;
                sample = (tap * 0.8f + scrape) * 0.9f;
                break;
            }

            case SoundId::MonsterStepHeavy: {
                // Heavy Tank stomp
                const float freq = 42.0f + 110.0f * std::exp(-curT * 22.0f);
                v.phase1 += freq * dt;
                if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;
                const float thud = std::sin(v.phase1 * kTwoPi) * std::exp(-curT * 14.0f);
                sample = thud * 1.3f;
                break;
            }

            case SoundId::MonsterAlert: {
                // Alien Mantis screech / battle hiss (FM synthesis)
                v.phase2 += 48.0f * dt;
                if (v.phase2 >= 1.0f) v.phase2 -= 1.0f;
                const float mod = std::sin(v.phase2 * kTwoPi) * 3.6f;

                const float carFreq = 320.0f + 180.0f * std::sin(curT * (kPi / v.duration));
                v.phase1 += carFreq * dt;
                if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;

                const float env     = std::sin(curT * (kPi / v.duration));
                const float screech = std::sin(v.phase1 * kTwoPi + mod);
                const float hiss    = sfxNoise.Next() * 0.35f;
                sample = (screech * 0.7f + hiss) * env * 1.4f;
                break;
            }

            case SoundId::MonsterAttackScout: {
                // Razor slash whoosh + bite snap
                v.phase1 += (140.0f + 550.0f * std::exp(-curT * 28.0f)) * dt;
                if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;
                const float snap   = std::sin(v.phase1 * kTwoPi) * std::exp(-curT * 18.0f);
                const float whoosh = sfxNoise.Next() * std::sin(curT * (kPi / v.duration)) * 0.5f;
                sample = (snap * 0.75f + whoosh) * 1.35f;
                break;
            }

            case SoundId::MonsterAttackTank: {
                // Guttural roar & crushing ground smash
                v.phase2 += 26.0f * dt;
                if (v.phase2 >= 1.0f) v.phase2 -= 1.0f;
                const float mod = std::sin(v.phase2 * kTwoPi) * 2.8f;

                const float smashF = 35.0f + 115.0f * std::exp(-curT * 12.0f);
                v.phase1 += smashF * dt;
                if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;

                const float roar  = std::sin(v.phase1 * kTwoPi + mod) * 0.55f;
                const float smash = std::sin(v.phase1 * kTwoPi) * 0.75f;
                sample = (roar + smash) * std::exp(-curT * 4.5f) * 1.5f;
                break;
            }

            case SoundId::MonsterAttackShooter: {
                // Acid spit projectile launch (bubbling chirp + squelch)
                const float chirp = 190.0f + 620.0f * std::exp(-curT * 14.0f) +
                                    45.0f * std::sin(curT * 60.0f);
                v.phase1 += chirp * dt;
                if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;
                const float env   = std::sin(curT * (kPi / v.duration));
                const float splat = sfxNoise.Next() * 0.3f * env;
                sample = (std::sin(v.phase1 * kTwoPi) * 0.8f + splat) * env * 1.3f;
                break;
            }

            case SoundId::MonsterHurt: {
                // Pain shriek (rapid FM modulation)
                v.phase2 += 75.0f * dt;
                if (v.phase2 >= 1.0f) v.phase2 -= 1.0f;
                const float mod = std::sin(v.phase2 * kTwoPi) * 2.6f;

                const float carF = 520.0f + 70.0f * std::sin(curT * 85.0f);
                v.phase1 += carF * dt;
                if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;

                const float env = std::exp(-curT * 16.0f);
                sample = std::sin(v.phase1 * kTwoPi + mod) * env * 1.4f;
                break;
            }

            case SoundId::MonsterDeath: {
                // Dying descent screech & squelch
                const float progress = std::clamp(curT / v.duration, 0.0f, 1.0f);
                const float env = 1.0f - progress;
                const float freq = 45.0f + 360.0f * env;

                v.phase2 += 35.0f * dt;
                if (v.phase2 >= 1.0f) v.phase2 -= 1.0f;
                const float mod = std::sin(v.phase2 * kTwoPi) * (1.8f * env);

                v.phase1 += freq * dt;
                if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;

                const float screech = std::sin(v.phase1 * kTwoPi + mod) * env;
                const float rumble  = sfxNoise.Next() * 0.25f * env;
                sample = (screech * 0.75f + rumble) * 1.4f;
                break;
            }
        }

        v.time += dt;
        if (v.time >= v.duration) {
            v.active = false;
        }

        return sample * v.volume;
    }
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

    SDL_AudioSpec spec{};
    spec.format   = SDL_AUDIO_F32;
    spec.channels = kChannels;
    spec.freq     = kSampleRate;

    m_impl->device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (!m_impl->device) {
        SDL_Log("[Audio] SDL_OpenAudioDevice failed: %s", SDL_GetError());
        delete m_impl;
        m_impl = nullptr;
        return false;
    }

    m_impl->stream = SDL_CreateAudioStream(&spec, &spec);
    if (!m_impl->stream) {
        SDL_Log("[Audio] SDL_CreateAudioStream failed: %s", SDL_GetError());
        SDL_CloseAudioDevice(m_impl->device);
        delete m_impl;
        m_impl = nullptr;
        return false;
    }

    if (!SDL_BindAudioStream(m_impl->device, m_impl->stream)) {
        SDL_Log("[Audio] SDL_BindAudioStream failed: %s", SDL_GetError());
        SDL_DestroyAudioStream(m_impl->stream);
        SDL_CloseAudioDevice(m_impl->device);
        delete m_impl;
        m_impl = nullptr;
        return false;
    }

    m_running     = true;
    m_paused      = false;
    m_musicActive = false;

    // Fill initial buffer so playback is immediate
    Tick();
    SDL_ResumeAudioDevice(m_impl->device);

    SDL_Log("[Audio] Subsystem ready (procedural music & SFX).");
    return true;
}

bool AudioSystem::PlayProceduralMusic(float volume) {
    if (!m_running || !m_impl) return false;
    m_musicActive = true;
    m_volume      = std::clamp(volume, 0.0f, 1.0f);
    m_impl->musicSynth = ProceduralSynth{};
    SDL_Log("[Audio] Procedural music started.");
    return true;
}

void AudioSystem::StopMusic() {
    m_musicActive = false;
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

void AudioSystem::PlaySound(SoundId id, float volume, float pan) {
    if (!m_running || !m_impl) return;

    pan = std::clamp(pan, -1.0f, 1.0f);
    // Constant-power panning
    const float panAngle = (pan + 1.0f) * 0.25f * kPi; // [0, PI/2]
    const float panL = std::cos(panAngle);
    const float panR = std::sin(panAngle);

    // Duration table per sound
    float duration = 0.2f;
    switch (id) {
        case SoundId::Pistol:              duration = 0.16f; break;
        case SoundId::Shotgun:             duration = 0.45f; break;
        case SoundId::Chaingun:            duration = 0.12f; break;
        case SoundId::PlayerStep:          duration = 0.09f; break;
        case SoundId::MonsterStep:         duration = 0.08f; break;
        case SoundId::MonsterStepHeavy:    duration = 0.18f; break;
        case SoundId::MonsterAlert:        duration = 0.38f; break;
        case SoundId::MonsterAttackScout:  duration = 0.24f; break;
        case SoundId::MonsterAttackTank:   duration = 0.45f; break;
        case SoundId::MonsterAttackShooter:duration = 0.30f; break;
        case SoundId::MonsterHurt:         duration = 0.18f; break;
        case SoundId::MonsterDeath:        duration = 0.55f; break;
    }

    // Voice allocation: find first inactive voice, or steal the oldest
    int targetSlot = -1;
    float maxAge = -1.0f;
    for (int i = 0; i < kMaxVoices; ++i) {
        if (!m_impl->voices[i].active) {
            targetSlot = i;
            break;
        }
        if (m_impl->voices[i].time > maxAge) {
            maxAge = m_impl->voices[i].time;
            targetSlot = i;
        }
    }

    if (targetSlot >= 0) {
        SoundVoice& v = m_impl->voices[targetSlot];
        v.active      = true;
        v.id          = id;
        v.time        = 0.0f;
        v.duration    = duration;
        v.volume      = std::clamp(volume, 0.0f, 2.0f);
        v.panL        = panL;
        v.panR        = panR;
        v.phase1      = 0.0f;
        v.phase2      = 0.0f;
        v.filterState = 0.0f;
        v.customParam = (id == SoundId::PlayerStep) ? (std::rand() % 2 == 0 ? 0.0f : 1.0f) : 0.0f;
    }
}

void AudioSystem::PlaySpatialSound(SoundId id, const Vector3& emitterPos,
                                   const Vector3& listenerPos, float listenerAngle,
                                   float baseVolume) {
    const float dx = emitterPos.x - listenerPos.x;
    const float dz = emitterPos.z - listenerPos.z;
    const float distSq = dx * dx + dz * dz;
    constexpr float kMaxDist = 32.0f;
    if (distSq > kMaxDist * kMaxDist) return;

    const float dist = std::sqrt(distSq);
    // Realistic distance attenuation: 1.0 at 0m, ~0.5 at 6m, ~0.2 at 20m
    const float distGain = 1.0f / (1.0f + dist * 0.14f);
    const float vol = baseVolume * distGain;
    if (vol < 0.01f) return;

    // Stereo panning based on listener's yaw
    const float rightX = std::cos(listenerAngle);
    const float rightZ = -std::sin(listenerAngle);
    const float dotRight = (dist > 0.001f) ? (dx * rightX + dz * rightZ) / dist : 0.0f;
    const float pan = std::clamp(dotRight, -1.0f, 1.0f);

    PlaySound(id, vol, pan);
}

void AudioSystem::Tick() {
    if (!m_impl || !m_impl->stream || m_paused) return;

    const int bytesPerFrame = kChannels * static_cast<int>(sizeof(float));
    const int targetBytes   = kStreamBufferFrames * bytesPerFrame;
    const float dt          = 1.0f / static_cast<float>(kSampleRate);

    while (SDL_GetAudioStreamAvailable(m_impl->stream) < targetBytes) {
        m_impl->buffer.resize(kChunkFrames * kChannels);

        for (int i = 0; i < kChunkFrames; ++i) {
            float musicL = 0.0f, musicR = 0.0f;
            if (m_musicActive) {
                m_impl->musicSynth.ProcessSample(musicL, musicR);
            }

            float sfxL = 0.0f, sfxR = 0.0f;
            for (SoundVoice& v : m_impl->voices) {
                if (v.active) {
                    const float s = m_impl->ProcessVoice(v, dt);
                    sfxL += s * v.panL;
                    sfxR += s * v.panR;
                }
            }

            const float totalL = (m_musicActive ? musicL * m_volume : 0.0f) + sfxL;
            const float totalR = (m_musicActive ? musicR * m_volume : 0.0f) + sfxR;

            m_impl->buffer[i * 2 + 0] = SoftLimit(totalL * 1.2f);
            m_impl->buffer[i * 2 + 1] = SoftLimit(totalR * 1.2f);
        }

        SDL_PutAudioStreamData(m_impl->stream,
                               m_impl->buffer.data(),
                               kChunkFrames * bytesPerFrame);
    }
}

void AudioSystem::Shutdown() {
    if (!m_running) return;
    StopMusic();
    if (m_impl) {
        if (m_impl->stream) {
            SDL_DestroyAudioStream(m_impl->stream);
            m_impl->stream = nullptr;
        }
        if (m_impl->device) {
            SDL_CloseAudioDevice(m_impl->device);
            m_impl->device = 0;
        }
        delete m_impl;
        m_impl = nullptr;
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    m_running = false;
    SDL_Log("[Audio] Shutdown complete.");
}
