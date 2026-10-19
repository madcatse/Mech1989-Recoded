#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace mw::audio {
constexpr uint32_t kSpeakerPitHz = 1193182;
constexpr uint32_t kSpeakerCallbackDivisor = 5041;
struct SpeakerBank {
    std::vector<uint8_t> bytes;
    std::array<uint16_t, 15> offsets{};
    static SpeakerBank load(const std::filesystem::path& path);
    static SpeakerBank decode(const std::vector<uint8_t>& bytes);
};
struct SpeakerChannel {
    std::array<uint16_t, 24> words{};
    uint16_t priority = 0;
    bool active = false;
};
struct SpeakerOutput { int channel = -1; uint16_t divisor = 0; bool gate = false; };
class PcSpeaker {
public:
    explicit PcSpeaker(SpeakerBank bank);
    bool play(uint16_t effect, uint16_t priority);
    void stop();
    SpeakerOutput tick();
    const std::array<SpeakerChannel, 4>& channels() const { return channels_; }
    // Stereo PCM. Callback/sample timing is independent of render buffer size.
    void render(int16_t* stereo, size_t frames, uint32_t sampleRate = 48000);
private:
    void advance(SpeakerChannel& channel);
    SpeakerBank bank_;
    std::array<SpeakerChannel, 4> channels_{};
    SpeakerOutput output_{};
    uint64_t clock_ = 0;
    bool firstSample_ = true;
    double phase_ = 0, previousInput_ = 0, highPass_ = 0, lowPass_ = 0;
};
}
