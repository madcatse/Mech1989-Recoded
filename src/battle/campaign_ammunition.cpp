#include "battle/campaign_ammunition.h"

#include <algorithm>
#include <stdexcept>

namespace mw::battle {
namespace {

uint16_t readU16Le(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 1u >= data.size()) {
        throw std::runtime_error(".GAM ammunition read exceeds save data");
    }
    return static_cast<uint16_t>(
        static_cast<uint16_t>(data[offset]) |
        (static_cast<uint16_t>(data[offset + 1u]) << 8u));
}

void writeU16Le(std::vector<uint8_t>& data, size_t offset, uint16_t value) {
    if (offset + 1u >= data.size()) {
        throw std::runtime_error(".GAM ammunition write exceeds save data");
    }
    data[offset] = static_cast<uint8_t>(value & 0xffu);
    data[offset + 1u] = static_cast<uint8_t>((value >> 8u) & 0xffu);
}

} // namespace

size_t originalGamOwnedMechAmmunitionOffset(
    size_t presentPoolOrdinal,
    size_t ownedMechSlot) {
    if (presentPoolOrdinal >= kOriginalGamOwnedMechAmmunitionPlaneCount ||
        ownedMechSlot >= kOriginalGamOwnedMechSlotCount) {
        throw std::runtime_error(".GAM ammunition index is out of range");
    }
    return kOriginalGamOwnedMechAmmunitionBase +
           presentPoolOrdinal *
               kOriginalGamOwnedMechAmmunitionPlaneStride +
           ownedMechSlot * 2u;
}

std::array<int, 6> decodeOriginalGamOwnedMechAmmunition(
    const std::vector<uint8_t>& data,
    size_t ownedMechSlot,
    const std::array<int, 6>& capacityByPool) {
    std::array<int, 6> result{};
    size_t ordinal = 0;
    for (size_t pool = 0; pool < capacityByPool.size(); ++pool) {
        if (capacityByPool[pool] <= 0) {
            continue;
        }
        const int saved = static_cast<int>(readU16Le(
            data,
            originalGamOwnedMechAmmunitionOffset(ordinal, ownedMechSlot)));
        result[pool] = std::clamp(saved, 0, capacityByPool[pool]);
        ++ordinal;
    }
    return result;
}

void encodeOriginalGamOwnedMechAmmunition(
    std::vector<uint8_t>& data,
    size_t ownedMechSlot,
    const std::array<int, 6>& capacityByPool,
    const std::array<int, 6>& ammunitionByPool) {
    for (size_t ordinal = 0;
         ordinal < kOriginalGamOwnedMechAmmunitionPlaneCount;
         ++ordinal) {
        writeU16Le(
            data,
            originalGamOwnedMechAmmunitionOffset(ordinal, ownedMechSlot),
            0u);
    }

    size_t ordinal = 0;
    for (size_t pool = 0; pool < capacityByPool.size(); ++pool) {
        if (capacityByPool[pool] <= 0) {
            continue;
        }
        writeU16Le(
            data,
            originalGamOwnedMechAmmunitionOffset(ordinal, ownedMechSlot),
            static_cast<uint16_t>(std::clamp(
                ammunitionByPool[pool], 0, capacityByPool[pool])));
        ++ordinal;
    }
}

} // namespace mw::battle
