#pragma once
#include "battle/campaign_market.h"
#include "battle/battle_world.h"

namespace mw::battle {
struct OriginalSalvageRecord {
    uint16_t chassis = 0xffff;
    uint16_t condition = 0;
};
using OriginalSalvageRecords = std::array<OriginalSalvageRecord,4>;
inline OriginalSalvageRecords originalSimulatorEmptySalvage() {
    // BTECH2C83..2C9B initializes all16 bytes, including empty conditions.
    // MW_MAIN's no-battle return initializes chassis only; keep {} distinct.
    OriginalSalvageRecords records;
    for (auto& record:records) record={0xffff,0xffff};
    return records;
}
struct OriginalSalvageCandidate {
    bool occupied = false;
    int16_t aggregate = 0; // BTECH control +16, AFTER boundary override.
    OriginalSalvageRecord record;
};
struct OriginalSalvageTables {
    OriginalMarketTables market;
    std::array<uint16_t,10> scrapTons{};
    std::array<uint16_t,5> employerThresholds{}, employerPercents{};
};
OriginalSalvageTables decodeOriginalSalvageTables(const std::vector<uint8_t>& exe);
// BTECH 3D70, valid original runtime domain. Each division truncates separately.
uint16_t originalSalvageCondition(int engine, int sensors, int cockpit, int gyro,
    int workingWeapons, int installedWeapons, int remainingPoints, int maximumPoints);
// BTECH 3B35; existing records survive subsequent successful stages, cap four.
void appendOriginalSalvage(OriginalSalvageRecords& records,
    const std::array<OriginalSalvageCandidate,4>& candidates, int result, int playerMode);
uint16_t originalEmployerPayoutPercent(const OriginalSalvageTables& tables,
    uint8_t house, uint16_t roll, bool finalMission = false);
uint64_t originalSalvageAward(const OriginalSalvageTables& tables,
    const OriginalSalvageRecords& records, uint8_t economyTier,
    uint16_t salvagePercent, uint16_t employerPercent, int result, bool finalMission = false,
    bool correctAccumulatorBug = false);
// correctAccumulatorBug=true is the production policy requested by the user:
// 32EB clobbers caller DI, losing earlier trophies and adding the economy index.
// false preserves that bug ONLY for exact original-instruction comparisons.
// Runtime adapter: exact damage, original slot order, unconditional failures
// and actor-local prone/actuator loss (intentional shared-pointer bug fix).
// Escaped actors are excluded; generic Destroyed is not a source predicate.
void appendCampaignSalvage(OriginalSalvageRecords& records,
    const BattleSnapshot& snapshot, bool victory);
uint8_t campaignDefeatedOpponents(const BattleSnapshot& snapshot);
} // namespace mw::battle
