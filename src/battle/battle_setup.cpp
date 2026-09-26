#include "battle/battle_setup.h"

#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mw::battle {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr size_t kDarkWingFinalMissionSelector = 12;
constexpr size_t kMissionBriefingCount = 34;
constexpr int32_t kOriginalBoundaryMinX = -0xac80;
constexpr int32_t kOriginalBoundaryMaxX = 0xac80;
constexpr int32_t kOriginalBoundaryMinZ = -24000;
constexpr int32_t kOriginalBoundaryMaxZ = 24000;
constexpr double kOriginalGridCellSize = 512.0;

double oppositeHeading(double headingRadians) {
    return headingRadians + kPi;
}

double headingToward(const Transform& from, const Transform& to) {
    return std::atan2(to.x - from.x, to.z - from.z);
}

bool requiresExtendedSequenceRemap(size_t missionSelector) {
    return missionSelector == 14u || missionSelector == 15u || missionSelector == 17u;
}

BattleSetupSlotMetadata setupSlotMetadata(
    const OriginalBattlePlacementPosition& position,
    size_t slotIndex,
    const char* role) {
    BattleSetupSlotMetadata slot;
    slot.valid = true;
    slot.role = role;
    slot.slotIndex = slotIndex;
    slot.transform = position.transform;
    slot.gridX = position.gridX;
    slot.gridY = position.gridY;
    slot.originalRawPositionProven = true;
    slot.originalRawX = position.originalWorldX;
    slot.originalRawZ = position.originalWorldZ;
    return slot;
}

BattleSetupMetadata setupMetadataFromOriginalSetup(const OriginalBattlefieldSetup& setup) {
    BattleSetupMetadata metadata;
    metadata.valid = true;
    metadata.provenance = "original_btech_snario_diagnostic";
    metadata.scenarioIndex = setup.scenarioIndex;
    metadata.briefingMissionIdValid = setup.missionSelector < kMissionBriefingCount;
    metadata.missionSelectorContractProven = metadata.briefingMissionIdValid;
    metadata.briefingMissionId = setup.missionSelector;
    metadata.handoffMissionId = metadata.briefingMissionIdValid
        ? static_cast<uint8_t>(setup.missionSelector + 1u)
        : 0u;
    metadata.initialPlacementSelector = setup.missionSelector;
    metadata.missionSelector = setup.missionSelector;
    metadata.extendedSequenceRemapRequired = requiresExtendedSequenceRemap(setup.missionSelector);
    metadata.placementSelectorRuntimeResolved =
        metadata.missionSelectorContractProven && !metadata.extendedSequenceRemapRequired;
    metadata.missionSelectorProvenance = metadata.extendedSequenceRemapRequired
        ? "btech_extended_pre_remap_diagnostic"
        : (metadata.missionSelectorContractProven
               ? "mw_main_context7_minus_one_direct"
               : "btech_internal_selector_diagnostic");
    metadata.terrainMode = setup.terrainMode;
    metadata.tileNames = setup.tileNames;
    metadata.playerMode = setup.placement.playerSide.mode;
    metadata.opposingMode = setup.placement.opposingSide.mode;
    metadata.playerBankId = setup.placement.playerSide.bankId;
    metadata.opposingBankId = setup.placement.opposingSide.bankId;
    metadata.playerSlotIndex = 0;
    metadata.objectiveOpposingSlotIndex = 1;
    metadata.objectiveSourceSlot = "opposing:1";
    metadata.initialPlayerTransform = setup.playerStartTransform;
    metadata.playerSlots.reserve(setup.placement.playerSide.positions.size());
    for (size_t i = 0; i < setup.placement.playerSide.positions.size(); ++i) {
        BattleSetupSlotMetadata slot = setupSlotMetadata(
            setup.placement.playerSide.positions[i],
            i,
            i == 0u ? "player" : "player_allied");
        // BTECH gives the complete four-position player bank one initial side
        // facing. The controlled transform already carries that decoded facing.
        slot.transform.headingRadians = setup.playerStartTransform.headingRadians;
        metadata.playerSlots.push_back(std::move(slot));
    }
    metadata.opposingSlots.reserve(setup.placement.opposingSide.positions.size());
    for (size_t i = 0; i < setup.placement.opposingSide.positions.size(); ++i) {
        metadata.opposingSlots.push_back(
            setupSlotMetadata(
                setup.placement.opposingSide.positions[i],
                i,
                i == metadata.objectiveOpposingSlotIndex ? "objective_diagnostic" : "opposing"));
    }
    if (setup.placement.dedicatedObjective.valid) {
        const OriginalBattleDedicatedObjectivePlacement& source = setup.placement.dedicatedObjective;
        metadata.dedicatedObjective.valid = true;
        metadata.dedicatedObjective.sourceSide = source.sourceSide;
        metadata.dedicatedObjective.placementMode = source.sourceMode;
        metadata.dedicatedObjective.bankId = source.bankId;
        metadata.dedicatedObjective.coordinateOffset = source.coordinateOffset;
        metadata.dedicatedObjective.transform = source.position.transform;
        metadata.dedicatedObjective.gridX = source.position.gridX;
        metadata.dedicatedObjective.gridY = source.position.gridY;
        metadata.objectiveSourceSlot = source.sourceSide == 0 ? "dedicated:player" : "dedicated:opposing";
    }
    return metadata;
}

BattleObjectiveState objectiveFromOriginalSetup(const OriginalBattlefieldSetup& setup) {
    const OriginalBattleMissionPlacementModes modes =
        originalBattleMissionPlacementModes(setup.missionSelector);
    const BattleMissionObjectiveBriefing missionObjective =
        battleMissionObjectiveBriefingById(setup.missionSelector);
    BattleObjectiveState objective;
    objective.valid = true;
    objective.role = "target";
    objective.missionIntent = missionObjective.intent;
    objective.missionIntentProven = missionObjective.intentProven;
    objective.missionTargetKind = missionObjective.targetKind;
    objective.missionIntentProvenance = missionObjective.provenance;
    objective.transformProven = true;
    objective.missionSemanticsProven = false;
    objective.damagePolicyProven = modes.specialObjectDamagePolicyProven;
    objective.damageSuppressed = modes.specialObjectDamageSuppressed;
    objective.depletionPolicyProven = modes.specialObjectDepletionPolicyProven;
    objective.depletionSetsPlayerWinCondition =
        modes.specialObjectDepletionSetsPlayerWinCondition;
    objective.depletionSetsPlayerLossCondition =
        modes.specialObjectDepletionSetsPlayerLossCondition;
    objective.depletionResultCode = modes.specialObjectDepletionResultCode;
    objective.transform = setup.targetTransform;
    if (setup.placement.dedicatedObjective.valid) {
        const OriginalBattleDedicatedObjectivePlacement& target = setup.placement.dedicatedObjective;
        objective.provenance = "btech_mode4_dedicated_object";
        objective.sourceSlot = target.sourceSide == 0 ? "dedicated:player" : "dedicated:opposing";
        objective.activeObjectProven = true;
        objective.gridX = target.position.gridX;
        objective.gridY = target.position.gridY;
        if (missionObjective.intent == BattleMissionObjectiveIntent::Protect &&
            setup.missionSelector >= 2u && setup.missionSelector <= 7u) {
            objective.role = "protect";
            objective.missionIntentBoundToObjective = true;
            objective.missionSemanticsProven = true;
        }
    } else {
        const OriginalBattlePlacementPosition& target = setup.placement.opposingSide.positions[1];
        objective.provenance = "setup_derived_diagnostic";
        objective.sourceSlot = "opposing:1";
        objective.activeObjectProven = false;
        objective.gridX = target.gridX;
        objective.gridY = target.gridY;
    }
    objective.staticModel = originalBattleObjectiveStaticModel();
    return objective;
}

BattlefieldBoundaryState battlefieldBoundaryFromOriginalSetup(
    const OriginalBattlefieldSetup& setup,
    double terrainCellSize) {
    BattlefieldBoundaryState boundary;
    boundary.valid = true;
    boundary.provenance = "btech_ds0336_ds0342_snario_exit_mask";
    boundary.originalMinX = kOriginalBoundaryMinX;
    boundary.originalMaxX = kOriginalBoundaryMaxX;
    boundary.originalMinZ = kOriginalBoundaryMinZ;
    boundary.originalMaxZ = kOriginalBoundaryMaxZ;
    boundary.worldMinX = 0.0;
    boundary.worldMaxX =
        static_cast<double>(kOriginalBoundaryMaxX - kOriginalBoundaryMinX) /
        kOriginalGridCellSize * terrainCellSize;
    boundary.worldMinZ = 0.0;
    boundary.worldMaxZ =
        static_cast<double>(kOriginalBoundaryMaxZ - kOriginalBoundaryMinZ) /
        kOriginalGridCellSize * terrainCellSize;
    boundary.mapProjectionProven = true;
    boundary.actorExitEncodingProven = true;
    boundary.exitMaskSemanticsProven = true;
    boundary.playerAllowedExitMask = setup.placement.playerSide.mode == 1
        ? static_cast<uint8_t>(setup.placement.scenarioRaw[14] & 0x0fu)
        : 0;
    boundary.opposingAllowedExitMask = setup.placement.opposingSide.mode == 1
        ? static_cast<uint8_t>(setup.placement.scenarioRaw[15] & 0x0fu)
        : 0;
    boundary.sideCompletionExitPolicyProven = true;
    boundary.playerExitOutcomeProven = true;
    boundary.ordinaryPlayerExitResultCode = 2;
    boundary.allowedPlayerExitResultCode = 0;
    boundary.outcomeEvaluationDeferred = false;
    return boundary;
}

} // namespace

OriginalBattlefieldSetup decodeOriginalBattlefieldSetup(
    const std::filesystem::path& snarioPath,
    size_t scenarioIndex,
    size_t missionSelector,
    double terrainCellSize) {
    const std::vector<legacy3d::TerrainScenarioRecord> records =
        legacy3d::loadTerrainScenarioRecords(snarioPath);
    const std::optional<legacy3d::TerrainScenarioRecord> scenario =
        legacy3d::terrainScenarioRecordByIndex(records, scenarioIndex);
    if (!scenario.has_value()) {
        throw std::runtime_error("original battle setup scenario index is outside SNARIO.DAT");
    }
    if (!scenario->isTerrainLayoutCandidate()) {
        throw std::runtime_error("original battle setup scenario is not an active terrain layout candidate: " +
                                 std::to_string(scenarioIndex));
    }

    OriginalBattlefieldSetup setup;
    setup.scenarioIndex = scenarioIndex;
    setup.missionSelector = missionSelector;
    setup.terrainMode = scenario->terrainMode;
    setup.terrainScenario = *scenario;
    setup.tileNames = legacy3d::terrainScenarioTileNames(*scenario);
    setup.placement = decodeOriginalBattlePlacement(snarioPath, scenarioIndex, missionSelector, terrainCellSize);
    setup.playerStartTransform = setup.placement.playerSide.positions[0].transform;
    setup.enemyStartTransform = setup.placement.opposingSide.positions[0].transform;
    setup.targetTransform = setup.placement.dedicatedObjective.valid
        ? setup.placement.dedicatedObjective.position.transform
        : setup.placement.opposingSide.positions[1].transform;

    setup.playerStartTransform.headingRadians = setup.placement.playerHeadingToOpposingSlot1Radians;
    setup.enemyStartTransform.headingRadians = oppositeHeading(setup.placement.playerHeadingToOpposingSlot0Radians);
    setup.metadata = setupMetadataFromOriginalSetup(setup);
    setup.objective = objectiveFromOriginalSetup(setup);
    setup.battlefieldBoundary = battlefieldBoundaryFromOriginalSetup(setup, terrainCellSize);
    return setup;
}

OriginalBattlefieldSetup decodeOriginalDarkWingFinalBattlefieldSetup(
    const std::filesystem::path& snarioPath,
    size_t scenarioIndex,
    double terrainCellSize) {
    return decodeOriginalBattlefieldSetup(
        snarioPath,
        scenarioIndex,
        kDarkWingFinalMissionSelector,
        terrainCellSize);
}

void applyOriginalDarkWingFinalOpposition(
    BattleStartParams& params,
    const OriginalBattlefieldSetup& setup,
    std::optional<uint8_t> factionHouseId,
    std::string factionHouseName) {
    BattleOppositionRosterMetadata roster = originalDarkWingFinalOppositionRoster();
    if (!roster.initialMissionSelector || setup.missionSelector != *roster.initialMissionSelector) {
        throw std::runtime_error("Dark Wing final opposition requires the proven BTECH mission selector");
    }
    if (!params.combatantLaunchStates.empty()) {
        throw std::runtime_error("Dark Wing final opposition cannot be mixed with existing non-player launch states");
    }
    if (roster.orderedSlots.size() != setup.placement.opposingSide.positions.size()) {
        throw std::runtime_error("Dark Wing final opposition does not match the proven opposing placement slots");
    }

    params.combatantLaunchStates.reserve(roster.orderedSlots.size());
    for (const BattleOppositionRosterSlot& slot : roster.orderedSlots) {
        if (slot.opposingPlacementSlot >= setup.placement.opposingSide.positions.size()) {
            throw std::runtime_error("Dark Wing final opposition references an unavailable placement slot");
        }
        BattleCombatantLaunchState launch;
        launch.mechPresetId = slot.mechPresetId;
        launch.startTransform = setup.placement.opposingSide.positions[slot.opposingPlacementSlot].transform;
        launch.startTransform.headingRadians = headingToward(launch.startTransform, setup.playerStartTransform);
        launch.roster.team = BattleTeam::Opposing;
        launch.roster.factionHouseId = factionHouseId;
        launch.roster.factionHouseName = factionHouseName;
        launch.roster.provenance = roster.provenance;
        launch.roster.sourceSlot = "opposing:" + std::to_string(slot.opposingPlacementSlot);
        params.combatantLaunchStates.push_back(std::move(launch));
    }
    roster.combatantsSpawned = true;
    params.oppositionRoster = std::move(roster);
}

void applyOriginalContractOpposition(
    BattleStartParams& params,
    const OriginalBattlefieldSetup& setup,
    BattleOppositionSpawnPlan spawnPlan,
    std::optional<uint8_t> factionHouseId,
    std::string factionHouseName) {
    if (!params.combatantLaunchStates.empty()) {
        throw std::runtime_error("contract opposition cannot be mixed with existing non-player launch states");
    }
    BattleOppositionRosterMetadata roster = btechOppositionRosterFromSpawnPlan(
        spawnPlan, "original_btech_contract_hml_round_robin");
    if (!roster.valid || roster.orderedSlots.empty()) {
        throw std::runtime_error("contract opposition requires a resolved BTECH spawn plan");
    }
    if (roster.orderedSlots.size() > setup.placement.opposingSide.positions.size()) {
        throw std::runtime_error("contract opposition exceeds available opposing placement slots");
    }

    params.combatantLaunchStates.reserve(roster.orderedSlots.size());
    for (const BattleOppositionRosterSlot& slot : roster.orderedSlots) {
        if (slot.opposingPlacementSlot >= setup.placement.opposingSide.positions.size()) {
            throw std::runtime_error("contract opposition references an unavailable placement slot");
        }
        BattleCombatantLaunchState launch;
        launch.mechPresetId = slot.mechPresetId;
        launch.startTransform =
            setup.placement.opposingSide.positions[slot.opposingPlacementSlot].transform;
        launch.startTransform.headingRadians =
            headingToward(launch.startTransform, setup.playerStartTransform);
        launch.roster.team = BattleTeam::Opposing;
        launch.roster.factionHouseId = factionHouseId;
        launch.roster.factionHouseName = factionHouseName;
        launch.roster.provenance = roster.provenance;
        launch.roster.sourceSlot =
            "opposing:" + std::to_string(slot.opposingPlacementSlot);
        params.combatantLaunchStates.push_back(std::move(launch));
    }
    spawnPlan.combatantsSpawned = true;
    params.contract.oppositionSpawnPlan = std::move(spawnPlan);
    roster.combatantsSpawned = true;
    params.oppositionRoster = std::move(roster);
}

} // namespace mw::battle
