#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mw::battle {

enum class BtechOppositionEstimateClass : uint8_t {
    Heavy,
    Medium,
    Light,
};

const char* btechOppositionEstimateClassName(BtechOppositionEstimateClass value);

struct BtechMechTypeDefinition {
    uint8_t typeId = 0;
    BtechOppositionEstimateClass estimatedClass = BtechOppositionEstimateClass::Light;
    std::string_view displayName;
    std::string_view mechPresetId;
};

const std::array<BtechMechTypeDefinition, 8>& btechMechTypeDefinitions();
std::optional<BtechMechTypeDefinition> btechMechTypeDefinitionById(uint8_t typeId);
std::optional<BtechMechTypeDefinition> btechMechTypeDefinitionByPreset(std::string_view mechPresetId);
std::vector<std::string> btechMechPresetIdsForTypeRange(uint8_t minTypeId, uint8_t maxTypeId);

struct BtechOppositionSpawnRequest {
    size_t spawnOrdinal = 0;
    uint8_t countBucketIndex = 0;
    uint8_t sourceContextOffset = 3;
    BtechOppositionEstimateClass estimatedClass = BtechOppositionEstimateClass::Heavy;
    uint8_t candidateTypeMin = 0;
    uint8_t candidateTypeMax = 0;
    std::vector<std::string> candidateMechPresetIds;
    std::optional<uint8_t> selectedTypeId;
    std::string selectedMechPresetId;
    uint8_t runtimeObjectSlot = 4;
    uint8_t opposingPlacementSlot = 0;
};

struct BattleOppositionSpawnPlan {
    bool valid = false;
    std::string provenance;
    std::array<uint8_t, 3> sourceCounts{};
    std::array<uint8_t, 3> remainingCounts{};
    size_t objectLimit = 4;
    bool freshBattleOnly = true;
    bool countBucketSemanticsProven = false;
    bool candidateTypeMappingProven = false;
    bool selectedTypeMappingProven = false;
    bool opposingSlotMappingProven = false;
    uint32_t deterministicSelectionSeed = 0;
    bool combatantsSpawned = false;
    std::vector<BtechOppositionSpawnRequest> requests;
};

BattleOppositionSpawnPlan decodeFreshBtechOppositionSpawnPlan(
    std::array<uint8_t, 3> sourceCounts,
    std::string provenance = "btech_context_3_5_diagnostic");

// MW_MAIN.EXE DS:4B55 is the ten-entry contract strength table.  The table is
// ordered like the campaign chassis catalog (Locust through BattleMaster).
uint8_t mwMainContractMechStrength(size_t campaignChassisIndex);
uint8_t mwMainContractReputationStrength(uint16_t reputationPoints);
int mwMainContractForceScore(
    uint16_t reputationPoints,
    uint16_t assignedMechStrengthTotal);
std::array<uint8_t, 3> mwMainContractOppositionCounts(
    int adjustedForceScore,
    uint8_t heavyMixRoll);

std::array<uint8_t, 3> btechExtendedStageOppositionCounts(
    std::array<uint8_t, 3> sourceCounts,
    size_t stageIndex);

BattleOppositionSpawnPlan resolveBtechOppositionSpawnPlan(
    std::array<uint8_t, 3> sourceCounts,
    uint32_t deterministicSelectionSeed,
    std::string provenance = "btech_context_3_5_runtime");

struct BattleOppositionRosterEntry {
    uint8_t btechTypeId = 0;
    std::string mechPresetId;
    size_t count = 0;
};

struct BattleOppositionRosterSlot {
    size_t spawnOrdinal = 0;
    uint8_t btechTypeId = 0;
    std::string mechPresetId;
    uint8_t runtimeObjectSlot = 4;
    uint8_t opposingPlacementSlot = 0;
};

struct BattleOppositionRosterMetadata {
    bool valid = false;
    std::string provenance;
    std::optional<uint8_t> sourceContextMissionByte;
    std::optional<size_t> initialMissionSelector;
    bool compositionProven = false;
    bool spawnOrderProven = false;
    bool placementProven = false;
    bool combatantsSpawned = false;
    std::vector<BattleOppositionRosterEntry> entries;
    std::vector<BattleOppositionRosterSlot> orderedSlots;
};

BattleOppositionRosterMetadata originalDarkWingFinalOppositionRoster();
BattleOppositionRosterMetadata btechOppositionRosterFromSpawnPlan(
    const BattleOppositionSpawnPlan& plan,
    std::string provenance = "original_btech_contract_opposition");

} // namespace mw::battle
