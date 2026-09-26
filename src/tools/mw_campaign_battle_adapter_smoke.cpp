#include "battle/battle_module.h"
#include "battle/battle_setup.h"
#include "presentation/campaign_battle_gl_viewport.h"
#include "presentation/campaign_cockpit_compositor.h"
#include "mech3d/mech_catalog.h"
#include "mech3d/model_assembly.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Options {
    std::filesystem::path snarioPath = std::filesystem::path("Sorted Original Files") / "DAT" / "SNARIO.DAT";
    size_t scenarioIndex = 2;
};

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--snario") {
            if (++i >= argc) {
                throw std::runtime_error("--snario requires a path");
            }
            options.snarioPath = argv[i];
        } else if (arg == "--scenario-index") {
            if (++i >= argc) {
                throw std::runtime_error("--scenario-index requires a value");
            }
            options.scenarioIndex = static_cast<size_t>(std::stoul(argv[i]));
        } else {
            throw std::runtime_error("unknown argument: " + arg);
        }
    }
    return options;
}

mw::battle::BattleReplay playerFireReplay() {
    mw::battle::BattleReplay replay;
    for (uint64_t fireTick : {0ull, 3ull, 6ull}) {
        mw::battle::BattleInputCommand command;
        command.tickIndex = fireTick;
        command.fireWeapon = true;
        replay.commands.push_back(command);
    }
    return replay;
}

mw::battle::BattleReplay westExitReplay() {
    mw::battle::BattleInputCommand command;
    command.tickIndex = 0;
    command.throttle = 1.0;
    return mw::battle::BattleReplay{{command}};
}

mw::battle::BattleStartParams campaignAcceptedParams(
    const Options& options,
    bool addOpposingCombatant,
    bool hostileTargetHouse = true) {
    mw::battle::BattleStartParams params;
    mw::battle::applyBattleDriveRuntimeTuning(params);
    params.terrainScenarioPath = options.snarioPath;
    params.terrainScenarioIndex = options.scenarioIndex;
    params.terrainEnvironmentId = 1;
    params.mission = mw::battle::defaultBattleMissionBriefing();
    params.contract.valid = true;
    params.contract.provenance = "campaign_contract";
    params.contract.employerHouseId = 4;
    params.contract.employerHouseName = "DAVION";
    params.contract.targetHouseId = 3;
    params.contract.targetHouseName = "LIAO";
    params.contract.hasHostileTargetHouse = hostileTargetHouse;
    params.contract.targetPlanetName = "WARLOCK";
    params.contract.targetPlanetTerrainCode = 11;
    params.contract.targetEnvironmentId = 1;
    params.contract.terrainScenarioIndex = options.scenarioIndex;
    params.contract.terrainScenarioProvenance = "smoke_explicit_snario_candidate";
    params.contract.estimatedHeavyMechs = 0;
    params.contract.estimatedMediumMechs = 0;
    params.contract.estimatedLightMechs = 1;
    params.contract.oppositionEstimateVariable = true;
    params.contract.garrisonAutoResolveDeferred = true;
    params.contract.oppositionSpawnPlan = mw::battle::decodeFreshBtechOppositionSpawnPlan(
        {{0, 0, 1}},
        "campaign_hml_to_btech_context_3_5");
    params.contract.priceK = 150;
    params.contract.salvagePercent = 5;
    params.contract.advancePercent = 5;
    params.playerMechPresetId = "locust";
    params.playerRoster.team = mw::battle::BattleTeam::Player;
    params.playerRoster.factionHouseId = static_cast<uint8_t>(4);
    params.playerRoster.factionHouseName = "DAVION";
    params.playerRoster.provenance = "accepted_campaign_contract";
    params.playerRoster.sourceSlot = "player:0";

    const mw::battle::OriginalBattlefieldSetup setup =
        mw::battle::decodeOriginalBattlefieldSetup(
            options.snarioPath,
            options.scenarioIndex,
            params.mission.originalId,
            params.terrainCellSize);
    params.playerStartTransform = setup.playerStartTransform;
    params.setupMetadata = setup.metadata;
    params.objective = setup.objective;
    params.battlefieldBoundary = setup.battlefieldBoundary;

    if (addOpposingCombatant) {
        mw::battle::BattleCombatantLaunchState enemy;
        enemy.mechPresetId = "locust";
        enemy.startTransform = setup.enemyStartTransform;
        enemy.roster.team = mw::battle::BattleTeam::Opposing;
        if (hostileTargetHouse) {
            enemy.roster.factionHouseId = static_cast<uint8_t>(3);
            enemy.roster.factionHouseName = "LIAO";
            enemy.roster.provenance = "phase11_deterministic_adapter_explicit_roster";
        } else {
            enemy.roster.factionHouseName = "UNAFFILIATED";
            enemy.roster.provenance = "phase11_deterministic_adapter_garrison_opposition_roster";
        }
        enemy.roster.sourceSlot = "opposing:0";
        params.combatantLaunchStates.push_back(std::move(enemy));
    }
    return params;
}

mw::battle::BattleStartParams campaignDarkWingFinalParams(const Options& options) {
    mw::battle::BattleStartParams params;
    mw::battle::applyBattleDriveRuntimeTuning(params);
    params.terrainScenarioPath = options.snarioPath;
    params.terrainScenarioIndex = options.scenarioIndex;
    params.playerMechPresetId = "locust";
    params.playerRoster.team = mw::battle::BattleTeam::Player;
    params.playerRoster.provenance = "campaign_story_dark_wing_final";
    params.playerRoster.sourceSlot = "player:0";

    const std::optional<mw::battle::BattleMissionDefinition> definition =
        mw::battle::battleMissionDefinitionById(12);
    expect(definition.has_value(), "Dark Wing final mission definition missing");
    params.mission = mw::battle::battleMissionBriefingFromDefinition(*definition);

    const mw::battle::OriginalBattlefieldSetup setup =
        mw::battle::decodeOriginalDarkWingFinalBattlefieldSetup(
            options.snarioPath,
            options.scenarioIndex,
            params.terrainCellSize);
    params.playerStartTransform = setup.playerStartTransform;
    params.setupMetadata = setup.metadata;
    params.battlefieldBoundary = setup.battlefieldBoundary;
    params.objective = {};
    mw::battle::applyOriginalDarkWingFinalOpposition(
        params,
        setup,
        std::nullopt,
        "DARK WING");
    params.combatAiPolicy =
        mw::battle::BattleCombatAiPolicy::Phase10CompatibilityFsm;
    params.deterministicCombatRuntimeEnabled = true;
    params.mechSystemsSnapshotEnabled = true;
    return params;
}

mw::battle::CampaignBattleOutcomePackage runAdapter(
    mw::battle::BattleStartParams params,
    mw::battle::BattleReplay replay,
    uint64_t maxTicks,
    bool acceptedContract = true) {
    mw::battle::CampaignBattleLaunchInput input;
    input.acceptedContract = acceptedContract;
    input.startParams = std::move(params);
    input.replay = std::move(replay);
    input.runOptions.maxTicks = maxTicks;
    input.runOptions.stopOnTerminalResult = true;
    return mw::battle::runCampaignBattleLaunchAdapter(input);
}

mw::battle::CampaignBattleOutcomePackage runLiveSessionAdapter(
    const mw::battle::BattleStartParams& params,
    const mw::battle::BattleReplay& replay,
    uint64_t maxTicks) {
    mw::battle::BattleWorld world = mw::battle::BattleWorld::create(params);
    for (mw::battle::BattleInputCommand command : replay.commands) {
        world.enqueueInput(command);
    }
    const mw::battle::BattleSnapshot startSnapshot = world.missionStartSnapshot();
    uint64_t ticksExecuted = 0;
    while (!world.battleResult().has_value() && ticksExecuted < maxTicks) {
        world.tick();
        ++ticksExecuted;
    }
    return mw::battle::campaignBattleOutcomePackageFromSnapshots(
        startSnapshot,
        world.snapshot(),
        world.battleResult(),
        ticksExecuted);
}

mw::battle::CampaignBattleOutcomePackage runInteractiveInputSessionAdapter(
    const mw::battle::BattleStartParams& params,
    const mw::battle::BattleReplay& replay,
    uint64_t maxTicks) {
    mw::battle::BattleWorld world = mw::battle::BattleWorld::create(params);
    const mw::battle::BattleSnapshot startSnapshot = world.missionStartSnapshot();
    uint64_t ticksExecuted = 0;
    while (!world.battleResult().has_value() && ticksExecuted < maxTicks) {
        const uint64_t tickIndex = world.tickIndex();
        for (mw::battle::BattleInputCommand command : replay.commands) {
            if (command.tickIndex != tickIndex) {
                continue;
            }
            if (!mw::battle::isValid(command.entityId)) {
                command.entityId = world.playerEntityId();
            }
            world.enqueueInput(command);
        }
        world.tick();
        ++ticksExecuted;
    }
    return mw::battle::campaignBattleOutcomePackageFromSnapshots(
        startSnapshot,
        world.snapshot(),
        world.battleResult(),
        ticksExecuted);
}

struct CampaignPresentationProbe {
    bool tacticalMapReady = false;
    bool cockpitReady = false;
    bool externalReady = false;
    bool snapshotOwned = false;
    size_t combatants = 0;
    bool renderSceneReady = false;
    bool destroyedSensorReticleSuppressed = false;
    bool reactorShutdownHandoffReady = false;
    bool renderResourcesPresent = false;
    bool objectiveSeparate = false;
    bool cockpitResolved = false;
    size_t renderVisuals = 0;
    bool projectileVisualsReady = false;
    bool projectileInterpolationReady = false;
    bool immediateBeamVisualsReady = false;
    bool machineGunFlashVisualsReady = false;
    bool machineGunImpactVisualsReady = false;
    bool impactVisualsReady = false;
    size_t projectileVisuals = 0;
    size_t projectileVertices = 0;
    size_t projectileTriangles = 0;
    size_t terrainTiles = 0;
    bool glResourcesReady = false;
    bool farGroundApronReady = false;
    size_t glTerrainVertices = 0;
    size_t glTerrainObjects = 0;
    size_t glTerrainObjectRecords = 0;
    size_t glTerrainObjectVertices = 0;
    bool glTerrainPaletteLoaded = false;
    std::string glTerrainPaletteId;
    size_t glTerrainObjectDitherVertices = 0;
    size_t glTerrainObjectDitherTriangles = 0;
    bool glTerrainEnvironmentPalettesReady = false;
    size_t glVisuals = 0;
    bool glObjectiveLoaded = false;
    bool cockpitBackdropReady = false;
    int cockpitBackdropCodec = 0;
    int cockpitViewportWidth = 0;
    int cockpitViewportHeight = 0;
    bool cockpitWeaponPanelLayoutsReady = false;
    bool cockpitTargetScanReady = false;
    bool mechHitProfilesReady = false;
    size_t mechHitProfileTriangles = 0;
    bool cockpitDynamicLayersReady = false;
    size_t cockpitStruts = 0;
    size_t cockpitWidgets = 0;
    size_t cockpitHudNumbers = 0;
    int cockpitFontWidth = 0;
    int cockpitFontHeight = 0;
    size_t lightStrutPlacements = 0;
    size_t mediumStrutPlacements = 0;
    size_t heavyStrutPlacements = 0;
    bool battleStatusBmpsReady = false;
    size_t battleStatusBmps = 0;
    size_t battleStatusBlackArmorPixels = 0;
    size_t battleStatusCyanOutlinePixels = 0;
    bool cockpitGaugesReady = false;
    size_t cockpitHudColors = 0;
    int cockpitMaxZoom = 0;
    bool cockpitCompassRightTurn = false;
    bool cockpitViewportHudReady = false;
    bool externalCameraReady = false;
    bool missionStatusMapReady = false;
    int missionStatusBackdropCodec = 0;
    bool cockpitCommandMapReady = false;
    int cockpitCommandMapBackdropCodec = 0;
    bool cockpitMinimapReady = false;
    bool cockpitRadarReady = false;
    size_t cockpitMinimapTerrainSamples = 0;
    size_t cockpitMinimapSurfacePlacements = 0;
    size_t cockpitMinimapMountainPlacements = 0;
    size_t cockpitMinimapMountainGradientPlacements = 0;
    size_t cockpitMinimapMountainFourBandPlacements = 0;
    std::array<size_t, 4> cockpitMinimapHeightBandSamples{};
};

struct CampaignRenderInterpolationProbe {
    bool valid = false;
    size_t interpolatedVisuals = 0;
};

struct CampaignCockpitMotionProbe {
    bool valid = false;
    int locustHeight = 0;
    int phoenixHawkHeight = 0;
    int battleMasterHeight = 0;
};

CampaignRenderInterpolationProbe probeCampaignRenderInterpolation(
    const mw::battle::BattleStartParams& params,
    const std::filesystem::path& sortedOriginalFilesRoot) {
    constexpr double pi = 3.14159265358979323846;
    const auto radians = [](double degrees) {
        constexpr double localPi = 3.14159265358979323846;
        return degrees * localPi / 180.0;
    };
    mw::battle::BattleSnapshot previous =
        mw::battle::BattleWorld::create(params).snapshot();
    mw::battle::BattleSnapshot current = previous;
    expect(previous.combatants.size() >= 2u, "render interpolation probe roster missing");

    previous.tickIndex = 40;
    current.tickIndex = 41;
    previous.combatants[0].transform = {100.0, 10.0, 200.0, radians(350.0)};
    current.combatants[0].transform = {300.0, 30.0, 600.0, radians(10.0)};
    previous.combatants[0].torsoYawRadians = radians(170.0);
    current.combatants[0].torsoYawRadians = radians(-170.0);
    previous.combatants[0].activeAnimationId = "walk";
    current.combatants[0].activeAnimationId = "walk";
    previous.combatants[0].walkAnimationElapsedMs = 100;
    current.combatants[0].walkAnimationElapsedMs = 200;
    previous.combatants[1].transform = {1000.0, 0.0, 1200.0, radians(350.0)};
    current.combatants[1].transform = {1400.0, 0.0, 1600.0, radians(10.0)};
    previous.camera.transform = {110.0, 400.0, 220.0, radians(350.0)};
    current.camera.transform = {310.0, 420.0, 620.0, radians(10.0)};
    previous.objective.damage = 1;
    current.objective.damage = 2;
    current.combatants[0].destroyedComponentIds.push_back(1);

    const uint64_t previousFingerprint = mw::battle::battleSnapshotFingerprint(previous);
    const uint64_t currentFingerprint = mw::battle::battleSnapshotFingerprint(current);
    const mw::battle::CampaignBattleRenderScenePackage scene =
        mw::battle::campaignBattleInterpolatedRenderSceneFromSnapshots(
            previous,
            current,
            0.5,
            mw::battle::CampaignBattlePresentationMode::External,
            sortedOriginalFilesRoot);
    const mw::battle::CampaignBattlePresentationPackage presentation =
        mw::battle::campaignBattleInterpolatedPresentationPackageFromSnapshots(
            previous,
            current,
            0.5,
            mw::battle::CampaignBattlePresentationMode::Cockpit);

    const auto visualById = [&scene](mw::battle::EntityId id) {
        for (const mw::battle::CampaignBattleRenderVisualInstance& visual :
             scene.combatantVisuals) {
            if (visual.entityId == id) {
                return &visual;
            }
        }
        return static_cast<const mw::battle::CampaignBattleRenderVisualInstance*>(nullptr);
    };
    const mw::battle::CampaignBattleRenderVisualInstance* player =
        visualById(current.combatants[0].id);
    const mw::battle::CampaignBattleRenderVisualInstance* enemy =
        visualById(current.combatants[1].id);
    const auto approximatelyEqual = [](double a, double b) {
        return std::abs(a - b) < 0.000001;
    };

    CampaignRenderInterpolationProbe probe;
    probe.interpolatedVisuals = scene.interpolatedVisualCount;
    probe.valid =
        scene.valid && scene.presentationInterpolated &&
        scene.previousSnapshotTickIndex == 40u &&
        scene.currentSnapshotTickIndex == 41u &&
        approximatelyEqual(scene.interpolationAlpha, 0.5) &&
        scene.snapshotFingerprint == currentFingerprint &&
        presentation.snapshotFingerprint == currentFingerprint &&
        approximatelyEqual(presentation.playerX, 200.0) &&
        approximatelyEqual(presentation.playerZ, 400.0) &&
        approximatelyEqual(presentation.playerHeadingRadians, 0.0) &&
        approximatelyEqual(std::abs(presentation.torsoYawRadians), pi) &&
        approximatelyEqual(presentation.cameraHeadingRadians, 0.0) &&
        mw::battle::battleSnapshotFingerprint(previous) == previousFingerprint &&
        mw::battle::battleSnapshotFingerprint(current) == currentFingerprint &&
        player != nullptr && enemy != nullptr &&
        approximatelyEqual(player->transform.x, 200.0) && approximatelyEqual(player->transform.y, 20.0) &&
        approximatelyEqual(player->transform.z, 400.0) &&
        approximatelyEqual(player->transform.headingRadians, 0.0) &&
        approximatelyEqual(std::abs(player->torsoYawRadians), pi) &&
        player->animationElapsedMs == 150u &&
        player->destroyedComponentIds == current.combatants[0].destroyedComponentIds &&
        approximatelyEqual(enemy->transform.x, 1200.0) && approximatelyEqual(enemy->transform.z, 1400.0) &&
        approximatelyEqual(enemy->transform.headingRadians, 0.0) &&
        approximatelyEqual(scene.camera.transform.x, 210.0) &&
        approximatelyEqual(scene.camera.transform.headingRadians, 0.0) &&
        scene.objective.damage == current.objective.damage;
    return probe;
}

CampaignCockpitMotionProbe probeCampaignCockpitMotion(
    const mw::battle::BattleStartParams& params,
    const std::filesystem::path& sortedOriginalFilesRoot) {
    const double locustHeight = mw::battle::campaignBattleCatalogCockpitCameraHeight(
        sortedOriginalFilesRoot,
        "locust");
    const double phoenixHawkHeight = mw::battle::campaignBattleCatalogCockpitCameraHeight(
        sortedOriginalFilesRoot,
        "phoenix_hawk");
    const double battleMasterHeight = mw::battle::campaignBattleCatalogCockpitCameraHeight(
        sortedOriginalFilesRoot,
        "battlemaster");

    mw::battle::BattleStartParams heightParams = params;
    heightParams.cockpitCameraHeight = locustHeight;
    const mw::battle::BattleSnapshot heightSnapshot =
        mw::battle::BattleWorld::create(heightParams).snapshot();
    expect(!heightSnapshot.combatants.empty(), "campaign cockpit height player missing");

    mw::battle::BattleSnapshot previous = heightSnapshot;
    mw::battle::BattleSnapshot current = heightSnapshot;
    previous.tickIndex = 70;
    current.tickIndex = 71;
    previous.combatants[0].activeAnimationId = "walk";
    current.combatants[0].activeAnimationId = "walk";
    previous.combatants[0].forwardSpeed = previous.combatants[0].maxForwardSpeed;
    current.combatants[0].forwardSpeed = current.combatants[0].maxForwardSpeed;
    previous.combatants[0].walkAnimationElapsedMs = 0;
    current.combatants[0].walkAnimationElapsedMs = 360;
    const uint64_t previousFingerprint = mw::battle::battleSnapshotFingerprint(previous);
    const uint64_t currentFingerprint = mw::battle::battleSnapshotFingerprint(current);

    const mw::battle::CampaignBattleRenderScenePackage cockpitScene =
        mw::battle::campaignBattleInterpolatedRenderSceneFromSnapshots(
            previous,
            current,
            0.5,
            mw::battle::CampaignBattlePresentationMode::Cockpit,
            sortedOriginalFilesRoot);
    const mw::battle::CampaignBattleRenderScenePackage externalScene =
        mw::battle::campaignBattleInterpolatedRenderSceneFromSnapshots(
            previous,
            current,
            0.5,
            mw::battle::CampaignBattlePresentationMode::External,
            sortedOriginalFilesRoot);
    const mw::battle::BattleCockpitBobTuning bobTuning =
        mw::battle::battleCockpitBobPrototypeTuning();
    const double reverseQuarterOffset = mw::battle::battleCockpitBobOffsetWorldUnits(
        -current.combatants[0].maxReverseSpeed,
        current.combatants[0].maxForwardSpeed,
        current.combatants[0].maxReverseSpeed,
        540.0,
        false);
    const double stoppedOffset = mw::battle::battleCockpitBobOffsetWorldUnits(
        0.0,
        current.combatants[0].maxForwardSpeed,
        current.combatants[0].maxReverseSpeed,
        180.0,
        false);
    const double destroyedOffset = mw::battle::battleCockpitBobOffsetWorldUnits(
        current.combatants[0].maxForwardSpeed,
        current.combatants[0].maxForwardSpeed,
        current.combatants[0].maxReverseSpeed,
        180.0,
        true);
    const auto approximatelyEqual = [](double a, double b) {
        return std::abs(a - b) < 0.000001;
    };

    CampaignCockpitMotionProbe probe;
    probe.locustHeight = static_cast<int>(std::lround(locustHeight));
    probe.phoenixHawkHeight = static_cast<int>(std::lround(phoenixHawkHeight));
    probe.battleMasterHeight = static_cast<int>(std::lround(battleMasterHeight));
    probe.valid =
        approximatelyEqual(locustHeight, 380.0) &&
        approximatelyEqual(phoenixHawkHeight, 565.0) &&
        approximatelyEqual(battleMasterHeight, 950.0) &&
        heightSnapshot.camera.valid &&
        approximatelyEqual(
            heightSnapshot.camera.transform.y - heightSnapshot.combatants[0].transform.y,
            locustHeight) &&
        cockpitScene.valid && cockpitScene.presentationInterpolated &&
        approximatelyEqual(cockpitScene.cockpitCameraBobOffsetY, 45.0) &&
        approximatelyEqual(externalScene.cockpitCameraBobOffsetY, 0.0) &&
        approximatelyEqual(reverseQuarterOffset, -45.0) &&
        approximatelyEqual(stoppedOffset, 0.0) &&
        approximatelyEqual(destroyedOffset, 0.0) &&
        approximatelyEqual(bobTuning.cycleMs, 720.0) &&
        approximatelyEqual(bobTuning.maxWorldUnits, 45.0) &&
        mw::battle::battleSnapshotFingerprint(previous) == previousFingerprint &&
        mw::battle::battleSnapshotFingerprint(current) == currentFingerprint;
    return probe;
}

CampaignPresentationProbe probeCampaignPresentationHandoff(
    const mw::battle::BattleStartParams& params,
    const std::filesystem::path& sortedOriginalFilesRoot) {
    mw::battle::BattleWorld world = mw::battle::BattleWorld::create(params);
    const mw::battle::BattleSnapshot snapshot = world.snapshot();
    const mw::battle::CampaignBattlePresentationPackage tactical =
        mw::battle::campaignBattlePresentationPackageFromSnapshot(
            snapshot,
            mw::battle::CampaignBattlePresentationMode::TacticalMap);
    const mw::battle::CampaignBattlePresentationPackage missionStatus =
        mw::battle::campaignBattlePresentationPackageFromSnapshot(
            snapshot,
            mw::battle::CampaignBattlePresentationMode::MissionStatus);
    const mw::battle::CampaignBattlePresentationPackage cockpitCommandMap =
        mw::battle::campaignBattlePresentationPackageFromSnapshot(
            snapshot,
            mw::battle::CampaignBattlePresentationMode::CockpitCommandMap);
    const mw::battle::CampaignBattlePresentationPackage cockpit =
        mw::battle::campaignBattlePresentationPackageFromSnapshot(
            snapshot,
            mw::battle::CampaignBattlePresentationMode::Cockpit);
    const mw::battle::CampaignBattlePresentationPackage external =
        mw::battle::campaignBattlePresentationPackageFromSnapshot(
            snapshot,
            mw::battle::CampaignBattlePresentationMode::External);
    const mw::battle::CampaignBattleRenderScenePackage renderScene =
        mw::battle::campaignBattleRenderSceneFromSnapshot(
            snapshot,
            mw::battle::CampaignBattlePresentationMode::External,
            sortedOriginalFilesRoot);
    mw::battle::BattleStartParams destroyedSensorParams = params;
    mw::battle::BattleCombatantLaunchState destroyedSensorLaunch;
    if (destroyedSensorParams.playerLaunchState.has_value()) {
        destroyedSensorLaunch = *destroyedSensorParams.playerLaunchState;
    } else {
        destroyedSensorLaunch.mechPresetId = destroyedSensorParams.playerMechPresetId;
        destroyedSensorLaunch.startTransform = destroyedSensorParams.playerStartTransform;
        destroyedSensorLaunch.roster = destroyedSensorParams.playerRoster;
    }
    destroyedSensorLaunch.persistentMechState.valid = true;
    destroyedSensorLaunch.persistentMechState.sensors = 3u;
    destroyedSensorParams.playerLaunchState = destroyedSensorLaunch;
    destroyedSensorParams.originalMajorSystemRuntimeEnabled = true;
    const mw::battle::BattleWorld destroyedSensorWorld =
        mw::battle::BattleWorld::create(destroyedSensorParams);
    const mw::battle::CampaignBattleRenderScenePackage destroyedSensorScene =
        mw::battle::campaignBattleRenderSceneFromSnapshot(
            destroyedSensorWorld.snapshot(),
            mw::battle::CampaignBattlePresentationMode::Cockpit,
            sortedOriginalFilesRoot);
    const bool destroyedSensorReticleSuppressed =
        destroyedSensorScene.valid && !destroyedSensorScene.cockpitCrosshairVisible;
    mw::battle::BattleSnapshot reactorShutdownSnapshot = snapshot;
    reactorShutdownSnapshot.combatants.front().heat.enabled = true;
    reactorShutdownSnapshot.combatants.front().heat.reactorShutdown = true;
    reactorShutdownSnapshot.combatants.front().heat.originalUpdateCount = 4u;
    const mw::battle::CampaignBattleRenderScenePackage reactorShutdownScene =
        mw::battle::campaignBattleRenderSceneFromSnapshot(
            reactorShutdownSnapshot,
            mw::battle::CampaignBattlePresentationMode::Cockpit,
            sortedOriginalFilesRoot);
    reactorShutdownSnapshot.combatants.front().heat.originalUpdateCount = 8u;
    const mw::battle::CampaignBattleRenderScenePackage reactorShutdownHiddenScene =
        mw::battle::campaignBattleRenderSceneFromSnapshot(
            reactorShutdownSnapshot,
            mw::battle::CampaignBattlePresentationMode::Cockpit,
            sortedOriginalFilesRoot);
    const bool reactorShutdownHandoffReady =
        reactorShutdownScene.valid &&
        reactorShutdownScene.cockpitReactorShutdown &&
        reactorShutdownScene.cockpitReactorShutdownMessageVisible &&
        reactorShutdownHiddenScene.cockpitReactorShutdown &&
        !reactorShutdownHiddenScene.cockpitReactorShutdownMessageVisible;
    mw::battle::BattleSnapshot projectilePrevious = snapshot;
    projectilePrevious.tickIndex = 10u;
    projectilePrevious.elapsedMs = 500u;
    projectilePrevious.combatants.front().mechPresetId = "rifleman";
    projectilePrevious.combatants.front().activeAnimationId = "idle";
    projectilePrevious.combatants.front().walkAnimationElapsedMs = 0u;
    projectilePrevious.combatants.front().forwardSpeed = 0.0;
    mw::battle::BattleWeaponInstanceState syntheticArmAc;
    syntheticArmAc.weaponInstanceId = 3u;
    syntheticArmAc.weaponTypeId = "ac5";
    syntheticArmAc.locationId = "RA";
    mw::battle::BattleWeaponInstanceState syntheticTorsoMissile;
    syntheticTorsoMissile.weaponInstanceId = 1u;
    syntheticTorsoMissile.weaponTypeId = "srm4";
    syntheticTorsoMissile.locationId = "LT";
    projectilePrevious.combatants.front().weapons = {
        syntheticArmAc,
        syntheticTorsoMissile,
    };
    mw::battle::BattleProjectileState acProjectile;
    acProjectile.valid = true;
    acProjectile.projectileId = 41u;
    acProjectile.launchTickIndex = 8u;
    acProjectile.shooterEntityId = snapshot.combatants.front().id;
    acProjectile.weaponInstanceId = 3u;
    acProjectile.weaponTypeId = "ac5";
    acProjectile.position = {100.0, 200.0, 300.0};
    acProjectile.previousPosition = acProjectile.position;
    acProjectile.direction = {0.0, 0.0, 1.0};
    acProjectile.originalVisualClass = 0u;
    acProjectile.targetBound = true;
    acProjectile.targetKind = mw::battle::BattleShotTargetKind::Combatant;
    acProjectile.targetEntityId = snapshot.combatants.back().id;
    acProjectile.targetLocalPoint = {0.0, 100.0, 0.0};
    acProjectile.launchForwardOffset = 45.0;
    acProjectile.pendingShot.valid = true;
    acProjectile.pendingShot.aimOrigin = {100.0, 200.0, 255.0};
    acProjectile.pendingShot.impactPoint = {100.0, 200.0, 3000.0};
    mw::battle::BattleProjectileState missileProjectile = acProjectile;
    missileProjectile.projectileId = 42u;
    missileProjectile.weaponInstanceId = 1u;
    missileProjectile.weaponTypeId = "srm4";
    missileProjectile.position = {400.0, 500.0, 600.0};
    missileProjectile.previousPosition = missileProjectile.position;
    missileProjectile.direction = {1.0, 0.0, 0.0};
    missileProjectile.originalVisualClass = 1u;
    missileProjectile.targetBound = true;
    missileProjectile.targetKind = mw::battle::BattleShotTargetKind::Combatant;
    missileProjectile.targetEntityId = snapshot.combatants.back().id;
    missileProjectile.targetLocalPoint = {0.0, 100.0, 0.0};
    missileProjectile.launchForwardOffset = 405.0;
    missileProjectile.pendingShot.aimOrigin = {-5.0, 500.0, 600.0};
    missileProjectile.pendingShot.impactPoint = {3000.0, 500.0, 600.0};
    mw::battle::BattleProjectileState missedMissileProjectile =
        missileProjectile;
    missedMissileProjectile.projectileId = 43u;
    missedMissileProjectile.targetBound = false;
    missedMissileProjectile.targetKind =
        mw::battle::BattleShotTargetKind::None;
    missedMissileProjectile.targetEntityId = {};
    missedMissileProjectile.position = {0.0, 500.0, 1600.0};
    missedMissileProjectile.previousPosition =
        missedMissileProjectile.position;
    missedMissileProjectile.direction = {0.0, 0.0, 1.0};
    missedMissileProjectile.pendingShot.aimOrigin = {0.0, 500.0, 0.0};
    missedMissileProjectile.pendingShot.aimDirection = {0.0, 0.0, 1.0};
    // This is crosshair/terrain diagnostic evidence, not a terminal impact.
    // The projectile has already travelled beyond it and must keep moving.
    missedMissileProjectile.pendingShot.impactPoint = {0.0, 500.0, 500.0};
    missedMissileProjectile.pendingShot.maximumRange = 4000.0;
    projectilePrevious.projectiles = {
        acProjectile,
        missileProjectile,
        missedMissileProjectile,
    };
    mw::battle::BattleSnapshot projectileCurrent = projectilePrevious;
    projectileCurrent.tickIndex = 11u;
    projectileCurrent.elapsedMs = 550u;
    projectileCurrent.projectiles[0].previousPosition =
        projectilePrevious.projectiles[0].position;
    projectileCurrent.projectiles[0].position = {140.0, 220.0, 360.0};
    projectileCurrent.projectiles[1].previousPosition =
        projectilePrevious.projectiles[1].position;
    projectileCurrent.projectiles[1].position = {480.0, 500.0, 600.0};
    projectileCurrent.projectiles[2].previousPosition =
        projectilePrevious.projectiles[2].position;
    projectileCurrent.projectiles[2].position = {0.0, 500.0, 1650.0};
    const uint64_t projectilePreviousFingerprint =
        mw::battle::battleSnapshotFingerprint(projectilePrevious);
    const uint64_t projectileCurrentFingerprint =
        mw::battle::battleSnapshotFingerprint(projectileCurrent);
    const mw::battle::CampaignBattleRenderScenePackage projectileScene =
        mw::battle::campaignBattleRenderSceneFromSnapshot(
            projectileCurrent,
            mw::battle::CampaignBattlePresentationMode::External,
            sortedOriginalFilesRoot);
    const mw::battle::CampaignBattleRenderScenePackage projectileQuarterScene =
        mw::battle::campaignBattleInterpolatedRenderSceneFromSnapshots(
            projectilePrevious,
            projectileCurrent,
            0.25,
            mw::battle::CampaignBattlePresentationMode::External,
            sortedOriginalFilesRoot);
    const mw::battle::CampaignBattleRenderScenePackage projectileThreeQuarterScene =
        mw::battle::campaignBattleInterpolatedRenderSceneFromSnapshots(
            projectilePrevious,
            projectileCurrent,
            0.75,
            mw::battle::CampaignBattlePresentationMode::External,
            sortedOriginalFilesRoot);
    const mw::presentation::CampaignBattleGlResourceProbe projectileGlProbe =
        mw::presentation::probeCampaignBattleGlResources(projectileScene);
    mw::battle::BattleSnapshot projectileWalkingSnapshot = projectileCurrent;
    projectileWalkingSnapshot.combatants.front().activeAnimationId = "walk";
    projectileWalkingSnapshot.combatants.front().walkAnimationElapsedMs = 540u;
    projectileWalkingSnapshot.combatants.front().forwardSpeed = 250.0;
    const mw::battle::CampaignBattleRenderScenePackage projectileWalkingScene =
        mw::battle::campaignBattleRenderSceneFromSnapshot(
            projectileWalkingSnapshot,
            mw::battle::CampaignBattlePresentationMode::External,
            sortedOriginalFilesRoot);
    const mw::presentation::CampaignBattleGlResourceProbe
        projectileWalkingGlProbe =
            mw::presentation::probeCampaignBattleGlResources(
                projectileWalkingScene);

    mw::battle::BattleSnapshot shadowHawkProjectileSnapshot =
        projectileCurrent;
    shadowHawkProjectileSnapshot.combatants.front().mechPresetId =
        "shadow_hawk";
    mw::battle::BattleWeaponInstanceState shadowHawkSrm;
    shadowHawkSrm.weaponInstanceId = 7u;
    shadowHawkSrm.weaponTypeId = "srm2";
    shadowHawkSrm.locationId = "HD";
    shadowHawkProjectileSnapshot.combatants.front().weapons = {
        shadowHawkSrm,
    };
    mw::battle::BattleProjectileState shadowHawkSrmProjectile =
        missedMissileProjectile;
    shadowHawkSrmProjectile.projectileId = 44u;
    shadowHawkSrmProjectile.weaponInstanceId = 7u;
    shadowHawkSrmProjectile.weaponTypeId = "srm2";
    shadowHawkProjectileSnapshot.projectiles = {
        shadowHawkSrmProjectile,
    };
    const mw::battle::CampaignBattleRenderScenePackage shadowHawkProjectileScene =
        mw::battle::campaignBattleRenderSceneFromSnapshot(
            shadowHawkProjectileSnapshot,
            mw::battle::CampaignBattlePresentationMode::Cockpit,
            sortedOriginalFilesRoot);
    const mw::presentation::CampaignBattleGlResourceProbe
        shadowHawkProjectileGlProbe =
            mw::presentation::probeCampaignBattleGlResources(
                shadowHawkProjectileScene);
    mw::battle::BattleSnapshot laserBeamSnapshot = snapshot;
    laserBeamSnapshot.tickIndex = 21u;
    laserBeamSnapshot.elapsedMs = 1050u;
    mw::battle::BattleWeaponInstanceState laserWeapon;
    laserWeapon.weaponInstanceId = 1u;
    laserWeapon.weaponTypeId = "medium_laser";
    laserWeapon.locationId = "CT";
    laserWeapon.deliveryMode = mw::battle::BattleWeaponDeliveryMode::Immediate;
    laserBeamSnapshot.combatants.front().weapons = {laserWeapon};
    laserBeamSnapshot.lastShot.valid = true;
    laserBeamSnapshot.lastShot.sequence = 7u;
    laserBeamSnapshot.lastShot.tickIndex = 20u;
    laserBeamSnapshot.lastShot.shooterEntityId =
        laserBeamSnapshot.combatants.front().id;
    laserBeamSnapshot.lastShot.weaponInstanceId = 1u;
    laserBeamSnapshot.lastShot.result = mw::battle::BattleShotResult::Hit;
    laserBeamSnapshot.lastShot.hit = true;
    laserBeamSnapshot.lastShot.deliveryState =
        mw::battle::BattleShotDeliveryState::Immediate;
    laserBeamSnapshot.lastShot.aimOrigin = {100.0, 380.0, 200.0};
    laserBeamSnapshot.lastShot.aimDirection = {0.0, 0.0, 1.0};
    laserBeamSnapshot.lastShot.impactPoint = {100.0, 420.0, 1500.0};
    laserBeamSnapshot.lastShot.maximumRange = 4800.0;
    laserBeamSnapshot.shotEvents = {laserBeamSnapshot.lastShot};
    const uint64_t laserBeamFingerprint =
        mw::battle::battleSnapshotFingerprint(laserBeamSnapshot);
    const mw::battle::CampaignBattleRenderScenePackage laserBeamScene =
        mw::battle::campaignBattleRenderSceneFromSnapshot(
            laserBeamSnapshot,
            mw::battle::CampaignBattlePresentationMode::Cockpit,
            sortedOriginalFilesRoot);
    mw::battle::CampaignBattleRenderScenePackage bobbedLaserBeamScene =
        laserBeamScene;
    bobbedLaserBeamScene.cockpitCameraBobOffsetY = 45.0;
    const mw::presentation::CampaignBattleGlResourceProbe laserBeamGlProbe =
        mw::presentation::probeCampaignBattleGlResources(bobbedLaserBeamScene);
    mw::battle::BattleSnapshot ppcBeamSnapshot = laserBeamSnapshot;
    ppcBeamSnapshot.combatants.front().weapons.front().weaponTypeId = "ppc";
    ppcBeamSnapshot.combatants.front().weapons.front().locationId = "RA";
    const uint64_t ppcBeamFingerprint =
        mw::battle::battleSnapshotFingerprint(ppcBeamSnapshot);
    const mw::battle::CampaignBattleRenderScenePackage ppcBeamScene =
        mw::battle::campaignBattleRenderSceneFromSnapshot(
            ppcBeamSnapshot,
            mw::battle::CampaignBattlePresentationMode::Cockpit,
            sortedOriginalFilesRoot);
    mw::battle::BattleSnapshot multiBeamSnapshot = laserBeamSnapshot;
    mw::battle::BattleWeaponInstanceState secondBeamWeapon = laserWeapon;
    secondBeamWeapon.weaponInstanceId = 2u;
    secondBeamWeapon.weaponTypeId = "ppc";
    secondBeamWeapon.locationId = "RA";
    multiBeamSnapshot.combatants.front().weapons.push_back(secondBeamWeapon);
    mw::battle::BattleShotDiagnostic secondBeamShot =
        multiBeamSnapshot.lastShot;
    secondBeamShot.sequence = 8u;
    secondBeamShot.weaponInstanceId = 2u;
    secondBeamShot.impactPoint.x += 80.0;
    multiBeamSnapshot.shotEvents.push_back(secondBeamShot);
    const uint64_t multiBeamFingerprint =
        mw::battle::battleSnapshotFingerprint(multiBeamSnapshot);
    const mw::battle::CampaignBattleRenderScenePackage multiBeamScene =
        mw::battle::campaignBattleRenderSceneFromSnapshot(
            multiBeamSnapshot,
            mw::battle::CampaignBattlePresentationMode::Cockpit,
            sortedOriginalFilesRoot);
    mw::battle::BattleSnapshot machineGunFlashSnapshot = laserBeamSnapshot;
    machineGunFlashSnapshot.combatants.front().weapons.front().weaponTypeId =
        "machine_gun";
    machineGunFlashSnapshot.combatants.front().weapons.front().locationId =
        "RA";
    const uint64_t machineGunFlashFingerprint =
        mw::battle::battleSnapshotFingerprint(machineGunFlashSnapshot);
    const mw::battle::CampaignBattleRenderScenePackage machineGunFlashScene =
        mw::battle::campaignBattleRenderSceneFromSnapshot(
            machineGunFlashSnapshot,
            mw::battle::CampaignBattlePresentationMode::Cockpit,
            sortedOriginalFilesRoot);
    mw::battle::BattleSnapshot expiredMachineGunFlashSnapshot =
        machineGunFlashSnapshot;
    expiredMachineGunFlashSnapshot.tickIndex = 22u;
    const mw::battle::CampaignBattleRenderScenePackage
        expiredMachineGunFlashScene =
            mw::battle::campaignBattleRenderSceneFromSnapshot(
                expiredMachineGunFlashSnapshot,
                mw::battle::CampaignBattlePresentationMode::Cockpit,
                sortedOriginalFilesRoot);
    mw::battle::BattleSnapshot nonPlayerMachineGunFlashSnapshot =
        machineGunFlashSnapshot;
    nonPlayerMachineGunFlashSnapshot.combatants.front().playerControlled = false;
    const mw::battle::CampaignBattleRenderScenePackage
        nonPlayerMachineGunFlashScene =
            mw::battle::campaignBattleRenderSceneFromSnapshot(
                nonPlayerMachineGunFlashSnapshot,
                mw::battle::CampaignBattlePresentationMode::Cockpit,
                sortedOriginalFilesRoot);
    mw::battle::BattleSnapshot machineGunImpactFirstSnapshot =
        machineGunFlashSnapshot;
    mw::battle::BattleImpactEvent machineGunImpactEvent;
    machineGunImpactEvent.valid = true;
    machineGunImpactEvent.shotSequence =
        machineGunImpactFirstSnapshot.lastShot.sequence;
    machineGunImpactEvent.impactTickIndex = 20u;
    machineGunImpactEvent.shooterEntityId =
        machineGunImpactFirstSnapshot.combatants.front().id;
    machineGunImpactEvent.weaponInstanceId = 1u;
    machineGunImpactEvent.targetKind =
        mw::battle::BattleShotTargetKind::Combatant;
    machineGunImpactEvent.targetEntityId =
        machineGunImpactFirstSnapshot.combatants.back().id;
    machineGunImpactEvent.deliveryState =
        mw::battle::BattleShotDeliveryState::Immediate;
    machineGunImpactEvent.visualSequence =
        mw::battle::BattleImpactVisualSequence::
            OriginalMachineGunTwoStage;
    machineGunImpactEvent.point =
        machineGunImpactFirstSnapshot.lastShot.impactPoint;
    machineGunImpactFirstSnapshot.impactEvents = {machineGunImpactEvent};
    const uint64_t machineGunImpactFingerprint =
        mw::battle::battleSnapshotFingerprint(machineGunImpactFirstSnapshot);
    const mw::battle::CampaignBattleRenderScenePackage machineGunImpactFirstScene =
        mw::battle::campaignBattleRenderSceneFromSnapshot(
            machineGunImpactFirstSnapshot,
            mw::battle::CampaignBattlePresentationMode::Cockpit,
            sortedOriginalFilesRoot);
    const mw::presentation::CampaignBattleGlResourceProbe
        machineGunImpactFirstGlProbe =
            mw::presentation::probeCampaignBattleGlResources(
                machineGunImpactFirstScene);
    mw::battle::BattleSnapshot machineGunImpactSecondSnapshot =
        machineGunImpactFirstSnapshot;
    machineGunImpactSecondSnapshot.tickIndex = 23u;
    const mw::battle::CampaignBattleRenderScenePackage machineGunImpactSecondScene =
        mw::battle::campaignBattleRenderSceneFromSnapshot(
            machineGunImpactSecondSnapshot,
            mw::battle::CampaignBattlePresentationMode::Cockpit,
            sortedOriginalFilesRoot);
    const mw::presentation::CampaignBattleGlResourceProbe
        machineGunImpactSecondGlProbe =
            mw::presentation::probeCampaignBattleGlResources(
                machineGunImpactSecondScene);
    mw::battle::BattleSnapshot machineGunImpactExpiredSnapshot =
        machineGunImpactSecondSnapshot;
    machineGunImpactExpiredSnapshot.tickIndex = 25u;
    const mw::battle::CampaignBattleRenderScenePackage machineGunImpactExpiredScene =
        mw::battle::campaignBattleRenderSceneFromSnapshot(
            machineGunImpactExpiredSnapshot,
            mw::battle::CampaignBattlePresentationMode::Cockpit,
            sortedOriginalFilesRoot);
    mw::battle::BattleSnapshot secondTickBeamSnapshot = laserBeamSnapshot;
    secondTickBeamSnapshot.tickIndex = 22u;
    secondTickBeamSnapshot.elapsedMs = 1100u;
    const mw::battle::CampaignBattleRenderScenePackage secondTickBeamScene =
        mw::battle::campaignBattleRenderSceneFromSnapshot(
            secondTickBeamSnapshot,
            mw::battle::CampaignBattlePresentationMode::Cockpit,
            sortedOriginalFilesRoot);
    mw::battle::BattleSnapshot expiredBeamSnapshot = laserBeamSnapshot;
    expiredBeamSnapshot.tickIndex = 23u;
    expiredBeamSnapshot.elapsedMs = 1150u;
    const mw::battle::CampaignBattleRenderScenePackage expiredBeamScene =
        mw::battle::campaignBattleRenderSceneFromSnapshot(
            expiredBeamSnapshot,
            mw::battle::CampaignBattlePresentationMode::Cockpit,
            sortedOriginalFilesRoot);
    mw::battle::BattleSnapshot beamOverlapSnapshot = laserBeamSnapshot;
    beamOverlapSnapshot.tickIndex = 22u;
    beamOverlapSnapshot.elapsedMs = 1100u;
    mw::battle::BattleWeaponInstanceState overlapMachineGun = laserWeapon;
    overlapMachineGun.weaponInstanceId = 3u;
    overlapMachineGun.weaponTypeId = "machine_gun";
    overlapMachineGun.locationId = "RA";
    beamOverlapSnapshot.combatants.front().weapons.push_back(overlapMachineGun);
    mw::battle::BattleShotDiagnostic overlapMachineGunShot =
        beamOverlapSnapshot.lastShot;
    overlapMachineGunShot.sequence = 9u;
    overlapMachineGunShot.tickIndex = 21u;
    overlapMachineGunShot.weaponInstanceId = 3u;
    beamOverlapSnapshot.lastShot = overlapMachineGunShot;
    beamOverlapSnapshot.shotEvents = {overlapMachineGunShot};
    const uint64_t beamOverlapFingerprint =
        mw::battle::battleSnapshotFingerprint(beamOverlapSnapshot);
    const mw::battle::CampaignBattleRenderScenePackage beamOverlapScene =
        mw::battle::campaignBattleInterpolatedRenderSceneFromSnapshots(
            laserBeamSnapshot,
            beamOverlapSnapshot,
            0.5,
            mw::battle::CampaignBattlePresentationMode::Cockpit,
            sortedOriginalFilesRoot);
    mw::battle::BattleSnapshot impactSnapshot = laserBeamSnapshot;
    impactSnapshot.tickIndex = 100u;
    impactSnapshot.elapsedMs = 5000u;
    impactSnapshot.impactEvents.clear();
    for (uint64_t stage = 0; stage < 6u; ++stage) {
        mw::battle::BattleImpactEvent event;
        event.valid = true;
        event.shotSequence = 100u + stage;
        event.impactTickIndex = 99u - stage * 2u;
        event.shooterEntityId = impactSnapshot.combatants.front().id;
        event.weaponInstanceId = 1u;
        event.targetKind = mw::battle::BattleShotTargetKind::Combatant;
        event.targetEntityId = impactSnapshot.combatants.back().id;
        event.deliveryState = stage == 0u
            ? mw::battle::BattleShotDeliveryState::ProjectileImpact
            : mw::battle::BattleShotDeliveryState::Immediate;
        event.point = {
            100.0 + static_cast<double>(stage) * 20.0,
            420.0,
            1500.0,
        };
        impactSnapshot.impactEvents.push_back(event);
    }
    const uint64_t impactFingerprint =
        mw::battle::battleSnapshotFingerprint(impactSnapshot);
    const mw::battle::CampaignBattleRenderScenePackage impactScene =
        mw::battle::campaignBattleRenderSceneFromSnapshot(
            impactSnapshot,
            mw::battle::CampaignBattlePresentationMode::External,
            sortedOriginalFilesRoot);
    const mw::presentation::CampaignBattleGlResourceProbe impactGlProbe =
        mw::presentation::probeCampaignBattleGlResources(impactScene);
    mw::battle::BattleSnapshot terrainSmokeSnapshot = impactSnapshot;
    terrainSmokeSnapshot.impactEvents.clear();
    for (uint64_t stage = 0; stage < 3u; ++stage) {
        mw::battle::BattleImpactEvent event;
        event.valid = true;
        event.shotSequence = 200u + stage;
        event.impactTickIndex = 99u - stage * 2u;
        event.shooterEntityId = terrainSmokeSnapshot.combatants.front().id;
        event.weaponInstanceId = 1u;
        event.targetKind = mw::battle::BattleShotTargetKind::Terrain;
        event.deliveryState =
            mw::battle::BattleShotDeliveryState::ProjectileTerrainImpact;
        event.visualSequence =
            mw::battle::BattleImpactVisualSequence::TerrainSmokeLastThree;
        event.point = {
            300.0 + static_cast<double>(stage) * 20.0,
            0.0,
            1500.0,
        };
        terrainSmokeSnapshot.impactEvents.push_back(event);
    }
    const mw::battle::CampaignBattleRenderScenePackage terrainSmokeScene =
        mw::battle::campaignBattleRenderSceneFromSnapshot(
            terrainSmokeSnapshot,
            mw::battle::CampaignBattlePresentationMode::External,
            sortedOriginalFilesRoot);
    const mw::presentation::CampaignBattleGlResourceProbe terrainSmokeGlProbe =
        mw::presentation::probeCampaignBattleGlResources(terrainSmokeScene);
    mw::battle::BattleSnapshot farImpactSnapshot = impactSnapshot;
    for (mw::battle::BattleImpactEvent& event : farImpactSnapshot.impactEvents) {
        event.point.z += 8000.0;
    }
    const mw::battle::CampaignBattleRenderScenePackage farImpactScene =
        mw::battle::campaignBattleRenderSceneFromSnapshot(
            farImpactSnapshot,
            mw::battle::CampaignBattlePresentationMode::External,
            sortedOriginalFilesRoot);
    const mw::presentation::CampaignBattleGlResourceProbe farImpactGlProbe =
        mw::presentation::probeCampaignBattleGlResources(farImpactScene);
    mw::battle::BattleSnapshot expiredImpactSnapshot = impactSnapshot;
    expiredImpactSnapshot.tickIndex = 112u;
    const mw::battle::CampaignBattleRenderScenePackage expiredImpactScene =
        mw::battle::campaignBattleRenderSceneFromSnapshot(
            expiredImpactSnapshot,
            mw::battle::CampaignBattlePresentationMode::External,
            sortedOriginalFilesRoot);
    const mw::battle::CampaignBattleRenderScenePackage missionStatusScene =
        mw::battle::campaignBattleRenderSceneFromSnapshot(
            snapshot,
            mw::battle::CampaignBattlePresentationMode::MissionStatus,
            sortedOriginalFilesRoot);
    const mw::battle::CampaignBattleRenderScenePackage cockpitCommandMapScene =
        mw::battle::campaignBattleRenderSceneFromSnapshot(
            snapshot,
            mw::battle::CampaignBattlePresentationMode::CockpitCommandMap,
            sortedOriginalFilesRoot);
    mw::battle::BattleSnapshot scannedSnapshot = snapshot;
    scannedSnapshot.targetScan.selectedTargetKind =
        mw::battle::BattleTargetScanTargetKind::Combatant;
    scannedSnapshot.targetScan.selectedTargetEntityId =
        snapshot.combatants.back().id;
    const mw::battle::CampaignBattleRenderScenePackage scannedCockpitScene =
        mw::battle::campaignBattleRenderSceneFromSnapshot(
            scannedSnapshot,
            mw::battle::CampaignBattlePresentationMode::Cockpit,
            sortedOriginalFilesRoot);
    mw::battle::BattleSnapshot scannedObjectiveSnapshot = snapshot;
    scannedObjectiveSnapshot.targetScan.selectedTargetKind =
        mw::battle::BattleTargetScanTargetKind::Objective;
    scannedObjectiveSnapshot.targetScan.selectedTargetEntityId = {};
    const mw::battle::CampaignBattleRenderScenePackage scannedObjectiveCockpitScene =
        mw::battle::campaignBattleRenderSceneFromSnapshot(
            scannedObjectiveSnapshot,
            mw::battle::CampaignBattlePresentationMode::Cockpit,
            sortedOriginalFilesRoot);
    mw::battle::BattleSnapshot protectedObjectiveSnapshot =
        scannedObjectiveSnapshot;
    protectedObjectiveSnapshot.objective.missionIntent =
        mw::battle::BattleMissionObjectiveIntent::Protect;
    protectedObjectiveSnapshot.objective.missionIntentProven = true;
    protectedObjectiveSnapshot.objective.missionIntentBoundToObjective = true;
    protectedObjectiveSnapshot.objective.missionSemanticsProven = true;
    protectedObjectiveSnapshot.objective.missionTargetKind = "water_factory";
    protectedObjectiveSnapshot.objective.missionIntentProvenance =
        "phase12_target_scan_smoke";
    const mw::battle::CampaignBattleRenderScenePackage protectedObjectiveCockpitScene =
        mw::battle::campaignBattleRenderSceneFromSnapshot(
            protectedObjectiveSnapshot,
            mw::battle::CampaignBattlePresentationMode::Cockpit,
            sortedOriginalFilesRoot);
    mw::battle::BattleSnapshot desertSnapshot = snapshot;
    desertSnapshot.terrain.environmentId = 0;
    mw::battle::BattleSnapshot snowSnapshot = snapshot;
    snowSnapshot.terrain.environmentId = 2;
    const mw::battle::CampaignBattleRenderScenePackage desertScene =
        mw::battle::campaignBattleRenderSceneFromSnapshot(
            desertSnapshot,
            mw::battle::CampaignBattlePresentationMode::External,
            sortedOriginalFilesRoot);
    const mw::battle::CampaignBattleRenderScenePackage snowScene =
        mw::battle::campaignBattleRenderSceneFromSnapshot(
            snowSnapshot,
            mw::battle::CampaignBattlePresentationMode::External,
            sortedOriginalFilesRoot);
    CampaignPresentationProbe probe;
    probe.destroyedSensorReticleSuppressed = destroyedSensorReticleSuppressed;
    probe.reactorShutdownHandoffReady = reactorShutdownHandoffReady;
    probe.mechHitProfilesReady = true;
    for (const mw::mech3d::MechCatalogEntry& entry :
         mw::mech3d::mechCatalogEntries()) {
        const mw::battle::BattleMechHitProfile hitProfile =
            mw::battle::campaignBattleCatalogHitProfile(
                sortedOriginalFilesRoot, entry.presetId);
        std::array<bool, 5> directSections{};
        bool torsoComposite = false;
        for (const mw::battle::BattleMechHitTriangle& triangle :
             hitProfile.triangles) {
            torsoComposite = torsoComposite || triangle.torsoComposite;
            if (triangle.torsoComposite) {
                continue;
            }
            switch (triangle.directArmorSection) {
            case mw::mech3d::MechArmorSectionId::LeftArm:
                directSections[0] = true;
                break;
            case mw::mech3d::MechArmorSectionId::RightArm:
                directSections[1] = true;
                break;
            case mw::mech3d::MechArmorSectionId::LeftLeg:
                directSections[2] = true;
                break;
            case mw::mech3d::MechArmorSectionId::RightLeg:
                directSections[3] = true;
                break;
            case mw::mech3d::MechArmorSectionId::Head:
                directSections[4] = true;
                break;
            default:
                break;
            }
        }
        probe.mechHitProfilesReady =
            probe.mechHitProfilesReady && hitProfile.valid &&
            hitProfile.geometryFingerprint != 0u &&
            hitProfile.aimOriginHeight > 0.0 &&
            hitProfile.cockpitViewportHeight > 0 && torsoComposite &&
            std::all_of(
                directSections.begin(), directSections.end(),
                [](bool present) { return present; });
        probe.mechHitProfileTriangles += hitProfile.triangles.size();
    }
    probe.combatants = tactical.combatantCount;
    probe.tacticalMapReady = tactical.valid && tactical.objectiveValid;
    probe.cockpitReady = cockpit.valid && cockpit.cameraValid;
    probe.externalReady = external.valid && external.opposingCombatantCount > 0u;
    probe.snapshotOwned = missionStatus.snapshotOwned &&
                          cockpitCommandMap.snapshotOwned &&
                          tactical.snapshotOwned &&
                          cockpit.snapshotOwned &&
                          external.snapshotOwned &&
                          tactical.snapshotFingerprint == cockpit.snapshotFingerprint &&
                          cockpit.snapshotFingerprint == external.snapshotFingerprint;
    probe.renderSceneReady =
        renderScene.valid &&
        renderScene.snapshotOwned &&
        renderScene.snapshotFingerprint == tactical.snapshotFingerprint;
    probe.renderVisuals = renderScene.combatantVisuals.size();
    probe.projectileVisuals = projectileScene.projectileVisuals.size();
    probe.projectileVertices = projectileGlProbe.projectileVertexCount;
    probe.projectileTriangles = projectileGlProbe.projectileTriangleCount;
    probe.projectileVisualsReady =
        projectileScene.valid && projectileScene.snapshotOwned &&
        projectileScene.snapshotFingerprint == projectileCurrentFingerprint &&
        projectileScene.projectileVisuals.size() == 3u &&
        projectileScene.projectileVisuals[0].recordIndex == 0 &&
        projectileScene.projectileVisuals[1].recordIndex == 1 &&
        projectileScene.projectileVisuals[0].locationId == "RA" &&
        projectileScene.projectileVisuals[1].locationId == "LT" &&
        projectileScene.projectileVisuals[0].launchAimOriginValid &&
        projectileScene.projectileVisuals[1].launchAimOriginValid &&
        projectileScene.projectileVisuals[0].visualTargetPointValid &&
        projectileScene.projectileVisuals[1].visualTargetPointValid &&
        projectileScene.projectileVisuals[2].visualTargetPointValid &&
        !projectileScene.projectileVisuals[2].targetBound &&
        std::abs(
            projectileScene.projectileVisuals[0].visualTargetPoint.z -
            acProjectile.pendingShot.impactPoint.z) > 0.001 &&
        std::abs(
            projectileScene.projectileVisuals[1].visualTargetPoint.z -
            missileProjectile.pendingShot.impactPoint.z) > 0.001 &&
        std::abs(
            projectileScene.projectileVisuals[2].visualTargetPoint.z -
            missedMissileProjectile.pendingShot.impactPoint.z) > 0.001 &&
        std::abs(
            projectileScene.projectileVisuals[2].visualTargetPoint.z -
            4000.0) < 0.001 &&
        !projectileScene.projectileVisuals[0].modelMountPolicyProven &&
        !projectileScene.projectileVisuals[1].modelMountPolicyProven &&
        projectileScene.projectileVisuals[0].originalResourceMappingProven &&
        projectileScene.projectileVisuals[1].originalResourceMappingProven &&
        !projectileScene.projectileVisuals[0].spinTimingProven &&
        !projectileScene.projectileVisuals[1].spinTimingProven &&
        projectileScene.projectileVisuals[0].modelResourcePath.filename() ==
            "OTHPCK.TBL" &&
        std::filesystem::exists(
            projectileScene.projectileVisuals[0].modelResourcePath) &&
        projectileGlProbe.valid && projectileGlProbe.projectileRecord0Loaded &&
        projectileGlProbe.projectileRecord1Loaded &&
        projectileGlProbe.projectileVisualCount == 3u &&
        projectileGlProbe.projectileWeaponMountCount == 3u &&
        projectileGlProbe.projectileMountOffsetCount == 3u &&
        projectileGlProbe.projectileStraightMountPathCount == 3u &&
        projectileGlProbe.projectileUnboundAuthoritativePathCount == 1u &&
        projectileGlProbe.projectileMountedPoseFingerprint != 0u &&
        projectileWalkingScene.valid && projectileWalkingGlProbe.valid &&
        projectileWalkingGlProbe.projectileMountedPoseFingerprint ==
            projectileGlProbe.projectileMountedPoseFingerprint &&
        shadowHawkProjectileScene.valid && shadowHawkProjectileGlProbe.valid &&
        shadowHawkProjectileScene.projectileVisuals.size() == 1u &&
        shadowHawkProjectileScene.projectileVisuals.front().locationId ==
            "HD" &&
        shadowHawkProjectileGlProbe.projectileVisualCount == 1u &&
        shadowHawkProjectileGlProbe.projectileRecord1Loaded &&
        shadowHawkProjectileGlProbe.projectileWeaponMountCount == 1u &&
        shadowHawkProjectileGlProbe.projectileMountOffsetCount == 1u &&
        shadowHawkProjectileGlProbe.projectileUnboundAuthoritativePathCount ==
            1u &&
        projectileGlProbe.projectileVertexCount == 528u &&
        projectileGlProbe.projectileTriangleCount == 134u;
    probe.projectileInterpolationReady =
        projectileQuarterScene.valid &&
        projectileThreeQuarterScene.valid &&
        projectileQuarterScene.presentationInterpolated &&
        projectileThreeQuarterScene.presentationInterpolated &&
        projectileQuarterScene.interpolatedProjectileCount == 3u &&
        projectileThreeQuarterScene.interpolatedProjectileCount == 3u &&
        std::abs(projectileQuarterScene.projectileVisuals[0].position.x -
                 110.0) < 0.001 &&
        std::abs(projectileThreeQuarterScene.projectileVisuals[0].position.x -
                 130.0) < 0.001 &&
        std::abs(projectileQuarterScene.projectileVisuals[0].spinDegrees +
                 50.625) < 0.001 &&
        std::abs(projectileThreeQuarterScene.projectileVisuals[0].spinDegrees +
                 61.875) < 0.001 &&
        mw::battle::battleSnapshotFingerprint(projectilePrevious) ==
            projectilePreviousFingerprint &&
        mw::battle::battleSnapshotFingerprint(projectileCurrent) ==
            projectileCurrentFingerprint;
    probe.immediateBeamVisualsReady =
        laserBeamScene.valid && ppcBeamScene.valid && multiBeamScene.valid &&
        secondTickBeamScene.valid && expiredBeamScene.valid &&
        beamOverlapScene.valid &&
        laserBeamGlProbe.valid &&
        laserBeamGlProbe.beamWeaponMountCount == 1u &&
        laserBeamGlProbe.playerCockpitBeamCount == 1u &&
        laserBeamGlProbe.playerBeamNearClipCorrectionCount == 1u &&
        laserBeamGlProbe.playerBeamStartsBeyondNearClip &&
        laserBeamGlProbe.playerBeamCockpitBobApplied &&
        laserBeamScene.beamVisuals.size() == 1u &&
        ppcBeamScene.beamVisuals.size() == 1u &&
        secondTickBeamScene.beamVisuals.size() == 1u &&
        expiredBeamScene.beamVisuals.empty() &&
        beamOverlapScene.beamVisuals.size() == 1u &&
        beamOverlapScene.machineGunFlashVisuals.size() == 1u &&
        laserBeamScene.beamVisuals.front().weaponTypeId == "medium_laser" &&
        laserBeamScene.beamVisuals.front().color ==
            mw::battle::CampaignBattleBeamColor::LaserYellow &&
        laserBeamScene.beamVisuals.front().originalTopologyProven &&
        !laserBeamScene.beamVisuals.front().widthScaleProven &&
        !laserBeamScene.beamVisuals.front().originalOneTickLifetimeProven &&
        laserBeamScene.beamVisuals.front().originalColorClassProven &&
        !laserBeamScene.beamVisuals.front().mountOffsetProven &&
        std::abs(laserBeamScene.beamVisuals.front().halfWidth - 21.0) < 0.001 &&
        std::abs(laserBeamScene.beamVisuals.front().start.y - 366.0) < 0.001 &&
        ppcBeamScene.beamVisuals.front().weaponTypeId == "ppc" &&
        ppcBeamScene.beamVisuals.front().locationId == "RA" &&
        ppcBeamScene.beamVisuals.front().color ==
            mw::battle::CampaignBattleBeamColor::PpcCyan &&
        multiBeamScene.beamVisuals.size() == 2u &&
        multiBeamScene.beamVisuals[0].shotSequence == 7u &&
        multiBeamScene.beamVisuals[1].shotSequence == 8u &&
        multiBeamScene.beamVisuals[1].color ==
            mw::battle::CampaignBattleBeamColor::PpcCyan &&
        std::abs(
            std::hypot(
                ppcBeamScene.beamVisuals.front().start.x -
                    laserBeamScene.beamVisuals.front().start.x,
                ppcBeamScene.beamVisuals.front().start.z -
                    laserBeamScene.beamVisuals.front().start.z) -
            70.0) < 0.001 &&
        mw::battle::battleSnapshotFingerprint(laserBeamSnapshot) ==
            laserBeamFingerprint &&
        mw::battle::battleSnapshotFingerprint(ppcBeamSnapshot) ==
            ppcBeamFingerprint &&
        mw::battle::battleSnapshotFingerprint(multiBeamSnapshot) ==
            multiBeamFingerprint &&
        mw::battle::battleSnapshotFingerprint(beamOverlapSnapshot) ==
            beamOverlapFingerprint;
    probe.machineGunFlashVisualsReady =
        machineGunFlashScene.valid && expiredMachineGunFlashScene.valid &&
        nonPlayerMachineGunFlashScene.valid &&
        machineGunFlashScene.machineGunFlashVisuals.size() == 1u &&
        machineGunFlashScene.machineGunFlashVisuals.front().spriteIndex == 9 &&
        machineGunFlashScene.machineGunFlashVisuals.front().rightSide &&
        machineGunFlashScene.machineGunFlashVisuals.front().
            originalResourceMappingProven &&
        !machineGunFlashScene.machineGunFlashVisuals.front().
            originalPlacementProven &&
        !machineGunFlashScene.machineGunFlashVisuals.front().
            originalOneTickLifetimeProven &&
        expiredMachineGunFlashScene.machineGunFlashVisuals.empty() &&
        nonPlayerMachineGunFlashScene.machineGunFlashVisuals.empty() &&
        mw::battle::campaignBattleMachineGunFlashSpriteIndex(
            mw::battle::CampaignCockpitFamily::Light, "LA") == 6 &&
        mw::battle::campaignBattleMachineGunFlashSpriteIndex(
            mw::battle::CampaignCockpitFamily::Medium, "LT") == 7 &&
        mw::battle::campaignBattleMachineGunFlashSpriteIndex(
            mw::battle::CampaignCockpitFamily::Heavy, "LA") == 8 &&
        mw::battle::campaignBattleMachineGunFlashSpriteIndex(
            mw::battle::CampaignCockpitFamily::Light, "RA") == 9 &&
        mw::battle::campaignBattleMachineGunFlashSpriteIndex(
            mw::battle::CampaignCockpitFamily::Medium, "RT") == 10 &&
        mw::battle::campaignBattleMachineGunFlashSpriteIndex(
            mw::battle::CampaignCockpitFamily::Heavy, "RA") == 11 &&
        mw::battle::campaignBattleMachineGunFlashSpriteIndex(
            mw::battle::CampaignCockpitFamily::Heavy, "CT") == -1 &&
        mw::battle::battleSnapshotFingerprint(machineGunFlashSnapshot) ==
            machineGunFlashFingerprint;
    probe.machineGunImpactVisualsReady =
        machineGunImpactFirstScene.valid &&
        machineGunImpactSecondScene.valid &&
        machineGunImpactExpiredScene.valid &&
        machineGunImpactFirstGlProbe.valid &&
        machineGunImpactSecondGlProbe.valid &&
        machineGunImpactFirstScene.impactVisuals.size() == 1u &&
        machineGunImpactSecondScene.impactVisuals.size() == 1u &&
        machineGunImpactExpiredScene.impactVisuals.empty() &&
        machineGunImpactFirstScene.impactVisuals.front().recordIndex == 8 &&
        machineGunImpactSecondScene.impactVisuals.front().recordIndex == 9 &&
        machineGunImpactFirstScene.impactVisuals.front().machineGunSequence &&
        machineGunImpactFirstScene.impactVisuals.front().
            originalResourceMappingProven &&
        machineGunImpactFirstScene.impactVisuals.front().
            originalMachineGunTwoUpdateSequenceProven &&
        !machineGunImpactFirstScene.impactVisuals.front().stageDurationProven &&
        !machineGunImpactFirstScene.impactVisuals.front().
            originalSixUpdateSequenceProven &&
        machineGunImpactFirstGlProbe.machineGunImpactLinePrimitiveCount == 5u &&
        machineGunImpactFirstGlProbe.machineGunImpactTrianglePrimitiveCount == 0u &&
        machineGunImpactFirstGlProbe.machineGunImpactRecordMask == 0x01u &&
        machineGunImpactSecondGlProbe.machineGunImpactLinePrimitiveCount == 0u &&
        machineGunImpactSecondGlProbe.machineGunImpactTrianglePrimitiveCount == 5u &&
        machineGunImpactSecondGlProbe.machineGunImpactRecordMask == 0x02u &&
        mw::battle::battleSnapshotFingerprint(machineGunImpactFirstSnapshot) ==
            machineGunImpactFingerprint;
    probe.impactVisualsReady =
        impactScene.valid && impactGlProbe.valid && farImpactScene.valid &&
        farImpactGlProbe.valid && expiredImpactScene.valid &&
        impactScene.impactVisuals.size() == 6u &&
        impactGlProbe.impactVisualCount == 6u &&
        impactGlProbe.impactSpherePrimitiveCount == 16u &&
        impactGlProbe.impactRecordMask == 0x3fu &&
        impactGlProbe.impactFlatEgaColorMask == 0xd100u &&
        impactGlProbe.impactStageColorsFlat &&
        std::abs(impactGlProbe.impactMaximumWorldRadius - 140.0f) < 0.001f &&
        std::abs(
            farImpactGlProbe.impactMaximumWorldRadius -
            impactGlProbe.impactMaximumWorldRadius) < 0.001f &&
        std::abs(
            farImpactScene.impactVisuals.front().position.z -
            impactScene.impactVisuals.front().position.z - 8000.0) < 0.001 &&
        impactScene.impactVisuals.front().recordIndex == 2 &&
        impactScene.impactVisuals.back().recordIndex == 7 &&
        impactScene.impactVisuals.front().originalResourceMappingProven &&
        impactScene.impactVisuals.front().originalSixUpdateSequenceProven &&
        !impactScene.impactVisuals.front().stageDurationProven &&
        !impactScene.impactVisuals.front().sphereRasterizationProven &&
        terrainSmokeScene.impactVisuals.size() == 3u &&
        terrainSmokeScene.impactVisuals.front().recordIndex == 5 &&
        terrainSmokeScene.impactVisuals.back().recordIndex == 7 &&
        terrainSmokeScene.impactVisuals.front().terrainSmokeSequence &&
        !terrainSmokeScene.impactVisuals.front().machineGunSequence &&
        !terrainSmokeScene.impactVisuals.front().
            originalSixUpdateSequenceProven &&
        terrainSmokeGlProbe.valid &&
        terrainSmokeGlProbe.impactVisualCount == 3u &&
        terrainSmokeGlProbe.impactRecordMask == 0x38u &&
        expiredImpactScene.impactVisuals.empty() &&
        projectileScene.impactVisuals.empty() &&
        mw::battle::battleSnapshotFingerprint(impactSnapshot) ==
            impactFingerprint;
    probe.terrainTiles = renderScene.terrain.gridPaths.size();
    probe.objectiveSeparate =
        renderScene.objective.valid &&
        renderScene.objective.staticModel.valid &&
        renderScene.objective.modelResourcePath.filename() == "OTHPCK.TBL" &&
        renderScene.terrain.palettePath.filename() == "TROPIC.PAL" &&
        renderScene.combatantVisuals.size() == snapshot.combatants.size();
    probe.cockpitResolved =
        renderScene.cockpit.valid &&
        renderScene.cockpit.family == mw::battle::CampaignCockpitFamily::Light &&
        renderScene.cockpit.backdropPath.filename() == "LIGHT.SCR" &&
        renderScene.cockpit.palettePath.filename() == "TROPIC.PAL" &&
        renderScene.cockpit.smallMechsPath.filename() == "SM_MECHS.BMP";
    probe.renderResourcesPresent =
        std::filesystem::exists(renderScene.terrain.terrainShapePath) &&
        std::filesystem::exists(renderScene.terrain.palettePath) &&
        std::filesystem::exists(renderScene.cockpit.backdropPath) &&
        std::filesystem::exists(renderScene.cockpit.palettePath) &&
        std::filesystem::exists(renderScene.cockpit.smallMechsPath) &&
        std::filesystem::exists(renderScene.objective.modelResourcePath);
    for (const mw::battle::CampaignBattleRenderVisualInstance& visual : renderScene.combatantVisuals) {
        probe.renderResourcesPresent =
            probe.renderResourcesPresent && std::filesystem::exists(visual.modelResourcePath);
    }
    for (const std::filesystem::path& path : renderScene.terrain.gridPaths) {
        probe.renderResourcesPresent = probe.renderResourcesPresent && std::filesystem::exists(path);
    }
    for (const std::filesystem::path& path : renderScene.terrain.worldPaths) {
        probe.renderResourcesPresent = probe.renderResourcesPresent && std::filesystem::exists(path);
    }
    const mw::presentation::CampaignBattleGlResourceProbe glProbe =
        mw::presentation::probeCampaignBattleGlResources(renderScene);
    probe.glResourcesReady = glProbe.valid && glProbe.gpuBatchContractPreserved;
    const mw::presentation::CampaignBattleFarGroundApron farGroundApron =
        mw::presentation::campaignBattleFarGroundApron(renderScene.terrain);
    const double expectedApronMargin =
        mw::presentation::campaignBattleFarClipDistance() * 1.25;
    probe.farGroundApronReady =
        farGroundApron.valid &&
        std::abs(farGroundApron.minX -
                 (renderScene.terrain.boundsMinX - expectedApronMargin)) < 0.001 &&
        std::abs(farGroundApron.maxX -
                 (renderScene.terrain.boundsMaxX + expectedApronMargin)) < 0.001 &&
        std::abs(farGroundApron.minZ -
                 (renderScene.terrain.boundsMinZ - expectedApronMargin)) < 0.001 &&
        std::abs(farGroundApron.maxZ -
                 (renderScene.terrain.boundsMaxZ + expectedApronMargin)) < 0.001 &&
        farGroundApron.groundY == 0.0;
    const mw::presentation::CampaignBattleGlVirtualViewportRect externalViewport =
        mw::presentation::campaignBattleGlVirtualViewportRect(
            mw::battle::CampaignBattlePresentationMode::External,
            mw::battle::CampaignCockpitFamily::Light);
    const mw::presentation::CampaignBattleGlVirtualViewportRect cockpitViewport =
        mw::presentation::campaignBattleGlVirtualViewportRect(
            mw::battle::CampaignBattlePresentationMode::Cockpit,
            mw::battle::CampaignCockpitFamily::Light);
    const mw::presentation::CampaignBattleExternalCameraOrbit draggedCamera =
        mw::presentation::campaignBattleExternalCameraAfterDrag({}, 100, 400);
    const mw::presentation::CampaignBattleExternalCameraOrbit zoomedCamera =
        mw::presentation::campaignBattleExternalCameraAfterWheel({}, 120);
    probe.externalCameraReady =
        externalViewport.x == 0 && externalViewport.y == 0 &&
        externalViewport.width == 320 && externalViewport.height == 200 &&
        cockpitViewport.x == 11 && cockpitViewport.y == 11 &&
        cockpitViewport.width == 297 && cockpitViewport.height == 92 &&
        std::abs(draggedCamera.yawOffsetDegrees + 28.0f) < 0.001f &&
        std::abs(draggedCamera.pitchDegrees - 62.0f) < 0.001f &&
        std::abs(zoomedCamera.distance - 2880.0f) < 0.001f;
    probe.glTerrainVertices = glProbe.terrainVertexCount;
    probe.glTerrainObjects = glProbe.terrainObjectCount;
    probe.glTerrainObjectRecords = glProbe.terrainObjectRecordCount;
    probe.glTerrainObjectVertices = glProbe.terrainObjectVertexCount;
    probe.glTerrainPaletteLoaded = glProbe.terrainPaletteLoaded;
    probe.glTerrainPaletteId = glProbe.terrainPaletteId;
    probe.glTerrainObjectDitherVertices = glProbe.terrainObjectDitherVertexCount;
    probe.glTerrainObjectDitherTriangles = glProbe.terrainObjectDitherTriangleCount;
    const mw::presentation::CampaignEgaPaletteBank desertPalette =
        mw::presentation::loadCampaignEgaPaletteBank(
            mw::battle::campaignBattleOriginalResourcePath(
                sortedOriginalFilesRoot,
                std::filesystem::path("PAL") / "DESERT.PAL"),
            1u);
    const mw::presentation::CampaignEgaPaletteBank tropicPalette =
        mw::presentation::loadCampaignEgaPaletteBank(
            mw::battle::campaignBattleOriginalResourcePath(
                sortedOriginalFilesRoot,
                std::filesystem::path("PAL") / "TROPIC.PAL"),
            1u);
    const mw::presentation::CampaignEgaPaletteBank arcticPalette =
        mw::presentation::loadCampaignEgaPaletteBank(
            mw::battle::campaignBattleOriginalResourcePath(
                sortedOriginalFilesRoot,
                std::filesystem::path("PAL") / "ARCTIC.PAL"),
            1u);
    const std::array<uint8_t, 128> checker =
        mw::legacy3d::terrainCheckerStipple4x4();
    probe.glTerrainEnvironmentPalettesReady =
        desertScene.terrain.palettePath.filename() == "DESERT.PAL" &&
        renderScene.terrain.palettePath.filename() == "TROPIC.PAL" &&
        snowScene.terrain.palettePath.filename() == "ARCTIC.PAL" &&
        desertPalette.valid && tropicPalette.valid && arcticPalette.valid &&
        desertPalette.colorPairs == mw::legacy3d::terrainPaletteBank1Bytes("desert") &&
        tropicPalette.colorPairs == mw::legacy3d::terrainPaletteBank1Bytes("green") &&
        arcticPalette.colorPairs == mw::legacy3d::terrainPaletteBank1Bytes("snow") &&
        checker[0] == 0x0fu && checker[15] == 0x0fu &&
        checker[16] == 0xf0u && checker[31] == 0xf0u && checker[32] == 0x0fu;
    probe.glVisuals = glProbe.visualCount;
    probe.glObjectiveLoaded = glProbe.objectiveLoaded;
    const mw::presentation::CampaignCockpitBackdropImage backdrop =
        mw::presentation::loadCampaignCockpitBackdrop(renderScene.cockpit);
    const mw::presentation::CampaignCockpitLayout& cockpitLayout =
        mw::presentation::campaignCockpitLayout(renderScene.cockpit.family);
    probe.cockpitBackdropReady =
        backdrop.valid &&
        backdrop.width == 320 &&
        backdrop.height == 200 &&
        backdrop.bgraPixels.size() == 320u * 200u;
    probe.cockpitBackdropCodec = backdrop.codec;
    probe.cockpitViewportWidth = cockpitLayout.viewport.width;
    probe.cockpitViewportHeight = cockpitLayout.viewport.height;
    const auto& lightWeapons =
        mw::presentation::campaignCockpitWeaponPanelLayout(
            mw::battle::CampaignCockpitFamily::Light);
    const auto& mediumWeapons =
        mw::presentation::campaignCockpitWeaponPanelLayout(
            mw::battle::CampaignCockpitFamily::Medium);
    const auto& heavyWeapons =
        mw::presentation::campaignCockpitWeaponPanelLayout(
            mw::battle::CampaignCockpitFamily::Heavy);
    const auto heavyReadyColors =
        mw::presentation::campaignCockpitWeaponTextColors(
            mw::battle::CampaignCockpitFamily::Heavy,
            mw::battle::BattleWeaponReadiness::Ready);
    const auto lightReadyColors =
        mw::presentation::campaignCockpitWeaponTextColors(
            mw::battle::CampaignCockpitFamily::Light,
            mw::battle::BattleWeaponReadiness::Ready);
    const auto mediumReadyColors =
        mw::presentation::campaignCockpitWeaponTextColors(
            mw::battle::CampaignCockpitFamily::Medium,
            mw::battle::BattleWeaponReadiness::Ready);
    const auto heavyCooldownColors =
        mw::presentation::campaignCockpitWeaponTextColors(
            mw::battle::CampaignCockpitFamily::Heavy,
            mw::battle::BattleWeaponReadiness::Cooldown);
    const auto lightCooldownColors =
        mw::presentation::campaignCockpitWeaponTextColors(
            mw::battle::CampaignCockpitFamily::Light,
            mw::battle::BattleWeaponReadiness::Cooldown);
    const auto mediumCooldownColors =
        mw::presentation::campaignCockpitWeaponTextColors(
            mw::battle::CampaignCockpitFamily::Medium,
            mw::battle::BattleWeaponReadiness::Cooldown);
    const auto shutdownCooldownColors =
        mw::presentation::campaignCockpitWeaponTextColors(
            mw::battle::CampaignCockpitFamily::Heavy,
            mw::battle::BattleWeaponReadiness::MechOffline,
            false,
            true);
    const auto shutdownReadyColors =
        mw::presentation::campaignCockpitWeaponTextColors(
            mw::battle::CampaignCockpitFamily::Heavy,
            mw::battle::BattleWeaponReadiness::MechOffline);
    const auto destroyedCooldownColors =
        mw::presentation::campaignCockpitWeaponTextColors(
            mw::battle::CampaignCockpitFamily::Heavy,
            mw::battle::BattleWeaponReadiness::NonFunctional,
            false,
            true);
    mw::battle::BattleSnapshot weaponRangeSnapshot = snapshot;
    weaponRangeSnapshot.targetScan.selectedTargetKind =
        mw::battle::BattleTargetScanTargetKind::Combatant;
    weaponRangeSnapshot.targetScan.selectedTargetEntityId =
        snapshot.combatants.back().id;
    weaponRangeSnapshot.targetScan.selectedTargetDistanceMeters = 900.0;
    mw::battle::BattleWeaponInstanceState mediumLaserRangeWeapon;
    mediumLaserRangeWeapon.originalRangeValueProven = true;
    mediumLaserRangeWeapon.maximumRange = 4800.0;
    const bool mediumLaserRangeActive =
        mw::presentation::campaignCockpitWeaponRangeIndicatorActive(
            weaponRangeSnapshot,
            mediumLaserRangeWeapon);
    const auto inRangeColors =
        mw::presentation::campaignCockpitWeaponTextColors(
            mw::battle::CampaignCockpitFamily::Light,
            mw::battle::BattleWeaponReadiness::Cooldown,
            mediumLaserRangeActive);
    weaponRangeSnapshot.targetScan.selectedTargetDistanceMeters = 1000.0;
    const bool mediumLaserRangeInactive =
        !mw::presentation::campaignCockpitWeaponRangeIndicatorActive(
            weaponRangeSnapshot,
            mediumLaserRangeWeapon);
    weaponRangeSnapshot.targetScan.selectedTargetKind =
        mw::battle::BattleTargetScanTargetKind::None;
    const bool noTargetRangeInactive =
        !mw::presentation::campaignCockpitWeaponRangeIndicatorActive(
            weaponRangeSnapshot,
            mediumLaserRangeWeapon);
    probe.cockpitWeaponPanelLayoutsReady =
        !lightWeapons.rightSide && lightWeapons.statusX == 19 &&
        lightWeapons.firstRowY == 125 && lightWeapons.nameX == 29 &&
        lightWeapons.ammunitionRightExclusive == 81 &&
        lightWeapons.rangeClassX == 86 && lightWeapons.rowCapacity == 5u &&
        !mediumWeapons.rightSide && mediumWeapons.statusX == 19 &&
        mediumWeapons.firstRowY == 125 && mediumWeapons.nameX == 29 &&
        mediumWeapons.ammunitionRightExclusive == 81 &&
        mediumWeapons.rangeClassX == 86 && mediumWeapons.rowCapacity == 5u &&
        heavyWeapons.rightSide && heavyWeapons.statusX == 233 &&
        heavyWeapons.firstRowY == 112 && heavyWeapons.nameX == 243 &&
        heavyWeapons.ammunitionRightExclusive == 295 &&
        heavyWeapons.rangeClassX == 300 &&
        heavyWeapons.secondBankFirstRow == 5u &&
        heavyWeapons.secondBankYOffset == 3 &&
        heavyWeapons.rowCapacity == 10u &&
        mw::presentation::campaignCockpitWeaponPanelRowY(heavyWeapons, 4u) ==
            144 &&
        mw::presentation::campaignCockpitWeaponPanelRowY(heavyWeapons, 5u) ==
            155 &&
        mw::presentation::campaignCockpitWeaponPanelRowY(heavyWeapons, 9u) ==
            187 &&
        heavyReadyColors.nameBgra == 0xffffffffu &&
        heavyReadyColors.rangeBgra == 0xff000000u &&
        lightReadyColors.nameBgra == 0xffffffffu &&
        lightReadyColors.rangeBgra == 0xff000000u &&
        mediumReadyColors.nameBgra == 0xffffffffu &&
        mediumReadyColors.rangeBgra == 0xff000000u &&
        heavyCooldownColors.nameBgra == 0xffaa0000u &&
        heavyCooldownColors.rangeBgra == 0xff000000u &&
        lightCooldownColors.nameBgra == 0xffaa0000u &&
        lightCooldownColors.rangeBgra == 0xff000000u &&
        mediumCooldownColors.nameBgra == 0xffaa0000u &&
        mediumCooldownColors.rangeBgra == 0xff000000u &&
        shutdownCooldownColors.nameBgra == 0xffaa0000u &&
        shutdownReadyColors.nameBgra == 0xffffffffu &&
        destroyedCooldownColors.nameBgra == 0xff000000u &&
        mediumLaserRangeActive && mediumLaserRangeInactive &&
        noTargetRangeInactive &&
        inRangeColors.nameBgra == 0xffaa0000u &&
        inRangeColors.rangeBgra == 0xffffff55u;
    const auto lightTargetMfd =
        mw::presentation::campaignCockpitTargetMfdGeometry(
            mw::battle::CampaignCockpitFamily::Light);
    const auto mediumTargetMfd =
        mw::presentation::campaignCockpitTargetMfdGeometry(
            mw::battle::CampaignCockpitFamily::Medium);
    const auto heavyTargetMfd =
        mw::presentation::campaignCockpitTargetMfdGeometry(
            mw::battle::CampaignCockpitFamily::Heavy);
    const mw::presentation::CampaignIndexedSpriteArchive smallMechs =
        mw::presentation::loadCampaignIndexedSpriteArchive(
            renderScene.cockpit.smallMechsPath);
    const bool smallMechDimensionsReady =
        smallMechs.valid && smallMechs.sprites.size() == 9u &&
        std::all_of(
            smallMechs.sprites.begin(),
            smallMechs.sprites.end(),
            [](const mw::presentation::CampaignIndexedSprite& sprite) {
                return sprite.width == 56 && sprite.height == 49;
            });
    const auto smallMechPixelIndex = [](
                                         const mw::presentation::CampaignIndexedSprite& sprite,
                                         int x,
                                         int y) {
        const uint8_t packed = sprite.packedPixels[
            static_cast<size_t>(y) * static_cast<size_t>(sprite.rowStride) +
            static_cast<size_t>(x / 2)];
        return x % 2 == 0
            ? static_cast<uint8_t>((packed >> 4u) & 0x0fu)
            : static_cast<uint8_t>(packed & 0x0fu);
    };
    bool originalSmallMechDamageRegionsReady = smallMechDimensionsReady;
    constexpr std::array<mw::mech3d::MechArmorSectionId, 8>
        visibleDamageSections{{
            mw::mech3d::MechArmorSectionId::LeftArm,
            mw::mech3d::MechArmorSectionId::RightArm,
            mw::mech3d::MechArmorSectionId::LeftLeg,
            mw::mech3d::MechArmorSectionId::RightLeg,
            mw::mech3d::MechArmorSectionId::CenterTorso,
            mw::mech3d::MechArmorSectionId::LeftTorso,
            mw::mech3d::MechArmorSectionId::RightTorso,
            mw::mech3d::MechArmorSectionId::Head,
        }};
    // Source-zero details outside the original rectangles remain black.  Most
    // frames have none; PHO/RIF/WAR intentionally retain these fixed details.
    constexpr std::array<size_t, 8> expectedUnmappedZeroPixels{{
        0u, 0u, 15u, 0u, 2u, 26u, 0u, 0u,
    }};
    if (smallMechDimensionsReady) {
        for (int mechIndex = 0; mechIndex < 8; ++mechIndex) {
            std::array<bool, visibleDamageSections.size()> observed{};
            size_t unmappedZeroPixels = 0u;
            const auto& sprite = smallMechs.sprites[static_cast<size_t>(mechIndex)];
            for (int y = 0; y < sprite.height; ++y) {
                for (int x = lightTargetMfd.sourceX;
                     x < lightTargetMfd.sourceX + lightTargetMfd.rect.width;
                     ++x) {
                    if (smallMechPixelIndex(sprite, x, y) != 0u) {
                        continue;
                    }
                    const auto section = mw::presentation::
                        campaignCockpitTargetScanPixelArmorSection(
                            mechIndex,
                            x,
                            y);
                    if (!section) {
                        ++unmappedZeroPixels;
                        continue;
                    }
                    const auto found = std::find(
                        visibleDamageSections.begin(),
                        visibleDamageSections.end(),
                        *section);
                    if (found != visibleDamageSections.end()) {
                        observed[static_cast<size_t>(std::distance(
                            visibleDamageSections.begin(),
                            found))] = true;
                    }
                }
            }
            originalSmallMechDamageRegionsReady =
                originalSmallMechDamageRegionsReady &&
                unmappedZeroPixels == expectedUnmappedZeroPixels[
                    static_cast<size_t>(mechIndex)] &&
                std::all_of(observed.begin(), observed.end(), [](bool value) {
                    return value;
                });
        }
    }
    mw::mech3d::MechDetailedDamageState exactArmorDisplay;
    exactArmorDisplay.valid = true;
    auto& exactArmor = exactArmorDisplay.armorSections[static_cast<size_t>(
        mw::mech3d::MechArmorSectionId::CenterTorso)];
    exactArmor.armorMaximum = 10u;
    exactArmor.armorRemaining = 5u;
    auto& exactInternal = exactArmorDisplay.internalSections[static_cast<size_t>(
        mw::mech3d::MechInternalSectionId::CenterTorso)];
    exactInternal.structureMaximum = 6u;
    exactInternal.structureRemaining = 6u;
    probe.cockpitTargetScanReady =
        scannedCockpitScene.valid && scannedCockpitScene.snapshotOwned &&
        scannedCockpitScene.scannedTargetKind ==
            mw::battle::BattleTargetScanTargetKind::Combatant &&
        scannedCockpitScene.scannedTargetEntityId ==
            scannedSnapshot.targetScan.selectedTargetEntityId &&
        scannedObjectiveCockpitScene.valid &&
        scannedObjectiveCockpitScene.snapshotOwned &&
        scannedObjectiveCockpitScene.scannedTargetKind ==
            mw::battle::BattleTargetScanTargetKind::Objective &&
        !mw::battle::isValid(scannedObjectiveCockpitScene.scannedTargetEntityId) &&
        scannedObjectiveCockpitScene.objective.valid &&
        !scannedObjectiveCockpitScene.objective.playerProtected &&
        protectedObjectiveCockpitScene.valid &&
        protectedObjectiveCockpitScene.objective.playerProtected &&
        smallMechDimensionsReady &&
        originalSmallMechDamageRegionsReady &&
        lightTargetMfd.rect.x == 241 && lightTargetMfd.rect.y == 123 &&
        lightTargetMfd.rect.width == 53 && lightTargetMfd.rect.height == 49 &&
        lightTargetMfd.sourceX == 0 &&
        mediumTargetMfd.rect.x == 241 && mediumTargetMfd.rect.y == 123 &&
        mediumTargetMfd.rect.width == 53 && mediumTargetMfd.rect.height == 49 &&
        mediumTargetMfd.sourceX == 0 &&
        heavyTargetMfd.rect.x == 24 && heavyTargetMfd.rect.y == 132 &&
        heavyTargetMfd.rect.width == 53 && heavyTargetMfd.rect.height == 49 &&
        heavyTargetMfd.sourceX == 0 &&
        mw::presentation::campaignCockpitTargetScanSpriteIndex("locust") == 0 &&
        mw::presentation::campaignCockpitTargetScanSpriteIndex("jenner") == 1 &&
        mw::presentation::campaignCockpitTargetScanSpriteIndex("phoenix_hawk") == 2 &&
        mw::presentation::campaignCockpitTargetScanSpriteIndex("shadow_hawk") == 3 &&
        mw::presentation::campaignCockpitTargetScanSpriteIndex("rifleman") == 4 &&
        mw::presentation::campaignCockpitTargetScanSpriteIndex("warhammer") == 5 &&
        mw::presentation::campaignCockpitTargetScanSpriteIndex("marauder") == 6 &&
        mw::presentation::campaignCockpitTargetScanSpriteIndex("battlemaster") == 7 &&
        mw::presentation::campaignCockpitTargetScanObjectiveSpriteIndex() == 8 &&
        mw::presentation::campaignCockpitTargetScanSpriteIndex("unsupported") == -1 &&
        smallMechPixelIndex(smallMechs.sprites[0], 24, 4) == 0u &&
        smallMechPixelIndex(smallMechs.sprites[0], 25, 6) == 0u &&
        mw::presentation::campaignCockpitTargetScanPixelRole(0, 25, 6) ==
            mw::battle::BattleMechSystemRole::Cockpit &&
        mw::presentation::campaignCockpitTargetScanPixelRole(0, 24, 4) ==
            mw::battle::BattleMechSystemRole::Core &&
        mw::presentation::campaignCockpitTargetScanPixelRole(1, 11, 5) ==
            mw::battle::BattleMechSystemRole::Weapons &&
        mw::presentation::campaignCockpitTargetScanPixelRole(7, 19, 22) ==
            mw::battle::BattleMechSystemRole::Mobility &&
        mw::presentation::campaignCockpitTargetScanPixelRole(0, 2, 20) ==
            mw::battle::BattleMechSystemRole::Unknown &&
        mw::presentation::campaignCockpitTargetScanPixelArmorSection(0, 24, 4) ==
            mw::mech3d::MechArmorSectionId::CenterTorso &&
        mw::presentation::campaignCockpitTargetScanPixelArmorSection(0, 25, 6) ==
            mw::mech3d::MechArmorSectionId::Head &&
        mw::presentation::campaignCockpitTargetScanPixelArmorSection(1, 11, 5) ==
            mw::mech3d::MechArmorSectionId::RightArm &&
        mw::presentation::campaignCockpitTargetScanPixelArmorSection(7, 19, 22) ==
            mw::mech3d::MechArmorSectionId::RightLeg &&
        !mw::presentation::campaignCockpitTargetScanPixelArmorSection(0, 2, 20) &&
        !mw::presentation::campaignCockpitTargetScanPixelArmorSection(8, 28, 12) &&
        mw::presentation::campaignDetailedDamageDisplayStatus(0u, 0) ==
            mw::battle::BattleMechSystemStatus::Online &&
        mw::presentation::campaignDetailedDamageDisplayStatus(1u, 0) ==
            mw::battle::BattleMechSystemStatus::Degraded &&
        mw::presentation::campaignDetailedDamageDisplayStatus(0u, 2) ==
            mw::battle::BattleMechSystemStatus::Degraded &&
        mw::presentation::campaignDetailedDamageDisplayStatus(2u, 0) ==
            mw::battle::BattleMechSystemStatus::Offline &&
        mw::presentation::campaignDetailedDamageDisplayStatus(3u, 0) ==
            mw::battle::BattleMechSystemStatus::Destroyed &&
        mw::presentation::campaignDetailedDamageDisplayStatus(0u, 0, false) ==
            mw::battle::BattleMechSystemStatus::Destroyed &&
        mw::presentation::campaignDetailedArmorDisplayStatus(
            exactArmorDisplay,
            mw::mech3d::MechArmorSectionId::CenterTorso) ==
            mw::battle::BattleMechSystemStatus::Degraded &&
        mw::presentation::campaignCockpitTargetScanDisplayColorIndex(
            10u,
            mw::battle::BattleMechSystemRole::Unknown,
            mw::battle::BattleMechSystemStatus::Online,
            false) == 10u &&
        mw::presentation::campaignCockpitTargetScanDisplayColorIndex(
            3u,
            mw::battle::BattleMechSystemRole::Core,
            mw::battle::BattleMechSystemStatus::Online,
            false) == 0u &&
        mw::presentation::campaignCockpitTargetScanDisplayColorIndex(
            0u,
            mw::battle::BattleMechSystemRole::Core,
            mw::battle::BattleMechSystemStatus::Online,
            false) == 7u &&
        mw::presentation::campaignCockpitTargetScanDisplayColorIndex(
            0u,
            mw::battle::BattleMechSystemRole::Core,
            mw::battle::BattleMechSystemStatus::Degraded,
            false) == 14u &&
        mw::presentation::campaignCockpitTargetScanDisplayColorIndex(
            0u,
            mw::battle::BattleMechSystemRole::Core,
            mw::battle::BattleMechSystemStatus::Destroyed,
            false) == 0u &&
        mw::presentation::campaignCockpitTargetScanDisplayColorIndex(
            0u,
            mw::battle::BattleMechSystemRole::Core,
            mw::battle::BattleMechSystemStatus::Offline,
            false) == 4u &&
        mw::presentation::campaignCockpitTargetScanDisplayColorIndex(
            0u,
            mw::battle::BattleMechSystemRole::Core,
            mw::battle::BattleMechSystemStatus::Online,
            true) == 0u &&
        mw::presentation::campaignBattleTargetRectangleCount(
            mw::battle::BattleTeam::Player) == 2 &&
        mw::presentation::campaignBattleTargetRectangleCount(
            mw::battle::BattleTeam::Opposing) == 1 &&
        mw::presentation::campaignBattleTargetRectangleCount(
            mw::battle::BattleTeam::Neutral) == 0;
    probe.cockpitTargetScanReady = probe.cockpitTargetScanReady &&
        mw::presentation::campaignBattleObjectiveTargetRectangleCount(false) == 1 &&
        mw::presentation::campaignBattleObjectiveTargetRectangleCount(true) == 2;
    const mw::presentation::CampaignCockpitBackdropImage missionStatusBackdrop =
        mw::presentation::loadCampaignCockpitBackdrop(missionStatusScene.cockpit);
    const mw::presentation::CampaignCockpitRect missionStatusRect =
        mw::presentation::campaignMissionStatusMapRect();
    probe.missionStatusMapReady =
        missionStatus.valid && missionStatusScene.valid &&
        missionStatusScene.snapshotOwned &&
        missionStatusScene.authoritativeTickIndex == snapshot.tickIndex &&
        missionStatusScene.cockpit.backdropPath.filename() == "STATUS.SCR" &&
        missionStatusBackdrop.valid && missionStatusBackdrop.width == 320 &&
        missionStatusBackdrop.height == 200 &&
        missionStatusRect.x == 5 && missionStatusRect.y == 15 &&
        missionStatusRect.width == 197 && missionStatusRect.height == 107 &&
        missionStatusScene.mission.title == snapshot.mission.title &&
        missionStatusScene.setup.valid == snapshot.setup.valid &&
        missionStatusScene.battlefieldBoundary.valid == snapshot.battlefieldBoundary.valid;
    probe.missionStatusBackdropCodec = missionStatusBackdrop.codec;
    const mw::presentation::CampaignCockpitBackdropImage cockpitCommandMapBackdrop =
        mw::presentation::loadCampaignCockpitBackdrop(cockpitCommandMapScene.cockpit);
    const mw::presentation::CampaignCockpitRect cockpitCommandMapRect =
        mw::presentation::campaignCockpitCommandMapRect();
    probe.cockpitCommandMapReady =
        cockpitCommandMap.valid && cockpitCommandMap.snapshotOwned &&
        cockpitCommandMapScene.valid && cockpitCommandMapScene.snapshotOwned &&
        cockpitCommandMapScene.authoritativeTickIndex == snapshot.tickIndex &&
        cockpitCommandMapScene.cockpit.backdropPath.filename() == "MAP.SCR" &&
        cockpitCommandMapBackdrop.valid && cockpitCommandMapBackdrop.width == 320 &&
        cockpitCommandMapBackdrop.height == 200 &&
        cockpitCommandMapRect.x == 62 && cockpitCommandMapRect.y == 7 &&
        cockpitCommandMapRect.width == 197 && cockpitCommandMapRect.height == 107 &&
        !mw::battle::campaignBattlePresentationPausesSimulation(
            mw::battle::CampaignBattlePresentationMode::CockpitCommandMap) &&
        mw::battle::campaignBattlePresentationPausesSimulation(
            mw::battle::CampaignBattlePresentationMode::MissionStatus);
    probe.cockpitCommandMapBackdropCodec = cockpitCommandMapBackdrop.codec;
    const mw::presentation::CampaignCockpitMinimapTerrain minimapTerrain =
        mw::presentation::loadCampaignCockpitMinimapTerrain(renderScene.terrain);
    const mw::presentation::CampaignCockpitRect minimapRect =
        mw::presentation::campaignCockpitMinimapRect();
    probe.cockpitMinimapTerrainSamples = minimapTerrain.samples.size();
    probe.cockpitMinimapSurfacePlacements =
        minimapTerrain.driveableSurfacePlacements;
    probe.cockpitMinimapMountainPlacements =
        minimapTerrain.solidMountainPlacements;
    probe.cockpitMinimapMountainGradientPlacements =
        minimapTerrain.solidMountainGradientPlacements;
    probe.cockpitMinimapMountainFourBandPlacements =
        minimapTerrain.solidMountainFourBandPlacements;
    probe.cockpitMinimapHeightBandSamples =
        minimapTerrain.heightBandSampleCounts;
    probe.cockpitMinimapReady =
        minimapTerrain.valid && !minimapTerrain.samples.empty() &&
        minimapTerrain.provenance ==
            "WLD_TERPCK_projected_triangle_raster_surface_flat_mountain_height_bands" &&
        minimapTerrain.driveableSurfacePlacements == 5u &&
        minimapTerrain.solidMountainPlacements == 8u &&
        minimapTerrain.solidMountainGradientPlacements == 8u &&
        minimapTerrain.solidMountainFourBandPlacements == 8u &&
        minimapTerrain.heightBandSampleCounts ==
            std::array<size_t, 4>{1045u, 403u, 338u, 130u} &&
        minimapTerrain.samples.size() == 1916u &&
        minimapRect.x == 120 && minimapRect.y == 117 &&
        minimapRect.width == 72 && minimapRect.height == 67 &&
        mw::presentation::campaignBattleMapWorldUnitsPerPixel() == 500.0;
    const mw::presentation::CampaignCockpitRadarGeometry radarGeometry =
        mw::presentation::campaignCockpitRadarGeometry();
    mw::battle::Transform radarPlayer;
    radarPlayer.headingRadians = 0.0;
    mw::battle::Transform radarForward;
    radarForward.z = 10000.0;
    mw::battle::Transform radarVisibleLeftEdge;
    radarVisibleLeftEdge.x = 10000.0;
    radarVisibleLeftEdge.z = 10000.0;
    mw::battle::Transform radarVisibleRightEdge;
    radarVisibleRightEdge.x = -10000.0;
    radarVisibleRightEdge.z = 10000.0;
    mw::battle::Transform radarOutside;
    radarOutside.x = 10001.0;
    radarOutside.z = 10000.0;
    mw::battle::Transform radarBehind;
    radarBehind.z = -1000.0;
    mw::battle::Transform radarBeyond;
    radarBeyond.z = 20001.0;
    const auto forwardContact =
        mw::presentation::campaignCockpitRadarContactProjection(
            radarPlayer, radarForward, 4000u);
    const auto visibleLeftEdgeContact =
        mw::presentation::campaignCockpitRadarContactProjection(
            radarPlayer, radarVisibleLeftEdge, 4000u);
    const auto visibleRightEdgeContact =
        mw::presentation::campaignCockpitRadarContactProjection(
            radarPlayer, radarVisibleRightEdge, 4000u);
    const auto outsideContact =
        mw::presentation::campaignCockpitRadarContactProjection(
            radarPlayer, radarOutside, 4000u);
    const auto behindContact =
        mw::presentation::campaignCockpitRadarContactProjection(
            radarPlayer, radarBehind, 4000u);
    const auto beyondContact =
        mw::presentation::campaignCockpitRadarContactProjection(
            radarPlayer, radarBeyond, 4000u);
    probe.cockpitRadarReady =
        radarGeometry.rect.x == 120 && radarGeometry.rect.y == 117 &&
        radarGeometry.rect.width == 72 && radarGeometry.rect.height == 67 &&
        radarGeometry.leftX == 122 && radarGeometry.rightX == 189 &&
        radarGeometry.topY == 119 && radarGeometry.apexX == 156 &&
        radarGeometry.apexY == 160 && radarGeometry.labelY == 173 &&
        mw::presentation::campaignCockpitRadarWorldUnitsPerMeter() == 5.0 &&
        mw::battle::battleCockpitRadarRangeMeters(0u) == 4000u &&
        mw::battle::battleCockpitRadarRangeMeters(1u) == 2000u &&
        mw::battle::battleCockpitRadarRangeMeters(2u) == 1000u &&
        mw::battle::battleCockpitRadarRangeMeters(3u) == 500u &&
        mw::battle::battleCockpitRadarRangeMeters(4u) == 4000u &&
        mw::presentation::campaignCockpitRadarRangeLabel(4000u) == "4000 M" &&
        mw::presentation::campaignCockpitRadarRangeLabel(2000u) == "2000 M" &&
        mw::presentation::campaignCockpitRadarRangeLabel(1000u) == "1000 M" &&
        mw::presentation::campaignCockpitRadarRangeLabel(500u) == "500 M" &&
        forwardContact.visible && forwardContact.x == 156 &&
        forwardContact.y == 139 &&
        std::abs(forwardContact.distanceMeters - 2000.0) < 0.001 &&
        visibleLeftEdgeContact.visible && visibleLeftEdgeContact.x == 139 &&
        visibleLeftEdgeContact.y == 139 &&
        visibleRightEdgeContact.visible && visibleRightEdgeContact.x == 173 &&
        visibleRightEdgeContact.y == 139 &&
        !outsideContact.visible && !behindContact.visible &&
        !beyondContact.visible;
    const mw::presentation::CampaignCockpitDynamicLayers dynamicLayers =
        mw::presentation::loadCampaignCockpitDynamicLayers(renderScene.cockpit);
    probe.cockpitDynamicLayersReady = dynamicLayers.valid;
    constexpr std::array<std::pair<int, int>, 6> machineGunFlashDimensions{{
        {48, 11}, {48, 15}, {40, 19},
        {48, 11}, {48, 15}, {40, 19},
    }};
    bool machineGunFlashResourcesReady =
        dynamicLayers.valid && dynamicLayers.widgets.size() == 12u;
    for (size_t index = 0; index < machineGunFlashDimensions.size() &&
         machineGunFlashResourcesReady; ++index) {
        const auto& sprite = dynamicLayers.widgets[index + 6u];
        const auto [expectedWidth, expectedHeight] =
            machineGunFlashDimensions[index];
        bool hasVisiblePixel = false;
        for (size_t alpha = 3u; alpha < sprite.rgbaPixels.size(); alpha += 4u) {
            hasVisiblePixel = hasVisiblePixel || sprite.rgbaPixels[alpha] != 0u;
        }
        machineGunFlashResourcesReady =
            sprite.index == static_cast<int>(index + 6u) &&
            sprite.width == expectedWidth &&
            sprite.height == expectedHeight && hasVisiblePixel;
    }
    probe.machineGunFlashVisualsReady =
        probe.machineGunFlashVisualsReady && machineGunFlashResourcesReady;
    probe.cockpitStruts = dynamicLayers.struts.size();
    probe.cockpitWidgets = dynamicLayers.widgets.size();
    probe.cockpitHudNumbers = dynamicLayers.hudNumbers.size();
    probe.cockpitFontWidth = dynamicLayers.font.width;
    probe.cockpitFontHeight = dynamicLayers.font.height;
    probe.lightStrutPlacements = mw::presentation::campaignCockpitStrutPlacements(
        mw::battle::CampaignCockpitFamily::Light).size();
    probe.mediumStrutPlacements = mw::presentation::campaignCockpitStrutPlacements(
        mw::battle::CampaignCockpitFamily::Medium).size();
    probe.heavyStrutPlacements = mw::presentation::campaignCockpitStrutPlacements(
        mw::battle::CampaignCockpitFamily::Heavy).size();
    static constexpr std::array<const char*, 8> battleStatusBmpNames = {{
        "LOC.BMP", "JEN.BMP", "PHO.BMP", "SHA.BMP",
        "RIF.BMP", "WAR.BMP", "MAR.BMP", "BAT.BMP",
    }};
    probe.battleStatusBmpsReady = true;
    for (const char* fileName : battleStatusBmpNames) {
        const mw::presentation::CampaignIndexedSpriteArchive archive =
            mw::presentation::loadCampaignIndexedSpriteArchive(
                mw::battle::campaignBattleOriginalResourcePath(
                    sortedOriginalFilesRoot,
                    std::filesystem::path("BMP") / fileName));
        if (!archive.valid || archive.sprites.size() != 1u) {
            probe.battleStatusBmpsReady = false;
            continue;
        }
        const mw::presentation::CampaignIndexedSprite& sprite = archive.sprites.front();
        if (sprite.width != 152 || sprite.height != 193 || sprite.rowStride != 76 ||
            sprite.packedPixels.size() != 76u * 193u) {
            probe.battleStatusBmpsReady = false;
            continue;
        }
        ++probe.battleStatusBmps;
        for (int y = 0; y < sprite.height; ++y) {
            for (int x = 0; x < sprite.width; ++x) {
                const uint8_t packed = sprite.packedPixels[
                    static_cast<size_t>(y) * static_cast<size_t>(sprite.rowStride) +
                    static_cast<size_t>(x / 2)];
                const uint8_t color = (x % 2 == 0)
                    ? static_cast<uint8_t>((packed >> 4u) & 0x0fu)
                    : static_cast<uint8_t>(packed & 0x0fu);
                if (color == 0u) {
                    ++probe.battleStatusBlackArmorPixels;
                } else if (color == 3u) {
                    ++probe.battleStatusCyanOutlinePixels;
                }
            }
        }
    }
    probe.battleStatusBmpsReady =
        probe.battleStatusBmpsReady &&
        probe.battleStatusBmps == battleStatusBmpNames.size() &&
        probe.battleStatusBlackArmorPixels > 0u &&
        probe.battleStatusCyanOutlinePixels > 0u;
    mw::battle::CombatantSnapshot gaugeCombatant = snapshot.combatants.front();
    gaugeCombatant.forwardSpeed = gaugeCombatant.maxForwardSpeed;
    gaugeCombatant.jumpCapable = true;
    gaugeCombatant.jumpMaxFuel = 1.5;
    gaugeCombatant.jumpFuel = gaugeCombatant.jumpMaxFuel * 0.5;
    gaugeCombatant.jumpActivationFuel = 0.975;
    gaugeCombatant.heat.enabled = true;
    gaugeCombatant.heat.rawHeat = 800;
    const mw::presentation::CampaignCockpitGaugeState lightGauge =
        mw::presentation::campaignCockpitGaugeState(
            gaugeCombatant,
            mw::battle::CampaignCockpitFamily::Light);
    const mw::presentation::CampaignCockpitGaugeState mediumGauge =
        mw::presentation::campaignCockpitGaugeState(
            gaugeCombatant,
            mw::battle::CampaignCockpitFamily::Medium);
    const mw::presentation::CampaignCockpitGaugeState heavyGauge =
        mw::presentation::campaignCockpitGaugeState(
            gaugeCombatant,
            mw::battle::CampaignCockpitFamily::Heavy);
    gaugeCombatant.heat.rawHeat = mw::battle::battleMaximumRawHeat();
    const mw::presentation::CampaignCockpitGaugeState maximumHeatGauge =
        mw::presentation::campaignCockpitGaugeState(
            gaugeCombatant,
            mw::battle::CampaignCockpitFamily::Light);
    probe.cockpitGaugesReady =
        lightGauge.speedX == 236 && lightGauge.speedY == 177 &&
        lightGauge.forwardBars == 16 && lightGauge.reverseBars == 0 &&
        lightGauge.heatX == 198 && lightGauge.heatBottomY == 183 &&
        lightGauge.heatBars == 10 &&
        lightGauge.jumpGaugeVisible && lightGauge.jumpGauge.x == 299 &&
        lightGauge.jumpGauge.y == 132 && lightGauge.jumpGauge.height == 40 &&
        lightGauge.jumpFuelFillHeight == 20 &&
        mediumGauge.jumpGaugeVisible &&
        mediumGauge.heatX == 198 && mediumGauge.heatBottomY == 183 &&
        mediumGauge.heatBars == 10 &&
        mediumGauge.jumpGauge.x == lightGauge.jumpGauge.x &&
        mediumGauge.jumpGauge.y == lightGauge.jumpGauge.y &&
        heavyGauge.speedX == 22 && heavyGauge.speedY == 186 &&
        heavyGauge.heatX == 198 && heavyGauge.heatBottomY == 183 &&
        heavyGauge.heatBars == 10 &&
        !heavyGauge.jumpGaugeVisible &&
        maximumHeatGauge.heatBars == 30;
    probe.cockpitHudColors = mw::presentation::campaignCockpitHudColorCount();
    mw::presentation::CampaignBattleGlCockpitOptions cockpitOptions;
    cockpitOptions.zoomLevel = 3;
    cockpitOptions.hudRgb = mw::presentation::campaignCockpitHudColor(3u).rgb;
    probe.cockpitMaxZoom = cockpitOptions.zoomLevel;
    constexpr double pi = 3.14159265358979323846;
    const int centerHeading = static_cast<int>(std::lround(
        mw::presentation::campaignCockpitDisplayHeadingDegrees(pi * 0.5)));
    const int rightTurnHeading = static_cast<int>(std::lround(
        mw::presentation::campaignCockpitDisplayHeadingDegrees(pi / 3.0)));
    probe.cockpitCompassRightTurn = centerHeading == 90 && rightTurnHeading == 120;
    const mw::presentation::CampaignCockpitViewportHudLayout lightHud =
        mw::presentation::campaignCockpitViewportHudLayout(
            mw::battle::CampaignCockpitFamily::Light,
            0);
    const mw::presentation::CampaignCockpitViewportHudLayout lightAimUpHud =
        mw::presentation::campaignCockpitViewportHudLayout(
            mw::battle::CampaignCockpitFamily::Light,
            2);
    const mw::presentation::CampaignCockpitViewportHudLayout mediumHud =
        mw::presentation::campaignCockpitViewportHudLayout(
            mw::battle::CampaignCockpitFamily::Medium,
            0);
    const mw::presentation::CampaignCockpitViewportHudLayout heavyHud =
        mw::presentation::campaignCockpitViewportHudLayout(
            mw::battle::CampaignCockpitFamily::Heavy,
            0);
    probe.cockpitViewportHudReady =
        lightHud.crosshairCenterX == 159 && lightHud.crosshairCenterY == 62 &&
        lightAimUpHud.crosshairCenterY == 56 &&
        lightHud.leftLabelX == 60 && lightHud.rightLabelX == 222 &&
        lightHud.labelY == 96 && !lightHud.zoomLabelOnLeft &&
        mediumHud.crosshairCenterX == 159 && mediumHud.crosshairCenterY == 51 &&
        mediumHud.leftLabelX == 59 && mediumHud.rightLabelX == 223 &&
        mediumHud.labelY == 96 && !mediumHud.zoomLabelOnLeft &&
        heavyHud.crosshairCenterX == 160 && heavyHud.crosshairCenterY == 51 &&
        heavyHud.leftLabelX == 53 && heavyHud.rightLabelX == 221 &&
        heavyHud.labelY == 96 && heavyHud.zoomLabelOnLeft;
    return probe;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parseOptions(argc, argv);

        mw::battle::BattleStartParams victoryParams = campaignAcceptedParams(options, true);
        expect(
            victoryParams.setupMetadata.playerSlots.size() == 4u &&
                victoryParams.setupMetadata.playerSlots[0].slotIndex == 0u &&
                victoryParams.setupMetadata.playerSlots[1].slotIndex == 1u &&
                victoryParams.setupMetadata.playerSlots[2].slotIndex == 2u &&
                victoryParams.setupMetadata.playerSlots[3].slotIndex == 3u &&
                victoryParams.setupMetadata.playerSlots[0].role == "player" &&
                victoryParams.setupMetadata.playerSlots[1].role == "player_allied" &&
                std::all_of(
                    victoryParams.setupMetadata.playerSlots.begin(),
                    victoryParams.setupMetadata.playerSlots.end(),
                    [&victoryParams](const mw::battle::BattleSetupSlotMetadata& slot) {
                        return std::abs(
                                   slot.transform.headingRadians -
                                   victoryParams.playerStartTransform.headingRadians) <
                            0.000001;
                    }),
            "campaign setup dropped original player lance placement slots");
        mw::battle::CampaignBattleRenderVisualInstance controlledVisual;
        controlledVisual.team = mw::battle::BattleTeam::Player;
        controlledVisual.playerControlled = true;
        mw::battle::CampaignBattleRenderVisualInstance alliedVisual;
        alliedVisual.team = mw::battle::BattleTeam::Player;
        alliedVisual.playerControlled = false;
        expect(
            mw::presentation::campaignBattleVisualHiddenInCockpit(
                controlledVisual) &&
                !mw::presentation::campaignBattleVisualHiddenInCockpit(
                    alliedVisual),
            "campaign cockpit incorrectly hides allied player-team visuals");
        const mw::battle::BattleDriveRuntimeTuning driveTuning =
            mw::battle::battleDrivePrototypeTuning();
        expect(
            std::abs(victoryParams.fixedTickSeconds - driveTuning.fixedTickSeconds) < 0.000001 &&
                std::abs(victoryParams.maxForwardSpeed - driveTuning.maxForwardSpeed) < 0.000001 &&
                std::abs(victoryParams.maxReverseSpeed - driveTuning.maxReverseSpeed) < 0.000001 &&
                std::abs(victoryParams.acceleration - driveTuning.acceleration) < 0.000001 &&
                std::abs(victoryParams.deceleration - driveTuning.deceleration) < 0.000001 &&
                std::abs(victoryParams.maxTurnRateRadians - driveTuning.maxTurnRateRadians) < 0.000001,
            "campaign battle drive tuning diverged from standalone prototype");
        mw::battle::BattleStartParams motionParams = campaignAcceptedParams(options, false);
        mw::battle::BattleWorld motionWorld = mw::battle::BattleWorld::create(motionParams);
        const mw::battle::BattleSnapshot motionStart = motionWorld.snapshot();
        for (uint64_t tick = 0; tick < 20u; ++tick) {
            mw::battle::BattleInputCommand command;
            command.tickIndex = motionWorld.tickIndex();
            command.entityId = motionWorld.playerEntityId();
            command.throttle = 1.0;
            motionWorld.enqueueInput(command);
            motionWorld.tick();
        }
        const mw::battle::BattleSnapshot motionEnd = motionWorld.snapshot();
        const double motionDx = motionEnd.combatants.front().transform.x -
                                motionStart.combatants.front().transform.x;
        const double motionDz = motionEnd.combatants.front().transform.z -
                                motionStart.combatants.front().transform.z;
        const double oneSecondMotionDistance = std::hypot(motionDx, motionDz);
        expect(
            std::abs(oneSecondMotionDistance - 262.5) < 0.001 &&
                std::abs(motionEnd.combatants.front().forwardSpeed - 500.0) < 0.001,
            "campaign one-second movement no longer matches standalone prototype tuning");
        victoryParams.deterministicCombatRuntimeEnabled = true;
        victoryParams.mechSystemsSnapshotEnabled = true;
        victoryParams.weaponRange = 50000.0;
        victoryParams.weaponCooldownTicks = 3;
        victoryParams.weaponDamagePerHit = 1;
        victoryParams.mechSystemMaxDamage = 3;
        const mw::battle::CampaignBattleOutcomePackage victory =
            runAdapter(victoryParams, playerFireReplay(), 120);
        expect(victory.valid && victory.terminal, "campaign victory adapter did not return a terminal package");
        expect(victory.launchSource == mw::battle::CampaignBattleLaunchSource::AcceptedContract &&
                   victory.outcome == mw::battle::CampaignBattleOutcome::Victory &&
                   victory.rawResultCode == 0 &&
                   victory.terminalTickIndex == 107 &&
                   victory.reason == "all_opposing_mechs_destroyed",
               "campaign victory adapter outcome changed");
        expect(victory.startCombatantCount == 2u && victory.finalCombatantCount == 2u,
               "campaign victory adapter combatant diagnostics changed");
        expect(victory.contractMetadataValid && victory.objectiveValid,
               "campaign victory adapter dropped contract/objective diagnostics");
        expect(victory.startSnapshotFingerprint != 0 && victory.finalSnapshotFingerprint != 0,
               "campaign victory adapter fingerprint diagnostics missing");

        const mw::battle::CampaignBattleOutcomePackage liveVictory =
            runLiveSessionAdapter(victoryParams, playerFireReplay(), 120);
        expect(liveVictory.outcome == victory.outcome &&
                   liveVictory.rawResultCode == victory.rawResultCode &&
                   liveVictory.terminalTickIndex == victory.terminalTickIndex &&
                   liveVictory.ticksExecuted == victory.ticksExecuted &&
                   liveVictory.finalSnapshotFingerprint == victory.finalSnapshotFingerprint,
               "campaign live battle session adapter diverged from runStandaloneBattle");

        const mw::battle::CampaignBattleOutcomePackage inputVictory =
            runInteractiveInputSessionAdapter(victoryParams, playerFireReplay(), 120);
        expect(inputVictory.outcome == victory.outcome &&
                   inputVictory.rawResultCode == victory.rawResultCode &&
                   inputVictory.terminalTickIndex == victory.terminalTickIndex &&
                   inputVictory.ticksExecuted == victory.ticksExecuted &&
                   inputVictory.finalSnapshotFingerprint == victory.finalSnapshotFingerprint,
               "campaign interactive input session diverged from runStandaloneBattle");

        mw::battle::BattleStartParams garrisonParams =
            campaignAcceptedParams(options, true, false);
        garrisonParams.deterministicCombatRuntimeEnabled = true;
        garrisonParams.mechSystemsSnapshotEnabled = true;
        garrisonParams.weaponRange = 50000.0;
        garrisonParams.weaponCooldownTicks = 3;
        garrisonParams.weaponDamagePerHit = 1;
        garrisonParams.mechSystemMaxDamage = 3;
        const mw::battle::CampaignBattleOutcomePackage garrisonVictory =
            runInteractiveInputSessionAdapter(garrisonParams, playerFireReplay(), 120);
        expect(garrisonVictory.outcome == mw::battle::CampaignBattleOutcome::Victory &&
                   garrisonVictory.finalCombatantCount == 2u,
               "campaign non-hostile contract opposition roster was dropped");

        const std::filesystem::path sortedOriginalFilesRoot =
            options.snarioPath.parent_path().parent_path();
        const CampaignPresentationProbe presentationProbe =
            probeCampaignPresentationHandoff(victoryParams, sortedOriginalFilesRoot);
        const CampaignRenderInterpolationProbe interpolationProbe =
            probeCampaignRenderInterpolation(victoryParams, sortedOriginalFilesRoot);
        const CampaignCockpitMotionProbe cockpitMotionProbe =
            probeCampaignCockpitMotion(victoryParams, sortedOriginalFilesRoot);
        const std::filesystem::path flatOriginalFilesRoot =
            sortedOriginalFilesRoot.parent_path() / "Original";
        expect(
            std::filesystem::exists(flatOriginalFilesRoot / "MW_1PICS.BIN") &&
                std::filesystem::exists(flatOriginalFilesRoot / "TERPCK.TBL") &&
                std::filesystem::exists(flatOriginalFilesRoot / "LIGHT.SCR"),
            "flat original installation fixture missing");
        const CampaignPresentationProbe flatPresentationProbe =
            probeCampaignPresentationHandoff(victoryParams, flatOriginalFilesRoot);
        const CampaignCockpitMotionProbe flatCockpitMotionProbe =
            probeCampaignCockpitMotion(victoryParams, flatOriginalFilesRoot);
        expect(presentationProbe.snapshotOwned &&
                   presentationProbe.combatants == 2u,
               "campaign presentation handoff lost snapshot-owned roster/objective/camera data");
        expect(presentationProbe.renderSceneReady &&
                   presentationProbe.renderResourcesPresent &&
                   presentationProbe.objectiveSeparate &&
                   presentationProbe.cockpitResolved &&
                   presentationProbe.renderVisuals == 2u &&
                   presentationProbe.projectileVisualsReady &&
                   presentationProbe.projectileInterpolationReady &&
                   presentationProbe.immediateBeamVisualsReady &&
                   presentationProbe.machineGunFlashVisualsReady &&
                   presentationProbe.machineGunImpactVisualsReady &&
                   presentationProbe.impactVisualsReady &&
                   presentationProbe.projectileVisuals == 3u &&
                   presentationProbe.projectileVertices == 528u &&
                   presentationProbe.projectileTriangles == 134u &&
                   presentationProbe.terrainTiles == 4u &&
                   presentationProbe.glResourcesReady &&
                   presentationProbe.farGroundApronReady &&
                   presentationProbe.externalCameraReady &&
                   presentationProbe.glTerrainVertices == 16356u &&
                   presentationProbe.glTerrainObjects == 14u &&
                   presentationProbe.glTerrainObjectRecords == 10u &&
                   presentationProbe.glTerrainObjectVertices == 1022u &&
                   presentationProbe.glTerrainPaletteLoaded &&
                   presentationProbe.glTerrainPaletteId == "green" &&
                   presentationProbe.glTerrainObjectDitherVertices > 0u &&
                   presentationProbe.glTerrainObjectDitherTriangles > 0u &&
                   presentationProbe.glTerrainEnvironmentPalettesReady &&
                   presentationProbe.glVisuals == 2u &&
                   presentationProbe.glObjectiveLoaded &&
                   presentationProbe.cockpitBackdropReady &&
                   presentationProbe.cockpitBackdropCodec == 2 &&
                   presentationProbe.cockpitViewportWidth == 297 &&
                   presentationProbe.cockpitViewportHeight == 92 &&
                   presentationProbe.cockpitWeaponPanelLayoutsReady &&
                   presentationProbe.cockpitTargetScanReady &&
                   presentationProbe.mechHitProfilesReady &&
                   presentationProbe.mechHitProfileTriangles > 0u &&
                   presentationProbe.cockpitDynamicLayersReady &&
                   presentationProbe.cockpitStruts == 12u &&
                   presentationProbe.cockpitWidgets == 12u &&
                   presentationProbe.cockpitHudNumbers == 12u &&
                   presentationProbe.cockpitFontWidth == 6 &&
                   presentationProbe.cockpitFontHeight == 6 &&
                   presentationProbe.lightStrutPlacements == 6u &&
                   presentationProbe.mediumStrutPlacements == 5u &&
                   presentationProbe.heavyStrutPlacements == 3u &&
                   presentationProbe.battleStatusBmpsReady &&
                   presentationProbe.battleStatusBmps == 8u &&
                   presentationProbe.battleStatusBlackArmorPixels > 0u &&
                   presentationProbe.battleStatusCyanOutlinePixels > 0u,
               "campaign renderer bridge did not resolve the snapshot-owned battle scene");
        expect(presentationProbe.cockpitGaugesReady,
               "campaign cockpit gauge geometry or fill state changed");
        expect(
            flatPresentationProbe.renderSceneReady &&
                flatPresentationProbe.renderResourcesPresent &&
                flatPresentationProbe.glResourcesReady &&
                flatPresentationProbe.cockpitBackdropReady &&
                flatPresentationProbe.cockpitDynamicLayersReady &&
                flatPresentationProbe.missionStatusMapReady &&
                flatPresentationProbe.cockpitCommandMapReady &&
                flatPresentationProbe.cockpitMinimapReady &&
                flatPresentationProbe.cockpitRadarReady &&
                flatPresentationProbe.cockpitTargetScanReady &&
                flatPresentationProbe.battleStatusBmpsReady &&
                flatCockpitMotionProbe.locustHeight == cockpitMotionProbe.locustHeight &&
                flatCockpitMotionProbe.phoenixHawkHeight ==
                    cockpitMotionProbe.phoenixHawkHeight &&
                flatCockpitMotionProbe.battleMasterHeight ==
                    cockpitMotionProbe.battleMasterHeight,
            "flat beside-executable original resource layout diverged from sorted layout");
        expect(presentationProbe.cockpitHudColors == 17u &&
                   std::string(mw::presentation::campaignCockpitHudColor(3u).id) == "cyan",
               "campaign cockpit HUD color cycle changed");
        expect(presentationProbe.cockpitMaxZoom == 3,
               "campaign cockpit zoom range changed");
        expect(presentationProbe.cockpitCompassRightTurn,
               "campaign cockpit compass right-turn sign changed");
        expect(presentationProbe.cockpitViewportHudReady,
               "campaign cockpit viewport HUD geometry changed");
        expect(presentationProbe.destroyedSensorReticleSuppressed,
               "destroyed sensors did not suppress the cockpit reticle render handoff");
        expect(presentationProbe.reactorShutdownHandoffReady,
               "reactor shutdown warning render handoff changed");
        expect(presentationProbe.externalCameraReady,
               "campaign full-screen external orbit camera controls changed");
        expect(presentationProbe.missionStatusMapReady &&
                   presentationProbe.missionStatusBackdropCodec == 2 &&
                   presentationProbe.cockpitCommandMapReady &&
                   presentationProbe.cockpitCommandMapBackdropCodec == 2 &&
                   presentationProbe.cockpitMinimapReady &&
                    presentationProbe.cockpitRadarReady &&
                    presentationProbe.cockpitMinimapTerrainSamples > 0u &&
                    presentationProbe.cockpitMinimapSurfacePlacements > 0u &&
                    presentationProbe.cockpitMinimapMountainPlacements > 0u,
               "campaign standalone start map or cockpit minimap contract changed");
        expect(interpolationProbe.valid && interpolationProbe.interpolatedVisuals == 2u,
               "campaign render interpolation or heading wraparound contract changed");
        expect(cockpitMotionProbe.valid,
               "campaign catalog cockpit height or presentation-only walk bob changed");

        mw::battle::BattleStartParams defeatParams = campaignAcceptedParams(options, true);
        defeatParams.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::Phase10CompatibilityFsm;
        defeatParams.deterministicCombatRuntimeEnabled = true;
        defeatParams.mechSystemsSnapshotEnabled = true;
        defeatParams.weaponRange = 700.0;
        defeatParams.enemyAttackRange = 650.0;
        defeatParams.weaponCooldownTicks = 3;
        defeatParams.weaponDamagePerHit = 1;
        defeatParams.mechSystemMaxDamage = 3;
        defeatParams.combatantLaunchStates[0].startTransform =
            mw::battle::Transform{
                defeatParams.playerStartTransform.x + 200.0,
                0.0,
                defeatParams.playerStartTransform.z,
                0.0};
        const mw::battle::CampaignBattleOutcomePackage defeat =
            runAdapter(defeatParams, mw::battle::BattleReplay{}, 120);
        expect(defeat.outcome == mw::battle::CampaignBattleOutcome::Defeat &&
                   defeat.rawResultCode == 1 &&
                   defeat.terminalTickIndex == 107 &&
                   defeat.reason == "player_mech_destroyed",
               "campaign defeat adapter outcome changed");
        const mw::battle::CampaignBattleOutcomePackage liveDefeat =
            runLiveSessionAdapter(defeatParams, mw::battle::BattleReplay{}, 120);
        expect(liveDefeat.outcome == defeat.outcome &&
                   liveDefeat.rawResultCode == defeat.rawResultCode &&
                   liveDefeat.terminalTickIndex == defeat.terminalTickIndex,
               "campaign live defeat session diverged from adapter");

        mw::battle::BattleStartParams withdrawParams = campaignAcceptedParams(options, false);
        withdrawParams.playerStartTransform =
            mw::battle::Transform{1.0, 0.0, 20000.0, -1.5707963267948966};
        const mw::battle::CampaignBattleOutcomePackage withdraw =
            runAdapter(withdrawParams, westExitReplay(), 4);
        expect(withdraw.outcome == mw::battle::CampaignBattleOutcome::Withdraw &&
                   withdraw.rawResultCode == 2 &&
                   withdraw.terminalTickIndex == 1 &&
                   withdraw.reason == "player_ordinary_boundary_exit",
               "campaign withdraw adapter outcome changed");
        expect(mw::battle::campaignBattleOutcomeIsMissionFailure(defeat.outcome) &&
                   mw::battle::campaignBattleOutcomeIsMissionFailure(withdraw.outcome) &&
                   !mw::battle::campaignBattleOutcomeIsMissionFailure(victory.outcome) &&
                   !mw::battle::campaignBattleOutcomeIsMissionFailure(
                       mw::battle::CampaignBattleOutcome::Unsupported),
               "campaign mission-failure classification changed");
        const mw::battle::CampaignBattleOutcomePackage liveWithdraw =
            runInteractiveInputSessionAdapter(withdrawParams, westExitReplay(), 4);
        expect(liveWithdraw.outcome == withdraw.outcome &&
                   liveWithdraw.rawResultCode == withdraw.rawResultCode &&
                   liveWithdraw.terminalTickIndex == withdraw.terminalTickIndex,
               "campaign live withdraw session diverged from adapter");

        const mw::battle::CampaignBattleOutcomePackage unsupported =
            runAdapter(campaignAcceptedParams(options, false), mw::battle::BattleReplay{}, 1);
        expect(unsupported.outcome == mw::battle::CampaignBattleOutcome::Unsupported &&
                   unsupported.reason == "battle_runtime_no_terminal_result",
               "campaign unsupported adapter outcome changed");
        const mw::battle::CampaignBattleOutcomePackage liveUnsupported =
            runLiveSessionAdapter(
                campaignAcceptedParams(options, true),
                mw::battle::BattleReplay{},
                1);
        expect(liveUnsupported.outcome == unsupported.outcome &&
                   !liveUnsupported.terminal &&
                   liveUnsupported.ticksExecuted == 1u,
               "campaign live unsupported session diverged from adapter");

        expect(!mw::battle::campaignBattleDiagnosticTickLimitReached(
                   std::nullopt,
                   900u) &&
                   !mw::battle::campaignBattleDiagnosticTickLimitReached(
                       std::nullopt,
                       900000u) &&
                   !mw::battle::campaignBattleDiagnosticTickLimitReached(900u, 899u) &&
                   mw::battle::campaignBattleDiagnosticTickLimitReached(900u, 900u),
               "campaign live/diagnostic tick-limit contract changed");
        mw::battle::BattleWorld noTimeoutWorld = mw::battle::BattleWorld::create(
            campaignAcceptedParams(options, false));
        for (uint64_t tick = 0; tick < 901u; ++tick) {
            noTimeoutWorld.tick();
        }
        expect(noTimeoutWorld.tickIndex() == 901u &&
                   !noTimeoutWorld.battleResult().has_value(),
               "campaign battle core gained a 900-tick mission timeout");
        const mw::mech3d::ModelAnimationDefinition longRunWalk =
            mw::mech3d::makeCatalogAnimationDefinition("locust", "walk");
        expect(!longRunWalk.sequences.empty(),
               "campaign long-run animation probe has no sequence");
        const uint64_t longRunElapsedMs = (1ull << 40u) + 1234567u;
        const int longRunFrameDurationMs = longRunWalk.sequences.front().frameDurationMs;
        const int longRunFrameCount = mw::mech3d::animationFrameCount(longRunWalk);
        const int expectedLongRunFrame = static_cast<int>(
            (longRunElapsedMs / static_cast<uint64_t>(longRunFrameDurationMs)) %
            static_cast<uint64_t>(longRunFrameCount));
        expect(mw::mech3d::animationFrameForElapsedMs(
                   longRunWalk,
                   longRunElapsedMs) == expectedLongRunFrame,
               "campaign animation phase retained a 32-bit elapsed-time limit");

        const std::array<size_t, 20>& scenarioIndices =
            mw::battle::campaignBattleTerrainScenarioIndices();
        expect(scenarioIndices == std::array<size_t, 20>{{
                   0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u,
                   10u, 11u, 12u, 13u, 14u, 15u, 16u, 17u, 18u, 19u,
               }},
               "campaign BTECH modulo-20 SNARIO seed set changed");
        for (size_t i = 0; i < scenarioIndices.size(); ++i) {
            const mw::battle::OriginalBattlefieldSetup activeSetup =
                mw::battle::decodeOriginalBattlefieldSetup(
                    options.snarioPath,
                    scenarioIndices[i],
                    victoryParams.mission.originalId,
                    500.0);
            expect(activeSetup.scenarioIndex == scenarioIndices[i] &&
                       activeSetup.metadata.valid && activeSetup.battlefieldBoundary.valid,
                   "campaign modulo-20 SNARIO setup changed");
        }
        expect(
            mw::battle::campaignBattleTerrainScenarioIndexFromTargetPlanetTableOrder(20u) == 0u &&
                mw::battle::campaignBattleTerrainScenarioIndexFromTargetPlanetTableOrder(112u) == 12u &&
                mw::battle::campaignBattleTerrainScenarioIndexFromTargetPlanetTableOrder(145u) == 5u,
            "campaign MW_MAIN target-planet to BTECH scenario handoff changed");
        const mw::battle::OriginalBattlefieldSetup scenario2Setup =
            mw::battle::decodeOriginalBattlefieldSetup(
                options.snarioPath,
                scenarioIndices[0],
                victoryParams.mission.originalId,
                500.0);
        const mw::battle::OriginalBattlefieldSetup scenario3Setup =
            mw::battle::decodeOriginalBattlefieldSetup(
                options.snarioPath,
                scenarioIndices[1],
                victoryParams.mission.originalId,
                500.0);
        expect(
            scenario2Setup.playerStartTransform.x != scenario3Setup.playerStartTransform.x ||
                scenario2Setup.playerStartTransform.z != scenario3Setup.playerStartTransform.z ||
                scenario2Setup.enemyStartTransform.x != scenario3Setup.enemyStartTransform.x ||
                scenario2Setup.enemyStartTransform.z != scenario3Setup.enemyStartTransform.z ||
                scenario2Setup.objective.transform.x != scenario3Setup.objective.transform.x ||
                scenario2Setup.objective.transform.z != scenario3Setup.objective.transform.z,
            "campaign SNARIO scenario selection no longer changes placement banks");

        expect(
            mw::presentation::campaignBattleMapPlayerBlipBgra(
                mw::battle::CampaignBattlePresentationMode::CockpitCommandMap,
                2) == 0xff000000u &&
                mw::presentation::campaignBattleMapPlayerBlipBgra(
                    mw::battle::CampaignBattlePresentationMode::CockpitCommandMap,
                    1) == 0xffffffffu &&
                mw::presentation::campaignBattleMapPlayerBlipBgra(
                    mw::battle::CampaignBattlePresentationMode::MissionStatus,
                    2) == 0xffffffffu,
            "campaign snow command-map player contrast changed");
        const auto playerMarker =
            mw::presentation::campaignBattleMapPlayerMarker(
                0,
                mw::battle::CampaignBattlePresentationMode::MissionStatus,
                1);
        const auto firstAllyMarker =
            mw::presentation::campaignBattleMapPlayerMarker(
                1,
                mw::battle::CampaignBattlePresentationMode::MissionStatus,
                1);
        const auto secondAllyMarker =
            mw::presentation::campaignBattleMapPlayerMarker(
                2,
                mw::battle::CampaignBattlePresentationMode::CockpitCommandMap,
                1);
        const auto thirdAllyMarker =
            mw::presentation::campaignBattleMapPlayerMarker(
                3,
                mw::battle::CampaignBattlePresentationMode::CockpitCommandMap,
                1);
        expect(
            mw::presentation::campaignBattlePlayerLanceSlot("player:0", true) == 0 &&
                mw::presentation::campaignBattlePlayerLanceSlot("player:1", false) == 1 &&
                mw::presentation::campaignBattlePlayerLanceSlot("player:2", false) == 2 &&
                mw::presentation::campaignBattlePlayerLanceSlot("player:3", false) == 3 &&
                playerMarker.shape ==
                    mw::presentation::CampaignBattleMapPlayerMarkerShape::Square &&
                playerMarker.bgra == 0xffffffffu &&
                playerMarker.egaIndex == 15u &&
                playerMarker.pixels == std::array<uint8_t, 9>{{
                    1, 1, 1, 1, 1, 1, 1, 1, 1}} &&
                firstAllyMarker.shape ==
                    mw::presentation::CampaignBattleMapPlayerMarkerShape::H &&
                firstAllyMarker.bgra == 0xff5555ffu &&
                firstAllyMarker.egaIndex == 9u &&
                firstAllyMarker.pixels == std::array<uint8_t, 9>{{
                    1, 0, 1, 1, 1, 1, 1, 0, 1}} &&
                secondAllyMarker.shape ==
                    mw::presentation::CampaignBattleMapPlayerMarkerShape::RotatedH &&
                secondAllyMarker.bgra == 0xff55ffffu &&
                secondAllyMarker.egaIndex == 11u &&
                secondAllyMarker.pixels == std::array<uint8_t, 9>{{
                    1, 1, 1, 0, 1, 0, 1, 1, 1}} &&
                thirdAllyMarker.shape ==
                    mw::presentation::CampaignBattleMapPlayerMarkerShape::Plus &&
                thirdAllyMarker.bgra == 0xff00aa00u &&
                thirdAllyMarker.egaIndex == 2u &&
                thirdAllyMarker.contrastOutline &&
                thirdAllyMarker.pixels == std::array<uint8_t, 9>{{
                    0, 1, 0, 1, 1, 1, 0, 1, 0}} &&
                mw::presentation::campaignCockpitMinimapPlayerBlipBgra(true) ==
                    0xff000000u &&
                mw::presentation::campaignCockpitMinimapPlayerBlipBgra(false) ==
                    0xffffffffu,
            "campaign player lance map marker contract changed");

        const mw::battle::BattleStartParams finalParams =
            campaignDarkWingFinalParams(options);
        const mw::battle::BattleSnapshot finalStart =
            mw::battle::BattleWorld::create(finalParams).missionStartSnapshot();
        expect(finalStart.mission.originalId == 12u &&
                   finalStart.setup.missionSelector == 12u &&
                   finalStart.oppositionRoster.initialMissionSelector == 12u,
               "campaign Dark Wing final selector changed");
        expect(finalStart.combatants.size() == 5u && !finalStart.objective.valid,
               "campaign Dark Wing final snapshot ownership changed");
        expect(finalStart.combatants[1].mechPresetId == "battlemaster" &&
                   finalStart.combatants[2].mechPresetId == "battlemaster" &&
                   finalStart.combatants[3].mechPresetId == "battlemaster" &&
                   finalStart.combatants[4].mechPresetId == "warhammer",
               "campaign Dark Wing final ordered roster changed");
        for (size_t slot = 0; slot < 4u; ++slot) {
            expect(finalStart.combatants[slot + 1u].roster.sourceSlot ==
                       "opposing:" + std::to_string(slot),
                   "campaign Dark Wing final source slot changed");
        }
        const mw::battle::CampaignBattleOutcomePackage finalOutcome =
            runLiveSessionAdapter(finalParams, mw::battle::BattleReplay{}, 1);
        expect(finalOutcome.outcome == mw::battle::CampaignBattleOutcome::Unsupported &&
                   finalOutcome.launchSource == mw::battle::CampaignBattleLaunchSource::DarkWingFinal &&
                   !finalOutcome.terminal &&
                   finalOutcome.ticksExecuted == 1u &&
                   finalOutcome.finalCombatantCount == 5u &&
                   !finalOutcome.contractMetadataValid &&
                   !finalOutcome.objectiveValid,
               "campaign Dark Wing final launch/result handoff changed");
        const mw::battle::CampaignBattlePostResultReceipt finalReceipt =
            mw::battle::campaignBattlePostResultReceiptFromOutcome(
                finalOutcome,
                true);
        expect(finalReceipt.valid &&
                   finalReceipt.launchSource ==
                       mw::battle::CampaignBattleLaunchSource::DarkWingFinal &&
                   finalReceipt.destination ==
                       mw::battle::CampaignBattlePostResultDestination::CampaignMainMenu &&
                   finalReceipt.outcomeAcknowledged &&
                   finalReceipt.safeCampaignReturn &&
                   finalReceipt.persistentStateUnchanged &&
                   !finalReceipt.settlementCommitted &&
                   !finalReceipt.extendedEndingSequenceExecuted &&
                   finalReceipt.extendedEndingSequenceDeferred &&
                   finalReceipt.outcomeReason == finalOutcome.reason &&
                   finalReceipt.terminal == finalOutcome.terminal &&
                   finalReceipt.rawResultCode == finalOutcome.rawResultCode &&
                   finalReceipt.finalSnapshotFingerprint ==
                       finalOutcome.finalSnapshotFingerprint &&
                   finalReceipt.terminalTickIndex == finalOutcome.terminalTickIndex &&
                   finalReceipt.terminalElapsedMs == finalOutcome.terminalElapsedMs &&
                   finalReceipt.ticksExecuted == finalOutcome.ticksExecuted &&
                   finalReceipt.reason ==
                       "dark_wing_final_safe_return_extended_ending_deferred",
               "campaign Dark Wing final post-result receipt changed");
        const mw::battle::CampaignBattlePostResultReceipt acceptedReceipt =
            mw::battle::campaignBattlePostResultReceiptFromOutcome(victory, true);
        expect(acceptedReceipt.valid && acceptedReceipt.safeCampaignReturn &&
                   !acceptedReceipt.settlementCommitted &&
                   !acceptedReceipt.extendedEndingSequenceExecuted &&
                   !acceptedReceipt.extendedEndingSequenceDeferred,
               "accepted-contract post-result receipt changed");
        mw::battle::CampaignBattleConsequenceContext consequenceContext;
        consequenceContext.salvage = 22500;
        consequenceContext.campaignDateTicks = 72;
        consequenceContext.pilotExperienceAward = true;
        consequenceContext.salvageBetaValidationRequired = true;
        consequenceContext.salvageEstimateAvailable = true;
        consequenceContext.campaignTimeProven = true;
        consequenceContext.pilotExperienceProven = true;
        expect(
            mw::battle::campaignBattleOriginalMissionDurationDateTicks(1) == 360 &&
                mw::battle::campaignBattleOriginalMissionDurationDateTicks(2) == 60 &&
                mw::battle::campaignBattleOriginalMissionDurationDateTicks(11) == 2 &&
                mw::battle::campaignBattleOriginalMissionDurationDateTicks(36) == 2 &&
                mw::battle::campaignBattleOriginalMissionDurationDateTicks(37) == 0 &&
                mw::battle::campaignBattleOriginalMissionElapsedDateTicks(2, 2, 3, 11) == 72 &&
                mw::battle::campaignBattleOriginalMissionElapsedDateTicks(0, 0, 0, 11) == 30,
            "original campaign mission time contract changed");
        const mw::battle::CampaignPilotExperienceState poorBeforePromotion =
            mw::battle::campaignPilotExperienceAfterSurvivedMission(0, 1, false);
        const mw::battle::CampaignPilotExperienceState poorPromotion =
            mw::battle::campaignPilotExperienceAfterSurvivedMission(0, 2, false);
        const mw::battle::CampaignPilotExperienceState averagePromotion =
            mw::battle::campaignPilotExperienceAfterSurvivedMission(1, 9, false);
        const mw::battle::CampaignPilotExperienceState goodPromotion =
            mw::battle::campaignPilotExperienceAfterSurvivedMission(2, 14, false);
        const mw::battle::CampaignPilotExperienceState hiredMaximum =
            mw::battle::campaignPilotExperienceAfterSurvivedMission(3, 27, false);
        const mw::battle::CampaignPilotExperienceState commanderMaximum =
            mw::battle::campaignPilotExperienceAfterSurvivedMission(3, 27, true);
        expect(
            poorBeforePromotion.skill == 0 && poorBeforePromotion.missionCounter == 2 &&
                !poorBeforePromotion.promoted &&
                poorPromotion.skill == 1 && poorPromotion.missionCounter == 0 &&
                poorPromotion.promoted &&
                averagePromotion.skill == 2 && averagePromotion.missionCounter == 0 &&
                averagePromotion.promoted &&
                goodPromotion.skill == 3 && goodPromotion.missionCounter == 0 &&
                goodPromotion.promoted &&
                hiredMaximum.skill == 3 && hiredMaximum.missionCounter == 0 &&
                !hiredMaximum.promoted &&
                commanderMaximum.skill == 3 && commanderMaximum.missionCounter == 28 &&
                !commanderMaximum.promoted,
            "original campaign pilot XP thresholds changed");
        const mw::battle::CampaignBattleConsequencePlan victoryConsequencePlan =
            mw::battle::campaignBattleConsequencePlanFromOutcome(
                victory,
                victoryParams.contract,
                consequenceContext);
        const mw::battle::CampaignBattleConsequencePlan repeatedVictoryConsequencePlan =
            mw::battle::campaignBattleConsequencePlanFromOutcome(
                victory,
                victoryParams.contract,
                consequenceContext);
        expect(victoryConsequencePlan.valid &&
                   victoryConsequencePlan.commitEligible &&
                   victoryConsequencePlan.payment == 150000u &&
                   victoryConsequencePlan.salvage == 22500u &&
                   victoryConsequencePlan.reputationDelta == 1 &&
                   victoryConsequencePlan.housePositiveDelta[4] == 2 &&
                   victoryConsequencePlan.houseNegativeDelta[3] == 2 &&
                   victoryConsequencePlan.campaignDateTicks == 72u &&
                   victoryConsequencePlan.pilotExperienceAward &&
                   victoryConsequencePlan.salvageBetaValidationRequired &&
                   !victoryConsequencePlan.salvageDeferred &&
                   !victoryConsequencePlan.campaignTimeDeferred &&
                   !victoryConsequencePlan.pilotExperienceDeferred &&
                   victoryConsequencePlan.persistentMechDamageDeferred &&
                   victoryConsequencePlan.repairCostDeferred &&
                   victoryConsequencePlan.pilotDeathDeferred &&
                   victoryConsequencePlan.planFingerprint != 0 &&
                   repeatedVictoryConsequencePlan.planFingerprint ==
                       victoryConsequencePlan.planFingerprint,
               "accepted-contract victory consequence plan changed");
        const mw::battle::CampaignBattleConsequencePlan defeatConsequencePlan =
            mw::battle::campaignBattleConsequencePlanFromOutcome(
                defeat,
                defeatParams.contract,
                consequenceContext);
        const mw::battle::CampaignBattleConsequencePlan withdrawConsequencePlan =
            mw::battle::campaignBattleConsequencePlanFromOutcome(
                withdraw,
                withdrawParams.contract,
                consequenceContext);
        const mw::battle::CampaignBattleConsequencePlan unsupportedConsequencePlan =
            mw::battle::campaignBattleConsequencePlanFromOutcome(
                unsupported,
                victoryParams.contract);
        expect(defeatConsequencePlan.commitEligible &&
                   defeatConsequencePlan.payment == 0u &&
                   defeatConsequencePlan.salvage == 0u &&
                   defeatConsequencePlan.campaignDateTicks == 72u &&
                   defeatConsequencePlan.pilotExperienceAward &&
                   defeatConsequencePlan.reputationDelta == 0 &&
                   defeatConsequencePlan.houseNegativeDelta[4] == 1 &&
                   defeatConsequencePlan.houseNegativeDelta[3] == 2 &&
                   withdrawConsequencePlan.commitEligible &&
                   withdrawConsequencePlan.salvage == 0u &&
                   withdrawConsequencePlan.campaignDateTicks == 72u &&
                   withdrawConsequencePlan.pilotExperienceAward &&
                   withdrawConsequencePlan.houseNegativeDelta ==
                       defeatConsequencePlan.houseNegativeDelta &&
                   !unsupportedConsequencePlan.commitEligible,
               "accepted-contract failure consequence matrix changed");
        const uint64_t consequenceStateFingerprint = 0x123456789abcdef0ull;
        const mw::battle::CampaignBattleConsequenceApplyGuard allowedConsequenceGuard =
            mw::battle::campaignBattleConsequencePlanApplyGuard(
                victoryConsequencePlan,
                consequenceStateFingerprint,
                consequenceStateFingerprint,
                0);
        const mw::battle::CampaignBattleConsequenceApplyGuard mismatchedConsequenceGuard =
            mw::battle::campaignBattleConsequencePlanApplyGuard(
                victoryConsequencePlan,
                consequenceStateFingerprint,
                consequenceStateFingerprint + 1u,
                0);
        const mw::battle::CampaignBattleConsequenceApplyGuard duplicateConsequenceGuard =
            mw::battle::campaignBattleConsequencePlanApplyGuard(
                victoryConsequencePlan,
                consequenceStateFingerprint,
                consequenceStateFingerprint,
                victoryConsequencePlan.planFingerprint);
        expect(allowedConsequenceGuard.valid && allowedConsequenceGuard.allowed &&
                   allowedConsequenceGuard.preconditionMatched &&
                   !mismatchedConsequenceGuard.allowed &&
                   !mismatchedConsequenceGuard.preconditionMatched &&
                   !duplicateConsequenceGuard.allowed &&
                   duplicateConsequenceGuard.duplicateRejected,
               "campaign consequence atomic/idempotence guard changed");
        mw::battle::CampaignBattleConsequenceCommitReceipt committedConsequences;
        committedConsequences.valid = true;
        committedConsequences.attempted = true;
        committedConsequences.committed = true;
        committedConsequences.verified = true;
        committedConsequences.saveRoundTripVerified = true;
        committedConsequences.deferredConsequencesPresent = true;
        committedConsequences.planFingerprint = victoryConsequencePlan.planFingerprint;
        committedConsequences.beforeStateFingerprint = consequenceStateFingerprint;
        committedConsequences.afterStateFingerprint = consequenceStateFingerprint + 1u;
        const mw::battle::CampaignBattlePostResultReceipt committedReceipt =
            mw::battle::campaignBattlePostResultReceiptFromOutcome(
                victory,
                false,
                committedConsequences);
        expect(committedReceipt.valid && committedReceipt.safeCampaignReturn &&
                   !committedReceipt.persistentStateUnchanged &&
                   committedReceipt.settlementCommitted &&
                   committedReceipt.deferredConsequencesPresent &&
                   committedReceipt.consequenceSaveRoundTripVerified &&
                   committedReceipt.consequencePlanFingerprint ==
                       victoryConsequencePlan.planFingerprint &&
                   committedReceipt.reason ==
                   "accepted_contract_proven_consequences_committed_deferred_persistence",
               "campaign committed consequence receipt changed");
        const mw::battle::BattleSnapshot persistenceSnapshot =
            mw::battle::BattleWorld::create(victoryParams).snapshot();
        const std::vector<mw::battle::CampaignBattlePersistentRosterBinding>
            persistenceBindings{{"player:0", 2, 1}};
        const mw::battle::CampaignBattlePersistenceReport persistenceReport =
            mw::battle::campaignBattlePersistenceReportFromSnapshot(
                persistenceSnapshot,
                persistenceBindings,
                1u);
        const mw::battle::CampaignBattlePersistenceReport repeatedPersistenceReport =
            mw::battle::campaignBattlePersistenceReportFromSnapshot(
                persistenceSnapshot,
                persistenceBindings,
                1u);
        expect(persistenceReport.valid && persistenceReport.allBindingsMapped &&
                   persistenceReport.allMissionParticipantsLaunched &&
                   persistenceReport.requestedBindingCount == 1u &&
                   persistenceReport.mappedBindingCount == 1u &&
                   persistenceReport.unmappedBindingCount == 0u &&
                   persistenceReport.unboundPlayerCombatantCount == 0u &&
                   persistenceReport.combatants.size() == 1u &&
                   persistenceReport.combatants.front().sourceSlot == "player:0" &&
                   persistenceReport.combatants.front().ownedMechIndex == 2 &&
                   persistenceReport.combatants.front().crewSlot == 1 &&
                   persistenceReport.combatants.front().mechSystems.valid &&
                   persistenceReport.persistentDamageTranslationDeferred &&
                   persistenceReport.repairCostTranslationDeferred &&
                   persistenceReport.pilotSurvivalTranslationDeferred &&
                   persistenceReport.reportFingerprint != 0 &&
                   repeatedPersistenceReport.reportFingerprint ==
                       persistenceReport.reportFingerprint,
               "campaign persistence roster binding report changed");
        const mw::battle::CampaignBattlePersistenceReport unlaunchedPersistenceReport =
            mw::battle::campaignBattlePersistenceReportFromSnapshot(
                persistenceSnapshot,
                persistenceBindings,
                4u);
        const mw::battle::CampaignBattlePersistenceReport unmappedPersistenceReport =
            mw::battle::campaignBattlePersistenceReportFromSnapshot(
                persistenceSnapshot,
                {{"player:1", 3, 2}},
                1u);
        const mw::battle::CampaignBattlePersistenceReport duplicatePersistenceReport =
            mw::battle::campaignBattlePersistenceReportFromSnapshot(
                persistenceSnapshot,
                {{"player:0", 2, 1}, {"player:0", 3, 2}},
                2u);
        expect(unlaunchedPersistenceReport.valid &&
                   unlaunchedPersistenceReport.allBindingsMapped &&
                   !unlaunchedPersistenceReport.allMissionParticipantsLaunched &&
                   unlaunchedPersistenceReport.unlaunchedMissionParticipantCount == 3u &&
                   unmappedPersistenceReport.valid &&
                   !unmappedPersistenceReport.allBindingsMapped &&
                   unmappedPersistenceReport.unmappedBindingCount == 1u &&
                   unmappedPersistenceReport.unboundPlayerCombatantCount == 1u &&
                   !duplicatePersistenceReport.valid &&
                   duplicatePersistenceReport.duplicateBindingCount == 1u,
               "campaign persistence incomplete/duplicate guards changed");
        const mw::battle::CampaignBattlePersistentDamageTranslationPlan
            intactDamageTranslationPlan =
                mw::battle::campaignBattlePersistentDamageTranslationPlanFromReport(
                    persistenceReport);
        const mw::battle::CampaignBattlePersistentDamageTranslationPlan
            repeatedIntactDamageTranslationPlan =
                mw::battle::campaignBattlePersistentDamageTranslationPlanFromReport(
                    persistenceReport);
        expect(intactDamageTranslationPlan.valid &&
                   intactDamageTranslationPlan.allBindingsMapped &&
                   intactDamageTranslationPlan.allMissionParticipantsLaunched &&
                   !intactDamageTranslationPlan.mutationEligible &&
                   intactDamageTranslationPlan.noMutationGuaranteed &&
                   intactDamageTranslationPlan.combatantCount == 1u &&
                   intactDamageTranslationPlan.persistentFieldCount == 29u &&
                   intactDamageTranslationPlan.exactPersistentFieldCount == 0u &&
                   intactDamageTranslationPlan.ambiguousPersistentFieldCount +
                           intactDamageTranslationPlan.missingSourcePersistentFieldCount ==
                       29u &&
                   intactDamageTranslationPlan.combatants.front().fields.size() == 12u &&
                   intactDamageTranslationPlan.planFingerprint != 0u &&
                   repeatedIntactDamageTranslationPlan.planFingerprint ==
                       intactDamageTranslationPlan.planFingerprint,
               "campaign intact persistent damage translation audit changed");

        mw::battle::BattleSnapshot degradedPersistenceSnapshot = persistenceSnapshot;
        bool degradedCore = false;
        for (mw::battle::CombatantSnapshot& combatant :
             degradedPersistenceSnapshot.combatants) {
            if (!(combatant.roster.team == mw::battle::BattleTeam::Player ||
                  combatant.playerControlled)) {
                continue;
            }
            for (mw::battle::BattleMechSystemSnapshot& system :
                 combatant.mechSystems.systems) {
                if (system.role == mw::battle::BattleMechSystemRole::Core) {
                    system.status = mw::battle::BattleMechSystemStatus::Degraded;
                    system.damage = std::max(1, system.damage);
                    degradedCore = true;
                }
            }
        }
        expect(degradedCore, "campaign persistence smoke has no player core system");
        const mw::battle::CampaignBattlePersistenceReport degradedPersistenceReport =
            mw::battle::campaignBattlePersistenceReportFromSnapshot(
                degradedPersistenceSnapshot,
                persistenceBindings,
                1u);
        const mw::battle::CampaignBattlePersistentDamageTranslationPlan
            degradedDamageTranslationPlan =
                mw::battle::campaignBattlePersistentDamageTranslationPlanFromReport(
                    degradedPersistenceReport);

        mw::battle::BattleSnapshot destroyedPersistenceSnapshot = persistenceSnapshot;
        for (mw::battle::CombatantSnapshot& combatant :
             destroyedPersistenceSnapshot.combatants) {
            if (!(combatant.roster.team == mw::battle::BattleTeam::Player ||
                  combatant.playerControlled)) {
                continue;
            }
            combatant.missionStatus = mw::battle::CombatantMissionStatus::Destroyed;
            combatant.mechDestroyed = true;
            combatant.mechSystems.wholeMechDestroyed = true;
            for (mw::battle::BattleMechSystemSnapshot& system :
                 combatant.mechSystems.systems) {
                system.status = mw::battle::BattleMechSystemStatus::Destroyed;
                system.damage = std::max(system.damage, system.maxDamage);
            }
        }
        const mw::battle::CampaignBattlePersistenceReport destroyedPersistenceReport =
            mw::battle::campaignBattlePersistenceReportFromSnapshot(
                destroyedPersistenceSnapshot,
                persistenceBindings,
                1u);
        const mw::battle::CampaignBattlePersistentDamageTranslationPlan
            destroyedDamageTranslationPlan =
                mw::battle::campaignBattlePersistentDamageTranslationPlanFromReport(
                    destroyedPersistenceReport);
        const mw::battle::CampaignBattlePersistentDamageTranslationPlan
            invalidDamageTranslationPlan =
                mw::battle::campaignBattlePersistentDamageTranslationPlanFromReport(
                    duplicatePersistenceReport);
        expect(degradedDamageTranslationPlan.valid &&
                   !degradedDamageTranslationPlan.mutationEligible &&
                   degradedDamageTranslationPlan.noMutationGuaranteed &&
                   degradedDamageTranslationPlan.persistentFieldCount == 29u &&
                   destroyedDamageTranslationPlan.valid &&
                   !destroyedDamageTranslationPlan.mutationEligible &&
                   destroyedDamageTranslationPlan.noMutationGuaranteed &&
                   destroyedDamageTranslationPlan.combatants.front().mechDestroyed &&
                   destroyedDamageTranslationPlan.persistentFieldCount == 29u &&
                   degradedDamageTranslationPlan.planFingerprint !=
                       intactDamageTranslationPlan.planFingerprint &&
                   destroyedDamageTranslationPlan.planFingerprint !=
                       degradedDamageTranslationPlan.planFingerprint &&
                   !invalidDamageTranslationPlan.valid &&
                   !invalidDamageTranslationPlan.mutationEligible &&
                   invalidDamageTranslationPlan.noMutationGuaranteed,
               "campaign degraded/destroyed persistent damage no-guess guard changed");

        mw::battle::BattleStartParams entryStateParams = victoryParams;
        mw::battle::BattlePersistentMechState damagedPlayerEntry;
        damagedPlayerEntry.valid = true;
        damagedPlayerEntry.provenance = "campaign_owned_mech_immutable_entry_state";
        damagedPlayerEntry.engine = 1u;
        damagedPlayerEntry.heatSinksWorking = 9u;
        damagedPlayerEntry.heatSinksTotal = 10u;
        damagedPlayerEntry.jumpJetsWorking = 2u;
        damagedPlayerEntry.jumpJetsTotal = 3u;
        damagedPlayerEntry.armorPercent = 92u;
        damagedPlayerEntry.weaponConditions[1] = 2u;
        damagedPlayerEntry.armorDamage[0] = 1u;
        damagedPlayerEntry.armorDamage[8] = 1u;
        mw::battle::BattleCombatantLaunchState controlledEntry;
        controlledEntry.mechPresetId = entryStateParams.playerMechPresetId;
        controlledEntry.startTransform = entryStateParams.playerStartTransform;
        controlledEntry.roster = entryStateParams.playerRoster;
        controlledEntry.persistentMechState = damagedPlayerEntry;
        entryStateParams.playerLaunchState = controlledEntry;
        entryStateParams.playerAlliedLaunchStates.clear();
        for (size_t allyIndex = 1u; allyIndex < 4u; ++allyIndex) {
            mw::battle::BattleCombatantLaunchState ally = controlledEntry;
            ally.startTransform.x += static_cast<double>(allyIndex) * 250.0;
            ally.roster.sourceSlot = "player:" + std::to_string(allyIndex);
            ally.roster.provenance = "campaign_assigned_lance_launch_state";
            ally.persistentMechState.engine = static_cast<uint8_t>(allyIndex % 2u);
            ally.persistentMechState.armorDamage[allyIndex] =
                static_cast<uint8_t>(allyIndex);
            entryStateParams.playerAlliedLaunchStates.push_back(std::move(ally));
        }
        mw::battle::BattlePersistentMechState pristineOpposition;
        pristineOpposition.valid = true;
        pristineOpposition.provenance = "campaign_opposition_pristine_launch_state";
        pristineOpposition.heatSinksWorking = 10u;
        pristineOpposition.heatSinksTotal = 10u;
        pristineOpposition.armorPercent = 100u;
        for (mw::battle::BattleCombatantLaunchState& enemy :
             entryStateParams.combatantLaunchStates) {
            enemy.persistentMechState = pristineOpposition;
        }
        mw::battle::BattleWorld entryStateWorld =
            mw::battle::BattleWorld::create(entryStateParams);
        const mw::battle::BattleSnapshot entryStateStart =
            entryStateWorld.missionStartSnapshot();
        const mw::battle::CampaignBattleRenderScenePackage entryStateMapScene =
            mw::battle::campaignBattleRenderSceneFromSnapshot(
                entryStateStart,
                mw::battle::CampaignBattlePresentationMode::MissionStatus,
                sortedOriginalFilesRoot);
        std::array<bool, 4> mappedPlayerSlots{};
        for (const mw::battle::CampaignBattleRenderVisualInstance& visual :
             entryStateMapScene.combatantVisuals) {
            const int playerSlot =
                mw::presentation::campaignBattlePlayerLanceSlot(
                    visual.rosterSourceSlot,
                    visual.playerControlled);
            if (visual.team == mw::battle::BattleTeam::Player &&
                playerSlot >= 0 &&
                static_cast<size_t>(playerSlot) < mappedPlayerSlots.size()) {
                mappedPlayerSlots[static_cast<size_t>(playerSlot)] = true;
            }
        }
        expect(
            entryStateMapScene.valid &&
                std::all_of(
                    mappedPlayerSlots.begin(),
                    mappedPlayerSlots.end(),
                    [](bool mapped) { return mapped; }),
            "campaign render scene lost player lance map identities");
        entryStateWorld.runTicks(2u);
        const mw::battle::BattleSnapshot entryStateFinal = entryStateWorld.snapshot();
        const mw::battle::CampaignBattleEntryMechStateGuard entryStateGuard =
            mw::battle::campaignBattleEntryMechStateGuardFromSnapshots(
                entryStateStart,
                entryStateFinal);
        expect(entryStateGuard.valid &&
                   entryStateGuard.playerStatesPreserved &&
                   entryStateGuard.opposingStatesPristine &&
                   entryStateGuard.allCombatantStatesPreserved &&
                   entryStateGuard.playerCombatantCount == 4u &&
                   entryStateGuard.opposingCombatantCount == 1u &&
                   entryStateGuard.preservedCombatantCount == 5u &&
                   entryStateGuard.guardFingerprint != 0u,
               "campaign lance entry-state roundtrip/pristine enemy guard changed");
        const mw::battle::CombatantSnapshot* controlledEntrySnapshot = nullptr;
        for (const mw::battle::CombatantSnapshot& combatant :
             entryStateStart.combatants) {
            if (combatant.playerControlled) {
                controlledEntrySnapshot = &combatant;
                break;
            }
        }
        expect(controlledEntrySnapshot != nullptr &&
                   mw::battle::battlePersistentMechStateFingerprint(
                       controlledEntrySnapshot->persistentMechState) ==
                       mw::battle::battlePersistentMechStateFingerprint(
                           damagedPlayerEntry) &&
                   !mw::battle::battlePersistentMechStateIsPristine(
                       controlledEntrySnapshot->persistentMechState),
               "campaign controlled mech damaged entry state changed");
        const mw::battle::BattleCombatantLaunchState carriedEntryState =
            mw::battle::prepareNextMissionLaunchState(
                *controlledEntrySnapshot,
                controlledEntrySnapshot->transform);
        expect(mw::battle::battlePersistentMechStateFingerprint(
                   carriedEntryState.persistentMechState) ==
                   mw::battle::battlePersistentMechStateFingerprint(
                       damagedPlayerEntry),
               "campaign persistent mech entry state carryover changed");
        mw::battle::BattleSnapshot nonPristineOppositionStart = entryStateStart;
        for (mw::battle::CombatantSnapshot& combatant :
             nonPristineOppositionStart.combatants) {
            if (combatant.roster.team == mw::battle::BattleTeam::Opposing) {
                combatant.persistentMechState.engine = 1u;
            }
        }
        const mw::battle::CampaignBattleEntryMechStateGuard
            nonPristineOppositionGuard =
                mw::battle::campaignBattleEntryMechStateGuardFromSnapshots(
                    nonPristineOppositionStart,
                    entryStateFinal);
        expect(nonPristineOppositionGuard.valid &&
                   !nonPristineOppositionGuard.opposingStatesPristine &&
                   nonPristineOppositionGuard.nonPristineOpposingCount == 1u,
               "campaign non-pristine opposition negative guard changed");
        const std::vector<const mw::battle::CampaignBattleOutcomePackage*>
            acceptedOutcomeMatrix{&victory, &defeat, &withdraw, &unsupported};
        expect(
            mw::battle::campaignBattleDebriefPresentationForOutcome(victory) ==
                    mw::battle::CampaignBattleDebriefPresentation::Victory &&
                mw::battle::campaignBattleDebriefPresentationForOutcome(defeat) ==
                    mw::battle::CampaignBattleDebriefPresentation::Defeat &&
                mw::battle::campaignBattleDebriefPresentationForOutcome(withdraw) ==
                    mw::battle::CampaignBattleDebriefPresentation::Defeat &&
                mw::battle::campaignBattleDebriefPresentationForOutcome(unsupported) ==
                    mw::battle::CampaignBattleDebriefPresentation::None &&
                mw::battle::campaignBattleDebriefPresentationForOutcome(finalOutcome) ==
                    mw::battle::CampaignBattleDebriefPresentation::None,
            "campaign original debrief presentation routing changed");

        mw::battle::CampaignContractVisitLedger contractVisitLedger;
        mw::battle::beginCampaignContractVisit(contractVisitLedger, 41);
        expect(
            mw::battle::campaignContractOfferAvailableForVisit(
                contractVisitLedger,
                41,
                2u) &&
                mw::battle::completeCampaignContractOfferForVisit(
                    contractVisitLedger,
                    41,
                    2u) &&
                !mw::battle::campaignContractOfferAvailableForVisit(
                    contractVisitLedger,
                    41,
                    2u) &&
                !mw::battle::completeCampaignContractOfferForVisit(
                    contractVisitLedger,
                    41,
                    2u),
            "campaign completed contract was not consumed exactly once");
        mw::battle::beginCampaignContractVisit(contractVisitLedger, 41);
        expect(
            !mw::battle::campaignContractOfferAvailableForVisit(
                contractVisitLedger,
                41,
                2u),
            "campaign completed contract did not remain hidden during same visit");
        mw::battle::beginCampaignContractVisit(contractVisitLedger, 42);
        expect(
            contractVisitLedger.completedOfferSlots.empty() &&
                mw::battle::campaignContractOfferAvailableForVisit(
                    contractVisitLedger,
                    42,
                    2u),
            "campaign completed contract did not reset on next planet visit");
        for (const mw::battle::CampaignBattleOutcomePackage* matrixOutcome :
             acceptedOutcomeMatrix) {
            const mw::battle::CampaignBattlePostResultReceipt matrixReceipt =
                mw::battle::campaignBattlePostResultReceiptFromOutcome(
                    *matrixOutcome,
                    true);
            expect(matrixReceipt.valid &&
                       matrixReceipt.launchSource ==
                           mw::battle::CampaignBattleLaunchSource::AcceptedContract &&
                       matrixReceipt.outcome == matrixOutcome->outcome &&
                       matrixReceipt.destination ==
                           mw::battle::CampaignBattlePostResultDestination::CampaignMainMenu &&
                       matrixReceipt.outcomeAcknowledged &&
                       matrixReceipt.safeCampaignReturn &&
                       matrixReceipt.persistentStateUnchanged &&
                       !matrixReceipt.settlementCommitted &&
                       !matrixReceipt.extendedEndingSequenceExecuted &&
                       !matrixReceipt.extendedEndingSequenceDeferred &&
                       matrixReceipt.outcomeReason == matrixOutcome->reason &&
                       matrixReceipt.terminal == matrixOutcome->terminal &&
                       matrixReceipt.rawResultCode == matrixOutcome->rawResultCode &&
                       matrixReceipt.finalSnapshotFingerprint ==
                           matrixOutcome->finalSnapshotFingerprint &&
                       matrixReceipt.terminalTickIndex == matrixOutcome->terminalTickIndex &&
                       matrixReceipt.terminalElapsedMs == matrixOutcome->terminalElapsedMs &&
                       matrixReceipt.ticksExecuted == matrixOutcome->ticksExecuted &&
                       matrixReceipt.reason ==
                           "accepted_contract_safe_return_settlement_deferred",
                   "accepted-contract post-result receipt matrix changed");
        }
        const mw::battle::CampaignBattlePostResultReceipt changedStateReceipt =
            mw::battle::campaignBattlePostResultReceiptFromOutcome(
                finalOutcome,
                false);
        expect(changedStateReceipt.valid &&
                   !changedStateReceipt.safeCampaignReturn &&
                   !changedStateReceipt.persistentStateUnchanged &&
                   changedStateReceipt.reason ==
                       "campaign_state_changed_while_acknowledging_battle_result",
               "campaign post-result receipt state guard changed");

        std::cout
            << "mw_campaign_battle_adapter_smoke: ok"
            << " phase11_adapter=started"
            << " campaign_roundtrip=victory:code" << victory.rawResultCode
            << ":ticks" << victory.terminalTickIndex
            << " campaign_defeat=defeat:code" << defeat.rawResultCode
            << ":ticks" << defeat.terminalTickIndex
            << " campaign_withdraw=withdraw:code" << withdraw.rawResultCode
            << ":ticks" << withdraw.terminalTickIndex
            << " campaign_unsupported=unsupported:no_terminal"
            << " campaign_live_outcomes=defeat" << liveDefeat.terminalTickIndex
            << ",withdraw" << liveWithdraw.terminalTickIndex
            << ",unsupported" << liveUnsupported.ticksExecuted
            << " campaign_contract=yes"
            << " campaign_objective=yes"
            << " campaign_units=" << victory.startCombatantCount << "->" << victory.finalCombatantCount
            << " campaign_ticks_executed=" << victory.ticksExecuted
            << " campaign_live_session=victory:code" << liveVictory.rawResultCode
            << ":ticks" << liveVictory.terminalTickIndex
            << " campaign_input_session=victory:code" << inputVictory.rawResultCode
            << ":ticks" << inputVictory.terminalTickIndex
            << " campaign_garrison_opposition=present:" << garrisonVictory.finalCombatantCount
            << " campaign_presentation=map,cockpit,external:snapshot_owned"
            << " campaign_presentation_units=" << presentationProbe.combatants
            << " campaign_render_scene=resolved:" << presentationProbe.renderVisuals
            << ":tiles" << presentationProbe.terrainTiles
            << ":objective_separate"
            << " campaign_gl_resources=ready:terrain" << presentationProbe.glTerrainVertices
            << ":visuals" << presentationProbe.glVisuals
            << ":objective"
            << " campaign_projectile_visuals=OTHPCK:records0,1:instances"
            << presentationProbe.projectileVisuals
            << ":vertices" << presentationProbe.projectileVertices
            << ":triangles" << presentationProbe.projectileTriangles
            << ":fixed_tick_interpolated:spin_beta_presentation_only"
            << " campaign_immediate_beams=laser_yellow,ppc_cyan:two_ticks:"
               "FUN_1000_bc5e_wedge:narrow_lower_beta"
            << " campaign_weapon_mounts=model_components+torso_thirds:"
               "camera_bob_attached:projectile_bind_mount_to_target_beta"
            << " campaign_machine_gun_flash=COCKPIT.BMP:6-11:"
               "family_side_mapped:one_tick_beta"
            << " campaign_impact_visuals=OTHPCK:records2-7:"
               "two_ticks_per_stage:16_radius_primitives:world_scaled:"
               "authoritative_contacts"
            << " campaign_machine_gun_impacts=OTHPCK:records8-9:"
               "two_ticks_per_stage"
            << " campaign_terrain_objects=ready:objects" << presentationProbe.glTerrainObjects
            << ":records" << presentationProbe.glTerrainObjectRecords
            << ":vertices" << presentationProbe.glTerrainObjectVertices
            << ":raw_wld:flat_ground"
            << " campaign_terrain_palette=" << presentationProbe.glTerrainPaletteId
            << ":env012:bank1:dither" << presentationProbe.glTerrainObjectDitherVertices
            << "v" << presentationProbe.glTerrainObjectDitherTriangles
            << "t:checker"
            << " campaign_dither_parity=prototype_shared:palette_batch:checker4x4"
            << " campaign_cockpit_compositor=ready:light:codec"
            << presentationProbe.cockpitBackdropCodec
            << ":320x200:viewport"
            << presentationProbe.cockpitViewportWidth
            << "x" << presentationProbe.cockpitViewportHeight
            << " campaign_cockpit_layers=ready:struts" << presentationProbe.cockpitStruts
            << ":widgets" << presentationProbe.cockpitWidgets
            << ":numbers" << presentationProbe.cockpitHudNumbers
            << ":font" << presentationProbe.cockpitFontWidth
            << "x" << presentationProbe.cockpitFontHeight
            << ":placements" << presentationProbe.lightStrutPlacements
            << "," << presentationProbe.mediumStrutPlacements
            << "," << presentationProbe.heavyStrutPlacements
            << " campaign_cockpit_controls=ready:speed16:jump40:zoom"
            << presentationProbe.cockpitMaxZoom
            << ":colors" << presentationProbe.cockpitHudColors
            << " campaign_cockpit_heat=ready:30x80:x198:y183:"
               "dark3+light1+dark5+light2"
            << " campaign_reactor_shutdown=FUSION_REACTOR_SHUT_DOWN:"
               "x88:y88:ega12:bit2_flash:snapshot_owned"
            << " campaign_cockpit_compass=right:090->120"
            << " campaign_cockpit_viewport_hud=ready:crosshair3:labels6x6:families3"
            << " campaign_weapon_cooldown_name=ready_white:cooldown_ega4_red:"
               "shutdown_preserved:snapshot_tick:families3"
            << " campaign_target_scan=enter:4000m:SM_MECHS9:type_order0_7:objective8:source0:outer_plus1:original_rects8x9:stencil_gray_destroyed_black:hud_color_box:enemy_box1:friendly_box2:objective_box1:protect_box2:snapshot_detailed_damage"
            << " campaign_mech_hit_profiles=stationary_bind:original_triangles:8:components5_plus_torso"
            << " campaign_external_camera=fullscreen320x200:right_drag:wheel_zoom:standalone_parity"
            << " campaign_maps=standalone_parity:status197x107:terpck+dither:mfd72x67:wld_terpck_projected:surface+mountain:placements"
            << presentationProbe.cockpitMinimapSurfacePlacements << ','
            << presentationProbe.cockpitMinimapMountainPlacements
            << ":gradients" << presentationProbe.cockpitMinimapMountainGradientPlacements
            << ":four_band" << presentationProbe.cockpitMinimapMountainFourBandPlacements
            << ":bands" << presentationProbe.cockpitMinimapHeightBandSamples[0]
            << ',' << presentationProbe.cockpitMinimapHeightBandSamples[1]
            << ',' << presentationProbe.cockpitMinimapHeightBandSamples[2]
            << ',' << presentationProbe.cockpitMinimapHeightBandSamples[3]
            << ":samples" << presentationProbe.cockpitMinimapTerrainSamples
            << ":blips2,1:north_up"
            << " campaign_lance_markers=source_slots0_3:green_plus_black_outline_on_green"
            << " campaign_command_map=standalone_parity:map_scr62,7,197x107:c_or_enter:simulation_live:authoritative_snapshot"
            << " campaign_scenario_binding=target_planet_ds0956:mod20:records20:mission_class_bound"
            << " campaign_boundary_failure=ordinary:typed_withdraw:campaign_defeat:code2"
            << " campaign_snow_command_player=black:other_maps_white"
            << " campaign_no_timeout=live_unbounded:tick901:diagnostic_limits_explicit"
            << " campaign_far_ground=apron200000:environment_color:visual_only"
            << " campaign_long_run=uint64_counters:bounded_state:animation64"
            << " campaign_render_interpolation=60hz:sim50ms:alpha50:wrap350->010"
            << " campaign_drive_parity=standalone:tick50ms:max750:accel500:one_sec263"
            << " campaign_cockpit_motion=catalog_heights:"
            << cockpitMotionProbe.locustHeight << ","
            << cockpitMotionProbe.phoenixHawkHeight << ","
            << cockpitMotionProbe.battleMasterHeight
            << ":bob720ms:45y:presentation_only"
            << " campaign_final_launch=selector12:units5:roster3bm1wh:unsupported1"
            << " campaign_launch_sources=accepted_contract,dark_wing_final"
            << " campaign_persistence_binding=source_slot:owned_mech+crew:final_systems:unlaunched_explicit:translation_deferred"
            << " campaign_damage_translation=audit29:coarse5:no_guess:intact+degraded+destroyed:mutation_deferred"
            << " campaign_battle_mech_status=g_pause:btech_bmp152x193x8:auto_white_register:black_armor_stencil:cyan_outline_to_black:q_cockpit:c_command:no_pilot:entry_state29:lance4:enemy_pristine:roundtrip_no_commit"
            << " campaign_consequence_plan=atomic:payment+salvage+reputation+house+time+xp:deferred3:idempotent:save_roundtrip"
            << " campaign_consequence_commit=payment+salvage+reputation+house+time+xp:deferred3"
            << " campaign_time=original:jump28+origin4+target2+mission_table:half_day_ticks"
            << " campaign_pilot_xp=original:survivors:thresholds3,10,15:gam0645+0712"
            << " campaign_salvage=current_estimate:beta_original_validation_required"
            << " campaign_pilot_health=death_only:no_injury:runtime_death_mapping_deferred"
            << " campaign_original_debrief=accepted:victory,defeat,withdraw:typed_commit_on_dismiss"
            << " campaign_contract_visit=completed_offer_hidden:same_visit:reset_on_travel"
            << " campaign_receipt_matrix=accepted:victory,defeat,withdraw,unsupported:immutable:safe_return:settlement_no"
            << " campaign_final_receipt=campaign_main_menu:state_unchanged:settlement_no:ending_deferred"
            << " campaign_resource_layouts=flat_original_install+sorted_evidence"
            << " campaign_fingerprint=present"
            << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "mw_campaign_battle_adapter_smoke: failed: " << error.what() << "\n";
        return 1;
    }
}
