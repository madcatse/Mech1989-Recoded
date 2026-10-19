#include "audio/music_player.h"
#include "audio/mt32_synth.h"
#include <windows.h>
#include <mmsystem.h>
#include <array>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace mw::audio {
const char* musicFileName(MusicTrack track) {
    switch (track) {
    case MusicTrack::Theme: return "MECH.MUS";
    case MusicTrack::Bar: return "MECHBAR.MUS";
    case MusicTrack::Success: return "LAUNCH.MUS"; // Original plays this at successful debrief, not launch.
    case MusicTrack::Death: return "DEATH.MUS";
    case MusicTrack::Ending: return "VICTORY.MUS";
    default: return "";
    }
}

struct MusicPlayer::Impl {
    struct Buffer {
        std::array<int16_t, 960> samples{}; // 10 ms stereo
        WAVEHDR header{};
    };
    std::array<MusicSequence, 6> sequences;
    Mt32Synth synth;
    MusicRenderer renderer;
    std::array<Buffer, 4> buffers;
    HWAVEOUT device = nullptr;
    HANDLE event = nullptr;
    std::thread thread;
    std::atomic<bool> quit{false};
    mutable std::mutex mutex;
    MusicTrack requested = MusicTrack::None;
    uint64_t revision = 0;
    std::string status = "Music output not initialised";

    ~Impl() {
        quit = true;
        if (event) SetEvent(event);
        if (thread.joinable()) thread.join();
        if (device) {
            waveOutReset(device);
            for (auto& b : buffers) if (b.header.dwFlags & WHDR_PREPARED)
                waveOutUnprepareHeader(device, &b.header, sizeof(WAVEHDR));
            waveOutClose(device);
        }
        if (event) CloseHandle(event);
    }
    void work() {
        uint64_t applied = 0;
        try {
            while (!quit) {
                {
                    std::lock_guard<std::mutex> guard(mutex);
                    if (applied != revision) {
                        waveOutReset(device);
                        synth.stopNotes();
                        renderer.start(requested == MusicTrack::None ? nullptr : &sequences[static_cast<size_t>(requested)]);
                        applied = revision;
                    }
                }
                for (auto& b : buffers) {
                    if (b.header.dwFlags & WHDR_INQUEUE) continue;
                    renderer.render(synth, b.samples.data(), 480);
                    if (waveOutWrite(device, &b.header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
                        throw std::runtime_error("Music output device stopped accepting audio");
                }
                WaitForSingleObject(event, 20);
            }
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> guard(mutex);
            status = e.what();
        }
    }
};

MusicPlayer::MusicPlayer() : impl_(std::make_unique<Impl>()) {}
MusicPlayer::~MusicPlayer() = default;
bool MusicPlayer::initialize(const std::filesystem::path& resources, const std::filesystem::path& executableDirectory) {
    if (impl_->thread.joinable()) return true;
    try {
        // Build a new owner so every failed startup releases partial resources.
        auto next = std::make_unique<Impl>();
        next->synth.open(findMt32Library(executableDirectory), findMt32Roms(resources));
        for (size_t i = 1; i < next->sequences.size(); ++i)
            next->sequences[i] = loadOriginalMusic(resources / musicFileName(static_cast<MusicTrack>(i)));
        next->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!next->event) throw std::runtime_error("Cannot create music output event");
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = 2;
        format.nSamplesPerSec = kAudioSampleRate;
        format.wBitsPerSample = 16;
        format.nBlockAlign = 4;
        format.nAvgBytesPerSec = kAudioSampleRate * 4;
        if (waveOutOpen(&next->device, WAVE_MAPPER, &format,
            reinterpret_cast<DWORD_PTR>(next->event), 0, CALLBACK_EVENT) != MMSYSERR_NOERROR)
            throw std::runtime_error("No stereo music output device available");
        for (auto& b : next->buffers) {
            b.header.lpData = reinterpret_cast<char*>(b.samples.data());
            b.header.dwBufferLength = static_cast<DWORD>(b.samples.size() * sizeof(int16_t));
            if (waveOutPrepareHeader(next->device, &b.header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
                throw std::runtime_error("Cannot prepare music output buffer");
        }
        next->status = next->synth.description();
        next->requested = impl_->requested;
        next->revision = impl_->revision;
        next->thread = std::thread([p = next.get()] { p->work(); });
        impl_ = std::move(next);
        return true;
    } catch (const std::exception& e) {
        impl_->status = e.what();
        return false;
    }
}
void MusicPlayer::play(MusicTrack track) {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    impl_->requested = track;
    ++impl_->revision;
    if (impl_->event) SetEvent(impl_->event);
}
MusicTrack MusicPlayer::requestedTrack() const {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    return impl_->requested;
}
std::string MusicPlayer::status() const {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    return impl_->status;
}
}
