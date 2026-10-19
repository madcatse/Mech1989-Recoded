#pragma once

#include "battle/campaign_contracts.h"
#include <array>
#include <cstdint>
#include <vector>

namespace mw::battle {

constexpr size_t kOriginalMarketSlots = 12;
struct OriginalMarketMech {
    uint16_t chassis = 0xffff;
    std::array<uint8_t, 29> condition{};
    std::array<uint16_t, 3> ammoTypes{};
    std::array<uint16_t, 3> ammunition{};
};
struct OriginalMechMarket {
    std::array<OriginalMarketMech, kOriginalMarketSlots> slots{};
    uint16_t saleQuota = 0;
    uint16_t sales = 0;
    size_t count() const;
};
struct OriginalMarketTables {
    std::array<uint8_t, 3> classMaximums{}, classStarts{};
    std::array<uint8_t, 10> weaponCounts{}, jumpJets{}, ammoCounts{};
    std::array<std::array<uint16_t, 3>, 10> ammoTypes{}, ammoCapacities{};
    std::array<uint8_t, 4> priceCoefficients{};
    std::array<uint16_t, 10> basePrices{};
};

OriginalMarketTables decodeOriginalMarketTables(const std::vector<uint8_t>& exe);
OriginalMarketMech generateOriginalStartingMech(const OriginalMarketTables&, OriginalContractRng&);
// MW_MAIN 3D09/3BF7; uses the shared 0C17 RNG implementation. Inactive slot
// ammo is retained, as in the executable. Unused scratch ammo starts at zero.
OriginalMechMarket generateOriginalMechMarket(const OriginalMarketTables& tables,
    uint8_t tier, OriginalContractRng& rng, OriginalMechMarket previous = {});
OriginalMechMarket decodeOriginalMechMarket(const std::vector<uint8_t>& gam);
void encodeOriginalMechMarket(std::vector<uint8_t>& gam, const OriginalMechMarket& market);
bool originalMarketAcceptsSale(const OriginalMechMarket& market, uint8_t tier);
uint32_t originalMarketPrice(const OriginalMarketTables& tables, uint16_t chassis,
    uint8_t tier, uint32_t repairCost, bool buying);

} // namespace mw::battle
