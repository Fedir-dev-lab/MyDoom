#include "AudioSystem.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

// ===========================================================================
//  Acoustic & Audio Engine Constants
// ===========================================================================
constexpr float    kPi                = 3.14159265358979323846f;
constexpr float    kTwoPi             = 2.0f * kPi;
constexpr int      kSampleRate        = 44100;
constexpr int      kChannels          = 2;          // stereo
constexpr int      kChunkFrames       = 512;        // frames per update slice
constexpr int      kStreamBufferFrames= 8192;       // ~185ms buffer queued
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
//  Pink Noise Filter (Voss-McCartney 1/f acoustic modeling)
//  Simulates realistic air turbulence, gas expansion, and kinetic friction
// ===========================================================================
struct PinkFilter {
    float b0 = 0.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;

    float Process(float white) {
        b0 = 0.99886f * b0 + white * 0.0555179f;
        b1 = 0.99332f * b1 + white * 0.0750759f;
        b2 = 0.96900f * b2 + white * 0.1538520f;
        return (b0 + b1 + b2 + white * 0.5362f) * 0.35f;
    }
};

// ===========================================================================
//  2-Pole State-Variable Resonant Filter (SVF)
//  Used for vocal tract formants, gun barrel acoustic resonance, and surface thuds
// ===========================================================================
struct ResonantFilter {
    float lp = 0.0f;
    float bp = 0.0f;

    void Process(float in, float cutoffHz, float q) {
        float f = 2.0f * std::sin(kPi * cutoffHz / static_cast<float>(kSampleRate));
        f = std::clamp(f, 0.0005f, 0.95f);
        float damp = std::clamp(1.0f / q, 0.04f, 2.0f);
        float hp = in - lp - damp * bp;
        bp += f * hp;
        lp += f * bp;
    }
};

// Simple 1-pole Low-Pass Filter
struct OnePoleLP {
    float state = 0.0f;

    float Process(float in, float coef) {
        state += coef * (in - state);
        return state;
    }
};

// Nonlinear Acoustic Soft Saturation
inline float Saturate(float x) {
    return x / (1.0f + 0.35f * std::abs(x));
}

// Master limiter
inline float MasterLimit(float x) {
    if (x >  1.2f) return 0.95f;
    if (x < -1.2f) return -0.95f;
    return x / (1.0f + 0.45f * std::abs(x));
}

// ===========================================================================
//  Physical Modeling SFX Voice
// ===========================================================================
struct SoundVoice {
    bool           active      = false;
    SoundId        id          = SoundId::Pistol;
    float          time        = 0.0f;
    float          duration    = 0.0f;
    float          volume      = 1.0f;
    float          panL        = 0.7f;
    float          panR        = 0.7f;

    // Oscillators & phase accumulators
    float          phase1      = 0.0f;
    float          phase2      = 0.0f;
    float          phase3      = 0.0f;

    // Dual resonant acoustic filters
    ResonantFilter resFilt1;
    ResonantFilter resFilt2;
    OnePoleLP      onePole1;
    OnePoleLP      onePole2;
    PinkFilter     pinkFilt;

    // Micro-variations
    float          pitchVar    = 1.0f;
    float          seed        = 0.0f;
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

    float bpm         = 72.0f;
    float subBassFreq = 55.0f;
    float chords[4][4] = {
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

    float arpNotes[16] = {
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

    void LoadProfile(const SoundtrackProfile& profile) {
        bpm = profile.bpm;
        subBassFreq = profile.subBassFreq;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                chords[i][j] = profile.chords[i][j];
            }
        }
        for (int i = 0; i < 16; ++i) {
            arpNotes[i] = profile.arpNotes[i];
        }
        for (int k = 0; k < 4; ++k) {
            targetFreq[k] = chords[chordIndex % 4][k];
        }
    }

    void ProcessSample(float& outL, float& outR) {
        const float bDur = 60.0f / bpm;
        const float sDur = bDur * 0.25f;
        const float cDur = bDur * 8.0f;

        // 1. Wind
        windLfoPhase += 0.08f * dt;
        if (windLfoPhase > 1.0f) windLfoPhase -= 1.0f;
        const float windCutoffL = 0.012f + 0.008f * std::sin(windLfoPhase * kTwoPi);
        const float windCutoffR = 0.012f + 0.008f * std::sin((windLfoPhase + 0.25f) * kTwoPi);
        const float windL = windLPL.Process(noise.Next(), windCutoffL) * 0.12f;
        const float windR = windLPR.Process(noise.Next(), windCutoffR) * 0.12f;

        // 2. Sub-Bass
        subPhaseL += (subBassFreq - 0.3f) * dt;
        if (subPhaseL >= 1.0f) subPhaseL -= 1.0f;
        subPhaseR += (subBassFreq + 0.3f) * dt;
        if (subPhaseR >= 1.0f) subPhaseR -= 1.0f;
        subPhaseHarm += (subBassFreq * 2.0f) * dt;
        if (subPhaseHarm >= 1.0f) subPhaseHarm -= 1.0f;
        const float subBassL = std::sin(subPhaseL * kTwoPi) * 0.22f +
                               std::sin(subPhaseHarm * kTwoPi) * 0.05f;
        const float subBassR = std::sin(subPhaseR * kTwoPi) * 0.22f +
                               std::sin(subPhaseHarm * kTwoPi) * 0.05f;

        // 3. Pad
        chordTimer += dt;
        if (chordTimer >= cDur) {
            chordTimer -= cDur;
            chordIndex = (chordIndex + 1) % 4;
            for (int k = 0; k < 4; ++k) {
                targetFreq[k] = chords[chordIndex][k];
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
        if (arpTimer >= sDur) {
            arpTimer -= sDur;
            arpStep = (arpStep + 1) % 16;
            arpEnv  = 0.75f;
        }
        arpPhase += arpNotes[arpStep] * dt;
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
        const int beatIdx = static_cast<int>(t / bDur);
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
        const int halfBeat = static_cast<int>(t / (bDur * 0.5f));
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

} // namespace

// ===========================================================================
//  AudioSystem::Impl — Physical Modeling Engine
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
        const float rawWhite = sfxNoise.Next();
        const float pink = v.pinkFilt.Process(rawWhite);

        switch (v.id) {
            // ---------------------------------------------------------------
            // 1. PISTOL: 10mm Tactical Service Pistol
            // Stages:
            //   - 0..3ms: Supersonic primer crack (hard impulse)
            //   - 3..35ms: Muzzle blast & expanding gas concussion
            //   - 35..90ms: Barrel cavity resonance (1050 Hz)
            //   - 60..120ms: Mechanical slide cycle & bolt snap
            // ---------------------------------------------------------------
            case SoundId::Pistol: {
                // Initial supersonic crack
                const float crackEnv = std::exp(-curT * 120.0f);
                const float crack = (curT < 0.003f) ? (rawWhite * 1.8f) : (rawWhite * crackEnv * 0.6f);

                // Concussive sub-punch: 240 Hz plunging down to 55 Hz
                const float subFreq = 55.0f + 210.0f * std::exp(-curT * 40.0f) * v.pitchVar;
                v.phase1 += subFreq * dt;
                if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;
                const float punch = std::sin(v.phase1 * kTwoPi) * std::exp(-curT * 22.0f);

                // Barrel acoustic resonance (ringing metal cavity ~1050 Hz)
                v.resFilt1.Process(pink, 1050.0f * v.pitchVar, 4.5f);
                const float barrelRumble = v.resFilt1.bp * std::exp(-curT * 28.0f);

                // Mechanical slide cycle sound at ~55ms
                float mechSnap = 0.0f;
                if (curT >= 0.045f && curT <= 0.085f) {
                    const float mechT = curT - 0.045f;
                    v.phase2 += 2200.0f * dt;
                    if (v.phase2 >= 1.0f) v.phase2 -= 1.0f;
                    mechSnap = (std::sin(v.phase2 * kTwoPi) * 0.4f + rawWhite * 0.6f) *
                               std::exp(-mechT * 80.0f) * 0.35f;
                }

                // Room reflection tail
                v.onePole1.Process(pink, 0.06f);
                const float tail = v.onePole1.state * std::exp(-curT * 9.0f) * 0.25f;

                const float rawOut = crack * 0.7f + punch * 0.85f + barrelRumble * 0.75f + mechSnap + tail;
                sample = Saturate(rawOut * 1.6f);
                break;
            }

            // ---------------------------------------------------------------
            // 2. SHOTGUN: 12-Gauge Tactical Combat Shotgun
            // Stages:
            //   - 0..15ms: Massive explosive detonation & pressure wave
            //   - 15..140ms: Sub-bass chest displacement (35 Hz boom)
            //   - 20..220ms: Dense expanding cloud of gas (turbulent pink noise)
            //   - 190..270ms: Forend pump racked BACK (slide friction & click)
            //   - 280..380ms: Forend pump slammed FORWARD (bolt lock battery)
            // ---------------------------------------------------------------
            case SoundId::Shotgun: {
                // High-pressure detonation shockwave
                const float shock = (curT < 0.012f) ? Saturate(rawWhite * 2.5f) : 0.0f;

                // Sub-bass chest-slamming concussion: 155 Hz dropping to 32 Hz
                const float subFreq = 32.0f + 140.0f * std::exp(-curT * 14.0f) * v.pitchVar;
                v.phase1 += subFreq * dt;
                if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;
                const float subPunch = std::sin(v.phase1 * kTwoPi) * std::exp(-curT * 8.5f);

                // Heavy turbulent gas expansion
                v.resFilt1.Process(pink, 480.0f * v.pitchVar, 2.5f);
                const float blast = (v.resFilt1.lp + v.resFilt1.bp * 0.8f) * std::exp(-curT * 9.0f);

                // Atmospheric low-end room boom
                v.onePole1.Process(subPunch + blast, 0.08f);
                const float roomRumble = v.onePole1.state * std::exp(-curT * 5.5f);

                // Real Mechanical Pump-Action:
                // Part A: Rack BACK (~0.18s to 0.26s)
                float pumpBack = 0.0f;
                if (curT >= 0.18f && curT <= 0.26f) {
                    const float pt = curT - 0.18f;
                    v.phase2 += 1650.0f * dt;
                    if (v.phase2 >= 1.0f) v.phase2 -= 1.0f;
                    v.resFilt2.Process(pink, 1400.0f, 3.5f);
                    pumpBack = (std::sin(v.phase2 * kTwoPi) * 0.35f + v.resFilt2.bp * 0.65f) *
                               std::exp(-pt * 35.0f) * 0.65f;
                }

                // Part B: Slam FORWARD into battery (~0.28s to 0.37s)
                float pumpForward = 0.0f;
                if (curT >= 0.28f && curT <= 0.37f) {
                    const float pt = curT - 0.28f;
                    v.phase3 += 850.0f * dt;
                    if (v.phase3 >= 1.0f) v.phase3 -= 1.0f;
                    v.resFilt2.Process(rawWhite, 2400.0f, 4.0f);
                    pumpForward = (std::sin(v.phase3 * kTwoPi) * 0.5f + v.resFilt2.bp * 0.5f) *
                                  std::exp(-pt * 40.0f) * 0.85f;
                }

                const float rawOut = shock * 0.8f + subPunch * 1.1f + blast * 0.9f +
                                     roomRumble * 0.5f + pumpBack + pumpForward;
                sample = Saturate(rawOut * 1.55f);
                break;
            }

            // ---------------------------------------------------------------
            // 3. CHAINGUN: Motorized Rotary Machine Gun
            // Fast tungsten kinetic crack + hot muzzle brake gas pressure
            // ---------------------------------------------------------------
            case SoundId::Chaingun: {
                // Supersonic shockwave snap
                const float crack = (curT < 0.005f) ? (rawWhite * 1.6f) : 0.0f;

                // Punch sweep: 460 Hz down to 85 Hz
                const float freq = 85.0f + 380.0f * std::exp(-curT * 55.0f) * v.pitchVar;
                v.phase1 += freq * dt;
                if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;
                const float punch = std::sin(v.phase1 * kTwoPi) * std::exp(-curT * 28.0f);

                // High-pressure muzzle brake gas hiss
                v.resFilt1.Process(pink, 1850.0f * v.pitchVar, 3.0f);
                const float brakeGas = v.resFilt1.bp * std::exp(-curT * 38.0f);

                // Metallic rotary bolt clack
                v.phase2 += 2800.0f * dt;
                if (v.phase2 >= 1.0f) v.phase2 -= 1.0f;
                const float clack = std::sin(v.phase2 * kTwoPi) * std::exp(-curT * 60.0f) * 0.35f;

                sample = Saturate((crack * 0.8f + punch * 0.9f + brakeGas * 0.7f + clack) * 1.6f);
                break;
            }

            // ---------------------------------------------------------------
            // 4. PLAYER FOOTSTEP: Heavy Armored Combat Boot on Regolith/Grating
            //   - Heel strike: Low acoustic thud of heavy sole (65-80 Hz)
            //   - Regolith crunch: High-shelf textured Martian basalt dust
            //   - Tactical gear jingle: Subtle cloth/holster rustle
            // ---------------------------------------------------------------
            case SoundId::PlayerStep: {
                // Heavy heel strike
                const float heelFreq = (65.0f + v.seed * 12.0f) * std::exp(-curT * 35.0f);
                v.phase1 += heelFreq * dt;
                if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;
                const float heelThud = std::sin(v.phase1 * kTwoPi) * std::exp(-curT * 38.0f);

                // Regolith / gravel crunch (bandpass filtered shaped noise)
                v.resFilt1.Process(pink, 2200.0f + v.seed * 350.0f, 2.2f);
                const float crunch = v.resFilt1.bp * std::exp(-curT * 42.0f) * 0.65f;

                // Low-end floor resonance
                v.onePole1.Process(heelThud, 0.15f);
                const float floorRes = v.onePole1.state * std::exp(-curT * 25.0f) * 0.45f;

                sample = (heelThud * 0.75f + crunch * 0.8f + floorRes) * 1.1f;
                break;
            }

            // ---------------------------------------------------------------
            // 5. MONSTER FOOTSTEP (Scout / Shooter): Chitinous Claws
            //   - Double-tap claw strike (fore-claw then rear-claw)
            //   - Resonant insectoid carapace tap (1350 Hz & 2700 Hz)
            // ---------------------------------------------------------------
            case SoundId::MonsterStep: {
                // Primary claw tap
                v.resFilt1.Process(pink, 1350.0f * v.pitchVar, 4.0f);
                const float tap1 = v.resFilt1.bp * std::exp(-curT * 45.0f);

                // Secondary claw drag/tap at ~18ms
                float tap2 = 0.0f;
                if (curT >= 0.016f) {
                    const float dt2 = curT - 0.016f;
                    v.resFilt2.Process(rawWhite, 2750.0f * v.pitchVar, 3.5f);
                    tap2 = v.resFilt2.bp * std::exp(-dt2 * 55.0f) * 0.7f;
                }

                // Chitin friction scrape
                v.onePole1.Process(rawWhite, 0.12f);
                const float scrape = v.onePole1.state * std::exp(-curT * 40.0f) * 0.35f;

                sample = (tap1 * 0.8f + tap2 * 0.6f + scrape) * 1.2f;
                break;
            }

            // ---------------------------------------------------------------
            // 6. MONSTER FOOTSTEP HEAVY (Tank): Multi-Ton Seismic Stomp
            //   - 32 Hz ground vibration
            //   - Grinding stone / crushed metal grating
            // ---------------------------------------------------------------
            case SoundId::MonsterStepHeavy: {
                // Seismic floor shake (32 Hz)
                const float stompFreq = 34.0f * std::exp(-curT * 12.0f);
                v.phase1 += stompFreq * dt;
                if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;
                const float sub = std::sin(v.phase1 * kTwoPi) * std::exp(-curT * 11.0f);

                // Crushing debris & mass impact
                v.resFilt1.Process(pink, 220.0f, 2.0f);
                const float massCrunch = v.resFilt1.lp * std::exp(-curT * 16.0f);

                sample = Saturate((sub * 1.1f + massCrunch * 0.9f) * 1.4f);
                break;
            }

            // ---------------------------------------------------------------
            // 7. MONSTER ALERT / ROAR: Mantis Predatory Screech
            //   - Glottal pulse train passing through dual throat formant filters:
            //     F1 ~650 Hz (guttural chest), F2 ~1850 Hz (chitin mandible shriek)
            //   - Stridulation mandible chatter (55 Hz modulation)
            //   - Predatory lung hiss
            // ---------------------------------------------------------------
            case SoundId::MonsterAlert: {
                // Glottal fundamental pitch envelope: starts low, spikes up aggressively
                const float progress = curT / v.duration;
                const float pitchCurve = std::sin(progress * kPi);
                const float f0 = (110.0f + 160.0f * pitchCurve) * v.pitchVar;

                // Glottal buzz pulse train
                v.phase1 += f0 * dt;
                if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;
                const float glottal = (v.phase1 < 0.25f) ? (1.0f - v.phase1 * 4.0f) : -0.25f;

                // Mandible stridulation chatter (55 Hz amplitude flutter)
                v.phase2 += 55.0f * dt;
                if (v.phase2 >= 1.0f) v.phase2 -= 1.0f;
                const float chatter = 0.65f + 0.35f * std::sin(v.phase2 * kTwoPi);

                // Vocal tract Formant 1 (~680 Hz - guttural cavity)
                v.resFilt1.Process(glottal * chatter, 680.0f * v.pitchVar, 3.8f);

                // Vocal tract Formant 2 (~1900 Hz - piercing screech cavity)
                v.resFilt2.Process(glottal * chatter, 1900.0f * v.pitchVar, 4.5f);

                // Harsh predatory air hiss
                v.onePole1.Process(rawWhite, 0.25f);
                const float hiss = v.onePole1.state * pitchCurve * 0.45f;

                const float vocalSound = (v.resFilt1.bp * 0.9f + v.resFilt2.bp * 0.7f + hiss) * pitchCurve;
                sample = Saturate(vocalSound * 1.55f);
                break;
            }

            // ---------------------------------------------------------------
            // 8. MONSTER ATTACK (Scout): Razor Blade Whoosh & Visceral Snap
            // ---------------------------------------------------------------
            case SoundId::MonsterAttackScout: {
                const float progress = curT / v.duration;
                // Aerodynamic cutting whoosh (sweeping bandpass noise)
                const float whooshFreq = 3200.0f - 2400.0f * progress;
                v.resFilt1.Process(pink, whooshFreq, 2.8f);
                const float whoosh = v.resFilt1.bp * std::sin(progress * kPi);

                // Sharp cutting snap at midpoint
                float snap = 0.0f;
                if (curT >= 0.06f && curT <= 0.16f) {
                    const float st = curT - 0.06f;
                    v.phase1 += (180.0f + 650.0f * std::exp(-st * 30.0f)) * dt;
                    if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;
                    snap = std::sin(v.phase1 * kTwoPi) * std::exp(-st * 25.0f) * 0.9f;
                }

                sample = Saturate((whoosh * 1.1f + snap * 0.85f) * 1.4f);
                break;
            }

            // ---------------------------------------------------------------
            // 9. MONSTER ATTACK (Tank): Earth-Shattering Claw Smash
            // ---------------------------------------------------------------
            case SoundId::MonsterAttackTank: {
                // Sub-concussion wave (38 Hz)
                const float subFreq = 38.0f + 95.0f * std::exp(-curT * 12.0f);
                v.phase1 += subFreq * dt;
                if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;
                const float sub = std::sin(v.phase1 * kTwoPi) * std::exp(-curT * 8.0f);

                // Guttural war growl
                v.phase2 += 75.0f * dt;
                if (v.phase2 >= 1.0f) v.phase2 -= 1.0f;
                v.resFilt1.Process(std::sin(v.phase2 * kTwoPi), 380.0f, 3.2f);
                const float growl = v.resFilt1.bp * std::exp(-curT * 6.5f);

                // Heavy ground pulverization noise
                v.resFilt2.Process(pink, 280.0f, 1.8f);
                const float shatter = v.resFilt2.lp * std::exp(-curT * 10.0f);

                sample = Saturate((sub * 1.15f + growl * 0.6f + shatter * 0.85f) * 1.5f);
                break;
            }

            // ---------------------------------------------------------------
            // 10. MONSTER ATTACK (Shooter): Wet Acidic Spit Launch
            // Pressurized throat contraction -> bubbling acidic discharge
            // ---------------------------------------------------------------
            case SoundId::MonsterAttackShooter: {
                const float progress = curT / v.duration;
                const float env = std::sin(progress * kPi);

                // Bubbling acid throat chirp (FM pitch modulation)
                v.phase2 += 42.0f * dt;
                if (v.phase2 >= 1.0f) v.phase2 -= 1.0f;
                const float bubbleMod = std::sin(v.phase2 * kTwoPi) * 90.0f;

                const float chirpFreq = 220.0f + 640.0f * std::exp(-curT * 12.0f) + bubbleMod;
                v.phase1 += chirpFreq * dt;
                if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;

                // Wet fluid splat
                v.resFilt1.Process(pink, 950.0f, 3.0f);
                const float splat = v.resFilt1.bp * env * 0.7f;

                const float spit = std::sin(v.phase1 * kTwoPi) * env * 0.75f;
                sample = Saturate((spit + splat) * 1.4f);
                break;
            }

            // ---------------------------------------------------------------
            // 11. MONSTER HURT: Piercing Insectoid Pain Shriek
            // Chitin shell cracking under ballistic impact + agonized vocal screech
            // ---------------------------------------------------------------
            case SoundId::MonsterHurt: {
                // Ballistic chitin crack (0..8ms)
                const float crack = (curT < 0.008f) ? (rawWhite * 1.7f) : 0.0f;

                // High distressed alien vocal screech
                const float progress = curT / v.duration;
                const float env = std::exp(-curT * 14.0f);

                v.phase2 += 80.0f * dt;
                if (v.phase2 >= 1.0f) v.phase2 -= 1.0f;
                const float screamMod = std::sin(v.phase2 * kTwoPi) * 180.0f;

                const float screamFreq = 580.0f + 250.0f * std::sin(progress * kPi) + screamMod;
                v.phase1 += screamFreq * dt;
                if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;

                v.resFilt1.Process(std::sin(v.phase1 * kTwoPi), 1850.0f, 3.5f);
                const float shriek = v.resFilt1.bp * env;

                sample = Saturate((crack * 0.75f + shriek * 1.1f) * 1.4f);
                break;
            }

            // ---------------------------------------------------------------
            // 12. MONSTER DEATH: Descending Death Shriek & Carcass Collapse
            // ---------------------------------------------------------------
            case SoundId::MonsterDeath: {
                const float progress = std::clamp(curT / v.duration, 0.0f, 1.0f);
                const float env = 1.0f - progress;

                // Descending death shriek
                const float shriekFreq = 48.0f + 420.0f * std::pow(env, 1.8f);
                v.phase2 += 36.0f * dt;
                if (v.phase2 >= 1.0f) v.phase2 -= 1.0f;
                const float deathMod = std::sin(v.phase2 * kTwoPi) * (140.0f * env);

                v.phase1 += (shriekFreq + deathMod) * dt;
                if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;

                v.resFilt1.Process(std::sin(v.phase1 * kTwoPi), 950.0f * env + 120.0f, 3.0f);
                const float dyingVoice = v.resFilt1.bp * env;

                // Carcass impact thud as it falls onto the ground (at ~0.25s)
                float bodyFall = 0.0f;
                if (curT >= 0.22f) {
                    const float ft = curT - 0.22f;
                    v.phase3 += 42.0f * dt;
                    if (v.phase3 >= 1.0f) v.phase3 -= 1.0f;
                    bodyFall = std::sin(v.phase3 * kTwoPi) * std::exp(-ft * 12.0f) * 0.9f;
                }

                sample = Saturate((dyingVoice * 1.1f + bodyFall) * 1.45f);
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

    // Pre-seed buffer & unpause device
    Tick();
    SDL_ResumeAudioDevice(m_impl->device);

    SDL_Log("[Audio] Procedural acoustic modeling audio engine ready.");
    return true;
}

bool AudioSystem::PlayProceduralMusic(float volume) {
    if (!m_running || !m_impl) return false;
    m_musicActive = true;
    m_volume      = std::clamp(volume, 0.0f, 1.0f);
    m_impl->musicSynth = ProceduralSynth{};
    m_impl->musicSynth.LoadProfile(m_profile);
    return true;
}

void AudioSystem::ApplySoundtrackProfile(const SoundtrackProfile& profile) {
    m_profile = profile;
    if (m_impl) {
        m_impl->musicSynth.LoadProfile(m_profile);
    }
}

const SoundtrackProfile& AudioSystem::GetSoundtrackProfile() const {
    return m_profile;
}

bool AudioSystem::ConvertAndApplyWav(const std::string& wavPath) {
    SoundtrackProfile newProf;
    if (!SoundtrackConverter::ConvertWav(wavPath, newProf)) {
        return false;
    }
    ApplySoundtrackProfile(newProf);
    PlayProceduralMusic(m_volume);
    return true;
}

void AudioSystem::ResetToDefaultSoundtrack() {
    ApplySoundtrackProfile(SoundtrackConverter::DefaultProfile());
    PlayProceduralMusic(m_volume);
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
    const float panAngle = (pan + 1.0f) * 0.25f * kPi;
    const float panL = std::cos(panAngle);
    const float panR = std::sin(panAngle);

    // Duration table per physically modeled sound
    float duration = 0.2f;
    switch (id) {
        case SoundId::Pistol:              duration = 0.20f; break;
        case SoundId::Shotgun:             duration = 0.48f; break;
        case SoundId::Chaingun:            duration = 0.14f; break;
        case SoundId::PlayerStep:          duration = 0.10f; break;
        case SoundId::MonsterStep:         duration = 0.09f; break;
        case SoundId::MonsterStepHeavy:    duration = 0.22f; break;
        case SoundId::MonsterAlert:        duration = 0.42f; break;
        case SoundId::MonsterAttackScout:  duration = 0.26f; break;
        case SoundId::MonsterAttackTank:   duration = 0.48f; break;
        case SoundId::MonsterAttackShooter:duration = 0.32f; break;
        case SoundId::MonsterHurt:         duration = 0.20f; break;
        case SoundId::MonsterDeath:        duration = 0.60f; break;
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
        v.phase3      = 0.0f;
        v.resFilt1    = ResonantFilter{};
        v.resFilt2    = ResonantFilter{};
        v.onePole1    = OnePoleLP{};
        v.onePole2    = OnePoleLP{};
        v.pinkFilt    = PinkFilter{};

        // Subtle realistic pitch variance (+- 4%)
        const float r = (static_cast<float>(m_impl->sfxNoise.state & 0xFFu) / 255.0f);
        v.pitchVar    = 0.96f + 0.08f * r;
        v.seed        = r;
    }
}

void AudioSystem::PlaySpatialSound(SoundId id, const Vector3& emitterPos,
                                   const Vector3& listenerPos, float listenerAngle,
                                   float baseVolume) {
    const float dx = emitterPos.x - listenerPos.x;
    const float dz = emitterPos.z - listenerPos.z;
    const float distSq = dx * dx + dz * dz;
    constexpr float kMaxDist = 34.0f;
    if (distSq > kMaxDist * kMaxDist) return;

    const float dist = std::sqrt(distSq);
    // Physically-based inverse distance attenuation
    const float distGain = 1.0f / (1.0f + dist * 0.13f);
    const float vol = baseVolume * distGain;
    if (vol < 0.008f) return;

    // Stereo panning based on listener's local orientation
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

            m_impl->buffer[i * 2 + 0] = MasterLimit(totalL);
            m_impl->buffer[i * 2 + 1] = MasterLimit(totalR);
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
