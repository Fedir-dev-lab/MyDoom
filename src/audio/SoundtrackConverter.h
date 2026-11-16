#pragma once
// ---------------------------------------------------------------------------
// SoundtrackConverter — Pure C++ audio analyzer & procedural music converter.
//
// Analyzes user audio files (.wav) from scratch (zero external libraries):
//   - Pure C++ WAV parser (16-bit, 24-bit, 32-bit float, mono/stereo)
//   - Radix-2 Cooley-Tukey FFT (Fast Fourier Transform)
//   - Automated tempo / BPM onset detection
//   - Harmonic chord extraction (spectral peak chroma mapping)
//   - 16-step arpeggio / lead melody extraction
//
// Converts any song into the engine's real-time procedural synthesizer format!
// ---------------------------------------------------------------------------
#include <array>
#include <string>
#include <vector>

struct SoundtrackProfile {
    std::string name           = "Mars Ambient (Default)";
    float       bpm            = 72.0f;
    float       subBassFreq    = 55.0f;
    float       chords[4][4]   = {
        { 110.00f, 130.81f, 164.81f, 196.00f }, // Am9
        {  87.31f, 110.00f, 130.81f, 164.81f }, // Fmaj7
        {  73.42f,  87.31f, 110.00f, 130.81f }, // Dm9
        {  82.41f,  98.00f, 123.47f, 146.83f }  // Em7
    };
    float       arpNotes[16]   = {
        220.00f, 261.63f, 329.63f, 392.00f,
        440.00f, 392.00f, 329.63f, 261.63f,
        220.00f, 329.63f, 261.63f, 440.00f,
        392.00f, 329.63f, 261.63f, 196.00f
    };
    bool        isCustom       = false;
    std::string statusMessage  = "Default procedural soundtrack";
};

class SoundtrackConverter {
public:
    // Analyzes a WAV audio file and converts its musical DNA into a SoundtrackProfile.
    // Returns false if file cannot be read or is invalid.
    static bool ConvertWav(const std::string& wavPath, SoundtrackProfile& outProfile);

    // Creates the default built-in Mars Ambient profile.
    static SoundtrackProfile DefaultProfile();
};
