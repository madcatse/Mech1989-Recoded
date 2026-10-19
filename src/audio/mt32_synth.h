#pragma once

#include "audio/original_music.h"
#include <memory>
#include <string>
#include <array>

namespace mw::audio {

constexpr uint32_t kAudioSampleRate = 48000;

// Optional, dynamically loaded Munt C interface. ROMs remain user supplied.
class Mt32Synth {
public:
    Mt32Synth();
    ~Mt32Synth();
    Mt32Synth(const Mt32Synth&) = delete;
    Mt32Synth& operator=(const Mt32Synth&) = delete;
    void open(const std::filesystem::path& library, const std::filesystem::path& romDirectory);
    void send(const uint8_t* bytes, uint32_t length);
    void sendAt(const uint8_t* bytes, uint32_t length, uint32_t synthTimestamp);
    uint32_t synthPosition() const;
    void render(int16_t* stereo, uint32_t frames);
    void stopNotes();
    std::string description() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Used both by the live player and offline acceptance renders. Calls into Munt
// at the event's exact output sample, independently of render block boundaries.
class MusicRenderer {
public:
    void start(const MusicSequence* sequence);
    void render(Mt32Synth& synth, int16_t* stereo, uint32_t frames);
    uint64_t position() const { return frame_; }
private:
    const MusicSequence* sequence_ = nullptr;
    size_t next_ = 0;
    uint64_t frame_ = 0;
    uint32_t synthOrigin_ = 0;
    bool started_ = false;
    uint64_t produced_ = 0;
    std::array<int16_t, 960> cache_{};
    uint32_t cachedFrames_ = 0;
};

std::filesystem::path findMt32Library(const std::filesystem::path& executableDirectory);
std::filesystem::path findMt32Roms(const std::filesystem::path& resourceRoot);

}
