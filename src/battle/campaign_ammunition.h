#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mw::battle {

constexpr size_t kOriginalGamOwnedMechAmmunitionBase = 0x025e;
constexpr size_t kOriginalGamOwnedMechAmmunitionPlaneStride = 0x18;
constexpr size_t kOriginalGamOwnedMechAmmunitionPlaneCount = 3;
constexpr size_t kOriginalGamOwnedMechSlotCount = 12;
constexpr size_t kOriginalGamMarketAmmunitionBase = 0x02a6;
constexpr size_t kOriginalGamOwnedAmmoTypesBase = 0x02fa;
constexpr size_t kOriginalGamExtraAmmunitionBase = 0x02ee;

static_assert(
    kOriginalGamOwnedMechAmmunitionBase +
            kOriginalGamOwnedMechAmmunitionPlaneCount *
                kOriginalGamOwnedMechAmmunitionPlaneStride ==
        kOriginalGamMarketAmmunitionBase,
    "owned-mech ammunition must not overwrite market inventory");

size_t originalGamOwnedMechAmmunitionOffset(
    size_t presentPoolOrdinal,
    size_t ownedMechSlot);

std::array<int, 6> decodeOriginalGamOwnedMechAmmunition(
    const std::vector<uint8_t>& data,
    size_t ownedMechSlot,
    const std::array<int, 6>& capacityByPool);

// Same-chassis updates write active quantities but preserve all type WORDs
// and unused quantities, as original Save/exit do. New/replaced slots use
// the default full initialization.
void encodeOriginalGamOwnedMechAmmunition(
    std::vector<uint8_t>& data,
    size_t ownedMechSlot,
    const std::array<int, 6>& capacityByPool,
    const std::array<int, 6>& ammunitionByPool,
    bool preserveOriginalSlot = false);

} // namespace mw::battle
