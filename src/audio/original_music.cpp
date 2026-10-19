#include "audio/original_music.h"

#include <fstream>
#include <iterator>
#include <stdexcept>

namespace mw::audio {
namespace {
struct Reader {
    const std::vector<uint8_t>& data;
    size_t pos = 0;
    uint8_t byte() {
        if (pos >= data.size()) throw std::runtime_error("Truncated MUS resource");
        return data[pos++];
    }
    uint32_t be(unsigned count) {
        uint32_t value = 0;
        while (count--) value = (value << 8) | byte();
        return value;
    }
    uint32_t vlq() {
        uint32_t value = 0;
        for (int n = 0; n < 4; ++n) {
            const auto b = byte();
            value = (value << 7) | (b & 127);
            if (b < 128) return value;
        }
        throw std::runtime_error("Oversized MUS delta/length");
    }
};
struct Event {
    uint32_t delta = 0;
    uint32_t tempo = 0;
    bool end = false;
    uint64_t timeUnits = 0;
    std::vector<uint8_t> bytes;
};
}

uint64_t callbackSample(uint32_t callback, uint32_t sampleRate) {
    return static_cast<uint64_t>(callback) * kMusicCallbackClocks *
        kPitClockDenominator * sampleRate / kPitClockNumerator;
}
uint64_t musicSample(uint64_t timeUnits, uint32_t sampleRate) {
    // Quotient/remainder form avoids an unnecessary large intermediate.
    constexpr uint64_t unitsPerSecond = 96000000;
    return timeUnits / unitsPerSecond * sampleRate + timeUnits % unitsPerSecond * sampleRate / unitsPerSecond;
}

MusicSequence loadOriginalMusic(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open MUS resource: " + path.string());
    return readOriginalMusic({std::istreambuf_iterator<char>(input), {}});
}

MusicSequence readOriginalMusic(const std::vector<uint8_t>& data) {
    Reader r{data};
    if (r.be(4) != 0x4d546864 || r.be(4) != 6 || r.be(2) != 0 ||
        r.be(2) != 1 || r.be(2) != 96 || r.be(4) != 0x4d54726b)
        throw std::runtime_error("Unsupported MUS header");
    const auto length = r.be(4);
    if (length != data.size() - r.pos) throw std::runtime_error("MUS track length mismatch");
    std::vector<Event> events;
    uint8_t running = 0;
    bool ended = false;
    uint64_t elapsed = 0;
    uint32_t smfTempo = 500000;
    while (r.pos < data.size()) {
        Event e;
        e.delta = r.vlq();
        elapsed += static_cast<uint64_t>(e.delta) * smfTempo;
        e.timeUnits = elapsed;
        // The original signed-word countdown cannot represent longer delays.
        if (e.delta > 32767) throw std::runtime_error("MUS delay exceeds DOS clock range");
        auto status = r.byte();
        if (status < 128) {
            if (!running) throw std::runtime_error("MUS running status without a channel event");
            --r.pos;
            status = running;
        }
        if (status < 0xf0) {
            running = status;
            e.bytes.push_back(status);
            const int count = (status >> 4) == 12 || (status >> 4) == 13 ? 1 : 2;
            for (int n = 0; n < count; ++n) {
                const auto b = r.byte();
                if (b >= 128) throw std::runtime_error("Invalid MUS channel data");
                e.bytes.push_back(b);
            }
        } else if (status == 0xf0 || status == 0xf7) {
            running = 0;
            const auto size = r.vlq();
            if (size > data.size() - r.pos) throw std::runtime_error("Truncated MUS SysEx");
            if (status == 0xf0) e.bytes.push_back(status);
            for (uint32_t n = 0; n < size; ++n) e.bytes.push_back(r.byte());
        } else if (status == 0xff) {
            const auto type = r.byte();
            const auto size = r.vlq();
            if (size > data.size() - r.pos) throw std::runtime_error("Truncated MUS meta event");
            if (type == 0x51) {
                if (size != 3) throw std::runtime_error("Invalid MUS tempo length");
                e.tempo = r.be(3);
                if (!e.tempo || 60000000 / e.tempo > 682)
                    throw std::runtime_error("MUS tempo exceeds DOS clock range");
                smfTempo = e.tempo;
            } else {
                r.pos += size;
                if (type == 0x2f) {
                    if (size || r.pos != data.size()) throw std::runtime_error("Invalid MUS end marker");
                    e.end = ended = true;
                }
            }
            // Original 0741:042C keeps running status for F8..FF.
        } else if (status == 0xf8 || status == 0xfa || status == 0xfb ||
                   status == 0xfc || status == 0xfe) {
            e.bytes.push_back(status);
        } else {
            throw std::runtime_error("Unsupported MUS system event");
        }
        events.push_back(std::move(e));
    }
    if (!ended) throw std::runtime_error("MUS end marker missing");

    MusicSequence result;
    uint32_t remainder = 0, tempo96 = 11520, callback = 0;
    int32_t countdown = static_cast<int32_t>(events.front().delta);
    size_t next = 0;
    while (next < events.size()) {
        if (++callback > 1000000) throw std::runtime_error("MUS duration exceeds supported limit");
        const auto sum = (remainder + tempo96) & 65535u;
        countdown += static_cast<int32_t>(sum / 4368);
        remainder = sum % 4368;
        while (countdown >= 0 && next < events.size()) {
            const auto& e = events[next++];
            if (e.tempo) {
                tempo96 = (60000000 / e.tempo) * 96;
                remainder = 0;
            }
            for (auto b : e.bytes) result.bytes.push_back({callback, b, e.timeUnits});
            if (e.end) {
                for (int ch = 15; ch >= 0; --ch) {
                    result.bytes.push_back({callback, static_cast<uint8_t>(0xb0 + ch), e.timeUnits});
                    result.bytes.push_back({callback, 123, e.timeUnits});
                    result.bytes.push_back({callback, 0, e.timeUnits});
                }
            }
            if (next < events.size()) countdown -= static_cast<int32_t>(events[next].delta);
        }
    }
    result.endCallback = callback;
    result.endTimeUnits = elapsed;
    return result;
}
}
