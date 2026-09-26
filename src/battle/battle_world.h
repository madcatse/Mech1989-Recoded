#pragma once

#include "battle/battle_opposition.h"
#include "battle/mission_contract.h"
#include "legacy3d/terrain_parser.h"
#include "mech3d/mech_runtime_state.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace mw::battle {

struct BattleWeaponInstanceState;

struct EntityId {
    uint32_t value = 0;
};

bool operator==(EntityId a, EntityId b);
bool operator!=(EntityId a, EntityId b);
bool isValid(EntityId id);

struct Transform {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double headingRadians = 0.0;
};

struct CameraSnapshot {
    bool valid = false;
    EntityId attachedEntityId{};
    Transform transform{};
    double localForwardOffset = 0.0;
    double localHeight = 0.0;
};

enum class CombatantMissionStatus {
    Active,
    Disabled,
    Destroyed,
    Escaped,
};

enum class BattleTeam : uint8_t {
    Player,
    Opposing,
    Neutral,
};

const char* battleTeamName(BattleTeam team);

enum class BattleEnemyAiState : uint8_t {
    None,
    Search,
    Approach,
    Attack,
    Retreat,
};

const char* battleEnemyAiStateName(BattleEnemyAiState state);

enum class BattleEnemyAiTargetKind : uint8_t {
    None,
    Player,
    Objective,
};

enum class BattleCombatAiPolicy : uint8_t {
    Disabled,
    Phase10CompatibilityFsm,
    OriginalBtechStationarySingleTargetFire,
    OriginalBtechModeZeroFirstMovementUpdate,
    OriginalBtechModeZeroRepeatedMovement,
    OriginalBtechModeZeroMultiTargetRepeatedMovement,
    OriginalBtechNonObjectiveModesMultiTargetRepeatedMovement,
    OriginalBtechNonObjectiveSymmetricRepeatedMovement50eeInvariant,
    OriginalBtechModeThreeNullDataPointRepeatedMovement,
    CompatibilityModeThreePlayerCompanionSelectedTargetRepeatedMovement,
    CompatibilityModeThreePlayerCompanionMissionObjectiveRepeatedMovement,
    ReplacementDeterministicCombatAi,
};

enum class BattleReplacementAiNavMode : uint8_t {
    Idle,
    ApproachObjective,
    EngageObstruction,
    Detour,
    RouteBlocked,
    Arrived,
    DefendAnchor,
    EngageThreat,
    Guarding,
    AdvanceToEngagement,
    EngageOpponent,
    HoldingEngagementLine,
    AssaultObjective,
    AdvanceToGarrison,
    HoldingGarrisonLine,
    HoldingObjectiveGuard,
    SprintToExit,
    InterceptRunner,
    PathWaypoint,
    RetreatToBoundary,
};

enum class BattleReplacementAiTargetKind : uint8_t {
    None,
    Combatant,
    Objective,
};

enum class BattleReplacementAiMissionSlice : uint8_t {
    None,
    DestroyDisableApproach,
    RetrieveArrival,
    AssaultEngagement,
    ProtectGarrisonDefense,
    DeathmatchEngagement,
    SprintInterception,
};

struct BattleReplacementAiState {
    bool active = false;
    BattleReplacementAiMissionSlice missionSlice =
        BattleReplacementAiMissionSlice::None;
    uint64_t decisionSequence = 0;
    uint64_t lastDecisionTickIndex = 0;
    uint8_t approachSlot = 0;
    uint8_t approachSlotCount = 0;
    Transform missionAnchor{};
    Transform movementDestination{};
    BattleReplacementAiTargetKind targetKind =
        BattleReplacementAiTargetKind::None;
    EntityId combatTargetEntityId{};
    uint32_t selectedWeaponInstanceId = 0;
    double desiredCombatDistance = 0.0;
    BattleReplacementAiNavMode navMode = BattleReplacementAiNavMode::Idle;
    uint8_t stuckDecisionCount = 0;
    uint8_t detourDecisionsRemaining = 0;
    uint8_t detourAttempts = 0;
    double lastDestinationDistance = 0.0;
    Transform navigationWaypoint{};
    bool navigationWaypointActive = false;
    uint32_t pathReplanCount = 0;
    uint32_t pathFailureCount = 0;
    uint64_t nextPathRetryTickIndex = 0;
    bool lastMoveRejected = false;
    bool friendlyLaneBlocked = false;
    bool terrainLineOfSightBlocked = false;
    bool aimAligned = false;
    double lastAimErrorRadians = 0.0;
    uint64_t nextFireDecisionTickIndex = 0;
    uint64_t targetLockUntilTickIndex = 0;
    uint64_t nextTorsoTurnTickIndex = 0;
    bool proximityThreatOverride = false;
    bool aimRecoveryActive = false;
    bool combatOrbitActive = false;
    int8_t orbitDirection = 0;
    uint64_t orbitDirectionHoldUntilTickIndex = 0;
    Transform combatOrbitDestination{};
    bool arrivalReached = false;
    uint64_t arrivalTickIndex = 0;
    bool missionTriggered = false;
    EntityId retaliationTargetEntityId{};
    uint64_t retaliationUntilTickIndex = 0;
    bool retreating = false;
    EntityId retreatThreatEntityId{};
    Transform retreatDestination{};
};

enum class BattleRetrievalPhase : uint8_t {
    Inactive,
    AwaitingContact,
    ContactedByPlayer,
};

struct BattleRetrievalRuntimeState {
    bool active = false;
    BattleRetrievalPhase phase = BattleRetrievalPhase::Inactive;
    std::string provenance;
    Transform contactAnchor{};
    EntityId contactEntityId{};
    double contactRadius = 0.0;
    uint64_t contactTickIndex = 0;
};

// Player-visible BTECH command-screen orders. These numeric values are the
// exact selectable indices stored in AI control word +0x00; internal runtime
// derivatives above 5 deliberately do not enter this enum.
enum class BattleOriginalAiLanceOrder : uint8_t {
    ActOnOwn = 0,
    Ambush = 1,
    Defend = 2,
    AttackEnemy = 3,
    MoveAttack = 4,
    MoveAvoid = 5,
};

enum class BattleOriginalAiOrderTargetInput : uint8_t {
    None = 0,
    MapLocation = 2,
    LiveObjectOrBase = 3,
};

struct BattleOriginalAiLanceOrderDiagnostic {
    bool exact = false;
    int16_t controlWord00 = -1;
    bool selectable = false;
    BattleOriginalAiLanceOrder order =
        BattleOriginalAiLanceOrder::ActOnOwn;
    BattleOriginalAiOrderTargetInput targetInput =
        BattleOriginalAiOrderTargetInput::None;
    const char* label = "";
};

BattleOriginalAiLanceOrderDiagnostic battleOriginalAiLanceOrder(
    int16_t controlWord00);

// Exact dispatch owned by the Act On Own (+0x00 == 0) branch of 7797. The
// numeric side mode is a per-battle DS:ED0/ED2 word, not a companion order or
// a replacement AI state. Only the two routes that enter 7c3c use the
// recovered cached-live-target selector.
enum class BattleOriginalAiActOnOwnRoute : uint8_t {
    Open = 0,
    SelectedCachedLiveTarget,
    ModeOneTablePoint,
    SeparateObjectPoint,
    StatusSevenReturn,
};

struct BattleOriginalAiActOnOwnRouteDiagnostic {
    bool exact = false;
    int16_t controlWord00 = -1;
    int16_t numericSideModeWord = -1;
    bool actOnOwn = false;
    BattleOriginalAiActOnOwnRoute route =
        BattleOriginalAiActOnOwnRoute::Open;
    bool callsTargetSelector23bf = false;
    bool callsMovementPoint94a4 = false;
    bool requiresSideDirectionFlags = false;
    bool requiresSeparateObjectPose = false;
    bool writesStatus46 = false;
    int16_t status46 = -1;
};

BattleOriginalAiActOnOwnRouteDiagnostic battleOriginalAiActOnOwnRoute7797(
    int16_t controlWord00,
    int16_t numericSideModeWord);

const char* battleEnemyAiTargetKindName(BattleEnemyAiTargetKind kind);

enum class BattleMechSystemRole : uint8_t {
    Unknown,
    Core,
    Cockpit,
    Mobility,
    Weapons,
    JumpJets,
};

const char* battleMechSystemRoleName(BattleMechSystemRole role);

enum class BattleMechSystemStatus : uint8_t {
    Online,
    Degraded,
    Offline,
    Destroyed,
};

const char* battleMechSystemStatusName(BattleMechSystemStatus status);

struct BattleMechSystemSnapshot {
    std::string systemId;
    std::string label;
    BattleMechSystemRole role = BattleMechSystemRole::Unknown;
    BattleMechSystemStatus status = BattleMechSystemStatus::Online;
    int damage = 0;
    int maxDamage = 0;
    std::vector<int> sourceComponentIds;
    bool movementCritical = false;
    bool combatCritical = false;
};

struct BattleMechSystemsSnapshot {
    bool valid = false;
    std::string provenance;
    bool wholeMechDestroyed = false;
    bool mobilityOnline = true;
    bool cockpitOnline = true;
    bool weaponsOnline = true;
    bool jumpJetsOnline = false;
    std::vector<BattleMechSystemSnapshot> systems;
};

struct BattleCombatantRosterMetadata {
    BattleTeam team = BattleTeam::Neutral;
    std::optional<uint8_t> factionHouseId;
    std::string factionHouseName;
    std::string provenance;
    std::string sourceSlot;
    std::optional<uint8_t> originalLiveObjectSlot;
    uint8_t gunnerySkill = 1;
};

struct BattleTerrainSnapshot {
    bool scenarioLoaded = false;
    size_t scenarioIndex = 0;
    uint8_t terrainMode = 0;
    std::vector<std::string> tileNames;
    std::optional<int> environmentId;
    double boundsMinX = 0.0;
    double boundsMaxX = 0.0;
    double boundsMinZ = 0.0;
    double boundsMaxZ = 0.0;
    bool collisionGridValid = false;
    std::string collisionGridProvenance;
    int collisionGridWidth = 0;
    int collisionGridHeight = 0;
    double collisionGridCellSize = 0.0;
    size_t collisionGridBlockingSampleCount = 0;
    uint64_t collisionGridFingerprint = 0;
    size_t collisionObstacleCount = 0;
    uint64_t collisionObstacleFingerprint = 0;
    size_t driveableSurfaceFeatureCount = 0;
    uint64_t driveableSurfaceFeatureFingerprint = 0;
    bool originalTerrainSceneValid = false;
    std::string originalTerrainSceneProvenance;
    size_t originalTerrainCollisionRecordCount = 0;
    size_t originalTerrainSceneObjectCount = 0;
    size_t originalTerrainQueryObjectCount = 0;
    uint64_t originalTerrainSceneFingerprint = 0;
};

struct BattleTerrainCollisionGrid {
    bool valid = false;
    std::string provenance;
    int width = 0;
    int height = 0;
    double cellSize = 0.0;
    std::vector<uint8_t> rawSamples;
};

BattleTerrainCollisionGrid loadOriginalBattleTerrainCollisionGrid(
    const std::vector<std::filesystem::path>& gridPaths,
    double cellSize);

struct BattleTerrainCollisionObstacle {
    uint32_t stableId = 0;
    uint16_t recordIndex = 0;
    size_t worldTileIndex = 0;
    size_t sourceIndex = 0;
    double centerX = 0.0;
    double centerZ = 0.0;
    double halfExtentX = 0.0;
    double halfExtentZ = 0.0;
    double height = 0.0;
    struct FootprintPoint {
        double x = 0.0;
        double z = 0.0;
    };
    std::vector<FootprintPoint> footprint;
};

struct BattleTerrainDriveableSurfaceFeature {
    uint32_t stableId = 0;
    uint16_t recordIndex = 0;
    size_t worldTileIndex = 0;
    size_t sourceIndex = 0;
    double centerX = 0.0;
    double centerZ = 0.0;
    double halfExtentX = 0.0;
    double halfExtentZ = 0.0;
    double height = 0.0;
    std::string interaction = "slope_pitch_deferred_nonblocking";
};

struct BattleTerrainCollisionObstacles {
    bool valid = false;
    std::string provenance;
    std::vector<BattleTerrainCollisionObstacle> entries;
    std::vector<BattleTerrainDriveableSurfaceFeature> driveableSurfaces;
};

BattleTerrainCollisionObstacles loadOriginalBattleTerrainCollisionObstacles(
    const std::filesystem::path& terrainShapePath,
    const std::vector<std::filesystem::path>& worldPaths,
    double boundsMinX,
    double boundsMaxX,
    double boundsMinZ,
    double boundsMaxZ,
    double cellSize);

struct BattleOriginalTerrainSceneObject {
    size_t sourceWorldIndex = 0;
    size_t sourceObjectIndex = 0;
    uint16_t recordIndex = 0;
    uint8_t objectFlags = 0;
    int32_t rawX = 0;
    int32_t rawZ = 0;
    int32_t rawY = 0;
};

struct BattleOriginalTerrainSceneCatalog {
    bool valid = false;
    std::string provenance;
    size_t sourceWorldCount = 0;
    std::vector<legacy3d::TerpckGiCollisionRecord> collisionRecords;
    std::vector<uint8_t> recordScaleShifts;
    std::vector<BattleOriginalTerrainSceneObject> objects;
    std::vector<size_t> queryObjectIndices;
};

BattleOriginalTerrainSceneCatalog loadOriginalBattleTerrainSceneCatalog(
    const std::filesystem::path& terpckGiPath,
    const std::filesystem::path& terpckTblPath,
    const std::vector<std::filesystem::path>& worldPaths);

struct BattleOriginalTerrainSceneQueryDiagnostic {
    bool exact = false;
    bool catalogAvailable = false;
    bool cacheContractValid = true;
    bool cachedObjectTested = false;
    bool cachedObjectAccepted = false;
    bool clearedCacheAfterMiss = false;
    size_t visitedObjectCount = 0;
    size_t planarQueryCount = 0;
    bool hit = false;
    size_t selectedSceneObjectIndex = std::numeric_limits<size_t>::max();
    uint16_t selectedRecordIndex = 0xffffu;
    uint8_t selectedPriority = 0;
    uint8_t selectedResultByte = 0;
    uint8_t selectedSubrecordIndex = 0xffu;
    int32_t scaledDeltaX = 0;
    int32_t scaledDeltaZ = 0;
    int16_t localHeightWord = 0;
    int32_t worldHeight = 0;
    legacy3d::TerpckGiContactCorrection contactCorrection;
};

BattleOriginalTerrainSceneQueryDiagnostic battleOriginalTerrainSceneQuery(
    const BattleOriginalTerrainSceneCatalog& catalog,
    int32_t queryX,
    int32_t queryZ,
    uint8_t liveByte15,
    uint8_t liveByte17,
    uint8_t liveByte19,
    std::optional<size_t> cachedSceneObjectIndex = std::nullopt,
    std::optional<uint8_t> cachedSubrecordIndex = std::nullopt);

enum class BattlefieldBoundaryEdge : uint8_t {
    North = 0,
    South = 1,
    West = 2,
    East = 3,
};

const char* battlefieldBoundaryEdgeName(BattlefieldBoundaryEdge edge);

constexpr uint8_t battlefieldBoundaryEdgeMask(BattlefieldBoundaryEdge edge) {
    return static_cast<uint8_t>(1u << static_cast<uint8_t>(edge));
}

constexpr int battlefieldActorExitStatus(BattlefieldBoundaryEdge edge) {
    return -(static_cast<int>(edge) + 2);
}

struct BattlefieldBoundaryState {
    bool valid = false;
    std::string provenance;
    int32_t originalMinX = 0;
    int32_t originalMaxX = 0;
    int32_t originalMinZ = 0;
    int32_t originalMaxZ = 0;
    double worldMinX = 0.0;
    double worldMaxX = 0.0;
    double worldMinZ = 0.0;
    double worldMaxZ = 0.0;
    bool mapProjectionProven = false;
    bool actorExitEncodingProven = false;
    bool exitMaskSemanticsProven = false;
    uint8_t playerAllowedExitMask = 0;
    uint8_t opposingAllowedExitMask = 0;
    bool sideCompletionExitPolicyProven = false;
    bool playerExitOutcomeProven = false;
    int ordinaryPlayerExitResultCode = -1;
    int allowedPlayerExitResultCode = -1;
    bool outcomeEvaluationDeferred = true;
};

enum class BattleMissionRuntimeState : uint8_t {
    InProgress,
    Victory,
    Defeat,
    Withdraw,
    Unsupported,
};

const char* battleMissionRuntimeStateName(BattleMissionRuntimeState state);

struct BattleResult {
    bool valid = false;
    BattleMissionRuntimeState state = BattleMissionRuntimeState::InProgress;
    std::string reason;
    int rawResultCode = -1;
    uint64_t terminalTickIndex = 0;
    uint64_t terminalElapsedMs = 0;
    EntityId sourceEntityId{};
    int actorExitStatus = 0;
    BattlefieldBoundaryEdge exitEdge = BattlefieldBoundaryEdge::North;
    uint8_t exitMask = 0;
    bool exitAllowed = false;
};

struct BattleMissionConclusionDelayState {
    bool active = false;
    BattleMissionRuntimeState pendingState =
        BattleMissionRuntimeState::InProgress;
    std::string reason;
    uint64_t startTickIndex = 0;
    uint64_t resolveTickIndex = 0;
    EntityId sourceEntityId{};
};

struct BattleSetupSlotMetadata {
    bool valid = false;
    std::string role;
    size_t slotIndex = 0;
    Transform transform{};
    double gridX = 0.0;
    double gridY = 0.0;
    bool originalRawPositionProven = false;
    int32_t originalRawX = 0;
    int32_t originalRawZ = 0;
};

struct BattleSetupObjectiveMetadata {
    bool valid = false;
    std::string provenance = "btech_mode4_dedicated_transform";
    int sourceSide = -1;
    int placementMode = -1;
    uint8_t bankId = 0;
    size_t coordinateOffset = 0;
    int legacyTargetIndex = 8;
    int resourceRecordIndex = 14;
    Transform transform{};
    double gridX = 0.0;
    double gridY = 0.0;
};

struct BattleSetupMetadata {
    bool valid = false;
    std::string provenance;
    size_t scenarioIndex = 0;
    bool missionSelectorContractProven = false;
    bool briefingMissionIdValid = false;
    size_t briefingMissionId = 0;
    uint8_t handoffMissionId = 0;
    size_t initialPlacementSelector = 0;
    size_t missionSelector = 0;
    bool extendedSequenceRemapRequired = false;
    bool placementSelectorRuntimeResolved = false;
    std::string missionSelectorProvenance;
    uint8_t terrainMode = 0;
    std::vector<std::string> tileNames;
    int playerMode = 0;
    int opposingMode = 0;
    uint8_t playerBankId = 0;
    uint8_t opposingBankId = 0;
    size_t playerSlotIndex = 0;
    size_t objectiveOpposingSlotIndex = 0;
    std::string objectiveSourceSlot = "opposing:1";
    Transform initialPlayerTransform{};
    std::vector<BattleSetupSlotMetadata> playerSlots;
    std::vector<BattleSetupSlotMetadata> opposingSlots;
    BattleSetupObjectiveMetadata dedicatedObjective;
};

struct BattleStaticModelRef {
    bool valid = false;
    std::string provenance;
    std::string resourceName;
    int recordIndex = -1;
    std::string swizzle = "xzy";
    double scale = 1.0;
    bool groundAligned = true;
    bool animated = false;
};

BattleStaticModelRef originalBattleObjectiveStaticModel();

struct BattleObjectiveState {
    bool valid = false;
    std::string role = "target";
    std::string provenance = "setup_derived_diagnostic";
    std::string sourceSlot = "opposing:1";
    BattleMissionObjectiveIntent missionIntent = BattleMissionObjectiveIntent::Unknown;
    bool missionIntentProven = false;
    std::string missionTargetKind;
    std::string missionIntentProvenance;
    bool missionIntentBoundToObjective = false;
    bool transformProven = false;
    bool activeObjectProven = false;
    bool missionSemanticsProven = false;
    bool damagePolicyProven = false;
    bool damageSuppressed = false;
    bool depletionPolicyProven = false;
    bool depletionSetsPlayerWinCondition = false;
    bool depletionSetsPlayerLossCondition = false;
    int depletionResultCode = -1;
    int damage = 0;
    int maxDamage = 0;
    bool depleted = false;
    EntityId lastDamageSourceEntityId{};
    Transform transform{};
    double gridX = 0.0;
    double gridY = 0.0;
    BattleStaticModelRef staticModel;
};

constexpr bool battleObjectiveProtectedByPlayer(
    const BattleObjectiveState& objective) {
    return objective.valid && objective.missionIntentProven &&
        objective.missionIntentBoundToObjective &&
        objective.missionSemanticsProven &&
        objective.missionIntent == BattleMissionObjectiveIntent::Protect;
}

struct BattleContractMetadata {
    bool valid = false;
    std::string provenance;
    uint8_t employerHouseId = 0;
    std::string employerHouseName;
    uint8_t targetHouseId = 0;
    std::string targetHouseName;
    bool hasHostileTargetHouse = true;
    std::string targetPlanetName;
    uint8_t targetPlanetTerrainCode = 0;
    std::optional<int> targetEnvironmentId;
    size_t terrainScenarioIndex = 2;
    std::string terrainScenarioProvenance = "phase11_fixed_scenario_2";
    int estimatedHeavyMechs = 0;
    int estimatedMediumMechs = 0;
    int estimatedLightMechs = 0;
    bool oppositionEstimateVariable = true;
    bool garrisonAutoResolveDeferred = true;
    BattleOppositionSpawnPlan oppositionSpawnPlan;
    int priceK = 0;
    int salvagePercent = 0;
    int advancePercent = 0;
};

struct BattlePersistentMechState {
    bool valid = false;
    std::string provenance;
    uint8_t engine = 0;
    uint8_t gyros = 0;
    uint8_t sensors = 0;
    uint8_t lifeSupport = 0;
    uint8_t heatSinksWorking = 0;
    uint8_t heatSinksTotal = 0;
    uint8_t leftArmActuator = 0;
    uint8_t rightArmActuator = 0;
    uint8_t leftLegActuator = 0;
    uint8_t rightLegActuator = 0;
    uint8_t jumpJetsWorking = 0;
    uint8_t jumpJetsTotal = 0;
    uint8_t armorPercent = 100;
    std::array<uint8_t, 10> weaponConditions{};
    std::array<uint8_t, 9> armorDamage{};
};

enum class BattleWeaponAmmunitionOwnership : uint8_t {
    EnergyNoAmmunition,
    SharedAmmunitionPool,
};

enum class BattleWeaponReadiness : uint8_t {
    Ready,
    Cooldown,
    NoAmmunition,
    NonFunctional,
    MechOffline,
};

enum class BattleHeatCoolingPolicy : uint8_t {
    // FUN_1000_ad86 doubles the working-sink term when DS:1E86 is 2.  The
    // campaign environment/resource bridge identifies that value as arctic.
    OriginalCoolingWithArcticDouble,
};

constexpr int battleMaximumRawHeat() {
    return 0x960;
}

constexpr int battleRawHeatPerGaugeSegment() {
    return 0x50;
}

constexpr int battleHeatGaugeSegmentCount() {
    return battleMaximumRawHeat() / battleRawHeatPerGaugeSegment();
}

constexpr int battleOriginalReactorShutdownRawHeat() {
    return 0x640;
}

constexpr uint64_t battleOriginalReactorShutdownFlashUpdates() {
    return 4u;
}

constexpr int battleOriginalVerticalJumpRawHeat() {
    return 0x0c;
}

constexpr int battleOriginalForwardJumpRawHeat() {
    return 0x14;
}

struct BattleHeatState {
    bool enabled = false;
    int rawHeat = 0;
    double originalUpdateAccumulator = 0.0;
    uint64_t originalUpdateCount = 0;
    bool reactorShutdown = false;
    uint64_t reactorShutdownStateChangedTickIndex = 0;
    int lastWeaponHeatAdded = 0;
    int lastJumpJetHeatAdded = 0;
    uint64_t lastJumpJetHeatTickIndex = 0;
    int lastGroundMovementHeatAdded = 0;
    uint64_t lastGroundMovementHeatTickIndex = 0;
    int lastEngineHeatAdded = 0;
    uint64_t lastEngineHeatTickIndex = 0;
    int lastCoolingApplied = 0;
    uint64_t lastCoolingTickIndex = 0;
    uint8_t lastCoolingMultiplier = 1;
    BattleHeatCoolingPolicy coolingPolicy =
        BattleHeatCoolingPolicy::OriginalCoolingWithArcticDouble;
    bool originalWeaponHeatAdditionProven = true;
    bool originalJumpJetHeatValuesProven = true;
    bool jumpJetControlMappingProven = false;
    bool originalGroundMovementHeatThresholdsProven = true;
    bool groundMovementSpeedMappingProven = false;
    bool originalEngineDamageHeatProven = true;
    bool originalNormalCoolingFormulaProven = true;
    bool originalArcticDoubleCoolingProven = true;
    bool originalReactorShutdownThresholdProven = true;
    bool originalReactorShutdownRecoveryProven = true;
    bool originalReactorShutdownMessageCadenceProven = true;
    bool originalUpdateCadenceProven = false;
};

bool battleReactorShutdownMessageVisible(const BattleHeatState& heat);

enum class BattleMajorSystemId : uint8_t {
    Sensors = 0,
    Gyros = 1,
    Engine = 2,
    LifeSupport = 3,
};

struct BattleMajorSystemWarningEvent {
    bool valid = false;
    uint64_t sequence = 0;
    uint64_t tickIndex = 0;
    BattleMajorSystemId system = BattleMajorSystemId::Sensors;
    BattleMechSystemStatus previousStatus = BattleMechSystemStatus::Online;
    BattleMechSystemStatus currentStatus = BattleMechSystemStatus::Online;
};

// Runtime consequences are derived from the four authoritative critical
// component entries.  They do not duplicate or mutate damage state.
struct BattleMajorSystemRuntimeState {
    bool enabled = false;
    BattleMechSystemStatus sensors = BattleMechSystemStatus::Online;
    BattleMechSystemStatus gyros = BattleMechSystemStatus::Online;
    BattleMechSystemStatus engine = BattleMechSystemStatus::Online;
    BattleMechSystemStatus lifeSupport = BattleMechSystemStatus::Online;
    bool crosshairVisible = true;
    bool targetingAvailable = true;
    bool topographicMapVisible = true;
    bool radarContactsVisible = true;
    double gyroMovementScale = 1.0;
    bool movementBlocked = false;
    bool engineShutdown = false;
    bool lifeSupportFailure = false;
    int engineHeatPerOriginalUpdate = 0;
    bool sensorBlinkCadenceProven = false;
    bool gyroDamageScaleProven = false;
    bool destroyedSystemEffectsProven = true;
};

BattleMajorSystemRuntimeState battleMajorSystemRuntimeState(
    const mech3d::MechDetailedDamageState& damage,
    uint64_t elapsedMs,
    bool enabled = true);

enum class BattleWeaponDeliveryMode : uint8_t {
    Immediate,
    OriginalProjectilePool,
};

enum class BattleShotDeliveryState : uint8_t {
    None,
    Immediate,
    ProjectileInFlight,
    ProjectileImpact,
    ProjectileTerrainImpact,
    ProjectileIntercepted,
    ProjectileExpired,
};

enum class BattleShotHitLocation : uint8_t {
    UnresolvedOriginalDistribution,
    CenterTorsoProvisionalStationaryLab,
    VisibleComponentOriginalIdentity,
    TorsoPartitionProvisional,
    OriginalAiAspect2d6Table,
};

enum class BattleShotRuntimeEffect : uint8_t {
    None,
    CoarseCoreSystemDamageBridge,
    DetailedArmorSectionDamage,
    DetailedArmorInternalDamage,
    DetailedCriticalResolved,
    DetailedCriticalResolutionPending,
    ProjectileTerrainSmokeNoDamage,
    ProjectileInterceptedNoDamage,
    ObjectiveDamage,
};

enum class BattleCriticalHitKind : uint8_t {
    None,
    Engine,
    Gyros,
    Sensors,
    LifeSupport,
    CockpitFlag,
    Actuator,
    InstalledWeapon,
    AmmunitionBin,
    JumpJet,
};

enum class BattleShotTargetKind : uint8_t {
    None,
    Combatant,
    Objective,
    Terrain,
    Projectile,
};

enum class BattleShotResult : uint8_t {
    None,
    Hit,
    MissUnresolved,
    MissOutOfRange,
    MissCrosshairRay,
    HitTerrain,
    HitProjectile,
    RejectedNoTarget,
    RejectedTargetPolicy,
    RejectedProjectilePoolFull,
};

enum class BattleOriginalAiDecisionResult : uint8_t {
    None,
    RejectedShooterNotStationary,
    RejectedTargetCardinality,
    RejectedTargetNotStationary,
    RejectedNoReadyWeaponAcquisition,
    RejectedTargetAcquisitionRange,
    RejectedTargetVerticalEnvelope,
    RejectedTargetAimEnvelope,
    RejectedCandidateStateOpen,
    RejectedCandidateAimGeometryOpen,
    RejectedFriendlyFireLane,
    RejectedShooterOffline,
    RejectedWeaponNonFunctional,
    RejectedWeaponCooldown,
    RejectedWeaponNoAmmunition,
    RejectedWeaponOutOfRange,
    RejectedWeaponHeatGate,
    RejectedNoEligibleWeapon,
    FireRequested,
};

enum class BattleOriginalAiAttackAspect : uint8_t {
    Front,
    Rear,
    Left,
    Right,
};

enum class BattleWeaponTargetPolicy : uint8_t {
    UnresolvedFailClosed,
    SelectedScannerTargetProvisional,
    SelectedScannerTargetCrosshairRayProvisional,
    CrosshairRayVisibleCombatantProvisional,
    LegacyNearestOpposingDiagnostic,
};

struct BattleHitPoint {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct BattleOriginalAiCandidateGeometryDiagnostic {
    bool exact = false;
    bool insideEnvelope = false;
    bool planarEnvelopePassed = false;
    bool verticalEnvelopePassed = false;
    bool centerline = false;
    uint32_t distance = 0;
    int16_t shooterHeading = 0;
    int16_t candidateHeading = 0;
    int16_t yawDelta = 0;
    int16_t planarHalfWidth = 0;
    int16_t verticalAngle = 0;
    int16_t verticalDelta = 0;
    int16_t verticalLimit = 0x0b60;
};

BattleOriginalAiCandidateGeometryDiagnostic
battleOriginalAiZeroPitchCandidateGeometry(
    const Transform& shooter,
    const Transform& candidate);

// Pure fixed-word reconstruction of the component-heading correction emitted
// by BTECH FUN_1000_8307.  This is diagnostic evidence only: applying the
// command to live locomotion still requires the 87c4 integration/collision
// caller and an original-update cadence binding.
struct BattleOriginalAiComponentHeadingDiagnostic {
    bool exact = false;
    int16_t bodyHeading = 0;
    int16_t componentRelativeHeadingBefore = 0;
    int16_t desiredHeading = 0;
    int16_t signedCombinedError = 0;
    int16_t limitedCorrection = 0;
    int16_t componentRelativeHeadingCandidate = 0;
    int16_t componentRelativeHeadingAfter = 0;
    uint16_t maximumCorrection = 0x038e;
    uint16_t strictComponentLimit = 0x18e2;
    bool strictComponentLimitPassed = false;
};

BattleOriginalAiComponentHeadingDiagnostic
battleOriginalAiComponentHeadingCommand(
    int16_t bodyHeading,
    int16_t componentRelativeHeading,
    int16_t desiredHeading);

struct BattleOriginalAiBodyTurnCommandDiagnostic {
    bool exact = false;
    int16_t desiredHeading = 0;
    int16_t currentHeading = 0;
    int16_t wrappedDelta = 0;
    int16_t turnCommand = 0;
    uint16_t maximumTurnCommand = 0x0222;
};

BattleOriginalAiBodyTurnCommandDiagnostic
battleOriginalAiBodyTurnCommand(
    int16_t desiredHeading,
    int16_t currentHeading);

// Pure 16-bit reconstruction of the ordinary raw-speed write in BTECH
// FUN_1000_84de.  The +0x06 definition word and live left/right leg actuator
// words retain their raw values.  This diagnostic is not a world-unit or
// fixed-tick movement command.
struct BattleOriginalAiRawSpeedCommandDiagnostic {
    bool exact = false;
    int16_t definitionWord06 = 0;
    int16_t heatSpeedPenalty = 0;
    int16_t leftLegActuatorWord = 0;
    int16_t rightLegActuatorWord = 0;
    int16_t limitingLegActuatorWord = 0;
    int16_t wrappedProduct = 0;
    int16_t forwardDesiredRawSpeed = 0;
    int16_t movementMode = 0;
    int16_t desiredRawSpeed = 0;
    int16_t currentRawSpeed = 0;
    int16_t requestedDelta = 0;
    int16_t appliedDelta = 0;
    int16_t nextRawSpeed = 0;
    bool positiveAccelerationCapped = false;
};

BattleOriginalAiRawSpeedCommandDiagnostic
battleOriginalAiRawSpeedCommand(
    int16_t definitionWord06,
    int16_t heatSpeedPenalty,
    int16_t leftLegActuatorWord,
    int16_t rightLegActuatorWord,
    int16_t movementMode,
    int16_t currentRawSpeed);

// Exact BTECH c5ac planar branch for zero pitch and roll. The fixed heading
// selects source Q14 sine/cosine words; signed products use the original
// arithmetic shift by fourteen before 32-bit wrapped position addition.
struct BattleOriginalAiPlanarPoseStepDiagnostic {
    bool exact = false;
    int32_t xBefore = 0;
    int32_t zBefore = 0;
    int16_t heading = 0;
    int16_t rawSpeed = 0;
    int16_t sineQ14 = 0;
    int16_t cosineQ14 = 0;
    int32_t speedDeltaX = 0;
    int32_t speedDeltaZ = 0;
    int16_t driftX = 0;
    int16_t driftZ = 0;
    int32_t xAfter = 0;
    int32_t zAfter = 0;
};

BattleOriginalAiPlanarPoseStepDiagnostic
battleOriginalAiPlanarPoseStep(
    int32_t x,
    int32_t z,
    int16_t heading,
    int16_t rawSpeed,
    int16_t driftX,
    int16_t driftZ);

struct BattleOriginalAiPoseStepDiagnostic {
    bool exact = false;
    int32_t xBefore = 0;
    int32_t yBefore = 0;
    int32_t zBefore = 0;
    int16_t pitchBefore = 0;
    int16_t rollBefore = 0;
    int16_t headingBefore = 0;
    int16_t pitchCorrection = 0;
    int16_t rollCorrection = 0;
    int16_t turnCommand = 0;
    int16_t pitchAfter = 0;
    int16_t rollAfter = 0;
    int16_t headingAfter = 0;
    int16_t rawSpeed = 0;
    int16_t sineHeadingQ14 = 0;
    int16_t cosineHeadingQ14 = 0;
    int16_t sinePitchQ14 = 0;
    int16_t cosinePitchQ14 = 0;
    int32_t speedDeltaX = 0;
    int32_t speedDeltaY = 0;
    int32_t speedDeltaZ = 0;
    int16_t driftX = 0;
    int16_t driftZ = 0;
    int16_t driftY = 0;
    int32_t xAfter = 0;
    int32_t yAfter = 0;
    int32_t zAfter = 0;
};

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
    int16_t driftY);

// Exact ordinary (control state +0x51 != 3) BTECH 87c4 commit/rollback
// decision after its c42e and 89ea probes have produced caller-owned numeric
// results.  This does not emulate either probe or authorize live movement.
struct BattleOriginalAiOrdinaryPoseCommitDiagnostic {
    bool exact = false;
    int16_t rawSpeed = 0;
    uint16_t previousSlotProbeWord = 0;
    uint16_t nextSlotProbeWord = 0;
    uint8_t sceneProbeResult = 0;
    bool movingBranch = false;
    bool poseAccepted = false;
    bool poseRestored = false;
    bool writesAssociatedRecordProbeFlag = false;
    bool associatedRecordProbeFlagValue = false;
    bool clearsControlWords = false;
    bool clearsMotionWords30Through3c = false;
    int16_t rollbackHeadingDelta = 0;
};

BattleOriginalAiOrdinaryPoseCommitDiagnostic
battleOriginalAiOrdinaryPoseCommit(
    int16_t rawSpeed,
    uint16_t previousSlotProbeWord,
    uint16_t nextSlotProbeWord,
    uint8_t sceneProbeResult,
    bool xLowWordOdd);

struct BattleOriginalAiOrdinaryPoseTransactionDiagnostic {
    bool exact = false;
    BattleOriginalAiPoseStepDiagnostic tentativeStep;
    BattleOriginalTerrainSceneQueryDiagnostic sceneQuery;
    BattleOriginalAiOrdinaryPoseCommitDiagnostic commit;
    int32_t xAfter = 0;
    int32_t yAfter = 0;
    int32_t zAfter = 0;
    int16_t pitchAfter = 0;
    int16_t rollAfter = 0;
    int16_t headingAfter = 0;
    int16_t controlWord30After = 0;
    int16_t controlWord32After = 0;
    int16_t controlWord34After = 0;
    int16_t driftWord36After = 0;
    int16_t driftWord38After = 0;
    int16_t driftWord3aAfter = 0;
    int16_t rawSpeedAfter = 0;
    int16_t controlState4cAfter = 0;
    int16_t controlState53After = 0;
    uint16_t slotProbeWordAfter = 0;
    bool setsMovingFlagBit4 = false;
    bool associatedRecordProbeFlagWritten = false;
    bool associatedRecordProbeFlagValue = false;
    bool sceneCacheValidAfter = false;
    size_t sceneCacheObjectIndexAfter = std::numeric_limits<size_t>::max();
    uint8_t sceneCacheSubrecordIndexAfter = 0xffu;
};

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
    std::optional<size_t> cachedSceneObjectIndex = std::nullopt,
    std::optional<uint8_t> cachedSubrecordIndex = std::nullopt);

// Exact accepted ordinary-action subset of BTECH 9bfa.  It deliberately
// admits only the mode-zero command-bit-0x10 path reached after a nonzero
// accepted 87c4 movement transaction.
struct BattleOriginalAiOrdinaryModeZeroPostStepDiagnostic {
    bool exact = false;
    int16_t animationModeWord4cBefore = 0;
    uint8_t commandByte50Before = 0;
    int16_t sharedStateWord51Before = 0;
    int16_t frameWord53Before = 0;
    uint8_t selectedActionCode = 0;
    uint8_t modeZeroFrameCount = 0;
    int16_t frameWord53AfterIncrement = 0;
    int16_t animationModeWord4cAfter = 0;
    uint8_t commandByte50After = 0;
    int16_t sharedStateWord51After = 0;
    int16_t frameWord53After = 0;
    bool componentAnimationSideEffectsExcluded = true;
};

BattleOriginalAiOrdinaryModeZeroPostStepDiagnostic
battleOriginalAiOrdinaryModeZeroPostStep9bfa(
    int16_t animationModeWord4c,
    uint8_t commandByte50,
    int16_t sharedStateWord51,
    int16_t frameWord53);

// Exact raw-coordinate result from BTECH 0001:2738. Codes 0..3 use the
// BattlefieldBoundaryEdge numeric order; 4 means inside the inclusive bounds.
struct BattleOriginalAiRawBoundaryProbeDiagnostic {
    bool exact = false;
    int32_t rawX = 0;
    int32_t rawZ = 0;
    uint8_t resultCode = 4;
    bool inside = true;
    bool xAxisWon = false;
};

BattleOriginalAiRawBoundaryProbeDiagnostic
battleOriginalAiRawBoundaryProbe(int32_t rawX, int32_t rawZ);

// Exact one-cell BTECH 5454 relation dispatch. The two visibility inputs stay
// explicit because 5206 is simulation-owned while 50ee consumes original
// display/clipping state. This helper never reads replacement renderer state.
struct BattleOriginalAiRelationCellDiagnosticInput {
    bool firstSlotOccupied = false;
    bool secondSlotOccupied = false;
    uint8_t firstSlot = 0;
    uint8_t secondSlot = 0;
    int16_t firstSideWord = 0;
    int16_t secondSideWord = 0;
    int16_t displayModeWordE1c = 0;
    bool candidateClipFlagBit2 = false;
    bool playerViewStrictBoundsPassed = false;
    bool selectedComponentDisplayListBit1 = false;
    bool simulationVisibility5206 = false;
};

struct BattleOriginalAiRelationCellDiagnostic {
    bool exact = false;
    bool orderedSidePair = false;
    bool usesDisplayVisibility50ee = false;
    bool usesSimulationVisibility5206 = false;
    bool relationValue = false;
    bool mirroredValue = false;
};

BattleOriginalAiRelationCellDiagnostic battleOriginalAiRelationCell(
    const BattleOriginalAiRelationCellDiagnosticInput& input);

// Exact BTECH 50ee return-value diagnostic. The selected-component byte is
// retained legacy DOS display-list state from the previous completed frame;
// callers must supply it explicitly and must never substitute renderer state.
struct BattleOriginalAiDisplayVisibility50eeInput {
    int16_t targetIndex = 0;
    bool targetRawPoseAvailable = false;
    int32_t targetRawX = 0;
    int32_t targetRawZ = 0;
    bool playerViewRawPoseAvailable = false;
    int32_t playerViewRawX = 0;
    int32_t playerViewRawZ = 0;
    bool extendedControlWordAvailable = false;
    int16_t extendedControlWord = 0;
    bool selectedComponentPointerAvailable = false;
    uint8_t selectedComponentByte1 = 0;
};

struct BattleOriginalAiDisplayVisibility50eeDiagnostic {
    bool exact = false;
    bool targetIndexSupported = false;
    bool usesLiveObjectPointer = false;
    bool usesSeparateObjectPointer = false;
    bool extendedControlGateRequired = false;
    bool extendedControlGatePassed = false;
    int32_t deltaX = 0;
    int32_t deltaZ = 0;
    uint32_t xMagnitude = 0;
    uint32_t zMagnitude = 0;
    bool xStrictBoundsPassed = false;
    bool zStrictBoundsPassed = false;
    bool strictBoundsPassed = false;
    bool selectedComponentPointerRead = false;
    bool selectedComponentDisplayListBit1 = false;
    uint16_t relationWord = 0;
};

BattleOriginalAiDisplayVisibility50eeDiagnostic
battleOriginalAiDisplayVisibility50ee(
    const BattleOriginalAiDisplayVisibility50eeInput& input);

// Exact immediate-return subset of BTECH 5206. The sampled GRD line path is
// deliberately not approximated: either strict planar gate at 0x200 or above
// leaves exact=false and sampledTerrainPathOpen=true.
struct BattleOriginalAiSimulationVisibilityNearDiagnostic {
    bool exact = false;
    bool endpointOrderingExact = false;
    bool endpointsSwapped = false;
    int32_t orderedFirstX = 0;
    int32_t orderedFirstZ = 0;
    int32_t orderedFirstY = 0;
    int32_t orderedSecondX = 0;
    int32_t orderedSecondZ = 0;
    int32_t orderedSecondY = 0;
    int32_t rawDeltaX = 0;
    int32_t rawDeltaZ = 0;
    int32_t rawDeltaY = 0;
    uint16_t zMagnitudeLow = 0;
    int16_t zMagnitudeHigh = 0;
    bool strictXBelow0x200 = false;
    bool strictZBelow0x200 = false;
    bool immediateVisible = false;
    bool sampledTerrainPathOpen = false;
    bool terrainGridRead = false;
    bool endpointHeightAffectsImmediateResult = false;
    uint16_t relationWord = 0;
};

BattleOriginalAiSimulationVisibilityNearDiagnostic
battleOriginalAiSimulationVisibility5206Near(
    int32_t firstX,
    int32_t firstZ,
    int32_t firstY,
    int32_t secondX,
    int32_t secondZ,
    int32_t secondY);

// Exact in-bounds sampled path of BTECH 5206 -> 5559.  Each step records the
// original terminal/overshoot probe as well as the GRD byte used as
// terrainHeight=rawSample<<4.  A sample which would take 5559's far D5CE path
// leaves exact=false instead of substituting a flat or clamped result.
struct BattleOriginalAiSimulationVisibilitySample5206Diagnostic {
    int32_t rawX = 0;
    int32_t rawZ = 0;
    int32_t rawY = 0;
    bool reachesOrPassesEndpoint = false;
    int16_t gridX = -1;
    int16_t gridZ = -1;
    uint8_t rawTerrainSample = 0;
    uint16_t terrainHeight = 0;
    bool terrainBlocks = false;
};

struct BattleOriginalAiSimulationVisibilitySampledDiagnostic {
    bool exact = false;
    bool gridContractValid = false;
    BattleOriginalAiSimulationVisibilityNearDiagnostic nearPath;
    bool immediateVisible = false;
    bool sampledTerrainPathUsed = false;
    bool xMajorAxis = false;
    bool zMajorAxis = false;
    bool negativeZDirection = false;
    int16_t rawStepX = 0;
    int16_t rawStepZ = 0;
    int16_t rawStepY = 0;
    uint16_t sampledPointCount = 0;
    bool terminalSampleVisited = false;
    bool farSceneQueryOpen = false;
    bool blockedByTerrain = false;
    uint16_t blockingSampleIndex = 0xffffu;
    uint16_t relationWord = 0;
    std::vector<BattleOriginalAiSimulationVisibilitySample5206Diagnostic>
        samples;
};

BattleOriginalAiSimulationVisibilitySampledDiagnostic
battleOriginalAiSimulationVisibility5206Sampled(
    const BattleTerrainCollisionGrid& grid,
    int32_t firstX,
    int32_t firstZ,
    int32_t firstY,
    int32_t secondX,
    int32_t secondZ,
    int32_t secondY);

// Exact visible-loop result of BTECH 6fd8 for one of the eight 0x55-byte AI
// control records. Unoccupied records are reported as untouched: the unknown
// external call which precedes the loop is deliberately not interpreted.
struct BattleOriginalAiControlInitializationDiagnostic {
    bool exact = false;
    uint8_t liveObjectSlot = 0;
    bool liveObjectOccupied = false;
    bool visibleLoopWroteRecord = false;
    bool recordValuesKnown = false;
    uint16_t zeroFillByteCount = 0;
    std::array<uint8_t, 0x55> recordBytes{};
    int16_t word00 = 0;
    int16_t word0e = 0;
    int16_t sideWord10 = 0;
    int16_t slotWord12 = 0;
    int16_t word14 = 0;
    int16_t aggregateWord16 = 0;
    int16_t word18 = 0;
    int16_t word1a = 0;
    int16_t selectionCountWord1c = 0;
    int16_t cachedPoseStateWord2c = 0;
    int16_t selectorWord3a = 0;
    int16_t selectorWord3c = 0;
    uint8_t flagByte3e = 0;
    int16_t numericStateWord46 = 0;
    int16_t sharedStateWord51 = 0;
    int16_t word53 = 0;
};

BattleOriginalAiControlInitializationDiagnostic
battleOriginalAiControlInitialization6fd8(
    uint8_t liveObjectSlot,
    bool liveObjectOccupied);

enum class BattleOriginalAiInitialHeadingSource : uint8_t {
    Open,
    SharedSlot0ToSlot4Bearing,
    SideHeadingFlag,
    SeparateObjectiveBearing,
    ModeThreeNullDataBearing,
};

// Exact BTECH 1a22 initial authoritative-body pose. Modes 0/2/4 use the
// shared slot-0-to-slot-4 bearing, mode 1 uses the side SNARIO flag byte, and
// mode 3 uses the DS:CCEE raw pose. A fresh process has loader-zero CCEE and
// therefore reads the separately typed DS:0008 data point.
struct BattleOriginalAiInitialBodyPoseDiagnosticInput {
    uint8_t liveObjectSlot = 0;
    bool liveObjectOccupied = false;
    int16_t numericSideModeWord = 0;
    bool anchorsAvailable = false;
    int32_t slotRawX = 0;
    int32_t slotRawZ = 0;
    int32_t slot0RawX = 0;
    int32_t slot0RawZ = 0;
    int32_t slot4RawX = 0;
    int32_t slot4RawZ = 0;
    bool sideHeadingFlagsAvailable = false;
    uint8_t sideHeadingFlags = 0;
    bool separateObjectiveRawPoseAvailable = false;
    int32_t separateObjectiveRawX = 0;
    int32_t separateObjectiveRawZ = 0;
    bool modeThreeNullDataRawPoseAvailable = false;
    int32_t modeThreeNullDataRawX = 0;
    int32_t modeThreeNullDataRawZ = 0;
};

struct BattleOriginalAiInitialBodyPoseDiagnostic {
    bool exact = false;
    bool anchorsAvailable = false;
    bool liveObjectOccupied = false;
    bool numericSideModeZero = false;
    bool numericSideModeSupported = false;
    bool sideHeadingFlagsAvailable = false;
    uint8_t sideHeadingFlags = 0;
    uint8_t selectedSideHeadingFlagMask = 0;
    bool separateObjectiveRawPoseAvailable = false;
    bool modeThreeNullDataRawPoseAvailable = false;
    BattleOriginalAiInitialHeadingSource headingSource =
        BattleOriginalAiInitialHeadingSource::Open;
    uint8_t liveObjectSlot = 0;
    int16_t sideWord = 0;
    int16_t numericSideModeWord = 0;
    int32_t rawX = 0;
    int32_t rawY = 0;
    int32_t rawZ = 0;
    int16_t pitch = 0;
    int16_t roll = 0;
    int16_t sharedSlot0ToSlot4Bearing = 0;
    int16_t heading = 0;
    bool positionCopiedToAuthoritativeBody = false;
};

BattleOriginalAiInitialBodyPoseDiagnostic
battleOriginalAiInitialBodyPose1a22(
    const BattleOriginalAiInitialBodyPoseDiagnosticInput& input);

// Compatibility wrapper retaining the earlier strict mode-zero contract.
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
    int32_t slot4RawZ);

// Exact one-slot BTECH 7070 aggregate, including the following 4106 and raw
// boundary overrides. Numeric live/control offsets remain explicit where the
// source does not close a gameplay name.
struct BattleOriginalAiAggregateWeaponDiagnosticInput {
    uint8_t installationByte = 0;
    int16_t originalDamageWord = 0;
};

struct BattleOriginalAiAggregateDiagnosticInput {
    uint8_t liveObjectSlot = 0;
    bool occupied = false;
    int32_t rawX = 0;
    int32_t rawZ = 0;
    int16_t lifeSupportConditionWord0d = 0;
    int16_t sensorConditionWord0f = 0;
    int16_t engineConditionWord11 = 0;
    int16_t gyroConditionWord13 = 0;
    int16_t opaqueWord15 = 0;
    int16_t leftLegActuatorWord6c = 0;
    int16_t rightLegActuatorWord6e = 0;
    int16_t sharedCurrentControlStateWord51 = 0;
    std::array<int16_t, 9> armorWords{};
    std::array<int16_t, 8> internalWords{};
    std::array<BattleOriginalAiAggregateWeaponDiagnosticInput, 10> weapons{};
};

struct BattleOriginalAiAggregateDiagnostic {
    bool exact = false;
    bool authoritativeStateBindingAttempted = false;
    bool authoritativeStateBindingExact = false;
    uint8_t liveObjectSlot = 0;
    bool occupied = false;
    bool armorContributionEnabled = false;
    bool weaponContributionEnabled = false;
    int16_t armorContribution = 0;
    int16_t internalContribution = 0;
    int16_t weaponContribution = 0;
    uint8_t contributingWeaponCount = 0;
    int16_t preViabilityAggregate = 0;
    bool viability4106Passed = false;
    bool viabilityReplacedWithMinusOne = false;
    int16_t postViabilityAggregate = 0;
    BattleOriginalAiRawBoundaryProbeDiagnostic boundary;
    bool boundaryOverrideApplied = false;
    bool clearsControlStateWord46 = false;
    bool setsSixComponentFlagBits80 = false;
    int16_t finalAggregateWord16 = 0;
};

BattleOriginalAiAggregateDiagnostic battleOriginalAiAggregate7070(
    const BattleOriginalAiAggregateDiagnosticInput& input);

BattleOriginalAiAggregateDiagnostic
battleOriginalAiAggregate7070FromAuthoritativeState(
    uint8_t liveObjectSlot,
    int32_t rawX,
    int32_t rawZ,
    int16_t sharedCurrentControlStateWord51,
    const mech3d::MechDetailedDamageState& detailedDamage,
    const std::vector<BattleWeaponInstanceState>& weapons);

// Exact BTECH 89ea occupied-live-slot overlap predicate and its 8d02 dispatch
// gate. The source performs no team comparison here.
struct BattleOriginalAiLiveSlotOverlapDiagnostic {
    bool exact = false;
    int32_t deltaX = 0;
    int32_t deltaZ = 0;
    int32_t deltaY = 0;
    int16_t rawSpeed = 0;
    bool sameOwnerWord = false;
    bool teamFilterApplied = false;
    bool overlaps = false;
    bool movementBlocked = false;
    bool dispatchesSharedImpact = false;
};

BattleOriginalAiLiveSlotOverlapDiagnostic
battleOriginalAiLiveSlotOverlap(
    int32_t deltaX,
    int32_t deltaZ,
    int32_t deltaY,
    int16_t rawSpeed,
    bool sameOwnerWord);

// Exact BTECH 8c38 -> 5dea contact predicate for the separately owned
// DS:CCEE objective. Side effects and damage are intentionally not reproduced.
struct BattleOriginalAiObjectiveContactDiagnostic {
    bool exact = false;
    bool objectiveInactiveBitSet = false;
    int16_t opaqueGateWordEd0 = 0;
    bool playerSlot = false;
    int32_t deltaX = 0;
    int32_t objectY = 0;
    int32_t deltaZ = 0;
    uint16_t zThreshold = 0;
    bool contactPredicate = false;
    bool movementBlocked = false;
};

BattleOriginalAiObjectiveContactDiagnostic
battleOriginalAiObjectiveContact(
    bool objectiveInactiveBitSet,
    int16_t opaqueGateWordEd0,
    bool playerSlot,
    int32_t deltaX,
    int32_t objectY,
    int32_t deltaZ);

// Exact ordinary BTECH 89ea return path.  The caller supplies the already
// identified control words and the ordered live-slot/objective records.  The
// shared 8d02 impact is reported but deliberately not delivered here.
struct BattleOriginalAiSlotProbeLiveObjectDiagnosticInput {
    EntityId entityId{};
    bool occupied = false;
    int16_t ownerWord = 0;
    int32_t rawX = 0;
    int32_t rawY = 0;
    int32_t rawZ = 0;
};

struct BattleOriginalAiSlotProbeDiagnosticInput {
    uint8_t movingSlot = 0;
    int16_t controlWord00 = 0;
    int16_t controlState46 = 0;
    int16_t movingOwnerWord = 0;
    int16_t rawSpeed = 0;
    int32_t tentativeRawX = 0;
    int32_t tentativeRawY = 0;
    int32_t tentativeRawZ = 0;
    int32_t cachedDestinationRawX = 0;
    int32_t cachedDestinationRawY = 0;
    int32_t cachedDestinationRawZ = 0;
    std::vector<BattleOriginalAiSlotProbeLiveObjectDiagnosticInput> liveSlots;
    bool objectiveInactiveBitSet = true;
    int16_t opaqueGateWordEd0 = 0;
    int32_t objectiveRawX = 0;
    int32_t objectiveRawZ = 0;
};

struct BattleOriginalAiSlotProbeDiagnostic {
    bool exact = false;
    uint16_t resultWord = 0;
    bool boundaryClassifierBypassed = false;
    bool boundaryClassifierCalled = false;
    BattleOriginalAiRawBoundaryProbeDiagnostic boundary;
    uint8_t visitedLiveSlotCount = 0;
    bool liveSlotBlocked = false;
    uint8_t blockingLiveSlot = 0xffu;
    EntityId blockingEntityId{};
    bool dispatchesSharedImpact = false;
    BattleOriginalAiObjectiveContactDiagnostic objective;
    bool objectiveBlocked = false;
};

BattleOriginalAiSlotProbeDiagnostic battleOriginalAiSlotProbe(
    const BattleOriginalAiSlotProbeDiagnosticInput& input);

// Exact BTECH D5CE cache/list selection order with D3F2 narrow-phase results
// supplied by the caller. This diagnostic does not recreate scene geometry.
struct BattleOriginalAiSceneProbeCandidateDiagnosticInput {
    bool objectFlagBit10 = false;
    bool recordByte7Nonzero = false;
    uint8_t priority = 0;
    bool narrowPhaseHit = false;
};

struct BattleOriginalAiSceneProbeSelectionDiagnostic {
    bool exact = false;
    bool recordTableAvailable = false;
    bool cachedRecordPresent = false;
    uint8_t cachedPriority = 0;
    bool cacheEligible = false;
    bool cachedNarrowPhaseHit = false;
    bool clearedCacheAfterMiss = false;
    bool selectedFromCache = false;
    bool hit = false;
    uint16_t selectedCandidateIndex = 0xffffu;
    uint8_t selectedPriority = 0;
    uint16_t visitedCandidateCount = 0;
    uint16_t narrowPhaseTestCount = 0;
    bool terminatedOnPriorityZero = false;
};

BattleOriginalAiSceneProbeSelectionDiagnostic
battleOriginalAiSceneProbeSelection(
    bool recordTableAvailable,
    bool cachedRecordPresent,
    uint8_t cachedPriority,
    bool cachedNarrowPhaseHit,
    const std::vector<BattleOriginalAiSceneProbeCandidateDiagnosticInput>&
        candidates);

// Exact BTECH 94a4 -> 7c7d routing for an already selected raw-coordinate
// destination. Ordinary states must pass the desired heading through 95a1;
// this diagnostic intentionally does not guess that callee's steering result.
struct BattleOriginalAiMovementTargetDiagnostic {
    bool exact = false;
    int32_t deltaX = 0;
    int32_t deltaZ = 0;
    int16_t numericState51 = 0;
    int16_t currentHeading = 0;
    int16_t desiredHeadingBeforeSteering = 0;
    int16_t movementMode = 0;
    bool insideStrictStopSquare = false;
    bool routesThroughSteeringProbes = false;
    bool routesDirectlyToSpeedCommand = false;
    bool finalHeadingOpen = false;
};

BattleOriginalAiMovementTargetDiagnostic
battleOriginalAiMovementTarget(
    int32_t deltaX,
    int32_t deltaZ,
    int16_t numericState51,
    int16_t currentHeading);

// Exact BTECH 95a1 selector transition once the four 9700 blocked/clear
// results are supplied. Unvisited probe inputs are ignored.
struct BattleOriginalAiSteeringChoiceDiagnostic {
    bool exact = false;
    int16_t desiredHeadingInput = 0;
    bool forwardRequest = true;
    int16_t headingUsedForProbes = 0;
    int16_t selectorWord3aBefore = 0;
    int16_t selectorWord3cBefore = 0;
    std::array<bool, 4> probeBlocked{};
    std::array<bool, 4> probeVisited{};
    uint8_t probeVisitCount = 0;
    int16_t selectorWord3aAfter = 0;
    int16_t selectorWord3cAfter = 0;
    int16_t selectedHeadingOffset = 0;
    int16_t finalDesiredHeading = 0;
    int16_t movementMode = 0;
};

BattleOriginalAiSteeringChoiceDiagnostic
battleOriginalAiSteeringChoice(
    int16_t desiredHeading,
    bool forwardRequest,
    int16_t selectorWord3a,
    int16_t selectorWord3c,
    const std::array<bool, 4>& probeBlocked);

// Exact BTECH 9a77 oriented near-lane predicate. The caller owns candidate
// filtering and supplies the raw planar delta, body heading and lateral width.
struct BattleOriginalAiOrientedLaneDiagnostic {
    bool exact = false;
    int32_t deltaX = 0;
    int32_t deltaZ = 0;
    int16_t bodyHeading = 0;
    uint16_t lateralWidth = 0;
    uint32_t approximateDistance = 0;
    bool strictDistancePassed = false;
    int16_t candidateHeading = 0;
    int16_t headingDelta = 0;
    uint16_t absoluteHeadingDelta = 0;
    bool strictHeadingPassed = false;
    int16_t sineQ14 = 0;
    int16_t lateralProjection = 0;
    uint16_t absoluteLateralProjection = 0;
    bool strictLateralWidthPassed = false;
    bool insideLane = false;
};

BattleOriginalAiOrientedLaneDiagnostic
battleOriginalAiOrientedLane(
    int32_t deltaX,
    int32_t deltaZ,
    int16_t bodyHeading,
    uint16_t lateralWidth);

// Exact four-step 9700/9939 raw GRD window scan for one recovered probe
// direction. Windows which would address beyond the supplied 174x94 buffer
// fail closed as not exact instead of reading adjacent DOS memory.
struct BattleOriginalAiTerrainProbeDiagnostic {
    bool exact = false;
    bool gridContractValid = false;
    uint8_t probeIndex = 0;
    int16_t desiredHeading = 0;
    int16_t rawDirectionX = 0;
    int16_t rawDirectionZ = 0;
    std::array<int32_t, 4> rawProbeX{};
    std::array<int32_t, 4> rawProbeZ{};
    std::array<int16_t, 4> gridX{};
    std::array<int16_t, 4> gridZ{};
    uint8_t visitedStepCount = 0;
    uint16_t visitedSampleCount = 0;
    bool footprintOutsideGrid = false;
    bool blocked = false;
    uint8_t blockingStepIndex = 0xffu;
    int16_t blockingGridX = -1;
    int16_t blockingGridZ = -1;
    uint8_t blockingRawSample = 0;
};

BattleOriginalAiTerrainProbeDiagnostic
battleOriginalAiTerrainProbe(
    const BattleTerrainCollisionGrid& grid,
    int32_t rawX,
    int32_t rawZ,
    int16_t desiredHeading,
    uint8_t probeIndex);

// One zero-pitch/zero-roll C5ac step converted through the exact SNARIO raw
// coordinate mapping. The 0.1-second duration is the replacement's explicit
// compatibility bridge, not a recovered BTECH wall-clock constant.
struct BattleOriginalAiRawWorldPoseBridgeDiagnostic {
    bool exactCoordinateMapping = false;
    bool originalUpdateSecondsProvisional = true;
    double terrainCellSize = 0.0;
    double originalUpdateSeconds = 0.1;
    double worldXBefore = 0.0;
    double worldZBefore = 0.0;
    int16_t rawHeadingBefore = 0;
    int16_t rawTurnCommand = 0;
    int16_t rawHeadingAfter = 0;
    int16_t rawSpeed = 0;
    int32_t rawDeltaX = 0;
    int32_t rawDeltaZ = 0;
    double worldDeltaX = 0.0;
    double worldDeltaZ = 0.0;
    double worldXAfter = 0.0;
    double worldZAfter = 0.0;
    double worldHeadingRadiansAfter = 0.0;
    double worldForwardSpeedPerSecond = 0.0;
};

BattleOriginalAiRawWorldPoseBridgeDiagnostic
battleOriginalAiRawWorldPoseBridge(
    double worldX,
    double worldZ,
    double terrainCellSize,
    int16_t rawHeading,
    int16_t rawTurnCommand,
    int16_t rawSpeed);

struct BattleOriginalAiApproximatePlanarDistanceDiagnostic {
    bool exact = false;
    int32_t deltaX = 0;
    int32_t deltaZ = 0;
    uint32_t xMagnitude = 0;
    uint32_t zMagnitude = 0;
    uint32_t maximumMagnitude = 0;
    uint32_t minimumMagnitude = 0;
    uint32_t distance = 0;
};

BattleOriginalAiApproximatePlanarDistanceDiagnostic
battleOriginalAiApproximatePlanarDistanceB961(
    int32_t deltaX,
    int32_t deltaZ);

// Exact BTECH 27fb -> 23bf -> 7c3c movement-destination operands. Numeric
// control words deliberately retain their offsets and do not acquire guessed
// gameplay names.
struct BattleOriginalAiMovementCandidateDiagnosticInput {
    EntityId entityId{};
    bool liveSlotOccupied = false;
    int16_t sideWord = 0;
    int16_t cachedPoseStateWord2c = 0;
    int16_t aggregateWord16 = 0;
    int16_t priorSelectionCountWord1c = 0;
    int32_t cachedRawX = 0;
    int32_t cachedRawY = 0;
    int32_t cachedRawZ = 0;
    bool distanceMatrixValueExact = false;
    uint32_t approximateDistance = 0;
};

struct BattleOriginalAiSingleMovementTargetDiagnostic {
    bool exact = false;
    bool slotContractValid = false;
    int16_t shooterSideWord = 0;
    int16_t shooterAggregateWord16 = 0;
    uint8_t eligibleCandidateCount = 0;
    bool multipleCandidatesOpen = false;
    bool priorSelectionCountOpen = false;
    int16_t aggregateDeltaWord = 0;
    bool rejectedByMinus99Gate = false;
    int16_t score = 0;
    bool scoreHalvedByStateWord2c = false;
    bool selected = false;
    uint8_t selectedSlot = 0xffu;
    EntityId selectedEntityId{};
    int32_t selectedCachedRawX = 0;
    int32_t selectedCachedRawY = 0;
    int32_t selectedCachedRawZ = 0;
    bool writesState46Zero = false;
    bool writesState46One = false;
    bool incrementsSelectedWord1c = false;
};

BattleOriginalAiSingleMovementTargetDiagnostic
battleOriginalAiSingleMovementTarget(
    int16_t shooterSideWord,
    int16_t shooterAggregateWord16,
    const std::vector<BattleOriginalAiMovementCandidateDiagnosticInput>&
        candidates);

struct BattleOriginalAiMovementCandidateScoreDiagnostic {
    bool eligible = false;
    uint8_t slot = 0xffu;
    uint32_t approximateDistance = 0;
    uint16_t distanceLowWord = 0;
    int16_t aggregateDifferenceWord = 0;
    uint16_t absoluteAggregateDifference = 0;
    int16_t selectionCountWord1c = 0;
    int16_t aggregateTerm = 0;
    int16_t selectionPenalty = 0;
    int16_t distanceTerm = 0;
    int16_t scoreBeforeCachedState = 0;
    bool scoreHalvedByStateWord2c = false;
    int16_t score = 0;
    bool rejectedByMinus100Gate = false;
    bool replacedWinner = false;
};

struct BattleOriginalAiMovementTargetSelectionDiagnostic {
    bool exact = false;
    bool slotContractValid = false;
    int16_t shooterSideWord = 0;
    int16_t shooterAggregateWord16 = 0;
    uint8_t eligibleCandidateCount = 0;
    uint16_t maximumDistanceLowWord = 0;
    int16_t maximumAggregateMagnitudeWord = 0;
    std::array<BattleOriginalAiMovementCandidateScoreDiagnostic, 8>
        candidateScores{};
    bool selected = false;
    uint8_t selectedSlot = 0xffu;
    EntityId selectedEntityId{};
    int32_t selectedCachedRawX = 0;
    int32_t selectedCachedRawY = 0;
    int32_t selectedCachedRawZ = 0;
    int16_t bestScore = 0;
    int16_t selectedCountBefore = 0;
    int16_t selectedCountAfter = 0;
    bool strictEarlierSlotTie = true;
    bool writesState46Zero = false;
    bool writesState46One = false;
    bool incrementsSelectedWord1c = false;
};

BattleOriginalAiMovementTargetSelectionDiagnostic
battleOriginalAiMovementTargetSelection23bf(
    int16_t shooterSideWord,
    int16_t shooterAggregateWord16,
    const std::vector<BattleOriginalAiMovementCandidateDiagnosticInput>&
        candidates);

// Caller-level proof for one relation whose exact zero/nonzero value remains
// open. Both exhaustive 23bf executions must select the same non-open target
// and produce the same persistent selected-count mutation.
struct BattleOriginalAiOpenRelationSelectionDiagnostic {
    bool exact = false;
    bool openSlotValid = false;
    uint8_t openSlot = 0xffu;
    int16_t eligibleStateWord2c = 0;
    BattleOriginalAiMovementTargetSelectionDiagnostic relationZero;
    BattleOriginalAiMovementTargetSelectionDiagnostic relationNonzero;
    bool targetSelectionIndependent = false;
    uint8_t selectedSlot = 0xffu;
    EntityId selectedEntityId{};
    int32_t selectedCachedRawX = 0;
    int32_t selectedCachedRawY = 0;
    int32_t selectedCachedRawZ = 0;
    int16_t selectedCountAfter = 0;
};

BattleOriginalAiOpenRelationSelectionDiagnostic
battleOriginalAiMovementTargetSelection23bfWithOpenRelation(
    int16_t shooterSideWord,
    int16_t shooterAggregateWord16,
    const std::vector<BattleOriginalAiMovementCandidateDiagnosticInput>&
        candidates,
    uint8_t openSlot,
    int16_t eligibleStateWord2c);

// Caller-complete ordinary BTECH 7c3c/7797 -> 94a4 -> 95a1 -> 9700 -> 84de
// -> 87c4 transaction after a movement point has already been supplied.
// Ownership is explicit: a selected live combatant, the fresh mode-3
// null-pointer data point, or a separately owned compatibility mission point.
// The 5454 relation matrix can still take the render-owned 50ee branch for
// live-combatant selection in the main loop.
enum class BattleOriginalAiMovementTargetOwnership : uint8_t {
    LiveCombatant,
    ModeThreeNullDataPoint,
    MissionObjectivePoint,
};

struct BattleOriginalAiSelectedTargetMovementInput {
    uint8_t movingSlot = 0;
    int16_t movingOwnerWord = 0;
    int16_t controlWord00 = 0;
    int16_t controlState46 = 0;
    int16_t numericState51 = 0;
    int32_t rawX = 0;
    int32_t rawY = 0;
    int32_t rawZ = 0;
    int16_t pitch = 0;
    int16_t roll = 0;
    int16_t heading = 0;
    int16_t controlWord30 = 0;
    int16_t controlWord32 = 0;
    int16_t driftWord36 = 0;
    int16_t driftWord38 = 0;
    int16_t driftWord3a = 0;
    int16_t rawSpeed = 0;
    int16_t definitionWord06 = 0;
    int16_t definitionHeightWord02 = 0;
    int16_t heatSpeedPenalty = 0;
    int16_t leftLegActuatorWord = 0;
    int16_t rightLegActuatorWord = 0;
    int16_t selectorWord3a = 0;
    int16_t selectorWord3c = 0;
    uint16_t previousSlotProbeWord = 0;
    int16_t controlState4c = 0;
    int16_t controlState53 = 0;
    EntityId selectedTargetEntityId{};
    BattleOriginalAiMovementTargetOwnership selectedTargetOwnership =
        BattleOriginalAiMovementTargetOwnership::LiveCombatant;
    int32_t selectedTargetRawX = 0;
    int32_t selectedTargetRawY = 0;
    int32_t selectedTargetRawZ = 0;
    std::vector<BattleOriginalAiSlotProbeLiveObjectDiagnosticInput> liveSlots;
    bool objectiveLaneEnabled = false;
    bool objectiveInactiveBitSet = true;
    int16_t opaqueObjectiveGateWordEd0 = 0;
    int32_t objectiveRawX = 0;
    int32_t objectiveRawZ = 0;
    std::optional<size_t> cachedSceneObjectIndex;
    std::optional<uint8_t> cachedSubrecordIndex;
};

struct BattleOriginalAiSelectedTargetMovementDiagnostic {
    bool exact = false;
    bool selectedTargetOwnedByCaller = true;
    bool targetAcquisitionClosed = false;
    bool ordinaryStateAccepted = false;
    bool probeEndpointDomainClosed = false;
    bool boundaryRelationOpen = false;
    bool liveObjectLaneDomainClosed = false;
    bool objectiveLaneDomainClosed = false;
    bool liveObjectLaneBlocked = false;
    EntityId blockingLiveObjectEntityId{};
    bool objectiveLaneBlocked = false;
    std::array<BattleOriginalAiTerrainProbeDiagnostic, 4> terrainProbes{};
    std::array<bool, 4> probeBlocked{};
    BattleOriginalAiMovementTargetDiagnostic movementTarget;
    BattleOriginalAiSteeringChoiceDiagnostic steering;
    BattleOriginalAiRawSpeedCommandDiagnostic speedCommand;
    BattleOriginalAiBodyTurnCommandDiagnostic turnCommand;
    BattleOriginalAiSlotProbeDiagnostic slotProbe;
    BattleOriginalAiOrdinaryPoseTransactionDiagnostic poseTransaction;
};

BattleOriginalAiSelectedTargetMovementDiagnostic
battleOriginalAiSelectedTargetMovement(
    const BattleTerrainCollisionGrid& grid,
    const BattleOriginalTerrainSceneCatalog& catalog,
    const BattleOriginalAiSelectedTargetMovementInput& input);

// Exact C5ac subset for zero pitch, zero roll, zero heading and zero angular
// commands.  Positions are the original signed 32-bit word pairs; this does
// not bypass the still-open 87c4 collision accept/restore caller in live play.
struct BattleOriginalAiZeroAttitudePoseStepDiagnostic {
    bool exact = false;
    int32_t xBefore = 0;
    int32_t yBefore = 0;
    int32_t zBefore = 0;
    int16_t rawSpeed = 0;
    int16_t driftX = 0;
    int16_t driftY = 0;
    int16_t driftZ = 0;
    int32_t xAfter = 0;
    int32_t yAfter = 0;
    int32_t zAfter = 0;
};

BattleOriginalAiZeroAttitudePoseStepDiagnostic
battleOriginalAiZeroAttitudePoseStep(
    int32_t x,
    int32_t y,
    int32_t z,
    int16_t rawSpeed,
    int16_t driftX,
    int16_t driftY,
    int16_t driftZ);

struct BattleOriginalAiDecisionDiagnostic {
    bool valid = false;
    uint64_t sequence = 0;
    uint64_t originalUpdateCount = 0;
    uint64_t decisionTickIndex = 0;
    uint64_t fireTickIndex = 0;
    EntityId shooterEntityId{};
    BattleShotTargetKind targetKind = BattleShotTargetKind::None;
    EntityId targetEntityId{};
    uint32_t weaponInstanceId = 0;
    BattleOriginalAiDecisionResult result =
        BattleOriginalAiDecisionResult::None;
    double targetDistance = 0.0;
    uint8_t readyFunctionalWeaponCount = 0;
    uint16_t maximumReadyRangeWord = 0;
    double targetSearchRadius = 0.0;
    bool targetAcquisitionRangePassed = false;
    bool targetSelectionCenterlineSubsetProven = false;
    bool targetSelectionPlanarFixedAngleProven = false;
    bool targetSelectionZeroPitchVerticalFixedAngleProven = false;
    bool originalApproximateDistanceProven = false;
    int16_t shooterFixedHeading = 0;
    int16_t selectedCandidateFixedHeading = 0;
    int16_t selectedCandidateYawDelta = 0;
    int16_t selectedCandidateHalfWidth = 0;
    int16_t selectedCandidateVerticalAngle = 0;
    int16_t selectedCandidateVerticalDelta = 0;
    int16_t selectedCandidateVerticalLimit = 0x0b60;
    bool selectedCandidateVerticalGatePassed = false;
    EntityId blockingCombatantEntityId{};
    double blockingCombatantDistance = 0.0;
    uint8_t blockingCandidateFlags = 0;
    bool friendlyFireLaneSuppressed = false;
    double minimumRange = 0.0;
    double maximumRange = 0.0;
    bool strictRangeGatePassed = false;
    int16_t weaponScore = 0;
    uint8_t rangeBucket = 0;
    int16_t targetNumber = 0;
    uint8_t hitRoll = 0;
    bool hit = false;
    BattleOriginalAiAttackAspect attackAspect =
        BattleOriginalAiAttackAspect::Front;
    uint8_t locationRollIndex = 0;
    mech3d::MechArmorSectionId armorSection =
        mech3d::MechArmorSectionId::CenterTorso;
    bool automaticCallerAndLocationTableProven = false;
    bool originalRandomSequenceProven = false;
    std::string provenance;
};

// Immutable collision surface decoded from the same original component mesh
// used by presentation. Torso triangles need the still-provisional sub-region
// partition; arms, legs and cockpit have direct FUN_1000_47f7 identities.
struct BattleMechHitTriangle {
    BattleHitPoint a;
    BattleHitPoint b;
    BattleHitPoint c;
    int componentId = -1;
    mech3d::MechArmorSectionId directArmorSection =
        mech3d::MechArmorSectionId::CenterTorso;
    bool torsoComposite = false;
};

struct BattleMechHitProfile {
    bool valid = false;
    std::string mechPresetId;
    std::vector<BattleMechHitTriangle> triangles;
    double aimOriginHeight = 0.0;
    int cockpitViewportHeight = 0;
    double neutralCrosshairY = 0.0;
    double crosshairPixelsPerAimStep = 3.0;
    double verticalFieldOfViewDegrees = 45.0;
    uint64_t geometryFingerprint = 0;
    std::string provenance;
};

struct BattleProjectileHitProfile {
    bool valid = false;
    uint16_t originalVisualClass = 0;
    std::vector<BattleMechHitTriangle> triangles;
    uint64_t geometryFingerprint = 0;
    bool originalModelGeometryProven = false;
    bool spinTimingProven = false;
    std::string provenance;
};

std::vector<BattleProjectileHitProfile>
loadOriginalBattleProjectileHitProfiles(
    const std::filesystem::path& othpckPath);

struct BattleWeaponInstanceState {
    uint32_t weaponInstanceId = 0;
    uint8_t slotIndex = 0;
    std::string weaponTypeId;
    std::string displayName;
    std::string locationId;
    char displayRangeClass = 'M';
    uint8_t condition = 0;
    bool functional = true;
    BattleWeaponAmmunitionOwnership ammunitionOwnership =
        BattleWeaponAmmunitionOwnership::EnergyNoAmmunition;
    int8_t ammunitionPoolIndex = -1;
    bool ammunitionStateKnown = true;
    int ammunitionRemaining = 0;
    uint64_t cooldownTicksRemaining = 0;
    uint64_t cooldownTicksOnFire = 0;
    uint16_t originalCooldownUpdateCount = 0;
    bool originalCooldownValueProven = false;
    bool originalCooldownCadenceProven = false;
    uint16_t originalDamage = 0;
    bool originalDamageValueProven = false;
    uint16_t originalHeat = 0;
    bool originalHeatValueProven = false;
    uint16_t originalCriticalWeight = 0;
    bool originalCriticalWeightProven = false;
    std::array<uint16_t, 4> originalRangeWords{};
    bool originalRangeValueProven = false;
    double maximumRange = 0.0;
    BattleWeaponDeliveryMode deliveryMode =
        BattleWeaponDeliveryMode::Immediate;
    int8_t originalProjectileTypeIndex = -1;
    uint16_t originalProjectileSpeedRaw = 0;
    uint16_t originalProjectileLifetimeUpdates = 0;
    uint16_t originalProjectileDamageClass = 0;
    uint16_t originalProjectileVisualClass = 0;
    bool originalProjectileDefinitionProven = false;
    BattleWeaponReadiness readiness = BattleWeaponReadiness::Ready;
    bool selected = false;
    uint64_t shotsFired = 0;
};

struct BattleShotDiagnostic {
    bool valid = false;
    uint64_t sequence = 0;
    uint64_t tickIndex = 0;
    EntityId shooterEntityId{};
    uint32_t weaponInstanceId = 0;
    BattleShotTargetKind targetKind = BattleShotTargetKind::None;
    EntityId targetEntityId{};
    uint32_t targetProjectileId = 0;
    BattleShotResult result = BattleShotResult::None;
    double targetDistance = 0.0;
    double maximumRange = 0.0;
    bool rangeGatePassed = false;
    bool replacementFireSolution = false;
    bool replacementTerrainLineOfSightClear = false;
    bool replacementAimAligned = false;
    uint8_t replacementGunnerySkill = 0;
    int16_t replacementTargetNumber = 0;
    uint8_t replacementHitRoll = 0;
    BattleOriginalAiDecisionDiagnostic originalAiDecision;
    bool hit = false;
    BattleShotHitLocation hitLocation =
        BattleShotHitLocation::UnresolvedOriginalDistribution;
    bool hitLocationProven = false;
    mech3d::MechArmorSectionId armorSection =
        mech3d::MechArmorSectionId::CenterTorso;
    BattleHitPoint aimOrigin;
    BattleHitPoint aimDirection;
    BattleHitPoint impactPoint;
    int hitComponentId = -1;
    uint64_t hitGeometryFingerprint = 0;
    bool aimProjectionProven = false;
    BattleShotDeliveryState deliveryState = BattleShotDeliveryState::None;
    uint32_t projectileId = 0;
    uint64_t impactTickIndex = 0;
    uint16_t impactDamage = 0;
    uint8_t missileClusterRollIndex = 0;
    bool missileClusterTableProven = false;
    bool projectileMotionTimingProven = false;
    uint16_t originalDamage = 0;
    bool originalDamageValueProven = false;
    BattleShotRuntimeEffect runtimeEffect = BattleShotRuntimeEffect::None;
    BattleMechSystemRole affectedSystem = BattleMechSystemRole::Unknown;
    int damageBefore = 0;
    int damageApplied = 0;
    int damageAfter = 0;
    int armorBefore = 0;
    int armorDamageApplied = 0;
    int armorAfter = 0;
    int internalBefore = 0;
    int internalDamageApplied = 0;
    int internalAfter = 0;
    bool criticalResolutionAttempted = false;
    uint8_t criticalRoll = 0;
    uint8_t criticalAttemptsRequested = 0;
    uint8_t criticalHitsApplied = 0;
    uint8_t criticalFallbackCount = 0;
    BattleCriticalHitKind lastCriticalHitKind =
        BattleCriticalHitKind::None;
    uint8_t lastCriticalHitIndex = 0;
    mech3d::MechInternalSectionId lastCriticalLocation =
        mech3d::MechInternalSectionId::CenterTorso;
    bool criticalCandidateSelectionPending = false;
    bool originalCriticalRulesProven = true;
    bool originalCriticalRandomSequenceProven = false;
    bool criticalResolutionPending = false;
    int unresolvedCriticalDamage = 0;
    std::string provenance;
};

enum class BattleImpactVisualSequence : uint8_t {
    OriginalSixStage,
    OriginalMachineGunTwoStage,
    TerrainSmokeLastThree,
};

// Bounded history of authoritative physical weapon contacts. Presentation
// selects the original six impact records from its age; no visual geometry or
// renderer time is stored here. The retention window accommodates the active
// two-fixed-tick-per-stage presentation tuning.
struct BattleImpactEvent {
    bool valid = false;
    uint64_t shotSequence = 0;
    uint64_t impactTickIndex = 0;
    EntityId shooterEntityId{};
    uint32_t weaponInstanceId = 0;
    BattleShotTargetKind targetKind = BattleShotTargetKind::None;
    EntityId targetEntityId{};
    uint32_t projectileId = 0;
    BattleShotDeliveryState deliveryState = BattleShotDeliveryState::None;
    BattleImpactVisualSequence visualSequence =
        BattleImpactVisualSequence::OriginalSixStage;
    BattleHitPoint point;
};

struct BattleProjectileState {
    bool valid = false;
    uint32_t projectileId = 0;
    uint64_t launchTickIndex = 0;
    EntityId shooterEntityId{};
    uint32_t weaponInstanceId = 0;
    std::string weaponTypeId;
    Transform launchShooterTransform;
    double launchTorsoYawRadians = 0.0;
    BattleHitPoint launchModelMount;
    bool launchModelMountValid = false;
    bool launchModelMountPolicyProven = false;
    BattleShotTargetKind targetKind = BattleShotTargetKind::None;
    EntityId targetEntityId{};
    bool targetBound = false;
    BattleHitPoint previousPosition;
    BattleHitPoint position;
    BattleHitPoint direction;
    BattleHitPoint targetLocalPoint;
    int8_t originalProjectileTypeIndex = -1;
    uint16_t originalSpeedRaw = 0;
    uint16_t originalLifetimeUpdates = 0;
    uint16_t originalLifetimeUpdatesRemaining = 0;
    uint16_t originalDamageClass = 0;
    uint16_t originalVisualClass = 0;
    double launchForwardOffset = 0.0;
    bool launchForwardOffsetProven = false;
    double movementSubstepAccumulator = 0.0;
    double lifetimeUpdateAccumulator = 0.0;
    bool originalTargetTrackingProven = false;
    bool motionScaleProven = false;
    uint64_t collisionGeometryFingerprint = 0;
    bool collisionGeometryProven = false;
    bool laserInterceptible = false;
    BattleShotDiagnostic pendingShot;
    std::string provenance;
};

enum class BattleCollisionKind : uint8_t {
    None,
    MechMech,
    TerrainGrid,
    TerrainObject,
    ObjectiveStructure,
};

enum class BattleCollisionRuntimeEffect : uint8_t {
    None,
    PositionalSeparationNoDamage,
};

struct BattleCollisionDiagnostic {
    bool valid = false;
    uint64_t sequence = 0;
    uint64_t tickIndex = 0;
    BattleCollisionKind kind = BattleCollisionKind::None;
    EntityId entityId{};
    EntityId otherEntityId{};
    int terrainGridX = -1;
    int terrainGridZ = -1;
    uint32_t terrainObstacleId = 0;
    uint16_t terrainRecordIndex = 0;
    double normalX = 0.0;
    double normalZ = 0.0;
    double penetration = 0.0;
    double displacement = 0.0;
    double speedBefore = 0.0;
    double speedAfter = 0.0;
    BattleCollisionRuntimeEffect runtimeEffect =
        BattleCollisionRuntimeEffect::None;
    bool damageSemanticsProven = false;
    int damageApplied = 0;
    std::string provenance;
};

uint64_t battlePersistentMechStateFingerprint(
    const BattlePersistentMechState& state);

bool battlePersistentMechStateIsPristine(
    const BattlePersistentMechState& state);

struct BattleCombatantLaunchState {
    std::string mechPresetId = "locust";
    Transform startTransform{43500.0, 0.0, 23500.0, 0.0};
    BattleCombatantRosterMetadata roster{
        BattleTeam::Opposing,
        std::nullopt,
        {},
        "explicit_launch_roster",
        {},
    };
    CombatantMissionStatus priorMissionStatus = CombatantMissionStatus::Active;
    bool minimalRepairApplied = false;
    bool fragileAfterMinimalRepair = false;
    std::vector<int> destroyedComponentIds;
    BattlePersistentMechState persistentMechState;
    bool ammunitionStateValid = false;
    std::array<int, 6> ammunitionByPool{};
};

struct BattleMechCollisionProfile {
    std::string mechPresetId;
    double radiusWorld = 0.0;
    std::string provenance;
};

struct BattleStartParams {
    BattleMissionBriefing mission;
    std::filesystem::path terrainScenarioPath;
    size_t terrainScenarioIndex = 2;
    std::optional<int> terrainEnvironmentId;
    double terrainCellSize = 500.0;
    std::string playerMechPresetId = "locust";
    Transform playerStartTransform{43500.0, 0.0, 23500.0, 0.0};
    BattleCombatantRosterMetadata playerRoster{
        BattleTeam::Player,
        std::nullopt,
        {},
        "player_start_params",
        "player:0",
    };
    std::optional<BattleCombatantLaunchState> playerLaunchState;
    std::vector<BattleCombatantLaunchState> playerAlliedLaunchStates;
    std::vector<BattleCombatantLaunchState> combatantLaunchStates;
    double fixedTickSeconds = 0.1;
    double maxForwardSpeed = 250.0;
    double maxReverseSpeed = 100.0;
    double acceleration = 500.0;
    double deceleration = 750.0;
    double maxTurnRateRadians = 0.7853981633974483;
    int maxPlayerTorsoYawSteps = 7;
    double playerTorsoYawStepRadians = 0.1047197551196598;
    int maxPlayerAimPitchUpSteps = 9;
    int maxPlayerAimPitchDownSteps = 9;
    double cockpitCameraForwardOffset = 50.0;
    double cockpitCameraHeight = 180.0;
    double jumpJetMaxFuel = 1.5;
    double jumpJetActivationFuel = 0.975;
    double jumpJetFuelBurnPerSecond = 0.12;
    double jumpJetFuelRechargePerSecond = 0.072;
    double jumpJetInitialImpulse = 1200.0;
    double jumpJetInitialImpulseSeconds = 1.25;
    double jumpJetUpAcceleration = 760.0;
    double jumpJetForwardAcceleration = 520.0;
    double jumpJetGravity = 390.0;
    double jumpJetHardLandingSpeed = 420.0;
    bool constrainToTerrainBounds = true;
    BattleCombatAiPolicy combatAiPolicy = BattleCombatAiPolicy::Disabled;
    bool mechSystemsSnapshotEnabled = false;
    bool deterministicCombatRuntimeEnabled = false;
    bool individualWeaponRuntimeEnabled = false;
    bool originalProjectileRuntimeEnabled = false;
    bool originalHeatRuntimeEnabled = false;
    bool originalMajorSystemRuntimeEnabled = false;
    bool stationaryTargetHitDiagnosticEnabled = false;
    BattleWeaponTargetPolicy weaponTargetPolicy =
        BattleWeaponTargetPolicy::LegacyNearestOpposingDiagnostic;
    bool enemyRetreatOnDamageEnabled = false;
    bool deterministicObjectiveRuntimeEnabled = false;
    bool deterministicCollisionRuntimeEnabled = false;
    BattleTerrainCollisionGrid terrainCollisionGrid;
    BattleTerrainCollisionObstacles terrainCollisionObstacles;
    BattleOriginalTerrainSceneCatalog originalTerrainSceneCatalog;
    std::vector<BattleMechCollisionProfile> mechCollisionProfiles;
    std::vector<BattleMechHitProfile> mechHitProfiles;
    std::vector<BattleProjectileHitProfile> projectileHitProfiles;
    double mechCollisionRadiusCells = 0.2;
    double collisionRestitution = 0.12;
    double collisionBackoffWorldUnits = 80.0;
    double collisionFeedbackCooldownSeconds = 1.0;
    double objectiveCollisionRadiusWorld = 450.0;
    double enemyActivationRange = 6000.0;
    double enemyAttackRange = 650.0;
    double weaponRange = 700.0;
    uint64_t weaponCooldownTicks = 3;
    int weaponDamagePerHit = 1;
    int mechSystemMaxDamage = 3;
    int enemyRetreatCoreDamageThreshold = 2;
    int enemyRetreatRemainingDurabilityPercent = 35;
    int objectiveMaxDamage = 3;
    double objectiveRetaliationSeconds = 12.0;
    double combatConclusionDelaySeconds = 5.0;
    BattleContractMetadata contract;
    BattleOppositionRosterMetadata oppositionRoster;
    BattleSetupMetadata setupMetadata;
    BattleObjectiveState objective;
    BattlefieldBoundaryState battlefieldBoundary;
};

// Typed campaign bridge for the proved Audit 38 live-target subset. Fresh
// mode-3 DS:CCEE null-data behavior remains diagnostic/laboratory-only; the
// campaign can select an explicitly named compatibility fallback that reuses
// the proved live-target selector for player companions only.
struct BattleOriginalAiCampaignMovementActivationDiagnostic {
    bool exact = false;
    bool incomingPolicyDisabled = false;
    bool setupAvailable = false;
    bool nonObjectiveModesSupported = false;
    BattleOriginalAiActOnOwnRouteDiagnostic playerActOnOwnRoute;
    BattleOriginalAiActOnOwnRouteDiagnostic opposingActOnOwnRoute;
    bool actOnOwnSelectedTargetRoutesSupported = false;
    bool playerSelectedTargetRouteSupported = false;
    bool playerOnlySelectedTargetActivated = false;
    bool modeThreeNullDataRouteSupported = false;
    bool modeThreeNullDataCampaignRejected = false;
    bool modeThreeSelectedTargetCompatibilityActivated = false;
    bool modeThreeMissionObjectiveCompatibilityActivated = false;
    bool activeSeparateObjectiveAbsent = false;
    bool activeSeparateObjectiveBound = false;
    bool completePhase12Runtime = false;
    bool originalMovementResourcesAvailable = false;
    bool activePlayerCompanionPresent = false;
    bool activeOpponentPresent = false;
    BattleOriginalAiLanceOrderDiagnostic freshOrder;
    bool defaultActOnOwnOrder = false;
    bool activate = false;
    BattleCombatAiPolicy selectedPolicy = BattleCombatAiPolicy::Disabled;
    std::string provenance;
};

BattleOriginalAiCampaignMovementActivationDiagnostic
battleOriginalAiCampaignMovementActivation(
    const BattleStartParams& params);

struct BattleDriveRuntimeTuning {
    double fixedTickSeconds = 0.05;
    double maxForwardSpeed = 750.0;
    double maxReverseSpeed = 250.0;
    double acceleration = 500.0;
    double deceleration = 750.0;
    double maxTurnRateRadians = 0.39269908169872414;
};

constexpr BattleDriveRuntimeTuning battleDrivePrototypeTuning() {
    return {};
}

void applyBattleDriveRuntimeTuning(
    BattleStartParams& params,
    const BattleDriveRuntimeTuning& tuning = battleDrivePrototypeTuning());

struct BattleInputCommand {
    uint64_t tickIndex = 0;
    EntityId entityId{};
    double throttle = 0.0;
    double turn = 0.0;
    int torsoYawStepDelta = 0;
    int aimPitchStepDelta = 0;
    bool jumpJetToggle = false;
    bool jumpForwardThrust = false;
    bool jumpVerticalThrust = false;
    bool fireWeapon = false;
    int selectedWeaponStepDelta = 0;
    uint32_t selectWeaponInstanceId = 0;
    bool toggleCockpitRadar = false;
    bool cycleCockpitRadarRange = false;
    bool cycleTargetScan = false;
};

constexpr uint16_t battleCockpitRadarRangeMeters(uint8_t rangeIndex) {
    switch (rangeIndex & 3u) {
    case 0:
        return 4000u;
    case 1:
        return 2000u;
    case 2:
        return 1000u;
    default:
        return 500u;
    }
}

struct BattleCockpitRadarState {
    bool active = false;
    uint8_t rangeIndex = 0;
    uint16_t rangeMeters = battleCockpitRadarRangeMeters(0);
    uint64_t commandSequence = 0;
    uint64_t lastCommandTickIndex = 0;
};

// Capture-calibrated to the same 4000-metre scale as the Phase 12 radar.
// The 5:1 conversion is authoritative for acquisition, but remains
// provisional until the DOS world-unit conversion is recovered.
constexpr uint16_t battleTargetScanRangeMeters() {
    return 4000u;
}

constexpr double battleTargetScanWorldUnitsPerMeter() {
    return 5.0;
}

// AI teams share contacts over the same maximum 4000-metre envelope shown by
// the cockpit radar. Cycling the presentation scale must not shorten AI sight.
constexpr double battleRadarContactRangeWorldUnits() {
    return static_cast<double>(battleCockpitRadarRangeMeters(0)) *
        battleTargetScanWorldUnitsPerMeter();
}

enum class BattleTargetScanTargetKind : uint8_t {
    None = 0,
    Combatant = 1,
    Objective = 2,
};

struct BattleTargetScanState {
    BattleTargetScanTargetKind selectedTargetKind =
        BattleTargetScanTargetKind::None;
    EntityId selectedTargetEntityId{};
    uint16_t rangeMeters = battleTargetScanRangeMeters();
    double selectedTargetDistanceMeters = 0.0;
    uint64_t commandSequence = 0;
    uint64_t lastCommandTickIndex = 0;
};

constexpr bool battleTargetScanHasSelection(
    const BattleTargetScanState& state) {
    return state.selectedTargetKind != BattleTargetScanTargetKind::None;
}

enum class BattleOriginalAiFirstMovementUpdateResult : uint8_t {
    None,
    RejectedMovementBlocked,
    RejectedTargetCardinality,
    RejectedTargetSlot,
    RejectedRelationOpen,
    RejectedRelationBlocked,
    RejectedAggregate,
    RejectedTargetSelection,
    RejectedMovementTransaction,
    RejectedPoseCommit,
    RejectedPostStepStateOpen,
    Committed,
};

struct BattleOriginalAiMotionRuntimeState {
    bool initialized = false;
    std::string provenance;
    uint8_t liveObjectSlot = 0xffu;
    int16_t sideWord = 0;
    int16_t numericSideModeWord = 0;
    std::array<uint8_t, 0x55> controlRecordBytes{};
    int32_t rawX = 0;
    int32_t rawY = 0;
    int32_t rawZ = 0;
    int16_t pitch = 0;
    int16_t roll = 0;
    int16_t heading = 0;
    int16_t controlWord30 = 0;
    int16_t controlWord32 = 0;
    int16_t controlWord34 = 0;
    int16_t driftWord36 = 0;
    int16_t driftWord38 = 0;
    int16_t driftWord3a = 0;
    int16_t rawSpeedWord3c = 0;
    uint16_t slotProbeWord = 0;
    bool sceneCacheValid = false;
    size_t sceneCacheObjectIndex = std::numeric_limits<size_t>::max();
    uint8_t sceneCacheSubrecordIndex = 0xffu;
    bool worldPositionBindingExact = false;
    bool firstMovementUpdateAttempted = false;
    bool firstMovementUpdateCommitted = false;
    BattleOriginalAiFirstMovementUpdateResult firstMovementUpdateResult =
        BattleOriginalAiFirstMovementUpdateResult::None;
    EntityId firstMovementTargetEntityId{};
    uint8_t firstMovementTargetLiveObjectSlot = 0xffu;
    uint16_t firstMovementRelationWord = 0;
    bool firstMovementRelationSampled = false;
    uint16_t firstMovementRelationSampleCount = 0;
    int16_t firstMovementMoverAggregateWord16 = 0;
    int16_t firstMovementTargetAggregateWord16 = 0;
    bool firstMovementSelection50eeIndependent = false;
    uint64_t firstMovementDecisionTickIndex = 0;
    uint64_t movementUpdateAttemptCount = 0;
    BattleOriginalAiFirstMovementUpdateResult lastMovementUpdateResult =
        BattleOriginalAiFirstMovementUpdateResult::None;
    EntityId lastMovementTargetEntityId{};
    uint8_t lastMovementTargetLiveObjectSlot = 0xffu;
    uint16_t lastMovementRelationWord = 0;
    bool lastMovementRelationSampled = false;
    uint16_t lastMovementRelationSampleCount = 0;
    int16_t lastMovementMoverAggregateWord16 = 0;
    int16_t lastMovementTargetAggregateWord16 = 0;
    bool lastMovementSelection50eeIndependent = false;
    uint64_t lastMovementDecisionTickIndex = 0;
    bool lastMovementPostStepExact = false;
    uint8_t lastMovementPostStepActionCode = 0;
    uint64_t acceptedPoseCommitCount = 0;
    uint64_t lastPoseCommitTickIndex = 0;
};

struct Combatant {
    EntityId id{};
    std::string mechPresetId;
    bool playerControlled = false;
    BattleCombatantRosterMetadata roster;
    Transform transform{};
    double throttle = 0.0;
    double turn = 0.0;
    double targetForwardSpeed = 0.0;
    double forwardSpeed = 0.0;
    int originalMaxSpeedKph = 0;
    double maxForwardSpeed = 0.0;
    double maxReverseSpeed = 0.0;
    int torsoYawStep = 0;
    int aimPitchStep = 0;
    bool boundaryContact = false;
    bool collisionContact = false;
    uint64_t collisionCount = 0;
    uint64_t lastCollisionTickIndex = 0;
    uint64_t collisionFeedbackCooldownTicksRemaining = 0;
    double collisionRadiusWorld = 0.0;
    BattleEnemyAiState enemyAiState = BattleEnemyAiState::None;
    BattleReplacementAiState replacementAi;
    BattleEnemyAiTargetKind enemyAiTargetKind = BattleEnemyAiTargetKind::None;
    EntityId enemyAiTargetEntityId{};
    Transform enemyAiTargetTransform{};
    double enemyAiTargetDistance = 0.0;
    double originalAiUpdateAccumulator = 0.0;
    uint64_t originalAiUpdateCount = 0;
    uint64_t originalAiDecisionSequence = 0;
    BattleOriginalAiDecisionDiagnostic lastOriginalAiDecision;
    BattleOriginalAiMotionRuntimeState originalAiMotion;
    uint64_t walkAnimationElapsedMs = 0;
    bool deathAnimationStarted = false;
    bool jumpJetsEnabled = false;
    bool jumpCapable = false;
    int jumpCapacityMeters = 0;
    int jumpJetCount = 0;
    bool jumpJetThrusting = false;
    bool jumpForwardThrusting = false;
    bool airborne = false;
    bool hardLanding = false;
    bool knockdownCandidate = false;
    double jumpFuel = 1.5;
    double jumpMaxFuel = 1.5;
    double jumpActivationFuel = 0.975;
    double verticalSpeed = 0.0;
    double jumpLaunchImpulseRemaining = 0.0;
    double landingImpactSpeed = 0.0;
    CombatantMissionStatus missionStatus = CombatantMissionStatus::Active;
    bool minimalRepairApplied = false;
    bool fragileAfterMinimalRepair = false;
    bool fireWeaponRequested = false;
    uint32_t selectedWeaponInstanceId = 0;
    std::vector<BattleWeaponInstanceState> weapons;
    bool ammunitionStateValid = false;
    std::array<int, 6> ammunitionByPool{};
    uint64_t lastFireRequestTickIndex = 0;
    uint32_t lastFireRequestWeaponInstanceId = 0;
    bool lastFireRequestAccepted = false;
    BattleHeatState heat;
    BattleMajorSystemRuntimeState majorSystems;
    bool majorSystemRuntimeInitialized = false;
    uint64_t majorSystemWarningSequence = 0;
    BattleMajorSystemWarningEvent lastMajorSystemWarning;
    uint64_t weaponCooldownTicksRemaining = 0;
    uint64_t weaponShotsFired = 0;
    uint64_t weaponHitsLanded = 0;
    EntityId lastWeaponTargetEntityId{};
    BattleMechSystemRole lastWeaponTargetSystem = BattleMechSystemRole::Unknown;
    EntityId lastDamageSourceEntityId{};
    BattlePersistentMechState persistentMechState;
    mech3d::MechRuntimeState mechRuntime;
    std::vector<mech3d::MechComponentDamageRule> damageRules;
};

struct CombatantSnapshot {
    EntityId id{};
    std::string mechPresetId;
    bool hitGeometryAvailable = false;
    uint64_t hitGeometryFingerprint = 0;
    bool playerControlled = false;
    BattleCombatantRosterMetadata roster;
    Transform transform{};
    double throttle = 0.0;
    double turn = 0.0;
    double targetForwardSpeed = 0.0;
    double forwardSpeed = 0.0;
    int originalMaxSpeedKph = 0;
    double maxForwardSpeed = 0.0;
    double maxReverseSpeed = 0.0;
    int torsoYawStep = 0;
    double torsoYawRadians = 0.0;
    int aimPitchStep = 0;
    bool boundaryContact = false;
    bool collisionContact = false;
    uint64_t collisionCount = 0;
    uint64_t lastCollisionTickIndex = 0;
    uint64_t collisionFeedbackCooldownTicksRemaining = 0;
    double collisionRadiusWorld = 0.0;
    BattleEnemyAiState enemyAiState = BattleEnemyAiState::None;
    BattleReplacementAiState replacementAi;
    BattleEnemyAiTargetKind enemyAiTargetKind = BattleEnemyAiTargetKind::None;
    EntityId enemyAiTargetEntityId{};
    Transform enemyAiTargetTransform{};
    double enemyAiTargetDistance = 0.0;
    double originalAiUpdateAccumulator = 0.0;
    uint64_t originalAiUpdateCount = 0;
    BattleOriginalAiDecisionDiagnostic lastOriginalAiDecision;
    BattleOriginalAiMotionRuntimeState originalAiMotion;
    uint64_t walkAnimationElapsedMs = 0;
    bool jumpJetsEnabled = false;
    bool jumpCapable = false;
    int jumpCapacityMeters = 0;
    int jumpJetCount = 0;
    bool jumpJetReady = false;
    bool jumpJetThrusting = false;
    bool jumpForwardThrusting = false;
    bool airborne = false;
    bool hardLanding = false;
    bool knockdownCandidate = false;
    double jumpFuel = 1.5;
    double jumpMaxFuel = 1.5;
    double jumpActivationFuel = 0.975;
    double verticalSpeed = 0.0;
    double jumpLaunchImpulseRemaining = 0.0;
    double landingImpactSpeed = 0.0;
    CombatantMissionStatus missionStatus = CombatantMissionStatus::Active;
    bool minimalRepairApplied = false;
    bool fragileAfterMinimalRepair = false;
    uint64_t weaponCooldownTicksRemaining = 0;
    uint64_t weaponShotsFired = 0;
    uint64_t weaponHitsLanded = 0;
    EntityId lastWeaponTargetEntityId{};
    BattleMechSystemRole lastWeaponTargetSystem = BattleMechSystemRole::Unknown;
    EntityId lastDamageSourceEntityId{};
    uint32_t selectedWeaponInstanceId = 0;
    std::vector<BattleWeaponInstanceState> weapons;
    uint64_t lastFireRequestTickIndex = 0;
    uint32_t lastFireRequestWeaponInstanceId = 0;
    bool lastFireRequestAccepted = false;
    BattleHeatState heat;
    BattleMajorSystemRuntimeState majorSystems;
    BattleMajorSystemWarningEvent lastMajorSystemWarning;
    std::string requestedAnimationId;
    std::string activeAnimationId;
    bool mechDestroyed = false;
    std::vector<int> destroyedComponentIds;
    std::vector<int> hiddenComponentIds;
    std::vector<int> disabledComponentIds;
    BattleMechSystemsSnapshot mechSystems;
    mech3d::MechDetailedDamageState detailedDamage;
    BattlePersistentMechState persistentMechState;
};

struct BattleSnapshot {
    uint64_t tickIndex = 0;
    uint64_t elapsedMs = 0;
    BattleCombatAiPolicy combatAiPolicy = BattleCombatAiPolicy::Disabled;
    std::string combatAiPolicyProvenance;
    BattleMissionBriefing mission;
    BattleContractMetadata contract;
    BattleOppositionRosterMetadata oppositionRoster;
    BattleTerrainSnapshot terrain;
    BattleSetupMetadata setup;
    BattleObjectiveState objective;
    BattleRetrievalRuntimeState retrieval;
    BattlefieldBoundaryState battlefieldBoundary;
    BattleMissionRuntimeState missionRuntimeState = BattleMissionRuntimeState::InProgress;
    BattleResult result;
    BattleMissionConclusionDelayState conclusionDelay;
    CameraSnapshot camera;
    std::vector<CombatantSnapshot> combatants;
    std::vector<BattleProjectileState> projectiles;
    std::vector<BattleShotDiagnostic> shotEvents;
    std::vector<BattleImpactEvent> impactEvents;
    BattleShotDiagnostic lastShot;
    BattleCollisionDiagnostic lastCollision;
    BattleCockpitRadarState cockpitRadar;
    BattleTargetScanState targetScan;
};

struct BattleReplay {
    std::vector<BattleInputCommand> commands;
};

// Header dependencies must not be allowed to produce a silently mixed battle
// ABI.  This value is evaluated in every caller and compared with the copy
// compiled into mw_battle before BattleWorld crosses the library boundary.
// In particular, BattleProjectileState is held in BattleSnapshot vectors and
// a stale element size corrupts their destruction as soon as the first
// projectile is launched.
constexpr uint64_t battleWorldHeaderAbiFingerprint() noexcept {
    uint64_t hash = 1469598103934665603ull;
    const auto mix = [&hash](uint64_t value) constexpr {
        hash ^= value;
        hash *= 1099511628211ull;
    };
    mix(sizeof(BattleStartParams));
    mix(alignof(BattleStartParams));
    mix(sizeof(BattleInputCommand));
    mix(alignof(BattleInputCommand));
    mix(sizeof(BattleWeaponInstanceState));
    mix(alignof(BattleWeaponInstanceState));
    mix(sizeof(BattleHeatState));
    mix(alignof(BattleHeatState));
    mix(sizeof(BattleProjectileState));
    mix(alignof(BattleProjectileState));
    mix(sizeof(CombatantSnapshot));
    mix(alignof(CombatantSnapshot));
    mix(sizeof(BattleSnapshot));
    mix(alignof(BattleSnapshot));
    mix(sizeof(BattleReplay));
    mix(alignof(BattleReplay));
    return hash;
}

uint64_t battleWorldLibraryAbiFingerprint() noexcept;

class BattleWorld {
public:
    static BattleWorld create(
        const BattleStartParams& params,
        uint64_t callerAbiFingerprint = battleWorldHeaderAbiFingerprint());

    EntityId playerEntityId() const;
    uint64_t tickIndex() const;
    uint64_t elapsedMs() const;
    double fixedTickSeconds() const;
    BattleMissionRuntimeState missionRuntimeState() const;
    std::optional<BattleResult> battleResult() const;

    void enqueueInput(BattleInputCommand command);
    void destroyComponent(EntityId id, int componentId);
    void hideComponent(EntityId id, int componentId);
    void disableComponent(EntityId id, int componentId);
    void setMissionStatus(EntityId id, CombatantMissionStatus status);
    void tick();
    void runTicks(uint64_t tickCount);

    const BattleSnapshot& missionStartSnapshot() const;
    BattleSnapshot snapshot() const;

private:
    BattleWorld() = default;

    EntityId spawnCombatant(
        std::string mechPresetId,
        bool playerControlled,
        Transform transform,
        BattleCombatantRosterMetadata roster);
    Combatant* findCombatant(EntityId id);
    const Combatant* findCombatant(EntityId id) const;
    void applyQueuedInputs();
    void updateDeterministicEnemyRuntime();
    void updateReplacementCombatAi();
    void updateReplacementCombatAiRequests();
    void updateOriginalMovement();
    void updateOriginalCombatAiRequests();
    void updateIndividualWeaponRuntime();
    void updateBattleProjectiles();
    void updateImpactEventHistory();
    void updateMajorSystemRuntime();
    void updateOriginalHeatRuntime();
    void updateDeterministicCombatRuntime();
    void cycleTargetScan();
    void refreshTargetScanState();
    void resolveDeterministicCollisions(
        const std::vector<Transform>& previousTransforms);
    void updateReplacementMissionRuntime();
    void evaluateMissionRuntimeAfterMovement();
    void evaluateMissionRuntimeAfterCombat();
    void evaluateMissionRuntimeAfterObjective();
    void completeMission(BattleResult result);

    BattleStartParams params_{};
    BattleTerrainSnapshot terrain_{};
    BattleObjectiveState objective_{};
    BattleRetrievalRuntimeState retrieval_{};
    std::vector<Combatant> combatants_;
    std::vector<BattleInputCommand> queuedInputs_;
    BattleSnapshot missionStartSnapshot_{};
    BattleMissionRuntimeState missionRuntimeState_ = BattleMissionRuntimeState::InProgress;
    std::optional<BattleResult> result_;
    BattleMissionConclusionDelayState conclusionDelay_;
    EntityId playerEntityId_{};
    uint32_t nextEntityId_ = 1;
    uint64_t tickIndex_ = 0;
    uint64_t elapsedMs_ = 0;
    uint64_t shotSequence_ = 0;
    BattleShotDiagnostic lastShot_{};
    std::vector<BattleShotDiagnostic> shotEvents_;
    std::vector<BattleImpactEvent> impactEvents_;
    uint32_t nextProjectileId_ = 1;
    std::vector<BattleProjectileState> projectiles_;
    uint64_t collisionSequence_ = 0;
    BattleCollisionDiagnostic lastCollision_{};
    BattleCockpitRadarState cockpitRadar_{};
    BattleTargetScanState targetScan_{};
};

BattleSnapshot runBattleReplay(
    const BattleStartParams& params,
    const BattleReplay& replay,
    uint64_t tickCount);

uint64_t battleSnapshotFingerprint(const BattleSnapshot& snapshot);

BattleCombatantLaunchState prepareNextMissionLaunchState(
    const CombatantSnapshot& previousMissionSnapshot,
    Transform nextMissionStartTransform);

} // namespace mw::battle
