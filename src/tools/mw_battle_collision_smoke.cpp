#include "battle/battle_module.h"
#include "battle/battle_world.h"
#include "mech3d/mech_catalog.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expectNear(double actual, double expected, double tolerance, const std::string& label) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(
            label + " expected " + std::to_string(expected) +
            " got " + std::to_string(actual));
    }
}

mw::battle::BattleStartParams baseCollisionParams() {
    mw::battle::BattleStartParams params;
    mw::battle::applyBattleDriveRuntimeTuning(params);
    params.mission = mw::battle::defaultBattleMissionBriefing();
    params.playerRoster.team = mw::battle::BattleTeam::Player;
    params.playerRoster.sourceSlot = "player:0";
    params.terrainCellSize = 100.0;
    params.constrainToTerrainBounds = false;
    params.deterministicCollisionRuntimeEnabled = true;
    params.mechCollisionRadiusCells = 0.2;
    params.collisionRestitution = 0.12;
    return params;
}

const mw::battle::CombatantSnapshot& player(
    const mw::battle::BattleSnapshot& snapshot) {
    for (const mw::battle::CombatantSnapshot& combatant : snapshot.combatants) {
        if (combatant.playerControlled) {
            return combatant;
        }
    }
    throw std::runtime_error("collision smoke player snapshot missing");
}

} // namespace

int main() {
    try {
        const std::filesystem::path originalRoot = "Sorted Original Files";
        const std::vector<std::filesystem::path> originalWorldPaths{
            originalRoot / "WLD" / "TILE3.WLD",
            originalRoot / "WLD" / "TILE4.WLD",
            originalRoot / "WLD" / "TILE8.WLD",
            originalRoot / "WLD" / "TILE15.WLD",
        };
        const mw::battle::BattleTerrainCollisionObstacles originalObstacles =
            mw::battle::loadOriginalBattleTerrainCollisionObstacles(
                originalRoot / "TBL" / "viewer8" / "TERPCK.TBL",
                originalWorldPaths,
                0.0, 173.0 * 500.0, 0.0, 93.0 * 500.0, 500.0);
        expect(originalObstacles.valid && !originalObstacles.entries.empty(),
               "original scenario-2 WLD/TERPCK raised collision geometry is empty");
        expect(!originalObstacles.driveableSurfaces.empty(),
               "original low TERPCK slope/surface features were not typed separately");
        expect(std::all_of(
                   originalObstacles.entries.begin(), originalObstacles.entries.end(),
                   [](const mw::battle::BattleTerrainCollisionObstacle& obstacle) {
                       return obstacle.height > 1.0 && obstacle.halfExtentX > 0.5 &&
                              obstacle.halfExtentZ > 0.5;
                   }),
               "flat WLD overlays leaked into raised terrain collision geometry");

        std::vector<mw::battle::BattleMechCollisionProfile> catalogProfiles;
        for (const mw::mech3d::MechCatalogEntry& entry :
             mw::mech3d::mechCatalogEntries()) {
            const double radius = mw::battle::campaignBattleCatalogCollisionRadius(
                originalRoot, entry.presetId);
            expect(radius > 0.0 && radius < 1000.0,
                   "catalog bind-pose collision radius is outside visual model scale");
            catalogProfiles.push_back({
                entry.presetId,
                radius,
                "catalog_bind_pose_horizontal_assembly_bounds",
            });
        }
        expect(catalogProfiles.size() == 8u,
               "catalog collision profiles do not cover all eight mechs");

        mw::battle::BattleStartParams decorativeGridParams = baseCollisionParams();
        decorativeGridParams.playerStartTransform = {749.0, 0.0, 1000.0, 1.5707963267948966};
        decorativeGridParams.terrainCollisionGrid.valid = true;
        decorativeGridParams.terrainCollisionGrid.provenance = "synthetic_nonblocking_GRD";
        decorativeGridParams.terrainCollisionGrid.width = 20;
        decorativeGridParams.terrainCollisionGrid.height = 20;
        decorativeGridParams.terrainCollisionGrid.cellSize = 100.0;
        decorativeGridParams.terrainCollisionGrid.rawSamples.assign(400u, 0u);
        decorativeGridParams.terrainCollisionGrid.rawSamples[10u * 20u + 10u] = 0x20u;
        const mw::battle::BattleSnapshot decorativeGridSnapshot =
            mw::battle::runBattleReplay(decorativeGridParams, {}, 1u);
        expect(!decorativeGridSnapshot.lastCollision.valid,
               "unproven GRD colour/detail sample still blocks a mech");

        mw::battle::BattleStartParams terrainParams = baseCollisionParams();
        terrainParams.playerStartTransform = {879.0, 0.0, 1000.0, 1.5707963267948966};
        terrainParams.terrainCollisionObstacles.valid = true;
        terrainParams.terrainCollisionObstacles.provenance = "synthetic_raised_TERPCK_bounds";
        mw::battle::BattleTerrainCollisionObstacle syntheticObstacle;
        syntheticObstacle.stableId = 7u;
        syntheticObstacle.recordIndex = 12u;
        syntheticObstacle.centerX = 1000.0;
        syntheticObstacle.centerZ = 1000.0;
        syntheticObstacle.halfExtentX = 100.0;
        syntheticObstacle.halfExtentZ = 100.0;
        syntheticObstacle.height = 1000.0;
        syntheticObstacle.footprint = {
            {900.0, 900.0},
            {1100.0, 900.0},
            {1100.0, 1100.0},
            {900.0, 1100.0},
        };
        terrainParams.terrainCollisionObstacles.entries.push_back(syntheticObstacle);

        mw::battle::BattleInputCommand forward;
        forward.tickIndex = 0;
        forward.throttle = 1.0;
        mw::battle::BattleReplay terrainReplay;
        terrainReplay.commands.push_back(forward);
        const mw::battle::BattleSnapshot terrainA =
            mw::battle::runBattleReplay(terrainParams, terrainReplay, 1u);
        const mw::battle::BattleSnapshot terrainB =
            mw::battle::runBattleReplay(terrainParams, terrainReplay, 1u);
        const mw::battle::CombatantSnapshot& terrainPlayer = player(terrainA);
        expect(terrainA.terrain.collisionObstacleCount == 1u &&
                   terrainA.terrain.collisionObstacleFingerprint != 0u,
               "terrain collision obstacle metadata missing from immutable snapshot");
        expect(terrainA.lastCollision.valid &&
                   terrainA.lastCollision.kind == mw::battle::BattleCollisionKind::TerrainObject,
               "raised terrain object did not publish collision diagnostic");
        expect(terrainA.lastCollision.terrainObstacleId == 7u &&
                   terrainA.lastCollision.terrainRecordIndex == 12u,
               "terrain collision diagnostic lost its WLD/TERPCK identity");
        expect(terrainPlayer.collisionContact && terrainPlayer.collisionCount == 1u,
               "terrain collision contact/count missing from combatant snapshot");
        expectNear(terrainPlayer.forwardSpeed, 0.0, 0.0001,
                   "terrain collision stop response");
        expect(terrainPlayer.transform.x < terrainParams.playerStartTransform.x,
               "terrain collision did not apply the outward positional backoff");
        expect(
            terrainParams.playerStartTransform.x - terrainPlayer.transform.x >= 80.0,
            "terrain collision visible backoff is weaker than the 80-unit response");
        expect(terrainPlayer.collisionFeedbackCooldownTicksRemaining == 20u,
               "one-second collision feedback cooldown changed");
        expect(terrainA.lastCollision.runtimeEffect ==
                   mw::battle::BattleCollisionRuntimeEffect::PositionalSeparationNoDamage &&
                   !terrainA.lastCollision.damageSemanticsProven &&
                   terrainA.lastCollision.damageApplied == 0,
               "unproven collision damage must remain typed and fail-closed");
        expect(
            mw::battle::battleSnapshotFingerprint(terrainA) ==
                mw::battle::battleSnapshotFingerprint(terrainB),
            "terrain collision replay fingerprint changed");
        const mw::battle::CampaignBattleRenderScenePackage collisionScene =
            mw::battle::campaignBattleRenderSceneFromSnapshot(
                terrainA,
                mw::battle::CampaignBattlePresentationMode::Cockpit,
                originalRoot);
        expect(collisionScene.playerCollisionFlashEvent &&
                   collisionScene.playerCollisionSequence ==
                       terrainA.lastCollision.sequence,
               "immutable player collision event did not reach cockpit presentation");
        mw::battle::BattleSnapshot quietSnapshot = terrainA;
        for (mw::battle::CombatantSnapshot& combatant : quietSnapshot.combatants) {
            combatant.collisionContact = false;
        }
        const mw::battle::CampaignBattleRenderScenePackage quietScene =
            mw::battle::campaignBattleRenderSceneFromSnapshot(
                quietSnapshot,
                mw::battle::CampaignBattlePresentationMode::Cockpit,
                originalRoot);
        expect(!quietScene.playerCollisionFlashEvent,
               "cockpit collision flash was not edge/event driven");

        mw::battle::BattleWorld cooldownWorld = mw::battle::BattleWorld::create(terrainParams);
        cooldownWorld.enqueueInput(forward);
        cooldownWorld.tick();
        const uint64_t firstCollisionSequence = cooldownWorld.snapshot().lastCollision.sequence;
        for (int tick = 0; tick < 5; ++tick) {
            cooldownWorld.tick();
        }
        expect(cooldownWorld.snapshot().lastCollision.sequence == firstCollisionSequence,
               "continuous terrain contact retriggered feedback inside one-second cooldown");

        mw::battle::BattleStartParams cornerParams = terrainParams;
        cornerParams.playerStartTransform = {884.0, 0.0, 884.0, 0.0};
        const mw::battle::BattleSnapshot cornerSnapshot =
            mw::battle::runBattleReplay(cornerParams, {}, 1u);
        expect(!cornerSnapshot.lastCollision.valid,
               "terrain collision still uses empty AABB corners instead of the footprint");

        mw::battle::BattleStartParams mechParams = baseCollisionParams();
        mechParams.playerStartTransform = {1000.0, 0.0, 1000.0, 0.0};
        mw::battle::BattleCombatantLaunchState enemy;
        enemy.mechPresetId = "warhammer";
        enemy.startTransform = {1030.0, 0.0, 1000.0, 0.0};
        enemy.roster.team = mw::battle::BattleTeam::Opposing;
        enemy.roster.sourceSlot = "opposing:0";
        mechParams.combatantLaunchStates.push_back(enemy);

        mw::battle::BattleWorld mechWorld = mw::battle::BattleWorld::create(mechParams);
        const uint64_t beforeReadFingerprint =
            mw::battle::battleSnapshotFingerprint(mechWorld.snapshot());
        for (int renderRead = 0; renderRead < 240; ++renderRead) {
            expect(
                mw::battle::battleSnapshotFingerprint(mechWorld.snapshot()) ==
                    beforeReadFingerprint,
                "presentation cadence/read mutated authoritative collision state");
        }
        mechWorld.tick();
        const mw::battle::BattleSnapshot mechSnapshot = mechWorld.snapshot();
        expect(mechSnapshot.lastCollision.valid &&
                   mechSnapshot.lastCollision.kind == mw::battle::BattleCollisionKind::MechMech,
               "overlapping mechs did not publish mech-mech collision diagnostic");
        expect(mechSnapshot.combatants.size() == 2u &&
                   mechSnapshot.combatants[0].collisionContact &&
                   mechSnapshot.combatants[1].collisionContact,
               "mech-mech contact was not attached to both combatants");
        const double separation = std::hypot(
            mechSnapshot.combatants[1].transform.x -
                mechSnapshot.combatants[0].transform.x,
            mechSnapshot.combatants[1].transform.z -
                mechSnapshot.combatants[0].transform.z);
        expectNear(separation, 120.0, 0.001,
                    "two fallback model-scale radii plus visible backoff");
        expect(mechSnapshot.lastCollision.damageApplied == 0 &&
                   !mechSnapshot.lastCollision.damageSemanticsProven,
               "mech-mech contact invented collision damage");

        const mw::battle::BattleSnapshot mechReplayA =
            mw::battle::runBattleReplay(mechParams, {}, 1u);
        const mw::battle::BattleSnapshot mechReplayB =
            mw::battle::runBattleReplay(mechParams, {}, 1u);
        expect(
            mw::battle::battleSnapshotFingerprint(mechReplayA) ==
                mw::battle::battleSnapshotFingerprint(mechReplayB),
            "mech-mech collision replay fingerprint changed");
        expect(
            mw::battle::battleSnapshotFingerprint(mechSnapshot) ==
                mw::battle::battleSnapshotFingerprint(mechReplayA),
            "stepped and replay collision ownership differ");

        std::cout
            << "mw_battle_collision_smoke: ok"
            << " terrain=WLD/TERPCK:raised_bounds"
            << " original_obstacles=" << originalObstacles.entries.size()
            << " driveable_surfaces=" << originalObstacles.driveableSurfaces.size()
            << " terrain_footprint=convex_projected_model"
            << " terrain_contact=" << terrainA.lastCollision.terrainObstacleId
            << " mech_footprint=catalog_bind_pose_radii:8"
            << " catalog_radii=";
        for (size_t index = 0; index < catalogProfiles.size(); ++index) {
            if (index != 0u) {
                std::cout << ',';
            }
            std::cout << catalogProfiles[index].mechPresetId << ':'
                      << catalogProfiles[index].radiusWorld;
        }
        std::cout
            << " mech_separation=" << separation
            << " stop_and_backoff=yes"
            << " cockpit_flash=event_owned:2frames:1s_cooldown"
            << " grd_spots=nonblocking_hidden_in_3d"
            << " damage=deferred_fail_closed"
            << " replay_match=yes"
            << " render_cadence_independent=yes\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "mw_battle_collision_smoke: " << error.what() << '\n';
        return 1;
    }
}
