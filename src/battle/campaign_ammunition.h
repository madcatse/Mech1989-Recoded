#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mw::battle {

constexpr size_t kOriginalGamOwnedMechAmmunitionBase = 0x025e;
constexpr size_t kOriginalGamOwnedMechAmmunitionPlaneStride = 0x18;
constexpr size_t kOriginalGamOwnedMechAmmunitionPlaneCount = 6;
constexpr size_t kOriginalGamOwnedMechSlotCount = 12;
constexpr size_t kOriginalGamExtraAmmunitionBase = 0x02ee;

static_assert(
    kOriginalGamOwnedMechAmmunitionBase +
            kOriginalGamOwnedMechAmmunitionPlaneCount *
                kOriginalGamOwnedMechAmmunitionPlaneStride ==
        kOriginalGamExtraAmmunitionBase,
    "owned-mech ammunition planes must end at hold inventory");

size_t originalGamOwnedMechAmmunitionOffset(
    size_t presentPoolOrdinal,
    size_t ownedMechSlot);

std::array<int, 6> decodeOriginalGamOwnedMechAmmunition(
    const std::vector<uint8_t>& data,
    size_t ownedMechSlot,
    const std::array<int, 6>& capacityByPool);

void encodeOriginalGamOwnedMechAmmunition(
    std::vector<uint8_t>& data,
    size_t ownedMechSlot,
    const std::array<int, 6>& capacityByPool,
    const std::array<int, 6>& ammunitionByPool);

} // namespace mw::battle
