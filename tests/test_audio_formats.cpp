#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

#include "audio/SoundtrackConverter.h"

// Helper: writes a minimal valid Standard MIDI file (Format 0)
static std::string CreateTestMidiFile(const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    assert(out.is_open());

    // MThd chunk
    // Length: 6, Format: 0, Tracks: 1, Division: 96 ticks/quarter
    const uint8_t mthd[] = {
        'M', 'T', 'h', 'd',
        0x00, 0x00, 0x00, 0x06,
        0x00, 0x00,             // Format 0
        0x00, 0x01,             // 1 track
        0x00, 0x60              // 96 ticks per quarter note
    };
    out.write(reinterpret_cast<const char*>(mthd), sizeof(mthd));

    // Track data buffer
    std::vector<uint8_t> trk;

    // Set Tempo: 125 BPM -> 60,000,000 / 125 = 480,000 us (0x075300)
    // Delta-time 0
    trk.push_back(0x00);
    trk.push_back(0xFF); trk.push_back(0x51); trk.push_back(0x03);
    trk.push_back(0x07); trk.push_back(0x53); trk.push_back(0x00);

    // Track Name: "MarsTest"
    trk.push_back(0x00);
    trk.push_back(0xFF); trk.push_back(0x03); trk.push_back(0x08);
    const char* name = "MarsTest";
    trk.insert(trk.end(), name, name + 8);

    // Some notes across 4 beats (384 ticks)
    // Note On C4 (60)
    trk.push_back(0x00); // dt=0
    trk.push_back(0x90); trk.push_back(60); trk.push_back(90);
    // Note Off C4 at dt=48
    trk.push_back(48);
    trk.push_back(0x80); trk.push_back(60); trk.push_back(0);

    // Note On E4 (64)
    trk.push_back(48); // dt=48
    trk.push_back(0x90); trk.push_back(64); trk.push_back(85);
    // Note Off E4 at dt=48
    trk.push_back(48);
    trk.push_back(0x80); trk.push_back(64); trk.push_back(0);

    // Note On G4 (67)
    trk.push_back(48);
    trk.push_back(0x90); trk.push_back(67); trk.push_back(80);
    // Note Off G4 at dt=48
    trk.push_back(48);
    trk.push_back(0x80); trk.push_back(67); trk.push_back(0);

    // Note On B4 (71)
    trk.push_back(48);
    trk.push_back(0x90); trk.push_back(71); trk.push_back(95);
    // Note Off B4 at dt=48
    trk.push_back(48);
    trk.push_back(0x80); trk.push_back(71); trk.push_back(0);

    // End of Track
    trk.push_back(0x00);
    trk.push_back(0xFF); trk.push_back(0x2F); trk.push_back(0x00);

    // MTrk header
    const uint8_t mtrkHeader[4] = { 'M', 'T', 'r', 'k' };
    out.write(reinterpret_cast<const char*>(mtrkHeader), 4);
    const uint32_t len = static_cast<uint32_t>(trk.size());
    const uint8_t lenBytes[4] = {
        static_cast<uint8_t>((len >> 24) & 0xFF),
        static_cast<uint8_t>((len >> 16) & 0xFF),
        static_cast<uint8_t>((len >> 8) & 0xFF),
        static_cast<uint8_t>(len & 0xFF)
    };
    out.write(reinterpret_cast<const char*>(lenBytes), 4);
    out.write(reinterpret_cast<const char*>(trk.data()), trk.size());
    out.close();

    return path;
}

// Helper: writes a minimal valid Impulse Tracker module (.it)
static std::string CreateTestITFile(const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    assert(out.is_open());

    std::vector<uint8_t> itData(256, 0);

    // IMPM magic
    itData[0] = 'I'; itData[1] = 'M'; itData[2] = 'P'; itData[3] = 'M';

    // Song name at offset 4 (26 chars)
    const char* title = "MarsTracker";
    std::memcpy(&itData[4], title, std::strlen(title));

    // OrdNum at 0x20 = 1
    itData[0x20] = 1; itData[0x21] = 0;
    // InsNum at 0x22 = 0
    itData[0x22] = 0; itData[0x23] = 0;
    // SmpNum at 0x24 = 0
    itData[0x24] = 0; itData[0x25] = 0;
    // PatNum at 0x26 = 1
    itData[0x26] = 1; itData[0x27] = 0;

    // Initial tempo at 0x33 = 135 BPM
    itData[0x33] = 135;

    // Orders offset is 0xC0 (192). Order[0] = pattern 0
    itData[0xC0] = 0;

    // Pattern parapointers start at 0xC0 + OrdNum(1) + InsNum*4 + SmpNum*4 = 193
    // We put Pattern 0 at offset 200
    const uint32_t patOffset = 200;
    itData.resize(patOffset);
    itData[193] = static_cast<uint8_t>(patOffset & 0xFF);
    itData[194] = static_cast<uint8_t>((patOffset >> 8) & 0xFF);
    itData[195] = static_cast<uint8_t>((patOffset >> 16) & 0xFF);
    itData[196] = static_cast<uint8_t>((patOffset >> 24) & 0xFF);

    // Pattern 0 data
    // 2 bytes: length of data
    // 2 bytes: number of rows (e.g. 32)
    // 4 bytes: reserved
    // then packed rows
    std::vector<uint8_t> patBody;
    for (int r = 0; r < 32; ++r) {
        // Channel 1 (byte 1), mask = 1 (note follows)
        patBody.push_back(static_cast<uint8_t>(1 | 128)); // channel 1 + mask follows
        patBody.push_back(1);       // mask: note follows
        patBody.push_back(static_cast<uint8_t>(48 + (r % 12))); // note C-4 + offset
        // End of row
        patBody.push_back(0);
    }

    uint16_t dataLen = static_cast<uint16_t>(patBody.size());
    uint16_t numRows = 32;

    itData.push_back(static_cast<uint8_t>(dataLen & 0xFF));
    itData.push_back(static_cast<uint8_t>((dataLen >> 8) & 0xFF));
    itData.push_back(static_cast<uint8_t>(numRows & 0xFF));
    itData.push_back(static_cast<uint8_t>((numRows >> 8) & 0xFF));
    itData.push_back(0); itData.push_back(0); itData.push_back(0); itData.push_back(0); // 4 reserved bytes

    itData.insert(itData.end(), patBody.begin(), patBody.end());

    out.write(reinterpret_cast<const char*>(itData.data()), itData.size());
    out.close();

    return path;
}

int main() {
    std::cout << "[Test Audio Formats] Starting tests...\n";

    const std::string midiFile = "/tmp/test_song.mid";
    const std::string itFile   = "/tmp/test_module.it";
    const std::string profFile = "/tmp/test_soundtrack_profile.dat";

    CreateTestMidiFile(midiFile);
    CreateTestITFile(itFile);

    // 1. Test MIDI Conversion
    {
        SoundtrackProfile prof;
        bool ok = SoundtrackConverter::ConvertMidi(midiFile, prof);
        assert(ok && "ConvertMidi failed on valid MIDI file");
        assert(prof.isCustom && "Profile should be marked as custom");
        assert(std::fabs(prof.bpm - 125.0f) < 2.0f && "BPM should match 125");
        assert(prof.name.find("MarsTest") != std::string::npos && "Name should contain track name");
        assert(prof.subBassFreq > 20.0f && prof.subBassFreq < 100.0f && "Sub-bass in musical range");
        std::cout << "  ✓ ConvertMidi passed: " << prof.name << " (" << prof.bpm << " BPM)\n";
    }

    // 2. Test IT Module Conversion
    {
        SoundtrackProfile prof;
        bool ok = SoundtrackConverter::ConvertIT(itFile, prof);
        assert(ok && "ConvertIT failed on valid IT file");
        assert(prof.isCustom && "Profile should be marked as custom");
        assert(prof.bpm == 135.0f && "IT initial tempo should be 135 BPM");
        assert(prof.name.find("MarsTracker") != std::string::npos && "Name should contain module title");
        std::cout << "  ✓ ConvertIT passed: " << prof.name << " (" << prof.bpm << " BPM)\n";
    }

    // 3. Test Universal ConvertFile (auto-detection)
    {
        SoundtrackProfile profMidi;
        bool ok1 = SoundtrackConverter::ConvertFile(midiFile, profMidi);
        assert(ok1 && "ConvertFile should auto-detect and parse MIDI");
        assert(profMidi.name.find("MIDI") != std::string::npos);

        SoundtrackProfile profIT;
        bool ok2 = SoundtrackConverter::ConvertFile(itFile, profIT);
        assert(ok2 && "ConvertFile should auto-detect and parse IT");
        assert(profIT.name.find("IT") != std::string::npos);

        std::cout << "  ✓ ConvertFile universal auto-detection passed\n";
    }

    // 4. Test Profile Serialization & Deserialization
    {
        SoundtrackProfile orig;
        orig.name = "Test Mars Profile";
        orig.bpm = 128.0f;
        orig.subBassFreq = 45.0f;
        orig.isCustom = true;
        orig.statusMessage = "Test Profile Status Message";
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                orig.chords[i][j] = 100.0f + i * 20.0f + j * 5.0f;
            }
        }
        for (int i = 0; i < 16; ++i) {
            orig.arpNotes[i] = 200.0f + i * 15.0f;
        }

        bool saved = SoundtrackConverter::SaveProfile(profFile, orig);
        assert(saved && "SaveProfile should succeed");

        SoundtrackProfile loaded;
        bool loadedOk = SoundtrackConverter::LoadProfile(profFile, loaded);
        assert(loadedOk && "LoadProfile should succeed");
        assert(loaded.name == orig.name);
        assert(loaded.bpm == orig.bpm);
        assert(loaded.subBassFreq == orig.subBassFreq);
        assert(loaded.isCustom == orig.isCustom);
        assert(loaded.statusMessage == orig.statusMessage);
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                assert(std::fabs(loaded.chords[i][j] - orig.chords[i][j]) < 0.01f);
            }
        }
        for (int i = 0; i < 16; ++i) {
            assert(std::fabs(loaded.arpNotes[i] - orig.arpNotes[i]) < 0.01f);
        }

        std::cout << "  ✓ SaveProfile and LoadProfile serialization passed\n";
    }

    // Clean up temporary files
    std::remove(midiFile.c_str());
    std::remove(itFile.c_str());
    std::remove(profFile.c_str());

    std::cout << "[Test Audio Formats] ALL TESTS PASSED SUCCESSFULLY!\n";
    return 0;
}
