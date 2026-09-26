#include "battle/battle_world.h"

#include "legacy3d/shape_parser.h"
#include "legacy3d/terrain_parser.h"
#include "mech3d/mech_catalog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <queue>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace mw::battle {

bool combatantMechDestroyed(const Combatant& combatant);

namespace {

// Fresh BTECH mode-3 battles leave near pointer DS:CCEE at loader-zero.
// The original therefore consumes the unrelocated 12-byte payload at DS:0008.
constexpr int32_t kOriginalModeThreeNullDataRawX = 0x5220534d;
constexpr int32_t kOriginalModeThreeNullDataRawZ = 0x542d6e75;
constexpr int32_t kOriginalModeThreeNullDataRawY = 0x20656d69;

int32_t originalAiSignedDword(int64_t value);
int16_t originalAiFixedAngle(int32_t first, int32_t second);
void enterCombatantDeathState(Combatant& combatant);
uint64_t deathAnimationLastFrameElapsedMs(const Combatant& combatant);

} // namespace

void applyBattleDriveRuntimeTuning(
    BattleStartParams& params,
    const BattleDriveRuntimeTuning& tuning) {
    params.fixedTickSeconds = tuning.fixedTickSeconds;
    params.maxForwardSpeed = tuning.maxForwardSpeed;
    params.maxReverseSpeed = tuning.maxReverseSpeed;
    params.acceleration = tuning.acceleration;
    params.deceleration = tuning.deceleration;
    params.maxTurnRateRadians = tuning.maxTurnRateRadians;
}

BattleOriginalAiLanceOrderDiagnostic battleOriginalAiLanceOrder(
    int16_t controlWord00) {
    BattleOriginalAiLanceOrderDiagnostic result;
    result.controlWord00 = controlWord00;
    if (controlWord00 < 0 || controlWord00 > 5) {
        return result;
    }

    static constexpr std::array<const char*, 6> kLabels{{
        "ACT ON OWN",
        "AMBUSH",
        "DEFEND",
        "ATTACK ENEMY",
        "MOVE (ATTACK)",
        "MOVE (AVOID)",
    }};
    static constexpr std::array<BattleOriginalAiOrderTargetInput, 6>
        kTargetInputs{{
            BattleOriginalAiOrderTargetInput::None,
            BattleOriginalAiOrderTargetInput::MapLocation,
            BattleOriginalAiOrderTargetInput::MapLocation,
            BattleOriginalAiOrderTargetInput::LiveObjectOrBase,
            BattleOriginalAiOrderTargetInput::MapLocation,
            BattleOriginalAiOrderTargetInput::MapLocation,
        }};
    const size_t index = static_cast<size_t>(controlWord00);
    result.exact = true;
    result.selectable = true;
    result.order = static_cast<BattleOriginalAiLanceOrder>(controlWord00);
    result.targetInput = kTargetInputs[index];
    result.label = kLabels[index];
    return result;
}

BattleOriginalAiActOnOwnRouteDiagnostic battleOriginalAiActOnOwnRoute7797(
    int16_t controlWord00,
    int16_t numericSideModeWord) {
    BattleOriginalAiActOnOwnRouteDiagnostic result;
    result.controlWord00 = controlWord00;
    result.numericSideModeWord = numericSideModeWord;
    result.actOnOwn = controlWord00 == 0;
    if (!result.actOnOwn) {
        return result;
    }

    switch (numericSideModeWord) {
    case 0:
    case 2:
        result.exact = true;
        result.route =
            BattleOriginalAiActOnOwnRoute::SelectedCachedLiveTarget;
        result.callsTargetSelector23bf = true;
        result.callsMovementPoint94a4 = true;
        return result;
    case 1:
        result.exact = true;
        result.route = BattleOriginalAiActOnOwnRoute::ModeOneTablePoint;
        result.callsMovementPoint94a4 = true;
        result.requiresSideDirectionFlags = true;
        return result;
    case 3:
        result.exact = true;
        result.route = BattleOriginalAiActOnOwnRoute::SeparateObjectPoint;
        result.callsMovementPoint94a4 = true;
        result.requiresSeparateObjectPose = true;
        result.writesStatus46 = true;
        result.status46 = static_cast<int16_t>(6);
        return result;
    case 4:
        result.exact = true;
        result.route = BattleOriginalAiActOnOwnRoute::StatusSevenReturn;
        result.writesStatus46 = true;
        result.status46 = static_cast<int16_t>(7);
        return result;
    default:
        return result;
    }
}

BattleOriginalAiCampaignMovementActivationDiagnostic
battleOriginalAiCampaignMovementActivation(
    const BattleStartParams& params) {
    BattleOriginalAiCampaignMovementActivationDiagnostic result;
    result.exact = true;
    result.selectedPolicy = params.combatAiPolicy;
    result.incomingPolicyDisabled =
        params.combatAiPolicy == BattleCombatAiPolicy::Disabled;
    result.setupAvailable =
        params.setupMetadata.valid &&
        !params.setupMetadata.playerSlots.empty() &&
        !params.setupMetadata.opposingSlots.empty();
    result.playerActOnOwnRoute = battleOriginalAiActOnOwnRoute7797(
        static_cast<int16_t>(0),
        static_cast<int16_t>(params.setupMetadata.playerMode));
    result.opposingActOnOwnRoute = battleOriginalAiActOnOwnRoute7797(
        static_cast<int16_t>(0),
        static_cast<int16_t>(params.setupMetadata.opposingMode));
    result.actOnOwnSelectedTargetRoutesSupported =
        result.setupAvailable &&
        result.playerActOnOwnRoute.exact &&
        result.opposingActOnOwnRoute.exact &&
        result.playerActOnOwnRoute.route ==
            BattleOriginalAiActOnOwnRoute::SelectedCachedLiveTarget &&
        result.opposingActOnOwnRoute.route ==
            BattleOriginalAiActOnOwnRoute::SelectedCachedLiveTarget;
    result.playerSelectedTargetRouteSupported =
        result.setupAvailable && result.playerActOnOwnRoute.exact &&
        result.playerActOnOwnRoute.route ==
            BattleOriginalAiActOnOwnRoute::SelectedCachedLiveTarget &&
        (params.setupMetadata.playerMode == 0 ||
         params.setupMetadata.playerMode == 2);
    result.modeThreeNullDataRouteSupported =
        result.setupAvailable &&
        result.playerActOnOwnRoute.exact &&
        result.opposingActOnOwnRoute.exact &&
        result.playerActOnOwnRoute.route ==
            BattleOriginalAiActOnOwnRoute::SeparateObjectPoint &&
        result.opposingActOnOwnRoute.route ==
            BattleOriginalAiActOnOwnRoute::SeparateObjectPoint &&
        params.setupMetadata.playerMode == 3 &&
        params.setupMetadata.opposingMode == 3;
    // Retained for snapshot/API stability. This now means the stricter route
    // condition above, rather than admitting mode 1 merely because it has no
    // separate objective.
    result.nonObjectiveModesSupported =
        result.actOnOwnSelectedTargetRoutesSupported;
    result.activeSeparateObjectiveAbsent =
        !params.objective.activeObjectProven;
    result.activeSeparateObjectiveBound =
        params.objective.activeObjectProven &&
        params.objective.sourceSlot == params.setupMetadata.objectiveSourceSlot;
    result.completePhase12Runtime =
        params.deterministicCombatRuntimeEnabled &&
        params.individualWeaponRuntimeEnabled &&
        params.originalProjectileRuntimeEnabled &&
        params.originalHeatRuntimeEnabled &&
        params.originalMajorSystemRuntimeEnabled;
    result.originalMovementResourcesAvailable =
        params.terrainCollisionGrid.valid &&
        params.terrainCollisionGrid.width == 174 &&
        params.terrainCollisionGrid.height == 94 &&
        params.terrainCollisionGrid.rawSamples.size() == 174u * 94u &&
        params.originalTerrainSceneCatalog.valid;

    const auto activePlayerCompanion = [](const BattleCombatantLaunchState& state) {
        return state.roster.team == BattleTeam::Player &&
            state.priorMissionStatus == CombatantMissionStatus::Active;
    };
    const auto activeOpponent = [](const BattleCombatantLaunchState& state) {
        return state.roster.team == BattleTeam::Opposing &&
            state.priorMissionStatus == CombatantMissionStatus::Active;
    };
    result.activePlayerCompanionPresent = std::any_of(
        params.playerAlliedLaunchStates.begin(),
        params.playerAlliedLaunchStates.end(),
        activePlayerCompanion);
    result.activeOpponentPresent = std::any_of(
        params.combatantLaunchStates.begin(),
        params.combatantLaunchStates.end(),
        activeOpponent);

    const auto coveredBySetup = [](const auto& launches,
                                   const auto& setupSlots,
                                   const auto& activePredicate,
                                   size_t firstSlot) {
        size_t ordinal = firstSlot;
        for (const BattleCombatantLaunchState& launch : launches) {
            if (activePredicate(launch)) {
                if (ordinal >= setupSlots.size() ||
                    !setupSlots[ordinal].valid ||
                    !setupSlots[ordinal].originalRawPositionProven) {
                    return false;
                }
            }
            ++ordinal;
        }
        return true;
    };
    const bool activeSlotsCovered = result.setupAvailable &&
        coveredBySetup(
            params.playerAlliedLaunchStates,
            params.setupMetadata.playerSlots,
            activePlayerCompanion,
            1u) &&
        coveredBySetup(
            params.combatantLaunchStates,
            params.setupMetadata.opposingSlots,
            activeOpponent,
            0u);

    result.freshOrder = battleOriginalAiLanceOrder(0);
    result.defaultActOnOwnOrder =
        result.freshOrder.exact && result.freshOrder.selectable &&
        result.freshOrder.order == BattleOriginalAiLanceOrder::ActOnOwn &&
        result.freshOrder.targetInput ==
            BattleOriginalAiOrderTargetInput::None;
    const bool activateSelectedTargetRoutes =
        result.incomingPolicyDisabled &&
        result.nonObjectiveModesSupported &&
        result.activeSeparateObjectiveAbsent &&
        result.completePhase12Runtime &&
        result.originalMovementResourcesAvailable &&
        result.activePlayerCompanionPresent &&
        result.activeOpponentPresent &&
        activeSlotsCovered &&
        result.defaultActOnOwnOrder;
    result.playerOnlySelectedTargetActivated =
        result.incomingPolicyDisabled &&
        result.playerSelectedTargetRouteSupported &&
        result.opposingActOnOwnRoute.exact &&
        result.opposingActOnOwnRoute.route ==
            BattleOriginalAiActOnOwnRoute::StatusSevenReturn &&
        params.setupMetadata.opposingMode == 4 &&
        result.completePhase12Runtime &&
        result.originalMovementResourcesAvailable &&
        result.activePlayerCompanionPresent &&
        result.activeOpponentPresent &&
        activeSlotsCovered &&
        result.defaultActOnOwnOrder;
    result.modeThreeNullDataCampaignRejected =
        result.incomingPolicyDisabled &&
        result.modeThreeNullDataRouteSupported &&
        result.activeSeparateObjectiveAbsent &&
        result.completePhase12Runtime &&
        result.originalMovementResourcesAvailable &&
        result.activePlayerCompanionPresent &&
        result.activeOpponentPresent &&
        activeSlotsCovered &&
        result.defaultActOnOwnOrder;
    result.modeThreeSelectedTargetCompatibilityActivated = false;
    const bool missionObjectiveIntentSupported =
        params.objective.missionIntentProven &&
        (params.objective.missionIntent ==
             BattleMissionObjectiveIntent::Destroy ||
         params.objective.missionIntent ==
             BattleMissionObjectiveIntent::Disable ||
         params.objective.missionIntent ==
             BattleMissionObjectiveIntent::Retrieve);
    const bool missionObjectiveRawPointAvailable =
        params.setupMetadata.objectiveOpposingSlotIndex <
            params.setupMetadata.opposingSlots.size() &&
        params.setupMetadata.opposingSlots[
            params.setupMetadata.objectiveOpposingSlotIndex].valid &&
        params.setupMetadata.opposingSlots[
            params.setupMetadata.objectiveOpposingSlotIndex].
                originalRawPositionProven;
    result.modeThreeMissionObjectiveCompatibilityActivated =
        result.modeThreeNullDataCampaignRejected &&
        params.objective.valid && params.objective.transformProven &&
        !params.objective.activeObjectProven &&
        params.objective.sourceSlot ==
            params.setupMetadata.objectiveSourceSlot &&
        missionObjectiveIntentSupported &&
        missionObjectiveRawPointAvailable;
    result.activate = activateSelectedTargetRoutes ||
        result.playerOnlySelectedTargetActivated ||
        result.modeThreeMissionObjectiveCompatibilityActivated;
    if (activateSelectedTargetRoutes) {
        result.selectedPolicy = BattleCombatAiPolicy::
            OriginalBtechNonObjectiveSymmetricRepeatedMovement50eeInvariant;
        result.provenance =
            "BTECH.EXE:command_table_02b6+0ba5+6fd8:"
            "7797_act_on_own_modes0_2_target_selector:"
            "audit38_50ee_invariant_campaign_bridge";
    } else if (result.playerOnlySelectedTargetActivated) {
        result.selectedPolicy = BattleCombatAiPolicy::
            OriginalBtechNonObjectiveModesMultiTargetRepeatedMovement;
        result.provenance =
            "BTECH.EXE:7797_player_mode0_2_23bf:"
            "opposing_mode4_status7:player_companion_only:"
            "objective_gate_word_le2";
    } else if (result.modeThreeMissionObjectiveCompatibilityActivated) {
        result.selectedPolicy = BattleCombatAiPolicy::
            CompatibilityModeThreePlayerCompanionMissionObjectiveRepeatedMovement;
        result.provenance =
            "compatibility_provisional:BTECH_mode3_CCEE_null_at_fresh_start:"
            "destroy_disable_retrieve_mission_objective_raw_point_substitution";
    } else {
        result.provenance =
            "campaign_policy_preserved:unsupported_or_explicit";
    }
    return result;
}

BattleTerrainCollisionGrid loadOriginalBattleTerrainCollisionGrid(
    const std::vector<std::filesystem::path>& gridPaths,
    double cellSize) {
    if (gridPaths.size() != 4u || cellSize <= 0.0) {
        throw std::runtime_error(
            "battle terrain collision grid requires four GRD paths and a positive cell size");
    }
    std::array<legacy3d::TerrainGrid, 4> grids;
    for (size_t index = 0; index < grids.size(); ++index) {
        grids[index] = legacy3d::loadTerrainGrid(gridPaths[index]);
    }
    const legacy3d::TerrainGrid combined = legacy3d::composeTerrainGrid2x2(
        grids,
        "battle_collision_174x94",
        false,
        true);

    BattleTerrainCollisionGrid result;
    result.valid = true;
    result.provenance =
        "BTECH.EXE:FUN_1000_9700:nonzero_GRD_5x5_four_substep_probe";
    result.width = combined.width;
    result.height = combined.height;
    result.cellSize = cellSize;
    result.rawSamples.reserve(combined.samples.size());
    for (const legacy3d::TerrainGridSample& sample : combined.samples) {
        result.rawSamples.push_back(sample.rawValue);
    }
    return result;
}

namespace {

using TerrainFootprintPoint = BattleTerrainCollisionObstacle::FootprintPoint;

double terrainFootprintCross(
    const TerrainFootprintPoint& origin,
    const TerrainFootprintPoint& a,
    const TerrainFootprintPoint& b) {
    return (a.x - origin.x) * (b.z - origin.z) -
           (a.z - origin.z) * (b.x - origin.x);
}

std::vector<TerrainFootprintPoint> convexTerrainFootprint(
    std::vector<TerrainFootprintPoint> points) {
    std::sort(points.begin(), points.end(), [](const auto& a, const auto& b) {
        return a.x < b.x || (a.x == b.x && a.z < b.z);
    });
    points.erase(
        std::unique(points.begin(), points.end(), [](const auto& a, const auto& b) {
            return std::abs(a.x - b.x) < 0.001 && std::abs(a.z - b.z) < 0.001;
        }),
        points.end());
    if (points.size() <= 2u) {
        return points;
    }
    std::vector<TerrainFootprintPoint> hull;
    hull.reserve(points.size() * 2u);
    for (const TerrainFootprintPoint& point : points) {
        while (hull.size() >= 2u &&
               terrainFootprintCross(hull[hull.size() - 2u], hull.back(), point) <= 0.0) {
            hull.pop_back();
        }
        hull.push_back(point);
    }
    const size_t lowerSize = hull.size();
    for (size_t index = points.size() - 1u; index-- > 0u;) {
        const TerrainFootprintPoint& point = points[index];
        while (hull.size() > lowerSize &&
               terrainFootprintCross(hull[hull.size() - 2u], hull.back(), point) <= 0.0) {
            hull.pop_back();
        }
        hull.push_back(point);
    }
    if (!hull.empty()) {
        hull.pop_back();
    }
    return hull;
}

} // namespace

BattleTerrainCollisionObstacles loadOriginalBattleTerrainCollisionObstacles(
    const std::filesystem::path& terrainShapePath,
    const std::vector<std::filesystem::path>& worldPaths,
    double boundsMinX,
    double boundsMaxX,
    double boundsMinZ,
    double boundsMaxZ,
    double cellSize) {
    if (worldPaths.size() != 4u || cellSize <= 0.0 ||
        boundsMaxX <= boundsMinX || boundsMaxZ <= boundsMinZ) {
        throw std::runtime_error(
            "battle terrain object collision requires TERPCK, four WLD paths, bounds, and cell size");
    }
    const std::vector<legacy3d::RuntimeRecord> records =
        legacy3d::loadRuntimeShapeRecords(terrainShapePath);
    legacy3d::GpuBatchOptions options;
    options.shadeMode = "stored";
    options.swizzle = "xzy";
    const legacy3d::TerrainPlacementBounds placementBounds{
        static_cast<float>(boundsMinX), static_cast<float>(boundsMaxX),
        static_cast<float>(boundsMinZ), static_cast<float>(boundsMaxZ)};

    BattleTerrainCollisionObstacles result;
    result.valid = true;
    result.provenance =
        "WLD_TERPCK_typed_surface_and_convex_footprint_collision_v2";
    uint32_t nextStableId = 1u;
    for (size_t worldTileIndex = 0; worldTileIndex < worldPaths.size(); ++worldTileIndex) {
        const legacy3d::TerrainWorld world =
            legacy3d::loadTerrainWorld(worldPaths[worldTileIndex]);
        const std::vector<legacy3d::TerrainObjectPlacement> placements =
            legacy3d::mapTerrainWorldPlacementsFromRawCoordinates(
                world, placementBounds, static_cast<float>(cellSize));
        for (const legacy3d::TerrainObjectPlacement& placement : placements) {
            const uint32_t stableId = nextStableId++;
            if (static_cast<size_t>(placement.recordIndex) >= records.size()) {
                throw std::runtime_error("battle terrain collision object record is out of range");
            }
            const legacy3d::GpuBatch batch = legacy3d::buildGpuBatch(
                records[static_cast<size_t>(placement.recordIndex)], options);
            bool boundsValid = false;
            double minX = 0.0, maxX = 0.0, minY = 0.0, maxY = 0.0;
            double minZ = 0.0, maxZ = 0.0;
            for (size_t offset = 0; offset + 5u < batch.vertices.size(); offset += 6u) {
                const double x = batch.vertices[offset];
                const double y = batch.vertices[offset + 1u];
                const double z = batch.vertices[offset + 2u];
                if (!boundsValid) {
                    boundsValid = true;
                    minX = maxX = x; minY = maxY = y; minZ = maxZ = z;
                } else {
                    minX = std::min(minX, x); maxX = std::max(maxX, x);
                    minY = std::min(minY, y); maxY = std::max(maxY, y);
                    minZ = std::min(minZ, z); maxZ = std::max(maxZ, z);
                }
            }
            const double width = maxX - minX;
            const double depth = maxZ - minZ;
            const double height = maxY - minY;
            // TERPCK records 26..29 are the low 175..266-unit surface forms.
            // They will own pitch/height later and must not behave as walls now.
            if (!boundsValid || height <= 1.0 || width <= 1.0 || depth <= 1.0) {
                continue;
            }
            if (height <= 300.0) {
                BattleTerrainDriveableSurfaceFeature surface;
                surface.stableId = stableId;
                surface.recordIndex = placement.recordIndex;
                surface.worldTileIndex = worldTileIndex;
                surface.sourceIndex = placement.sourceIndex;
                surface.centerX = placement.x;
                surface.centerZ = placement.z;
                surface.halfExtentX = width * 0.5;
                surface.halfExtentZ = depth * 0.5;
                surface.height = height;
                result.driveableSurfaces.push_back(std::move(surface));
                continue;
            }
            BattleTerrainCollisionObstacle obstacle;
            obstacle.stableId = stableId;
            obstacle.recordIndex = placement.recordIndex;
            obstacle.worldTileIndex = worldTileIndex;
            obstacle.sourceIndex = placement.sourceIndex;
            obstacle.centerX = placement.x;
            obstacle.centerZ = placement.z;
            obstacle.halfExtentX = width * 0.5;
            obstacle.halfExtentZ = depth * 0.5;
            obstacle.height = height;
            const double centerX = (minX + maxX) * 0.5;
            const double centerZ = (minZ + maxZ) * 0.5;
            std::vector<TerrainFootprintPoint> footprintPoints;
            footprintPoints.reserve(batch.vertices.size() / 6u);
            for (size_t offset = 0; offset + 5u < batch.vertices.size(); offset += 6u) {
                footprintPoints.push_back({
                    batch.vertices[offset] + placement.x - centerX,
                    placement.z + centerZ - batch.vertices[offset + 2u],
                });
            }
            obstacle.footprint = convexTerrainFootprint(std::move(footprintPoints));
            if (obstacle.footprint.size() < 3u) {
                continue;
            }
            result.entries.push_back(std::move(obstacle));
        }
    }
    return result;
}

BattleOriginalTerrainSceneCatalog loadOriginalBattleTerrainSceneCatalog(
    const std::filesystem::path& terpckGiPath,
    const std::filesystem::path& terpckTblPath,
    const std::vector<std::filesystem::path>& worldPaths) {
    if (worldPaths.size() != 4u) {
        throw std::runtime_error(
            "original battle terrain scene requires exactly four WLD resources");
    }

    const legacy3d::TerpckGiResource collisionCatalog =
        legacy3d::loadTerpckGiResource(terpckGiPath);
    const std::vector<legacy3d::RuntimeRecord> runtimeRecords =
        legacy3d::loadRuntimeShapeRecords(terpckTblPath);
    if (runtimeRecords.size() < collisionCatalog.collisionRecords.size()) {
        throw std::runtime_error(
            "TERPCK.TBL does not cover every TERPCK.GI collision record");
    }

    BattleOriginalTerrainSceneCatalog result;
    result.valid = true;
    result.provenance =
        "BTECH.EXE:242c+afd5+b0af+1e78:D5CE_scene_catalog";
    result.sourceWorldCount = worldPaths.size();
    result.collisionRecords = collisionCatalog.collisionRecords;
    result.recordScaleShifts.reserve(result.collisionRecords.size());
    for (size_t index = 0; index < result.collisionRecords.size(); ++index) {
        result.recordScaleShifts.push_back(runtimeRecords[index].scaleShift);
    }

    for (size_t worldIndex = 0; worldIndex < worldPaths.size(); ++worldIndex) {
        const legacy3d::TerrainWorld world =
            legacy3d::loadTerrainWorld(worldPaths[worldIndex]);
        for (size_t sourceIndex = 0; sourceIndex < world.objects.size(); ++sourceIndex) {
            const legacy3d::TerrainWorldObject& source = world.objects[sourceIndex];
            BattleOriginalTerrainSceneObject object;
            object.sourceWorldIndex = worldIndex;
            object.sourceObjectIndex = sourceIndex;
            object.recordIndex = source.recordIndex;
            object.objectFlags = 0x10u;
            object.rawX = source.x;
            object.rawZ = source.z;
            object.rawY = source.y;
            result.objects.push_back(object);

            const int16_t signedRecordIndex =
                static_cast<int16_t>(source.recordIndex);
            if (signedRecordIndex >= 0 &&
                static_cast<size_t>(signedRecordIndex) <
                    result.collisionRecords.size()) {
                result.queryObjectIndices.push_back(result.objects.size() - 1u);
            }
        }
    }
    return result;
}

BattleOriginalTerrainSceneQueryDiagnostic battleOriginalTerrainSceneQuery(
    const BattleOriginalTerrainSceneCatalog& catalog,
    int32_t queryX,
    int32_t queryZ,
    uint8_t liveByte15,
    uint8_t liveByte17,
    uint8_t liveByte19,
    std::optional<size_t> cachedSceneObjectIndex,
    std::optional<uint8_t> cachedSubrecordIndex) {
    BattleOriginalTerrainSceneQueryDiagnostic result;
    result.exact = true;
    result.catalogAvailable = catalog.valid;
    if (!catalog.valid) {
        return result;
    }
    if (catalog.recordScaleShifts.size() != catalog.collisionRecords.size()) {
        result.exact = false;
        return result;
    }
    if (cachedSceneObjectIndex.has_value() != cachedSubrecordIndex.has_value()) {
        result.exact = false;
        result.cacheContractValid = false;
        return result;
    }

    std::optional<legacy3d::TerpckGiPlanarQueryResult> selectedPlanar;
    const auto queryObject = [&](size_t objectIndex,
                                 std::optional<uint8_t> cachedIndex)
        -> std::optional<legacy3d::TerpckGiPlanarQueryResult> {
        if (objectIndex >= catalog.objects.size()) {
            return std::nullopt;
        }
        const BattleOriginalTerrainSceneObject& object = catalog.objects[objectIndex];
        const size_t recordIndex = static_cast<size_t>(object.recordIndex);
        if (recordIndex >= catalog.collisionRecords.size()) {
            return std::nullopt;
        }
        ++result.planarQueryCount;
        legacy3d::TerpckGiPlanarQueryResult planar;
        try {
            planar = legacy3d::terpckGiQueryRecordPlanar(
                catalog.collisionRecords[recordIndex],
                queryX,
                queryZ,
                object.rawX,
                object.rawZ,
                catalog.recordScaleShifts[recordIndex],
                cachedIndex);
        } catch (const legacy3d::ParseError&) {
            result.exact = false;
            result.cacheContractValid = false;
            return std::nullopt;
        }
        if (!planar.subrecordIndex.has_value()) {
            return std::nullopt;
        }
        return planar;
    };

    if (cachedSceneObjectIndex.has_value()) {
        const size_t objectIndex = *cachedSceneObjectIndex;
        if (objectIndex >= catalog.objects.size()) {
            result.exact = false;
            result.cacheContractValid = false;
            return result;
        }
        const BattleOriginalTerrainSceneObject& object = catalog.objects[objectIndex];
        const size_t recordIndex = static_cast<size_t>(object.recordIndex);
        if (recordIndex >= catalog.collisionRecords.size()) {
            result.exact = false;
            result.cacheContractValid = false;
            return result;
        }
        const legacy3d::TerpckGiCollisionRecord& record =
            catalog.collisionRecords[recordIndex];
        if (record.priorityByte5 == 0u) {
            result.cachedObjectTested = true;
            selectedPlanar = queryObject(objectIndex, cachedSubrecordIndex);
            if (selectedPlanar.has_value()) {
                result.cachedObjectAccepted = true;
                result.hit = true;
                result.selectedSceneObjectIndex = objectIndex;
                result.selectedRecordIndex = object.recordIndex;
                result.selectedPriority = record.priorityByte5;
            } else if (result.exact) {
                result.clearedCacheAfterMiss = true;
            }
        } else {
            result.clearedCacheAfterMiss = true;
        }
    }

    if (!result.hit) {
        for (size_t objectIndex : catalog.queryObjectIndices) {
            ++result.visitedObjectCount;
            if (objectIndex >= catalog.objects.size()) {
                result.exact = false;
                return result;
            }
            const BattleOriginalTerrainSceneObject& object = catalog.objects[objectIndex];
            const size_t recordIndex = static_cast<size_t>(object.recordIndex);
            if (recordIndex >= catalog.collisionRecords.size()) {
                result.exact = false;
                return result;
            }
            const legacy3d::TerpckGiCollisionRecord& record =
                catalog.collisionRecords[recordIndex];
            if ((object.objectFlags & 0x10u) == 0u ||
                record.subrecordCountByte7 == 0u ||
                (result.hit && record.priorityByte5 > result.selectedPriority)) {
                continue;
            }
            const auto planar = queryObject(objectIndex, std::nullopt);
            if (!planar.has_value()) {
                continue;
            }
            result.hit = true;
            result.selectedSceneObjectIndex = objectIndex;
            result.selectedRecordIndex = object.recordIndex;
            result.selectedPriority = record.priorityByte5;
            selectedPlanar = planar;
            if (record.priorityByte5 == 0u) {
                break;
            }
        }
    }

    if (!result.hit || !selectedPlanar.has_value()) {
        return result;
    }
    const BattleOriginalTerrainSceneObject& object =
        catalog.objects[result.selectedSceneObjectIndex];
    const legacy3d::TerpckGiCollisionRecord& record =
        catalog.collisionRecords[result.selectedRecordIndex];
    const size_t subrecordIndex = *selectedPlanar->subrecordIndex;
    if (subrecordIndex >= record.subrecords.size() || subrecordIndex > 0xffu) {
        result.exact = false;
        return result;
    }
    const legacy3d::TerpckGiCollisionSubrecord& subrecord =
        record.subrecords[subrecordIndex];
    result.selectedResultByte = record.resultByte6;
    result.selectedSubrecordIndex = static_cast<uint8_t>(subrecordIndex);
    result.scaledDeltaX = selectedPlanar->scaledDeltaX;
    result.scaledDeltaZ = selectedPlanar->scaledDeltaZ;
    if (record.priorityByte5 == 0u) {
        const int16_t pointX = static_cast<int16_t>(
            static_cast<uint16_t>(selectedPlanar->scaledDeltaX));
        const int16_t pointZ = static_cast<int16_t>(
            static_cast<uint16_t>(selectedPlanar->scaledDeltaZ));
        result.localHeightWord =
            legacy3d::terpckGiSubrecordHeightWord(subrecord, pointX, pointZ);
        result.worldHeight = legacy3d::terpckGiScaleHeightToWorld(
            result.localHeightWord,
            catalog.recordScaleShifts[result.selectedRecordIndex],
            object.rawY);
        result.contactCorrection = legacy3d::terpckGiContactCorrection(
            subrecord, liveByte15, liveByte17, liveByte19);
    }
    return result;
}

std::vector<BattleProjectileHitProfile>
loadOriginalBattleProjectileHitProfiles(
    const std::filesystem::path& othpckPath) {
    const std::vector<legacy3d::RuntimeRecord> records =
        legacy3d::loadRuntimeShapeRecords(othpckPath);
    std::vector<BattleProjectileHitProfile> result;
    for (const uint16_t visualClass :
         std::array<uint16_t, 2>{uint16_t{0}, uint16_t{1}}) {
        if (static_cast<size_t>(visualClass) >= records.size()) {
            throw std::runtime_error(
                "projectile collision profile record is out of range");
        }
        const legacy3d::RuntimeRecord& record =
            records[static_cast<size_t>(visualClass)];
        BattleProjectileHitProfile profile;
        profile.valid = true;
        profile.originalVisualClass = visualClass;
        profile.originalModelGeometryProven = true;
        profile.spinTimingProven = false;
        profile.provenance =
            "OTHPCK.TBL:record_" + std::to_string(visualClass) +
            ":raw_polygon_triangles_xzy";

        uint64_t hash = 14695981039346656037ull;
        const auto appendHash = [&hash](uint64_t value) {
            for (int byte = 0; byte < 8; ++byte) {
                hash ^= static_cast<uint8_t>(
                    (value >> (byte * 8)) & 0xffu);
                hash *= 1099511628211ull;
            }
        };
        appendHash(visualClass);
        for (const legacy3d::RuntimePart& part : record.parts) {
            for (const legacy3d::RuntimeCommand& command : part.commands) {
                for (const legacy3d::RuntimePrimitive& primitive :
                     command.primitives) {
                    if (primitive.kind.rfind("polygon", 0) != 0) {
                        continue;
                    }
                    std::vector<size_t> indices;
                    indices.reserve(primitive.normalizedIndices.size());
                    for (uint8_t index : primitive.normalizedIndices) {
                        if (static_cast<size_t>(index) < record.vertices.size()) {
                            indices.push_back(static_cast<size_t>(index));
                        }
                    }
                    if (indices.size() < 3u) {
                        continue;
                    }
                    const auto pointAt = [&record](size_t index) {
                        const legacy3d::Vec3i point = record.vertices[index];
                        return BattleHitPoint{
                            static_cast<double>(point.x),
                            static_cast<double>(point.z),
                            static_cast<double>(point.y),
                        };
                    };
                    const BattleHitPoint first = pointAt(indices[0]);
                    for (size_t index = 1u; index + 1u < indices.size(); ++index) {
                        const BattleMechHitTriangle triangle{
                            first,
                            pointAt(indices[index]),
                            pointAt(indices[index + 1u]),
                            -1,
                            mech3d::MechArmorSectionId::CenterTorso,
                            false,
                        };
                        profile.triangles.push_back(triangle);
                        for (const BattleHitPoint* point : {
                                 &triangle.a, &triangle.b, &triangle.c}) {
                            appendHash(static_cast<uint64_t>(
                                static_cast<int64_t>(point->x)));
                            appendHash(static_cast<uint64_t>(
                                static_cast<int64_t>(point->y)));
                            appendHash(static_cast<uint64_t>(
                                static_cast<int64_t>(point->z)));
                        }
                    }
                }
            }
        }
        profile.geometryFingerprint = hash;
        profile.valid = !profile.triangles.empty() && hash != 0u;
        result.push_back(std::move(profile));
    }
    return result;
}

namespace {

constexpr int kScenarioTilesWide = 174;
constexpr int kScenarioTilesHigh = 94;
constexpr double kMovementEpsilon = 0.0001;
constexpr double kTurnEpsilon = 0.0001;
constexpr double kStationaryTurnWalkAnimationSpeedRatio = 0.25;
constexpr double kLocustOriginalMaxSpeedKph = 129.0;
// The BTECH table stores update counts rather than seconds. Controlled runtime
// timing remains to be captured, so the port uses the existing 100 ms legacy
// battle-step compatibility unit and converts it to the active fixed tick.
constexpr double kOriginalWeaponCooldownCompatibilityUpdateSeconds = 0.1;
constexpr uint64_t kFnvOffset = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

uint64_t collisionGridFingerprint(const BattleTerrainCollisionGrid& grid) {
    uint64_t hash = kFnvOffset;
    const auto append = [&hash](uint8_t value) {
        hash ^= value;
        hash *= kFnvPrime;
    };
    for (int byte = 0; byte < 4; ++byte) {
        append(static_cast<uint8_t>((static_cast<uint32_t>(grid.width) >> (byte * 8)) & 0xffu));
        append(static_cast<uint8_t>((static_cast<uint32_t>(grid.height) >> (byte * 8)) & 0xffu));
    }
    const int64_t cell = static_cast<int64_t>(std::llround(grid.cellSize * 1000.0));
    for (int byte = 0; byte < 8; ++byte) {
        append(static_cast<uint8_t>((static_cast<uint64_t>(cell) >> (byte * 8)) & 0xffu));
    }
    for (uint8_t sample : grid.rawSamples) {
        append(sample);
    }
    return hash;
}

uint64_t collisionObstacleFingerprint(const BattleTerrainCollisionObstacles& obstacles) {
    uint64_t hash = kFnvOffset;
    const auto append = [&hash](uint64_t value) {
        for (int byte = 0; byte < 8; ++byte) {
            hash ^= static_cast<uint8_t>((value >> (byte * 8)) & 0xffu);
            hash *= kFnvPrime;
        }
    };
    for (const BattleTerrainCollisionObstacle& obstacle : obstacles.entries) {
        append(obstacle.stableId);
        append(obstacle.recordIndex);
        append(obstacle.worldTileIndex);
        append(obstacle.sourceIndex);
        append(static_cast<uint64_t>(std::llround(obstacle.centerX * 1000.0)));
        append(static_cast<uint64_t>(std::llround(obstacle.centerZ * 1000.0)));
        append(static_cast<uint64_t>(std::llround(obstacle.halfExtentX * 1000.0)));
        append(static_cast<uint64_t>(std::llround(obstacle.halfExtentZ * 1000.0)));
        append(static_cast<uint64_t>(std::llround(obstacle.height * 1000.0)));
        append(obstacle.footprint.size());
        for (const TerrainFootprintPoint& point : obstacle.footprint) {
            append(static_cast<uint64_t>(std::llround(point.x * 1000.0)));
            append(static_cast<uint64_t>(std::llround(point.z * 1000.0)));
        }
    }
    return hash;
}

uint64_t driveableSurfaceFingerprint(const BattleTerrainCollisionObstacles& geometry) {
    uint64_t hash = kFnvOffset;
    const auto append = [&hash](uint64_t value) {
        for (int byte = 0; byte < 8; ++byte) {
            hash ^= static_cast<uint8_t>((value >> (byte * 8)) & 0xffu);
            hash *= kFnvPrime;
        }
    };
    for (const BattleTerrainDriveableSurfaceFeature& surface : geometry.driveableSurfaces) {
        append(surface.stableId);
        append(surface.recordIndex);
        append(surface.worldTileIndex);
        append(surface.sourceIndex);
        append(static_cast<uint64_t>(std::llround(surface.centerX * 1000.0)));
        append(static_cast<uint64_t>(std::llround(surface.centerZ * 1000.0)));
        append(static_cast<uint64_t>(std::llround(surface.halfExtentX * 1000.0)));
        append(static_cast<uint64_t>(std::llround(surface.halfExtentZ * 1000.0)));
        append(static_cast<uint64_t>(std::llround(surface.height * 1000.0)));
    }
    return hash;
}

uint64_t originalTerrainSceneFingerprint(
    const BattleOriginalTerrainSceneCatalog& catalog) {
    uint64_t hash = kFnvOffset;
    const auto append = [&hash](uint64_t value) {
        for (int byte = 0; byte < 8; ++byte) {
            hash ^= static_cast<uint8_t>((value >> (byte * 8)) & 0xffu);
            hash *= kFnvPrime;
        }
    };
    append(catalog.sourceWorldCount);
    append(catalog.collisionRecords.size());
    for (const legacy3d::TerpckGiCollisionRecord& record :
         catalog.collisionRecords) {
        append(record.recordIndex);
        append(record.pointer.offset);
        append(record.pointer.segment);
        append(record.pointer.linearOffset);
        append(record.extentX);
        append(record.extentZ);
        append(record.modeByte4);
        append(record.priorityByte5);
        append(record.resultByte6);
        append(record.subrecordCountByte7);
        append(record.subrecordTableOffset);
        append(record.subrecordTableLinearOffset);
        append(record.subrecords.size());
        for (const legacy3d::TerpckGiCollisionSubrecord& subrecord :
             record.subrecords) {
            append(subrecord.linearOffset);
            append(static_cast<uint16_t>(subrecord.word0));
            append(static_cast<uint16_t>(subrecord.word2));
            append(static_cast<uint16_t>(subrecord.word4));
            append(subrecord.heightScaleByte6);
            append(subrecord.byte7);
            append(subrecord.byte8);
            append(subrecord.edgeCount);
            append(subrecord.edgeTableOffset);
            append(subrecord.edgeTableLinearOffset);
            append(static_cast<uint16_t>(subrecord.baseHeightWord));
            append(subrecord.planeOffset);
            append(subrecord.planeLinearOffset);
            append(static_cast<uint16_t>(subrecord.plane.word0));
            append(static_cast<uint16_t>(subrecord.plane.word2));
            append(static_cast<uint16_t>(subrecord.plane.word4));
            append(static_cast<uint16_t>(subrecord.plane.word6));
            append(subrecord.edges.size());
            for (const legacy3d::TerpckGiHalfPlaneEdge& edge :
                 subrecord.edges) {
                append(static_cast<uint16_t>(edge.coefficientA));
                append(static_cast<uint16_t>(edge.coefficientB));
                append(static_cast<uint16_t>(edge.anchorX));
                append(static_cast<uint16_t>(edge.anchorZ));
            }
        }
    }
    append(catalog.recordScaleShifts.size());
    for (uint8_t scaleShift : catalog.recordScaleShifts) {
        append(scaleShift);
    }
    append(catalog.objects.size());
    for (const BattleOriginalTerrainSceneObject& object : catalog.objects) {
        append(object.sourceWorldIndex);
        append(object.sourceObjectIndex);
        append(object.recordIndex);
        append(object.objectFlags);
        append(static_cast<uint32_t>(object.rawX));
        append(static_cast<uint32_t>(object.rawZ));
        append(static_cast<uint32_t>(object.rawY));
    }
    append(catalog.queryObjectIndices.size());
    for (size_t objectIndex : catalog.queryObjectIndices) {
        append(objectIndex);
    }
    return hash;
}

double clampUnit(double value) {
    return std::max(-1.0, std::min(1.0, value));
}

int clampInt(int value, int minValue, int maxValue) {
    return std::max(minValue, std::min(maxValue, value));
}

double moveToward(double current, double target, double maxDelta) {
    if (current < target) {
        return std::min(current + maxDelta, target);
    }
    if (current > target) {
        return std::max(current - maxDelta, target);
    }
    return current;
}

double targetSpeedForThrottle(const Combatant& combatant, double throttle) {
    const double clamped = clampUnit(throttle);
    if (clamped >= 0.0) {
        return clamped * combatant.maxForwardSpeed;
    }
    return clamped * combatant.maxReverseSpeed;
}

double collisionRadiusForPreset(
    const BattleStartParams& params,
    const std::string& mechPresetId) {
    const auto profile = std::find_if(
        params.mechCollisionProfiles.begin(),
        params.mechCollisionProfiles.end(),
        [&mechPresetId](const BattleMechCollisionProfile& candidate) {
            return candidate.mechPresetId == mechPresetId;
        });
    if (profile != params.mechCollisionProfiles.end()) {
        return profile->radiusWorld;
    }
    return params.mechCollisionRadiusCells * params.terrainCellSize;
}

struct TerrainFootprintContact {
    const BattleTerrainCollisionObstacle* obstacle = nullptr;
    double normalX = 0.0;
    double normalZ = 0.0;
    double penetration = 0.0;
};

bool terrainFootprintContact(
    const BattleTerrainCollisionObstacle& obstacle,
    const Transform& transform,
    double radius,
    TerrainFootprintContact* contact) {
    if (obstacle.footprint.size() < 3u ||
        transform.x < obstacle.centerX - obstacle.halfExtentX - radius ||
        transform.x > obstacle.centerX + obstacle.halfExtentX + radius ||
        transform.z < obstacle.centerZ - obstacle.halfExtentZ - radius ||
        transform.z > obstacle.centerZ + obstacle.halfExtentZ + radius) {
        return false;
    }

    bool inside = true;
    double closestDistanceSquared = std::numeric_limits<double>::max();
    double closestX = transform.x;
    double closestZ = transform.z;
    for (size_t index = 0; index < obstacle.footprint.size(); ++index) {
        const TerrainFootprintPoint& a = obstacle.footprint[index];
        const TerrainFootprintPoint& b =
            obstacle.footprint[(index + 1u) % obstacle.footprint.size()];
        const double edgeX = b.x - a.x;
        const double edgeZ = b.z - a.z;
        const double relativeX = transform.x - a.x;
        const double relativeZ = transform.z - a.z;
        if (edgeX * relativeZ - edgeZ * relativeX < -kMovementEpsilon) {
            inside = false;
        }
        const double lengthSquared = edgeX * edgeX + edgeZ * edgeZ;
        const double t = lengthSquared > kMovementEpsilon
                             ? std::clamp(
                                   (relativeX * edgeX + relativeZ * edgeZ) /
                                       lengthSquared,
                                   0.0,
                                   1.0)
                             : 0.0;
        const double candidateX = a.x + edgeX * t;
        const double candidateZ = a.z + edgeZ * t;
        const double dx = transform.x - candidateX;
        const double dz = transform.z - candidateZ;
        const double distanceSquared = dx * dx + dz * dz;
        if (distanceSquared < closestDistanceSquared) {
            closestDistanceSquared = distanceSquared;
            closestX = candidateX;
            closestZ = candidateZ;
        }
    }

    const double distance = std::sqrt(std::max(0.0, closestDistanceSquared));
    if (!inside && distance >= radius) {
        return false;
    }
    double normalX = inside ? closestX - transform.x : transform.x - closestX;
    double normalZ = inside ? closestZ - transform.z : transform.z - closestZ;
    const double normalLength = std::hypot(normalX, normalZ);
    if (normalLength > kMovementEpsilon) {
        normalX /= normalLength;
        normalZ /= normalLength;
    } else {
        normalX = transform.x - obstacle.centerX;
        normalZ = transform.z - obstacle.centerZ;
        const double centerDistance = std::hypot(normalX, normalZ);
        if (centerDistance > kMovementEpsilon) {
            normalX /= centerDistance;
            normalZ /= centerDistance;
        } else {
            normalX = 1.0;
            normalZ = 0.0;
        }
    }
    if (contact != nullptr) {
        contact->obstacle = &obstacle;
        contact->normalX = normalX;
        contact->normalZ = normalZ;
        contact->penetration = inside ? radius + distance : radius - distance;
    }
    return true;
}

double distance2d(const Transform& a, const Transform& b) {
    const double dx = b.x - a.x;
    const double dz = b.z - a.z;
    return std::sqrt(dx * dx + dz * dz);
}

bool replacementPathPointBlocked(
    const BattleTerrainCollisionObstacles& obstacles,
    const Transform& point,
    double clearanceRadius) {
    if (!obstacles.valid) {
        return false;
    }
    return std::any_of(
        obstacles.entries.begin(), obstacles.entries.end(),
        [&](const BattleTerrainCollisionObstacle& obstacle) {
            return terrainFootprintContact(
                obstacle, point, clearanceRadius, nullptr);
        });
}

bool replacementPathSegmentClear(
    const BattleTerrainCollisionObstacles& obstacles,
    const Transform& start,
    const Transform& end,
    double clearanceRadius) {
    if (!obstacles.valid || obstacles.entries.empty()) {
        return true;
    }
    const double distance = distance2d(start, end);
    const double sampleSpacing = std::clamp(
        clearanceRadius * 0.75, 64.0, 200.0);
    const size_t sampleCount = static_cast<size_t>(std::max(
        1.0, std::ceil(distance / sampleSpacing)));
    for (size_t sample = 1u; sample <= sampleCount; ++sample) {
        const double t = static_cast<double>(sample) /
            static_cast<double>(sampleCount);
        Transform point;
        point.x = start.x + (end.x - start.x) * t;
        point.z = start.z + (end.z - start.z) * t;
        if (replacementPathPointBlocked(
                obstacles, point, clearanceRadius)) {
            return false;
        }
    }
    return true;
}

struct ReplacementPathWaypointPlan {
    bool valid = false;
    Transform waypoint{};
    size_t exploredNodeCount = 0u;
};

ReplacementPathWaypointPlan replacementPathWaypointPlan(
    const BattleTerrainSnapshot& terrain,
    const BattleTerrainCollisionObstacles& obstacles,
    const Transform& start,
    const Transform& goal,
    double mechRadius) {
    ReplacementPathWaypointPlan result;
    if (!obstacles.valid || obstacles.entries.empty()) {
        return result;
    }

    const double clearanceRadius = std::max(1.0, mechRadius + 100.0);
    if (replacementPathSegmentClear(
            obstacles, start, goal, clearanceRadius)) {
        return result;
    }

    double minX = terrain.boundsMinX + clearanceRadius;
    double maxX = terrain.boundsMaxX - clearanceRadius;
    double minZ = terrain.boundsMinZ + clearanceRadius;
    double maxZ = terrain.boundsMaxZ - clearanceRadius;
    const bool terrainBoundsUsable = maxX > minX && maxZ > minZ &&
        start.x >= minX && start.x <= maxX &&
        start.z >= minZ && start.z <= maxZ &&
        goal.x >= minX && goal.x <= maxX &&
        goal.z >= minZ && goal.z <= maxZ;
    if (!terrainBoundsUsable) {
        minX = std::min(start.x, goal.x);
        maxX = std::max(start.x, goal.x);
        minZ = std::min(start.z, goal.z);
        maxZ = std::max(start.z, goal.z);
        for (const BattleTerrainCollisionObstacle& obstacle :
             obstacles.entries) {
            minX = std::min(
                minX, obstacle.centerX - obstacle.halfExtentX);
            maxX = std::max(
                maxX, obstacle.centerX + obstacle.halfExtentX);
            minZ = std::min(
                minZ, obstacle.centerZ - obstacle.halfExtentZ);
            maxZ = std::max(
                maxZ, obstacle.centerZ + obstacle.halfExtentZ);
        }
        const double margin = std::max(2000.0, clearanceRadius * 4.0);
        minX -= margin;
        maxX += margin;
        minZ -= margin;
        maxZ += margin;
    }

    double cellSize = 250.0;
    size_t width = static_cast<size_t>(
        std::ceil((maxX - minX) / cellSize)) + 1u;
    size_t height = static_cast<size_t>(
        std::ceil((maxZ - minZ) / cellSize)) + 1u;
    constexpr size_t kMaximumPathNodeCount = 180000u;
    if (width * height > kMaximumPathNodeCount) {
        const double scale = std::sqrt(
            static_cast<double>(width * height) /
            static_cast<double>(kMaximumPathNodeCount));
        cellSize *= scale;
        width = static_cast<size_t>(
            std::ceil((maxX - minX) / cellSize)) + 1u;
        height = static_cast<size_t>(
            std::ceil((maxZ - minZ) / cellSize)) + 1u;
    }
    if (width < 2u || height < 2u ||
        width > std::numeric_limits<size_t>::max() / height) {
        return result;
    }
    const size_t nodeCount = width * height;
    if (nodeCount == 0u || nodeCount > kMaximumPathNodeCount) {
        return result;
    }

    const auto nodeTransform = [=](size_t index) {
        Transform point;
        point.x = minX + static_cast<double>(index % width) * cellSize;
        point.z = minZ + static_cast<double>(index / width) * cellSize;
        return point;
    };
    const auto closestNode = [=](const Transform& point) {
        const size_t x = static_cast<size_t>(std::clamp<long long>(
            std::llround((point.x - minX) / cellSize),
            0ll, static_cast<long long>(width - 1u)));
        const size_t z = static_cast<size_t>(std::clamp<long long>(
            std::llround((point.z - minZ) / cellSize),
            0ll, static_cast<long long>(height - 1u)));
        return z * width + x;
    };
    std::vector<int8_t> blocked(nodeCount, int8_t{-1});
    const auto nodeBlocked = [&](size_t index) {
        int8_t& cached = blocked[index];
        if (cached < 0) {
            cached = replacementPathPointBlocked(
                obstacles, nodeTransform(index), clearanceRadius)
                ? int8_t{1} : int8_t{0};
        }
        return cached != 0;
    };
    const auto nearestOpenNode = [&](size_t origin) {
        if (!nodeBlocked(origin)) {
            return origin;
        }
        const long long originX = static_cast<long long>(origin % width);
        const long long originZ = static_cast<long long>(origin / width);
        const size_t maximumRing = std::min<size_t>(
            48u, std::max(width, height));
        for (size_t ring = 1u; ring <= maximumRing; ++ring) {
            size_t best = nodeCount;
            double bestDistance = std::numeric_limits<double>::max();
            for (long long dz = -static_cast<long long>(ring);
                 dz <= static_cast<long long>(ring); ++dz) {
                for (long long dx = -static_cast<long long>(ring);
                     dx <= static_cast<long long>(ring); ++dx) {
                    if (std::max(std::llabs(dx), std::llabs(dz)) !=
                        static_cast<long long>(ring)) {
                        continue;
                    }
                    const long long x = originX + dx;
                    const long long z = originZ + dz;
                    if (x < 0 || z < 0 ||
                        x >= static_cast<long long>(width) ||
                        z >= static_cast<long long>(height)) {
                        continue;
                    }
                    const size_t candidate =
                        static_cast<size_t>(z) * width +
                        static_cast<size_t>(x);
                    if (nodeBlocked(candidate)) {
                        continue;
                    }
                    const double candidateDistance =
                        distance2d(nodeTransform(candidate),
                                   nodeTransform(origin));
                    if (candidateDistance < bestDistance ||
                        (candidateDistance == bestDistance &&
                         candidate < best)) {
                        best = candidate;
                        bestDistance = candidateDistance;
                    }
                }
            }
            if (best != nodeCount) {
                return best;
            }
        }
        return nodeCount;
    };

    const size_t startNode = nearestOpenNode(closestNode(start));
    const size_t goalNode = nearestOpenNode(closestNode(goal));
    if (startNode == nodeCount || goalNode == nodeCount) {
        return result;
    }

    struct OpenNode {
        double estimate = 0.0;
        double cost = 0.0;
        size_t index = 0u;
    };
    struct OpenNodeLater {
        bool operator()(const OpenNode& left, const OpenNode& right) const {
            if (left.estimate != right.estimate) {
                return left.estimate > right.estimate;
            }
            if (left.cost != right.cost) {
                return left.cost > right.cost;
            }
            return left.index > right.index;
        }
    };
    const auto heuristic = [=](size_t index) {
        const long long dx = std::llabs(
            static_cast<long long>(index % width) -
            static_cast<long long>(goalNode % width));
        const long long dz = std::llabs(
            static_cast<long long>(index / width) -
            static_cast<long long>(goalNode / width));
        const long long diagonal = std::min(dx, dz);
        return static_cast<double>(dx + dz - 2ll * diagonal) +
            std::sqrt(2.0) * static_cast<double>(diagonal);
    };
    std::priority_queue<OpenNode,
                        std::vector<OpenNode>,
                        OpenNodeLater> open;
    std::vector<double> costs(
        nodeCount, std::numeric_limits<double>::max());
    std::vector<size_t> parents(nodeCount, nodeCount);
    std::vector<uint8_t> closed(nodeCount, 0u);
    costs[startNode] = 0.0;
    open.push({heuristic(startNode), 0.0, startNode});
    constexpr std::array<std::pair<int, int>, 8> kDirections{{
        {1, 0}, {0, 1}, {-1, 0}, {0, -1},
        {1, 1}, {-1, 1}, {-1, -1}, {1, -1},
    }};
    while (!open.empty()) {
        const OpenNode current = open.top();
        open.pop();
        if (closed[current.index] != 0u ||
            current.cost != costs[current.index]) {
            continue;
        }
        closed[current.index] = 1u;
        ++result.exploredNodeCount;
        if (current.index == goalNode) {
            break;
        }
        const int currentX = static_cast<int>(current.index % width);
        const int currentZ = static_cast<int>(current.index / width);
        for (const auto [dx, dz] : kDirections) {
            const int nextX = currentX + dx;
            const int nextZ = currentZ + dz;
            if (nextX < 0 || nextZ < 0 ||
                nextX >= static_cast<int>(width) ||
                nextZ >= static_cast<int>(height)) {
                continue;
            }
            const size_t next = static_cast<size_t>(nextZ) * width +
                static_cast<size_t>(nextX);
            if (nodeBlocked(next) || closed[next] != 0u) {
                continue;
            }
            if (dx != 0 && dz != 0) {
                const size_t horizontal =
                    static_cast<size_t>(currentZ) * width +
                    static_cast<size_t>(nextX);
                const size_t vertical =
                    static_cast<size_t>(nextZ) * width +
                    static_cast<size_t>(currentX);
                if (nodeBlocked(horizontal) || nodeBlocked(vertical)) {
                    continue;
                }
            }
            const double stepCost = dx != 0 && dz != 0
                ? std::sqrt(2.0) : 1.0;
            const double candidateCost = current.cost + stepCost;
            if (candidateCost >= costs[next]) {
                continue;
            }
            costs[next] = candidateCost;
            parents[next] = current.index;
            open.push({candidateCost + heuristic(next),
                       candidateCost, next});
        }
    }
    if (closed[goalNode] == 0u) {
        return result;
    }

    std::vector<size_t> path;
    for (size_t node = goalNode; node != nodeCount;
         node = parents[node]) {
        path.push_back(node);
        if (node == startNode) {
            break;
        }
    }
    if (path.empty() || path.back() != startNode) {
        return result;
    }
    std::reverse(path.begin(), path.end());
    size_t waypointNode = path.size() > 1u ? path[1] : path.front();
    for (size_t pathIndex = path.size(); pathIndex-- > 1u;) {
        const Transform candidate = nodeTransform(path[pathIndex]);
        if (replacementPathSegmentClear(
                obstacles, start, candidate, clearanceRadius)) {
            waypointNode = path[pathIndex];
            break;
        }
    }
    result.valid = true;
    result.waypoint = nodeTransform(waypointNode);
    return result;
}

double speedStepRate(const BattleStartParams& params, double current, double target) {
    if (params.acceleration <= 0.0 || params.deceleration <= 0.0) {
        throw std::runtime_error("battle movement acceleration/deceleration must be positive");
    }
    const bool changingDirection = (current < 0.0 && target > 0.0) || (current > 0.0 && target < 0.0);
    const bool slowingDown = std::abs(target) < std::abs(current);
    return (changingDirection || slowingDown) ? params.deceleration : params.acceleration;
}

uint64_t fixedTickMs(const BattleStartParams& params) {
    if (params.fixedTickSeconds <= 0.0) {
        throw std::runtime_error("battle fixed tick duration must be positive");
    }
    return static_cast<uint64_t>(std::llround(params.fixedTickSeconds * 1000.0));
}

uint64_t weaponCooldownTicks(
    const BattleStartParams& params,
    uint16_t originalUpdateCount) {
    if (originalUpdateCount == 0u) {
        return params.weaponCooldownTicks;
    }
    const double ticks =
        static_cast<double>(originalUpdateCount) *
        kOriginalWeaponCooldownCompatibilityUpdateSeconds /
        params.fixedTickSeconds;
    return std::max<uint64_t>(1u, static_cast<uint64_t>(std::llround(ticks)));
}

double maxSpeedForAnimation(const Combatant& combatant, double speed) {
    const double maxSpeed = speed < 0.0 ? combatant.maxReverseSpeed : combatant.maxForwardSpeed;
    if (maxSpeed <= 0.0) {
        throw std::runtime_error("battle movement max speeds must be positive");
    }
    return maxSpeed;
}

bool combatantUsesWalkAnimation(const Combatant& combatant) {
    const mech3d::MechCatalogMobility mobility =
        mech3d::catalogMobility(combatant.mechPresetId);
    const bool originalAiRawMotion =
        combatant.originalAiMotion.initialized &&
        combatant.originalAiMotion.worldPositionBindingExact &&
        combatant.originalAiMotion.acceptedPoseCommitCount > 0u &&
        mobility.originalBtechDefinitionSpeedWord06 > 0 &&
        combatant.originalAiMotion.rawSpeedWord3c != 0;
    return combatant.missionStatus == CombatantMissionStatus::Active &&
           !combatant.deathAnimationStarted &&
           !combatant.airborne &&
           (std::abs(combatant.forwardSpeed) > kMovementEpsilon ||
            std::abs(combatant.turn) > kTurnEpsilon ||
            originalAiRawMotion);
}

bool combatantReactorShutdown(const Combatant& combatant) {
    return combatant.heat.enabled && combatant.heat.reactorShutdown;
}

bool combatantControlShutdown(const Combatant& combatant) {
    return combatantReactorShutdown(combatant) ||
           (combatant.majorSystems.enabled &&
            (combatant.majorSystems.engineShutdown ||
             combatant.majorSystems.lifeSupportFailure));
}

bool combatantMovementBlocked(const Combatant& combatant) {
    return combatant.missionStatus == CombatantMissionStatus::Destroyed ||
           combatantReactorShutdown(combatant) ||
           (combatant.majorSystems.enabled &&
            combatant.majorSystems.movementBlocked);
}

bool canActivateJumpJets(const BattleStartParams& params, const Combatant& combatant) {
    return !combatantMovementBlocked(combatant) &&
           combatant.jumpCapable &&
           combatant.jumpFuel + 0.0001 >= params.jumpJetActivationFuel;
}

bool missionTerminal(BattleMissionRuntimeState state) {
    return state == BattleMissionRuntimeState::Victory ||
           state == BattleMissionRuntimeState::Defeat ||
           state == BattleMissionRuntimeState::Withdraw ||
           state == BattleMissionRuntimeState::Unsupported;
}

uint64_t walkAnimationTickMs(const Combatant& combatant, uint64_t tickMs) {
    const double speedRatio =
        std::abs(combatant.forwardSpeed) > kMovementEpsilon
            ? std::min(1.0, std::abs(combatant.forwardSpeed) / maxSpeedForAnimation(combatant, combatant.forwardSpeed))
            : 0.0;
    const double stationaryTurnRatio =
        std::abs(combatant.turn) > kTurnEpsilon
            ? std::min(1.0, std::abs(combatant.turn)) * kStationaryTurnWalkAnimationSpeedRatio
            : 0.0;
    const mech3d::MechCatalogMobility mobility =
        mech3d::catalogMobility(combatant.mechPresetId);
    const double originalAiRawSpeedRatio =
        combatant.originalAiMotion.initialized &&
        combatant.originalAiMotion.worldPositionBindingExact &&
        combatant.originalAiMotion.acceptedPoseCommitCount > 0u &&
        mobility.originalBtechDefinitionSpeedWord06 > 0
            ? std::min(
                  1.0,
                  std::abs(static_cast<double>(
                      combatant.originalAiMotion.rawSpeedWord3c)) /
                      static_cast<double>(
                          mobility.originalBtechDefinitionSpeedWord06))
            : 0.0;
    const double animationRatio = std::max(
        std::max(speedRatio, stationaryTurnRatio),
        originalAiRawSpeedRatio);
    return static_cast<uint64_t>(std::llround(static_cast<double>(tickMs) * animationRatio));
}

BattleTerrainSnapshot makeTerrainSnapshot(const BattleStartParams& params) {
    BattleTerrainSnapshot snapshot;
    snapshot.scenarioIndex = params.terrainScenarioIndex;
    snapshot.environmentId = params.terrainEnvironmentId;
    snapshot.boundsMinX = 0.0;
    snapshot.boundsMinZ = 0.0;
    snapshot.boundsMaxX = static_cast<double>(kScenarioTilesWide - 1) * params.terrainCellSize;
    snapshot.boundsMaxZ = static_cast<double>(kScenarioTilesHigh - 1) * params.terrainCellSize;

    if (params.terrainCollisionGrid.valid) {
        const BattleTerrainCollisionGrid& grid = params.terrainCollisionGrid;
        if (grid.width <= 0 || grid.height <= 0 || grid.cellSize <= 0.0 ||
            grid.rawSamples.size() !=
                static_cast<size_t>(grid.width) * static_cast<size_t>(grid.height)) {
            throw std::runtime_error("battle terrain collision grid is malformed");
        }
        snapshot.collisionGridValid = true;
        snapshot.collisionGridProvenance = grid.provenance;
        snapshot.collisionGridWidth = grid.width;
        snapshot.collisionGridHeight = grid.height;
        snapshot.collisionGridCellSize = grid.cellSize;
        snapshot.collisionGridBlockingSampleCount = static_cast<size_t>(std::count_if(
            grid.rawSamples.begin(),
            grid.rawSamples.end(),
            [](uint8_t sample) { return sample != 0u; }));
        snapshot.collisionGridFingerprint = collisionGridFingerprint(grid);
    }
    if (params.terrainCollisionObstacles.valid) {
        snapshot.collisionObstacleCount = params.terrainCollisionObstacles.entries.size();
        snapshot.collisionObstacleFingerprint =
            collisionObstacleFingerprint(params.terrainCollisionObstacles);
        snapshot.driveableSurfaceFeatureCount =
            params.terrainCollisionObstacles.driveableSurfaces.size();
        snapshot.driveableSurfaceFeatureFingerprint =
            driveableSurfaceFingerprint(params.terrainCollisionObstacles);
    }
    if (params.originalTerrainSceneCatalog.valid) {
        const BattleOriginalTerrainSceneCatalog& catalog =
            params.originalTerrainSceneCatalog;
        if (catalog.sourceWorldCount != 4u ||
            catalog.recordScaleShifts.size() != catalog.collisionRecords.size()) {
            throw std::runtime_error(
                "original terrain scene catalog is malformed");
        }
        for (size_t objectIndex : catalog.queryObjectIndices) {
            if (objectIndex >= catalog.objects.size()) {
                throw std::runtime_error(
                    "original terrain scene query object index is out of range");
            }
            const uint16_t recordIndex = catalog.objects[objectIndex].recordIndex;
            if (static_cast<size_t>(recordIndex) >= catalog.collisionRecords.size()) {
                throw std::runtime_error(
                    "original terrain scene query record index is out of range");
            }
        }
        snapshot.originalTerrainSceneValid = true;
        snapshot.originalTerrainSceneProvenance = catalog.provenance;
        snapshot.originalTerrainCollisionRecordCount =
            catalog.collisionRecords.size();
        snapshot.originalTerrainSceneObjectCount = catalog.objects.size();
        snapshot.originalTerrainQueryObjectCount =
            catalog.queryObjectIndices.size();
        snapshot.originalTerrainSceneFingerprint =
            originalTerrainSceneFingerprint(catalog);
    }

    if (params.terrainScenarioPath.empty()) {
        return snapshot;
    }

    const std::vector<legacy3d::TerrainScenarioRecord> records =
        legacy3d::loadTerrainScenarioRecords(params.terrainScenarioPath);
    const std::optional<legacy3d::TerrainScenarioRecord> record =
        legacy3d::terrainScenarioRecordByIndex(records, params.terrainScenarioIndex);
    if (!record) {
        throw std::runtime_error("battle terrain scenario index not found: " + std::to_string(params.terrainScenarioIndex));
    }
    if (!record->isTerrainLayoutCandidate()) {
        throw std::runtime_error("battle terrain scenario is not an active terrain layout candidate: " +
                                 std::to_string(params.terrainScenarioIndex));
    }

    snapshot.scenarioLoaded = true;
    snapshot.terrainMode = record->terrainMode;
    snapshot.tileNames = legacy3d::terrainScenarioTileNames(*record);
    return snapshot;
}

void hashBytes(uint64_t& hash, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= kFnvPrime;
    }
}

void hashUint64(uint64_t& hash, uint64_t value) {
    hashBytes(hash, &value, sizeof(value));
}

void hashInt64(uint64_t& hash, int64_t value) {
    hashBytes(hash, &value, sizeof(value));
}

void hashString(uint64_t& hash, const std::string& value) {
    hashUint64(hash, static_cast<uint64_t>(value.size()));
    hashBytes(hash, value.data(), value.size());
}

void hashOptionalInt(uint64_t& hash, const std::optional<int>& value) {
    hashUint64(hash, value.has_value() ? 1ull : 0ull);
    if (value) {
        hashInt64(hash, *value);
    }
}

void hashRosterMetadata(uint64_t& hash, const BattleCombatantRosterMetadata& roster) {
    hashUint64(hash, static_cast<uint64_t>(roster.team));
    hashUint64(hash, roster.factionHouseId.has_value() ? 1ull : 0ull);
    if (roster.factionHouseId) {
        hashUint64(hash, *roster.factionHouseId);
    }
    hashString(hash, roster.factionHouseName);
    hashString(hash, roster.provenance);
    hashString(hash, roster.sourceSlot);
    hashUint64(hash, roster.originalLiveObjectSlot.has_value() ? 1ull : 0ull);
    if (roster.originalLiveObjectSlot) {
        hashUint64(hash, *roster.originalLiveObjectSlot);
    }
    if (roster.gunnerySkill != 1u) {
        hashUint64(hash, 0x47554e4e45525931ull);
        hashUint64(hash, roster.gunnerySkill);
    }
}

void hashOriginalAiMotionRuntimeState(
    uint64_t& hash,
    const BattleOriginalAiMotionRuntimeState& state) {
    if (!state.initialized) {
        return;
    }
    hashUint64(hash, 0x41494d4full);
    hashString(hash, state.provenance);
    hashUint64(hash, state.liveObjectSlot);
    hashInt64(hash, state.sideWord);
    hashInt64(hash, state.numericSideModeWord);
    for (uint8_t byte : state.controlRecordBytes) {
        hashUint64(hash, byte);
    }
    hashInt64(hash, state.rawX);
    hashInt64(hash, state.rawY);
    hashInt64(hash, state.rawZ);
    hashInt64(hash, state.pitch);
    hashInt64(hash, state.roll);
    hashInt64(hash, state.heading);
    hashInt64(hash, state.controlWord30);
    hashInt64(hash, state.controlWord32);
    hashInt64(hash, state.controlWord34);
    hashInt64(hash, state.driftWord36);
    hashInt64(hash, state.driftWord38);
    hashInt64(hash, state.driftWord3a);
    hashInt64(hash, state.rawSpeedWord3c);
    hashUint64(hash, state.slotProbeWord);
    hashUint64(hash, state.sceneCacheValid ? 1ull : 0ull);
    if (state.sceneCacheValid) {
        hashUint64(hash, state.sceneCacheObjectIndex);
        hashUint64(hash, state.sceneCacheSubrecordIndex);
    }
    hashUint64(hash, state.worldPositionBindingExact ? 1ull : 0ull);
    hashUint64(hash, state.firstMovementUpdateAttempted ? 1ull : 0ull);
    hashUint64(hash, state.firstMovementUpdateCommitted ? 1ull : 0ull);
    hashUint64(
        hash,
        static_cast<uint64_t>(state.firstMovementUpdateResult));
    hashUint64(hash, state.firstMovementTargetEntityId.value);
    hashUint64(hash, state.firstMovementTargetLiveObjectSlot);
    hashUint64(hash, state.firstMovementRelationWord);
    hashUint64(hash, state.firstMovementRelationSampled ? 1ull : 0ull);
    hashUint64(hash, state.firstMovementRelationSampleCount);
    hashInt64(hash, state.firstMovementMoverAggregateWord16);
    hashInt64(hash, state.firstMovementTargetAggregateWord16);
    hashUint64(
        hash,
        state.firstMovementSelection50eeIndependent ? 1ull : 0ull);
    hashUint64(hash, state.firstMovementDecisionTickIndex);
    hashUint64(hash, state.movementUpdateAttemptCount);
    hashUint64(
        hash,
        static_cast<uint64_t>(state.lastMovementUpdateResult));
    hashUint64(hash, state.lastMovementTargetEntityId.value);
    hashUint64(hash, state.lastMovementTargetLiveObjectSlot);
    hashUint64(hash, state.lastMovementRelationWord);
    hashUint64(hash, state.lastMovementRelationSampled ? 1ull : 0ull);
    hashUint64(hash, state.lastMovementRelationSampleCount);
    hashInt64(hash, state.lastMovementMoverAggregateWord16);
    hashInt64(hash, state.lastMovementTargetAggregateWord16);
    hashUint64(
        hash,
        state.lastMovementSelection50eeIndependent ? 1ull : 0ull);
    hashUint64(hash, state.lastMovementDecisionTickIndex);
    hashUint64(hash, state.lastMovementPostStepExact ? 1ull : 0ull);
    hashUint64(hash, state.lastMovementPostStepActionCode);
    hashUint64(hash, state.acceptedPoseCommitCount);
    hashUint64(hash, state.lastPoseCommitTickIndex);
}

int64_t quantize(double value) {
    return static_cast<int64_t>(std::llround(value * 1000.0));
}

void hashTransform(uint64_t& hash, const Transform& transform);

void hashEnemyAiDiagnostics(uint64_t& hash, const CombatantSnapshot& combatant) {
    if (combatant.enemyAiState == BattleEnemyAiState::None &&
        combatant.enemyAiTargetKind == BattleEnemyAiTargetKind::None &&
        !isValid(combatant.enemyAiTargetEntityId)) {
        return;
    }
    hashUint64(hash, 0x41494447ull);
    hashUint64(hash, static_cast<uint64_t>(combatant.enemyAiState));
    hashUint64(hash, static_cast<uint64_t>(combatant.enemyAiTargetKind));
    hashUint64(hash, combatant.enemyAiTargetEntityId.value);
    hashTransform(hash, combatant.enemyAiTargetTransform);
    hashInt64(hash, quantize(combatant.enemyAiTargetDistance));
}

void hashReplacementAiState(
    uint64_t& hash,
    const BattleReplacementAiState& state) {
    if (!state.active) {
        return;
    }
    hashUint64(hash, 0x504831345245504cull);
    hashUint64(hash, state.active ? 1ull : 0ull);
    hashUint64(hash, static_cast<uint64_t>(state.missionSlice));
    hashUint64(hash, state.decisionSequence);
    hashUint64(hash, state.lastDecisionTickIndex);
    hashUint64(hash, state.approachSlot);
    hashUint64(hash, state.approachSlotCount);
    hashTransform(hash, state.missionAnchor);
    hashTransform(hash, state.movementDestination);
    hashUint64(hash, static_cast<uint64_t>(state.targetKind));
    hashUint64(hash, state.combatTargetEntityId.value);
    hashUint64(hash, state.selectedWeaponInstanceId);
    hashInt64(hash, quantize(state.desiredCombatDistance));
    hashUint64(hash, static_cast<uint64_t>(state.navMode));
    hashUint64(hash, state.stuckDecisionCount);
    hashUint64(hash, state.detourDecisionsRemaining);
    hashUint64(hash, state.detourAttempts);
    hashInt64(hash, quantize(state.lastDestinationDistance));
    hashTransform(hash, state.navigationWaypoint);
    hashUint64(hash, state.navigationWaypointActive ? 1ull : 0ull);
    hashUint64(hash, state.pathReplanCount);
    hashUint64(hash, state.pathFailureCount);
    hashUint64(hash, state.nextPathRetryTickIndex);
    hashUint64(hash, state.lastMoveRejected ? 1ull : 0ull);
    hashUint64(hash, state.friendlyLaneBlocked ? 1ull : 0ull);
    hashUint64(hash, state.terrainLineOfSightBlocked ? 1ull : 0ull);
    hashUint64(hash, state.aimAligned ? 1ull : 0ull);
    hashInt64(hash, quantize(state.lastAimErrorRadians));
    hashUint64(hash, state.nextFireDecisionTickIndex);
    hashUint64(hash, state.targetLockUntilTickIndex);
    hashUint64(hash, state.nextTorsoTurnTickIndex);
    hashUint64(hash, state.proximityThreatOverride ? 1ull : 0ull);
    hashUint64(hash, state.aimRecoveryActive ? 1ull : 0ull);
    hashUint64(hash, state.combatOrbitActive ? 1ull : 0ull);
    hashInt64(hash, state.orbitDirection);
    hashUint64(hash, state.orbitDirectionHoldUntilTickIndex);
    hashTransform(hash, state.combatOrbitDestination);
    hashUint64(hash, state.arrivalReached ? 1ull : 0ull);
    hashUint64(hash, state.arrivalTickIndex);
    hashUint64(hash, state.missionTriggered ? 1ull : 0ull);
    hashUint64(hash, state.retaliationTargetEntityId.value);
    hashUint64(hash, state.retaliationUntilTickIndex);
    hashUint64(hash, state.retreating ? 1ull : 0ull);
    hashUint64(hash, state.retreatThreatEntityId.value);
    hashTransform(hash, state.retreatDestination);
}

void hashRetrievalRuntimeState(
    uint64_t& hash,
    const BattleRetrievalRuntimeState& state) {
    if (!state.active) {
        return;
    }
    hashUint64(hash, 0x5048313452545256ull);
    hashUint64(hash, state.active ? 1ull : 0ull);
    hashUint64(hash, static_cast<uint64_t>(state.phase));
    hashString(hash, state.provenance);
    hashTransform(hash, state.contactAnchor);
    hashUint64(hash, state.contactEntityId.value);
    hashInt64(hash, quantize(state.contactRadius));
    hashUint64(hash, state.contactTickIndex);
}

void hashOriginalAiDecision(
    uint64_t& hash,
    const BattleOriginalAiDecisionDiagnostic& decision) {
    if (!decision.valid) {
        return;
    }
    hashUint64(hash, 0x4f52414944454331ull);
    hashUint64(hash, decision.sequence);
    hashUint64(hash, decision.originalUpdateCount);
    hashUint64(hash, decision.decisionTickIndex);
    hashUint64(hash, decision.fireTickIndex);
    hashUint64(hash, decision.shooterEntityId.value);
    hashUint64(hash, static_cast<uint64_t>(decision.targetKind));
    hashUint64(hash, decision.targetEntityId.value);
    hashUint64(hash, decision.weaponInstanceId);
    hashUint64(hash, static_cast<uint64_t>(decision.result));
    hashInt64(hash, quantize(decision.targetDistance));
    hashUint64(hash, decision.readyFunctionalWeaponCount);
    hashUint64(hash, decision.maximumReadyRangeWord);
    hashInt64(hash, quantize(decision.targetSearchRadius));
    hashUint64(hash, decision.targetAcquisitionRangePassed ? 1ull : 0ull);
    hashUint64(
        hash,
        decision.targetSelectionCenterlineSubsetProven ? 1ull : 0ull);
    hashUint64(
        hash,
        decision.targetSelectionPlanarFixedAngleProven ? 1ull : 0ull);
    hashUint64(
        hash,
        decision.targetSelectionZeroPitchVerticalFixedAngleProven
            ? 1ull
            : 0ull);
    hashUint64(
        hash,
        decision.originalApproximateDistanceProven ? 1ull : 0ull);
    hashInt64(hash, decision.shooterFixedHeading);
    hashInt64(hash, decision.selectedCandidateFixedHeading);
    hashInt64(hash, decision.selectedCandidateYawDelta);
    hashInt64(hash, decision.selectedCandidateHalfWidth);
    hashInt64(hash, decision.selectedCandidateVerticalAngle);
    hashInt64(hash, decision.selectedCandidateVerticalDelta);
    hashInt64(hash, decision.selectedCandidateVerticalLimit);
    hashUint64(
        hash,
        decision.selectedCandidateVerticalGatePassed ? 1ull : 0ull);
    hashUint64(hash, decision.blockingCombatantEntityId.value);
    hashInt64(hash, quantize(decision.blockingCombatantDistance));
    hashUint64(hash, decision.blockingCandidateFlags);
    hashUint64(hash, decision.friendlyFireLaneSuppressed ? 1ull : 0ull);
    hashInt64(hash, quantize(decision.minimumRange));
    hashInt64(hash, quantize(decision.maximumRange));
    hashUint64(hash, decision.strictRangeGatePassed ? 1ull : 0ull);
    hashInt64(hash, decision.weaponScore);
    hashUint64(hash, decision.rangeBucket);
    hashInt64(hash, decision.targetNumber);
    hashUint64(hash, decision.hitRoll);
    hashUint64(hash, decision.hit ? 1ull : 0ull);
    hashUint64(hash, static_cast<uint64_t>(decision.attackAspect));
    hashUint64(hash, decision.locationRollIndex);
    hashUint64(hash, static_cast<uint64_t>(decision.armorSection));
    hashUint64(
        hash,
        decision.automaticCallerAndLocationTableProven ? 1ull : 0ull);
    hashUint64(hash, decision.originalRandomSequenceProven ? 1ull : 0ull);
    hashString(hash, decision.provenance);
}

void hashWeaponDiagnostics(uint64_t& hash, const CombatantSnapshot& combatant) {
    if (combatant.weaponCooldownTicksRemaining == 0 &&
        combatant.weaponShotsFired == 0 &&
        combatant.weaponHitsLanded == 0 &&
        !isValid(combatant.lastWeaponTargetEntityId) &&
        combatant.lastWeaponTargetSystem == BattleMechSystemRole::Unknown &&
        !isValid(combatant.lastDamageSourceEntityId) &&
        combatant.weapons.empty()) {
        return;
    }
    hashUint64(hash, 0x5745504eull);
    hashUint64(hash, combatant.weaponCooldownTicksRemaining);
    hashUint64(hash, combatant.weaponShotsFired);
    hashUint64(hash, combatant.weaponHitsLanded);
    hashUint64(hash, combatant.lastWeaponTargetEntityId.value);
    hashUint64(hash, static_cast<uint64_t>(combatant.lastWeaponTargetSystem));
    hashUint64(hash, combatant.lastDamageSourceEntityId.value);
    hashUint64(hash, combatant.selectedWeaponInstanceId);
    hashUint64(hash, combatant.lastFireRequestTickIndex);
    hashUint64(hash, combatant.lastFireRequestWeaponInstanceId);
    hashUint64(hash, combatant.lastFireRequestAccepted ? 1ull : 0ull);
    hashUint64(hash, static_cast<uint64_t>(combatant.weapons.size()));
    for (const BattleWeaponInstanceState& weapon : combatant.weapons) {
        hashUint64(hash, weapon.weaponInstanceId);
        hashUint64(hash, weapon.slotIndex);
        hashString(hash, weapon.weaponTypeId);
        hashString(hash, weapon.displayName);
        hashString(hash, weapon.locationId);
        hashUint64(hash, static_cast<unsigned char>(weapon.displayRangeClass));
        hashUint64(hash, weapon.condition);
        hashUint64(hash, weapon.functional ? 1ull : 0ull);
        hashUint64(hash, static_cast<uint64_t>(weapon.ammunitionOwnership));
        hashInt64(hash, weapon.ammunitionPoolIndex);
        hashUint64(hash, weapon.ammunitionStateKnown ? 1ull : 0ull);
        hashInt64(hash, weapon.ammunitionRemaining);
        hashUint64(hash, weapon.cooldownTicksRemaining);
        hashUint64(hash, weapon.cooldownTicksOnFire);
        hashUint64(hash, weapon.originalCooldownUpdateCount);
        hashUint64(hash, weapon.originalCooldownValueProven ? 1ull : 0ull);
        hashUint64(hash, weapon.originalCooldownCadenceProven ? 1ull : 0ull);
        hashUint64(hash, weapon.originalDamage);
        hashUint64(hash, weapon.originalDamageValueProven ? 1ull : 0ull);
        hashUint64(hash, weapon.originalHeat);
        hashUint64(hash, weapon.originalHeatValueProven ? 1ull : 0ull);
        hashUint64(hash, weapon.originalCriticalWeight);
        hashUint64(hash, weapon.originalCriticalWeightProven ? 1ull : 0ull);
        for (uint16_t rangeWord : weapon.originalRangeWords) {
            hashUint64(hash, rangeWord);
        }
        hashUint64(hash, weapon.originalRangeValueProven ? 1ull : 0ull);
        hashInt64(hash, quantize(weapon.maximumRange));
        hashUint64(hash, static_cast<uint64_t>(weapon.deliveryMode));
        hashInt64(hash, weapon.originalProjectileTypeIndex);
        hashUint64(hash, weapon.originalProjectileSpeedRaw);
        hashUint64(hash, weapon.originalProjectileLifetimeUpdates);
        hashUint64(hash, weapon.originalProjectileDamageClass);
        hashUint64(hash, weapon.originalProjectileVisualClass);
        hashUint64(
            hash,
            weapon.originalProjectileDefinitionProven ? 1ull : 0ull);
        hashUint64(hash, static_cast<uint64_t>(weapon.readiness));
        hashUint64(hash, weapon.selected ? 1ull : 0ull);
        hashUint64(hash, weapon.shotsFired);
    }
}

void hashHeatState(uint64_t& hash, const BattleHeatState& heat) {
    if (!heat.enabled) {
        return;
    }
    hashUint64(hash, 0x48454154ull);
    hashInt64(hash, heat.rawHeat);
    hashInt64(hash, quantize(heat.originalUpdateAccumulator));
    hashUint64(hash, heat.originalUpdateCount);
    hashUint64(hash, heat.reactorShutdown ? 1ull : 0ull);
    hashUint64(hash, heat.reactorShutdownStateChangedTickIndex);
    hashInt64(hash, heat.lastWeaponHeatAdded);
    hashInt64(hash, heat.lastJumpJetHeatAdded);
    hashUint64(hash, heat.lastJumpJetHeatTickIndex);
    hashInt64(hash, heat.lastGroundMovementHeatAdded);
    hashUint64(hash, heat.lastGroundMovementHeatTickIndex);
    hashInt64(hash, heat.lastEngineHeatAdded);
    hashUint64(hash, heat.lastEngineHeatTickIndex);
    hashInt64(hash, heat.lastCoolingApplied);
    hashUint64(hash, heat.lastCoolingTickIndex);
    hashUint64(hash, heat.lastCoolingMultiplier);
    hashUint64(hash, static_cast<uint64_t>(heat.coolingPolicy));
    hashUint64(hash, heat.originalWeaponHeatAdditionProven ? 1ull : 0ull);
    hashUint64(hash, heat.originalJumpJetHeatValuesProven ? 1ull : 0ull);
    hashUint64(hash, heat.jumpJetControlMappingProven ? 1ull : 0ull);
    hashUint64(hash, heat.originalGroundMovementHeatThresholdsProven ? 1ull : 0ull);
    hashUint64(hash, heat.groundMovementSpeedMappingProven ? 1ull : 0ull);
    hashUint64(hash, heat.originalEngineDamageHeatProven ? 1ull : 0ull);
    hashUint64(hash, heat.originalNormalCoolingFormulaProven ? 1ull : 0ull);
    hashUint64(hash, heat.originalArcticDoubleCoolingProven ? 1ull : 0ull);
    hashUint64(hash, heat.originalReactorShutdownThresholdProven ? 1ull : 0ull);
    hashUint64(hash, heat.originalReactorShutdownRecoveryProven ? 1ull : 0ull);
    hashUint64(hash, heat.originalReactorShutdownMessageCadenceProven ? 1ull : 0ull);
    hashUint64(hash, heat.originalUpdateCadenceProven ? 1ull : 0ull);
}

void hashMajorSystemRuntimeState(
    uint64_t& hash,
    const BattleMajorSystemRuntimeState& systems) {
    if (!systems.enabled) {
        return;
    }
    hashUint64(hash, 0x5347454c52554e31ull);
    hashUint64(hash, static_cast<uint64_t>(systems.sensors));
    hashUint64(hash, static_cast<uint64_t>(systems.gyros));
    hashUint64(hash, static_cast<uint64_t>(systems.engine));
    hashUint64(hash, static_cast<uint64_t>(systems.lifeSupport));
    hashUint64(hash, systems.crosshairVisible ? 1ull : 0ull);
    hashUint64(hash, systems.targetingAvailable ? 1ull : 0ull);
    hashUint64(hash, systems.topographicMapVisible ? 1ull : 0ull);
    hashUint64(hash, systems.radarContactsVisible ? 1ull : 0ull);
    hashInt64(hash, quantize(systems.gyroMovementScale));
    hashUint64(hash, systems.movementBlocked ? 1ull : 0ull);
    hashUint64(hash, systems.engineShutdown ? 1ull : 0ull);
    hashUint64(hash, systems.lifeSupportFailure ? 1ull : 0ull);
    hashInt64(hash, systems.engineHeatPerOriginalUpdate);
    hashUint64(hash, systems.sensorBlinkCadenceProven ? 1ull : 0ull);
    hashUint64(hash, systems.gyroDamageScaleProven ? 1ull : 0ull);
    hashUint64(hash, systems.destroyedSystemEffectsProven ? 1ull : 0ull);
}

void hashMajorSystemWarningEvent(
    uint64_t& hash,
    const BattleMajorSystemWarningEvent& event) {
    if (!event.valid) {
        return;
    }
    hashUint64(hash, 0x5347454c5741524eull);
    hashUint64(hash, event.sequence);
    hashUint64(hash, event.tickIndex);
    hashUint64(hash, static_cast<uint64_t>(event.system));
    hashUint64(hash, static_cast<uint64_t>(event.previousStatus));
    hashUint64(hash, static_cast<uint64_t>(event.currentStatus));
}

void hashShotDiagnostic(uint64_t& hash, const BattleShotDiagnostic& shot) {
    if (!shot.valid) {
        return;
    }
    hashUint64(hash, 0x53484f54ull);
    hashUint64(hash, shot.sequence);
    hashUint64(hash, shot.tickIndex);
    hashUint64(hash, shot.shooterEntityId.value);
    hashUint64(hash, shot.weaponInstanceId);
    hashUint64(hash, static_cast<uint64_t>(shot.targetKind));
    hashUint64(hash, shot.targetEntityId.value);
    hashUint64(hash, shot.targetProjectileId);
    hashUint64(hash, static_cast<uint64_t>(shot.result));
    hashInt64(hash, quantize(shot.targetDistance));
    hashInt64(hash, quantize(shot.maximumRange));
    hashUint64(hash, shot.rangeGatePassed ? 1ull : 0ull);
    hashUint64(hash, shot.replacementFireSolution ? 1ull : 0ull);
    hashUint64(
        hash, shot.replacementTerrainLineOfSightClear ? 1ull : 0ull);
    hashUint64(hash, shot.replacementAimAligned ? 1ull : 0ull);
    hashUint64(hash, shot.replacementGunnerySkill);
    hashInt64(hash, shot.replacementTargetNumber);
    hashUint64(hash, shot.replacementHitRoll);
    hashOriginalAiDecision(hash, shot.originalAiDecision);
    hashUint64(hash, shot.hit ? 1ull : 0ull);
    hashUint64(hash, static_cast<uint64_t>(shot.hitLocation));
    hashUint64(hash, shot.hitLocationProven ? 1ull : 0ull);
    hashUint64(hash, static_cast<uint64_t>(shot.armorSection));
    for (const BattleHitPoint* point : {
             &shot.aimOrigin, &shot.aimDirection, &shot.impactPoint}) {
        hashInt64(hash, quantize(point->x));
        hashInt64(hash, quantize(point->y));
        hashInt64(hash, quantize(point->z));
    }
    hashInt64(hash, shot.hitComponentId);
    hashUint64(hash, shot.hitGeometryFingerprint);
    hashUint64(hash, shot.aimProjectionProven ? 1ull : 0ull);
    hashUint64(hash, static_cast<uint64_t>(shot.deliveryState));
    hashUint64(hash, shot.projectileId);
    hashUint64(hash, shot.impactTickIndex);
    hashUint64(hash, shot.impactDamage);
    hashUint64(hash, shot.missileClusterRollIndex);
    hashUint64(hash, shot.missileClusterTableProven ? 1ull : 0ull);
    hashUint64(hash, shot.projectileMotionTimingProven ? 1ull : 0ull);
    hashUint64(hash, shot.originalDamage);
    hashUint64(hash, shot.originalDamageValueProven ? 1ull : 0ull);
    hashUint64(hash, static_cast<uint64_t>(shot.runtimeEffect));
    hashUint64(hash, static_cast<uint64_t>(shot.affectedSystem));
    hashInt64(hash, shot.damageBefore);
    hashInt64(hash, shot.damageApplied);
    hashInt64(hash, shot.damageAfter);
    hashInt64(hash, shot.armorBefore);
    hashInt64(hash, shot.armorDamageApplied);
    hashInt64(hash, shot.armorAfter);
    hashInt64(hash, shot.internalBefore);
    hashInt64(hash, shot.internalDamageApplied);
    hashInt64(hash, shot.internalAfter);
    hashUint64(hash, shot.criticalResolutionAttempted ? 1ull : 0ull);
    hashUint64(hash, shot.criticalRoll);
    hashUint64(hash, shot.criticalAttemptsRequested);
    hashUint64(hash, shot.criticalHitsApplied);
    hashUint64(hash, shot.criticalFallbackCount);
    hashUint64(hash, static_cast<uint64_t>(shot.lastCriticalHitKind));
    hashUint64(hash, shot.lastCriticalHitIndex);
    hashUint64(hash, static_cast<uint64_t>(shot.lastCriticalLocation));
    hashUint64(hash, shot.criticalCandidateSelectionPending ? 1ull : 0ull);
    hashUint64(hash, shot.originalCriticalRulesProven ? 1ull : 0ull);
    hashUint64(hash, shot.originalCriticalRandomSequenceProven ? 1ull : 0ull);
    hashUint64(hash, shot.criticalResolutionPending ? 1ull : 0ull);
    hashInt64(hash, shot.unresolvedCriticalDamage);
    hashString(hash, shot.provenance);
}

void hashImpactEvent(uint64_t& hash, const BattleImpactEvent& event) {
    if (!event.valid) {
        return;
    }
    hashUint64(hash, 0x494d50414354ull);
    hashUint64(hash, event.shotSequence);
    hashUint64(hash, event.impactTickIndex);
    hashUint64(hash, event.shooterEntityId.value);
    hashUint64(hash, event.weaponInstanceId);
    hashUint64(hash, static_cast<uint64_t>(event.targetKind));
    hashUint64(hash, event.targetEntityId.value);
    hashUint64(hash, event.projectileId);
    hashUint64(hash, static_cast<uint64_t>(event.deliveryState));
    hashUint64(hash, static_cast<uint64_t>(event.visualSequence));
    hashInt64(hash, quantize(event.point.x));
    hashInt64(hash, quantize(event.point.y));
    hashInt64(hash, quantize(event.point.z));
}

void hashProjectileState(
    uint64_t& hash,
    const BattleProjectileState& projectile) {
    if (!projectile.valid) {
        return;
    }
    hashUint64(hash, 0x50524f4aull);
    hashUint64(hash, projectile.projectileId);
    hashUint64(hash, projectile.launchTickIndex);
    hashUint64(hash, projectile.shooterEntityId.value);
    hashUint64(hash, projectile.weaponInstanceId);
    hashString(hash, projectile.weaponTypeId);
    hashTransform(hash, projectile.launchShooterTransform);
    hashInt64(hash, quantize(projectile.launchTorsoYawRadians));
    hashInt64(hash, quantize(projectile.launchModelMount.x));
    hashInt64(hash, quantize(projectile.launchModelMount.y));
    hashInt64(hash, quantize(projectile.launchModelMount.z));
    hashUint64(hash, projectile.launchModelMountValid ? 1ull : 0ull);
    hashUint64(hash, projectile.launchModelMountPolicyProven ? 1ull : 0ull);
    hashUint64(hash, static_cast<uint64_t>(projectile.targetKind));
    hashUint64(hash, projectile.targetEntityId.value);
    hashUint64(hash, projectile.targetBound ? 1ull : 0ull);
    for (const BattleHitPoint* point : {
             &projectile.previousPosition,
             &projectile.position,
             &projectile.direction,
             &projectile.targetLocalPoint}) {
        hashInt64(hash, quantize(point->x));
        hashInt64(hash, quantize(point->y));
        hashInt64(hash, quantize(point->z));
    }
    hashInt64(hash, projectile.originalProjectileTypeIndex);
    hashUint64(hash, projectile.originalSpeedRaw);
    hashUint64(hash, projectile.originalLifetimeUpdates);
    hashUint64(hash, projectile.originalLifetimeUpdatesRemaining);
    hashUint64(hash, projectile.originalDamageClass);
    hashUint64(hash, projectile.originalVisualClass);
    hashInt64(hash, quantize(projectile.launchForwardOffset));
    hashUint64(hash, projectile.launchForwardOffsetProven ? 1ull : 0ull);
    hashInt64(hash, quantize(projectile.movementSubstepAccumulator));
    hashInt64(hash, quantize(projectile.lifetimeUpdateAccumulator));
    hashUint64(hash, projectile.originalTargetTrackingProven ? 1ull : 0ull);
    hashUint64(hash, projectile.motionScaleProven ? 1ull : 0ull);
    hashUint64(hash, projectile.collisionGeometryFingerprint);
    hashUint64(hash, projectile.collisionGeometryProven ? 1ull : 0ull);
    hashUint64(hash, projectile.laserInterceptible ? 1ull : 0ull);
    hashShotDiagnostic(hash, projectile.pendingShot);
    hashString(hash, projectile.provenance);
}

void hashCollisionDiagnostic(
    uint64_t& hash,
    const BattleCollisionDiagnostic& collision) {
    if (!collision.valid) {
        return;
    }
    hashUint64(hash, 0x434f4c4cull);
    hashUint64(hash, collision.sequence);
    hashUint64(hash, collision.tickIndex);
    hashUint64(hash, static_cast<uint64_t>(collision.kind));
    hashUint64(hash, collision.entityId.value);
    hashUint64(hash, collision.otherEntityId.value);
    hashInt64(hash, collision.terrainGridX);
    hashInt64(hash, collision.terrainGridZ);
    hashUint64(hash, collision.terrainObstacleId);
    hashUint64(hash, collision.terrainRecordIndex);
    hashInt64(hash, quantize(collision.normalX));
    hashInt64(hash, quantize(collision.normalZ));
    hashInt64(hash, quantize(collision.penetration));
    hashInt64(hash, quantize(collision.displacement));
    hashInt64(hash, quantize(collision.speedBefore));
    hashInt64(hash, quantize(collision.speedAfter));
    hashUint64(hash, static_cast<uint64_t>(collision.runtimeEffect));
    hashUint64(hash, collision.damageSemanticsProven ? 1ull : 0ull);
    hashInt64(hash, collision.damageApplied);
    hashString(hash, collision.provenance);
}

bool containsComponentId(const std::vector<int>& componentIds, int componentId) {
    return std::find(componentIds.begin(), componentIds.end(), componentId) != componentIds.end();
}

void addUniqueComponentId(std::vector<int>& componentIds, int componentId) {
    if (!containsComponentId(componentIds, componentId)) {
        componentIds.push_back(componentId);
    }
}

bool labelContains(const std::string& label, const char* token) {
    return label.find(token) != std::string::npos;
}

size_t armorSectionIndex(mech3d::MechArmorSectionId section) {
    return static_cast<size_t>(section);
}

size_t internalSectionIndex(mech3d::MechInternalSectionId section) {
    return static_cast<size_t>(section);
}

mech3d::MechInternalSectionId internalSectionForArmor(
    mech3d::MechArmorSectionId section) {
    switch (section) {
    case mech3d::MechArmorSectionId::RightArm:
        return mech3d::MechInternalSectionId::RightArm;
    case mech3d::MechArmorSectionId::LeftArm:
        return mech3d::MechInternalSectionId::LeftArm;
    case mech3d::MechArmorSectionId::RightLeg:
        return mech3d::MechInternalSectionId::RightLeg;
    case mech3d::MechArmorSectionId::LeftLeg:
        return mech3d::MechInternalSectionId::LeftLeg;
    case mech3d::MechArmorSectionId::Head:
        return mech3d::MechInternalSectionId::Head;
    case mech3d::MechArmorSectionId::CenterTorso:
    case mech3d::MechArmorSectionId::CenterRear:
        return mech3d::MechInternalSectionId::CenterTorso;
    case mech3d::MechArmorSectionId::RightTorso:
        return mech3d::MechInternalSectionId::RightTorso;
    case mech3d::MechArmorSectionId::LeftTorso:
        return mech3d::MechInternalSectionId::LeftTorso;
    case mech3d::MechArmorSectionId::Count:
        break;
    }
    return mech3d::MechInternalSectionId::CenterTorso;
}

size_t criticalComponentIndex(mech3d::MechCriticalComponentId component) {
    return static_cast<size_t>(component);
}

int systemDamageForRole(
    const mech3d::MechDetailedDamageState& damage,
    BattleMechSystemRole role) {
    const auto armorDamage = [&damage](mech3d::MechArmorSectionId section) {
        return damage.armorSections[armorSectionIndex(section)].battleDamage;
    };
    const auto criticalDamage = [&damage](mech3d::MechCriticalComponentId component) {
        return damage.criticalComponents[criticalComponentIndex(component)].battleDamage;
    };
    switch (role) {
    case BattleMechSystemRole::Core:
        return armorDamage(mech3d::MechArmorSectionId::CenterTorso);
    case BattleMechSystemRole::Cockpit:
        return armorDamage(mech3d::MechArmorSectionId::Head);
    case BattleMechSystemRole::Mobility:
        return std::max(
            armorDamage(mech3d::MechArmorSectionId::LeftLeg),
            armorDamage(mech3d::MechArmorSectionId::RightLeg));
    case BattleMechSystemRole::Weapons:
        return std::max(
            criticalDamage(mech3d::MechCriticalComponentId::LeftArmActuator),
            criticalDamage(mech3d::MechCriticalComponentId::RightArmActuator));
    case BattleMechSystemRole::JumpJets:
        return criticalDamage(mech3d::MechCriticalComponentId::JumpJets);
    case BattleMechSystemRole::Unknown:
        return 0;
    }
    return 0;
}

int& mutableSystemDamageForRole(
    mech3d::MechDetailedDamageState& damage,
    BattleMechSystemRole role) {
    switch (role) {
    case BattleMechSystemRole::Core:
        return damage.armorSections[armorSectionIndex(
            mech3d::MechArmorSectionId::CenterTorso)].battleDamage;
    case BattleMechSystemRole::Cockpit:
        return damage.armorSections[armorSectionIndex(
            mech3d::MechArmorSectionId::Head)].battleDamage;
    case BattleMechSystemRole::Mobility:
        return damage.armorSections[armorSectionIndex(
            mech3d::MechArmorSectionId::RightLeg)].battleDamage;
    case BattleMechSystemRole::Weapons:
        return damage.criticalComponents[criticalComponentIndex(
            mech3d::MechCriticalComponentId::RightArmActuator)].battleDamage;
    case BattleMechSystemRole::JumpJets:
        return damage.criticalComponents[criticalComponentIndex(
            mech3d::MechCriticalComponentId::JumpJets)].battleDamage;
    case BattleMechSystemRole::Unknown:
        return damage.armorSections[armorSectionIndex(
            mech3d::MechArmorSectionId::CenterTorso)].battleDamage;
    }
    return damage.armorSections[armorSectionIndex(
        mech3d::MechArmorSectionId::CenterTorso)].battleDamage;
}

void hashDetailedDamageState(
    uint64_t& hash,
    const mech3d::MechDetailedDamageState& damage) {
    if (!damage.valid) {
        return;
    }
    hashUint64(hash, 0x444d4745ull);
    hashString(hash, damage.provenance);
    for (const auto& section : damage.armorSections) {
        hashUint64(hash, static_cast<uint64_t>(section.section));
        hashUint64(hash, section.entryDamageLevel);
        hashUint64(hash, section.armorMaximum);
        hashUint64(hash, section.armorRemaining);
        hashInt64(hash, section.battleDamage);
    }
    for (const auto& section : damage.internalSections) {
        hashUint64(hash, static_cast<uint64_t>(section.section));
        hashUint64(hash, section.structureMaximum);
        hashUint64(hash, section.structureRemaining);
        hashInt64(hash, section.battleDamage);
    }
    for (const auto& component : damage.criticalComponents) {
        hashUint64(hash, static_cast<uint64_t>(component.component));
        hashUint64(hash, component.condition);
        hashUint64(hash, component.functional ? 1ull : 0ull);
        hashUint64(hash, component.workingCount);
        hashUint64(hash, component.totalCount);
        hashInt64(hash, component.battleDamage);
    }
    for (const auto& weapon : damage.installedWeapons) {
        hashUint64(hash, weapon.slotIndex);
        hashUint64(hash, weapon.condition);
        hashUint64(hash, weapon.functional ? 1ull : 0ull);
        hashInt64(hash, weapon.battleDamage);
    }
    hashUint64(hash, damage.originalCockpitCriticalFlag ? 1ull : 0ull);
    const bool hasAmmunitionCriticalHit = std::any_of(
        damage.ammunitionCriticalHits.begin(),
        damage.ammunitionCriticalHits.end(),
        [](uint64_t count) { return count != 0u; });
    if (damage.originalJumpJetCriticalDestroyedMask != 0u ||
        hasAmmunitionCriticalHit) {
        hashUint64(hash, 0x4352495453554646ull);
        hashUint64(hash, damage.originalJumpJetCriticalDestroyedMask);
        for (uint64_t count : damage.ammunitionCriticalHits) {
            hashUint64(hash, count);
        }
    }
    hashUint64(hash, damage.criticalAttempts);
    hashUint64(hash, damage.criticalHitsApplied);
    hashUint64(hash, damage.unresolvedCriticalSelections);
    hashUint64(hash, damage.criticalResolutionPending ? 1ull : 0ull);
    hashInt64(hash, damage.unresolvedCriticalDamage);
}

void catalogSystemComponentIds(
    const Combatant& combatant,
    std::vector<int>& coreComponentIds,
    std::vector<int>& cockpitComponentIds,
    std::vector<int>& mobilityComponentIds,
    std::vector<int>& weaponComponentIds) {
    const int cockpitComponentId = mech3d::catalogCockpitComponentId(combatant.mechPresetId);
    for (const mech3d::MechComponentDamageRule& rule : combatant.damageRules) {
        if (rule.componentId == cockpitComponentId || labelContains(rule.debugLabel, "cockpit")) {
            addUniqueComponentId(cockpitComponentIds, rule.componentId);
        } else if (labelContains(rule.debugLabel, "leg")) {
            addUniqueComponentId(mobilityComponentIds, rule.componentId);
        } else if (labelContains(rule.debugLabel, "torso")) {
            addUniqueComponentId(coreComponentIds, rule.componentId);
        } else if (labelContains(rule.debugLabel, "arm")) {
            addUniqueComponentId(weaponComponentIds, rule.componentId);
        } else if (rule.destroyedAction == mech3d::MechComponentDestroyedAction::DestroyMech) {
            addUniqueComponentId(coreComponentIds, rule.componentId);
        } else {
            addUniqueComponentId(weaponComponentIds, rule.componentId);
        }
    }
}

std::vector<int> catalogSystemComponentIdsForRole(
    const Combatant& combatant,
    BattleMechSystemRole role) {
    std::vector<int> coreComponentIds;
    std::vector<int> cockpitComponentIds;
    std::vector<int> mobilityComponentIds;
    std::vector<int> weaponComponentIds;
    catalogSystemComponentIds(
        combatant,
        coreComponentIds,
        cockpitComponentIds,
        mobilityComponentIds,
        weaponComponentIds);
    switch (role) {
    case BattleMechSystemRole::Core:
        return coreComponentIds;
    case BattleMechSystemRole::Cockpit:
        return cockpitComponentIds;
    case BattleMechSystemRole::Mobility:
        return mobilityComponentIds;
    case BattleMechSystemRole::Weapons:
        return weaponComponentIds;
    case BattleMechSystemRole::JumpJets:
    case BattleMechSystemRole::Unknown:
        return {};
    }
    return {};
}

BattleMechSystemStatus sourceComponentStatus(
    const mech3d::ResolvedMechRuntimeState& resolved,
    const std::vector<int>& componentIds) {
    size_t destroyedCount = 0;
    bool impaired = false;
    for (int componentId : componentIds) {
        if (containsComponentId(resolved.destroyedComponentIds, componentId)) {
            ++destroyedCount;
            impaired = true;
        }
        impaired = impaired ||
                   containsComponentId(resolved.disabledComponentIds, componentId) ||
                   containsComponentId(resolved.hiddenComponentIds, componentId);
    }
    if (!componentIds.empty() && destroyedCount == componentIds.size()) {
        return BattleMechSystemStatus::Destroyed;
    }
    return impaired ? BattleMechSystemStatus::Degraded : BattleMechSystemStatus::Online;
}

BattleMechSystemSnapshot makeMechSystemSnapshot(
    std::string systemId,
    std::string label,
    BattleMechSystemRole role,
    std::vector<int> componentIds,
    BattleMechSystemStatus status,
    int damage,
    int maxDamage,
    bool movementCritical,
    bool combatCritical) {
    BattleMechSystemSnapshot system;
    system.systemId = std::move(systemId);
    system.label = std::move(label);
    system.role = role;
    system.status = status;
    system.damage = damage;
    system.maxDamage = maxDamage;
    system.sourceComponentIds = std::move(componentIds);
    system.movementCritical = movementCritical;
    system.combatCritical = combatCritical;
    return system;
}

BattleMechSystemsSnapshot makeMechSystemsSnapshot(
    const BattleStartParams& params,
    const Combatant& combatant,
    const mech3d::ResolvedMechRuntimeState& resolved) {
    BattleMechSystemsSnapshot snapshot;
    snapshot.valid = true;
    snapshot.provenance = "catalog_component_runtime_projection_v0";
    snapshot.wholeMechDestroyed = resolved.mechDestroyed;

    std::vector<int> coreComponentIds;
    std::vector<int> cockpitComponentIds;
    std::vector<int> mobilityComponentIds;
    std::vector<int> weaponComponentIds;
    catalogSystemComponentIds(
        combatant,
        coreComponentIds,
        cockpitComponentIds,
        mobilityComponentIds,
        weaponComponentIds);

    auto addComponentBackedSystem =
        [&params, &snapshot, &resolved, &combatant](std::string systemId,
                                                    std::string label,
                                                    BattleMechSystemRole role,
                                                    std::vector<int> componentIds,
                                                    bool movementCritical,
                                                    bool combatCritical) {
            if (componentIds.empty()) {
                return;
            }
            BattleMechSystemStatus status = sourceComponentStatus(resolved, componentIds);
            const int damage = systemDamageForRole(
                combatant.mechRuntime.detailedDamage,
                role);
            const int maxDamage = params.mechSystemMaxDamage;
            if (maxDamage > 0 && damage >= maxDamage) {
                status = BattleMechSystemStatus::Destroyed;
            } else if (damage > 0 && status == BattleMechSystemStatus::Online) {
                status = BattleMechSystemStatus::Degraded;
            }
            if (resolved.mechDestroyed && movementCritical) {
                status = BattleMechSystemStatus::Destroyed;
            } else if (resolved.mechDestroyed && status == BattleMechSystemStatus::Online) {
                status = BattleMechSystemStatus::Offline;
            }
            snapshot.systems.push_back(makeMechSystemSnapshot(
                std::move(systemId),
                std::move(label),
                role,
                std::move(componentIds),
                status,
                damage,
                maxDamage,
                movementCritical,
                combatCritical));
        };

    addComponentBackedSystem("core", "Torso/Core", BattleMechSystemRole::Core, coreComponentIds, true, true);
    addComponentBackedSystem("cockpit", "Cockpit", BattleMechSystemRole::Cockpit, cockpitComponentIds, false, true);
    addComponentBackedSystem("mobility", "Legs/Mobility", BattleMechSystemRole::Mobility, mobilityComponentIds, true, false);
    addComponentBackedSystem("weapons", "Arms/Weapon Mounts", BattleMechSystemRole::Weapons, weaponComponentIds, false, true);

    if (combatant.jumpCapable) {
        const BattleMechSystemStatus status =
            resolved.mechDestroyed ? BattleMechSystemStatus::Offline : BattleMechSystemStatus::Online;
        snapshot.systems.push_back(makeMechSystemSnapshot(
            "jump_jets",
            "Jump Jets",
            BattleMechSystemRole::JumpJets,
            {},
            status,
            systemDamageForRole(
                combatant.mechRuntime.detailedDamage,
                BattleMechSystemRole::JumpJets),
            params.mechSystemMaxDamage,
            true,
            false));
    }

    auto roleOnline = [&snapshot](BattleMechSystemRole role) {
        return std::any_of(
            snapshot.systems.begin(),
            snapshot.systems.end(),
            [role](const BattleMechSystemSnapshot& system) {
                return system.role == role && system.status == BattleMechSystemStatus::Online;
            });
    };
    snapshot.mobilityOnline = !snapshot.wholeMechDestroyed && roleOnline(BattleMechSystemRole::Mobility);
    snapshot.cockpitOnline = !snapshot.wholeMechDestroyed && roleOnline(BattleMechSystemRole::Cockpit);
    snapshot.weaponsOnline = !snapshot.wholeMechDestroyed && roleOnline(BattleMechSystemRole::Weapons);
    snapshot.jumpJetsOnline = !snapshot.wholeMechDestroyed && roleOnline(BattleMechSystemRole::JumpJets);
    return snapshot;
}

void hashMechSystemsSnapshot(uint64_t& hash, const BattleMechSystemsSnapshot& systems) {
    if (!systems.valid) {
        return;
    }
    hashUint64(hash, 0x4d535953ull);
    hashString(hash, systems.provenance);
    hashUint64(hash, systems.wholeMechDestroyed ? 1ull : 0ull);
    hashUint64(hash, systems.mobilityOnline ? 1ull : 0ull);
    hashUint64(hash, systems.cockpitOnline ? 1ull : 0ull);
    hashUint64(hash, systems.weaponsOnline ? 1ull : 0ull);
    hashUint64(hash, systems.jumpJetsOnline ? 1ull : 0ull);
    hashUint64(hash, static_cast<uint64_t>(systems.systems.size()));
    for (const BattleMechSystemSnapshot& system : systems.systems) {
        hashString(hash, system.systemId);
        hashString(hash, system.label);
        hashUint64(hash, static_cast<uint64_t>(system.role));
        hashUint64(hash, static_cast<uint64_t>(system.status));
        hashInt64(hash, system.damage);
        hashInt64(hash, system.maxDamage);
        hashUint64(hash, system.movementCritical ? 1ull : 0ull);
        hashUint64(hash, system.combatCritical ? 1ull : 0ull);
        hashUint64(hash, static_cast<uint64_t>(system.sourceComponentIds.size()));
        for (int componentId : system.sourceComponentIds) {
            hashInt64(hash, componentId);
        }
    }
}

void hashTransform(uint64_t& hash, const Transform& transform) {
    hashInt64(hash, quantize(transform.x));
    hashInt64(hash, quantize(transform.y));
    hashInt64(hash, quantize(transform.z));
    hashInt64(hash, quantize(transform.headingRadians));
}

void hashSetupSlotMetadata(uint64_t& hash, const BattleSetupSlotMetadata& slot) {
    hashUint64(hash, slot.valid ? 1ull : 0ull);
    hashString(hash, slot.role);
    hashUint64(hash, static_cast<uint64_t>(slot.slotIndex));
    hashTransform(hash, slot.transform);
    hashInt64(hash, quantize(slot.gridX));
    hashInt64(hash, quantize(slot.gridY));
    hashUint64(hash, slot.originalRawPositionProven ? 1ull : 0ull);
    if (slot.originalRawPositionProven) {
        hashInt64(hash, slot.originalRawX);
        hashInt64(hash, slot.originalRawZ);
    }
}

void hashSetupObjectiveMetadata(uint64_t& hash, const BattleSetupObjectiveMetadata& objective) {
    hashUint64(hash, 0x4f424a38ull);
    hashString(hash, objective.provenance);
    hashInt64(hash, objective.sourceSide);
    hashInt64(hash, objective.placementMode);
    hashUint64(hash, objective.bankId);
    hashUint64(hash, static_cast<uint64_t>(objective.coordinateOffset));
    hashInt64(hash, objective.legacyTargetIndex);
    hashInt64(hash, objective.resourceRecordIndex);
    hashTransform(hash, objective.transform);
    hashInt64(hash, quantize(objective.gridX));
    hashInt64(hash, quantize(objective.gridY));
}

void hashSetupMetadata(uint64_t& hash, const BattleSetupMetadata& setup) {
    if (!setup.valid) {
        return;
    }
    hashUint64(hash, 0x53545550ull);
    hashString(hash, setup.provenance);
    hashUint64(hash, static_cast<uint64_t>(setup.scenarioIndex));
    hashUint64(hash, setup.missionSelectorContractProven ? 1ull : 0ull);
    hashUint64(hash, setup.briefingMissionIdValid ? 1ull : 0ull);
    hashUint64(hash, static_cast<uint64_t>(setup.briefingMissionId));
    hashUint64(hash, setup.handoffMissionId);
    hashUint64(hash, static_cast<uint64_t>(setup.initialPlacementSelector));
    hashUint64(hash, static_cast<uint64_t>(setup.missionSelector));
    hashUint64(hash, setup.extendedSequenceRemapRequired ? 1ull : 0ull);
    hashUint64(hash, setup.placementSelectorRuntimeResolved ? 1ull : 0ull);
    hashString(hash, setup.missionSelectorProvenance);
    hashUint64(hash, setup.terrainMode);
    hashUint64(hash, static_cast<uint64_t>(setup.tileNames.size()));
    for (const std::string& tileName : setup.tileNames) {
        hashString(hash, tileName);
    }
    hashInt64(hash, setup.playerMode);
    hashInt64(hash, setup.opposingMode);
    hashUint64(hash, setup.playerBankId);
    hashUint64(hash, setup.opposingBankId);
    hashUint64(hash, static_cast<uint64_t>(setup.playerSlotIndex));
    hashUint64(hash, static_cast<uint64_t>(setup.objectiveOpposingSlotIndex));
    hashString(hash, setup.objectiveSourceSlot);
    hashTransform(hash, setup.initialPlayerTransform);
    hashUint64(hash, static_cast<uint64_t>(setup.playerSlots.size()));
    for (const BattleSetupSlotMetadata& slot : setup.playerSlots) {
        hashSetupSlotMetadata(hash, slot);
    }
    hashUint64(hash, static_cast<uint64_t>(setup.opposingSlots.size()));
    for (const BattleSetupSlotMetadata& slot : setup.opposingSlots) {
        hashSetupSlotMetadata(hash, slot);
    }
    if (setup.dedicatedObjective.valid) {
        hashSetupObjectiveMetadata(hash, setup.dedicatedObjective);
    }
}

void hashObjectiveState(uint64_t& hash, const BattleObjectiveState& objective) {
    if (!objective.valid) {
        return;
    }
    hashUint64(hash, 0x4f424a45ull);
    hashString(hash, objective.role);
    hashString(hash, objective.provenance);
    hashString(hash, objective.sourceSlot);
    if (objective.missionIntentProven) {
        hashUint64(hash, 0x494e544eull);
        hashUint64(hash, static_cast<uint64_t>(objective.missionIntent));
        hashString(hash, objective.missionTargetKind);
        hashString(hash, objective.missionIntentProvenance);
        hashUint64(hash, objective.missionIntentBoundToObjective ? 1ull : 0ull);
    }
    hashUint64(hash, objective.transformProven ? 1ull : 0ull);
    hashUint64(hash, objective.activeObjectProven ? 1ull : 0ull);
    hashUint64(hash, objective.missionSemanticsProven ? 1ull : 0ull);
    hashUint64(hash, objective.damagePolicyProven ? 1ull : 0ull);
    hashUint64(hash, objective.damageSuppressed ? 1ull : 0ull);
    hashUint64(hash, objective.depletionPolicyProven ? 1ull : 0ull);
    hashUint64(hash, objective.depletionSetsPlayerWinCondition ? 1ull : 0ull);
    hashUint64(hash, objective.depletionSetsPlayerLossCondition ? 1ull : 0ull);
    hashInt64(hash, objective.depletionResultCode);
    if (objective.maxDamage > 0 || objective.damage > 0 || objective.depleted ||
        isValid(objective.lastDamageSourceEntityId)) {
        hashUint64(hash, 0x4f424a44ull);
        hashInt64(hash, objective.damage);
        hashInt64(hash, objective.maxDamage);
        hashUint64(hash, objective.depleted ? 1ull : 0ull);
        hashUint64(hash, objective.lastDamageSourceEntityId.value);
    }
    hashTransform(hash, objective.transform);
    hashInt64(hash, quantize(objective.gridX));
    hashInt64(hash, quantize(objective.gridY));
    if (objective.staticModel.valid) {
        hashUint64(hash, 0x4d4f444cull);
        hashString(hash, objective.staticModel.provenance);
        hashString(hash, objective.staticModel.resourceName);
        hashInt64(hash, objective.staticModel.recordIndex);
        hashString(hash, objective.staticModel.swizzle);
        hashInt64(hash, quantize(objective.staticModel.scale));
        hashUint64(hash, objective.staticModel.groundAligned ? 1ull : 0ull);
        hashUint64(hash, objective.staticModel.animated ? 1ull : 0ull);
    }
}

void hashBattlefieldBoundary(uint64_t& hash, const BattlefieldBoundaryState& boundary) {
    if (!boundary.valid) {
        return;
    }
    hashUint64(hash, 0x424f554eull);
    hashString(hash, boundary.provenance);
    hashInt64(hash, boundary.originalMinX);
    hashInt64(hash, boundary.originalMaxX);
    hashInt64(hash, boundary.originalMinZ);
    hashInt64(hash, boundary.originalMaxZ);
    hashInt64(hash, quantize(boundary.worldMinX));
    hashInt64(hash, quantize(boundary.worldMaxX));
    hashInt64(hash, quantize(boundary.worldMinZ));
    hashInt64(hash, quantize(boundary.worldMaxZ));
    hashUint64(hash, boundary.mapProjectionProven ? 1ull : 0ull);
    hashUint64(hash, boundary.actorExitEncodingProven ? 1ull : 0ull);
    hashUint64(hash, boundary.exitMaskSemanticsProven ? 1ull : 0ull);
    hashUint64(hash, boundary.playerAllowedExitMask);
    hashUint64(hash, boundary.opposingAllowedExitMask);
    hashUint64(hash, boundary.sideCompletionExitPolicyProven ? 1ull : 0ull);
    hashUint64(hash, boundary.playerExitOutcomeProven ? 1ull : 0ull);
    hashInt64(hash, boundary.ordinaryPlayerExitResultCode);
    hashInt64(hash, boundary.allowedPlayerExitResultCode);
    hashUint64(hash, boundary.outcomeEvaluationDeferred ? 1ull : 0ull);
}

void hashBattleResult(uint64_t& hash, const BattleResult& result) {
    if (!result.valid) {
        return;
    }
    hashUint64(hash, 0x52455354ull);
    hashUint64(hash, static_cast<uint64_t>(result.state));
    hashString(hash, result.reason);
    hashInt64(hash, result.rawResultCode);
    hashUint64(hash, result.terminalTickIndex);
    hashUint64(hash, result.terminalElapsedMs);
    hashUint64(hash, result.sourceEntityId.value);
    hashInt64(hash, result.actorExitStatus);
    hashUint64(hash, static_cast<uint64_t>(result.exitEdge));
    hashUint64(hash, result.exitMask);
    hashUint64(hash, result.exitAllowed ? 1ull : 0ull);
}

void hashOppositionSpawnPlan(uint64_t& hash, const BattleOppositionSpawnPlan& plan) {
    if (!plan.valid) {
        return;
    }
    hashUint64(hash, 0x4f50504full);
    hashString(hash, plan.provenance);
    for (uint8_t count : plan.sourceCounts) {
        hashUint64(hash, count);
    }
    for (uint8_t count : plan.remainingCounts) {
        hashUint64(hash, count);
    }
    hashUint64(hash, static_cast<uint64_t>(plan.objectLimit));
    hashUint64(hash, plan.freshBattleOnly ? 1ull : 0ull);
    hashUint64(hash, plan.countBucketSemanticsProven ? 1ull : 0ull);
    hashUint64(hash, plan.candidateTypeMappingProven ? 1ull : 0ull);
    hashUint64(hash, plan.selectedTypeMappingProven ? 1ull : 0ull);
    hashUint64(hash, plan.opposingSlotMappingProven ? 1ull : 0ull);
    hashUint64(hash, plan.deterministicSelectionSeed);
    hashUint64(hash, plan.combatantsSpawned ? 1ull : 0ull);
    hashUint64(hash, static_cast<uint64_t>(plan.requests.size()));
    for (const BtechOppositionSpawnRequest& request : plan.requests) {
        hashUint64(hash, static_cast<uint64_t>(request.spawnOrdinal));
        hashUint64(hash, request.countBucketIndex);
        hashUint64(hash, request.sourceContextOffset);
        hashUint64(hash, static_cast<uint64_t>(request.estimatedClass));
        hashUint64(hash, request.candidateTypeMin);
        hashUint64(hash, request.candidateTypeMax);
        hashUint64(hash, static_cast<uint64_t>(request.candidateMechPresetIds.size()));
        for (const std::string& presetId : request.candidateMechPresetIds) {
            hashString(hash, presetId);
        }
        hashUint64(hash, request.selectedTypeId.has_value() ? 1ull : 0ull);
        if (request.selectedTypeId) {
            hashUint64(hash, *request.selectedTypeId);
        }
        hashString(hash, request.selectedMechPresetId);
        hashUint64(hash, request.runtimeObjectSlot);
        hashUint64(hash, request.opposingPlacementSlot);
    }
}

void hashOppositionRosterMetadata(uint64_t& hash, const BattleOppositionRosterMetadata& roster) {
    if (!roster.valid) {
        return;
    }
    hashUint64(hash, 0x524f5354ull);
    hashString(hash, roster.provenance);
    hashUint64(hash, roster.sourceContextMissionByte.has_value() ? 1ull : 0ull);
    if (roster.sourceContextMissionByte) {
        hashUint64(hash, *roster.sourceContextMissionByte);
    }
    hashUint64(hash, roster.initialMissionSelector.has_value() ? 1ull : 0ull);
    if (roster.initialMissionSelector) {
        hashUint64(hash, static_cast<uint64_t>(*roster.initialMissionSelector));
    }
    hashUint64(hash, roster.compositionProven ? 1ull : 0ull);
    hashUint64(hash, roster.spawnOrderProven ? 1ull : 0ull);
    hashUint64(hash, roster.placementProven ? 1ull : 0ull);
    hashUint64(hash, roster.combatantsSpawned ? 1ull : 0ull);
    hashUint64(hash, static_cast<uint64_t>(roster.entries.size()));
    for (const BattleOppositionRosterEntry& entry : roster.entries) {
        hashUint64(hash, entry.btechTypeId);
        hashString(hash, entry.mechPresetId);
        hashUint64(hash, static_cast<uint64_t>(entry.count));
    }
    hashUint64(hash, static_cast<uint64_t>(roster.orderedSlots.size()));
    for (const BattleOppositionRosterSlot& slot : roster.orderedSlots) {
        hashUint64(hash, static_cast<uint64_t>(slot.spawnOrdinal));
        hashUint64(hash, slot.btechTypeId);
        hashString(hash, slot.mechPresetId);
        hashUint64(hash, slot.runtimeObjectSlot);
        hashUint64(hash, slot.opposingPlacementSlot);
    }
}

void hashContractMetadata(uint64_t& hash, const BattleContractMetadata& contract) {
    if (!contract.valid) {
        return;
    }
    hashUint64(hash, 0x434f4e54ull);
    hashString(hash, contract.provenance);
    hashUint64(hash, contract.employerHouseId);
    hashString(hash, contract.employerHouseName);
    hashUint64(hash, contract.targetHouseId);
    hashString(hash, contract.targetHouseName);
    hashUint64(hash, contract.hasHostileTargetHouse ? 1ull : 0ull);
    hashString(hash, contract.targetPlanetName);
    hashUint64(hash, contract.targetPlanetTerrainCode);
    hashOptionalInt(hash, contract.targetEnvironmentId);
    hashUint64(hash, static_cast<uint64_t>(contract.terrainScenarioIndex));
    hashString(hash, contract.terrainScenarioProvenance);
    hashInt64(hash, contract.estimatedHeavyMechs);
    hashInt64(hash, contract.estimatedMediumMechs);
    hashInt64(hash, contract.estimatedLightMechs);
    hashUint64(hash, contract.oppositionEstimateVariable ? 1ull : 0ull);
    hashUint64(hash, contract.garrisonAutoResolveDeferred ? 1ull : 0ull);
    hashOppositionSpawnPlan(hash, contract.oppositionSpawnPlan);
    hashInt64(hash, contract.priceK);
    hashInt64(hash, contract.salvagePercent);
    hashInt64(hash, contract.advancePercent);
}

double torsoYawRadians(const BattleStartParams& params, int torsoYawStep) {
    if (params.maxPlayerTorsoYawSteps < 0 || params.playerTorsoYawStepRadians < 0.0) {
        throw std::runtime_error("battle player torso yaw limits must be non-negative");
    }
    return static_cast<double>(torsoYawStep) * params.playerTorsoYawStepRadians;
}

void validateAimPitchLimits(const BattleStartParams& params) {
    if (params.maxPlayerAimPitchUpSteps < 0 || params.maxPlayerAimPitchDownSteps < 0) {
        throw std::runtime_error("battle player aim pitch limits must be non-negative");
    }
}

void validateObjectiveState(const BattleObjectiveState& objective) {
    if (!objective.valid) {
        if (objective.transformProven || objective.activeObjectProven ||
            objective.missionIntent != BattleMissionObjectiveIntent::Unknown ||
            objective.missionIntentProven || !objective.missionTargetKind.empty() ||
            !objective.missionIntentProvenance.empty() || objective.missionIntentBoundToObjective ||
            objective.missionSemanticsProven || objective.damagePolicyProven ||
            objective.damageSuppressed || objective.depletionPolicyProven ||
            objective.depletionSetsPlayerWinCondition ||
            objective.depletionSetsPlayerLossCondition || objective.depletionResultCode != -1 ||
            objective.damage != 0 || objective.maxDamage != 0 || objective.depleted ||
            isValid(objective.lastDamageSourceEntityId)) {
            throw std::runtime_error("invalid battle objective cannot carry proven metadata");
        }
        return;
    }
    if (objective.damage < 0 || objective.maxDamage < 0) {
        throw std::runtime_error("battle objective runtime damage cannot be negative");
    }
    if (objective.maxDamage == 0 &&
        (objective.damage != 0 || objective.depleted || isValid(objective.lastDamageSourceEntityId))) {
        throw std::runtime_error("battle objective runtime damage requires a positive maximum damage");
    }
    if (objective.maxDamage > 0 && objective.damage > objective.maxDamage) {
        throw std::runtime_error("battle objective runtime damage cannot exceed maximum damage");
    }
    if (objective.depleted && (objective.maxDamage <= 0 || objective.damage < objective.maxDamage)) {
        throw std::runtime_error("depleted battle objective requires full runtime damage");
    }
    if (objective.activeObjectProven && !objective.transformProven) {
        throw std::runtime_error("proven battle objective active object requires a proven transform");
    }
    if (objective.missionIntentProven &&
        (objective.missionIntent == BattleMissionObjectiveIntent::Unknown ||
         objective.missionTargetKind.empty() || objective.missionIntentProvenance.empty())) {
        throw std::runtime_error("proven battle objective mission intent requires typed title metadata");
    }
    if (!objective.missionIntentProven &&
        (objective.missionIntent != BattleMissionObjectiveIntent::Unknown ||
         !objective.missionTargetKind.empty() || !objective.missionIntentProvenance.empty())) {
        throw std::runtime_error("unproven battle objective mission intent cannot carry title metadata");
    }
    if (objective.missionIntentBoundToObjective &&
        (!objective.missionIntentProven || !objective.transformProven ||
         !objective.activeObjectProven || !objective.missionSemanticsProven)) {
        throw std::runtime_error("bound battle objective mission intent requires proven active-object semantics");
    }
    if (objective.missionSemanticsProven && !objective.missionIntentBoundToObjective) {
        throw std::runtime_error("proven battle objective mission semantics require a bound mission intent");
    }
    if (objective.damageSuppressed && !objective.damagePolicyProven) {
        throw std::runtime_error("suppressed battle objective damage requires a proven policy");
    }
    if ((objective.depletionSetsPlayerWinCondition ||
         objective.depletionSetsPlayerLossCondition || objective.depletionResultCode != -1) &&
        !objective.depletionPolicyProven) {
        throw std::runtime_error("battle objective depletion outcome requires a proven policy");
    }
    const bool hasDepletionOutcome =
        objective.depletionSetsPlayerWinCondition || objective.depletionSetsPlayerLossCondition;
    if (!hasDepletionOutcome && objective.depletionResultCode != -1) {
        throw std::runtime_error("battle objective depletion result requires an outcome condition");
    }
    if (hasDepletionOutcome) {
        const int expectedResultCode = objective.depletionSetsPlayerWinCondition ? 0 : 1;
        if (objective.depletionResultCode != expectedResultCode) {
            throw std::runtime_error("battle objective depletion result code conflicts with BTECH precedence");
        }
    }
}

void validateMissionSetupContract(const BattleStartParams& params) {
    if (params.setupMetadata.valid && params.setupMetadata.missionSelectorContractProven) {
        const BattleSetupMetadata& setup = params.setupMetadata;
        if (!setup.briefingMissionIdValid || !params.mission.valid ||
            setup.briefingMissionId != params.mission.originalId) {
            throw std::runtime_error("battle mission id must match the original setup briefing id");
        }
        if (setup.handoffMissionId != setup.briefingMissionId + 1u ||
            setup.initialPlacementSelector != setup.briefingMissionId) {
            throw std::runtime_error("battle mission handoff selector contract is inconsistent");
        }
        if (setup.extendedSequenceRemapRequired == setup.placementSelectorRuntimeResolved) {
            throw std::runtime_error("battle placement selector resolution state is inconsistent");
        }
        if (setup.placementSelectorRuntimeResolved && setup.missionSelector != setup.initialPlacementSelector) {
            throw std::runtime_error("direct battle placement selector changed after handoff");
        }
    }
    if (params.objective.missionIntentProven) {
        if (!params.mission.valid) {
            throw std::runtime_error("proven battle objective intent requires mission briefing ownership");
        }
        const BattleMissionObjectiveBriefing expected =
            battleMissionObjectiveBriefingById(params.mission.originalId);
        if (!expected.intentProven || expected.intent != params.objective.missionIntent ||
            expected.targetKind != params.objective.missionTargetKind ||
            expected.provenance != params.objective.missionIntentProvenance) {
            throw std::runtime_error("battle objective intent conflicts with the mission briefing catalog");
        }
    }
    if (params.objective.missionIntentBoundToObjective &&
        (!params.setupMetadata.valid || !params.setupMetadata.dedicatedObjective.valid ||
         params.objective.sourceSlot != params.setupMetadata.objectiveSourceSlot)) {
        throw std::runtime_error("bound battle objective intent requires matching dedicated setup metadata");
    }
}

void validateBattlefieldBoundary(const BattlefieldBoundaryState& boundary) {
    if (!boundary.valid) {
        return;
    }
    if (boundary.originalMinX >= boundary.originalMaxX ||
        boundary.originalMinZ >= boundary.originalMaxZ ||
        boundary.worldMinX >= boundary.worldMaxX ||
        boundary.worldMinZ >= boundary.worldMaxZ) {
        throw std::runtime_error("battlefield boundary requires ordered original and runtime bounds");
    }
    if (((boundary.playerAllowedExitMask | boundary.opposingAllowedExitMask) & 0xf0u) != 0) {
        throw std::runtime_error("battlefield boundary exit masks must use only four BTECH edge bits");
    }
    if ((boundary.playerAllowedExitMask != 0 || boundary.opposingAllowedExitMask != 0) &&
        !boundary.exitMaskSemanticsProven) {
        throw std::runtime_error("battlefield boundary allowed exits require proven mask semantics");
    }
    if ((boundary.ordinaryPlayerExitResultCode != -1 || boundary.allowedPlayerExitResultCode != -1) &&
        !boundary.playerExitOutcomeProven) {
        throw std::runtime_error("battlefield boundary result codes require proven player-exit outcomes");
    }
    if (!boundary.outcomeEvaluationDeferred &&
        (!boundary.actorExitEncodingProven || !boundary.playerExitOutcomeProven ||
         boundary.ordinaryPlayerExitResultCode == -1 || boundary.allowedPlayerExitResultCode == -1)) {
        throw std::runtime_error("active battlefield outcome evaluator requires proven exit status and result codes");
    }
}

void validateOppositionRosterMetadata(
    const BattleOppositionRosterMetadata& roster,
    const std::vector<BattleCombatantLaunchState>& launchStates) {
    if (!roster.valid) {
        return;
    }
    size_t totalCount = 0;
    for (const BattleOppositionRosterEntry& entry : roster.entries) {
        const std::optional<BtechMechTypeDefinition> definition =
            btechMechTypeDefinitionById(entry.btechTypeId);
        if (!definition) {
            throw std::runtime_error("battle opposition roster contains an unknown BTECH mech type id");
        }
        if (entry.mechPresetId != definition->mechPresetId) {
            throw std::runtime_error("battle opposition roster BTECH type id does not match its mech preset");
        }
        if (entry.count == 0) {
            throw std::runtime_error("battle opposition roster entries require a positive count");
        }
        totalCount += entry.count;
    }
    if (totalCount > 4u) {
        throw std::runtime_error("battle opposition roster exceeds the proven four-object BTECH limit");
    }
    if ((roster.spawnOrderProven || roster.placementProven) && roster.orderedSlots.size() != totalCount) {
        throw std::runtime_error("proven battle opposition roster requires one ordered slot per combatant");
    }

    std::array<size_t, 8> orderedTypeCounts{};
    for (size_t slotIndex = 0; slotIndex < roster.orderedSlots.size(); ++slotIndex) {
        const BattleOppositionRosterSlot& slot = roster.orderedSlots[slotIndex];
        const std::optional<BtechMechTypeDefinition> definition =
            btechMechTypeDefinitionById(slot.btechTypeId);
        if (!definition || slot.mechPresetId != definition->mechPresetId) {
            throw std::runtime_error("battle opposition ordered slot has an invalid BTECH type/preset mapping");
        }
        if (slot.spawnOrdinal != slotIndex ||
            slot.runtimeObjectSlot != 4u + slotIndex ||
            slot.opposingPlacementSlot != slotIndex) {
            throw std::runtime_error("battle opposition ordered slots changed their proven BTECH mapping");
        }
        ++orderedTypeCounts[slot.btechTypeId];
    }
    for (const BattleOppositionRosterEntry& entry : roster.entries) {
        if (!roster.orderedSlots.empty() && orderedTypeCounts[entry.btechTypeId] != entry.count) {
            throw std::runtime_error("battle opposition ordered slots do not match aggregate roster counts");
        }
    }

    if (roster.combatantsSpawned) {
        if (!roster.spawnOrderProven || !roster.placementProven || launchStates.size() != roster.orderedSlots.size()) {
            throw std::runtime_error("spawned battle opposition requires the complete proven ordered roster");
        }
        for (size_t slotIndex = 0; slotIndex < roster.orderedSlots.size(); ++slotIndex) {
            const BattleOppositionRosterSlot& slot = roster.orderedSlots[slotIndex];
            const BattleCombatantLaunchState& launch = launchStates[slotIndex];
            if (launch.mechPresetId != slot.mechPresetId || launch.roster.team != BattleTeam::Opposing ||
                launch.roster.sourceSlot != "opposing:" + std::to_string(slot.opposingPlacementSlot)) {
                throw std::runtime_error("spawned battle opposition launch states do not match ordered roster slots");
            }
        }
    }
}

bool clampToTerrainBounds(const BattleTerrainSnapshot& terrain, Transform& transform) {
    if (terrain.boundsMinX >= terrain.boundsMaxX || terrain.boundsMinZ >= terrain.boundsMaxZ) {
        return false;
    }

    const double clampedX = std::max(terrain.boundsMinX, std::min(terrain.boundsMaxX, transform.x));
    const double clampedZ = std::max(terrain.boundsMinZ, std::min(terrain.boundsMaxZ, transform.z));
    const bool clamped = clampedX != transform.x || clampedZ != transform.z;
    transform.x = clampedX;
    transform.z = clampedZ;
    return clamped;
}

std::optional<BattlefieldBoundaryEdge> boundaryExitEdge(
    const BattlefieldBoundaryState& boundary,
    const Transform& transform) {
    if (!boundary.valid || boundary.outcomeEvaluationDeferred) {
        return std::nullopt;
    }
    if (transform.z < boundary.worldMinZ) {
        return BattlefieldBoundaryEdge::North;
    }
    if (transform.z > boundary.worldMaxZ) {
        return BattlefieldBoundaryEdge::South;
    }
    if (transform.x < boundary.worldMinX) {
        return BattlefieldBoundaryEdge::West;
    }
    if (transform.x > boundary.worldMaxX) {
        return BattlefieldBoundaryEdge::East;
    }
    return std::nullopt;
}

CombatantSnapshot makeCombatantSnapshot(const BattleStartParams& params, const Combatant& combatant) {
    const mech3d::ResolvedMechRuntimeState resolved =
        mech3d::resolveMechRuntimeState(combatant.mechRuntime, combatant.damageRules);
    const CombatantMissionStatus missionStatus =
        combatantMechDestroyed(combatant)
            ? CombatantMissionStatus::Destroyed
            : combatant.missionStatus;

    CombatantSnapshot snapshot;
    snapshot.id = combatant.id;
    snapshot.mechPresetId = combatant.mechPresetId;
    const auto hitProfile = std::find_if(
        params.mechHitProfiles.begin(),
        params.mechHitProfiles.end(),
        [&combatant](const BattleMechHitProfile& profile) {
            return profile.valid &&
                profile.mechPresetId == combatant.mechPresetId;
        });
    if (hitProfile != params.mechHitProfiles.end()) {
        snapshot.hitGeometryAvailable = true;
        snapshot.hitGeometryFingerprint = hitProfile->geometryFingerprint;
    }
    snapshot.playerControlled = combatant.playerControlled;
    snapshot.roster = combatant.roster;
    snapshot.transform = combatant.transform;
    snapshot.throttle = combatant.throttle;
    snapshot.turn = combatant.turn;
    snapshot.targetForwardSpeed = combatant.targetForwardSpeed;
    snapshot.forwardSpeed = combatant.forwardSpeed;
    snapshot.originalMaxSpeedKph = combatant.originalMaxSpeedKph;
    snapshot.maxForwardSpeed = combatant.maxForwardSpeed;
    snapshot.maxReverseSpeed = combatant.maxReverseSpeed;
    snapshot.torsoYawStep = combatant.torsoYawStep;
    snapshot.torsoYawRadians = torsoYawRadians(params, combatant.torsoYawStep);
    snapshot.aimPitchStep = combatant.aimPitchStep;
    snapshot.boundaryContact = combatant.boundaryContact;
    snapshot.collisionContact = combatant.collisionContact;
    snapshot.collisionCount = combatant.collisionCount;
    snapshot.lastCollisionTickIndex = combatant.lastCollisionTickIndex;
    snapshot.collisionFeedbackCooldownTicksRemaining =
        combatant.collisionFeedbackCooldownTicksRemaining;
    snapshot.collisionRadiusWorld = combatant.collisionRadiusWorld;
    snapshot.enemyAiState = combatant.enemyAiState;
    snapshot.replacementAi = combatant.replacementAi;
    snapshot.enemyAiTargetKind = combatant.enemyAiTargetKind;
    snapshot.enemyAiTargetEntityId = combatant.enemyAiTargetEntityId;
    snapshot.enemyAiTargetTransform = combatant.enemyAiTargetTransform;
    snapshot.enemyAiTargetDistance = combatant.enemyAiTargetDistance;
    snapshot.originalAiUpdateAccumulator =
        combatant.originalAiUpdateAccumulator;
    snapshot.originalAiUpdateCount = combatant.originalAiUpdateCount;
    snapshot.lastOriginalAiDecision = combatant.lastOriginalAiDecision;
    snapshot.originalAiMotion = combatant.originalAiMotion;
    snapshot.walkAnimationElapsedMs = combatant.walkAnimationElapsedMs;
    snapshot.jumpJetsEnabled = combatant.jumpJetsEnabled;
    snapshot.jumpCapable = combatant.jumpCapable;
    snapshot.jumpCapacityMeters = combatant.jumpCapacityMeters;
    snapshot.jumpJetCount = combatant.jumpJetCount;
    snapshot.jumpJetReady = canActivateJumpJets(params, combatant);
    snapshot.jumpJetThrusting = combatant.jumpJetThrusting;
    snapshot.jumpForwardThrusting = combatant.jumpForwardThrusting;
    snapshot.airborne = combatant.airborne;
    snapshot.hardLanding = combatant.hardLanding;
    snapshot.knockdownCandidate = combatant.knockdownCandidate;
    snapshot.jumpFuel = combatant.jumpFuel;
    snapshot.jumpMaxFuel = combatant.jumpMaxFuel;
    snapshot.jumpActivationFuel = combatant.jumpActivationFuel;
    snapshot.verticalSpeed = combatant.verticalSpeed;
    snapshot.jumpLaunchImpulseRemaining = combatant.jumpLaunchImpulseRemaining;
    snapshot.landingImpactSpeed = combatant.landingImpactSpeed;
    snapshot.missionStatus = missionStatus;
    snapshot.minimalRepairApplied = combatant.minimalRepairApplied;
    snapshot.fragileAfterMinimalRepair = combatant.fragileAfterMinimalRepair;
    snapshot.weaponCooldownTicksRemaining = combatant.weaponCooldownTicksRemaining;
    snapshot.weaponShotsFired = combatant.weaponShotsFired;
    snapshot.weaponHitsLanded = combatant.weaponHitsLanded;
    snapshot.lastWeaponTargetEntityId = combatant.lastWeaponTargetEntityId;
    snapshot.lastWeaponTargetSystem = combatant.lastWeaponTargetSystem;
    snapshot.lastDamageSourceEntityId = combatant.lastDamageSourceEntityId;
    snapshot.selectedWeaponInstanceId = combatant.selectedWeaponInstanceId;
    snapshot.weapons = combatant.weapons;
    snapshot.lastFireRequestTickIndex = combatant.lastFireRequestTickIndex;
    snapshot.lastFireRequestWeaponInstanceId = combatant.lastFireRequestWeaponInstanceId;
    snapshot.lastFireRequestAccepted = combatant.lastFireRequestAccepted;
    snapshot.heat = combatant.heat;
    snapshot.majorSystems = combatant.majorSystems;
    snapshot.lastMajorSystemWarning = combatant.lastMajorSystemWarning;
    snapshot.requestedAnimationId = resolved.requestedAnimationId;
    snapshot.activeAnimationId = resolved.activeAnimationId;
    snapshot.mechDestroyed = missionStatus == CombatantMissionStatus::Destroyed;
    if (snapshot.mechDestroyed && snapshot.activeAnimationId != "death") {
        snapshot.activeAnimationId = "death";
    } else if (!snapshot.mechDestroyed && !combatantUsesWalkAnimation(combatant)) {
        snapshot.activeAnimationId = "idle";
    } else if (!snapshot.mechDestroyed && snapshot.activeAnimationId == "idle") {
        snapshot.activeAnimationId = "walk";
    }
    snapshot.destroyedComponentIds = resolved.destroyedComponentIds;
    snapshot.hiddenComponentIds = resolved.hiddenComponentIds;
    snapshot.disabledComponentIds = resolved.disabledComponentIds;
    snapshot.detailedDamage = combatant.mechRuntime.detailedDamage;
    snapshot.persistentMechState = combatant.persistentMechState;
    if (params.mechSystemsSnapshotEnabled || params.deterministicCombatRuntimeEnabled) {
        snapshot.mechSystems = makeMechSystemsSnapshot(params, combatant, resolved);
    }
    return snapshot;
}

void refreshCombatantWeaponState(
    const BattleStartParams& params,
    Combatant& combatant) {
    const bool mechOffline =
        combatant.missionStatus != CombatantMissionStatus::Active ||
        combatantControlShutdown(combatant) ||
        mech3d::resolveMechRuntimeState(
            combatant.mechRuntime,
            combatant.damageRules).mechDestroyed ||
        (params.mechSystemMaxDamage > 0 &&
         systemDamageForRole(
             combatant.mechRuntime.detailedDamage,
             BattleMechSystemRole::Weapons) >= params.mechSystemMaxDamage);
    for (BattleWeaponInstanceState& weapon : combatant.weapons) {
        weapon.selected = weapon.weaponInstanceId == combatant.selectedWeaponInstanceId;
        if (!weapon.originalRangeValueProven) {
            weapon.maximumRange = params.weaponRange;
        }
        if (weapon.slotIndex <
            combatant.mechRuntime.detailedDamage.installedWeapons.size()) {
            const auto& installed = combatant.mechRuntime.detailedDamage
                                        .installedWeapons[weapon.slotIndex];
            weapon.condition = installed.condition;
            weapon.functional = installed.functional;
        }
        if (!weapon.originalCooldownValueProven) {
            weapon.cooldownTicksOnFire = params.weaponCooldownTicks;
        }
        if (weapon.ammunitionOwnership ==
            BattleWeaponAmmunitionOwnership::SharedAmmunitionPool) {
            weapon.ammunitionStateKnown = combatant.ammunitionStateValid;
            if (weapon.ammunitionPoolIndex >= 0 &&
                static_cast<size_t>(weapon.ammunitionPoolIndex) <
                    combatant.ammunitionByPool.size()) {
                weapon.ammunitionRemaining = combatant.ammunitionByPool[
                    static_cast<size_t>(weapon.ammunitionPoolIndex)];
            }
        } else {
            weapon.ammunitionStateKnown = true;
            weapon.ammunitionRemaining = 0;
        }

        if (mechOffline) {
            weapon.readiness = BattleWeaponReadiness::MechOffline;
        } else if (!weapon.functional) {
            weapon.readiness = BattleWeaponReadiness::NonFunctional;
        } else if (weapon.cooldownTicksRemaining != 0) {
            weapon.readiness = BattleWeaponReadiness::Cooldown;
        } else if (weapon.ammunitionOwnership ==
                       BattleWeaponAmmunitionOwnership::SharedAmmunitionPool &&
                   (!weapon.ammunitionStateKnown ||
                    weapon.ammunitionRemaining <= 0)) {
            weapon.readiness = BattleWeaponReadiness::NoAmmunition;
        } else {
            weapon.readiness = BattleWeaponReadiness::Ready;
        }
    }
    const auto selected = std::find_if(
        combatant.weapons.begin(),
        combatant.weapons.end(),
        [&combatant](const BattleWeaponInstanceState& weapon) {
            return weapon.weaponInstanceId == combatant.selectedWeaponInstanceId;
        });
    combatant.weaponCooldownTicksRemaining =
        selected == combatant.weapons.end()
            ? 0
            : selected->cooldownTicksRemaining;
}

void initializeCombatantWeapons(
    const BattleStartParams& params,
    Combatant& combatant) {
    combatant.heat = {};
    combatant.heat.enabled = params.originalHeatRuntimeEnabled;
    mech3d::MechDetailedDamageState& detailed =
        combatant.mechRuntime.detailedDamage;
    detailed = {};
    detailed.valid = true;
    detailed.provenance =
        "BTECH_runtime_9armor_8internal_critical_installed_weapon_state_v2";
    for (size_t index = 0; index < detailed.armorSections.size(); ++index) {
        detailed.armorSections[index].section =
            static_cast<mech3d::MechArmorSectionId>(index);
        detailed.armorSections[index].entryDamageLevel =
            combatant.persistentMechState.valid
                ? combatant.persistentMechState.armorDamage[index]
                : 0u;
    }
    for (size_t index = 0; index < detailed.internalSections.size(); ++index) {
        detailed.internalSections[index].section =
            static_cast<mech3d::MechInternalSectionId>(index);
    }

    const mech3d::MechCatalogDamageProfile damageProfile =
        mech3d::catalogDamageProfile(combatant.mechPresetId);
    constexpr std::array<mech3d::MechArmorSectionId, 9>
        kBtechExternalToRuntime{{
            mech3d::MechArmorSectionId::LeftArm,
            mech3d::MechArmorSectionId::RightArm,
            mech3d::MechArmorSectionId::LeftLeg,
            mech3d::MechArmorSectionId::RightLeg,
            mech3d::MechArmorSectionId::CenterTorso,
            mech3d::MechArmorSectionId::CenterRear,
            mech3d::MechArmorSectionId::LeftTorso,
            mech3d::MechArmorSectionId::RightTorso,
            mech3d::MechArmorSectionId::Head,
        }};
    constexpr std::array<mech3d::MechInternalSectionId, 8>
        kBtechInternalToRuntime{{
            mech3d::MechInternalSectionId::LeftArm,
            mech3d::MechInternalSectionId::RightArm,
            mech3d::MechInternalSectionId::LeftLeg,
            mech3d::MechInternalSectionId::RightLeg,
            mech3d::MechInternalSectionId::CenterTorso,
            mech3d::MechInternalSectionId::LeftTorso,
            mech3d::MechInternalSectionId::RightTorso,
            mech3d::MechInternalSectionId::Head,
        }};
    for (size_t index = 0; index < kBtechExternalToRuntime.size(); ++index) {
        auto& section = detailed.armorSections[
            armorSectionIndex(kBtechExternalToRuntime[index])];
        section.armorMaximum =
            damageProfile.externalArmorMaximumBtechOrder[index];
    }
    for (size_t index = 0; index < kBtechInternalToRuntime.size(); ++index) {
        auto& section = detailed.internalSections[
            internalSectionIndex(kBtechInternalToRuntime[index])];
        section.structureMaximum =
            damageProfile.internalStructureMaximumBtechOrder[index];
    }
    // Original battle-entry conversion at BTECH.EXE.c:9031..9045. The loop
    // order matters because front/rear CT share one internal word and rear CT
    // is the final writer.
    for (mech3d::MechArmorSectionId sectionId : kBtechExternalToRuntime) {
        auto& armor = detailed.armorSections[armorSectionIndex(sectionId)];
        auto& internal = detailed.internalSections[internalSectionIndex(
            internalSectionForArmor(sectionId))];
        const int level = std::min<int>(armor.entryDamageLevel, 3);
        const int combinedMaximum =
            static_cast<int>(armor.armorMaximum) +
            static_cast<int>(internal.structureMaximum);
        const int combinedRemaining =
            (combinedMaximum * (3 - level)) / 3;
        if (static_cast<int>(internal.structureMaximum) < combinedRemaining) {
            internal.structureRemaining = internal.structureMaximum;
            armor.armorRemaining = static_cast<uint16_t>(
                combinedRemaining - internal.structureMaximum);
        } else {
            internal.structureRemaining =
                static_cast<uint16_t>(combinedRemaining);
            armor.armorRemaining = 0;
        }
    }
    const std::array<uint8_t, 10> criticalConditions{{
        combatant.persistentMechState.engine,
        combatant.persistentMechState.gyros,
        combatant.persistentMechState.sensors,
        combatant.persistentMechState.lifeSupport,
        0u,
        combatant.persistentMechState.leftArmActuator,
        combatant.persistentMechState.rightArmActuator,
        combatant.persistentMechState.leftLegActuator,
        combatant.persistentMechState.rightLegActuator,
        0u,
    }};
    for (size_t index = 0; index < detailed.criticalComponents.size(); ++index) {
        auto& component = detailed.criticalComponents[index];
        component.component =
            static_cast<mech3d::MechCriticalComponentId>(index);
        component.condition = criticalConditions[index];
        component.functional = component.condition < 3u;
        component.workingCount = component.functional ? 1u : 0u;
        component.totalCount = 1u;
    }
    // Battle entry initializes the four actuator words as four minus the
    // campaign damage conversion (BTECH.EXE.c:9005..9010).  JUNK maps to no
    // remaining actuator points; the intermediate campaign table is still a
    // bridge, so retain condition as the persistent display state.
    for (mech3d::MechCriticalComponentId id : {
             mech3d::MechCriticalComponentId::LeftArmActuator,
             mech3d::MechCriticalComponentId::RightArmActuator,
             mech3d::MechCriticalComponentId::LeftLegActuator,
             mech3d::MechCriticalComponentId::RightLegActuator}) {
        auto& actuator = detailed.criticalComponents[
            criticalComponentIndex(id)];
        actuator.totalCount = 4u;
        actuator.workingCount = actuator.condition >= 3u
            ? 0u
            : static_cast<uint8_t>(4u - actuator.condition);
        actuator.functional = actuator.workingCount != 0u;
    }
    auto& heatSinks = detailed.criticalComponents[
        static_cast<size_t>(mech3d::MechCriticalComponentId::HeatSinks)];
    heatSinks.workingCount = combatant.persistentMechState.heatSinksWorking;
    heatSinks.totalCount = combatant.persistentMechState.heatSinksTotal;
    heatSinks.functional = heatSinks.totalCount == 0u || heatSinks.workingCount != 0u;
    auto& jumpJets = detailed.criticalComponents[
        static_cast<size_t>(mech3d::MechCriticalComponentId::JumpJets)];
    jumpJets.workingCount = combatant.persistentMechState.jumpJetsWorking;
    jumpJets.totalCount = combatant.persistentMechState.jumpJetsTotal;
    jumpJets.functional = jumpJets.totalCount == 0u || jumpJets.workingCount != 0u;
    for (size_t index = 0; index < detailed.installedWeapons.size(); ++index) {
        auto& installed = detailed.installedWeapons[index];
        installed.slotIndex = static_cast<uint8_t>(index);
        installed.condition = combatant.persistentMechState.valid
            ? combatant.persistentMechState.weaponConditions[index]
            : 0u;
        installed.functional = installed.condition < 3u;
    }

    combatant.weapons.clear();
    const std::vector<mech3d::MechCatalogWeaponMount> mounts =
        mech3d::catalogWeaponMounts(combatant.mechPresetId);
    combatant.weapons.reserve(mounts.size());
    for (const mech3d::MechCatalogWeaponMount& mount : mounts) {
        BattleWeaponInstanceState weapon;
        weapon.weaponInstanceId = static_cast<uint32_t>(mount.slotIndex) + 1u;
        weapon.slotIndex = mount.slotIndex;
        weapon.weaponTypeId = mount.weaponTypeId;
        weapon.displayName = mount.displayName;
        weapon.locationId = mount.locationId;
        weapon.displayRangeClass = mount.displayRangeClass;
        const auto& installedDamage =
            detailed.installedWeapons[mount.slotIndex];
        weapon.condition = installedDamage.condition;
        // The original runtime owns a binary slot-functional byte. The exact
        // .GAM light/heavy conversion is not yet proven, so only JUNK is made
        // nonfunctional in this bridge.
        weapon.functional = installedDamage.functional;
        weapon.ammunitionPoolIndex = mount.ammunitionPoolIndex;
        weapon.ammunitionOwnership = mount.usesAmmunition()
            ? BattleWeaponAmmunitionOwnership::SharedAmmunitionPool
            : BattleWeaponAmmunitionOwnership::EnergyNoAmmunition;
        weapon.originalCooldownUpdateCount = mount.originalCooldownUpdateCount;
        weapon.cooldownTicksOnFire = weaponCooldownTicks(
            params,
            mount.originalCooldownUpdateCount);
        weapon.originalCooldownValueProven =
            mount.originalCooldownUpdateCount != 0u;
        weapon.originalCooldownCadenceProven = false;
        weapon.originalDamage = mount.originalDamage;
        weapon.originalDamageValueProven = mount.originalDamage != 0u;
        weapon.originalHeat = mount.originalHeat;
        weapon.originalHeatValueProven = true;
        weapon.originalCriticalWeight = mount.originalCriticalWeight;
        weapon.originalCriticalWeightProven =
            mount.originalCriticalWeight != 0u;
        weapon.originalRangeWords = mount.originalRangeWords;
        weapon.originalRangeValueProven =
            mount.originalMaximumRangeWord() != 0u;
        weapon.maximumRange = static_cast<double>(
            mount.originalMaximumRangeWord()) * 0x1e0;
        weapon.deliveryMode = mount.usesOriginalProjectilePool()
            ? BattleWeaponDeliveryMode::OriginalProjectilePool
            : BattleWeaponDeliveryMode::Immediate;
        weapon.originalProjectileTypeIndex =
            mount.originalProjectileTypeIndex;
        weapon.originalProjectileSpeedRaw =
            mount.originalProjectileSpeedRaw;
        weapon.originalProjectileLifetimeUpdates =
            mount.originalProjectileLifetimeUpdates;
        weapon.originalProjectileDamageClass =
            mount.originalProjectileDamageClass;
        weapon.originalProjectileVisualClass =
            mount.originalProjectileVisualClass;
        weapon.originalProjectileDefinitionProven =
            mount.usesOriginalProjectilePool();
        combatant.weapons.push_back(std::move(weapon));
    }
    combatant.selectedWeaponInstanceId = combatant.weapons.empty()
        ? 0u
        : combatant.weapons.front().weaponInstanceId;
    refreshCombatantWeaponState(params, combatant);
}

CameraSnapshot makeCameraSnapshot(const BattleStartParams& params, const Combatant* player) {
    CameraSnapshot camera;
    if (player == nullptr) {
        return camera;
    }
    camera.valid = true;
    camera.attachedEntityId = player->id;
    camera.localForwardOffset = params.cockpitCameraForwardOffset;
    camera.localHeight = params.cockpitCameraHeight;
    const double cameraHeading = player->transform.headingRadians + torsoYawRadians(params, player->torsoYawStep);
    camera.transform.headingRadians = cameraHeading;
    camera.transform.x =
        player->transform.x + std::sin(cameraHeading) * params.cockpitCameraForwardOffset;
    camera.transform.y = player->transform.y + params.cockpitCameraHeight;
    camera.transform.z =
        player->transform.z + std::cos(cameraHeading) * params.cockpitCameraForwardOffset;
    return camera;
}

} // namespace

bool operator==(EntityId a, EntityId b) {
    return a.value == b.value;
}

bool operator!=(EntityId a, EntityId b) {
    return !(a == b);
}

bool isValid(EntityId id) {
    return id.value != 0;
}

const char* battleTeamName(BattleTeam team) {
    switch (team) {
    case BattleTeam::Player:
        return "player";
    case BattleTeam::Opposing:
        return "opposing";
    case BattleTeam::Neutral:
        return "neutral";
    }
    return "unknown";
}

uint64_t battlePersistentMechStateFingerprint(
    const BattlePersistentMechState& state) {
    if (!state.valid) {
        return 0;
    }
    uint64_t hash = kFnvOffset;
    hashString(hash, state.provenance);
    for (uint8_t value : {
             state.engine,
             state.gyros,
             state.sensors,
             state.lifeSupport,
             state.heatSinksWorking,
             state.heatSinksTotal,
             state.leftArmActuator,
             state.rightArmActuator,
             state.leftLegActuator,
             state.rightLegActuator,
             state.jumpJetsWorking,
             state.jumpJetsTotal,
             state.armorPercent}) {
        hashUint64(hash, value);
    }
    for (uint8_t value : state.weaponConditions) {
        hashUint64(hash, value);
    }
    for (uint8_t value : state.armorDamage) {
        hashUint64(hash, value);
    }
    return hash;
}

bool battlePersistentMechStateIsPristine(
    const BattlePersistentMechState& state) {
    if (!state.valid ||
        state.engine != 0u ||
        state.gyros != 0u ||
        state.sensors != 0u ||
        state.lifeSupport != 0u ||
        state.leftArmActuator != 0u ||
        state.rightArmActuator != 0u ||
        state.leftLegActuator != 0u ||
        state.rightLegActuator != 0u ||
        state.heatSinksWorking != state.heatSinksTotal ||
        state.jumpJetsWorking != state.jumpJetsTotal ||
        state.armorPercent != 100u) {
        return false;
    }
    return std::all_of(
               state.weaponConditions.begin(),
               state.weaponConditions.end(),
               [](uint8_t value) { return value == 0u; }) &&
           std::all_of(
               state.armorDamage.begin(),
               state.armorDamage.end(),
               [](uint8_t value) { return value == 0u; });
}

const char* battleEnemyAiStateName(BattleEnemyAiState state) {
    switch (state) {
    case BattleEnemyAiState::None:
        return "none";
    case BattleEnemyAiState::Search:
        return "search";
    case BattleEnemyAiState::Approach:
        return "approach";
    case BattleEnemyAiState::Attack:
        return "attack";
    case BattleEnemyAiState::Retreat:
        return "retreat";
    }
    return "unknown";
}

const char* battleEnemyAiTargetKindName(BattleEnemyAiTargetKind kind) {
    switch (kind) {
    case BattleEnemyAiTargetKind::None:
        return "none";
    case BattleEnemyAiTargetKind::Player:
        return "player";
    case BattleEnemyAiTargetKind::Objective:
        return "objective";
    }
    return "unknown";
}

const char* battleMechSystemRoleName(BattleMechSystemRole role) {
    switch (role) {
    case BattleMechSystemRole::Unknown:
        return "unknown";
    case BattleMechSystemRole::Core:
        return "core";
    case BattleMechSystemRole::Cockpit:
        return "cockpit";
    case BattleMechSystemRole::Mobility:
        return "mobility";
    case BattleMechSystemRole::Weapons:
        return "weapons";
    case BattleMechSystemRole::JumpJets:
        return "jump_jets";
    }
    return "unknown";
}

const char* battleMechSystemStatusName(BattleMechSystemStatus status) {
    switch (status) {
    case BattleMechSystemStatus::Online:
        return "online";
    case BattleMechSystemStatus::Degraded:
        return "degraded";
    case BattleMechSystemStatus::Offline:
        return "offline";
    case BattleMechSystemStatus::Destroyed:
        return "destroyed";
    }
    return "unknown";
}

BattleMajorSystemRuntimeState battleMajorSystemRuntimeState(
    const mech3d::MechDetailedDamageState& damage,
    uint64_t elapsedMs,
    bool enabled) {
    BattleMajorSystemRuntimeState state;
    state.enabled = enabled && damage.valid;
    if (!state.enabled) {
        return state;
    }

    const auto component = [&damage](mech3d::MechCriticalComponentId id)
        -> const mech3d::MechCriticalComponentRuntimeState& {
        return damage.criticalComponents[static_cast<size_t>(id)];
    };
    const auto status = [](const mech3d::MechCriticalComponentRuntimeState& entry) {
        if (!entry.functional || entry.condition >= 3u) {
            return BattleMechSystemStatus::Destroyed;
        }
        if (entry.condition >= 2u) {
            return BattleMechSystemStatus::Offline;
        }
        if (entry.condition != 0u || entry.battleDamage > 0) {
            return BattleMechSystemStatus::Degraded;
        }
        return BattleMechSystemStatus::Online;
    };

    const auto& sensors = component(mech3d::MechCriticalComponentId::Sensors);
    const auto& gyros = component(mech3d::MechCriticalComponentId::Gyros);
    const auto& engine = component(mech3d::MechCriticalComponentId::Engine);
    const auto& lifeSupport = component(
        mech3d::MechCriticalComponentId::LifeSupport);
    state.sensors = status(sensors);
    state.gyros = status(gyros);
    state.engine = status(engine);
    state.lifeSupport = status(lifeSupport);

    // BTECH's damaged-sensor branch makes sight visibility intermittent, but
    // the raw four-condition-to-internal conversion does not expose separate
    // light/heavy periods.  These two wall-clock cadences are therefore typed
    // presentation/gameplay policy: light loses 0.3 s every 4 s; heavy loses
    // 0.4 s every 0.8 s.  They depend only on authoritative elapsed time.
    const uint64_t originalUpdate = elapsedMs / 100u;
    if (state.sensors == BattleMechSystemStatus::Degraded) {
        state.crosshairVisible = originalUpdate % 40u < 37u;
    } else if (state.sensors == BattleMechSystemStatus::Offline) {
        state.crosshairVisible = originalUpdate % 8u < 4u;
    } else if (state.sensors == BattleMechSystemStatus::Destroyed) {
        state.crosshairVisible = false;
    }
    state.targetingAvailable = state.crosshairVisible;
    state.topographicMapVisible =
        state.sensors != BattleMechSystemStatus::Destroyed;
    state.radarContactsVisible = state.topographicMapVisible;

    // The manual proves slower damaged movement and a complete stop when the
    // gyro is destroyed, but not the intermediate ratios.  Keep the selected
    // 3/4 and 1/2 scales explicit and isolated here.
    if (state.gyros == BattleMechSystemStatus::Degraded) {
        state.gyroMovementScale = 0.75;
    } else if (state.gyros == BattleMechSystemStatus::Offline) {
        state.gyroMovementScale = 0.5;
    } else if (state.gyros == BattleMechSystemStatus::Destroyed) {
        state.gyroMovementScale = 0.0;
    }

    state.engineShutdown =
        state.engine == BattleMechSystemStatus::Destroyed;
    state.lifeSupportFailure =
        state.lifeSupport == BattleMechSystemStatus::Destroyed;
    state.movementBlocked = state.engineShutdown ||
        state.gyroMovementScale <= 0.0 || state.lifeSupportFailure;
    state.engineHeatPerOriginalUpdate =
        static_cast<int>(std::min<uint8_t>(engine.condition, 3u)) * 5;
    return state;
}

bool battleReactorShutdownMessageVisible(const BattleHeatState& heat) {
    return heat.enabled && heat.reactorShutdown &&
           (heat.originalUpdateCount &
            battleOriginalReactorShutdownFlashUpdates()) != 0u;
}

const char* battlefieldBoundaryEdgeName(BattlefieldBoundaryEdge edge) {
    switch (edge) {
    case BattlefieldBoundaryEdge::North:
        return "north";
    case BattlefieldBoundaryEdge::South:
        return "south";
    case BattlefieldBoundaryEdge::West:
        return "west";
    case BattlefieldBoundaryEdge::East:
        return "east";
    }
    return "unknown";
}

const char* battleMissionRuntimeStateName(BattleMissionRuntimeState state) {
    switch (state) {
    case BattleMissionRuntimeState::InProgress:
        return "in_progress";
    case BattleMissionRuntimeState::Victory:
        return "victory";
    case BattleMissionRuntimeState::Defeat:
        return "defeat";
    case BattleMissionRuntimeState::Withdraw:
        return "withdraw";
    case BattleMissionRuntimeState::Unsupported:
        return "unsupported";
    }
    return "unknown";
}

BattleStaticModelRef originalBattleObjectiveStaticModel() {
    BattleStaticModelRef model;
    model.valid = true;
    model.provenance = "original_objective_visual";
    model.resourceName = "OTHPCK.TBL";
    model.recordIndex = 14;
    model.swizzle = "xzy";
    model.scale = 1.0;
    model.groundAligned = true;
    model.animated = false;
    return model;
}

uint64_t battleWorldLibraryAbiFingerprint() noexcept {
    return battleWorldHeaderAbiFingerprint();
}

BattleWorld BattleWorld::create(
    const BattleStartParams& params,
    uint64_t callerAbiFingerprint) {
    if (callerAbiFingerprint != battleWorldLibraryAbiFingerprint()) {
        throw std::runtime_error(
            "battle runtime ABI mismatch; perform a clean rebuild");
    }
    BattleWorld world;
    world.params_ = params;
    validateObjectiveState(params.objective);
    validateMissionSetupContract(params);
    validateBattlefieldBoundary(params.battlefieldBoundary);
    validateOppositionRosterMetadata(params.oppositionRoster, params.combatantLaunchStates);
    if (params.deterministicCollisionRuntimeEnabled &&
        (params.mechCollisionRadiusCells <= 0.0 ||
         params.collisionRestitution < 0.0 ||
         params.collisionRestitution > 1.0 ||
         params.collisionBackoffWorldUnits < 0.0 ||
         params.collisionFeedbackCooldownSeconds < 0.0 ||
         params.objectiveCollisionRadiusWorld <= 0.0 ||
         !std::isfinite(params.objectiveCollisionRadiusWorld))) {
        throw std::runtime_error("battle collision tuning is outside its deterministic domain");
    }
    if (params.objectiveRetaliationSeconds < 0.0 ||
        params.combatConclusionDelaySeconds < 0.0 ||
        !std::isfinite(params.objectiveRetaliationSeconds) ||
        !std::isfinite(params.combatConclusionDelaySeconds)) {
        throw std::runtime_error(
            "battle combat timing tuning is outside its deterministic domain");
    }
    if (params.originalHeatRuntimeEnabled &&
        (!params.deterministicCombatRuntimeEnabled ||
         !params.individualWeaponRuntimeEnabled)) {
        throw std::runtime_error(
            "original heat runtime requires individual weapon combat runtime");
    }
    const bool originalCombatAiPolicy =
        params.combatAiPolicy ==
            BattleCombatAiPolicy::OriginalBtechStationarySingleTargetFire ||
        params.combatAiPolicy ==
            BattleCombatAiPolicy::OriginalBtechModeZeroFirstMovementUpdate ||
        params.combatAiPolicy ==
            BattleCombatAiPolicy::OriginalBtechModeZeroRepeatedMovement ||
        params.combatAiPolicy ==
            BattleCombatAiPolicy::
                OriginalBtechModeZeroMultiTargetRepeatedMovement ||
        params.combatAiPolicy ==
            BattleCombatAiPolicy::
                OriginalBtechNonObjectiveModesMultiTargetRepeatedMovement ||
        params.combatAiPolicy ==
            BattleCombatAiPolicy::
                OriginalBtechNonObjectiveSymmetricRepeatedMovement50eeInvariant ||
        params.combatAiPolicy == BattleCombatAiPolicy::
            OriginalBtechModeThreeNullDataPointRepeatedMovement ||
        params.combatAiPolicy == BattleCombatAiPolicy::
            CompatibilityModeThreePlayerCompanionSelectedTargetRepeatedMovement ||
        params.combatAiPolicy == BattleCombatAiPolicy::
            CompatibilityModeThreePlayerCompanionMissionObjectiveRepeatedMovement;
    if (originalCombatAiPolicy &&
        (!params.deterministicCombatRuntimeEnabled ||
         !params.individualWeaponRuntimeEnabled ||
         !params.originalProjectileRuntimeEnabled ||
         !params.originalHeatRuntimeEnabled ||
         !params.originalMajorSystemRuntimeEnabled)) {
        throw std::runtime_error(
            "original combat AI requires the complete Phase 12 runtime");
    }
    if (params.combatAiPolicy ==
            BattleCombatAiPolicy::ReplacementDeterministicCombatAi &&
        (!params.deterministicCombatRuntimeEnabled ||
         !params.individualWeaponRuntimeEnabled ||
         !params.originalProjectileRuntimeEnabled ||
         !params.originalHeatRuntimeEnabled ||
         !params.originalMajorSystemRuntimeEnabled ||
         !params.deterministicCollisionRuntimeEnabled)) {
        throw std::runtime_error(
            "replacement combat AI requires the complete Phase 12 combat and collision runtime");
    }
    if (params.enemyRetreatOnDamageEnabled &&
        params.combatAiPolicy ==
            BattleCombatAiPolicy::ReplacementDeterministicCombatAi &&
        (params.enemyRetreatRemainingDurabilityPercent <= 0 ||
         params.enemyRetreatRemainingDurabilityPercent >= 100)) {
        throw std::runtime_error(
            "replacement enemy retreat durability threshold must be between 1 and 99 percent");
    }
    const bool originalMovementPolicy =
        params.combatAiPolicy ==
            BattleCombatAiPolicy::OriginalBtechModeZeroFirstMovementUpdate ||
        params.combatAiPolicy ==
            BattleCombatAiPolicy::OriginalBtechModeZeroRepeatedMovement ||
        params.combatAiPolicy ==
            BattleCombatAiPolicy::
                OriginalBtechModeZeroMultiTargetRepeatedMovement ||
        params.combatAiPolicy ==
            BattleCombatAiPolicy::
                OriginalBtechNonObjectiveModesMultiTargetRepeatedMovement ||
        params.combatAiPolicy ==
            BattleCombatAiPolicy::
                OriginalBtechNonObjectiveSymmetricRepeatedMovement50eeInvariant ||
        params.combatAiPolicy == BattleCombatAiPolicy::
            OriginalBtechModeThreeNullDataPointRepeatedMovement ||
        params.combatAiPolicy == BattleCombatAiPolicy::
            CompatibilityModeThreePlayerCompanionSelectedTargetRepeatedMovement ||
        params.combatAiPolicy == BattleCombatAiPolicy::
            CompatibilityModeThreePlayerCompanionMissionObjectiveRepeatedMovement;
    if (originalMovementPolicy &&
        (!params.terrainCollisionGrid.valid ||
         params.terrainCollisionGrid.width != 174 ||
         params.terrainCollisionGrid.height != 94 ||
         params.terrainCollisionGrid.rawSamples.size() != 174u * 94u ||
         !params.originalTerrainSceneCatalog.valid)) {
        throw std::runtime_error(
            "original first movement update requires owned 174x94 GRD and terrain scene data");
    }
    if (params.deterministicObjectiveRuntimeEnabled) {
        if (!params.deterministicCombatRuntimeEnabled) {
            throw std::runtime_error("battle objective runtime requires deterministic combat runtime");
        }
        if (params.objectiveMaxDamage <= 0) {
            throw std::runtime_error("battle objective runtime maximum damage must be positive");
        }
        if (!params.objective.valid ||
            !params.objective.missionIntentBoundToObjective ||
            !params.objective.missionSemanticsProven ||
            !params.objective.activeObjectProven ||
            !params.objective.transformProven ||
            !params.objective.damagePolicyProven ||
            params.objective.damageSuppressed) {
            throw std::runtime_error("battle objective runtime requires a proven damageable bound active object");
        }
    }
    world.objective_ = params.objective;
    const bool autoEnableWikiObjectiveRuntime =
        !params.deterministicObjectiveRuntimeEnabled;
    if (params.combatAiPolicy ==
            BattleCombatAiPolicy::ReplacementDeterministicCombatAi &&
        params.mission.valid && !params.mission.extended &&
        world.objective_.valid && world.objective_.transformProven) {
        const bool defenseScenario =
            params.mission.family == BattleMissionFamily::Defense;
        const bool assaultScenario =
            params.mission.family == BattleMissionFamily::Assault;
        const bool retrievalScenario =
            params.mission.family == BattleMissionFamily::Retrieval;
        if (defenseScenario || assaultScenario || retrievalScenario) {
            world.objective_.activeObjectProven = true;
            world.objective_.missionIntentBoundToObjective = true;
            world.objective_.missionSemanticsProven = true;
            world.objective_.provenance =
                "replacement_wiki_scenario_structure:" +
                world.objective_.provenance;
            if (defenseScenario) {
                world.objective_.role = "protect";
                world.objective_.missionIntent =
                    BattleMissionObjectiveIntent::Protect;
                world.objective_.missionIntentProven = true;
                world.objective_.damagePolicyProven = true;
                world.objective_.damageSuppressed = false;
                world.objective_.depletionPolicyProven = true;
                world.objective_.depletionSetsPlayerWinCondition = false;
                world.objective_.depletionSetsPlayerLossCondition = true;
                world.objective_.depletionResultCode = 1;
                world.params_.deterministicObjectiveRuntimeEnabled = true;
            } else if (assaultScenario) {
                world.objective_.role = "target";
                world.objective_.damagePolicyProven = true;
                world.objective_.damageSuppressed = false;
                world.objective_.depletionPolicyProven = true;
                world.objective_.depletionSetsPlayerWinCondition = true;
                world.objective_.depletionSetsPlayerLossCondition = false;
                world.objective_.depletionResultCode = 0;
                world.params_.deterministicObjectiveRuntimeEnabled = true;
            } else {
                world.objective_.role = "retrieve";
                world.objective_.missionIntent =
                    BattleMissionObjectiveIntent::Retrieve;
                world.objective_.missionIntentProven = true;
                world.objective_.damagePolicyProven = true;
                world.objective_.damageSuppressed = true;
                world.objective_.depletionPolicyProven = false;
                world.objective_.depletionSetsPlayerWinCondition = false;
                world.objective_.depletionSetsPlayerLossCondition = false;
                world.objective_.depletionResultCode = -1;
                world.objective_.maxDamage = 0;
            }
            if (autoEnableWikiObjectiveRuntime &&
                (defenseScenario || assaultScenario) &&
                world.objective_.maxDamage == 0) {
                world.objective_.maxDamage = std::max(
                    defenseScenario ? 300 : 100,
                    world.params_.objectiveMaxDamage);
            }
        }
    }
    if (world.params_.deterministicObjectiveRuntimeEnabled &&
        world.objective_.maxDamage == 0) {
        if (world.params_.objectiveMaxDamage <= 0) {
            throw std::runtime_error(
                "replacement mission objective maximum damage must be positive");
        }
        world.objective_.maxDamage = world.params_.objectiveMaxDamage;
    }
    world.params_.objective = world.objective_;
    validateObjectiveState(world.objective_);
    world.terrain_ = makeTerrainSnapshot(params);
    if (params.playerRoster.team != BattleTeam::Player) {
        throw std::runtime_error("battle player roster entry must belong to the player team");
    }
    std::array<bool, 8> claimedOriginalLiveObjectSlots{};
    const auto bindOriginalLiveObjectSlot =
        [&claimedOriginalLiveObjectSlots](
            BattleCombatantRosterMetadata& roster,
            std::optional<uint8_t> expectedSlot) {
            if (roster.originalLiveObjectSlot &&
                (!expectedSlot || *roster.originalLiveObjectSlot != *expectedSlot)) {
                throw std::runtime_error(
                    "battle roster original live-object slot does not match its ordered partition");
            }
            if (!expectedSlot) {
                return;
            }
            if (*expectedSlot >= claimedOriginalLiveObjectSlots.size() ||
                claimedOriginalLiveObjectSlots[*expectedSlot]) {
                throw std::runtime_error(
                    "battle roster original live-object slot is invalid or duplicated");
            }
            roster.originalLiveObjectSlot = *expectedSlot;
            claimedOriginalLiveObjectSlots[*expectedSlot] = true;
        };
    BattleCombatantRosterMetadata playerRoster = params.playerRoster;
    bindOriginalLiveObjectSlot(playerRoster, uint8_t{0});
    if (params.playerLaunchState.has_value()) {
        const BattleCombatantLaunchState& launchState = *params.playerLaunchState;
        world.playerEntityId_ = world.spawnCombatant(
            launchState.mechPresetId,
            true,
            launchState.startTransform,
            playerRoster);
        Combatant* player = world.findCombatant(world.playerEntityId_);
        if (player == nullptr) {
            throw std::runtime_error("battle player entity was not created");
        }
        player->minimalRepairApplied = launchState.minimalRepairApplied;
        player->fragileAfterMinimalRepair = launchState.fragileAfterMinimalRepair;
        player->persistentMechState = launchState.persistentMechState;
        player->ammunitionStateValid = launchState.ammunitionStateValid;
        player->ammunitionByPool = launchState.ammunitionByPool;
        initializeCombatantWeapons(params, *player);
        player->missionStatus = launchState.minimalRepairApplied
                                    ? CombatantMissionStatus::Active
                                    : launchState.priorMissionStatus;
        for (int componentId : launchState.destroyedComponentIds) {
            mech3d::destroyMechRuntimeComponent(player->mechRuntime, componentId);
        }
    } else {
        world.playerEntityId_ = world.spawnCombatant(
            params.playerMechPresetId,
            true,
        params.playerStartTransform,
        playerRoster);
    }
    for (size_t launchIndex = 0;
         launchIndex < params.playerAlliedLaunchStates.size();
         ++launchIndex) {
        const BattleCombatantLaunchState& launchState =
            params.playerAlliedLaunchStates[launchIndex];
        if (launchState.roster.team != BattleTeam::Player) {
            throw std::runtime_error(
                "battle allied launch roster entry must belong to the player team");
        }
        BattleCombatantRosterMetadata roster = launchState.roster;
        if (roster.sourceSlot.empty()) {
            roster.sourceSlot = "player:" + std::to_string(launchIndex + 1u);
        }
        bindOriginalLiveObjectSlot(
            roster,
            launchIndex < 3u
                ? std::optional<uint8_t>(
                      static_cast<uint8_t>(launchIndex + 1u))
                : std::nullopt);
        const EntityId entityId = world.spawnCombatant(
            launchState.mechPresetId,
            false,
            launchState.startTransform,
            std::move(roster));
        Combatant* combatant = world.findCombatant(entityId);
        if (combatant == nullptr) {
            throw std::runtime_error("battle allied player entity was not created");
        }
        combatant->minimalRepairApplied = launchState.minimalRepairApplied;
        combatant->fragileAfterMinimalRepair = launchState.fragileAfterMinimalRepair;
        combatant->missionStatus = launchState.minimalRepairApplied
                                       ? CombatantMissionStatus::Active
                                       : launchState.priorMissionStatus;
        combatant->persistentMechState = launchState.persistentMechState;
        combatant->ammunitionStateValid = launchState.ammunitionStateValid;
        combatant->ammunitionByPool = launchState.ammunitionByPool;
        initializeCombatantWeapons(params, *combatant);
        for (int componentId : launchState.destroyedComponentIds) {
            mech3d::destroyMechRuntimeComponent(combatant->mechRuntime, componentId);
        }
    }
    size_t opposingOrdinal = 0u;
    for (size_t launchIndex = 0; launchIndex < params.combatantLaunchStates.size(); ++launchIndex) {
        const BattleCombatantLaunchState& launchState = params.combatantLaunchStates[launchIndex];
        if (launchState.roster.team == BattleTeam::Player) {
            throw std::runtime_error("battle non-player launch roster entry cannot belong to the player team");
        }
        BattleCombatantRosterMetadata roster = launchState.roster;
        if (roster.sourceSlot.empty()) {
            roster.sourceSlot = std::string(battleTeamName(roster.team)) + ":" + std::to_string(launchIndex);
        }
        if (roster.team == BattleTeam::Opposing) {
            bindOriginalLiveObjectSlot(
                roster,
                opposingOrdinal < 4u
                    ? std::optional<uint8_t>(
                          static_cast<uint8_t>(4u + opposingOrdinal))
                    : std::nullopt);
            ++opposingOrdinal;
        } else {
            bindOriginalLiveObjectSlot(roster, std::nullopt);
        }
        const EntityId entityId = world.spawnCombatant(
            launchState.mechPresetId,
            false,
            launchState.startTransform,
            std::move(roster));
        Combatant* combatant = world.findCombatant(entityId);
        if (combatant == nullptr) {
            throw std::runtime_error("battle non-player entity was not created");
        }
        combatant->minimalRepairApplied = launchState.minimalRepairApplied;
        combatant->fragileAfterMinimalRepair = launchState.fragileAfterMinimalRepair;
        combatant->missionStatus = launchState.priorMissionStatus;
        combatant->persistentMechState = launchState.persistentMechState;
        combatant->ammunitionStateValid = launchState.ammunitionStateValid;
        combatant->ammunitionByPool = launchState.ammunitionByPool;
        initializeCombatantWeapons(params, *combatant);
        for (int componentId : launchState.destroyedComponentIds) {
            mech3d::destroyMechRuntimeComponent(combatant->mechRuntime, componentId);
        }
    }
    const auto initializeOriginalAiMotion = [&world]() {
        const BattleSetupMetadata& setup = world.params_.setupMetadata;
        if (!setup.valid || setup.playerSlots.empty() ||
            setup.opposingSlots.empty()) {
            return;
        }
        const BattleSetupSlotMetadata& slot0 = setup.playerSlots.front();
        const BattleSetupSlotMetadata& slot4 = setup.opposingSlots.front();
        const bool anchorsAvailable =
            slot0.valid && slot4.valid &&
            slot0.originalRawPositionProven &&
            slot4.originalRawPositionProven;
        if (!anchorsAvailable) {
            return;
        }
        for (Combatant& combatant : world.combatants_) {
            if (!combatant.roster.originalLiveObjectSlot) {
                continue;
            }
            const uint8_t liveSlot =
                *combatant.roster.originalLiveObjectSlot;
            const bool playerSide = liveSlot < 4u;
            const size_t sideIndex = playerSide
                ? static_cast<size_t>(liveSlot)
                : static_cast<size_t>(liveSlot - 4u);
            const auto& sideSlots = playerSide
                ? setup.playerSlots
                : setup.opposingSlots;
            if (sideIndex >= sideSlots.size()) {
                continue;
            }
            const BattleSetupSlotMetadata& source = sideSlots[sideIndex];
            if (!source.valid || !source.originalRawPositionProven) {
                continue;
            }
            const int16_t sideMode = static_cast<int16_t>(
                playerSide ? setup.playerMode : setup.opposingMode);
            const BattleOriginalAiControlInitializationDiagnostic control =
                battleOriginalAiControlInitialization6fd8(liveSlot, true);
            BattleOriginalAiInitialBodyPoseDiagnosticInput poseInput;
            poseInput.liveObjectSlot = liveSlot;
            poseInput.liveObjectOccupied = true;
            poseInput.numericSideModeWord = sideMode;
            poseInput.anchorsAvailable = anchorsAvailable;
            poseInput.slotRawX = source.originalRawX;
            poseInput.slotRawZ = source.originalRawZ;
            poseInput.slot0RawX = slot0.originalRawX;
            poseInput.slot0RawZ = slot0.originalRawZ;
            poseInput.slot4RawX = slot4.originalRawX;
            poseInput.slot4RawZ = slot4.originalRawZ;
            poseInput.sideHeadingFlagsAvailable =
                world.params_.battlefieldBoundary.valid &&
                world.params_.battlefieldBoundary.exitMaskSemanticsProven;
            poseInput.sideHeadingFlags = playerSide
                ? world.params_.battlefieldBoundary.playerAllowedExitMask
                : world.params_.battlefieldBoundary.opposingAllowedExitMask;
            poseInput.separateObjectiveRawPoseAvailable = false;
            poseInput.modeThreeNullDataRawPoseAvailable =
                sideMode == 3 &&
                world.params_.combatAiPolicy == BattleCombatAiPolicy::
                    OriginalBtechModeThreeNullDataPointRepeatedMovement;
            poseInput.modeThreeNullDataRawX =
                kOriginalModeThreeNullDataRawX;
            poseInput.modeThreeNullDataRawZ =
                kOriginalModeThreeNullDataRawZ;
            const BattleOriginalAiInitialBodyPoseDiagnostic pose =
                battleOriginalAiInitialBodyPose1a22(poseInput);
            const bool compatibilityModeThreeSelectedTargetPlayerPose =
                playerSide && sideMode == 3 &&
                world.params_.combatAiPolicy == BattleCombatAiPolicy::
                    CompatibilityModeThreePlayerCompanionSelectedTargetRepeatedMovement;
            const bool compatibilityModeThreeMissionObjectivePlayerPose =
                playerSide && sideMode == 3 &&
                world.params_.combatAiPolicy == BattleCombatAiPolicy::
                    CompatibilityModeThreePlayerCompanionMissionObjectiveRepeatedMovement &&
                setup.objectiveOpposingSlotIndex <
                    setup.opposingSlots.size() &&
                setup.opposingSlots[setup.objectiveOpposingSlotIndex].valid &&
                setup.opposingSlots[setup.objectiveOpposingSlotIndex].
                    originalRawPositionProven;
            const bool compatibilityModeThreePlayerPose =
                compatibilityModeThreeSelectedTargetPlayerPose ||
                compatibilityModeThreeMissionObjectivePlayerPose;
            if (!control.exact || !control.recordValuesKnown ||
                (!pose.exact && !compatibilityModeThreePlayerPose)) {
                continue;
            }
            BattleOriginalAiMotionRuntimeState state;
            state.initialized = true;
            state.provenance = compatibilityModeThreeMissionObjectivePlayerPose
                ? "compatibility_provisional:BTECH_mode3_CCEE_null:"
                  "player_companion_pose_from_slot+slot0_to_mission_objective_bearing"
                : compatibilityModeThreeSelectedTargetPlayerPose
                ? "compatibility_provisional:BTECH_mode3_CCEE_null:"
                  "player_companion_pose_from_slot+shared_slot0_to_slot4_bearing"
                : "BTECH.EXE:1a22+6fd8:mode0_1_2_3_4_original_slot_setup";
            state.liveObjectSlot = liveSlot;
            state.sideWord = compatibilityModeThreePlayerPose
                ? static_cast<int16_t>(0)
                : pose.sideWord;
            state.numericSideModeWord = sideMode;
            state.controlRecordBytes = control.recordBytes;
            state.rawX = compatibilityModeThreePlayerPose
                ? source.originalRawX
                : pose.rawX;
            state.rawY = compatibilityModeThreePlayerPose ? 300 : pose.rawY;
            state.rawZ = compatibilityModeThreePlayerPose
                ? source.originalRawZ
                : pose.rawZ;
            state.pitch = compatibilityModeThreePlayerPose ? 0 : pose.pitch;
            state.roll = compatibilityModeThreePlayerPose ? 0 : pose.roll;
            state.heading = compatibilityModeThreePlayerPose
                ? originalAiFixedAngle(
                      originalAiSignedDword(
                          static_cast<int64_t>(
                              compatibilityModeThreeMissionObjectivePlayerPose
                                  ? setup.opposingSlots[
                                        setup.objectiveOpposingSlotIndex].
                                        originalRawX
                                  : slot4.originalRawX) -
                          slot0.originalRawX),
                      originalAiSignedDword(
                          static_cast<int64_t>(
                              compatibilityModeThreeMissionObjectivePlayerPose
                                  ? setup.opposingSlots[
                                        setup.objectiveOpposingSlotIndex].
                                        originalRawZ
                                  : slot4.originalRawZ) -
                          slot0.originalRawZ))
                : pose.heading;
            constexpr double kBindingTolerance = 1.0e-6;
            state.worldPositionBindingExact =
                std::abs(combatant.transform.x - source.transform.x) <=
                    kBindingTolerance &&
                std::abs(combatant.transform.y - source.transform.y) <=
                    kBindingTolerance &&
                std::abs(combatant.transform.z - source.transform.z) <=
                    kBindingTolerance;
            combatant.originalAiMotion = std::move(state);
        }
    };
    initializeOriginalAiMotion();
    if (params.combatAiPolicy ==
            BattleCombatAiPolicy::ReplacementDeterministicCombatAi &&
        params.mission.valid &&
        params.mission.family == BattleMissionFamily::Retrieval &&
        world.objective_.valid && world.objective_.transformProven &&
        world.objective_.missionIntentProven &&
        world.objective_.missionIntent ==
            BattleMissionObjectiveIntent::Retrieve) {
        world.retrieval_.active = true;
        world.retrieval_.phase = BattleRetrievalPhase::AwaitingContact;
        world.retrieval_.provenance =
            "replacement:wiki_retrieval_structure_contact_v1";
        world.retrieval_.contactAnchor = world.objective_.transform;
        world.retrieval_.contactRadius = 250.0;
    }
    world.updateMajorSystemRuntime();
    world.missionStartSnapshot_ = world.snapshot();
    return world;
}

EntityId BattleWorld::playerEntityId() const {
    return playerEntityId_;
}

uint64_t BattleWorld::tickIndex() const {
    return tickIndex_;
}

uint64_t BattleWorld::elapsedMs() const {
    return elapsedMs_;
}

double BattleWorld::fixedTickSeconds() const {
    return params_.fixedTickSeconds;
}

BattleMissionRuntimeState BattleWorld::missionRuntimeState() const {
    return missionRuntimeState_;
}

std::optional<BattleResult> BattleWorld::battleResult() const {
    return result_;
}

void BattleWorld::enqueueInput(BattleInputCommand command) {
    if (!isValid(command.entityId)) {
        command.entityId = playerEntityId_;
    }
    command.throttle = clampUnit(command.throttle);
    command.turn = clampUnit(command.turn);
    queuedInputs_.push_back(command);
}

void BattleWorld::destroyComponent(EntityId id, int componentId) {
    Combatant* combatant = findCombatant(id);
    if (!combatant) {
        throw std::runtime_error("battle component destroy references unknown entity id: " + std::to_string(id.value));
    }
    mech3d::destroyMechRuntimeComponent(combatant->mechRuntime, componentId);
    const auto rule = std::find_if(
        combatant->damageRules.begin(), combatant->damageRules.end(),
        [componentId](const mech3d::MechComponentDamageRule& candidate) {
            return candidate.componentId == componentId;
        });
    if (rule != combatant->damageRules.end() &&
        (labelContains(rule->debugLabel, "left_leg") ||
         labelContains(rule->debugLabel, "right_leg"))) {
        mech3d::hideMechRuntimeComponent(
            combatant->mechRuntime, componentId);
    }
    enterCombatantDeathState(*combatant);
}

void BattleWorld::hideComponent(EntityId id, int componentId) {
    Combatant* combatant = findCombatant(id);
    if (!combatant) {
        throw std::runtime_error("battle component hide references unknown entity id: " + std::to_string(id.value));
    }
    mech3d::hideMechRuntimeComponent(combatant->mechRuntime, componentId);
}

void BattleWorld::disableComponent(EntityId id, int componentId) {
    Combatant* combatant = findCombatant(id);
    if (!combatant) {
        throw std::runtime_error("battle component disable references unknown entity id: " + std::to_string(id.value));
    }
    mech3d::disableMechRuntimeComponent(combatant->mechRuntime, componentId);
}

void BattleWorld::setMissionStatus(EntityId id, CombatantMissionStatus status) {
    Combatant* combatant = findCombatant(id);
    if (!combatant) {
        throw std::runtime_error("battle mission status references unknown entity id: " + std::to_string(id.value));
    }
    combatant->missionStatus = status;
    if (status == CombatantMissionStatus::Destroyed) {
        enterCombatantDeathState(*combatant);
    }
}

void BattleWorld::resolveDeterministicCollisions(
    const std::vector<Transform>& previousTransforms) {
    if (!params_.deterministicCollisionRuntimeEnabled) {
        return;
    }
    if (previousTransforms.size() != combatants_.size()) {
        throw std::runtime_error("battle collision previous-transform count changed");
    }

    for (Combatant& combatant : combatants_) {
        combatant.collisionContact = false;
        if (combatant.collisionFeedbackCooldownTicksRemaining > 0u) {
            --combatant.collisionFeedbackCooldownTicksRemaining;
        }
    }

    const BattleTerrainCollisionObstacles& obstacles =
        params_.terrainCollisionObstacles;
    const auto terrainBlocked = [&obstacles](
                                    const Transform& transform,
                                    double mechRadius,
                                    TerrainFootprintContact* hit) {
        if (!obstacles.valid) {
            return false;
        }
        for (const BattleTerrainCollisionObstacle& obstacle : obstacles.entries) {
            if (terrainFootprintContact(obstacle, transform, mechRadius, hit)) {
                return true;
            }
        }
        return false;
    };
    const auto markContact = [this](Combatant& combatant) {
        combatant.collisionContact = true;
        ++combatant.collisionCount;
        combatant.lastCollisionTickIndex = tickIndex_;
    };
    const uint64_t feedbackCooldownTicks = static_cast<uint64_t>(std::max(
        1.0,
        std::ceil(params_.collisionFeedbackCooldownSeconds / params_.fixedTickSeconds)));
    const auto publishCollision = [this, feedbackCooldownTicks](
                                      Combatant& owner,
                                      BattleCollisionDiagnostic diagnostic) {
        if (owner.collisionFeedbackCooldownTicksRemaining > 0u) {
            return false;
        }
        diagnostic.valid = true;
        diagnostic.sequence = ++collisionSequence_;
        diagnostic.tickIndex = tickIndex_;
        diagnostic.runtimeEffect =
            BattleCollisionRuntimeEffect::PositionalSeparationNoDamage;
        diagnostic.damageSemanticsProven = false;
        diagnostic.damageApplied = 0;
        lastCollision_ = std::move(diagnostic);
        owner.collisionFeedbackCooldownTicksRemaining = feedbackCooldownTicks;
        return true;
    };

    if (obstacles.valid) {
        for (size_t index = 0; index < combatants_.size(); ++index) {
            Combatant& combatant = combatants_[index];
            if (combatant.airborne || combatant.missionStatus == CombatantMissionStatus::Destroyed) {
                continue;
            }
            TerrainFootprintContact footprintContact;
            const double mechRadius = combatant.collisionRadiusWorld;
            if (!terrainBlocked(combatant.transform, mechRadius, &footprintContact)) {
                continue;
            }
            const BattleTerrainCollisionObstacle* obstacle = footprintContact.obstacle;

            const Transform proposed = combatant.transform;
            const Transform& previous = previousTransforms[index];
            const double normalX = footprintContact.normalX;
            const double normalZ = footprintContact.normalZ;
            const double penetration = footprintContact.penetration;

            Transform resolved = previous;
            const bool previousWasBlocked = terrainBlocked(previous, mechRadius, nullptr);
            if (previousWasBlocked) {
                resolved = proposed;
                resolved.x += normalX * (std::max(0.0, penetration) + 1.0);
                resolved.z += normalZ * (std::max(0.0, penetration) + 1.0);
            } else {
                Transform backedOff = previous;
                backedOff.x += normalX * params_.collisionBackoffWorldUnits;
                backedOff.z += normalZ * params_.collisionBackoffWorldUnits;
                if (!terrainBlocked(backedOff, mechRadius, nullptr)) {
                    resolved = backedOff;
                }
            }
            combatant.transform.x = resolved.x;
            combatant.transform.z = resolved.z;
            if (params_.constrainToTerrainBounds) {
                combatant.boundaryContact =
                    clampToTerrainBounds(terrain_, combatant.transform) ||
                    combatant.boundaryContact;
            }
            const double speedBefore = combatant.forwardSpeed;
            combatant.forwardSpeed = 0.0;
            combatant.targetForwardSpeed = 0.0;
            markContact(combatant);

            BattleCollisionDiagnostic diagnostic;
            diagnostic.kind = BattleCollisionKind::TerrainObject;
            diagnostic.entityId = combatant.id;
            diagnostic.terrainObstacleId = obstacle->stableId;
            diagnostic.terrainRecordIndex = obstacle->recordIndex;
            diagnostic.normalX = normalX;
            diagnostic.normalZ = normalZ;
            diagnostic.penetration = penetration;
            diagnostic.displacement = distance2d(proposed, combatant.transform);
            diagnostic.speedBefore = speedBefore;
            diagnostic.speedAfter = combatant.forwardSpeed;
            diagnostic.provenance =
                "WLD_TERPCK_convex_footprint_model_radius_stop_and_80wu_separation_v3";
            publishCollision(combatant, std::move(diagnostic));
        }
    }

    const bool solidObjective =
        objective_.valid && objective_.activeObjectProven &&
        objective_.transformProven && !objective_.depleted &&
        objective_.missionIntentBoundToObjective &&
        (objective_.missionIntent == BattleMissionObjectiveIntent::Protect ||
         objective_.missionIntent == BattleMissionObjectiveIntent::Destroy ||
         objective_.missionIntent == BattleMissionObjectiveIntent::Disable);
    if (solidObjective) {
        for (size_t index = 0; index < combatants_.size(); ++index) {
            Combatant& combatant = combatants_[index];
            if (combatant.airborne ||
                combatant.missionStatus == CombatantMissionStatus::Destroyed) {
                continue;
            }
            const double contactDistance =
                combatant.collisionRadiusWorld +
                params_.objectiveCollisionRadiusWorld;
            const double dx = combatant.transform.x - objective_.transform.x;
            const double dz = combatant.transform.z - objective_.transform.z;
            const double distance = std::hypot(dx, dz);
            if (distance >= contactDistance) {
                continue;
            }

            double nx = distance > kMovementEpsilon
                ? dx / distance : -std::sin(combatant.transform.headingRadians);
            double nz = distance > kMovementEpsilon
                ? dz / distance : -std::cos(combatant.transform.headingRadians);
            if (std::hypot(nx, nz) <= kMovementEpsilon) {
                nx = 1.0;
                nz = 0.0;
            }
            const Transform proposed = combatant.transform;
            const Transform& previous = previousTransforms[index];
            const double previousDistance = std::hypot(
                previous.x - objective_.transform.x,
                previous.z - objective_.transform.z);
            if (previousDistance >= contactDistance &&
                !terrainBlocked(previous, combatant.collisionRadiusWorld, nullptr)) {
                combatant.transform.x = previous.x;
                combatant.transform.z = previous.z;
            } else {
                const double separation =
                    contactDistance - distance +
                    params_.collisionBackoffWorldUnits;
                combatant.transform.x += nx * separation;
                combatant.transform.z += nz * separation;
            }
            if (params_.constrainToTerrainBounds) {
                combatant.boundaryContact =
                    clampToTerrainBounds(terrain_, combatant.transform) ||
                    combatant.boundaryContact;
            }
            const double speedBefore = combatant.forwardSpeed;
            combatant.forwardSpeed = 0.0;
            combatant.targetForwardSpeed = 0.0;
            markContact(combatant);

            BattleCollisionDiagnostic diagnostic;
            diagnostic.kind = BattleCollisionKind::ObjectiveStructure;
            diagnostic.entityId = combatant.id;
            diagnostic.normalX = nx;
            diagnostic.normalZ = nz;
            diagnostic.penetration = contactDistance - distance;
            diagnostic.displacement =
                distance2d(proposed, combatant.transform);
            diagnostic.speedBefore = speedBefore;
            diagnostic.speedAfter = combatant.forwardSpeed;
            diagnostic.provenance =
                "replacement_wiki_structure_radial_footprint_v1";
            publishCollision(combatant, std::move(diagnostic));
        }
    }

    for (size_t first = 0; first < combatants_.size(); ++first) {
        Combatant& a = combatants_[first];
        if (a.airborne || a.missionStatus == CombatantMissionStatus::Destroyed) {
            continue;
        }
        for (size_t second = first + 1u; second < combatants_.size(); ++second) {
            Combatant& b = combatants_[second];
            if (b.airborne || b.missionStatus == CombatantMissionStatus::Destroyed) {
                continue;
            }
            const double dx = b.transform.x - a.transform.x;
            const double dz = b.transform.z - a.transform.z;
            const double distance = std::sqrt(dx * dx + dz * dz);
            const double contactDistance =
                a.collisionRadiusWorld + b.collisionRadiusWorld;
            if (distance >= contactDistance) {
                continue;
            }
            const double nx = distance > kMovementEpsilon ? dx / distance : 1.0;
            const double nz = distance > kMovementEpsilon ? dz / distance : 0.0;
            const double contactPenetration = contactDistance - distance;
            const double separation =
                contactPenetration + params_.collisionBackoffWorldUnits;
            Transform candidateA = a.transform;
            Transform candidateB = b.transform;
            candidateA.x -= nx * separation * 0.5;
            candidateA.z -= nz * separation * 0.5;
            candidateB.x += nx * separation * 0.5;
            candidateB.z += nz * separation * 0.5;
            const bool aBlocked = terrainBlocked(
                candidateA, a.collisionRadiusWorld, nullptr);
            const bool bBlocked = terrainBlocked(
                candidateB, b.collisionRadiusWorld, nullptr);
            if (!aBlocked && !bBlocked) {
                a.transform = candidateA;
                b.transform = candidateB;
            } else if (!aBlocked) {
                a.transform.x -= nx * separation;
                a.transform.z -= nz * separation;
            } else if (!bBlocked) {
                b.transform.x += nx * separation;
                b.transform.z += nz * separation;
            }
            if (params_.constrainToTerrainBounds) {
                a.boundaryContact = clampToTerrainBounds(terrain_, a.transform) || a.boundaryContact;
                b.boundaryContact = clampToTerrainBounds(terrain_, b.transform) || b.boundaryContact;
            }

            const double speedBefore = a.forwardSpeed;
            const double aVelocityTowardB =
                (std::sin(a.transform.headingRadians) * nx +
                 std::cos(a.transform.headingRadians) * nz) * a.forwardSpeed;
            const double bVelocityTowardA =
                (std::sin(b.transform.headingRadians) * -nx +
                 std::cos(b.transform.headingRadians) * -nz) * b.forwardSpeed;
            if (aVelocityTowardB > 0.0) {
                a.forwardSpeed = 0.0;
                a.targetForwardSpeed = 0.0;
            }
            if (bVelocityTowardA > 0.0) {
                b.forwardSpeed = 0.0;
                b.targetForwardSpeed = 0.0;
            }
            markContact(a);
            markContact(b);

            BattleCollisionDiagnostic diagnostic;
            diagnostic.kind = BattleCollisionKind::MechMech;
            diagnostic.entityId = a.id;
            diagnostic.otherEntityId = b.id;
            diagnostic.normalX = -nx;
            diagnostic.normalZ = -nz;
            diagnostic.penetration = contactPenetration;
            diagnostic.displacement = separation;
            diagnostic.speedBefore = speedBefore;
            diagnostic.speedAfter = a.forwardSpeed;
            diagnostic.provenance =
                "catalog_bind_pose_horizontal_radii_plus_80wu_separation_v1";
            if (publishCollision(a, diagnostic)) {
                b.collisionFeedbackCooldownTicksRemaining = feedbackCooldownTicks;
            } else if (publishCollision(b, std::move(diagnostic))) {
                a.collisionFeedbackCooldownTicksRemaining = feedbackCooldownTicks;
            }
        }
    }
}

void BattleWorld::updateMajorSystemRuntime() {
    for (Combatant& combatant : combatants_) {
        const BattleMajorSystemRuntimeState previous = combatant.majorSystems;
        BattleMajorSystemRuntimeState current = battleMajorSystemRuntimeState(
            combatant.mechRuntime.detailedDamage,
            elapsedMs_,
            params_.originalMajorSystemRuntimeEnabled);
        if (current.enabled && combatant.majorSystemRuntimeInitialized) {
            const std::array<std::tuple<BattleMajorSystemId,
                                        BattleMechSystemStatus,
                                        BattleMechSystemStatus>, 4>
                transitions{{
                    {BattleMajorSystemId::Sensors, previous.sensors, current.sensors},
                    {BattleMajorSystemId::Gyros, previous.gyros, current.gyros},
                    {BattleMajorSystemId::Engine, previous.engine, current.engine},
                    {BattleMajorSystemId::LifeSupport,
                     previous.lifeSupport,
                     current.lifeSupport},
                }};
            for (const auto& [system, before, after] : transitions) {
                if (static_cast<uint8_t>(after) <=
                    static_cast<uint8_t>(before)) {
                    continue;
                }
                combatant.lastMajorSystemWarning.valid = true;
                combatant.lastMajorSystemWarning.sequence =
                    ++combatant.majorSystemWarningSequence;
                combatant.lastMajorSystemWarning.tickIndex = tickIndex_;
                combatant.lastMajorSystemWarning.system = system;
                combatant.lastMajorSystemWarning.previousStatus = before;
                combatant.lastMajorSystemWarning.currentStatus = after;
            }
        }
        combatant.majorSystems = current;
        combatant.majorSystemRuntimeInitialized = current.enabled;
        if (combatant.majorSystems.enabled &&
            combatant.majorSystems.lifeSupportFailure) {
            combatant.missionStatus = CombatantMissionStatus::Destroyed;
        }
        enterCombatantDeathState(combatant);
        if (combatantMovementBlocked(combatant)) {
            combatant.throttle = 0.0;
            combatant.turn = 0.0;
            combatant.targetForwardSpeed = 0.0;
            combatant.forwardSpeed = 0.0;
            combatant.jumpJetsEnabled = false;
            combatant.jumpJetThrusting = false;
            combatant.jumpForwardThrusting = false;
            combatant.jumpLaunchImpulseRemaining = 0.0;
        }
        if (combatant.majorSystems.enabled &&
            combatant.playerControlled &&
            combatant.majorSystems.sensors ==
                BattleMechSystemStatus::Destroyed) {
            targetScan_.selectedTargetKind =
                BattleTargetScanTargetKind::None;
            targetScan_.selectedTargetEntityId = {};
            targetScan_.selectedTargetDistanceMeters = 0.0;
        }
        if (combatant.majorSystems.enabled || combatant.heat.enabled) {
            refreshCombatantWeaponState(params_, combatant);
        }
    }
}

void BattleWorld::tick() {
    if (missionTerminal(missionRuntimeState_)) {
        return;
    }

    shotEvents_.clear();
    impactEvents_.erase(
        std::remove_if(
            impactEvents_.begin(),
            impactEvents_.end(),
            [this](const BattleImpactEvent& event) {
                return !event.valid ||
                    (tickIndex_ >= event.impactTickIndex &&
                     tickIndex_ - event.impactTickIndex >= 12u);
            }),
        impactEvents_.end());
    updateMajorSystemRuntime();
    applyQueuedInputs();
    updateDeterministicEnemyRuntime();

    std::vector<Transform> previousTransforms;
    previousTransforms.reserve(combatants_.size());
    for (const Combatant& combatant : combatants_) {
        previousTransforms.push_back(combatant.transform);
    }

    const uint64_t tickMs = fixedTickMs(params_);
    const double dt = params_.fixedTickSeconds;
    for (Combatant& combatant : combatants_) {
        combatant.turn = clampUnit(combatant.turn);
        combatant.throttle = clampUnit(combatant.throttle);
        if (combatantMovementBlocked(combatant)) {
            combatant.targetForwardSpeed = 0.0;
            combatant.forwardSpeed = 0.0;
        } else if (combatant.airborne || combatant.jumpJetsEnabled) {
            combatant.targetForwardSpeed = combatant.forwardSpeed;
            if (combatant.jumpJetsEnabled && combatant.jumpForwardThrusting && combatant.jumpFuel > 0.0) {
                if (combatant.forwardSpeed >= 0.0) {
                    combatant.forwardSpeed = std::min(
                        combatant.maxForwardSpeed,
                        combatant.forwardSpeed + params_.jumpJetForwardAcceleration * dt);
                }
            }
            combatant.targetForwardSpeed = combatant.forwardSpeed;
        } else {
            combatant.targetForwardSpeed =
                targetSpeedForThrottle(combatant, combatant.throttle) *
                (combatant.majorSystems.enabled
                     ? combatant.majorSystems.gyroMovementScale
                     : 1.0);
            const double maxSpeedDelta = speedStepRate(params_, combatant.forwardSpeed, combatant.targetForwardSpeed) * dt;
            combatant.forwardSpeed = moveToward(combatant.forwardSpeed, combatant.targetForwardSpeed, maxSpeedDelta);
        }
        combatant.transform.headingRadians +=
            combatant.turn * params_.maxTurnRateRadians * dt;
        combatant.transform.x += std::sin(combatant.transform.headingRadians) * combatant.forwardSpeed * dt;
        combatant.transform.z += std::cos(combatant.transform.headingRadians) * combatant.forwardSpeed * dt;
        evaluateMissionRuntimeAfterMovement();
        combatant.boundaryContact = false;
        if (params_.constrainToTerrainBounds) {
            combatant.boundaryContact = clampToTerrainBounds(terrain_, combatant.transform);
        }
        const bool originalAiGroundPoseOwned =
            !combatant.playerControlled &&
            combatant.originalAiMotion.initialized &&
            combatant.originalAiMotion.worldPositionBindingExact &&
            combatant.originalAiMotion.acceptedPoseCommitCount > 0u &&
            !combatant.airborne &&
            !combatant.jumpJetsEnabled;
        if (originalAiGroundPoseOwned) {
            combatant.verticalSpeed = 0.0;
            combatant.jumpLaunchImpulseRemaining = 0.0;
            combatant.jumpJetThrusting = false;
            combatant.jumpForwardThrusting = false;
            if (combatant.jumpCapable) {
                combatant.jumpFuel = std::min(
                    combatant.jumpMaxFuel,
                    combatant.jumpFuel +
                        params_.jumpJetFuelRechargePerSecond * dt);
            } else {
                combatant.jumpFuel = 0.0;
            }
        } else if (combatant.airborne || combatant.jumpJetsEnabled || combatant.transform.y > 0.0) {
            bool launchThrusting = false;
            double fuelBurnSeconds = 0.0;
            if (combatant.jumpJetsEnabled && combatant.jumpLaunchImpulseRemaining > 0.0 && combatant.jumpFuel > 0.0) {
                const double launchSeconds = std::max(0.001, params_.jumpJetInitialImpulseSeconds);
                const double launchAcceleration = params_.jumpJetInitialImpulse / launchSeconds;
                const double deltaV = std::min(combatant.jumpLaunchImpulseRemaining, launchAcceleration * dt);
                combatant.verticalSpeed += deltaV;
                combatant.jumpLaunchImpulseRemaining -= deltaV;
                fuelBurnSeconds += deltaV / launchAcceleration;
                launchThrusting = true;
            }
            if (combatant.jumpJetsEnabled && combatant.jumpJetThrusting && combatant.jumpFuel > 0.0) {
                combatant.verticalSpeed += params_.jumpJetUpAcceleration * dt;
                fuelBurnSeconds += dt;
            }
            combatant.jumpJetThrusting = combatant.jumpJetThrusting || launchThrusting;
            if (fuelBurnSeconds > 0.0) {
                combatant.jumpFuel = std::max(0.0, combatant.jumpFuel - params_.jumpJetFuelBurnPerSecond * fuelBurnSeconds);
                if (combatant.jumpFuel <= 0.0) {
                    combatant.jumpJetsEnabled = false;
                    combatant.jumpLaunchImpulseRemaining = 0.0;
                }
            }
            combatant.verticalSpeed -= params_.jumpJetGravity * dt;
            combatant.transform.y += combatant.verticalSpeed * dt;
            combatant.airborne = true;
            if (combatant.transform.y <= 0.0) {
                const double impactSpeed = std::max(0.0, -combatant.verticalSpeed);
                combatant.landingImpactSpeed = impactSpeed;
                combatant.hardLanding = impactSpeed > params_.jumpJetHardLandingSpeed;
                combatant.knockdownCandidate = combatant.hardLanding;
                combatant.transform.y = 0.0;
                combatant.verticalSpeed = 0.0;
                combatant.airborne = false;
                combatant.jumpJetsEnabled = false;
                combatant.jumpLaunchImpulseRemaining = 0.0;
            }
        } else {
            combatant.transform.y = 0.0;
            combatant.verticalSpeed = 0.0;
            combatant.jumpLaunchImpulseRemaining = 0.0;
            combatant.airborne = false;
            combatant.jumpJetThrusting = false;
            combatant.jumpForwardThrusting = false;
            if (combatant.jumpCapable) {
                combatant.jumpFuel = std::min(
                    combatant.jumpMaxFuel,
                    combatant.jumpFuel + params_.jumpJetFuelRechargePerSecond * dt);
            } else {
                combatant.jumpFuel = 0.0;
            }
        }

        if (combatant.deathAnimationStarted) {
            const uint64_t lastFrameElapsed =
                deathAnimationLastFrameElapsedMs(combatant);
            combatant.walkAnimationElapsedMs = std::min(
                lastFrameElapsed,
                combatant.walkAnimationElapsedMs + tickMs);
        } else if (combatantUsesWalkAnimation(combatant)) {
            combatant.walkAnimationElapsedMs += walkAnimationTickMs(combatant, tickMs);
        } else {
            combatant.walkAnimationElapsedMs = 0;
        }
    }

    resolveDeterministicCollisions(previousTransforms);
    updateReplacementMissionRuntime();

    if (!missionTerminal(missionRuntimeState_)) {
        updateDeterministicCombatRuntime();
        updateImpactEventHistory();
        updateMajorSystemRuntime();
        evaluateMissionRuntimeAfterObjective();
        evaluateMissionRuntimeAfterCombat();
    }

    refreshTargetScanState();

    ++tickIndex_;
    elapsedMs_ += tickMs;
}

void BattleWorld::runTicks(uint64_t tickCount) {
    for (uint64_t i = 0; i < tickCount; ++i) {
        tick();
    }
}

const BattleSnapshot& BattleWorld::missionStartSnapshot() const {
    return missionStartSnapshot_;
}

BattleSnapshot BattleWorld::snapshot() const {
    BattleSnapshot result;
    result.tickIndex = tickIndex_;
    result.elapsedMs = elapsedMs_;
    result.combatAiPolicy = params_.combatAiPolicy;
    if (result.combatAiPolicy ==
        BattleCombatAiPolicy::ReplacementDeterministicCombatAi) {
        result.combatAiPolicyProvenance =
            "replacement:phase14_wiki_five_scenarios_v13_damage_retreat";
    }
    result.mission = params_.mission;
    result.contract = params_.contract;
    result.oppositionRoster = params_.oppositionRoster;
    result.terrain = terrain_;
    result.setup = params_.setupMetadata;
    result.objective = objective_;
    result.retrieval = retrieval_;
    result.battlefieldBoundary = params_.battlefieldBoundary;
    result.missionRuntimeState = missionRuntimeState_;
    if (result_) {
        result.result = *result_;
    }
    result.conclusionDelay = conclusionDelay_;
    result.camera = makeCameraSnapshot(params_, findCombatant(playerEntityId_));
    result.combatants.reserve(combatants_.size());
    for (const Combatant& combatant : combatants_) {
        result.combatants.push_back(makeCombatantSnapshot(params_, combatant));
    }
    result.projectiles = projectiles_;
    result.shotEvents = shotEvents_;
    result.impactEvents = impactEvents_;
    result.lastShot = lastShot_;
    result.lastCollision = lastCollision_;
    result.cockpitRadar = cockpitRadar_;
    result.targetScan = targetScan_;
    return result;
}

EntityId BattleWorld::spawnCombatant(
    std::string mechPresetId,
    bool playerControlled,
    Transform transform,
    BattleCombatantRosterMetadata roster) {
    if (mechPresetId.empty()) {
        throw std::runtime_error("battle combatant requires a mech preset id");
    }

    Combatant combatant;
    combatant.id = EntityId{nextEntityId_++};
    combatant.mechPresetId = std::move(mechPresetId);
    combatant.playerControlled = playerControlled;
    combatant.roster = std::move(roster);
    combatant.transform = transform;
    const mech3d::MechCatalogMobility mobility = mech3d::catalogMobility(combatant.mechPresetId);
    if (mobility.maxSpeedKph <= 0) {
        throw std::runtime_error("battle combatant requires a positive original maximum speed");
    }
    const double speedScale = static_cast<double>(mobility.maxSpeedKph) / kLocustOriginalMaxSpeedKph;
    combatant.originalMaxSpeedKph = mobility.maxSpeedKph;
    combatant.maxForwardSpeed = params_.maxForwardSpeed * speedScale;
    combatant.maxReverseSpeed = params_.maxReverseSpeed * speedScale;
    if (params_.deterministicCollisionRuntimeEnabled) {
        combatant.collisionRadiusWorld =
            collisionRadiusForPreset(params_, combatant.mechPresetId);
        if (!(combatant.collisionRadiusWorld > 0.0) ||
            !std::isfinite(combatant.collisionRadiusWorld)) {
            throw std::runtime_error("battle combatant collision radius is invalid");
        }
    }
    combatant.jumpCapable = mobility.jumpCapable();
    combatant.jumpCapacityMeters = mobility.jumpCapacityMeters;
    combatant.jumpJetCount = mobility.jumpJetCount;
    combatant.jumpFuel = combatant.jumpCapable ? params_.jumpJetMaxFuel : 0.0;
    combatant.jumpMaxFuel = combatant.jumpCapable ? params_.jumpJetMaxFuel : 0.0;
    combatant.jumpActivationFuel = combatant.jumpCapable ? params_.jumpJetActivationFuel : 0.0;
    combatant.mechRuntime.currentAnimationId = "walk";
    combatant.damageRules = mech3d::catalogComponentDamageRules(combatant.mechPresetId);
    initializeCombatantWeapons(params_, combatant);
    combatants_.push_back(combatant);
    return combatants_.back().id;
}

Combatant* BattleWorld::findCombatant(EntityId id) {
    const auto it = std::find_if(
        combatants_.begin(),
        combatants_.end(),
        [id](const Combatant& combatant) {
            return combatant.id == id;
        });
    return it == combatants_.end() ? nullptr : &*it;
}

const Combatant* BattleWorld::findCombatant(EntityId id) const {
    const auto it = std::find_if(
        combatants_.begin(),
        combatants_.end(),
        [id](const Combatant& combatant) {
            return combatant.id == id;
        });
    return it == combatants_.end() ? nullptr : &*it;
}

namespace {

bool detailedCenterTorsoDestroyed(const Combatant& combatant) {
    const auto& detailed = combatant.mechRuntime.detailedDamage;
    if (!detailed.valid) {
        return false;
    }
    const auto& centerTorso = detailed.internalSections[static_cast<size_t>(
        mech3d::MechInternalSectionId::CenterTorso)];
    return centerTorso.structureMaximum > 0u &&
        centerTorso.structureRemaining == 0u;
}

uint64_t deathAnimationLastFrameElapsedMs(const Combatant& combatant) {
    const mech3d::MechCatalogAnimationKind kind =
        mech3d::catalogAnimationKind(combatant.mechPresetId, "death");
    if (kind == mech3d::MechCatalogAnimationKind::ComponentFrames) {
        const mech3d::ModelAnimationDefinition animation =
            mech3d::makeCatalogAnimationDefinition(
                combatant.mechPresetId, "death");
        const int frameCount = mech3d::animationFrameCount(animation);
        const int frameDurationMs = animation.sequences.empty()
            ? 0 : animation.sequences.front().frameDurationMs;
        return frameCount > 1 && frameDurationMs > 0
            ? static_cast<uint64_t>(frameCount - 1) *
                  static_cast<uint64_t>(frameDurationMs)
            : 0u;
    }
    if (kind == mech3d::MechCatalogAnimationKind::AssemblyFrames) {
        const mech3d::ModelAssemblyFrameAnimationDefinition animation =
            mech3d::makeCatalogAssemblyFrameAnimationDefinition(
                combatant.mechPresetId, "death");
        const int frameCount =
            mech3d::assemblyFrameAnimationFrameCount(animation);
        return frameCount > 1 && animation.frameDurationMs > 0
            ? static_cast<uint64_t>(frameCount - 1) *
                  static_cast<uint64_t>(animation.frameDurationMs)
            : 0u;
    }
    return 0u;
}

void enterCombatantDeathState(Combatant& combatant) {
    const bool destroyedByComponent = mech3d::resolveMechRuntimeState(
        combatant.mechRuntime, combatant.damageRules).mechDestroyed;
    if (combatant.missionStatus != CombatantMissionStatus::Destroyed &&
        !destroyedByComponent &&
        !detailedCenterTorsoDestroyed(combatant)) {
        return;
    }
    combatant.missionStatus = CombatantMissionStatus::Destroyed;
    combatant.throttle = 0.0;
    combatant.turn = 0.0;
    combatant.targetForwardSpeed = 0.0;
    combatant.forwardSpeed = 0.0;
    combatant.jumpJetsEnabled = false;
    combatant.jumpJetThrusting = false;
    combatant.jumpForwardThrusting = false;
    combatant.jumpLaunchImpulseRemaining = 0.0;
    if (!combatant.deathAnimationStarted) {
        combatant.deathAnimationStarted = true;
        combatant.walkAnimationElapsedMs = 0u;
        mech3d::setMechRuntimeAnimation(combatant.mechRuntime, "death");
    }
}

} // namespace

bool combatantMechDestroyed(const Combatant& combatant) {
    return mech3d::resolveMechRuntimeState(
               combatant.mechRuntime, combatant.damageRules).mechDestroyed ||
           detailedCenterTorsoDestroyed(combatant) ||
           combatant.missionStatus == CombatantMissionStatus::Destroyed;
}

namespace {

bool targetScanCandidate(
    const Combatant& player,
    const Combatant& candidate,
    double rangeWorld) {
    if (candidate.id == player.id ||
        candidate.missionStatus != CombatantMissionStatus::Active ||
        combatantMechDestroyed(candidate) ||
        (candidate.roster.team != BattleTeam::Player &&
         candidate.roster.team != BattleTeam::Opposing)) {
        return false;
    }
    return std::hypot(
               candidate.transform.x - player.transform.x,
               candidate.transform.z - player.transform.z) <= rangeWorld;
}

bool targetScanObjectiveCandidate(
    const Combatant& player,
    const BattleObjectiveState& objective,
    double rangeWorld) {
    return objective.valid && objective.staticModel.valid && !objective.depleted &&
        std::hypot(
            objective.transform.x - player.transform.x,
            objective.transform.z - player.transform.z) <= rangeWorld;
}

void clearTargetScanSelection(BattleTargetScanState& state) {
    state.selectedTargetKind = BattleTargetScanTargetKind::None;
    state.selectedTargetEntityId = {};
    state.selectedTargetDistanceMeters = 0.0;
}

} // namespace

void BattleWorld::cycleTargetScan() {
    ++targetScan_.commandSequence;
    targetScan_.lastCommandTickIndex = tickIndex_;

    const Combatant* player = findCombatant(playerEntityId_);
    if (player == nullptr) {
        clearTargetScanSelection(targetScan_);
        return;
    }

    const double rangeWorld =
        static_cast<double>(targetScan_.rangeMeters) *
        battleTargetScanWorldUnitsPerMeter();
    std::vector<const Combatant*> candidates;
    for (const Combatant& candidate : combatants_) {
        if (targetScanCandidate(*player, candidate, rangeWorld)) {
            candidates.push_back(&candidate);
        }
    }
    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const Combatant* left, const Combatant* right) {
            return left->id.value < right->id.value;
        });
    const bool objectiveCandidate =
        targetScanObjectiveCandidate(*player, objective_, rangeWorld);
    if (candidates.empty() && !objectiveCandidate) {
        clearTargetScanSelection(targetScan_);
        return;
    }

    if (targetScan_.selectedTargetKind ==
            BattleTargetScanTargetKind::Combatant) {
        const auto current = std::find_if(
            candidates.begin(),
            candidates.end(),
            [this](const Combatant* candidate) {
                return candidate->id == targetScan_.selectedTargetEntityId;
            });
        if (current != candidates.end() && std::next(current) != candidates.end()) {
            const Combatant* selected = *std::next(current);
            targetScan_.selectedTargetKind =
                BattleTargetScanTargetKind::Combatant;
            targetScan_.selectedTargetEntityId = selected->id;
            targetScan_.selectedTargetDistanceMeters = std::hypot(
                selected->transform.x - player->transform.x,
                selected->transform.z - player->transform.z) /
                battleTargetScanWorldUnitsPerMeter();
            return;
        }
        if (current != candidates.end() && objectiveCandidate) {
            targetScan_.selectedTargetKind =
                BattleTargetScanTargetKind::Objective;
            targetScan_.selectedTargetEntityId = {};
            targetScan_.selectedTargetDistanceMeters = std::hypot(
                objective_.transform.x - player->transform.x,
                objective_.transform.z - player->transform.z) /
                battleTargetScanWorldUnitsPerMeter();
            return;
        }
    }

    if (!candidates.empty()) {
        const Combatant* selected = candidates.front();
        targetScan_.selectedTargetKind =
            BattleTargetScanTargetKind::Combatant;
        targetScan_.selectedTargetEntityId = selected->id;
        targetScan_.selectedTargetDistanceMeters = std::hypot(
            selected->transform.x - player->transform.x,
            selected->transform.z - player->transform.z) /
            battleTargetScanWorldUnitsPerMeter();
        return;
    }

    targetScan_.selectedTargetKind = BattleTargetScanTargetKind::Objective;
    targetScan_.selectedTargetEntityId = {};
    targetScan_.selectedTargetDistanceMeters = std::hypot(
        objective_.transform.x - player->transform.x,
        objective_.transform.z - player->transform.z) /
        battleTargetScanWorldUnitsPerMeter();
}

void BattleWorld::refreshTargetScanState() {
    if (!battleTargetScanHasSelection(targetScan_)) {
        targetScan_.selectedTargetDistanceMeters = 0.0;
        return;
    }
    const Combatant* player = findCombatant(playerEntityId_);
    const double rangeWorld =
        static_cast<double>(targetScan_.rangeMeters) *
        battleTargetScanWorldUnitsPerMeter();
    if (player == nullptr) {
        clearTargetScanSelection(targetScan_);
        return;
    }

    if (targetScan_.selectedTargetKind ==
            BattleTargetScanTargetKind::Objective) {
        if (!targetScanObjectiveCandidate(*player, objective_, rangeWorld)) {
            clearTargetScanSelection(targetScan_);
            return;
        }
        targetScan_.selectedTargetDistanceMeters = std::hypot(
            objective_.transform.x - player->transform.x,
            objective_.transform.z - player->transform.z) /
            battleTargetScanWorldUnitsPerMeter();
        return;
    }

    const Combatant* selected =
        findCombatant(targetScan_.selectedTargetEntityId);
    if (selected == nullptr ||
        !targetScanCandidate(*player, *selected, rangeWorld)) {
        clearTargetScanSelection(targetScan_);
        return;
    }
    targetScan_.selectedTargetDistanceMeters = std::hypot(
        selected->transform.x - player->transform.x,
        selected->transform.z - player->transform.z) /
        battleTargetScanWorldUnitsPerMeter();
}

bool combatantCanFireWeapon(const BattleStartParams& params, const Combatant& combatant) {
    if (combatant.missionStatus != CombatantMissionStatus::Active || combatantMechDestroyed(combatant)) {
        return false;
    }
    if (combatantControlShutdown(combatant)) {
        return false;
    }
    if (params.mechSystemMaxDamage > 0 &&
        systemDamageForRole(
            combatant.mechRuntime.detailedDamage,
            BattleMechSystemRole::Weapons) >= params.mechSystemMaxDamage) {
        return false;
    }
    return true;
}

bool combatantCanRetreat(const BattleStartParams& params, const Combatant& combatant) {
    if (combatant.missionStatus != CombatantMissionStatus::Active || combatantMechDestroyed(combatant)) {
        return false;
    }
    if (combatant.maxForwardSpeed <= 0.0) {
        return false;
    }
    if (params.combatAiPolicy ==
        BattleCombatAiPolicy::ReplacementDeterministicCombatAi) {
        return !combatantMovementBlocked(combatant);
    }
    return params.mechSystemMaxDamage <= 0 ||
           systemDamageForRole(
               combatant.mechRuntime.detailedDamage,
               BattleMechSystemRole::Mobility) < params.mechSystemMaxDamage;
}

int combatantRemainingDurabilityPercent(const Combatant& combatant) {
    const auto& damage = combatant.mechRuntime.detailedDamage;
    if (!damage.valid) {
        return 100;
    }
    uint64_t remaining = 0u;
    uint64_t maximum = 0u;
    for (const auto& section : damage.armorSections) {
        remaining += section.armorRemaining;
        maximum += section.armorMaximum;
    }
    for (const auto& section : damage.internalSections) {
        remaining += section.structureRemaining;
        maximum += section.structureMaximum;
    }
    if (maximum == 0u) {
        return 100;
    }
    return static_cast<int>((remaining * 100u) / maximum);
}

bool combatantShouldRetreatFromDamage(const BattleStartParams& params, const Combatant& combatant) {
    if (!params.enemyRetreatOnDamageEnabled ||
        !combatantCanRetreat(params, combatant)) {
        return false;
    }
    if (params.combatAiPolicy ==
        BattleCombatAiPolicy::ReplacementDeterministicCombatAi) {
        return combatantRemainingDurabilityPercent(combatant) <=
            params.enemyRetreatRemainingDurabilityPercent;
    }
    return params.enemyRetreatCoreDamageThreshold > 0 &&
        systemDamageForRole(
            combatant.mechRuntime.detailedDamage,
            BattleMechSystemRole::Core) >=
                params.enemyRetreatCoreDamageThreshold;
}

void applySystemDestroyedEffect(Combatant& target, BattleMechSystemRole role) {
    const std::vector<int> componentIds = catalogSystemComponentIdsForRole(target, role);
    if (role == BattleMechSystemRole::Weapons) {
        for (int componentId : componentIds) {
            mech3d::hideMechRuntimeComponent(target.mechRuntime, componentId);
            mech3d::disableMechRuntimeComponent(target.mechRuntime, componentId);
        }
        return;
    }
    if (role == BattleMechSystemRole::JumpJets) {
        target.jumpCapable = false;
        target.jumpJetsEnabled = false;
        target.jumpFuel = 0.0;
        target.jumpMaxFuel = 0.0;
        target.jumpActivationFuel = 0.0;
        return;
    }
    if (!componentIds.empty()) {
        mech3d::destroyMechRuntimeComponent(target.mechRuntime, componentIds.front());
    }
    if (role == BattleMechSystemRole::Core ||
        role == BattleMechSystemRole::Cockpit ||
        role == BattleMechSystemRole::Mobility) {
        target.missionStatus = CombatantMissionStatus::Destroyed;
    }
}

void applyDeterministicSystemDamage(
    const BattleStartParams& params,
    Combatant& attacker,
    Combatant& target,
    BattleMechSystemRole role) {
    int& damage = mutableSystemDamageForRole(
        target.mechRuntime.detailedDamage,
        role);
    const int maxDamage = params.mechSystemMaxDamage;
    const bool alreadyDestroyed = maxDamage > 0 && damage >= maxDamage;
    damage = std::min(maxDamage, damage + params.weaponDamagePerHit);
    target.lastDamageSourceEntityId = attacker.id;
    attacker.lastWeaponTargetEntityId = target.id;
    attacker.lastWeaponTargetSystem = role;
    attacker.weaponHitsLanded += 1;
    if (!alreadyDestroyed && maxDamage > 0 && damage >= maxDamage) {
        applySystemDestroyedEffect(target, role);
    }
}

bool objectiveCanTakeDamage(const BattleObjectiveState& objective) {
    return objective.valid && objective.maxDamage > 0 && !objective.depleted;
}

const BattleMechHitProfile* hitProfileForPreset(
    const BattleStartParams& params,
    const std::string& mechPresetId) {
    const auto found = std::find_if(
        params.mechHitProfiles.begin(),
        params.mechHitProfiles.end(),
        [&mechPresetId](const BattleMechHitProfile& profile) {
            return profile.valid && profile.mechPresetId == mechPresetId;
        });
    return found == params.mechHitProfiles.end() ? nullptr : &*found;
}

BattleHitPoint subtract(BattleHitPoint left, BattleHitPoint right) {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

BattleHitPoint add(BattleHitPoint left, BattleHitPoint right) {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

BattleHitPoint multiply(BattleHitPoint point, double scale) {
    return {point.x * scale, point.y * scale, point.z * scale};
}

double dot(BattleHitPoint left, BattleHitPoint right) {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

BattleHitPoint cross(BattleHitPoint left, BattleHitPoint right) {
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

BattleHitPoint normalized(BattleHitPoint point) {
    const double length = std::sqrt(dot(point, point));
    if (!(length > 0.0) || !std::isfinite(length)) {
        return {};
    }
    return multiply(point, 1.0 / length);
}

struct BattleAimRay {
    BattleHitPoint origin;
    BattleHitPoint direction;
};

BattleAimRay crosshairAimRay(
    const BattleStartParams& params,
    const Combatant& attacker,
    const BattleMechHitProfile& profile) {
    constexpr double kPi = 3.14159265358979323846;
    const double heading = attacker.transform.headingRadians +
        torsoYawRadians(params, attacker.torsoYawStep);
    const double crosshairY = profile.neutralCrosshairY -
        static_cast<double>(attacker.aimPitchStep) *
            profile.crosshairPixelsPerAimStep;
    const double halfHeight =
        static_cast<double>(profile.cockpitViewportHeight) * 0.5;
    const double normalizedScreenY =
        halfHeight > 0.0 ? (halfHeight - crosshairY) / halfHeight : 0.0;
    const double verticalSlope = normalizedScreenY * std::tan(
        profile.verticalFieldOfViewDegrees * kPi / 360.0);
    BattleAimRay ray;
    ray.direction = normalized({
        std::sin(heading),
        verticalSlope,
        std::cos(heading),
    });
    ray.origin = {
        attacker.transform.x +
            std::sin(heading) * params.cockpitCameraForwardOffset,
        attacker.transform.y + profile.aimOriginHeight,
        attacker.transform.z +
            std::cos(heading) * params.cockpitCameraForwardOffset,
    };
    return ray;
}

BattleHitPoint worldPointToTargetLocal(
    BattleHitPoint point,
    const Transform& target) {
    const double dx = point.x - target.x;
    const double dz = point.z - target.z;
    const double cosine = std::cos(target.headingRadians);
    const double sine = std::sin(target.headingRadians);
    return {
        cosine * dx - sine * dz,
        point.y - target.y,
        sine * dx + cosine * dz,
    };
}

BattleHitPoint targetLocalPointToWorld(
    BattleHitPoint point,
    const Transform& target) {
    const double cosine = std::cos(target.headingRadians);
    const double sine = std::sin(target.headingRadians);
    return {
        target.x + cosine * point.x + sine * point.z,
        target.y + point.y,
        target.z - sine * point.x + cosine * point.z,
    };
}

BattleHitPoint worldDirectionToTargetLocal(
    BattleHitPoint direction,
    const Transform& target) {
    const double cosine = std::cos(target.headingRadians);
    const double sine = std::sin(target.headingRadians);
    return {
        cosine * direction.x - sine * direction.z,
        direction.y,
        sine * direction.x + cosine * direction.z,
    };
}

bool rayTriangleDistance(
    BattleHitPoint origin,
    BattleHitPoint direction,
    const BattleMechHitTriangle& triangle,
    double& distance) {
    constexpr double kEpsilon = 1.0e-9;
    const BattleHitPoint edge1 = subtract(triangle.b, triangle.a);
    const BattleHitPoint edge2 = subtract(triangle.c, triangle.a);
    const BattleHitPoint p = cross(direction, edge2);
    const double determinant = dot(edge1, p);
    if (std::abs(determinant) <= kEpsilon) {
        return false;
    }
    const double inverse = 1.0 / determinant;
    const BattleHitPoint translated = subtract(origin, triangle.a);
    const double u = dot(translated, p) * inverse;
    if (u < 0.0 || u > 1.0) {
        return false;
    }
    const BattleHitPoint q = cross(translated, edge1);
    const double v = dot(direction, q) * inverse;
    if (v < 0.0 || u + v > 1.0) {
        return false;
    }
    const double candidate = dot(edge2, q) * inverse;
    if (candidate <= kEpsilon || !std::isfinite(candidate)) {
        return false;
    }
    distance = candidate;
    return true;
}

const BattleProjectileHitProfile* projectileHitProfileForVisualClass(
    const BattleStartParams& params,
    uint16_t originalVisualClass) {
    const auto found = std::find_if(
        params.projectileHitProfiles.begin(),
        params.projectileHitProfiles.end(),
        [originalVisualClass](const BattleProjectileHitProfile& profile) {
            return profile.valid &&
                profile.originalVisualClass == originalVisualClass;
        });
    return found == params.projectileHitProfiles.end() ? nullptr : &*found;
}

struct BattleHitBounds {
    bool valid = false;
    BattleHitPoint minimum;
    BattleHitPoint maximum;
};

void includeHitBounds(BattleHitBounds& bounds, BattleHitPoint point) {
    if (!bounds.valid) {
        bounds.valid = true;
        bounds.minimum = point;
        bounds.maximum = point;
        return;
    }
    bounds.minimum.x = std::min(bounds.minimum.x, point.x);
    bounds.minimum.y = std::min(bounds.minimum.y, point.y);
    bounds.minimum.z = std::min(bounds.minimum.z, point.z);
    bounds.maximum.x = std::max(bounds.maximum.x, point.x);
    bounds.maximum.y = std::max(bounds.maximum.y, point.y);
    bounds.maximum.z = std::max(bounds.maximum.z, point.z);
}

BattleHitBounds hitProfileBounds(
    const BattleMechHitProfile& profile,
    const std::function<bool(const BattleMechHitTriangle&)>& include) {
    BattleHitBounds bounds;
    for (const BattleMechHitTriangle& triangle : profile.triangles) {
        if (!include(triangle)) {
            continue;
        }
        includeHitBounds(bounds, triangle.a);
        includeHitBounds(bounds, triangle.b);
        includeHitBounds(bounds, triangle.c);
    }
    return bounds;
}

std::optional<BattleHitPoint> battleModelWeaponMount(
    const BattleMechHitProfile& profile,
    const std::string& locationId,
    const Transform& transform,
    double torsoYawRadiansValue) {
    if (!profile.valid || profile.triangles.empty() || locationId.empty()) {
        return std::nullopt;
    }
    const BattleHitBounds all = hitProfileBounds(
        profile, [](const BattleMechHitTriangle&) { return true; });
    const BattleHitBounds torso = hitProfileBounds(
        profile,
        [](const BattleMechHitTriangle& triangle) {
            return triangle.torsoComposite;
        });
    const auto sectionBounds = [&profile](mech3d::MechArmorSectionId section) {
        return hitProfileBounds(
            profile,
            [section](const BattleMechHitTriangle& triangle) {
                return !triangle.torsoComposite &&
                    triangle.directArmorSection == section;
            });
    };
    const BattleHitBounds leftArm = sectionBounds(
        mech3d::MechArmorSectionId::LeftArm);
    const BattleHitBounds rightArm = sectionBounds(
        mech3d::MechArmorSectionId::RightArm);

    BattleHitBounds selected;
    if (locationId == "LA") {
        selected = leftArm;
    } else if (locationId == "RA") {
        selected = rightArm;
    } else if (locationId == "HD") {
        selected = sectionBounds(mech3d::MechArmorSectionId::Head);
    } else if (locationId == "LL") {
        selected = sectionBounds(mech3d::MechArmorSectionId::LeftLeg);
    } else if (locationId == "RL") {
        selected = sectionBounds(mech3d::MechArmorSectionId::RightLeg);
    } else {
        selected = torso.valid ? torso : all;
    }
    if (!all.valid || !selected.valid) {
        return std::nullopt;
    }

    double mountX = (selected.minimum.x + selected.maximum.x) * 0.5;
    if (locationId == "LT" || locationId == "CT" || locationId == "RT") {
        const double width = all.maximum.x - all.minimum.x;
        const double lowThirdCenter = all.minimum.x + width / 6.0;
        const double highThirdCenter = all.maximum.x - width / 6.0;
        const bool leftIsLow = leftArm.valid && rightArm.valid &&
            (leftArm.minimum.x + leftArm.maximum.x) <
                (rightArm.minimum.x + rightArm.maximum.x);
        if (locationId == "LT") {
            mountX = leftIsLow ? lowThirdCenter : highThirdCenter;
        } else if (locationId == "RT") {
            mountX = leftIsLow ? highThirdCenter : lowThirdCenter;
        } else {
            mountX = (all.minimum.x + all.maximum.x) * 0.5;
        }
    }
    BattleHitPoint modelPoint{
        mountX,
        (selected.minimum.y + selected.maximum.y) * 0.5,
        selected.maximum.z,
    };
    const double totalHeading =
        transform.headingRadians + torsoYawRadiansValue;
    const double cosine = std::cos(totalHeading);
    const double sine = std::sin(totalHeading);
    return BattleHitPoint{
        transform.x + cosine * modelPoint.x + sine * modelPoint.z,
        transform.y + modelPoint.y,
        transform.z - sine * modelPoint.x + cosine * modelPoint.z,
    };
}

struct BattleProjectileModelPose {
    BattleHitPoint position;
    BattleHitPoint direction;
};

BattleProjectileModelPose battleProjectileModelPose(
    const BattleProjectileState& projectile,
    bool targetPointValid,
    BattleHitPoint targetPoint) {
    BattleProjectileModelPose pose{
        projectile.position,
        normalized(projectile.direction),
    };
    if (!projectile.launchModelMountValid ||
        !projectile.pendingShot.valid) {
        return pose;
    }
    if (!projectile.targetBound || !targetPointValid) {
        pose.position = add(
            projectile.launchModelMount,
            subtract(
                projectile.position, projectile.pendingShot.aimOrigin));
        return pose;
    }
    const BattleHitPoint authoritativeRemaining =
        subtract(targetPoint, projectile.position);
    const BattleHitPoint authoritativeTotal =
        subtract(targetPoint, projectile.pendingShot.aimOrigin);
    const double totalDistance = std::sqrt(dot(
        authoritativeTotal, authoritativeTotal));
    const double remainingDistance = std::sqrt(dot(
        authoritativeRemaining, authoritativeRemaining));
    if (totalDistance <= 1.0e-9) {
        return pose;
    }
    const double progress = std::clamp(
        1.0 - remainingDistance / totalDistance, 0.0, 1.0);
    const BattleHitPoint mountToTarget =
        subtract(targetPoint, projectile.launchModelMount);
    pose.position = add(
        projectile.launchModelMount,
        multiply(mountToTarget, progress));
    pose.direction = normalized(mountToTarget);
    return pose;
}

BattleHitPoint projectileModelPointToWorld(
    BattleHitPoint point,
    const BattleProjectileState& projectile,
    const BattleProjectileModelPose& pose,
    uint64_t tickIndex) {
    constexpr double kPi = 3.14159265358979323846;
    const double spin = static_cast<double>(
        tickIndex > projectile.launchTickIndex
            ? tickIndex - projectile.launchTickIndex
            : 0u) * -22.5 * kPi / 180.0;
    const double spinCosine = std::cos(spin);
    const double spinSine = std::sin(spin);
    point = {
        spinCosine * point.x - spinSine * point.y,
        spinSine * point.x + spinCosine * point.y,
        point.z,
    };

    const BattleHitPoint direction = normalized(pose.direction);
    const double pitch = std::asin(std::clamp(direction.y, -1.0, 1.0));
    const double pitchCosine = std::cos(pitch);
    const double pitchSine = std::sin(pitch);
    point = {
        point.x,
        pitchCosine * point.y + pitchSine * point.z,
        -pitchSine * point.y + pitchCosine * point.z,
    };

    const double heading = std::atan2(direction.x, direction.z);
    const double headingCosine = std::cos(heading);
    const double headingSine = std::sin(heading);
    point = {
        headingCosine * point.x + headingSine * point.z,
        point.y,
        -headingSine * point.x + headingCosine * point.z,
    };
    return add(pose.position, point);
}

struct BattleProjectileRayHit {
    bool valid = false;
    double distance = 0.0;
    BattleHitPoint worldPoint;
};

BattleProjectileRayHit raycastProjectileModel(
    const BattleAimRay& ray,
    const BattleProjectileState& projectile,
    const BattleProjectileHitProfile& profile,
    const BattleProjectileModelPose& pose,
    uint64_t tickIndex) {
    BattleProjectileRayHit result;
    double nearestDistance = std::numeric_limits<double>::max();
    for (const BattleMechHitTriangle& localTriangle : profile.triangles) {
        BattleMechHitTriangle worldTriangle;
        worldTriangle.a = projectileModelPointToWorld(
            localTriangle.a, projectile, pose, tickIndex);
        worldTriangle.b = projectileModelPointToWorld(
            localTriangle.b, projectile, pose, tickIndex);
        worldTriangle.c = projectileModelPointToWorld(
            localTriangle.c, projectile, pose, tickIndex);
        double distance = 0.0;
        if (!rayTriangleDistance(
                ray.origin, ray.direction, worldTriangle, distance) ||
            distance >= nearestDistance) {
            continue;
        }
        nearestDistance = distance;
        result.valid = true;
        result.distance = distance;
        result.worldPoint = add(
            ray.origin, multiply(ray.direction, distance));
    }
    return result;
}

struct BattleProjectileTerrainHit {
    bool valid = false;
    double segmentFraction = 1.0;
    BattleHitPoint point;
    uint32_t obstacleId = 0;
    uint16_t recordIndex = 0;
};

bool clipSegmentHalfSpace(
    double valueAtStart,
    double delta,
    double& enter,
    double& exit) {
    constexpr double kEpsilon = 1.0e-9;
    if (std::abs(delta) <= kEpsilon) {
        return valueAtStart >= -kEpsilon;
    }
    const double crossing = -valueAtStart / delta;
    if (delta > 0.0) {
        enter = std::max(enter, crossing);
    } else {
        exit = std::min(exit, crossing);
    }
    return enter <= exit + kEpsilon;
}

bool segmentConvexTerrainPrismContact(
    BattleHitPoint start,
    BattleHitPoint end,
    const BattleTerrainCollisionObstacle& obstacle,
    double& fraction) {
    if (obstacle.footprint.size() < 3u || obstacle.height <= 0.0) {
        return false;
    }
    const BattleHitPoint delta = subtract(end, start);
    double enter = 0.0;
    double exit = 1.0;
    if (!clipSegmentHalfSpace(start.y, delta.y, enter, exit) ||
        !clipSegmentHalfSpace(
            obstacle.height - start.y, -delta.y, enter, exit)) {
        return false;
    }
    for (size_t index = 0; index < obstacle.footprint.size(); ++index) {
        const auto& a = obstacle.footprint[index];
        const auto& b = obstacle.footprint[
            (index + 1u) % obstacle.footprint.size()];
        const double edgeX = b.x - a.x;
        const double edgeZ = b.z - a.z;
        const double startValue =
            edgeX * (start.z - a.z) - edgeZ * (start.x - a.x);
        const double deltaValue = edgeX * delta.z - edgeZ * delta.x;
        if (!clipSegmentHalfSpace(
                startValue, deltaValue, enter, exit)) {
            return false;
        }
    }
    if (exit < 0.0 || enter > 1.0) {
        return false;
    }
    fraction = std::clamp(enter, 0.0, 1.0);
    return true;
}

BattleProjectileTerrainHit projectileTerrainContact(
    const BattleStartParams& params,
    BattleHitPoint start,
    BattleHitPoint end) {
    BattleProjectileTerrainHit result;
    constexpr double kGroundEpsilon = 1.0e-6;
    if (start.y < -kGroundEpsilon ||
        (start.y <= kGroundEpsilon && end.y < -kGroundEpsilon)) {
        result.valid = true;
        result.segmentFraction = 0.0;
    } else if (start.y > kGroundEpsilon && end.y <= 0.0) {
        const double denominator = start.y - end.y;
        result.valid = true;
        result.segmentFraction = denominator > kGroundEpsilon
            ? std::clamp(start.y / denominator, 0.0, 1.0)
            : 0.0;
    }
    if (params.terrainCollisionObstacles.valid) {
        for (const BattleTerrainCollisionObstacle& obstacle :
             params.terrainCollisionObstacles.entries) {
            double fraction = 0.0;
            if (!segmentConvexTerrainPrismContact(
                    start, end, obstacle, fraction) ||
                (result.valid && fraction >= result.segmentFraction)) {
                continue;
            }
            result.valid = true;
            result.segmentFraction = fraction;
            result.obstacleId = obstacle.stableId;
            result.recordIndex = obstacle.recordIndex;
        }
    }
    if (result.valid) {
        result.point = add(
            start,
            multiply(subtract(end, start), result.segmentFraction));
    }
    return result;
}

struct BattleMechRayHit {
    bool valid = false;
    double distance = 0.0;
    BattleHitPoint worldPoint;
    BattleHitPoint localPoint;
    BattleHitPoint localDirection;
    const BattleMechHitTriangle* triangle = nullptr;
};

BattleMechRayHit raycastMech(
    const BattleAimRay& ray,
    const Combatant& target,
    const BattleMechHitProfile& profile) {
    BattleMechRayHit result;
    const BattleHitPoint localOrigin =
        worldPointToTargetLocal(ray.origin, target.transform);
    const BattleHitPoint localDirection = normalized(
        worldDirectionToTargetLocal(ray.direction, target.transform));
    const auto hidden = [&target](int componentId) {
        return containsComponentId(
                   target.mechRuntime.hiddenComponentIds, componentId) ||
            containsComponentId(
                target.mechRuntime.disabledComponentIds, componentId);
    };
    for (const BattleMechHitTriangle& triangle : profile.triangles) {
        if (hidden(triangle.componentId)) {
            continue;
        }
        double distance = 0.0;
        if (!rayTriangleDistance(
                localOrigin, localDirection, triangle, distance) ||
            (result.valid && distance >= result.distance)) {
            continue;
        }
        result.valid = true;
        result.distance = distance;
        result.localPoint = add(
            localOrigin, multiply(localDirection, distance));
        result.worldPoint = add(
            ray.origin, multiply(ray.direction, distance));
        result.localDirection = localDirection;
        result.triangle = &triangle;
    }
    return result;
}

mech3d::MechArmorSectionId provisionalTorsoSection(
    const BattleMechRayHit& hit,
    const BattleMechHitProfile& profile) {
    double minimumX = std::numeric_limits<double>::max();
    double maximumX = std::numeric_limits<double>::lowest();
    for (const BattleMechHitTriangle& triangle : profile.triangles) {
        if (!triangle.torsoComposite) {
            continue;
        }
        for (const BattleHitPoint* point : {
                 &triangle.a, &triangle.b, &triangle.c}) {
            minimumX = std::min(minimumX, point->x);
            maximumX = std::max(maximumX, point->x);
        }
    }
    if (!(minimumX < maximumX)) {
        return mech3d::MechArmorSectionId::CenterTorso;
    }
    const double third = (maximumX - minimumX) / 3.0;
    if (hit.localPoint.x < minimumX + third) {
        return mech3d::MechArmorSectionId::RightTorso;
    }
    if (hit.localPoint.x > maximumX - third) {
        return mech3d::MechArmorSectionId::LeftTorso;
    }
    return hit.localDirection.z > 0.0
        ? mech3d::MechArmorSectionId::CenterRear
        : mech3d::MechArmorSectionId::CenterTorso;
}

BattleMechSystemRole systemRoleForArmorSection(
    mech3d::MechArmorSectionId section) {
    switch (section) {
    case mech3d::MechArmorSectionId::Head:
        return BattleMechSystemRole::Cockpit;
    case mech3d::MechArmorSectionId::RightArm:
    case mech3d::MechArmorSectionId::LeftArm:
        return BattleMechSystemRole::Weapons;
    case mech3d::MechArmorSectionId::RightLeg:
    case mech3d::MechArmorSectionId::LeftLeg:
        return BattleMechSystemRole::Mobility;
    case mech3d::MechArmorSectionId::CenterTorso:
    case mech3d::MechArmorSectionId::CenterRear:
    case mech3d::MechArmorSectionId::RightTorso:
    case mech3d::MechArmorSectionId::LeftTorso:
    case mech3d::MechArmorSectionId::Count:
        return BattleMechSystemRole::Core;
    }
    return BattleMechSystemRole::Unknown;
}

BattleHitPoint steerProjectileDirection(
    BattleHitPoint current,
    BattleHitPoint desired) {
    // FUN_1000_578b limits a target-bound projectile to 0x38e binary-angle
    // units per movement substep. This applies to the AC row as well as the
    // missile rows; the original path has no missile-only steering guard.
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kMaximumTurnRadians =
        static_cast<double>(0x38e) * (2.0 * kPi / 65536.0);
    current = normalized(current);
    desired = normalized(desired);
    const double cosine = std::clamp(dot(current, desired), -1.0, 1.0);
    const double angle = std::acos(cosine);
    if (!(angle > kMaximumTurnRadians) || !std::isfinite(angle)) {
        return desired;
    }
    const double fraction = kMaximumTurnRadians / angle;
    const double sine = std::sin(angle);
    if (std::abs(sine) <= 1.0e-9) {
        return normalized(add(
            multiply(current, 1.0 - fraction),
            multiply(desired, fraction)));
    }
    return normalized(add(
        multiply(current, std::sin((1.0 - fraction) * angle) / sine),
        multiply(desired, std::sin(fraction * angle) / sine)));
}

uint64_t projectileImpactRandomWord(
    const BattleProjectileState& projectile,
    uint64_t impactTickIndex) {
    // The original impact tables and their 2d6 indexing are closed, but the
    // executable's global PRNG sequencing is not. Keep that boundary explicit
    // while providing a stable battle-owned roll for replay determinism.
    uint64_t value = projectile.pendingShot.sequence;
    value ^= static_cast<uint64_t>(projectile.projectileId) << 32u;
    value ^= impactTickIndex + 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31u);
}

int projectileImpactDamage(
    const BattleProjectileState& projectile,
    uint64_t impactTickIndex,
    BattleShotDiagnostic& shot) {
    constexpr std::array<uint8_t, 11> kLrm5Hits{
        1, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5};
    constexpr std::array<uint8_t, 11> kSrm2Hits{
        1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2};
    constexpr std::array<uint8_t, 11> kSrm4Hits{
        1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4};
    constexpr std::array<uint8_t, 11> kSrm6Hits{
        2, 2, 3, 3, 4, 4, 4, 5, 5, 6, 6};

    if (projectile.originalDamageClass == 5u) {
        // FUN_1000_5d76 loads the fixed AC/5 value from DS:0F5A.
        return 5;
    }
    const uint64_t random = projectileImpactRandomWord(
        projectile, impactTickIndex);
    const size_t rollIndex = static_cast<size_t>(
        (random % 6u) + ((random >> 8u) % 6u));
    shot.missileClusterRollIndex = static_cast<uint8_t>(rollIndex);
    shot.missileClusterTableProven = true;
    switch (projectile.originalDamageClass) {
    case 6:
        return kLrm5Hits[rollIndex];
    case 7:
        return static_cast<int>(kSrm2Hits[rollIndex]) * 2;
    case 8:
        return static_cast<int>(kSrm4Hits[rollIndex]) * 2;
    case 9:
        return static_cast<int>(kSrm6Hits[rollIndex]) * 2;
    default:
        shot.missileClusterTableProven = false;
        return static_cast<int>(shot.originalDamage);
    }
}

int originalCriticalLocationIndex(
    mech3d::MechInternalSectionId section) {
    switch (section) {
    case mech3d::MechInternalSectionId::LeftArm: return 0;
    case mech3d::MechInternalSectionId::RightArm: return 1;
    case mech3d::MechInternalSectionId::LeftLeg: return 2;
    case mech3d::MechInternalSectionId::RightLeg: return 3;
    case mech3d::MechInternalSectionId::CenterTorso: return 4;
    case mech3d::MechInternalSectionId::LeftTorso: return 5;
    case mech3d::MechInternalSectionId::RightTorso: return 6;
    case mech3d::MechInternalSectionId::Head: return 7;
    case mech3d::MechInternalSectionId::Count: break;
    }
    return 4;
}

mech3d::MechInternalSectionId criticalLocationFromOriginalIndex(int index) {
    constexpr std::array<mech3d::MechInternalSectionId, 8> kLocations{{
        mech3d::MechInternalSectionId::LeftArm,
        mech3d::MechInternalSectionId::RightArm,
        mech3d::MechInternalSectionId::LeftLeg,
        mech3d::MechInternalSectionId::RightLeg,
        mech3d::MechInternalSectionId::CenterTorso,
        mech3d::MechInternalSectionId::LeftTorso,
        mech3d::MechInternalSectionId::RightTorso,
        mech3d::MechInternalSectionId::Head,
    }};
    return kLocations[static_cast<size_t>(std::clamp(index, 0, 7))];
}

bool weaponMountMatchesCriticalLocation(
    const BattleWeaponInstanceState& weapon,
    mech3d::MechInternalSectionId section) {
    const char* expected = "CT";
    switch (section) {
    case mech3d::MechInternalSectionId::LeftArm: expected = "LA"; break;
    case mech3d::MechInternalSectionId::RightArm: expected = "RA"; break;
    case mech3d::MechInternalSectionId::LeftLeg: expected = "LL"; break;
    case mech3d::MechInternalSectionId::RightLeg: expected = "RL"; break;
    case mech3d::MechInternalSectionId::CenterTorso: expected = "CT"; break;
    case mech3d::MechInternalSectionId::LeftTorso: expected = "LT"; break;
    case mech3d::MechInternalSectionId::RightTorso: expected = "RT"; break;
    case mech3d::MechInternalSectionId::Head: expected = "HD"; break;
    case mech3d::MechInternalSectionId::Count: return false;
    }
    return weapon.locationId == expected;
}

uint64_t criticalRandomWord(
    const BattleShotDiagnostic& shot,
    const Combatant& target,
    mech3d::MechInternalSectionId section,
    uint64_t drawIndex) {
    // FUN_1000_cb73 call sites and all bounds are proven. The sequencing of
    // the original global PRNG is not, so use a stable battle-owned stream and
    // expose that boundary in BattleShotDiagnostic.
    uint64_t value = shot.sequence;
    value ^= static_cast<uint64_t>(shot.weaponInstanceId) << 16u;
    value ^= static_cast<uint64_t>(target.id.value) << 32u;
    value ^= static_cast<uint64_t>(originalCriticalLocationIndex(section)) << 56u;
    value ^= (drawIndex + 1u) * 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31u);
}

enum class CriticalAttemptResult : uint8_t {
    Applied,
    NoCandidate,
    BudgetMiss,
};

CriticalAttemptResult scanOriginalCriticalAttempt(
    Combatant& target,
    mech3d::MechInternalSectionId location,
    BattleShotDiagnostic& shot,
    uint64_t& drawIndex) {
    auto& detailed = target.mechRuntime.detailedDamage;
    const int originalLocation = originalCriticalLocationIndex(location);
    int budget = static_cast<int>(
        criticalRandomWord(shot, target, location, drawIndex++) % 12u) + 1;
    if (originalLocation == 2 || originalLocation == 3 ||
        originalLocation == 7) {
        budget = (budget + 1) >> 1;
    }
    const int initialBudget = budget;

    auto recordComponentHit = [&](mech3d::MechCriticalComponentId id,
                                  BattleCriticalHitKind kind,
                                  int maximumCondition,
                                  int weightBase) -> bool {
        auto& component = detailed.criticalComponents[
            criticalComponentIndex(id)];
        const int effectiveCondition = std::min(
            maximumCondition,
            static_cast<int>(component.condition));
        if (effectiveCondition >= maximumCondition) {
            return false;
        }
        budget -= weightBase - effectiveCondition;
        if (budget >= 1) {
            return false;
        }
        component.condition = static_cast<uint8_t>(effectiveCondition + 1);
        component.battleDamage += 1;
        component.functional = component.condition < maximumCondition;
        component.workingCount = component.functional ? 1u : 0u;
        shot.lastCriticalHitKind = kind;
        shot.lastCriticalHitIndex = static_cast<uint8_t>(id);
        shot.lastCriticalLocation = location;
        return true;
    };

    if (originalLocation == 4) {
        if (recordComponentHit(
                mech3d::MechCriticalComponentId::Engine,
                BattleCriticalHitKind::Engine,
                3,
                6) ||
            recordComponentHit(
                mech3d::MechCriticalComponentId::Gyros,
                BattleCriticalHitKind::Gyros,
                2,
                4)) {
            return CriticalAttemptResult::Applied;
        }
    } else if (originalLocation == 7) {
        if (recordComponentHit(
                mech3d::MechCriticalComponentId::LifeSupport,
                BattleCriticalHitKind::LifeSupport,
                2,
                2) ||
            recordComponentHit(
                mech3d::MechCriticalComponentId::Sensors,
                BattleCriticalHitKind::Sensors,
                2,
                2)) {
            return CriticalAttemptResult::Applied;
        }
        if (!detailed.originalCockpitCriticalFlag) {
            --budget;
            if (budget < 1) {
                detailed.originalCockpitCriticalFlag = true;
                // FUN_1000_4106 treats the one-byte cockpit critical flag as
                // nonfunctional. Keep the battle consequence local; campaign
                // pilot death/write-back remains Phase 14.
                target.missionStatus = CombatantMissionStatus::Disabled;
                shot.lastCriticalHitKind = BattleCriticalHitKind::CockpitFlag;
                shot.lastCriticalHitIndex = 0u;
                shot.lastCriticalLocation = location;
                return CriticalAttemptResult::Applied;
            }
        }
    } else if (originalLocation >= 0 && originalLocation <= 3) {
        constexpr std::array<mech3d::MechCriticalComponentId, 4> kActuators{{
            mech3d::MechCriticalComponentId::LeftArmActuator,
            mech3d::MechCriticalComponentId::RightArmActuator,
            mech3d::MechCriticalComponentId::LeftLegActuator,
            mech3d::MechCriticalComponentId::RightLegActuator,
        }};
        auto& actuator = detailed.criticalComponents[
            criticalComponentIndex(kActuators[static_cast<size_t>(originalLocation)])];
        if (actuator.workingCount != 0u) {
            budget -= static_cast<int>(actuator.workingCount);
            if (budget < 1) {
                --actuator.workingCount;
                ++actuator.battleDamage;
                actuator.functional = actuator.workingCount != 0u;
                shot.lastCriticalHitKind = BattleCriticalHitKind::Actuator;
                shot.lastCriticalHitIndex = static_cast<uint8_t>(
                    kActuators[static_cast<size_t>(originalLocation)]);
                shot.lastCriticalLocation = location;
                return CriticalAttemptResult::Applied;
            }
        }
    }

    for (BattleWeaponInstanceState& weapon : target.weapons) {
        if (!weapon.functional || !weapon.originalCriticalWeightProven ||
            !weaponMountMatchesCriticalLocation(weapon, location)) {
            continue;
        }
        budget -= static_cast<int>(weapon.originalCriticalWeight);
        if (budget >= 1) {
            continue;
        }
        auto& installed = detailed.installedWeapons[weapon.slotIndex];
        installed.functional = false;
        ++installed.battleDamage;
        weapon.functional = false;
        shot.lastCriticalHitKind = BattleCriticalHitKind::InstalledWeapon;
        shot.lastCriticalHitIndex = weapon.slotIndex;
        shot.lastCriticalLocation = location;
        return CriticalAttemptResult::Applied;
    }

    const mech3d::MechCatalogCriticalProfile criticalProfile =
        mech3d::catalogCriticalProfile(target.mechPresetId);
    for (size_t pool = 0; pool < target.ammunitionByPool.size(); ++pool) {
        if (criticalProfile.ammunitionLocationBtechOrder[pool] !=
                originalLocation ||
            target.ammunitionByPool[pool] == 0) {
            continue;
        }
        --budget;
        if (budget >= 1) {
            continue;
        }
        target.ammunitionByPool[pool] = 0;
        ++detailed.ammunitionCriticalHits[pool];
        shot.lastCriticalHitKind = BattleCriticalHitKind::AmmunitionBin;
        shot.lastCriticalHitIndex = static_cast<uint8_t>(pool);
        shot.lastCriticalLocation = location;
        return CriticalAttemptResult::Applied;
    }

    auto& jumpJets = detailed.criticalComponents[criticalComponentIndex(
        mech3d::MechCriticalComponentId::JumpJets)];
    uint8_t destroyedJumpSlots = 0u;
    for (uint8_t bits = detailed.originalJumpJetCriticalDestroyedMask;
         bits != 0u;
         bits >>= 1u) {
        destroyedJumpSlots += static_cast<uint8_t>(bits & 1u);
    }
    const uint8_t activeSlotsAtEntry = static_cast<uint8_t>(std::min<int>(
        3,
        static_cast<int>(jumpJets.workingCount) +
            static_cast<int>(destroyedJumpSlots)));
    for (size_t slot = 0; slot < criticalProfile.jumpJetSlots.size(); ++slot) {
        const auto& definition = criticalProfile.jumpJetSlots[slot];
        const uint8_t slotBit = static_cast<uint8_t>(1u << slot);
        if (slot >= activeSlotsAtEntry ||
            (detailed.originalJumpJetCriticalDestroyedMask & slotBit) != 0u ||
            definition.originalLocationIndex != originalLocation ||
            definition.originalWeight == 0u) {
            continue;
        }
        budget -= static_cast<int>(definition.originalWeight);
        if (budget >= 1) {
            continue;
        }
        detailed.originalJumpJetCriticalDestroyedMask |= slotBit;
        if (jumpJets.workingCount != 0u) {
            --jumpJets.workingCount;
        }
        ++jumpJets.battleDamage;
        jumpJets.functional =
            jumpJets.totalCount == 0u || jumpJets.workingCount != 0u;
        shot.lastCriticalHitKind = BattleCriticalHitKind::JumpJet;
        shot.lastCriticalHitIndex = static_cast<uint8_t>(slot);
        shot.lastCriticalLocation = location;
        return CriticalAttemptResult::Applied;
    }

    // With a complete known candidate set, FUN_1000_4c0a rerolls when the
    // budget exceeded its total weight and reports failure only when the set
    // was empty at the start of the draw.
    if (budget == initialBudget) {
        return CriticalAttemptResult::NoCandidate;
    }
    return CriticalAttemptResult::BudgetMiss;
}

CriticalAttemptResult applyOriginalCriticalAttempt(
    Combatant& target,
    mech3d::MechInternalSectionId location,
    BattleShotDiagnostic& shot,
    uint64_t& drawIndex) {
    for (int reroll = 0; reroll < 4096; ++reroll) {
        const CriticalAttemptResult result = scanOriginalCriticalAttempt(
            target, location, shot, drawIndex);
        if (result != CriticalAttemptResult::BudgetMiss) {
            return result;
        }
    }
    throw std::runtime_error(
        "deterministic critical selection exceeded original reroll guard");
}

void resolveOriginalCriticals(
    Combatant& target,
    mech3d::MechInternalSectionId location,
    BattleShotDiagnostic& shot,
    uint64_t& drawIndex) {
    shot.criticalResolutionAttempted = true;
    auto& detailed = target.mechRuntime.detailedDamage;
    const int firstDie = static_cast<int>(
        criticalRandomWord(shot, target, location, drawIndex++) % 6u) + 1;
    const int secondDie = static_cast<int>(
        criticalRandomWord(shot, target, location, drawIndex++) % 6u) + 1;
    int roll = firstDie + secondDie;
    shot.criticalRoll = static_cast<uint8_t>(roll);
    shot.criticalAttemptsRequested = static_cast<uint8_t>(
        shot.criticalAttemptsRequested +
        (roll < 8 ? 0 : ((roll - 8) / 2) + 1));
    mech3d::MechInternalSectionId currentLocation = location;

    while (roll >= 8) {
        ++detailed.criticalAttempts;
        const CriticalAttemptResult result = applyOriginalCriticalAttempt(
            target, currentLocation, shot, drawIndex);
        if (result == CriticalAttemptResult::Applied) {
            ++shot.criticalHitsApplied;
            ++detailed.criticalHitsApplied;
            roll -= 2;
            currentLocation = location;
            continue;
        }
        constexpr std::array<int, 8> kFallback{{5, 6, 5, 6, 4, 4, 4, 4}};
        const int originalIndex = originalCriticalLocationIndex(currentLocation);
        const int fallbackIndex = kFallback[static_cast<size_t>(originalIndex)];
        if (fallbackIndex == originalIndex) {
            return;
        }
        currentLocation = criticalLocationFromOriginalIndex(fallbackIndex);
        ++shot.criticalFallbackCount;
    }
}

int originalArmorLocationIndex(mech3d::MechArmorSectionId section) {
    switch (section) {
    case mech3d::MechArmorSectionId::LeftArm: return 0;
    case mech3d::MechArmorSectionId::RightArm: return 1;
    case mech3d::MechArmorSectionId::LeftLeg: return 2;
    case mech3d::MechArmorSectionId::RightLeg: return 3;
    case mech3d::MechArmorSectionId::CenterTorso: return 4;
    case mech3d::MechArmorSectionId::CenterRear: return 5;
    case mech3d::MechArmorSectionId::LeftTorso: return 6;
    case mech3d::MechArmorSectionId::RightTorso: return 7;
    case mech3d::MechArmorSectionId::Head: return 8;
    case mech3d::MechArmorSectionId::Count: break;
    }
    return 4;
}

mech3d::MechArmorSectionId armorLocationFromOriginalIndex(int index) {
    constexpr std::array<mech3d::MechArmorSectionId, 9> kLocations{{
        mech3d::MechArmorSectionId::LeftArm,
        mech3d::MechArmorSectionId::RightArm,
        mech3d::MechArmorSectionId::LeftLeg,
        mech3d::MechArmorSectionId::RightLeg,
        mech3d::MechArmorSectionId::CenterTorso,
        mech3d::MechArmorSectionId::CenterRear,
        mech3d::MechArmorSectionId::LeftTorso,
        mech3d::MechArmorSectionId::RightTorso,
        mech3d::MechArmorSectionId::Head,
    }};
    return kLocations[static_cast<size_t>(std::clamp(index, 0, 8))];
}

void applyOriginalDestroyedSectionVisual(
    Combatant& target,
    mech3d::MechArmorSectionId section) {
    const char* labelToken = nullptr;
    bool detachOnDeath = false;
    switch (section) {
    case mech3d::MechArmorSectionId::LeftArm: labelToken = "left_arm"; break;
    case mech3d::MechArmorSectionId::RightArm: labelToken = "right_arm"; break;
    case mech3d::MechArmorSectionId::LeftLeg:
        labelToken = "left_leg";
        detachOnDeath = true;
        break;
    case mech3d::MechArmorSectionId::RightLeg:
        labelToken = "right_leg";
        detachOnDeath = true;
        break;
    // Torso and cockpit destruction kills the chassis without removing the
    // corresponding geometry from the death pose.
    case mech3d::MechArmorSectionId::Head: return;
    default: return;
    }
    const auto rule = std::find_if(
        target.damageRules.begin(),
        target.damageRules.end(),
        [labelToken](const mech3d::MechComponentDamageRule& candidate) {
            return labelContains(candidate.debugLabel, labelToken);
        });
    if (rule == target.damageRules.end()) {
        return;
    }
    mech3d::destroyMechRuntimeComponent(
        target.mechRuntime, rule->componentId);
    if (detachOnDeath) {
        mech3d::hideMechRuntimeComponent(
            target.mechRuntime, rule->componentId);
    }
}

void applyDetailedCombatantDamage(
    const BattleStartParams& params,
    Combatant& attacker,
    Combatant& target,
    int damageAmount,
    BattleShotDiagnostic& shot) {
    shot.runtimeEffect = BattleShotRuntimeEffect::DetailedArmorSectionDamage;
    auto& detailed = target.mechRuntime.detailedDamage;
    const mech3d::MechArmorSectionId initialArmorSection = shot.armorSection;
    auto& initialSection = detailed.armorSections[
        armorSectionIndex(initialArmorSection)];
    auto& initialInternal = detailed.internalSections[internalSectionIndex(
        internalSectionForArmor(initialArmorSection))];
    shot.armorBefore = initialSection.armorRemaining;
    shot.internalBefore = initialInternal.structureRemaining;
    shot.damageBefore =
        initialSection.battleDamage + initialInternal.battleDamage;
    int damageRemaining = std::max(0, damageAmount);
    uint64_t criticalDrawIndex = 0u;
    mech3d::MechArmorSectionId currentArmorSection = initialArmorSection;
    constexpr std::array<int, 9> kDestroyedInternalContinuation{{
        6, 7, 6, 7, 5, 4, 4, 4, 4}};

    for (int propagationGuard = 0; propagationGuard < 10; ++propagationGuard) {
        auto& section = detailed.armorSections[
            armorSectionIndex(currentArmorSection)];
        const mech3d::MechInternalSectionId internalSectionId =
            internalSectionForArmor(currentArmorSection);
        auto& internal = detailed.internalSections[
            internalSectionIndex(internalSectionId)];
        const int armorBefore = static_cast<int>(section.armorRemaining);

        if (damageRemaining < armorBefore) {
            section.armorRemaining = static_cast<uint16_t>(
                armorBefore - damageRemaining);
            section.battleDamage += damageRemaining;
            shot.armorDamageApplied += damageRemaining;
            damageRemaining = 0;
            break;
        }

        section.armorRemaining = 0u;
        section.battleDamage += armorBefore;
        shot.armorDamageApplied += armorBefore;
        damageRemaining -= armorBefore;

        if (internal.structureRemaining != 0u) {
            // FUN_1000_4ece invokes FUN_1000_4baa before subtracting any
            // internal points, including exact armor equality.
            resolveOriginalCriticals(
                target,
                internalSectionId,
                shot,
                criticalDrawIndex);

            const int internalBefore =
                static_cast<int>(internal.structureRemaining);
            if (damageRemaining < internalBefore) {
                internal.structureRemaining = static_cast<uint16_t>(
                    internalBefore - damageRemaining);
                internal.battleDamage += damageRemaining;
                shot.internalDamageApplied += damageRemaining;
                damageRemaining = 0;
                break;
            }

            internal.structureRemaining = 0u;
            internal.battleDamage += internalBefore;
            shot.internalDamageApplied += internalBefore;
            damageRemaining -= internalBefore;
            applyOriginalDestroyedSectionVisual(target, currentArmorSection);

            // The physical call at unpacked 0x12804 is FUN_1000_4c0a itself:
            // one extra weighted critical attempt. Success consumes the
            // remaining hit; only an empty candidate set continues via E4C.
            ++detailed.criticalAttempts;
            const CriticalAttemptResult destroyedResult =
                applyOriginalCriticalAttempt(
                    target,
                    internalSectionId,
                    shot,
                    criticalDrawIndex);
            if (destroyedResult == CriticalAttemptResult::Applied) {
                ++shot.criticalHitsApplied;
                ++detailed.criticalHitsApplied;
                break;
            }
        } else if (internalSectionId ==
                   mech3d::MechInternalSectionId::CenterTorso) {
            break;
        }

        // FUN_1000_4ece's outer loop is `while (damage != 0)`. Equality may
        // still reach the post-destruction attempt above, but a zero remainder
        // must not begin another E4C section and trigger a spurious bare-armor
        // critical there.
        if (damageRemaining == 0) {
            break;
        }

        const int externalIndex =
            originalArmorLocationIndex(currentArmorSection);
        currentArmorSection = armorLocationFromOriginalIndex(
            kDestroyedInternalContinuation[static_cast<size_t>(externalIndex)]);
    }

    if (shot.criticalHitsApplied != 0u) {
        shot.runtimeEffect = BattleShotRuntimeEffect::DetailedCriticalResolved;
    } else if (shot.internalDamageApplied != 0) {
        shot.runtimeEffect =
            BattleShotRuntimeEffect::DetailedArmorInternalDamage;
    }
    shot.armorAfter = initialSection.armorRemaining;
    shot.internalAfter = initialInternal.structureRemaining;
    shot.damageApplied =
        shot.armorDamageApplied + shot.internalDamageApplied;
    shot.damageAfter =
        initialSection.battleDamage + initialInternal.battleDamage;
    shot.criticalCandidateSelectionPending = false;
    shot.criticalResolutionPending = false;
    shot.unresolvedCriticalDamage = 0;
    detailed.criticalResolutionPending = false;
    detailed.unresolvedCriticalDamage = 0;
    shot.provenance +=
        ":BTECH_4ece_4baa_4c0a_full_critical_E4C";
    target.lastDamageSourceEntityId = attacker.id;
    if (target.roster.team != attacker.roster.team &&
        params.objectiveRetaliationSeconds > 0.0) {
        const uint64_t retaliationTicks = std::max<uint64_t>(
            1u,
            static_cast<uint64_t>(std::ceil(
                params.objectiveRetaliationSeconds /
                params.fixedTickSeconds)));
        const bool retaliationExpired =
            !isValid(target.replacementAi.retaliationTargetEntityId) ||
            shot.tickIndex >
                target.replacementAi.retaliationUntilTickIndex;
        const bool sameRetaliationTarget =
            target.replacementAi.retaliationTargetEntityId == attacker.id;
        if (retaliationExpired || sameRetaliationTarget) {
            target.replacementAi.retaliationTargetEntityId = attacker.id;
            target.replacementAi.retaliationUntilTickIndex =
                shot.tickIndex + retaliationTicks;
        }
    }
    attacker.lastWeaponTargetEntityId = target.id;
    attacker.lastWeaponTargetSystem = shot.affectedSystem;
    attacker.weaponHitsLanded += 1;
    refreshCombatantWeaponState(params, target);
}

void applyDeterministicObjectiveDamage(
    Combatant& attacker,
    BattleObjectiveState& objective,
    int damageAmount) {
    if (!objectiveCanTakeDamage(objective)) {
        return;
    }
    objective.damage = std::min(
        objective.maxDamage,
        objective.damage + std::max(0, damageAmount));
    objective.lastDamageSourceEntityId = attacker.id;
    attacker.lastWeaponTargetEntityId = {};
    attacker.lastWeaponTargetSystem = BattleMechSystemRole::Unknown;
    attacker.weaponHitsLanded += 1;
    if (objective.damage >= objective.maxDamage) {
        objective.depleted = true;
    }
}

void BattleWorld::updateDeterministicEnemyRuntime() {
    if (params_.combatAiPolicy ==
        BattleCombatAiPolicy::ReplacementDeterministicCombatAi) {
        updateReplacementCombatAi();
        return;
    }
    const bool originalMovementPolicy =
        params_.combatAiPolicy ==
            BattleCombatAiPolicy::OriginalBtechModeZeroFirstMovementUpdate ||
        params_.combatAiPolicy ==
            BattleCombatAiPolicy::OriginalBtechModeZeroRepeatedMovement ||
        params_.combatAiPolicy ==
            BattleCombatAiPolicy::
                OriginalBtechModeZeroMultiTargetRepeatedMovement ||
        params_.combatAiPolicy ==
            BattleCombatAiPolicy::
                OriginalBtechNonObjectiveModesMultiTargetRepeatedMovement ||
        params_.combatAiPolicy ==
            BattleCombatAiPolicy::
                OriginalBtechNonObjectiveSymmetricRepeatedMovement50eeInvariant ||
        params_.combatAiPolicy == BattleCombatAiPolicy::
            OriginalBtechModeThreeNullDataPointRepeatedMovement ||
        params_.combatAiPolicy == BattleCombatAiPolicy::
            CompatibilityModeThreePlayerCompanionSelectedTargetRepeatedMovement ||
        params_.combatAiPolicy == BattleCombatAiPolicy::
            CompatibilityModeThreePlayerCompanionMissionObjectiveRepeatedMovement;
    if (originalMovementPolicy) {
        updateOriginalMovement();
        return;
    }
    if (params_.combatAiPolicy !=
        BattleCombatAiPolicy::Phase10CompatibilityFsm) {
        return;
    }
    if (params_.enemyActivationRange < 0.0 || params_.enemyAttackRange < 0.0) {
        throw std::runtime_error("battle enemy activation and attack ranges must be non-negative");
    }
    if (params_.enemyActivationRange < params_.enemyAttackRange) {
        throw std::runtime_error("battle enemy activation range must not be smaller than attack range");
    }
    if (params_.enemyRetreatOnDamageEnabled && params_.enemyRetreatCoreDamageThreshold <= 0) {
        throw std::runtime_error("battle enemy retreat damage threshold must be positive");
    }

    Combatant* target = nullptr;
    for (Combatant& combatant : combatants_) {
        if (combatant.roster.team == BattleTeam::Player &&
            combatant.missionStatus == CombatantMissionStatus::Active) {
            target = &combatant;
            break;
        }
    }
    const bool targetBoundProtectObjective =
        objective_.valid &&
        objective_.missionIntent == BattleMissionObjectiveIntent::Protect &&
        objective_.missionIntentBoundToObjective &&
        objective_.missionSemanticsProven &&
        objective_.transformProven &&
        objective_.activeObjectProven;

    for (Combatant& combatant : combatants_) {
        if (combatant.playerControlled || combatant.roster.team != BattleTeam::Opposing) {
            continue;
        }
        if (combatant.missionStatus != CombatantMissionStatus::Active) {
            combatant.enemyAiState = BattleEnemyAiState::Retreat;
            combatant.enemyAiTargetKind = BattleEnemyAiTargetKind::None;
            combatant.enemyAiTargetEntityId = {};
            combatant.enemyAiTargetTransform = {};
            combatant.enemyAiTargetDistance = 0.0;
            combatant.throttle = 0.0;
            continue;
        }

        BattleEnemyAiTargetKind targetKind = BattleEnemyAiTargetKind::None;
        EntityId targetEntityId{};
        Transform targetTransform{};
        bool targetActivatesImmediately = false;
        if (targetBoundProtectObjective) {
            targetKind = BattleEnemyAiTargetKind::Objective;
            targetTransform = objective_.transform;
            targetActivatesImmediately = true;
        } else if (target != nullptr) {
            targetKind = BattleEnemyAiTargetKind::Player;
            targetEntityId = target->id;
            targetTransform = target->transform;
        }

        if (targetKind == BattleEnemyAiTargetKind::None) {
            combatant.enemyAiState = BattleEnemyAiState::Search;
            combatant.enemyAiTargetKind = BattleEnemyAiTargetKind::None;
            combatant.enemyAiTargetEntityId = {};
            combatant.enemyAiTargetTransform = {};
            combatant.enemyAiTargetDistance = 0.0;
            combatant.throttle = 0.0;
            continue;
        }

        combatant.enemyAiTargetKind = targetKind;
        combatant.enemyAiTargetEntityId = targetEntityId;
        combatant.enemyAiTargetTransform = targetTransform;
        combatant.enemyAiTargetDistance = distance2d(combatant.transform, targetTransform);
        if (targetKind == BattleEnemyAiTargetKind::Player &&
            combatantShouldRetreatFromDamage(params_, combatant)) {
            combatant.enemyAiState = BattleEnemyAiState::Retreat;
            combatant.transform.headingRadians =
                std::atan2(combatant.transform.x - targetTransform.x, combatant.transform.z - targetTransform.z);
            combatant.throttle = 1.0;
        } else if (!targetActivatesImmediately && combatant.enemyAiTargetDistance > params_.enemyActivationRange) {
            combatant.enemyAiState = BattleEnemyAiState::Search;
            combatant.throttle = 0.0;
        } else if (combatant.enemyAiTargetDistance <= params_.enemyAttackRange) {
            combatant.enemyAiState = BattleEnemyAiState::Attack;
            combatant.transform.headingRadians =
                std::atan2(targetTransform.x - combatant.transform.x, targetTransform.z - combatant.transform.z);
            combatant.throttle = 0.0;
        } else {
            combatant.enemyAiState = BattleEnemyAiState::Approach;
            combatant.transform.headingRadians =
                std::atan2(targetTransform.x - combatant.transform.x, targetTransform.z - combatant.transform.z);
            combatant.throttle = 1.0;
        }
        combatant.turn = 0.0;
    }
}

namespace {

constexpr int kReplacementAiHeatGate = 0x5a0;

bool replacementWeaponHasAmmunition(
    const BattleWeaponInstanceState& weapon) {
    return weapon.ammunitionOwnership !=
               BattleWeaponAmmunitionOwnership::SharedAmmunitionPool ||
        (weapon.ammunitionStateKnown && weapon.ammunitionRemaining > 0);
}

const BattleWeaponInstanceState* replacementCombatWeapon(
    const Combatant& shooter,
    std::optional<double> targetDistance) {
    const BattleWeaponInstanceState* selected = nullptr;
    std::tuple<int, int64_t, int64_t, uint8_t, uint32_t> selectedScore{};
    for (const BattleWeaponInstanceState& weapon : shooter.weapons) {
        if (!weapon.functional || !weapon.originalRangeValueProven ||
            weapon.maximumRange <= 0.0 ||
            !replacementWeaponHasAmmunition(weapon) ||
            (weapon.readiness != BattleWeaponReadiness::Ready &&
             weapon.readiness != BattleWeaponReadiness::Cooldown)) {
            continue;
        }
        const bool inRange = targetDistance.has_value() &&
            *targetDistance > 0.0 && *targetDistance < weapon.maximumRange;
        const bool heatSafe = !shooter.heat.enabled ||
            (weapon.originalHeatValueProven &&
             shooter.heat.rawHeat + static_cast<int>(weapon.originalHeat) <
                 kReplacementAiHeatGate);
        const bool ready = weapon.readiness == BattleWeaponReadiness::Ready &&
            heatSafe;
        const int category = inRange ? (ready ? 0 : 1) : (ready ? 2 : 3);
        const int64_t firstTie = inRange
            ? -static_cast<int64_t>(weapon.originalDamage)
            : -static_cast<int64_t>(std::llround(weapon.maximumRange));
        const int64_t secondTie = inRange
            ? static_cast<int64_t>(weapon.originalHeat)
            : -static_cast<int64_t>(weapon.originalDamage);
        const auto score = std::make_tuple(
            category, firstTie, secondTie,
            weapon.slotIndex, weapon.weaponInstanceId);
        if (selected == nullptr || score < selectedScore) {
            selected = &weapon;
            selectedScore = score;
        }
    }
    return selected;
}

int replacementAiSideDamage(const Combatant& combatant, bool leftSide) {
    const auto& damage = combatant.mechRuntime.detailedDamage;
    if (!damage.valid) {
        return 0;
    }
    const auto armorDamage = [&damage](mech3d::MechArmorSectionId id) {
        const auto& section = damage.armorSections[armorSectionIndex(id)];
        return static_cast<int>(section.armorMaximum) -
            static_cast<int>(section.armorRemaining);
    };
    const auto internalDamage = [&damage](mech3d::MechInternalSectionId id) {
        const auto& section = damage.internalSections[internalSectionIndex(id)];
        return static_cast<int>(section.structureMaximum) -
            static_cast<int>(section.structureRemaining);
    };
    if (leftSide) {
        return armorDamage(mech3d::MechArmorSectionId::LeftArm) +
            armorDamage(mech3d::MechArmorSectionId::LeftTorso) +
            armorDamage(mech3d::MechArmorSectionId::LeftLeg) +
            internalDamage(mech3d::MechInternalSectionId::LeftArm) +
            internalDamage(mech3d::MechInternalSectionId::LeftTorso) +
            internalDamage(mech3d::MechInternalSectionId::LeftLeg);
    }
    return armorDamage(mech3d::MechArmorSectionId::RightArm) +
        armorDamage(mech3d::MechArmorSectionId::RightTorso) +
        armorDamage(mech3d::MechArmorSectionId::RightLeg) +
        internalDamage(mech3d::MechInternalSectionId::RightArm) +
        internalDamage(mech3d::MechInternalSectionId::RightTorso) +
        internalDamage(mech3d::MechInternalSectionId::RightLeg);
}

int8_t replacementAiPreferredOrbitDirection(const Combatant& combatant) {
    const int leftDamage = replacementAiSideDamage(combatant, true);
    const int rightDamage = replacementAiSideDamage(combatant, false);
    if (leftDamage > rightDamage) {
        // Clockwise motion keeps the target on the healthier right side.
        return 1;
    }
    if (rightDamage > leftDamage) {
        return -1;
    }
    return (combatant.id.value & 1u) == 0u ? 1 : -1;
}

Transform replacementAiRetreatDestination(
    const BattleStartParams& params,
    const Combatant& combatant,
    const Combatant* threat) {
    double directionX = threat == nullptr
        ? std::sin(combatant.transform.headingRadians)
        : combatant.transform.x - threat->transform.x;
    double directionZ = threat == nullptr
        ? std::cos(combatant.transform.headingRadians)
        : combatant.transform.z - threat->transform.z;
    if (std::hypot(directionX, directionZ) <= 1.0e-9 &&
        params.battlefieldBoundary.valid) {
        directionX = combatant.transform.x -
            (params.battlefieldBoundary.worldMinX +
             params.battlefieldBoundary.worldMaxX) * 0.5;
        directionZ = combatant.transform.z -
            (params.battlefieldBoundary.worldMinZ +
             params.battlefieldBoundary.worldMaxZ) * 0.5;
    }
    const double directionLength = std::hypot(directionX, directionZ);
    if (directionLength <= 1.0e-9) {
        directionX = 1.0;
        directionZ = 0.0;
    } else {
        directionX /= directionLength;
        directionZ /= directionLength;
    }

    double exitDistance = 20000.0;
    if (params.battlefieldBoundary.valid) {
        const auto positiveIntersection = [](double numerator,
                                             double direction) {
            if (std::abs(direction) <= 1.0e-9) {
                return std::numeric_limits<double>::max();
            }
            const double distance = numerator / direction;
            return distance >= 0.0
                ? distance : std::numeric_limits<double>::max();
        };
        exitDistance = std::min(
            positiveIntersection(
                (directionX > 0.0
                     ? params.battlefieldBoundary.worldMaxX
                     : params.battlefieldBoundary.worldMinX) -
                    combatant.transform.x,
                directionX),
            positiveIntersection(
                (directionZ > 0.0
                     ? params.battlefieldBoundary.worldMaxZ
                     : params.battlefieldBoundary.worldMinZ) -
                    combatant.transform.z,
                directionZ));
        if (!std::isfinite(exitDistance) ||
            exitDistance == std::numeric_limits<double>::max()) {
            exitDistance = 20000.0;
        }
    }
    const double outsideMargin = std::max(
        500.0, combatant.collisionRadiusWorld + 100.0);
    Transform destination = combatant.transform;
    destination.x += directionX * (exitDistance + outsideMargin);
    destination.z += directionZ * (exitDistance + outsideMargin);
    return destination;
}

struct ReplacementAiAimSolution {
    bool valid = false;
    bool terrainLineOfSightClear = false;
    bool aimAligned = false;
    double aimErrorRadians = 0.0;
    BattleHitPoint origin;
    BattleHitPoint targetPoint;
    BattleHitPoint direction;
};

ReplacementAiAimSolution replacementAiAimSolution(
    const BattleStartParams& params,
    const Combatant& shooter,
    const BattleWeaponInstanceState& weapon,
    const Combatant* target,
    const BattleObjectiveState* objective) {
    constexpr double kPi = 3.14159265358979323846;
    ReplacementAiAimSolution result;
    if (target == nullptr && objective == nullptr) {
        return result;
    }

    const double torsoHeading = shooter.transform.headingRadians +
        torsoYawRadians(params, shooter.torsoYawStep);
    const BattleMechHitProfile* shooterProfile =
        hitProfileForPreset(params, shooter.mechPresetId);
    const double shooterAimHeight = shooterProfile != nullptr
        ? shooterProfile->aimOriginHeight : 350.0;
    result.origin = {
        shooter.transform.x +
            std::sin(torsoHeading) * params.cockpitCameraForwardOffset,
        shooter.transform.y + shooterAimHeight,
        shooter.transform.z +
            std::cos(torsoHeading) * params.cockpitCameraForwardOffset,
    };
    if (shooterProfile != nullptr) {
        const auto mount = battleModelWeaponMount(
            *shooterProfile,
            weapon.locationId,
            shooter.transform,
            torsoYawRadians(params, shooter.torsoYawStep));
        if (mount.has_value()) {
            result.origin = *mount;
        }
    }

    double targetClearance = 500.0;
    if (target != nullptr) {
        const BattleMechHitProfile* targetProfile =
            hitProfileForPreset(params, target->mechPresetId);
        const double targetAimHeight = targetProfile != nullptr
            ? targetProfile->aimOriginHeight * 0.55 : 300.0;
        result.targetPoint = {
            target->transform.x,
            target->transform.y + targetAimHeight,
            target->transform.z,
        };
        targetClearance = target->collisionRadiusWorld + 100.0;
    } else {
        result.targetPoint = {
            objective->transform.x,
            objective->transform.y + std::max(300.0, shooterAimHeight * 0.5),
            objective->transform.z,
        };
    }

    const BattleHitPoint displacement = subtract(
        result.targetPoint, result.origin);
    const double lineDistance = std::sqrt(dot(displacement, displacement));
    if (lineDistance <= 1.0e-6) {
        return result;
    }
    result.direction = multiply(displacement, 1.0 / lineDistance);
    const double desiredHeading = std::atan2(
        result.targetPoint.x - result.origin.x,
        result.targetPoint.z - result.origin.z);
    result.aimErrorRadians = std::remainder(
        desiredHeading - torsoHeading, 2.0 * kPi);
    constexpr double kReplacementAimToleranceRadians =
        12.0 * kPi / 180.0;
    result.aimAligned =
        std::abs(result.aimErrorRadians) <= kReplacementAimToleranceRadians;

    const BattleProjectileTerrainHit terrainHit = projectileTerrainContact(
        params, result.origin, result.targetPoint);
    const double terrainDistance = terrainHit.segmentFraction * lineDistance;
    result.terrainLineOfSightClear = !terrainHit.valid ||
        terrainDistance >= lineDistance - targetClearance;
    result.valid = true;
    return result;
}

} // namespace

void BattleWorld::updateReplacementCombatAi() {
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kTwoPi = 2.0 * kPi;
    const bool singleMission = params_.mission.valid && !params_.mission.extended;
    const bool deathmatchMission = singleMission &&
        params_.mission.family == BattleMissionFamily::Deathmatch;
    const bool assaultMission = singleMission &&
        params_.mission.family == BattleMissionFamily::Assault &&
        objective_.valid && objective_.transformProven;
    const bool retrievalMission = singleMission &&
        params_.mission.family == BattleMissionFamily::Retrieval &&
        objective_.valid && objective_.transformProven;
    const bool sprintMission = singleMission &&
        params_.mission.family == BattleMissionFamily::Sprint;
    const bool boundProtectObjective =
        singleMission && params_.mission.family == BattleMissionFamily::Defense &&
        battleObjectiveProtectedByPlayer(objective_) &&
        objective_.transformProven && objective_.activeObjectProven;
    const bool defenseMission = boundProtectObjective;
    const bool supportedMission = deathmatchMission || assaultMission ||
        retrievalMission || sprintMission || defenseMission;
    std::vector<Combatant*> actors;
    for (Combatant& combatant : combatants_) {
        const bool replacementActor = supportedMission &&
            !combatant.playerControlled &&
            (combatant.roster.team == BattleTeam::Player ||
             combatant.roster.team == BattleTeam::Opposing);
        if (replacementActor) {
            if (combatant.missionStatus == CombatantMissionStatus::Active &&
                !combatantMechDestroyed(combatant)) {
                actors.push_back(&combatant);
            } else {
                combatant.replacementAi.active = false;
                combatant.replacementAi.missionSlice =
                    BattleReplacementAiMissionSlice::None;
                combatant.replacementAi.missionAnchor = {};
                combatant.replacementAi.targetKind =
                    BattleReplacementAiTargetKind::None;
                combatant.replacementAi.combatTargetEntityId = {};
                combatant.replacementAi.arrivalReached = false;
                combatant.replacementAi.arrivalTickIndex = 0;
                combatant.replacementAi.missionTriggered = false;
                combatant.replacementAi.combatOrbitActive = false;
                combatant.replacementAi.retreating = false;
                combatant.replacementAi.retreatThreatEntityId = {};
                combatant.replacementAi.retreatDestination = {};
                combatant.throttle = 0.0;
                combatant.turn = 0.0;
            }
        } else if (!combatant.playerControlled) {
            combatant.replacementAi.active = false;
            combatant.replacementAi.missionSlice =
                BattleReplacementAiMissionSlice::None;
            combatant.replacementAi.targetKind =
                BattleReplacementAiTargetKind::None;
            combatant.replacementAi.combatTargetEntityId = {};
            combatant.replacementAi.missionTriggered = false;
            combatant.replacementAi.combatOrbitActive = false;
            combatant.replacementAi.retreating = false;
            combatant.replacementAi.retreatThreatEntityId = {};
            combatant.replacementAi.retreatDestination = {};
            combatant.throttle = 0.0;
            combatant.turn = 0.0;
        }
    }
    std::sort(actors.begin(), actors.end(),
        [](const Combatant* a, const Combatant* b) {
            return a->id.value < b->id.value;
        });
    BattleReplacementAiMissionSlice missionSlice =
        BattleReplacementAiMissionSlice::None;
    if (deathmatchMission) {
        missionSlice = BattleReplacementAiMissionSlice::DeathmatchEngagement;
    } else if (defenseMission) {
        missionSlice = BattleReplacementAiMissionSlice::ProtectGarrisonDefense;
    } else if (assaultMission) {
        missionSlice = BattleReplacementAiMissionSlice::AssaultEngagement;
    } else if (retrievalMission) {
        missionSlice = BattleReplacementAiMissionSlice::RetrieveArrival;
    } else if (sprintMission) {
        missionSlice = BattleReplacementAiMissionSlice::SprintInterception;
    }
    if (missionSlice == BattleReplacementAiMissionSlice::None) {
        for (Combatant* actor : actors) {
            actor->replacementAi.active = false;
            actor->replacementAi.missionSlice =
                BattleReplacementAiMissionSlice::None;
            actor->replacementAi.missionAnchor = {};
            actor->replacementAi.targetKind =
                BattleReplacementAiTargetKind::None;
            actor->replacementAi.combatTargetEntityId = {};
            actor->replacementAi.arrivalReached = false;
            actor->replacementAi.arrivalTickIndex = 0;
            actor->replacementAi.missionTriggered = false;
            actor->replacementAi.combatOrbitActive = false;
            actor->replacementAi.retreating = false;
            actor->replacementAi.retreatThreatEntityId = {};
            actor->replacementAi.retreatDestination = {};
            actor->throttle = 0.0;
            actor->turn = 0.0;
        }
        return;
    }

    const bool defenseReaction = missionSlice ==
        BattleReplacementAiMissionSlice::ProtectGarrisonDefense;
    const bool deathmatchEngagement = missionSlice ==
        BattleReplacementAiMissionSlice::DeathmatchEngagement;
    const bool assaultEngagement = missionSlice ==
        BattleReplacementAiMissionSlice::AssaultEngagement;
    const bool retrievalEngagement = missionSlice ==
        BattleReplacementAiMissionSlice::RetrieveArrival;
    const bool sprintInterception = missionSlice ==
        BattleReplacementAiMissionSlice::SprintInterception;
    Transform playerLaunchCentroid{};
    size_t playerLaunchCount = 0u;
    const Transform playerLaunchTransform = params_.playerLaunchState
        ? params_.playerLaunchState->startTransform
        : params_.playerStartTransform;
    playerLaunchCentroid.x = playerLaunchTransform.x;
    playerLaunchCentroid.z = playerLaunchTransform.z;
    playerLaunchCount = 1u;
    for (const BattleCombatantLaunchState& launch :
         params_.playerAlliedLaunchStates) {
        if (launch.roster.team != BattleTeam::Player) {
            continue;
        }
        playerLaunchCentroid.x += launch.startTransform.x;
        playerLaunchCentroid.z += launch.startTransform.z;
        ++playerLaunchCount;
    }
    playerLaunchCentroid.x /= static_cast<double>(playerLaunchCount);
    playerLaunchCentroid.z /= static_cast<double>(playerLaunchCount);

    Transform opposingLaunchCentroid{};
    size_t opposingLaunchCount = 0u;
    for (const BattleCombatantLaunchState& launch :
         params_.combatantLaunchStates) {
        if (launch.roster.team != BattleTeam::Opposing) {
            continue;
        }
        opposingLaunchCentroid.x += launch.startTransform.x;
        opposingLaunchCentroid.z += launch.startTransform.z;
        ++opposingLaunchCount;
    }
    if (opposingLaunchCount > 0u) {
        opposingLaunchCentroid.x /=
            static_cast<double>(opposingLaunchCount);
        opposingLaunchCentroid.z /=
            static_cast<double>(opposingLaunchCount);
    } else {
        opposingLaunchCentroid = params_.playerStartTransform;
    }

    const auto teamDetectsCombatant =
        [this](BattleTeam observerTeam, const Combatant& target) {
            for (const Combatant& observer : combatants_) {
                if (observer.roster.team == observerTeam &&
                    observer.missionStatus == CombatantMissionStatus::Active &&
                    !combatantMechDestroyed(observer) &&
                    (!observer.majorSystems.enabled ||
                     observer.majorSystems.radarContactsVisible) &&
                    distance2d(observer.transform, target.transform) <=
                        battleRadarContactRangeWorldUnits()) {
                    return true;
                }
            }
            return false;
        };

    bool objectiveGuardTriggered = false;
    if (assaultEngagement || retrievalEngagement) {
        for (const Combatant& combatant : combatants_) {
            if (combatant.roster.team == BattleTeam::Player &&
                combatant.missionStatus == CombatantMissionStatus::Active &&
                !combatantMechDestroyed(combatant) &&
                teamDetectsCombatant(BattleTeam::Opposing, combatant)) {
                objectiveGuardTriggered = true;
                break;
            }
        }
    }
    const auto launchTransformFor = [this](const Combatant& actor) {
        const auto& launches = actor.roster.team == BattleTeam::Player
            ? params_.playerAlliedLaunchStates
            : params_.combatantLaunchStates;
        const auto found = std::find_if(
            launches.begin(), launches.end(),
            [&actor](const BattleCombatantLaunchState& launch) {
                return launch.roster.sourceSlot == actor.roster.sourceSlot;
            });
        return found == launches.end() ? actor.transform : found->startTransform;
    };
    Transform sprintExitAnchor = playerLaunchCentroid;
    if (sprintInterception) {
        double exitAxisX = playerLaunchCentroid.x - opposingLaunchCentroid.x;
        double exitAxisZ = playerLaunchCentroid.z - opposingLaunchCentroid.z;
        const double exitAxisLength = std::hypot(exitAxisX, exitAxisZ);
        if (exitAxisLength > 0.0) {
            exitAxisX /= exitAxisLength;
            exitAxisZ /= exitAxisLength;
        } else {
            exitAxisX = 0.0;
            exitAxisZ = 1.0;
        }
        double exitDistance = 20000.0;
        if (params_.battlefieldBoundary.valid) {
            const auto positiveIntersection = [](double numerator,
                                                 double direction) {
                if (std::abs(direction) < 1.0e-9) {
                    return std::numeric_limits<double>::max();
                }
                const double value = numerator / direction;
                return value > 0.0
                    ? value : std::numeric_limits<double>::max();
            };
            exitDistance = std::min(
                positiveIntersection(
                    (exitAxisX > 0.0
                         ? params_.battlefieldBoundary.worldMaxX
                         : params_.battlefieldBoundary.worldMinX) -
                        opposingLaunchCentroid.x,
                    exitAxisX),
                positiveIntersection(
                    (exitAxisZ > 0.0
                         ? params_.battlefieldBoundary.worldMaxZ
                         : params_.battlefieldBoundary.worldMinZ) -
                        opposingLaunchCentroid.z,
                    exitAxisZ));
            if (!std::isfinite(exitDistance) ||
                exitDistance == std::numeric_limits<double>::max()) {
                exitDistance = 20000.0;
            }
        }
        sprintExitAnchor.x = opposingLaunchCentroid.x +
            exitAxisX * exitDistance;
        sprintExitAnchor.z = opposingLaunchCentroid.z +
            exitAxisZ * exitDistance;
    }

    std::vector<EntityId> assignedTargets;
    for (size_t actorIndex = 0; actorIndex < actors.size(); ++actorIndex) {
        Combatant& companion = *actors[actorIndex];
        const bool playerSideActor =
            companion.roster.team == BattleTeam::Player;
        const bool opposingSideActor =
            companion.roster.team == BattleTeam::Opposing;
        const bool opposingDefenseActor = defenseReaction &&
            opposingSideActor;
        const bool objectiveAssault = opposingDefenseActor &&
            boundProtectObjective;
        const bool objectiveDefender = defenseReaction && playerSideActor;
        const bool objectiveGuard =
            (assaultEngagement || retrievalEngagement) && opposingSideActor;
        const bool objectiveApproach =
            (assaultEngagement || retrievalEngagement) && playerSideActor;
        const bool sprintRunner = sprintInterception && opposingSideActor;
        const bool sprintInterceptor = sprintInterception && playerSideActor;
        size_t rank = 0u;
        size_t sideActorCount = 0u;
        for (const Combatant* actor : actors) {
            if (actor->roster.team == companion.roster.team) {
                if (actor->id.value < companion.id.value) {
                    ++rank;
                }
                ++sideActorCount;
            }
        }
        Transform missionAnchor = objective_.transform;
        double axisX = objective_.transform.x -
            params_.playerStartTransform.x;
        double axisZ = objective_.transform.z -
            params_.playerStartTransform.z;
        if (deathmatchEngagement || sprintInterceptor) {
            const Transform& sourceCentroid =
                playerSideActor
                    ? playerLaunchCentroid : opposingLaunchCentroid;
            missionAnchor = playerSideActor
                ? opposingLaunchCentroid : playerLaunchCentroid;
            axisX = missionAnchor.x - sourceCentroid.x;
            axisZ = missionAnchor.z - sourceCentroid.z;
        } else if (sprintRunner) {
            missionAnchor = sprintExitAnchor;
            axisX = sprintExitAnchor.x - opposingLaunchCentroid.x;
            axisZ = sprintExitAnchor.z - opposingLaunchCentroid.z;
        } else if (defenseReaction && opposingLaunchCount > 0u) {
            axisX = opposingLaunchCentroid.x - missionAnchor.x;
            axisZ = opposingLaunchCentroid.z - missionAnchor.z;
        } else if (objectiveGuard) {
            const Transform actorLaunch = launchTransformFor(companion);
            axisX = objective_.transform.x - actorLaunch.x;
            axisZ = objective_.transform.z - actorLaunch.z;
        }
        const double axisLength = std::hypot(axisX, axisZ);
        if (axisLength > 0.0) {
            axisX /= axisLength;
            axisZ /= axisLength;
        } else {
            axisX = 0.0;
            axisZ = 1.0;
        }
        BattleReplacementAiState& state = companion.replacementAi;
        const Transform previousAnchor = state.missionAnchor;
        const Transform previousDestination = state.movementDestination;
        const EntityId previousTarget = state.combatTargetEntityId;
        const BattleReplacementAiTargetKind previousTargetKind =
            state.targetKind;
        const BattleReplacementAiMissionSlice previousMissionSlice =
            state.missionSlice;
        const bool previousMissionTriggered = state.missionTriggered;
        if (!sprintRunner &&
            (state.retreating ||
             combatantShouldRetreatFromDamage(params_, companion))) {
            Combatant* retreatThreat = findCombatant(
                state.retreatThreatEntityId);
            if (retreatThreat == nullptr ||
                retreatThreat->roster.team == companion.roster.team ||
                retreatThreat->missionStatus !=
                    CombatantMissionStatus::Active ||
                combatantMechDestroyed(*retreatThreat)) {
                retreatThreat = findCombatant(companion.lastDamageSourceEntityId);
            }
            if (retreatThreat == nullptr ||
                retreatThreat->roster.team == companion.roster.team ||
                retreatThreat->missionStatus !=
                    CombatantMissionStatus::Active ||
                combatantMechDestroyed(*retreatThreat)) {
                retreatThreat = nullptr;
                double nearestThreatDistance =
                    std::numeric_limits<double>::max();
                for (Combatant& candidate : combatants_) {
                    if (candidate.roster.team == companion.roster.team ||
                        candidate.missionStatus !=
                            CombatantMissionStatus::Active ||
                        combatantMechDestroyed(candidate)) {
                        continue;
                    }
                    const double candidateDistance = distance2d(
                        companion.transform, candidate.transform);
                    if (candidateDistance < nearestThreatDistance) {
                        nearestThreatDistance = candidateDistance;
                        retreatThreat = &candidate;
                    }
                }
            }
            if (!state.retreating) {
                state.retreatDestination =
                    replacementAiRetreatDestination(
                        params_, companion, retreatThreat);
            }
            state.retreating = true;
            state.retreatThreatEntityId = retreatThreat == nullptr
                ? EntityId{} : retreatThreat->id;
            companion.enemyAiState = BattleEnemyAiState::Retreat;
            companion.enemyAiTargetKind = retreatThreat == nullptr
                ? BattleEnemyAiTargetKind::None
                : BattleEnemyAiTargetKind::Player;
            companion.enemyAiTargetEntityId = state.retreatThreatEntityId;
            companion.enemyAiTargetTransform = retreatThreat == nullptr
                ? Transform{} : retreatThreat->transform;
            companion.enemyAiTargetDistance = retreatThreat == nullptr
                ? 0.0
                : distance2d(companion.transform,
                             retreatThreat->transform);
        }
        const bool retreatingFromDamage = state.retreating;
        state.active = true;
        state.missionSlice = missionSlice;
        state.missionTriggered = objectiveGuard
            ? ((previousMissionSlice == missionSlice &&
                previousMissionTriggered) || objectiveGuardTriggered)
            : true;
        ++state.decisionSequence;
        state.lastDecisionTickIndex = tickIndex_;
        state.approachSlot = static_cast<uint8_t>(rank);
        state.approachSlotCount = static_cast<uint8_t>(sideActorCount);
        state.missionAnchor = missionAnchor;
        const BattleWeaponInstanceState* selectedWeapon = nullptr;
        if (!retreatingFromDamage &&
            params_.stationaryTargetHitDiagnosticEnabled) {
            for (const BattleWeaponInstanceState& weapon : companion.weapons) {
                if (weapon.originalRangeValueProven &&
                    weapon.maximumRange > 0.0 &&
                    (weapon.readiness == BattleWeaponReadiness::Ready ||
                     weapon.readiness == BattleWeaponReadiness::Cooldown)) {
                    if (selectedWeapon == nullptr ||
                        std::tie(weapon.slotIndex, weapon.weaponInstanceId) <
                            std::tie(selectedWeapon->slotIndex,
                                     selectedWeapon->weaponInstanceId)) {
                        selectedWeapon = &weapon;
                    }
                }
            }
        } else if (!retreatingFromDamage) {
            selectedWeapon = replacementCombatWeapon(
                companion,
                distance2d(companion.transform, missionAnchor));
        }
        state.selectedWeaponInstanceId = selectedWeapon == nullptr
            ? 0u : selectedWeapon->weaponInstanceId;
        const double missionCombatDistance = selectedWeapon == nullptr
            ? 0.0 : 0.75 * selectedWeapon->maximumRange;
        const bool retrieveRoute = retrievalEngagement && playerSideActor;
        const double forwardOffset = objectiveAssault
            ? std::max(600.0, missionCombatDistance)
            : objectiveDefender
                ? 700.0
                : retrieveRoute ? 600.0 : 900.0;
        const double lateralSpacing = objectiveAssault
            ? 600.0
            : objectiveDefender
                ? 700.0
                : retrieveRoute ? 600.0
                : sprintRunner ? 400.0 : 800.0;
        const double arrivalRadius =
            defenseReaction || retrieveRoute || deathmatchEngagement ||
                    sprintInterception || objectiveGuard
                ? 125.0 : 100.0;
        const double lateral =
            (static_cast<double>(rank) -
             (static_cast<double>(sideActorCount) - 1.0) * 0.5) *
            lateralSpacing;
        state.movementDestination = missionAnchor;
        if (objectiveGuard) {
            state.movementDestination = launchTransformFor(companion);
        } else if (sprintRunner) {
            state.movementDestination.x -= axisZ * lateral;
            state.movementDestination.z += axisX * lateral;
            if (params_.battlefieldBoundary.valid) {
                state.movementDestination.x = std::clamp(
                    state.movementDestination.x,
                    params_.battlefieldBoundary.worldMinX,
                    params_.battlefieldBoundary.worldMaxX);
                state.movementDestination.z = std::clamp(
                    state.movementDestination.z,
                    params_.battlefieldBoundary.worldMinZ,
                    params_.battlefieldBoundary.worldMaxZ);
            }
        } else if (defenseReaction) {
            state.movementDestination.x +=
                axisX * forwardOffset - axisZ * lateral;
            state.movementDestination.z +=
                axisZ * forwardOffset + axisX * lateral;
        } else {
            state.movementDestination.x -=
                axisX * forwardOffset + axisZ * lateral;
            state.movementDestination.z -=
                axisZ * forwardOffset - axisX * lateral;
        }
        if (retreatingFromDamage) {
            state.movementDestination = state.retreatDestination;
        }
        if (previousMissionSlice != state.missionSlice ||
            previousAnchor.x != state.missionAnchor.x ||
            previousAnchor.z != state.missionAnchor.z ||
            previousDestination.x != state.movementDestination.x ||
            previousDestination.z != state.movementDestination.z) {
            state.stuckDecisionCount = 0;
            state.detourDecisionsRemaining = 0;
            state.detourAttempts = 0;
            state.lastDestinationDistance = 0.0;
            state.navigationWaypointActive = false;
            state.pathFailureCount = 0u;
            state.nextPathRetryTickIndex = tickIndex_;
            state.arrivalReached = false;
            state.arrivalTickIndex = 0;
        }

        Combatant* obstruction = nullptr;
        state.proximityThreatOverride = false;
        const auto activeHostile = [&companion](const Combatant* candidate) {
            return candidate != nullptr &&
                candidate->roster.team != companion.roster.team &&
                candidate->missionStatus == CombatantMissionStatus::Active &&
                !combatantMechDestroyed(*candidate);
        };
        Combatant* previousCombatTarget =
            previousTargetKind ==
                    BattleReplacementAiTargetKind::Combatant
                ? findCombatant(previousTarget)
                : nullptr;
        if (!activeHostile(previousCombatTarget)) {
            previousCombatTarget = nullptr;
        }

        // A hostile passing through the local formation is an immediate
        // combat problem even when the lance assignment points elsewhere.
        // Keep the current close target through a wider release radius so two
        // nearby contacts cannot make the torso flick between them each tick.
        constexpr double kCloseThreatAcquireDistance = 1200.0;
        constexpr double kCloseThreatReleaseDistance = 1600.0;
        if (!retreatingFromDamage && !sprintRunner &&
            previousCombatTarget != nullptr &&
            distance2d(companion.transform,
                       previousCombatTarget->transform) <=
                kCloseThreatReleaseDistance) {
            obstruction = previousCombatTarget;
            state.proximityThreatOverride = true;
        } else if (!retreatingFromDamage && !sprintRunner) {
            size_t bestAssignmentCount =
                std::numeric_limits<size_t>::max();
            double bestDistance = kCloseThreatAcquireDistance;
            for (Combatant& candidate : combatants_) {
                if (!activeHostile(&candidate)) {
                    continue;
                }
                const double candidateDistance = distance2d(
                    companion.transform, candidate.transform);
                if (candidateDistance > kCloseThreatAcquireDistance) {
                    continue;
                }
                const size_t assignmentCount = static_cast<size_t>(
                    std::count(assignedTargets.begin(),
                               assignedTargets.end(), candidate.id));
                if (std::tie(assignmentCount, candidateDistance,
                             candidate.id.value) <
                    std::make_tuple(
                        bestAssignmentCount, bestDistance,
                        obstruction == nullptr
                            ? std::numeric_limits<uint64_t>::max()
                            : obstruction->id.value)) {
                    bestAssignmentCount = assignmentCount;
                    bestDistance = candidateDistance;
                    obstruction = &candidate;
                }
            }
            state.proximityThreatOverride = obstruction != nullptr;
        }

        const bool previousTargetDetected =
            !retreatingFromDamage && previousCombatTarget != nullptr &&
            (deathmatchEngagement || sprintInterceptor ||
             (objectiveGuard && state.missionTriggered) ||
             ((objectiveDefender || objectiveApproach) &&
              teamDetectsCombatant(BattleTeam::Player,
                                   *previousCombatTarget)) ||
             (objectiveAssault &&
              previousCombatTarget->id ==
                  state.retaliationTargetEntityId &&
              tickIndex_ <= state.retaliationUntilTickIndex));
        if (obstruction == nullptr && previousTargetDetected &&
            tickIndex_ < state.targetLockUntilTickIndex) {
            obstruction = previousCombatTarget;
        }
        if (!retreatingFromDamage && objectiveAssault &&
            obstruction == nullptr &&
            isValid(state.retaliationTargetEntityId) &&
            tickIndex_ <= state.retaliationUntilTickIndex) {
            Combatant* retaliate = findCombatant(
                state.retaliationTargetEntityId);
            if (retaliate != nullptr &&
                retaliate->roster.team != companion.roster.team &&
                retaliate->missionStatus == CombatantMissionStatus::Active &&
                !combatantMechDestroyed(*retaliate)) {
                obstruction = retaliate;
            }
        }
        if (!retreatingFromDamage && obstruction == nullptr &&
            (deathmatchEngagement || sprintInterceptor ||
             (objectiveGuard && state.missionTriggered))) {
            size_t bestAssignmentCount = std::numeric_limits<size_t>::max();
            int bestCurrentPenalty = std::numeric_limits<int>::max();
            double bestTargetDistance = std::numeric_limits<double>::max();
            for (Combatant& candidate : combatants_) {
                if (candidate.roster.team == companion.roster.team ||
                    candidate.missionStatus != CombatantMissionStatus::Active ||
                    combatantMechDestroyed(candidate)) {
                    continue;
                }
                const bool currentTarget =
                    previousTargetKind ==
                        BattleReplacementAiTargetKind::Combatant &&
                    candidate.id == previousTarget;
                const double targetDistance =
                    distance2d(candidate.transform, companion.transform);
                const size_t assignmentCount = static_cast<size_t>(
                    std::count(assignedTargets.begin(),
                               assignedTargets.end(), candidate.id));
                const int currentPenalty = currentTarget ? 0 : 1;
                if (std::make_tuple(assignmentCount, currentPenalty,
                                    targetDistance, candidate.id.value) <
                    std::make_tuple(
                        bestAssignmentCount, bestCurrentPenalty,
                        bestTargetDistance,
                        obstruction == nullptr
                            ? std::numeric_limits<uint64_t>::max()
                            : obstruction->id.value)) {
                    bestAssignmentCount = assignmentCount;
                    bestCurrentPenalty = currentPenalty;
                    bestTargetDistance = targetDistance;
                    obstruction = &candidate;
                }
            }
        }
        if (!retreatingFromDamage && obstruction == nullptr &&
            (objectiveDefender || objectiveApproach)) {
            size_t bestAssignmentCount = std::numeric_limits<size_t>::max();
            int bestCurrentPenalty = std::numeric_limits<int>::max();
            double bestTargetDistance = std::numeric_limits<double>::max();
            for (Combatant& candidate : combatants_) {
                if (candidate.roster.team != BattleTeam::Opposing ||
                    candidate.missionStatus != CombatantMissionStatus::Active ||
                    combatantMechDestroyed(candidate) ||
                    !teamDetectsCombatant(BattleTeam::Player, candidate)) {
                    continue;
                }
                const bool currentTarget =
                    previousTargetKind ==
                        BattleReplacementAiTargetKind::Combatant &&
                    candidate.id == previousTarget;
                const double targetDistance =
                    distance2d(candidate.transform, companion.transform);
                const size_t assignmentCount = static_cast<size_t>(
                    std::count(assignedTargets.begin(),
                               assignedTargets.end(), candidate.id));
                const int currentPenalty = currentTarget ? 0 : 1;
                if (std::make_tuple(assignmentCount, currentPenalty,
                                    targetDistance, candidate.id.value) <
                    std::make_tuple(
                        bestAssignmentCount, bestCurrentPenalty,
                        bestTargetDistance,
                        obstruction == nullptr
                            ? std::numeric_limits<uint64_t>::max()
                            : obstruction->id.value)) {
                    bestAssignmentCount = assignmentCount;
                    bestCurrentPenalty = currentPenalty;
                    bestTargetDistance = targetDistance;
                    obstruction = &candidate;
                }
            }
        }
        state.targetKind = retreatingFromDamage
            ? BattleReplacementAiTargetKind::None
            : objectiveAssault && obstruction == nullptr
            ? BattleReplacementAiTargetKind::Objective
            : obstruction == nullptr
                ? BattleReplacementAiTargetKind::None
                : BattleReplacementAiTargetKind::Combatant;
        state.combatTargetEntityId = obstruction == nullptr
            ? EntityId{} : obstruction->id;
        if (state.targetKind != previousTargetKind ||
            state.combatTargetEntityId != previousTarget) {
            state.stuckDecisionCount = 0;
            state.detourDecisionsRemaining = 0;
            state.detourAttempts = 0;
            state.lastDestinationDistance = 0.0;
            state.navigationWaypointActive = false;
            state.pathFailureCount = 0u;
            state.nextPathRetryTickIndex = tickIndex_;
            state.nextFireDecisionTickIndex = tickIndex_;
            state.targetLockUntilTickIndex = obstruction == nullptr
                ? 0u
                : tickIndex_ + static_cast<uint64_t>(std::ceil(
                      8.0 / params_.fixedTickSeconds));
            state.combatOrbitActive = false;
            state.combatOrbitDestination = {};
            state.orbitDirection = obstruction == nullptr
                ? 0 : replacementAiPreferredOrbitDirection(companion);
            state.orbitDirectionHoldUntilTickIndex = obstruction == nullptr
                ? 0u
                : tickIndex_ + static_cast<uint64_t>(std::ceil(
                      12.0 / params_.fixedTickSeconds));
            state.desiredCombatDistance = selectedWeapon == nullptr
                ? 0.0
                : (params_.stationaryTargetHitDiagnosticEnabled
                       ? 0.75 : 0.7) * selectedWeapon->maximumRange;
        }
        state.friendlyLaneBlocked = false;
        state.terrainLineOfSightBlocked = false;
        state.aimAligned = false;
        state.lastAimErrorRadians = 0.0;
        state.aimRecoveryActive = false;
        double combatAimHeading = companion.transform.headingRadians;
        double combatBodyRelativeAim = 0.0;
        if (obstruction != nullptr) {
            assignedTargets.push_back(obstruction->id);
        }
        if (!retreatingFromDamage &&
            !params_.stationaryTargetHitDiagnosticEnabled) {
            const std::optional<double> targetDistance =
                obstruction != nullptr
                ? std::optional<double>(distance2d(
                      companion.transform, obstruction->transform))
                : state.targetKind == BattleReplacementAiTargetKind::Objective
                    ? std::optional<double>(distance2d(
                          companion.transform, objective_.transform))
                    : std::nullopt;
            selectedWeapon = replacementCombatWeapon(
                companion, targetDistance);
            state.selectedWeaponInstanceId = selectedWeapon == nullptr
                ? 0u : selectedWeapon->weaponInstanceId;
            const double candidateCombatDistance = selectedWeapon == nullptr
                ? 0.0 : 0.7 * selectedWeapon->maximumRange;
            if (state.desiredCombatDistance <= 0.0 || obstruction == nullptr) {
                state.desiredCombatDistance = candidateCombatDistance;
            }
            const Transform* aimTarget = obstruction != nullptr
                ? &obstruction->transform
                : state.targetKind == BattleReplacementAiTargetKind::Objective
                    ? &objective_.transform : nullptr;
            int desiredTorsoYawStep = 0;
            if (aimTarget != nullptr &&
                params_.playerTorsoYawStepRadians > 0.0) {
                const int aiMaxTorsoYawSteps = std::max(
                    params_.maxPlayerTorsoYawSteps,
                    static_cast<int>(std::llround(
                        (kPi / 2.0) /
                        params_.playerTorsoYawStepRadians)));
                combatAimHeading = std::atan2(
                    aimTarget->x - companion.transform.x,
                    aimTarget->z - companion.transform.z);
                combatBodyRelativeAim = std::remainder(
                    combatAimHeading - companion.transform.headingRadians,
                    kTwoPi);
                desiredTorsoYawStep = clampInt(
                    static_cast<int>(std::llround(
                        combatBodyRelativeAim /
                            params_.playerTorsoYawStepRadians)),
                    -aiMaxTorsoYawSteps,
                    aiMaxTorsoYawSteps);
            }
            if (companion.torsoYawStep != desiredTorsoYawStep &&
                tickIndex_ >= state.nextTorsoTurnTickIndex) {
                companion.torsoYawStep +=
                    desiredTorsoYawStep > companion.torsoYawStep ? 1 : -1;
                constexpr double kReplacementTorsoTurnRateRadiansPerSecond =
                    30.0 * kPi / 180.0;
                const uint64_t torsoStepTicks = std::max<uint64_t>(
                    1u,
                    static_cast<uint64_t>(std::ceil(
                        params_.playerTorsoYawStepRadians /
                        (kReplacementTorsoTurnRateRadiansPerSecond *
                         params_.fixedTickSeconds))));
                state.nextTorsoTurnTickIndex =
                    tickIndex_ + torsoStepTicks;
            }
            if (aimTarget != nullptr && selectedWeapon != nullptr) {
                const double aimTargetDistance = distance2d(
                    companion.transform, *aimTarget);
                const double remainingAimError = std::remainder(
                    combatBodyRelativeAim -
                        torsoYawRadians(params_, companion.torsoYawStep),
                    kTwoPi);
                constexpr double kAimRecoveryThresholdRadians =
                    12.0 * kPi / 180.0;
                state.aimRecoveryActive =
                    aimTargetDistance < selectedWeapon->maximumRange &&
                    std::abs(remainingAimError) >
                        kAimRecoveryThresholdRadians;
            }
        }
        if (obstruction != nullptr) {
            state.desiredCombatDistance = std::max(
                state.desiredCombatDistance,
                companion.collisionRadiusWorld +
                    obstruction->collisionRadiusWorld + 100.0);
        }

        const double destinationDistance =
            distance2d(companion.transform, state.movementDestination);
        if (!retrieveRoute) {
            state.arrivalReached = false;
            state.arrivalTickIndex = 0;
        } else {
            if (state.arrivalReached && destinationDistance > 250.0) {
                state.arrivalReached = false;
                state.arrivalTickIndex = 0;
            }
            if (!state.arrivalReached && obstruction == nullptr &&
                destinationDistance <= arrivalRadius) {
                state.arrivalReached = true;
                state.arrivalTickIndex = tickIndex_;
            }
        }

        Transform finalGoal = obstruction == nullptr
            ? state.movementDestination : obstruction->transform;
        double finalGoalDistance =
            distance2d(companion.transform, finalGoal);
        double finalStopDistance = obstruction == nullptr
            ? arrivalRadius : state.desiredCombatDistance + 50.0;
        state.combatOrbitActive = false;
        if (obstruction != nullptr && state.desiredCombatDistance > 0.0 &&
            finalGoalDistance <= state.desiredCombatDistance + 300.0) {
            const int leftDamage = replacementAiSideDamage(companion, true);
            const int rightDamage = replacementAiSideDamage(companion, false);
            const int8_t preferredDirection =
                replacementAiPreferredOrbitDirection(companion);
            if (state.orbitDirection == 0) {
                state.orbitDirection = preferredDirection;
            } else if (tickIndex_ >= state.orbitDirectionHoldUntilTickIndex &&
                       preferredDirection != state.orbitDirection &&
                       std::abs(leftDamage - rightDamage) >= 10) {
                state.orbitDirection = preferredDirection;
            }
            if (tickIndex_ >= state.orbitDirectionHoldUntilTickIndex) {
                state.orbitDirectionHoldUntilTickIndex = tickIndex_ +
                    static_cast<uint64_t>(std::ceil(
                        12.0 / params_.fixedTickSeconds));
            }
            const double radialDistance = std::max(1.0, finalGoalDistance);
            const double radialX =
                (companion.transform.x - obstruction->transform.x) /
                radialDistance;
            const double radialZ =
                (companion.transform.z - obstruction->transform.z) /
                radialDistance;
            const double orbitRadius = std::max(
                companion.collisionRadiusWorld +
                    obstruction->collisionRadiusWorld + 150.0,
                state.desiredCombatDistance);
            constexpr double kOrbitAdvanceRadians = kPi / 6.0;
            const double orbitSin = std::sin(kOrbitAdvanceRadians) *
                static_cast<double>(state.orbitDirection);
            const double orbitCos = std::cos(kOrbitAdvanceRadians);
            state.combatOrbitDestination = obstruction->transform;
            state.combatOrbitDestination.x += orbitRadius *
                (radialX * orbitCos + radialZ * orbitSin);
            state.combatOrbitDestination.z += orbitRadius *
                (-radialX * orbitSin + radialZ * orbitCos);
            const double orbitBoundaryMargin =
                companion.collisionRadiusWorld + 50.0;
            if (params_.battlefieldBoundary.valid &&
                params_.battlefieldBoundary.worldMinX + orbitBoundaryMargin <
                    params_.battlefieldBoundary.worldMaxX - orbitBoundaryMargin &&
                params_.battlefieldBoundary.worldMinZ + orbitBoundaryMargin <
                    params_.battlefieldBoundary.worldMaxZ - orbitBoundaryMargin) {
                state.combatOrbitDestination.x = std::clamp(
                    state.combatOrbitDestination.x,
                    params_.battlefieldBoundary.worldMinX + orbitBoundaryMargin,
                    params_.battlefieldBoundary.worldMaxX - orbitBoundaryMargin);
                state.combatOrbitDestination.z = std::clamp(
                    state.combatOrbitDestination.z,
                    params_.battlefieldBoundary.worldMinZ + orbitBoundaryMargin,
                    params_.battlefieldBoundary.worldMaxZ - orbitBoundaryMargin);
            } else if (params_.constrainToTerrainBounds) {
                clampToTerrainBounds(terrain_, state.combatOrbitDestination);
            }
            finalGoal = state.combatOrbitDestination;
            finalGoalDistance = distance2d(companion.transform, finalGoal);
            finalStopDistance = 100.0;
            state.combatOrbitActive = true;
        } else {
            state.combatOrbitDestination = {};
        }
        BattleReplacementAiNavMode stationaryMissionMode =
            BattleReplacementAiNavMode::ApproachObjective;
        BattleReplacementAiNavMode movingMissionMode =
            BattleReplacementAiNavMode::ApproachObjective;
        BattleReplacementAiNavMode combatMode =
            BattleReplacementAiNavMode::EngageObstruction;
        if (retreatingFromDamage) {
            stationaryMissionMode =
                BattleReplacementAiNavMode::RetreatToBoundary;
            movingMissionMode =
                BattleReplacementAiNavMode::RetreatToBoundary;
            combatMode = BattleReplacementAiNavMode::RetreatToBoundary;
        } else if (objectiveAssault) {
            stationaryMissionMode = BattleReplacementAiNavMode::AssaultObjective;
            movingMissionMode = BattleReplacementAiNavMode::AssaultObjective;
            combatMode = BattleReplacementAiNavMode::EngageOpponent;
        } else if (objectiveGuard) {
            stationaryMissionMode =
                BattleReplacementAiNavMode::HoldingObjectiveGuard;
            movingMissionMode =
                BattleReplacementAiNavMode::HoldingObjectiveGuard;
            combatMode = BattleReplacementAiNavMode::EngageOpponent;
        } else if (sprintRunner) {
            stationaryMissionMode = BattleReplacementAiNavMode::SprintToExit;
            movingMissionMode = BattleReplacementAiNavMode::SprintToExit;
        } else if (sprintInterceptor) {
            stationaryMissionMode =
                BattleReplacementAiNavMode::InterceptRunner;
            movingMissionMode = BattleReplacementAiNavMode::InterceptRunner;
            combatMode = BattleReplacementAiNavMode::InterceptRunner;
        } else if (deathmatchEngagement) {
            stationaryMissionMode =
                BattleReplacementAiNavMode::HoldingEngagementLine;
            movingMissionMode =
                BattleReplacementAiNavMode::AdvanceToEngagement;
            combatMode = BattleReplacementAiNavMode::EngageOpponent;
        } else if (defenseReaction) {
            stationaryMissionMode = BattleReplacementAiNavMode::Guarding;
            movingMissionMode = BattleReplacementAiNavMode::DefendAnchor;
            combatMode = BattleReplacementAiNavMode::EngageThreat;
        }

        const double waypointArrivalRadius = std::max(
            125.0, companion.collisionRadiusWorld + 50.0);
        if (state.navigationWaypointActive &&
            (distance2d(companion.transform, state.navigationWaypoint) <=
                 waypointArrivalRadius ||
             !replacementPathSegmentClear(
                 params_.terrainCollisionObstacles,
                 companion.transform,
                 state.navigationWaypoint,
                 companion.collisionRadiusWorld + 100.0))) {
            state.navigationWaypointActive = false;
            state.lastDestinationDistance = 0.0;
            state.nextPathRetryTickIndex = tickIndex_;
        }
        const bool finalPathBlocked =
            finalGoalDistance > finalStopDistance + waypointArrivalRadius &&
            !replacementPathSegmentClear(
                params_.terrainCollisionObstacles,
                companion.transform,
                finalGoal,
                companion.collisionRadiusWorld + 100.0);
        if (!state.navigationWaypointActive && finalPathBlocked &&
            tickIndex_ >= state.nextPathRetryTickIndex) {
            const ReplacementPathWaypointPlan plan =
                replacementPathWaypointPlan(
                    terrain_, params_.terrainCollisionObstacles,
                    companion.transform, finalGoal,
                    companion.collisionRadiusWorld);
            if (plan.valid &&
                distance2d(companion.transform, plan.waypoint) >
                    waypointArrivalRadius) {
                state.navigationWaypoint = plan.waypoint;
                state.navigationWaypointActive = true;
                state.pathReplanCount = std::min<uint32_t>(
                    std::numeric_limits<uint32_t>::max() - 1u,
                    state.pathReplanCount) + 1u;
                state.pathFailureCount = 0u;
                state.stuckDecisionCount = 0u;
                state.detourDecisionsRemaining = 0u;
                state.lastDestinationDistance = 0.0;
                state.nextPathRetryTickIndex = tickIndex_ + 10u;
            } else {
                state.pathFailureCount = std::min<uint32_t>(
                    std::numeric_limits<uint32_t>::max() - 1u,
                    state.pathFailureCount) + 1u;
                state.stuckDecisionCount = std::max<uint8_t>(
                    state.stuckDecisionCount, 8u);
                state.nextPathRetryTickIndex = tickIndex_ + 20u;
            }
        }
        const Transform navigationGoal = state.navigationWaypointActive
            ? state.navigationWaypoint : finalGoal;
        const double goalDistance =
            distance2d(companion.transform, navigationGoal);
        const double stopDistance = state.navigationWaypointActive
            ? waypointArrivalRadius : finalStopDistance;
        state.lastMoveRejected = companion.collisionContact ||
            companion.boundaryContact;
        const double progressEpsilon = std::max(
            2.0,
            companion.maxForwardSpeed * params_.fixedTickSeconds * 0.1);
        if (state.combatOrbitActive && !state.navigationWaypointActive) {
            state.lastDestinationDistance = goalDistance;
            if (state.lastMoveRejected && companion.throttle > 0.0) {
                state.stuckDecisionCount = static_cast<uint8_t>(std::min(
                    8, static_cast<int>(state.stuckDecisionCount) + 1));
            } else {
                state.stuckDecisionCount = 0;
            }
        } else if (state.lastDestinationDistance == 0.0 ||
            state.detourDecisionsRemaining > 0) {
            state.stuckDecisionCount = 0;
            state.lastDestinationDistance = goalDistance;
        } else if (goalDistance <
                   state.lastDestinationDistance - progressEpsilon) {
            state.stuckDecisionCount = 0;
            state.lastDestinationDistance = goalDistance;
        } else if (state.lastMoveRejected || companion.throttle > 0.0) {
            state.stuckDecisionCount = static_cast<uint8_t>(std::min(
                8, static_cast<int>(state.stuckDecisionCount) + 1));
        }
        if (retrieveRoute && state.arrivalReached && obstruction == nullptr) {
            state.navMode = BattleReplacementAiNavMode::Arrived;
            state.stuckDecisionCount = 0;
            state.detourDecisionsRemaining = 0;
            state.detourAttempts = 0;
            state.lastDestinationDistance = destinationDistance;
            state.navigationWaypointActive = false;
            companion.throttle = 0.0;
            companion.turn = 0.0;
            continue;
        }
        if (state.stuckDecisionCount >= 8 &&
            state.detourDecisionsRemaining == 0) {
            const uint32_t recoveryLevel = std::min<uint32_t>(
                5u, state.pathFailureCount + state.detourAttempts);
            state.detourDecisionsRemaining = static_cast<uint8_t>(
                std::min<uint32_t>(220u, 60u + recoveryLevel * 30u));
            state.detourAttempts = state.detourAttempts == 255u
                ? 1u
                : static_cast<uint8_t>(state.detourAttempts + 1u);
            state.stuckDecisionCount = 0;
            state.navigationWaypointActive = false;
            state.nextPathRetryTickIndex = tickIndex_ +
                state.detourDecisionsRemaining;
        }
        if (goalDistance <= stopDistance &&
            state.detourDecisionsRemaining == 0) {
            state.navMode = obstruction == nullptr
                ? stationaryMissionMode : combatMode;
            companion.throttle = 0.0;
        } else {
            state.navMode = state.detourDecisionsRemaining > 0
                ? BattleReplacementAiNavMode::Detour
                : state.navigationWaypointActive
                    ? BattleReplacementAiNavMode::PathWaypoint
                : obstruction == nullptr
                    ? movingMissionMode : combatMode;
            companion.throttle = retreatingFromDamage
                ? 1.0 : state.combatOrbitActive ? 0.5 : 0.6;
        }
        const Transform& facingGoal = objectiveAssault &&
                goalDistance <= stopDistance &&
                state.detourDecisionsRemaining == 0 &&
                !state.navigationWaypointActive
            ? state.missionAnchor : navigationGoal;
        double desiredHeading = std::atan2(
            facingGoal.x - companion.transform.x,
            facingGoal.z - companion.transform.z);
        if (state.detourDecisionsRemaining > 0) {
            const bool turnLeft =
                (static_cast<size_t>(state.detourAttempts) + rank) % 2u == 0u;
            const double recoveryAngle =
                kPi / 3.0 +
                static_cast<double>(state.detourAttempts % 3u) * kPi / 12.0;
            desiredHeading += (turnLeft ? 1.0 : -1.0) * recoveryAngle;
            --state.detourDecisionsRemaining;
        }
        if (state.aimRecoveryActive && obstruction != nullptr &&
            !state.navigationWaypointActive) {
            desiredHeading = combatAimHeading;
            constexpr double kReverseAimRecoveryRadians =
                120.0 * kPi / 180.0;
            companion.throttle =
                std::abs(combatBodyRelativeAim) >=
                        kReverseAimRecoveryRadians
                    ? -0.25
                    : 0.0;
        }
        double delta = std::remainder(
            desiredHeading - companion.transform.headingRadians, kTwoPi);
        companion.turn = std::clamp(
            delta / (params_.maxTurnRateRadians * params_.fixedTickSeconds),
            -1.0, 1.0);
        if (!state.aimRecoveryActive) {
            if (std::abs(delta) > kPi / 3.0) {
                companion.throttle = 0.0;
            } else if (std::abs(delta) > kPi / 6.0) {
                companion.throttle *= 0.25;
            }
        }
    }
}

void BattleWorld::updateOriginalMovement() {
    constexpr double kOriginalFirstUpdateSeconds = 0.1;
    constexpr double kTwoPi = 6.28318530717958647692;
    constexpr double kPi = 3.14159265358979323846;

    const auto loadControlWord = [](
                                     const BattleOriginalAiMotionRuntimeState& state,
                                     size_t offset) {
        return static_cast<int16_t>(static_cast<uint16_t>(
            state.controlRecordBytes[offset] |
            (static_cast<uint16_t>(state.controlRecordBytes[offset + 1u]) <<
             8u)));
    };
    const auto storeControlWord = [](
                                      BattleOriginalAiMotionRuntimeState& state,
                                      size_t offset,
                                      int16_t value) {
        const uint16_t bits = static_cast<uint16_t>(value);
        state.controlRecordBytes[offset] =
            static_cast<uint8_t>(bits & 0xffu);
        state.controlRecordBytes[offset + 1u] =
            static_cast<uint8_t>((bits >> 8u) & 0xffu);
    };
    const auto setupSlot = [this](uint8_t liveSlot)
        -> const BattleSetupSlotMetadata* {
        const bool playerSide = liveSlot < 4u;
        const size_t sideIndex = playerSide
            ? static_cast<size_t>(liveSlot)
            : static_cast<size_t>(liveSlot - 4u);
        const auto& slots = playerSide
            ? params_.setupMetadata.playerSlots
            : params_.setupMetadata.opposingSlots;
        if (sideIndex >= slots.size()) {
            return nullptr;
        }
        const BattleSetupSlotMetadata& slot = slots[sideIndex];
        return slot.valid && slot.originalRawPositionProven ? &slot : nullptr;
    };
    const auto originalRawPosition = [&setupSlot](
                                         const Combatant& combatant,
                                         int32_t& rawX,
                                         int32_t& rawY,
                                         int32_t& rawZ) {
        if (!combatant.roster.originalLiveObjectSlot) {
            return false;
        }
        if (combatant.originalAiMotion.initialized) {
            rawX = combatant.originalAiMotion.rawX;
            rawY = combatant.originalAiMotion.rawY;
            rawZ = combatant.originalAiMotion.rawZ;
            return true;
        }
        const BattleSetupSlotMetadata* source =
            setupSlot(*combatant.roster.originalLiveObjectSlot);
        if (source == nullptr) {
            return false;
        }
        rawX = source->originalRawX;
        rawY = 300;
        rawZ = source->originalRawZ;
        return true;
    };

    std::array<Combatant*, 8> liveCombatants{};
    for (Combatant& combatant : combatants_) {
        if (combatant.roster.originalLiveObjectSlot &&
            *combatant.roster.originalLiveObjectSlot < liveCombatants.size()) {
            liveCombatants[*combatant.roster.originalLiveObjectSlot] =
                &combatant;
        }
    }

    const bool modeThreeNullDataPolicy =
        params_.combatAiPolicy == BattleCombatAiPolicy::
            OriginalBtechModeThreeNullDataPointRepeatedMovement;
    const bool modeThreeMissionObjectiveCompatibilityPolicy =
        params_.combatAiPolicy == BattleCombatAiPolicy::
            CompatibilityModeThreePlayerCompanionMissionObjectiveRepeatedMovement;
    const BattleSetupSlotMetadata* modeThreeMissionObjectiveRawPoint = nullptr;
    if (modeThreeMissionObjectiveCompatibilityPolicy &&
        params_.setupMetadata.objectiveOpposingSlotIndex <
            params_.setupMetadata.opposingSlots.size()) {
        const BattleSetupSlotMetadata& source =
            params_.setupMetadata.opposingSlots[
                params_.setupMetadata.objectiveOpposingSlotIndex];
        if (source.valid && source.originalRawPositionProven) {
            modeThreeMissionObjectiveRawPoint = &source;
        }
    }
    if (modeThreeNullDataPolicy ||
        modeThreeMissionObjectiveCompatibilityPolicy) {
        if (modeThreeMissionObjectiveCompatibilityPolicy &&
            modeThreeMissionObjectiveRawPoint == nullptr) {
            return;
        }
        const int32_t pointRawX = modeThreeNullDataPolicy
            ? kOriginalModeThreeNullDataRawX
            : modeThreeMissionObjectiveRawPoint->originalRawX;
        const int32_t pointRawY = modeThreeNullDataPolicy
            ? kOriginalModeThreeNullDataRawY
            : 300;
        const int32_t pointRawZ = modeThreeNullDataPolicy
            ? kOriginalModeThreeNullDataRawZ
            : modeThreeMissionObjectiveRawPoint->originalRawZ;
        for (uint8_t movingSlot = 1u; movingSlot < 4u; ++movingSlot) {
            Combatant* mover = liveCombatants[movingSlot];
            if (mover == nullptr || mover->playerControlled ||
                mover->roster.team != BattleTeam::Player ||
                !mover->originalAiMotion.initialized ||
                mover->originalAiMotion.numericSideModeWord != 3) {
                continue;
            }

            mover->originalAiUpdateAccumulator +=
                params_.fixedTickSeconds / kOriginalFirstUpdateSeconds;
            if (mover->originalAiUpdateAccumulator < 1.0) {
                continue;
            }
            mover->originalAiUpdateAccumulator -= 1.0;
            ++mover->originalAiUpdateCount;

            BattleOriginalAiMotionRuntimeState& state = mover->originalAiMotion;
            const bool firstAttempt = !state.firstMovementUpdateAttempted;
            if (firstAttempt) {
                state.firstMovementUpdateAttempted = true;
                state.firstMovementDecisionTickIndex = tickIndex_;
            }
            ++state.movementUpdateAttemptCount;
            state.lastMovementDecisionTickIndex = tickIndex_;
            state.lastMovementUpdateResult =
                BattleOriginalAiFirstMovementUpdateResult::None;
            state.lastMovementTargetEntityId = {};
            state.lastMovementTargetLiveObjectSlot = 8u;
            state.lastMovementRelationWord = 0u;
            state.lastMovementRelationSampled = false;
            state.lastMovementRelationSampleCount = 0u;
            state.lastMovementMoverAggregateWord16 = 0;
            state.lastMovementTargetAggregateWord16 = 0;
            state.lastMovementSelection50eeIndependent = false;
            state.lastMovementPostStepExact = false;
            state.lastMovementPostStepActionCode = 0u;
            state.provenance = modeThreeNullDataPolicy
                ? "BTECH.EXE:loader_zero_CCEE+DS0008_runtime_bytes:"
                  "1a22+7797_mode3+94a4+9bfa:"
                  "player_companion_repeated:0.1s_provisional"
                : "compatibility_provisional:BTECH_mode3_CCEE_null:"
                  "destroy_disable_retrieve_mission_objective_point:"
                  "live_object_steering_lane_omitted:"
                  "player_companion_repeated:0.1s_provisional";

            const auto recordResult = [&state, firstAttempt](
                                          BattleOriginalAiFirstMovementUpdateResult value) {
                state.lastMovementUpdateResult = value;
                if (firstAttempt) {
                    state.firstMovementUpdateResult = value;
                }
            };
            if (loadControlWord(state, 0x4cu) != 0 ||
                state.controlRecordBytes[0x50u] != 0u ||
                loadControlWord(state, 0x51u) != 0 ||
                loadControlWord(state, 0x53u) != 0) {
                recordResult(
                    BattleOriginalAiFirstMovementUpdateResult::
                        RejectedPostStepStateOpen);
                continue;
            }
            if (combatantMovementBlocked(*mover)) {
                recordResult(
                    BattleOriginalAiFirstMovementUpdateResult::
                        RejectedMovementBlocked);
                continue;
            }

            const BattleOriginalAiActOnOwnRouteDiagnostic route =
                battleOriginalAiActOnOwnRoute7797(
                    loadControlWord(state, 0x00u),
                    state.numericSideModeWord);
            if (!route.exact ||
                route.route != BattleOriginalAiActOnOwnRoute::SeparateObjectPoint ||
                !route.requiresSeparateObjectPose || route.status46 != 6) {
                recordResult(
                    BattleOriginalAiFirstMovementUpdateResult::
                        RejectedMovementTransaction);
                continue;
            }
            storeControlWord(state, 0x46u, route.status46);
            storeControlWord(state, 0x1eu, 8);

            BattleOriginalAiSelectedTargetMovementInput movementInput;
            movementInput.movingSlot = movingSlot;
            movementInput.movingOwnerWord = state.sideWord;
            movementInput.controlWord00 = loadControlWord(state, 0x00u);
            movementInput.controlState46 = loadControlWord(state, 0x46u);
            movementInput.numericState51 = loadControlWord(state, 0x51u);
            movementInput.rawX = state.rawX;
            movementInput.rawY = state.rawY;
            movementInput.rawZ = state.rawZ;
            movementInput.pitch = state.pitch;
            movementInput.roll = state.roll;
            movementInput.heading = state.heading;
            movementInput.controlWord30 = state.controlWord30;
            movementInput.controlWord32 = state.controlWord32;
            movementInput.driftWord36 = state.driftWord36;
            movementInput.driftWord38 = state.driftWord38;
            movementInput.driftWord3a = state.driftWord3a;
            movementInput.rawSpeed = state.rawSpeedWord3c;
            const mech3d::MechCatalogMobility mobility =
                mech3d::catalogMobility(mover->mechPresetId);
            movementInput.definitionWord06 = static_cast<int16_t>(
                mobility.originalBtechDefinitionSpeedWord06);
            movementInput.definitionHeightWord02 = static_cast<int16_t>(
                mobility.originalBtechDefinitionHeightWord02);
            const int heatAboveFirstThreshold =
                std::max(0, mover->heat.rawHeat - 400);
            movementInput.heatSpeedPenalty = static_cast<int16_t>(
                ((heatAboveFirstThreshold + 399) / 400) * 6);
            const auto& criticals =
                mover->mechRuntime.detailedDamage.criticalComponents;
            movementInput.leftLegActuatorWord = criticals[static_cast<size_t>(
                mech3d::MechCriticalComponentId::LeftLegActuator)].workingCount;
            movementInput.rightLegActuatorWord = criticals[static_cast<size_t>(
                mech3d::MechCriticalComponentId::RightLegActuator)].workingCount;
            movementInput.selectorWord3a = loadControlWord(state, 0x3au);
            movementInput.selectorWord3c = loadControlWord(state, 0x3cu);
            movementInput.previousSlotProbeWord = state.slotProbeWord;
            movementInput.controlState4c = loadControlWord(state, 0x4cu);
            movementInput.controlState53 = loadControlWord(state, 0x53u);
            movementInput.selectedTargetOwnership =
                modeThreeNullDataPolicy
                    ? BattleOriginalAiMovementTargetOwnership::
                          ModeThreeNullDataPoint
                    : BattleOriginalAiMovementTargetOwnership::
                          MissionObjectivePoint;
            movementInput.selectedTargetRawX = pointRawX;
            movementInput.selectedTargetRawY = pointRawY;
            movementInput.selectedTargetRawZ = pointRawZ;
            movementInput.objectiveLaneEnabled = true;
            movementInput.objectiveInactiveBitSet = false;
            movementInput.opaqueObjectiveGateWordEd0 = 3;
            movementInput.objectiveRawX = pointRawX;
            movementInput.objectiveRawZ = pointRawZ;
            movementInput.liveSlots.resize(8u);
            bool livePositionOpen = false;
            for (uint8_t liveSlot = 0u; liveSlot < 8u; ++liveSlot) {
                if (modeThreeMissionObjectiveCompatibilityPolicy) {
                    // The replacement campaign fallback is a point route,
                    // not the recovered live-target steering route. Keep its
                    // live-object lane empty so neither the launch formation
                    // nor the single target enemy becomes the destination or
                    // permanently suppresses a companion. Authoritative
                    // battle collision still resolves after movement. This
                    // compatibility boundary is explicit in provenance.
                    continue;
                }
                Combatant* live = liveCombatants[liveSlot];
                if (live == nullptr) {
                    continue;
                }
                int32_t rawX = 0;
                int32_t rawY = 0;
                int32_t rawZ = 0;
                if (!originalRawPosition(*live, rawX, rawY, rawZ)) {
                    livePositionOpen = true;
                    break;
                }
                auto& liveInput = movementInput.liveSlots[liveSlot];
                liveInput.entityId = live->id;
                liveInput.occupied = true;
                liveInput.ownerWord = liveSlot < 4u ? 0 : 1;
                liveInput.rawX = rawX;
                liveInput.rawY = rawY;
                liveInput.rawZ = rawZ;
            }
            if (livePositionOpen) {
                recordResult(
                    BattleOriginalAiFirstMovementUpdateResult::
                        RejectedMovementTransaction);
                continue;
            }
            if (state.sceneCacheValid) {
                movementInput.cachedSceneObjectIndex =
                    state.sceneCacheObjectIndex;
                movementInput.cachedSubrecordIndex =
                    state.sceneCacheSubrecordIndex;
            }

            const BattleOriginalAiSelectedTargetMovementDiagnostic movement =
                battleOriginalAiSelectedTargetMovement(
                    params_.terrainCollisionGrid,
                    params_.originalTerrainSceneCatalog,
                    movementInput);
            if (!movement.exact || !movement.targetAcquisitionClosed) {
                recordResult(
                    BattleOriginalAiFirstMovementUpdateResult::
                        RejectedMovementTransaction);
                continue;
            }
            if (!movement.poseTransaction.commit.poseAccepted) {
                recordResult(
                    BattleOriginalAiFirstMovementUpdateResult::RejectedPoseCommit);
                continue;
            }
            if (!movement.poseTransaction.setsMovingFlagBit4) {
                recordResult(
                    BattleOriginalAiFirstMovementUpdateResult::
                        RejectedPostStepStateOpen);
                continue;
            }

            const BattleOriginalAiOrdinaryModeZeroPostStepDiagnostic postStep =
                battleOriginalAiOrdinaryModeZeroPostStep9bfa(
                    movement.poseTransaction.controlState4cAfter,
                    0x10u,
                    loadControlWord(state, 0x51u),
                    movement.poseTransaction.controlState53After);
            state.lastMovementPostStepExact = postStep.exact;
            state.lastMovementPostStepActionCode =
                postStep.selectedActionCode;
            if (!postStep.exact) {
                recordResult(
                    BattleOriginalAiFirstMovementUpdateResult::
                        RejectedPostStepStateOpen);
                continue;
            }

            const auto& pose = movement.poseTransaction;
            state.rawX = pose.xAfter;
            state.rawY = pose.yAfter;
            state.rawZ = pose.zAfter;
            state.pitch = pose.pitchAfter;
            state.roll = pose.rollAfter;
            state.heading = pose.headingAfter;
            state.controlWord30 = pose.controlWord30After;
            state.controlWord32 = pose.controlWord32After;
            state.controlWord34 = pose.controlWord34After;
            state.driftWord36 = pose.driftWord36After;
            state.driftWord38 = pose.driftWord38After;
            state.driftWord3a = pose.driftWord3aAfter;
            state.rawSpeedWord3c = pose.rawSpeedAfter;
            state.slotProbeWord = pose.slotProbeWordAfter;
            state.sceneCacheValid = pose.sceneCacheValidAfter;
            state.sceneCacheObjectIndex = pose.sceneCacheObjectIndexAfter;
            state.sceneCacheSubrecordIndex = pose.sceneCacheSubrecordIndexAfter;
            if (movement.steering.exact) {
                storeControlWord(
                    state, 0x3au, movement.steering.selectorWord3aAfter);
                storeControlWord(
                    state, 0x3cu, movement.steering.selectorWord3cAfter);
            }
            storeControlWord(
                state, 0x4cu, postStep.animationModeWord4cAfter);
            state.controlRecordBytes[0x50u] = postStep.commandByte50After;
            storeControlWord(
                state, 0x51u, postStep.sharedStateWord51After);
            storeControlWord(
                state, 0x53u, postStep.frameWord53After);

            const double rawToWorld = params_.terrainCellSize / 512.0;
            const int32_t halfDefinitionHeight =
                static_cast<int32_t>(
                    mobility.originalBtechDefinitionHeightWord02) /
                2;
            mover->transform.x =
                (static_cast<double>(state.rawX) + 44160.0) * rawToWorld;
            mover->transform.y =
                static_cast<double>(state.rawY - halfDefinitionHeight) *
                rawToWorld;
            mover->transform.z =
                (24000.0 - static_cast<double>(state.rawZ)) * rawToWorld;
            mover->transform.headingRadians = std::remainder(
                static_cast<double>(state.heading) * kTwoPi / 65536.0 + kPi,
                kTwoPi);
            mover->throttle = 0.0;
            mover->turn = 0.0;
            mover->targetForwardSpeed = 0.0;
            mover->forwardSpeed = 0.0;
            state.worldPositionBindingExact = true;
            if (firstAttempt) {
                state.firstMovementTargetEntityId = {};
                state.firstMovementTargetLiveObjectSlot = 8u;
                state.firstMovementUpdateCommitted = true;
            }
            recordResult(BattleOriginalAiFirstMovementUpdateResult::Committed);
            ++state.acceptedPoseCommitCount;
            state.lastPoseCommitTickIndex = tickIndex_;
        }
        return;
    }

    Combatant* target = nullptr;
    size_t activeOpposingCount = 0u;
    for (Combatant& candidate : combatants_) {
        if (candidate.roster.team != BattleTeam::Opposing ||
            candidate.missionStatus != CombatantMissionStatus::Active ||
            combatantMechDestroyed(candidate)) {
            continue;
        }
        target = &candidate;
        ++activeOpposingCount;
    }

    const bool repeatedPolicy =
        params_.combatAiPolicy ==
            BattleCombatAiPolicy::OriginalBtechModeZeroRepeatedMovement ||
        params_.combatAiPolicy == BattleCombatAiPolicy::
            OriginalBtechModeZeroMultiTargetRepeatedMovement ||
        params_.combatAiPolicy == BattleCombatAiPolicy::
            OriginalBtechNonObjectiveModesMultiTargetRepeatedMovement ||
        params_.combatAiPolicy == BattleCombatAiPolicy::
            OriginalBtechNonObjectiveSymmetricRepeatedMovement50eeInvariant ||
        params_.combatAiPolicy == BattleCombatAiPolicy::
            CompatibilityModeThreePlayerCompanionSelectedTargetRepeatedMovement;
    const bool multipleTargetPolicy =
        params_.combatAiPolicy == BattleCombatAiPolicy::
            OriginalBtechModeZeroMultiTargetRepeatedMovement ||
        params_.combatAiPolicy == BattleCombatAiPolicy::
            OriginalBtechNonObjectiveModesMultiTargetRepeatedMovement ||
        params_.combatAiPolicy == BattleCombatAiPolicy::
            OriginalBtechNonObjectiveSymmetricRepeatedMovement50eeInvariant ||
        params_.combatAiPolicy == BattleCombatAiPolicy::
            CompatibilityModeThreePlayerCompanionSelectedTargetRepeatedMovement;
    const bool nonObjectiveModesPolicy =
        params_.combatAiPolicy == BattleCombatAiPolicy::
            OriginalBtechNonObjectiveModesMultiTargetRepeatedMovement ||
        params_.combatAiPolicy == BattleCombatAiPolicy::
            OriginalBtechNonObjectiveSymmetricRepeatedMovement50eeInvariant;
    const bool symmetricPolicy =
        params_.combatAiPolicy == BattleCombatAiPolicy::
            OriginalBtechNonObjectiveSymmetricRepeatedMovement50eeInvariant;
    const bool compatibilityModeThreePlayerPolicy =
        params_.combatAiPolicy == BattleCombatAiPolicy::
            CompatibilityModeThreePlayerCompanionSelectedTargetRepeatedMovement;
    const bool selectedLiveTargetModesPolicy =
        nonObjectiveModesPolicy || compatibilityModeThreePlayerPolicy;
    std::array<int16_t, 8> selectionCounts{};

    const uint8_t movingSlotEnd = symmetricPolicy ? 8u : 4u;
    for (uint8_t movingSlot = 1u; movingSlot < movingSlotEnd;
         ++movingSlot) {
        Combatant* mover = liveCombatants[movingSlot];
        const bool moverPlayerSide = movingSlot < 4u;
        const BattleTeam expectedMoverTeam = moverPlayerSide
            ? BattleTeam::Player
            : BattleTeam::Opposing;
        if (mover == nullptr || mover->playerControlled ||
            mover->roster.team != expectedMoverTeam ||
            !mover->originalAiMotion.initialized ||
            (selectedLiveTargetModesPolicy
                 ? (compatibilityModeThreePlayerPolicy
                        ? mover->originalAiMotion.numericSideModeWord != 3
                        : (mover->originalAiMotion.numericSideModeWord < 0 ||
                           mover->originalAiMotion.numericSideModeWord > 2))
                 : mover->originalAiMotion.numericSideModeWord != 0) ||
            (!repeatedPolicy &&
             mover->originalAiMotion.firstMovementUpdateAttempted)) {
            continue;
        }

        mover->originalAiUpdateAccumulator +=
            params_.fixedTickSeconds / kOriginalFirstUpdateSeconds;
        if (mover->originalAiUpdateAccumulator < 1.0) {
            continue;
        }
        mover->originalAiUpdateAccumulator -= 1.0;
        ++mover->originalAiUpdateCount;

        BattleOriginalAiMotionRuntimeState& state = mover->originalAiMotion;
        const bool firstAttempt = !state.firstMovementUpdateAttempted;
        if (firstAttempt) {
            state.firstMovementUpdateAttempted = true;
            state.firstMovementDecisionTickIndex = tickIndex_;
        }
        ++state.movementUpdateAttemptCount;
        state.lastMovementDecisionTickIndex = tickIndex_;
        state.lastMovementUpdateResult =
            BattleOriginalAiFirstMovementUpdateResult::None;
        state.lastMovementTargetEntityId = {};
        state.lastMovementTargetLiveObjectSlot = 0xffu;
        state.lastMovementRelationWord = 0u;
        state.lastMovementRelationSampled = false;
        state.lastMovementRelationSampleCount = 0u;
        state.lastMovementMoverAggregateWord16 = 0;
        state.lastMovementTargetAggregateWord16 = 0;
        state.lastMovementSelection50eeIndependent = false;
        state.lastMovementPostStepExact = false;
        state.lastMovementPostStepActionCode = 0u;
        state.provenance =
            compatibilityModeThreePlayerPolicy
                ? "compatibility_provisional:BTECH_mode3_CCEE_null:"
                  "23bf_selected_live_target+7c3c+9bfa:"
                  "player_companion_only:0.1s_provisional"
                : symmetricPolicy
                ? "BTECH.EXE:71e1+4ab5+23bf+9bfa:"
                  "symmetric_modes0_1_2_action3:"
                  "opposing_slot0_relation_exhaustive:"
                  "companion_winner_invariant:0.1s_provisional"
                : nonObjectiveModesPolicy
                ? "BTECH.EXE:71e1+4ab5+23bf+9bfa:"
                  "multi_target_modes0_1_2_action3:"
                  "player_side_nonplayer:5206_in_bounds:"
                  "stationary_opposing_raw_pose:0.1s_provisional"
                : multipleTargetPolicy
                ? "BTECH.EXE:71e1+4ab5+23bf+9bfa:"
                  "multi_target_mode0_action3:player_side_nonplayer:"
                  "5206_in_bounds:stationary_opposing_raw_pose:"
                  "0.1s_provisional"
                : repeatedPolicy
                ? "BTECH.EXE:71e1+9bfa:repeated_mode0_action3:"
                  "27fb+7070+23bf+7c3c:player_side_nonplayer:"
                  "5206_in_bounds:0.1s_provisional"
                : "BTECH.EXE:71e1:first_update:27fb+7070+23bf+7c3c:"
                  "player_side_nonplayer:5206_in_bounds:0.1s_provisional";

        const auto recordResult = [&](BattleOriginalAiFirstMovementUpdateResult value) {
            state.lastMovementUpdateResult = value;
            if (firstAttempt) {
                state.firstMovementUpdateResult = value;
            }
        };
        const auto recordTarget = [&](EntityId entityId, uint8_t liveSlot) {
            state.lastMovementTargetEntityId = entityId;
            state.lastMovementTargetLiveObjectSlot = liveSlot;
            if (firstAttempt) {
                state.firstMovementTargetEntityId = entityId;
                state.firstMovementTargetLiveObjectSlot = liveSlot;
            }
        };
        const auto recordRelation = [&](uint16_t relationWord,
                                        bool sampled,
                                        uint16_t sampleCount) {
            state.lastMovementRelationWord = relationWord;
            state.lastMovementRelationSampled = sampled;
            state.lastMovementRelationSampleCount = sampleCount;
            if (firstAttempt) {
                state.firstMovementRelationWord = relationWord;
                state.firstMovementRelationSampled = sampled;
                state.firstMovementRelationSampleCount = sampleCount;
            }
        };
        const auto recordAggregates = [&](int16_t moverWord, int16_t targetWord) {
            state.lastMovementMoverAggregateWord16 = moverWord;
            state.lastMovementTargetAggregateWord16 = targetWord;
            if (firstAttempt) {
                state.firstMovementMoverAggregateWord16 = moverWord;
                state.firstMovementTargetAggregateWord16 = targetWord;
            }
        };
        const auto record50eeIndependence = [&](bool independent) {
            state.lastMovementSelection50eeIndependent = independent;
            if (firstAttempt) {
                state.firstMovementSelection50eeIndependent = independent;
            }
        };

        if (repeatedPolicy &&
            (loadControlWord(state, 0x4cu) != 0 ||
             state.controlRecordBytes[0x50u] != 0u ||
             loadControlWord(state, 0x51u) != 0 ||
             loadControlWord(state, 0x53u) != 0)) {
            recordResult(
                BattleOriginalAiFirstMovementUpdateResult::
                    RejectedPostStepStateOpen);
            continue;
        }

        if (combatantMovementBlocked(*mover)) {
            recordResult(
                BattleOriginalAiFirstMovementUpdateResult::
                    RejectedMovementBlocked);
            continue;
        }
        if (symmetricPolicy && objective_.activeObjectProven) {
            recordResult(
                BattleOriginalAiFirstMovementUpdateResult::
                    RejectedMovementTransaction);
            continue;
        }
        if (!multipleTargetPolicy &&
            (activeOpposingCount != 1u || target == nullptr)) {
            recordResult(
                BattleOriginalAiFirstMovementUpdateResult::
                    RejectedTargetCardinality);
            continue;
        }
        const BattleOriginalAiAggregateDiagnostic moverAggregate =
            battleOriginalAiAggregate7070FromAuthoritativeState(
                movingSlot,
                state.rawX,
                state.rawZ,
                0,
                mover->mechRuntime.detailedDamage,
                mover->weapons);
        if (!moverAggregate.exact ||
            !moverAggregate.authoritativeStateBindingExact) {
            recordResult(
                BattleOriginalAiFirstMovementUpdateResult::RejectedAggregate);
            continue;
        }

        std::vector<BattleOriginalAiMovementCandidateDiagnosticInput>
            candidates(8u);
        std::array<BattleOriginalAiSimulationVisibilitySampledDiagnostic, 8>
            candidateVisibility{};
        std::array<BattleOriginalAiAggregateDiagnostic, 8>
            candidateAggregates{};
        bool candidateBindingOpen = false;
        bool singleCandidateRelationBlocked = false;
        const uint8_t candidateSlotBegin = moverPlayerSide ? 4u : 0u;
        const uint8_t candidateSlotEnd = moverPlayerSide ? 8u : 4u;
        const BattleTeam expectedCandidateTeam = moverPlayerSide
            ? BattleTeam::Opposing
            : BattleTeam::Player;
        for (uint8_t candidateSlot = candidateSlotBegin;
             candidateSlot < candidateSlotEnd;
             ++candidateSlot) {
            Combatant* candidate = liveCombatants[candidateSlot];
            if (candidate == nullptr ||
                candidate->roster.team != expectedCandidateTeam ||
                (!multipleTargetPolicy && candidate != target)) {
                continue;
            }

            int32_t candidateRawX = 0;
            int32_t candidateRawY = 0;
            int32_t candidateRawZ = 0;
            if (!originalRawPosition(
                    *candidate,
                    candidateRawX,
                    candidateRawY,
                    candidateRawZ)) {
                candidateBindingOpen = true;
                break;
            }
            const bool openPlayerRelation =
                symmetricPolicy && !moverPlayerSide &&
                candidateSlot == 0u;
            bool relationValue = false;
            if (!openPlayerRelation) {
                auto& visibility = candidateVisibility[candidateSlot];
                visibility =
                    battleOriginalAiSimulationVisibility5206Sampled(
                        params_.terrainCollisionGrid,
                        state.rawX,
                        state.rawZ,
                        state.rawY,
                        candidateRawX,
                        candidateRawZ,
                        candidateRawY);
                BattleOriginalAiRelationCellDiagnosticInput relationInput;
                relationInput.firstSlotOccupied = true;
                relationInput.secondSlotOccupied = true;
                relationInput.firstSlot = moverPlayerSide
                    ? movingSlot
                    : candidateSlot;
                relationInput.secondSlot = moverPlayerSide
                    ? candidateSlot
                    : movingSlot;
                relationInput.firstSideWord = 0;
                relationInput.secondSideWord = 1;
                relationInput.simulationVisibility5206 =
                    visibility.exact && visibility.relationWord != 0u;
                const BattleOriginalAiRelationCellDiagnostic relation =
                    battleOriginalAiRelationCell(relationInput);
                if (!visibility.exact || !relation.exact ||
                    !relation.usesSimulationVisibility5206) {
                    candidateBindingOpen = true;
                    break;
                }
                relationValue = relation.relationValue;
                if (!multipleTargetPolicy && !relationValue) {
                    recordTarget(candidate->id, candidateSlot);
                    recordRelation(
                        visibility.relationWord,
                        visibility.sampledTerrainPathUsed,
                        visibility.sampledPointCount);
                    singleCandidateRelationBlocked = true;
                    break;
                }
            }

            auto& aggregate = candidateAggregates[candidateSlot];
            aggregate = battleOriginalAiAggregate7070FromAuthoritativeState(
                candidateSlot,
                candidateRawX,
                candidateRawZ,
                0,
                candidate->mechRuntime.detailedDamage,
                candidate->weapons);
            const BattleOriginalAiApproximatePlanarDistanceDiagnostic distance =
                battleOriginalAiApproximatePlanarDistanceB961(
                    static_cast<int32_t>(
                        static_cast<int64_t>(candidateRawX) - state.rawX),
                    static_cast<int32_t>(
                        static_cast<int64_t>(candidateRawZ) - state.rawZ));
            if (!aggregate.exact ||
                !aggregate.authoritativeStateBindingExact ||
                !distance.exact) {
                candidateBindingOpen = true;
                break;
            }

            auto& candidateInput = candidates[candidateSlot];
            candidateInput.entityId = candidate->id;
            candidateInput.liveSlotOccupied = true;
            candidateInput.sideWord = moverPlayerSide ? 1 : 0;
            candidateInput.cachedPoseStateWord2c =
                relationValue ? 2 : 0;
            candidateInput.aggregateWord16 = aggregate.finalAggregateWord16;
            candidateInput.priorSelectionCountWord1c =
                selectionCounts[candidateSlot];
            candidateInput.distanceMatrixValueExact = true;
            candidateInput.approximateDistance = distance.distance;
            candidateInput.cachedRawX = candidateRawX;
            candidateInput.cachedRawY = candidateRawY;
            candidateInput.cachedRawZ = candidateRawZ;
        }
        if (candidateBindingOpen) {
            recordResult(
                BattleOriginalAiFirstMovementUpdateResult::
                    RejectedRelationOpen);
            continue;
        }
        if (singleCandidateRelationBlocked) {
            recordResult(
                BattleOriginalAiFirstMovementUpdateResult::
                    RejectedRelationBlocked);
            continue;
        }

        BattleOriginalAiMovementTargetSelectionDiagnostic selection;
        if (symmetricPolicy && !moverPlayerSide) {
            const BattleOriginalAiOpenRelationSelectionDiagnostic
                openSelection =
                    battleOriginalAiMovementTargetSelection23bfWithOpenRelation(
                        state.sideWord,
                        moverAggregate.finalAggregateWord16,
                        candidates,
                        0u,
                        2);
            if (!openSelection.exact ||
                !openSelection.targetSelectionIndependent) {
                recordResult(
                    BattleOriginalAiFirstMovementUpdateResult::
                        RejectedRelationOpen);
                continue;
            }
            selection = openSelection.relationZero;
            record50eeIndependence(true);
        } else {
            selection = battleOriginalAiMovementTargetSelection23bf(
                state.sideWord,
                moverAggregate.finalAggregateWord16,
                candidates);
        }
        if (!selection.exact || !selection.selected ||
            selection.selectedSlot >= liveCombatants.size() ||
            liveCombatants[selection.selectedSlot] == nullptr ||
            selection.selectedEntityId !=
                liveCombatants[selection.selectedSlot]->id) {
            recordResult(
                BattleOriginalAiFirstMovementUpdateResult::
                    RejectedTargetSelection);
            continue;
        }
        const uint8_t targetSlot = selection.selectedSlot;
        const auto& visibility = candidateVisibility[targetSlot];
        const auto& targetAggregate = candidateAggregates[targetSlot];
        recordTarget(selection.selectedEntityId, targetSlot);
        recordRelation(
            visibility.relationWord,
            visibility.sampledTerrainPathUsed,
            visibility.sampledPointCount);
        recordAggregates(
            moverAggregate.finalAggregateWord16,
            targetAggregate.finalAggregateWord16);
        selectionCounts[targetSlot] = selection.selectedCountAfter;

        BattleOriginalAiSelectedTargetMovementInput movementInput;
        movementInput.movingSlot = movingSlot;
        movementInput.movingOwnerWord = state.sideWord;
        movementInput.controlWord00 = loadControlWord(state, 0x00u);
        movementInput.controlState46 = loadControlWord(state, 0x46u);
        movementInput.numericState51 = loadControlWord(state, 0x51u);
        movementInput.rawX = state.rawX;
        movementInput.rawY = state.rawY;
        movementInput.rawZ = state.rawZ;
        movementInput.pitch = state.pitch;
        movementInput.roll = state.roll;
        movementInput.heading = state.heading;
        movementInput.controlWord30 = state.controlWord30;
        movementInput.controlWord32 = state.controlWord32;
        movementInput.driftWord36 = state.driftWord36;
        movementInput.driftWord38 = state.driftWord38;
        movementInput.driftWord3a = state.driftWord3a;
        movementInput.rawSpeed = state.rawSpeedWord3c;
        const mech3d::MechCatalogMobility mobility =
            mech3d::catalogMobility(mover->mechPresetId);
        movementInput.definitionWord06 = static_cast<int16_t>(
            mobility.originalBtechDefinitionSpeedWord06);
        movementInput.definitionHeightWord02 = static_cast<int16_t>(
            mobility.originalBtechDefinitionHeightWord02);
        const int heatAboveFirstThreshold =
            std::max(0, mover->heat.rawHeat - 400);
        movementInput.heatSpeedPenalty = static_cast<int16_t>(
            ((heatAboveFirstThreshold + 399) / 400) * 6);
        const auto& criticals =
            mover->mechRuntime.detailedDamage.criticalComponents;
        movementInput.leftLegActuatorWord = criticals[static_cast<size_t>(
            mech3d::MechCriticalComponentId::LeftLegActuator)].workingCount;
        movementInput.rightLegActuatorWord = criticals[static_cast<size_t>(
            mech3d::MechCriticalComponentId::RightLegActuator)].workingCount;
        movementInput.selectorWord3a = loadControlWord(state, 0x3au);
        movementInput.selectorWord3c = loadControlWord(state, 0x3cu);
        movementInput.previousSlotProbeWord = state.slotProbeWord;
        movementInput.controlState4c = loadControlWord(state, 0x4cu);
        movementInput.controlState53 = loadControlWord(state, 0x53u);
        movementInput.selectedTargetEntityId = selection.selectedEntityId;
        movementInput.selectedTargetRawX = selection.selectedCachedRawX;
        movementInput.selectedTargetRawY = selection.selectedCachedRawY;
        movementInput.selectedTargetRawZ = selection.selectedCachedRawZ;
        movementInput.liveSlots.resize(8u);
        bool livePositionOpen = false;
        for (uint8_t liveSlot = 0u; liveSlot < 8u; ++liveSlot) {
            Combatant* live = liveCombatants[liveSlot];
            if (live == nullptr) {
                continue;
            }
            int32_t rawX = 0;
            int32_t rawY = 0;
            int32_t rawZ = 0;
            if (!originalRawPosition(*live, rawX, rawY, rawZ)) {
                livePositionOpen = true;
                break;
            }
            auto& liveInput = movementInput.liveSlots[liveSlot];
            liveInput.entityId = live->id;
            liveInput.occupied = true;
            liveInput.ownerWord = liveSlot < 4u ? 0 : 1;
            liveInput.rawX = rawX;
            liveInput.rawY = rawY;
            liveInput.rawZ = rawZ;
        }
        if (livePositionOpen) {
            recordResult(
                BattleOriginalAiFirstMovementUpdateResult::
                    RejectedMovementTransaction);
            continue;
        }
        if (objective_.activeObjectProven &&
            state.numericSideModeWord > 2) {
            recordResult(
                BattleOriginalAiFirstMovementUpdateResult::
                    RejectedMovementTransaction);
            continue;
        }
        movementInput.objectiveLaneEnabled = false;
        movementInput.objectiveInactiveBitSet = true;
        if (state.sceneCacheValid) {
            movementInput.cachedSceneObjectIndex =
                state.sceneCacheObjectIndex;
            movementInput.cachedSubrecordIndex =
                state.sceneCacheSubrecordIndex;
        }

        const BattleOriginalAiSelectedTargetMovementDiagnostic movement =
            battleOriginalAiSelectedTargetMovement(
                params_.terrainCollisionGrid,
                params_.originalTerrainSceneCatalog,
                movementInput);
        if (!movement.exact) {
            recordResult(
                BattleOriginalAiFirstMovementUpdateResult::
                    RejectedMovementTransaction);
            continue;
        }
        if (!movement.poseTransaction.commit.poseAccepted) {
            recordResult(
                BattleOriginalAiFirstMovementUpdateResult::RejectedPoseCommit);
            continue;
        }
        if (repeatedPolicy &&
            !movement.poseTransaction.setsMovingFlagBit4) {
            recordResult(
                BattleOriginalAiFirstMovementUpdateResult::
                    RejectedPostStepStateOpen);
            continue;
        }

        BattleOriginalAiOrdinaryModeZeroPostStepDiagnostic postStep;
        if (repeatedPolicy) {
            postStep = battleOriginalAiOrdinaryModeZeroPostStep9bfa(
                movement.poseTransaction.controlState4cAfter,
                0x10u,
                loadControlWord(state, 0x51u),
                movement.poseTransaction.controlState53After);
            state.lastMovementPostStepExact = postStep.exact;
            state.lastMovementPostStepActionCode =
                postStep.selectedActionCode;
            if (!postStep.exact) {
                recordResult(
                    BattleOriginalAiFirstMovementUpdateResult::
                        RejectedPostStepStateOpen);
                continue;
            }
        }

        const auto& pose = movement.poseTransaction;
        state.rawX = pose.xAfter;
        state.rawY = pose.yAfter;
        state.rawZ = pose.zAfter;
        state.pitch = pose.pitchAfter;
        state.roll = pose.rollAfter;
        state.heading = pose.headingAfter;
        state.controlWord30 = pose.controlWord30After;
        state.controlWord32 = pose.controlWord32After;
        state.controlWord34 = pose.controlWord34After;
        state.driftWord36 = pose.driftWord36After;
        state.driftWord38 = pose.driftWord38After;
        state.driftWord3a = pose.driftWord3aAfter;
        state.rawSpeedWord3c = pose.rawSpeedAfter;
        state.slotProbeWord = pose.slotProbeWordAfter;
        state.sceneCacheValid = pose.sceneCacheValidAfter;
        state.sceneCacheObjectIndex = pose.sceneCacheObjectIndexAfter;
        state.sceneCacheSubrecordIndex = pose.sceneCacheSubrecordIndexAfter;
        if (movement.steering.exact) {
            storeControlWord(
                state, 0x3au, movement.steering.selectorWord3aAfter);
            storeControlWord(
                state, 0x3cu, movement.steering.selectorWord3cAfter);
        }
        if (repeatedPolicy) {
            storeControlWord(
                state, 0x4cu, postStep.animationModeWord4cAfter);
            state.controlRecordBytes[0x50u] = postStep.commandByte50After;
            storeControlWord(
                state, 0x51u, postStep.sharedStateWord51After);
            storeControlWord(
                state, 0x53u, postStep.frameWord53After);
        }

        const double rawToWorld = params_.terrainCellSize / 512.0;
        const int32_t halfDefinitionHeight =
            static_cast<int32_t>(
                mobility.originalBtechDefinitionHeightWord02) /
            2;
        mover->transform.x =
            (static_cast<double>(state.rawX) + 44160.0) * rawToWorld;
        mover->transform.y =
            static_cast<double>(state.rawY - halfDefinitionHeight) *
            rawToWorld;
        mover->transform.z =
            (24000.0 - static_cast<double>(state.rawZ)) * rawToWorld;
        mover->transform.headingRadians = std::remainder(
            static_cast<double>(state.heading) * kTwoPi / 65536.0 + kPi,
            kTwoPi);
        mover->throttle = 0.0;
        mover->turn = 0.0;
        mover->targetForwardSpeed = 0.0;
        mover->forwardSpeed = 0.0;
        state.worldPositionBindingExact = true;
        if (firstAttempt) {
            state.firstMovementUpdateCommitted = true;
        }
        recordResult(BattleOriginalAiFirstMovementUpdateResult::Committed);
        ++state.acceptedPoseCommitCount;
        state.lastPoseCommitTickIndex = tickIndex_;
    }
}

namespace {

constexpr double kOriginalAiUpdateSeconds = 0.1;
constexpr double kOriginalWeaponRangeScale = 480.0;
constexpr int kOriginalAiHeatGate = 0x5a0;

uint64_t originalAiRandomWord(
    const Combatant& shooter,
    const Combatant& target,
    uint64_t decisionSequence,
    uint64_t drawIndex) {
    uint64_t value = decisionSequence;
    value ^= static_cast<uint64_t>(shooter.id.value) << 16u;
    value ^= static_cast<uint64_t>(target.id.value) << 32u;
    value ^= (drawIndex + 1u) * 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31u);
}

uint8_t originalAiD6(
    const Combatant& shooter,
    const Combatant& target,
    uint64_t decisionSequence,
    uint64_t drawIndex) {
    return static_cast<uint8_t>(
        originalAiRandomWord(shooter, target, decisionSequence, drawIndex) %
            6u +
        1u);
}

BattleOriginalAiAttackAspect originalAiAttackAspect(
    const Combatant& shooter,
    const Combatant& target) {
    constexpr double kPi = 3.14159265358979323846;
    const double targetToShooter = std::atan2(
        shooter.transform.x - target.transform.x,
        shooter.transform.z - target.transform.z);
    double delta = std::remainder(
        targetToShooter - target.transform.headingRadians,
        2.0 * kPi);
    const double magnitude = std::abs(delta);
    constexpr double kFrontBoundary =
        static_cast<double>(0x2aa8) * (2.0 * kPi / 65536.0);
    constexpr double kRearBoundary =
        static_cast<double>(0x6aa5) * (2.0 * kPi / 65536.0);
    if (magnitude < kFrontBoundary) {
        return BattleOriginalAiAttackAspect::Front;
    }
    if (magnitude < kRearBoundary) {
        return delta < 0.0 ? BattleOriginalAiAttackAspect::Right
                           : BattleOriginalAiAttackAspect::Left;
    }
    return BattleOriginalAiAttackAspect::Rear;
}

uint8_t originalAiRangeBucket(
    const BattleWeaponInstanceState& weapon,
    double distance) {
    for (uint8_t index = 0; index < weapon.originalRangeWords.size(); ++index) {
        if (distance <=
            static_cast<double>(weapon.originalRangeWords[index]) *
                kOriginalWeaponRangeScale) {
            return index;
        }
    }
    return 4u;
}

mech3d::MechArmorSectionId originalAiLocation(
    BattleOriginalAiAttackAspect aspect,
    uint8_t rollIndex) {
    constexpr std::array<std::array<uint8_t, 11>, 4> kLocations{{
        {{4, 1, 1, 3, 7, 4, 6, 2, 0, 0, 8}},
        {{5, 1, 1, 3, 7, 5, 6, 2, 0, 0, 8}},
        {{6, 2, 0, 0, 2, 6, 4, 5, 1, 3, 8}},
        {{7, 3, 1, 1, 3, 7, 4, 5, 0, 2, 8}},
    }};
    const size_t row = static_cast<size_t>(aspect);
    const size_t column = std::min<size_t>(rollIndex, 10u);
    return armorLocationFromOriginalIndex(kLocations[row][column]);
}

uint64_t replacementAiRandomWord(
    const Combatant& shooter,
    uint64_t targetIdentity,
    uint64_t decisionSequence,
    uint64_t drawIndex) {
    uint64_t value = decisionSequence;
    value ^= static_cast<uint64_t>(shooter.id.value) << 16u;
    value ^= targetIdentity << 32u;
    value ^= (drawIndex + 1u) * 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31u);
}

uint8_t replacementAiD6(
    const Combatant& shooter,
    uint64_t targetIdentity,
    uint64_t decisionSequence,
    uint64_t drawIndex) {
    return static_cast<uint8_t>(
        replacementAiRandomWord(
            shooter, targetIdentity, decisionSequence, drawIndex) % 6u + 1u);
}

struct ReplacementAiHitResolution {
    int16_t targetNumber = 0;
    uint8_t hitRoll = 0;
    bool hit = false;
    uint8_t locationRollIndex = 0;
    mech3d::MechArmorSectionId armorSection =
        mech3d::MechArmorSectionId::CenterTorso;
};

ReplacementAiHitResolution replacementAiHitResolution(
    const Combatant& shooter,
    const Combatant* target,
    const BattleWeaponInstanceState& weapon,
    double targetDistance,
    uint64_t decisionSequence) {
    static constexpr std::array<int, 5> kRangeModifiers{{6, 2, 4, 6, 12}};
    ReplacementAiHitResolution result;
    const uint64_t targetIdentity = target != nullptr
        ? static_cast<uint64_t>(target->id.value) : 0xffffu;
    const uint8_t rangeBucket = originalAiRangeBucket(
        weapon, targetDistance);
    const int rangeModifier = kRangeModifiers[
        std::min<size_t>(rangeBucket, kRangeModifiers.size() - 1u)];
    const int gunnery = std::clamp<int>(shooter.roster.gunnerySkill, 0, 3);
    int targetNumber = (7 - gunnery) + rangeModifier - 4;
    if (target != nullptr) {
        const double shooterMovement = std::abs(shooter.forwardSpeed) /
            std::max(1.0, shooter.maxForwardSpeed);
        const double targetMovement = std::abs(target->forwardSpeed) /
            std::max(1.0, target->maxForwardSpeed);
        targetNumber += static_cast<int>(std::lround(
            std::clamp(shooterMovement, 0.0, 1.0) * 2.0));
        targetNumber += static_cast<int>(std::lround(
            std::clamp(targetMovement, 0.0, 1.0) * 3.0));
        if (std::abs(target->forwardSpeed) <= 1.0e-6) {
            --targetNumber;
        }
    } else {
        // Mission structures are much larger than a mech silhouette.
        targetNumber -= 2;
    }
    result.targetNumber = static_cast<int16_t>(
        std::clamp(targetNumber, 2, 12));
    result.hitRoll = static_cast<uint8_t>(
        replacementAiD6(
            shooter, targetIdentity, decisionSequence, 0u) +
        replacementAiD6(
            shooter, targetIdentity, decisionSequence, 1u));
    result.hit = result.hitRoll >= result.targetNumber;
    if (result.hit && target != nullptr) {
        const uint8_t firstLocationDie = replacementAiD6(
            shooter, targetIdentity, decisionSequence, 2u);
        const uint8_t secondLocationDie = replacementAiD6(
            shooter, targetIdentity, decisionSequence, 3u);
        result.locationRollIndex = static_cast<uint8_t>(
            firstLocationDie + secondLocationDie - 2u);
        result.armorSection = originalAiLocation(
            originalAiAttackAspect(shooter, *target),
            result.locationRollIndex);
    }
    return result;
}

BattleHitPoint replacementAiMissDirection(
    BattleHitPoint exactDirection,
    const Combatant& shooter,
    uint64_t targetIdentity,
    uint64_t decisionSequence,
    int targetNumber) {
    constexpr double kPi = 3.14159265358979323846;
    const auto signedUnit = [&shooter, targetIdentity, decisionSequence](
                                uint64_t drawIndex) {
        const uint64_t word = replacementAiRandomWord(
            shooter, targetIdentity, decisionSequence, drawIndex);
        return static_cast<double>(word & 0xffffu) / 32767.5 - 1.0;
    };
    const double spread =
        (2.0 + 0.45 * static_cast<double>(std::max(0, targetNumber - 2))) *
        kPi / 180.0;
    const double yaw = std::atan2(exactDirection.x, exactDirection.z) +
        signedUnit(4u) * spread;
    const double pitch = std::asin(std::clamp(
        exactDirection.y, -1.0, 1.0)) + signedUnit(5u) * spread;
    const double horizontal = std::cos(pitch);
    return normalized({
        std::sin(yaw) * horizontal,
        std::sin(pitch),
        std::cos(yaw) * horizontal,
    });
}

bool originalAiStationary(const Combatant& combatant) {
    constexpr double kEpsilon = 1.0e-9;
    return std::abs(combatant.forwardSpeed) <= kEpsilon &&
        std::abs(combatant.targetForwardSpeed) <= kEpsilon &&
        std::abs(combatant.throttle) <= kEpsilon &&
        !combatant.airborne && !combatant.jumpJetsEnabled;
}

constexpr std::array<uint16_t, 512> kOriginalFixedAtanTable{{
    0, 1, 2, 3, 5, 6, 7, 8, 10, 11, 12, 14, 15, 16, 17, 19,
    20, 21, 22, 24, 25, 26, 27, 29, 30, 31, 33, 34, 35, 36, 38, 39,
    40, 41, 43, 44, 45, 47, 48, 49, 50, 52, 53, 54, 55, 57, 58, 59,
    60, 62, 63, 64, 65, 67, 68, 69, 71, 72, 73, 74, 76, 77, 78, 79,
    81, 82, 83, 84, 86, 87, 88, 89, 91, 92, 93, 94, 96, 97, 98, 99,
    101, 102, 103, 104, 106, 107, 108, 109, 110, 112, 113, 114, 115, 117, 118, 119,
    120, 122, 123, 124, 125, 126, 128, 129, 130, 131, 133, 134, 135, 136, 137, 139,
    140, 141, 142, 144, 145, 146, 147, 148, 150, 151, 152, 153, 154, 156, 157, 158,
    159, 160, 162, 163, 164, 165, 166, 168, 169, 170, 171, 172, 174, 175, 176, 177,
    178, 179, 181, 182, 183, 184, 185, 186, 188, 189, 190, 191, 192, 193, 195, 196,
    197, 198, 199, 200, 202, 203, 204, 205, 206, 207, 208, 210, 211, 212, 213, 214,
    215, 216, 218, 219, 220, 221, 222, 223, 224, 226, 227, 228, 229, 230, 231, 232,
    233, 234, 236, 237, 238, 239, 240, 241, 242, 243, 244, 246, 247, 248, 249, 250,
    251, 252, 253, 254, 255, 257, 258, 259, 260, 261, 262, 263, 264, 265, 266, 267,
    268, 269, 270, 272, 273, 274, 275, 276, 277, 278, 279, 280, 281, 282, 283, 284,
    285, 286, 287, 288, 289, 290, 291, 293, 294, 295, 296, 297, 298, 299, 300, 301,
    302, 303, 304, 305, 306, 307, 308, 309, 310, 311, 312, 313, 314, 315, 316, 317,
    318, 319, 320, 321, 322, 323, 324, 325, 326, 327, 328, 329, 330, 331, 332, 333,
    334, 334, 335, 336, 337, 338, 339, 340, 341, 342, 343, 344, 345, 346, 347, 348,
    349, 350, 351, 352, 353, 353, 354, 355, 356, 357, 358, 359, 360, 361, 362, 363,
    364, 365, 365, 366, 367, 368, 369, 370, 371, 372, 373, 374, 375, 375, 376, 377,
    378, 379, 380, 381, 382, 383, 383, 384, 385, 386, 387, 388, 389, 390, 390, 391,
    392, 393, 394, 395, 396, 396, 397, 398, 399, 400, 401, 402, 402, 403, 404, 405,
    406, 407, 407, 408, 409, 410, 411, 412, 412, 413, 414, 415, 416, 417, 417, 418,
    419, 420, 421, 421, 422, 423, 424, 425, 425, 426, 427, 428, 429, 429, 430, 431,
    432, 433, 433, 434, 435, 436, 437, 437, 438, 439, 440, 440, 441, 442, 443, 444,
    444, 445, 446, 447, 447, 448, 449, 450, 450, 451, 452, 453, 453, 454, 455, 456,
    456, 457, 458, 459, 459, 460, 461, 462, 462, 463, 464, 464, 465, 466, 467, 467,
    468, 469, 470, 470, 471, 472, 472, 473, 474, 475, 475, 476, 477, 477, 478, 479,
    479, 480, 481, 482, 482, 483, 484, 484, 485, 486, 486, 487, 488, 488, 489, 490,
    490, 491, 492, 493, 493, 494, 495, 495, 496, 497, 497, 498, 499, 499, 500, 500,
    501, 502, 502, 503, 504, 504, 505, 506, 506, 507, 508, 508, 509, 510, 510, 511,
}};

int16_t originalAiSignedWord(int64_t value) {
    value %= 0x10000;
    if (value > std::numeric_limits<int16_t>::max()) {
        value -= 0x10000;
    } else if (value < std::numeric_limits<int16_t>::min()) {
        value += 0x10000;
    }
    return static_cast<int16_t>(value);
}

int32_t originalAiSignedDword(int64_t value) {
    value %= 0x100000000ll;
    if (value > std::numeric_limits<int32_t>::max()) {
        value -= 0x100000000ll;
    } else if (value < std::numeric_limits<int32_t>::min()) {
        value += 0x100000000ll;
    }
    return static_cast<int32_t>(value);
}

struct OriginalAiFixedMagnitude {
    uint16_t low = 0;
    int16_t high = 0;
};

OriginalAiFixedMagnitude originalAiFixedMagnitude(int32_t value) {
    OriginalAiFixedMagnitude result;
    const uint32_t bits = static_cast<uint32_t>(value);
    result.low = static_cast<uint16_t>(bits & 0xffffu);
    result.high = static_cast<int16_t>(bits >> 16u);
    if (result.low == 0x8000u && result.high == 0) {
        result.low = 0x7fffu;
        return result;
    }
    if (result.high < 0) {
        const bool borrow = result.low != 0u;
        result.low = static_cast<uint16_t>(0u - result.low);
        result.high = originalAiSignedWord(
            -static_cast<int32_t>(result.high) - (borrow ? 1 : 0));
    }
    return result;
}

bool originalAiFixedMagnitudeBelow(int32_t value, uint16_t threshold) {
    const OriginalAiFixedMagnitude magnitude = originalAiFixedMagnitude(value);
    return magnitude.high < 1 &&
        (magnitude.high < 0 || magnitude.low < threshold);
}

bool originalAiFixedMagnitudeAtMost(int32_t value, uint16_t threshold) {
    const OriginalAiFixedMagnitude magnitude = originalAiFixedMagnitude(value);
    return magnitude.high < 1 &&
        (magnitude.high < 0 || magnitude.low <= threshold);
}

int16_t originalAiCosineQ14(uint16_t angle) {
    uint16_t index = static_cast<uint16_t>(angle >> 4u);
    if ((index & 0x0800u) != 0u) {
        index = static_cast<uint16_t>(0x1000u - index);
    }
    if (index == 0u) {
        return 0x4000;
    }
    if (index == 0x0400u) {
        return 0;
    }
    if (index == 0x0800u) {
        return static_cast<int16_t>(-0x4000);
    }
    constexpr double kPi = 3.14159265358979323846;
    const double sourceWord = std::floor(
        std::cos(static_cast<double>(index) * kPi / 2048.0) *
        16384.0);
    return static_cast<int16_t>(sourceWord);
}

int16_t originalAiSineQ14(int16_t angle) {
    return originalAiCosineQ14(static_cast<uint16_t>(
        static_cast<uint16_t>(angle) + 0xc000u));
}

int32_t originalAiArithmeticShiftRight14(int32_t value) {
    constexpr int64_t kScale = 1ll << 14;
    if (value >= 0) {
        return value / static_cast<int32_t>(kScale);
    }
    return static_cast<int32_t>(
        -((-static_cast<int64_t>(value) + kScale - 1) / kScale));
}

int32_t originalAiArithmeticShiftRight5(int32_t value) {
    constexpr int64_t kScale = 1ll << 5;
    if (value >= 0) {
        return value / static_cast<int32_t>(kScale);
    }
    return static_cast<int32_t>(
        -((-static_cast<int64_t>(value) + kScale - 1) / kScale));
}

std::optional<int32_t> originalAiIntegralCoordinate(double value) {
    if (!std::isfinite(value)) {
        return std::nullopt;
    }
    const double rounded = std::nearbyint(value);
    if (std::abs(value - rounded) > 1.0e-9 ||
        rounded < static_cast<double>(std::numeric_limits<int32_t>::min()) ||
        rounded > static_cast<double>(std::numeric_limits<int32_t>::max())) {
        return std::nullopt;
    }
    return static_cast<int32_t>(rounded);
}

std::optional<int16_t> originalAiFixedHeading(double radians) {
    constexpr double kTwoPi = 6.28318530717958647692;
    if (!std::isfinite(radians)) {
        return std::nullopt;
    }
    const double fixed = std::remainder(radians, kTwoPi) *
        (65536.0 / kTwoPi);
    const double rounded = std::nearbyint(fixed);
    if (std::abs(fixed - rounded) > 1.0e-7) {
        return std::nullopt;
    }
    return originalAiSignedWord(static_cast<int64_t>(rounded));
}

int16_t originalAiFixedAngle(int32_t first, int32_t second) {
    const bool firstNegative = first < 0;
    const bool secondNegative = second < 0;
    const uint64_t firstMagnitude = firstNegative
        ? static_cast<uint64_t>(-static_cast<int64_t>(first))
        : static_cast<uint64_t>(first);
    const uint64_t secondMagnitude = secondNegative
        ? static_cast<uint64_t>(-static_cast<int64_t>(second))
        : static_cast<uint64_t>(second);

    int32_t quadrantAngle = 0;
    if (firstMagnitude == 0u) {
        quadrantAngle = 0x400;
    } else if (secondMagnitude == 0u) {
        quadrantAngle = 0;
    } else if (firstMagnitude == secondMagnitude) {
        quadrantAngle = 0x200;
    } else if (secondMagnitude < firstMagnitude) {
        const size_t ratio = static_cast<size_t>(
            secondMagnitude * 512u / firstMagnitude);
        quadrantAngle = kOriginalFixedAtanTable[ratio];
    } else {
        const size_t ratio = static_cast<size_t>(
            firstMagnitude * 512u / secondMagnitude);
        quadrantAngle = 0x400 - kOriginalFixedAtanTable[ratio];
    }

    if (firstNegative) {
        quadrantAngle = 0x800 - quadrantAngle;
    }
    if (secondNegative) {
        quadrantAngle = 0x1000 - quadrantAngle;
    }
    return originalAiSignedWord(
        static_cast<int64_t>(quadrantAngle - 0x400) * 16);
}

std::optional<uint32_t> originalAiApproximatePlanarDistance(
    int32_t deltaX,
    int32_t deltaZ) {
    const OriginalAiFixedMagnitude xWords =
        originalAiFixedMagnitude(deltaX);
    const OriginalAiFixedMagnitude zWords =
        originalAiFixedMagnitude(deltaZ);
    if (xWords.high < 0 || zWords.high < 0) {
        return std::nullopt;
    }
    const uint64_t absoluteX =
        (static_cast<uint64_t>(static_cast<uint16_t>(xWords.high)) << 16u) |
        xWords.low;
    const uint64_t absoluteZ =
        (static_cast<uint64_t>(static_cast<uint16_t>(zWords.high)) << 16u) |
        zWords.low;
    const uint64_t maximum = std::max(absoluteX, absoluteZ);
    const uint64_t minimum = std::min(absoluteX, absoluteZ);
    const uint64_t distance = maximum + ((minimum * 3u) >> 3u);
    if (distance > std::numeric_limits<uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<uint32_t>(distance);
}

BattleOriginalAiCandidateGeometryDiagnostic
originalAiZeroPitchCandidateGeometryImpl(
    const Transform& shooter,
    const Transform& candidate) {
    BattleOriginalAiCandidateGeometryDiagnostic result;
    const auto shooterX = originalAiIntegralCoordinate(shooter.x);
    const auto shooterY = originalAiIntegralCoordinate(shooter.y);
    const auto shooterZ = originalAiIntegralCoordinate(shooter.z);
    const auto candidateX = originalAiIntegralCoordinate(candidate.x);
    const auto candidateY = originalAiIntegralCoordinate(candidate.y);
    const auto candidateZ = originalAiIntegralCoordinate(candidate.z);
    const auto shooterHeading =
        originalAiFixedHeading(shooter.headingRadians);
    if (!shooterX || !shooterY || !shooterZ || !candidateX ||
        !candidateY || !candidateZ || !shooterHeading) {
        return result;
    }

    const int64_t deltaX64 =
        static_cast<int64_t>(*candidateX) - *shooterX;
    const int64_t deltaZ64 =
        static_cast<int64_t>(*candidateZ) - *shooterZ;
    const int64_t deltaY64 =
        static_cast<int64_t>(*candidateY) - *shooterY;
    if (deltaX64 < std::numeric_limits<int32_t>::min() ||
        deltaX64 > std::numeric_limits<int32_t>::max() ||
        deltaZ64 < std::numeric_limits<int32_t>::min() ||
        deltaZ64 > std::numeric_limits<int32_t>::max() ||
        deltaY64 < std::numeric_limits<int32_t>::min() ||
        deltaY64 > std::numeric_limits<int32_t>::max()) {
        return result;
    }
    const int32_t deltaX = static_cast<int32_t>(deltaX64);
    const int32_t deltaZ = static_cast<int32_t>(deltaZ64);
    const int32_t deltaY = static_cast<int32_t>(deltaY64);
    const std::optional<uint32_t> distance =
        originalAiApproximatePlanarDistance(deltaX, deltaZ);
    if (!distance ||
        *distance > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        return result;
    }

    result.exact = true;
    result.distance = *distance;
    result.shooterHeading = *shooterHeading;
    result.candidateHeading = originalAiFixedAngle(deltaX, deltaZ);
    result.yawDelta = originalAiSignedWord(
        static_cast<int64_t>(result.candidateHeading) -
        result.shooterHeading);
    result.planarHalfWidth = originalAiFixedAngle(
        -180, static_cast<int32_t>(result.distance));
    const int32_t absoluteYaw = result.yawDelta ==
            std::numeric_limits<int16_t>::min()
        ? 0x8000
        : std::abs(static_cast<int32_t>(result.yawDelta));
    result.planarEnvelopePassed =
        absoluteYaw <= result.planarHalfWidth;
    result.verticalAngle = originalAiFixedAngle(
        deltaY, static_cast<int32_t>(result.distance));
    result.verticalDelta = result.verticalAngle;
    const int32_t absoluteVertical = result.verticalDelta ==
            std::numeric_limits<int16_t>::min()
        ? 0x7fff
        : std::abs(static_cast<int32_t>(result.verticalDelta));
    result.verticalEnvelopePassed =
        absoluteVertical < result.verticalLimit;
    result.insideEnvelope =
        result.verticalEnvelopePassed && result.planarEnvelopePassed;
    result.centerline = result.yawDelta == 0;
    return result;
}

} // namespace

BattleOriginalAiApproximatePlanarDistanceDiagnostic
battleOriginalAiApproximatePlanarDistanceB961(
    int32_t deltaX,
    int32_t deltaZ) {
    BattleOriginalAiApproximatePlanarDistanceDiagnostic result;
    result.deltaX = deltaX;
    result.deltaZ = deltaZ;
    const OriginalAiFixedMagnitude xWords =
        originalAiFixedMagnitude(deltaX);
    const OriginalAiFixedMagnitude zWords =
        originalAiFixedMagnitude(deltaZ);
    if (xWords.high < 0 || zWords.high < 0) {
        return result;
    }
    result.xMagnitude =
        (static_cast<uint32_t>(static_cast<uint16_t>(xWords.high)) << 16u) |
        xWords.low;
    result.zMagnitude =
        (static_cast<uint32_t>(static_cast<uint16_t>(zWords.high)) << 16u) |
        zWords.low;
    result.maximumMagnitude =
        std::max(result.xMagnitude, result.zMagnitude);
    result.minimumMagnitude =
        std::min(result.xMagnitude, result.zMagnitude);
    const uint64_t distance =
        static_cast<uint64_t>(result.maximumMagnitude) +
        ((static_cast<uint64_t>(result.minimumMagnitude) * 3u) >> 3u);
    if (distance > std::numeric_limits<uint32_t>::max()) {
        return result;
    }
    result.exact = true;
    result.distance = static_cast<uint32_t>(distance);
    return result;
}

BattleOriginalAiCandidateGeometryDiagnostic
battleOriginalAiZeroPitchCandidateGeometry(
    const Transform& shooter,
    const Transform& candidate) {
    return originalAiZeroPitchCandidateGeometryImpl(shooter, candidate);
}

BattleOriginalAiComponentHeadingDiagnostic
battleOriginalAiComponentHeadingCommand(
    int16_t bodyHeading,
    int16_t componentRelativeHeading,
    int16_t desiredHeading) {
    BattleOriginalAiComponentHeadingDiagnostic result;
    result.exact = true;
    result.bodyHeading = bodyHeading;
    result.componentRelativeHeadingBefore = componentRelativeHeading;
    result.desiredHeading = desiredHeading;

    const int16_t error = originalAiSignedWord(
        static_cast<int64_t>(bodyHeading) + componentRelativeHeading -
        desiredHeading);
    result.signedCombinedError = error;

    int magnitude = error == std::numeric_limits<int16_t>::min()
        ? std::numeric_limits<int16_t>::max()
        : std::abs(static_cast<int>(error));
    magnitude = std::min<int>(magnitude, result.maximumCorrection);
    const int correction = error < 0 ? magnitude : (error > 0 ? -magnitude : 0);
    result.limitedCorrection = static_cast<int16_t>(correction);
    result.componentRelativeHeadingCandidate = originalAiSignedWord(
        static_cast<int64_t>(componentRelativeHeading) + correction);

    const int candidateMagnitude =
        result.componentRelativeHeadingCandidate ==
                std::numeric_limits<int16_t>::min()
            ? std::numeric_limits<int16_t>::max()
            : std::abs(static_cast<int>(
                  result.componentRelativeHeadingCandidate));
    result.strictComponentLimitPassed =
        candidateMagnitude < result.strictComponentLimit;
    result.componentRelativeHeadingAfter = result.strictComponentLimitPassed
        ? result.componentRelativeHeadingCandidate
        : componentRelativeHeading;
    return result;
}

BattleOriginalAiBodyTurnCommandDiagnostic
battleOriginalAiBodyTurnCommand(
    int16_t desiredHeading,
    int16_t currentHeading) {
    BattleOriginalAiBodyTurnCommandDiagnostic result;
    result.exact = true;
    result.desiredHeading = desiredHeading;
    result.currentHeading = currentHeading;
    result.wrappedDelta = originalAiSignedWord(
        static_cast<int64_t>(desiredHeading) - currentHeading);
    if (result.wrappedDelta == std::numeric_limits<int16_t>::min()) {
        result.wrappedDelta = static_cast<int16_t>(-0x7fff);
    }
    const int magnitude = std::min<int>(
        std::abs(static_cast<int>(result.wrappedDelta)),
        result.maximumTurnCommand);
    result.turnCommand = static_cast<int16_t>(
        result.wrappedDelta < 0 ? -magnitude : magnitude);
    return result;
}

BattleOriginalAiRawSpeedCommandDiagnostic
battleOriginalAiRawSpeedCommand(
    int16_t definitionWord06,
    int16_t heatSpeedPenalty,
    int16_t leftLegActuatorWord,
    int16_t rightLegActuatorWord,
    int16_t movementMode,
    int16_t currentRawSpeed) {
    BattleOriginalAiRawSpeedCommandDiagnostic result;
    result.exact = true;
    result.definitionWord06 = definitionWord06;
    result.heatSpeedPenalty = heatSpeedPenalty;
    result.leftLegActuatorWord = leftLegActuatorWord;
    result.rightLegActuatorWord = rightLegActuatorWord;
    result.limitingLegActuatorWord = std::min(
        leftLegActuatorWord, rightLegActuatorWord);
    const int16_t reducedDefinitionWord = originalAiSignedWord(
        static_cast<int64_t>(definitionWord06) - heatSpeedPenalty);
    result.wrappedProduct = originalAiSignedWord(
        static_cast<int64_t>(reducedDefinitionWord) *
        result.limitingLegActuatorWord);
    result.forwardDesiredRawSpeed = static_cast<int16_t>(
        result.wrappedProduct / 4);
    result.movementMode = movementMode;
    result.desiredRawSpeed = result.forwardDesiredRawSpeed;
    if (movementMode == 1) {
        const int arithmeticHalf = result.forwardDesiredRawSpeed >= 0
            ? result.forwardDesiredRawSpeed / 2
            : -((std::abs(static_cast<int>(
                    result.forwardDesiredRawSpeed)) + 1) / 2);
        result.desiredRawSpeed = originalAiSignedWord(-arithmeticHalf);
    } else if (movementMode == 2) {
        result.desiredRawSpeed = 0;
    }

    result.currentRawSpeed = currentRawSpeed;
    result.requestedDelta = originalAiSignedWord(
        static_cast<int64_t>(result.desiredRawSpeed) - currentRawSpeed);
    result.appliedDelta = result.requestedDelta;
    if (result.appliedDelta > 5) {
        result.appliedDelta = 5;
        result.positiveAccelerationCapped = true;
    }
    result.nextRawSpeed = originalAiSignedWord(
        static_cast<int64_t>(currentRawSpeed) + result.appliedDelta);
    return result;
}

BattleOriginalAiPlanarPoseStepDiagnostic
battleOriginalAiPlanarPoseStep(
    int32_t x,
    int32_t z,
    int16_t heading,
    int16_t rawSpeed,
    int16_t driftX,
    int16_t driftZ) {
    BattleOriginalAiPlanarPoseStepDiagnostic result;
    result.exact = true;
    result.xBefore = x;
    result.zBefore = z;
    result.heading = heading;
    result.rawSpeed = rawSpeed;
    result.sineQ14 = originalAiSineQ14(heading);
    result.cosineQ14 = originalAiCosineQ14(
        static_cast<uint16_t>(heading));
    result.speedDeltaX = originalAiArithmeticShiftRight14(
        static_cast<int32_t>(rawSpeed) *
        -static_cast<int32_t>(result.sineQ14));
    result.speedDeltaZ = originalAiArithmeticShiftRight14(
        static_cast<int32_t>(rawSpeed) *
        static_cast<int32_t>(result.cosineQ14));
    result.driftX = driftX;
    result.driftZ = driftZ;
    result.xAfter = originalAiSignedDword(
        static_cast<int64_t>(x) + result.speedDeltaX + driftX);
    result.zAfter = originalAiSignedDword(
        static_cast<int64_t>(z) + result.speedDeltaZ + driftZ);
    return result;
}

BattleOriginalAiPoseStepDiagnostic battleOriginalAiPoseStep(
    int32_t x,
    int32_t y,
    int32_t z,
    int16_t pitch,
    int16_t roll,
    int16_t heading,
    int16_t pitchCorrection,
    int16_t rollCorrection,
    int16_t turnCommand,
    int16_t rawSpeed,
    int16_t driftX,
    int16_t driftZ,
    int16_t driftY) {
    BattleOriginalAiPoseStepDiagnostic result;
    result.exact = true;
    result.xBefore = x;
    result.yBefore = y;
    result.zBefore = z;
    result.pitchBefore = pitch;
    result.rollBefore = roll;
    result.headingBefore = heading;
    result.pitchCorrection = pitchCorrection;
    result.rollCorrection = rollCorrection;
    result.turnCommand = turnCommand;
    result.pitchAfter = originalAiSignedWord(
        static_cast<int64_t>(pitch) + pitchCorrection);
    result.rollAfter = originalAiSignedWord(
        static_cast<int64_t>(roll) + rollCorrection);
    result.headingAfter = originalAiSignedWord(
        static_cast<int64_t>(heading) + turnCommand);
    result.rawSpeed = rawSpeed;
    result.sineHeadingQ14 = originalAiSineQ14(result.headingAfter);
    result.cosineHeadingQ14 = originalAiCosineQ14(
        static_cast<uint16_t>(result.headingAfter));
    result.sinePitchQ14 = originalAiSineQ14(result.pitchAfter);
    result.cosinePitchQ14 = originalAiCosineQ14(
        static_cast<uint16_t>(result.pitchAfter));

    const auto multiplyQ14 = [](int32_t left, int32_t right) {
        const int32_t wrappedProduct = originalAiSignedDword(
            static_cast<int64_t>(left) * static_cast<int64_t>(right));
        return originalAiArithmeticShiftRight14(wrappedProduct);
    };
    if (result.pitchAfter == 0 && result.rollAfter == 0) {
        result.speedDeltaX = multiplyQ14(
            rawSpeed, -static_cast<int32_t>(result.sineHeadingQ14));
        result.speedDeltaZ = result.headingAfter == 0
            ? rawSpeed
            : multiplyQ14(rawSpeed, result.cosineHeadingQ14);
    } else {
        const int32_t negativePitchProjection = multiplyQ14(
            rawSpeed, -static_cast<int32_t>(result.cosinePitchQ14));
        result.speedDeltaX = multiplyQ14(
            result.sineHeadingQ14, negativePitchProjection);
        const int32_t pitchProjection = multiplyQ14(
            rawSpeed, result.cosinePitchQ14);
        result.speedDeltaZ = multiplyQ14(
            result.cosineHeadingQ14, pitchProjection);
        result.speedDeltaY = multiplyQ14(
            rawSpeed, result.sinePitchQ14);
    }
    result.driftX = driftX;
    result.driftZ = driftZ;
    result.driftY = driftY;
    result.xAfter = originalAiSignedDword(
        static_cast<int64_t>(x) + result.speedDeltaX + driftX);
    result.zAfter = originalAiSignedDword(
        static_cast<int64_t>(z) + result.speedDeltaZ + driftZ);
    result.yAfter = originalAiSignedDword(
        static_cast<int64_t>(y) + result.speedDeltaY + driftY);
    return result;
}

BattleOriginalAiOrdinaryPoseCommitDiagnostic
battleOriginalAiOrdinaryPoseCommit(
    int16_t rawSpeed,
    uint16_t previousSlotProbeWord,
    uint16_t nextSlotProbeWord,
    uint8_t sceneProbeResult,
    bool xLowWordOdd) {
    BattleOriginalAiOrdinaryPoseCommitDiagnostic result;
    result.exact = true;
    result.rawSpeed = rawSpeed;
    result.previousSlotProbeWord = previousSlotProbeWord;
    result.nextSlotProbeWord = nextSlotProbeWord;
    result.sceneProbeResult = sceneProbeResult;
    result.movingBranch = rawSpeed != 0;
    if (!result.movingBranch) {
        result.poseAccepted = true;
        return result;
    }

    result.poseAccepted =
        (nextSlotProbeWord == 0u || previousSlotProbeWord != 0u) &&
        sceneProbeResult != 2u;
    result.poseRestored = !result.poseAccepted;
    if (result.poseAccepted) {
        result.writesAssociatedRecordProbeFlag = true;
        result.associatedRecordProbeFlagValue = sceneProbeResult == 1u;
        return result;
    }

    result.clearsControlWords = true;
    result.clearsMotionWords30Through3c = true;
    if (rawSpeed < 6) {
        result.rollbackHeadingDelta = static_cast<int16_t>(
            (xLowWordOdd ? 4 : -4) * 0x00b6);
    }
    return result;
}

BattleOriginalAiOrdinaryPoseTransactionDiagnostic
battleOriginalAiOrdinaryPoseTransaction(
    const BattleOriginalTerrainSceneCatalog& catalog,
    int32_t x,
    int32_t y,
    int32_t z,
    int16_t pitch,
    int16_t roll,
    int16_t heading,
    int16_t controlWord30,
    int16_t controlWord32,
    int16_t controlWord34,
    int16_t driftWord36,
    int16_t driftWord38,
    int16_t driftWord3a,
    int16_t rawSpeed,
    int16_t definitionHeightWord02,
    uint16_t previousSlotProbeWord,
    uint16_t nextSlotProbeWord,
    int16_t controlState4c,
    int16_t controlState53,
    std::optional<size_t> cachedSceneObjectIndex,
    std::optional<uint8_t> cachedSubrecordIndex) {
    BattleOriginalAiOrdinaryPoseTransactionDiagnostic result;
    result.tentativeStep = battleOriginalAiPoseStep(
        x,
        y,
        z,
        0,
        0,
        heading,
        controlWord30,
        controlWord32,
        controlWord34,
        rawSpeed,
        driftWord36,
        driftWord38,
        driftWord3a);
    const auto highByte = [](int16_t value) {
        return static_cast<uint8_t>(
            static_cast<uint16_t>(value) >> 8u);
    };
    result.sceneQuery = battleOriginalTerrainSceneQuery(
        catalog,
        result.tentativeStep.xAfter,
        result.tentativeStep.zAfter,
        highByte(result.tentativeStep.pitchAfter),
        highByte(result.tentativeStep.rollAfter),
        highByte(result.tentativeStep.headingAfter),
        cachedSceneObjectIndex,
        cachedSubrecordIndex);
    if (!result.tentativeStep.exact || !result.sceneQuery.exact ||
        !result.sceneQuery.catalogAvailable) {
        return result;
    }

    const auto correctionWord = [](uint8_t desired, uint8_t live) {
        const int desiredSigned = desired <= 0x7fu
            ? static_cast<int>(desired)
            : static_cast<int>(desired) - 0x100;
        const int liveSigned = live <= 0x7fu
            ? static_cast<int>(live)
            : static_cast<int>(live) - 0x100;
        const int delta = desiredSigned - liveSigned;
        const int step = delta < 0 ? -1 : (delta > 0 ? 1 : 0);
        return static_cast<int16_t>(step * 0x0100);
    };
    if (!result.sceneQuery.hit || result.sceneQuery.selectedPriority != 0u) {
        result.sceneQuery.contactCorrection.desiredByte0 = 0u;
        result.sceneQuery.contactCorrection.desiredByte1 = 0u;
        result.sceneQuery.contactCorrection.controlWord30 = correctionWord(
            0u, highByte(result.tentativeStep.pitchAfter));
        result.sceneQuery.contactCorrection.controlWord32 = correctionWord(
            0u, highByte(result.tentativeStep.rollAfter));
    }

    const uint8_t sceneResult = result.sceneQuery.hit
        ? result.sceneQuery.selectedResultByte
        : 0u;
    result.commit = battleOriginalAiOrdinaryPoseCommit(
        rawSpeed,
        previousSlotProbeWord,
        nextSlotProbeWord,
        sceneResult,
        (static_cast<uint16_t>(x) & 1u) != 0u);
    result.exact = result.commit.exact;
    result.setsMovingFlagBit4 = rawSpeed != 0;
    result.sceneCacheValidAfter = result.sceneQuery.hit;
    if (result.sceneCacheValidAfter) {
        result.sceneCacheObjectIndexAfter =
            result.sceneQuery.selectedSceneObjectIndex;
        result.sceneCacheSubrecordIndexAfter =
            result.sceneQuery.selectedSubrecordIndex;
    }

    const int32_t c42eHeight =
        result.sceneQuery.hit && result.sceneQuery.selectedPriority == 0u
            ? result.sceneQuery.worldHeight
            : 0;
    const int32_t halfDefinitionHeight =
        definitionHeightWord02 >= 0
            ? static_cast<int32_t>(definitionHeightWord02) / 2
            : -((-
                    static_cast<int32_t>(definitionHeightWord02) + 1) /
                2);

    result.controlWord30After =
        result.sceneQuery.contactCorrection.controlWord30;
    result.controlWord32After =
        result.sceneQuery.contactCorrection.controlWord32;
    result.controlWord34After = controlWord34;
    result.driftWord36After = driftWord36;
    result.driftWord38After = driftWord38;
    result.driftWord3aAfter = driftWord3a;
    result.rawSpeedAfter = rawSpeed;
    result.controlState4cAfter = controlState4c;
    result.controlState53After = controlState53;

    if (result.commit.poseAccepted) {
        result.xAfter = result.tentativeStep.xAfter;
        result.yAfter = originalAiSignedDword(
            static_cast<int64_t>(c42eHeight) + halfDefinitionHeight);
        result.zAfter = result.tentativeStep.zAfter;
        result.pitchAfter = result.tentativeStep.pitchAfter;
        result.rollAfter = result.tentativeStep.rollAfter;
        result.headingAfter = result.tentativeStep.headingAfter;
        result.slotProbeWordAfter = rawSpeed == 0
            ? previousSlotProbeWord
            : nextSlotProbeWord;
        result.associatedRecordProbeFlagWritten =
            result.commit.writesAssociatedRecordProbeFlag;
        result.associatedRecordProbeFlagValue =
            result.commit.associatedRecordProbeFlagValue;
        return result;
    }

    result.xAfter = x;
    result.yAfter = y;
    result.zAfter = z;
    result.pitchAfter = pitch;
    result.rollAfter = roll;
    result.headingAfter = originalAiSignedWord(
        static_cast<int64_t>(heading) + result.commit.rollbackHeadingDelta);
    result.controlWord30After = 0;
    result.controlWord32After = 0;
    result.controlWord34After = 0;
    result.driftWord36After = 0;
    result.driftWord38After = 0;
    result.driftWord3aAfter = 0;
    result.rawSpeedAfter = 0;
    result.controlState4cAfter = 0;
    result.controlState53After = 0;
    result.slotProbeWordAfter = previousSlotProbeWord;
    return result;
}

BattleOriginalAiOrdinaryModeZeroPostStepDiagnostic
battleOriginalAiOrdinaryModeZeroPostStep9bfa(
    int16_t animationModeWord4c,
    uint8_t commandByte50,
    int16_t sharedStateWord51,
    int16_t frameWord53) {
    BattleOriginalAiOrdinaryModeZeroPostStepDiagnostic result;
    result.animationModeWord4cBefore = animationModeWord4c;
    result.commandByte50Before = commandByte50;
    result.sharedStateWord51Before = sharedStateWord51;
    result.frameWord53Before = frameWord53;

    if (animationModeWord4c != 0 || commandByte50 != 0x10u ||
        sharedStateWord51 != 0 || frameWord53 != 0) {
        return result;
    }

    result.exact = true;
    result.selectedActionCode = 3u;
    result.modeZeroFrameCount = 1u;
    result.frameWord53AfterIncrement = 1;
    result.animationModeWord4cAfter = 0;
    result.commandByte50After = 0u;
    result.sharedStateWord51After = 0;
    result.frameWord53After = 0;
    return result;
}

BattleOriginalAiRawBoundaryProbeDiagnostic
battleOriginalAiRawBoundaryProbe(int32_t rawX, int32_t rawZ) {
    BattleOriginalAiRawBoundaryProbeDiagnostic result;
    result.exact = true;
    result.rawX = rawX;
    result.rawZ = rawZ;
    if (!originalAiFixedMagnitudeAtMost(rawX, 0xac80u)) {
        result.resultCode = rawX < 0 ? 2u : 3u;
        result.inside = false;
        result.xAxisWon = true;
        return result;
    }
    if (!originalAiFixedMagnitudeAtMost(rawZ, 24000u)) {
        result.resultCode = rawZ < 0 ? 1u : 0u;
        result.inside = false;
        return result;
    }
    return result;
}

BattleOriginalAiRelationCellDiagnostic battleOriginalAiRelationCell(
    const BattleOriginalAiRelationCellDiagnosticInput& input) {
    BattleOriginalAiRelationCellDiagnostic result;
    if (input.firstSlot >= 8u || input.secondSlot >= 8u) {
        return result;
    }
    result.exact = true;
    if (!input.firstSlotOccupied || !input.secondSlotOccupied) {
        return result;
    }
    result.orderedSidePair =
        input.firstSideWord == 0 && input.secondSideWord == 1;
    if (!result.orderedSidePair) {
        return result;
    }

    result.usesDisplayVisibility50ee =
        input.firstSlot == 0u && input.displayModeWordE1c == 0 &&
        input.candidateClipFlagBit2;
    result.usesSimulationVisibility5206 =
        !result.usesDisplayVisibility50ee;
    result.relationValue = result.usesDisplayVisibility50ee
        ? input.playerViewStrictBoundsPassed &&
              input.selectedComponentDisplayListBit1
        : input.simulationVisibility5206;
    result.mirroredValue = result.relationValue;
    return result;
}

BattleOriginalAiDisplayVisibility50eeDiagnostic
battleOriginalAiDisplayVisibility50ee(
    const BattleOriginalAiDisplayVisibility50eeInput& input) {
    BattleOriginalAiDisplayVisibility50eeDiagnostic result;
    if (input.targetIndex < 0 || input.targetIndex > 8) {
        return result;
    }
    result.targetIndexSupported = true;
    result.usesLiveObjectPointer = input.targetIndex < 8;
    result.usesSeparateObjectPointer = input.targetIndex == 8;
    result.extendedControlGateRequired = result.usesSeparateObjectPointer;
    if (!input.targetRawPoseAvailable ||
        !input.playerViewRawPoseAvailable) {
        return result;
    }

    result.deltaX = originalAiSignedDword(
        static_cast<int64_t>(input.playerViewRawX) - input.targetRawX);
    result.deltaZ = originalAiSignedDword(
        static_cast<int64_t>(input.playerViewRawZ) - input.targetRawZ);
    const OriginalAiFixedMagnitude xMagnitude =
        originalAiFixedMagnitude(result.deltaX);
    const OriginalAiFixedMagnitude zMagnitude =
        originalAiFixedMagnitude(result.deltaZ);
    result.xMagnitude =
        (static_cast<uint32_t>(static_cast<uint16_t>(xMagnitude.high)) << 16u) |
        xMagnitude.low;
    result.zMagnitude =
        (static_cast<uint32_t>(static_cast<uint16_t>(zMagnitude.high)) << 16u) |
        zMagnitude.low;

    if (result.extendedControlGateRequired) {
        if (!input.extendedControlWordAvailable) {
            return result;
        }
        result.extendedControlGatePassed = input.extendedControlWord > -2;
        if (!result.extendedControlGatePassed) {
            result.exact = true;
            return result;
        }
    } else {
        result.extendedControlGatePassed = true;
    }

    result.xStrictBoundsPassed =
        originalAiFixedMagnitudeBelow(result.deltaX, 35000u);
    result.zStrictBoundsPassed =
        originalAiFixedMagnitudeBelow(result.deltaZ, 35000u);
    result.strictBoundsPassed =
        result.xStrictBoundsPassed && result.zStrictBoundsPassed;
    if (!result.strictBoundsPassed) {
        result.exact = true;
        return result;
    }
    if (!input.selectedComponentPointerAvailable) {
        return result;
    }

    result.selectedComponentPointerRead = true;
    result.selectedComponentDisplayListBit1 =
        (input.selectedComponentByte1 & 0x02u) != 0u;
    result.relationWord =
        result.selectedComponentDisplayListBit1 ? 2u : 0u;
    result.exact = true;
    return result;
}

BattleOriginalAiSimulationVisibilityNearDiagnostic
battleOriginalAiSimulationVisibility5206Near(
    int32_t firstX,
    int32_t firstZ,
    int32_t firstY,
    int32_t secondX,
    int32_t secondZ,
    int32_t secondY) {
    BattleOriginalAiSimulationVisibilityNearDiagnostic result;
    result.endpointOrderingExact = true;
    if (secondX < firstX) {
        result.endpointsSwapped = true;
        std::swap(firstX, secondX);
        std::swap(firstZ, secondZ);
        std::swap(firstY, secondY);
    }
    result.orderedFirstX = firstX;
    result.orderedFirstZ = firstZ;
    result.orderedFirstY = firstY;
    result.orderedSecondX = secondX;
    result.orderedSecondZ = secondZ;
    result.orderedSecondY = secondY;
    result.rawDeltaX = originalAiSignedDword(
        static_cast<int64_t>(secondX) - firstX);
    result.rawDeltaZ = originalAiSignedDword(
        static_cast<int64_t>(secondZ) - firstZ);
    result.rawDeltaY = originalAiSignedDword(
        static_cast<int64_t>(secondY) - firstY);

    const uint32_t deltaXBits = static_cast<uint32_t>(result.rawDeltaX);
    const uint16_t deltaXLow = static_cast<uint16_t>(deltaXBits & 0xffffu);
    const int16_t deltaXHigh = static_cast<int16_t>(deltaXBits >> 16u);
    result.strictXBelow0x200 =
        deltaXHigh < 1 && (deltaXHigh < 0 || deltaXLow < 0x0200u);

    const OriginalAiFixedMagnitude zMagnitude =
        originalAiFixedMagnitude(result.rawDeltaZ);
    result.zMagnitudeLow = zMagnitude.low;
    result.zMagnitudeHigh = zMagnitude.high;
    result.strictZBelow0x200 =
        zMagnitude.high < 1 &&
        (zMagnitude.high < 0 || zMagnitude.low < 0x0200u);

    result.immediateVisible =
        result.strictXBelow0x200 && result.strictZBelow0x200;
    result.sampledTerrainPathOpen = !result.immediateVisible;
    if (result.immediateVisible) {
        result.exact = true;
        result.relationWord = 1u;
    }
    return result;
}

BattleOriginalAiSimulationVisibilitySampledDiagnostic
battleOriginalAiSimulationVisibility5206Sampled(
    const BattleTerrainCollisionGrid& grid,
    int32_t firstX,
    int32_t firstZ,
    int32_t firstY,
    int32_t secondX,
    int32_t secondZ,
    int32_t secondY) {
    BattleOriginalAiSimulationVisibilitySampledDiagnostic result;
    result.nearPath = battleOriginalAiSimulationVisibility5206Near(
        firstX, firstZ, firstY, secondX, secondZ, secondY);
    if (result.nearPath.exact) {
        result.exact = true;
        result.immediateVisible = true;
        result.relationWord = result.nearPath.relationWord;
        return result;
    }

    result.sampledTerrainPathUsed = true;
    result.gridContractValid =
        grid.valid && grid.width == 174 && grid.height == 94 &&
        grid.rawSamples.size() == 174u * 94u;
    if (!result.gridContractValid) {
        return result;
    }
    if (result.nearPath.rawDeltaX < 0 ||
        !originalAiFixedMagnitudeBelow(
            result.nearPath.orderedFirstX, 0xac80u) ||
        !originalAiFixedMagnitudeBelow(
            result.nearPath.orderedFirstZ, 24000u) ||
        !originalAiFixedMagnitudeBelow(
            result.nearPath.orderedSecondX, 0xac80u) ||
        !originalAiFixedMagnitudeBelow(
            result.nearPath.orderedSecondZ, 24000u)) {
        result.farSceneQueryOpen = true;
        return result;
    }

    const uint32_t zMagnitude =
        static_cast<uint32_t>(result.nearPath.zMagnitudeLow) |
        (static_cast<uint32_t>(
             static_cast<uint16_t>(result.nearPath.zMagnitudeHigh)) << 16u);
    const uint32_t deltaX =
        static_cast<uint32_t>(result.nearPath.rawDeltaX);
    result.xMajorAxis = deltaX > zMagnitude;
    result.zMajorAxis = !result.xMajorAxis;
    result.negativeZDirection = result.nearPath.rawDeltaZ < 0;
    const int64_t majorMagnitude = result.xMajorAxis
        ? static_cast<int64_t>(deltaX)
        : static_cast<int64_t>(zMagnitude);
    if (majorMagnitude == 0) {
        return result;
    }

    if (result.xMajorAxis) {
        result.rawStepX = 0x0200;
        result.rawStepZ = originalAiSignedWord(
            static_cast<int64_t>(originalAiSignedDword(
                static_cast<int64_t>(result.nearPath.rawDeltaZ) *
                0x0200ll)) /
            majorMagnitude);
    } else {
        result.rawStepX = originalAiSignedWord(
            static_cast<int64_t>(originalAiSignedDword(
                static_cast<int64_t>(result.nearPath.rawDeltaX) *
                0x0200ll)) /
            majorMagnitude);
        result.rawStepZ = result.negativeZDirection
            ? static_cast<int16_t>(-0x0200)
            : static_cast<int16_t>(0x0200);
    }
    result.rawStepY = originalAiSignedWord(
        static_cast<int64_t>(originalAiSignedDword(
            static_cast<int64_t>(result.nearPath.rawDeltaY) * 0x0200ll)) /
        majorMagnitude);

    int32_t currentX = result.nearPath.orderedFirstX;
    int32_t currentZ = result.nearPath.orderedFirstZ;
    int32_t currentY = result.nearPath.orderedFirstY;
    constexpr size_t kMaximumInBoundsSamples = 512u;
    for (size_t sampleIndex = 0u;
         sampleIndex < kMaximumInBoundsSamples;
         ++sampleIndex) {
        currentX = originalAiSignedDword(
            static_cast<int64_t>(currentX) + result.rawStepX);
        currentZ = originalAiSignedDword(
            static_cast<int64_t>(currentZ) + result.rawStepZ);
        currentY = originalAiSignedDword(
            static_cast<int64_t>(currentY) + result.rawStepY);

        BattleOriginalAiSimulationVisibilitySample5206Diagnostic sample;
        sample.rawX = currentX;
        sample.rawZ = currentZ;
        sample.rawY = currentY;
        sample.reachesOrPassesEndpoint = result.xMajorAxis
            ? currentX >= result.nearPath.orderedSecondX
            : (result.negativeZDirection
                  ? currentZ <= result.nearPath.orderedSecondZ
                  : currentZ >= result.nearPath.orderedSecondZ);

        if (!originalAiFixedMagnitudeBelow(currentX, 0xac80u) ||
            !originalAiFixedMagnitudeBelow(currentZ, 24000u)) {
            result.samples.push_back(sample);
            result.sampledPointCount = static_cast<uint16_t>(
                result.samples.size());
            result.terminalSampleVisited =
                result.terminalSampleVisited ||
                sample.reachesOrPassesEndpoint;
            result.farSceneQueryOpen = true;
            return result;
        }

        const int gridX = static_cast<int>(
            (static_cast<int64_t>(currentX) + 0xac80ll) / 0x0200ll);
        const int gridZ = static_cast<int>(
            (24000ll - static_cast<int64_t>(currentZ)) / 0x0200ll);
        if (gridX < 0 || gridX >= grid.width ||
            gridZ < 0 || gridZ >= grid.height) {
            result.samples.push_back(sample);
            result.sampledPointCount = static_cast<uint16_t>(
                result.samples.size());
            result.farSceneQueryOpen = true;
            return result;
        }
        sample.gridX = static_cast<int16_t>(gridX);
        sample.gridZ = static_cast<int16_t>(gridZ);
        sample.rawTerrainSample = grid.rawSamples[
            static_cast<size_t>(gridX) * static_cast<size_t>(grid.height) +
            static_cast<size_t>(gridZ)];
        sample.terrainHeight = static_cast<uint16_t>(
            static_cast<uint16_t>(sample.rawTerrainSample) << 4u);
        sample.terrainBlocks =
            static_cast<int32_t>(sample.terrainHeight) > currentY;
        result.samples.push_back(sample);
        result.sampledPointCount = static_cast<uint16_t>(
            result.samples.size());
        result.terminalSampleVisited =
            result.terminalSampleVisited ||
            sample.reachesOrPassesEndpoint;
        if (sample.terrainBlocks) {
            result.exact = true;
            result.blockedByTerrain = true;
            result.blockingSampleIndex =
                static_cast<uint16_t>(sampleIndex);
            return result;
        }
        if (sample.reachesOrPassesEndpoint) {
            result.exact = true;
            result.relationWord = 1u;
            return result;
        }
    }
    return result;
}

BattleOriginalAiControlInitializationDiagnostic
battleOriginalAiControlInitialization6fd8(
    uint8_t liveObjectSlot,
    bool liveObjectOccupied) {
    BattleOriginalAiControlInitializationDiagnostic result;
    result.liveObjectSlot = liveObjectSlot;
    result.liveObjectOccupied = liveObjectOccupied;
    if (liveObjectSlot >= 8u) {
        return result;
    }
    result.exact = true;
    if (!liveObjectOccupied) {
        return result;
    }

    result.visibleLoopWroteRecord = true;
    result.recordValuesKnown = true;
    result.zeroFillByteCount = 0x55u;
    const auto storeWord = [&result](size_t offset, int16_t value) {
        const uint16_t bits = static_cast<uint16_t>(value);
        result.recordBytes[offset] = static_cast<uint8_t>(bits & 0xffu);
        result.recordBytes[offset + 1u] =
            static_cast<uint8_t>((bits >> 8u) & 0xffu);
    };
    const auto loadWord = [&result](size_t offset) {
        return static_cast<int16_t>(static_cast<uint16_t>(
            result.recordBytes[offset] |
            (static_cast<uint16_t>(result.recordBytes[offset + 1u]) << 8u)));
    };

    storeWord(0x00u, 0);
    storeWord(0x10u, liveObjectSlot > 3u ? 1 : 0);
    storeWord(0x12u, static_cast<int16_t>(liveObjectSlot));
    storeWord(0x1au, -1);
    storeWord(0x14u, -1);
    storeWord(0x18u, -1);
    storeWord(0x0eu, -1);
    result.recordBytes[0x3eu] = static_cast<uint8_t>(
        result.recordBytes[0x3eu] | 1u);

    result.word00 = loadWord(0x00u);
    result.word0e = loadWord(0x0eu);
    result.sideWord10 = loadWord(0x10u);
    result.slotWord12 = loadWord(0x12u);
    result.word14 = loadWord(0x14u);
    result.aggregateWord16 = loadWord(0x16u);
    result.word18 = loadWord(0x18u);
    result.word1a = loadWord(0x1au);
    result.selectionCountWord1c = loadWord(0x1cu);
    result.cachedPoseStateWord2c = loadWord(0x2cu);
    result.selectorWord3a = loadWord(0x3au);
    result.selectorWord3c = loadWord(0x3cu);
    result.flagByte3e = result.recordBytes[0x3eu];
    result.numericStateWord46 = loadWord(0x46u);
    result.sharedStateWord51 = loadWord(0x51u);
    result.word53 = loadWord(0x53u);
    return result;
}

BattleOriginalAiInitialBodyPoseDiagnostic
battleOriginalAiInitialBodyPose1a22(
    const BattleOriginalAiInitialBodyPoseDiagnosticInput& input) {
    BattleOriginalAiInitialBodyPoseDiagnostic result;
    result.liveObjectSlot = input.liveObjectSlot;
    result.liveObjectOccupied = input.liveObjectOccupied;
    result.numericSideModeWord = input.numericSideModeWord;
    result.numericSideModeZero = input.numericSideModeWord == 0;
    result.numericSideModeSupported =
        input.numericSideModeWord >= 0 && input.numericSideModeWord <= 4;
    result.anchorsAvailable = input.anchorsAvailable;
    result.sideHeadingFlagsAvailable = input.sideHeadingFlagsAvailable;
    result.sideHeadingFlags = input.sideHeadingFlags;
    result.separateObjectiveRawPoseAvailable =
        input.separateObjectiveRawPoseAvailable;
    result.modeThreeNullDataRawPoseAvailable =
        input.modeThreeNullDataRawPoseAvailable;
    if (input.liveObjectSlot >= 8u || !input.liveObjectOccupied ||
        !result.numericSideModeSupported || !input.anchorsAvailable) {
        return result;
    }

    result.sideWord = input.liveObjectSlot > 3u ? 1 : 0;
    result.rawX = input.slotRawX;
    result.rawY = 300;
    result.rawZ = input.slotRawZ;
    result.pitch = 0;
    result.roll = 0;
    result.positionCopiedToAuthoritativeBody = true;

    const int32_t anchorDeltaX = originalAiSignedDword(
        static_cast<int64_t>(input.slot4RawX) - input.slot0RawX);
    const int32_t anchorDeltaZ = originalAiSignedDword(
        static_cast<int64_t>(input.slot4RawZ) - input.slot0RawZ);
    result.sharedSlot0ToSlot4Bearing =
        originalAiFixedAngle(anchorDeltaX, anchorDeltaZ);

    switch (input.numericSideModeWord) {
    case 0:
    case 2:
    case 4:
        result.headingSource =
            BattleOriginalAiInitialHeadingSource::
                SharedSlot0ToSlot4Bearing;
        result.heading = result.sideWord == 0
            ? result.sharedSlot0ToSlot4Bearing
            : originalAiSignedWord(
                  static_cast<int64_t>(
                      result.sharedSlot0ToSlot4Bearing) +
                  0x7ff8);
        break;
    case 1:
        if (!input.sideHeadingFlagsAvailable) {
            return result;
        }
        result.headingSource =
            BattleOriginalAiInitialHeadingSource::SideHeadingFlag;
        if ((input.sideHeadingFlags & 0x01u) != 0u) {
            result.selectedSideHeadingFlagMask = 0x01u;
            result.heading = 0;
        } else if ((input.sideHeadingFlags & 0x02u) != 0u) {
            result.selectedSideHeadingFlagMask = 0x02u;
            result.heading = static_cast<int16_t>(0x7ff8);
        } else if ((input.sideHeadingFlags & 0x04u) != 0u) {
            result.selectedSideHeadingFlagMask = 0x04u;
            result.heading = static_cast<int16_t>(0x3ffc);
        } else if ((input.sideHeadingFlags & 0x08u) != 0u) {
            result.selectedSideHeadingFlagMask = 0x08u;
            result.heading = static_cast<int16_t>(-0x3ffc);
        }
        break;
    case 3: {
        if (!input.separateObjectiveRawPoseAvailable &&
            !input.modeThreeNullDataRawPoseAvailable) {
            return result;
        }
        const bool nullDataPoint =
            input.modeThreeNullDataRawPoseAvailable;
        result.headingSource = nullDataPoint
            ? BattleOriginalAiInitialHeadingSource::ModeThreeNullDataBearing
            : BattleOriginalAiInitialHeadingSource::SeparateObjectiveBearing;
        const int32_t originX = result.sideWord == 0
            ? input.slot0RawX
            : input.slot4RawX;
        const int32_t originZ = result.sideWord == 0
            ? input.slot0RawZ
            : input.slot4RawZ;
        const int32_t targetRawX = nullDataPoint
            ? input.modeThreeNullDataRawX
            : input.separateObjectiveRawX;
        const int32_t targetRawZ = nullDataPoint
            ? input.modeThreeNullDataRawZ
            : input.separateObjectiveRawZ;
        const int32_t objectiveDeltaX = originalAiSignedDword(
            static_cast<int64_t>(targetRawX) - originX);
        const int32_t objectiveDeltaZ = originalAiSignedDword(
            static_cast<int64_t>(targetRawZ) - originZ);
        result.heading = originalAiFixedAngle(
            objectiveDeltaX, objectiveDeltaZ);
        break;
    }
    default:
        return result;
    }

    result.exact = true;
    return result;
}

BattleOriginalAiInitialBodyPoseDiagnostic
battleOriginalAiInitialBodyPose1a22ModeZero(
    uint8_t liveObjectSlot,
    bool liveObjectOccupied,
    int16_t numericSideModeWord,
    bool anchorsAvailable,
    int32_t slotRawX,
    int32_t slotRawZ,
    int32_t slot0RawX,
    int32_t slot0RawZ,
    int32_t slot4RawX,
    int32_t slot4RawZ) {
    BattleOriginalAiInitialBodyPoseDiagnosticInput input;
    input.liveObjectSlot = liveObjectSlot;
    input.liveObjectOccupied = liveObjectOccupied;
    input.numericSideModeWord = numericSideModeWord;
    input.anchorsAvailable = anchorsAvailable;
    input.slotRawX = slotRawX;
    input.slotRawZ = slotRawZ;
    input.slot0RawX = slot0RawX;
    input.slot0RawZ = slot0RawZ;
    input.slot4RawX = slot4RawX;
    input.slot4RawZ = slot4RawZ;
    if (numericSideModeWord != 0) {
        BattleOriginalAiInitialBodyPoseDiagnostic result;
        result.liveObjectSlot = liveObjectSlot;
        result.liveObjectOccupied = liveObjectOccupied;
        result.numericSideModeWord = numericSideModeWord;
        result.numericSideModeZero = false;
        result.numericSideModeSupported =
            numericSideModeWord >= 0 && numericSideModeWord <= 4;
        result.anchorsAvailable = anchorsAvailable;
        return result;
    }
    return battleOriginalAiInitialBodyPose1a22(input);
}

BattleOriginalAiAggregateDiagnostic battleOriginalAiAggregate7070(
    const BattleOriginalAiAggregateDiagnosticInput& input) {
    BattleOriginalAiAggregateDiagnostic result;
    result.liveObjectSlot = input.liveObjectSlot;
    result.occupied = input.occupied;
    if (input.liveObjectSlot >= 8u) {
        return result;
    }
    result.exact = true;
    if (!input.occupied) {
        return result;
    }

    int16_t aggregate = 0;
    const auto addWord = [](int16_t left, int16_t right) {
        return originalAiSignedWord(
            static_cast<int64_t>(left) + right);
    };
    result.armorContributionEnabled = input.gyroConditionWord13 != 2;
    if (result.armorContributionEnabled) {
        for (int16_t word : input.armorWords) {
            result.armorContribution = addWord(
                result.armorContribution, word);
            aggregate = addWord(aggregate, word);
        }
    }
    for (int16_t word : input.internalWords) {
        result.internalContribution = addWord(
            result.internalContribution, word);
        aggregate = addWord(aggregate, word);
    }
    result.weaponContributionEnabled = input.sensorConditionWord0f != 2;
    if (result.weaponContributionEnabled) {
        for (const auto& weapon : input.weapons) {
            if (weapon.installationByte == 0u) {
                continue;
            }
            if (result.contributingWeaponCount <
                std::numeric_limits<uint8_t>::max()) {
                ++result.contributingWeaponCount;
            }
            const int16_t doubledDamage = originalAiSignedWord(
                static_cast<int64_t>(weapon.originalDamageWord) * 2);
            result.weaponContribution = addWord(
                result.weaponContribution, doubledDamage);
            aggregate = addWord(aggregate, doubledDamage);
        }
    }
    result.preViabilityAggregate = aggregate;

    result.viability4106Passed =
        input.engineConditionWord11 < 3 &&
        input.lifeSupportConditionWord0d < 2 &&
        input.opaqueWord15 == 0 &&
        input.internalWords[2] != 0 &&
        input.internalWords[3] != 0 &&
        (input.sharedCurrentControlStateWord51 != 1 ||
         (input.leftLegActuatorWord6c != 0 &&
          input.rightLegActuatorWord6e != 0));
    result.viabilityReplacedWithMinusOne = !result.viability4106Passed;
    result.postViabilityAggregate = result.viability4106Passed
        ? aggregate
        : static_cast<int16_t>(-1);
    result.finalAggregateWord16 = result.postViabilityAggregate;

    result.boundary = battleOriginalAiRawBoundaryProbe(
        input.rawX, input.rawZ);
    if (!result.boundary.exact) {
        result.exact = false;
        return result;
    }
    result.boundaryOverrideApplied = result.boundary.resultCode < 4u;
    if (result.boundaryOverrideApplied) {
        result.finalAggregateWord16 = static_cast<int16_t>(
            -(static_cast<int16_t>(result.boundary.resultCode) + 2));
        result.clearsControlStateWord46 = true;
        result.setsSixComponentFlagBits80 = true;
    }
    return result;
}

BattleOriginalAiAggregateDiagnostic
battleOriginalAiAggregate7070FromAuthoritativeState(
    uint8_t liveObjectSlot,
    int32_t rawX,
    int32_t rawZ,
    int16_t sharedCurrentControlStateWord51,
    const mech3d::MechDetailedDamageState& detailedDamage,
    const std::vector<BattleWeaponInstanceState>& weapons) {
    BattleOriginalAiAggregateDiagnostic failure;
    failure.authoritativeStateBindingAttempted = true;
    failure.liveObjectSlot = liveObjectSlot;
    failure.occupied = true;
    if (liveObjectSlot >= 8u || !detailedDamage.valid) {
        return failure;
    }

    BattleOriginalAiAggregateDiagnosticInput input;
    input.liveObjectSlot = liveObjectSlot;
    input.occupied = true;
    input.rawX = rawX;
    input.rawZ = rawZ;
    input.sharedCurrentControlStateWord51 =
        sharedCurrentControlStateWord51;

    for (size_t index = 0; index < input.armorWords.size(); ++index) {
        const auto expected = static_cast<mech3d::MechArmorSectionId>(index);
        const auto& section = detailedDamage.armorSections[index];
        if (section.section != expected) {
            return failure;
        }
        input.armorWords[index] = static_cast<int16_t>(
            static_cast<uint16_t>(section.armorRemaining));
    }
    for (size_t index = 0; index < input.internalWords.size(); ++index) {
        const auto expected = static_cast<mech3d::MechInternalSectionId>(index);
        const auto& section = detailedDamage.internalSections[index];
        if (section.section != expected) {
            return failure;
        }
        input.internalWords[index] = static_cast<int16_t>(
            static_cast<uint16_t>(section.structureRemaining));
    }
    for (size_t index = 0;
         index < detailedDamage.criticalComponents.size(); ++index) {
        if (detailedDamage.criticalComponents[index].component !=
            static_cast<mech3d::MechCriticalComponentId>(index)) {
            return failure;
        }
    }
    for (size_t index = 0;
         index < detailedDamage.installedWeapons.size(); ++index) {
        if (detailedDamage.installedWeapons[index].slotIndex != index) {
            return failure;
        }
    }

    const auto critical = [&detailedDamage](
                              mech3d::MechCriticalComponentId id)
        -> const mech3d::MechCriticalComponentRuntimeState& {
        return detailedDamage.criticalComponents[
            static_cast<size_t>(id)];
    };
    input.lifeSupportConditionWord0d = critical(
        mech3d::MechCriticalComponentId::LifeSupport).condition;
    input.sensorConditionWord0f = critical(
        mech3d::MechCriticalComponentId::Sensors).condition;
    input.engineConditionWord11 = critical(
        mech3d::MechCriticalComponentId::Engine).condition;
    input.gyroConditionWord13 = critical(
        mech3d::MechCriticalComponentId::Gyros).condition;
    input.opaqueWord15 = detailedDamage.originalCockpitCriticalFlag ? 1 : 0;
    input.leftLegActuatorWord6c = critical(
        mech3d::MechCriticalComponentId::LeftLegActuator).workingCount;
    input.rightLegActuatorWord6e = critical(
        mech3d::MechCriticalComponentId::RightLegActuator).workingCount;

    std::array<bool, 10> seenWeaponSlots{};
    for (const BattleWeaponInstanceState& weapon : weapons) {
        if (weapon.slotIndex >= input.weapons.size() ||
            seenWeaponSlots[weapon.slotIndex] ||
            !weapon.originalDamageValueProven) {
            return failure;
        }
        seenWeaponSlots[weapon.slotIndex] = true;
        const auto& installed =
            detailedDamage.installedWeapons[weapon.slotIndex];
        if (weapon.functional != installed.functional ||
            weapon.condition != installed.condition) {
            return failure;
        }
        input.weapons[weapon.slotIndex].installationByte =
            installed.functional ? 1u : 0u;
        input.weapons[weapon.slotIndex].originalDamageWord =
            static_cast<int16_t>(weapon.originalDamage);
    }

    BattleOriginalAiAggregateDiagnostic result =
        battleOriginalAiAggregate7070(input);
    result.authoritativeStateBindingAttempted = true;
    result.authoritativeStateBindingExact = result.exact;
    return result;
}

BattleOriginalAiLiveSlotOverlapDiagnostic
battleOriginalAiLiveSlotOverlap(
    int32_t deltaX,
    int32_t deltaZ,
    int32_t deltaY,
    int16_t rawSpeed,
    bool sameOwnerWord) {
    BattleOriginalAiLiveSlotOverlapDiagnostic result;
    result.exact = true;
    result.deltaX = deltaX;
    result.deltaZ = deltaZ;
    result.deltaY = deltaY;
    result.rawSpeed = rawSpeed;
    result.sameOwnerWord = sameOwnerWord;
    result.overlaps =
        originalAiFixedMagnitudeBelow(deltaX, 0x0154u) &&
        originalAiFixedMagnitudeBelow(deltaZ, 0x0154u) &&
        originalAiFixedMagnitudeBelow(deltaY, 800u);
    result.movementBlocked = result.overlaps;
    result.dispatchesSharedImpact =
        result.overlaps && rawSpeed >= 6 && !sameOwnerWord;
    return result;
}

BattleOriginalAiObjectiveContactDiagnostic
battleOriginalAiObjectiveContact(
    bool objectiveInactiveBitSet,
    int16_t opaqueGateWordEd0,
    bool playerSlot,
    int32_t deltaX,
    int32_t objectY,
    int32_t deltaZ) {
    BattleOriginalAiObjectiveContactDiagnostic result;
    result.exact = true;
    result.objectiveInactiveBitSet = objectiveInactiveBitSet;
    result.opaqueGateWordEd0 = opaqueGateWordEd0;
    result.playerSlot = playerSlot;
    result.deltaX = deltaX;
    result.objectY = objectY;
    result.deltaZ = deltaZ;
    result.zThreshold = playerSlot ? 0x0c80u : 0x0578u;
    result.contactPredicate =
        !objectiveInactiveBitSet &&
        originalAiFixedMagnitudeBelow(deltaX, 0x0c80u) &&
        objectY < 3000 &&
        originalAiFixedMagnitudeBelow(deltaZ, result.zThreshold);
    result.movementBlocked =
        opaqueGateWordEd0 > 2 && result.contactPredicate;
    return result;
}

BattleOriginalAiSlotProbeDiagnostic battleOriginalAiSlotProbe(
    const BattleOriginalAiSlotProbeDiagnosticInput& input) {
    BattleOriginalAiSlotProbeDiagnostic result;
    if (input.movingSlot >= 8u || input.liveSlots.size() > 8u) {
        return result;
    }
    result.exact = true;

    // FUN_1000_89ea skips the 0001:2738 battlefield classifier for the
    // player slot, numeric control state 8, numeric control word 3, or a
    // cached destination outside this exact unsigned/signed domain.
    result.boundaryClassifierBypassed =
        input.movingSlot == 0u || input.controlState46 == 8 ||
        input.controlWord00 == 3 ||
        input.cachedDestinationRawY < 0 ||
        !originalAiFixedMagnitudeBelow(
            input.cachedDestinationRawX, 0xac80u) ||
        !originalAiFixedMagnitudeBelow(
            input.cachedDestinationRawZ, 24000u);
    if (!result.boundaryClassifierBypassed) {
        result.boundaryClassifierCalled = true;
        result.boundary = battleOriginalAiRawBoundaryProbe(
            input.tentativeRawX, input.tentativeRawZ);
        if (!result.boundary.inside) {
            result.resultWord = 1u;
            return result;
        }
    }

    for (size_t slot = 0; slot < input.liveSlots.size(); ++slot) {
        if (slot == input.movingSlot || !input.liveSlots[slot].occupied) {
            continue;
        }
        if (result.visitedLiveSlotCount <
            std::numeric_limits<uint8_t>::max()) {
            ++result.visitedLiveSlotCount;
        }
        const BattleOriginalAiSlotProbeLiveObjectDiagnosticInput& candidate =
            input.liveSlots[slot];
        const BattleOriginalAiLiveSlotOverlapDiagnostic overlap =
            battleOriginalAiLiveSlotOverlap(
                originalAiSignedDword(
                    static_cast<int64_t>(candidate.rawX) -
                    input.tentativeRawX),
                originalAiSignedDword(
                    static_cast<int64_t>(candidate.rawZ) -
                    input.tentativeRawZ),
                originalAiSignedDword(
                    static_cast<int64_t>(candidate.rawY) -
                    input.tentativeRawY),
                input.rawSpeed,
                candidate.ownerWord == input.movingOwnerWord);
        if (!overlap.movementBlocked) {
            continue;
        }
        result.resultWord = 1u;
        result.liveSlotBlocked = true;
        result.blockingLiveSlot = static_cast<uint8_t>(slot);
        result.blockingEntityId = candidate.entityId;
        result.dispatchesSharedImpact = overlap.dispatchesSharedImpact;
        return result;
    }

    result.objective = battleOriginalAiObjectiveContact(
        input.objectiveInactiveBitSet,
        input.opaqueGateWordEd0,
        input.movingSlot == 0u,
        originalAiSignedDword(
            static_cast<int64_t>(input.objectiveRawX) -
            input.tentativeRawX),
        input.tentativeRawY,
        originalAiSignedDword(
            static_cast<int64_t>(input.objectiveRawZ) -
            input.tentativeRawZ));
    result.objectiveBlocked = result.objective.movementBlocked;
    result.resultWord = result.objectiveBlocked ? 1u : 0u;
    return result;
}

BattleOriginalAiSceneProbeSelectionDiagnostic
battleOriginalAiSceneProbeSelection(
    bool recordTableAvailable,
    bool cachedRecordPresent,
    uint8_t cachedPriority,
    bool cachedNarrowPhaseHit,
    const std::vector<BattleOriginalAiSceneProbeCandidateDiagnosticInput>&
        candidates) {
    BattleOriginalAiSceneProbeSelectionDiagnostic result;
    result.exact = true;
    result.recordTableAvailable = recordTableAvailable;
    result.cachedRecordPresent = cachedRecordPresent;
    result.cachedPriority = cachedPriority;
    result.cachedNarrowPhaseHit = cachedNarrowPhaseHit;
    if (!recordTableAvailable) {
        return result;
    }

    result.cacheEligible = cachedRecordPresent && cachedPriority == 0u;
    if (result.cacheEligible) {
        ++result.narrowPhaseTestCount;
        if (cachedNarrowPhaseHit) {
            result.selectedFromCache = true;
            result.hit = true;
            result.selectedPriority = 0u;
            result.terminatedOnPriorityZero = true;
            return result;
        }
        result.clearedCacheAfterMiss = true;
    }

    for (size_t index = 0; index < candidates.size(); ++index) {
        ++result.visitedCandidateCount;
        const BattleOriginalAiSceneProbeCandidateDiagnosticInput& candidate =
            candidates[index];
        if (!candidate.objectFlagBit10 ||
            !candidate.recordByte7Nonzero ||
            (result.hit && candidate.priority > result.selectedPriority)) {
            continue;
        }
        ++result.narrowPhaseTestCount;
        if (!candidate.narrowPhaseHit) {
            continue;
        }
        result.hit = true;
        result.selectedCandidateIndex = static_cast<uint16_t>(index);
        result.selectedPriority = candidate.priority;
        if (candidate.priority == 0u) {
            result.terminatedOnPriorityZero = true;
            return result;
        }
    }
    return result;
}

BattleOriginalAiMovementTargetDiagnostic
battleOriginalAiMovementTarget(
    int32_t deltaX,
    int32_t deltaZ,
    int16_t numericState51,
    int16_t currentHeading) {
    BattleOriginalAiMovementTargetDiagnostic result;
    result.exact = true;
    result.deltaX = deltaX;
    result.deltaZ = deltaZ;
    result.numericState51 = numericState51;
    result.currentHeading = currentHeading;
    result.insideStrictStopSquare =
        originalAiFixedMagnitudeBelow(deltaX, 0x00a0u) &&
        originalAiFixedMagnitudeBelow(deltaZ, 0x00a0u);
    if (result.insideStrictStopSquare) {
        result.desiredHeadingBeforeSteering = currentHeading;
        result.movementMode = 2;
        result.routesDirectlyToSpeedCommand = true;
        return result;
    }

    result.desiredHeadingBeforeSteering =
        originalAiFixedAngle(deltaX, deltaZ);
    result.movementMode = 0;
    if (numericState51 == 3) {
        result.routesDirectlyToSpeedCommand = true;
    } else {
        result.routesThroughSteeringProbes = true;
        result.finalHeadingOpen = true;
    }
    return result;
}

BattleOriginalAiSteeringChoiceDiagnostic
battleOriginalAiSteeringChoice(
    int16_t desiredHeading,
    bool forwardRequest,
    int16_t selectorWord3a,
    int16_t selectorWord3c,
    const std::array<bool, 4>& probeBlocked) {
    BattleOriginalAiSteeringChoiceDiagnostic result;
    result.desiredHeadingInput = desiredHeading;
    result.forwardRequest = forwardRequest;
    result.selectorWord3aBefore = selectorWord3a;
    result.selectorWord3cBefore = selectorWord3c;
    result.probeBlocked = probeBlocked;
    result.selectorWord3aAfter = selectorWord3a;
    result.selectorWord3cAfter = selectorWord3c;
    if (selectorWord3c < 0 || selectorWord3c > 2) {
        return result;
    }

    result.exact = true;
    result.headingUsedForProbes = originalAiSignedWord(
        static_cast<int64_t>(desiredHeading) +
        (forwardRequest ? 0 : 0x7ff8));
    const auto visitProbe = [&result](size_t index) {
        result.probeVisited[index] = true;
        ++result.probeVisitCount;
        return result.probeBlocked[index];
    };

    const bool probeZeroBlocked = visitProbe(0u);
    if (!probeZeroBlocked && result.selectorWord3aAfter != 1) {
        result.selectorWord3cAfter = 0;
    } else {
        result.selectorWord3aAfter = 0;
        const bool probeOneBlocked = result.selectorWord3cAfter == 2
            ? true
            : visitProbe(1u);
        if (result.selectorWord3cAfter == 2 || probeOneBlocked) {
            if (!visitProbe(2u)) {
                result.selectorWord3cAfter = 2;
            } else if (!visitProbe(3u)) {
                result.selectorWord3aAfter = 1;
            } else {
                result.selectorWord3cAfter =
                    result.selectorWord3cAfter == 1 ? 2 : 1;
            }
        } else {
            result.selectorWord3cAfter = 1;
        }
    }

    constexpr std::array<int16_t, 3> kHeadingOffsets{{
        0,
        static_cast<int16_t>(0x3ffc),
        static_cast<int16_t>(-0x3ffc),
    }};
    result.selectedHeadingOffset = result.selectorWord3aAfter == 0
        ? kHeadingOffsets[static_cast<size_t>(result.selectorWord3cAfter)]
        : static_cast<int16_t>(0x7ff8);
    result.finalDesiredHeading = originalAiSignedWord(
        static_cast<int64_t>(result.headingUsedForProbes) +
        result.selectedHeadingOffset +
        (forwardRequest ? 0 : 0x7ff8));
    result.movementMode = forwardRequest ? 0 : 1;
    return result;
}

BattleOriginalAiOrientedLaneDiagnostic
battleOriginalAiOrientedLane(
    int32_t deltaX,
    int32_t deltaZ,
    int16_t bodyHeading,
    uint16_t lateralWidth) {
    BattleOriginalAiOrientedLaneDiagnostic result;
    result.deltaX = deltaX;
    result.deltaZ = deltaZ;
    result.bodyHeading = bodyHeading;
    result.lateralWidth = lateralWidth;
    const std::optional<uint32_t> distance =
        originalAiApproximatePlanarDistance(deltaX, deltaZ);
    if (!distance || lateralWidth > 0x7fffu) {
        return result;
    }

    result.exact = true;
    result.approximateDistance = *distance;
    result.strictDistancePassed = *distance < 0x0800u;
    result.candidateHeading = originalAiFixedAngle(deltaX, deltaZ);
    result.headingDelta = originalAiSignedWord(
        static_cast<int64_t>(result.candidateHeading) - bodyHeading);
    result.absoluteHeadingDelta = result.headingDelta ==
            std::numeric_limits<int16_t>::min()
        ? 0x7fffu
        : static_cast<uint16_t>(std::abs(
              static_cast<int32_t>(result.headingDelta)));
    result.strictHeadingPassed = result.absoluteHeadingDelta < 0x3ffcu;
    result.sineQ14 = originalAiSineQ14(result.headingDelta);
    result.lateralProjection = static_cast<int16_t>(
        originalAiArithmeticShiftRight14(
            static_cast<int32_t>(*distance) *
            static_cast<int32_t>(result.sineQ14)));
    result.absoluteLateralProjection = result.lateralProjection ==
            std::numeric_limits<int16_t>::min()
        ? 0x7fffu
        : static_cast<uint16_t>(std::abs(
              static_cast<int32_t>(result.lateralProjection)));
    result.strictLateralWidthPassed =
        result.absoluteLateralProjection < lateralWidth;
    result.insideLane =
        result.strictDistancePassed &&
        result.strictHeadingPassed &&
        result.strictLateralWidthPassed;
    return result;
}

BattleOriginalAiTerrainProbeDiagnostic
battleOriginalAiTerrainProbe(
    const BattleTerrainCollisionGrid& grid,
    int32_t rawX,
    int32_t rawZ,
    int16_t desiredHeading,
    uint8_t probeIndex) {
    BattleOriginalAiTerrainProbeDiagnostic result;
    result.probeIndex = probeIndex;
    result.desiredHeading = desiredHeading;
    result.gridContractValid =
        grid.valid && grid.width == 174 && grid.height == 94 &&
        grid.rawSamples.size() ==
            static_cast<size_t>(grid.width) *
                static_cast<size_t>(grid.height);
    if (!result.gridContractValid || probeIndex >= 4u) {
        return result;
    }

    const int16_t baseX = originalAiSignedWord(
        -originalAiArithmeticShiftRight5(
            static_cast<int32_t>(originalAiSineQ14(desiredHeading))));
    const int16_t baseZ = originalAiSignedWord(
        originalAiArithmeticShiftRight5(
            static_cast<int32_t>(originalAiCosineQ14(
                static_cast<uint16_t>(desiredHeading)))));
    switch (probeIndex) {
    case 0:
        result.rawDirectionX = baseX;
        result.rawDirectionZ = baseZ;
        break;
    case 1:
        result.rawDirectionX = originalAiSignedWord(-baseZ);
        result.rawDirectionZ = baseX;
        break;
    case 2:
        result.rawDirectionX = baseZ;
        result.rawDirectionZ = originalAiSignedWord(-baseX);
        break;
    case 3:
        result.rawDirectionX = originalAiSignedWord(-baseX);
        result.rawDirectionZ = originalAiSignedWord(-baseZ);
        break;
    default:
        return result;
    }

    result.exact = true;
    int32_t accumulatedX = 0;
    int32_t accumulatedZ = 0;
    for (size_t step = 0; step < 4u; ++step) {
        accumulatedX = originalAiSignedDword(
            static_cast<int64_t>(accumulatedX) + result.rawDirectionX);
        accumulatedZ = originalAiSignedDword(
            static_cast<int64_t>(accumulatedZ) + result.rawDirectionZ);
        const int32_t probeX = originalAiSignedDword(
            static_cast<int64_t>(rawX) + accumulatedX);
        const int32_t probeZ = originalAiSignedDword(
            static_cast<int64_t>(rawZ) + accumulatedZ);
        result.rawProbeX[step] = probeX;
        result.rawProbeZ[step] = probeZ;
        ++result.visitedStepCount;
        if (!originalAiFixedMagnitudeBelow(probeX, 0xac80u) ||
            !originalAiFixedMagnitudeBelow(probeZ, 24000u)) {
            continue;
        }

        const int gridX = static_cast<int>(
            (static_cast<int64_t>(probeX) + 0xac80) / 0x0200);
        const int gridZ = static_cast<int>(
            (24000ll - static_cast<int64_t>(probeZ)) / 0x0200);
        result.gridX[step] = static_cast<int16_t>(gridX);
        result.gridZ[step] = static_cast<int16_t>(gridZ);
        const int minimumX = gridX - 2;
        const int minimumZ = gridZ - 2;
        if (minimumX < 0 || minimumZ < 0 ||
            minimumX + 4 >= grid.width ||
            minimumZ + 4 >= grid.height) {
            result.exact = false;
            result.footprintOutsideGrid = true;
            return result;
        }

        for (int xOffset = 0; xOffset < 5; ++xOffset) {
            for (int zOffset = 0; zOffset < 5; ++zOffset) {
                const int sampleX = minimumX + xOffset;
                const int sampleZ = minimumZ + zOffset;
                ++result.visitedSampleCount;
                const uint8_t sample = grid.rawSamples[
                    static_cast<size_t>(sampleX) *
                        static_cast<size_t>(grid.height) +
                    static_cast<size_t>(sampleZ)];
                if (sample == 0u) {
                    continue;
                }
                result.blocked = true;
                result.blockingStepIndex = static_cast<uint8_t>(step);
                result.blockingGridX = static_cast<int16_t>(sampleX);
                result.blockingGridZ = static_cast<int16_t>(sampleZ);
                result.blockingRawSample = sample;
                return result;
            }
        }
    }
    return result;
}

BattleOriginalAiRawWorldPoseBridgeDiagnostic
battleOriginalAiRawWorldPoseBridge(
    double worldX,
    double worldZ,
    double terrainCellSize,
    int16_t rawHeading,
    int16_t rawTurnCommand,
    int16_t rawSpeed) {
    BattleOriginalAiRawWorldPoseBridgeDiagnostic result;
    result.terrainCellSize = terrainCellSize;
    result.worldXBefore = worldX;
    result.worldZBefore = worldZ;
    result.rawHeadingBefore = rawHeading;
    result.rawTurnCommand = rawTurnCommand;
    result.rawSpeed = rawSpeed;
    if (!std::isfinite(worldX) || !std::isfinite(worldZ) ||
        !std::isfinite(terrainCellSize) || terrainCellSize <= 0.0) {
        return result;
    }

    result.exactCoordinateMapping = true;
    result.rawHeadingAfter = originalAiSignedWord(
        static_cast<int64_t>(rawHeading) + rawTurnCommand);
    const BattleOriginalAiPlanarPoseStepDiagnostic rawStep =
        battleOriginalAiPlanarPoseStep(
            0, 0, result.rawHeadingAfter, rawSpeed, 0, 0);
    result.rawDeltaX = rawStep.speedDeltaX;
    result.rawDeltaZ = rawStep.speedDeltaZ;
    constexpr double kOriginalGridCellSize = 512.0;
    const double scale = terrainCellSize / kOriginalGridCellSize;
    result.worldDeltaX = static_cast<double>(result.rawDeltaX) * scale;
    result.worldDeltaZ = -static_cast<double>(result.rawDeltaZ) * scale;
    result.worldXAfter = worldX + result.worldDeltaX;
    result.worldZAfter = worldZ + result.worldDeltaZ;
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kTwoPi = 2.0 * kPi;
    result.worldHeadingRadiansAfter = std::remainder(
        static_cast<double>(result.rawHeadingAfter) *
            kTwoPi / 65536.0 + kPi,
        kTwoPi);
    result.worldForwardSpeedPerSecond =
        static_cast<double>(rawSpeed) * scale /
        result.originalUpdateSeconds;
    return result;
}

BattleOriginalAiMovementTargetSelectionDiagnostic
battleOriginalAiMovementTargetSelection23bf(
    int16_t shooterSideWord,
    int16_t shooterAggregateWord16,
    const std::vector<BattleOriginalAiMovementCandidateDiagnosticInput>&
        candidates) {
    BattleOriginalAiMovementTargetSelectionDiagnostic result;
    result.shooterSideWord = shooterSideWord;
    result.shooterAggregateWord16 = shooterAggregateWord16;
    result.slotContractValid = candidates.size() <= 8u;
    if (!result.slotContractValid) {
        return result;
    }

    const auto arithmeticShiftRightWord = [](int16_t value, uint8_t count) {
        const int32_t divisor = 1 << count;
        const int32_t promoted = value;
        return static_cast<int16_t>(
            promoted >= 0
                ? promoted / divisor
                : -((-promoted + divisor - 1) / divisor));
    };
    const auto absoluteAggregateDifference = [](int16_t value) {
        if (value == std::numeric_limits<int16_t>::min()) {
            return static_cast<uint16_t>(0x7fffu);
        }
        return static_cast<uint16_t>(
            value < 0 ? -static_cast<int32_t>(value) : value);
    };

    for (size_t slot = 0; slot < candidates.size(); ++slot) {
        const BattleOriginalAiMovementCandidateDiagnosticInput& candidate =
            candidates[slot];
        if (!candidate.liveSlotOccupied ||
            candidate.sideWord == shooterSideWord ||
            candidate.cachedPoseStateWord2c == 0 ||
            candidate.aggregateWord16 < 1) {
            continue;
        }
        if (!candidate.distanceMatrixValueExact) {
            return result;
        }
        auto& score = result.candidateScores[slot];
        score.eligible = true;
        score.slot = static_cast<uint8_t>(slot);
        score.approximateDistance = candidate.approximateDistance;
        score.distanceLowWord = static_cast<uint16_t>(
            candidate.approximateDistance & 0xffffu);
        ++result.eligibleCandidateCount;

        const int64_t candidateDistanceSigned =
            candidate.approximateDistance <=
                    static_cast<uint32_t>(std::numeric_limits<int32_t>::max())
                ? static_cast<int64_t>(candidate.approximateDistance)
                : static_cast<int64_t>(candidate.approximateDistance) -
                      0x100000000ll;
        const int64_t currentDistanceSigned = static_cast<int16_t>(
            result.maximumDistanceLowWord);
        if (candidateDistanceSigned >= currentDistanceSigned) {
            result.maximumDistanceLowWord = score.distanceLowWord;
        }

        const int16_t aggregateDifference = originalAiSignedWord(
            static_cast<int64_t>(shooterAggregateWord16) -
            candidate.aggregateWord16);
        const uint16_t magnitude =
            absoluteAggregateDifference(aggregateDifference);
        if (static_cast<int16_t>(magnitude) >=
            result.maximumAggregateMagnitudeWord) {
            result.maximumAggregateMagnitudeWord =
                static_cast<int16_t>(magnitude);
        }
    }

    if (result.eligibleCandidateCount == 0u) {
        result.exact = true;
        result.writesState46Zero = true;
        return result;
    }

    result.exact = true;
    for (size_t slot = 0; slot < candidates.size(); ++slot) {
        auto& score = result.candidateScores[slot];
        if (!score.eligible) {
            continue;
        }
        const auto& candidate = candidates[slot];
        score.selectionCountWord1c =
            candidate.priorSelectionCountWord1c;
        const int16_t aggregateOperand =
            candidate.priorSelectionCountWord1c == 0
                ? candidate.aggregateWord16
                : 0;
        score.aggregateDifferenceWord = originalAiSignedWord(
            static_cast<int64_t>(shooterAggregateWord16) -
            aggregateOperand);
        score.absoluteAggregateDifference =
            absoluteAggregateDifference(score.aggregateDifferenceWord);
        if (score.aggregateDifferenceWord <= -100) {
            score.rejectedByMinus100Gate = true;
            continue;
        }

        const int16_t aggregateDifferenceFromMaximum =
            originalAiSignedWord(
                static_cast<int64_t>(result.maximumAggregateMagnitudeWord) -
                score.absoluteAggregateDifference);
        score.aggregateTerm = arithmeticShiftRightWord(
            aggregateDifferenceFromMaximum,
            score.aggregateDifferenceWord < 0 ? 5u : 6u);
        score.selectionPenalty = originalAiSignedWord(
            static_cast<int64_t>(candidate.priorSelectionCountWord1c) *
            0x80);
        const int16_t distanceDifference = originalAiSignedWord(
            static_cast<int64_t>(result.maximumDistanceLowWord) -
            score.distanceLowWord);
        score.distanceTerm =
            arithmeticShiftRightWord(distanceDifference, 7u);
        int16_t candidateScore = originalAiSignedWord(
            static_cast<int64_t>(score.aggregateTerm) -
            score.selectionPenalty);
        candidateScore = originalAiSignedWord(
            static_cast<int64_t>(candidateScore) + score.distanceTerm);
        candidateScore = originalAiSignedWord(
            static_cast<int64_t>(candidateScore) + 0x80);
        score.scoreBeforeCachedState = candidateScore;
        score.scoreHalvedByStateWord2c =
            candidate.cachedPoseStateWord2c == 1;
        if (score.scoreHalvedByStateWord2c) {
            candidateScore = arithmeticShiftRightWord(candidateScore, 1u);
        }
        score.score = candidateScore;

        if (candidateScore > result.bestScore) {
            result.bestScore = candidateScore;
            result.selected = true;
            result.selectedSlot = static_cast<uint8_t>(slot);
            result.selectedEntityId = candidate.entityId;
            result.selectedCachedRawX = candidate.cachedRawX;
            result.selectedCachedRawY = candidate.cachedRawY;
            result.selectedCachedRawZ = candidate.cachedRawZ;
            result.selectedCountBefore =
                candidate.priorSelectionCountWord1c;
            score.replacedWinner = true;
        }
    }

    if (!result.selected || result.bestScore < 1) {
        result.selected = false;
        result.selectedSlot = 0xffu;
        result.selectedEntityId = {};
        result.writesState46One = true;
        return result;
    }

    result.selectedCountAfter = originalAiSignedWord(
        static_cast<int64_t>(result.selectedCountBefore) + 1);
    result.incrementsSelectedWord1c = true;
    return result;
}

BattleOriginalAiOpenRelationSelectionDiagnostic
battleOriginalAiMovementTargetSelection23bfWithOpenRelation(
    int16_t shooterSideWord,
    int16_t shooterAggregateWord16,
    const std::vector<BattleOriginalAiMovementCandidateDiagnosticInput>&
        candidates,
    uint8_t openSlot,
    int16_t eligibleStateWord2c) {
    BattleOriginalAiOpenRelationSelectionDiagnostic result;
    result.openSlot = openSlot;
    result.eligibleStateWord2c = eligibleStateWord2c;
    if (candidates.size() > 8u || openSlot >= candidates.size()) {
        return result;
    }
    result.openSlotValid = true;
    if (eligibleStateWord2c == 0) {
        return result;
    }

    auto relationZeroInputs = candidates;
    auto relationNonzeroInputs = candidates;
    relationZeroInputs[openSlot].cachedPoseStateWord2c = 0;
    relationNonzeroInputs[openSlot].cachedPoseStateWord2c =
        eligibleStateWord2c;
    result.relationZero = battleOriginalAiMovementTargetSelection23bf(
        shooterSideWord,
        shooterAggregateWord16,
        relationZeroInputs);
    result.relationNonzero = battleOriginalAiMovementTargetSelection23bf(
        shooterSideWord,
        shooterAggregateWord16,
        relationNonzeroInputs);
    result.exact =
        result.relationZero.exact && result.relationNonzero.exact;
    if (!result.exact || !result.relationZero.selected ||
        !result.relationNonzero.selected ||
        result.relationZero.selectedSlot == openSlot ||
        result.relationNonzero.selectedSlot == openSlot) {
        return result;
    }

    const auto& zero = result.relationZero;
    const auto& nonzero = result.relationNonzero;
    result.targetSelectionIndependent =
        zero.selectedSlot == nonzero.selectedSlot &&
        zero.selectedEntityId == nonzero.selectedEntityId &&
        zero.selectedCachedRawX == nonzero.selectedCachedRawX &&
        zero.selectedCachedRawY == nonzero.selectedCachedRawY &&
        zero.selectedCachedRawZ == nonzero.selectedCachedRawZ &&
        zero.selectedCountAfter == nonzero.selectedCountAfter &&
        zero.incrementsSelectedWord1c ==
            nonzero.incrementsSelectedWord1c;
    if (!result.targetSelectionIndependent) {
        return result;
    }
    result.selectedSlot = zero.selectedSlot;
    result.selectedEntityId = zero.selectedEntityId;
    result.selectedCachedRawX = zero.selectedCachedRawX;
    result.selectedCachedRawY = zero.selectedCachedRawY;
    result.selectedCachedRawZ = zero.selectedCachedRawZ;
    result.selectedCountAfter = zero.selectedCountAfter;
    return result;
}

BattleOriginalAiSingleMovementTargetDiagnostic
battleOriginalAiSingleMovementTarget(
    int16_t shooterSideWord,
    int16_t shooterAggregateWord16,
    const std::vector<BattleOriginalAiMovementCandidateDiagnosticInput>&
        candidates) {
    BattleOriginalAiSingleMovementTargetDiagnostic result;
    result.shooterSideWord = shooterSideWord;
    result.shooterAggregateWord16 = shooterAggregateWord16;
    result.slotContractValid = candidates.size() <= 8u;
    if (!result.slotContractValid) {
        return result;
    }

    std::vector<BattleOriginalAiMovementCandidateDiagnosticInput> exactInputs =
        candidates;
    size_t eligibleCount = 0u;
    size_t eligibleSlot = 0u;
    for (size_t slot = 0; slot < exactInputs.size(); ++slot) {
        auto& candidate = exactInputs[slot];
        if (!candidate.liveSlotOccupied ||
            candidate.sideWord == shooterSideWord ||
            candidate.cachedPoseStateWord2c == 0 ||
            candidate.aggregateWord16 < 1) {
            continue;
        }
        ++eligibleCount;
        eligibleSlot = slot;
        candidate.distanceMatrixValueExact = true;
        candidate.approximateDistance = 0u;
    }
    result.eligibleCandidateCount = static_cast<uint8_t>(eligibleCount);
    if (eligibleCount > 1u) {
        result.multipleCandidatesOpen = true;
        return result;
    }
    if (eligibleCount == 1u &&
        exactInputs[eligibleSlot].priorSelectionCountWord1c != 0) {
        result.priorSelectionCountOpen = true;
        return result;
    }

    const BattleOriginalAiMovementTargetSelectionDiagnostic general =
        battleOriginalAiMovementTargetSelection23bf(
            shooterSideWord, shooterAggregateWord16, exactInputs);
    result.exact = general.exact;
    result.selected = general.selected;
    result.selectedSlot = general.selectedSlot;
    result.selectedEntityId = general.selectedEntityId;
    result.selectedCachedRawX = general.selectedCachedRawX;
    result.selectedCachedRawY = general.selectedCachedRawY;
    result.selectedCachedRawZ = general.selectedCachedRawZ;
    result.score = general.bestScore;
    result.writesState46Zero = general.writesState46Zero;
    result.writesState46One = general.writesState46One;
    result.incrementsSelectedWord1c = general.incrementsSelectedWord1c;
    if (eligibleCount == 1u) {
        const auto& score = general.candidateScores[eligibleSlot];
        result.aggregateDeltaWord = score.aggregateDifferenceWord;
        result.rejectedByMinus99Gate = score.rejectedByMinus100Gate;
        result.scoreHalvedByStateWord2c =
            score.scoreHalvedByStateWord2c;
    }
    return result;
}

BattleOriginalAiSelectedTargetMovementDiagnostic
battleOriginalAiSelectedTargetMovement(
    const BattleTerrainCollisionGrid& grid,
    const BattleOriginalTerrainSceneCatalog& catalog,
    const BattleOriginalAiSelectedTargetMovementInput& input) {
    BattleOriginalAiSelectedTargetMovementDiagnostic result;
    const bool liveCombatantTarget =
        input.selectedTargetOwnership ==
        BattleOriginalAiMovementTargetOwnership::LiveCombatant;
    if (input.numericState51 != 0 || input.movingSlot >= 8u ||
        input.liveSlots.size() > 8u ||
        (liveCombatantTarget
             ? !isValid(input.selectedTargetEntityId)
             : isValid(input.selectedTargetEntityId))) {
        return result;
    }
    result.ordinaryStateAccepted = true;
    result.targetAcquisitionClosed = !liveCombatantTarget;

    const int32_t targetDeltaX = originalAiSignedDword(
        static_cast<int64_t>(input.selectedTargetRawX) - input.rawX);
    const int32_t targetDeltaZ = originalAiSignedDword(
        static_cast<int64_t>(input.selectedTargetRawZ) - input.rawZ);
    result.movementTarget = battleOriginalAiMovementTarget(
        targetDeltaX, targetDeltaZ, input.numericState51, input.heading);
    if (!result.movementTarget.exact) {
        return result;
    }

    int16_t finalDesiredHeading = input.heading;
    int16_t movementMode = result.movementTarget.movementMode;
    if (result.movementTarget.routesThroughSteeringProbes) {
        for (size_t slot = 0; slot < input.liveSlots.size(); ++slot) {
            const auto& candidate = input.liveSlots[slot];
            if (slot == input.movingSlot || !candidate.occupied) {
                continue;
            }
            const BattleOriginalAiOrientedLaneDiagnostic lane =
                battleOriginalAiOrientedLane(
                    originalAiSignedDword(
                        static_cast<int64_t>(candidate.rawX) - input.rawX),
                    originalAiSignedDword(
                        static_cast<int64_t>(candidate.rawZ) - input.rawZ),
                    input.heading,
                    800u);
            if (!lane.exact) {
                return result;
            }
            if (lane.insideLane) {
                result.liveObjectLaneBlocked = true;
                result.blockingLiveObjectEntityId = candidate.entityId;
                break;
            }
        }
        result.liveObjectLaneDomainClosed = true;
        if (input.objectiveLaneEnabled) {
            const BattleOriginalAiOrientedLaneDiagnostic objectiveLane =
                battleOriginalAiOrientedLane(
                    originalAiSignedDword(
                        static_cast<int64_t>(input.objectiveRawX) - input.rawX),
                    originalAiSignedDword(
                        static_cast<int64_t>(input.objectiveRawZ) - input.rawZ),
                    input.heading,
                    0x05dcu);
            if (!objectiveLane.exact) {
                return result;
            }
            result.objectiveLaneBlocked = objectiveLane.insideLane;
            result.objectiveLaneDomainClosed = true;
        } else {
            result.objectiveLaneDomainClosed = true;
        }

        result.probeEndpointDomainClosed = true;
        for (size_t probe = 0; probe < result.terrainProbes.size(); ++probe) {
            if (result.liveObjectLaneBlocked || result.objectiveLaneBlocked) {
                result.probeBlocked[probe] = true;
                continue;
            }
            result.terrainProbes[probe] = battleOriginalAiTerrainProbe(
                grid,
                input.rawX,
                input.rawZ,
                result.movementTarget.desiredHeadingBeforeSteering,
                static_cast<uint8_t>(probe));
            const BattleOriginalAiTerrainProbeDiagnostic& terrain =
                result.terrainProbes[probe];
            if (!terrain.exact) {
                result.probeEndpointDomainClosed = false;
                return result;
            }
            if (terrain.blocked) {
                result.probeBlocked[probe] = true;
                continue;
            }
            if (terrain.visitedStepCount != 4u) {
                result.probeEndpointDomainClosed = false;
                return result;
            }
            const int32_t endpointX = terrain.rawProbeX.back();
            const int32_t endpointZ = terrain.rawProbeZ.back();
            if (!originalAiFixedMagnitudeBelow(endpointX, 0xac81u) ||
                !originalAiFixedMagnitudeBelow(endpointZ, 0x5dc1u)) {
                result.probeEndpointDomainClosed = false;
                result.boundaryRelationOpen = true;
                return result;
            }
            result.probeBlocked[probe] = terrain.blocked;
        }

        result.steering = battleOriginalAiSteeringChoice(
            result.movementTarget.desiredHeadingBeforeSteering,
            true,
            input.selectorWord3a,
            input.selectorWord3c,
            result.probeBlocked);
        if (!result.steering.exact) {
            return result;
        }
        finalDesiredHeading = result.steering.finalDesiredHeading;
        movementMode = result.steering.movementMode;
    }

    result.speedCommand = battleOriginalAiRawSpeedCommand(
        input.definitionWord06,
        input.heatSpeedPenalty,
        input.leftLegActuatorWord,
        input.rightLegActuatorWord,
        movementMode,
        input.rawSpeed);
    result.turnCommand = battleOriginalAiBodyTurnCommand(
        finalDesiredHeading, input.heading);
    if (!result.speedCommand.exact || !result.turnCommand.exact) {
        return result;
    }

    const auto preliminary = battleOriginalAiOrdinaryPoseTransaction(
        catalog,
        input.rawX,
        input.rawY,
        input.rawZ,
        input.pitch,
        input.roll,
        input.heading,
        input.controlWord30,
        input.controlWord32,
        result.turnCommand.turnCommand,
        input.driftWord36,
        input.driftWord38,
        input.driftWord3a,
        result.speedCommand.nextRawSpeed,
        input.definitionHeightWord02,
        input.previousSlotProbeWord,
        0u,
        input.controlState4c,
        input.controlState53,
        input.cachedSceneObjectIndex,
        input.cachedSubrecordIndex);
    if (!preliminary.tentativeStep.exact ||
        !preliminary.sceneQuery.exact ||
        !preliminary.sceneQuery.catalogAvailable) {
        return result;
    }

    uint16_t nextSlotProbeWord = input.previousSlotProbeWord;
    if (result.speedCommand.nextRawSpeed != 0) {
        BattleOriginalAiSlotProbeDiagnosticInput slotInput;
        slotInput.movingSlot = input.movingSlot;
        slotInput.controlWord00 = input.controlWord00;
        slotInput.controlState46 = input.controlState46;
        slotInput.movingOwnerWord = input.movingOwnerWord;
        slotInput.rawSpeed = result.speedCommand.nextRawSpeed;
        slotInput.tentativeRawX = preliminary.tentativeStep.xAfter;
        slotInput.tentativeRawY =
            preliminary.sceneQuery.hit &&
                    preliminary.sceneQuery.selectedPriority == 0u
                ? preliminary.sceneQuery.worldHeight
                : 0;
        slotInput.tentativeRawZ = preliminary.tentativeStep.zAfter;
        slotInput.cachedDestinationRawX = input.selectedTargetRawX;
        slotInput.cachedDestinationRawY = input.selectedTargetRawY;
        slotInput.cachedDestinationRawZ = input.selectedTargetRawZ;
        slotInput.liveSlots = input.liveSlots;
        slotInput.objectiveInactiveBitSet = input.objectiveInactiveBitSet;
        slotInput.opaqueGateWordEd0 = input.opaqueObjectiveGateWordEd0;
        slotInput.objectiveRawX = input.objectiveRawX;
        slotInput.objectiveRawZ = input.objectiveRawZ;
        result.slotProbe = battleOriginalAiSlotProbe(slotInput);
        if (!result.slotProbe.exact) {
            return result;
        }
        nextSlotProbeWord = result.slotProbe.resultWord;
    }

    result.poseTransaction = battleOriginalAiOrdinaryPoseTransaction(
        catalog,
        input.rawX,
        input.rawY,
        input.rawZ,
        input.pitch,
        input.roll,
        input.heading,
        input.controlWord30,
        input.controlWord32,
        result.turnCommand.turnCommand,
        input.driftWord36,
        input.driftWord38,
        input.driftWord3a,
        result.speedCommand.nextRawSpeed,
        input.definitionHeightWord02,
        input.previousSlotProbeWord,
        nextSlotProbeWord,
        input.controlState4c,
        input.controlState53,
        input.cachedSceneObjectIndex,
        input.cachedSubrecordIndex);
    result.exact = result.poseTransaction.exact;
    return result;
}

BattleOriginalAiZeroAttitudePoseStepDiagnostic
battleOriginalAiZeroAttitudePoseStep(
    int32_t x,
    int32_t y,
    int32_t z,
    int16_t rawSpeed,
    int16_t driftX,
    int16_t driftY,
    int16_t driftZ) {
    BattleOriginalAiZeroAttitudePoseStepDiagnostic result;
    result.exact = true;
    result.xBefore = x;
    result.yBefore = y;
    result.zBefore = z;
    result.rawSpeed = rawSpeed;
    result.driftX = driftX;
    result.driftY = driftY;
    result.driftZ = driftZ;
    const BattleOriginalAiPlanarPoseStepDiagnostic planar =
        battleOriginalAiPlanarPoseStep(
            x, z, 0, rawSpeed, driftX, driftZ);
    result.xAfter = planar.xAfter;
    result.zAfter = planar.zAfter;
    result.yAfter = originalAiSignedDword(
        static_cast<int64_t>(y) + driftY);
    return result;
}

void BattleWorld::updateOriginalCombatAiRequests() {
    if (params_.combatAiPolicy !=
        BattleCombatAiPolicy::OriginalBtechStationarySingleTargetFire) {
        return;
    }

    constexpr std::array<int, 5> kRangeModifiers{{6, 2, 4, 6, 12}};
    for (Combatant& shooter : combatants_) {
        if (shooter.playerControlled ||
            shooter.roster.team != BattleTeam::Opposing) {
            continue;
        }
        shooter.originalAiUpdateAccumulator +=
            params_.fixedTickSeconds / kOriginalAiUpdateSeconds;
        if (shooter.originalAiUpdateAccumulator < 1.0) {
            continue;
        }
        shooter.originalAiUpdateAccumulator -= 1.0;
        ++shooter.originalAiUpdateCount;
        shooter.fireWeaponRequested = false;

        BattleOriginalAiDecisionDiagnostic decision;
        decision.valid = true;
        decision.sequence = ++shooter.originalAiDecisionSequence;
        decision.originalUpdateCount = shooter.originalAiUpdateCount;
        decision.decisionTickIndex = tickIndex_;
        decision.shooterEntityId = shooter.id;
        decision.provenance =
            "BTECH.EXE:FUN_1000_71e1/7d2e/a3ee/b961/a660/c74e/"
            "1AA6_0953_table/a79f/a86d/aae0:"
            "stationary_integer_zero_pitch_3d_target_and_friendly_lane:"
            "0.1s_cadence_bridge:"
            "deterministic_non_original_prng";

        if (!originalAiStationary(shooter)) {
            decision.result = BattleOriginalAiDecisionResult::
                RejectedShooterNotStationary;
            shooter.lastOriginalAiDecision = std::move(decision);
            continue;
        }
        if (!combatantCanFireWeapon(params_, shooter)) {
            decision.result = BattleOriginalAiDecisionResult::
                RejectedShooterOffline;
            shooter.lastOriginalAiDecision = std::move(decision);
            continue;
        }

        bool readyWeaponRangeOpen = false;
        for (const BattleWeaponInstanceState& weapon : shooter.weapons) {
            if (!weapon.functional || weapon.cooldownTicksRemaining != 0u) {
                continue;
            }
            if (!weapon.originalRangeValueProven) {
                readyWeaponRangeOpen = true;
                break;
            }
            if (decision.readyFunctionalWeaponCount <
                std::numeric_limits<uint8_t>::max()) {
                ++decision.readyFunctionalWeaponCount;
            }
            decision.maximumReadyRangeWord = std::max(
                decision.maximumReadyRangeWord,
                weapon.originalRangeWords.back());
        }
        decision.targetSearchRadius =
            static_cast<double>(decision.maximumReadyRangeWord) *
                kOriginalWeaponRangeScale -
            400.0;
        if (readyWeaponRangeOpen ||
            decision.readyFunctionalWeaponCount == 0u) {
            decision.result = BattleOriginalAiDecisionResult::
                RejectedNoReadyWeaponAcquisition;
            shooter.lastOriginalAiDecision = std::move(decision);
            continue;
        }

        Combatant* target = nullptr;
        size_t targetCount = 0;
        for (Combatant& candidate : combatants_) {
            if (candidate.id == shooter.id ||
                candidate.roster.team != BattleTeam::Player ||
                candidate.missionStatus != CombatantMissionStatus::Active ||
                combatantMechDestroyed(candidate)) {
                continue;
            }
            target = &candidate;
            ++targetCount;
        }
        if (targetCount != 1u || target == nullptr) {
            decision.result = BattleOriginalAiDecisionResult::
                RejectedTargetCardinality;
            shooter.lastOriginalAiDecision = std::move(decision);
            continue;
        }
        decision.targetKind = BattleShotTargetKind::Combatant;
        decision.targetEntityId = target->id;
        if (!originalAiStationary(*target)) {
            decision.result = BattleOriginalAiDecisionResult::
                RejectedTargetNotStationary;
            shooter.lastOriginalAiDecision = std::move(decision);
            continue;
        }

        Combatant* selectedTarget = nullptr;
        double bestTargetDistance = decision.targetSearchRadius;
        bool candidateSetOpen = false;
        bool candidateAimGeometryOpen = false;
        bool targetInsideSearchRadius = false;
        bool targetInsideAimEnvelope = false;
        bool targetInsideVerticalEnvelope = false;
        bool targetInsidePlanarEnvelope = false;
        bool centerlineSubset = true;
        for (Combatant& candidate : combatants_) {
            if (candidate.id == shooter.id) {
                continue;
            }
            const bool sameSide =
                candidate.roster.team == shooter.roster.team;
            const bool ordinaryTarget = &candidate == target;
            if (!sameSide && !ordinaryTarget) {
                candidateSetOpen = true;
                break;
            }
            if (sameSide &&
                (candidate.missionStatus != CombatantMissionStatus::Active ||
                 combatantMechDestroyed(candidate) ||
                 !originalAiStationary(candidate))) {
                candidateSetOpen = true;
                break;
            }

            if (shooter.aimPitchStep != 0 || shooter.torsoYawStep != 0) {
                candidateAimGeometryOpen = true;
                break;
            }
            const BattleOriginalAiCandidateGeometryDiagnostic geometry =
                battleOriginalAiZeroPitchCandidateGeometry(
                    shooter.transform, candidate.transform);
            if (!geometry.exact) {
                candidateAimGeometryOpen = true;
                break;
            }
            const double candidateDistance =
                static_cast<double>(geometry.distance);
            if (ordinaryTarget) {
                decision.targetDistance = candidateDistance;
                decision.originalApproximateDistanceProven = true;
                targetInsideSearchRadius =
                    candidateDistance < decision.targetSearchRadius;
                targetInsideVerticalEnvelope =
                    geometry.verticalEnvelopePassed;
                targetInsidePlanarEnvelope =
                    geometry.planarEnvelopePassed;
            }
            if (!(candidateDistance < bestTargetDistance)) {
                continue;
            }
            centerlineSubset = centerlineSubset && geometry.centerline;
            if (!geometry.insideEnvelope) {
                continue;
            }

            bestTargetDistance = candidateDistance;
            decision.shooterFixedHeading = geometry.shooterHeading;
            decision.selectedCandidateFixedHeading =
                geometry.candidateHeading;
            decision.selectedCandidateYawDelta = geometry.yawDelta;
            decision.selectedCandidateHalfWidth = geometry.planarHalfWidth;
            decision.selectedCandidateVerticalAngle =
                geometry.verticalAngle;
            decision.selectedCandidateVerticalDelta =
                geometry.verticalDelta;
            decision.selectedCandidateVerticalLimit =
                geometry.verticalLimit;
            decision.selectedCandidateVerticalGatePassed =
                geometry.verticalEnvelopePassed;
            if (sameSide) {
                selectedTarget = nullptr;
                decision.blockingCombatantEntityId = candidate.id;
                decision.blockingCombatantDistance = candidateDistance;
                decision.blockingCandidateFlags = 1u;
                decision.friendlyFireLaneSuppressed = true;
            } else {
                selectedTarget = &candidate;
                targetInsideAimEnvelope = true;
                decision.blockingCombatantEntityId = {};
                decision.blockingCombatantDistance = 0.0;
                decision.blockingCandidateFlags = 0u;
                decision.friendlyFireLaneSuppressed = false;
            }
        }
        if (candidateSetOpen) {
            decision.result = BattleOriginalAiDecisionResult::
                RejectedCandidateStateOpen;
            shooter.lastOriginalAiDecision = std::move(decision);
            continue;
        }
        if (candidateAimGeometryOpen) {
            decision.result = BattleOriginalAiDecisionResult::
                RejectedCandidateAimGeometryOpen;
            shooter.lastOriginalAiDecision = std::move(decision);
            continue;
        }
        decision.targetSelectionCenterlineSubsetProven = centerlineSubset;
        decision.targetSelectionPlanarFixedAngleProven = true;
        decision.targetSelectionZeroPitchVerticalFixedAngleProven = true;
        if (selectedTarget == nullptr) {
            if (decision.friendlyFireLaneSuppressed) {
                decision.result =
                    BattleOriginalAiDecisionResult::RejectedFriendlyFireLane;
            } else if (!targetInsideSearchRadius) {
                decision.result = BattleOriginalAiDecisionResult::
                    RejectedTargetAcquisitionRange;
            } else if (!targetInsideVerticalEnvelope) {
                decision.result = BattleOriginalAiDecisionResult::
                    RejectedTargetVerticalEnvelope;
            } else if (!targetInsidePlanarEnvelope) {
                decision.result = BattleOriginalAiDecisionResult::
                    RejectedTargetAimEnvelope;
            } else if (!targetInsideAimEnvelope) {
                decision.result = BattleOriginalAiDecisionResult::
                    RejectedTargetAimEnvelope;
            } else {
                decision.result = BattleOriginalAiDecisionResult::
                    RejectedTargetAcquisitionRange;
            }
            shooter.lastOriginalAiDecision = std::move(decision);
            continue;
        }
        target = selectedTarget;
        decision.targetAcquisitionRangePassed = true;

        BattleWeaponInstanceState* selected = nullptr;
        int bestScore = 0;
        BattleOriginalAiDecisionResult lastGate =
            BattleOriginalAiDecisionResult::RejectedNoEligibleWeapon;
        const auto rememberFirstGate =
            [&lastGate](BattleOriginalAiDecisionResult gate) {
                if (lastGate == BattleOriginalAiDecisionResult::
                        RejectedNoEligibleWeapon) {
                    lastGate = gate;
                }
            };
        for (uint8_t slot = 0; slot < 10u; ++slot) {
            const auto found = std::find_if(
                shooter.weapons.begin(),
                shooter.weapons.end(),
                [slot](const BattleWeaponInstanceState& weapon) {
                    return weapon.slotIndex == slot;
                });
            if (found == shooter.weapons.end()) {
                continue;
            }
            BattleWeaponInstanceState& weapon = *found;
            if (lastGate == BattleOriginalAiDecisionResult::
                    RejectedNoEligibleWeapon) {
                decision.minimumRange =
                    static_cast<double>(weapon.originalRangeWords.front()) *
                    kOriginalWeaponRangeScale;
                decision.maximumRange =
                    static_cast<double>(weapon.originalRangeWords.back()) *
                    kOriginalWeaponRangeScale;
            }
            if (!weapon.functional ||
                weapon.readiness == BattleWeaponReadiness::NonFunctional) {
                rememberFirstGate(BattleOriginalAiDecisionResult::
                    RejectedWeaponNonFunctional);
                continue;
            }
            if (weapon.cooldownTicksRemaining != 0u ||
                weapon.readiness == BattleWeaponReadiness::Cooldown) {
                rememberFirstGate(BattleOriginalAiDecisionResult::
                    RejectedWeaponCooldown);
                continue;
            }
            if (weapon.ammunitionOwnership ==
                    BattleWeaponAmmunitionOwnership::SharedAmmunitionPool &&
                (!weapon.ammunitionStateKnown ||
                 weapon.ammunitionRemaining <= 0)) {
                rememberFirstGate(BattleOriginalAiDecisionResult::
                    RejectedWeaponNoAmmunition);
                continue;
            }
            const bool rangePassed = weapon.originalRangeValueProven &&
                decision.minimumRange < decision.targetDistance &&
                decision.targetDistance < decision.maximumRange;
            if (!rangePassed) {
                rememberFirstGate(BattleOriginalAiDecisionResult::
                    RejectedWeaponOutOfRange);
                continue;
            }
            if (!weapon.originalHeatValueProven ||
                shooter.heat.rawHeat + static_cast<int>(weapon.originalHeat) >=
                    kOriginalAiHeatGate) {
                rememberFirstGate(BattleOriginalAiDecisionResult::
                    RejectedWeaponHeatGate);
                continue;
            }
            if (!weapon.originalDamageValueProven) {
                rememberFirstGate(BattleOriginalAiDecisionResult::
                    RejectedNoEligibleWeapon);
                continue;
            }

            // FUN_1000_a79f adds cbca(3). Its global seed/interleaving is open,
            // so the first slice uses the neutral jitter while retaining the
            // strict greater-than replacement and thus the original tie order.
            const int score = static_cast<int>(weapon.originalDamage);
            if (bestScore < score) {
                bestScore = score;
                selected = &weapon;
            }
        }

        if (selected == nullptr) {
            decision.result = lastGate;
            shooter.lastOriginalAiDecision = std::move(decision);
            continue;
        }

        decision.weaponInstanceId = selected->weaponInstanceId;
        decision.minimumRange =
            static_cast<double>(selected->originalRangeWords.front()) *
            kOriginalWeaponRangeScale;
        decision.maximumRange =
            static_cast<double>(selected->originalRangeWords.back()) *
            kOriginalWeaponRangeScale;
        decision.strictRangeGatePassed = true;
        decision.weaponScore = static_cast<int16_t>(bestScore);
        decision.rangeBucket = originalAiRangeBucket(
            *selected, decision.targetDistance);
        const int rangeModifier = kRangeModifiers[
            std::min<size_t>(decision.rangeBucket, 4u)];
        constexpr int kOriginalDefaultShooterBase = 4;
        int targetNumber = kOriginalDefaultShooterBase + rangeModifier - 4;
        targetNumber += static_cast<int>(
            std::abs(target->forwardSpeed)) / 30;
        if (target->forwardSpeed == 0.0) {
            targetNumber -= 4;
        }
        decision.targetNumber = static_cast<int16_t>(targetNumber);
        const uint8_t firstHitDie = originalAiD6(
            shooter, *target, decision.sequence, 0u);
        const uint8_t secondHitDie = originalAiD6(
            shooter, *target, decision.sequence, 1u);
        decision.hitRoll = static_cast<uint8_t>(firstHitDie + secondHitDie);
        decision.hit = decision.hitRoll >= decision.targetNumber;
        decision.attackAspect = originalAiAttackAspect(shooter, *target);
        if (decision.hit) {
            const uint8_t firstLocationDie = originalAiD6(
                shooter, *target, decision.sequence, 2u);
            const uint8_t secondLocationDie = originalAiD6(
                shooter, *target, decision.sequence, 3u);
            decision.locationRollIndex = static_cast<uint8_t>(
                firstLocationDie + secondLocationDie - 2u);
            decision.armorSection = originalAiLocation(
                decision.attackAspect, decision.locationRollIndex);
        }
        decision.automaticCallerAndLocationTableProven = true;
        decision.originalRandomSequenceProven = false;
        decision.result = BattleOriginalAiDecisionResult::FireRequested;

        shooter.selectedWeaponInstanceId = selected->weaponInstanceId;
        shooter.fireWeaponRequested = true;
        shooter.lastOriginalAiDecision = std::move(decision);
    }
}

void BattleWorld::updateBattleProjectiles() {
    if (!params_.originalProjectileRuntimeEnabled || projectiles_.empty()) {
        return;
    }

    // The original projectile routine performs as many as five movement
    // substeps per battle update. The replacement's established compatibility
    // cadence treats one original update as 0.1 s; that wall-clock mapping and
    // the world-unit scale remain explicitly provisional.
    constexpr double kOriginalUpdateSeconds = 0.1;
    constexpr double kMovementSubstepsPerOriginalUpdate = 5.0;
    const double originalUpdatesThisTick =
        params_.fixedTickSeconds / kOriginalUpdateSeconds;

    for (BattleProjectileState& projectile : projectiles_) {
        if (!projectile.valid) {
            continue;
        }

        projectile.lifetimeUpdateAccumulator += originalUpdatesThisTick;
        while (projectile.lifetimeUpdateAccumulator >= 1.0 &&
               projectile.originalLifetimeUpdatesRemaining > 0u) {
            projectile.lifetimeUpdateAccumulator -= 1.0;
            --projectile.originalLifetimeUpdatesRemaining;
        }
        if (projectile.originalLifetimeUpdatesRemaining == 0u) {
            BattleShotDiagnostic expired = projectile.pendingShot;
            expired.deliveryState = BattleShotDeliveryState::ProjectileExpired;
            expired.impactTickIndex = tickIndex_;
            expired.projectileId = projectile.projectileId;
            expired.impactDamage = 0;
            expired.damageApplied = 0;
            if (expired.hit) {
                expired.hit = false;
                expired.result = BattleShotResult::MissUnresolved;
            }
            expired.provenance +=
                ":projectile_expired_without_authoritative_impact";
            shotEvents_.push_back(expired);
            lastShot_ = std::move(expired);
            projectile.valid = false;
            continue;
        }

        projectile.movementSubstepAccumulator +=
            originalUpdatesThisTick * kMovementSubstepsPerOriginalUpdate;
        int movementSubsteps = static_cast<int>(
            std::floor(projectile.movementSubstepAccumulator));
        projectile.movementSubstepAccumulator -= movementSubsteps;

        bool impacted = false;
        for (int substep = 0; substep < movementSubsteps && !impacted;
             ++substep) {
            BattleHitPoint targetPoint{};
            Combatant* target = nullptr;
            bool targetAvailable = false;
            if (projectile.targetBound &&
                projectile.targetKind == BattleShotTargetKind::Combatant) {
                target = findCombatant(projectile.targetEntityId);
                targetAvailable = target != nullptr &&
                    target->missionStatus == CombatantMissionStatus::Active &&
                    !combatantMechDestroyed(*target);
                if (targetAvailable) {
                    targetPoint = targetLocalPointToWorld(
                        projectile.targetLocalPoint, target->transform);
                }
            } else if (projectile.targetBound &&
                       projectile.targetKind ==
                           BattleShotTargetKind::Objective) {
                targetAvailable = objectiveCanTakeDamage(objective_);
                if (targetAvailable) {
                    targetPoint = targetLocalPointToWorld(
                        projectile.targetLocalPoint, objective_.transform);
                }
            }
            if (projectile.targetBound && !targetAvailable) {
                projectile.targetBound = false;
                projectile.pendingShot.provenance +=
                    ":bound_target_lost_projectile_continues_straight";
            }

            double targetDistance = std::numeric_limits<double>::max();
            if (targetAvailable) {
                const BattleHitPoint targetVector =
                    subtract(targetPoint, projectile.position);
                targetDistance = std::sqrt(dot(targetVector, targetVector));
                projectile.direction = steerProjectileDirection(
                    projectile.direction, targetVector);
            }

            const double movementDistance =
                static_cast<double>(projectile.originalSpeedRaw);
            const BattleHitPoint nextPosition = add(
                projectile.position,
                multiply(projectile.direction, movementDistance));
            const BattleProjectileModelPose startModelPose =
                battleProjectileModelPose(
                    projectile, targetAvailable, targetPoint);
            BattleProjectileState nextProjectile = projectile;
            nextProjectile.position = nextPosition;
            const BattleProjectileModelPose endModelPose =
                battleProjectileModelPose(
                    nextProjectile, targetAvailable, targetPoint);
            const BattleProjectileTerrainHit terrainHit =
                projectileTerrainContact(
                    params_, startModelPose.position, endModelPose.position);
            const bool targetWithinSubstep =
                targetAvailable && targetDistance <= movementDistance;
            const double targetFraction = targetWithinSubstep &&
                    movementDistance > 0.0
                ? targetDistance / movementDistance
                : std::numeric_limits<double>::max();
            if (terrainHit.valid &&
                (!targetWithinSubstep ||
                 terrainHit.segmentFraction + 1.0e-9 < targetFraction)) {
                projectile.previousPosition = projectile.position;
                projectile.position = terrainHit.point;
                BattleShotDiagnostic impact = projectile.pendingShot;
                impact.targetKind = BattleShotTargetKind::Terrain;
                impact.targetEntityId = {};
                impact.targetProjectileId = 0u;
                impact.result = BattleShotResult::HitTerrain;
                impact.hit = true;
                impact.deliveryState =
                    BattleShotDeliveryState::ProjectileTerrainImpact;
                impact.impactTickIndex = tickIndex_;
                impact.projectileId = projectile.projectileId;
                impact.impactPoint = terrainHit.point;
                impact.impactDamage = 0u;
                impact.damageBefore = 0;
                impact.damageApplied = 0;
                impact.damageAfter = 0;
                impact.runtimeEffect = BattleShotRuntimeEffect::
                    ProjectileTerrainSmokeNoDamage;
                impact.projectileMotionTimingProven = false;
                impact.provenance += terrainHit.obstacleId != 0u
                    ? ":projectile_swept_WLD_TERPCK_prism_contact_" +
                        std::to_string(terrainHit.obstacleId) + "_record_" +
                        std::to_string(terrainHit.recordIndex)
                    : ":projectile_ground_plane_contact";
                shotEvents_.push_back(impact);
                lastShot_ = std::move(impact);
                projectile.valid = false;
                impacted = true;
                continue;
            }

            if (targetWithinSubstep) {
                projectile.previousPosition = projectile.position;
                projectile.position = targetPoint;
                BattleShotDiagnostic impact = projectile.pendingShot;
                impact.deliveryState =
                    BattleShotDeliveryState::ProjectileImpact;
                impact.impactTickIndex = tickIndex_;
                impact.projectileId = projectile.projectileId;
                impact.impactPoint = targetPoint;
                impact.projectileMotionTimingProven = false;
                const int damage = projectileImpactDamage(
                    projectile, tickIndex_, impact);
                impact.impactDamage = static_cast<uint16_t>(
                    std::max(0, damage));
                Combatant* attacker = findCombatant(
                    projectile.shooterEntityId);
                if (attacker != nullptr && target != nullptr) {
                    applyDetailedCombatantDamage(
                        params_, *attacker, *target, damage, impact);
                } else if (attacker != nullptr &&
                           projectile.targetKind ==
                               BattleShotTargetKind::Objective) {
                    impact.runtimeEffect =
                        BattleShotRuntimeEffect::ObjectiveDamage;
                    impact.damageBefore = objective_.damage;
                    applyDeterministicObjectiveDamage(
                        *attacker, objective_, damage);
                    impact.damageAfter = objective_.damage;
                    impact.damageApplied =
                        impact.damageAfter - impact.damageBefore;
                } else {
                    impact.hit = false;
                    impact.result = BattleShotResult::MissUnresolved;
                    impact.provenance +=
                        ":impact_attribution_target_unavailable";
                }
                impact.provenance +=
                    ":BTECH_5654_578b_5d76_battle_owned_projectile";
                shotEvents_.push_back(impact);
                lastShot_ = std::move(impact);
                projectile.valid = false;
                impacted = true;
                continue;
            }

            projectile.previousPosition = projectile.position;
            projectile.position = nextPosition;
        }
    }

    projectiles_.erase(
        std::remove_if(
            projectiles_.begin(),
            projectiles_.end(),
            [](const BattleProjectileState& projectile) {
                return !projectile.valid;
            }),
        projectiles_.end());
}

void BattleWorld::updateImpactEventHistory() {
    for (const BattleShotDiagnostic& shot : shotEvents_) {
        const bool ordinaryImpact =
            shot.result == BattleShotResult::Hit &&
            (shot.targetKind == BattleShotTargetKind::Combatant ||
             shot.targetKind == BattleShotTargetKind::Objective) &&
            (shot.deliveryState == BattleShotDeliveryState::Immediate ||
             shot.deliveryState ==
                 BattleShotDeliveryState::ProjectileImpact);
        const bool terrainImpact =
            shot.result == BattleShotResult::HitTerrain &&
            shot.targetKind == BattleShotTargetKind::Terrain &&
            shot.deliveryState ==
                BattleShotDeliveryState::ProjectileTerrainImpact;
        const bool projectileInterception =
            shot.result == BattleShotResult::HitProjectile &&
            shot.targetKind == BattleShotTargetKind::Projectile &&
            shot.deliveryState ==
                BattleShotDeliveryState::ProjectileIntercepted;
        if (!shot.valid || !shot.hit ||
            (!ordinaryImpact && !terrainImpact &&
             !projectileInterception) ||
            shot.impactTickIndex != tickIndex_) {
            continue;
        }
        bool machineGunImpact = false;
        if (shot.deliveryState == BattleShotDeliveryState::Immediate) {
            const Combatant* shooter = findCombatant(shot.shooterEntityId);
            if (shooter == nullptr) {
                continue;
            }
            const auto weapon = std::find_if(
                shooter->weapons.begin(),
                shooter->weapons.end(),
                [&shot](const BattleWeaponInstanceState& candidate) {
                    return candidate.weaponInstanceId == shot.weaponInstanceId;
                });
            if (weapon == shooter->weapons.end() ||
                (weapon->weaponTypeId != "small_laser" &&
                 weapon->weaponTypeId != "medium_laser" &&
                 weapon->weaponTypeId != "large_laser" &&
                 weapon->weaponTypeId != "ppc" &&
                 weapon->weaponTypeId != "machine_gun")) {
                // Only original immediate-delivery weapon classes with an
                // identified OTHPCK temporary-impact sequence enter history.
                continue;
            }
            machineGunImpact = weapon->weaponTypeId == "machine_gun";
        }
        const auto duplicate = std::find_if(
            impactEvents_.begin(),
            impactEvents_.end(),
            [&shot](const BattleImpactEvent& event) {
                return event.shotSequence == shot.sequence;
            });
        if (duplicate != impactEvents_.end()) {
            continue;
        }
        BattleImpactEvent event;
        event.valid = true;
        event.shotSequence = shot.sequence;
        event.impactTickIndex = shot.impactTickIndex;
        event.shooterEntityId = shot.shooterEntityId;
        event.weaponInstanceId = shot.weaponInstanceId;
        event.targetKind = shot.targetKind;
        event.targetEntityId = shot.targetEntityId;
        event.projectileId = projectileInterception
            ? shot.targetProjectileId
            : shot.projectileId;
        event.deliveryState = shot.deliveryState;
        if (terrainImpact) {
            event.visualSequence =
                BattleImpactVisualSequence::TerrainSmokeLastThree;
        } else if (machineGunImpact) {
            event.visualSequence = BattleImpactVisualSequence::
                OriginalMachineGunTwoStage;
        }
        event.point = shot.impactPoint;
        impactEvents_.push_back(event);
    }
}

void BattleWorld::updateDeterministicCombatRuntime() {
    if (!params_.deterministicCombatRuntimeEnabled) {
        return;
    }
    if (params_.weaponRange < 0.0 ||
        params_.weaponDamagePerHit <= 0 ||
        params_.weaponCooldownTicks == 0 ||
        params_.mechSystemMaxDamage <= 0) {
        throw std::runtime_error("battle deterministic combat parameters are invalid");
    }
    if (params_.deterministicObjectiveRuntimeEnabled &&
        (objective_.maxDamage <= 0 || params_.objectiveMaxDamage <= 0)) {
        throw std::runtime_error("battle deterministic objective parameters are invalid");
    }

    if (params_.individualWeaponRuntimeEnabled) {
        updateIndividualWeaponRuntime();
        return;
    }

    for (Combatant& combatant : combatants_) {
        if (combatant.weaponCooldownTicksRemaining > 0) {
            --combatant.weaponCooldownTicksRemaining;
        }
    }

    for (Combatant& attacker : combatants_) {
        if (!combatantCanFireWeapon(params_, attacker) ||
            attacker.weaponCooldownTicksRemaining > 0) {
            attacker.fireWeaponRequested = false;
            continue;
        }

        Combatant* target = nullptr;
        bool targetObjective = false;
        if (attacker.playerControlled && attacker.fireWeaponRequested) {
            double bestDistance = params_.weaponRange;
            for (Combatant& candidate : combatants_) {
                if (candidate.roster.team != BattleTeam::Opposing ||
                    candidate.missionStatus != CombatantMissionStatus::Active ||
                    combatantMechDestroyed(candidate)) {
                    continue;
                }
                const double distance = distance2d(attacker.transform, candidate.transform);
                if (distance <= bestDistance) {
                    bestDistance = distance;
                    target = &candidate;
                }
            }
        } else if (!attacker.playerControlled &&
                   attacker.enemyAiState == BattleEnemyAiState::Attack &&
                   attacker.enemyAiTargetKind == BattleEnemyAiTargetKind::Player &&
                   isValid(attacker.enemyAiTargetEntityId)) {
            Combatant* candidate = findCombatant(attacker.enemyAiTargetEntityId);
            if (candidate != nullptr &&
                candidate->missionStatus == CombatantMissionStatus::Active &&
                !combatantMechDestroyed(*candidate) &&
                distance2d(attacker.transform, candidate->transform) <= params_.weaponRange) {
                target = candidate;
            }
        } else if (!attacker.playerControlled &&
                   params_.deterministicObjectiveRuntimeEnabled &&
                   attacker.enemyAiState == BattleEnemyAiState::Attack &&
                   attacker.enemyAiTargetKind == BattleEnemyAiTargetKind::Objective &&
                   objectiveCanTakeDamage(objective_) &&
                   distance2d(attacker.transform, objective_.transform) <= params_.weaponRange) {
            targetObjective = true;
        }

        if (target != nullptr) {
            attacker.weaponShotsFired += 1;
            attacker.weaponCooldownTicksRemaining = params_.weaponCooldownTicks;
            applyDeterministicSystemDamage(
                params_,
                attacker,
                *target,
                BattleMechSystemRole::Core);
        } else if (targetObjective) {
            attacker.weaponShotsFired += 1;
            attacker.weaponCooldownTicksRemaining = params_.weaponCooldownTicks;
            applyDeterministicObjectiveDamage(
                attacker,
                objective_,
                params_.weaponDamagePerHit);
        }
        attacker.fireWeaponRequested = false;
    }
}

void BattleWorld::updateReplacementCombatAiRequests() {
    if (params_.combatAiPolicy !=
        BattleCombatAiPolicy::ReplacementDeterministicCombatAi) {
        return;
    }
    for (Combatant& shooter : combatants_) {
        BattleReplacementAiState& state = shooter.replacementAi;
        if (!state.active ||
            state.targetKind == BattleReplacementAiTargetKind::None ||
            state.selectedWeaponInstanceId == 0u ||
            tickIndex_ < state.nextFireDecisionTickIndex) {
            continue;
        }
        state.friendlyLaneBlocked = false;
        const Combatant* target = nullptr;
        const Transform* targetTransform = nullptr;
        if (state.targetKind == BattleReplacementAiTargetKind::Combatant) {
            target = findCombatant(state.combatTargetEntityId);
            if (target == nullptr ||
                target->roster.team == shooter.roster.team ||
                target->missionStatus != CombatantMissionStatus::Active ||
                combatantMechDestroyed(*target)) {
                continue;
            }
            targetTransform = &target->transform;
        } else if (state.targetKind ==
                   BattleReplacementAiTargetKind::Objective) {
            if (state.missionSlice !=
                    BattleReplacementAiMissionSlice::ProtectGarrisonDefense ||
                shooter.roster.team != BattleTeam::Opposing ||
                !objectiveCanTakeDamage(objective_) ||
                !objective_.damagePolicyProven ||
                objective_.damageSuppressed) {
                continue;
            }
            targetTransform = &objective_.transform;
        }
        if (targetTransform == nullptr ||
            !combatantCanFireWeapon(params_, shooter)) {
            continue;
        }
        const auto weapon = std::find_if(
            shooter.weapons.begin(), shooter.weapons.end(),
            [&state](const BattleWeaponInstanceState& candidate) {
                return candidate.weaponInstanceId ==
                    state.selectedWeaponInstanceId;
            });
        if (weapon == shooter.weapons.end() ||
            weapon->readiness != BattleWeaponReadiness::Ready ||
            !weapon->originalRangeValueProven) {
            continue;
        }
        const double dx = targetTransform->x - shooter.transform.x;
        const double dz = targetTransform->z - shooter.transform.z;
        const double distance = std::hypot(dx, dz);
        if (distance <= 0.0 || distance >= weapon->maximumRange) {
            continue;
        }
        if (!params_.stationaryTargetHitDiagnosticEnabled) {
            if (shooter.heat.enabled &&
                (!weapon->originalHeatValueProven ||
                 shooter.heat.rawHeat +
                         static_cast<int>(weapon->originalHeat) >=
                     kReplacementAiHeatGate)) {
                continue;
            }
            const ReplacementAiAimSolution aim = replacementAiAimSolution(
                params_,
                shooter,
                *weapon,
                target,
                target == nullptr ? &objective_ : nullptr);
            state.terrainLineOfSightBlocked =
                aim.valid && !aim.terrainLineOfSightClear;
            state.aimAligned = aim.valid && aim.aimAligned;
            state.lastAimErrorRadians = aim.aimErrorRadians;
            if (!aim.valid || !aim.terrainLineOfSightClear ||
                !aim.aimAligned) {
                continue;
            }
        }
        for (const Combatant& friendly : combatants_) {
            if (friendly.id == shooter.id ||
                friendly.roster.team != shooter.roster.team ||
                friendly.missionStatus != CombatantMissionStatus::Active ||
                combatantMechDestroyed(friendly)) {
                continue;
            }
            const double fx = friendly.transform.x - shooter.transform.x;
            const double fz = friendly.transform.z - shooter.transform.z;
            const double along = (fx * dx + fz * dz) / distance;
            const double cross = std::abs(fx * dz - fz * dx) / distance;
            if (along > 0.0 && along < distance &&
                cross <= friendly.collisionRadiusWorld + 100.0) {
                state.friendlyLaneBlocked = true;
                break;
            }
        }
        if (state.friendlyLaneBlocked) {
            continue;
        }
        shooter.selectedWeaponInstanceId = weapon->weaponInstanceId;
        shooter.fireWeaponRequested = true;
        state.nextFireDecisionTickIndex = tickIndex_ +
            std::max<uint64_t>(
                1u,
                static_cast<uint64_t>(std::ceil(
                    0.4 / params_.fixedTickSeconds)));
    }
}

void BattleWorld::updateIndividualWeaponRuntime() {
    for (Combatant& combatant : combatants_) {
        combatant.heat.lastWeaponHeatAdded = 0;
        for (BattleWeaponInstanceState& weapon : combatant.weapons) {
            if (weapon.cooldownTicksRemaining > 0) {
                --weapon.cooldownTicksRemaining;
            }
        }
        refreshCombatantWeaponState(params_, combatant);
    }

    updateOriginalCombatAiRequests();
    updateReplacementCombatAiRequests();

    // Existing projectiles resolve before this tick's new fire requests, so a
    // launch can never move or impact on its own launch tick.
    updateBattleProjectiles();

    for (Combatant& attacker : combatants_) {
        if (!attacker.fireWeaponRequested) {
            continue;
        }
        attacker.lastFireRequestTickIndex = tickIndex_;
        attacker.lastFireRequestWeaponInstanceId =
            attacker.selectedWeaponInstanceId;
        attacker.lastFireRequestAccepted = false;

        const auto selected = std::find_if(
            attacker.weapons.begin(),
            attacker.weapons.end(),
            [&attacker](const BattleWeaponInstanceState& weapon) {
                return weapon.weaponInstanceId ==
                    attacker.selectedWeaponInstanceId;
            });
        if (selected == attacker.weapons.end() ||
            selected->readiness != BattleWeaponReadiness::Ready) {
            attacker.fireWeaponRequested = false;
            continue;
        }

        BattleWeaponInstanceState& weapon = *selected;
        attacker.lastFireRequestAccepted = true;
        const bool originalAiShot =
            params_.combatAiPolicy ==
                BattleCombatAiPolicy::OriginalBtechStationarySingleTargetFire &&
            attacker.lastOriginalAiDecision.valid &&
            attacker.lastOriginalAiDecision.result ==
                BattleOriginalAiDecisionResult::FireRequested &&
            attacker.lastOriginalAiDecision.decisionTickIndex == tickIndex_ &&
            attacker.lastOriginalAiDecision.weaponInstanceId ==
                weapon.weaponInstanceId;
        const bool replacementAiShot =
            params_.combatAiPolicy ==
                BattleCombatAiPolicy::ReplacementDeterministicCombatAi &&
            attacker.replacementAi.active &&
            !attacker.playerControlled &&
            attacker.replacementAi.selectedWeaponInstanceId ==
                weapon.weaponInstanceId &&
            attacker.replacementAi.targetKind !=
                BattleReplacementAiTargetKind::None;
        if (originalAiShot) {
            attacker.lastOriginalAiDecision.fireTickIndex = tickIndex_;
        }
        attacker.weaponShotsFired += 1;
        weapon.shotsFired += 1;
        weapon.cooldownTicksRemaining = weapon.cooldownTicksOnFire;
        if (attacker.heat.enabled) {
            attacker.heat.lastWeaponHeatAdded =
                static_cast<int>(weapon.originalHeat);
            attacker.heat.rawHeat = std::clamp(
                attacker.heat.rawHeat +
                    attacker.heat.lastWeaponHeatAdded,
                0,
                battleMaximumRawHeat());
        }
        if (weapon.ammunitionOwnership ==
            BattleWeaponAmmunitionOwnership::SharedAmmunitionPool) {
            const size_t poolIndex =
                static_cast<size_t>(weapon.ammunitionPoolIndex);
            if (poolIndex < attacker.ammunitionByPool.size() &&
                attacker.ammunitionByPool[poolIndex] > 0) {
                --attacker.ammunitionByPool[poolIndex];
            }
        }

        BattleShotDiagnostic shot;
        shot.valid = true;
        shot.sequence = ++shotSequence_;
        shot.tickIndex = tickIndex_;
        shot.shooterEntityId = attacker.id;
        shot.weaponInstanceId = weapon.weaponInstanceId;
        shot.maximumRange = weapon.maximumRange;
        shot.originalDamage = weapon.originalDamage;
        shot.originalDamageValueProven = weapon.originalDamageValueProven;
        if (originalAiShot) {
            shot.originalAiDecision = attacker.lastOriginalAiDecision;
        }

        Combatant* target = nullptr;
        bool targetObjective = false;
        bool targetPolicyRejected = false;
        bool crosshairRayPolicy = false;
        BattleMechRayHit geometryHit;
        BattleProjectileState* interceptedProjectile = nullptr;
        BattleProjectileRayHit projectileGeometryHit;
        BattleAimRay aimRay;
        bool aimRayAvailable = false;
        ReplacementAiAimSolution replacementAim;
        std::optional<ReplacementAiHitResolution> replacementHit;
        if (originalAiShot) {
            target = findCombatant(
                attacker.lastOriginalAiDecision.targetEntityId);
            if (target == nullptr ||
                target->missionStatus != CombatantMissionStatus::Active ||
                combatantMechDestroyed(*target)) {
                target = nullptr;
                targetPolicyRejected = true;
            }
            shot.provenance =
                "phase13_original_stationary_single_target_ai_common_pipeline";
        } else if (replacementAiShot) {
            if (attacker.replacementAi.targetKind ==
                BattleReplacementAiTargetKind::Objective) {
                targetObjective = objectiveCanTakeDamage(objective_) &&
                    objective_.damagePolicyProven &&
                    !objective_.damageSuppressed &&
                    attacker.roster.team == BattleTeam::Opposing &&
                    attacker.replacementAi.missionSlice ==
                        BattleReplacementAiMissionSlice::
                            ProtectGarrisonDefense;
                targetPolicyRejected = !targetObjective;
                shot.provenance =
                    "replacement_phase14_protect_objective_assault_common_pipeline";
            } else {
                target = findCombatant(
                    attacker.replacementAi.combatTargetEntityId);
                if (target == nullptr ||
                    target->roster.team == attacker.roster.team ||
                    target->missionStatus != CombatantMissionStatus::Active ||
                    combatantMechDestroyed(*target)) {
                    target = nullptr;
                    targetPolicyRejected = true;
                }
            }
            if (attacker.replacementAi.targetKind ==
                    BattleReplacementAiTargetKind::Combatant &&
                attacker.replacementAi.missionSlice ==
                BattleReplacementAiMissionSlice::DeathmatchEngagement) {
                shot.provenance =
                    "replacement_phase14_balanced_opponent_common_pipeline";
            } else if (attacker.replacementAi.targetKind ==
                           BattleReplacementAiTargetKind::Combatant &&
                       attacker.replacementAi.missionSlice ==
                       BattleReplacementAiMissionSlice::
                           ProtectGarrisonDefense) {
                shot.provenance = attacker.roster.team == BattleTeam::Opposing
                    ? "replacement_phase14_garrison_assault_common_pipeline"
                    : "replacement_phase14_assigned_defense_threat_common_pipeline";
            } else if (attacker.replacementAi.targetKind ==
                       BattleReplacementAiTargetKind::Combatant) {
                shot.provenance =
                    "replacement_phase14_assigned_obstruction_common_pipeline";
            }
        } else if (params_.weaponTargetPolicy ==
             BattleWeaponTargetPolicy::SelectedScannerTargetProvisional) {
            if (targetScan_.selectedTargetKind ==
                    BattleTargetScanTargetKind::Combatant) {
                target = findCombatant(targetScan_.selectedTargetEntityId);
                if (target == nullptr ||
                    target->roster.team != BattleTeam::Opposing ||
                    target->missionStatus != CombatantMissionStatus::Active ||
                    combatantMechDestroyed(*target)) {
                    target = nullptr;
                    targetPolicyRejected = true;
                }
            } else if (targetScan_.selectedTargetKind ==
                           BattleTargetScanTargetKind::Objective) {
                targetObjective = objectiveCanTakeDamage(objective_) &&
                    objective_.damagePolicyProven &&
                    !objective_.damageSuppressed;
                targetPolicyRejected = !targetObjective;
            }
            shot.provenance =
                "phase12_selected_scanner_target_provisional_stationary_lab";
        } else if (params_.weaponTargetPolicy ==
                   BattleWeaponTargetPolicy::
                       CrosshairRayVisibleCombatantProvisional ||
                   params_.weaponTargetPolicy ==
                       BattleWeaponTargetPolicy::
                           SelectedScannerTargetCrosshairRayProvisional) {
            crosshairRayPolicy = true;
            if (attacker.playerControlled) {
                if (targetScan_.selectedTargetKind ==
                               BattleTargetScanTargetKind::Objective) {
                    targetObjective = objectiveCanTakeDamage(objective_) &&
                        objective_.damagePolicyProven &&
                        !objective_.damageSuppressed;
                }
            } else if (attacker.enemyAiTargetKind ==
                           BattleEnemyAiTargetKind::Objective) {
                targetObjective = objectiveCanTakeDamage(objective_) &&
                    objective_.damagePolicyProven &&
                    !objective_.damageSuppressed;
            }
            shot.provenance =
                "phase12_crosshair_visible_combatant_component_ray_"
                "provisional_projection";
        } else if (params_.weaponTargetPolicy ==
                   BattleWeaponTargetPolicy::LegacyNearestOpposingDiagnostic) {
            double nearestDistance = std::numeric_limits<double>::max();
            for (Combatant& candidate : combatants_) {
                if (candidate.roster.team != BattleTeam::Opposing ||
                    candidate.missionStatus != CombatantMissionStatus::Active ||
                    combatantMechDestroyed(candidate)) {
                    continue;
                }
                const double distance =
                    distance2d(attacker.transform, candidate.transform);
                if (distance < nearestDistance) {
                    nearestDistance = distance;
                    target = &candidate;
                }
            }
            shot.provenance =
                "phase12_typed_legacy_nearest_opposing_diagnostic";
        } else {
            targetPolicyRejected = true;
            shot.provenance = "phase12_unresolved_target_policy_fail_closed";
        }

        if (replacementAiShot &&
            !params_.stationaryTargetHitDiagnosticEnabled &&
            !targetPolicyRejected) {
            replacementAim = replacementAiAimSolution(
                params_,
                attacker,
                weapon,
                target,
                targetObjective ? &objective_ : nullptr);
            shot.replacementFireSolution = replacementAim.valid;
            shot.replacementTerrainLineOfSightClear =
                replacementAim.valid &&
                replacementAim.terrainLineOfSightClear;
            shot.replacementAimAligned =
                replacementAim.valid && replacementAim.aimAligned;
            shot.replacementGunnerySkill = attacker.roster.gunnerySkill;
            if (replacementAim.valid) {
                shot.aimOrigin = replacementAim.origin;
                shot.aimDirection = replacementAim.direction;
                shot.impactPoint = replacementAim.targetPoint;
                shot.aimProjectionProven = false;
            }
            if (!replacementAim.valid ||
                !replacementAim.terrainLineOfSightClear ||
                !replacementAim.aimAligned) {
                targetPolicyRejected = true;
                shot.provenance +=
                    ":replacement_fire_solution_changed_before_delivery";
            }
        }

        if (crosshairRayPolicy && !targetObjective) {
            const BattleMechHitProfile* shooterProfile =
                hitProfileForPreset(params_, attacker.mechPresetId);
            if (shooterProfile == nullptr) {
                targetPolicyRejected = true;
                shot.provenance += ":missing_shooter_catalog_hit_profile";
            } else {
                const BattleAimRay ray =
                    crosshairAimRay(params_, attacker, *shooterProfile);
                aimRay = ray;
                aimRayAvailable = true;
                shot.aimOrigin = ray.origin;
                shot.aimDirection = ray.direction;
                shot.aimProjectionProven = false;
                double nearestRayDistance =
                    std::numeric_limits<double>::max();
                for (Combatant& candidate : combatants_) {
                    if (candidate.id == attacker.id ||
                        (!attacker.playerControlled &&
                         candidate.roster.team == attacker.roster.team) ||
                        candidate.missionStatus !=
                            CombatantMissionStatus::Active ||
                        combatantMechDestroyed(candidate)) {
                        continue;
                    }
                    const BattleMechHitProfile* candidateProfile =
                        hitProfileForPreset(params_, candidate.mechPresetId);
                    if (candidateProfile == nullptr) {
                        continue;
                    }
                    const BattleMechRayHit candidateHit =
                        raycastMech(ray, candidate, *candidateProfile);
                    if (!candidateHit.valid ||
                        candidateHit.distance >= nearestRayDistance) {
                        continue;
                    }
                    nearestRayDistance = candidateHit.distance;
                    target = &candidate;
                    geometryHit = candidateHit;
                    shot.hitGeometryFingerprint =
                        candidateProfile->geometryFingerprint;
                    shot.impactPoint = candidateHit.worldPoint;
                    shot.hitComponentId =
                        candidateHit.triangle->componentId;
                    if (candidateHit.triangle->torsoComposite) {
                        shot.hitLocation = BattleShotHitLocation::
                            TorsoPartitionProvisional;
                        shot.hitLocationProven = false;
                        shot.armorSection = provisionalTorsoSection(
                            candidateHit, *candidateProfile);
                    } else {
                        shot.hitLocation = BattleShotHitLocation::
                            VisibleComponentOriginalIdentity;
                        shot.hitLocationProven = true;
                        shot.armorSection = candidateHit.triangle->
                            directArmorSection;
                    }
                    shot.affectedSystem =
                        systemRoleForArmorSection(shot.armorSection);
                }
                if (attacker.playerControlled && target != nullptr &&
                    target->roster.team == attacker.roster.team) {
                    shot.provenance +=
                        ":BTECH_47f7_player_visible_friendly_component";
                }
            }
        }

        const bool laserInterceptionWeapon =
            weapon.weaponTypeId == "small_laser" ||
            weapon.weaponTypeId == "medium_laser" ||
            weapon.weaponTypeId == "large_laser";
        if (crosshairRayPolicy && laserInterceptionWeapon) {
            if (!aimRayAvailable) {
                const BattleMechHitProfile* shooterProfile =
                    hitProfileForPreset(params_, attacker.mechPresetId);
                if (shooterProfile != nullptr) {
                    aimRay = crosshairAimRay(
                        params_, attacker, *shooterProfile);
                    aimRayAvailable = true;
                    shot.aimOrigin = aimRay.origin;
                    shot.aimDirection = aimRay.direction;
                    shot.aimProjectionProven = false;
                }
            }
            if (aimRayAvailable) {
                double nearestRayDistance = geometryHit.valid
                    ? geometryHit.distance
                    : std::numeric_limits<double>::max();
                for (BattleProjectileState& projectile : projectiles_) {
                    if (!projectile.valid || !projectile.laserInterceptible) {
                        continue;
                    }
                    const BattleProjectileHitProfile* profile =
                        projectileHitProfileForVisualClass(
                            params_, projectile.originalVisualClass);
                    if (profile == nullptr || profile->triangles.empty()) {
                        continue;
                    }
                    bool projectileTargetPointValid = false;
                    BattleHitPoint projectileTargetPoint;
                    if (projectile.targetBound &&
                        projectile.targetKind ==
                            BattleShotTargetKind::Combatant) {
                        const Combatant* projectileTarget = findCombatant(
                            projectile.targetEntityId);
                        if (projectileTarget != nullptr) {
                            projectileTargetPoint = targetLocalPointToWorld(
                                projectile.targetLocalPoint,
                                projectileTarget->transform);
                            projectileTargetPointValid = true;
                        }
                    } else if (projectile.targetBound &&
                               projectile.targetKind ==
                                   BattleShotTargetKind::Objective &&
                               objective_.valid) {
                        projectileTargetPoint = targetLocalPointToWorld(
                            projectile.targetLocalPoint,
                            objective_.transform);
                        projectileTargetPointValid = true;
                    }
                    const BattleProjectileModelPose modelPose =
                        battleProjectileModelPose(
                            projectile,
                            projectileTargetPointValid,
                            projectileTargetPoint);
                    const BattleProjectileRayHit candidateHit =
                        raycastProjectileModel(
                            aimRay,
                            projectile,
                            *profile,
                            modelPose,
                            tickIndex_);
                    if (!candidateHit.valid ||
                        candidateHit.distance >= nearestRayDistance) {
                        continue;
                    }
                    nearestRayDistance = candidateHit.distance;
                    interceptedProjectile = &projectile;
                    projectileGeometryHit = candidateHit;
                    target = nullptr;
                    targetObjective = false;
                    geometryHit = {};
                    shot.hitGeometryFingerprint =
                        profile->geometryFingerprint;
                    shot.impactPoint = candidateHit.worldPoint;
                    shot.hitComponentId = -1;
                    shot.hitLocation = BattleShotHitLocation::
                        UnresolvedOriginalDistribution;
                    shot.hitLocationProven = false;
                    shot.affectedSystem = BattleMechSystemRole::Unknown;
                }
            }
        }

        if (interceptedProjectile != nullptr) {
            shot.targetKind = BattleShotTargetKind::Projectile;
            shot.targetEntityId = {};
            shot.targetProjectileId =
                interceptedProjectile->projectileId;
            shot.targetDistance = projectileGeometryHit.distance;
            shot.provenance +=
                ":laser_ray_exact_OTHPCK_missile_model_intersection_"
                "spin_compatibility_timing";
        } else if (target != nullptr) {
            shot.targetKind = BattleShotTargetKind::Combatant;
            shot.targetEntityId = target->id;
            shot.targetDistance = originalAiShot
                ? attacker.lastOriginalAiDecision.targetDistance
                : distance2d(attacker.transform, target->transform);
        } else if (targetObjective) {
            shot.targetKind = BattleShotTargetKind::Objective;
            shot.targetDistance = distance2d(
                attacker.transform,
                objective_.transform);
        }

        if (replacementAiShot &&
            !params_.stationaryTargetHitDiagnosticEnabled &&
            !targetPolicyRejected &&
            shot.targetKind != BattleShotTargetKind::None) {
            replacementHit = replacementAiHitResolution(
                attacker,
                target,
                weapon,
                shot.targetDistance,
                attacker.replacementAi.decisionSequence);
            shot.replacementTargetNumber = replacementHit->targetNumber;
            shot.replacementHitRoll = replacementHit->hitRoll;
        }

        if (targetPolicyRejected) {
            shot.result = BattleShotResult::RejectedTargetPolicy;
        } else if (crosshairRayPolicy && !targetObjective &&
                   target == nullptr && interceptedProjectile == nullptr) {
            shot.result = BattleShotResult::MissCrosshairRay;
            shot.provenance += ":ray_missed_visible_component_mesh";
        } else if (shot.targetKind == BattleShotTargetKind::None) {
            shot.result = BattleShotResult::RejectedNoTarget;
        } else if (originalAiShot) {
            shot.rangeGatePassed =
                shot.originalAiDecision.strictRangeGatePassed &&
                shot.originalAiDecision.minimumRange < shot.targetDistance &&
                shot.targetDistance < shot.originalAiDecision.maximumRange;
            if (!shot.rangeGatePassed) {
                shot.result = BattleShotResult::MissOutOfRange;
            } else {
                shot.hit = shot.originalAiDecision.hit;
                shot.result = shot.hit ? BattleShotResult::Hit
                                       : BattleShotResult::MissUnresolved;
            }
        } else {
            shot.rangeGatePassed = shot.targetDistance <= shot.maximumRange;
            if (!shot.rangeGatePassed) {
                shot.result = BattleShotResult::MissOutOfRange;
            } else if (crosshairRayPolicy && target != nullptr &&
                       !geometryHit.valid) {
                shot.result = BattleShotResult::MissCrosshairRay;
                shot.provenance += ":ray_missed_visible_component_mesh";
            } else if (crosshairRayPolicy) {
                // Objective ray geometry remains separate; preserve the
                // existing proven objective ownership while mech aiming moves
                // to component geometry.
                shot.result = BattleShotResult::Hit;
                shot.hit = true;
            } else if (replacementHit.has_value()) {
                shot.hit = replacementHit->hit;
                shot.result = shot.hit ? BattleShotResult::Hit
                                       : BattleShotResult::MissUnresolved;
                if (!shot.hit) {
                    const uint64_t targetIdentity = target != nullptr
                        ? static_cast<uint64_t>(target->id.value) : 0xffffu;
                    shot.aimDirection = replacementAiMissDirection(
                        replacementAim.direction,
                        attacker,
                        targetIdentity,
                        attacker.replacementAi.decisionSequence,
                        replacementHit->targetNumber);
                    shot.impactPoint = {};
                }
                shot.provenance +=
                    ":replacement_gunnery_range_motion_2d6";
            } else if (!params_.stationaryTargetHitDiagnosticEnabled) {
                shot.result = BattleShotResult::MissUnresolved;
            } else {
                shot.result = BattleShotResult::Hit;
                shot.hit = true;
            }
        }

        if (shot.hit && target != nullptr) {
            if (originalAiShot) {
                shot.hitLocation =
                    BattleShotHitLocation::OriginalAiAspect2d6Table;
                shot.hitLocationProven = true;
                shot.armorSection =
                    shot.originalAiDecision.armorSection;
                shot.affectedSystem =
                    systemRoleForArmorSection(shot.armorSection);
            } else if (replacementHit.has_value()) {
                shot.hitLocation =
                    BattleShotHitLocation::OriginalAiAspect2d6Table;
                shot.hitLocationProven = true;
                shot.armorSection = replacementHit->armorSection;
                shot.affectedSystem =
                    systemRoleForArmorSection(shot.armorSection);
            } else if (!crosshairRayPolicy) {
                shot.hitLocation =
                    BattleShotHitLocation::
                        CenterTorsoProvisionalStationaryLab;
                shot.hitLocationProven = false;
                shot.armorSection =
                    mech3d::MechArmorSectionId::CenterTorso;
                shot.affectedSystem = BattleMechSystemRole::Core;
            }
        }

        const bool originalProjectileDelivery =
            params_.originalProjectileRuntimeEnabled &&
            weapon.deliveryMode ==
                BattleWeaponDeliveryMode::OriginalProjectilePool &&
            weapon.originalProjectileDefinitionProven &&
            weapon.originalProjectileTypeIndex >= 0 &&
            weapon.originalProjectileSpeedRaw > 0u &&
            weapon.originalProjectileLifetimeUpdates > 0u;
        const bool projectileLaunchRequested = originalProjectileDelivery &&
            (shot.hit ||
             shot.result == BattleShotResult::MissOutOfRange ||
             shot.result == BattleShotResult::MissCrosshairRay ||
             shot.result == BattleShotResult::MissUnresolved);
        constexpr size_t kOriginalProjectilePoolCapacity = 32u;
        if (projectileLaunchRequested &&
            projectiles_.size() >= kOriginalProjectilePoolCapacity) {
            shot.hit = false;
            shot.result = BattleShotResult::RejectedProjectilePoolFull;
            shot.deliveryState = BattleShotDeliveryState::None;
            shot.provenance +=
                ":BTECH_5654_32_record_pool_full_fail_closed";
        } else if (projectileLaunchRequested) {
            BattleProjectileState projectile;
            projectile.valid = true;
            projectile.projectileId = nextProjectileId_++;
            projectile.launchTickIndex = tickIndex_;
            projectile.shooterEntityId = attacker.id;
            projectile.weaponInstanceId = weapon.weaponInstanceId;
            projectile.weaponTypeId = weapon.weaponTypeId;
            projectile.launchShooterTransform = attacker.transform;
            projectile.launchTorsoYawRadians = torsoYawRadians(
                params_, attacker.torsoYawStep);
            projectile.targetKind = shot.targetKind;
            projectile.targetEntityId = shot.targetEntityId;
            projectile.targetBound = shot.hit &&
                (target != nullptr || targetObjective);
            projectile.originalProjectileTypeIndex =
                weapon.originalProjectileTypeIndex;
            projectile.originalSpeedRaw =
                weapon.originalProjectileSpeedRaw;
            projectile.originalLifetimeUpdates =
                weapon.originalProjectileLifetimeUpdates;
            projectile.originalLifetimeUpdatesRemaining =
                weapon.originalProjectileLifetimeUpdates;
            projectile.originalDamageClass =
                weapon.originalProjectileDamageClass;
            projectile.originalVisualClass =
                weapon.originalProjectileVisualClass;
            const BattleProjectileHitProfile* projectileHitProfile =
                projectileHitProfileForVisualClass(
                    params_, projectile.originalVisualClass);
            if (projectileHitProfile != nullptr) {
                projectile.collisionGeometryFingerprint =
                    projectileHitProfile->geometryFingerprint;
                projectile.collisionGeometryProven =
                    projectileHitProfile->originalModelGeometryProven;
            }
            projectile.laserInterceptible =
                projectile.originalVisualClass == 1u &&
                projectileHitProfile != nullptr &&
                !projectileHitProfile->triangles.empty();
            projectile.originalTargetTrackingProven =
                projectile.targetBound;
            projectile.motionScaleProven = false;

            const BattleMechHitProfile* shooterProfile =
                hitProfileForPreset(params_, attacker.mechPresetId);
            if (shooterProfile != nullptr) {
                const std::optional<BattleHitPoint> modelMount =
                    battleModelWeaponMount(
                        *shooterProfile,
                        weapon.locationId,
                        attacker.transform,
                        projectile.launchTorsoYawRadians);
                if (modelMount.has_value()) {
                    projectile.launchModelMount = *modelMount;
                    projectile.launchModelMountValid = true;
                    projectile.launchModelMountPolicyProven = false;
                }
            }
            const double heading = attacker.transform.headingRadians +
                torsoYawRadians(params_, attacker.torsoYawStep);
            projectile.position = shot.aimOrigin;
            if (dot(projectile.position, projectile.position) <= 0.0) {
                projectile.position = {
                    attacker.transform.x +
                        std::sin(heading) *
                            params_.cockpitCameraForwardOffset,
                    attacker.transform.y +
                        (shooterProfile != nullptr
                             ? shooterProfile->aimOriginHeight
                             : 0.0),
                    attacker.transform.z +
                        std::cos(heading) *
                            params_.cockpitCameraForwardOffset,
                };
            }
            BattleHitPoint targetPoint = shot.impactPoint;
            if (target != nullptr) {
                if (dot(targetPoint, targetPoint) <= 0.0) {
                    const BattleMechHitProfile* targetProfile =
                        hitProfileForPreset(params_, target->mechPresetId);
                    targetPoint = {
                        target->transform.x,
                        target->transform.y +
                            (targetProfile != nullptr
                                 ? targetProfile->aimOriginHeight * 0.55
                                 : 0.0),
                        target->transform.z,
                    };
                }
                projectile.targetLocalPoint = worldPointToTargetLocal(
                    targetPoint, target->transform);
            } else if (targetObjective) {
                if (dot(targetPoint, targetPoint) <= 0.0) {
                    targetPoint = {
                        objective_.transform.x,
                        objective_.transform.y,
                        objective_.transform.z,
                    };
                }
                projectile.targetLocalPoint = worldPointToTargetLocal(
                    targetPoint, objective_.transform);
            }

            projectile.direction = normalized(
                subtract(targetPoint, projectile.position));
            if (!projectile.targetBound ||
                dot(projectile.direction, projectile.direction) <= 0.0) {
                projectile.direction = normalized(shot.aimDirection);
            }
            if (dot(projectile.direction, projectile.direction) <= 0.0) {
                projectile.direction = {
                    std::sin(heading), 0.0, std::cos(heading)};
            }

            // The captured OTHPCK models extend behind their local origin by
            // 45 units (AC/5 record 000) and 405 units (rocket record 001).
            // Start the model origin that far forward so its rear edge begins
            // at the established aim origin instead of behind the cockpit.
            // The resource bounds are exact; their coupling to the original
            // launch hardpoint remains an explicit compatibility policy.
            double modelRearExtent = 0.0;
            if (projectile.originalVisualClass == 0u) {
                modelRearExtent = 45.0;
            } else if (projectile.originalVisualClass == 1u) {
                modelRearExtent = 405.0;
            }
            projectile.launchForwardOffset = modelRearExtent;
            if (projectile.targetBound) {
                const BattleHitPoint remaining = subtract(
                    targetPoint,
                    projectile.position);
                const double remainingDistance = std::sqrt(dot(
                    remaining,
                    remaining));
                projectile.launchForwardOffset = std::min(
                    projectile.launchForwardOffset,
                    std::max(
                        0.0,
                        remainingDistance -
                            static_cast<double>(
                                projectile.originalSpeedRaw)));
            }
            projectile.launchForwardOffsetProven = false;
            const BattleHitPoint launchOrigin = projectile.position;
            projectile.position.x +=
                projectile.direction.x * projectile.launchForwardOffset;
            projectile.position.y +=
                projectile.direction.y * projectile.launchForwardOffset;
            projectile.position.z +=
                projectile.direction.z * projectile.launchForwardOffset;
            const BattleProjectileTerrainHit launchTerrainHit =
                projectileTerrainContact(
                    params_, launchOrigin, projectile.position);
            if (launchTerrainHit.valid) {
                projectile.launchForwardOffset *=
                    launchTerrainHit.segmentFraction;
                projectile.position = launchTerrainHit.point;
            }
            projectile.previousPosition = projectile.position;

            shot.deliveryState =
                BattleShotDeliveryState::ProjectileInFlight;
            shot.projectileId = projectile.projectileId;
            shot.impactTickIndex = 0;
            shot.impactDamage = 0;
            shot.projectileMotionTimingProven = false;
            shot.runtimeEffect = BattleShotRuntimeEffect::None;
            shot.damageBefore = 0;
            shot.damageApplied = 0;
            shot.damageAfter = 0;
            shot.provenance +=
                ":BTECH_original_projectile_pool_launch_"
                "compatibility_tick_scale";
            projectile.pendingShot = shot;
            projectile.provenance =
                "BTECH.EXE:FUN_1000_5654/FUN_1000_578b:"
                "five_substeps_per_0.1s_compatibility_policy:"
                "OTHPCK_rear_extent_launch_offset_beta";
            if (launchTerrainHit.valid) {
                projectile.provenance +=
                    ":launch_offset_clipped_to_terrain_contact";
            }
            projectiles_.push_back(std::move(projectile));
        } else if (shot.hit && interceptedProjectile != nullptr) {
            shot.result = BattleShotResult::HitProjectile;
            shot.deliveryState =
                BattleShotDeliveryState::ProjectileIntercepted;
            shot.impactTickIndex = tickIndex_;
            shot.projectileId = 0u;
            shot.impactDamage = 0u;
            shot.runtimeEffect =
                BattleShotRuntimeEffect::ProjectileInterceptedNoDamage;
            shot.damageBefore = 0;
            shot.damageApplied = 0;
            shot.damageAfter = 0;
            interceptedProjectile->valid = false;
            interceptedProjectile->targetBound = false;
            shot.provenance +=
                ":missile_airburst_removed_without_damage";
        } else if (shot.hit && target != nullptr) {
            shot.deliveryState = BattleShotDeliveryState::Immediate;
            shot.impactTickIndex = tickIndex_;
            shot.impactDamage = weapon.originalDamage;
            applyDetailedCombatantDamage(
                params_,
                attacker,
                *target,
                static_cast<int>(weapon.originalDamage),
                shot);
        } else if (shot.hit && targetObjective) {
            shot.deliveryState = BattleShotDeliveryState::Immediate;
            shot.impactTickIndex = tickIndex_;
            shot.impactDamage = weapon.originalDamage;
            shot.runtimeEffect = BattleShotRuntimeEffect::ObjectiveDamage;
            shot.damageBefore = objective_.damage;
            applyDeterministicObjectiveDamage(
                attacker,
                objective_,
                static_cast<int>(weapon.originalDamage));
            shot.damageAfter = objective_.damage;
            shot.damageApplied = shot.damageAfter - shot.damageBefore;
        }
        shotEvents_.push_back(shot);
        lastShot_ = std::move(shot);
        refreshCombatantWeaponState(params_, attacker);
        attacker.fireWeaponRequested = false;
    }

    projectiles_.erase(
        std::remove_if(
            projectiles_.begin(),
            projectiles_.end(),
            [](const BattleProjectileState& projectile) {
                return !projectile.valid;
            }),
        projectiles_.end());

    updateOriginalHeatRuntime();
}

void BattleWorld::updateOriginalHeatRuntime() {
    constexpr double kOriginalUpdateSeconds = 0.1;
    const double originalUpdatesThisTick =
        params_.fixedTickSeconds / kOriginalUpdateSeconds;

    for (Combatant& combatant : combatants_) {
        BattleHeatState& heat = combatant.heat;
        if (!heat.enabled) {
            continue;
        }
        heat.lastJumpJetHeatAdded = 0;
        heat.lastGroundMovementHeatAdded = 0;
        heat.lastEngineHeatAdded = 0;
        heat.lastCoolingApplied = 0;
        heat.lastCoolingMultiplier =
            terrain_.environmentId == 2 ? uint8_t{2} : uint8_t{1};
        heat.originalUpdateAccumulator += originalUpdatesThisTick;
        while (heat.originalUpdateAccumulator + 1.0e-12 >= 1.0) {
            heat.originalUpdateAccumulator -= 1.0;
            ++heat.originalUpdateCount;
            // FUN_1000_ad86 uses abs(current motion) and strict one-third and
            // two-thirds thresholds of the chassis speed reference.  Current
            // replacement velocity is linearly chassis-scaled, so comparing
            // against maxForwardSpeed preserves those ratios; the raw-unit
            // bridge remains explicitly provisional.
            if (!combatant.airborne && combatant.maxForwardSpeed > 0.0) {
                const double speed = std::abs(combatant.forwardSpeed);
                int movementHeat = 0;
                if (speed > combatant.maxForwardSpeed * (2.0 / 3.0)) {
                    movementHeat = 2;
                } else if (speed > combatant.maxForwardSpeed * (1.0 / 3.0)) {
                    movementHeat = 1;
                }
                if (movementHeat != 0) {
                    heat.lastGroundMovementHeatAdded += movementHeat;
                    heat.lastGroundMovementHeatTickIndex = tickIndex_;
                    heat.rawHeat = std::clamp(
                        heat.rawHeat + movementHeat,
                        0,
                        battleMaximumRawHeat());
                }
            }
            // The original jump branch adds 0x0c for vertical-only thrust and
            // 0x14 for its combined forward/up thrust before FUN_1000_ad86
            // applies heat-sink cooling. Our Up/Down control split is a
            // compatibility mapping because the current jump physics and fuel
            // scale remain provisional.
            if (combatant.jumpJetThrusting) {
                const int jumpHeat = combatant.jumpForwardThrusting
                    ? battleOriginalForwardJumpRawHeat()
                    : battleOriginalVerticalJumpRawHeat();
                heat.lastJumpJetHeatAdded += jumpHeat;
                heat.lastJumpJetHeatTickIndex = tickIndex_;
                heat.rawHeat = std::clamp(
                    heat.rawHeat + jumpHeat,
                    0,
                    battleMaximumRawHeat());
            }
            const int engineHeat = combatant.majorSystems.enabled
                ? combatant.majorSystems.engineHeatPerOriginalUpdate
                : static_cast<int>(std::min<uint8_t>(
                      combatant.mechRuntime.detailedDamage.criticalComponents[
                          criticalComponentIndex(
                              mech3d::MechCriticalComponentId::Engine)]
                          .condition,
                      3u)) * 5;
            if (engineHeat > 0) {
                heat.lastEngineHeatAdded += engineHeat;
                heat.lastEngineHeatTickIndex = tickIndex_;
                heat.rawHeat = std::clamp(
                    heat.rawHeat + engineHeat,
                    0,
                    battleMaximumRawHeat());
            }
            const auto& heatSinks =
                combatant.mechRuntime.detailedDamage.criticalComponents[
                    criticalComponentIndex(
                        mech3d::MechCriticalComponentId::HeatSinks)];
            const int workingHeatSinks =
                static_cast<int>(heatSinks.workingCount) *
                static_cast<int>(heat.lastCoolingMultiplier);
            const int originalCooling = (workingHeatSinks << 2) / 3;
            const int before = heat.rawHeat;
            heat.rawHeat = std::clamp(
                heat.rawHeat - originalCooling,
                0,
                battleMaximumRawHeat());
            heat.lastCoolingApplied += before - heat.rawHeat;
            heat.lastCoolingTickIndex = tickIndex_;
            const bool reactorShutdown =
                heat.rawHeat > battleOriginalReactorShutdownRawHeat();
            if (reactorShutdown != heat.reactorShutdown) {
                heat.reactorShutdown = reactorShutdown;
                heat.reactorShutdownStateChangedTickIndex = tickIndex_;
            }
        }
        if (heat.originalUpdateAccumulator < 0.0 &&
            heat.originalUpdateAccumulator > -1.0e-9) {
            heat.originalUpdateAccumulator = 0.0;
        }
    }
}

void BattleWorld::updateReplacementMissionRuntime() {
    if (params_.combatAiPolicy !=
            BattleCombatAiPolicy::ReplacementDeterministicCombatAi ||
        missionTerminal(missionRuntimeState_)) {
        return;
    }
    const uint64_t completedTickIndex = tickIndex_ + 1u;

    for (Combatant& combatant : combatants_) {
        if (combatant.playerControlled ||
            (combatant.roster.team != BattleTeam::Player &&
             combatant.roster.team != BattleTeam::Opposing) ||
            combatant.missionStatus != CombatantMissionStatus::Active ||
            !combatant.replacementAi.retreating) {
            continue;
        }
        const std::optional<BattlefieldBoundaryEdge> exitEdge =
            boundaryExitEdge(
                params_.battlefieldBoundary, combatant.transform);
        if (!exitEdge && !combatant.boundaryContact) {
            continue;
        }
        combatant.missionStatus = CombatantMissionStatus::Escaped;
        combatant.replacementAi.active = false;
        combatant.replacementAi.targetKind =
            BattleReplacementAiTargetKind::None;
        combatant.replacementAi.combatTargetEntityId = {};
        combatant.replacementAi.selectedWeaponInstanceId = 0u;
        combatant.throttle = 0.0;
        combatant.turn = 0.0;
        combatant.targetForwardSpeed = 0.0;
        combatant.forwardSpeed = 0.0;
    }

    if (retrieval_.active &&
        retrieval_.phase == BattleRetrievalPhase::AwaitingContact) {
        Combatant* player = findCombatant(playerEntityId_);
        if (player != nullptr && player->playerControlled &&
            player->roster.team == BattleTeam::Player &&
            player->missionStatus == CombatantMissionStatus::Active &&
            !combatantMechDestroyed(*player) &&
            distance2d(player->transform, retrieval_.contactAnchor) <=
                retrieval_.contactRadius) {
            retrieval_.phase = BattleRetrievalPhase::ContactedByPlayer;
            retrieval_.contactEntityId = player->id;
            retrieval_.contactTickIndex = completedTickIndex;
            BattleResult result;
            result.valid = true;
            result.state = BattleMissionRuntimeState::Victory;
            result.reason = "retrieval_structure_contact";
            result.rawResultCode = 0;
            result.terminalTickIndex = completedTickIndex;
            result.terminalElapsedMs = elapsedMs_ + fixedTickMs(params_);
            result.sourceEntityId = player->id;
            completeMission(std::move(result));
            return;
        }
    }

    if (!params_.mission.valid || params_.mission.extended ||
        params_.mission.family != BattleMissionFamily::Sprint) {
        return;
    }
    for (const Combatant& combatant : combatants_) {
        if (combatant.roster.team != BattleTeam::Opposing ||
            combatant.missionStatus != CombatantMissionStatus::Active ||
            combatantMechDestroyed(combatant) ||
            !combatant.replacementAi.active ||
            combatant.replacementAi.missionSlice !=
                BattleReplacementAiMissionSlice::SprintInterception) {
            continue;
        }
        const double finishRadius = std::max(
            125.0, combatant.collisionRadiusWorld);
        if (!combatant.boundaryContact &&
            distance2d(combatant.transform,
                       combatant.replacementAi.movementDestination) >
                finishRadius) {
            continue;
        }
        BattleResult result;
        result.valid = true;
        result.state = BattleMissionRuntimeState::Defeat;
        result.reason = "sprint_opponent_escaped";
        result.rawResultCode = 1;
        result.terminalTickIndex = completedTickIndex;
        result.terminalElapsedMs = elapsedMs_ + fixedTickMs(params_);
        result.sourceEntityId = combatant.id;
        completeMission(std::move(result));
        return;
    }
}

void BattleWorld::evaluateMissionRuntimeAfterMovement() {
    if (missionTerminal(missionRuntimeState_)) {
        return;
    }
    const Combatant* player = findCombatant(playerEntityId_);
    if (player == nullptr) {
        return;
    }
    const std::optional<BattlefieldBoundaryEdge> exitEdge =
        boundaryExitEdge(params_.battlefieldBoundary, player->transform);
    if (!exitEdge) {
        return;
    }

    const uint8_t exitMask = battlefieldBoundaryEdgeMask(*exitEdge);
    const bool allowedExit = (params_.battlefieldBoundary.playerAllowedExitMask & exitMask) != 0;
    const bool replacementScenario = params_.combatAiPolicy ==
        BattleCombatAiPolicy::ReplacementDeterministicCombatAi;

    BattleResult result;
    result.valid = true;
    result.state = replacementScenario
        ? BattleMissionRuntimeState::Defeat
        : allowedExit ? BattleMissionRuntimeState::Victory
                      : BattleMissionRuntimeState::Withdraw;
    result.reason = replacementScenario
        ? "player_fled_battlefield"
        : allowedExit ? "player_allowed_boundary_exit"
                      : "player_ordinary_boundary_exit";
    result.rawResultCode = replacementScenario
        ? 1
        : allowedExit
            ? params_.battlefieldBoundary.allowedPlayerExitResultCode
            : params_.battlefieldBoundary.ordinaryPlayerExitResultCode;
    result.terminalTickIndex = tickIndex_ + 1u;
    result.terminalElapsedMs = elapsedMs_ + fixedTickMs(params_);
    result.sourceEntityId = player->id;
    result.actorExitStatus = battlefieldActorExitStatus(*exitEdge);
    result.exitEdge = *exitEdge;
    result.exitMask = exitMask;
    result.exitAllowed = !replacementScenario && allowedExit;
    completeMission(std::move(result));
}

void BattleWorld::evaluateMissionRuntimeAfterObjective() {
    if (missionTerminal(missionRuntimeState_) ||
        !params_.deterministicObjectiveRuntimeEnabled ||
        !objective_.depleted) {
        return;
    }
    if (!objective_.missionIntentBoundToObjective || !objective_.missionSemanticsProven) {
        BattleResult result;
        result.valid = true;
        result.state = BattleMissionRuntimeState::Unsupported;
        result.reason = "objective_depleted_without_bound_mission_semantics";
        result.rawResultCode = -1;
        result.terminalTickIndex = tickIndex_ + 1u;
        result.terminalElapsedMs = elapsedMs_ + fixedTickMs(params_);
        result.sourceEntityId = objective_.lastDamageSourceEntityId;
        completeMission(std::move(result));
        return;
    }

    BattleMissionRuntimeState state = BattleMissionRuntimeState::Unsupported;
    int rawResultCode = -1;
    std::string reason = "objective_depleted";
    if (objective_.depletionPolicyProven &&
        (objective_.depletionSetsPlayerWinCondition || objective_.depletionSetsPlayerLossCondition)) {
        state = objective_.depletionSetsPlayerWinCondition
            ? BattleMissionRuntimeState::Victory
            : BattleMissionRuntimeState::Defeat;
        rawResultCode = objective_.depletionResultCode;
        reason = objective_.depletionSetsPlayerWinCondition
            ? "objective_depletion_player_win"
            : "objective_depletion_player_loss";
    } else if (objective_.missionIntent == BattleMissionObjectiveIntent::Protect) {
        state = BattleMissionRuntimeState::Defeat;
        rawResultCode = 1;
        reason = "protected_objective_depleted";
    } else if (objective_.missionIntent == BattleMissionObjectiveIntent::Destroy ||
               objective_.missionIntent == BattleMissionObjectiveIntent::Disable) {
        state = BattleMissionRuntimeState::Victory;
        rawResultCode = 0;
        reason = objective_.missionIntent == BattleMissionObjectiveIntent::Destroy
            ? "destroy_objective_depleted"
            : "disable_objective_depleted";
    }

    BattleResult result;
    result.valid = true;
    result.state = state;
    result.reason = std::move(reason);
    result.rawResultCode = rawResultCode;
    result.terminalTickIndex = tickIndex_ + 1u;
    result.terminalElapsedMs = elapsedMs_ + fixedTickMs(params_);
    result.sourceEntityId = objective_.lastDamageSourceEntityId;
    completeMission(std::move(result));
}

void BattleWorld::evaluateMissionRuntimeAfterCombat() {
    if (missionTerminal(missionRuntimeState_) || !params_.deterministicCombatRuntimeEnabled) {
        return;
    }
    const Combatant* player = findCombatant(playerEntityId_);
    if (player == nullptr) {
        return;
    }
    const bool replacementScenario = params_.combatAiPolicy ==
        BattleCombatAiPolicy::ReplacementDeterministicCombatAi;
    bool anyActivePlayerCombatant = false;
    if (replacementScenario) {
        for (const Combatant& combatant : combatants_) {
            if (combatant.roster.team == BattleTeam::Player &&
                combatant.missionStatus == CombatantMissionStatus::Active &&
                !combatantMechDestroyed(combatant)) {
                anyActivePlayerCombatant = true;
                break;
            }
        }
    }
    std::optional<BattleResult> candidateResult;
    if ((replacementScenario && !anyActivePlayerCombatant) ||
        (!replacementScenario &&
         (player->missionStatus == CombatantMissionStatus::Destroyed ||
          combatantMechDestroyed(*player)))) {
        BattleResult result;
        result.valid = true;
        result.state = BattleMissionRuntimeState::Defeat;
        result.reason = replacementScenario
            ? "all_player_mechs_destroyed" : "player_mech_destroyed";
        result.rawResultCode = 1;
        result.sourceEntityId = player->lastDamageSourceEntityId;
        candidateResult = std::move(result);
    }

    if (!candidateResult.has_value()) {
        bool sawOpposingCombatant = false;
        bool anyActiveOpposingCombatant = false;
        bool anyEscapedOpposingCombatant = false;
        for (const Combatant& combatant : combatants_) {
            if (combatant.roster.team != BattleTeam::Opposing) {
                continue;
            }
            sawOpposingCombatant = true;
            anyEscapedOpposingCombatant =
                anyEscapedOpposingCombatant ||
                combatant.missionStatus ==
                    CombatantMissionStatus::Escaped;
            if (combatant.missionStatus == CombatantMissionStatus::Active &&
                !combatantMechDestroyed(combatant)) {
                anyActiveOpposingCombatant = true;
                break;
            }
        }
        if (sawOpposingCombatant && !anyActiveOpposingCombatant) {
            BattleResult result;
            result.valid = true;
            result.state = BattleMissionRuntimeState::Victory;
            result.reason = anyEscapedOpposingCombatant
                ? "all_opposing_mechs_destroyed_or_escaped"
                : "all_opposing_mechs_destroyed";
            result.rawResultCode = 0;
            result.sourceEntityId = player->id;
            candidateResult = std::move(result);
        }
    }

    if (!candidateResult.has_value()) {
        conclusionDelay_ = {};
        return;
    }

    BattleResult& result = *candidateResult;
    const uint64_t delayTicks = static_cast<uint64_t>(std::ceil(
        params_.combatConclusionDelaySeconds / params_.fixedTickSeconds));
    if (delayTicks > 0u) {
        const bool samePendingResult = conclusionDelay_.active &&
            conclusionDelay_.pendingState == result.state &&
            conclusionDelay_.reason == result.reason &&
            conclusionDelay_.sourceEntityId == result.sourceEntityId;
        if (!samePendingResult) {
            conclusionDelay_.active = true;
            conclusionDelay_.pendingState = result.state;
            conclusionDelay_.reason = result.reason;
            conclusionDelay_.startTickIndex = tickIndex_;
            conclusionDelay_.resolveTickIndex = tickIndex_ + delayTicks;
            conclusionDelay_.sourceEntityId = result.sourceEntityId;
            return;
        }
        if (tickIndex_ < conclusionDelay_.resolveTickIndex) {
            return;
        }
    }

    result.terminalTickIndex = tickIndex_ + 1u;
    result.terminalElapsedMs = elapsedMs_ + fixedTickMs(params_);
    completeMission(std::move(result));
}

void BattleWorld::completeMission(BattleResult result) {
    if (result.valid && !missionTerminal(result.state)) {
        throw std::runtime_error("battle result must use a terminal mission runtime state");
    }
    if (result.valid) {
        conclusionDelay_ = {};
        missionRuntimeState_ = result.state;
        result_ = std::move(result);
    }
}

void BattleWorld::applyQueuedInputs() {
    for (const BattleInputCommand& command : queuedInputs_) {
        if (command.tickIndex != tickIndex_) {
            continue;
        }
        Combatant* combatant = findCombatant(command.entityId);
        if (!combatant) {
            throw std::runtime_error("battle input references unknown entity id: " + std::to_string(command.entityId.value));
        }
        combatant->throttle = clampUnit(command.throttle);
        combatant->turn = clampUnit(command.turn);
        if (combatantControlShutdown(*combatant)) {
            combatant->throttle = 0.0;
            combatant->turn = 0.0;
            combatant->fireWeaponRequested = false;
            combatant->jumpJetsEnabled = false;
            combatant->jumpJetThrusting = false;
            combatant->jumpForwardThrusting = false;
            continue;
        }
        if (combatant->playerControlled && command.toggleCockpitRadar) {
            cockpitRadar_.active = !cockpitRadar_.active;
            ++cockpitRadar_.commandSequence;
            cockpitRadar_.lastCommandTickIndex = tickIndex_;
        }
        if (combatant->playerControlled && command.cycleCockpitRadarRange &&
            cockpitRadar_.active) {
            cockpitRadar_.rangeIndex =
                static_cast<uint8_t>((cockpitRadar_.rangeIndex + 1u) & 3u);
            cockpitRadar_.rangeMeters =
                battleCockpitRadarRangeMeters(cockpitRadar_.rangeIndex);
            ++cockpitRadar_.commandSequence;
            cockpitRadar_.lastCommandTickIndex = tickIndex_;
        }
        if (combatant->playerControlled && command.cycleTargetScan &&
            (!combatant->majorSystems.enabled ||
             combatant->majorSystems.targetingAvailable)) {
            cycleTargetScan();
        }
        combatant->fireWeaponRequested = command.fireWeapon;
        if (command.selectWeaponInstanceId != 0) {
            const auto requested = std::find_if(
                combatant->weapons.begin(),
                combatant->weapons.end(),
                [&command](const BattleWeaponInstanceState& weapon) {
                    return weapon.weaponInstanceId ==
                        command.selectWeaponInstanceId;
                });
            if (requested != combatant->weapons.end()) {
                combatant->selectedWeaponInstanceId =
                    requested->weaponInstanceId;
            }
        }
        if (command.selectedWeaponStepDelta != 0 &&
            !combatant->weapons.empty()) {
            auto selected = std::find_if(
                combatant->weapons.begin(),
                combatant->weapons.end(),
                [combatant](const BattleWeaponInstanceState& weapon) {
                    return weapon.weaponInstanceId ==
                        combatant->selectedWeaponInstanceId;
                });
            int index = selected == combatant->weapons.end()
                ? 0
                : static_cast<int>(std::distance(
                      combatant->weapons.begin(), selected));
            const int count = static_cast<int>(combatant->weapons.size());
            index = (index + command.selectedWeaponStepDelta) % count;
            if (index < 0) {
                index += count;
            }
            combatant->selectedWeaponInstanceId =
                combatant->weapons[static_cast<size_t>(index)]
                    .weaponInstanceId;
        }
        refreshCombatantWeaponState(params_, *combatant);
        combatant->jumpJetThrusting = false;
        combatant->jumpForwardThrusting = false;
        if (command.torsoYawStepDelta != 0) {
            combatant->torsoYawStep =
                clampInt(
                    combatant->torsoYawStep + command.torsoYawStepDelta,
                    -params_.maxPlayerTorsoYawSteps,
                    params_.maxPlayerTorsoYawSteps);
        }
        if (command.aimPitchStepDelta != 0) {
            validateAimPitchLimits(params_);
            combatant->aimPitchStep =
                clampInt(
                    combatant->aimPitchStep + command.aimPitchStepDelta,
                    -params_.maxPlayerAimPitchDownSteps,
                    params_.maxPlayerAimPitchUpSteps);
        }
        if (command.jumpJetToggle && (combatant->jumpJetsEnabled || combatant->airborne || canActivateJumpJets(params_, *combatant))) {
            combatant->jumpJetsEnabled = !combatant->jumpJetsEnabled;
            if (combatant->jumpJetsEnabled && !combatant->airborne && combatant->transform.y <= 0.0) {
                combatant->airborne = true;
                combatant->jumpLaunchImpulseRemaining =
                    std::max(combatant->jumpLaunchImpulseRemaining, params_.jumpJetInitialImpulse);
                combatant->jumpFuel = std::max(0.0, combatant->jumpFuel - params_.jumpJetActivationFuel * 0.25);
                combatant->hardLanding = false;
                combatant->knockdownCandidate = false;
                combatant->landingImpactSpeed = 0.0;
            } else if (!combatant->jumpJetsEnabled) {
                combatant->jumpLaunchImpulseRemaining = 0.0;
            }
        }
        if (combatant->jumpFuel <= 0.0) {
            combatant->jumpJetsEnabled = false;
            combatant->jumpLaunchImpulseRemaining = 0.0;
        }
        if (combatant->jumpJetsEnabled && combatant->jumpFuel > 0.0) {
            combatant->jumpJetThrusting = command.jumpForwardThrust || command.jumpVerticalThrust;
            combatant->jumpForwardThrusting = command.jumpForwardThrust;
        }
    }

    queuedInputs_.erase(
        std::remove_if(
            queuedInputs_.begin(),
            queuedInputs_.end(),
            [this](const BattleInputCommand& command) {
                return command.tickIndex <= tickIndex_;
            }),
        queuedInputs_.end());
}

BattleSnapshot runBattleReplay(
    const BattleStartParams& params,
    const BattleReplay& replay,
    uint64_t tickCount) {
    BattleWorld world = BattleWorld::create(params);
    for (BattleInputCommand command : replay.commands) {
        if (!isValid(command.entityId)) {
            command.entityId = world.playerEntityId();
        }
        world.enqueueInput(command);
    }
    world.runTicks(tickCount);
    return world.snapshot();
}

uint64_t battleSnapshotFingerprint(const BattleSnapshot& snapshot) {
    uint64_t hash = kFnvOffset;
    hashUint64(hash, snapshot.tickIndex);
    hashUint64(hash, snapshot.elapsedMs);
    if (snapshot.combatAiPolicy ==
            BattleCombatAiPolicy::ReplacementDeterministicCombatAi ||
        !snapshot.combatAiPolicyProvenance.empty()) {
        hashUint64(hash, 0x50483134504f4c59ull);
        hashUint64(hash, static_cast<uint64_t>(snapshot.combatAiPolicy));
        hashString(hash, snapshot.combatAiPolicyProvenance);
    }
    hashContractMetadata(hash, snapshot.contract);
    hashOppositionRosterMetadata(hash, snapshot.oppositionRoster);
    hashUint64(hash, snapshot.terrain.scenarioLoaded ? 1ull : 0ull);
    hashUint64(hash, static_cast<uint64_t>(snapshot.terrain.scenarioIndex));
    hashUint64(hash, snapshot.terrain.terrainMode);
    hashOptionalInt(hash, snapshot.terrain.environmentId);
    hashInt64(hash, quantize(snapshot.terrain.boundsMinX));
    hashInt64(hash, quantize(snapshot.terrain.boundsMaxX));
    hashInt64(hash, quantize(snapshot.terrain.boundsMinZ));
    hashInt64(hash, quantize(snapshot.terrain.boundsMaxZ));
    if (snapshot.terrain.collisionGridValid) {
        hashUint64(hash, 0x43475244ull);
        hashString(hash, snapshot.terrain.collisionGridProvenance);
        hashInt64(hash, snapshot.terrain.collisionGridWidth);
        hashInt64(hash, snapshot.terrain.collisionGridHeight);
        hashInt64(hash, quantize(snapshot.terrain.collisionGridCellSize));
        hashUint64(hash, snapshot.terrain.collisionGridBlockingSampleCount);
        hashUint64(hash, snapshot.terrain.collisionGridFingerprint);
    }
    if (snapshot.terrain.collisionObstacleCount != 0u) {
        hashUint64(hash, 0x434f4253ull);
        hashUint64(hash, snapshot.terrain.collisionObstacleCount);
        hashUint64(hash, snapshot.terrain.collisionObstacleFingerprint);
    }
    if (snapshot.terrain.driveableSurfaceFeatureCount != 0u) {
        hashUint64(hash, 0x53555246ull);
        hashUint64(hash, snapshot.terrain.driveableSurfaceFeatureCount);
        hashUint64(hash, snapshot.terrain.driveableSurfaceFeatureFingerprint);
    }
    if (snapshot.terrain.originalTerrainSceneValid) {
        hashUint64(hash, 0x4f53434eull);
        hashString(hash, snapshot.terrain.originalTerrainSceneProvenance);
        hashUint64(hash, snapshot.terrain.originalTerrainCollisionRecordCount);
        hashUint64(hash, snapshot.terrain.originalTerrainSceneObjectCount);
        hashUint64(hash, snapshot.terrain.originalTerrainQueryObjectCount);
        hashUint64(hash, snapshot.terrain.originalTerrainSceneFingerprint);
    }
    hashUint64(hash, static_cast<uint64_t>(snapshot.terrain.tileNames.size()));
    for (const std::string& tileName : snapshot.terrain.tileNames) {
        hashString(hash, tileName);
    }
    hashSetupMetadata(hash, snapshot.setup);
    hashObjectiveState(hash, snapshot.objective);
    hashRetrievalRuntimeState(hash, snapshot.retrieval);
    hashBattlefieldBoundary(hash, snapshot.battlefieldBoundary);
    hashBattleResult(hash, snapshot.result);
    if (snapshot.conclusionDelay.active) {
        hashUint64(hash, 0x434f4e434c444c59ull);
        hashUint64(
            hash,
            static_cast<uint64_t>(
                snapshot.conclusionDelay.pendingState));
        hashString(hash, snapshot.conclusionDelay.reason);
        hashUint64(hash, snapshot.conclusionDelay.startTickIndex);
        hashUint64(hash, snapshot.conclusionDelay.resolveTickIndex);
        hashUint64(hash, snapshot.conclusionDelay.sourceEntityId.value);
    }
    hashShotDiagnostic(hash, snapshot.lastShot);
    if (!snapshot.shotEvents.empty()) {
        hashUint64(hash, 0x53484f54ull);
        hashUint64(hash, static_cast<uint64_t>(snapshot.shotEvents.size()));
        for (const BattleShotDiagnostic& shot : snapshot.shotEvents) {
            hashShotDiagnostic(hash, shot);
        }
    }
    if (!snapshot.impactEvents.empty()) {
        hashUint64(hash, 0x494d504c495354ull);
        hashUint64(hash, static_cast<uint64_t>(snapshot.impactEvents.size()));
        for (const BattleImpactEvent& event : snapshot.impactEvents) {
            hashImpactEvent(hash, event);
        }
    }
    hashUint64(hash, static_cast<uint64_t>(snapshot.projectiles.size()));
    for (const BattleProjectileState& projectile : snapshot.projectiles) {
        hashProjectileState(hash, projectile);
    }
    hashCollisionDiagnostic(hash, snapshot.lastCollision);
    if (snapshot.cockpitRadar.commandSequence != 0u) {
        hashUint64(hash, 0x52414452ull);
        hashUint64(hash, snapshot.cockpitRadar.active ? 1ull : 0ull);
        hashUint64(hash, snapshot.cockpitRadar.rangeIndex);
        hashUint64(hash, snapshot.cockpitRadar.rangeMeters);
        hashUint64(hash, snapshot.cockpitRadar.commandSequence);
        hashUint64(hash, snapshot.cockpitRadar.lastCommandTickIndex);
    }
    if (snapshot.targetScan.commandSequence != 0u) {
        hashUint64(hash, 0x5343414eull);
        hashUint64(
            hash,
            static_cast<uint64_t>(snapshot.targetScan.selectedTargetKind));
        hashUint64(hash, snapshot.targetScan.selectedTargetEntityId.value);
        hashUint64(hash, snapshot.targetScan.rangeMeters);
        hashInt64(
            hash,
            quantize(snapshot.targetScan.selectedTargetDistanceMeters));
        hashUint64(hash, snapshot.targetScan.commandSequence);
        hashUint64(hash, snapshot.targetScan.lastCommandTickIndex);
    }

    hashUint64(hash, static_cast<uint64_t>(snapshot.combatants.size()));
    for (const CombatantSnapshot& combatant : snapshot.combatants) {
        hashUint64(hash, combatant.id.value);
        hashString(hash, combatant.mechPresetId);
        if (combatant.hitGeometryAvailable ||
            combatant.hitGeometryFingerprint != 0u) {
            hashUint64(hash, 0x48495447454f4d31ull);
            hashUint64(hash, combatant.hitGeometryAvailable ? 1ull : 0ull);
            hashUint64(hash, combatant.hitGeometryFingerprint);
        }
        hashUint64(hash, combatant.playerControlled ? 1ull : 0ull);
        hashRosterMetadata(hash, combatant.roster);
        hashInt64(hash, quantize(combatant.transform.x));
        hashInt64(hash, quantize(combatant.transform.y));
        hashInt64(hash, quantize(combatant.transform.z));
        hashInt64(hash, quantize(combatant.transform.headingRadians));
        hashInt64(hash, quantize(combatant.throttle));
        hashInt64(hash, quantize(combatant.turn));
        hashInt64(hash, quantize(combatant.targetForwardSpeed));
        hashInt64(hash, quantize(combatant.forwardSpeed));
        hashInt64(hash, combatant.torsoYawStep);
        hashInt64(hash, quantize(combatant.torsoYawRadians));
        hashInt64(hash, combatant.aimPitchStep);
        hashUint64(hash, combatant.boundaryContact ? 1ull : 0ull);
        if (combatant.collisionRadiusWorld > 0.0 || combatant.collisionContact ||
            combatant.collisionCount != 0u ||
            combatant.collisionFeedbackCooldownTicksRemaining != 0u) {
            hashUint64(hash, 0x434f4e54ull);
            hashUint64(hash, combatant.collisionContact ? 1ull : 0ull);
            hashUint64(hash, combatant.collisionCount);
            hashUint64(hash, combatant.lastCollisionTickIndex);
            hashUint64(hash, combatant.collisionFeedbackCooldownTicksRemaining);
            hashInt64(hash, quantize(combatant.collisionRadiusWorld));
        }
        hashEnemyAiDiagnostics(hash, combatant);
        hashReplacementAiState(hash, combatant.replacementAi);
        if (combatant.originalAiUpdateAccumulator != 0.0 ||
            combatant.originalAiUpdateCount != 0u ||
            combatant.lastOriginalAiDecision.valid) {
            hashUint64(hash, 0x504831334149ull);
            hashInt64(
                hash,
                quantize(combatant.originalAiUpdateAccumulator));
            hashUint64(hash, combatant.originalAiUpdateCount);
            hashOriginalAiDecision(hash, combatant.lastOriginalAiDecision);
        }
        hashOriginalAiMotionRuntimeState(hash, combatant.originalAiMotion);
        hashUint64(hash, combatant.walkAnimationElapsedMs);
        hashUint64(hash, combatant.jumpJetsEnabled ? 1ull : 0ull);
        hashUint64(hash, combatant.jumpJetReady ? 1ull : 0ull);
        hashUint64(hash, combatant.jumpJetThrusting ? 1ull : 0ull);
        hashUint64(hash, combatant.jumpForwardThrusting ? 1ull : 0ull);
        hashUint64(hash, combatant.airborne ? 1ull : 0ull);
        hashUint64(hash, combatant.hardLanding ? 1ull : 0ull);
        hashUint64(hash, combatant.knockdownCandidate ? 1ull : 0ull);
        hashInt64(hash, quantize(combatant.jumpFuel));
        hashInt64(hash, quantize(combatant.jumpMaxFuel));
        hashInt64(hash, quantize(combatant.jumpActivationFuel));
        hashInt64(hash, quantize(combatant.verticalSpeed));
        hashInt64(hash, quantize(combatant.jumpLaunchImpulseRemaining));
        hashInt64(hash, quantize(combatant.landingImpactSpeed));
        hashUint64(hash, static_cast<uint64_t>(combatant.missionStatus));
        hashUint64(hash, combatant.minimalRepairApplied ? 1ull : 0ull);
        hashUint64(hash, combatant.fragileAfterMinimalRepair ? 1ull : 0ull);
        hashWeaponDiagnostics(hash, combatant);
        hashHeatState(hash, combatant.heat);
        hashMajorSystemRuntimeState(hash, combatant.majorSystems);
        hashMajorSystemWarningEvent(hash, combatant.lastMajorSystemWarning);
        hashString(hash, combatant.requestedAnimationId);
        hashString(hash, combatant.activeAnimationId);
        hashUint64(hash, combatant.mechDestroyed ? 1ull : 0ull);
        for (const std::vector<int>* ids : {
                 &combatant.destroyedComponentIds,
                 &combatant.hiddenComponentIds,
                 &combatant.disabledComponentIds,
             }) {
            hashUint64(hash, static_cast<uint64_t>((*ids).size()));
            for (int id : *ids) {
                hashInt64(hash, id);
            }
        }
        hashMechSystemsSnapshot(hash, combatant.mechSystems);
        hashDetailedDamageState(hash, combatant.detailedDamage);
        if (combatant.persistentMechState.valid) {
            hashUint64(
                hash,
                battlePersistentMechStateFingerprint(
                    combatant.persistentMechState));
        }
    }
    hashUint64(hash, snapshot.camera.valid ? 1ull : 0ull);
    hashUint64(hash, snapshot.camera.attachedEntityId.value);
    hashInt64(hash, quantize(snapshot.camera.transform.x));
    hashInt64(hash, quantize(snapshot.camera.transform.y));
    hashInt64(hash, quantize(snapshot.camera.transform.z));
    hashInt64(hash, quantize(snapshot.camera.transform.headingRadians));
    hashInt64(hash, quantize(snapshot.camera.localForwardOffset));
    hashInt64(hash, quantize(snapshot.camera.localHeight));
    return hash;
}

BattleCombatantLaunchState prepareNextMissionLaunchState(
    const CombatantSnapshot& previousMissionSnapshot,
    Transform nextMissionStartTransform) {
    BattleCombatantLaunchState launchState;
    launchState.mechPresetId = previousMissionSnapshot.mechPresetId;
    launchState.startTransform = nextMissionStartTransform;
    launchState.roster = previousMissionSnapshot.roster;
    launchState.destroyedComponentIds = previousMissionSnapshot.destroyedComponentIds;
    launchState.persistentMechState = previousMissionSnapshot.persistentMechState;
    launchState.priorMissionStatus = previousMissionSnapshot.missionStatus;

    const auto& carriedDetailed = previousMissionSnapshot.detailedDamage;
    const bool detailedDamageChanged =
        std::any_of(
            carriedDetailed.armorSections.begin(),
            carriedDetailed.armorSections.end(),
            [](const auto& section) { return section.battleDamage != 0; }) ||
        std::any_of(
            carriedDetailed.internalSections.begin(),
            carriedDetailed.internalSections.end(),
            [](const auto& section) { return section.battleDamage != 0; }) ||
        std::any_of(
            carriedDetailed.criticalComponents.begin(),
            carriedDetailed.criticalComponents.end(),
            [](const auto& component) { return component.battleDamage != 0; }) ||
        std::any_of(
            carriedDetailed.installedWeapons.begin(),
            carriedDetailed.installedWeapons.end(),
            [](const auto& weapon) { return weapon.battleDamage != 0; });
    if (carriedDetailed.valid && detailedDamageChanged) {
        BattlePersistentMechState& persistent =
            launchState.persistentMechState;
        persistent.valid = true;
        persistent.provenance =
            "wiki_campaign_intermission:carry_damage_without_full_repair";
        uint64_t armorRemaining = 0u;
        uint64_t armorMaximum = 0u;
        for (size_t index = 0;
             index < previousMissionSnapshot.detailedDamage.armorSections.size();
             ++index) {
            const auto& armor =
                previousMissionSnapshot.detailedDamage.armorSections[index];
            const auto& internal =
                previousMissionSnapshot.detailedDamage.internalSections[
                    internalSectionIndex(internalSectionForArmor(armor.section))];
            const uint32_t combinedMaximum =
                static_cast<uint32_t>(armor.armorMaximum) +
                static_cast<uint32_t>(internal.structureMaximum);
            const uint32_t combinedRemaining =
                static_cast<uint32_t>(armor.armorRemaining) +
                static_cast<uint32_t>(internal.structureRemaining);
            const uint32_t combinedDamage = combinedMaximum > combinedRemaining
                ? combinedMaximum - combinedRemaining : 0u;
            persistent.armorDamage[index] = combinedMaximum == 0u
                ? 0u
                : static_cast<uint8_t>(std::clamp<uint32_t>(
                      (combinedDamage * 3u + combinedMaximum - 1u) /
                          combinedMaximum,
                      0u, 3u));
            armorRemaining += armor.armorRemaining;
            armorMaximum += armor.armorMaximum;
        }
        persistent.armorPercent = armorMaximum == 0u
            ? 0u
            : static_cast<uint8_t>(std::clamp<uint64_t>(
                  (armorRemaining * 100u) / armorMaximum, 0u, 100u));

        const auto& criticals =
            previousMissionSnapshot.detailedDamage.criticalComponents;
        const auto criticalCondition = [&criticals](
                                           mech3d::MechCriticalComponentId id) {
            const auto& component =
                criticals[static_cast<size_t>(id)];
            const uint8_t lostCount = component.totalCount >
                    component.workingCount
                ? static_cast<uint8_t>(
                      component.totalCount - component.workingCount)
                : 0u;
            return static_cast<uint8_t>(std::min<int>(
                3, std::max<int>(component.condition, lostCount)));
        };
        persistent.engine = criticalCondition(
            mech3d::MechCriticalComponentId::Engine);
        persistent.gyros = criticalCondition(
            mech3d::MechCriticalComponentId::Gyros);
        persistent.sensors = criticalCondition(
            mech3d::MechCriticalComponentId::Sensors);
        persistent.lifeSupport = criticalCondition(
            mech3d::MechCriticalComponentId::LifeSupport);
        persistent.leftArmActuator = criticalCondition(
            mech3d::MechCriticalComponentId::LeftArmActuator);
        persistent.rightArmActuator = criticalCondition(
            mech3d::MechCriticalComponentId::RightArmActuator);
        persistent.leftLegActuator = criticalCondition(
            mech3d::MechCriticalComponentId::LeftLegActuator);
        persistent.rightLegActuator = criticalCondition(
            mech3d::MechCriticalComponentId::RightLegActuator);
        const auto& heatSinks = criticals[static_cast<size_t>(
            mech3d::MechCriticalComponentId::HeatSinks)];
        persistent.heatSinksWorking = heatSinks.workingCount;
        persistent.heatSinksTotal = heatSinks.totalCount;
        const auto& jumpJets = criticals[static_cast<size_t>(
            mech3d::MechCriticalComponentId::JumpJets)];
        persistent.jumpJetsWorking = jumpJets.workingCount;
        persistent.jumpJetsTotal = jumpJets.totalCount;
        for (size_t index = 0;
             index < persistent.weaponConditions.size();
             ++index) {
            persistent.weaponConditions[index] =
                previousMissionSnapshot.detailedDamage
                    .installedWeapons[index].condition;
        }
    }

    if (previousMissionSnapshot.missionStatus == CombatantMissionStatus::Disabled) {
        launchState.minimalRepairApplied = true;
        launchState.fragileAfterMinimalRepair = true;
        launchState.priorMissionStatus = CombatantMissionStatus::Disabled;
        BattlePersistentMechState& persistent =
            launchState.persistentMechState;
        persistent.engine = std::min<uint8_t>(persistent.engine, 2u);
        persistent.gyros = std::min<uint8_t>(persistent.gyros, 2u);
        persistent.leftLegActuator =
            std::min<uint8_t>(persistent.leftLegActuator, 2u);
        persistent.rightLegActuator =
            std::min<uint8_t>(persistent.rightLegActuator, 2u);
        persistent.armorDamage[static_cast<size_t>(
            mech3d::MechArmorSectionId::CenterTorso)] =
            std::min<uint8_t>(
                persistent.armorDamage[static_cast<size_t>(
                    mech3d::MechArmorSectionId::CenterTorso)],
                2u);
    } else if (previousMissionSnapshot.missionStatus == CombatantMissionStatus::Destroyed) {
        launchState.minimalRepairApplied = false;
        launchState.fragileAfterMinimalRepair = false;
        launchState.priorMissionStatus = CombatantMissionStatus::Destroyed;
    }

    return launchState;
}

} // namespace mw::battle
