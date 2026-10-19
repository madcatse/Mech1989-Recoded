#pragma once
#include <filesystem>
#include <memory>
#include <string>

namespace mw::audio {
enum class MusicTrack { None, Theme, Bar, Success, Death, Ending };
const char* musicFileName(MusicTrack track);

class MusicPlayer {
public:
    MusicPlayer();
    ~MusicPlayer();
    MusicPlayer(const MusicPlayer&) = delete;
    MusicPlayer& operator=(const MusicPlayer&) = delete;
    // Headless App tests never initialise an output device. Requests are still
    // observable, and cannot affect the simulation or campaign random stream.
    bool initialize(const std::filesystem::path& resources, const std::filesystem::path& executableDirectory);
    void play(MusicTrack track);
    MusicTrack requestedTrack() const;
    std::string status() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
