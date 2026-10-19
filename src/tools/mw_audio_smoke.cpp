#include "audio/original_music.h"
#include "audio/mt32_synth.h"
#include "audio/music_player.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <cstdlib>

using namespace mw::audio;
namespace fs = std::filesystem;
namespace {
size_t checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
std::vector<uint8_t> read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Missing fixture: " + path.string());
    return {std::istreambuf_iterator<char>(in), {}};
}
uint16_t u16(const std::vector<uint8_t>& b, size_t p) { return b[p] | (b[p+1] << 8); }
void le(std::ostream& out, uint32_t value, int count) {
    for (int i = 0; i < count; ++i) out.put(static_cast<char>((value >> (i*8)) & 255));
}
void render(const char* name, const fs::path& output) {
    // Munt TVP deliberately uses the thread-local CRT rand() for MCU jitter.
    // Equal seeds are a test control, never applied by the live application.
    std::srand(1);
    Mt32Synth synth;
    synth.open(findMt32Library(fs::absolute("build/Release")), findMt32Roms(fs::absolute("Original")));
    std::cout << synth.description() << '\n';
    const auto sequence = loadOriginalMusic(fs::path("Original") / (std::string(name) + ".MUS"));
    MusicRenderer renderer;
    renderer.start(&sequence);
    // Preserve releases/reverb after the original EOT All Notes Off.
    const auto total = static_cast<uint32_t>(musicSample(sequence.endTimeUnits, kAudioSampleRate) + 3*kAudioSampleRate);
    fs::create_directories(output);
    std::ofstream out(output / (std::string(name) + ".wav"), std::ios::binary);
    if (!out) throw std::runtime_error("Cannot write audio evidence");
    out.write("RIFF", 4); le(out, 36 + total*4, 4); out.write("WAVEfmt ", 8);
    le(out, 16, 4); le(out, 1, 2); le(out, 2, 2); le(out, kAudioSampleRate, 4);
    le(out, kAudioSampleRate*4, 4); le(out, 4, 2); le(out, 16, 2);
    out.write("data", 4); le(out, total*4, 4);
    std::array<int16_t, 2048> samples{};
    uint32_t done = 0, nonzero = 0, clipped = 0;
    int peak = 0;
    double energy = 0;
    while (done < total) {
        const auto frames = std::min<uint32_t>(1024, total - done);
        renderer.render(synth, samples.data(), frames);
        for (uint32_t i = 0; i < frames*2; ++i) {
            const int value = samples[i];
            nonzero += value != 0;
            clipped += value == 32767 || value == -32768;
            peak = std::max(peak, std::abs(value));
            energy += static_cast<double>(value)*value;
        }
        out.write(reinterpret_cast<const char*>(samples.data()), frames*4);
        done += frames;
    }
    require(out.good(), "Audio output write failed");
    out.close();
    require(nonzero > total / 2, "MT-32 render is silent");
    require(peak > 100, "MT-32 render too quiet");
    // Compare fresh offline/live contexts with identical Munt jitter history.
    std::srand(1);
    Mt32Synth live;
    live.open(findMt32Library(fs::absolute("build/Release")), findMt32Roms(fs::absolute("Original")));
    MusicRenderer liveRenderer;
    liveRenderer.start(&sequence);
    std::ifstream reference(output / (std::string(name) + ".wav"), std::ios::binary);
    reference.seekg(44);
    std::array<int16_t, 960> liveSamples{}, expected{};
    int maxDifference = 0;
    double differenceEnergy = 0;
    done = 0;
    while (done < total) {
        const auto frames = std::min<uint32_t>(480, total - done);
        liveRenderer.render(live, liveSamples.data(), frames);
        reference.read(reinterpret_cast<char*>(expected.data()), frames*4);
        require(reference.good(), "Cannot read offline audio reference");
        for (uint32_t i = 0; i < frames*2; ++i) {
            const int delta = static_cast<int>(liveSamples[i]) - expected[i];
            maxDifference = std::max(maxDifference, std::abs(delta));
            differenceEnergy += static_cast<double>(delta)*delta;
        }
        done += frames;
    }
    std::cout << name << ": frames=" << total << " peak=" << peak << " clipped=" << clipped
        << " rms=" << std::sqrt(energy / (total*2)) << " block_max_delta=" << maxDifference
        << " block_rms_delta=" << std::sqrt(differenceEnergy/(total*2)) << '\n';
    require(maxDifference == 0, "Live/offline MT-32 samples differ with equal jitter seed");
}
}

int main(int argc, char** argv) {
    try {
        const std::array<const char*, 5> names{"DEATH", "LAUNCH", "MECH", "MECHBAR", "VICTORY"};
        const std::array<uint32_t, 5> callbacks{2742, 887, 6072, 4486, 4166};
        const std::array<uint64_t, 5> standardTime{3615710670ull, 1168500000ull, 8005629150ull, 5914080000ull, 5492947707ull};
        for (size_t n = 0; n < names.size(); ++n) {
            const auto data = read(fs::path("Original") / (std::string(names[n]) + ".MUS"));
            const auto sequence = readOriginalMusic(data);
            const auto trace = read(fs::path("research/fixtures/dosbox_audio") / (std::string(names[n]) + ".bin"));
            require(trace.size() == sequence.bytes.size()*4, "DOS MIDI byte count differs");
            require(sequence.endCallback == callbacks[n], "DOS MIDI end time differs");
            require(sequence.endTimeUnits == standardTime[n], "Standard MIDI tempo/duration differs");
            for (size_t j = 0; j < sequence.bytes.size(); ++j) {
                require(sequence.bytes[j].callback == u16(trace, j*4), "DOS MIDI callback differs");
                require(sequence.bytes[j].value == u16(trace, j*4+2), "DOS MIDI byte differs");
            }
            for (size_t cut : {size_t(0), size_t(21), data.size()-1}) {
                bool failed = false;
                try { readOriginalMusic({data.begin(), data.begin()+cut}); }
                catch (const std::runtime_error&) { failed = true; }
                require(failed, "Truncated MUS accepted");
            }
            std::cout << names[n] << ": " << sequence.bytes.size() << " DOS records exact; callback " << sequence.endCallback << '\n';
        }
        if (argc > 1 && std::string(argv[1]) == "--render")
            for (const auto name : names) render(name, "outputs/phase17_audio/rendered");
        if (argc > 1 && std::string(argv[1]) == "--device") {
            MusicPlayer player;
            require(player.initialize(fs::absolute("Original"), fs::absolute("build/Release")), player.status().c_str());
            player.play(MusicTrack::Theme);
            std::this_thread::sleep_for(std::chrono::milliseconds(1200));
            player.play(MusicTrack::Bar);
            std::this_thread::sleep_for(std::chrono::milliseconds(1200));
            player.play(MusicTrack::None);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            require(player.status().find("Munt ") == 0, "Audio worker failed");
            std::cout << "Output device, track switch, stop and RAII shutdown: " << player.status() << '\n';
        }
        std::cout << "mw_audio_smoke: ok " << checks << " checks\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "mw_audio_smoke: " << e.what() << '\n'; return 1; }
}
