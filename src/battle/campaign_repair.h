#pragma once
#include "battle/campaign_contracts.h"
#include <array>
#include <cstdint>
#include <vector>
namespace mw::battle {
// MW_MAIN DS:3B53, 3C1B, 3CAB and 2AED; prices are WORDs, armor BYTEs.
struct OriginalRepairTables {
    std::array<std::array<uint16_t, 10>, 10> components{};
    std::array<std::array<uint16_t, 10>, 10> weapons{};
    std::array<uint8_t, 10> armor{}, weaponCounts{};
    std::array<uint8_t, 4> availabilityThresholds{};
};
using OriginalMechCondition = std::array<uint8_t, 29>;
// Unsaved DOS DS:3A77 cache, stride 22 by chassis (not by owned-mech slot).
struct OriginalRepairSession {
    std::array<uint8_t, 220> unavailable{};
    OriginalContractRng rng;
};
// Non-armor record field 0..19. Selected tests !=0; full tests ==1.
// An uncached attempt consumes exactly one inclusive R(100) before funds.
bool originalRepairAvailable(const OriginalRepairTables& tables, OriginalRepairSession& session,
    uint16_t chassis, uint8_t economyTier, size_t field, bool fullRepair = false);
// Caller must prepay the complete quote. Returns whether any fields failed;
// caller refunds the remaining quote. Armor never draws availability.
bool applyOriginalFullRepair(const OriginalRepairTables& tables, OriginalRepairSession& session,
    uint16_t chassis, uint8_t economyTier, OriginalMechCondition& condition);

OriginalRepairTables decodeOriginalRepairTables(const std::vector<uint8_t>& exe);
// UI selector: components 0..9, armor 10, weapons 11..20.
uint32_t originalRepairUnitCost(const OriginalRepairTables& tables, uint16_t chassis,
    uint16_t serviceFactor, size_t selector, const OriginalMechCondition& condition);
uint32_t originalRepairTotalCost(const OriginalRepairTables& tables, uint16_t chassis,
    uint16_t serviceFactor, const OriginalMechCondition& condition);
// Actual 3FC5 -> 0C3E argument is AX, not the unused CX time-table load.
// Includes the original high-byte carry from slot*29 for non-armor slots 9..11.
uint16_t originalRepairStepTicks(size_t ownedSlot, size_t selector,
    const OriginalMechCondition& condition);
// Successful paid mutation only; availability, time and payment are separate.
bool applyOriginalRepairStep(OriginalMechCondition& condition, size_t selector);
uint16_t generateOriginalServiceFactor(uint8_t economyTier, OriginalContractRng& rng);
} // namespace mw::battle
