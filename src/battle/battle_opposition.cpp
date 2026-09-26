#include "battle/battle_opposition.h"

#include <algorithm>
#include <array>
#include <utility>

namespace mw::battle {
namespace {

struct BtechBucketTypeRange {
    BtechOppositionEstimateClass estimatedClass = BtechOppositionEstimateClass::Heavy;
    uint8_t min = 0;
    uint8_t max = 0;
};

constexpr std::array<BtechBucketTypeRange, 3> kBtechBucketTypeRanges{{
    {BtechOppositionEstimateClass::Heavy, 4, 7},
    {BtechOppositionEstimateClass::Medium, 2, 3},
    {BtechOppositionEstimateClass::Light, 0, 1},
}};

constexpr std::array<BtechMechTypeDefinition, 8> kBtechMechTypes{{
    {0, BtechOppositionEstimateClass::Light, "LOCUST", "locust"},
    {1, BtechOppositionEstimateClass::Light, "JENNER", "jenner"},
    {2, BtechOppositionEstimateClass::Medium, "PHOENIX HAWK", "phoenix_hawk"},
    {3, BtechOppositionEstimateClass::Medium, "SHADOW HAWK", "shadow_hawk"},
    {4, BtechOppositionEstimateClass::Heavy, "RIFLEMAN", "rifleman"},
    {5, BtechOppositionEstimateClass::Heavy, "WARHAMMER", "warhammer"},
    {6, BtechOppositionEstimateClass::Heavy, "MARAUDER", "marauder"},
    {7, BtechOppositionEstimateClass::Heavy, "BATTLEMASTER", "battlemaster"},
}};

// Original MW_MAIN.EXE bytes at DS:4B55.  Wasp and Wolverine exist in the
// campaign roster even though BTECH does not generate them as opponents.
constexpr std::array<uint8_t, 10> kMwMainContractMechStrengths{{
    1, 1, 1, 2, 2, 2, 3, 3, 3, 3,
}};

uint32_t nextDeterministicSelectionValue(uint32_t& state) {
    // The original BTECH selection consumes FUN_1000_cb73(0x80) and then only
    // observes bits 2 and 3.  The transferred DOS PRNG state is not part of the
    // reconstructed campaign save, so the runtime contract seed supplies a
    // stable state while preserving the original bit masks below.
    if (state == 0u) {
        state = 0x6d2b79f5u;
    }
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return state;
}

} // namespace

const char* btechOppositionEstimateClassName(BtechOppositionEstimateClass value) {
    switch (value) {
    case BtechOppositionEstimateClass::Heavy:
        return "heavy";
    case BtechOppositionEstimateClass::Medium:
        return "medium";
    case BtechOppositionEstimateClass::Light:
        return "light";
    }
    return "unknown";
}

const std::array<BtechMechTypeDefinition, 8>& btechMechTypeDefinitions() {
    return kBtechMechTypes;
}

std::optional<BtechMechTypeDefinition> btechMechTypeDefinitionById(uint8_t typeId) {
    if (typeId >= kBtechMechTypes.size()) {
        return std::nullopt;
    }
    return kBtechMechTypes[typeId];
}

std::optional<BtechMechTypeDefinition> btechMechTypeDefinitionByPreset(std::string_view mechPresetId) {
    const auto found = std::find_if(
        kBtechMechTypes.begin(),
        kBtechMechTypes.end(),
        [&](const BtechMechTypeDefinition& definition) {
            return definition.mechPresetId == mechPresetId;
        });
    if (found == kBtechMechTypes.end()) {
        return std::nullopt;
    }
    return *found;
}

std::vector<std::string> btechMechPresetIdsForTypeRange(uint8_t minTypeId, uint8_t maxTypeId) {
    std::vector<std::string> presets;
    if (minTypeId > maxTypeId) {
        return presets;
    }
    for (uint16_t typeId = minTypeId; typeId <= maxTypeId; ++typeId) {
        const std::optional<BtechMechTypeDefinition> definition =
            btechMechTypeDefinitionById(static_cast<uint8_t>(typeId));
        if (definition) {
            presets.emplace_back(definition->mechPresetId);
        }
    }
    return presets;
}

BattleOppositionSpawnPlan decodeFreshBtechOppositionSpawnPlan(
    std::array<uint8_t, 3> sourceCounts,
    std::string provenance) {
    BattleOppositionSpawnPlan plan;
    plan.valid = true;
    plan.provenance = std::move(provenance);
    plan.sourceCounts = sourceCounts;
    plan.remainingCounts = sourceCounts;
    plan.countBucketSemanticsProven = true;
    plan.candidateTypeMappingProven = true;
    plan.opposingSlotMappingProven = true;
    plan.requests.reserve(plan.objectLimit);

    bool anyRemaining = true;
    while (plan.requests.size() < plan.objectLimit && anyRemaining) {
        anyRemaining = false;
        for (size_t bucket = 0; bucket < plan.remainingCounts.size(); ++bucket) {
            if (plan.remainingCounts[bucket] == 0) {
                continue;
            }
            anyRemaining = true;
            --plan.remainingCounts[bucket];
            const BtechBucketTypeRange range = kBtechBucketTypeRanges[bucket];
            BtechOppositionSpawnRequest request;
            request.spawnOrdinal = plan.requests.size();
            request.countBucketIndex = static_cast<uint8_t>(bucket);
            request.sourceContextOffset = static_cast<uint8_t>(3 + bucket);
            request.estimatedClass = range.estimatedClass;
            request.candidateTypeMin = range.min;
            request.candidateTypeMax = range.max;
            request.candidateMechPresetIds = btechMechPresetIdsForTypeRange(range.min, range.max);
            request.runtimeObjectSlot = static_cast<uint8_t>(4 + plan.requests.size());
            request.opposingPlacementSlot = static_cast<uint8_t>(plan.requests.size());
            plan.requests.push_back(std::move(request));
            if (plan.requests.size() == plan.objectLimit) {
                break;
            }
        }
    }
    return plan;
}

uint8_t mwMainContractMechStrength(size_t campaignChassisIndex) {
    if (campaignChassisIndex >= kMwMainContractMechStrengths.size()) {
        return 0;
    }
    return kMwMainContractMechStrengths[campaignChassisIndex];
}

uint8_t mwMainContractReputationStrength(uint16_t reputationPoints) {
    if (reputationPoints > 35u) {
        return 3u;
    }
    if (reputationPoints > 20u) {
        return 2u;
    }
    if (reputationPoints > 8u) {
        return 1u;
    }
    return 0u;
}

int mwMainContractForceScore(
    uint16_t reputationPoints,
    uint16_t assignedMechStrengthTotal) {
    return static_cast<int>(
        mwMainContractReputationStrength(reputationPoints) +
        assignedMechStrengthTotal) * 10;
}

std::array<uint8_t, 3> mwMainContractOppositionCounts(
    int adjustedForceScore,
    uint8_t heavyMixRoll) {
    const int score = std::max(0, adjustedForceScore);
    std::array<uint8_t, 3> counts{{
        static_cast<uint8_t>(score / 25),
        static_cast<uint8_t>((score % 25) / 15),
        static_cast<uint8_t>(((score % 25) % 15) / 7),
    }};

    int total = static_cast<int>(counts[0]) +
        static_cast<int>(counts[1]) + static_cast<int>(counts[2]);
    while (total >= 5) {
        // MW_MAIN removes the cheapest bucket first.
        if (counts[2] > 0u) {
            --counts[2];
        } else if (counts[1] > 0u) {
            --counts[1];
        } else if (counts[0] > 0u) {
            --counts[0];
        }
        --total;
    }
    if (total < 1) {
        counts[2] = 1u;
    }

    // FUN_101b_0c17 is called with AX=10 here, hence an inclusive 0..10 roll.
    // Values 0..4 retain four heavies, 5..7 make 3H+1M, and 8..10 make 2H+2M.
    const uint8_t roll = static_cast<uint8_t>(heavyMixRoll % 11u);
    if (counts[0] == 4u && roll > 4u) {
        counts[0] = 3u;
        counts[1] = 1u;
        if (roll > 7u) {
            counts[0] = 2u;
            counts[1] = 2u;
        }
    }
    return counts;
}

std::array<uint8_t, 3> btechExtendedStageOppositionCounts(
    std::array<uint8_t, 3> sourceCounts,
    size_t stageIndex) {
    const int chainIndex = static_cast<int>(std::min<size_t>(stageIndex, 2u));
    for (uint8_t& count : sourceCounts) {
        const int adjusted = std::max(0, static_cast<int>(count) - chainIndex);
        count = static_cast<uint8_t>((adjusted + 2) / 3);
    }
    return sourceCounts;
}

BattleOppositionSpawnPlan resolveBtechOppositionSpawnPlan(
    std::array<uint8_t, 3> sourceCounts,
    uint32_t deterministicSelectionSeed,
    std::string provenance) {
    BattleOppositionSpawnPlan plan = decodeFreshBtechOppositionSpawnPlan(
        sourceCounts, std::move(provenance));
    plan.selectedTypeMappingProven = true;
    plan.deterministicSelectionSeed = deterministicSelectionSeed;
    uint32_t state = deterministicSelectionSeed;
    for (BtechOppositionSpawnRequest& request : plan.requests) {
        const uint32_t randomValue = nextDeterministicSelectionValue(state);
        uint8_t typeId = 0;
        switch (request.estimatedClass) {
        case BtechOppositionEstimateClass::Heavy:
            typeId = static_cast<uint8_t>(((randomValue & 0x0cu) >> 2u) + 4u);
            break;
        case BtechOppositionEstimateClass::Medium:
            typeId = static_cast<uint8_t>(((randomValue & 0x04u) >> 2u) + 2u);
            break;
        case BtechOppositionEstimateClass::Light:
            typeId = static_cast<uint8_t>((randomValue & 0x04u) >> 2u);
            break;
        }
        const std::optional<BtechMechTypeDefinition> definition =
            btechMechTypeDefinitionById(typeId);
        if (definition) {
            request.selectedTypeId = typeId;
            request.selectedMechPresetId = std::string(definition->mechPresetId);
        }
    }
    return plan;
}

BattleOppositionRosterMetadata btechOppositionRosterFromSpawnPlan(
    const BattleOppositionSpawnPlan& plan,
    std::string provenance) {
    BattleOppositionRosterMetadata roster;
    if (!plan.valid || !plan.selectedTypeMappingProven) {
        return roster;
    }
    roster.valid = true;
    roster.provenance = std::move(provenance);
    roster.compositionProven = true;
    roster.spawnOrderProven = true;
    roster.placementProven = true;
    std::array<size_t, 8> counts{};
    for (const BtechOppositionSpawnRequest& request : plan.requests) {
        if (!request.selectedTypeId || request.selectedMechPresetId.empty()) {
            return {};
        }
        ++counts[*request.selectedTypeId];
        roster.orderedSlots.push_back({
            request.spawnOrdinal,
            *request.selectedTypeId,
            request.selectedMechPresetId,
            request.runtimeObjectSlot,
            request.opposingPlacementSlot,
        });
    }
    for (size_t typeId = 0; typeId < counts.size(); ++typeId) {
        if (counts[typeId] == 0u) {
            continue;
        }
        const std::optional<BtechMechTypeDefinition> definition =
            btechMechTypeDefinitionById(static_cast<uint8_t>(typeId));
        if (!definition) {
            return {};
        }
        roster.entries.push_back({
            static_cast<uint8_t>(typeId),
            std::string(definition->mechPresetId),
            counts[typeId],
        });
    }
    return roster;
}

BattleOppositionRosterMetadata originalDarkWingFinalOppositionRoster() {
    BattleOppositionRosterMetadata roster;
    roster.valid = true;
    roster.provenance = "original_btech_special_context_63";
    roster.sourceContextMissionByte = static_cast<uint8_t>(0x63);
    roster.initialMissionSelector = 12;
    roster.compositionProven = true;
    roster.spawnOrderProven = true;
    roster.placementProven = true;
    roster.combatantsSpawned = false;
    roster.entries = {
        {7, std::string(kBtechMechTypes[7].mechPresetId), 3},
        {5, std::string(kBtechMechTypes[5].mechPresetId), 1},
    };
    roster.orderedSlots = {
        {0, 7, std::string(kBtechMechTypes[7].mechPresetId), 4, 0},
        {1, 7, std::string(kBtechMechTypes[7].mechPresetId), 5, 1},
        {2, 7, std::string(kBtechMechTypes[7].mechPresetId), 6, 2},
        {3, 5, std::string(kBtechMechTypes[5].mechPresetId), 7, 3},
    };
    return roster;
}

} // namespace mw::battle
