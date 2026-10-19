#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
namespace mw::audio {
struct SpeakerRequest { uint16_t effect=0, priority=0; };
class SpeakerPlayer {
public:
    SpeakerPlayer();
    ~SpeakerPlayer();
    bool initialize(const std::filesystem::path& resources);
    void play(uint16_t effect,uint16_t priority);
    void stop();
    std::string status() const;
    uint64_t requestCount() const;
    SpeakerRequest lastRequest() const;
    bool stopped() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
