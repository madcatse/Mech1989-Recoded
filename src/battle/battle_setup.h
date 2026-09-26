#pragma once

#include "battle/battle_placement.h"
#include "legacy3d/terrain_parser.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace mw::battle {

struct OriginalBattlefieldSetup {
    size_t scenarioIndex = 0;
    size_t missionSelector = 0;
    uint8_t terrainMode = 0;
    legacy3d::TerrainScenarioRecord terrainScenario{};
    std::vector<std::string> tileNames;
    OriginalBattlePlacement placement{};
    Transform playerStartTransform{};
    Transform enemyStartTransform{};
    Transform targetTransform{};
    BattleSetupMetadata metadata{};
    BattleObjectiveState objective{};
    BattlefieldBoundaryState battlefieldBoundary{};
};

OriginalBattlefieldSetup decodeOriginalBattlefieldSetup(
    const std::filesystem::path& snarioPath,
    size_t scenarioIndex,
    size_t missionSelector,
    double terrainCellSize);

OriginalBattlefieldSetup decodeOriginalDarkWingFinalBattlefieldSetup(
    const std::filesystem::path& snarioPath,
    size_t scenarioIndex,
    double terrainCellSize);

void applyOriginalDarkWingFinalOpposition(
    BattleStartParams& params,
    const OriginalBattlefieldSetup& setup,
    std::optional<uint8_t> factionHouseId = std::nullopt,
    std::string factionHouseName = {});

void applyOriginalContractOpposition(
    BattleStartParams& params,
    const OriginalBattlefieldSetup& setup,
    BattleOppositionSpawnPlan spawnPlan,
    std::optional<uint8_t> factionHouseId = std::nullopt,
    std::string factionHouseName = {});

} // namespace mw::battle
