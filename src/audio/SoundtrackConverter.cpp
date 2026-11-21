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

inline float MidiNoteToHz(int note) {
    return 440.0f * std::pow(2.0f, static_cast<float>(note - 69) / 12.0f);
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

// ---------------------------------------------------------------------------
// Pure C++ Standard MIDI (.mid / .midi) Parser & Procedural Music Converter
// Format 0 & Format 1 support with note tracking, tempo, chords & lead arpeggio.
// ---------------------------------------------------------------------------
bool SoundtrackConverter::ConvertMidi(const std::string& midiPath, SoundtrackProfile& outProfile) {
    std::ifstream file(midiPath, std::ios::binary);
    if (!file.is_open()) return false;

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    if (data.size() < 14) return false;

    // Check "MThd" signature
    if (std::memcmp(data.data(), "MThd", 4) != 0) return false;

    const uint32_t headerLen = (static_cast<uint32_t>(data[4]) << 24) |
                               (static_cast<uint32_t>(data[5]) << 16) |
                               (static_cast<uint32_t>(data[6]) << 8)  |
                                static_cast<uint32_t>(data[7]);
    if (headerLen < 6 || data.size() < 8 + headerLen) return false;

    const uint16_t numTracks = (static_cast<uint16_t>(data[10]) << 8) | data[11];
    const uint16_t division  = (static_cast<uint16_t>(data[12]) << 8) | data[13];
    (void)division;

    struct TimedNote {
        uint32_t tick = 0;
        uint8_t  channel = 0;
        uint8_t  note = 60;
        uint8_t  velocity = 64;
    };
    std::vector<TimedNote> notes;
    std::string trackName;
    float detectedBpm = 120.0f;

    size_t offset = 8 + headerLen;
    for (int t = 0; t < numTracks && offset + 8 <= data.size(); ++t) {
        if (std::memcmp(&data[offset], "MTrk", 4) != 0) {
            size_t nextTrk = offset + 1;
            while (nextTrk + 8 <= data.size() && std::memcmp(&data[nextTrk], "MTrk", 4) != 0) {
                ++nextTrk;
            }
            if (nextTrk + 8 > data.size()) break;
            offset = nextTrk;
        }

        const uint32_t trkLen = (static_cast<uint32_t>(data[offset + 4]) << 24) |
                                (static_cast<uint32_t>(data[offset + 5]) << 16) |
                                (static_cast<uint32_t>(data[offset + 6]) << 8)  |
                                 static_cast<uint32_t>(data[offset + 7]);
        offset += 8;
        const size_t trkEnd = std::min(data.size(), offset + trkLen);
        const uint8_t* ptr = &data[offset];
        const uint8_t* end = &data[trkEnd];
        offset = trkEnd;

        uint32_t currentTick = 0;
        uint8_t runningStatus = 0;

        while (ptr < end) {
            // Delta-time (VLQ)
            uint32_t dt = 0;
            while (ptr < end) {
                uint8_t b = *ptr++;
                dt = (dt << 7) | (b & 0x7F);
                if ((b & 0x80) == 0) break;
            }
            currentTick += dt;
            if (ptr >= end) break;

            uint8_t status = *ptr;
            if (status & 0x80) {
                runningStatus = status;
                ++ptr;
            } else {
                status = runningStatus;
            }

            if (status == 0xFF) { // Meta Event
                if (ptr >= end) break;
                uint8_t metaType = *ptr++;
                uint32_t metaLen = 0;
                while (ptr < end) {
                    uint8_t b = *ptr++;
                    metaLen = (metaLen << 7) | (b & 0x7F);
                    if ((b & 0x80) == 0) break;
                }
                if (ptr + metaLen > end) break;

                if (metaType == 0x03 && trackName.empty() && metaLen > 0) {
                    trackName.assign(reinterpret_cast<const char*>(ptr), metaLen);
                } else if (metaType == 0x51 && metaLen == 3) {
                    uint32_t us = (static_cast<uint32_t>(ptr[0]) << 16) |
                                  (static_cast<uint32_t>(ptr[1]) << 8)  |
                                   static_cast<uint32_t>(ptr[2]);
                    if (us > 0) {
                        detectedBpm = 60000000.0f / static_cast<float>(us);
                    }
                }
                ptr += metaLen;
            } else if (status == 0xF0 || status == 0xF7) { // SysEx
                uint32_t sysexLen = 0;
                while (ptr < end) {
                    uint8_t b = *ptr++;
                    sysexLen = (sysexLen << 7) | (b & 0x7F);
                    if ((b & 0x80) == 0) break;
                }
                if (ptr + sysexLen > end) break;
                ptr += sysexLen;
            } else { // Channel voice
                uint8_t msg = status & 0xF0;
                uint8_t ch  = status & 0x0F;
                if (msg == 0x90) { // Note On
                    if (ptr + 1 >= end) break;
                    uint8_t note = *ptr++;
                    uint8_t vel  = *ptr++;
                    if (vel > 0 && ch != 9) { // skip percussion ch 10
                        notes.push_back({ currentTick, ch, note, vel });
                    }
                } else if (msg == 0x80) { // Note Off
                    if (ptr + 1 >= end) break;
                    ptr += 2;
                } else if (msg == 0xA0 || msg == 0xB0 || msg == 0xE0) {
                    if (ptr + 1 >= end) break;
                    ptr += 2;
                } else if (msg == 0xC0 || msg == 0xD0) {
                    if (ptr >= end) break;
                    ptr += 1;
                }
            }
        }
    }

    if (notes.empty()) {
        return false;
    }

    std::sort(notes.begin(), notes.end(), [](const TimedNote& a, const TimedNote& b) {
        return a.tick < b.tick;
    });

    const uint32_t maxTick = std::max(1u, notes.back().tick);
    detectedBpm = std::clamp(detectedBpm, 45.0f, 210.0f);
    outProfile.bpm = detectedBpm;

    // 1. Chords extraction: divide into 4 time quarters
    const double quarterTicks = static_cast<double>(maxTick) / 4.0;
    for (int sec = 0; sec < 4; ++sec) {
        const double tStart = sec * quarterTicks;
        const double tEnd   = (sec + 1) * quarterTicks;

        std::array<int, 12> pitchHist = {0};
        for (const auto& n : notes) {
            if (n.tick >= tStart && n.tick < tEnd) {
                pitchHist[n.note % 12]++;
            }
        }

        std::vector<std::pair<int, int>> sortedClasses;
        for (int p = 0; p < 12; ++p) {
            if (pitchHist[p] > 0) sortedClasses.push_back({ pitchHist[p], p });
        }
        std::sort(sortedClasses.rbegin(), sortedClasses.rend());

        std::vector<float> chordFreqs;
        for (size_t i = 0; i < sortedClasses.size() && chordFreqs.size() < 4; ++i) {
            int p = sortedClasses[i].second;
            float f = SnapToMusicalNote(MidiNoteToHz(48 + p), 70.0f, 220.0f);
            chordFreqs.push_back(f);
        }

        if (chordFreqs.empty()) chordFreqs.push_back(110.0f);
        while (chordFreqs.size() < 4) {
            float base = chordFreqs[0];
            if (chordFreqs.size() == 1) chordFreqs.push_back(base * 1.25992f);
            else if (chordFreqs.size() == 2) chordFreqs.push_back(base * 1.49831f);
            else if (chordFreqs.size() == 3) chordFreqs.push_back(base * 1.78180f);
        }

        std::sort(chordFreqs.begin(), chordFreqs.end());
        for (int k = 0; k < 4; ++k) {
            outProfile.chords[sec][k] = chordFreqs[k];
        }
    }

    // 2. Arpeggio / Lead melody (16 steps)
    const double stepTicks = static_cast<double>(maxTick) / 16.0;
    for (int step = 0; step < 16; ++step) {
        const double sStart = step * stepTicks;
        const double sEnd   = (step + 1) * stepTicks;

        int bestNote = -1;
        int maxScore = -1;
        for (const auto& n : notes) {
            if (n.tick >= sStart && n.tick < sEnd) {
                int score = n.velocity + n.note * 2;
                if (score > maxScore) {
                    maxScore = score;
                    bestNote = n.note;
                }
            }
        }

        if (bestNote >= 0) {
            outProfile.arpNotes[step] = SnapToMusicalNote(MidiNoteToHz(bestNote), 180.0f, 660.0f);
        } else if (step > 0) {
            outProfile.arpNotes[step] = outProfile.arpNotes[step - 1];
        } else {
            outProfile.arpNotes[step] = 220.0f;
        }
    }

    // 3. Sub-bass
    int lowestNote = 127;
    for (const auto& n : notes) {
        if (n.note < lowestNote) lowestNote = n.note;
    }
    float subFreq = (lowestNote < 127) ? MidiNoteToHz(lowestNote) : outProfile.chords[0][0] * 0.5f;
    while (subFreq > 60.0f) subFreq *= 0.5f;
    while (subFreq < 40.0f) subFreq *= 2.0f;
    outProfile.subBassFreq = subFreq;

    // File basename for display
    std::string base = midiPath;
    const size_t slash = base.find_last_of("/\\");
    if (slash != std::string::npos) base = base.substr(slash + 1);

    std::string displayName = trackName.empty() ? base : trackName;
    outProfile.name = "MIDI (" + displayName + ")";
    outProfile.isCustom = true;

    char msgBuf[128];
    std::snprintf(msgBuf, sizeof(msgBuf), "CONVERTED MIDI -> %d BPM, 4 CHORDS",
                  static_cast<int>(outProfile.bpm));
    outProfile.statusMessage = msgBuf;

    return true;
}

// ---------------------------------------------------------------------------
// Pure C++ Impulse Tracker (.it) Module Parser & Procedural Music Converter
// Reads IMPM header, orders, and packed patterns to extract harmony & tempo.
// ---------------------------------------------------------------------------
bool SoundtrackConverter::ConvertIT(const std::string& itPath, SoundtrackProfile& outProfile) {
    std::ifstream file(itPath, std::ios::binary);
    if (!file.is_open()) return false;

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    if (data.size() < 192) return false;

    // Check "IMPM" signature
    if (std::memcmp(data.data(), "IMPM", 4) != 0) return false;

    char rawName[27] = {0};
    std::memcpy(rawName, &data[4], 26);
    std::string songName(rawName);
    while (!songName.empty() && (songName.back() == ' ' || songName.back() == '\0')) {
        songName.pop_back();
    }

    const uint16_t ordNum = static_cast<uint16_t>(data[0x20]) | (static_cast<uint16_t>(data[0x21]) << 8);
    const uint16_t insNum = static_cast<uint16_t>(data[0x22]) | (static_cast<uint16_t>(data[0x23]) << 8);
    const uint16_t smpNum = static_cast<uint16_t>(data[0x24]) | (static_cast<uint16_t>(data[0x25]) << 8);
    const uint16_t patNum = static_cast<uint16_t>(data[0x26]) | (static_cast<uint16_t>(data[0x27]) << 8);
    const uint8_t initialTempo = data[0x33];

    float bpm = (initialTempo >= 32) ? static_cast<float>(initialTempo) : 125.0f;
    outProfile.bpm = std::clamp(bpm, 45.0f, 210.0f);

    const size_t ordersOffset = 0xC0;
    if (ordersOffset + ordNum > data.size()) return false;

    const size_t patPtrsOffset = ordersOffset + ordNum + insNum * 4 + smpNum * 4;
    if (patPtrsOffset + patNum * 4 > data.size()) return false;

    std::vector<uint32_t> patOffsets(patNum, 0);
    for (size_t p = 0; p < patNum; ++p) {
        const size_t off = patPtrsOffset + p * 4;
        patOffsets[p] = static_cast<uint32_t>(data[off]) |
                        (static_cast<uint32_t>(data[off + 1]) << 8) |
                        (static_cast<uint32_t>(data[off + 2]) << 16) |
                        (static_cast<uint32_t>(data[off + 3]) << 24);
    }

    struct ITNoteEvent {
        uint32_t globalRow = 0;
        int channel = 0;
        int note = 60; // 0..119
    };
    std::vector<ITNoteEvent> notes;

    uint32_t globalRow = 0;
    for (size_t o = 0; o < ordNum; ++o) {
        uint8_t patIdx = data[ordersOffset + o];
        if (patIdx == 255) break;
        if (patIdx == 254) continue;
        if (patIdx >= patNum) continue;

        uint32_t pOff = patOffsets[patIdx];
        if (pOff == 0 || pOff + 8 > data.size()) continue;

        uint16_t dataLen = static_cast<uint16_t>(data[pOff]) | (static_cast<uint16_t>(data[pOff + 1]) << 8);
        uint16_t rows    = static_cast<uint16_t>(data[pOff + 2]) | (static_cast<uint16_t>(data[pOff + 3]) << 8);
        if (rows == 0) rows = 64;

        const uint8_t* ptr = &data[pOff + 8];
        const uint8_t* end = std::min(&data[pOff + 8 + dataLen], data.data() + data.size());

        uint8_t lastMask[64] = {0};
        uint8_t lastNote[64] = {255};

        uint16_t r = 0;
        while (r < rows && ptr < end) {
            uint8_t b = *ptr++;
            if (b == 0) {
                ++r;
                ++globalRow;
                continue;
            }

            int ch = (b - 1) & 63;
            uint8_t mask = 0;
            if (b & 128) {
                if (ptr >= end) break;
                mask = *ptr++;
                lastMask[ch] = mask;
            } else {
                mask = lastMask[ch];
            }

            if (mask & 1) { // Note follows
                if (ptr >= end) break;
                uint8_t n = *ptr++;
                lastNote[ch] = n;
                if (n < 120) {
                    notes.push_back({ globalRow, ch, n });
                }
            } else if (mask & 16) { // Reuse note
                if (lastNote[ch] < 120) {
                    notes.push_back({ globalRow, ch, lastNote[ch] });
                }
            }

            if (mask & 2) { if (ptr < end) ++ptr; }
            if (mask & 4) { if (ptr < end) ++ptr; }
            if (mask & 8) { if (ptr + 1 < end) ptr += 2; }
        }
    }

    if (notes.empty()) {
        return false;
    }

    const uint32_t maxRow = std::max(1u, globalRow);

    // 1. Chords (4 sections)
    const double quarterRows = static_cast<double>(maxRow) / 4.0;
    for (int sec = 0; sec < 4; ++sec) {
        const double rStart = sec * quarterRows;
        const double rEnd   = (sec + 1) * quarterRows;

        std::array<int, 12> pitchHist = {0};
        for (const auto& n : notes) {
            if (n.globalRow >= rStart && n.globalRow < rEnd) {
                pitchHist[n.note % 12]++;
            }
        }

        std::vector<std::pair<int, int>> sortedClasses;
        for (int p = 0; p < 12; ++p) {
            if (pitchHist[p] > 0) sortedClasses.push_back({ pitchHist[p], p });
        }
        std::sort(sortedClasses.rbegin(), sortedClasses.rend());

        std::vector<float> chordFreqs;
        for (size_t i = 0; i < sortedClasses.size() && chordFreqs.size() < 4; ++i) {
            int p = sortedClasses[i].second;
            float f = SnapToMusicalNote(MidiNoteToHz(48 + p), 70.0f, 220.0f);
            chordFreqs.push_back(f);
        }

        if (chordFreqs.empty()) chordFreqs.push_back(110.0f);
        while (chordFreqs.size() < 4) {
            float base = chordFreqs[0];
            if (chordFreqs.size() == 1) chordFreqs.push_back(base * 1.25992f);
            else if (chordFreqs.size() == 2) chordFreqs.push_back(base * 1.49831f);
            else if (chordFreqs.size() == 3) chordFreqs.push_back(base * 1.78180f);
        }

        std::sort(chordFreqs.begin(), chordFreqs.end());
        for (int k = 0; k < 4; ++k) {
            outProfile.chords[sec][k] = chordFreqs[k];
        }
    }

    // 2. Arpeggio / Lead (16 steps)
    const double stepRows = static_cast<double>(maxRow) / 16.0;
    for (int step = 0; step < 16; ++step) {
        const double sStart = step * stepRows;
        const double sEnd   = (step + 1) * stepRows;

        int highestNote = -1;
        for (const auto& n : notes) {
            if (n.globalRow >= sStart && n.globalRow < sEnd) {
                if (n.note > highestNote) {
                    highestNote = n.note;
                }
            }
        }

        if (highestNote >= 0) {
            outProfile.arpNotes[step] = SnapToMusicalNote(MidiNoteToHz(highestNote), 180.0f, 660.0f);
        } else if (step > 0) {
            outProfile.arpNotes[step] = outProfile.arpNotes[step - 1];
        } else {
            outProfile.arpNotes[step] = 220.0f;
        }
    }

    // 3. Sub-bass
    int lowestNote = 120;
    for (const auto& n : notes) {
        if (n.note < lowestNote) lowestNote = n.note;
    }
    float subFreq = (lowestNote < 120) ? MidiNoteToHz(lowestNote) : outProfile.chords[0][0] * 0.5f;
    while (subFreq > 60.0f) subFreq *= 0.5f;
    while (subFreq < 40.0f) subFreq *= 2.0f;
    outProfile.subBassFreq = subFreq;

    std::string base = itPath;
    const size_t slash = base.find_last_of("/\\");
    if (slash != std::string::npos) base = base.substr(slash + 1);

    std::string displayName = songName.empty() ? base : songName;
    outProfile.name = "IT (" + displayName + ")";
    outProfile.isCustom = true;

    char msgBuf[128];
    std::snprintf(msgBuf, sizeof(msgBuf), "CONVERTED IT -> %d BPM, 4 CHORDS",
                  static_cast<int>(outProfile.bpm));
    outProfile.statusMessage = msgBuf;

    return true;
}

// ---------------------------------------------------------------------------
// Universal format converter (auto-detects WAV, MIDI, IT by header / extension)
// ---------------------------------------------------------------------------
bool SoundtrackConverter::ConvertFile(const std::string& filePath, SoundtrackProfile& outProfile) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return false;

    char magic[12] = {0};
    file.read(magic, sizeof(magic));
    file.close();

    // 1. Magic signatures
    if (std::memcmp(magic, "MThd", 4) == 0) {
        return ConvertMidi(filePath, outProfile);
    }
    if (std::memcmp(magic, "IMPM", 4) == 0) {
        return ConvertIT(filePath, outProfile);
    }
    if (std::memcmp(magic, "RIFF", 4) == 0 && std::memcmp(&magic[8], "WAVE", 4) == 0) {
        return ConvertWav(filePath, outProfile);
    }

    // 2. File extensions (case-insensitive)
    std::string ext;
    const size_t dot = filePath.find_last_of('.');
    if (dot != std::string::npos) {
        ext = filePath.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
    }

    if (ext == ".mid" || ext == ".midi") {
        return ConvertMidi(filePath, outProfile);
    }
    if (ext == ".it") {
        return ConvertIT(filePath, outProfile);
    }
    if (ext == ".wav") {
        return ConvertWav(filePath, outProfile);
    }

    // 3. Fallback try in sequence
    if (ConvertMidi(filePath, outProfile)) return true;
    if (ConvertIT(filePath, outProfile)) return true;
    if (ConvertWav(filePath, outProfile)) return true;

    return false;
}

bool SoundtrackConverter::SaveProfile(const std::string& filePath, const SoundtrackProfile& profile) {
    std::ofstream out(filePath);
    if (!out.is_open()) return false;
    out << "NAME " << profile.name << "\n";
    out << "BPM " << profile.bpm << "\n";
    out << "SUBBASS " << profile.subBassFreq << "\n";
    out << "CUSTOM " << (profile.isCustom ? 1 : 0) << "\n";
    for (int sec = 0; sec < 4; ++sec) {
        out << "CHORD " << sec << " "
            << profile.chords[sec][0] << " "
            << profile.chords[sec][1] << " "
            << profile.chords[sec][2] << " "
            << profile.chords[sec][3] << "\n";
    }
    out << "ARP ";
    for (int i = 0; i < 16; ++i) {
        out << profile.arpNotes[i] << " ";
    }
    out << "\n";
    out << "STATUS " << profile.statusMessage << "\n";
    return true;
}

bool SoundtrackConverter::LoadProfile(const std::string& filePath, SoundtrackProfile& outProfile) {
    std::ifstream in(filePath);
    if (!in.is_open()) return false;
    std::string token;
    while (in >> token) {
        if (token == "NAME") {
            std::getline(in >> std::ws, outProfile.name);
        } else if (token == "BPM") {
            in >> outProfile.bpm;
        } else if (token == "SUBBASS") {
            in >> outProfile.subBassFreq;
        } else if (token == "CUSTOM") {
            int c = 0; in >> c;
            outProfile.isCustom = (c != 0);
        } else if (token == "CHORD") {
            int sec = 0; in >> sec;
            if (sec >= 0 && sec < 4) {
                in >> outProfile.chords[sec][0]
                   >> outProfile.chords[sec][1]
                   >> outProfile.chords[sec][2]
                   >> outProfile.chords[sec][3];
            }
        } else if (token == "ARP") {
            for (int i = 0; i < 16; ++i) {
                in >> outProfile.arpNotes[i];
            }
        } else if (token == "STATUS") {
            std::getline(in >> std::ws, outProfile.statusMessage);
        }
    }
    return true;
}
