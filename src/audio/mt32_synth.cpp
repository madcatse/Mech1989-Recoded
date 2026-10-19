#include "audio/mt32_synth.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace mw::audio {
namespace {
// C ABI declarations for replaceable Munt DLLs. Releases bundle the library;
// Roland ROMs are supplied separately by the player.
struct Context;
union ReportHandler { const void* v0; };
struct RomInfo { const char* fields[6]; };
template<class T> T symbol(HMODULE dll, const char* name) {
    const auto address = GetProcAddress(dll, name);
    if (!address) throw std::runtime_error(std::string("Munt API missing: ") + name);
    T result;
    static_assert(sizeof(result) == sizeof(address));
    std::memcpy(&result, &address, sizeof(result));
    return result;
}
std::filesystem::path envPath(const wchar_t* name) {
    std::array<wchar_t, 32768> value{};
    const auto size = GetEnvironmentVariableW(name, value.data(), static_cast<DWORD>(value.size()));
    return size && size < value.size() ? std::filesystem::path(value.data()) : std::filesystem::path();
}
}

struct Mt32Synth::Impl {
    HMODULE dll = nullptr;
    Context* context = nullptr;
    void (__cdecl* freeContext)(Context*) = nullptr;
    void (__cdecl* parse)(const Context*, const uint8_t*, uint32_t) = nullptr;
    void (__cdecl* parseAt)(const Context*, const uint8_t*, uint32_t, uint32_t) = nullptr;
    uint32_t (__cdecl* position)(const Context*) = nullptr;
    void (__cdecl* render)(const Context*, int16_t*, uint32_t) = nullptr;
    void (__cdecl* flush)(const Context*) = nullptr;
    std::array<std::vector<uint8_t>, 2> roms;
    std::string description;
    ~Impl() {
        if (context && freeContext) freeContext(context);
        if (dll) FreeLibrary(dll);
    }
};

Mt32Synth::Mt32Synth() = default;
Mt32Synth::~Mt32Synth() = default;

void Mt32Synth::open(const std::filesystem::path& library, const std::filesystem::path& romDirectory) {
    auto next = std::make_unique<Impl>();
    // Absolute paths only: never search the working directory for dependencies.
    next->dll = LoadLibraryExW(std::filesystem::absolute(library).c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!next->dll) throw std::runtime_error("Cannot load MT-32 emulator: " + library.string());
    const auto create = symbol<Context* (__cdecl*)(ReportHandler, void*)>(next->dll, "mt32emu_create_context");
    next->freeContext = symbol<void (__cdecl*)(Context*)>(next->dll, "mt32emu_free_context");
    next->parse = symbol<void (__cdecl*)(const Context*, const uint8_t*, uint32_t)>(next->dll, "mt32emu_parse_stream");
    next->parseAt = symbol<void (__cdecl*)(const Context*, const uint8_t*, uint32_t, uint32_t)>(next->dll, "mt32emu_parse_stream_at");
    next->position = symbol<uint32_t (__cdecl*)(const Context*)>(next->dll, "mt32emu_get_internal_rendered_sample_count");
    next->render = symbol<void (__cdecl*)(const Context*, int16_t*, uint32_t)>(next->dll, "mt32emu_render_bit16s");
    next->flush = symbol<void (__cdecl*)(const Context*)>(next->dll, "mt32emu_flush_midi_queue");
    next->context = create({nullptr}, nullptr);
    if (!next->context) throw std::runtime_error("Cannot create MT-32 emulator");
    const auto add = symbol<int (__cdecl*)(Context*, const uint8_t*, size_t, const char*)>(next->dll, "mt32emu_add_rom_data");
    const std::array<const wchar_t*, 2> names{L"MT32_CONTROL.ROM", L"MT32_PCM.ROM"};
    for (size_t n = 0; n < names.size(); ++n) {
        const auto path = romDirectory / names[n];
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("MT-32 ROM missing: " + path.string());
        next->roms[n] = {std::istreambuf_iterator<char>(input), {}};
        if (add(next->context, next->roms[n].data(), next->roms[n].size(), nullptr) != static_cast<int>(n + 1))
            throw std::runtime_error("Unrecognised MT-32 ROM: " + path.string());
    }
    symbol<void (__cdecl*)(Context*, double)>(next->dll, "mt32emu_set_stereo_output_samplerate")(
        next->context, kAudioSampleRate);
    if (symbol<int (__cdecl*)(const Context*)>(next->dll, "mt32emu_open_synth")(next->context) != 0)
        throw std::runtime_error("Cannot open MT-32 with this ROM pair");
    // Leave headroom for the combined dry/reverb peaks in the original scores.
    symbol<void (__cdecl*)(const Context*, float)>(next->dll, "mt32emu_set_output_gain")(next->context, 0.75f);
    symbol<void (__cdecl*)(const Context*, float)>(next->dll, "mt32emu_set_reverb_output_gain")(next->context, 0.75f);
    const auto rate = symbol<uint32_t (__cdecl*)(const Context*)>(next->dll,
        "mt32emu_get_actual_stereo_output_samplerate")(next->context);
    if (rate != kAudioSampleRate) throw std::runtime_error("Unexpected MT-32 output rate");
    RomInfo info{};
    symbol<void (__cdecl*)(const Context*, RomInfo*)>(next->dll, "mt32emu_get_rom_info")(next->context, &info);
    next->description = std::string("Munt ") + symbol<const char* (__cdecl*)()>(next->dll,
        "mt32emu_get_library_version_string")() + "; " + (info.fields[1] ? info.fields[1] : "unknown control ROM") +
        "; " + (info.fields[4] ? info.fields[4] : "unknown PCM ROM") + "; 48000 Hz stereo";
    impl_ = std::move(next);
}
void Mt32Synth::send(const uint8_t* bytes, uint32_t length) { impl_->parse(impl_->context, bytes, length); }
void Mt32Synth::sendAt(const uint8_t* bytes, uint32_t length, uint32_t timestamp) {
    impl_->parseAt(impl_->context, bytes, length, timestamp);
}
uint32_t Mt32Synth::synthPosition() const { return impl_->position(impl_->context); }
void Mt32Synth::render(int16_t* stereo, uint32_t frames) { impl_->render(impl_->context, stereo, frames); }
void Mt32Synth::stopNotes() {
    impl_->flush(impl_->context);
    for (int ch = 15; ch >= 0; --ch) {
        const uint8_t bytes[]{static_cast<uint8_t>(0xb0 + ch), 123, 0};
        send(bytes, 3);
    }
}
std::string Mt32Synth::description() const { return impl_ ? impl_->description : "MT-32 unavailable"; }

void MusicRenderer::start(const MusicSequence* sequence) {
    sequence_ = sequence; next_ = 0; frame_ = produced_ = 0; started_ = false; cachedFrames_ = 0;
}
void MusicRenderer::render(Mt32Synth& synth, int16_t* stereo, uint32_t frames) {
    if (!started_) { synthOrigin_ = synth.synthPosition(); started_ = true; }
    // Schedule ahead of the resampler's lookahead, in Munt's native 32-kHz
    // clock. Sending "ASAP" after rendering to an output sample is block-size
    // dependent because a resampler can already have consumed later input.
    while (frames) {
        if (!cachedFrames_) {
            // Fixed synthesis quantum also keeps Munt's per-part random pitch
            // jitter processing independent of the caller's output buffer size.
            const auto horizon = produced_ + 480 + 1024;
            while (sequence_ && next_ < sequence_->bytes.size()) {
                const auto& event = sequence_->bytes[next_];
                if (musicSample(event.timeUnits, kAudioSampleRate) > horizon) break;
                synth.sendAt(&event.value, 1, synthOrigin_ + static_cast<uint32_t>(musicSample(event.timeUnits, 32000)));
                ++next_;
            }
            synth.render(cache_.data(), 480);
            produced_ += 480;
            cachedFrames_ = 480;
        }
        const auto count = std::min(frames, cachedFrames_);
        std::copy_n(cache_.data() + (480-cachedFrames_)*2, count*2, stereo);
        stereo += count*2;
        cachedFrames_ -= count;
        frames -= count;
        frame_ += count;
    }
}

std::filesystem::path findMt32Library(const std::filesystem::path& exe) {
    const auto override = envPath(L"MW_MT32_DLL");
    if (!override.empty()) return override;
    const auto local = exe / L"mt32emu-2.dll";
    if (std::filesystem::exists(local)) return local;
    return envPath(L"ProgramFiles") / L"DOSBox Staging" / L"mt32emu-2.dll";
}
std::filesystem::path findMt32Roms(const std::filesystem::path& root) {
    const auto override = envPath(L"MW_MT32_ROMS");
    if (!override.empty()) return override;
    const auto local = root / L"mt32 roms";
    return std::filesystem::exists(local) ? local : root.parent_path() / L"mt32 roms";
}
}
