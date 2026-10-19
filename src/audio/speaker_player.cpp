#include "audio/speaker_player.h"
#include "audio/pc_speaker.h"
#include <windows.h>
#include <mmsystem.h>
#include <array>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <thread>
namespace mw::audio {
struct SpeakerPlayer::Impl {
    struct Buffer { std::array<int16_t,960> samples{};WAVEHDR header{}; };
    std::array<Buffer,4> buffers;
    std::unique_ptr<PcSpeaker> speaker;
    HWAVEOUT device=nullptr;HANDLE event=nullptr;
    std::thread thread;std::atomic<bool> quit{false};
    mutable std::mutex mutex;
    std::vector<SpeakerRequest> pending;
    uint64_t count=0;SpeakerRequest last{};
    bool reset=false,isStopped=true;
    std::string status="PC Speaker output not initialised";
    ~Impl() {
        quit=true;if(event) SetEvent(event);
        if(thread.joinable()) thread.join();
        if(device) {
            waveOutReset(device);
            for(auto& b:buffers) if(b.header.dwFlags&WHDR_PREPARED) waveOutUnprepareHeader(device,&b.header,sizeof(WAVEHDR));
            waveOutClose(device);
        }
        if(event) CloseHandle(event);
    }
    void work() {
        try {
            while(!quit) {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if(reset) { waveOutReset(device);speaker->stop();reset=false; }
                    for(auto r:pending) speaker->play(r.effect,r.priority);
                    pending.clear();
                }
                for(auto& b:buffers) if(!(b.header.dwFlags&WHDR_INQUEUE)) {
                    speaker->render(b.samples.data(),480);
                    if(waveOutWrite(device,&b.header,sizeof(WAVEHDR))!=MMSYSERR_NOERROR)
                        throw std::runtime_error("PC Speaker output device stopped accepting audio");
                }
                WaitForSingleObject(event,20);
            }
        } catch(const std::exception& e) { std::lock_guard<std::mutex> lock(mutex);status=e.what(); }
    }
};
SpeakerPlayer::SpeakerPlayer():impl_(std::make_unique<Impl>()) {}
SpeakerPlayer::~SpeakerPlayer()=default;
bool SpeakerPlayer::initialize(const std::filesystem::path& resources) {
    if(impl_->thread.joinable()) return true;
    try {
        auto next=std::make_unique<Impl>();
        next->speaker=std::make_unique<PcSpeaker>(SpeakerBank::load(resources/"BTECHSND.SND"));
        next->event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!next->event) throw std::runtime_error("Cannot create speaker output event");
        WAVEFORMATEX f{};f.wFormatTag=WAVE_FORMAT_PCM;f.nChannels=2;f.nSamplesPerSec=48000;
        f.wBitsPerSample=16;f.nBlockAlign=4;f.nAvgBytesPerSec=192000;
        if(waveOutOpen(&next->device,WAVE_MAPPER,&f,reinterpret_cast<DWORD_PTR>(next->event),0,CALLBACK_EVENT)!=MMSYSERR_NOERROR)
            throw std::runtime_error("No PC Speaker output device available");
        for(auto& b:next->buffers) {
            b.header.lpData=reinterpret_cast<char*>(b.samples.data());b.header.dwBufferLength=sizeof(b.samples);
            if(waveOutPrepareHeader(next->device,&b.header,sizeof(WAVEHDR))!=MMSYSERR_NOERROR)
                throw std::runtime_error("Cannot prepare speaker output buffer");
        }
        next->status="PC Speaker: 15 original programs,48 kHz output";
        next->thread=std::thread([p=next.get()] { p->work(); });impl_=std::move(next);return true;
    } catch(const std::exception& e) { impl_->status=e.what();return false; }
}
void SpeakerPlayer::play(uint16_t effect,uint16_t priority) {
    if(effect>=15) return;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->last={effect,priority};++impl_->count;impl_->isStopped=false;
    if(impl_->pending.size()==128) impl_->pending.erase(impl_->pending.begin());
    impl_->pending.push_back({effect,priority});if(impl_->event) SetEvent(impl_->event);
}
void SpeakerPlayer::stop() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->pending.clear();impl_->reset=true;impl_->isStopped=true;
    if(impl_->event) SetEvent(impl_->event);
}
std::string SpeakerPlayer::status() const { std::lock_guard<std::mutex> l(impl_->mutex);return impl_->status; }
uint64_t SpeakerPlayer::requestCount() const { std::lock_guard<std::mutex> l(impl_->mutex);return impl_->count; }
SpeakerRequest SpeakerPlayer::lastRequest() const { std::lock_guard<std::mutex> l(impl_->mutex);return impl_->last; }
bool SpeakerPlayer::stopped() const { std::lock_guard<std::mutex> l(impl_->mutex);return impl_->isStopped; }
}
