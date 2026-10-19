#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace mw::audio {

// MW_MAIN IRQ0: PIT divisor 8192, sequencer on alternate interrupts.
constexpr uint32_t kMusicCallbackClocks = 16384;
constexpr uint32_t kPitClockNumerator = 14318180;
constexpr uint32_t kPitClockDenominator = 12;

struct MidiByte {
    uint32_t callback = 0;
    uint8_t value = 0;
    // Standard MIDI timing in 1/96 microseconds. DOS callback is audit-only.
    uint64_t timeUnits = 0;
};

struct MusicSequence {
    std::vector<MidiByte> bytes;
    uint32_t endCallback = 0;
    uint64_t endTimeUnits = 0;
};

// Strictly bounded reader for the original format-0, 96-PPQN MUS resources.
// Retains F7 continuations, raw real-time bytes and the DOS integer tempo clock.
MusicSequence readOriginalMusic(const std::vector<uint8_t>& data);
MusicSequence loadOriginalMusic(const std::filesystem::path& path);
uint64_t callbackSample(uint32_t callback, uint32_t sampleRate);
uint64_t musicSample(uint64_t timeUnits, uint32_t sampleRate);

}
