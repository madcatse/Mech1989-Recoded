#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>

namespace mw::battle {
// BTECH actual CB56/CB7F/CBD6, DS281C/285C/285E. Separate from MW_MAIN.
// State is process RAM, never part of GAM or the exact-damage companion.
struct OriginalBtechRng {
    std::array<uint16_t,32> words{{15698,32561,32930,60694,17087,23617,3166,37414,
        42379,18477,25817,33453,57721,8523,33100,55094,38863,44721,63435,27291,
        3161,34100,29065,5523,27589,1964,15487,42046,55913,6174,39889,54655}};
    uint16_t first = 0, second = 15;
    void addSeed(uint16_t seed) {
        for (auto& word : words) word=static_cast<uint16_t>(word+seed);
    }
    uint16_t exclusive(uint16_t bound) {
        // Original bound0 loops forever; FFFF divides by zero. Reject corrupt
        // callers instead. Budget also prevents a corrupt all-zero state hang.
        if (!bound || bound==65535 || first>=32 || second>=32)
            throw std::invalid_argument("invalid BTECH RNG state or bound");
        for (unsigned retry=0;retry<1048576;++retry) {
            words[first]=static_cast<uint16_t>(words[first]+words[second]);
            const auto value=static_cast<uint16_t>((words[first]&0x7fff)%(bound+1u));
            first=static_cast<uint16_t>((first+1)&31);
            second=static_cast<uint16_t>((second+1)&31);
            if(value) return static_cast<uint16_t>(value-1);
        }
        throw std::runtime_error("BTECH RNG rejection budget exhausted");
    }
    int symmetric(uint16_t radius) {
        if(radius>16382) throw std::invalid_argument("invalid BTECH symmetric radius");
        return static_cast<int>(exclusive(static_cast<uint16_t>(2*radius+1)))-radius;
    }
};
inline bool operator==(const OriginalBtechRng& a,const OriginalBtechRng& b) {
    return a.words==b.words && a.first==b.first && a.second==b.second;
}
inline bool operator!=(const OriginalBtechRng& a,const OriginalBtechRng& b) { return !(a==b); }
} // namespace mw::battle
