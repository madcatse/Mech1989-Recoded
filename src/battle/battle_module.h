#pragma once

#include "battle/battle_world.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace mw::battle {

struct StandaloneBattleRunOptions {
    uint64_t maxTicks = 0;
    bool stopOnTerminalResult = true;
};

struct StandaloneBattleRunResult {
    BattleSnapshot startSnapshot;
    BattleSnapshot finalSnapshot;
    std::optional<BattleResult> result;
    bool terminal = false;
    uint64_t ticksExecuted = 0;
};

StandaloneBattleRunResult runStandaloneBattle(
    const BattleStartParams& params,
    const BattleReplay& replay,
    StandaloneBattleRunOptions options);

enum class CampaignBattleOutcome : uint8_t {
    Victory,
    Defeat,
    Withdraw,
    Unsupported,
};

const char* campaignBattleOutcomeName(CampaignBattleOutcome outcome);
bool campaignBattleOutcomeIsMissionFailure(CampaignBattleOutcome outcome);
bool campaignBattleDiagnosticTickLimitReached(
    std::optional<uint64_t> tickLimit,
    uint64_t ticksExecuted);

const std::array<size_t, 20>& campaignBattleTerrainScenarioIndices();
size_t campaignBattleTerrainScenarioIndexFromTargetPlanetTableOrder(
    uint16_t oneBasedTableOrder);

enum class CampaignBattleLaunchSource : uint8_t {
    Unknown,
    AcceptedContract,
    DarkWingFinal,
};

const char* campaignBattleLaunchSourceName(CampaignBattleLaunchSource source);

enum class CampaignBattlePostResultDestination : uint8_t {
    Unsupported,
    CampaignMainMenu,
};

const char* campaignBattlePostResultDestinationName(
    CampaignBattlePostResultDestination destination);

enum class CampaignBattlePresentationMode : uint8_t {
    MissionStatus,
    CockpitCommandMap,
    TacticalMap,
    Cockpit,
    External,
};

const char* campaignBattlePresentationModeName(CampaignBattlePresentationMode mode);
bool campaignBattlePresentationPausesSimulation(CampaignBattlePresentationMode mode);

struct CampaignBattleLaunchInput {
    bool acceptedContract = false;
    BattleStartParams startParams;
    BattleReplay replay;
    StandaloneBattleRunOptions runOptions;
};

struct CampaignBattleOutcomePackage {
    bool valid = false;
    CampaignBattleLaunchSource launchSource = CampaignBattleLaunchSource::Unknown;
    CampaignBattleOutcome outcome = CampaignBattleOutcome::Unsupported;
    std::string reason;
    int rawResultCode = -1;
    bool terminal = false;
    uint64_t terminalTickIndex = 0;
    uint64_t terminalElapsedMs = 0;
    uint64_t ticksExecuted = 0;
    uint64_t startSnapshotFingerprint = 0;
    uint64_t finalSnapshotFingerprint = 0;
    size_t startCombatantCount = 0;
    size_t finalCombatantCount = 0;
    bool objectiveValid = false;
    bool contractMetadataValid = false;
};

enum class CampaignBattleDebriefPresentation : uint8_t {
    None,
    Victory,
    Defeat,
};

CampaignBattleDebriefPresentation campaignBattleDebriefPresentationForOutcome(
    const CampaignBattleOutcomePackage& outcome);

struct CampaignContractVisitLedger {
    int visitSerial = 0;
    std::vector<size_t> completedOfferSlots;
};

void beginCampaignContractVisit(
    CampaignContractVisitLedger& ledger,
    int visitSerial);
bool campaignContractOfferAvailableForVisit(
    const CampaignContractVisitLedger& ledger,
    int visitSerial,
    size_t offerSlot);
bool completeCampaignContractOfferForVisit(
    CampaignContractVisitLedger& ledger,
    int visitSerial,
    size_t offerSlot);

struct CampaignBattleConsequencePlan {
    bool valid = false;
    bool commitEligible = false;
    CampaignBattleLaunchSource launchSource = CampaignBattleLaunchSource::Unknown;
    CampaignBattleOutcome outcome = CampaignBattleOutcome::Unsupported;
    std::string reason;
    uint64_t payment = 0;
    uint64_t salvage = 0;
    int reputationDelta = 0;
    std::array<int, 5> housePositiveDelta{};
    std::array<int, 5> houseNegativeDelta{};
    uint32_t campaignDateTicks = 0;
    bool pilotExperienceAward = false;
    bool salvageBetaValidationRequired = false;
    bool salvageDeferred = true;
    bool campaignTimeDeferred = true;
    bool pilotExperienceDeferred = true;
    bool persistentMechDamageDeferred = true;
    bool repairCostDeferred = true;
    bool pilotDeathDeferred = true;
    uint64_t outcomeFingerprint = 0;
    uint64_t planFingerprint = 0;
};

struct CampaignBattleConsequenceContext {
    uint64_t salvage = 0;
    uint32_t campaignDateTicks = 0;
    bool pilotExperienceAward = false;
    bool salvageBetaValidationRequired = false;
    bool salvageEstimateAvailable = false;
    bool campaignTimeProven = false;
    bool pilotExperienceProven = false;
};

struct CampaignPilotExperienceState {
    uint8_t skill = 0;
    uint16_t missionCounter = 0;
    bool promoted = false;
};

struct CampaignBattleConsequenceApplyGuard {
    bool valid = false;
    bool allowed = false;
    bool preconditionMatched = false;
    bool duplicateRejected = false;
    std::string reason;
};

struct CampaignBattleConsequenceCommitReceipt {
    bool valid = false;
    bool attempted = false;
    bool committed = false;
    bool verified = false;
    bool saveRoundTripVerified = false;
    bool duplicateRejected = false;
    bool deferredConsequencesPresent = false;
    std::string reason;
    uint64_t planFingerprint = 0;
    uint64_t beforeStateFingerprint = 0;
    uint64_t afterStateFingerprint = 0;
};

struct CampaignBattlePersistentRosterBinding {
    std::string sourceSlot;
    int ownedMechIndex = -1;
    int crewSlot = -1;
};

struct CampaignBattleEntryMechStateGuard {
    bool valid = false;
    bool playerStatesPreserved = false;
    bool opposingStatesPristine = false;
    bool allCombatantStatesPreserved = false;
    std::string reason;
    uint64_t startSnapshotFingerprint = 0;
    uint64_t finalSnapshotFingerprint = 0;
    uint64_t guardFingerprint = 0;
    size_t playerCombatantCount = 0;
    size_t opposingCombatantCount = 0;
    size_t preservedCombatantCount = 0;
    size_t missingFinalCombatantCount = 0;
    size_t invalidEntryStateCount = 0;
    size_t nonPristineOpposingCount = 0;
};

struct CampaignBattlePersistentCombatantReport {
    bool valid = false;
    std::string sourceSlot;
    int ownedMechIndex = -1;
    int crewSlot = -1;
    EntityId entityId{};
    std::string mechPresetId;
    CombatantMissionStatus missionStatus = CombatantMissionStatus::Active;
    bool mechDestroyed = false;
    BattleMechSystemsSnapshot mechSystems;
    BattlePersistentMechState persistentMechState;
    std::vector<int> destroyedComponentIds;
    std::vector<int> disabledComponentIds;
};

struct CampaignBattlePersistenceReport {
    bool valid = false;
    bool allBindingsMapped = false;
    bool allMissionParticipantsLaunched = false;
    std::string reason;
    uint64_t finalSnapshotFingerprint = 0;
    uint64_t reportFingerprint = 0;
    size_t requestedBindingCount = 0;
    size_t mappedBindingCount = 0;
    size_t unmappedBindingCount = 0;
    size_t duplicateBindingCount = 0;
    size_t duplicateCombatantCount = 0;
    size_t unboundPlayerCombatantCount = 0;
    size_t missionParticipantCount = 0;
    size_t unlaunchedMissionParticipantCount = 0;
    bool persistentDamageTranslationDeferred = true;
    bool repairCostTranslationDeferred = true;
    bool pilotSurvivalTranslationDeferred = true;
    std::vector<CampaignBattlePersistentCombatantReport> combatants;
};

enum class CampaignBattlePersistentDamageField : uint8_t {
    Engine,
    Gyros,
    Sensors,
    LifeSupport,
    HeatSinkCount,
    LeftArmActuator,
    RightArmActuator,
    LeftLegActuator,
    RightLegActuator,
    JumpJetCount,
    WeaponConditions,
    ArmorSections,
};

struct CampaignBattlePersistentDamageFieldAudit {
    CampaignBattlePersistentDamageField field =
        CampaignBattlePersistentDamageField::Engine;
    size_t persistentFieldCount = 0;
    BattleMechSystemRole sourceRole = BattleMechSystemRole::Unknown;
    BattleMechSystemStatus observedStatus = BattleMechSystemStatus::Online;
    bool sourceObserved = false;
    bool exactMappingProven = false;
    std::string reason;
};

struct CampaignBattlePersistentDamageCombatantPlan {
    bool valid = false;
    std::string sourceSlot;
    int ownedMechIndex = -1;
    int crewSlot = -1;
    EntityId entityId{};
    CombatantMissionStatus missionStatus = CombatantMissionStatus::Active;
    bool mechDestroyed = false;
    size_t persistentFieldCount = 0;
    size_t exactPersistentFieldCount = 0;
    size_t ambiguousPersistentFieldCount = 0;
    size_t missingSourcePersistentFieldCount = 0;
    bool mutationEligible = false;
    bool noMutationGuaranteed = true;
    std::string reason;
    std::vector<CampaignBattlePersistentDamageFieldAudit> fields;
};

struct CampaignBattlePersistentDamageTranslationPlan {
    bool valid = false;
    bool allBindingsMapped = false;
    bool allMissionParticipantsLaunched = false;
    bool mutationEligible = false;
    bool noMutationGuaranteed = true;
    std::string reason;
    uint64_t persistenceReportFingerprint = 0;
    uint64_t planFingerprint = 0;
    size_t combatantCount = 0;
    size_t persistentFieldCount = 0;
    size_t exactPersistentFieldCount = 0;
    size_t ambiguousPersistentFieldCount = 0;
    size_t missingSourcePersistentFieldCount = 0;
    size_t unlaunchedMissionParticipantCount = 0;
    std::vector<CampaignBattlePersistentDamageCombatantPlan> combatants;
};

struct CampaignBattlePostResultReceipt {
    bool valid = false;
    CampaignBattleLaunchSource launchSource = CampaignBattleLaunchSource::Unknown;
    CampaignBattleOutcome outcome = CampaignBattleOutcome::Unsupported;
    CampaignBattlePostResultDestination destination =
        CampaignBattlePostResultDestination::Unsupported;
    std::string reason;
    std::string outcomeReason;
    bool outcomeAcknowledged = false;
    bool safeCampaignReturn = false;
    bool persistentStateUnchanged = false;
    bool settlementCommitted = false;
    bool deferredConsequencesPresent = false;
    uint64_t consequencePlanFingerprint = 0;
    bool consequenceSaveRoundTripVerified = false;
    bool extendedEndingSequenceExecuted = false;
    bool extendedEndingSequenceDeferred = false;
    bool terminal = false;
    int rawResultCode = -1;
    uint64_t finalSnapshotFingerprint = 0;
    uint64_t terminalTickIndex = 0;
    uint64_t terminalElapsedMs = 0;
    uint64_t ticksExecuted = 0;
};

struct CampaignBattlePresentationPackage {
    bool valid = false;
    CampaignBattlePresentationMode mode = CampaignBattlePresentationMode::TacticalMap;
    std::string reason;
    uint64_t snapshotFingerprint = 0;
    size_t combatantCount = 0;
    size_t playerCombatantCount = 0;
    size_t opposingCombatantCount = 0;
    size_t neutralCombatantCount = 0;
    bool objectiveValid = false;
    bool battlefieldBoundaryValid = false;
    bool terrainValid = false;
    bool cameraValid = false;
    bool playerValid = false;
    EntityId playerEntityId{};
    double playerX = 0.0;
    double playerZ = 0.0;
    double playerHeadingRadians = 0.0;
    double cameraHeadingRadians = 0.0;
    double torsoYawRadians = 0.0;
    int playerHeadingDegrees = 0;
    int cameraHeadingDegrees = 0;
    int torsoYawDegrees = 0;
    int aimPitchStep = 0;
    int speed = 0;
    bool jumpCapable = false;
    bool jumpJetReady = false;
    bool tacticalMapReady = false;
    bool cockpitReady = false;
    bool externalReady = false;
    bool snapshotOwned = false;
};

enum class CampaignCockpitFamily : uint8_t {
    Light,
    Medium,
    Heavy,
};

const char* campaignCockpitFamilyName(CampaignCockpitFamily family);

struct CampaignBattleRenderVisualInstance {
    EntityId entityId{};
    BattleTeam team = BattleTeam::Neutral;
    bool playerControlled = false;
    std::string rosterSourceSlot;
    std::string mechPresetId;
    std::filesystem::path modelResourcePath;
    Transform transform{};
    double forwardSpeed = 0.0;
    double maxForwardSpeed = 0.0;
    double maxReverseSpeed = 0.0;
    double torsoYawRadians = 0.0;
    int aimPitchStep = 0;
    std::string animationId;
    uint64_t animationElapsedMs = 0;
    bool mechDestroyed = false;
    std::vector<int> destroyedComponentIds;
    std::vector<int> hiddenComponentIds;
    std::vector<int> disabledComponentIds;
};

struct CampaignBattleRenderObjectiveInstance {
    bool valid = false;
    bool playerProtected = false;
    Transform transform{};
    BattleStaticModelRef staticModel;
    std::filesystem::path modelResourcePath;
    int damage = 0;
    int maxDamage = 0;
    bool depleted = false;
};

struct CampaignBattleRenderProjectileInstance {
    uint32_t projectileId = 0;
    uint64_t launchTickIndex = 0;
    EntityId shooterEntityId{};
    uint32_t weaponInstanceId = 0;
    std::string weaponTypeId;
    std::string locationId;
    Transform launchShooterTransform;
    double launchTorsoYawRadians = 0.0;
    BattleHitPoint launchModelMount;
    bool launchModelMountValid = false;
    std::filesystem::path modelResourcePath;
    int recordIndex = -1;
    BattleHitPoint position;
    BattleHitPoint direction;
    BattleHitPoint launchAimOrigin;
    bool launchAimOriginValid = false;
    BattleHitPoint visualTargetPoint;
    bool visualTargetPointValid = false;
    bool targetBound = false;
    double launchForwardOffset = 0.0;
    double spinDegrees = 0.0;
    bool originalResourceMappingProven = false;
    bool spinTimingProven = false;
    bool modelMountPolicyProven = false;
};

enum class CampaignBattleBeamColor : uint8_t {
    LaserYellow,
    PpcCyan,
};

// Immutable, fixed-tick presentation description for the filled
// five-vertex beam volume created by BTECH.EXE:FUN_1000_bc5e.  It is derived
// from the authoritative shot diagnostic; presentation never feeds it back
// into hit resolution or the replay fingerprint.
struct CampaignBattleRenderBeamInstance {
    uint64_t shotSequence = 0;
    uint64_t shotTickIndex = 0;
    EntityId shooterEntityId{};
    uint32_t weaponInstanceId = 0;
    std::string weaponTypeId;
    std::string locationId;
    BattleHitPoint start;
    BattleHitPoint end;
    double halfWidth = 0.0;
    CampaignBattleBeamColor color = CampaignBattleBeamColor::LaserYellow;
    bool originalTopologyProven = false;
    bool widthScaleProven = false;
    bool originalOneTickLifetimeProven = false;
    bool originalColorClassProven = false;
    bool mountOffsetProven = false;
};

struct CampaignBattleRenderMachineGunFlashInstance {
    uint64_t shotSequence = 0;
    uint64_t shotTickIndex = 0;
    EntityId shooterEntityId{};
    uint32_t weaponInstanceId = 0;
    std::string locationId;
    int spriteIndex = -1;
    bool rightSide = false;
    bool originalResourceMappingProven = false;
    bool originalPlacementProven = false;
    bool originalOneTickLifetimeProven = false;
};

struct CampaignBattleRenderImpactInstance {
    uint64_t shotSequence = 0;
    uint64_t impactTickIndex = 0;
    BattleShotTargetKind targetKind = BattleShotTargetKind::None;
    EntityId targetEntityId{};
    BattleHitPoint position;
    std::filesystem::path modelResourcePath;
    int recordIndex = -1;
    bool machineGunSequence = false;
    bool terrainSmokeSequence = false;
    bool originalResourceMappingProven = false;
    bool originalSixUpdateSequenceProven = false;
    bool originalMachineGunTwoUpdateSequenceProven = false;
    bool stageDurationProven = false;
    bool sphereRasterizationProven = false;
};

struct CampaignBattleRenderTerrainResources {
    bool valid = false;
    size_t scenarioIndex = 0;
    uint8_t terrainMode = 0;
    std::optional<int> environmentId;
    double boundsMinX = 0.0;
    double boundsMaxX = 0.0;
    double boundsMinZ = 0.0;
    double boundsMaxZ = 0.0;
    std::filesystem::path terrainShapePath;
    std::filesystem::path palettePath;
    std::vector<std::filesystem::path> gridPaths;
    std::vector<std::filesystem::path> worldPaths;
};

struct CampaignBattleRenderCockpitResources {
    bool valid = false;
    CampaignCockpitFamily family = CampaignCockpitFamily::Light;
    std::filesystem::path backdropPath;
    std::filesystem::path palettePath;
    std::filesystem::path strutsPath;
    std::filesystem::path widgetsPath;
    std::filesystem::path hudNumbersPath;
    std::filesystem::path hudFontPath;
    std::filesystem::path smallMechsPath;
};

struct CampaignBattleRenderScenePackage {
    bool valid = false;
    std::string reason;
    CampaignBattlePresentationMode mode = CampaignBattlePresentationMode::TacticalMap;
    uint64_t snapshotFingerprint = 0;
    uint64_t authoritativeTickIndex = 0;
    bool playerCollisionFlashEvent = false;
    uint64_t playerCollisionSequence = 0;
    CameraSnapshot camera;
    BattleMissionBriefing mission;
    BattleSetupMetadata setup;
    BattlefieldBoundaryState battlefieldBoundary;
    double cockpitCameraBobOffsetY = 0.0;
    bool cockpitCrosshairVisible = true;
    bool cockpitReactorShutdown = false;
    bool cockpitReactorShutdownMessageVisible = false;
    CampaignBattleRenderTerrainResources terrain;
    CampaignBattleRenderCockpitResources cockpit;
    BattleTargetScanTargetKind scannedTargetKind =
        BattleTargetScanTargetKind::None;
    EntityId scannedTargetEntityId{};
    std::vector<CampaignBattleRenderVisualInstance> combatantVisuals;
    std::vector<CampaignBattleRenderProjectileInstance> projectileVisuals;
    std::vector<CampaignBattleRenderBeamInstance> beamVisuals;
    std::vector<CampaignBattleRenderMachineGunFlashInstance>
        machineGunFlashVisuals;
    std::vector<CampaignBattleRenderImpactInstance> impactVisuals;
    CampaignBattleRenderObjectiveInstance objective;
    size_t playerVisualCount = 0;
    size_t opposingVisualCount = 0;
    bool presentationInterpolated = false;
    uint64_t previousSnapshotTickIndex = 0;
    uint64_t currentSnapshotTickIndex = 0;
    double interpolationAlpha = 1.0;
    size_t interpolatedVisualCount = 0;
    size_t interpolatedProjectileCount = 0;
    bool resourcePathsResolved = false;
    bool snapshotOwned = false;
};

int campaignBattleProjectileRecordIndex(uint16_t originalVisualClass);
int campaignBattleMachineGunFlashSpriteIndex(
    CampaignCockpitFamily family,
    const std::string& locationId);
double campaignBattleProjectileSpinDegrees(
    uint64_t launchTickIndex,
    uint64_t snapshotTickIndex);

struct BattleCockpitBobTuning {
    double cycleMs = 720.0;
    double maxWorldUnits = 45.0;
};

constexpr BattleCockpitBobTuning battleCockpitBobPrototypeTuning() {
    return {};
}

double battleCockpitBobSpeedRatio(
    double forwardSpeed,
    double maxForwardSpeed,
    double maxReverseSpeed);

double battleCockpitBobOffsetWorldUnits(
    double forwardSpeed,
    double maxForwardSpeed,
    double maxReverseSpeed,
    double animationPhaseElapsedMs,
    bool mechDestroyed,
    const BattleCockpitBobTuning& tuning = battleCockpitBobPrototypeTuning());

// Resolves an original DOS resource from either a flat installation directory
// (root/FILE.EXT) or the repository's categorized evidence layout
// (root/TBL/viewer8/FILE.EXT, root/PAL/FILE.EXT, and so on).
std::filesystem::path campaignBattleOriginalResourcePath(
    const std::filesystem::path& originalFilesRoot,
    const std::filesystem::path& categorizedRelativePath);

double campaignBattleCatalogCockpitCameraHeight(
    const std::filesystem::path& sortedOriginalFilesRoot,
    const std::string& mechPresetId);
double campaignBattleCatalogCollisionRadius(
    const std::filesystem::path& sortedOriginalFilesRoot,
    const std::string& mechPresetId);
BattleMechHitProfile campaignBattleCatalogHitProfile(
    const std::filesystem::path& sortedOriginalFilesRoot,
    const std::string& mechPresetId);

CampaignBattleOutcomePackage runCampaignBattleLaunchAdapter(
    const CampaignBattleLaunchInput& input);

CampaignBattleOutcomePackage campaignBattleOutcomePackageFromSnapshots(
    const BattleSnapshot& startSnapshot,
    const BattleSnapshot& finalSnapshot,
    const std::optional<BattleResult>& result,
    uint64_t ticksExecuted);

CampaignBattleConsequencePlan campaignBattleConsequencePlanFromOutcome(
    const CampaignBattleOutcomePackage& outcome,
    const BattleContractMetadata& contract);

CampaignBattleConsequencePlan campaignBattleConsequencePlanFromOutcome(
    const CampaignBattleOutcomePackage& outcome,
    const BattleContractMetadata& contract,
    const CampaignBattleConsequenceContext& context);

uint16_t campaignBattleOriginalMissionDurationDateTicks(uint8_t originalMissionId);

uint32_t campaignBattleOriginalMissionElapsedDateTicks(
    uint16_t jumpCount,
    uint8_t originPlanetTimeFactor,
    uint8_t targetPlanetTimeFactor,
    uint8_t originalMissionId);

CampaignPilotExperienceState campaignPilotExperienceAfterSurvivedMission(
    uint8_t skill,
    uint16_t missionCounter,
    bool commander);

CampaignBattleConsequenceApplyGuard campaignBattleConsequencePlanApplyGuard(
    const CampaignBattleConsequencePlan& plan,
    uint64_t expectedStateFingerprint,
    uint64_t currentStateFingerprint,
    uint64_t lastCommittedPlanFingerprint);

CampaignBattlePersistenceReport campaignBattlePersistenceReportFromSnapshot(
    const BattleSnapshot& finalSnapshot,
    const std::vector<CampaignBattlePersistentRosterBinding>& bindings,
    size_t missionParticipantCount);

CampaignBattleEntryMechStateGuard campaignBattleEntryMechStateGuardFromSnapshots(
    const BattleSnapshot& startSnapshot,
    const BattleSnapshot& finalSnapshot);

CampaignBattlePersistentDamageTranslationPlan
campaignBattlePersistentDamageTranslationPlanFromReport(
    const CampaignBattlePersistenceReport& report);

CampaignBattlePostResultReceipt campaignBattlePostResultReceiptFromOutcome(
    const CampaignBattleOutcomePackage& outcome,
    bool persistentStateUnchanged);

CampaignBattlePostResultReceipt campaignBattlePostResultReceiptFromOutcome(
    const CampaignBattleOutcomePackage& outcome,
    bool persistentStateUnchanged,
    const CampaignBattleConsequenceCommitReceipt& consequenceCommit);

CampaignBattlePresentationPackage campaignBattlePresentationPackageFromSnapshot(
    const BattleSnapshot& snapshot,
    CampaignBattlePresentationMode mode);

CampaignBattlePresentationPackage campaignBattleInterpolatedPresentationPackageFromSnapshots(
    const BattleSnapshot& previousSnapshot,
    const BattleSnapshot& currentSnapshot,
    double interpolationAlpha,
    CampaignBattlePresentationMode mode);

CampaignBattleRenderScenePackage campaignBattleRenderSceneFromSnapshot(
    const BattleSnapshot& snapshot,
    CampaignBattlePresentationMode mode,
    const std::filesystem::path& sortedOriginalFilesRoot);

CampaignBattleRenderScenePackage campaignBattleInterpolatedRenderSceneFromSnapshots(
    const BattleSnapshot& previousSnapshot,
    const BattleSnapshot& currentSnapshot,
    double interpolationAlpha,
    CampaignBattlePresentationMode mode,
    const std::filesystem::path& sortedOriginalFilesRoot);

} // namespace mw::battle
