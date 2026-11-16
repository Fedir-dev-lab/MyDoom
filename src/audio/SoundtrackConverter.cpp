#include "SoundtrackConverter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace {

constexpr float kPi = 3.14159265358979323846f;

// ---------------------------------------------------------------------------
// Pure C++ Cooley-Tukey Radix-2 FFT (zero libraries)
// ---------------------------------------------------------------------------
void Radix2FFT(std::vector<float>& real, std::vector<float>& imag) {
    const size_t n = real.size();
    if (n <= 1) return;

    // Bit-reversal permutation
    size_t j = 0;
    for (size_t i = 0; i < n - 1; ++i) {
        if (i < j) {
            std::swap(real[i], real[j]);
            std::swap(imag[i], imag[j]);
        }
        size_t k = n >> 1;
        while (k <= j) {
            j -= k;
            k >>= 1;
        }
        j += k;
    }

    // Butterfly passes
    for (size_t len = 2; len <= n; len <<= 1) {
        const float angle = -2.0f * kPi / static_cast<float>(len);
        const float wlen_r = std::cos(angle);
        const float wlen_i = std::sin(angle);
        for (size_t i = 0; i < n; i += len) {
            float w_r = 1.0f;
            float w_i = 0.0f;
            for (size_t k = 0; k < len / 2; ++k) {
                const size_t u = i + k;
                const size_t v = i + k + len / 2;
                const float tr = w_r * real[v] - w_i * imag[v];
                const float ti = w_r * imag[v] + w_i * real[v];
                real[v] = real[u] - tr;
                imag[v] = imag[u] - ti;
                real[u] += tr;
                imag[u] += ti;
                const float next_w_r = w_r * wlen_r - w_i * wlen_i;
                w_i = w_r * wlen_i + w_i * wlen_r;
                w_r = next_w_r;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Snap continuous frequency in Hz to closest 12-TET musical note
// ---------------------------------------------------------------------------
float SnapToMusicalNote(float freq, float minHz = 65.0f, float maxHz = 880.0f) {
    if (freq <= 20.0f) return minHz;
    // MIDI note = 69 + 12 * log2(freq / 440.0)
    const float midi = 69.0f + 12.0f * (std::log(freq / 440.0f) / std::log(2.0f));
    const int noteIdx = static_cast<int>(std::round(midi));
    float tuned = 440.0f * std::pow(2.0f, static_cast<float>(noteIdx - 69) / 12.0f);

    // Fold octaves into target range
    while (tuned < minHz) tuned *= 2.0f;
    while (tuned > maxHz) tuned *= 0.5f;
    return tuned;
}

// ---------------------------------------------------------------------------
// Pure C++ WAV File Loader (16-bit, 24-bit, 32-bit float, mono/stereo)
// ---------------------------------------------------------------------------
bool LoadWavSamples(const std::string& path, std::vector<float>& outMonoSamples, int& outSampleRate) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    // RIFF Header
    char riff[4];
    uint32_t fileSize = 0;
    char wave[4];
    file.read(riff, 4);
    file.read(reinterpret_cast<char*>(&fileSize), 4);
    file.read(wave, 4);
    if (std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(wave, "WAVE", 4) != 0) {
        return false;
    }

    uint16_t audioFormat = 0;
    uint16_t numChannels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    bool foundFmt = false;
    bool foundData = false;
    uint32_t dataBytes = 0;
    std::streampos dataPos = 0;

    // Chunk parser
    while (file.good() && !file.eof()) {
        char chunkId[4];
        uint32_t chunkSize = 0;
        file.read(chunkId, 4);
        file.read(reinterpret_cast<char*>(&chunkSize), 4);
        if (file.gcount() < 4) break;

        if (std::memcmp(chunkId, "fmt ", 4) == 0) {
            file.read(reinterpret_cast<char*>(&audioFormat), 2);
            file.read(reinterpret_cast<char*>(&numChannels), 2);
            file.read(reinterpret_cast<char*>(&sampleRate), 4);
            uint32_t byteRate = 0;
            uint16_t blockAlign = 0;
            file.read(reinterpret_cast<char*>(&byteRate), 4);
            file.read(reinterpret_cast<char*>(&blockAlign), 2);
            file.read(reinterpret_cast<char*>(&bitsPerSample), 2);
            if (chunkSize > 16) {
                file.seekg(chunkSize - 16, std::ios::cur);
            }
            foundFmt = true;
        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            dataBytes = chunkSize;
            dataPos = file.tellg();
            foundData = true;
            break;
        } else {
            // Skip unknown chunk (e.g. LIST, JUNK, PEAK)
            file.seekg(chunkSize, std::ios::cur);
        }
    }

    if (!foundFmt || !foundData || numChannels == 0 || sampleRate == 0) {
        return false;
    }

    outSampleRate = static_cast<int>(sampleRate);
    file.seekg(dataPos);

    const int bytesPerSample = bitsPerSample / 8;
    const int frameBytes = bytesPerSample * numChannels;
    if (frameBytes <= 0) return false;

    const size_t totalFrames = dataBytes / frameBytes;
    outMonoSamples.resize(totalFrames);

    std::vector<uint8_t> rawBuf(dataBytes);
    file.read(reinterpret_cast<char*>(rawBuf.data()), dataBytes);

    for (size_t i = 0; i < totalFrames; ++i) {
        float sampleSum = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch) {
            const size_t offset = i * frameBytes + ch * bytesPerSample;
            float val = 0.0f;
            if (audioFormat == 1) { // PCM integer
                if (bitsPerSample == 16) {
                    int16_t s = 0;
                    std::memcpy(&s, &rawBuf[offset], 2);
                    val = static_cast<float>(s) / 32768.0f;
                } else if (bitsPerSample == 24) {
                    int32_t s = (static_cast<int32_t>(rawBuf[offset + 0]) << 8) |
                                (static_cast<int32_t>(rawBuf[offset + 1]) << 16) |
                                (static_cast<int32_t>(static_cast<int8_t>(rawBuf[offset + 2])) << 24);
                    val = static_cast<float>(s) / 2147483648.0f;
                } else if (bitsPerSample == 8) {
                    val = (static_cast<float>(rawBuf[offset]) - 128.0f) / 128.0f;
                }
            } else if (audioFormat == 3 && bitsPerSample == 32) { // IEEE 32-bit Float
                std::memcpy(&val, &rawBuf[offset], 4);
            }
            sampleSum += val;
        }
        outMonoSamples[i] = sampleSum / static_cast<float>(numChannels);
    }

    return !outMonoSamples.empty();
}

} // namespace

// ===========================================================================
//  SoundtrackConverter API implementation
// ===========================================================================

SoundtrackProfile SoundtrackConverter::DefaultProfile() {
    return SoundtrackProfile{};
}

bool SoundtrackConverter::ConvertWav(const std::string& wavPath, SoundtrackProfile& outProfile) {
    std::vector<float> audio;
    int sampleRate = 44100;

    if (!LoadWavSamples(wavPath, audio, sampleRate)) {
        return false;
    }

    if (audio.size() < 4096) {
        return false;
    }

    // -----------------------------------------------------------------------
    // 1. Detect BPM via spectral energy onset autocorrelation
    // -----------------------------------------------------------------------
    constexpr int kHopSize = 512;
    constexpr int kWindowSize = 1024;
    const size_t numHops = (audio.size() - kWindowSize) / kHopSize;

    std::vector<float> energyEnvelope;
    energyEnvelope.reserve(numHops);

    float prevEnergy = 0.0f;
    for (size_t h = 0; h < numHops; ++h) {
        float energy = 0.0f;
        const size_t start = h * kHopSize;
        for (int i = 0; i < kWindowSize; ++i) {
            const float s = audio[start + i];
            energy += s * s;
        }
        // Energy flux (only positive onset rise)
        const float flux = std::max(0.0f, energy - prevEnergy);
        prevEnergy = energy;
        energyEnvelope.push_back(flux);
    }

    // Autocorrelation over BPM range [60..160]
    // Period T in hops = (60.0 / BPM) * (sampleRate / hopSize)
    const float hopsPerSec = static_cast<float>(sampleRate) / static_cast<float>(kHopSize);
    const int minLag = static_cast<int>(hopsPerSec * (60.0f / 160.0f)); // 160 BPM
    const int maxLag = static_cast<int>(hopsPerSec * (60.0f / 60.0f));  // 60 BPM

    float bestCorr = -1.0f;
    int bestLag = static_cast<int>(hopsPerSec * (60.0f / 80.0f)); // default fallback 80 BPM

    if (energyEnvelope.size() > static_cast<size_t>(maxLag * 2)) {
        const size_t evalLen = std::min(energyEnvelope.size() - maxLag, size_t(2000));
        for (int lag = minLag; lag <= maxLag; ++lag) {
            float corr = 0.0f;
            for (size_t i = 0; i < evalLen; ++i) {
                corr += energyEnvelope[i] * energyEnvelope[i + lag];
            }
            if (corr > bestCorr) {
                bestCorr = corr;
                bestLag = lag;
            }
        }
    }

    float detectedBpm = (60.0f * hopsPerSec) / static_cast<float>(bestLag);
    // Normalize into musical range [70..135]
    while (detectedBpm < 68.0f)  detectedBpm *= 2.0f;
    while (detectedBpm > 140.0f) detectedBpm *= 0.5f;
    detectedBpm = std::round(detectedBpm);

    outProfile.bpm = detectedBpm;

    // -----------------------------------------------------------------------
    // 2. Harmonic Chord Extraction (Divide audio into 4 sections)
    // -----------------------------------------------------------------------
    constexpr size_t kFFTSize = 2048;
    const size_t totalFrames = audio.size();
    const size_t sectionLen = totalFrames / 4;

    std::vector<float> real(kFFTSize);
    std::vector<float> imag(kFFTSize, 0.0f);

    for (int sec = 0; sec < 4; ++sec) {
        // Sample middle of the section for stable harmonic content
        const size_t secCenter = sec * sectionLen + sectionLen / 2;
        const size_t fftStart = (secCenter > kFFTSize) ? (secCenter - kFFTSize / 2) : 0;

        for (size_t i = 0; i < kFFTSize; ++i) {
            if (fftStart + i < totalFrames) {
                // Hanning window
                const float w = 0.5f * (1.0f - std::cos(2.0f * kPi * static_cast<float>(i) / static_cast<float>(kFFTSize - 1)));
                real[i] = audio[fftStart + i] * w;
            } else {
                real[i] = 0.0f;
            }
            imag[i] = 0.0f;
        }

        Radix2FFT(real, imag);

        // Find frequency peaks in the musical range [65 Hz .. 600 Hz]
        struct Peak { float freq; float mag; };
        std::vector<Peak> peaks;

        const float binHz = static_cast<float>(sampleRate) / static_cast<float>(kFFTSize);
        const int minBin = static_cast<int>(55.0f / binHz);
        const int maxBin = static_cast<int>(650.0f / binHz);

        for (int b = minBin + 1; b < maxBin - 1; ++b) {
            const float mag = real[b] * real[b] + imag[b] * imag[b];
            const float prevMag = real[b-1] * real[b-1] + imag[b-1] * imag[b-1];
            const float nextMag = real[b+1] * real[b+1] + imag[b+1] * imag[b+1];
            if (mag > prevMag && mag > nextMag && mag > 0.001f) {
                peaks.push_back({ static_cast<float>(b) * binHz, mag });
            }
        }

        std::sort(peaks.begin(), peaks.end(), [](const Peak& a, const Peak& b) {
            return a.mag > b.mag;
        });

        // Fill 4 chord voices with quantized musical pitches
        const float defaultChord[4] = { 110.0f, 130.81f, 164.81f, 196.0f };
        for (int v = 0; v < 4; ++v) {
            if (v < static_cast<int>(peaks.size())) {
                outProfile.chords[sec][v] = SnapToMusicalNote(peaks[v].freq, 75.0f, 350.0f);
            } else {
                outProfile.chords[sec][v] = defaultChord[v];
            }
        }
        std::sort(&outProfile.chords[sec][0], &outProfile.chords[sec][4]);
    }

    // -----------------------------------------------------------------------
    // 3. 16-Step Arpeggio / Lead Melody Extraction
    // -----------------------------------------------------------------------
    const size_t stepFrames = totalFrames / 16;
    for (int step = 0; step < 16; ++step) {
        const size_t start = step * stepFrames;
        for (size_t i = 0; i < kFFTSize; ++i) {
            if (start + i < totalFrames) {
                const float w = 0.5f * (1.0f - std::cos(2.0f * kPi * static_cast<float>(i) / static_cast<float>(kFFTSize - 1)));
                real[i] = audio[start + i] * w;
            } else {
                real[i] = 0.0f;
            }
            imag[i] = 0.0f;
        }

        Radix2FFT(real, imag);

        const float binHz = static_cast<float>(sampleRate) / static_cast<float>(kFFTSize);
        const int minBin = static_cast<int>(180.0f / binHz);
        const int maxBin = static_cast<int>(900.0f / binHz);

        float maxMag = 0.0f;
        int maxBinIdx = minBin;
        for (int b = minBin; b < maxBin; ++b) {
            const float mag = real[b] * real[b] + imag[b] * imag[b];
            if (mag > maxMag) {
                maxMag = mag;
                maxBinIdx = b;
            }
        }

        const float detectedFreq = static_cast<float>(maxBinIdx) * binHz;
        outProfile.arpNotes[step] = SnapToMusicalNote(detectedFreq, 180.0f, 660.0f);
    }

    // -----------------------------------------------------------------------
    // 4. Sub-bass tuning
    // -----------------------------------------------------------------------
    outProfile.subBassFreq = outProfile.chords[0][0] * 0.5f;
    while (outProfile.subBassFreq > 60.0f) outProfile.subBassFreq *= 0.5f;
    while (outProfile.subBassFreq < 40.0f) outProfile.subBassFreq *= 2.0f;

    // File basename for display
    std::string base = wavPath;
    const size_t slash = base.find_last_of("/\\");
    if (slash != std::string::npos) base = base.substr(slash + 1);

    outProfile.name = "Custom (" + base + ")";
    outProfile.isCustom = true;

    char msgBuf[128];
    std::snprintf(msgBuf, sizeof(msgBuf), "CONVERTED %s -> %d BPM, 4 CHORDS",
                  base.c_str(), static_cast<int>(outProfile.bpm));
    outProfile.statusMessage = msgBuf;

    return true;
}
