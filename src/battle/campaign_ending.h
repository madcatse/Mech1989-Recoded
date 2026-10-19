#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace mw::battle {
// MW_MAIN 1CF0..1D01. Result 0 / context 99 bypass ordinary settlement.
constexpr bool originalDarkWingEnding(uint8_t result, uint8_t context) {
    return result == 0 && context == 99;
}
struct EndingSprite { int entry; int x; int y; };
struct EndingFrame { std::array<EndingSprite, 3> sprites{}; size_t count = 0; };
// MW_MAIN 2317..2418: five car-approach frames, then twenty pairs of
// trophy gestures (FPICS 7/8), synchronized with the waving crowd (FPICS 9).
constexpr EndingFrame originalDarkWingEndingFrame(size_t frame) {
    if (frame == 0) return {{{{9,0,59},{1,124,45},{}}},2};
    if (frame == 1) return {{{{2,109,38},{},{}}},1};
    if (frame == 2) return {{{{9,0,59},{3,94,31},{}}},2};
    if (frame == 3) return {{{{4,81,25},{},{}}},1};
    if (frame == 4) return {{{{9,0,59},{5,58,14},{}}},2};
    if (frame < 45 && (frame - 5) % 2 == 0)
        return {{{{6,39,62},{7,145,8},{}}},2};
    if (frame < 45) return {{{{9,0,59},{6,39,62},{8,145,8}}},3};
    return {};
}
}
