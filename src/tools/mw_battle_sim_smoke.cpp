#include "battle/battle_module.h"
#include "battle/battle_setup.h"
#include "battle/battle_world.h"
#include "battle/campaign_ammunition.h"
#include "mech3d/mech_catalog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct Options {
    std::filesystem::path snarioPath = "Sorted Original Files/DAT/SNARIO.DAT";
    size_t scenarioIndex = 2;
    int environmentId = 1;
    uint64_t tickCount = 12;
};

void printUsage() {
    std::cerr
        << "usage: mw_battle_sim_smoke [--snario SNARIO.DAT] [--scenario-index N] [--environment-id 0|1|2] [--ticks N]\n";
}

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto requireValue = [&](const std::string& name) -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error(name + " requires a value");
            }
            return argv[++i];
        };

        if (arg == "--snario") {
            options.snarioPath = requireValue(arg);
        } else if (arg == "--scenario-index") {
            options.scenarioIndex = static_cast<size_t>(std::stoull(requireValue(arg)));
        } else if (arg == "--environment-id") {
            options.environmentId = std::stoi(requireValue(arg));
        } else if (arg == "--ticks") {
            options.tickCount = std::stoull(requireValue(arg));
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            std::exit(0);
        } else {
            throw std::runtime_error("unknown option: " + arg);
        }
    }
    return options;
}

std::string joinStrings(const std::vector<std::string>& values) {
    std::ostringstream oss;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            oss << ",";
        }
        oss << values[i];
    }
    return oss.str();
}

std::string joinOppositionRequests(const mw::battle::BattleOppositionSpawnPlan& plan) {
    std::ostringstream oss;
    for (size_t i = 0; i < plan.requests.size(); ++i) {
        if (i != 0) {
            oss << ',';
        }
        const mw::battle::BtechOppositionSpawnRequest& request = plan.requests[i];
        oss << static_cast<int>(request.sourceContextOffset)
            << ':' << static_cast<int>(request.candidateTypeMin)
            << '-' << static_cast<int>(request.candidateTypeMax);
    }
    return oss.str();
}

std::string joinOppositionSlots(const mw::battle::BattleOppositionSpawnPlan& plan) {
    std::ostringstream oss;
    for (size_t i = 0; i < plan.requests.size(); ++i) {
        if (i != 0) {
            oss << ',';
        }
        const mw::battle::BtechOppositionSpawnRequest& request = plan.requests[i];
        oss << static_cast<int>(request.runtimeObjectSlot)
            << "->" << static_cast<int>(request.opposingPlacementSlot);
    }
    return oss.str();
}

std::string joinOppositionClasses(const mw::battle::BattleOppositionSpawnPlan& plan) {
    std::ostringstream oss;
    for (size_t i = 0; i < plan.requests.size(); ++i) {
        if (i != 0) {
            oss << ',';
        }
        oss << mw::battle::btechOppositionEstimateClassName(plan.requests[i].estimatedClass);
    }
    return oss.str();
}

std::string joinOppositionCandidatePresets(const mw::battle::BattleOppositionSpawnPlan& plan) {
    std::ostringstream oss;
    for (size_t i = 0; i < plan.requests.size(); ++i) {
        if (i != 0) {
            oss << ';';
        }
        oss << joinStrings(plan.requests[i].candidateMechPresetIds);
    }
    return oss.str();
}

std::string joinBtechMechTypePresets() {
    std::ostringstream oss;
    const auto& definitions = mw::battle::btechMechTypeDefinitions();
    for (size_t i = 0; i < definitions.size(); ++i) {
        if (i != 0) {
            oss << ',';
        }
        oss << static_cast<int>(definitions[i].typeId) << ':' << definitions[i].mechPresetId;
    }
    return oss.str();
}

size_t oppositionRosterCount(
    const mw::battle::BattleOppositionRosterMetadata& roster,
    std::string_view mechPresetId) {
    const auto found = std::find_if(
        roster.entries.begin(),
        roster.entries.end(),
        [&](const mw::battle::BattleOppositionRosterEntry& entry) {
            return entry.mechPresetId == mechPresetId;
        });
    return found == roster.entries.end() ? 0u : found->count;
}

std::string joinOppositionRosterSlots(const mw::battle::BattleOppositionRosterMetadata& roster) {
    std::ostringstream oss;
    for (size_t i = 0; i < roster.orderedSlots.size(); ++i) {
        if (i != 0) {
            oss << ',';
        }
        const mw::battle::BattleOppositionRosterSlot& slot = roster.orderedSlots[i];
        oss << slot.spawnOrdinal << ':' << static_cast<int>(slot.btechTypeId)
            << '@' << static_cast<int>(slot.runtimeObjectSlot)
            << "->" << static_cast<int>(slot.opposingPlacementSlot);
    }
    return oss.str();
}

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expectNear(double actual, double expected, double tolerance, const std::string& label) {
    if (std::abs(actual - expected) > tolerance) {
        std::ostringstream oss;
        oss << label << " expected " << expected << " got " << actual;
        throw std::runtime_error(oss.str());
    }
}

bool containsId(const std::vector<int>& values, int id) {
    return std::find(values.begin(), values.end(), id) != values.end();
}

void appendHitRectangle(
    mw::battle::BattleMechHitProfile& profile,
    double minimumX,
    double maximumX,
    double minimumY,
    double maximumY,
    double z,
    int componentId,
    mw::mech3d::MechArmorSectionId section,
    bool torsoComposite) {
    const mw::battle::BattleHitPoint lowerLeft{minimumX, minimumY, z};
    const mw::battle::BattleHitPoint lowerRight{maximumX, minimumY, z};
    const mw::battle::BattleHitPoint upperRight{maximumX, maximumY, z};
    const mw::battle::BattleHitPoint upperLeft{minimumX, maximumY, z};
    profile.triangles.push_back({
        lowerLeft, lowerRight, upperRight, componentId, section,
        torsoComposite});
    profile.triangles.push_back({
        lowerLeft, upperRight, upperLeft, componentId, section,
        torsoComposite});
}

mw::battle::BattleMechHitProfile syntheticCrosshairHitProfile() {
    mw::battle::BattleMechHitProfile profile;
    profile.valid = true;
    profile.mechPresetId = "locust";
    profile.aimOriginHeight = 100.0;
    profile.cockpitViewportHeight = 100;
    profile.neutralCrosshairY = 50.0;
    profile.crosshairPixelsPerAimStep = 3.0;
    profile.verticalFieldOfViewDegrees = 45.0;
    profile.geometryFingerprint = 0x5048415345313248ull;
    profile.provenance = "phase12_synthetic_crosshair_component_mesh";
    appendHitRectangle(
        profile, -30.0, 30.0, 90.0, 110.0, 0.0, 1,
        mw::mech3d::MechArmorSectionId::CenterTorso, true);
    appendHitRectangle(
        profile, -30.0, 30.0, 135.0, 155.0, 0.0, 6,
        mw::mech3d::MechArmorSectionId::Head, false);
    appendHitRectangle(
        profile, -30.0, 30.0, 45.0, 65.0, 0.0, 2,
        mw::mech3d::MechArmorSectionId::RightLeg, false);
    return profile;
}

const mw::battle::BattleMechSystemSnapshot* findSystem(
    const mw::battle::BattleMechSystemsSnapshot& systems,
    mw::battle::BattleMechSystemRole role) {
    const auto it = std::find_if(
        systems.systems.begin(),
        systems.systems.end(),
        [role](const mw::battle::BattleMechSystemSnapshot& system) {
            return system.role == role;
        });
    return it == systems.systems.end() ? nullptr : &*it;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parseOptions(argc, argv);

        mw::battle::BattleStartParams params;
        params.terrainScenarioPath = options.snarioPath;
        params.terrainScenarioIndex = options.scenarioIndex;
        params.terrainEnvironmentId = options.environmentId;
        params.mission = mw::battle::defaultBattleMissionBriefing();
        params.contract.valid = true;
        params.contract.provenance = "campaign_contract_diagnostic";
        params.contract.employerHouseId = 4;
        params.contract.employerHouseName = "DAVION";
        params.contract.targetHouseId = 3;
        params.contract.targetHouseName = "LIAO";
        params.contract.hasHostileTargetHouse = true;
        params.contract.targetPlanetName = "WARLOCK";
        params.contract.targetPlanetTerrainCode = 11;
        params.contract.targetEnvironmentId = 1;
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
        params.playerStartTransform = mw::battle::Transform{43500.0, 0.0, 23500.0, 0.0};
        params.playerRoster.team = mw::battle::BattleTeam::Player;
        params.playerRoster.factionHouseId = static_cast<uint8_t>(4);
        params.playerRoster.factionHouseName = "DAVION";
        params.playerRoster.provenance = "accepted_campaign_contract";
        params.playerRoster.sourceSlot = "player:0";
        params.objective.valid = true;
        params.objective.role = "target";
        params.objective.provenance = "setup_derived_diagnostic";
        params.objective.sourceSlot = "opposing:1";
        params.objective.transformProven = true;
        params.objective.activeObjectProven = false;
        params.objective.missionSemanticsProven = false;
        params.objective.damagePolicyProven = true;
        params.objective.damageSuppressed = true;
        params.objective.depletionPolicyProven = true;
        params.objective.depletionSetsPlayerWinCondition = true;
        params.objective.depletionSetsPlayerLossCondition = true;
        params.objective.depletionResultCode = 0;
        params.objective.transform = mw::battle::Transform{68000.0, 0.0, 35000.0, 0.0};
        params.objective.gridX = 136.0;
        params.objective.gridY = 70.0;
        params.objective.staticModel = mw::battle::originalBattleObjectiveStaticModel();

        mw::battle::BattleWorld world = mw::battle::BattleWorld::create(params);
        const mw::battle::BattleSnapshot& start = world.missionStartSnapshot();
        expect(start.tickIndex == 0, "mission start snapshot must be at tick 0");
        expect(start.elapsedMs == 0, "mission start snapshot must have zero elapsed time");
        expect(start.terrain.scenarioLoaded, "terrain scenario was not loaded");
        expect(start.terrain.scenarioIndex == options.scenarioIndex, "terrain scenario index mismatch");
        expect(start.terrain.tileNames == std::vector<std::string>({"TILE3", "TILE4", "TILE8", "TILE15"}),
               "scenario 2 tile names changed");
        expect(start.mission.valid, "mission briefing missing from start snapshot");
        expect(start.mission.originalId == 10u, "default mission id changed");
        expect(start.mission.title == "RESCUE OF A KIDNAP VICTIM", "default mission title changed");
        expect(start.contract.valid, "contract metadata missing from start snapshot");
        expect(start.contract.provenance == "campaign_contract_diagnostic", "contract provenance changed");
        expect(start.contract.employerHouseName == "DAVION", "contract employer changed");
        expect(start.contract.targetHouseName == "LIAO", "contract target house changed");
        expect(start.contract.targetPlanetName == "WARLOCK", "contract target planet changed");
        expect(start.contract.targetEnvironmentId == 1, "contract target environment changed");
        expect(start.contract.estimatedHeavyMechs == 0, "contract heavy estimate changed");
        expect(start.contract.estimatedMediumMechs == 0, "contract medium estimate changed");
        expect(start.contract.estimatedLightMechs == 1, "contract light estimate changed");
        expect(start.contract.garrisonAutoResolveDeferred, "garrison auto-resolve must remain deferred");
        expect(start.contract.oppositionSpawnPlan.valid, "opposition spawn diagnostic missing");
        expect(
            start.contract.oppositionSpawnPlan.provenance == "campaign_hml_to_btech_context_3_5",
            "opposition spawn diagnostic provenance changed");
        expect(start.contract.oppositionSpawnPlan.countBucketSemanticsProven,
               "BTECH count bucket semantics should be proven");
        expect(start.contract.oppositionSpawnPlan.candidateTypeMappingProven,
               "BTECH candidate type-to-preset mapping should be proven");
        expect(start.contract.oppositionSpawnPlan.opposingSlotMappingProven,
               "BTECH opposing slot mapping should be proven");
        expect(!start.contract.oppositionSpawnPlan.combatantsSpawned,
               "opposition diagnostic must not spawn combatants");
        expect(start.contract.oppositionSpawnPlan.requests.size() == 1u,
               "single light estimate should produce one diagnostic request");
        expect(start.contract.oppositionSpawnPlan.requests[0].sourceContextOffset == 5,
               "single light estimate should use context byte 5 candidate");
        expect(start.contract.oppositionSpawnPlan.requests[0].estimatedClass ==
                   mw::battle::BtechOppositionEstimateClass::Light,
               "single light estimate should preserve its proven weight class");
        expect(start.contract.oppositionSpawnPlan.requests[0].candidateTypeMin == 0 &&
                   start.contract.oppositionSpawnPlan.requests[0].candidateTypeMax == 1,
               "context byte 5 candidate type range changed");
        expect(start.contract.oppositionSpawnPlan.requests[0].candidateMechPresetIds ==
                   std::vector<std::string>({"locust", "jenner"}),
               "light BTECH candidate presets changed");
        expect(start.combatants.size() == 1, "mission start should contain one combatant");
        expect(start.combatants[0].id == world.playerEntityId(), "player entity id mismatch");
        expect(start.combatants[0].mechPresetId == "locust", "player mech preset mismatch");
        expect(start.combatants[0].roster.team == mw::battle::BattleTeam::Player,
               "player roster team changed");
        expect(start.combatants[0].roster.factionHouseId == 4,
               "player roster faction id changed");
        expect(start.combatants[0].roster.factionHouseName == "DAVION",
               "player roster faction name changed");
        expect(start.combatants[0].roster.provenance == "accepted_campaign_contract",
               "player roster provenance changed");
        expect(start.combatants[0].roster.sourceSlot == "player:0",
               "player roster source slot changed");
        expect(start.combatants[0].roster.originalLiveObjectSlot == 0u,
               "player original live-object slot changed");
        expect(start.combatants[0].activeAnimationId == "idle", "stationary player mech should start idle");
        expect(start.combatants[0].originalMaxSpeedKph == 129, "Locust original maximum speed changed");
        expectNear(start.combatants[0].maxForwardSpeed, params.maxForwardSpeed, 0.001, "Locust world maximum speed");
        expect(!start.combatants[0].jumpCapable, "Locust must not have jump capability");
        expect(start.combatants[0].jumpCapacityMeters == 0, "Locust jump capacity changed");
        expect(start.combatants[0].jumpJetCount == 0, "Locust jump-jet count changed");
        expect(!start.combatants[0].jumpJetReady, "Locust jump jets must never be ready");
        expectNear(start.combatants[0].jumpFuel, 0.0, 0.001, "Locust jump fuel");

        const std::array<std::pair<const char*, mw::mech3d::MechCatalogMobility>, 10> mobilityCases{{
            {"locust", {129, 0, 0, 72, 500}},
            {"wasp", {95, 180, 6, 0, 0}},
            {"jenner", {118, 150, 3, 66, 600}},
            {"phoenix_hawk", {97, 180, 6, 54, 700}},
            {"shadow_hawk", {86, 90, 3, 48, 700}},
            {"wolverine", {86, 150, 5, 0, 0}},
            {"rifleman", {64, 0, 0, 36, 800}},
            {"warhammer", {64, 0, 0, 36, 800}},
            {"marauder", {64, 0, 0, 36, 900}},
            {"battlemaster", {64, 0, 0, 36, 960}},
        }};
        for (const auto& mobilityCase : mobilityCases) {
            const mw::mech3d::MechCatalogMobility actual = mw::mech3d::catalogMobility(mobilityCase.first);
            expect(actual.maxSpeedKph == mobilityCase.second.maxSpeedKph,
                   std::string(mobilityCase.first) + " maximum speed changed");
            expect(actual.jumpCapacityMeters == mobilityCase.second.jumpCapacityMeters,
                   std::string(mobilityCase.first) + " jump capacity changed");
            expect(actual.jumpJetCount == mobilityCase.second.jumpJetCount,
                   std::string(mobilityCase.first) + " jump-jet count changed");
            expect(actual.originalBtechDefinitionSpeedWord06 ==
                       mobilityCase.second.originalBtechDefinitionSpeedWord06,
                   std::string(mobilityCase.first) +
                       " BTECH definition +0x06 speed word changed");
            expect(actual.originalBtechDefinitionHeightWord02 ==
                       mobilityCase.second.originalBtechDefinitionHeightWord02,
                   std::string(mobilityCase.first) +
                       " BTECH definition +0x02 height word changed");
        }

        // Phase 12 weapon vertical: the catalog owns ordered installations for
        // every campaign-supported 3D chassis, while BattleWorld owns all
        // mutable selection, ammunition, cooldown, and shot state.
        const std::array<std::pair<const char*, size_t>, 8> weaponLoadoutCases{{
            {"locust", 3u},
            {"jenner", 5u},
            {"phoenix_hawk", 5u},
            {"shadow_hawk", 4u},
            {"rifleman", 6u},
            {"warhammer", 9u},
            {"marauder", 5u},
            {"battlemaster", 10u},
        }};
        for (const auto& weaponLoadoutCase : weaponLoadoutCases) {
            const auto mounts = mw::mech3d::catalogWeaponMounts(weaponLoadoutCase.first);
            expect(
                mounts.size() == weaponLoadoutCase.second,
                std::string(weaponLoadoutCase.first) + " weapon installation count changed");
            for (size_t mountIndex = 0; mountIndex < mounts.size(); ++mountIndex) {
                expect(
                    mounts[mountIndex].slotIndex == mountIndex,
                    std::string(weaponLoadoutCase.first) + " weapon slot order changed");
                expect(
                    !mounts[mountIndex].weaponTypeId.empty() &&
                        !mounts[mountIndex].locationId.empty() &&
                        (mounts[mountIndex].displayRangeClass == 'S' ||
                         mounts[mountIndex].displayRangeClass == 'M' ||
                         mounts[mountIndex].displayRangeClass == 'L'),
                    std::string(weaponLoadoutCase.first) +
                        " weapon identity/location/range class missing");
                expect(
                    mounts[mountIndex].originalCooldownUpdateCount != 0u,
                    std::string(weaponLoadoutCase.first) +
                        " original weapon cooldown count missing");
            }
        }
        struct AmmunitionProfileCase {
            const char* presetId = "";
            std::array<uint16_t, 6> maximumByPool{};
            std::array<int8_t, 6> gamPlaneOrdinalByPool{};
        };
        const std::array<AmmunitionProfileCase, 8> ammunitionProfileCases{{
            {"locust", {{0, 0, 0, 0, 0, 200}}, {{-1, -1, -1, -1, -1, 0}}},
            {"jenner", {{0, 0, 0, 25, 0, 0}}, {{-1, -1, -1, 0, -1, -1}}},
            {"phoenix_hawk", {{0, 0, 0, 0, 0, 200}}, {{-1, -1, -1, -1, -1, 0}}},
            {"shadow_hawk", {{20, 24, 50, 0, 0, 0}}, {{0, 1, 2, -1, -1, -1}}},
            {"rifleman", {{20, 0, 0, 0, 0, 0}}, {{0, -1, -1, -1, -1, -1}}},
            {"warhammer", {{0, 0, 0, 0, 15, 200}}, {{-1, -1, -1, -1, 0, 1}}},
            {"marauder", {{20, 0, 0, 0, 0, 0}}, {{0, -1, -1, -1, -1, -1}}},
            {"battlemaster", {{0, 0, 0, 0, 30, 200}}, {{-1, -1, -1, -1, 0, 1}}},
        }};
        for (const AmmunitionProfileCase& ammunitionCase :
             ammunitionProfileCases) {
            const mw::mech3d::MechCatalogAmmunitionProfile profile =
                mw::mech3d::catalogAmmunitionProfile(
                    ammunitionCase.presetId);
            expect(
                profile.maximumByPool == ammunitionCase.maximumByPool,
                std::string(ammunitionCase.presetId) +
                    " original ammunition capacities changed");
            expect(
                profile.gamPlaneOrdinalByPool() ==
                    ammunitionCase.gamPlaneOrdinalByPool,
                std::string(ammunitionCase.presetId) +
                    " .GAM ammunition plane order changed");

            const auto mounts =
                mw::mech3d::catalogWeaponMounts(ammunitionCase.presetId);
            for (const mw::mech3d::MechCatalogWeaponMount& mount : mounts) {
                if (!mount.usesAmmunition()) {
                    continue;
                }
                const size_t pool =
                    static_cast<size_t>(mount.ammunitionPoolIndex);
                expect(
                    pool < profile.maximumByPool.size() &&
                        profile.maximumByPool[pool] != 0u,
                    std::string(ammunitionCase.presetId) +
                        " installed ammunition weapon has no chassis pool");
            }
        }
        std::vector<uint8_t> ammunitionGamProbe(
            mw::battle::kOriginalGamExtraAmmunitionBase + 12u,
            uint8_t{0xa5});
        const std::array<int, 6> warhammerCapacities{{
            0, 0, 0, 0, 15, 200}};
        const std::array<int, 6> warhammerSavedAmmunition{{
            0, 0, 0, 0, 7, 123}};
        mw::battle::encodeOriginalGamOwnedMechAmmunition(
            ammunitionGamProbe,
            3u,
            warhammerCapacities,
            warhammerSavedAmmunition);
        expect(
            mw::battle::decodeOriginalGamOwnedMechAmmunition(
                ammunitionGamProbe, 3u, warhammerCapacities) ==
                warhammerSavedAmmunition,
            "Warhammer .GAM SRM6/MG ammunition planes collapsed");
        expect(
            ammunitionGamProbe[
                mw::battle::originalGamOwnedMechAmmunitionOffset(0u, 3u)] ==
                    7u &&
                ammunitionGamProbe[
                    mw::battle::originalGamOwnedMechAmmunitionOffset(1u, 3u)] ==
                    123u &&
                ammunitionGamProbe[
                    mw::battle::originalGamOwnedMechAmmunitionOffset(2u, 3u)] ==
                    0u &&
                ammunitionGamProbe[
                    mw::battle::originalGamOwnedMechAmmunitionOffset(0u, 4u)] ==
                    0xa5u,
            "Warhammer .GAM ammunition plane offsets changed");

        const std::array<int, 6> shadowHawkCapacities{{
            20, 24, 50, 0, 0, 0}};
        const std::array<int, 6> shadowHawkSavedAmmunition{{
            19, 23, 49, 0, 0, 0}};
        mw::battle::encodeOriginalGamOwnedMechAmmunition(
            ammunitionGamProbe,
            7u,
            shadowHawkCapacities,
            shadowHawkSavedAmmunition);
        expect(
            mw::battle::decodeOriginalGamOwnedMechAmmunition(
                ammunitionGamProbe, 7u, shadowHawkCapacities) ==
                shadowHawkSavedAmmunition,
            "Shadow Hawk .GAM three-pool ammunition round-trip changed");

        expect(
            mw::mech3d::catalogWeaponMounts("locust").size() == 3u &&
                mw::mech3d::catalogWeaponMounts("shadow_hawk").size() == 4u &&
                mw::mech3d::catalogWeaponMounts("battlemaster").size() == 10u,
            "light/medium/heavy installed weapon coverage changed");
        expect(
            mw::mech3d::catalogWeaponMounts("locust")[0].displayRangeClass == 'M' &&
                mw::mech3d::catalogWeaponMounts("locust")[1].displayRangeClass == 'S' &&
                mw::mech3d::catalogWeaponMounts("shadow_hawk")[0].displayRangeClass == 'L' &&
                mw::mech3d::catalogWeaponMounts("shadow_hawk")[2].displayRangeClass == 'M' &&
                mw::mech3d::catalogWeaponMounts("warhammer")[0].displayRangeClass == 'L' &&
                mw::mech3d::catalogWeaponMounts("warhammer")[5].displayRangeClass == 'S',
            "original cockpit S/M/L weapon range classes changed");
        expect(
            mw::mech3d::catalogWeaponMounts("locust")[0].originalCooldownUpdateCount == 30u &&
                mw::mech3d::catalogWeaponMounts("locust")[1].originalCooldownUpdateCount == 2u &&
                mw::mech3d::catalogWeaponMounts("jenner")[0].originalCooldownUpdateCount == 15u &&
                mw::mech3d::catalogWeaponMounts("shadow_hawk")[0].originalCooldownUpdateCount == 15u &&
                mw::mech3d::catalogWeaponMounts("warhammer")[0].originalCooldownUpdateCount == 30u,
            "BTECH original weapon cooldown table changed");
        expect(
            mw::mech3d::catalogWeaponMounts("locust")[0].originalDamage == 5u &&
                mw::mech3d::catalogWeaponMounts("locust")[0].originalHeat == 240u &&
                mw::mech3d::catalogWeaponMounts("locust")[0].originalRangeWords ==
                    std::array<uint16_t, 4>{0u, 4u, 7u, 10u} &&
                mw::mech3d::catalogWeaponMounts("locust")[1].originalDamage == 2u &&
                mw::mech3d::catalogWeaponMounts("jenner")[0].originalDamage == 4u,
            "BTECH original damage, heat, or range table changed");
        expect(
            mw::mech3d::catalogWeaponMounts("warhammer")[5].originalHeat == 80u &&
                mw::mech3d::catalogWeaponMounts("phoenix_hawk")[0].originalHeat == 640u &&
                mw::mech3d::catalogWeaponMounts("locust")[1].originalHeat == 0u &&
                mw::mech3d::catalogWeaponMounts("warhammer")[0].originalHeat == 800u &&
                mw::mech3d::catalogWeaponMounts("shadow_hawk")[0].originalHeat == 80u &&
                mw::mech3d::catalogWeaponMounts("shadow_hawk")[1].originalHeat == 160u &&
                mw::mech3d::catalogWeaponMounts("shadow_hawk")[2].originalHeat == 160u &&
                mw::mech3d::catalogWeaponMounts("jenner")[0].originalHeat == 240u &&
                mw::mech3d::catalogWeaponMounts("warhammer")[2].originalHeat == 320u,
            "BTECH ten-row weapon heat values changed");
        expect(
            !mw::mech3d::catalogWeaponMounts("locust")[0].usesOriginalProjectilePool() &&
                mw::mech3d::catalogWeaponMounts("shadow_hawk")[0]
                        .usesOriginalProjectilePool() &&
                mw::mech3d::catalogWeaponMounts("shadow_hawk")[0]
                        .originalProjectileTypeIndex == 0 &&
                mw::mech3d::catalogWeaponMounts("shadow_hawk")[0]
                        .originalProjectileSpeedRaw == 60u &&
                mw::mech3d::catalogWeaponMounts("shadow_hawk")[1]
                        .originalProjectileDamageClass == 6u &&
                mw::mech3d::catalogWeaponMounts("jenner")[0]
                        .originalProjectileDamageClass == 8u,
            "BTECH weapon-to-projectile rows or decoded projectile fields changed");
        expect(
            mw::mech3d::catalogWeaponMounts("locust")[0]
                    .originalCriticalWeight == 1u &&
                mw::mech3d::catalogWeaponMounts("phoenix_hawk")[0]
                    .originalCriticalWeight == 2u &&
                mw::mech3d::catalogWeaponMounts("warhammer")[0]
                    .originalCriticalWeight == 3u &&
                mw::mech3d::catalogWeaponMounts("shadow_hawk")[0]
                    .originalCriticalWeight == 4u &&
                mw::mech3d::catalogWeaponMounts("warhammer")[2]
                    .originalCriticalWeight == 2u,
            "BTECH original critical-selection weapon weights changed");
        const auto locustCritical =
            mw::mech3d::catalogCriticalProfile("locust");
        const auto jennerCritical =
            mw::mech3d::catalogCriticalProfile("jenner");
        const auto shadowHawkCritical =
            mw::mech3d::catalogCriticalProfile("shadow_hawk");
        const auto riflemanCritical =
            mw::mech3d::catalogCriticalProfile("rifleman");
        const auto phoenixCritical =
            mw::mech3d::catalogCriticalProfile("phoenix_hawk");
        const auto warhammerCritical =
            mw::mech3d::catalogCriticalProfile("warhammer");
        const auto marauderCritical =
            mw::mech3d::catalogCriticalProfile("marauder");
        const auto battleMasterCritical =
            mw::mech3d::catalogCriticalProfile("battlemaster");
        expect(
            locustCritical.ammunitionLocationBtechOrder ==
                    std::array<int8_t, 6>{-1, -1, -1, -1, -1, 4} &&
                jennerCritical.ammunitionLocationBtechOrder[3] == 6 &&
                shadowHawkCritical.ammunitionLocationBtechOrder ==
                    std::array<int8_t, 6>{5, 6, 7, 4, -1, -1} &&
                riflemanCritical.ammunitionLocationBtechOrder[0] == 4 &&
                jennerCritical.jumpJetSlots[0].originalLocationIndex == 6 &&
                jennerCritical.jumpJetSlots[0].originalWeight == 2u &&
                jennerCritical.jumpJetSlots[1].originalLocationIndex == 5 &&
                jennerCritical.jumpJetSlots[2].originalLocationIndex == 4 &&
                jennerCritical.jumpJetSlots[2].originalWeight == 1u &&
                phoenixCritical.ammunitionLocationBtechOrder ==
                    std::array<int8_t, 6>{-1, -1, -1, -1, -1, 4} &&
                phoenixCritical.jumpJetSlots[0].originalLocationIndex == 6 &&
                phoenixCritical.jumpJetSlots[0].originalWeight == 3u &&
                phoenixCritical.jumpJetSlots[1].originalLocationIndex == 5 &&
                phoenixCritical.jumpJetSlots[1].originalWeight == 3u &&
                warhammerCritical.ammunitionLocationBtechOrder ==
                    std::array<int8_t, 6>{-1, -1, -1, -1, 6, 4} &&
                marauderCritical.ammunitionLocationBtechOrder ==
                    std::array<int8_t, 6>{5, -1, -1, -1, -1, -1} &&
                battleMasterCritical.ammunitionLocationBtechOrder ==
                    std::array<int8_t, 6>{-1, -1, -1, -1, 5, 5} &&
                locustCritical.jumpJetSlots[0].originalLocationIndex == -1 &&
                riflemanCritical.jumpJetSlots[0].originalLocationIndex == -1 &&
                warhammerCritical.jumpJetSlots[0].originalLocationIndex == -1 &&
                marauderCritical.jumpJetSlots[0].originalLocationIndex == -1 &&
                battleMasterCritical.jumpJetSlots[0].originalLocationIndex == -1,
            "BTECH per-chassis ammunition/jump critical rows changed");

        mw::battle::BattleStartParams weaponParams = params;
        mw::battle::applyBattleDriveRuntimeTuning(weaponParams);
        weaponParams.objective = {};
        weaponParams.playerStartTransform = {1000.0, 0.0, 1000.0, 0.0};
        weaponParams.mechSystemsSnapshotEnabled = true;
        weaponParams.deterministicCombatRuntimeEnabled = true;
        weaponParams.individualWeaponRuntimeEnabled = true;
        weaponParams.originalProjectileRuntimeEnabled = true;
        weaponParams.originalHeatRuntimeEnabled = true;
        weaponParams.stationaryTargetHitDiagnosticEnabled = true;
        weaponParams.weaponTargetPolicy =
            mw::battle::BattleWeaponTargetPolicy::SelectedScannerTargetProvisional;
        weaponParams.weaponRange = 700.0;
        weaponParams.weaponCooldownTicks = 3;
        weaponParams.weaponDamagePerHit = 1;
        weaponParams.mechSystemMaxDamage = 20;
        weaponParams.projectileHitProfiles =
            mw::battle::loadOriginalBattleProjectileHitProfiles(
                options.snarioPath.parent_path().parent_path() /
                "TBL" / "viewer8" / "OTHPCK.TBL");
        expect(
            weaponParams.projectileHitProfiles.size() == 2u &&
                weaponParams.projectileHitProfiles[0].valid &&
                weaponParams.projectileHitProfiles[1].valid &&
                !weaponParams.projectileHitProfiles[0].triangles.empty() &&
                !weaponParams.projectileHitProfiles[1].triangles.empty() &&
                weaponParams.projectileHitProfiles[0]
                    .originalModelGeometryProven &&
                weaponParams.projectileHitProfiles[1]
                    .originalModelGeometryProven &&
                !weaponParams.projectileHitProfiles[1].spinTimingProven,
            "OTHPCK projectile hit geometry contract changed");

        mw::battle::BattleCombatantLaunchState weaponPlayerLaunch;
        weaponPlayerLaunch.mechPresetId = "locust";
        weaponPlayerLaunch.startTransform = weaponParams.playerStartTransform;
        weaponPlayerLaunch.roster = weaponParams.playerRoster;
        weaponPlayerLaunch.persistentMechState.valid = true;
        weaponPlayerLaunch.persistentMechState.provenance = "phase12_smoke_pristine";
        weaponPlayerLaunch.persistentMechState.heatSinksWorking = 10u;
        weaponPlayerLaunch.persistentMechState.heatSinksTotal = 10u;
        weaponPlayerLaunch.persistentMechState.armorPercent = 100;
        weaponPlayerLaunch.ammunitionStateValid = true;
        weaponPlayerLaunch.ammunitionByPool.fill(4);
        weaponParams.playerLaunchState = weaponPlayerLaunch;

        mw::battle::BattleCombatantLaunchState weaponTargetLaunch;
        weaponTargetLaunch.mechPresetId = "shadow_hawk";
        weaponTargetLaunch.startTransform = {1000.0, 0.0, 1100.0, 0.0};
        weaponTargetLaunch.roster.team = mw::battle::BattleTeam::Opposing;
        weaponTargetLaunch.roster.provenance = "phase12_stationary_target";
        weaponTargetLaunch.roster.sourceSlot = "opposing:stationary";
        weaponTargetLaunch.persistentMechState.valid = true;
        weaponTargetLaunch.persistentMechState.provenance = "phase12_smoke_pristine";
        weaponTargetLaunch.persistentMechState.heatSinksWorking = 10u;
        weaponTargetLaunch.persistentMechState.heatSinksTotal = 10u;
        weaponTargetLaunch.persistentMechState.armorPercent = 100;
        weaponTargetLaunch.ammunitionStateValid = true;
        weaponTargetLaunch.ammunitionByPool.fill(4);
        weaponParams.combatantLaunchStates = {weaponTargetLaunch};

        auto playerFrom = [](const mw::battle::BattleSnapshot& snapshot)
            -> const mw::battle::CombatantSnapshot& {
            const auto found = std::find_if(
                snapshot.combatants.begin(),
                snapshot.combatants.end(),
                [](const mw::battle::CombatantSnapshot& combatant) {
                    return combatant.playerControlled;
                });
            if (found == snapshot.combatants.end()) {
                throw std::runtime_error("phase12 snapshot lost player combatant");
            }
            return *found;
        };
        auto weaponFrom = [](const mw::battle::CombatantSnapshot& combatant, uint32_t id)
            -> const mw::battle::BattleWeaponInstanceState& {
            const auto found = std::find_if(
                combatant.weapons.begin(),
                combatant.weapons.end(),
                [id](const mw::battle::BattleWeaponInstanceState& weapon) {
                    return weapon.weaponInstanceId == id;
                });
            if (found == combatant.weapons.end()) {
                throw std::runtime_error("phase12 snapshot lost weapon instance");
            }
            return *found;
        };
        auto combatantFrom = [](const mw::battle::BattleSnapshot& snapshot,
                                mw::battle::EntityId id)
            -> const mw::battle::CombatantSnapshot& {
            const auto found = std::find_if(
                snapshot.combatants.begin(),
                snapshot.combatants.end(),
                [id](const mw::battle::CombatantSnapshot& combatant) {
                    return combatant.id == id;
                });
            if (found == snapshot.combatants.end()) {
                throw std::runtime_error("phase12 snapshot lost target combatant");
            }
            return *found;
        };

        mw::battle::BattleStartParams warhammerAmmunitionParams = weaponParams;
        mw::battle::BattleCombatantLaunchState warhammerLaunch =
            weaponPlayerLaunch;
        warhammerLaunch.mechPresetId = "warhammer";
        warhammerLaunch.persistentMechState.heatSinksWorking = 18u;
        warhammerLaunch.persistentMechState.heatSinksTotal = 18u;
        warhammerLaunch.ammunitionByPool = {{0, 0, 0, 0, 15, 200}};
        warhammerAmmunitionParams.playerMechPresetId = "warhammer";
        warhammerAmmunitionParams.playerLaunchState = warhammerLaunch;
        warhammerAmmunitionParams.combatantLaunchStates.clear();
        const mw::battle::BattleWorld warhammerAmmunitionWorld =
            mw::battle::BattleWorld::create(warhammerAmmunitionParams);
        const auto& warhammerAmmunitionPlayer = playerFrom(
            warhammerAmmunitionWorld.missionStartSnapshot());
        const auto& warhammerSrm6 =
            weaponFrom(warhammerAmmunitionPlayer, 3u);
        const auto& warhammerRightMachineGun =
            weaponFrom(warhammerAmmunitionPlayer, 8u);
        const auto& warhammerLeftMachineGun =
            weaponFrom(warhammerAmmunitionPlayer, 9u);
        expect(
            warhammerSrm6.weaponTypeId == "srm6" &&
                warhammerSrm6.ammunitionPoolIndex == 4 &&
                warhammerSrm6.ammunitionRemaining == 15,
            "Warhammer SRM6 must launch with original ammunition 15");
        expect(
            warhammerRightMachineGun.weaponTypeId == "machine_gun" &&
                warhammerLeftMachineGun.weaponTypeId == "machine_gun" &&
                warhammerRightMachineGun.ammunitionPoolIndex == 5 &&
                warhammerLeftMachineGun.ammunitionPoolIndex == 5 &&
                warhammerRightMachineGun.ammunitionRemaining == 200 &&
                warhammerLeftMachineGun.ammunitionRemaining == 200,
            "Warhammer machine guns must share original ammunition 200");

        mw::battle::BattleWorld weaponWorld = mw::battle::BattleWorld::create(weaponParams);
        const auto& weaponStartPlayer = playerFrom(weaponWorld.missionStartSnapshot());
        const auto& weaponStartTarget =
            combatantFrom(weaponWorld.missionStartSnapshot(), mw::battle::EntityId{2u});
        const auto& startTargetCenterTorso =
            weaponStartTarget.detailedDamage.armorSections[static_cast<size_t>(
                mw::mech3d::MechArmorSectionId::CenterTorso)];
        const auto& startTargetCenterInternal =
            weaponStartTarget.detailedDamage.internalSections[static_cast<size_t>(
                mw::mech3d::MechInternalSectionId::CenterTorso)];
        expect(startTargetCenterTorso.armorMaximum == 23u &&
                   startTargetCenterTorso.armorRemaining == 23u &&
                   startTargetCenterInternal.structureMaximum == 6u &&
                   startTargetCenterInternal.structureRemaining == 6u,
               "Shadow Hawk original center-torso armor/internal maxima changed");
        expect(weaponStartPlayer.weapons.size() == 3u, "Locust snapshot weapon count changed");
        expect(weaponStartPlayer.selectedWeaponInstanceId == 1u,
               "first functional installed weapon should start selected");
        expect(weaponStartPlayer.heat.enabled &&
                   weaponStartPlayer.heat.rawHeat == 0 &&
                   weaponStartPlayer.heat.originalWeaponHeatAdditionProven &&
                   weaponStartPlayer.heat.originalNormalCoolingFormulaProven &&
                   !weaponStartPlayer.heat.originalUpdateCadenceProven &&
                   weaponStartPlayer.heat.coolingPolicy ==
                       mw::battle::BattleHeatCoolingPolicy::
                           OriginalCoolingWithArcticDouble,
               "original heat state/evidence boundary changed");
        const auto& mediumLaserStart = weaponFrom(weaponStartPlayer, 1u);
        expect(mediumLaserStart.weaponTypeId == "medium_laser" &&
                   mediumLaserStart.locationId == "CT" &&
                   mediumLaserStart.displayRangeClass == 'M',
               "Locust energy weapon identity or mount changed");
        expect(mediumLaserStart.ammunitionOwnership ==
                   mw::battle::BattleWeaponAmmunitionOwnership::EnergyNoAmmunition &&
                   mediumLaserStart.ammunitionStateKnown &&
                   mediumLaserStart.ammunitionRemaining == 0,
               "energy weapon must not invent ammunition");
        expect(mediumLaserStart.originalCooldownUpdateCount == 30u &&
                   mediumLaserStart.cooldownTicksOnFire == 60u &&
                   mediumLaserStart.originalCooldownValueProven &&
                   !mediumLaserStart.originalCooldownCadenceProven,
               "medium-laser original cooldown count or fixed-tick conversion changed");
        expect(mediumLaserStart.originalDamage == 5u &&
                   mediumLaserStart.originalDamageValueProven &&
                   mediumLaserStart.originalHeat == 240u &&
                   mediumLaserStart.originalHeatValueProven &&
                   mediumLaserStart.originalRangeWords ==
                       std::array<uint16_t, 4>{0u, 4u, 7u, 10u} &&
                   mediumLaserStart.maximumRange == 4800.0,
               "medium-laser proven original runtime fields changed");
        const auto& machineGunStart = weaponFrom(weaponStartPlayer, 2u);
        expect(machineGunStart.weaponTypeId == "machine_gun" &&
                   machineGunStart.locationId == "RA" &&
                   machineGunStart.displayRangeClass == 'S',
               "Locust ballistic weapon identity or mount changed");
        expect(machineGunStart.ammunitionOwnership ==
                   mw::battle::BattleWeaponAmmunitionOwnership::SharedAmmunitionPool &&
                   machineGunStart.ammunitionPoolIndex == 5 &&
                   machineGunStart.ammunitionRemaining == 4,
               "machine-gun ammunition ownership changed");
        expect(machineGunStart.originalCooldownUpdateCount == 2u &&
                   machineGunStart.cooldownTicksOnFire == 4u &&
                   machineGunStart.originalCooldownValueProven,
               "machine-gun original cooldown count or fixed-tick conversion changed");

        mw::battle::BattleInputCommand selectAndFire;
        selectAndFire.tickIndex = 0;
        selectAndFire.entityId = weaponWorld.playerEntityId();
        selectAndFire.selectWeaponInstanceId = 2u;
        selectAndFire.cycleTargetScan = true;
        selectAndFire.fireWeapon = true;
        weaponWorld.enqueueInput(selectAndFire);
        weaponWorld.tick();
        const mw::battle::BattleSnapshot firstShot = weaponWorld.snapshot();
        const auto& firstShotPlayer = playerFrom(firstShot);
        const auto& firstShotWeapon = weaponFrom(firstShotPlayer, 2u);
        expect(firstShot.tickIndex == 1u, "weapon input must apply on one simulation tick boundary");
        expect(firstShotPlayer.selectedWeaponInstanceId == 2u && firstShotWeapon.selected,
               "direct weapon selection did not become authoritative at the tick boundary");
        expect(firstShotPlayer.lastFireRequestTickIndex == 0u &&
                   firstShotPlayer.lastFireRequestWeaponInstanceId == 2u &&
                   firstShotPlayer.lastFireRequestAccepted,
               "accepted fire request diagnostic changed");
        expect(firstShotWeapon.cooldownTicksRemaining == 4u &&
                   firstShotWeapon.readiness == mw::battle::BattleWeaponReadiness::Cooldown,
               "fired weapon cooldown/readiness changed");
        expect(firstShotWeapon.ammunitionRemaining == 3 && firstShotWeapon.shotsFired == 1u,
               "proven ballistic ammunition decrement changed");
        expect(firstShot.lastShot.valid && firstShot.lastShot.sequence == 1u &&
                   firstShot.lastShot.tickIndex == 0u &&
                   firstShot.lastShot.shooterEntityId == firstShotPlayer.id &&
                   firstShot.lastShot.weaponInstanceId == 2u &&
                   firstShot.lastShot.targetKind ==
                       mw::battle::BattleShotTargetKind::Combatant &&
                   firstShot.lastShot.targetEntityId.value == 2u &&
                   firstShot.lastShot.result == mw::battle::BattleShotResult::Hit &&
                   firstShot.lastShot.rangeGatePassed &&
                   firstShot.lastShot.hit,
               "selected stationary target authoritative shot result changed");
        expect(firstShot.impactEvents.size() == 1u &&
                   firstShot.impactEvents.front().shotSequence ==
                       firstShot.lastShot.sequence &&
                   firstShot.impactEvents.front().weaponInstanceId == 2u &&
                   firstShot.impactEvents.front().deliveryState ==
                       mw::battle::BattleShotDeliveryState::Immediate &&
                   firstShot.tickIndex -
                       firstShot.impactEvents.front().impactTickIndex == 1u,
               "machine-gun hit must publish its original temporary-effect event");
        expect(firstShot.lastShot.hitLocation ==
                   mw::battle::BattleShotHitLocation::CenterTorsoProvisionalStationaryLab &&
                   !firstShot.lastShot.hitLocationProven &&
                   firstShot.lastShot.armorSection ==
                       mw::mech3d::MechArmorSectionId::CenterTorso &&
                   firstShot.lastShot.runtimeEffect ==
                       mw::battle::BattleShotRuntimeEffect::DetailedArmorSectionDamage &&
                   firstShot.lastShot.affectedSystem == mw::battle::BattleMechSystemRole::Core &&
                   firstShot.lastShot.originalDamage == 2u &&
                   firstShot.lastShot.originalDamageValueProven &&
                   firstShot.lastShot.damageApplied == 2,
               "stationary hit must apply the proven weapon value to the typed provisional section");
        const auto& firstShotTarget =
            combatantFrom(firstShot, firstShot.lastShot.targetEntityId);
        const auto& centerTorso = firstShotTarget.detailedDamage.armorSections[
            static_cast<size_t>(mw::mech3d::MechArmorSectionId::CenterTorso)];
        const auto coarseCore = std::find_if(
            firstShotTarget.mechSystems.systems.begin(),
            firstShotTarget.mechSystems.systems.end(),
            [](const mw::battle::BattleMechSystemSnapshot& system) {
                return system.role == mw::battle::BattleMechSystemRole::Core;
            });
        expect(firstShotTarget.detailedDamage.valid && centerTorso.battleDamage == 2 &&
                   centerTorso.armorRemaining == 21u &&
                   firstShot.lastShot.armorBefore == 23 &&
                   firstShot.lastShot.armorDamageApplied == 2 &&
                   firstShot.lastShot.armorAfter == 21 &&
                   firstShot.lastShot.internalBefore == 6 &&
                   firstShot.lastShot.internalDamageApplied == 0 &&
                   firstShot.lastShot.internalAfter == 6 &&
                   coarseCore != firstShotTarget.mechSystems.systems.end() &&
                   coarseCore->damage == 2,
               "detailed center-torso state and compatibility projection diverged");

        mw::battle::BattleStartParams entryDamageParams = weaponParams;
        entryDamageParams.combatantLaunchStates[0].persistentMechState.armorDamage[
            static_cast<size_t>(mw::mech3d::MechArmorSectionId::CenterTorso)] = 1u;
        entryDamageParams.combatantLaunchStates[0].persistentMechState.armorDamage[
            static_cast<size_t>(mw::mech3d::MechArmorSectionId::CenterRear)] = 2u;
        const mw::battle::BattleSnapshot entryDamageStart =
            mw::battle::BattleWorld::create(entryDamageParams).missionStartSnapshot();
        const auto& entryDamageTarget =
            combatantFrom(entryDamageStart, mw::battle::EntityId{2u});
        expect(entryDamageTarget.detailedDamage.armorSections[static_cast<size_t>(
                   mw::mech3d::MechArmorSectionId::CenterTorso)].armorRemaining == 13u &&
                   entryDamageTarget.detailedDamage.armorSections[static_cast<size_t>(
                       mw::mech3d::MechArmorSectionId::CenterRear)].armorRemaining == 2u &&
                   entryDamageTarget.detailedDamage.internalSections[static_cast<size_t>(
                       mw::mech3d::MechInternalSectionId::CenterTorso)]
                           .structureRemaining == 6u,
               "original campaign-level to armor/internal point conversion changed");

        mw::battle::BattleInputCommand cooldownFire;
        cooldownFire.tickIndex = 1;
        cooldownFire.entityId = weaponWorld.playerEntityId();
        cooldownFire.fireWeapon = true;
        weaponWorld.enqueueInput(cooldownFire);
        weaponWorld.tick();
        const mw::battle::BattleSnapshot cooldownSnapshot = weaponWorld.snapshot();
        const auto& cooldownPlayer = playerFrom(cooldownSnapshot);
        const auto& cooldownWeapon = weaponFrom(cooldownPlayer, 2u);
        expect(!cooldownPlayer.lastFireRequestAccepted && cooldownWeapon.shotsFired == 1u &&
                   cooldownWeapon.cooldownTicksRemaining == 3u,
               "cooldown weapon must reject repeated tick-owned fire");

        mw::battle::BattleInputCommand cycleSelection;
        cycleSelection.tickIndex = 2;
        cycleSelection.entityId = weaponWorld.playerEntityId();
        cycleSelection.selectedWeaponStepDelta = -1;
        weaponWorld.enqueueInput(cycleSelection);
        weaponWorld.tick();
        expect(playerFrom(weaponWorld.snapshot()).selectedWeaponInstanceId == 1u,
               "tick-owned weapon cycling changed");

        mw::battle::BattleInputCommand energyFire;
        energyFire.tickIndex = 3;
        energyFire.entityId = weaponWorld.playerEntityId();
        energyFire.fireWeapon = true;
        weaponWorld.enqueueInput(energyFire);
        weaponWorld.tick();
        const mw::battle::BattleSnapshot energySnapshot = weaponWorld.snapshot();
        const auto& energyPlayer = playerFrom(energySnapshot);
        expect(weaponFrom(energyPlayer, 1u).shotsFired == 1u &&
                   weaponFrom(energyPlayer, 2u).ammunitionRemaining == 3,
               "energy fire must not consume ballistic ammunition");
        expect(weaponFrom(energyPlayer, 1u).cooldownTicksRemaining == 60u &&
                   weaponFrom(energyPlayer, 1u).readiness ==
                       mw::battle::BattleWeaponReadiness::Cooldown,
               "laser must use its independent original cooldown count");
        expect(energyPlayer.heat.rawHeat == 227 &&
                   energyPlayer.heat.lastWeaponHeatAdded == 240 &&
                   energyPlayer.heat.lastCoolingApplied == 13 &&
                   energyPlayer.heat.lastCoolingTickIndex == 3u &&
                   std::abs(energyPlayer.heat.originalUpdateAccumulator) <
                       0.000001,
               "medium-laser heat or BTECH normal sink cooling formula changed");
        const auto& energyTarget =
            combatantFrom(energySnapshot, energySnapshot.lastShot.targetEntityId);
        expect(energySnapshot.lastShot.originalDamage == 5u &&
                   energySnapshot.lastShot.damageApplied == 5 &&
                   energyTarget.detailedDamage.armorSections[
                       static_cast<size_t>(
                           mw::mech3d::MechArmorSectionId::CenterTorso)]
                           .battleDamage == 7,
               "laser smoke must apply its own proven damage to the shared detailed section");

        mw::battle::BattleStartParams centerTorsoKillParams = weaponParams;
        centerTorsoKillParams.combatantLaunchStates[0].mechPresetId =
            "locust";
        mw::battle::BattleWorld centerTorsoKillWorld =
            mw::battle::BattleWorld::create(centerTorsoKillParams);
        const auto centerTorsoKillStart = centerTorsoKillWorld.snapshot();
        const auto& centerTorsoKillTargetStart =
            combatantFrom(centerTorsoKillStart, mw::battle::EntityId{2u});
        const auto& centerTorsoKillArmor =
            centerTorsoKillTargetStart.detailedDamage.armorSections[
                static_cast<size_t>(
                    mw::mech3d::MechArmorSectionId::CenterTorso)];
        const auto& centerTorsoKillInternal =
            centerTorsoKillTargetStart.detailedDamage.internalSections[
                static_cast<size_t>(
                    mw::mech3d::MechInternalSectionId::CenterTorso)];
        const uint64_t centerTorsoShotCount = static_cast<uint64_t>(
            (centerTorsoKillArmor.armorRemaining +
             centerTorsoKillInternal.structureRemaining + 4u) / 5u);
        for (uint64_t shotIndex = 0u;
             shotIndex < centerTorsoShotCount;
             ++shotIndex) {
            mw::battle::BattleInputCommand fire;
            fire.tickIndex = shotIndex * 60u;
            fire.entityId = centerTorsoKillWorld.playerEntityId();
            fire.selectWeaponInstanceId = 1u;
            fire.cycleTargetScan = shotIndex == 0u;
            fire.fireWeapon = true;
            centerTorsoKillWorld.enqueueInput(fire);
        }
        centerTorsoKillWorld.runTicks(
            (centerTorsoShotCount - 1u) * 60u + 1u);
        const auto centerTorsoKilled = centerTorsoKillWorld.snapshot();
        const auto& centerTorsoKilledTarget =
            combatantFrom(centerTorsoKilled, mw::battle::EntityId{2u});
        expect(centerTorsoKilledTarget.detailedDamage.internalSections[
                   static_cast<size_t>(
                       mw::mech3d::MechInternalSectionId::CenterTorso)]
                       .structureRemaining == 0u &&
                   centerTorsoKilledTarget.mechDestroyed &&
                   centerTorsoKilledTarget.missionStatus ==
                       mw::battle::CombatantMissionStatus::Destroyed &&
                   centerTorsoKilledTarget.activeAnimationId == "death" &&
                   !containsId(centerTorsoKilledTarget.hiddenComponentIds, 2),
               "zero center-torso structure must kill the mech without hiding its torso");
        expect(centerTorsoKilled.missionRuntimeState ==
                   mw::battle::BattleMissionRuntimeState::InProgress &&
                   centerTorsoKilled.conclusionDelay.active &&
                   centerTorsoKilled.conclusionDelay.pendingState ==
                       mw::battle::BattleMissionRuntimeState::Victory &&
                   centerTorsoKilled.conclusionDelay.resolveTickIndex -
                           centerTorsoKilled.conclusionDelay.startTickIndex ==
                       100u,
               "last-mech destruction must leave a five-second conclusion window");
        centerTorsoKillWorld.runTicks(20u);
        const uint64_t centerTorsoDeathPoseElapsed =
            combatantFrom(centerTorsoKillWorld.snapshot(),
                          mw::battle::EntityId{2u})
                .walkAnimationElapsedMs;
        expect(centerTorsoDeathPoseElapsed > 0u,
               "center-torso kill must visibly advance the death animation");
        const auto centerTorsoDelayFrame = centerTorsoKillWorld.snapshot();
        const uint64_t centerTorsoTicksToResult =
            centerTorsoDelayFrame.conclusionDelay.resolveTickIndex >=
                    centerTorsoDelayFrame.tickIndex
                ? centerTorsoDelayFrame.conclusionDelay.resolveTickIndex -
                      centerTorsoDelayFrame.tickIndex + 1u
                : 0u;
        centerTorsoKillWorld.runTicks(centerTorsoTicksToResult);
        const auto centerTorsoVictory = centerTorsoKillWorld.snapshot();
        expect(centerTorsoVictory.result.valid &&
                   centerTorsoVictory.result.state ==
                       mw::battle::BattleMissionRuntimeState::Victory,
               "center-torso kill conclusion delay must resolve to victory");

        mw::battle::BattleStartParams damagedSinkParams = weaponParams;
        damagedSinkParams.playerLaunchState->persistentMechState.heatSinksWorking = 4u;
        mw::battle::BattleWorld damagedSinkWorld =
            mw::battle::BattleWorld::create(damagedSinkParams);
        mw::battle::BattleInputCommand damagedSinkFire;
        damagedSinkFire.tickIndex = 0u;
        damagedSinkFire.entityId = damagedSinkWorld.playerEntityId();
        damagedSinkFire.selectWeaponInstanceId = 1u;
        damagedSinkFire.cycleTargetScan = true;
        damagedSinkFire.fireWeapon = true;
        damagedSinkWorld.enqueueInput(damagedSinkFire);
        damagedSinkWorld.tick();
        expect(playerFrom(damagedSinkWorld.snapshot()).heat.rawHeat == 240,
               "weapon heat must be visible before the first compatibility cooling update");
        damagedSinkWorld.tick();
        const mw::battle::BattleSnapshot damagedSinkSnapshot =
            damagedSinkWorld.snapshot();
        const auto& damagedSinkPlayer = playerFrom(damagedSinkSnapshot);
        const auto& damagedHeatSinks =
            damagedSinkPlayer.detailedDamage.criticalComponents[
                static_cast<size_t>(
                    mw::mech3d::MechCriticalComponentId::HeatSinks)];
        expect(damagedHeatSinks.workingCount == 4u &&
                   damagedHeatSinks.totalCount == 10u &&
                   damagedSinkPlayer.heat.rawHeat == 235 &&
                   damagedSinkPlayer.heat.lastCoolingApplied == 5,
               "heat cooling must read the authoritative detailed working-sink count");

        mw::battle::BattleStartParams arcticCoolingParams = damagedSinkParams;
        arcticCoolingParams.terrainEnvironmentId = 2;
        mw::battle::BattleWorld arcticCoolingWorld =
            mw::battle::BattleWorld::create(arcticCoolingParams);
        damagedSinkFire.entityId = arcticCoolingWorld.playerEntityId();
        arcticCoolingWorld.enqueueInput(damagedSinkFire);
        arcticCoolingWorld.tick();
        arcticCoolingWorld.tick();
        const mw::battle::BattleSnapshot arcticCoolingSnapshot =
            arcticCoolingWorld.snapshot();
        const auto& arcticCoolingPlayer = playerFrom(arcticCoolingSnapshot);
        expect(arcticCoolingPlayer.heat.rawHeat == 230 &&
                   arcticCoolingPlayer.heat.lastCoolingApplied == 10 &&
                   arcticCoolingPlayer.heat.lastCoolingMultiplier == 2u &&
                   arcticCoolingPlayer.heat.originalArcticDoubleCoolingProven,
               "arctic environment must double the proven working-sink cooling term");

        mw::battle::BattleStartParams movementHeatParams = weaponParams;
        movementHeatParams.combatantLaunchStates.clear();
        movementHeatParams.acceleration = 2500.0;
        movementHeatParams.playerLaunchState->persistentMechState.heatSinksWorking = 0u;
        movementHeatParams.playerLaunchState->persistentMechState.heatSinksTotal = 10u;
        mw::battle::BattleWorld movementHeatWorld =
            mw::battle::BattleWorld::create(movementHeatParams);
        mw::battle::BattleInputCommand accelerate;
        accelerate.tickIndex = 0u;
        accelerate.entityId = movementHeatWorld.playerEntityId();
        accelerate.throttle = 1.0;
        movementHeatWorld.enqueueInput(accelerate);
        movementHeatWorld.runTicks(2u);
        const mw::battle::BattleSnapshot oneThirdSnapshot =
            movementHeatWorld.snapshot();
        const auto& oneThirdHeat = playerFrom(oneThirdSnapshot).heat;
        expect(oneThirdHeat.rawHeat == 0 &&
                   oneThirdHeat.lastGroundMovementHeatAdded == 0,
               "ground movement heat must use a strict one-third threshold");
        movementHeatWorld.runTicks(2u);
        const mw::battle::BattleSnapshot twoThirdsSnapshot =
            movementHeatWorld.snapshot();
        const auto& twoThirdsHeat = playerFrom(twoThirdsSnapshot).heat;
        expect(twoThirdsHeat.rawHeat == 1 &&
                   twoThirdsHeat.lastGroundMovementHeatAdded == 1 &&
                   twoThirdsHeat.lastGroundMovementHeatTickIndex == 3u,
               "exactly two-thirds speed must remain in the original +1 band");
        movementHeatWorld.runTicks(2u);
        const mw::battle::BattleSnapshot fullSpeedSnapshot =
            movementHeatWorld.snapshot();
        const auto& fullSpeedHeat = playerFrom(fullSpeedSnapshot).heat;
        expect(fullSpeedHeat.rawHeat == 3 &&
                   fullSpeedHeat.lastGroundMovementHeatAdded == 2 &&
                   fullSpeedHeat.lastGroundMovementHeatTickIndex == 5u &&
                   fullSpeedHeat.originalGroundMovementHeatThresholdsProven &&
                   !fullSpeedHeat.groundMovementSpeedMappingProven,
               "ground movement above two-thirds speed must add two raw heat");
        mw::battle::BattleReplay movementHeatReplay;
        accelerate.entityId = {};
        movementHeatReplay.commands = {accelerate};
        const mw::battle::BattleSnapshot movementHeatReplayA =
            mw::battle::runBattleReplay(
                movementHeatParams, movementHeatReplay, 6u);
        const mw::battle::BattleSnapshot movementHeatReplayB =
            mw::battle::runBattleReplay(
                movementHeatParams, movementHeatReplay, 6u);
        expect(
            mw::battle::battleSnapshotFingerprint(movementHeatReplayA) ==
                mw::battle::battleSnapshotFingerprint(movementHeatReplayB) &&
                playerFrom(movementHeatReplayA).heat.rawHeat == 3,
            "ground movement heat replay fingerprint changed");
        mw::battle::BattleWorld movementHeatCadenceWorld =
            mw::battle::BattleWorld::create(movementHeatParams);
        accelerate.entityId = movementHeatCadenceWorld.playerEntityId();
        movementHeatCadenceWorld.enqueueInput(accelerate);
        for (uint64_t tick = 0u; tick < 6u; ++tick) {
            for (int frame = 0; frame < 31; ++frame) {
                static_cast<void>(movementHeatCadenceWorld.snapshot());
            }
            movementHeatCadenceWorld.tick();
        }
        expect(
            mw::battle::battleSnapshotFingerprint(
                movementHeatCadenceWorld.snapshot()) ==
                mw::battle::battleSnapshotFingerprint(movementHeatReplayA),
            "render cadence changed battle-owned ground movement heat");

        mw::battle::BattleStartParams reverseHeatParams = movementHeatParams;
        reverseHeatParams.maxReverseSpeed = reverseHeatParams.maxForwardSpeed;
        mw::battle::BattleWorld reverseHeatWorld =
            mw::battle::BattleWorld::create(reverseHeatParams);
        accelerate.entityId = reverseHeatWorld.playerEntityId();
        accelerate.throttle = -1.0;
        reverseHeatWorld.enqueueInput(accelerate);
        reverseHeatWorld.runTicks(6u);
        const mw::battle::BattleSnapshot reverseHeatSnapshot =
            reverseHeatWorld.snapshot();
        expect(playerFrom(reverseHeatSnapshot).heat.rawHeat == 3 &&
                   playerFrom(reverseHeatSnapshot).heat
                       .lastGroundMovementHeatAdded == 2,
               "ground movement heat must use absolute reverse speed");

        mw::battle::BattleStartParams airborneHeatParams = movementHeatParams;
        airborneHeatParams.playerLaunchState->mechPresetId = "jenner";
        airborneHeatParams.playerLaunchState->persistentMechState.jumpJetsWorking = 3u;
        airborneHeatParams.playerLaunchState->persistentMechState.jumpJetsTotal = 3u;
        mw::battle::BattleWorld airborneHeatWorld =
            mw::battle::BattleWorld::create(airborneHeatParams);
        accelerate.entityId = airborneHeatWorld.playerEntityId();
        accelerate.throttle = 1.0;
        airborneHeatWorld.enqueueInput(accelerate);
        airborneHeatWorld.runTicks(6u);
        mw::battle::BattleInputCommand launchAtSpeed;
        launchAtSpeed.tickIndex = 6u;
        launchAtSpeed.entityId = airborneHeatWorld.playerEntityId();
        launchAtSpeed.jumpJetToggle = true;
        airborneHeatWorld.enqueueInput(launchAtSpeed);
        airborneHeatWorld.runTicks(2u);
        const mw::battle::BattleSnapshot airborneHeatSnapshot =
            airborneHeatWorld.snapshot();
        const auto& airborneHeat = playerFrom(airborneHeatSnapshot);
        expect(airborneHeat.airborne &&
                   airborneHeat.forwardSpeed >
                       airborneHeat.maxForwardSpeed * (2.0 / 3.0) &&
                   airborneHeat.heat.lastGroundMovementHeatAdded == 0 &&
                   airborneHeat.heat.lastJumpJetHeatAdded ==
                       mw::battle::battleOriginalVerticalJumpRawHeat(),
               "airborne state must suppress ground movement heat");

        mw::battle::BattleHeatState shutdownFlashProbe;
        shutdownFlashProbe.enabled = true;
        shutdownFlashProbe.reactorShutdown = true;
        shutdownFlashProbe.originalUpdateCount = 3u;
        expect(!mw::battle::battleReactorShutdownMessageVisible(
                   shutdownFlashProbe),
               "reactor shutdown warning must be hidden before bit-2 phase");
        shutdownFlashProbe.originalUpdateCount = 4u;
        expect(mw::battle::battleReactorShutdownMessageVisible(
                   shutdownFlashProbe),
               "reactor shutdown warning must appear for original updates 4..7");
        shutdownFlashProbe.originalUpdateCount = 7u;
        expect(mw::battle::battleReactorShutdownMessageVisible(
                   shutdownFlashProbe),
               "reactor shutdown warning visible phase shortened");
        shutdownFlashProbe.originalUpdateCount = 8u;
        expect(!mw::battle::battleReactorShutdownMessageVisible(
                   shutdownFlashProbe),
               "reactor shutdown warning must hide when original bit 2 clears");

        mw::battle::BattleStartParams heatShutdownParams = weaponParams;
        heatShutdownParams.playerMechPresetId = "warhammer";
        heatShutdownParams.playerLaunchState->mechPresetId = "warhammer";
        heatShutdownParams.playerLaunchState->persistentMechState.heatSinksWorking = 0u;
        heatShutdownParams.playerLaunchState->persistentMechState.heatSinksTotal = 18u;
        heatShutdownParams.playerLaunchState->ammunitionByPool.fill(20);
        mw::battle::BattleReplay heatShutdownReplay;
        for (const auto [tick, weaponId] :
             std::array<std::pair<uint64_t, uint32_t>, 3>{{
                 {0u, 1u},
                 {1u, 2u},
                 {2u, 4u},
             }}) {
            mw::battle::BattleInputCommand command;
            command.tickIndex = tick;
            command.selectWeaponInstanceId = weaponId;
            command.cycleTargetScan = tick == 0u;
            command.fireWeapon = true;
            heatShutdownReplay.commands.push_back(command);
        }
        mw::battle::BattleInputCommand blockedShutdownCommand;
        blockedShutdownCommand.tickIndex = 4u;
        blockedShutdownCommand.selectWeaponInstanceId = 5u;
        blockedShutdownCommand.throttle = 1.0;
        blockedShutdownCommand.jumpJetToggle = true;
        blockedShutdownCommand.jumpForwardThrust = true;
        blockedShutdownCommand.fireWeapon = true;
        heatShutdownReplay.commands.push_back(blockedShutdownCommand);

        mw::battle::BattleWorld heatShutdownWorld =
            mw::battle::BattleWorld::create(heatShutdownParams);
        for (mw::battle::BattleInputCommand command :
             heatShutdownReplay.commands) {
            command.entityId = heatShutdownWorld.playerEntityId();
            heatShutdownWorld.enqueueInput(command);
        }
        heatShutdownWorld.runTicks(2u);
        const mw::battle::BattleSnapshot exactThresholdSnapshot =
            heatShutdownWorld.snapshot();
        const auto& exactThresholdPlayer =
            playerFrom(exactThresholdSnapshot);
        expect(exactThresholdPlayer.heat.rawHeat ==
                   mw::battle::battleOriginalReactorShutdownRawHeat() &&
                   !exactThresholdPlayer.heat.reactorShutdown,
               "original reactor must remain online at exactly raw heat 0x640");
        heatShutdownWorld.runTicks(2u);
        const mw::battle::BattleSnapshot shutdownSnapshot =
            heatShutdownWorld.snapshot();
        const auto& shutdownPlayer = playerFrom(shutdownSnapshot);
        expect(shutdownPlayer.heat.rawHeat == 1840 &&
                   shutdownPlayer.heat.reactorShutdown &&
                   shutdownPlayer.heat.reactorShutdownStateChangedTickIndex == 3u &&
                   shutdownPlayer.heat.originalUpdateCount == 2u &&
                   shutdownPlayer.heat.originalReactorShutdownThresholdProven &&
                   shutdownPlayer.heat.originalReactorShutdownRecoveryProven &&
                   shutdownPlayer.heat.originalReactorShutdownMessageCadenceProven &&
                   shutdownPlayer.weaponShotsFired == 3u &&
                   weaponFrom(shutdownPlayer, 4u).readiness ==
                       mw::battle::BattleWeaponReadiness::MechOffline,
               "heat above 0x640 must authoritatively shut down the reactor");
        heatShutdownWorld.tick();
        const mw::battle::BattleSnapshot blockedShutdownSnapshot =
            heatShutdownWorld.snapshot();
        const auto& blockedShutdownPlayer =
            playerFrom(blockedShutdownSnapshot);
        expect(blockedShutdownPlayer.weaponShotsFired == 3u &&
                   blockedShutdownPlayer.forwardSpeed == 0.0 &&
                   blockedShutdownPlayer.targetForwardSpeed == 0.0 &&
                   !blockedShutdownPlayer.jumpJetsEnabled &&
                   !blockedShutdownPlayer.jumpJetThrusting,
               "reactor shutdown must reject fire and stop movement/jump thrust");

        mw::battle::BattleStartParams jumpShutdownParams = heatShutdownParams;
        jumpShutdownParams.playerMechPresetId = "jenner";
        jumpShutdownParams.playerLaunchState->mechPresetId = "jenner";
        jumpShutdownParams.playerLaunchState->persistentMechState.engine = 3u;
        jumpShutdownParams.playerLaunchState->persistentMechState.jumpJetsWorking = 3u;
        jumpShutdownParams.playerLaunchState->persistentMechState.jumpJetsTotal = 3u;
        mw::battle::BattleWorld jumpShutdownWorld =
            mw::battle::BattleWorld::create(jumpShutdownParams);
        jumpShutdownWorld.runTicks(216u);
        const mw::battle::BattleSnapshot jumpShutdownHotSnapshot =
            jumpShutdownWorld.snapshot();
        expect(playerFrom(jumpShutdownHotSnapshot).jumpCapable &&
                   playerFrom(jumpShutdownHotSnapshot).heat.reactorShutdown,
               "jump shutdown smoke must reach overheat on a jump-capable mech");
        mw::battle::BattleInputCommand blockedJumpCommand;
        blockedJumpCommand.tickIndex = jumpShutdownWorld.tickIndex();
        blockedJumpCommand.entityId = jumpShutdownWorld.playerEntityId();
        blockedJumpCommand.jumpJetToggle = true;
        blockedJumpCommand.jumpForwardThrust = true;
        jumpShutdownWorld.enqueueInput(blockedJumpCommand);
        jumpShutdownWorld.tick();
        const mw::battle::BattleSnapshot blockedJumpSnapshot =
            jumpShutdownWorld.snapshot();
        expect(!playerFrom(blockedJumpSnapshot).jumpJetsEnabled &&
                   !playerFrom(blockedJumpSnapshot).jumpJetThrusting &&
                   !playerFrom(blockedJumpSnapshot).airborne,
               "reactor shutdown must reject jump activation on a capable mech");
        heatShutdownWorld.runTicks(3u);
        const mw::battle::BattleSnapshot visibleShutdownSnapshot =
            heatShutdownWorld.snapshot();
        const auto& visibleShutdownPlayer =
            playerFrom(visibleShutdownSnapshot);
        expect(visibleShutdownPlayer.heat.originalUpdateCount == 4u &&
                   mw::battle::battleReactorShutdownMessageVisible(
                       visibleShutdownPlayer.heat),
               "reactor shutdown warning did not follow original bit-2 cadence");

        const mw::battle::BattleSnapshot heatShutdownReplaySnapshot =
            mw::battle::runBattleReplay(
                heatShutdownParams, heatShutdownReplay, 8u);
        expect(mw::battle::battleSnapshotFingerprint(
                   heatShutdownReplaySnapshot) ==
                   mw::battle::battleSnapshotFingerprint(
                       heatShutdownWorld.snapshot()),
               "reactor shutdown replay fingerprint changed");
        mw::battle::BattleWorld heatShutdownCadenceWorld =
            mw::battle::BattleWorld::create(heatShutdownParams);
        for (mw::battle::BattleInputCommand command :
             heatShutdownReplay.commands) {
            command.entityId = heatShutdownCadenceWorld.playerEntityId();
            heatShutdownCadenceWorld.enqueueInput(command);
        }
        for (uint64_t tick = 0u; tick < 8u; ++tick) {
            for (int frame = 0; frame < 29; ++frame) {
                static_cast<void>(heatShutdownCadenceWorld.snapshot());
            }
            heatShutdownCadenceWorld.tick();
        }
        expect(mw::battle::battleSnapshotFingerprint(
                   heatShutdownCadenceWorld.snapshot()) ==
                   mw::battle::battleSnapshotFingerprint(
                       heatShutdownReplaySnapshot),
               "render cadence changed authoritative reactor shutdown state");

        mw::battle::BattleStartParams heatRecoveryParams = heatShutdownParams;
        heatRecoveryParams.playerLaunchState->persistentMechState.heatSinksWorking = 10u;
        mw::battle::BattleWorld heatRecoveryWorld =
            mw::battle::BattleWorld::create(heatRecoveryParams);
        for (size_t index = 0; index < 3u; ++index) {
            mw::battle::BattleInputCommand command =
                heatShutdownReplay.commands[index];
            command.entityId = heatRecoveryWorld.playerEntityId();
            heatRecoveryWorld.enqueueInput(command);
        }
        heatRecoveryWorld.runTicks(4u);
        const mw::battle::BattleSnapshot recoveryShutdownSnapshot =
            heatRecoveryWorld.snapshot();
        expect(playerFrom(recoveryShutdownSnapshot).heat.reactorShutdown,
               "cooling recovery smoke never entered reactor shutdown");
        heatRecoveryWorld.runTicks(34u);
        const mw::battle::BattleSnapshot recoveredHeatSnapshot =
            heatRecoveryWorld.snapshot();
        const auto& recoveredHeatPlayer =
            playerFrom(recoveredHeatSnapshot);
        expect(!recoveredHeatPlayer.heat.reactorShutdown &&
                   recoveredHeatPlayer.heat.rawHeat <=
                       mw::battle::battleOriginalReactorShutdownRawHeat() &&
                   recoveredHeatPlayer.heat.reactorShutdownStateChangedTickIndex == 37u &&
                   weaponFrom(recoveredHeatPlayer, 4u).readiness !=
                       mw::battle::BattleWeaponReadiness::MechOffline,
               "reactor must recover automatically once cooling reaches 0x640");

        mw::battle::BattleStartParams majorSystemParams = weaponParams;
        majorSystemParams.originalMajorSystemRuntimeEnabled = true;
        majorSystemParams.playerLaunchState->persistentMechState.engine = 2u;
        majorSystemParams.playerLaunchState->persistentMechState.heatSinksWorking = 0u;
        majorSystemParams.playerLaunchState->persistentMechState.heatSinksTotal = 10u;
        const mw::battle::BattleSnapshot engineHeatA =
            mw::battle::runBattleReplay(majorSystemParams, {}, 2u);
        const mw::battle::BattleSnapshot engineHeatB =
            mw::battle::runBattleReplay(majorSystemParams, {}, 2u);
        const auto& engineHeatPlayer = playerFrom(engineHeatA);
        expect(engineHeatPlayer.majorSystems.enabled &&
                   engineHeatPlayer.majorSystems.engine ==
                       mw::battle::BattleMechSystemStatus::Offline &&
                   engineHeatPlayer.majorSystems.engineHeatPerOriginalUpdate == 10 &&
                   engineHeatPlayer.heat.rawHeat == 10 &&
                   engineHeatPlayer.heat.lastEngineHeatAdded == 10 &&
                   engineHeatPlayer.heat.lastEngineHeatTickIndex == 1u &&
                   engineHeatPlayer.heat.originalEngineDamageHeatProven,
               "heavy engine damage must add condition*5 heat per original update");
        expect(mw::battle::battleSnapshotFingerprint(engineHeatA) ==
                   mw::battle::battleSnapshotFingerprint(engineHeatB),
               "major-system and engine-heat replay fingerprint changed");

        auto systemStateForCondition = [&weaponStartPlayer](
                                           mw::mech3d::MechCriticalComponentId id,
                                           uint8_t condition,
                                           uint64_t elapsedMs) {
            mw::mech3d::MechDetailedDamageState damage =
                weaponStartPlayer.detailedDamage;
            auto& entry = damage.criticalComponents[static_cast<size_t>(id)];
            entry.condition = condition;
            entry.functional = condition < 3u;
            return mw::battle::battleMajorSystemRuntimeState(
                damage, elapsedMs, true);
        };
        const auto sensorGreen = systemStateForCondition(
            mw::mech3d::MechCriticalComponentId::Sensors, 0u, 0u);
        const auto sensorYellowOn = systemStateForCondition(
            mw::mech3d::MechCriticalComponentId::Sensors, 1u, 0u);
        const auto sensorYellowOff = systemStateForCondition(
            mw::mech3d::MechCriticalComponentId::Sensors, 1u, 3700u);
        const auto sensorRedOn = systemStateForCondition(
            mw::mech3d::MechCriticalComponentId::Sensors, 2u, 0u);
        const auto sensorRedOff = systemStateForCondition(
            mw::mech3d::MechCriticalComponentId::Sensors, 2u, 400u);
        const auto sensorBlack = systemStateForCondition(
            mw::mech3d::MechCriticalComponentId::Sensors, 3u, 0u);
        expect(sensorGreen.sensors == mw::battle::BattleMechSystemStatus::Online &&
                   sensorGreen.crosshairVisible &&
                   sensorYellowOn.sensors ==
                       mw::battle::BattleMechSystemStatus::Degraded &&
                   sensorYellowOn.crosshairVisible &&
                   !sensorYellowOff.crosshairVisible &&
                   sensorRedOn.sensors ==
                       mw::battle::BattleMechSystemStatus::Offline &&
                   sensorRedOn.crosshairVisible &&
                   !sensorRedOff.crosshairVisible &&
                   sensorBlack.sensors ==
                       mw::battle::BattleMechSystemStatus::Destroyed &&
                   !sensorBlack.crosshairVisible &&
                   !sensorBlack.targetingAvailable &&
                   !sensorBlack.topographicMapVisible &&
                   !sensorBlack.radarContactsVisible &&
                   !sensorBlack.sensorBlinkCadenceProven,
               "four-state sensor behavior or deterministic blink policy changed");

        mw::battle::BattleStartParams gyroDamagedParams = majorSystemParams;
        gyroDamagedParams.playerLaunchState->persistentMechState.engine = 0u;
        gyroDamagedParams.playerLaunchState->persistentMechState.gyros = 1u;
        mw::battle::BattleReplay driveReplay;
        mw::battle::BattleInputCommand driveCommand;
        driveCommand.tickIndex = 0u;
        driveCommand.throttle = 1.0;
        driveReplay.commands.push_back(driveCommand);
        const mw::battle::BattleSnapshot gyroDamagedA =
            mw::battle::runBattleReplay(gyroDamagedParams, driveReplay, 20u);
        const auto& gyroDamagedPlayer = playerFrom(gyroDamagedA);
        expect(gyroDamagedPlayer.majorSystems.gyros ==
                   mw::battle::BattleMechSystemStatus::Degraded &&
                   std::abs(gyroDamagedPlayer.majorSystems.gyroMovementScale - 0.75) <
                       0.000001 &&
                   !gyroDamagedPlayer.majorSystems.gyroDamageScaleProven &&
                   std::abs(gyroDamagedPlayer.targetForwardSpeed -
                            gyroDamagedPlayer.maxForwardSpeed * 0.75) < 0.001,
               "damaged gyro must constrain movement through its typed provisional scale");
        mw::battle::BattleWorld gyroCadenceWorld =
            mw::battle::BattleWorld::create(gyroDamagedParams);
        driveCommand.entityId = gyroCadenceWorld.playerEntityId();
        gyroCadenceWorld.enqueueInput(driveCommand);
        for (int tick = 0; tick < 20; ++tick) {
            for (int frame = 0; frame < 17; ++frame) {
                static_cast<void>(gyroCadenceWorld.snapshot());
            }
            gyroCadenceWorld.tick();
        }
        expect(mw::battle::battleSnapshotFingerprint(
                   gyroCadenceWorld.snapshot()) ==
                   mw::battle::battleSnapshotFingerprint(gyroDamagedA),
               "render cadence changed major-system movement state");

        mw::battle::BattleStartParams gyroDestroyedParams = gyroDamagedParams;
        gyroDestroyedParams.playerLaunchState->persistentMechState.gyros = 3u;
        const auto gyroDestroyed = mw::battle::runBattleReplay(
            gyroDestroyedParams, driveReplay, 2u);
        expect(playerFrom(gyroDestroyed).majorSystems.movementBlocked &&
                   playerFrom(gyroDestroyed).forwardSpeed == 0.0,
               "destroyed gyros must stop movement");

        mw::battle::BattleStartParams engineDestroyedParams = majorSystemParams;
        engineDestroyedParams.playerLaunchState->persistentMechState.engine = 3u;
        mw::battle::BattleReplay shutdownReplay;
        mw::battle::BattleInputCommand shutdownCommand;
        shutdownCommand.tickIndex = 0u;
        shutdownCommand.throttle = 1.0;
        shutdownCommand.fireWeapon = true;
        shutdownReplay.commands.push_back(shutdownCommand);
        const auto engineDestroyed = mw::battle::runBattleReplay(
            engineDestroyedParams, shutdownReplay, 2u);
        expect(playerFrom(engineDestroyed).majorSystems.engineShutdown &&
                   playerFrom(engineDestroyed).forwardSpeed == 0.0 &&
                   playerFrom(engineDestroyed).weaponShotsFired == 0u,
               "destroyed engine must shut down movement and weapons");

        mw::battle::BattleStartParams lifeDestroyedParams = majorSystemParams;
        lifeDestroyedParams.playerLaunchState->persistentMechState.engine = 0u;
        lifeDestroyedParams.playerLaunchState->persistentMechState.lifeSupport = 3u;
        const auto lifeDestroyed = mw::battle::runBattleReplay(
            lifeDestroyedParams, {}, 1u);
        expect(playerFrom(lifeDestroyed).majorSystems.lifeSupportFailure &&
                   playerFrom(lifeDestroyed).missionStatus ==
                       mw::battle::CombatantMissionStatus::Destroyed &&
                   lifeDestroyed.missionRuntimeState ==
                       mw::battle::BattleMissionRuntimeState::InProgress &&
                   lifeDestroyed.conclusionDelay.active &&
                   lifeDestroyed.conclusionDelay.pendingState ==
                       mw::battle::BattleMissionRuntimeState::Defeat,
               "destroyed life support must cause the player combatant to die");

        mw::battle::BattleStartParams jumpHeatParams = weaponParams;
        jumpHeatParams.playerMechPresetId = "jenner";
        jumpHeatParams.playerLaunchState->mechPresetId = "jenner";
        jumpHeatParams.playerLaunchState->persistentMechState.heatSinksWorking = 0u;
        jumpHeatParams.playerLaunchState->persistentMechState.heatSinksTotal = 10u;
        jumpHeatParams.playerLaunchState->persistentMechState.jumpJetsWorking = 3u;
        jumpHeatParams.playerLaunchState->persistentMechState.jumpJetsTotal = 3u;
        mw::battle::BattleReplay jumpHeatReplay;
        for (uint64_t tick = 0; tick < 4u; ++tick) {
            mw::battle::BattleInputCommand command;
            command.tickIndex = tick;
            command.jumpJetToggle = tick == 0u;
            command.jumpForwardThrust = tick < 2u;
            command.jumpVerticalThrust = tick >= 2u;
            jumpHeatReplay.commands.push_back(command);
        }
        const mw::battle::BattleSnapshot jumpHeatReplayA =
            mw::battle::runBattleReplay(jumpHeatParams, jumpHeatReplay, 4u);
        const mw::battle::BattleSnapshot jumpHeatReplayB =
            mw::battle::runBattleReplay(jumpHeatParams, jumpHeatReplay, 4u);
        const auto& jumpHeatPlayer = playerFrom(jumpHeatReplayA);
        expect(jumpHeatPlayer.jumpCapable && jumpHeatPlayer.jumpJetThrusting &&
                   jumpHeatPlayer.heat.rawHeat == 32 &&
                   jumpHeatPlayer.heat.lastJumpJetHeatAdded ==
                       mw::battle::battleOriginalVerticalJumpRawHeat() &&
                   jumpHeatPlayer.heat.lastJumpJetHeatTickIndex == 3u &&
                   jumpHeatPlayer.heat.originalJumpJetHeatValuesProven &&
                   !jumpHeatPlayer.heat.jumpJetControlMappingProven &&
                   mw::battle::battleOriginalForwardJumpRawHeat() == 20,
               "original jump-jet heat increments or provisional control mapping changed");
        expect(
            mw::battle::battleSnapshotFingerprint(jumpHeatReplayA) ==
                mw::battle::battleSnapshotFingerprint(jumpHeatReplayB),
            "jump-jet heat replay fingerprint changed");
        mw::battle::BattleWorld jumpHeatCadenceWorld =
            mw::battle::BattleWorld::create(jumpHeatParams);
        for (uint64_t tick = 0; tick < 4u; ++tick) {
            for (int frame = 0; frame < 31; ++frame) {
                static_cast<void>(jumpHeatCadenceWorld.snapshot());
            }
            mw::battle::BattleInputCommand command =
                jumpHeatReplay.commands[static_cast<size_t>(tick)];
            command.entityId = jumpHeatCadenceWorld.playerEntityId();
            jumpHeatCadenceWorld.enqueueInput(command);
            jumpHeatCadenceWorld.tick();
        }
        expect(
            mw::battle::battleSnapshotFingerprint(
                jumpHeatCadenceWorld.snapshot()) ==
                mw::battle::battleSnapshotFingerprint(jumpHeatReplayA),
            "snapshot/render cadence changed authoritative jump-jet heat");

        mw::battle::BattleInputCommand machineGunReadyFire;
        machineGunReadyFire.tickIndex = 4;
        machineGunReadyFire.entityId = weaponWorld.playerEntityId();
        machineGunReadyFire.selectWeaponInstanceId = 2u;
        machineGunReadyFire.fireWeapon = true;
        weaponWorld.enqueueInput(machineGunReadyFire);
        weaponWorld.tick();
        const mw::battle::BattleSnapshot readyAgainSnapshot = weaponWorld.snapshot();
        const auto& readyAgainPlayer = playerFrom(readyAgainSnapshot);
        expect(readyAgainPlayer.lastFireRequestAccepted &&
                   weaponFrom(readyAgainPlayer, 2u).shotsFired == 2u &&
                   weaponFrom(readyAgainPlayer, 2u).cooldownTicksRemaining == 4u,
               "weapon did not become ready after its exact independent cooldown");

        mw::battle::BattleStartParams destroyedWeaponParams = weaponParams;
        destroyedWeaponParams.playerLaunchState->persistentMechState.weaponConditions[1] = 3u;
        mw::battle::BattleWorld destroyedWeaponWorld =
            mw::battle::BattleWorld::create(destroyedWeaponParams);
        mw::battle::BattleInputCommand destroyedWeaponFire;
        destroyedWeaponFire.tickIndex = 0;
        destroyedWeaponFire.entityId = destroyedWeaponWorld.playerEntityId();
        destroyedWeaponFire.selectWeaponInstanceId = 2u;
        destroyedWeaponFire.fireWeapon = true;
        destroyedWeaponWorld.enqueueInput(destroyedWeaponFire);
        destroyedWeaponWorld.tick();
        const mw::battle::BattleSnapshot destroyedWeaponSnapshot =
            destroyedWeaponWorld.snapshot();
        const auto& destroyedWeaponPlayer = playerFrom(destroyedWeaponSnapshot);
        const auto& destroyedWeapon = weaponFrom(destroyedWeaponPlayer, 2u);
        expect(!destroyedWeapon.functional &&
                   destroyedWeapon.readiness == mw::battle::BattleWeaponReadiness::NonFunctional &&
                   destroyedWeapon.shotsFired == 0u && destroyedWeapon.ammunitionRemaining == 4 &&
                   !destroyedWeaponPlayer.detailedDamage.installedWeapons[1].functional &&
                   !destroyedWeaponPlayer.lastFireRequestAccepted &&
                   destroyedWeaponPlayer.heat.rawHeat == 0 &&
                   destroyedWeaponPlayer.heat.lastWeaponHeatAdded == 0,
               "destroyed/nonfunctional installed weapon must fail closed");

        mw::battle::BattleWorld noSelectionWorld =
            mw::battle::BattleWorld::create(weaponParams);
        mw::battle::BattleInputCommand noSelectionFire;
        noSelectionFire.tickIndex = 0;
        noSelectionFire.entityId = noSelectionWorld.playerEntityId();
        noSelectionFire.selectWeaponInstanceId = 1u;
        noSelectionFire.fireWeapon = true;
        noSelectionWorld.enqueueInput(noSelectionFire);
        noSelectionWorld.tick();
        const mw::battle::BattleSnapshot noSelectionSnapshot =
            noSelectionWorld.snapshot();
        const auto& noSelectionTarget =
            combatantFrom(noSelectionSnapshot, mw::battle::EntityId{2u});
        expect(noSelectionSnapshot.lastShot.result ==
                   mw::battle::BattleShotResult::RejectedNoTarget &&
                   noSelectionSnapshot.lastShot.targetKind ==
                       mw::battle::BattleShotTargetKind::None &&
                   noSelectionTarget.detailedDamage.armorSections[
                       static_cast<size_t>(
                           mw::mech3d::MechArmorSectionId::CenterTorso)]
                           .battleDamage == 0,
               "selected-target policy must not hide a nearest-target fallback");

        mw::battle::BattleStartParams rangeParams = weaponParams;
        rangeParams.combatantLaunchStates[0].startTransform =
            {1000.0, 0.0, 7000.0, 0.0};
        mw::battle::BattleWorld rangeWorld = mw::battle::BattleWorld::create(rangeParams);
        mw::battle::BattleInputCommand rangeFire;
        rangeFire.tickIndex = 0;
        rangeFire.entityId = rangeWorld.playerEntityId();
        rangeFire.selectWeaponInstanceId = 1u;
        rangeFire.cycleTargetScan = true;
        rangeFire.fireWeapon = true;
        rangeWorld.enqueueInput(rangeFire);
        rangeWorld.tick();
        const mw::battle::BattleSnapshot rangeSnapshot = rangeWorld.snapshot();
        expect(rangeSnapshot.lastShot.result == mw::battle::BattleShotResult::MissOutOfRange &&
                   !rangeSnapshot.lastShot.hit && !rangeSnapshot.lastShot.rangeGatePassed &&
                   rangeSnapshot.lastShot.targetKind ==
                       mw::battle::BattleShotTargetKind::Combatant &&
                   rangeSnapshot.lastShot.maximumRange == 4800.0 &&
                   rangeSnapshot.lastShot.targetDistance == 6000.0 &&
                   weaponFrom(playerFrom(rangeSnapshot), 1u).shotsFired == 1u &&
                   playerFrom(rangeSnapshot).heat.rawHeat == 240,
               "proven per-weapon maximum range must reject the selected distant target");

        mw::battle::BattleStartParams missileParams = weaponParams;
        missileParams.playerLaunchState->mechPresetId = "jenner";
        missileParams.playerLaunchState->persistentMechState.weaponConditions.fill(0u);
        missileParams.combatantLaunchStates[0].startTransform.z = 2000.0;
        mw::battle::BattleWorld missileWorld =
            mw::battle::BattleWorld::create(missileParams);
        mw::battle::BattleInputCommand missileFire;
        missileFire.tickIndex = 0;
        missileFire.entityId = missileWorld.playerEntityId();
        missileFire.selectWeaponInstanceId = 1u;
        missileFire.cycleTargetScan = true;
        missileFire.fireWeapon = true;
        missileWorld.enqueueInput(missileFire);
        missileWorld.tick();
        const mw::battle::BattleSnapshot missileLaunch = missileWorld.snapshot();
        const auto& missileWeapon = weaponFrom(playerFrom(missileLaunch), 1u);
        const auto& missileLaunchTarget = combatantFrom(
            missileLaunch, mw::battle::EntityId{2u});
        expect(missileWeapon.weaponTypeId == "srm4" &&
                   missileWeapon.ammunitionRemaining == 3 &&
                   playerFrom(missileLaunch).heat.rawHeat == 240 &&
                   playerFrom(missileLaunch).heat.lastWeaponHeatAdded == 240 &&
                   missileLaunch.lastShot.result ==
                       mw::battle::BattleShotResult::Hit &&
                   missileLaunch.lastShot.deliveryState ==
                       mw::battle::BattleShotDeliveryState::ProjectileInFlight &&
                   missileLaunch.lastShot.originalDamage == 4u &&
                   missileLaunch.lastShot.damageApplied == 0 &&
                   missileLaunch.projectiles.size() == 1u &&
                   missileLaunch.projectiles[0].originalProjectileTypeIndex == 3 &&
                   missileLaunch.projectiles[0].originalSpeedRaw == 50u &&
                   missileLaunch.projectiles[0].originalLifetimeUpdates == 28u &&
                   missileLaunch.projectiles[0].originalDamageClass == 8u &&
                   missileLaunch.projectiles[0].launchForwardOffset == 405.0 &&
                   !missileLaunch.projectiles[0].launchForwardOffsetProven &&
                   missileLaunchTarget.detailedDamage.armorSections[static_cast<size_t>(
                       mw::mech3d::MechArmorSectionId::CenterTorso)]
                           .armorRemaining == 23u,
               "SRM4 launch must consume shared ammo without applying immediate damage");
        for (int tick = 0;
             tick < 12 && !missileWorld.snapshot().projectiles.empty();
             ++tick) {
            missileWorld.tick();
        }
        const mw::battle::BattleSnapshot missileImpact = missileWorld.snapshot();
        const auto& missileImpactTarget = combatantFrom(
            missileImpact, mw::battle::EntityId{2u});
        expect(missileImpact.projectiles.empty() &&
                   missileImpact.lastShot.deliveryState ==
                       mw::battle::BattleShotDeliveryState::ProjectileImpact &&
                   missileImpact.lastShot.impactTickIndex >
                       missileImpact.lastShot.tickIndex &&
                   missileImpact.lastShot.missileClusterTableProven &&
                   (missileImpact.lastShot.impactDamage == 2u ||
                    missileImpact.lastShot.impactDamage == 4u ||
                    missileImpact.lastShot.impactDamage == 6u ||
                    missileImpact.lastShot.impactDamage == 8u) &&
                   missileImpact.lastShot.damageApplied ==
                       missileImpact.lastShot.impactDamage &&
                   missileImpactTarget.detailedDamage.armorSections[static_cast<size_t>(
                       mw::mech3d::MechArmorSectionId::CenterTorso)]
                           .armorRemaining ==
                       23u - missileImpact.lastShot.impactDamage,
               "SRM4 impact must use the original cluster table and authoritative delayed damage");
        expect(missileImpact.impactEvents.size() == 1u &&
                   missileImpact.impactEvents.front().shotSequence ==
                       missileImpact.lastShot.sequence &&
                   missileImpact.impactEvents.front().impactTickIndex ==
                       missileImpact.lastShot.impactTickIndex &&
                   missileImpact.impactEvents.front().deliveryState ==
                       mw::battle::BattleShotDeliveryState::ProjectileImpact &&
                   missileImpact.tickIndex -
                       missileImpact.impactEvents.front().impactTickIndex == 1u,
               "projectile contact must publish the first fixed-tick impact stage");
        for (int tick = 0; tick < 11; ++tick) {
            missileWorld.tick();
        }
        const mw::battle::BattleSnapshot missileImpactTwelfthTick =
            missileWorld.snapshot();
        expect(missileImpactTwelfthTick.impactEvents.size() == 1u &&
                   missileImpactTwelfthTick.tickIndex -
                       missileImpactTwelfthTick.impactEvents.front().impactTickIndex == 12u,
               "authoritative impact history did not retain two ticks per stage");
        missileWorld.tick();
        expect(missileWorld.snapshot().impactEvents.empty(),
               "authoritative impact history survived beyond twelve ticks");

        mw::battle::BattleStartParams distantMissileParams = missileParams;
        distantMissileParams.combatantLaunchStates[0].startTransform.z = 7000.0;
        mw::battle::BattleWorld distantMissileWorld =
            mw::battle::BattleWorld::create(distantMissileParams);
        mw::battle::BattleInputCommand distantMissileFire;
        distantMissileFire.tickIndex = 0u;
        distantMissileFire.entityId = distantMissileWorld.playerEntityId();
        distantMissileFire.selectWeaponInstanceId = 1u;
        distantMissileFire.cycleTargetScan = true;
        distantMissileFire.fireWeapon = true;
        distantMissileFire.throttle = 1.0;
        distantMissileWorld.enqueueInput(distantMissileFire);
        distantMissileWorld.tick();
        const auto distantMissileLaunch = distantMissileWorld.snapshot();
        expect(distantMissileLaunch.lastShot.result ==
                   mw::battle::BattleShotResult::MissOutOfRange &&
                   distantMissileLaunch.lastShot.deliveryState ==
                       mw::battle::BattleShotDeliveryState::ProjectileInFlight &&
                   distantMissileLaunch.projectiles.size() == 1u &&
                   !distantMissileLaunch.projectiles.front().targetBound &&
                   distantMissileLaunch.projectiles.front().weaponTypeId == "srm4" &&
                   weaponFrom(playerFrom(distantMissileLaunch), 1u).
                           ammunitionRemaining == 3,
               "out-of-range moving SRM fire must still create its visible unguided projectile");
        const auto distantMissileStart =
            distantMissileLaunch.projectiles.front().position;
        distantMissileWorld.tick();
        const auto distantMissileMoved = distantMissileWorld.snapshot();
        expect(!distantMissileMoved.projectiles.empty() &&
                   distantMissileMoved.projectiles.front().position.z >
                       distantMissileStart.z,
               "out-of-range SRM projectile must advance after its launch tick");

        mw::battle::BattleStartParams ballisticParams = weaponParams;
        ballisticParams.playerLaunchState->mechPresetId = "shadow_hawk";
        ballisticParams.playerLaunchState->persistentMechState.weaponConditions.fill(0u);
        ballisticParams.combatantLaunchStates[0].startTransform.z = 2000.0;
        mw::battle::BattleWorld ballisticWorld =
            mw::battle::BattleWorld::create(ballisticParams);
        mw::battle::BattleInputCommand ballisticFire;
        ballisticFire.tickIndex = 0;
        ballisticFire.entityId = ballisticWorld.playerEntityId();
        ballisticFire.selectWeaponInstanceId = 1u;
        ballisticFire.cycleTargetScan = true;
        ballisticFire.fireWeapon = true;
        ballisticWorld.enqueueInput(ballisticFire);
        ballisticWorld.tick();
        const mw::battle::BattleSnapshot ballisticLaunch =
            ballisticWorld.snapshot();
        expect(ballisticLaunch.projectiles.size() == 1u &&
                   ballisticLaunch.projectiles[0].weaponTypeId == "ac5" &&
                   ballisticLaunch.projectiles[0].originalProjectileTypeIndex == 0 &&
                   ballisticLaunch.projectiles[0].launchForwardOffset == 45.0 &&
                   !ballisticLaunch.projectiles[0].launchForwardOffsetProven &&
                   ballisticLaunch.lastShot.deliveryState ==
                       mw::battle::BattleShotDeliveryState::ProjectileInFlight &&
                   ballisticLaunch.lastShot.damageApplied == 0 &&
                   weaponFrom(playerFrom(ballisticLaunch), 1u)
                           .ammunitionRemaining == 3,
               "AC/5 launch must create the decoded ballistic projectile and consume ammo");
        for (int tick = 0;
             tick < 12 && !ballisticWorld.snapshot().projectiles.empty();
             ++tick) {
            ballisticWorld.tick();
        }
        const mw::battle::BattleSnapshot ballisticImpact =
            ballisticWorld.snapshot();
        expect(ballisticImpact.projectiles.empty() &&
                   ballisticImpact.lastShot.deliveryState ==
                       mw::battle::BattleShotDeliveryState::ProjectileImpact &&
                   ballisticImpact.lastShot.impactDamage == 5u &&
                   ballisticImpact.lastShot.damageApplied == 5 &&
                   !ballisticImpact.lastShot.missileClusterTableProven &&
                   playerFrom(ballisticImpact).heat.rawHeat < 80 &&
                   playerFrom(ballisticImpact).heat.rawHeat >= 0,
                "AC/5 impact must apply the original fixed five-point damage without splash");

        mw::battle::BattleTerrainCollisionObstacle projectileObstacle;
        projectileObstacle.stableId = 77u;
        projectileObstacle.recordIndex = 14u;
        projectileObstacle.centerX = 1000.0;
        projectileObstacle.centerZ = 1800.0;
        projectileObstacle.halfExtentX = 100.0;
        projectileObstacle.halfExtentZ = 50.0;
        projectileObstacle.height = 300.0;
        projectileObstacle.footprint = {
            {900.0, 1750.0},
            {1100.0, 1750.0},
            {1100.0, 1850.0},
            {900.0, 1850.0},
        };
        mw::battle::BattleStartParams terrainMissileParams = missileParams;
        terrainMissileParams.combatantLaunchStates[0].startTransform.z = 3000.0;
        terrainMissileParams.terrainCollisionObstacles.valid = true;
        terrainMissileParams.terrainCollisionObstacles.provenance =
            "phase12_synthetic_projectile_prism";
        terrainMissileParams.terrainCollisionObstacles.entries = {
            projectileObstacle};
        mw::battle::BattleWorld terrainMissileWorld =
            mw::battle::BattleWorld::create(terrainMissileParams);
        missileFire.entityId = terrainMissileWorld.playerEntityId();
        terrainMissileWorld.enqueueInput(missileFire);
        terrainMissileWorld.tick();
        const int terrainTargetArmorBefore =
            combatantFrom(
                terrainMissileWorld.snapshot(), mw::battle::EntityId{2u})
                .detailedDamage.armorSections[static_cast<size_t>(
                    mw::mech3d::MechArmorSectionId::CenterTorso)]
                .armorRemaining;
        for (int tick = 0;
             tick < 24 && !terrainMissileWorld.snapshot().projectiles.empty();
             ++tick) {
            terrainMissileWorld.tick();
        }
        const mw::battle::BattleSnapshot terrainMissileImpact =
            terrainMissileWorld.snapshot();
        const int terrainTargetArmorAfter =
            combatantFrom(
                terrainMissileImpact, mw::battle::EntityId{2u})
                .detailedDamage.armorSections[static_cast<size_t>(
                    mw::mech3d::MechArmorSectionId::CenterTorso)]
                .armorRemaining;
        expect(
            terrainMissileImpact.projectiles.empty() &&
                terrainMissileImpact.lastShot.result ==
                    mw::battle::BattleShotResult::HitTerrain &&
                terrainMissileImpact.lastShot.targetKind ==
                    mw::battle::BattleShotTargetKind::Terrain &&
                terrainMissileImpact.lastShot.deliveryState ==
                    mw::battle::BattleShotDeliveryState::
                        ProjectileTerrainImpact &&
                terrainMissileImpact.lastShot.runtimeEffect ==
                    mw::battle::BattleShotRuntimeEffect::
                        ProjectileTerrainSmokeNoDamage &&
                terrainMissileImpact.lastShot.damageApplied == 0 &&
                terrainMissileImpact.lastShot.impactDamage == 0u &&
                terrainTargetArmorAfter == terrainTargetArmorBefore &&
                terrainMissileImpact.impactEvents.size() == 1u &&
                terrainMissileImpact.impactEvents.front().visualSequence ==
                    mw::battle::BattleImpactVisualSequence::
                        TerrainSmokeLastThree &&
                terrainMissileImpact.lastShot.provenance.find(
                    "projectile_swept_WLD_TERPCK_prism_contact_77") !=
                    std::string::npos,
            "missile terrain contact must stop flight without damage and publish gray smoke");

        mw::battle::BattleStartParams terrainBallisticParams = ballisticParams;
        terrainBallisticParams.combatantLaunchStates[0].startTransform.z = 3000.0;
        terrainBallisticParams.terrainCollisionObstacles =
            terrainMissileParams.terrainCollisionObstacles;
        mw::battle::BattleWorld terrainBallisticWorld =
            mw::battle::BattleWorld::create(terrainBallisticParams);
        ballisticFire.entityId = terrainBallisticWorld.playerEntityId();
        terrainBallisticWorld.enqueueInput(ballisticFire);
        terrainBallisticWorld.tick();
        for (int tick = 0;
             tick < 24 && !terrainBallisticWorld.snapshot().projectiles.empty();
             ++tick) {
            terrainBallisticWorld.tick();
        }
        expect(
            terrainBallisticWorld.snapshot().lastShot.result ==
                    mw::battle::BattleShotResult::HitTerrain &&
                terrainBallisticWorld.snapshot().lastShot.damageApplied == 0,
            "AC/5 terrain contact must stop flight without damage");

        mw::battle::BattleReplay terrainMissileReplay;
        missileFire.entityId = {};
        terrainMissileReplay.commands = {missileFire};
        const mw::battle::BattleSnapshot terrainMissileReplayA =
            mw::battle::runBattleReplay(
                terrainMissileParams, terrainMissileReplay, 16u);
        const mw::battle::BattleSnapshot terrainMissileReplayB =
            mw::battle::runBattleReplay(
                terrainMissileParams, terrainMissileReplay, 16u);
        expect(
            mw::battle::battleSnapshotFingerprint(terrainMissileReplayA) ==
                mw::battle::battleSnapshotFingerprint(terrainMissileReplayB),
            "terrain projectile contact replay fingerprint changed");
        mw::battle::BattleWorld terrainMissileCadenceWorld =
            mw::battle::BattleWorld::create(terrainMissileParams);
        for (uint64_t tick = 0u; tick < 16u; ++tick) {
            for (int frame = 0; frame < 19; ++frame) {
                static_cast<void>(terrainMissileCadenceWorld.snapshot());
            }
            if (tick == 0u) {
                missileFire.entityId =
                    terrainMissileCadenceWorld.playerEntityId();
                terrainMissileCadenceWorld.enqueueInput(missileFire);
            }
            terrainMissileCadenceWorld.tick();
        }
        expect(
            mw::battle::battleSnapshotFingerprint(
                terrainMissileCadenceWorld.snapshot()) ==
                mw::battle::battleSnapshotFingerprint(
                    terrainMissileReplayA),
            "render cadence changed authoritative terrain projectile contact");

        mw::battle::BattleReplay projectileReplay;
        mw::battle::BattleInputCommand replayMissile = missileFire;
        replayMissile.entityId = {};
        projectileReplay.commands = {replayMissile};
        const mw::battle::BattleSnapshot projectileReplayA =
            mw::battle::runBattleReplay(missileParams, projectileReplay, 6u);
        const mw::battle::BattleSnapshot projectileReplayB =
            mw::battle::runBattleReplay(missileParams, projectileReplay, 6u);
        expect(
            mw::battle::battleSnapshotFingerprint(projectileReplayA) ==
                mw::battle::battleSnapshotFingerprint(projectileReplayB) &&
                projectileReplayA.lastShot.deliveryState ==
                    mw::battle::BattleShotDeliveryState::ProjectileImpact,
            "battle-owned projectile replay fingerprint changed");
        mw::battle::BattleWorld projectileCadenceWorld =
            mw::battle::BattleWorld::create(missileParams);
        for (uint64_t tick = 0; tick < 6u; ++tick) {
            for (int frame = 0; frame < 29; ++frame) {
                static_cast<void>(projectileCadenceWorld.snapshot());
            }
            if (tick == 0u) {
                replayMissile.entityId = projectileCadenceWorld.playerEntityId();
                projectileCadenceWorld.enqueueInput(replayMissile);
            }
            projectileCadenceWorld.tick();
            for (int frame = 0; frame < 41; ++frame) {
                static_cast<void>(projectileCadenceWorld.snapshot());
            }
        }
        expect(
            mw::battle::battleSnapshotFingerprint(
                projectileCadenceWorld.snapshot()) ==
                mw::battle::battleSnapshotFingerprint(projectileReplayA),
            "snapshot/render cadence changed battle-owned projectile state");

        mw::battle::BattleStartParams penetrationParams = weaponParams;
        penetrationParams.playerLaunchState->mechPresetId = "locust";
        penetrationParams.combatantLaunchStates[0].mechPresetId = "locust";
        penetrationParams.combatantLaunchStates[0].ammunitionByPool.fill(0);
        mw::battle::BattleWorld penetrationWorld =
            mw::battle::BattleWorld::create(penetrationParams);
        mw::battle::BattleInputCommand firstCriticalLaser;
        firstCriticalLaser.tickIndex = 0u;
        firstCriticalLaser.entityId = penetrationWorld.playerEntityId();
        firstCriticalLaser.selectWeaponInstanceId = 1u;
        firstCriticalLaser.cycleTargetScan = true;
        firstCriticalLaser.fireWeapon = true;
        penetrationWorld.enqueueInput(firstCriticalLaser);
        penetrationWorld.tick();
        const mw::battle::BattleSnapshot afterFirstCriticalLaser =
            penetrationWorld.snapshot();
        const auto& afterFirstCriticalTarget = combatantFrom(
            afterFirstCriticalLaser,
            mw::battle::EntityId{2u});
        expect(afterFirstCriticalTarget.detailedDamage.armorSections[static_cast<size_t>(
                   mw::mech3d::MechArmorSectionId::CenterTorso)].armorRemaining == 5u &&
                   !afterFirstCriticalLaser.lastShot.criticalResolutionAttempted,
               "sub-armor laser damage must stop before the original critical gate");
        for (uint64_t tick = 1u; tick <= 60u; ++tick) {
            penetrationWorld.tick();
        }
        mw::battle::BattleInputCommand secondCriticalLaser;
        secondCriticalLaser.tickIndex = 61u;
        secondCriticalLaser.entityId = penetrationWorld.playerEntityId();
        secondCriticalLaser.fireWeapon = true;
        penetrationWorld.enqueueInput(secondCriticalLaser);
        penetrationWorld.tick();
        const mw::battle::BattleSnapshot penetrationSnapshot =
            penetrationWorld.snapshot();
        const auto& penetratedTarget = combatantFrom(
            penetrationSnapshot,
            mw::battle::EntityId{2u});
        const auto& penetratedInternal =
            penetratedTarget.detailedDamage.internalSections[static_cast<size_t>(
                mw::mech3d::MechInternalSectionId::CenterTorso)];
        const auto& penetratedGyros =
            penetratedTarget.detailedDamage.criticalComponents[static_cast<size_t>(
                mw::mech3d::MechCriticalComponentId::Gyros)];
        expect(penetrationSnapshot.lastShot.originalDamage == 5u &&
                   penetrationSnapshot.lastShot.runtimeEffect ==
                       mw::battle::BattleShotRuntimeEffect::
                           DetailedCriticalResolved &&
                   penetrationSnapshot.lastShot.armorDamageApplied == 5 &&
                   penetrationSnapshot.lastShot.internalDamageApplied == 0 &&
                   penetrationSnapshot.lastShot.damageApplied == 5 &&
                   penetrationSnapshot.lastShot.criticalResolutionAttempted &&
                   penetrationSnapshot.lastShot.criticalRoll == 10u &&
                   penetrationSnapshot.lastShot.criticalAttemptsRequested == 2u &&
                   penetrationSnapshot.lastShot.criticalHitsApplied == 2u &&
                   penetrationSnapshot.lastShot.lastCriticalHitKind ==
                       mw::battle::BattleCriticalHitKind::Gyros &&
                   penetrationSnapshot.lastShot.lastCriticalHitIndex ==
                       static_cast<uint8_t>(
                           mw::mech3d::MechCriticalComponentId::Gyros) &&
                   !penetrationSnapshot.lastShot.criticalCandidateSelectionPending &&
                   penetrationSnapshot.lastShot.originalCriticalRulesProven &&
                   !penetrationSnapshot.lastShot.originalCriticalRandomSequenceProven &&
                   !penetrationSnapshot.lastShot.criticalResolutionPending &&
                   penetrationSnapshot.lastShot.unresolvedCriticalDamage == 0 &&
                   penetratedInternal.structureRemaining == 6u &&
                   penetratedInternal.battleDamage == 0 &&
                   !penetratedTarget.detailedDamage.installedWeapons[0].functional &&
                   penetratedTarget.detailedDamage.installedWeapons[0].battleDamage == 1 &&
                   penetratedGyros.condition == 1u &&
                   penetratedGyros.battleDamage == 1 &&
                   mw::battle::battleMajorSystemRuntimeState(
                       penetratedTarget.detailedDamage, 0u, true).gyros ==
                       mw::battle::BattleMechSystemStatus::Degraded &&
                   weaponFrom(penetratedTarget, 1u).readiness ==
                       mw::battle::BattleWeaponReadiness::NonFunctional &&
                   penetratedTarget.detailedDamage.criticalAttempts == 2u &&
                   penetratedTarget.detailedDamage.criticalHitsApplied == 2u &&
                   !penetratedTarget.detailedDamage.criticalResolutionPending &&
                   penetratedTarget.detailedDamage.unresolvedCriticalDamage == 0,
               "complete ordered critical prefix must mutate weapon and gyro without pending state");

        mw::battle::BattleReplay criticalReplay;
        firstCriticalLaser.entityId = {};
        secondCriticalLaser.entityId = {};
        criticalReplay.commands = {firstCriticalLaser, secondCriticalLaser};
        const mw::battle::BattleSnapshot criticalReplayA =
            mw::battle::runBattleReplay(
                penetrationParams, criticalReplay, 62u);
        const mw::battle::BattleSnapshot criticalReplayB =
            mw::battle::runBattleReplay(
                penetrationParams, criticalReplay, 62u);
        expect(
            mw::battle::battleSnapshotFingerprint(criticalReplayA) ==
                mw::battle::battleSnapshotFingerprint(criticalReplayB) &&
                criticalReplayA.lastShot.criticalHitsApplied == 2u,
            "battle-owned critical resolution replay fingerprint changed");
        mw::battle::BattleWorld criticalCadenceWorld =
            mw::battle::BattleWorld::create(penetrationParams);
        firstCriticalLaser.entityId = criticalCadenceWorld.playerEntityId();
        secondCriticalLaser.entityId = criticalCadenceWorld.playerEntityId();
        criticalCadenceWorld.enqueueInput(firstCriticalLaser);
        criticalCadenceWorld.enqueueInput(secondCriticalLaser);
        for (uint64_t tick = 0u; tick < 62u; ++tick) {
            for (int frame = 0; frame < 17; ++frame) {
                static_cast<void>(criticalCadenceWorld.snapshot());
            }
            criticalCadenceWorld.tick();
        }
        expect(
            mw::battle::battleSnapshotFingerprint(
                criticalCadenceWorld.snapshot()) ==
                mw::battle::battleSnapshotFingerprint(criticalReplayA),
            "render cadence changed authoritative critical resolution");

        mw::battle::BattleStartParams destroyedCriticalParams = weaponParams;
        destroyedCriticalParams.playerLaunchState->mechPresetId = "warhammer";
        auto& destroyedCriticalTarget =
            destroyedCriticalParams.combatantLaunchStates[0];
        destroyedCriticalTarget.mechPresetId = "locust";
        destroyedCriticalTarget.ammunitionByPool.fill(0);
        destroyedCriticalTarget.persistentMechState.armorDamage[static_cast<size_t>(
            mw::mech3d::MechArmorSectionId::CenterTorso)] = 2u;
        mw::battle::BattleInputCommand destroyedCriticalFire;
        destroyedCriticalFire.tickIndex = 0u;
        destroyedCriticalFire.selectWeaponInstanceId = 1u;
        destroyedCriticalFire.cycleTargetScan = true;
        destroyedCriticalFire.fireWeapon = true;
        mw::battle::BattleReplay destroyedCriticalReplay;
        destroyedCriticalReplay.commands = {destroyedCriticalFire};
        const mw::battle::BattleSnapshot destroyedCriticalSnapshot =
            mw::battle::runBattleReplay(
                destroyedCriticalParams, destroyedCriticalReplay, 1u);
        const auto& destroyedCriticalResult = combatantFrom(
            destroyedCriticalSnapshot, mw::battle::EntityId{2u});
        expect(
            destroyedCriticalSnapshot.lastShot.criticalRoll == 3u &&
                destroyedCriticalSnapshot.lastShot.criticalAttemptsRequested == 0u &&
                destroyedCriticalSnapshot.lastShot.criticalHitsApplied == 1u &&
                destroyedCriticalSnapshot.lastShot.damageApplied == 6 &&
                destroyedCriticalResult.detailedDamage.internalSections[
                    static_cast<size_t>(
                        mw::mech3d::MechInternalSectionId::CenterTorso)]
                        .structureRemaining == 0u &&
                destroyedCriticalResult.detailedDamage.armorSections[
                    static_cast<size_t>(
                        mw::mech3d::MechArmorSectionId::CenterRear)]
                        .armorRemaining == 6u &&
                destroyedCriticalResult.detailedDamage.criticalAttempts == 1u &&
                !destroyedCriticalSnapshot.lastShot.criticalResolutionPending &&
                destroyedCriticalSnapshot.lastShot.unresolvedCriticalDamage == 0,
            "successful post-internal FUN_4c0a attempt must consume remaining damage before E4C");

        mw::battle::BattleStartParams ammoCriticalParams = weaponParams;
        ammoCriticalParams.playerLaunchState->mechPresetId = "warhammer";
        auto& ammoCriticalTarget = ammoCriticalParams.combatantLaunchStates[0];
        ammoCriticalTarget.mechPresetId = "rifleman";
        ammoCriticalTarget.ammunitionByPool.fill(0);
        ammoCriticalTarget.ammunitionByPool[0] = 4;
        ammoCriticalTarget.persistentMechState.engine = 3u;
        ammoCriticalTarget.persistentMechState.gyros = 2u;
        ammoCriticalTarget.persistentMechState.weaponConditions.fill(3u);
        ammoCriticalTarget.persistentMechState.armorDamage[static_cast<size_t>(
            mw::mech3d::MechArmorSectionId::CenterTorso)] = 1u;
        mw::battle::BattleInputCommand firstPpc = firstCriticalLaser;
        firstPpc.selectWeaponInstanceId = 1u;
        mw::battle::BattleInputCommand secondPpc = secondCriticalLaser;
        const mw::battle::BattleReplay ammoCriticalReplay{{firstPpc, secondPpc}};
        const mw::battle::BattleSnapshot ammoCriticalSnapshot =
            mw::battle::runBattleReplay(
                ammoCriticalParams, ammoCriticalReplay, 62u);
        const auto& ammoCriticalResult = combatantFrom(
            ammoCriticalSnapshot, mw::battle::EntityId{2u});
        expect(ammoCriticalSnapshot.lastShot.criticalRoll == 10u &&
                   ammoCriticalSnapshot.lastShot.criticalHitsApplied == 1u &&
                   ammoCriticalSnapshot.lastShot.lastCriticalHitKind ==
                       mw::battle::BattleCriticalHitKind::AmmunitionBin &&
                   ammoCriticalSnapshot.lastShot.lastCriticalHitIndex == 0u &&
                   !ammoCriticalSnapshot.lastShot
                       .criticalCandidateSelectionPending &&
                   !ammoCriticalSnapshot.lastShot.criticalResolutionPending &&
                   ammoCriticalResult.detailedDamage
                           .ammunitionCriticalHits[0] == 1u &&
                   weaponFrom(ammoCriticalResult, 3u).ammunitionRemaining == 0 &&
                   weaponFrom(ammoCriticalResult, 4u).ammunitionRemaining == 0,
               "original Rifleman CT ammunition critical must zero the shared AC/5 pool");

        mw::battle::BattleStartParams jumpCriticalParams = ammoCriticalParams;
        auto& jumpCriticalTarget = jumpCriticalParams.combatantLaunchStates[0];
        jumpCriticalTarget.mechPresetId = "jenner";
        jumpCriticalTarget.ammunitionByPool.fill(0);
        jumpCriticalTarget.persistentMechState.jumpJetsWorking = 3u;
        jumpCriticalTarget.persistentMechState.jumpJetsTotal = 3u;
        jumpCriticalTarget.persistentMechState.armorDamage.fill(0u);
        jumpCriticalTarget.persistentMechState.armorDamage[static_cast<size_t>(
            mw::mech3d::MechArmorSectionId::CenterTorso)] = 1u;
        const mw::battle::BattleSnapshot jumpCriticalSnapshot =
            mw::battle::runBattleReplay(
                jumpCriticalParams, ammoCriticalReplay, 62u);
        const auto& jumpCriticalResult = combatantFrom(
            jumpCriticalSnapshot, mw::battle::EntityId{2u});
        const auto& jumpCriticalState =
            jumpCriticalResult.detailedDamage.criticalComponents[
                static_cast<size_t>(
                    mw::mech3d::MechCriticalComponentId::JumpJets)];
        expect(jumpCriticalSnapshot.lastShot.criticalRoll == 10u &&
                   jumpCriticalSnapshot.lastShot.criticalHitsApplied == 1u &&
                   jumpCriticalSnapshot.lastShot.lastCriticalHitKind ==
                       mw::battle::BattleCriticalHitKind::JumpJet &&
                   jumpCriticalSnapshot.lastShot.lastCriticalHitIndex == 2u &&
                   jumpCriticalState.workingCount == 2u &&
                   jumpCriticalState.battleDamage == 1 &&
                   jumpCriticalResult.detailedDamage
                           .originalJumpJetCriticalDestroyedMask == 4u &&
                   jumpCriticalResult.detailedDamage.internalSections[
                       static_cast<size_t>(
                           mw::mech3d::MechInternalSectionId::CenterTorso)]
                           .structureRemaining == 0u &&
                   jumpCriticalResult.detailedDamage.armorSections[
                       static_cast<size_t>(
                           mw::mech3d::MechArmorSectionId::CenterRear)]
                           .armorRemaining == 5u &&
                   !jumpCriticalSnapshot.lastShot.criticalResolutionPending,
               "Jenner CT jump-jet critical and E4C rear-armor continuation changed");

        mw::battle::BattleStartParams objectiveWeaponParams = weaponParams;
        objectiveWeaponParams.combatantLaunchStates.clear();
        objectiveWeaponParams.objective = {};
        objectiveWeaponParams.objective.valid = true;
        objectiveWeaponParams.objective.provenance = "phase12_objective_weapon_smoke";
        objectiveWeaponParams.objective.transform = {1000.0, 0.0, 1100.0, 0.0};
        objectiveWeaponParams.objective.transformProven = true;
        objectiveWeaponParams.objective.staticModel =
            mw::battle::originalBattleObjectiveStaticModel();
        objectiveWeaponParams.objective.damagePolicyProven = true;
        objectiveWeaponParams.objective.maxDamage = 20;
        mw::battle::BattleWorld objectiveWeaponWorld =
            mw::battle::BattleWorld::create(objectiveWeaponParams);
        mw::battle::BattleInputCommand objectiveFire;
        objectiveFire.tickIndex = 0;
        objectiveFire.entityId = objectiveWeaponWorld.playerEntityId();
        objectiveFire.selectWeaponInstanceId = 1u;
        objectiveFire.cycleTargetScan = true;
        objectiveFire.fireWeapon = true;
        objectiveWeaponWorld.enqueueInput(objectiveFire);
        objectiveWeaponWorld.tick();
        const mw::battle::BattleSnapshot objectiveShot = objectiveWeaponWorld.snapshot();
        expect(objectiveShot.targetScan.selectedTargetKind ==
                   mw::battle::BattleTargetScanTargetKind::Objective &&
                   objectiveShot.lastShot.targetKind ==
                       mw::battle::BattleShotTargetKind::Objective &&
                   objectiveShot.lastShot.result == mw::battle::BattleShotResult::Hit &&
                   objectiveShot.lastShot.runtimeEffect ==
                       mw::battle::BattleShotRuntimeEffect::ObjectiveDamage &&
                   objectiveShot.lastShot.damageApplied == 5 &&
                   objectiveShot.objective.damage == 5 &&
                   playerFrom(objectiveShot).heat.rawHeat == 240 &&
                   objectiveShot.impactEvents.size() == 1u &&
                   objectiveShot.impactEvents.front().targetKind ==
                       mw::battle::BattleShotTargetKind::Objective &&
                   !mw::battle::isValid(objectiveShot.lastShot.targetEntityId),
               "objective target must remain separate and receive exact weapon damage");

        mw::battle::BattleReplay weaponReplay;
        mw::battle::BattleInputCommand replayMachineGun = selectAndFire;
        replayMachineGun.entityId = {};
        mw::battle::BattleInputCommand replayLaser = energyFire;
        replayLaser.entityId = {};
        replayLaser.selectWeaponInstanceId = 1u;
        weaponReplay.commands = {replayMachineGun, replayLaser};
        const mw::battle::BattleSnapshot weaponReplayA =
            mw::battle::runBattleReplay(weaponParams, weaponReplay, 4u);
        const mw::battle::BattleSnapshot weaponReplayB =
            mw::battle::runBattleReplay(weaponParams, weaponReplay, 4u);
        expect(
            mw::battle::battleSnapshotFingerprint(weaponReplayA) ==
                mw::battle::battleSnapshotFingerprint(weaponReplayB),
            "stationary target weapon replay fingerprint changed");

        mw::battle::BattleWorld renderCadenceWorld = mw::battle::BattleWorld::create(weaponParams);
        for (uint64_t tick = 0; tick < 4u; ++tick) {
            for (int renderFrame = 0; renderFrame < 37; ++renderFrame) {
                static_cast<void>(renderCadenceWorld.snapshot());
            }
            for (mw::battle::BattleInputCommand command : weaponReplay.commands) {
                if (command.tickIndex == tick) {
                    command.entityId = renderCadenceWorld.playerEntityId();
                    renderCadenceWorld.enqueueInput(command);
                }
            }
            renderCadenceWorld.tick();
            for (int renderFrame = 0; renderFrame < 53; ++renderFrame) {
                static_cast<void>(renderCadenceWorld.snapshot());
            }
        }
        const mw::battle::BattleSnapshot renderCadenceFinal = renderCadenceWorld.snapshot();
        expect(
            mw::battle::battleSnapshotFingerprint(renderCadenceFinal) ==
                mw::battle::battleSnapshotFingerprint(weaponReplayA) &&
                playerFrom(renderCadenceFinal).weaponShotsFired == 2u,
            "render cadence changed authoritative weapon state or shot count");

        mw::battle::BattleStartParams crosshairParams = weaponParams;
        crosshairParams.stationaryTargetHitDiagnosticEnabled = false;
        crosshairParams.weaponTargetPolicy =
            mw::battle::BattleWeaponTargetPolicy::
                CrosshairRayVisibleCombatantProvisional;
        crosshairParams.combatantLaunchStates[0].mechPresetId = "locust";
        crosshairParams.combatantLaunchStates[0].startTransform = {
            1000.0, 0.0, 1400.0, 3.14159265358979323846};
        crosshairParams.mechHitProfiles = {syntheticCrosshairHitProfile()};

        bool battleAbiMismatchRejected = false;
        try {
            static_cast<void>(mw::battle::BattleWorld::create(
                crosshairParams,
                mw::battle::battleWorldHeaderAbiFingerprint() ^ 1ull));
        } catch (const std::runtime_error& error) {
            battleAbiMismatchRejected =
                std::string(error.what()).find("battle runtime ABI mismatch") !=
                std::string::npos;
        }
        expect(
            battleAbiMismatchRejected,
            "mixed battle header/library ABI must fail before a snapshot crosses the boundary");

        auto sectionBattleDamage = [](const mw::battle::CombatantSnapshot& combatant,
                                      mw::mech3d::MechArmorSectionId section) {
            return combatant.detailedDamage.armorSections[
                static_cast<size_t>(section)].battleDamage;
        };
        auto fireCrosshairWorld = [&](int aimPitchStep) {
            mw::battle::BattleWorld world =
                mw::battle::BattleWorld::create(crosshairParams);
            mw::battle::BattleInputCommand command;
            command.tickIndex = 0u;
            command.entityId = world.playerEntityId();
            command.aimPitchStepDelta = aimPitchStep;
            command.selectWeaponInstanceId = 2u;
            command.fireWeapon = true;
            world.enqueueInput(command);
            world.tick();
            return world.snapshot();
        };

        const mw::battle::BattleSnapshot torsoRayShot = fireCrosshairWorld(0);
        const auto& torsoRayTarget = combatantFrom(
            torsoRayShot, mw::battle::EntityId{2u});
        expect(torsoRayShot.lastShot.result == mw::battle::BattleShotResult::Hit &&
                   torsoRayShot.lastShot.hitLocation ==
                       mw::battle::BattleShotHitLocation::
                           TorsoPartitionProvisional &&
                   !torsoRayShot.lastShot.hitLocationProven &&
                   !torsoRayShot.lastShot.aimProjectionProven &&
                   torsoRayShot.lastShot.armorSection ==
                       mw::mech3d::MechArmorSectionId::CenterTorso &&
                   torsoRayShot.lastShot.hitComponentId == 1 &&
                   torsoRayShot.lastShot.hitGeometryFingerprint ==
                       crosshairParams.mechHitProfiles[0].geometryFingerprint &&
                   sectionBattleDamage(
                       torsoRayTarget,
                       mw::mech3d::MechArmorSectionId::CenterTorso) == 2 &&
                   sectionBattleDamage(
                       torsoRayTarget,
                       mw::mech3d::MechArmorSectionId::RightTorso) == 0,
               "neutral torso ray must use the provisional geometric partition");

        const mw::battle::BattleSnapshot headRayShot = fireCrosshairWorld(5);
        const auto& headRayTarget = combatantFrom(
            headRayShot, mw::battle::EntityId{2u});
        expect(headRayShot.lastShot.result == mw::battle::BattleShotResult::Hit &&
                   headRayShot.lastShot.hitLocation ==
                       mw::battle::BattleShotHitLocation::
                           VisibleComponentOriginalIdentity &&
                   headRayShot.lastShot.hitLocationProven &&
                   headRayShot.lastShot.armorSection ==
                       mw::mech3d::MechArmorSectionId::Head &&
                   headRayShot.lastShot.hitComponentId == 6 &&
                   sectionBattleDamage(
                       headRayTarget,
                       mw::mech3d::MechArmorSectionId::Head) == 2 &&
                   sectionBattleDamage(
                       headRayTarget,
                       mw::mech3d::MechArmorSectionId::CenterTorso) == 0,
               "raised crosshair must damage only the visible cockpit/head component");

        const mw::battle::BattleSnapshot legRayShot = fireCrosshairWorld(-5);
        const auto& legRayTarget = combatantFrom(
            legRayShot, mw::battle::EntityId{2u});
        expect(legRayShot.lastShot.result == mw::battle::BattleShotResult::Hit &&
                   legRayShot.lastShot.hitLocationProven &&
                   legRayShot.lastShot.armorSection ==
                       mw::mech3d::MechArmorSectionId::RightLeg &&
                   legRayShot.lastShot.hitComponentId == 2 &&
                   sectionBattleDamage(
                       legRayTarget,
                       mw::mech3d::MechArmorSectionId::RightLeg) == 2 &&
                   sectionBattleDamage(
                       legRayTarget,
                       mw::mech3d::MechArmorSectionId::CenterTorso) == 0,
               "lowered crosshair must damage only the visible leg component");

        mw::battle::BattleStartParams limbContinuationParams = crosshairParams;
        limbContinuationParams.playerMechPresetId = "warhammer";
        limbContinuationParams.playerLaunchState->mechPresetId = "warhammer";
        auto& limbTargetLaunch =
            limbContinuationParams.combatantLaunchStates[0];
        limbTargetLaunch.ammunitionByPool.fill(0);
        limbTargetLaunch.persistentMechState.engine = 3u;
        limbTargetLaunch.persistentMechState.gyros = 2u;
        limbTargetLaunch.persistentMechState.leftArmActuator = 3u;
        limbTargetLaunch.persistentMechState.rightArmActuator = 3u;
        limbTargetLaunch.persistentMechState.leftLegActuator = 3u;
        limbTargetLaunch.persistentMechState.rightLegActuator = 3u;
        limbTargetLaunch.persistentMechState.weaponConditions.fill(3u);
        mw::battle::BattleMechHitProfile leftArmProfile =
            syntheticCrosshairHitProfile();
        leftArmProfile.triangles.resize(2u);
        for (auto& triangle : leftArmProfile.triangles) {
            triangle.componentId = 5;
            triangle.directArmorSection =
                mw::mech3d::MechArmorSectionId::LeftArm;
            triangle.torsoComposite = false;
        }
        leftArmProfile.provenance =
            "phase12_synthetic_left_arm_internal_continuation";
        mw::battle::BattleMechHitProfile warhammerAimProfile =
            syntheticCrosshairHitProfile();
        warhammerAimProfile.mechPresetId = "warhammer";
        limbContinuationParams.mechHitProfiles = {
            leftArmProfile, warhammerAimProfile};
        mw::battle::BattleReplay limbContinuationReplay;
        mw::battle::BattleInputCommand limbContinuationFire;
        limbContinuationFire.tickIndex = 0u;
        limbContinuationFire.selectWeaponInstanceId = 1u;
        limbContinuationFire.fireWeapon = true;
        limbContinuationReplay.commands = {limbContinuationFire};
        const mw::battle::BattleSnapshot limbContinuationA =
            mw::battle::runBattleReplay(
                limbContinuationParams, limbContinuationReplay, 1u);
        const mw::battle::BattleSnapshot limbContinuationB =
            mw::battle::runBattleReplay(
                limbContinuationParams, limbContinuationReplay, 1u);
        const auto& limbContinuationTarget = combatantFrom(
            limbContinuationA, mw::battle::EntityId{2u});
        expect(
            limbContinuationA.lastShot.result ==
                    mw::battle::BattleShotResult::Hit &&
                limbContinuationA.lastShot.armorSection ==
                    mw::mech3d::MechArmorSectionId::LeftArm &&
                limbContinuationA.lastShot.damageApplied == 10 &&
                limbContinuationTarget.detailedDamage.armorSections[
                    static_cast<size_t>(
                        mw::mech3d::MechArmorSectionId::LeftArm)]
                        .armorRemaining == 0u &&
                limbContinuationTarget.detailedDamage.internalSections[
                    static_cast<size_t>(
                        mw::mech3d::MechInternalSectionId::LeftArm)]
                        .structureRemaining == 0u &&
                limbContinuationTarget.detailedDamage.armorSections[
                    static_cast<size_t>(
                        mw::mech3d::MechArmorSectionId::LeftTorso)]
                        .armorRemaining == 5u &&
                containsId(limbContinuationTarget.destroyedComponentIds, 5) &&
                !limbContinuationTarget.mechDestroyed &&
                !limbContinuationA.lastShot.criticalResolutionPending &&
                limbContinuationA.lastShot.unresolvedCriticalDamage == 0 &&
                mw::battle::battleSnapshotFingerprint(limbContinuationA) ==
                    mw::battle::battleSnapshotFingerprint(limbContinuationB),
            "destroyed left-arm internal must hide the limb and carry remainder to LT via E4C");
        mw::battle::BattleWorld limbCadenceWorld =
            mw::battle::BattleWorld::create(limbContinuationParams);
        for (int frame = 0; frame < 43; ++frame) {
            static_cast<void>(limbCadenceWorld.snapshot());
        }
        limbContinuationFire.entityId = limbCadenceWorld.playerEntityId();
        limbCadenceWorld.enqueueInput(limbContinuationFire);
        limbCadenceWorld.tick();
        for (int frame = 0; frame < 61; ++frame) {
            static_cast<void>(limbCadenceWorld.snapshot());
        }
        expect(
            mw::battle::battleSnapshotFingerprint(
                limbCadenceWorld.snapshot()) ==
                mw::battle::battleSnapshotFingerprint(limbContinuationA),
            "render cadence changed post-internal E4C continuation");

        const mw::battle::BattleSnapshot missedRayShot = fireCrosshairWorld(9);
        const auto& missedRayTarget = combatantFrom(
            missedRayShot, mw::battle::EntityId{2u});
        int missedRayDamage = 0;
        for (const auto& section : missedRayTarget.detailedDamage.armorSections) {
            missedRayDamage += section.battleDamage;
        }
        expect(missedRayShot.lastShot.result ==
                   mw::battle::BattleShotResult::MissCrosshairRay &&
                   !missedRayShot.lastShot.hit && missedRayDamage == 0 &&
                   weaponFrom(playerFrom(missedRayShot), 2u).shotsFired == 1u &&
                   weaponFrom(playerFrom(missedRayShot), 2u).ammunitionRemaining == 3,
               "crosshair ray miss must consume the fired round but apply no damage");

        // FUN_1000_47f7 scans all occupied live slots 1..7 for the visible
        // component handle and never compares team/side.  A friendly mech
        // geometrically in front of an opponent must therefore own the
        // player's shot; projectile delivery remains target-bound through
        // the common 5a49 -> 5654 -> 578b -> 4ece path.
        mw::battle::BattleStartParams friendlyHitParams = crosshairParams;
        friendlyHitParams.playerMechPresetId = "jenner";
        expect(
            friendlyHitParams.playerLaunchState.has_value(),
            "player-friendly hit fixture lost player launch state");
        friendlyHitParams.playerLaunchState->mechPresetId = "jenner";
        friendlyHitParams.playerLaunchState->startTransform =
            friendlyHitParams.playerStartTransform;
        friendlyHitParams.playerLaunchState->ammunitionByPool.fill(0);
        friendlyHitParams.playerLaunchState->ammunitionByPool[3] = 25;

        mw::battle::BattleCombatantLaunchState friendlyLaunch =
            weaponTargetLaunch;
        friendlyLaunch.mechPresetId = "locust";
        friendlyLaunch.startTransform = {1000.0, 0.0, 1200.0, 0.0};
        friendlyLaunch.roster.team = mw::battle::BattleTeam::Player;
        friendlyLaunch.roster.provenance =
            "BTECH_47f7_player_visible_friendly_component";
        friendlyLaunch.roster.sourceSlot = "player:friendly_crosshair";
        friendlyHitParams.playerAlliedLaunchStates = {friendlyLaunch};

        friendlyHitParams.combatantLaunchStates[0].mechPresetId = "locust";
        friendlyHitParams.combatantLaunchStates[0].startTransform = {
            1000.0, 0.0, 1400.0, 0.0};
        mw::battle::BattleMechHitProfile friendlyLocustProfile =
            syntheticCrosshairHitProfile();
        mw::battle::BattleMechHitProfile friendlyJennerProfile =
            syntheticCrosshairHitProfile();
        friendlyJennerProfile.mechPresetId = "jenner";
        friendlyHitParams.mechHitProfiles = {
            friendlyLocustProfile, friendlyJennerProfile};

        mw::battle::BattleWorld friendlyMissileWorld =
            mw::battle::BattleWorld::create(friendlyHitParams);
        mw::battle::BattleInputCommand friendlyMissileFire;
        friendlyMissileFire.tickIndex = 0u;
        friendlyMissileFire.entityId = friendlyMissileWorld.playerEntityId();
        friendlyMissileFire.selectWeaponInstanceId = 1u;
        friendlyMissileFire.fireWeapon = true;
        friendlyMissileWorld.enqueueInput(friendlyMissileFire);
        friendlyMissileWorld.tick();
        const mw::battle::BattleSnapshot friendlyMissileLaunch =
            friendlyMissileWorld.snapshot();
        expect(
            friendlyMissileLaunch.combatants.size() == 3u &&
                friendlyMissileLaunch.combatants[0]
                        .roster.originalLiveObjectSlot == 0u &&
                friendlyMissileLaunch.combatants[1]
                        .roster.originalLiveObjectSlot == 1u &&
                friendlyMissileLaunch.combatants[2]
                        .roster.originalLiveObjectSlot == 4u &&
                friendlyMissileLaunch.lastShot.hit &&
                friendlyMissileLaunch.lastShot.targetKind ==
                    mw::battle::BattleShotTargetKind::Combatant &&
                friendlyMissileLaunch.lastShot.targetEntityId ==
                    mw::battle::EntityId{2u} &&
                friendlyMissileLaunch.lastShot.deliveryState ==
                    mw::battle::BattleShotDeliveryState::ProjectileInFlight &&
                friendlyMissileLaunch.lastShot.damageApplied == 0 &&
                friendlyMissileLaunch.lastShot.provenance.find(
                    "BTECH_47f7_player_visible_friendly_component") !=
                    std::string::npos &&
                friendlyMissileLaunch.projectiles.size() == 1u &&
                friendlyMissileLaunch.projectiles.front().targetBound &&
                friendlyMissileLaunch.projectiles.front().targetEntityId ==
                    mw::battle::EntityId{2u} &&
                weaponFrom(playerFrom(friendlyMissileLaunch), 1u)
                        .ammunitionRemaining == 24 &&
                weaponFrom(playerFrom(friendlyMissileLaunch), 1u)
                        .cooldownTicksRemaining > 0u,
            "player SRM must bind the nearer friendly component through the common projectile path");

        for (uint64_t tick = 1u; tick < 8u; ++tick) {
            friendlyMissileWorld.tick();
        }
        const mw::battle::BattleSnapshot friendlyMissileImpact =
            friendlyMissileWorld.snapshot();
        const auto& friendlyMissileTarget = combatantFrom(
            friendlyMissileImpact, mw::battle::EntityId{2u});
        const auto& friendlyMissileEnemy = combatantFrom(
            friendlyMissileImpact, mw::battle::EntityId{3u});
        expect(
            friendlyMissileImpact.projectiles.empty(),
            "bound player SRM did not leave the projectile pool");
        expect(
            friendlyMissileImpact.lastShot.hit &&
                friendlyMissileImpact.lastShot.targetEntityId ==
                    mw::battle::EntityId{2u} &&
                friendlyMissileImpact.lastShot.deliveryState ==
                    mw::battle::BattleShotDeliveryState::ProjectileImpact,
            "bound player SRM lost friendly target ownership at delivery");
        expect(
            friendlyMissileImpact.lastShot.damageApplied > 0,
            "bound player SRM impact reported no authoritative damage");
        expect(
            sectionBattleDamage(
                friendlyMissileTarget,
                friendlyMissileImpact.lastShot.armorSection) > 0,
            "bound player SRM did not damage its recorded friendly armor section");
        expect(
            sectionBattleDamage(
                friendlyMissileEnemy,
                friendlyMissileImpact.lastShot.armorSection) == 0,
            "bound player SRM incorrectly damaged the farther opposing mech");

        mw::battle::BattleWorld friendlyLaserWorld =
            mw::battle::BattleWorld::create(friendlyHitParams);
        mw::battle::BattleInputCommand friendlyLaserFire;
        friendlyLaserFire.tickIndex = 0u;
        friendlyLaserFire.entityId = friendlyLaserWorld.playerEntityId();
        friendlyLaserFire.selectWeaponInstanceId = 2u;
        friendlyLaserFire.fireWeapon = true;
        friendlyLaserWorld.enqueueInput(friendlyLaserFire);
        friendlyLaserWorld.tick();
        const mw::battle::BattleSnapshot friendlyLaserImpact =
            friendlyLaserWorld.snapshot();
        expect(
            friendlyLaserImpact.lastShot.hit &&
                friendlyLaserImpact.lastShot.targetEntityId ==
                    mw::battle::EntityId{2u} &&
                friendlyLaserImpact.lastShot.deliveryState ==
                    mw::battle::BattleShotDeliveryState::Immediate &&
                friendlyLaserImpact.lastShot.damageApplied > 0 &&
                sectionBattleDamage(
                    combatantFrom(
                        friendlyLaserImpact, mw::battle::EntityId{2u}),
                    friendlyLaserImpact.lastShot.armorSection) > 0 &&
                sectionBattleDamage(
                    combatantFrom(
                        friendlyLaserImpact, mw::battle::EntityId{3u}),
                    friendlyLaserImpact.lastShot.armorSection) == 0,
            "player immediate weapon must use the same friendly component ownership");

        mw::battle::BattleStartParams inactiveFriendlyParams =
            friendlyHitParams;
        inactiveFriendlyParams.playerAlliedLaunchStates[0].priorMissionStatus =
            mw::battle::CombatantMissionStatus::Destroyed;
        mw::battle::BattleWorld inactiveFriendlyWorld =
            mw::battle::BattleWorld::create(inactiveFriendlyParams);
        friendlyLaserFire.entityId = inactiveFriendlyWorld.playerEntityId();
        inactiveFriendlyWorld.enqueueInput(friendlyLaserFire);
        inactiveFriendlyWorld.tick();
        const mw::battle::BattleSnapshot inactiveFriendlyShot =
            inactiveFriendlyWorld.snapshot();
        expect(
            inactiveFriendlyShot.lastShot.hit &&
                inactiveFriendlyShot.lastShot.targetEntityId ==
                    mw::battle::EntityId{3u} &&
                sectionBattleDamage(
                    combatantFrom(
                        inactiveFriendlyShot, mw::battle::EntityId{2u}),
                    inactiveFriendlyShot.lastShot.armorSection) == 0 &&
                sectionBattleDamage(
                    combatantFrom(
                        inactiveFriendlyShot, mw::battle::EntityId{3u}),
                    inactiveFriendlyShot.lastShot.armorSection) > 0,
            "inactive friendly combatant must remain excluded from the player crosshair caller");

        mw::battle::BattleReplay friendlyMissileReplay;
        friendlyMissileFire.entityId = {};
        friendlyMissileReplay.commands = {friendlyMissileFire};
        const mw::battle::BattleSnapshot friendlyMissileReplayA =
            mw::battle::runBattleReplay(
                friendlyHitParams, friendlyMissileReplay, 8u);
        const mw::battle::BattleSnapshot friendlyMissileReplayB =
            mw::battle::runBattleReplay(
                friendlyHitParams, friendlyMissileReplay, 8u);
        expect(
            mw::battle::battleSnapshotFingerprint(friendlyMissileReplayA) ==
                    mw::battle::battleSnapshotFingerprint(
                        friendlyMissileReplayB) &&
                friendlyMissileReplayA.lastShot.targetEntityId ==
                    mw::battle::EntityId{2u} &&
                friendlyMissileReplayA.lastShot.damageApplied > 0,
            "player-friendly projectile delivery must be replay deterministic");

        mw::battle::BattleWorld friendlyMissileCadenceWorld =
            mw::battle::BattleWorld::create(friendlyHitParams);
        for (uint64_t tick = 0u; tick < 8u; ++tick) {
            for (int frame = 0; frame < 31; ++frame) {
                static_cast<void>(friendlyMissileCadenceWorld.snapshot());
            }
            if (tick == 0u) {
                mw::battle::BattleInputCommand command = friendlyMissileFire;
                command.entityId =
                    friendlyMissileCadenceWorld.playerEntityId();
                friendlyMissileCadenceWorld.enqueueInput(command);
            }
            friendlyMissileCadenceWorld.tick();
        }
        expect(
            mw::battle::battleSnapshotFingerprint(
                friendlyMissileCadenceWorld.snapshot()) ==
                mw::battle::battleSnapshotFingerprint(
                    friendlyMissileReplayA),
            "render cadence changed player-friendly projectile ownership or damage");

        // Regression for the live crash reported after an SRM fire request
        // missed every visible component.  This differs from the Locust MG
        // miss above: the accepted miss owns an unbound projectile vector
        // element until the recovered projectile lifetime expires.
        mw::battle::BattleStartParams projectileMissParams = crosshairParams;
        projectileMissParams.playerMechPresetId = "jenner";
        projectileMissParams.playerStartTransform =
            crosshairParams.playerStartTransform;
        expect(
            projectileMissParams.playerLaunchState.has_value(),
            "SRM miss fixture lost its authoritative player launch state");
        projectileMissParams.playerLaunchState->mechPresetId = "jenner";
        projectileMissParams.playerLaunchState->startTransform =
            projectileMissParams.playerStartTransform;
        projectileMissParams.playerLaunchState->persistentMechState.
            weaponConditions.fill(0u);
        mw::battle::BattleMechHitProfile jennerHitProfile =
            syntheticCrosshairHitProfile();
        jennerHitProfile.mechPresetId = "jenner";
        projectileMissParams.mechHitProfiles.push_back(
            std::move(jennerHitProfile));
        mw::battle::BattleWorld projectileMissWorld =
            mw::battle::BattleWorld::create(projectileMissParams);
        mw::battle::BattleInputCommand projectileMissFire;
        projectileMissFire.tickIndex = 0u;
        projectileMissFire.entityId = projectileMissWorld.playerEntityId();
        projectileMissFire.aimPitchStepDelta = 9;
        projectileMissFire.selectWeaponInstanceId = 1u;
        projectileMissFire.fireWeapon = true;
        projectileMissWorld.enqueueInput(projectileMissFire);
        projectileMissWorld.tick();
        const mw::battle::BattleSnapshot projectileMissLaunch =
            projectileMissWorld.snapshot();
        expect(
            playerFrom(projectileMissLaunch).lastFireRequestAccepted &&
                projectileMissLaunch.lastShot.valid &&
                projectileMissLaunch.lastShot.result ==
                    mw::battle::BattleShotResult::MissCrosshairRay &&
                !projectileMissLaunch.lastShot.hit &&
                projectileMissLaunch.lastShot.targetKind ==
                    mw::battle::BattleShotTargetKind::None &&
                !mw::battle::isValid(
                    projectileMissLaunch.lastShot.targetEntityId) &&
                projectileMissLaunch.lastShot.deliveryState ==
                    mw::battle::BattleShotDeliveryState::ProjectileInFlight &&
                projectileMissLaunch.shotEvents.size() == 1u &&
                projectileMissLaunch.shotEvents.front().sequence ==
                    projectileMissLaunch.lastShot.sequence &&
                projectileMissLaunch.projectiles.size() == 1u &&
                projectileMissLaunch.projectiles[0].weaponTypeId == "srm4" &&
                projectileMissLaunch.projectiles[0].originalVisualClass == 1u &&
                !projectileMissLaunch.projectiles[0].targetBound &&
                projectileMissLaunch.projectiles[0].launchForwardOffset == 405.0,
            "accepted SRM crosshair miss must own one safe unbound projectile");
        mw::battle::BattleSnapshot projectileMissExpired;
        for (uint64_t tick = 1u; tick <= 64u; ++tick) {
            projectileMissWorld.tick();
            const mw::battle::BattleSnapshot candidate =
                projectileMissWorld.snapshot();
            if (candidate.projectiles.empty()) {
                projectileMissExpired = candidate;
                break;
            }
        }
        expect(
            projectileMissExpired.projectiles.empty() &&
                projectileMissExpired.shotEvents.size() == 1u &&
                projectileMissExpired.lastShot.deliveryState ==
                    mw::battle::BattleShotDeliveryState::ProjectileExpired &&
                !projectileMissExpired.lastShot.hit &&
                projectileMissExpired.lastShot.damageApplied == 0,
            "unbound SRM miss must expire without target ownership or damage");

        mw::battle::BattleStartParams groundMissParams = projectileMissParams;
        groundMissParams.terrainCollisionObstacles = {};
        mw::battle::BattleWorld groundMissWorld =
            mw::battle::BattleWorld::create(groundMissParams);
        mw::battle::BattleInputCommand groundMissFire = projectileMissFire;
        groundMissFire.entityId = groundMissWorld.playerEntityId();
        groundMissFire.aimPitchStepDelta = -9;
        groundMissWorld.enqueueInput(groundMissFire);
        groundMissWorld.tick();
        for (int tick = 0;
             tick < 16 && !groundMissWorld.snapshot().projectiles.empty();
             ++tick) {
            groundMissWorld.tick();
        }
        const mw::battle::BattleSnapshot groundMissImpact =
            groundMissWorld.snapshot();
        expect(
            groundMissImpact.projectiles.empty() &&
                groundMissImpact.lastShot.result ==
                    mw::battle::BattleShotResult::HitTerrain &&
                groundMissImpact.lastShot.runtimeEffect ==
                    mw::battle::BattleShotRuntimeEffect::
                        ProjectileTerrainSmokeNoDamage &&
                std::abs(groundMissImpact.lastShot.impactPoint.y) < 0.001 &&
                groundMissImpact.lastShot.provenance.find(
                    "projectile_ground_plane_contact") != std::string::npos,
            "downward unbound missile must terminate on the flat ground plane");

        mw::battle::BattleReplay projectileMissReplay;
        projectileMissFire.entityId = {};
        projectileMissReplay.commands = {projectileMissFire};
        const mw::battle::BattleSnapshot projectileMissReplayA =
            mw::battle::runBattleReplay(
                projectileMissParams, projectileMissReplay, 65u);
        const mw::battle::BattleSnapshot projectileMissReplayB =
            mw::battle::runBattleReplay(
                projectileMissParams, projectileMissReplay, 65u);
        expect(
            mw::battle::battleSnapshotFingerprint(projectileMissReplayA) ==
                    mw::battle::battleSnapshotFingerprint(
                        projectileMissReplayB) &&
                projectileMissReplayA.projectiles.empty(),
            "unbound SRM miss replay or expiry changed");

        mw::battle::BattleStartParams missileInterceptParams = crosshairParams;
        missileInterceptParams.combatantLaunchStates[0].mechPresetId = "jenner";
        missileInterceptParams.combatantLaunchStates[0].startTransform = {
            1000.0, 0.0, 2500.0, 3.14159265358979323846};
        mw::battle::BattleMechHitProfile jennerInterceptProfile =
            syntheticCrosshairHitProfile();
        jennerInterceptProfile.mechPresetId = "jenner";
        missileInterceptParams.mechHitProfiles.push_back(
            jennerInterceptProfile);
        mw::battle::BattleWorld missileInterceptWorld =
            mw::battle::BattleWorld::create(missileInterceptParams);
        mw::battle::BattleInputCommand enemyMissileFire;
        enemyMissileFire.tickIndex = 0u;
        enemyMissileFire.entityId = mw::battle::EntityId{2u};
        enemyMissileFire.selectWeaponInstanceId = 1u;
        enemyMissileFire.fireWeapon = true;
        missileInterceptWorld.enqueueInput(enemyMissileFire);
        missileInterceptWorld.tick();
        const mw::battle::BattleSnapshot enemyMissileLaunch =
            missileInterceptWorld.snapshot();
        expect(
            enemyMissileLaunch.projectiles.size() == 1u &&
                enemyMissileLaunch.projectiles.front().weaponTypeId == "srm4" &&
                enemyMissileLaunch.projectiles.front().laserInterceptible &&
                enemyMissileLaunch.projectiles.front().collisionGeometryProven &&
                enemyMissileLaunch.projectiles.front()
                        .collisionGeometryFingerprint ==
                    missileInterceptParams.projectileHitProfiles[1]
                        .geometryFingerprint,
            "enemy missile launch lost exact OTHPCK interception geometry");
        const uint32_t interceptedMissileId =
            enemyMissileLaunch.projectiles.front().projectileId;
        const int interceptPlayerArmorBefore =
            sectionBattleDamage(
                playerFrom(enemyMissileLaunch),
                mw::mech3d::MechArmorSectionId::CenterTorso);
        const int interceptEnemyArmorBefore =
            sectionBattleDamage(
                combatantFrom(enemyMissileLaunch, mw::battle::EntityId{2u}),
                mw::mech3d::MechArmorSectionId::CenterTorso);
        mw::battle::BattleInputCommand interceptingLaser;
        interceptingLaser.tickIndex = 1u;
        interceptingLaser.entityId = missileInterceptWorld.playerEntityId();
        interceptingLaser.selectWeaponInstanceId = 1u;
        interceptingLaser.fireWeapon = true;
        missileInterceptWorld.enqueueInput(interceptingLaser);
        missileInterceptWorld.tick();
        const mw::battle::BattleSnapshot missileAirburst =
            missileInterceptWorld.snapshot();
        expect(
            missileAirburst.projectiles.empty() &&
                missileAirburst.lastShot.result ==
                    mw::battle::BattleShotResult::HitProjectile &&
                missileAirburst.lastShot.targetKind ==
                    mw::battle::BattleShotTargetKind::Projectile &&
                missileAirburst.lastShot.targetProjectileId ==
                    interceptedMissileId &&
                missileAirburst.lastShot.deliveryState ==
                    mw::battle::BattleShotDeliveryState::
                        ProjectileIntercepted &&
                missileAirburst.lastShot.runtimeEffect ==
                    mw::battle::BattleShotRuntimeEffect::
                        ProjectileInterceptedNoDamage &&
                missileAirburst.lastShot.damageApplied == 0 &&
                missileAirburst.lastShot.impactDamage == 0u &&
                sectionBattleDamage(
                    playerFrom(missileAirburst),
                    mw::mech3d::MechArmorSectionId::CenterTorso) ==
                    interceptPlayerArmorBefore &&
                sectionBattleDamage(
                    combatantFrom(
                        missileAirburst, mw::battle::EntityId{2u}),
                    mw::mech3d::MechArmorSectionId::CenterTorso) ==
                    interceptEnemyArmorBefore &&
                missileAirburst.impactEvents.size() == 1u &&
                missileAirburst.impactEvents.front().projectileId ==
                    interceptedMissileId &&
                missileAirburst.impactEvents.front().visualSequence ==
                    mw::battle::BattleImpactVisualSequence::
                        OriginalSixStage,
            "laser must airburst the intersected missile without applying damage");
        const mw::battle::CampaignBattleRenderScenePackage missileAirburstScene =
            mw::battle::campaignBattleRenderSceneFromSnapshot(
                missileAirburst,
                mw::battle::CampaignBattlePresentationMode::Cockpit,
                options.snarioPath.parent_path().parent_path());
        expect(
            missileAirburstScene.valid &&
                missileAirburstScene.beamVisuals.size() == 1u &&
                missileAirburstScene.impactVisuals.size() == 1u &&
                missileAirburstScene.impactVisuals.front().recordIndex == 2 &&
                std::abs(
                    missileAirburstScene.beamVisuals.front().end.x -
                    missileAirburst.lastShot.impactPoint.x) < 0.001 &&
                std::abs(
                    missileAirburstScene.beamVisuals.front().end.y -
                    missileAirburst.lastShot.impactPoint.y) < 0.001 &&
                std::abs(
                    missileAirburstScene.beamVisuals.front().end.z -
                    missileAirburst.lastShot.impactPoint.z) < 0.001,
            "interception scene must show the laser ending on the missile airburst");

        mw::battle::BattleReplay missileInterceptReplay;
        interceptingLaser.entityId = {};
        missileInterceptReplay.commands = {
            enemyMissileFire, interceptingLaser};
        const mw::battle::BattleSnapshot missileInterceptReplayA =
            mw::battle::runBattleReplay(
                missileInterceptParams, missileInterceptReplay, 3u);
        const mw::battle::BattleSnapshot missileInterceptReplayB =
            mw::battle::runBattleReplay(
                missileInterceptParams, missileInterceptReplay, 3u);
        expect(
            mw::battle::battleSnapshotFingerprint(missileInterceptReplayA) ==
                mw::battle::battleSnapshotFingerprint(
                    missileInterceptReplayB) &&
                missileInterceptReplayA.lastShot.result ==
                    mw::battle::BattleShotResult::HitProjectile,
            "laser missile interception replay fingerprint changed");
        mw::battle::BattleWorld missileInterceptCadenceWorld =
            mw::battle::BattleWorld::create(missileInterceptParams);
        for (uint64_t tick = 0u; tick < 3u; ++tick) {
            for (int frame = 0; frame < 23; ++frame) {
                static_cast<void>(missileInterceptCadenceWorld.snapshot());
            }
            for (mw::battle::BattleInputCommand command :
                 missileInterceptReplay.commands) {
                if (command.tickIndex == tick) {
                    if (!mw::battle::isValid(command.entityId)) {
                        command.entityId =
                            missileInterceptCadenceWorld.playerEntityId();
                    }
                    missileInterceptCadenceWorld.enqueueInput(command);
                }
            }
            missileInterceptCadenceWorld.tick();
        }
        expect(
            mw::battle::battleSnapshotFingerprint(
                missileInterceptCadenceWorld.snapshot()) ==
                mw::battle::battleSnapshotFingerprint(
                    missileInterceptReplayA),
            "render cadence changed laser missile interception");

        mw::battle::BattleStartParams acImmunityParams = crosshairParams;
        acImmunityParams.combatantLaunchStates[0].mechPresetId = "shadow_hawk";
        acImmunityParams.combatantLaunchStates[0].startTransform = {
            1000.0, 0.0, 2500.0, 3.14159265358979323846};
        mw::battle::BattleMechHitProfile shadowHawkInterceptProfile =
            syntheticCrosshairHitProfile();
        shadowHawkInterceptProfile.mechPresetId = "shadow_hawk";
        acImmunityParams.mechHitProfiles.push_back(
            shadowHawkInterceptProfile);
        mw::battle::BattleWorld acImmunityWorld =
            mw::battle::BattleWorld::create(acImmunityParams);
        mw::battle::BattleInputCommand enemyAcFire = enemyMissileFire;
        acImmunityWorld.enqueueInput(enemyAcFire);
        acImmunityWorld.tick();
        expect(
            acImmunityWorld.snapshot().projectiles.size() == 1u &&
                acImmunityWorld.snapshot().projectiles.front().weaponTypeId ==
                    "ac5" &&
                !acImmunityWorld.snapshot().projectiles.front()
                     .laserInterceptible,
            "AC/5 projectile must remain excluded from laser interception");
        interceptingLaser.entityId = acImmunityWorld.playerEntityId();
        acImmunityWorld.enqueueInput(interceptingLaser);
        acImmunityWorld.tick();
        const mw::battle::BattleSnapshot acLaserShot =
            acImmunityWorld.snapshot();
        expect(
            acLaserShot.projectiles.size() == 1u &&
                acLaserShot.lastShot.targetKind ==
                    mw::battle::BattleShotTargetKind::Combatant &&
                acLaserShot.lastShot.result ==
                    mw::battle::BattleShotResult::Hit &&
                acLaserShot.lastShot.targetProjectileId == 0u,
            "laser ray must pass through AC/5 projectile to its ordinary target");

        mw::battle::BattleReplay crosshairReplay;
        mw::battle::BattleInputCommand crosshairReplayFire;
        crosshairReplayFire.tickIndex = 0u;
        crosshairReplayFire.aimPitchStepDelta = 5;
        crosshairReplayFire.selectWeaponInstanceId = 2u;
        crosshairReplayFire.fireWeapon = true;
        crosshairReplay.commands = {crosshairReplayFire};
        const mw::battle::BattleSnapshot crosshairReplayA =
            mw::battle::runBattleReplay(crosshairParams, crosshairReplay, 2u);
        const mw::battle::BattleSnapshot crosshairReplayB =
            mw::battle::runBattleReplay(crosshairParams, crosshairReplay, 2u);
        expect(
            mw::battle::battleSnapshotFingerprint(crosshairReplayA) ==
                mw::battle::battleSnapshotFingerprint(crosshairReplayB),
            "crosshair component hit replay fingerprint changed");
        mw::battle::BattleWorld crosshairCadenceWorld =
            mw::battle::BattleWorld::create(crosshairParams);
        for (int frame = 0; frame < 41; ++frame) {
            static_cast<void>(crosshairCadenceWorld.snapshot());
        }
        crosshairReplayFire.entityId = crosshairCadenceWorld.playerEntityId();
        crosshairCadenceWorld.enqueueInput(crosshairReplayFire);
        crosshairCadenceWorld.tick();
        for (int frame = 0; frame < 67; ++frame) {
            static_cast<void>(crosshairCadenceWorld.snapshot());
        }
        crosshairCadenceWorld.tick();
        expect(
            mw::battle::battleSnapshotFingerprint(
                crosshairCadenceWorld.snapshot()) ==
                mw::battle::battleSnapshotFingerprint(crosshairReplayA),
            "render snapshot cadence changed crosshair component hit state");

        mw::battle::BattleStartParams enemyRayParams = crosshairParams;
        enemyRayParams.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::Phase10CompatibilityFsm;
        enemyRayParams.enemyActivationRange = 1000.0;
        enemyRayParams.enemyAttackRange = 800.0;
        mw::battle::BattleWorld enemyRayWorld =
            mw::battle::BattleWorld::create(enemyRayParams);
        mw::battle::BattleInputCommand enemyRayFire;
        enemyRayFire.tickIndex = 0u;
        enemyRayFire.entityId = mw::battle::EntityId{2u};
        enemyRayFire.selectWeaponInstanceId = 1u;
        enemyRayFire.fireWeapon = true;
        enemyRayWorld.enqueueInput(enemyRayFire);
        enemyRayWorld.tick();
        const mw::battle::BattleSnapshot enemyRayShot = enemyRayWorld.snapshot();
        const auto& enemyRayPlayer = playerFrom(enemyRayShot);
        expect(enemyRayShot.lastShot.result == mw::battle::BattleShotResult::Hit &&
                   enemyRayShot.lastShot.shooterEntityId.value == 2u &&
                   enemyRayShot.lastShot.targetEntityId == enemyRayPlayer.id &&
                   enemyRayShot.lastShot.armorSection ==
                       mw::mech3d::MechArmorSectionId::CenterTorso &&
                   sectionBattleDamage(
                       enemyRayPlayer,
                       mw::mech3d::MechArmorSectionId::CenterTorso) == 5,
               "enemy-owned fire request must use the same crosshair component ray pipeline");

        expect(start.objective.valid, "mission start snapshot should own objective state");
        expect(start.objective.provenance == "setup_derived_diagnostic", "objective provenance changed");
        expect(start.objective.sourceSlot == "opposing:1", "objective source slot changed");
        expect(start.objective.transformProven, "diagnostic objective transform should be proven");
        expect(!start.objective.activeObjectProven, "diagnostic objective must not claim an active object");
        expect(start.objective.missionIntent == mw::battle::BattleMissionObjectiveIntent::Unknown &&
                   !start.objective.missionIntentProven &&
                   start.objective.missionTargetKind.empty() &&
                   !start.objective.missionIntentBoundToObjective,
               "synthetic selector 10 objective must not infer title metadata");
        expect(!start.objective.missionSemanticsProven, "objective mission semantics must remain unproven");
        expect(start.objective.damagePolicyProven && start.objective.damageSuppressed,
               "selector 10 objective damage-suppression policy changed");
        expect(start.objective.depletionPolicyProven &&
                   start.objective.depletionSetsPlayerWinCondition &&
                   start.objective.depletionSetsPlayerLossCondition &&
                   start.objective.depletionResultCode == 0,
               "selector 10 objective depletion outcome policy changed");
        expectNear(start.objective.transform.x, 68000.0, 0.001, "objective x");
        expectNear(start.objective.transform.z, 35000.0, 0.001, "objective z");
        expect(start.objective.staticModel.valid, "objective static model missing");
        expect(start.objective.staticModel.resourceName == "OTHPCK.TBL", "objective model resource changed");
        expect(start.objective.staticModel.recordIndex == 14, "objective model record changed");
        expect(!start.objective.staticModel.animated, "objective model should remain static");
        const uint64_t startHash = mw::battle::battleSnapshotFingerprint(start);

        bool rejectedUnprovenObjectiveDamagePolicy = false;
        try {
            mw::battle::BattleStartParams invalidObjectiveParams = params;
            invalidObjectiveParams.objective.damagePolicyProven = false;
            static_cast<void>(mw::battle::BattleWorld::create(invalidObjectiveParams));
        } catch (const std::runtime_error&) {
            rejectedUnprovenObjectiveDamagePolicy = true;
        }
        expect(rejectedUnprovenObjectiveDamagePolicy,
               "suppressed objective damage policy must require proven source metadata");

        bool rejectedUnprovenObjectiveDepletionPolicy = false;
        try {
            mw::battle::BattleStartParams invalidObjectiveParams = params;
            invalidObjectiveParams.objective.depletionPolicyProven = false;
            static_cast<void>(mw::battle::BattleWorld::create(invalidObjectiveParams));
        } catch (const std::runtime_error&) {
            rejectedUnprovenObjectiveDepletionPolicy = true;
        }
        expect(rejectedUnprovenObjectiveDepletionPolicy,
               "objective depletion outcome must require proven source metadata");

        bool rejectedConflictingObjectiveDepletionResult = false;
        try {
            mw::battle::BattleStartParams invalidObjectiveParams = params;
            invalidObjectiveParams.objective.depletionResultCode = 1;
            static_cast<void>(mw::battle::BattleWorld::create(invalidObjectiveParams));
        } catch (const std::runtime_error&) {
            rejectedConflictingObjectiveDepletionResult = true;
        }
        expect(rejectedConflictingObjectiveDepletionResult,
               "objective depletion result must preserve BTECH player-win precedence");

        bool rejectedUnprovenObjectiveTransform = false;
        try {
            mw::battle::BattleStartParams invalidObjectiveParams = params;
            invalidObjectiveParams.objective.activeObjectProven = true;
            invalidObjectiveParams.objective.transformProven = false;
            static_cast<void>(mw::battle::BattleWorld::create(invalidObjectiveParams));
        } catch (const std::runtime_error&) {
            rejectedUnprovenObjectiveTransform = true;
        }
        expect(rejectedUnprovenObjectiveTransform,
               "proven active objective object must require a proven transform");

        mw::battle::BattleStartParams explicitRosterParams = params;
        mw::battle::BattleCombatantLaunchState explicitEnemy;
        explicitEnemy.mechPresetId = "shadow_hawk";
        explicitEnemy.startTransform = mw::battle::Transform{62000.0, 0.0, 34500.0, 0.5};
        explicitEnemy.roster.team = mw::battle::BattleTeam::Opposing;
        explicitEnemy.roster.factionHouseId = static_cast<uint8_t>(3);
        explicitEnemy.roster.factionHouseName = "LIAO";
        explicitEnemy.roster.provenance = "explicit_launch_roster";
        explicitEnemy.roster.sourceSlot = "opposing:0";
        explicitRosterParams.combatantLaunchStates.push_back(explicitEnemy);
        const mw::battle::BattleSnapshot explicitRosterSnapshot =
            mw::battle::BattleWorld::create(explicitRosterParams).missionStartSnapshot();
        expect(explicitRosterSnapshot.combatants.size() == 2u,
               "one explicit enemy roster entry should spawn exactly one non-player combatant");
        expect(!explicitRosterSnapshot.contract.oppositionSpawnPlan.combatantsSpawned,
               "explicit launch roster must not promote the diagnostic opposition plan into spawning");
        const mw::battle::CombatantSnapshot& explicitEnemySnapshot = explicitRosterSnapshot.combatants[1];
        expect(!explicitEnemySnapshot.playerControlled,
               "explicit opposing roster combatant must remain non-player controlled");
        expect(explicitEnemySnapshot.mechPresetId == "shadow_hawk",
               "explicit opposing roster mech preset changed");
        expect(explicitEnemySnapshot.roster.team == mw::battle::BattleTeam::Opposing,
               "explicit enemy roster team changed");
        expect(explicitEnemySnapshot.roster.factionHouseId == 3,
               "explicit enemy roster faction id changed");
        expect(explicitEnemySnapshot.roster.factionHouseName == "LIAO",
               "explicit enemy roster faction name changed");
        expect(explicitEnemySnapshot.roster.sourceSlot == "opposing:0",
               "explicit enemy roster source slot changed");
        expect(explicitRosterSnapshot.combatants[0]
                       .roster.originalLiveObjectSlot == 0u &&
                   explicitEnemySnapshot.roster.originalLiveObjectSlot == 4u,
               "ordered original player/opposing live-object slots changed");
        mw::battle::BattleSnapshot mutatedOriginalSlotSnapshot =
            explicitRosterSnapshot;
        mutatedOriginalSlotSnapshot.combatants[1]
            .roster.originalLiveObjectSlot = static_cast<uint8_t>(5u);
        expect(
            mw::battle::battleSnapshotFingerprint(
                mutatedOriginalSlotSnapshot) !=
                mw::battle::battleSnapshotFingerprint(
                    explicitRosterSnapshot),
            "original live-object slot must participate in replay fingerprint");
        bool rejectedMismatchedOriginalSlot = false;
        try {
            mw::battle::BattleStartParams mismatchedOriginalSlotParams =
                explicitRosterParams;
            mismatchedOriginalSlotParams.combatantLaunchStates[0]
                .roster.originalLiveObjectSlot = static_cast<uint8_t>(5u);
            static_cast<void>(
                mw::battle::BattleWorld::create(
                    mismatchedOriginalSlotParams));
        } catch (const std::runtime_error&) {
            rejectedMismatchedOriginalSlot = true;
        }
        expect(
            rejectedMismatchedOriginalSlot,
            "mismatched original live-object slot must fail closed");

        mw::battle::BattleStartParams enemyAiParams = explicitRosterParams;
        enemyAiParams.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::Phase10CompatibilityFsm;
        const mw::battle::BattleSnapshot enemyAiStart =
            mw::battle::BattleWorld::create(enemyAiParams).missionStartSnapshot();
        const mw::battle::BattleSnapshot enemyAiSearch =
            mw::battle::runBattleReplay(enemyAiParams, mw::battle::BattleReplay{}, 6);
        expect(enemyAiSearch.combatants.size() == 2u,
               "enemy AI search snapshot should keep one player and one enemy");
        expect(enemyAiSearch.combatants[1].enemyAiState == mw::battle::BattleEnemyAiState::Search,
               "far opposing combatant should wait in search state");
        expect(enemyAiSearch.combatants[1].enemyAiTargetKind == mw::battle::BattleEnemyAiTargetKind::Player,
               "searching opposing combatant should keep player target kind");
        expect(enemyAiSearch.combatants[1].enemyAiTargetEntityId == enemyAiSearch.combatants[0].id,
               "search state should still track the player team target");
        expectNear(enemyAiSearch.combatants[1].forwardSpeed, 0.0, 0.001,
                   "searching enemy should stand still");
        expectNear(enemyAiSearch.combatants[1].transform.headingRadians, 0.5, 0.0001,
                   "searching enemy should not rotate toward distant player");

        mw::battle::BattleStartParams enemyAiApproachParams = explicitRosterParams;
        enemyAiApproachParams.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::Phase10CompatibilityFsm;
        enemyAiApproachParams.combatantLaunchStates[0].startTransform =
            mw::battle::Transform{46500.0, 0.0, 23500.0, 0.5};
        const mw::battle::BattleSnapshot enemyAiApproachStart =
            mw::battle::BattleWorld::create(enemyAiApproachParams).missionStartSnapshot();
        const mw::battle::BattleSnapshot enemyAiApproachA =
            mw::battle::runBattleReplay(enemyAiApproachParams, mw::battle::BattleReplay{}, 6);
        const mw::battle::BattleSnapshot enemyAiApproachB =
            mw::battle::runBattleReplay(enemyAiApproachParams, mw::battle::BattleReplay{}, 6);
        expect(
            mw::battle::battleSnapshotFingerprint(enemyAiApproachA) ==
                mw::battle::battleSnapshotFingerprint(enemyAiApproachB),
            "deterministic enemy approach replay fingerprint changed");
        expect(enemyAiApproachA.combatants.size() == 2u,
               "enemy AI approach snapshot should keep one player and one enemy");
        const mw::battle::CombatantSnapshot& aiPlayer = enemyAiApproachA.combatants[0];
        const mw::battle::CombatantSnapshot& aiEnemy = enemyAiApproachA.combatants[1];
        const double enemyAiInitialDistance =
            std::hypot(
                enemyAiApproachStart.combatants[1].transform.x - enemyAiApproachStart.combatants[0].transform.x,
                enemyAiApproachStart.combatants[1].transform.z - enemyAiApproachStart.combatants[0].transform.z);
        expect(aiEnemy.enemyAiState == mw::battle::BattleEnemyAiState::Approach,
               "activated opposing combatant should approach the player target");
        expect(aiEnemy.enemyAiTargetKind == mw::battle::BattleEnemyAiTargetKind::Player,
               "approaching opposing combatant should target the player kind");
        expect(aiEnemy.enemyAiTargetEntityId == aiPlayer.id,
               "opposing combatant should target the player team");
        expect(aiEnemy.enemyAiTargetDistance < enemyAiInitialDistance,
               "opposing approach should reduce target distance");
        expect(aiEnemy.forwardSpeed > 0.0, "approaching enemy should command forward motion");

        mw::battle::BattleStartParams enemyAiAttackParams = explicitRosterParams;
        enemyAiAttackParams.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::Phase10CompatibilityFsm;
        enemyAiAttackParams.combatantLaunchStates[0].startTransform =
            mw::battle::Transform{43700.0, 0.0, 23500.0, 0.0};
        const mw::battle::BattleSnapshot enemyAiAttack =
            mw::battle::runBattleReplay(enemyAiAttackParams, mw::battle::BattleReplay{}, 1);
        expect(enemyAiAttack.combatants.size() == 2u,
               "enemy AI attack snapshot should keep one player and one enemy");
        expect(enemyAiAttack.combatants[1].enemyAiState == mw::battle::BattleEnemyAiState::Attack,
               "close opposing combatant should enter attack state");
        expect(enemyAiAttack.combatants[1].enemyAiTargetKind == mw::battle::BattleEnemyAiTargetKind::Player,
               "attack state should target the player kind");
        expect(enemyAiAttack.combatants[1].enemyAiTargetEntityId == enemyAiAttack.combatants[0].id,
               "attack state should preserve the player target id");
        expectNear(enemyAiAttack.combatants[1].forwardSpeed, 0.0, 0.001,
                   "attacking enemy should stop approach motion");

        mw::battle::BattleStartParams combatVictoryParams = explicitRosterParams;
        combatVictoryParams.deterministicCombatRuntimeEnabled = true;
        combatVictoryParams.weaponRange = 700.0;
        combatVictoryParams.weaponCooldownTicks = 3;
        combatVictoryParams.weaponDamagePerHit = 1;
        combatVictoryParams.mechSystemMaxDamage = 3;
        combatVictoryParams.combatantLaunchStates[0].startTransform =
            mw::battle::Transform{43800.0, 0.0, 23500.0, 0.0};
        mw::battle::BattleReplay playerFireReplay;
        for (uint64_t fireTick : {0ull, 3ull, 6ull}) {
            mw::battle::BattleInputCommand fireCommand;
            fireCommand.tickIndex = fireTick;
            fireCommand.fireWeapon = true;
            playerFireReplay.commands.push_back(fireCommand);
        }
        const mw::battle::StandaloneBattleRunResult combatVictoryA =
            mw::battle::runStandaloneBattle(
                combatVictoryParams,
                playerFireReplay,
                mw::battle::StandaloneBattleRunOptions{70, true});
        const mw::battle::StandaloneBattleRunResult combatVictoryB =
            mw::battle::runStandaloneBattle(
                combatVictoryParams,
                playerFireReplay,
                mw::battle::StandaloneBattleRunOptions{70, true});
        expect(combatVictoryA.terminal && combatVictoryA.result.has_value(),
               "deterministic combat victory should produce terminal result");
        expect(combatVictoryA.result->state == mw::battle::BattleMissionRuntimeState::Victory &&
                   combatVictoryA.result->rawResultCode == 0 &&
                   combatVictoryA.result->reason == "all_opposing_mechs_destroyed",
               "deterministic combat victory result changed");
        expect(combatVictoryA.ticksExecuted == 57,
               "deterministic combat victory terminal tick changed");
        expect(
            mw::battle::battleSnapshotFingerprint(combatVictoryA.finalSnapshot) ==
                mw::battle::battleSnapshotFingerprint(combatVictoryB.finalSnapshot),
            "deterministic combat victory replay fingerprint changed");
        expect(combatVictoryA.finalSnapshot.combatants.size() == 2u,
               "deterministic combat victory snapshot should keep player and enemy");
        const mw::battle::CombatantSnapshot& combatVictoryPlayer =
            combatVictoryA.finalSnapshot.combatants[0];
        const mw::battle::CombatantSnapshot& combatVictoryEnemy =
            combatVictoryA.finalSnapshot.combatants[1];
        const mw::battle::BattleMechSystemSnapshot* victoryEnemyCore =
            findSystem(combatVictoryEnemy.mechSystems, mw::battle::BattleMechSystemRole::Core);
        expect(combatVictoryPlayer.weaponShotsFired == 3 &&
                   combatVictoryPlayer.weaponHitsLanded == 3 &&
                   combatVictoryPlayer.lastWeaponTargetEntityId == combatVictoryEnemy.id,
               "player deterministic weapon diagnostics changed");
        expect(combatVictoryEnemy.mechDestroyed &&
                   combatVictoryEnemy.missionStatus == mw::battle::CombatantMissionStatus::Destroyed,
               "enemy should be destroyed by deterministic player combat");
        expect(victoryEnemyCore != nullptr &&
                   victoryEnemyCore->damage == 3 &&
                   victoryEnemyCore->maxDamage == 3 &&
                   victoryEnemyCore->status == mw::battle::BattleMechSystemStatus::Destroyed,
               "enemy core system damage snapshot changed");

        mw::battle::BattleStartParams combatDefeatParams = enemyAiAttackParams;
        combatDefeatParams.deterministicCombatRuntimeEnabled = true;
        combatDefeatParams.weaponRange = 700.0;
        combatDefeatParams.weaponCooldownTicks = 3;
        combatDefeatParams.weaponDamagePerHit = 1;
        combatDefeatParams.mechSystemMaxDamage = 3;
        const mw::battle::StandaloneBattleRunResult combatDefeat =
            mw::battle::runStandaloneBattle(
                combatDefeatParams,
                mw::battle::BattleReplay{},
                mw::battle::StandaloneBattleRunOptions{70, true});
        expect(combatDefeat.terminal && combatDefeat.result.has_value(),
               "deterministic combat defeat should produce terminal result");
        expect(combatDefeat.result->state == mw::battle::BattleMissionRuntimeState::Defeat &&
                   combatDefeat.result->rawResultCode == 1 &&
                   combatDefeat.result->reason == "player_mech_destroyed",
               "deterministic combat defeat result changed");
        expect(combatDefeat.ticksExecuted == 57,
               "deterministic combat defeat terminal tick changed");
        expect(combatDefeat.finalSnapshot.combatants.size() == 2u,
               "deterministic combat defeat snapshot should keep player and enemy");
        const mw::battle::CombatantSnapshot& combatDefeatPlayer =
            combatDefeat.finalSnapshot.combatants[0];
        const mw::battle::CombatantSnapshot& combatDefeatEnemy =
            combatDefeat.finalSnapshot.combatants[1];
        const mw::battle::BattleMechSystemSnapshot* defeatPlayerCore =
            findSystem(combatDefeatPlayer.mechSystems, mw::battle::BattleMechSystemRole::Core);
        expect(combatDefeatEnemy.weaponShotsFired == 3 &&
                   combatDefeatEnemy.weaponHitsLanded == 3 &&
                   combatDefeatEnemy.lastWeaponTargetEntityId == combatDefeatPlayer.id,
               "enemy deterministic weapon diagnostics changed");
        expect(combatDefeatPlayer.mechDestroyed &&
                   combatDefeatPlayer.lastDamageSourceEntityId == combatDefeatEnemy.id,
               "player should be destroyed by deterministic enemy combat");
        expect(defeatPlayerCore != nullptr &&
                   defeatPlayerCore->damage == 3 &&
                   defeatPlayerCore->maxDamage == 3 &&
                   defeatPlayerCore->status == mw::battle::BattleMechSystemStatus::Destroyed,
               "player core system damage snapshot changed");

        mw::battle::BattleStartParams combatRetreatParams = enemyAiAttackParams;
        combatRetreatParams.deterministicCombatRuntimeEnabled = true;
        combatRetreatParams.enemyRetreatOnDamageEnabled = true;
        combatRetreatParams.weaponRange = 700.0;
        combatRetreatParams.weaponCooldownTicks = 3;
        combatRetreatParams.weaponDamagePerHit = 1;
        combatRetreatParams.mechSystemMaxDamage = 5;
        combatRetreatParams.enemyRetreatCoreDamageThreshold = 2;
        mw::battle::BattleReplay retreatFireReplay;
        for (uint64_t fireTick : {0ull, 3ull}) {
            mw::battle::BattleInputCommand fireCommand;
            fireCommand.tickIndex = fireTick;
            fireCommand.fireWeapon = true;
            retreatFireReplay.commands.push_back(fireCommand);
        }
        const mw::battle::BattleSnapshot combatRetreatA =
            mw::battle::runBattleReplay(combatRetreatParams, retreatFireReplay, 5);
        const mw::battle::BattleSnapshot combatRetreatB =
            mw::battle::runBattleReplay(combatRetreatParams, retreatFireReplay, 5);
        expect(
            mw::battle::battleSnapshotFingerprint(combatRetreatA) ==
                mw::battle::battleSnapshotFingerprint(combatRetreatB),
            "deterministic combat retreat replay fingerprint changed");
        expect(combatRetreatA.combatants.size() == 2u,
               "deterministic combat retreat snapshot should keep player and enemy");
        expect(combatRetreatA.missionRuntimeState == mw::battle::BattleMissionRuntimeState::InProgress,
               "damage-triggered retreat should not terminally settle the battle");
        const mw::battle::CombatantSnapshot& combatRetreatPlayer = combatRetreatA.combatants[0];
        const mw::battle::CombatantSnapshot& combatRetreatEnemy = combatRetreatA.combatants[1];
        const mw::battle::BattleMechSystemSnapshot* retreatEnemyCore =
            findSystem(combatRetreatEnemy.mechSystems, mw::battle::BattleMechSystemRole::Core);
        expect(combatRetreatEnemy.enemyAiState == mw::battle::BattleEnemyAiState::Retreat,
               "damaged enemy should enter retreat state before destruction");
        expect(combatRetreatEnemy.enemyAiTargetKind == mw::battle::BattleEnemyAiTargetKind::Player &&
                   combatRetreatEnemy.enemyAiTargetEntityId == combatRetreatPlayer.id,
               "retreating enemy should keep the player as the threat target");
        expect(retreatEnemyCore != nullptr &&
                   retreatEnemyCore->damage == 2 &&
                   retreatEnemyCore->maxDamage == 5 &&
                   retreatEnemyCore->status == mw::battle::BattleMechSystemStatus::Degraded,
               "retreating enemy core damage snapshot changed");
        expect(combatRetreatPlayer.weaponShotsFired == 2 &&
                   combatRetreatPlayer.weaponHitsLanded == 2 &&
                   combatRetreatEnemy.weaponShotsFired == 2,
               "retreat combat weapon diagnostics changed");
        expect(combatRetreatEnemy.forwardSpeed > 0.0,
               "retreating enemy should command forward movement away from the player");
        expect(
            std::hypot(
                combatRetreatEnemy.transform.x - combatRetreatPlayer.transform.x,
                combatRetreatEnemy.transform.z - combatRetreatPlayer.transform.z) > 200.0,
               "retreating enemy should increase distance from the player");

        const std::string combatVictoryState =
            mw::battle::battleMissionRuntimeStateName(combatVictoryA.result->state);
        const std::string combatDefeatState =
            mw::battle::battleMissionRuntimeStateName(combatDefeat.result->state);
        const std::string combatCoreStatus =
            mw::battle::battleMechSystemStatusName(victoryEnemyCore->status);
        const std::string combatRetreatState =
            mw::battle::battleEnemyAiStateName(combatRetreatEnemy.enemyAiState);

        mw::battle::BattleStartParams changedFactionParams = explicitRosterParams;
        changedFactionParams.combatantLaunchStates[0].roster.factionHouseId = static_cast<uint8_t>(2);
        changedFactionParams.combatantLaunchStates[0].roster.factionHouseName = "MARIK";
        const mw::battle::BattleSnapshot changedFactionSnapshot =
            mw::battle::BattleWorld::create(changedFactionParams).missionStartSnapshot();
        expect(
            mw::battle::battleSnapshotFingerprint(explicitRosterSnapshot) !=
                mw::battle::battleSnapshotFingerprint(changedFactionSnapshot),
            "snapshot fingerprint must own combatant faction metadata");

        bool rejectedPlayerOwnedEnemy = false;
        try {
            mw::battle::BattleStartParams invalidRosterParams = params;
            explicitEnemy.roster.team = mw::battle::BattleTeam::Player;
            invalidRosterParams.combatantLaunchStates.push_back(explicitEnemy);
            static_cast<void>(mw::battle::BattleWorld::create(invalidRosterParams));
        } catch (const std::runtime_error&) {
            rejectedPlayerOwnedEnemy = true;
        }
        expect(rejectedPlayerOwnedEnemy,
               "non-player launch roster must reject player-team ownership");

        const mw::battle::BattleOppositionSpawnPlan cappedOppositionProbe =
            mw::battle::decodeFreshBtechOppositionSpawnPlan({{2, 1, 3}});
        expect(cappedOppositionProbe.requests.size() == 4u, "BTECH opposition object limit changed");
        expect(joinOppositionRequests(cappedOppositionProbe) == "3:4-7,4:2-3,5:0-1,3:4-7",
               "BTECH opposition round-robin request order changed");
        expect(cappedOppositionProbe.remainingCounts == std::array<uint8_t, 3>{{0, 0, 2}},
               "BTECH opposition capped remainder changed");
        expect(joinOppositionSlots(cappedOppositionProbe) == "4->0,5->1,6->2,7->3",
               "BTECH runtime-to-opposing slot mapping changed");
        expect(joinOppositionClasses(cappedOppositionProbe) == "heavy,medium,light,heavy",
               "BTECH opposition estimate class order changed");
        expect(
            joinOppositionCandidatePresets(cappedOppositionProbe) ==
                "rifleman,warhammer,marauder,battlemaster;phoenix_hawk,shadow_hawk;locust,jenner;rifleman,warhammer,marauder,battlemaster",
            "BTECH candidate type-to-preset ranges changed");
        expect(
            joinBtechMechTypePresets() ==
                "0:locust,1:jenner,2:phoenix_hawk,3:shadow_hawk,4:rifleman,5:warhammer,6:marauder,7:battlemaster",
            "BTECH mech type table changed");
        expect(!mw::battle::btechMechTypeDefinitionByPreset("wasp").has_value(),
               "Wasp must remain outside the BTECH opposition type table");
        expect(!mw::battle::btechMechTypeDefinitionByPreset("wolverine").has_value(),
               "Wolverine must remain outside the BTECH opposition type table");
        const mw::battle::BattleOppositionSpawnPlan zeroOppositionProbe =
            mw::battle::decodeFreshBtechOppositionSpawnPlan({{0, 0, 0}});
        expect(zeroOppositionProbe.requests.empty(), "zero BTECH counts should remain an empty diagnostic plan");

        expect(mw::battle::mwMainContractMechStrength(0) == 1u &&
                   mw::battle::mwMainContractMechStrength(2) == 1u &&
                   mw::battle::mwMainContractMechStrength(3) == 2u &&
                   mw::battle::mwMainContractMechStrength(5) == 2u &&
                   mw::battle::mwMainContractMechStrength(6) == 3u &&
                   mw::battle::mwMainContractMechStrength(9) == 3u,
               "MW_MAIN contract mech strength table changed");
        expect(mw::battle::mwMainContractReputationStrength(8) == 0u &&
                   mw::battle::mwMainContractReputationStrength(9) == 1u &&
                   mw::battle::mwMainContractReputationStrength(20) == 1u &&
                   mw::battle::mwMainContractReputationStrength(21) == 2u &&
                   mw::battle::mwMainContractReputationStrength(35) == 2u &&
                   mw::battle::mwMainContractReputationStrength(36) == 3u,
               "MW_MAIN contract reputation thresholds changed");
        expect(mw::battle::mwMainContractForceScore(36, 12) == 150,
               "MW_MAIN full heavy lance contract score changed");
        expect(
            mw::battle::mwMainContractOppositionCounts(122, 0) ==
                std::array<uint8_t, 3>{{4, 0, 0}},
            "MW_MAIN count cap must discard light and medium buckets first");
        expect(
            mw::battle::mwMainContractOppositionCounts(100, 4) ==
                std::array<uint8_t, 3>{{4, 0, 0}} &&
            mw::battle::mwMainContractOppositionCounts(100, 5) ==
                std::array<uint8_t, 3>{{3, 1, 0}} &&
            mw::battle::mwMainContractOppositionCounts(100, 8) ==
                std::array<uint8_t, 3>{{2, 2, 0}},
            "MW_MAIN four-heavy random mix thresholds changed");
        expect(
            mw::battle::btechExtendedStageOppositionCounts({{6, 3, 3}}, 0) ==
                std::array<uint8_t, 3>{{2, 1, 1}} &&
            mw::battle::btechExtendedStageOppositionCounts({{6, 3, 3}}, 1) ==
                std::array<uint8_t, 3>{{2, 1, 1}} &&
            mw::battle::btechExtendedStageOppositionCounts({{6, 3, 3}}, 2) ==
                std::array<uint8_t, 3>{{2, 1, 1}},
            "BTECH extended campaign count thirds changed");
        const mw::battle::BattleOppositionSpawnPlan resolvedOppositionProbe =
            mw::battle::resolveBtechOppositionSpawnPlan(
                {{2, 1, 1}}, 0x12345678u, "opposition_runtime_smoke");
        expect(resolvedOppositionProbe.requests.size() == 4u &&
                   resolvedOppositionProbe.selectedTypeMappingProven,
               "resolved BTECH opposition plan missing");
        for (const mw::battle::BtechOppositionSpawnRequest& request :
             resolvedOppositionProbe.requests) {
            expect(request.selectedTypeId.has_value() &&
                       *request.selectedTypeId >= request.candidateTypeMin &&
                       *request.selectedTypeId <= request.candidateTypeMax &&
                       !request.selectedMechPresetId.empty(),
                   "resolved BTECH opponent escaped its original type mask");
        }

        mw::battle::BattleStartParams finalRosterParams = params;
        finalRosterParams.oppositionRoster = mw::battle::originalDarkWingFinalOppositionRoster();
        const mw::battle::BattleSnapshot finalRosterSnapshot =
            mw::battle::BattleWorld::create(finalRosterParams).missionStartSnapshot();
        expect(finalRosterSnapshot.oppositionRoster.valid,
               "Dark Wing final opposition roster metadata missing");
        expect(finalRosterSnapshot.oppositionRoster.compositionProven,
               "Dark Wing final opposition composition must be marked proven");
        expect(finalRosterSnapshot.oppositionRoster.sourceContextMissionByte == 0x63,
               "Dark Wing final source context mission byte changed");
        expect(finalRosterSnapshot.oppositionRoster.initialMissionSelector == 12u,
               "Dark Wing final initial mission selector changed");
        expect(finalRosterSnapshot.oppositionRoster.spawnOrderProven,
               "Dark Wing final spawn order should be proven");
        expect(finalRosterSnapshot.oppositionRoster.placementProven,
               "Dark Wing final placement should be proven");
        expect(!finalRosterSnapshot.oppositionRoster.combatantsSpawned,
               "Dark Wing final roster factory must remain metadata-only until explicitly applied");
        expect(oppositionRosterCount(finalRosterSnapshot.oppositionRoster, "battlemaster") == 3u,
               "Dark Wing final BattleMaster count changed");
        expect(oppositionRosterCount(finalRosterSnapshot.oppositionRoster, "warhammer") == 1u,
               "Dark Wing final Warhammer count changed");
        expect(joinOppositionRosterSlots(finalRosterSnapshot.oppositionRoster) ==
                   "0:7@4->0,1:7@5->1,2:7@6->2,3:5@7->3",
               "Dark Wing final ordered slot contract changed");
        expect(finalRosterSnapshot.combatants.size() == 1u,
               "Dark Wing final roster factory must not implicitly spawn enemies");
        expect(
            mw::battle::battleSnapshotFingerprint(finalRosterSnapshot) != startHash,
            "snapshot fingerprint must own fixed opposition roster metadata");

        bool rejectedWaspOppositionRoster = false;
        try {
            mw::battle::BattleStartParams invalidFinalRosterParams = params;
            invalidFinalRosterParams.oppositionRoster = mw::battle::originalDarkWingFinalOppositionRoster();
            invalidFinalRosterParams.oppositionRoster.entries[0] = {1, "wasp", 3};
            static_cast<void>(mw::battle::BattleWorld::create(invalidFinalRosterParams));
        } catch (const std::runtime_error&) {
            rejectedWaspOppositionRoster = true;
        }
        expect(rejectedWaspOppositionRoster,
               "fixed BTECH opposition roster must reject Wasp type/preset substitution");

        const mw::battle::OriginalBattlefieldSetup finalBattleSetup =
            mw::battle::decodeOriginalDarkWingFinalBattlefieldSetup(
                options.snarioPath,
                options.scenarioIndex,
                500.0);
        expect(finalBattleSetup.missionSelector == 12u,
               "Dark Wing final setup must use the special-path initial selector");
        expect(finalBattleSetup.metadata.playerMode == 3 && finalBattleSetup.metadata.opposingMode == 3,
               "Dark Wing final placement modes changed");
        expect(finalBattleSetup.objective.damagePolicyProven &&
                   finalBattleSetup.objective.damageSuppressed &&
                   !finalBattleSetup.objective.missionSemanticsProven,
               "Dark Wing final selector-12 objective policy metadata changed");

        mw::battle::BattleStartParams contractSpawnParams = params;
        contractSpawnParams.mission = mw::battle::battleMissionBriefingFromDefinition(
            *mw::battle::battleMissionDefinitionById(12));
        contractSpawnParams.playerStartTransform = finalBattleSetup.playerStartTransform;
        contractSpawnParams.setupMetadata = finalBattleSetup.metadata;
        contractSpawnParams.objective = {};
        mw::battle::applyOriginalContractOpposition(
            contractSpawnParams,
            finalBattleSetup,
            resolvedOppositionProbe,
            static_cast<uint8_t>(3),
            "LIAO");
        const mw::battle::BattleSnapshot contractSpawnSnapshot =
            mw::battle::BattleWorld::create(contractSpawnParams).missionStartSnapshot();
        expect(contractSpawnSnapshot.combatants.size() == 5u &&
                   contractSpawnSnapshot.oppositionRoster.combatantsSpawned &&
                   contractSpawnSnapshot.contract.oppositionSpawnPlan.combatantsSpawned,
               "contract opposition bridge must create all four requested opponents");
        for (size_t slotIndex = 0; slotIndex < 4u; ++slotIndex) {
            const mw::battle::CombatantSnapshot& opponent =
                contractSpawnSnapshot.combatants[slotIndex + 1u];
            const mw::battle::Transform& expectedTransform =
                finalBattleSetup.placement.opposingSide.positions[slotIndex].transform;
            expect(opponent.roster.team == mw::battle::BattleTeam::Opposing &&
                       opponent.roster.sourceSlot ==
                           "opposing:" + std::to_string(slotIndex),
                   "contract opponent slot ownership changed");
            expectNear(opponent.transform.x, expectedTransform.x, 0.001,
                       "contract opposing slot x");
            expectNear(opponent.transform.z, expectedTransform.z, 0.001,
                       "contract opposing slot z");
        }

        mw::battle::BattleStartParams finalSpawnParams = params;
        finalSpawnParams.mission = mw::battle::battleMissionBriefingFromDefinition(
            *mw::battle::battleMissionDefinitionById(12));
        finalSpawnParams.playerStartTransform = finalBattleSetup.playerStartTransform;
        finalSpawnParams.setupMetadata = finalBattleSetup.metadata;
        finalSpawnParams.objective = {};
        mw::battle::applyOriginalDarkWingFinalOpposition(
            finalSpawnParams,
            finalBattleSetup,
            std::nullopt,
            "DARK WING");
        const mw::battle::BattleSnapshot finalSpawnSnapshot =
            mw::battle::BattleWorld::create(finalSpawnParams).missionStartSnapshot();
        expect(finalSpawnSnapshot.oppositionRoster.combatantsSpawned,
               "explicit Dark Wing final setup should mark its roster spawned");
        expect(finalSpawnSnapshot.combatants.size() == 5u,
               "Dark Wing final setup should create the player and four static opponents");
        expect(!finalSpawnSnapshot.objective.valid,
               "Dark Wing final roster bridge must not invent a factory objective for selector 12");
        for (size_t slotIndex = 0; slotIndex < 4; ++slotIndex) {
            const mw::battle::CombatantSnapshot& opponent = finalSpawnSnapshot.combatants[slotIndex + 1u];
            const mw::battle::BattleOppositionRosterSlot& rosterSlot =
                finalSpawnSnapshot.oppositionRoster.orderedSlots[slotIndex];
            const mw::battle::Transform& expectedTransform =
                finalBattleSetup.placement.opposingSide.positions[slotIndex].transform;
            expect(opponent.mechPresetId == rosterSlot.mechPresetId,
                   "Dark Wing final combatant order changed");
            expect(opponent.roster.team == mw::battle::BattleTeam::Opposing &&
                       opponent.roster.sourceSlot == "opposing:" + std::to_string(slotIndex),
                   "Dark Wing final combatant ownership changed");
            expectNear(opponent.transform.x, expectedTransform.x, 0.001,
                       "Dark Wing final opposing slot x");
            expectNear(opponent.transform.z, expectedTransform.z, 0.001,
                       "Dark Wing final opposing slot z");
            const auto& immutableSlot =
                finalSpawnSnapshot.setup.opposingSlots[slotIndex];
            const auto& sourceSlot =
                finalBattleSetup.placement.opposingSide.positions[slotIndex];
            expect(immutableSlot.originalRawPositionProven &&
                       immutableSlot.originalRawX == sourceSlot.originalWorldX &&
                       immutableSlot.originalRawZ == sourceSlot.originalWorldZ,
                   "original opposing raw placement must survive the immutable setup handoff");
        }
        for (size_t slotIndex = 0; slotIndex < 4; ++slotIndex) {
            const auto& immutableSlot =
                finalSpawnSnapshot.setup.playerSlots[slotIndex];
            const auto& sourceSlot =
                finalBattleSetup.placement.playerSide.positions[slotIndex];
            expect(immutableSlot.originalRawPositionProven &&
                       immutableSlot.originalRawX == sourceSlot.originalWorldX &&
                       immutableSlot.originalRawZ == sourceSlot.originalWorldZ,
                   "original player-side raw placement must survive the immutable setup handoff");
        }
        auto rawPlacementFingerprintMutation = finalSpawnSnapshot;
        ++rawPlacementFingerprintMutation.setup.playerSlots[0].originalRawX;
        expect(mw::battle::battleSnapshotFingerprint(finalSpawnSnapshot) !=
                   mw::battle::battleSnapshotFingerprint(
                       rawPlacementFingerprintMutation),
               "snapshot fingerprint must own proven original raw placement");
        mw::battle::BattleStartParams finalRuntimeParams = finalSpawnParams;
        finalRuntimeParams.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::Phase10CompatibilityFsm;
        finalRuntimeParams.deterministicCombatRuntimeEnabled = true;
        const mw::battle::BattleSnapshot finalRuntimeSnapshot =
            mw::battle::runBattleReplay(finalRuntimeParams, mw::battle::BattleReplay{}, 1);
        expect(finalRuntimeSnapshot.combatants.size() == 5u,
               "Dark Wing final runtime should keep the player and four opponents");
        size_t finalRuntimeOpposingTargets = 0;
        for (size_t slotIndex = 0; slotIndex < 4; ++slotIndex) {
            const mw::battle::CombatantSnapshot& opponent = finalRuntimeSnapshot.combatants[slotIndex + 1u];
            expect(opponent.roster.team == mw::battle::BattleTeam::Opposing,
                   "Dark Wing final runtime opponent team changed");
            expect(opponent.enemyAiTargetKind == mw::battle::BattleEnemyAiTargetKind::Player,
                   "Dark Wing final runtime should target the player through normal AI");
            expect(opponent.enemyAiState == mw::battle::BattleEnemyAiState::Search ||
                       opponent.enemyAiState == mw::battle::BattleEnemyAiState::Approach ||
                       opponent.enemyAiState == mw::battle::BattleEnemyAiState::Attack,
                   "Dark Wing final runtime opponent AI state changed");
            ++finalRuntimeOpposingTargets;
        }

        bool rejectedFinalLaunchRosterMismatch = false;
        try {
            mw::battle::BattleStartParams mismatchedFinalSpawnParams = finalSpawnParams;
            mismatchedFinalSpawnParams.combatantLaunchStates[0].mechPresetId = "warhammer";
            static_cast<void>(mw::battle::BattleWorld::create(mismatchedFinalSpawnParams));
        } catch (const std::runtime_error&) {
            rejectedFinalLaunchRosterMismatch = true;
        }
        expect(rejectedFinalLaunchRosterMismatch,
               "Dark Wing final launch states must remain owned by the ordered roster contract");

        const mw::battle::OriginalBattlefieldSetup lockedSetup =
            mw::battle::decodeOriginalBattlefieldSetup(options.snarioPath, options.scenarioIndex, 10, 500.0);
        const std::array<int, 35> expectedPlayerModes{{
            4, 4, 4, 4, 4, 4, 4, 0, 2, 3, 3, 3, 3, 3, 4, 0, 3, 2,
            1, 1, 2, 3, 3, 3, 3, 3, 3, 3, 0, 3, 3, 0, 3, 3, 3,
        }};
        const std::array<int, 35> expectedOpposingModes{{
            0, 0, 0, 0, 0, 0, 0, 2, 4, 3, 3, 3, 3, 3, 0, 2, 3, 4,
            1, 1, 4, 3, 3, 3, 3, 3, 3, 3, 2, 3, 3, 2, 3, 3, 3,
        }};
        for (size_t selector = 0; selector < expectedPlayerModes.size(); ++selector) {
            const mw::battle::OriginalBattleMissionPlacementModes modes =
                mw::battle::originalBattleMissionPlacementModes(selector);
            expect(modes.playerMode == expectedPlayerModes[selector] &&
                       modes.opposingMode == expectedOpposingModes[selector],
                   "decoded BTECH placement mode table changed at selector " + std::to_string(selector));
            expect(modes.dedicatedObjectiveAllocated ==
                       (modes.playerMode == 4 || modes.opposingMode == 4),
                   "mode-4 dedicated objective allocation marker changed");
            expect(modes.specialObjectDamagePolicyProven,
                   "BTECH special-object damage policy evidence changed");
            expect(modes.specialObjectDamageSuppressed == (selector >= 10u && selector <= 13u),
                   "BTECH special-object damage-suppression selector set changed");
            expect(modes.specialObjectDepletionPolicyProven,
                   "BTECH special-object depletion policy evidence changed");
            const bool expectedDepletionWin = expectedPlayerModes[selector] == 3;
            const bool expectedDepletionLoss = expectedOpposingModes[selector] == 3;
            expect(modes.specialObjectDepletionSetsPlayerWinCondition == expectedDepletionWin &&
                       modes.specialObjectDepletionSetsPlayerLossCondition == expectedDepletionLoss,
                   "BTECH special-object depletion condition mapping changed");
            const int expectedDepletionResult =
                expectedDepletionWin ? 0 : (expectedDepletionLoss ? 1 : -1);
            expect(modes.specialObjectDepletionResultCode == expectedDepletionResult,
                   "BTECH special-object depletion result code changed");
        }
        expect(lockedSetup.metadata.playerMode == 3 && lockedSetup.metadata.opposingMode == 3,
               "selector 10 placement modes changed");
        expectNear(lockedSetup.placement.opposingSide.positions[0].gridX, 113.25, 0.001,
                   "selector 10 opposing slot 0 grid x");
        expectNear(lockedSetup.placement.opposingSide.positions[0].gridY, 51.875, 0.001,
                   "selector 10 opposing slot 0 grid y");
        expectNear(lockedSetup.objective.gridX, 110.25, 0.001,
                   "selector 10 diagnostic objective grid x");
        expectNear(lockedSetup.objective.gridY, 65.875, 0.001,
                   "selector 10 diagnostic objective grid y");
        expect(!lockedSetup.metadata.dedicatedObjective.valid,
               "selector 10 must keep its opposing:1 objective diagnostic separate from mode-4 objective data");
        expect(lockedSetup.metadata.missionSelectorContractProven &&
                   lockedSetup.metadata.briefingMissionIdValid &&
                   lockedSetup.metadata.briefingMissionId == 10u &&
                   lockedSetup.metadata.handoffMissionId == 11u &&
                   lockedSetup.metadata.initialPlacementSelector == 10u &&
                   lockedSetup.metadata.missionSelector == 10u &&
                   !lockedSetup.metadata.extendedSequenceRemapRequired &&
                   lockedSetup.metadata.placementSelectorRuntimeResolved &&
                   lockedSetup.metadata.missionSelectorProvenance ==
                       "mw_main_context7_minus_one_direct",
               "selector 10 mission handoff contract changed");
        expect(lockedSetup.objective.transformProven && !lockedSetup.objective.activeObjectProven &&
                   !lockedSetup.objective.missionSemanticsProven,
               "selector 10 objective evidence flags changed");
        expect(lockedSetup.objective.missionIntent ==
                   mw::battle::BattleMissionObjectiveIntent::Retrieve &&
                   lockedSetup.objective.missionIntentProven &&
                   lockedSetup.objective.missionTargetKind ==
                       "kidnap_victim" &&
                   !lockedSetup.objective.missionIntentBoundToObjective,
               "selector 10 retrieval title metadata changed");
        expect(lockedSetup.objective.damagePolicyProven && lockedSetup.objective.damageSuppressed,
               "selector 10 objective damage-suppression metadata changed");
        expect(lockedSetup.objective.depletionPolicyProven &&
                   lockedSetup.objective.depletionSetsPlayerWinCondition &&
                   lockedSetup.objective.depletionSetsPlayerLossCondition &&
                   lockedSetup.objective.depletionResultCode == 0,
               "selector 10 objective depletion outcome metadata changed");
        expect(lockedSetup.battlefieldBoundary.valid &&
                   lockedSetup.battlefieldBoundary.provenance ==
                       "btech_ds0336_ds0342_snario_exit_mask",
               "original battlefield boundary metadata missing");
        expect(lockedSetup.battlefieldBoundary.originalMinX == -0xac80 &&
                   lockedSetup.battlefieldBoundary.originalMaxX == 0xac80 &&
                   lockedSetup.battlefieldBoundary.originalMinZ == -24000 &&
                   lockedSetup.battlefieldBoundary.originalMaxZ == 24000,
               "original battlefield coordinate bounds changed");
        expectNear(lockedSetup.battlefieldBoundary.worldMinX, 0.0, 0.001,
                   "original battlefield runtime min x");
        expectNear(lockedSetup.battlefieldBoundary.worldMaxX, 86250.0, 0.001,
                   "original battlefield runtime max x");
        expectNear(lockedSetup.battlefieldBoundary.worldMinZ, 0.0, 0.001,
                   "original battlefield runtime min z");
        expectNear(lockedSetup.battlefieldBoundary.worldMaxZ, 46875.0, 0.001,
                   "original battlefield runtime max z");
        expect(lockedSetup.battlefieldBoundary.mapProjectionProven &&
                   lockedSetup.battlefieldBoundary.actorExitEncodingProven &&
                   lockedSetup.battlefieldBoundary.exitMaskSemanticsProven &&
                   lockedSetup.battlefieldBoundary.sideCompletionExitPolicyProven,
               "original battlefield boundary evidence flags changed");
        expect(lockedSetup.battlefieldBoundary.playerAllowedExitMask == 0 &&
                   lockedSetup.battlefieldBoundary.opposingAllowedExitMask == 0,
               "non-mode-1 selector must not activate SNARIO exit masks");
        expect(lockedSetup.battlefieldBoundary.playerExitOutcomeProven &&
                   lockedSetup.battlefieldBoundary.ordinaryPlayerExitResultCode == 2 &&
                   lockedSetup.battlefieldBoundary.allowedPlayerExitResultCode == 0 &&
                   !lockedSetup.battlefieldBoundary.outcomeEvaluationDeferred,
               "original player-exit outcome metadata changed");

        const mw::battle::OriginalBattlefieldSetup mode1Setup =
            mw::battle::decodeOriginalBattlefieldSetup(options.snarioPath, options.scenarioIndex, 18, 500.0);
        expect(mode1Setup.metadata.playerMode == 1 && mode1Setup.metadata.opposingMode == 1,
               "selector 18 must retain mode-1 exit handling for both sides");
        expect(mode1Setup.battlefieldBoundary.playerAllowedExitMask ==
                   mw::battle::battlefieldBoundaryEdgeMask(mw::battle::BattlefieldBoundaryEdge::West) &&
                   mode1Setup.battlefieldBoundary.opposingAllowedExitMask ==
                       mw::battle::battlefieldBoundaryEdgeMask(mw::battle::BattlefieldBoundaryEdge::West),
               "scenario 2 mode-1 west-exit masks changed");
        expect(mw::battle::battlefieldActorExitStatus(mw::battle::BattlefieldBoundaryEdge::North) == -2 &&
                   mw::battle::battlefieldActorExitStatus(mw::battle::BattlefieldBoundaryEdge::South) == -3 &&
                   mw::battle::battlefieldActorExitStatus(mw::battle::BattlefieldBoundaryEdge::West) == -4 &&
                   mw::battle::battlefieldActorExitStatus(mw::battle::BattlefieldBoundaryEdge::East) == -5,
               "BTECH actor exit-status encoding changed");

        mw::battle::BattleStartParams boundaryContractParams = params;
        boundaryContractParams.mission = mw::battle::battleMissionBriefingFromDefinition(
            *mw::battle::battleMissionDefinitionById(18));
        boundaryContractParams.setupMetadata = mode1Setup.metadata;
        boundaryContractParams.objective = mode1Setup.objective;
        boundaryContractParams.battlefieldBoundary = mode1Setup.battlefieldBoundary;
        const mw::battle::BattleSnapshot boundaryOwnedSnapshot =
            mw::battle::BattleWorld::create(boundaryContractParams).missionStartSnapshot();
        expect(boundaryOwnedSnapshot.battlefieldBoundary.valid &&
                   boundaryOwnedSnapshot.battlefieldBoundary.playerAllowedExitMask == 4 &&
                   !boundaryOwnedSnapshot.battlefieldBoundary.outcomeEvaluationDeferred,
               "mission-start snapshot must own immutable battlefield boundary metadata");

        mw::battle::BattleStartParams ordinaryExitParams = params;
        ordinaryExitParams.setupMetadata = lockedSetup.metadata;
        ordinaryExitParams.objective = lockedSetup.objective;
        ordinaryExitParams.battlefieldBoundary = lockedSetup.battlefieldBoundary;
        ordinaryExitParams.playerStartTransform =
            mw::battle::Transform{1.0, 0.0, 20000.0, -1.5707963267948966};
        mw::battle::BattleWorld ordinaryExitWorld = mw::battle::BattleWorld::create(ordinaryExitParams);
        ordinaryExitWorld.enqueueInput(
            mw::battle::BattleInputCommand{0, ordinaryExitWorld.playerEntityId(), 1.0, 0.0});
        ordinaryExitWorld.runTicks(4);
        const mw::battle::BattleSnapshot ordinaryExitSnapshot = ordinaryExitWorld.snapshot();
        expect(ordinaryExitSnapshot.missionRuntimeState == mw::battle::BattleMissionRuntimeState::Withdraw,
               "ordinary player boundary exit should terminally withdraw");
        expect(ordinaryExitSnapshot.result.valid, "ordinary player boundary exit should create a result package");
        expect(ordinaryExitSnapshot.result.rawResultCode == 2,
               "ordinary player boundary exit should preserve raw failure result code 2");
        expect(ordinaryExitSnapshot.result.terminalTickIndex == 1 &&
                   ordinaryExitSnapshot.result.terminalElapsedMs == 100,
               "ordinary player boundary exit terminal tick changed");
        expect(ordinaryExitSnapshot.result.exitEdge == mw::battle::BattlefieldBoundaryEdge::West &&
                   ordinaryExitSnapshot.result.actorExitStatus == -4 &&
                   ordinaryExitSnapshot.result.exitMask == 4 &&
                   !ordinaryExitSnapshot.result.exitAllowed,
               "ordinary player boundary exit edge/status/mask changed");
        expect(ordinaryExitWorld.battleResult().has_value(), "ordinary exit result API missing terminal result");

        mw::battle::BattleStartParams allowedExitParams = boundaryContractParams;
        allowedExitParams.playerStartTransform =
            mw::battle::Transform{1.0, 0.0, 20000.0, -1.5707963267948966};
        mw::battle::BattleWorld allowedExitWorld = mw::battle::BattleWorld::create(allowedExitParams);
        allowedExitWorld.enqueueInput(
            mw::battle::BattleInputCommand{0, allowedExitWorld.playerEntityId(), 1.0, 0.0});
        const mw::battle::BattleSnapshot allowedExitReplay =
            mw::battle::runBattleReplay(
                allowedExitParams,
                mw::battle::BattleReplay{{
                    mw::battle::BattleInputCommand{0, {}, 1.0, 0.0},
                }},
                4);
        const mw::battle::StandaloneBattleRunResult allowedExitLaunchResult =
            mw::battle::runStandaloneBattle(
                allowedExitParams,
                mw::battle::BattleReplay{{
                    mw::battle::BattleInputCommand{0, {}, 1.0, 0.0},
                }},
                mw::battle::StandaloneBattleRunOptions{4, true});
        allowedExitWorld.runTicks(4);
        const mw::battle::BattleSnapshot allowedExitSnapshot = allowedExitWorld.snapshot();
        expect(allowedExitSnapshot.missionRuntimeState == mw::battle::BattleMissionRuntimeState::Victory,
               "allowed mode-1 player boundary exit should be victory");
        expect(allowedExitSnapshot.result.valid, "allowed player boundary exit should create a result package");
        expect(allowedExitSnapshot.result.rawResultCode == 0,
               "allowed player boundary exit should preserve raw success result code 0");
        expect(allowedExitSnapshot.result.terminalTickIndex == 1 &&
                   allowedExitSnapshot.result.terminalElapsedMs == 100,
               "allowed player boundary exit terminal tick changed");
        expect(allowedExitSnapshot.result.exitEdge == mw::battle::BattlefieldBoundaryEdge::West &&
                   allowedExitSnapshot.result.actorExitStatus == -4 &&
                   allowedExitSnapshot.result.exitMask == 4 &&
                   allowedExitSnapshot.result.exitAllowed,
               "allowed player boundary exit edge/status/mask changed");
        expect(
            mw::battle::battleSnapshotFingerprint(allowedExitSnapshot) ==
                mw::battle::battleSnapshotFingerprint(allowedExitReplay),
            "allowed player boundary exit replay fingerprint changed");
        expect(allowedExitWorld.battleResult().has_value(), "allowed exit result API missing terminal result");
        expect(allowedExitLaunchResult.terminal &&
                   allowedExitLaunchResult.result.has_value() &&
                   allowedExitLaunchResult.result->rawResultCode == 0 &&
                   allowedExitLaunchResult.ticksExecuted == 1 &&
                   mw::battle::battleSnapshotFingerprint(allowedExitLaunchResult.finalSnapshot) ==
                       mw::battle::battleSnapshotFingerprint(allowedExitSnapshot),
               "standalone battle launch/result boundary changed");

        bool rejectedUnprovenBoundaryMask = false;
        try {
            mw::battle::BattleStartParams invalidBoundaryParams = boundaryContractParams;
            invalidBoundaryParams.battlefieldBoundary.exitMaskSemanticsProven = false;
            static_cast<void>(mw::battle::BattleWorld::create(invalidBoundaryParams));
        } catch (const std::runtime_error&) {
            rejectedUnprovenBoundaryMask = true;
        }
        expect(rejectedUnprovenBoundaryMask,
               "active battlefield exit masks must require proven BTECH semantics");
        const mw::battle::OriginalBattlefieldSetup playerMode4Setup =
            mw::battle::decodeOriginalBattlefieldSetup(options.snarioPath, options.scenarioIndex, 0, 500.0);
        expect(playerMode4Setup.metadata.dedicatedObjective.valid,
               "selector 0 should expose its dedicated objective transform");
        expect(playerMode4Setup.metadata.dedicatedObjective.sourceSide == 0 &&
                   playerMode4Setup.metadata.dedicatedObjective.placementMode == 4,
               "selector 0 dedicated objective source changed");
        expect(playerMode4Setup.metadata.dedicatedObjective.bankId == 15 &&
                   playerMode4Setup.metadata.dedicatedObjective.coordinateOffset == 0x0e38,
               "selector 0 dedicated objective bank changed");
        expectNear(playerMode4Setup.metadata.dedicatedObjective.gridX, 152.25, 0.001,
                   "selector 0 dedicated objective grid x");
        expectNear(playerMode4Setup.metadata.dedicatedObjective.gridY, 75.875, 0.001,
                   "selector 0 dedicated objective grid y");
        expect(playerMode4Setup.metadata.dedicatedObjective.legacyTargetIndex == 8 &&
                   playerMode4Setup.metadata.dedicatedObjective.resourceRecordIndex == 14,
               "dedicated objective runtime identity changed");
        expect(playerMode4Setup.objective.transformProven && playerMode4Setup.objective.activeObjectProven &&
                   !playerMode4Setup.objective.missionSemanticsProven,
               "selector 0 objective evidence flags changed");
        expect(playerMode4Setup.objective.damagePolicyProven && !playerMode4Setup.objective.damageSuppressed,
               "selector 0 objective damage policy changed");
        expect(playerMode4Setup.objective.depletionPolicyProven &&
                   !playerMode4Setup.objective.depletionSetsPlayerWinCondition &&
                   !playerMode4Setup.objective.depletionSetsPlayerLossCondition &&
                   playerMode4Setup.objective.depletionResultCode == -1,
               "selector 0 objective depletion policy changed");
        expect(playerMode4Setup.objective.sourceSlot == "dedicated:player" &&
                   playerMode4Setup.objective.provenance == "btech_mode4_dedicated_object",
               "selector 0 snapshot objective source changed");
        expect(playerMode4Setup.objective.missionIntent ==
                   mw::battle::BattleMissionObjectiveIntent::Protect &&
                   playerMode4Setup.objective.missionIntentProven &&
                   playerMode4Setup.objective.missionTargetKind ==
                       "garrison_structure" &&
                   !playerMode4Setup.objective.missionIntentBoundToObjective,
               "selector 0 Wiki garrison intent metadata changed");

        mw::battle::BattleStartParams phase13MotionOwnershipParams = params;
        phase13MotionOwnershipParams.mission =
            mw::battle::battleMissionBriefingFromDefinition(
                *mw::battle::battleMissionDefinitionById(0));
        phase13MotionOwnershipParams.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::Disabled;
        phase13MotionOwnershipParams.playerStartTransform =
            playerMode4Setup.playerStartTransform;
        phase13MotionOwnershipParams.setupMetadata =
            playerMode4Setup.metadata;
        phase13MotionOwnershipParams.playerAlliedLaunchStates.clear();
        phase13MotionOwnershipParams.combatantLaunchStates.clear();
        mw::battle::BattleCombatantLaunchState phase13ModeZeroOpponent;
        phase13ModeZeroOpponent.mechPresetId = "locust";
        phase13ModeZeroOpponent.startTransform =
            playerMode4Setup.placement.opposingSide.positions[0].transform;
        phase13ModeZeroOpponent.roster.team =
            mw::battle::BattleTeam::Opposing;
        phase13ModeZeroOpponent.roster.sourceSlot = "opposing:0";
        phase13MotionOwnershipParams.combatantLaunchStates.push_back(
            phase13ModeZeroOpponent);
        const auto phase13MotionOwnership =
            mw::battle::BattleWorld::create(
                phase13MotionOwnershipParams).missionStartSnapshot();
        expect(phase13MotionOwnership.combatants.size() == 2u,
               "phase13 motion ownership roster changed");
        const auto& phase13PlayerMotion =
            phase13MotionOwnership.combatants[0].originalAiMotion;
        const auto& phase13OpponentMotion =
            phase13MotionOwnership.combatants[1].originalAiMotion;
        const auto& phase13OpponentRawSource =
            playerMode4Setup.placement.opposingSide.positions[0];
        const auto phase13ExpectedOpponentPose =
            mw::battle::battleOriginalAiInitialBodyPose1a22ModeZero(
                4u,
                true,
                0,
                true,
                phase13OpponentRawSource.originalWorldX,
                phase13OpponentRawSource.originalWorldZ,
                playerMode4Setup.placement.playerSide.positions[0].originalWorldX,
                playerMode4Setup.placement.playerSide.positions[0].originalWorldZ,
                phase13OpponentRawSource.originalWorldX,
                phase13OpponentRawSource.originalWorldZ);
        const auto& phase13PlayerRawSource =
            playerMode4Setup.placement.playerSide.positions[0];
        mw::battle::BattleOriginalAiInitialBodyPoseDiagnosticInput
            phase13ExpectedPlayerInput;
        phase13ExpectedPlayerInput.liveObjectSlot = 0u;
        phase13ExpectedPlayerInput.liveObjectOccupied = true;
        phase13ExpectedPlayerInput.numericSideModeWord = 4;
        phase13ExpectedPlayerInput.anchorsAvailable = true;
        phase13ExpectedPlayerInput.slotRawX =
            phase13PlayerRawSource.originalWorldX;
        phase13ExpectedPlayerInput.slotRawZ =
            phase13PlayerRawSource.originalWorldZ;
        phase13ExpectedPlayerInput.slot0RawX =
            phase13PlayerRawSource.originalWorldX;
        phase13ExpectedPlayerInput.slot0RawZ =
            phase13PlayerRawSource.originalWorldZ;
        phase13ExpectedPlayerInput.slot4RawX =
            phase13OpponentRawSource.originalWorldX;
        phase13ExpectedPlayerInput.slot4RawZ =
            phase13OpponentRawSource.originalWorldZ;
        const auto phase13ExpectedPlayerPose =
            mw::battle::battleOriginalAiInitialBodyPose1a22(
                phase13ExpectedPlayerInput);
        expect(phase13PlayerMotion.initialized &&
                   phase13PlayerMotion.liveObjectSlot == 0u &&
                   phase13PlayerMotion.sideWord == 0 &&
                   phase13PlayerMotion.numericSideModeWord == 4 &&
                   phase13PlayerMotion.heading ==
                       phase13ExpectedPlayerPose.heading &&
                   phase13PlayerMotion.worldPositionBindingExact &&
                   phase13OpponentMotion.initialized &&
                   phase13OpponentMotion.liveObjectSlot == 4u &&
                   phase13OpponentMotion.sideWord == 1 &&
                   phase13OpponentMotion.numericSideModeWord == 0 &&
                   phase13OpponentMotion.rawX ==
                       phase13OpponentRawSource.originalWorldX &&
                   phase13OpponentMotion.rawY == 300 &&
                   phase13OpponentMotion.rawZ ==
                       phase13OpponentRawSource.originalWorldZ &&
                   phase13OpponentMotion.heading ==
                       phase13ExpectedOpponentPose.heading &&
                   phase13OpponentMotion.controlRecordBytes[0x10u] == 1u &&
                   phase13OpponentMotion.controlRecordBytes[0x12u] == 4u &&
                   phase13OpponentMotion.controlRecordBytes[0x3eu] == 1u &&
                   phase13OpponentMotion.worldPositionBindingExact &&
                   phase13OpponentMotion.acceptedPoseCommitCount == 0u &&
                   phase13OpponentMotion.lastPoseCommitTickIndex == 0u,
               "phase13 BattleWorld-owned raw motion initialization changed");
        auto phase13MotionFingerprintMutation = phase13MotionOwnership;
        ++phase13MotionFingerprintMutation.combatants[1].
            originalAiMotion.rawX;
        expect(mw::battle::battleSnapshotFingerprint(
                   phase13MotionOwnership) !=
                   mw::battle::battleSnapshotFingerprint(
                       phase13MotionFingerprintMutation),
               "phase13 fingerprint must own raw motion state");

        const mw::battle::OriginalBattlefieldSetup protectSetup =
            mw::battle::decodeOriginalBattlefieldSetup(options.snarioPath, options.scenarioIndex, 2, 500.0);
        expect(protectSetup.metadata.dedicatedObjective.valid &&
                   protectSetup.objective.activeObjectProven,
               "selector 2 should expose its dedicated active objective");
        expect(protectSetup.objective.missionIntent ==
                   mw::battle::BattleMissionObjectiveIntent::Protect &&
                   protectSetup.objective.missionIntentProven &&
                   protectSetup.objective.missionTargetKind == "water_factory" &&
                   protectSetup.objective.missionIntentProvenance ==
                       "mw_main_exact_mission_title:wiki_scenario_semantics",
               "selector 2 protect briefing metadata changed");
        expect(protectSetup.objective.role == "protect" &&
                   protectSetup.objective.missionIntentBoundToObjective &&
                   protectSetup.objective.missionSemanticsProven,
               "selector 2 protect intent must bind to the dedicated objective");

        mw::battle::BattleStartParams protectParams = params;
        protectParams.mission = mw::battle::battleMissionBriefingFromDefinition(
            *mw::battle::battleMissionDefinitionById(2));
        protectParams.setupMetadata = protectSetup.metadata;
        protectParams.objective = protectSetup.objective;
        const mw::battle::BattleSnapshot protectSnapshot =
            mw::battle::BattleWorld::create(protectParams).missionStartSnapshot();
        expect(protectSnapshot.objective.missionIntentBoundToObjective &&
                   protectSnapshot.objective.missionTargetKind == "water_factory" &&
                   mw::battle::battleObjectiveProtectedByPlayer(
                       protectSnapshot.objective),
               "mission-start snapshot must own bound protect objective metadata");

        mw::battle::BattleStartParams protectAiParams = protectParams;
        protectAiParams.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::Phase10CompatibilityFsm;
        mw::battle::BattleCombatantLaunchState protectEnemy;
        protectEnemy.mechPresetId = "shadow_hawk";
        protectEnemy.startTransform = mw::battle::Transform{
            protectSetup.objective.transform.x + 12000.0,
            0.0,
            protectSetup.objective.transform.z,
            0.25};
        protectEnemy.roster.team = mw::battle::BattleTeam::Opposing;
        protectEnemy.roster.factionHouseId = static_cast<uint8_t>(3);
        protectEnemy.roster.factionHouseName = "LIAO";
        protectEnemy.roster.provenance = "explicit_launch_roster";
        protectEnemy.roster.sourceSlot = "opposing:0";
        protectAiParams.combatantLaunchStates.push_back(protectEnemy);
        const mw::battle::BattleSnapshot protectAiSnapshot =
            mw::battle::runBattleReplay(protectAiParams, mw::battle::BattleReplay{}, 1);
        expect(protectAiSnapshot.combatants.size() == 2u,
               "protect AI snapshot should keep one player and one enemy");
        const mw::battle::CombatantSnapshot& protectAiEnemy = protectAiSnapshot.combatants[1];
        expect(protectAiEnemy.enemyAiState == mw::battle::BattleEnemyAiState::Approach,
               "protect mission enemy should immediately approach the bound objective");
        expect(protectAiEnemy.enemyAiTargetKind == mw::battle::BattleEnemyAiTargetKind::Objective,
               "protect mission enemy should target the objective kind");
        expect(!mw::battle::isValid(protectAiEnemy.enemyAiTargetEntityId),
               "objective-targeting enemy should not report a combatant target id");
        expectNear(protectAiEnemy.enemyAiTargetTransform.x, protectSetup.objective.transform.x, 0.001,
                   "protect AI target x");
        expectNear(protectAiEnemy.enemyAiTargetTransform.z, protectSetup.objective.transform.z, 0.001,
                   "protect AI target z");
        expect(protectAiEnemy.enemyAiTargetDistance > protectAiParams.enemyActivationRange,
               "protect objective targeting should not depend on player activation range");
        expect(protectAiEnemy.forwardSpeed > 0.0,
               "protect mission enemy should command forward motion toward the objective");
        const std::string protectAiTargetKind =
            mw::battle::battleEnemyAiTargetKindName(protectAiEnemy.enemyAiTargetKind);
        const std::string protectAiState =
            mw::battle::battleEnemyAiStateName(protectAiEnemy.enemyAiState);

        mw::battle::BattleStartParams protectObjectiveRuntimeParams = protectParams;
        protectObjectiveRuntimeParams.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::Phase10CompatibilityFsm;
        protectObjectiveRuntimeParams.deterministicCombatRuntimeEnabled = true;
        protectObjectiveRuntimeParams.deterministicObjectiveRuntimeEnabled = true;
        protectObjectiveRuntimeParams.weaponRange = 700.0;
        protectObjectiveRuntimeParams.weaponCooldownTicks = 3;
        protectObjectiveRuntimeParams.weaponDamagePerHit = 1;
        protectObjectiveRuntimeParams.objectiveMaxDamage = 3;
        mw::battle::BattleCombatantLaunchState objectiveAttacker;
        objectiveAttacker.mechPresetId = "shadow_hawk";
        objectiveAttacker.startTransform = mw::battle::Transform{
            protectSetup.objective.transform.x + 200.0,
            0.0,
            protectSetup.objective.transform.z,
            0.0};
        objectiveAttacker.roster.team = mw::battle::BattleTeam::Opposing;
        objectiveAttacker.roster.factionHouseId = static_cast<uint8_t>(3);
        objectiveAttacker.roster.factionHouseName = "LIAO";
        objectiveAttacker.roster.provenance = "explicit_launch_roster";
        objectiveAttacker.roster.sourceSlot = "opposing:0";
        protectObjectiveRuntimeParams.combatantLaunchStates.push_back(objectiveAttacker);
        const mw::battle::StandaloneBattleRunResult protectObjectiveResult =
            mw::battle::runStandaloneBattle(
                protectObjectiveRuntimeParams,
                mw::battle::BattleReplay{},
                mw::battle::StandaloneBattleRunOptions{12, true});
        expect(protectObjectiveResult.terminal && protectObjectiveResult.result.has_value(),
               "protect objective runtime should produce a terminal result");
        expect(protectObjectiveResult.result->state == mw::battle::BattleMissionRuntimeState::Defeat &&
                   protectObjectiveResult.result->rawResultCode == 1 &&
                   protectObjectiveResult.result->reason == "protected_objective_depleted",
               "protect objective depletion result changed");
        expect(protectObjectiveResult.ticksExecuted == 7,
               "protect objective depletion terminal tick changed");
        expect(protectObjectiveResult.finalSnapshot.objective.depleted &&
                   protectObjectiveResult.finalSnapshot.objective.damage == 3 &&
                   protectObjectiveResult.finalSnapshot.objective.maxDamage == 3,
               "protect objective runtime damage snapshot changed");
        expect(protectObjectiveResult.finalSnapshot.combatants.size() == 2u,
               "protect objective runtime should keep player and attacker combatants");
        const mw::battle::CombatantSnapshot& protectObjectiveAttacker =
            protectObjectiveResult.finalSnapshot.combatants[1];
        expect(protectObjectiveAttacker.enemyAiTargetKind == mw::battle::BattleEnemyAiTargetKind::Objective &&
                   protectObjectiveAttacker.weaponShotsFired == 3 &&
                   protectObjectiveAttacker.weaponHitsLanded == 3 &&
                   protectObjectiveResult.finalSnapshot.objective.lastDamageSourceEntityId ==
                       protectObjectiveAttacker.id,
               "protect objective attacker diagnostics changed");
        bool rejectedDiagnosticObjectiveRuntime = false;
        try {
            mw::battle::BattleStartParams invalidObjectiveRuntimeParams = ordinaryExitParams;
            invalidObjectiveRuntimeParams.deterministicCombatRuntimeEnabled = true;
            invalidObjectiveRuntimeParams.deterministicObjectiveRuntimeEnabled = true;
            static_cast<void>(mw::battle::BattleWorld::create(invalidObjectiveRuntimeParams));
        } catch (const std::runtime_error&) {
            rejectedDiagnosticObjectiveRuntime = true;
        }
        expect(rejectedDiagnosticObjectiveRuntime,
               "objective runtime must reject diagnostic or damage-suppressed objectives");
        const std::string protectObjectiveState =
            mw::battle::battleMissionRuntimeStateName(protectObjectiveResult.result->state);
        const std::string protectObjectiveDepleted =
            protectObjectiveResult.finalSnapshot.objective.depleted ? "depleted" : "active";

        bool rejectedUnboundMissionSemantics = false;
        try {
            mw::battle::BattleStartParams invalidProtectParams = protectParams;
            invalidProtectParams.objective.missionIntentBoundToObjective = false;
            static_cast<void>(mw::battle::BattleWorld::create(invalidProtectParams));
        } catch (const std::runtime_error&) {
            rejectedUnboundMissionSemantics = true;
        }
        expect(rejectedUnboundMissionSemantics,
               "proven objective mission semantics must require an explicit transform binding");

        bool rejectedMissionSetupMismatch = false;
        try {
            mw::battle::BattleStartParams mismatchedProtectParams = protectParams;
            mismatchedProtectParams.mission = mw::battle::defaultBattleMissionBriefing();
            static_cast<void>(mw::battle::BattleWorld::create(mismatchedProtectParams));
        } catch (const std::runtime_error&) {
            rejectedMissionSetupMismatch = true;
        }
        expect(rejectedMissionSetupMismatch,
               "mission briefing id must match the snapshot-owned setup selector");

        bool rejectedMissionHandoffMismatch = false;
        try {
            mw::battle::BattleStartParams mismatchedHandoffParams = protectParams;
            ++mismatchedHandoffParams.setupMetadata.handoffMissionId;
            static_cast<void>(mw::battle::BattleWorld::create(mismatchedHandoffParams));
        } catch (const std::runtime_error&) {
            rejectedMissionHandoffMismatch = true;
        }
        expect(rejectedMissionHandoffMismatch,
               "snapshot setup must reject a mission handoff id that is not briefing id plus one");

        const mw::battle::OriginalBattlefieldSetup unboundProtectSetup =
            mw::battle::decodeOriginalBattlefieldSetup(options.snarioPath, options.scenarioIndex, 7, 500.0);
        expect(!unboundProtectSetup.objective.activeObjectProven &&
                   unboundProtectSetup.objective.missionIntent ==
                       mw::battle::BattleMissionObjectiveIntent::Protect &&
                   unboundProtectSetup.objective.missionIntentProven &&
                   unboundProtectSetup.objective.missionTargetKind == "landing_facilities" &&
                   !unboundProtectSetup.objective.missionIntentBoundToObjective &&
                   !unboundProtectSetup.objective.missionSemanticsProven,
               "selector 7 protect briefing must remain unbound from its diagnostic transform");

        const mw::battle::OriginalBattlefieldSetup destroySetup =
            mw::battle::decodeOriginalBattlefieldSetup(options.snarioPath, options.scenarioIndex, 22, 500.0);
        expect(!destroySetup.objective.activeObjectProven &&
                   destroySetup.objective.missionIntent ==
                       mw::battle::BattleMissionObjectiveIntent::Destroy &&
                   destroySetup.objective.missionIntentProven &&
                   destroySetup.objective.missionTargetKind == "water_factory" &&
                   !destroySetup.objective.missionIntentBoundToObjective &&
                   !destroySetup.objective.missionSemanticsProven,
               "selector 22 destroy briefing must remain unbound from its diagnostic transform");

        const mw::battle::OriginalBattlefieldSetup disableSetup =
            mw::battle::decodeOriginalBattlefieldSetup(options.snarioPath, options.scenarioIndex, 23, 500.0);
        expect(disableSetup.objective.missionIntent ==
                   mw::battle::BattleMissionObjectiveIntent::Disable &&
                   disableSetup.objective.missionIntentProven &&
                   disableSetup.objective.missionTargetKind == "weapons_factory" &&
                   !disableSetup.objective.missionIntentBoundToObjective,
               "selector 23 disable briefing metadata changed");
        const mw::battle::OriginalBattlefieldSetup opposingMode4Setup =
            mw::battle::decodeOriginalBattlefieldSetup(options.snarioPath, options.scenarioIndex, 17, 500.0);
        expect(opposingMode4Setup.metadata.missionSelectorContractProven &&
                   opposingMode4Setup.metadata.briefingMissionId == 17u &&
                   opposingMode4Setup.metadata.handoffMissionId == 18u &&
                   opposingMode4Setup.metadata.initialPlacementSelector == 17u &&
                   opposingMode4Setup.metadata.extendedSequenceRemapRequired &&
                   !opposingMode4Setup.metadata.placementSelectorRuntimeResolved &&
                   opposingMode4Setup.metadata.missionSelectorProvenance ==
                       "btech_extended_pre_remap_diagnostic",
               "selector 17 must remain an unresolved extended-chain placement probe");
        expect(opposingMode4Setup.metadata.dedicatedObjective.valid &&
                   opposingMode4Setup.metadata.dedicatedObjective.sourceSide == 1,
               "selector 17 should expose the opposing-side dedicated objective transform");
        expect(opposingMode4Setup.metadata.dedicatedObjective.bankId == 8 &&
                   opposingMode4Setup.metadata.dedicatedObjective.coordinateOffset == 0x0e00,
               "selector 17 dedicated objective bank changed");
        expectNear(opposingMode4Setup.metadata.dedicatedObjective.gridX, 43.125, 0.001,
                   "selector 17 dedicated objective grid x");
        expectNear(opposingMode4Setup.metadata.dedicatedObjective.gridY, 66.40625, 0.001,
                   "selector 17 dedicated objective grid y");
        expect(opposingMode4Setup.objective.sourceSlot == "dedicated:opposing" &&
                   opposingMode4Setup.objective.activeObjectProven,
               "selector 17 snapshot objective source changed");
        expect(opposingMode4Setup.objective.damagePolicyProven &&
                   !opposingMode4Setup.objective.damageSuppressed,
               "selector 17 objective damage policy changed");

        mw::battle::BattleStartParams systemsParams = params;
        systemsParams.mechSystemsSnapshotEnabled = true;
        const mw::battle::BattleSnapshot systemsStart =
            mw::battle::BattleWorld::create(systemsParams).missionStartSnapshot();
        expect(systemsStart.combatants.size() == 1, "mech systems snapshot should contain one combatant");
        const mw::battle::BattleMechSystemsSnapshot& locustSystems = systemsStart.combatants[0].mechSystems;
        expect(locustSystems.valid, "whole-mech systems snapshot missing");
        expect(locustSystems.provenance == "catalog_component_runtime_projection_v0",
               "whole-mech systems provenance changed");
        expect(!locustSystems.wholeMechDestroyed, "fresh mech systems should not be destroyed");
        expect(locustSystems.mobilityOnline && locustSystems.cockpitOnline && locustSystems.weaponsOnline,
               "fresh mech systems should start online");
        expect(!locustSystems.jumpJetsOnline, "Locust whole-mech systems should not expose online jump jets");
        expect(locustSystems.systems.size() == 4u, "Locust whole-mech systems grouping changed");
        const mw::battle::BattleMechSystemSnapshot* locustMobility =
            findSystem(locustSystems, mw::battle::BattleMechSystemRole::Mobility);
        const mw::battle::BattleMechSystemSnapshot* locustWeapons =
            findSystem(locustSystems, mw::battle::BattleMechSystemRole::Weapons);
        expect(locustMobility != nullptr && locustMobility->movementCritical,
               "Locust mobility system missing movement-critical state");
        expect(containsId(locustMobility->sourceComponentIds, 0) &&
                   containsId(locustMobility->sourceComponentIds, 4),
               "Locust mobility system source components changed");
        expect(locustWeapons != nullptr && locustWeapons->combatCritical,
               "Locust weapons system missing combat-critical state");
        expect(containsId(locustWeapons->sourceComponentIds, 1) &&
                   containsId(locustWeapons->sourceComponentIds, 5),
               "Locust weapons system source components changed");

        mw::battle::BattleStartParams jumpSystemsParams = systemsParams;
        jumpSystemsParams.playerMechPresetId = "jenner";
        const mw::battle::BattleSnapshot jumpSystemsStart =
            mw::battle::BattleWorld::create(jumpSystemsParams).missionStartSnapshot();
        const mw::battle::BattleMechSystemSnapshot* jumpJetSystem =
            findSystem(jumpSystemsStart.combatants[0].mechSystems, mw::battle::BattleMechSystemRole::JumpJets);
        expect(jumpJetSystem != nullptr, "jump-capable mech should expose jump-jet system");
        expect(jumpJetSystem->status == mw::battle::BattleMechSystemStatus::Online,
               "fresh jump-jet system should be online");
        expect(jumpSystemsStart.combatants[0].mechSystems.jumpJetsOnline,
               "jump-capable mech systems aggregate should mark jump jets online");

        mw::battle::BattleWorld systemsArmWorld = mw::battle::BattleWorld::create(systemsParams);
        systemsArmWorld.destroyComponent(systemsArmWorld.playerEntityId(), 5);
        const mw::battle::BattleSnapshot systemsArmDamage = systemsArmWorld.snapshot();
        const mw::battle::BattleMechSystemSnapshot* armWeapons =
            findSystem(systemsArmDamage.combatants[0].mechSystems, mw::battle::BattleMechSystemRole::Weapons);
        expect(armWeapons != nullptr &&
                   armWeapons->status == mw::battle::BattleMechSystemStatus::Degraded,
               "destroyed arm should degrade the weapons system projection");
        expect(!systemsArmDamage.combatants[0].mechSystems.wholeMechDestroyed,
               "destroyed arm should not destroy the whole mech systems projection");

        mw::battle::BattleWorld systemsLegWorld = mw::battle::BattleWorld::create(systemsParams);
        systemsLegWorld.destroyComponent(systemsLegWorld.playerEntityId(), 4);
        const mw::battle::BattleSnapshot systemsLegDamage = systemsLegWorld.snapshot();
        const mw::battle::BattleMechSystemSnapshot* legMobility =
            findSystem(systemsLegDamage.combatants[0].mechSystems, mw::battle::BattleMechSystemRole::Mobility);
        expect(systemsLegDamage.combatants[0].mechSystems.wholeMechDestroyed,
               "destroyed leg should destroy the whole mech systems projection");
        expect(!systemsLegDamage.combatants[0].mechSystems.mobilityOnline,
               "destroyed whole mech should not keep mobility online");
        expect(legMobility != nullptr &&
                   legMobility->status == mw::battle::BattleMechSystemStatus::Destroyed,
               "destroyed leg should mark mobility system destroyed");
        const std::string armWeaponsSystemStatus =
            mw::battle::battleMechSystemStatusName(armWeapons->status);
        const std::string legMobilitySystemStatus =
            mw::battle::battleMechSystemStatusName(legMobility->status);
        const std::string jumpJetSystemStatus =
            mw::battle::battleMechSystemStatusName(jumpJetSystem->status);

        world.destroyComponent(world.playerEntityId(), 5);
        const mw::battle::BattleSnapshot armDamage = world.snapshot();
        expect(armDamage.tickIndex == 0, "component runtime snapshot should not advance tick");
        expect(armDamage.combatants.size() == 1, "arm damage snapshot should contain one combatant");
        expect(armDamage.combatants[0].activeAnimationId == "idle", "destroyed arm should keep stationary idle animation");
        expect(!armDamage.combatants[0].mechDestroyed, "destroyed arm should not destroy mech");
        expect(containsId(armDamage.combatants[0].destroyedComponentIds, 5), "destroyed arm missing from snapshot");
        expect(containsId(armDamage.combatants[0].hiddenComponentIds, 5), "destroyed arm should be hidden");
        expect(containsId(armDamage.combatants[0].disabledComponentIds, 5), "destroyed arm should be disabled");

        world.destroyComponent(world.playerEntityId(), 4);
        const mw::battle::BattleSnapshot legDamage = world.snapshot();
        expect(legDamage.combatants.size() == 1, "leg damage snapshot should contain one combatant");
        expect(legDamage.combatants[0].mechDestroyed, "destroyed leg should destroy mech");
        expect(
            legDamage.combatants[0].missionStatus == mw::battle::CombatantMissionStatus::Destroyed,
            "destroyed leg should set destroyed mission status");
        expect(legDamage.combatants[0].activeAnimationId == "death", "destroyed leg should switch to death animation");
        expect(containsId(legDamage.combatants[0].destroyedComponentIds, 4), "destroyed leg missing from snapshot");
        expect(containsId(legDamage.combatants[0].hiddenComponentIds, 4),
               "destroyed leg must remain absent during the death animation");
        world.runTicks(20u);
        const auto settledDeath = world.snapshot();
        const uint64_t settledDeathElapsed =
            settledDeath.combatants[0].walkAnimationElapsedMs;
        expect(settledDeathElapsed > 0u &&
                   settledDeath.combatants[0].activeAnimationId == "death",
               "destroyed mech must advance its death animation");
        world.runTicks(100u);
        expect(world.snapshot().combatants[0].walkAnimationElapsedMs ==
                   settledDeathElapsed,
               "destroyed mech must freeze on the final death frame");
        expect(
            mw::battle::battleSnapshotFingerprint(world.missionStartSnapshot()) == startHash,
            "mission start snapshot changed after runtime mutations");

        mw::battle::BattleWorld disabledWorld = mw::battle::BattleWorld::create(params);
        disabledWorld.setMissionStatus(disabledWorld.playerEntityId(), mw::battle::CombatantMissionStatus::Disabled);
        const mw::battle::BattleSnapshot disabledSnapshot = disabledWorld.snapshot();
        expect(disabledSnapshot.combatants.size() == 1, "disabled snapshot should contain one combatant");
        expect(
            disabledSnapshot.combatants[0].missionStatus == mw::battle::CombatantMissionStatus::Disabled,
            "disabled status missing from snapshot");
        expect(!disabledSnapshot.combatants[0].mechDestroyed, "disabled mech should not be destroyed");

        mw::battle::BattleCombatantLaunchState nextMissionLaunch =
            mw::battle::prepareNextMissionLaunchState(
                disabledSnapshot.combatants[0],
                mw::battle::Transform{12000.0, 0.0, 8000.0, 0.25});
        expect(nextMissionLaunch.minimalRepairApplied, "disabled mech should receive minimal repair for next mission");
        expect(nextMissionLaunch.fragileAfterMinimalRepair, "minimal repair should mark mech fragile for future damage");
        expect(
            nextMissionLaunch.priorMissionStatus == mw::battle::CombatantMissionStatus::Disabled,
            "next mission launch should remember prior disabled status");
        auto damagedCampaignSnapshot = disabledSnapshot.combatants[0];
        auto& carriedArmor = damagedCampaignSnapshot.detailedDamage.
            armorSections[static_cast<size_t>(
                mw::mech3d::MechArmorSectionId::RightArm)];
        carriedArmor.armorRemaining = static_cast<uint16_t>(
            carriedArmor.armorRemaining > 0u
                ? carriedArmor.armorRemaining - 1u : 0u);
        carriedArmor.battleDamage = 1;
        damagedCampaignSnapshot.detailedDamage.criticalComponents[
            static_cast<size_t>(
                mw::mech3d::MechCriticalComponentId::Engine)].condition = 2u;
        damagedCampaignSnapshot.detailedDamage.criticalComponents[
            static_cast<size_t>(
                mw::mech3d::MechCriticalComponentId::Engine)].battleDamage = 1;
        damagedCampaignSnapshot.detailedDamage.installedWeapons[0].condition = 1u;
        damagedCampaignSnapshot.detailedDamage.installedWeapons[0].battleDamage = 1;
        const auto damagedCampaignLaunch =
            mw::battle::prepareNextMissionLaunchState(
                damagedCampaignSnapshot,
                mw::battle::Transform{12000.0, 0.0, 8000.0, 0.25});
        expect(damagedCampaignLaunch.persistentMechState.armorDamage[
                       static_cast<size_t>(
                           mw::mech3d::MechArmorSectionId::RightArm)] > 0u &&
                   damagedCampaignLaunch.persistentMechState.engine == 2u &&
                   damagedCampaignLaunch.persistentMechState.
                           weaponConditions[0] == 1u &&
                   damagedCampaignLaunch.persistentMechState.provenance ==
                       "wiki_campaign_intermission:carry_damage_without_full_repair",
               "extended campaign intermission must carry armor, critical and weapon damage");

        mw::battle::BattleStartParams nextMissionParams = params;
        nextMissionParams.terrainScenarioIndex = 3;
        nextMissionParams.playerLaunchState = nextMissionLaunch;
        const mw::battle::BattleWorld nextMissionWorld = mw::battle::BattleWorld::create(nextMissionParams);
        const mw::battle::BattleSnapshot nextMissionStart = nextMissionWorld.missionStartSnapshot();
        expect(nextMissionStart.terrain.scenarioIndex == 3, "next mission scenario index mismatch");
        expect(nextMissionStart.combatants.size() == 1, "next mission start should contain one combatant");
        expect(
            nextMissionStart.combatants[0].missionStatus == mw::battle::CombatantMissionStatus::Active,
            "minimally repaired mech should start next mission active");
        expect(nextMissionStart.combatants[0].minimalRepairApplied, "next mission snapshot missing minimal repair marker");
        expect(nextMissionStart.combatants[0].fragileAfterMinimalRepair, "next mission snapshot missing fragile marker");
        expect(nextMissionStart.combatants[0].activeAnimationId == "idle", "next mission mech should start idle");

        mw::battle::BattleStartParams boundaryParams = params;
        boundaryParams.playerStartTransform = mw::battle::Transform{43500.0, 0.0, 46490.0, 0.0};
        mw::battle::BattleWorld boundaryWorld = mw::battle::BattleWorld::create(boundaryParams);
        boundaryWorld.enqueueInput(mw::battle::BattleInputCommand{0, boundaryWorld.playerEntityId(), 1.0, 0.0});
        boundaryWorld.runTicks(2);
        const mw::battle::BattleSnapshot boundarySnapshot = boundaryWorld.snapshot();
        expect(boundarySnapshot.combatants.size() == 1, "boundary snapshot should contain one combatant");
        expect(boundarySnapshot.combatants[0].boundaryContact, "boundary contact missing from snapshot");
        expectNear(
            boundarySnapshot.combatants[0].transform.z,
            boundarySnapshot.terrain.boundsMaxZ,
            0.001,
            "boundary clamped z");
        expectNear(boundarySnapshot.combatants[0].transform.x, 43500.0, 0.001, "boundary unclamped x");

        mw::battle::BattleReplay reverseReplay;
        reverseReplay.commands = {
            mw::battle::BattleInputCommand{0, {}, -1.0, 0.0},
        };
        const mw::battle::BattleSnapshot reverseA = mw::battle::runBattleReplay(params, reverseReplay, 4);
        const mw::battle::BattleSnapshot reverseB = mw::battle::runBattleReplay(params, reverseReplay, 4);
        expect(
            mw::battle::battleSnapshotFingerprint(reverseA) == mw::battle::battleSnapshotFingerprint(reverseB),
            "reverse replay snapshot fingerprint mismatch");
        expect(reverseA.combatants.size() == 1, "reverse snapshot should contain one combatant");
        const mw::battle::CombatantSnapshot& reversePlayer = reverseA.combatants[0];
        expectNear(reversePlayer.targetForwardSpeed, -100.0, 0.001, "reverse target speed");
        expectNear(reversePlayer.forwardSpeed, -100.0, 0.001, "reverse current speed");
        expectNear(reversePlayer.transform.x, 43500.0, 0.001, "reverse x");
        expectNear(reversePlayer.transform.z, 23465.0, 0.001, "reverse z");
        expectNear(reversePlayer.transform.headingRadians, 0.0, 0.0001, "reverse heading");
        expect(reversePlayer.walkAnimationElapsedMs == 350, "reverse walk animation elapsed time mismatch");
        expect(reversePlayer.activeAnimationId == "walk", "moving reverse mech should use walk animation");
        expect(!reversePlayer.boundaryContact, "reverse smoke should not touch battlefield bounds");
        expect(reverseA.camera.valid, "reverse camera snapshot should be valid");
        expect(reverseA.camera.attachedEntityId == reversePlayer.id, "reverse camera should follow player entity");
        expectNear(reverseA.camera.transform.z, 23515.0, 0.001, "reverse camera z");

        mw::battle::BattleReplay turnReplay;
        turnReplay.commands = {
            mw::battle::BattleInputCommand{0, {}, 0.0, 2.0},
        };
        const mw::battle::BattleSnapshot turnA = mw::battle::runBattleReplay(params, turnReplay, 4);
        const mw::battle::BattleSnapshot turnB = mw::battle::runBattleReplay(params, turnReplay, 4);
        expect(
            mw::battle::battleSnapshotFingerprint(turnA) == mw::battle::battleSnapshotFingerprint(turnB),
            "turn replay snapshot fingerprint mismatch");
        expect(turnA.combatants.size() == 1, "turn snapshot should contain one combatant");
        const mw::battle::CombatantSnapshot& turnPlayer = turnA.combatants[0];
        expectNear(turnPlayer.turn, 1.0, 0.001, "turn input clamp");
        expectNear(turnPlayer.targetForwardSpeed, 0.0, 0.001, "turn target speed");
        expectNear(turnPlayer.forwardSpeed, 0.0, 0.001, "turn current speed");
        expectNear(turnPlayer.transform.x, 43500.0, 0.001, "turn x");
        expectNear(turnPlayer.transform.z, 23500.0, 0.001, "turn z");
        expectNear(turnPlayer.transform.headingRadians, 0.3141593, 0.0001, "turn heading");
        expect(turnPlayer.walkAnimationElapsedMs == 100, "stationary turn should advance walk animation at minimum rate");
        expect(turnPlayer.activeAnimationId == "walk", "stationary turn should use walk animation");
        expect(!turnPlayer.boundaryContact, "turn smoke should not touch battlefield bounds");
        expect(turnA.camera.valid, "turn camera snapshot should be valid");
        expect(turnA.camera.attachedEntityId == turnPlayer.id, "turn camera should follow player entity");
        expectNear(turnA.camera.transform.x, 43515.4508, 0.001, "turn camera x");
        expectNear(turnA.camera.transform.z, 23547.5528, 0.001, "turn camera z");
        expectNear(turnA.camera.transform.headingRadians, turnPlayer.transform.headingRadians, 0.0001, "turn camera heading");

        mw::battle::BattleWorld torsoWorld = mw::battle::BattleWorld::create(params);
        torsoWorld.enqueueInput(mw::battle::BattleInputCommand{0, {}, 0.0, 0.0, 3});
        torsoWorld.enqueueInput(mw::battle::BattleInputCommand{1, {}, 0.0, 0.0, 10});
        torsoWorld.runTicks(2);
        const mw::battle::BattleSnapshot torsoRightSnapshot = torsoWorld.snapshot();
        expect(torsoRightSnapshot.combatants.size() == 1, "torso right snapshot should contain one combatant");
        const mw::battle::CombatantSnapshot& torsoRightPlayer = torsoRightSnapshot.combatants[0];
        expect(torsoRightPlayer.torsoYawStep == 7, "torso yaw should clamp to the right limit");
        expectNear(torsoRightPlayer.torsoYawRadians, 0.7330383, 0.0001, "torso right yaw radians");
        expectNear(torsoRightPlayer.transform.headingRadians, 0.0, 0.0001, "torso twist should not rotate body heading");
        expectNear(torsoRightSnapshot.camera.transform.headingRadians, 0.7330383, 0.0001, "torso right camera heading");
        expectNear(torsoRightSnapshot.camera.transform.x, 43533.4565, 0.001, "torso right camera x");
        expectNear(torsoRightSnapshot.camera.transform.z, 23537.1572, 0.001, "torso right camera z");

        torsoWorld.enqueueInput(mw::battle::BattleInputCommand{2, {}, 0.0, 0.0, -20});
        torsoWorld.runTicks(1);
        const mw::battle::BattleSnapshot torsoLeftSnapshot = torsoWorld.snapshot();
        expect(torsoLeftSnapshot.combatants.size() == 1, "torso left snapshot should contain one combatant");
        const mw::battle::CombatantSnapshot& torsoLeftPlayer = torsoLeftSnapshot.combatants[0];
        expect(torsoLeftPlayer.torsoYawStep == -7, "torso yaw should clamp to the left limit");
        expectNear(torsoLeftPlayer.torsoYawRadians, -0.7330383, 0.0001, "torso left yaw radians");
        expectNear(torsoLeftPlayer.transform.headingRadians, 0.0, 0.0001, "torso twist should keep body heading");
        expectNear(torsoLeftSnapshot.camera.transform.headingRadians, -0.7330383, 0.0001, "torso left camera heading");
        expectNear(torsoLeftSnapshot.camera.transform.x, 43466.5435, 0.001, "torso left camera x");
        expectNear(torsoLeftSnapshot.camera.transform.z, 23537.1572, 0.001, "torso left camera z");

        mw::battle::BattleWorld aimWorld = mw::battle::BattleWorld::create(params);
        aimWorld.enqueueInput(mw::battle::BattleInputCommand{0, {}, 0.0, 0.0, 0, 3});
        aimWorld.enqueueInput(mw::battle::BattleInputCommand{1, {}, 0.0, 0.0, 0, 10});
        aimWorld.runTicks(2);
        const mw::battle::BattleSnapshot aimUpSnapshot = aimWorld.snapshot();
        expect(aimUpSnapshot.combatants.size() == 1, "aim up snapshot should contain one combatant");
        expect(aimUpSnapshot.combatants[0].aimPitchStep == 9, "aim pitch should clamp to the upper limit");

        aimWorld.enqueueInput(mw::battle::BattleInputCommand{2, {}, 0.0, 0.0, 0, -20});
        aimWorld.runTicks(1);
        const mw::battle::BattleSnapshot aimDownSnapshot = aimWorld.snapshot();
        expect(aimDownSnapshot.combatants.size() == 1, "aim down snapshot should contain one combatant");
        expect(aimDownSnapshot.combatants[0].aimPitchStep == -9, "aim pitch should clamp to the lower limit");

        mw::battle::BattleWorld nonJumpWorld = mw::battle::BattleWorld::create(params);
        nonJumpWorld.enqueueInput(mw::battle::BattleInputCommand{0, {}, 0.0, 0.0, 0, 0, true, true, true});
        nonJumpWorld.tick();
        const mw::battle::BattleSnapshot nonJumpSnapshot = nonJumpWorld.snapshot();
        const mw::battle::CombatantSnapshot& nonJumpPlayer = nonJumpSnapshot.combatants[0];
        expect(!nonJumpPlayer.airborne, "Locust jump input must not make it airborne");
        expect(!nonJumpPlayer.jumpJetsEnabled, "Locust jump input must not enable jump jets");
        expectNear(nonJumpPlayer.transform.y, 0.0, 0.001, "Locust ground height after jump input");

        mw::battle::BattleStartParams jumpParams = params;
        jumpParams.playerMechPresetId = "jenner";
        mw::battle::BattleWorld jumpWorld = mw::battle::BattleWorld::create(jumpParams);
        for (uint64_t tick = 0; tick < 6; ++tick) {
            mw::battle::BattleInputCommand command;
            command.tickIndex = tick;
            command.jumpJetToggle = tick == 0;
            command.jumpForwardThrust = tick <= 3;
            command.jumpVerticalThrust = tick > 3;
            jumpWorld.enqueueInput(command);
            jumpWorld.tick();
        }
        const mw::battle::BattleSnapshot jumpSnapshot = jumpWorld.snapshot();
        expect(jumpSnapshot.combatants.size() == 1, "jump snapshot should contain one combatant");
        const mw::battle::CombatantSnapshot& jumpPlayer = jumpSnapshot.combatants[0];
        expect(jumpPlayer.originalMaxSpeedKph == 118, "Jenner original maximum speed changed");
        expectNear(jumpPlayer.maxForwardSpeed, params.maxForwardSpeed * 118.0 / 129.0, 0.001,
                   "Jenner scaled world maximum speed");
        expect(jumpPlayer.jumpCapable, "Jenner should have jump capability");
        expect(jumpPlayer.jumpCapacityMeters == 150, "Jenner jump capacity changed");
        expect(jumpPlayer.jumpJetCount == 3, "Jenner jump-jet count changed");
        expect(jumpPlayer.airborne, "jumping player should be airborne");
        expect(jumpPlayer.jumpJetsEnabled, "jump jets should remain enabled while fuel is available");
        expect(jumpPlayer.jumpFuel < jumpPlayer.jumpMaxFuel, "jump fuel should be consumed");
        expect(jumpPlayer.transform.y > 0.0, "jump should raise the player transform");
        expect(jumpSnapshot.camera.transform.y > jumpParams.cockpitCameraHeight, "jump camera should follow player height");
        expect(jumpPlayer.forwardSpeed > 0.0, "jump forward thrust should move the player forward");

        mw::battle::BattleWorld landingWorld = mw::battle::BattleWorld::create(jumpParams);
        bool sawAirborne = false;
        std::optional<mw::battle::CombatantSnapshot> landingPlayer;
        for (uint64_t tick = 0; tick < 180; ++tick) {
            mw::battle::BattleInputCommand command;
            command.tickIndex = tick;
            command.jumpJetToggle = tick == 0;
            command.jumpForwardThrust = tick < 12;
            landingWorld.enqueueInput(command);
            landingWorld.tick();
            const mw::battle::BattleSnapshot snapshot = landingWorld.snapshot();
            expect(snapshot.combatants.size() == 1, "landing snapshot should contain one combatant");
            const mw::battle::CombatantSnapshot& candidate = snapshot.combatants[0];
            if (candidate.airborne) {
                sawAirborne = true;
            } else if (sawAirborne) {
                landingPlayer = candidate;
                break;
            }
        }
        expect(landingPlayer.has_value(), "jump test should return to the ground");
        expect(landingPlayer->forwardSpeed > 0.0, "landing should preserve horizontal momentum for the landing tick");
        expect(landingPlayer->landingImpactSpeed > 0.0, "landing should record impact speed");
        expect(landingPlayer->hardLanding, "unsoftened jump should mark a hard landing");
        expect(landingPlayer->knockdownCandidate, "hard landing should prepare knockdown candidate state");

        mw::battle::BattleReplay radarReplay;
        for (uint64_t tick = 0; tick < 7; ++tick) {
            mw::battle::BattleInputCommand command;
            command.tickIndex = tick;
            command.toggleCockpitRadar = tick == 0 || tick == 5;
            command.cycleCockpitRadarRange =
                tick == 1 || tick == 2 || tick == 3 || tick == 4 || tick == 6;
            radarReplay.commands.push_back(command);
        }
        mw::battle::BattleWorld radarWorld = mw::battle::BattleWorld::create(params);
        const mw::battle::BattleSnapshot radarInitial = radarWorld.snapshot();
        expect(!radarInitial.cockpitRadar.active,
               "cockpit radar should begin in terrain-map mode");
        expect(radarInitial.cockpitRadar.rangeIndex == 0u &&
                   radarInitial.cockpitRadar.rangeMeters == 4000u &&
                   radarInitial.cockpitRadar.commandSequence == 0u,
               "cockpit radar should begin at the original 4000 M range");
        const uint64_t radarInitialHash =
            mw::battle::battleSnapshotFingerprint(radarInitial);
        expect(radarInitialHash ==
                   mw::battle::battleSnapshotFingerprint(radarWorld.snapshot()),
               "idle cockpit radar state changed the snapshot fingerprint");

        const std::array<uint16_t, 7> expectedRadarRanges{{
            4000u, 2000u, 1000u, 500u, 4000u, 4000u, 4000u,
        }};
        const std::array<bool, 7> expectedRadarActive{{
            true, true, true, true, true, false, false,
        }};
        for (uint64_t tick = 0; tick < radarReplay.commands.size(); ++tick) {
            radarWorld.enqueueInput(radarReplay.commands[static_cast<size_t>(tick)]);
            radarWorld.tick();
            const mw::battle::BattleSnapshot radarTick = radarWorld.snapshot();
            expect(radarTick.cockpitRadar.active ==
                       expectedRadarActive[static_cast<size_t>(tick)],
                   "cockpit radar toggle escaped fixed-tick ownership");
            expect(radarTick.cockpitRadar.rangeMeters ==
                       expectedRadarRanges[static_cast<size_t>(tick)],
                   "cockpit radar range cycle changed");
        }
        const mw::battle::BattleSnapshot radarFinal = radarWorld.snapshot();
        expect(radarFinal.cockpitRadar.commandSequence == 6u &&
                   radarFinal.cockpitRadar.lastCommandTickIndex == 5u,
               "range input while radar is off should fail closed");
        const mw::battle::BattleSnapshot radarReplayA =
            mw::battle::runBattleReplay(params, radarReplay, 7u);
        const mw::battle::BattleSnapshot radarReplayB =
            mw::battle::runBattleReplay(params, radarReplay, 7u);
        expect(mw::battle::battleSnapshotFingerprint(radarReplayA) ==
                   mw::battle::battleSnapshotFingerprint(radarReplayB) &&
                   mw::battle::battleSnapshotFingerprint(radarReplayA) ==
                       mw::battle::battleSnapshotFingerprint(radarFinal),
               "cockpit radar replay fingerprint mismatch");

        mw::battle::BattleWorld radarCadenceWorld =
            mw::battle::BattleWorld::create(params);
        for (uint64_t tick = 0; tick < radarReplay.commands.size(); ++tick) {
            radarCadenceWorld.enqueueInput(
                radarReplay.commands[static_cast<size_t>(tick)]);
            for (int frame = 0; frame < 5; ++frame) {
                (void)radarCadenceWorld.snapshot();
            }
            radarCadenceWorld.tick();
        }
        expect(mw::battle::battleSnapshotFingerprint(
                   radarCadenceWorld.snapshot()) ==
                   mw::battle::battleSnapshotFingerprint(radarFinal),
               "render snapshot cadence changed cockpit radar commands");

        mw::battle::BattleStartParams scanParams = params;
        scanParams.objective = {};
        scanParams.playerStartTransform = {43500.0, 0.0, 23500.0, 0.0};
        scanParams.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::Disabled;
        scanParams.deterministicCombatRuntimeEnabled = false;
        scanParams.deterministicCollisionRuntimeEnabled = false;
        scanParams.mechSystemsSnapshotEnabled = true;

        mw::battle::BattleCombatantLaunchState scanAlly;
        scanAlly.mechPresetId = "jenner";
        scanAlly.startTransform = {43600.0, 0.0, 23500.0, 0.0};
        scanAlly.roster.team = mw::battle::BattleTeam::Player;
        scanAlly.roster.sourceSlot = "player:1";
        scanAlly.roster.provenance = "phase12_target_scan_smoke";
        scanParams.playerAlliedLaunchStates = {scanAlly};

        mw::battle::BattleCombatantLaunchState scanEnemy;
        scanEnemy.mechPresetId = "shadow_hawk";
        scanEnemy.startTransform = {43500.0, 0.0, 23700.0, 0.0};
        scanEnemy.roster.team = mw::battle::BattleTeam::Opposing;
        scanEnemy.roster.sourceSlot = "opposing:scan_near";
        scanEnemy.roster.provenance = "phase12_target_scan_smoke";
        mw::battle::BattleCombatantLaunchState scanFarEnemy = scanEnemy;
        scanFarEnemy.mechPresetId = "warhammer";
        scanFarEnemy.startTransform = {64500.0, 0.0, 23500.0, 0.0};
        scanFarEnemy.roster.sourceSlot = "opposing:scan_far";
        scanParams.combatantLaunchStates = {scanEnemy, scanFarEnemy};

        mw::battle::BattleWorld scanWorld =
            mw::battle::BattleWorld::create(scanParams);
        const mw::battle::BattleSnapshot scanStart = scanWorld.snapshot();
        expect(scanStart.targetScan.selectedTargetKind ==
                       mw::battle::BattleTargetScanTargetKind::None &&
                   !mw::battle::isValid(
                   scanStart.targetScan.selectedTargetEntityId) &&
                   scanStart.targetScan.rangeMeters == 4000u &&
                   scanStart.targetScan.commandSequence == 0u,
               "target scanner should start empty at 4000 M");
        std::vector<mw::battle::EntityId> scanCandidates;
        mw::battle::EntityId farScanEntity{};
        for (const mw::battle::CombatantSnapshot& combatant :
             scanStart.combatants) {
            if (combatant.roster.sourceSlot == "opposing:scan_far") {
                farScanEntity = combatant.id;
            }
            if (combatant.playerControlled) {
                continue;
            }
            const double distanceWorld = std::hypot(
                combatant.transform.x - scanParams.playerStartTransform.x,
                combatant.transform.z - scanParams.playerStartTransform.z);
            if (distanceWorld <=
                mw::battle::battleTargetScanRangeMeters() *
                    mw::battle::battleTargetScanWorldUnitsPerMeter()) {
                scanCandidates.push_back(combatant.id);
            }
        }
        std::sort(
            scanCandidates.begin(),
            scanCandidates.end(),
            [](mw::battle::EntityId left, mw::battle::EntityId right) {
                return left.value < right.value;
            });
        expect(scanCandidates.size() == 2u &&
                   mw::battle::isValid(farScanEntity),
               "target scanner candidate/range fixture changed");

        mw::battle::BattleReplay scanReplay;
        for (uint64_t tick = 0; tick < 3; ++tick) {
            mw::battle::BattleInputCommand command;
            command.tickIndex = tick;
            command.cycleTargetScan = true;
            scanReplay.commands.push_back(command);
            scanWorld.enqueueInput(command);
            scanWorld.tick();
            const mw::battle::BattleSnapshot selected = scanWorld.snapshot();
            const mw::battle::EntityId expected =
                scanCandidates[static_cast<size_t>(tick % 2u)];
            expect(selected.targetScan.selectedTargetKind ==
                           mw::battle::BattleTargetScanTargetKind::Combatant &&
                       selected.targetScan.selectedTargetEntityId == expected &&
                       selected.targetScan.selectedTargetEntityId != farScanEntity &&
                       selected.targetScan.commandSequence == tick + 1u &&
                       selected.targetScan.lastCommandTickIndex == tick,
                   "Enter target cycle escaped deterministic entity order/range");
        }
        const mw::battle::BattleSnapshot scanFinal = scanWorld.snapshot();
        const mw::battle::BattleSnapshot scanReplayA =
            mw::battle::runBattleReplay(scanParams, scanReplay, 3u);
        const mw::battle::BattleSnapshot scanReplayB =
            mw::battle::runBattleReplay(scanParams, scanReplay, 3u);
        expect(mw::battle::battleSnapshotFingerprint(scanReplayA) ==
                   mw::battle::battleSnapshotFingerprint(scanReplayB) &&
                   mw::battle::battleSnapshotFingerprint(scanReplayA) ==
                       mw::battle::battleSnapshotFingerprint(scanFinal),
               "target scan replay fingerprint mismatch");

        mw::battle::BattleWorld scanCadenceWorld =
            mw::battle::BattleWorld::create(scanParams);
        for (const mw::battle::BattleInputCommand& command :
             scanReplay.commands) {
            scanCadenceWorld.enqueueInput(command);
            for (int frame = 0; frame < 7; ++frame) {
                (void)scanCadenceWorld.snapshot();
            }
            scanCadenceWorld.tick();
        }
        expect(mw::battle::battleSnapshotFingerprint(
                   scanCadenceWorld.snapshot()) ==
                   mw::battle::battleSnapshotFingerprint(scanFinal),
               "render snapshot cadence changed target scan commands");

        mw::battle::BattleWorld scanInvalidationWorld =
            mw::battle::BattleWorld::create(scanParams);
        mw::battle::BattleInputCommand acquireTarget;
        acquireTarget.tickIndex = 0;
        acquireTarget.cycleTargetScan = true;
        scanInvalidationWorld.enqueueInput(acquireTarget);
        scanInvalidationWorld.tick();
        const mw::battle::EntityId invalidatedTarget =
            scanInvalidationWorld.snapshot().targetScan.selectedTargetEntityId;
        scanInvalidationWorld.setMissionStatus(
            invalidatedTarget,
            mw::battle::CombatantMissionStatus::Disabled);
        scanInvalidationWorld.tick();
        expect(!mw::battle::isValid(
                   scanInvalidationWorld.snapshot()
                       .targetScan.selectedTargetEntityId) &&
                   scanInvalidationWorld.snapshot()
                           .targetScan.selectedTargetKind ==
                       mw::battle::BattleTargetScanTargetKind::None,
               "disabled scanned target should clear fail-closed");

        mw::battle::BattleStartParams objectiveScanParams = scanParams;
        objectiveScanParams.objective = params.objective;
        objectiveScanParams.objective.valid = true;
        objectiveScanParams.objective.staticModel =
            mw::battle::originalBattleObjectiveStaticModel();
        objectiveScanParams.objective.transform =
            {43700.0, 0.0, 23500.0, 0.0};
        objectiveScanParams.objective.depleted = false;
        objectiveScanParams.objective.damage = 0;
        mw::battle::BattleWorld objectiveScanWorld =
            mw::battle::BattleWorld::create(objectiveScanParams);
        for (uint64_t tick = 0; tick < 3u; ++tick) {
            mw::battle::BattleInputCommand command;
            command.tickIndex = tick;
            command.cycleTargetScan = true;
            objectiveScanWorld.enqueueInput(command);
            objectiveScanWorld.tick();
        }
        const mw::battle::BattleSnapshot selectedObjective =
            objectiveScanWorld.snapshot();
        expect(selectedObjective.targetScan.selectedTargetKind ==
                       mw::battle::BattleTargetScanTargetKind::Objective &&
                   !mw::battle::isValid(
                       selectedObjective.targetScan.selectedTargetEntityId) &&
                   std::abs(
                       selectedObjective.targetScan.selectedTargetDistanceMeters -
                       40.0) < 0.0001,
               "Enter target cycle did not acquire the in-range objective after combatants");
        mw::battle::BattleInputCommand wrapObjectiveScan;
        wrapObjectiveScan.tickIndex = 3u;
        wrapObjectiveScan.cycleTargetScan = true;
        objectiveScanWorld.enqueueInput(wrapObjectiveScan);
        objectiveScanWorld.tick();
        expect(objectiveScanWorld.snapshot().targetScan.selectedTargetKind ==
                       mw::battle::BattleTargetScanTargetKind::Combatant &&
                   objectiveScanWorld.snapshot().targetScan.selectedTargetEntityId ==
                       scanCandidates.front(),
               "target scan did not wrap from objective to first combatant");

        mw::battle::BattleReplay replay;
        replay.commands = {
            mw::battle::BattleInputCommand{0, {}, 1.0, 0.0},
            mw::battle::BattleInputCommand{2, {}, 1.0, 0.0, 7},
            mw::battle::BattleInputCommand{4, {}, 1.0, 0.5},
            mw::battle::BattleInputCommand{8, {}, 0.0, 0.0},
        };

        mw::battle::BattleWorld steppedWorld = mw::battle::BattleWorld::create(params);
        const mw::battle::BattleSnapshot copiedBeforeTicks = steppedWorld.snapshot();
        const uint64_t copiedBeforeTicksHash = mw::battle::battleSnapshotFingerprint(copiedBeforeTicks);
        for (uint64_t tick = 0; tick < options.tickCount; ++tick) {
            for (mw::battle::BattleInputCommand command : replay.commands) {
                if (command.tickIndex == tick) {
                    command.entityId = steppedWorld.playerEntityId();
                    steppedWorld.enqueueInput(command);
                }
            }
            steppedWorld.tick();
        }
        expect(
            mw::battle::battleSnapshotFingerprint(copiedBeforeTicks) == copiedBeforeTicksHash,
            "copied snapshot changed after world ticks");
        const mw::battle::BattleSnapshot steppedFinal = steppedWorld.snapshot();
        const uint64_t steppedHash = mw::battle::battleSnapshotFingerprint(steppedFinal);

        const mw::battle::BattleSnapshot finalA =
            mw::battle::runBattleReplay(params, replay, options.tickCount);
        const mw::battle::BattleSnapshot finalB =
            mw::battle::runBattleReplay(params, replay, options.tickCount);
        const uint64_t hashA = mw::battle::battleSnapshotFingerprint(finalA);
        const uint64_t hashB = mw::battle::battleSnapshotFingerprint(finalB);
        expect(hashA == hashB, "battle replay snapshot fingerprint mismatch");
        expect(hashA == steppedHash, "manual fixed-tick replay did not match replay helper snapshot");
        expect(finalA.tickIndex == options.tickCount, "final tick mismatch");
        expect(finalA.elapsedMs == options.tickCount * 100ull, "final elapsed time mismatch");
        expect(finalA.combatants.size() == 1, "final snapshot should contain one combatant");

        const mw::battle::CombatantSnapshot& player = finalA.combatants[0];
        expect(player.id.value == 1, "first player entity id should be stable");
        expect(player.walkAnimationElapsedMs == 0, "stopped mech should return to idle walk pose");
        expectNear(player.targetForwardSpeed, 0.0, 0.001, "player target speed");
        expectNear(player.forwardSpeed, 0.0, 0.001, "player current speed");
        expect(player.torsoYawStep == 7, "main replay should twist player torso to the right limit");
        expectNear(player.torsoYawRadians, 0.7330383, 0.0001, "main replay torso yaw");
        expect(player.activeAnimationId == "idle", "stopped mech should switch back to idle animation");
        expect(!player.mechDestroyed, "player mech should not be destroyed in smoke replay");
        expectNear(player.transform.x, 43514.4853, 0.001, "player x");
        expectNear(player.transform.y, 0.0, 0.001, "player y");
        expectNear(player.transform.z, 23679.0532, 0.001, "player z");
        expectNear(player.transform.headingRadians, 0.1570796, 0.0001, "player heading");
        expect(finalA.camera.valid, "final camera snapshot should be valid");
        expect(finalA.camera.attachedEntityId == player.id, "camera should follow player entity");
        expectNear(finalA.camera.transform.y, 180.0, 0.001, "camera y");
        expectNear(finalA.camera.transform.headingRadians, 0.8901179, 0.0001, "camera heading with torso yaw");
        expectNear(finalA.camera.localForwardOffset, 50.0, 0.001, "camera local forward offset");
        expectNear(finalA.camera.localHeight, 180.0, 0.001, "camera local height");
        expect(finalA.objective.valid, "final snapshot should keep objective state");
        expectNear(finalA.objective.transform.x, start.objective.transform.x, 0.001, "final objective x");
        expectNear(finalA.objective.transform.z, start.objective.transform.z, 0.001, "final objective z");
        expect(finalA.objective.staticModel.resourceName == start.objective.staticModel.resourceName,
               "final objective model resource changed");
        expect(finalA.objective.staticModel.recordIndex == start.objective.staticModel.recordIndex,
               "final objective model record changed");
        expect(finalA.contract.valid, "final snapshot should keep contract metadata");
        expect(finalA.contract.targetPlanetName == start.contract.targetPlanetName, "final contract target planet changed");
        expect(
            joinOppositionRequests(finalA.contract.oppositionSpawnPlan) ==
                joinOppositionRequests(start.contract.oppositionSpawnPlan),
            "final snapshot changed opposition spawn diagnostic");

        // FUN_1000_8307: the component-relative heading moves toward the
        // desired fixed heading by at most 0x038e, then commits only when the
        // candidate magnitude is strictly below 0x18e2.  This pure diagnostic
        // deliberately does not apply the still-open 87c4 movement integrator.
        const auto phase13HeadingPositive =
            mw::battle::battleOriginalAiComponentHeadingCommand(
                0, 0, static_cast<int16_t>(0x2000));
        expect(
            phase13HeadingPositive.exact &&
                phase13HeadingPositive.signedCombinedError == -0x2000 &&
                phase13HeadingPositive.limitedCorrection == 0x038e &&
                phase13HeadingPositive.componentRelativeHeadingCandidate ==
                    0x038e &&
                phase13HeadingPositive.strictComponentLimitPassed &&
                phase13HeadingPositive.componentRelativeHeadingAfter ==
                    0x038e,
            "BTECH 8307 positive capped component-heading correction changed");
        const auto phase13HeadingNegative =
            mw::battle::battleOriginalAiComponentHeadingCommand(
                0, 0, static_cast<int16_t>(-0x2000));
        expect(
            phase13HeadingNegative.signedCombinedError == 0x2000 &&
                phase13HeadingNegative.limitedCorrection == -0x038e &&
                phase13HeadingNegative.componentRelativeHeadingAfter ==
                    -0x038e,
            "BTECH 8307 negative capped component-heading correction changed");
        const auto phase13HeadingSmall =
            mw::battle::battleOriginalAiComponentHeadingCommand(
                0, 0, static_cast<int16_t>(0x0100));
        expect(
            phase13HeadingSmall.limitedCorrection == 0x0100 &&
                phase13HeadingSmall.componentRelativeHeadingAfter == 0x0100,
            "BTECH 8307 sub-cap correction must preserve the exact delta");
        const auto phase13HeadingLimitAccepted =
            mw::battle::battleOriginalAiComponentHeadingCommand(
                0, static_cast<int16_t>(0x1800),
                static_cast<int16_t>(0x18e1));
        const auto phase13HeadingLimitRejected =
            mw::battle::battleOriginalAiComponentHeadingCommand(
                0, static_cast<int16_t>(0x1800),
                static_cast<int16_t>(0x18e2));
        expect(
            phase13HeadingLimitAccepted.strictComponentLimitPassed &&
                phase13HeadingLimitAccepted.componentRelativeHeadingAfter ==
                    0x18e1 &&
                !phase13HeadingLimitRejected.strictComponentLimitPassed &&
                phase13HeadingLimitRejected.componentRelativeHeadingCandidate ==
                    0x18e2 &&
                phase13HeadingLimitRejected.componentRelativeHeadingAfter ==
                    0x1800,
            "BTECH 8307 strict 0x18e2 component-heading boundary changed");
        const auto phase13HeadingHalfTurn =
            mw::battle::battleOriginalAiComponentHeadingCommand(
                static_cast<int16_t>(-0x8000), 0, 0);
        expect(
            phase13HeadingHalfTurn.signedCombinedError ==
                    static_cast<int16_t>(-0x8000) &&
                phase13HeadingHalfTurn.limitedCorrection == 0x038e,
            "BTECH 8307 signed half-turn special case changed");
        const auto phase13BodyTurnPositive =
            mw::battle::battleOriginalAiBodyTurnCommand(
                static_cast<int16_t>(0x2000), 0);
        const auto phase13BodyTurnNegative =
            mw::battle::battleOriginalAiBodyTurnCommand(
                static_cast<int16_t>(-0x2000), 0);
        const auto phase13BodyTurnSmall =
            mw::battle::battleOriginalAiBodyTurnCommand(
                static_cast<int16_t>(0x0100), 0);
        const auto phase13BodyTurnHalf =
            mw::battle::battleOriginalAiBodyTurnCommand(
                static_cast<int16_t>(-0x8000), 0);
        expect(
            phase13BodyTurnPositive.exact &&
                phase13BodyTurnPositive.turnCommand == 0x0222 &&
                phase13BodyTurnNegative.turnCommand == -0x0222 &&
                phase13BodyTurnSmall.turnCommand == 0x0100 &&
                phase13BodyTurnHalf.wrappedDelta == -0x7fff &&
                phase13BodyTurnHalf.turnCommand == -0x0222,
            "BTECH c86c/84de body-turn command boundary changed");

        // FUN_1000_84de: raw desired speed uses the wrapped 16-bit product
        // (definition+6 - heat penalty) * min(live+6c, live+6e), signed
        // truncate-toward-zero division by four, and a positive-only +5 cap.
        const auto phase13RawSpeedForward =
            mw::battle::battleOriginalAiRawSpeedCommand(72, 0, 4, 4, 0, 0);
        expect(
            phase13RawSpeedForward.exact &&
                phase13RawSpeedForward.limitingLegActuatorWord == 4 &&
                phase13RawSpeedForward.wrappedProduct == 288 &&
                phase13RawSpeedForward.forwardDesiredRawSpeed == 72 &&
                phase13RawSpeedForward.desiredRawSpeed == 72 &&
                phase13RawSpeedForward.requestedDelta == 72 &&
                phase13RawSpeedForward.appliedDelta == 5 &&
                phase13RawSpeedForward.nextRawSpeed == 5 &&
                phase13RawSpeedForward.positiveAccelerationCapped,
            "BTECH 84de positive raw-speed acceleration cap changed");
        const auto phase13RawSpeedPenalty =
            mw::battle::battleOriginalAiRawSpeedCommand(72, 6, 4, 3, 0, 52);
        expect(
            phase13RawSpeedPenalty.limitingLegActuatorWord == 3 &&
                phase13RawSpeedPenalty.wrappedProduct == 198 &&
                phase13RawSpeedPenalty.forwardDesiredRawSpeed == 49 &&
                phase13RawSpeedPenalty.requestedDelta == -3 &&
                phase13RawSpeedPenalty.appliedDelta == -3 &&
                phase13RawSpeedPenalty.nextRawSpeed == 49 &&
                !phase13RawSpeedPenalty.positiveAccelerationCapped,
            "BTECH 84de heat/characteristic raw-speed operand changed");
        const auto phase13RawSpeedReverse =
            mw::battle::battleOriginalAiRawSpeedCommand(72, 0, 4, 4, 1, 10);
        const auto phase13RawSpeedStop =
            mw::battle::battleOriginalAiRawSpeedCommand(72, 0, 4, 4, 2, 20);
        expect(
            phase13RawSpeedReverse.forwardDesiredRawSpeed == 72 &&
                phase13RawSpeedReverse.desiredRawSpeed == -36 &&
                phase13RawSpeedReverse.requestedDelta == -46 &&
                phase13RawSpeedReverse.nextRawSpeed == -36 &&
                phase13RawSpeedStop.desiredRawSpeed == 0 &&
                phase13RawSpeedStop.requestedDelta == -20 &&
                phase13RawSpeedStop.nextRawSpeed == 0,
            "BTECH 84de reverse/stop immediate reduction changed");
        const auto phase13RawSpeedSignedDivision =
            mw::battle::battleOriginalAiRawSpeedCommand(-3, 0, 1, 1, 0, 0);
        expect(
            phase13RawSpeedSignedDivision.wrappedProduct == -3 &&
                phase13RawSpeedSignedDivision.forwardDesiredRawSpeed == 0,
            "BTECH 84de signed truncate-toward-zero division changed");

        // FUN_1000_c5ac zero-attitude subset: raw speed advances the original
        // 32-bit Z pair, then signed +36/+38/+3a drift words advance X/Z/Y.
        const auto phase13PoseStep =
            mw::battle::battleOriginalAiZeroAttitudePoseStep(
                100, 300, 200, 5, -2, -4, 3);
        expect(
            phase13PoseStep.exact &&
                phase13PoseStep.xAfter == 98 &&
                phase13PoseStep.yAfter == 296 &&
                phase13PoseStep.zAfter == 208,
            "BTECH c5ac zero-attitude pose step changed");
        const auto phase13PoseStepReverse =
            mw::battle::battleOriginalAiZeroAttitudePoseStep(
                -100, -300, -200, -5, 2, 4, -3);
        expect(
            phase13PoseStepReverse.xAfter == -98 &&
                phase13PoseStepReverse.yAfter == -296 &&
                phase13PoseStepReverse.zAfter == -208,
            "BTECH c5ac signed reverse pose step changed");
        const auto phase13PoseStepWrap =
            mw::battle::battleOriginalAiZeroAttitudePoseStep(
                std::numeric_limits<int32_t>::max(), 0,
                std::numeric_limits<int32_t>::max(), 1, 1, 0, 0);
        expect(
            phase13PoseStepWrap.xAfter ==
                    std::numeric_limits<int32_t>::min() &&
                phase13PoseStepWrap.zAfter ==
                    std::numeric_limits<int32_t>::min(),
            "BTECH c5ac 32-bit two-word wrap changed");

        const auto phase13PlanarNorth =
            mw::battle::battleOriginalAiPlanarPoseStep(100, 200, 0, 72, 0, 0);
        const auto phase13PlanarWest =
            mw::battle::battleOriginalAiPlanarPoseStep(
                100, 200, static_cast<int16_t>(0x4000), 72, 0, 0);
        const auto phase13PlanarEast =
            mw::battle::battleOriginalAiPlanarPoseStep(
                100, 200, static_cast<int16_t>(-0x4000), 72, 0, 0);
        const auto phase13PlanarSouth =
            mw::battle::battleOriginalAiPlanarPoseStep(
                100, 200, static_cast<int16_t>(-0x8000), 72, 0, 0);
        expect(
            phase13PlanarNorth.exact &&
                phase13PlanarNorth.sineQ14 == 0 &&
                phase13PlanarNorth.cosineQ14 == 0x4000 &&
                phase13PlanarNorth.speedDeltaX == 0 &&
                phase13PlanarNorth.speedDeltaZ == 72 &&
                phase13PlanarNorth.xAfter == 100 &&
                phase13PlanarNorth.zAfter == 272 &&
                phase13PlanarWest.sineQ14 == 0x4000 &&
                phase13PlanarWest.cosineQ14 == 0 &&
                phase13PlanarWest.xAfter == 28 &&
                phase13PlanarWest.zAfter == 200 &&
                phase13PlanarEast.sineQ14 == -0x4000 &&
                phase13PlanarEast.cosineQ14 == 0 &&
                phase13PlanarEast.xAfter == 172 &&
                phase13PlanarSouth.sineQ14 == 0 &&
                phase13PlanarSouth.cosineQ14 == -0x4000 &&
                phase13PlanarSouth.zAfter == 128,
            "BTECH c5ac cardinal Q14 planar projection changed");
        const auto phase13PlanarNorthWest =
            mw::battle::battleOriginalAiPlanarPoseStep(
                100, 200, static_cast<int16_t>(0x2000), 72, 2, 3);
        const auto phase13PlanarNorthEast =
            mw::battle::battleOriginalAiPlanarPoseStep(
                100, 200, static_cast<int16_t>(-0x2000), 72, 0, 0);
        const auto phase13PlanarReverse =
            mw::battle::battleOriginalAiPlanarPoseStep(
                100, 200, static_cast<int16_t>(0x2000), -72, 0, 0);
        expect(
            phase13PlanarNorthWest.sineQ14 == 11585 &&
                phase13PlanarNorthWest.cosineQ14 == 11585 &&
                phase13PlanarNorthWest.speedDeltaX == -51 &&
                phase13PlanarNorthWest.speedDeltaZ == 50 &&
                phase13PlanarNorthWest.xAfter == 51 &&
                phase13PlanarNorthWest.zAfter == 253 &&
                phase13PlanarNorthEast.sineQ14 == -11586 &&
                phase13PlanarNorthEast.cosineQ14 == 11585 &&
                phase13PlanarNorthEast.speedDeltaX == 50 &&
                phase13PlanarNorthEast.speedDeltaZ == 50 &&
                phase13PlanarReverse.speedDeltaX == 50 &&
                phase13PlanarReverse.speedDeltaZ == -51,
            "BTECH c5ac diagonal Q14/SAR14 projection changed");

        const auto phase13StationaryPoseCommit =
            mw::battle::battleOriginalAiOrdinaryPoseCommit(
                0, 0, 1, 2, false);
        const auto phase13ClearPoseCommit =
            mw::battle::battleOriginalAiOrdinaryPoseCommit(
                72, 0, 0, 0, false);
        const auto phase13FirstOccupiedPoseRollback =
            mw::battle::battleOriginalAiOrdinaryPoseCommit(
                5, 0, 1, 0, false);
        const auto phase13RepeatedOccupiedPoseCommit =
            mw::battle::battleOriginalAiOrdinaryPoseCommit(
                72, 1, 1, 1, false);
        const auto phase13SceneCodeTwoPoseRollback =
            mw::battle::battleOriginalAiOrdinaryPoseCommit(
                5, 1, 0, 2, true);
        const auto phase13NoHeadingNudgePoseRollback =
            mw::battle::battleOriginalAiOrdinaryPoseCommit(
                6, 0, 1, 0, true);
        expect(
            phase13StationaryPoseCommit.exact &&
                !phase13StationaryPoseCommit.movingBranch &&
                phase13StationaryPoseCommit.poseAccepted &&
                !phase13StationaryPoseCommit.poseRestored &&
                !phase13StationaryPoseCommit.writesAssociatedRecordProbeFlag &&
                phase13ClearPoseCommit.movingBranch &&
                phase13ClearPoseCommit.poseAccepted &&
                phase13ClearPoseCommit.writesAssociatedRecordProbeFlag &&
                !phase13ClearPoseCommit.associatedRecordProbeFlagValue &&
                phase13FirstOccupiedPoseRollback.poseRestored &&
                phase13FirstOccupiedPoseRollback.clearsControlWords &&
                phase13FirstOccupiedPoseRollback.rollbackHeadingDelta ==
                    -4 * 0x00b6 &&
                phase13RepeatedOccupiedPoseCommit.poseAccepted &&
                phase13RepeatedOccupiedPoseCommit.associatedRecordProbeFlagValue &&
                phase13SceneCodeTwoPoseRollback.poseRestored &&
                phase13SceneCodeTwoPoseRollback.rollbackHeadingDelta ==
                    4 * 0x00b6 &&
                phase13NoHeadingNudgePoseRollback.poseRestored &&
                phase13NoHeadingNudgePoseRollback.rollbackHeadingDelta == 0,
            "BTECH 87c4 ordinary pose commit/rollback gate changed");

        const auto phase13BoundaryInside =
            mw::battle::battleOriginalAiRawBoundaryProbe(0xac80, 24000);
        const auto phase13BoundaryNorth =
            mw::battle::battleOriginalAiRawBoundaryProbe(0, 24001);
        const auto phase13BoundarySouth =
            mw::battle::battleOriginalAiRawBoundaryProbe(0, -24001);
        const auto phase13BoundaryWest =
            mw::battle::battleOriginalAiRawBoundaryProbe(-0xac81, 0);
        const auto phase13BoundaryEast =
            mw::battle::battleOriginalAiRawBoundaryProbe(0xac81, 0);
        const auto phase13BoundaryCorner =
            mw::battle::battleOriginalAiRawBoundaryProbe(-0xac81, 24001);
        expect(
            phase13BoundaryInside.exact &&
                phase13BoundaryInside.inside &&
                phase13BoundaryInside.resultCode == 4u &&
                !phase13BoundaryInside.xAxisWon &&
                phase13BoundaryNorth.resultCode == 0u &&
                phase13BoundarySouth.resultCode == 1u &&
                phase13BoundaryWest.resultCode == 2u &&
                phase13BoundaryEast.resultCode == 3u &&
                phase13BoundaryCorner.resultCode == 2u &&
                phase13BoundaryCorner.xAxisWon,
            "BTECH 0001:2738 raw boundary codes/order changed");

        mw::battle::BattleOriginalAiRelationCellDiagnosticInput
            phase13RelationInput;
        phase13RelationInput.firstSlotOccupied = true;
        phase13RelationInput.secondSlotOccupied = true;
        phase13RelationInput.firstSlot = 0u;
        phase13RelationInput.secondSlot = 4u;
        phase13RelationInput.firstSideWord = 0;
        phase13RelationInput.secondSideWord = 1;
        phase13RelationInput.displayModeWordE1c = 0;
        phase13RelationInput.candidateClipFlagBit2 = true;
        phase13RelationInput.playerViewStrictBoundsPassed = true;
        phase13RelationInput.selectedComponentDisplayListBit1 = true;
        const auto phase13DisplayRelation =
            mw::battle::battleOriginalAiRelationCell(phase13RelationInput);
        auto phase13DisplayBoundsInput = phase13RelationInput;
        phase13DisplayBoundsInput.playerViewStrictBoundsPassed = false;
        const auto phase13DisplayBoundsRelation =
            mw::battle::battleOriginalAiRelationCell(
                phase13DisplayBoundsInput);
        auto phase13SimulationRelationInput = phase13RelationInput;
        phase13SimulationRelationInput.candidateClipFlagBit2 = false;
        phase13SimulationRelationInput.simulationVisibility5206 = true;
        const auto phase13SimulationRelation =
            mw::battle::battleOriginalAiRelationCell(
                phase13SimulationRelationInput);
        auto phase13AlliedSlotRelationInput = phase13RelationInput;
        phase13AlliedSlotRelationInput.firstSlot = 1u;
        phase13AlliedSlotRelationInput.simulationVisibility5206 = true;
        const auto phase13AlliedSlotRelation =
            mw::battle::battleOriginalAiRelationCell(
                phase13AlliedSlotRelationInput);
        auto phase13EmptyRelationInput = phase13RelationInput;
        phase13EmptyRelationInput.secondSlotOccupied = false;
        const auto phase13EmptyRelation =
            mw::battle::battleOriginalAiRelationCell(
                phase13EmptyRelationInput);
        expect(
            phase13DisplayRelation.exact &&
                phase13DisplayRelation.orderedSidePair &&
                phase13DisplayRelation.usesDisplayVisibility50ee &&
                !phase13DisplayRelation.usesSimulationVisibility5206 &&
                phase13DisplayRelation.relationValue &&
                phase13DisplayRelation.mirroredValue &&
                phase13DisplayBoundsRelation.usesDisplayVisibility50ee &&
                !phase13DisplayBoundsRelation.relationValue &&
                !phase13SimulationRelation.usesDisplayVisibility50ee &&
                phase13SimulationRelation.usesSimulationVisibility5206 &&
                phase13SimulationRelation.relationValue &&
                phase13AlliedSlotRelation.usesSimulationVisibility5206 &&
                phase13AlliedSlotRelation.relationValue &&
                phase13EmptyRelation.exact &&
                !phase13EmptyRelation.orderedSidePair &&
                !phase13EmptyRelation.relationValue,
            "BTECH 5454/50ee relation-cell dispatch changed");

        mw::battle::BattleOriginalAiDisplayVisibility50eeInput
            phase13Display50eeInput;
        phase13Display50eeInput.targetIndex = 4;
        phase13Display50eeInput.targetRawPoseAvailable = true;
        phase13Display50eeInput.targetRawX = 0;
        phase13Display50eeInput.targetRawZ = 0;
        phase13Display50eeInput.playerViewRawPoseAvailable = true;
        phase13Display50eeInput.playerViewRawX = 34999;
        phase13Display50eeInput.playerViewRawZ = 0;
        phase13Display50eeInput.selectedComponentPointerAvailable = true;
        phase13Display50eeInput.selectedComponentByte1 = 0x02u;
        const auto phase13Display50eeInside =
            mw::battle::battleOriginalAiDisplayVisibility50ee(
                phase13Display50eeInput);
        auto phase13Display50eeBitClearInput = phase13Display50eeInput;
        phase13Display50eeBitClearInput.selectedComponentByte1 = 0u;
        const auto phase13Display50eeBitClear =
            mw::battle::battleOriginalAiDisplayVisibility50ee(
                phase13Display50eeBitClearInput);
        auto phase13Display50eeEqualXInput = phase13Display50eeInput;
        phase13Display50eeEqualXInput.playerViewRawX = 35000;
        phase13Display50eeEqualXInput.selectedComponentPointerAvailable = false;
        const auto phase13Display50eeEqualX =
            mw::battle::battleOriginalAiDisplayVisibility50ee(
                phase13Display50eeEqualXInput);
        auto phase13Display50eeEqualZInput = phase13Display50eeInput;
        phase13Display50eeEqualZInput.playerViewRawX = 0;
        phase13Display50eeEqualZInput.playerViewRawZ = -35000;
        phase13Display50eeEqualZInput.selectedComponentPointerAvailable = false;
        const auto phase13Display50eeEqualZ =
            mw::battle::battleOriginalAiDisplayVisibility50ee(
                phase13Display50eeEqualZInput);
        auto phase13Display50eeMagnitudeQuirkInput = phase13Display50eeInput;
        phase13Display50eeMagnitudeQuirkInput.playerViewRawX = 0x8000;
        const auto phase13Display50eeMagnitudeQuirk =
            mw::battle::battleOriginalAiDisplayVisibility50ee(
                phase13Display50eeMagnitudeQuirkInput);
        auto phase13Display50eeExtendedInput = phase13Display50eeInput;
        phase13Display50eeExtendedInput.targetIndex = 8;
        phase13Display50eeExtendedInput.extendedControlWordAvailable = true;
        phase13Display50eeExtendedInput.extendedControlWord = -2;
        phase13Display50eeExtendedInput.selectedComponentPointerAvailable =
            false;
        const auto phase13Display50eeExtendedRejected =
            mw::battle::battleOriginalAiDisplayVisibility50ee(
                phase13Display50eeExtendedInput);
        phase13Display50eeExtendedInput.extendedControlWord = -1;
        phase13Display50eeExtendedInput.selectedComponentPointerAvailable =
            true;
        const auto phase13Display50eeExtendedAccepted =
            mw::battle::battleOriginalAiDisplayVisibility50ee(
                phase13Display50eeExtendedInput);
        auto phase13Display50eeMissingExtendedInput =
            phase13Display50eeExtendedInput;
        phase13Display50eeMissingExtendedInput.extendedControlWordAvailable =
            false;
        const auto phase13Display50eeMissingExtended =
            mw::battle::battleOriginalAiDisplayVisibility50ee(
                phase13Display50eeMissingExtendedInput);
        auto phase13Display50eeMissingComponentInput = phase13Display50eeInput;
        phase13Display50eeMissingComponentInput.selectedComponentPointerAvailable =
            false;
        const auto phase13Display50eeMissingComponent =
            mw::battle::battleOriginalAiDisplayVisibility50ee(
                phase13Display50eeMissingComponentInput);
        auto phase13Display50eeInvalidInput = phase13Display50eeInput;
        phase13Display50eeInvalidInput.targetIndex = 9;
        const auto phase13Display50eeInvalid =
            mw::battle::battleOriginalAiDisplayVisibility50ee(
                phase13Display50eeInvalidInput);
        phase13Display50eeInvalidInput.targetIndex = -1;
        const auto phase13Display50eeNegativeIndex =
            mw::battle::battleOriginalAiDisplayVisibility50ee(
                phase13Display50eeInvalidInput);
        auto phase13Display50eeMissingPoseInput = phase13Display50eeInput;
        phase13Display50eeMissingPoseInput.targetRawPoseAvailable = false;
        const auto phase13Display50eeMissingPose =
            mw::battle::battleOriginalAiDisplayVisibility50ee(
                phase13Display50eeMissingPoseInput);
        auto phase13Display50eeComposedInput = phase13RelationInput;
        phase13Display50eeComposedInput.playerViewStrictBoundsPassed =
            phase13Display50eeInside.strictBoundsPassed;
        phase13Display50eeComposedInput.selectedComponentDisplayListBit1 =
            phase13Display50eeInside.selectedComponentDisplayListBit1;
        const auto phase13Display50eeComposedRelation =
            mw::battle::battleOriginalAiRelationCell(
                phase13Display50eeComposedInput);
        expect(
            phase13Display50eeInside.exact &&
                phase13Display50eeInside.targetIndexSupported &&
                phase13Display50eeInside.usesLiveObjectPointer &&
                !phase13Display50eeInside.usesSeparateObjectPointer &&
                !phase13Display50eeInside.extendedControlGateRequired &&
                phase13Display50eeInside.extendedControlGatePassed &&
                phase13Display50eeInside.deltaX == 34999 &&
                phase13Display50eeInside.deltaZ == 0 &&
                phase13Display50eeInside.xMagnitude == 34999u &&
                phase13Display50eeInside.zMagnitude == 0u &&
                phase13Display50eeInside.xStrictBoundsPassed &&
                phase13Display50eeInside.zStrictBoundsPassed &&
                phase13Display50eeInside.strictBoundsPassed &&
                phase13Display50eeInside.selectedComponentPointerRead &&
                phase13Display50eeInside.selectedComponentDisplayListBit1 &&
                phase13Display50eeInside.relationWord == 2u &&
                phase13Display50eeBitClear.exact &&
                phase13Display50eeBitClear.selectedComponentPointerRead &&
                !phase13Display50eeBitClear.selectedComponentDisplayListBit1 &&
                phase13Display50eeBitClear.relationWord == 0u &&
                phase13Display50eeEqualX.exact &&
                phase13Display50eeEqualX.xMagnitude == 35000u &&
                !phase13Display50eeEqualX.xStrictBoundsPassed &&
                !phase13Display50eeEqualX.strictBoundsPassed &&
                !phase13Display50eeEqualX.selectedComponentPointerRead &&
                phase13Display50eeEqualZ.exact &&
                phase13Display50eeEqualZ.zMagnitude == 35000u &&
                !phase13Display50eeEqualZ.zStrictBoundsPassed &&
                !phase13Display50eeEqualZ.selectedComponentPointerRead &&
                phase13Display50eeMagnitudeQuirk.exact &&
                phase13Display50eeMagnitudeQuirk.xMagnitude == 0x7fffu &&
                phase13Display50eeMagnitudeQuirk.relationWord == 2u &&
                phase13Display50eeExtendedRejected.exact &&
                phase13Display50eeExtendedRejected.usesSeparateObjectPointer &&
                phase13Display50eeExtendedRejected.extendedControlGateRequired &&
                !phase13Display50eeExtendedRejected.extendedControlGatePassed &&
                !phase13Display50eeExtendedRejected.selectedComponentPointerRead &&
                phase13Display50eeExtendedRejected.relationWord == 0u &&
                phase13Display50eeExtendedAccepted.exact &&
                phase13Display50eeExtendedAccepted.extendedControlGatePassed &&
                phase13Display50eeExtendedAccepted.selectedComponentPointerRead &&
                phase13Display50eeExtendedAccepted.relationWord == 2u &&
                !phase13Display50eeMissingExtended.exact &&
                !phase13Display50eeMissingComponent.exact &&
                !phase13Display50eeInvalid.exact &&
                !phase13Display50eeInvalid.targetIndexSupported &&
                !phase13Display50eeNegativeIndex.exact &&
                !phase13Display50eeNegativeIndex.targetIndexSupported &&
                !phase13Display50eeMissingPose.exact &&
                phase13Display50eeMissingPose.targetIndexSupported &&
                phase13Display50eeComposedRelation.exact &&
                phase13Display50eeComposedRelation.usesDisplayVisibility50ee &&
                phase13Display50eeComposedRelation.relationValue,
            "BTECH 50ee exact display-state diagnostic changed");

        const auto phase13NearVisibility =
            mw::battle::battleOriginalAiSimulationVisibility5206Near(
                611, -311, 1000, 100, 200, -200);
        const auto phase13NearEqualX =
            mw::battle::battleOriginalAiSimulationVisibility5206Near(
                100, 511, 77, 100, 0, -9000);
        const auto phase13VisibilityStrictX =
            mw::battle::battleOriginalAiSimulationVisibility5206Near(
                0, 0, 0, 0x0200, 0, 0);
        const auto phase13VisibilityStrictZ =
            mw::battle::battleOriginalAiSimulationVisibility5206Near(
                0, 0, 0, 0, -0x0200, 0);
        const auto phase13VisibilitySpecialMagnitude =
            mw::battle::battleOriginalAiSimulationVisibility5206Near(
                0, 0, 0, 0, 0x8000, 0);
        auto phase13NearRelationInput = phase13SimulationRelationInput;
        phase13NearRelationInput.simulationVisibility5206 =
            phase13NearVisibility.relationWord != 0u;
        const auto phase13NearRelation =
            mw::battle::battleOriginalAiRelationCell(
                phase13NearRelationInput);
        expect(
            phase13NearVisibility.exact &&
                phase13NearVisibility.endpointOrderingExact &&
                phase13NearVisibility.endpointsSwapped &&
                phase13NearVisibility.orderedFirstX == 100 &&
                phase13NearVisibility.orderedFirstZ == 200 &&
                phase13NearVisibility.orderedFirstY == -200 &&
                phase13NearVisibility.orderedSecondX == 611 &&
                phase13NearVisibility.orderedSecondZ == -311 &&
                phase13NearVisibility.orderedSecondY == 1000 &&
                phase13NearVisibility.rawDeltaX == 511 &&
                phase13NearVisibility.rawDeltaZ == -511 &&
                phase13NearVisibility.rawDeltaY == 1200 &&
                phase13NearVisibility.zMagnitudeLow == 511u &&
                phase13NearVisibility.zMagnitudeHigh == 0 &&
                phase13NearVisibility.strictXBelow0x200 &&
                phase13NearVisibility.strictZBelow0x200 &&
                phase13NearVisibility.immediateVisible &&
                !phase13NearVisibility.sampledTerrainPathOpen &&
                !phase13NearVisibility.terrainGridRead &&
                !phase13NearVisibility.endpointHeightAffectsImmediateResult &&
                phase13NearVisibility.relationWord == 1u &&
                phase13NearEqualX.exact &&
                !phase13NearEqualX.endpointsSwapped &&
                phase13VisibilityStrictX.endpointOrderingExact &&
                !phase13VisibilityStrictX.exact &&
                !phase13VisibilityStrictX.strictXBelow0x200 &&
                phase13VisibilityStrictX.sampledTerrainPathOpen &&
                phase13VisibilityStrictX.relationWord == 0u &&
                !phase13VisibilityStrictZ.exact &&
                !phase13VisibilityStrictZ.strictZBelow0x200 &&
                phase13VisibilityStrictZ.sampledTerrainPathOpen &&
                phase13VisibilitySpecialMagnitude.zMagnitudeLow == 0x7fffu &&
                phase13VisibilitySpecialMagnitude.zMagnitudeHigh == 0 &&
                !phase13VisibilitySpecialMagnitude.exact &&
                phase13NearRelation.usesSimulationVisibility5206 &&
                phase13NearRelation.relationValue,
            "BTECH 5206 near-cell immediate relation changed");

        mw::battle::BattleTerrainCollisionGrid phase13VisibilityGrid;
        phase13VisibilityGrid.valid = true;
        phase13VisibilityGrid.provenance =
            "phase13_5206_synthetic_raw_grd";
        phase13VisibilityGrid.width = 174;
        phase13VisibilityGrid.height = 94;
        phase13VisibilityGrid.cellSize = 500.0;
        phase13VisibilityGrid.rawSamples.assign(174u * 94u, 0u);
        const auto phase13SampledXMajor =
            mw::battle::battleOriginalAiSimulationVisibility5206Sampled(
                phase13VisibilityGrid, 0, 0, 300, 1000, 200, 300);
        const auto phase13SampledTie =
            mw::battle::battleOriginalAiSimulationVisibility5206Sampled(
                phase13VisibilityGrid, 0, 0, 300, 1024, 1024, 300);
        const auto phase13SampledNegativeZ =
            mw::battle::battleOriginalAiSimulationVisibility5206Sampled(
                phase13VisibilityGrid, 0, 0, 100, 200, -1000, 1101);
        auto phase13VisibilityBlockedGrid = phase13VisibilityGrid;
        phase13VisibilityBlockedGrid.rawSamples[87u * 94u + 46u] = 19u;
        const auto phase13SampledBlocked =
            mw::battle::battleOriginalAiSimulationVisibility5206Sampled(
                phase13VisibilityBlockedGrid,
                0, 0, 300, 1000, 200, 300);
        const auto phase13SampledEqualHeight =
            mw::battle::battleOriginalAiSimulationVisibility5206Sampled(
                phase13VisibilityBlockedGrid,
                0, 0, 304, 1000, 200, 304);
        auto phase13VisibilityTerminalGrid = phase13VisibilityGrid;
        phase13VisibilityTerminalGrid.rawSamples[88u * 94u + 46u] = 19u;
        const auto phase13SampledTerminalBlocked =
            mw::battle::battleOriginalAiSimulationVisibility5206Sampled(
                phase13VisibilityTerminalGrid,
                0, 0, 300, 1000, 200, 300);
        const auto phase13SampledFarOpen =
            mw::battle::battleOriginalAiSimulationVisibility5206Sampled(
                phase13VisibilityGrid,
                43500, 0, 300, 44100, 0, 300);
        expect(
            phase13SampledXMajor.exact &&
                phase13SampledXMajor.gridContractValid &&
                phase13SampledXMajor.sampledTerrainPathUsed &&
                phase13SampledXMajor.xMajorAxis &&
                !phase13SampledXMajor.zMajorAxis &&
                phase13SampledXMajor.rawStepX == 512 &&
                phase13SampledXMajor.rawStepZ == 102 &&
                phase13SampledXMajor.rawStepY == 0 &&
                phase13SampledXMajor.sampledPointCount == 2u &&
                phase13SampledXMajor.samples[0].rawX == 512 &&
                phase13SampledXMajor.samples[0].rawZ == 102 &&
                !phase13SampledXMajor.samples[0].
                    reachesOrPassesEndpoint &&
                phase13SampledXMajor.samples[1].rawX == 1024 &&
                phase13SampledXMajor.samples[1].rawZ == 204 &&
                phase13SampledXMajor.samples[1].
                    reachesOrPassesEndpoint &&
                phase13SampledXMajor.terminalSampleVisited &&
                phase13SampledXMajor.relationWord == 1u &&
                phase13SampledTie.exact &&
                phase13SampledTie.zMajorAxis &&
                phase13SampledTie.rawStepX == 512 &&
                phase13SampledTie.rawStepZ == 512 &&
                phase13SampledTie.sampledPointCount == 2u &&
                phase13SampledNegativeZ.exact &&
                phase13SampledNegativeZ.zMajorAxis &&
                phase13SampledNegativeZ.negativeZDirection &&
                phase13SampledNegativeZ.rawStepX == 102 &&
                phase13SampledNegativeZ.rawStepZ == -512 &&
                phase13SampledNegativeZ.rawStepY == 512 &&
                phase13SampledNegativeZ.samples[1].rawY == 1124 &&
                phase13SampledBlocked.exact &&
                phase13SampledBlocked.blockedByTerrain &&
                phase13SampledBlocked.blockingSampleIndex == 0u &&
                phase13SampledBlocked.samples[0].terrainHeight == 304u &&
                phase13SampledBlocked.relationWord == 0u &&
                phase13SampledEqualHeight.exact &&
                !phase13SampledEqualHeight.blockedByTerrain &&
                phase13SampledEqualHeight.relationWord == 1u &&
                phase13SampledTerminalBlocked.exact &&
                phase13SampledTerminalBlocked.blockedByTerrain &&
                phase13SampledTerminalBlocked.blockingSampleIndex == 1u &&
                phase13SampledTerminalBlocked.terminalSampleVisited &&
                !phase13SampledFarOpen.exact &&
                phase13SampledFarOpen.farSceneQueryOpen &&
                phase13SampledFarOpen.sampledPointCount == 2u &&
                phase13SampledFarOpen.terminalSampleVisited,
            "BTECH 5206/5559 sampled terrain relation changed");

        const auto phase13ControlInitPlayer =
            mw::battle::battleOriginalAiControlInitialization6fd8(
                0u, true);
        const auto phase13ControlInitOpponent =
            mw::battle::battleOriginalAiControlInitialization6fd8(
                4u, true);
        const auto phase13ControlInitUnoccupied =
            mw::battle::battleOriginalAiControlInitialization6fd8(
                6u, false);
        const auto phase13ControlInitInvalid =
            mw::battle::battleOriginalAiControlInitialization6fd8(
                8u, true);
        expect(
            phase13ControlInitPlayer.exact &&
                phase13ControlInitPlayer.visibleLoopWroteRecord &&
                phase13ControlInitPlayer.recordValuesKnown &&
                phase13ControlInitPlayer.zeroFillByteCount == 0x55u &&
                phase13ControlInitPlayer.word00 == 0 &&
                phase13ControlInitPlayer.word0e == -1 &&
                phase13ControlInitPlayer.sideWord10 == 0 &&
                phase13ControlInitPlayer.slotWord12 == 0 &&
                phase13ControlInitPlayer.word14 == -1 &&
                phase13ControlInitPlayer.aggregateWord16 == 0 &&
                phase13ControlInitPlayer.word18 == -1 &&
                phase13ControlInitPlayer.word1a == -1 &&
                phase13ControlInitPlayer.selectionCountWord1c == 0 &&
                phase13ControlInitPlayer.cachedPoseStateWord2c == 0 &&
                phase13ControlInitPlayer.selectorWord3a == 0 &&
                phase13ControlInitPlayer.selectorWord3c == 0 &&
                phase13ControlInitPlayer.flagByte3e == 1u &&
                phase13ControlInitPlayer.numericStateWord46 == 0 &&
                phase13ControlInitPlayer.sharedStateWord51 == 0 &&
                phase13ControlInitPlayer.word53 == 0 &&
                phase13ControlInitPlayer.recordBytes[0x0eu] == 0xffu &&
                phase13ControlInitPlayer.recordBytes[0x0fu] == 0xffu &&
                phase13ControlInitPlayer.recordBytes[0x3eu] == 1u &&
                phase13ControlInitOpponent.exact &&
                phase13ControlInitOpponent.sideWord10 == 1 &&
                phase13ControlInitOpponent.slotWord12 == 4 &&
                phase13ControlInitOpponent.sharedStateWord51 == 0 &&
                phase13ControlInitUnoccupied.exact &&
                !phase13ControlInitUnoccupied.visibleLoopWroteRecord &&
                !phase13ControlInitUnoccupied.recordValuesKnown &&
                phase13ControlInitUnoccupied.zeroFillByteCount == 0u &&
                !phase13ControlInitInvalid.exact,
            "BTECH 6fd8 occupied control-record initialization changed");

        const auto phase13InitialPosePlayerSide =
            mw::battle::battleOriginalAiInitialBodyPose1a22ModeZero(
                1u, true, 0, true,
                -25, 75,
                0, 0,
                0, 1000);
        const auto phase13InitialPoseOpponentSide =
            mw::battle::battleOriginalAiInitialBodyPose1a22ModeZero(
                4u, true, 0, true,
                0, 1000,
                0, 0,
                0, 1000);
        const auto phase13InitialPoseEastBearing =
            mw::battle::battleOriginalAiInitialBodyPose1a22ModeZero(
                0u, true, 0, true,
                0, 0,
                0, 0,
                1000, 0);
        const auto phase13InitialPoseOtherMode =
            mw::battle::battleOriginalAiInitialBodyPose1a22ModeZero(
                4u, true, 1, true,
                0, 1000,
                0, 0,
                0, 1000);
        const auto phase13InitialPoseMissingAnchor =
            mw::battle::battleOriginalAiInitialBodyPose1a22ModeZero(
                4u, true, 0, false,
                0, 1000,
                0, 0,
                0, 1000);
        const auto phase13GeneralInitialPose = [](
                                                   int16_t mode,
                                                   uint8_t flags,
                                                   bool flagsAvailable,
                                                   bool objectiveAvailable) {
            mw::battle::BattleOriginalAiInitialBodyPoseDiagnosticInput input;
            input.liveObjectSlot = 1u;
            input.liveObjectOccupied = true;
            input.numericSideModeWord = mode;
            input.anchorsAvailable = true;
            input.slotRawX = -25;
            input.slotRawZ = 75;
            input.slot0RawX = 0;
            input.slot0RawZ = 0;
            input.slot4RawX = 0;
            input.slot4RawZ = 1000;
            input.sideHeadingFlagsAvailable = flagsAvailable;
            input.sideHeadingFlags = flags;
            input.separateObjectiveRawPoseAvailable = objectiveAvailable;
            input.separateObjectiveRawX = 1000;
            input.separateObjectiveRawZ = 0;
            return mw::battle::battleOriginalAiInitialBodyPose1a22(input);
        };
        const auto phase13InitialPoseMode1North =
            phase13GeneralInitialPose(1, 0x01u, true, false);
        const auto phase13InitialPoseMode1South =
            phase13GeneralInitialPose(1, 0x02u, true, false);
        const auto phase13InitialPoseMode1West =
            phase13GeneralInitialPose(1, 0x04u, true, false);
        const auto phase13InitialPoseMode1East =
            phase13GeneralInitialPose(1, 0x08u, true, false);
        const auto phase13InitialPoseMode1Priority =
            phase13GeneralInitialPose(1, 0x0fu, true, false);
        const auto phase13InitialPoseMode1NoFlags =
            phase13GeneralInitialPose(1, 0x00u, true, false);
        const auto phase13InitialPoseMode1Open =
            phase13GeneralInitialPose(1, 0x04u, false, false);
        const auto phase13InitialPoseMode2 =
            phase13GeneralInitialPose(2, 0, false, false);
        const auto phase13InitialPoseMode4 =
            phase13GeneralInitialPose(4, 0, false, false);
        const auto phase13InitialPoseMode3Open =
            phase13GeneralInitialPose(3, 0, false, false);
        const auto phase13InitialPoseMode3Bound =
            phase13GeneralInitialPose(3, 0, false, true);
        mw::battle::BattleOriginalAiInitialBodyPoseDiagnosticInput
            phase13Mode3NullPoseInput;
        phase13Mode3NullPoseInput.liveObjectSlot = 1u;
        phase13Mode3NullPoseInput.liveObjectOccupied = true;
        phase13Mode3NullPoseInput.numericSideModeWord = 3;
        phase13Mode3NullPoseInput.anchorsAvailable = true;
        phase13Mode3NullPoseInput.slotRawX = -25;
        phase13Mode3NullPoseInput.slotRawZ = 75;
        phase13Mode3NullPoseInput.slot0RawX = 0;
        phase13Mode3NullPoseInput.slot0RawZ = 0;
        phase13Mode3NullPoseInput.slot4RawX = 0;
        phase13Mode3NullPoseInput.slot4RawZ = 1000;
        phase13Mode3NullPoseInput.modeThreeNullDataRawPoseAvailable = true;
        phase13Mode3NullPoseInput.modeThreeNullDataRawX = 0x5220534d;
        phase13Mode3NullPoseInput.modeThreeNullDataRawZ = 0x542d6e75;
        const auto phase13InitialPoseMode3Null =
            mw::battle::battleOriginalAiInitialBodyPose1a22(
                phase13Mode3NullPoseInput);
        expect(
            phase13InitialPosePlayerSide.exact &&
                phase13InitialPosePlayerSide.sideWord == 0 &&
                phase13InitialPosePlayerSide.rawX == -25 &&
                phase13InitialPosePlayerSide.rawY == 300 &&
                phase13InitialPosePlayerSide.rawZ == 75 &&
                phase13InitialPosePlayerSide.pitch == 0 &&
                phase13InitialPosePlayerSide.roll == 0 &&
                phase13InitialPosePlayerSide.sharedSlot0ToSlot4Bearing == 0 &&
                phase13InitialPosePlayerSide.heading == 0 &&
                phase13InitialPosePlayerSide.
                    positionCopiedToAuthoritativeBody &&
                phase13InitialPoseOpponentSide.exact &&
                phase13InitialPoseOpponentSide.sideWord == 1 &&
                phase13InitialPoseOpponentSide.heading == 0x7ff8 &&
                phase13InitialPoseEastBearing.exact &&
                phase13InitialPoseEastBearing.
                    sharedSlot0ToSlot4Bearing == -0x4000 &&
                phase13InitialPoseEastBearing.heading == -0x4000 &&
                !phase13InitialPoseOtherMode.exact &&
                !phase13InitialPoseOtherMode.numericSideModeZero &&
                !phase13InitialPoseMissingAnchor.exact &&
                !phase13InitialPoseMissingAnchor.anchorsAvailable,
            "BTECH 1a22 mode-zero initial authoritative body pose changed");
        expect(
            phase13InitialPoseMode1North.exact &&
                phase13InitialPoseMode1North.headingSource ==
                    mw::battle::BattleOriginalAiInitialHeadingSource::
                        SideHeadingFlag &&
                phase13InitialPoseMode1North.selectedSideHeadingFlagMask ==
                    0x01u &&
                phase13InitialPoseMode1North.heading == 0 &&
                phase13InitialPoseMode1South.exact &&
                phase13InitialPoseMode1South.selectedSideHeadingFlagMask ==
                    0x02u &&
                phase13InitialPoseMode1South.heading ==
                    static_cast<int16_t>(0x7ff8) &&
                phase13InitialPoseMode1West.exact &&
                phase13InitialPoseMode1West.selectedSideHeadingFlagMask ==
                    0x04u &&
                phase13InitialPoseMode1West.heading == 0x3ffc &&
                phase13InitialPoseMode1East.exact &&
                phase13InitialPoseMode1East.selectedSideHeadingFlagMask ==
                    0x08u &&
                phase13InitialPoseMode1East.heading == -0x3ffc &&
                phase13InitialPoseMode1Priority.exact &&
                phase13InitialPoseMode1Priority.
                    selectedSideHeadingFlagMask == 0x01u &&
                phase13InitialPoseMode1NoFlags.exact &&
                phase13InitialPoseMode1NoFlags.
                    selectedSideHeadingFlagMask == 0u &&
                phase13InitialPoseMode1NoFlags.heading == 0 &&
                !phase13InitialPoseMode1Open.exact &&
                phase13InitialPoseMode2.exact &&
                phase13InitialPoseMode2.headingSource ==
                    mw::battle::BattleOriginalAiInitialHeadingSource::
                        SharedSlot0ToSlot4Bearing &&
                phase13InitialPoseMode2.heading == 0 &&
                phase13InitialPoseMode4.exact &&
                phase13InitialPoseMode4.heading == 0 &&
                !phase13InitialPoseMode3Open.exact &&
                phase13InitialPoseMode3Bound.exact &&
                phase13InitialPoseMode3Bound.headingSource ==
                    mw::battle::BattleOriginalAiInitialHeadingSource::
                        SeparateObjectiveBearing &&
                phase13InitialPoseMode3Bound.heading == -0x4000 &&
                phase13InitialPoseMode3Null.exact &&
                phase13InitialPoseMode3Null.
                    modeThreeNullDataRawPoseAvailable &&
                !phase13InitialPoseMode3Null.
                    separateObjectiveRawPoseAvailable &&
                phase13InitialPoseMode3Null.headingSource ==
                    mw::battle::BattleOriginalAiInitialHeadingSource::
                        ModeThreeNullDataBearing,
            "BTECH 1a22 placement-mode heading branches changed");

        mw::battle::BattleOriginalAiAggregateDiagnosticInput
            phase13AggregateInput;
        phase13AggregateInput.liveObjectSlot = 4u;
        phase13AggregateInput.occupied = true;
        phase13AggregateInput.rawX = 0;
        phase13AggregateInput.rawZ = 0;
        phase13AggregateInput.leftLegActuatorWord6c = 4;
        phase13AggregateInput.rightLegActuatorWord6e = 4;
        phase13AggregateInput.armorWords = {{1, 2, 3, 4, 5, 6, 7, 8, 9}};
        phase13AggregateInput.internalWords = {{1, 2, 3, 4, 5, 6, 7, 8}};
        phase13AggregateInput.weapons[0].installationByte = 1u;
        phase13AggregateInput.weapons[0].originalDamageWord = 5;
        phase13AggregateInput.weapons[1].installationByte = 2u;
        phase13AggregateInput.weapons[1].originalDamageWord = 10;
        phase13AggregateInput.weapons[2].installationByte = 0u;
        phase13AggregateInput.weapons[2].originalDamageWord = 100;
        const auto phase13Aggregate =
            mw::battle::battleOriginalAiAggregate7070(
                phase13AggregateInput);
        auto phase13AggregateGyroInput = phase13AggregateInput;
        phase13AggregateGyroInput.gyroConditionWord13 = 2;
        const auto phase13AggregateGyro =
            mw::battle::battleOriginalAiAggregate7070(
                phase13AggregateGyroInput);
        auto phase13AggregateSensorInput = phase13AggregateInput;
        phase13AggregateSensorInput.sensorConditionWord0f = 2;
        const auto phase13AggregateSensor =
            mw::battle::battleOriginalAiAggregate7070(
                phase13AggregateSensorInput);
        auto phase13AggregateStateOneInput = phase13AggregateInput;
        phase13AggregateStateOneInput.sharedCurrentControlStateWord51 = 1;
        phase13AggregateStateOneInput.leftLegActuatorWord6c = 0;
        const auto phase13AggregateStateOne =
            mw::battle::battleOriginalAiAggregate7070(
                phase13AggregateStateOneInput);
        auto phase13AggregateStateZeroInput = phase13AggregateStateOneInput;
        phase13AggregateStateZeroInput.sharedCurrentControlStateWord51 = 0;
        const auto phase13AggregateStateZero =
            mw::battle::battleOriginalAiAggregate7070(
                phase13AggregateStateZeroInput);
        auto phase13AggregateBoundaryInput = phase13AggregateInput;
        phase13AggregateBoundaryInput.engineConditionWord11 = 3;
        phase13AggregateBoundaryInput.rawZ = 24001;
        const auto phase13AggregateBoundary =
            mw::battle::battleOriginalAiAggregate7070(
                phase13AggregateBoundaryInput);
        auto phase13AggregateWrapInput = phase13AggregateInput;
        phase13AggregateWrapInput.armorWords.fill(0);
        phase13AggregateWrapInput.internalWords.fill(0);
        phase13AggregateWrapInput.internalWords[0] = 32760;
        phase13AggregateWrapInput.internalWords[2] = 4;
        phase13AggregateWrapInput.internalWords[3] = 4;
        phase13AggregateWrapInput.weapons.fill({});
        phase13AggregateWrapInput.weapons[0].installationByte = 1u;
        phase13AggregateWrapInput.weapons[0].originalDamageWord = 5;
        const auto phase13AggregateWrap =
            mw::battle::battleOriginalAiAggregate7070(
                phase13AggregateWrapInput);
        auto phase13AggregateEmptyInput = phase13AggregateInput;
        phase13AggregateEmptyInput.occupied = false;
        const auto phase13AggregateEmpty =
            mw::battle::battleOriginalAiAggregate7070(
                phase13AggregateEmptyInput);
        expect(
            phase13Aggregate.exact && phase13Aggregate.occupied &&
                phase13Aggregate.armorContributionEnabled &&
                phase13Aggregate.weaponContributionEnabled &&
                phase13Aggregate.armorContribution == 45 &&
                phase13Aggregate.internalContribution == 36 &&
                phase13Aggregate.weaponContribution == 30 &&
                phase13Aggregate.contributingWeaponCount == 2u &&
                phase13Aggregate.preViabilityAggregate == 111 &&
                phase13Aggregate.viability4106Passed &&
                !phase13Aggregate.viabilityReplacedWithMinusOne &&
                phase13Aggregate.postViabilityAggregate == 111 &&
                phase13Aggregate.boundary.inside &&
                !phase13Aggregate.boundaryOverrideApplied &&
                phase13Aggregate.finalAggregateWord16 == 111 &&
                !phase13AggregateGyro.armorContributionEnabled &&
                phase13AggregateGyro.finalAggregateWord16 == 66 &&
                !phase13AggregateSensor.weaponContributionEnabled &&
                phase13AggregateSensor.finalAggregateWord16 == 81 &&
                !phase13AggregateStateOne.viability4106Passed &&
                phase13AggregateStateOne.finalAggregateWord16 == -1 &&
                phase13AggregateStateZero.viability4106Passed &&
                phase13AggregateStateZero.finalAggregateWord16 == 111 &&
                phase13AggregateBoundary.viabilityReplacedWithMinusOne &&
                phase13AggregateBoundary.boundaryOverrideApplied &&
                phase13AggregateBoundary.boundary.resultCode == 0u &&
                phase13AggregateBoundary.finalAggregateWord16 == -2 &&
                phase13AggregateBoundary.clearsControlStateWord46 &&
                phase13AggregateBoundary.setsSixComponentFlagBits80 &&
                phase13AggregateWrap.preViabilityAggregate == -32758 &&
                phase13AggregateWrap.finalAggregateWord16 == -32758 &&
                phase13AggregateEmpty.exact &&
                !phase13AggregateEmpty.occupied &&
                phase13AggregateEmpty.finalAggregateWord16 == 0,
            "BTECH 7070 aggregate/4106/boundary order changed");

        const auto& phase13BoundPlayer =
            explicitRosterSnapshot.combatants[0];
        const auto& phase13BoundEnemy =
            explicitRosterSnapshot.combatants[1];
        const auto phase13BoundPlayerAggregate =
            mw::battle::battleOriginalAiAggregate7070FromAuthoritativeState(
                0u,
                0,
                0,
                0,
                phase13BoundPlayer.detailedDamage,
                phase13BoundPlayer.weapons);
        const auto phase13BoundEnemyAggregate =
            mw::battle::battleOriginalAiAggregate7070FromAuthoritativeState(
                4u,
                200,
                200,
                0,
                phase13BoundEnemy.detailedDamage,
                phase13BoundEnemy.weapons);
        auto phase13BadOrderDamage = phase13BoundEnemy.detailedDamage;
        phase13BadOrderDamage.armorSections[0].section =
            mw::mech3d::MechArmorSectionId::LeftArm;
        const auto phase13BadOrderAggregate =
            mw::battle::battleOriginalAiAggregate7070FromAuthoritativeState(
                4u,
                200,
                200,
                0,
                phase13BadOrderDamage,
                phase13BoundEnemy.weapons);
        auto phase13UnprovenDamageWeapons = phase13BoundEnemy.weapons;
        phase13UnprovenDamageWeapons.front().originalDamageValueProven = false;
        const auto phase13UnprovenDamageAggregate =
            mw::battle::battleOriginalAiAggregate7070FromAuthoritativeState(
                4u,
                200,
                200,
                0,
                phase13BoundEnemy.detailedDamage,
                phase13UnprovenDamageWeapons);
        std::vector<mw::battle::BattleOriginalAiMovementCandidateDiagnosticInput>
            phase13BoundCandidates(8u);
        phase13BoundCandidates[0].entityId = phase13BoundPlayer.id;
        phase13BoundCandidates[0].liveSlotOccupied = true;
        phase13BoundCandidates[0].sideWord = 0;
        phase13BoundCandidates[0].cachedPoseStateWord2c =
            phase13NearVisibility.relationWord != 0u ? 2 : 0;
        phase13BoundCandidates[0].aggregateWord16 =
            phase13BoundPlayerAggregate.finalAggregateWord16;
        phase13BoundCandidates[0].cachedRawX = 100;
        phase13BoundCandidates[0].cachedRawY = 250;
        phase13BoundCandidates[0].cachedRawZ = 200;
        const auto phase13BoundMovementTarget =
            mw::battle::battleOriginalAiSingleMovementTarget(
                1,
                phase13BoundEnemyAggregate.finalAggregateWord16,
                phase13BoundCandidates);
        expect(
            phase13BoundPlayerAggregate.exact &&
                phase13BoundPlayerAggregate.authoritativeStateBindingAttempted &&
                phase13BoundPlayerAggregate.authoritativeStateBindingExact &&
                phase13BoundPlayerAggregate.finalAggregateWord16 > 0 &&
                phase13BoundEnemyAggregate.exact &&
                phase13BoundEnemyAggregate.authoritativeStateBindingExact &&
                phase13BoundEnemyAggregate.finalAggregateWord16 > 0 &&
                phase13BoundMovementTarget.exact &&
                phase13BoundMovementTarget.selected &&
                phase13BoundMovementTarget.selectedSlot == 0u &&
                phase13BoundMovementTarget.selectedEntityId ==
                    phase13BoundPlayer.id &&
                phase13BadOrderAggregate.authoritativeStateBindingAttempted &&
                !phase13BadOrderAggregate.authoritativeStateBindingExact &&
                !phase13BadOrderAggregate.exact &&
                phase13UnprovenDamageAggregate.
                    authoritativeStateBindingAttempted &&
                !phase13UnprovenDamageAggregate.
                    authoritativeStateBindingExact &&
                !phase13UnprovenDamageAggregate.exact,
            "BTECH 7070 authoritative damage binding/23bf handoff changed");

        const auto phase13OverlapSlow =
            mw::battle::battleOriginalAiLiveSlotOverlap(
                0x0153, -0x0153, 799, 5, false);
        const auto phase13OverlapImpact =
            mw::battle::battleOriginalAiLiveSlotOverlap(
                -0x0153, 0x0153, -799, 6, false);
        const auto phase13OverlapSameOwner =
            mw::battle::battleOriginalAiLiveSlotOverlap(0, 0, 0, 6, true);
        const auto phase13OverlapStrictX =
            mw::battle::battleOriginalAiLiveSlotOverlap(
                0x0154, 0, 0, 100, false);
        const auto phase13OverlapStrictZ =
            mw::battle::battleOriginalAiLiveSlotOverlap(
                0, -0x0154, 0, 100, false);
        const auto phase13OverlapStrictY =
            mw::battle::battleOriginalAiLiveSlotOverlap(
                0, 0, 800, 100, false);
        expect(
            phase13OverlapSlow.exact && phase13OverlapSlow.overlaps &&
                phase13OverlapSlow.movementBlocked &&
                !phase13OverlapSlow.teamFilterApplied &&
                !phase13OverlapSlow.dispatchesSharedImpact &&
                phase13OverlapImpact.dispatchesSharedImpact &&
                phase13OverlapSameOwner.movementBlocked &&
                !phase13OverlapSameOwner.dispatchesSharedImpact &&
                !phase13OverlapStrictX.overlaps &&
                !phase13OverlapStrictZ.overlaps &&
                !phase13OverlapStrictY.overlaps,
            "BTECH 89ea live-slot overlap/impact gate changed");

        const auto phase13ObjectiveNpcContact =
            mw::battle::battleOriginalAiObjectiveContact(
                false, 3, false, 3199, 2999, 1399);
        const auto phase13ObjectiveNpcStrictZ =
            mw::battle::battleOriginalAiObjectiveContact(
                false, 3, false, 0, 0, 1400);
        const auto phase13ObjectivePlayerExtendedZ =
            mw::battle::battleOriginalAiObjectiveContact(
                false, 3, true, 0, 0, 3199);
        const auto phase13ObjectivePlayerStrictZ =
            mw::battle::battleOriginalAiObjectiveContact(
                false, 3, true, 0, 0, 3200);
        const auto phase13ObjectiveStrictY =
            mw::battle::battleOriginalAiObjectiveContact(
                false, 3, false, 0, 3000, 0);
        const auto phase13ObjectiveInactive =
            mw::battle::battleOriginalAiObjectiveContact(
                true, 3, false, 0, 0, 0);
        const auto phase13ObjectiveOpaqueGate =
            mw::battle::battleOriginalAiObjectiveContact(
                false, 2, false, 0, 0, 0);
        expect(
            phase13ObjectiveNpcContact.exact &&
                phase13ObjectiveNpcContact.contactPredicate &&
                phase13ObjectiveNpcContact.movementBlocked &&
                phase13ObjectiveNpcContact.zThreshold == 1400u &&
                !phase13ObjectiveNpcStrictZ.contactPredicate &&
                phase13ObjectivePlayerExtendedZ.contactPredicate &&
                phase13ObjectivePlayerExtendedZ.movementBlocked &&
                phase13ObjectivePlayerExtendedZ.zThreshold == 3200u &&
                !phase13ObjectivePlayerStrictZ.contactPredicate &&
                !phase13ObjectiveStrictY.contactPredicate &&
                !phase13ObjectiveInactive.contactPredicate &&
                phase13ObjectiveOpaqueGate.contactPredicate &&
                !phase13ObjectiveOpaqueGate.movementBlocked,
            "BTECH 8c38/5dea separate-objective contact gates changed");

        using SlotProbeObject = mw::battle::
            BattleOriginalAiSlotProbeLiveObjectDiagnosticInput;
        mw::battle::BattleOriginalAiSlotProbeDiagnosticInput
            phase13SlotProbeInput;
        phase13SlotProbeInput.movingSlot = 1u;
        phase13SlotProbeInput.controlWord00 = 0;
        phase13SlotProbeInput.controlState46 = 0;
        phase13SlotProbeInput.movingOwnerWord = 1;
        phase13SlotProbeInput.rawSpeed = 6;
        phase13SlotProbeInput.cachedDestinationRawX = 1000;
        phase13SlotProbeInput.cachedDestinationRawY = 0;
        phase13SlotProbeInput.cachedDestinationRawZ = 1000;
        phase13SlotProbeInput.liveSlots = {
            SlotProbeObject{{101u}, true, 0, 1000, 0, 1000},
            SlotProbeObject{{102u}, true, 1, 0, 0, 0},
        };
        const auto phase13SlotProbeClear =
            mw::battle::battleOriginalAiSlotProbe(phase13SlotProbeInput);
        auto phase13SlotProbeBoundaryInput = phase13SlotProbeInput;
        phase13SlotProbeBoundaryInput.tentativeRawX = 0xac81;
        const auto phase13SlotProbeBoundary =
            mw::battle::battleOriginalAiSlotProbe(
                phase13SlotProbeBoundaryInput);
        auto phase13SlotProbeOverlapInput = phase13SlotProbeInput;
        phase13SlotProbeOverlapInput.liveSlots[0].rawX = 0x0153;
        phase13SlotProbeOverlapInput.liveSlots[0].rawY = 799;
        phase13SlotProbeOverlapInput.liveSlots[0].rawZ = -0x0153;
        const auto phase13SlotProbeOverlap =
            mw::battle::battleOriginalAiSlotProbe(
                phase13SlotProbeOverlapInput);
        auto phase13SlotProbeObjectiveInput = phase13SlotProbeInput;
        phase13SlotProbeObjectiveInput.objectiveInactiveBitSet = false;
        phase13SlotProbeObjectiveInput.opaqueGateWordEd0 = 3;
        phase13SlotProbeObjectiveInput.objectiveRawZ = 1399;
        const auto phase13SlotProbeObjective =
            mw::battle::battleOriginalAiSlotProbe(
                phase13SlotProbeObjectiveInput);
        auto phase13SlotProbeBypassInput = phase13SlotProbeBoundaryInput;
        phase13SlotProbeBypassInput.cachedDestinationRawX = 0xac80;
        const auto phase13SlotProbeBypass =
            mw::battle::battleOriginalAiSlotProbe(
                phase13SlotProbeBypassInput);
        expect(
            phase13SlotProbeClear.exact &&
                phase13SlotProbeClear.boundaryClassifierCalled &&
                phase13SlotProbeClear.boundary.inside &&
                phase13SlotProbeClear.resultWord == 0u &&
                phase13SlotProbeClear.visitedLiveSlotCount == 1u &&
                !phase13SlotProbeClear.objectiveBlocked &&
                phase13SlotProbeBoundary.resultWord == 1u &&
                !phase13SlotProbeBoundary.boundary.inside &&
                phase13SlotProbeBoundary.visitedLiveSlotCount == 0u &&
                phase13SlotProbeOverlap.resultWord == 1u &&
                phase13SlotProbeOverlap.liveSlotBlocked &&
                phase13SlotProbeOverlap.blockingLiveSlot == 0u &&
                phase13SlotProbeOverlap.blockingEntityId.value == 101u &&
                phase13SlotProbeOverlap.dispatchesSharedImpact &&
                phase13SlotProbeObjective.resultWord == 1u &&
                phase13SlotProbeObjective.objectiveBlocked &&
                phase13SlotProbeBypass.boundaryClassifierBypassed &&
                !phase13SlotProbeBypass.boundaryClassifierCalled &&
                phase13SlotProbeBypass.resultWord == 0u,
            "BTECH 89ea composed slot-probe order/result changed");

        using SceneProbeCandidate =
            mw::battle::BattleOriginalAiSceneProbeCandidateDiagnosticInput;
        const std::vector<SceneProbeCandidate> phase13SceneCandidates{
            {false, true, 0u, true},
            {true, false, 0u, true},
            {true, true, 5u, true},
            {true, true, 6u, true},
            {true, true, 5u, true},
            {true, true, 3u, false},
            {true, true, 3u, true},
            {true, true, 0u, true},
            {true, true, 0u, true},
        };
        const auto phase13SceneNoTable =
            mw::battle::battleOriginalAiSceneProbeSelection(
                false, false, 0u, false, phase13SceneCandidates);
        const auto phase13SceneCachedHit =
            mw::battle::battleOriginalAiSceneProbeSelection(
                true, true, 0u, true, phase13SceneCandidates);
        const auto phase13SceneCachedMiss =
            mw::battle::battleOriginalAiSceneProbeSelection(
                true, true, 0u, false, phase13SceneCandidates);
        expect(
            phase13SceneNoTable.exact &&
                !phase13SceneNoTable.hit &&
                phase13SceneNoTable.visitedCandidateCount == 0u &&
                phase13SceneNoTable.narrowPhaseTestCount == 0u &&
                phase13SceneCachedHit.cacheEligible &&
                phase13SceneCachedHit.selectedFromCache &&
                phase13SceneCachedHit.hit &&
                phase13SceneCachedHit.terminatedOnPriorityZero &&
                phase13SceneCachedHit.visitedCandidateCount == 0u &&
                phase13SceneCachedHit.narrowPhaseTestCount == 1u &&
                phase13SceneCachedMiss.clearedCacheAfterMiss &&
                !phase13SceneCachedMiss.selectedFromCache &&
                phase13SceneCachedMiss.hit &&
                phase13SceneCachedMiss.selectedCandidateIndex == 7u &&
                phase13SceneCachedMiss.selectedPriority == 0u &&
                phase13SceneCachedMiss.visitedCandidateCount == 8u &&
                phase13SceneCachedMiss.narrowPhaseTestCount == 6u &&
                phase13SceneCachedMiss.terminatedOnPriorityZero,
            "BTECH D5CE cache/filter/lower-priority termination changed");

        const auto phase13SceneEqualPriority =
            mw::battle::battleOriginalAiSceneProbeSelection(
                true,
                true,
                1u,
                true,
                std::vector<SceneProbeCandidate>{
                    {true, true, 5u, true},
                    {true, true, 6u, true},
                    {true, true, 5u, true},
                });
        expect(
            !phase13SceneEqualPriority.cacheEligible &&
                !phase13SceneEqualPriority.clearedCacheAfterMiss &&
                phase13SceneEqualPriority.hit &&
                phase13SceneEqualPriority.selectedCandidateIndex == 2u &&
                phase13SceneEqualPriority.selectedPriority == 5u &&
                phase13SceneEqualPriority.visitedCandidateCount == 3u &&
                phase13SceneEqualPriority.narrowPhaseTestCount == 2u &&
                !phase13SceneEqualPriority.terminatedOnPriorityZero,
            "BTECH D5CE equal-priority last-successful selection changed");

        const auto phase13MovementNear =
            mw::battle::battleOriginalAiMovementTarget(
                159, -159, 0, static_cast<int16_t>(0x1234));
        const auto phase13MovementStrictX =
            mw::battle::battleOriginalAiMovementTarget(160, 0, 0, 0);
        const auto phase13MovementStrictZState3 =
            mw::battle::battleOriginalAiMovementTarget(0, -160, 3, 0);
        expect(
            phase13MovementNear.exact &&
                phase13MovementNear.insideStrictStopSquare &&
                phase13MovementNear.movementMode == 2 &&
                phase13MovementNear.desiredHeadingBeforeSteering == 0x1234 &&
                phase13MovementNear.routesDirectlyToSpeedCommand &&
                !phase13MovementNear.routesThroughSteeringProbes &&
                !phase13MovementNear.finalHeadingOpen &&
                !phase13MovementStrictX.insideStrictStopSquare &&
                phase13MovementStrictX.desiredHeadingBeforeSteering ==
                    static_cast<int16_t>(-0x4000) &&
                phase13MovementStrictX.movementMode == 0 &&
                phase13MovementStrictX.routesThroughSteeringProbes &&
                !phase13MovementStrictX.routesDirectlyToSpeedCommand &&
                phase13MovementStrictX.finalHeadingOpen &&
                !phase13MovementStrictZState3.insideStrictStopSquare &&
                phase13MovementStrictZState3.numericState51 == 3 &&
                phase13MovementStrictZState3.routesDirectlyToSpeedCommand &&
                !phase13MovementStrictZState3.routesThroughSteeringProbes &&
                !phase13MovementStrictZState3.finalHeadingOpen,
            "BTECH 94a4/7c7d movement destination routing changed");

        const auto phase13SteeringForwardClear =
            mw::battle::battleOriginalAiSteeringChoice(
                0x1000, true, 0, 0, {false, true, true, true});
        const auto phase13SteeringFirstSideClear =
            mw::battle::battleOriginalAiSteeringChoice(
                0x1000, true, 0, 0, {true, false, true, true});
        const auto phase13SteeringSecondSideClear =
            mw::battle::battleOriginalAiSteeringChoice(
                0x1000, true, 0, 0, {true, true, false, true});
        const auto phase13SteeringReverseProbeClear =
            mw::battle::battleOriginalAiSteeringChoice(
                0x1000, true, 0, 0, {true, true, true, false});
        const auto phase13SteeringAllBlocked =
            mw::battle::battleOriginalAiSteeringChoice(
                0x1000, true, 0, 1, {true, true, true, true});
        const auto phase13SteeringSkipFirstSide =
            mw::battle::battleOriginalAiSteeringChoice(
                0x1000, true, 0, 2, {true, false, false, true});
        const auto phase13SteeringReverseRequest =
            mw::battle::battleOriginalAiSteeringChoice(
                0x1000, false, 0, 0, {false, true, true, true});
        expect(
            phase13SteeringForwardClear.exact &&
                phase13SteeringForwardClear.probeVisitCount == 1u &&
                phase13SteeringForwardClear.probeVisited[0] &&
                !phase13SteeringForwardClear.probeVisited[1] &&
                phase13SteeringForwardClear.selectorWord3cAfter == 0 &&
                phase13SteeringForwardClear.selectedHeadingOffset == 0 &&
                phase13SteeringForwardClear.finalDesiredHeading == 0x1000 &&
                phase13SteeringForwardClear.movementMode == 0 &&
                phase13SteeringFirstSideClear.probeVisitCount == 2u &&
                phase13SteeringFirstSideClear.selectorWord3cAfter == 1 &&
                phase13SteeringFirstSideClear.selectedHeadingOffset == 0x3ffc &&
                phase13SteeringSecondSideClear.probeVisitCount == 3u &&
                phase13SteeringSecondSideClear.selectorWord3cAfter == 2 &&
                phase13SteeringSecondSideClear.selectedHeadingOffset ==
                    static_cast<int16_t>(-0x3ffc) &&
                phase13SteeringReverseProbeClear.probeVisitCount == 4u &&
                phase13SteeringReverseProbeClear.selectorWord3aAfter == 1 &&
                phase13SteeringReverseProbeClear.selectedHeadingOffset ==
                    static_cast<int16_t>(0x7ff8) &&
                phase13SteeringAllBlocked.selectorWord3cAfter == 2 &&
                phase13SteeringAllBlocked.probeVisitCount == 4u &&
                phase13SteeringSkipFirstSide.selectorWord3cAfter == 2 &&
                !phase13SteeringSkipFirstSide.probeVisited[1] &&
                phase13SteeringSkipFirstSide.probeVisitCount == 2u &&
                phase13SteeringReverseRequest.headingUsedForProbes ==
                    static_cast<int16_t>(-0x7008) &&
                phase13SteeringReverseRequest.finalDesiredHeading == 0x0ff0 &&
                phase13SteeringReverseRequest.movementMode == 1,
            "BTECH 95a1 steering probe order/selector transition changed");

        mw::battle::BattleTerrainCollisionGrid phase13ProbeGrid;
        phase13ProbeGrid.valid = true;
        phase13ProbeGrid.provenance = "phase13_synthetic_raw_grd";
        phase13ProbeGrid.width = 174;
        phase13ProbeGrid.height = 94;
        phase13ProbeGrid.cellSize = 500.0;
        phase13ProbeGrid.rawSamples.assign(174u * 94u, 0u);
        const auto phase13TerrainProbeClear =
            mw::battle::battleOriginalAiTerrainProbe(
                phase13ProbeGrid, 0, 0, 0, 0u);
        phase13ProbeGrid.rawSamples[86u * 94u + 45u] = 0x63u;
        const auto phase13TerrainProbeBlocked =
            mw::battle::battleOriginalAiTerrainProbe(
                phase13ProbeGrid, 0, 0, 0, 0u);
        const auto phase13TerrainProbeLateral =
            mw::battle::battleOriginalAiTerrainProbe(
                phase13ProbeGrid, 0, 0, 0, 1u);
        const auto phase13TerrainProbeEdgeOpen =
            mw::battle::battleOriginalAiTerrainProbe(
                phase13ProbeGrid, -44000, 0, 0, 0u);
        expect(
            phase13TerrainProbeClear.exact &&
                phase13TerrainProbeClear.gridContractValid &&
                phase13TerrainProbeClear.rawDirectionX == 0 &&
                phase13TerrainProbeClear.rawDirectionZ == 512 &&
                phase13TerrainProbeClear.rawProbeZ[0] == 512 &&
                phase13TerrainProbeClear.rawProbeZ[3] == 2048 &&
                phase13TerrainProbeClear.gridX[0] == 86 &&
                phase13TerrainProbeClear.gridZ[0] == 45 &&
                phase13TerrainProbeClear.visitedStepCount == 4u &&
                phase13TerrainProbeClear.visitedSampleCount == 100u &&
                !phase13TerrainProbeClear.blocked &&
                phase13TerrainProbeBlocked.exact &&
                phase13TerrainProbeBlocked.blocked &&
                phase13TerrainProbeBlocked.blockingStepIndex == 0u &&
                phase13TerrainProbeBlocked.blockingGridX == 86 &&
                phase13TerrainProbeBlocked.blockingGridZ == 45 &&
                phase13TerrainProbeBlocked.blockingRawSample == 0x63u &&
                phase13TerrainProbeBlocked.visitedSampleCount == 13u &&
                phase13TerrainProbeLateral.rawDirectionX == -512 &&
                phase13TerrainProbeLateral.rawDirectionZ == 0 &&
                !phase13TerrainProbeEdgeOpen.exact &&
                phase13TerrainProbeEdgeOpen.footprintOutsideGrid,
            "BTECH 9700/9939 four-step raw GRD probe changed");

        const auto phase13LaneCenter =
            mw::battle::battleOriginalAiOrientedLane(0, 2047, 0, 1u);
        const auto phase13LaneStrictDistance =
            mw::battle::battleOriginalAiOrientedLane(0, 2048, 0, 800u);
        const auto phase13LaneStrictWidth =
            mw::battle::battleOriginalAiOrientedLane(0, 1000, 0, 0u);
        const auto phase13LaneHeadingInside =
            mw::battle::battleOriginalAiOrientedLane(
                0, 1000, static_cast<int16_t>(-0x3ffb), 800u);
        const auto phase13LaneHeadingBoundary =
            mw::battle::battleOriginalAiOrientedLane(
                0, 1000, static_cast<int16_t>(-0x3ffc), 800u);
        expect(
            phase13LaneCenter.exact &&
                phase13LaneCenter.approximateDistance == 2047u &&
                phase13LaneCenter.strictDistancePassed &&
                phase13LaneCenter.candidateHeading == 0 &&
                phase13LaneCenter.headingDelta == 0 &&
                phase13LaneCenter.strictHeadingPassed &&
                phase13LaneCenter.lateralProjection == 0 &&
                phase13LaneCenter.strictLateralWidthPassed &&
                phase13LaneCenter.insideLane &&
                !phase13LaneStrictDistance.strictDistancePassed &&
                !phase13LaneStrictDistance.insideLane &&
                phase13LaneStrictWidth.absoluteLateralProjection == 0u &&
                !phase13LaneStrictWidth.strictLateralWidthPassed &&
                !phase13LaneStrictWidth.insideLane &&
                phase13LaneHeadingInside.absoluteHeadingDelta == 0x3ffbu &&
                phase13LaneHeadingInside.strictHeadingPassed &&
                phase13LaneHeadingBoundary.absoluteHeadingDelta == 0x3ffcu &&
                !phase13LaneHeadingBoundary.strictHeadingPassed,
            "BTECH 9700/9a77 oriented lane strict gates changed");

        const auto phase13RawWorldNorth =
            mw::battle::battleOriginalAiRawWorldPoseBridge(
                1000.0, 2000.0, 512.0, 0, 0, 72);
        const auto phase13RawWorldTurned =
            mw::battle::battleOriginalAiRawWorldPoseBridge(
                1000.0, 2000.0, 512.0, 0,
                static_cast<int16_t>(0x4000), 72);
        const auto phase13RawWorldDefaultScale =
            mw::battle::battleOriginalAiRawWorldPoseBridge(
                1000.0, 2000.0, 500.0, 0, 0, 72);
        expect(
            phase13RawWorldNorth.exactCoordinateMapping &&
                phase13RawWorldNorth.originalUpdateSecondsProvisional &&
                phase13RawWorldNorth.rawDeltaX == 0 &&
                phase13RawWorldNorth.rawDeltaZ == 72 &&
                phase13RawWorldNorth.worldDeltaX == 0.0 &&
                phase13RawWorldNorth.worldDeltaZ == -72.0 &&
                phase13RawWorldNorth.worldXAfter == 1000.0 &&
                phase13RawWorldNorth.worldZAfter == 1928.0 &&
                std::abs(phase13RawWorldNorth.worldHeadingRadiansAfter -
                         3.14159265358979323846) < 1.0e-12 &&
                phase13RawWorldNorth.worldForwardSpeedPerSecond == 720.0 &&
                phase13RawWorldTurned.rawHeadingAfter == 0x4000 &&
                phase13RawWorldTurned.rawDeltaX == -72 &&
                phase13RawWorldTurned.rawDeltaZ == 0 &&
                phase13RawWorldTurned.worldDeltaX == -72.0 &&
                phase13RawWorldTurned.worldDeltaZ == 0.0 &&
                std::abs(phase13RawWorldTurned.worldHeadingRadiansAfter +
                         1.57079632679489661923) < 1.0e-12 &&
                std::abs(phase13RawWorldDefaultScale.worldDeltaZ + 70.3125) <
                    1.0e-12 &&
                std::abs(
                    phase13RawWorldDefaultScale.worldForwardSpeedPerSecond -
                    703.125) < 1.0e-12,
            "BTECH c5ac raw-to-battle-world coordinate bridge changed");

        const auto phase13MovementTargetEmpty =
            mw::battle::battleOriginalAiSingleMovementTarget(0, 200, {});
        std::vector<
            mw::battle::BattleOriginalAiMovementCandidateDiagnosticInput>
            phase13MovementCandidates{
                {{101u}, false, 1, 2, 100, 0, 1000, 2000, 3000},
                {{102u}, true, 0, 2, 100, 0, 1100, 2100, 3100},
                {{103u}, true, 1, 0, 100, 0, 1200, 2200, 3200},
                {{104u}, true, 1, 2, 0, 0, 1300, 2300, 3300},
                {{105u}, true, 1, 2, 150, 0, 1400, 2400, 3400},
            };
        const auto phase13MovementTargetSelected =
            mw::battle::battleOriginalAiSingleMovementTarget(
                0, 200, phase13MovementCandidates);
        phase13MovementCandidates.back().cachedPoseStateWord2c = 1;
        const auto phase13MovementTargetStaleCache =
            mw::battle::battleOriginalAiSingleMovementTarget(
                0, 200, phase13MovementCandidates);
        phase13MovementCandidates.back().aggregateWord16 = 299;
        const auto phase13MovementTargetMinus99Accepted =
            mw::battle::battleOriginalAiSingleMovementTarget(
                0, 200, phase13MovementCandidates);
        phase13MovementCandidates.back().aggregateWord16 = 300;
        const auto phase13MovementTargetScoreRejected =
            mw::battle::battleOriginalAiSingleMovementTarget(
                0, 200, phase13MovementCandidates);
        phase13MovementCandidates.back().aggregateWord16 = 150;
        phase13MovementCandidates.back().priorSelectionCountWord1c = 1;
        const auto phase13MovementTargetPriorCountOpen =
            mw::battle::battleOriginalAiSingleMovementTarget(
                0, 200, phase13MovementCandidates);
        phase13MovementCandidates.back().priorSelectionCountWord1c = 0;
        phase13MovementCandidates.push_back(
            {{106u}, true, 1, 2, 125, 0, 1500, 2500, 3500});
        const auto phase13MovementTargetMultipleOpen =
            mw::battle::battleOriginalAiSingleMovementTarget(
                0, 200, phase13MovementCandidates);
        phase13MovementCandidates.push_back({});
        phase13MovementCandidates.push_back({});
        phase13MovementCandidates.push_back({});
        const auto phase13MovementTargetTooManySlots =
            mw::battle::battleOriginalAiSingleMovementTarget(
                0, 200, phase13MovementCandidates);
        expect(
            phase13MovementTargetEmpty.exact &&
                phase13MovementTargetEmpty.slotContractValid &&
                phase13MovementTargetEmpty.eligibleCandidateCount == 0u &&
                phase13MovementTargetEmpty.writesState46Zero &&
                !phase13MovementTargetEmpty.selected &&
                phase13MovementTargetSelected.exact &&
                phase13MovementTargetSelected.eligibleCandidateCount == 1u &&
                phase13MovementTargetSelected.aggregateDeltaWord == 50 &&
                phase13MovementTargetSelected.score == 128 &&
                !phase13MovementTargetSelected.scoreHalvedByStateWord2c &&
                phase13MovementTargetSelected.selected &&
                phase13MovementTargetSelected.selectedSlot == 4u &&
                phase13MovementTargetSelected.selectedEntityId.value == 105u &&
                phase13MovementTargetSelected.selectedCachedRawX == 1400 &&
                phase13MovementTargetSelected.selectedCachedRawY == 2400 &&
                phase13MovementTargetSelected.selectedCachedRawZ == 3400 &&
                phase13MovementTargetSelected.incrementsSelectedWord1c &&
                phase13MovementTargetStaleCache.exact &&
                phase13MovementTargetStaleCache.score == 64 &&
                phase13MovementTargetStaleCache.scoreHalvedByStateWord2c &&
                phase13MovementTargetMinus99Accepted.exact &&
                phase13MovementTargetMinus99Accepted.aggregateDeltaWord == -99 &&
                phase13MovementTargetMinus99Accepted.selected &&
                phase13MovementTargetMinus99Accepted.score == 64 &&
                phase13MovementTargetScoreRejected.exact &&
                phase13MovementTargetScoreRejected.aggregateDeltaWord == -100 &&
                phase13MovementTargetScoreRejected.rejectedByMinus99Gate &&
                phase13MovementTargetScoreRejected.writesState46One &&
                !phase13MovementTargetScoreRejected.selected &&
                !phase13MovementTargetPriorCountOpen.exact &&
                phase13MovementTargetPriorCountOpen.priorSelectionCountOpen &&
                !phase13MovementTargetMultipleOpen.exact &&
                phase13MovementTargetMultipleOpen.multipleCandidatesOpen &&
                phase13MovementTargetMultipleOpen.eligibleCandidateCount == 2u &&
                !phase13MovementTargetTooManySlots.slotContractValid &&
                !phase13MovementTargetTooManySlots.exact,
            "BTECH 27fb/23bf/7c3c single movement target changed");

        std::vector<std::filesystem::path> phase13SceneWorldPaths;
        for (const std::string& tileName : finalA.terrain.tileNames) {
            phase13SceneWorldPaths.push_back(
                std::filesystem::path("Sorted Original Files/WLD") /
                (tileName + ".WLD"));
        }
        const mw::battle::BattleOriginalTerrainSceneCatalog phase13SceneCatalog =
            mw::battle::loadOriginalBattleTerrainSceneCatalog(
                "Sorted Original Files/GI/TERPCK.GI",
                "Sorted Original Files/TBL/viewer8/TERPCK.TBL",
                phase13SceneWorldPaths);
        expect(phase13SceneCatalog.valid &&
                   phase13SceneCatalog.sourceWorldCount == 4u &&
                   phase13SceneCatalog.collisionRecords.size() == 30u &&
                   phase13SceneCatalog.recordScaleShifts.size() == 30u &&
                   phase13SceneCatalog.objects.size() == 14u &&
                   phase13SceneCatalog.queryObjectIndices.size() == 14u,
               "BTECH 242c/afd5/b0af/1e78 owned scene catalog changed");

        std::optional<mw::battle::BattleOriginalTerrainSceneQueryDiagnostic>
            phase13ComposedSceneHit;
        int32_t phase13ComposedQueryX = 0;
        int32_t phase13ComposedQueryZ = 0;
        for (size_t objectIndex : phase13SceneCatalog.queryObjectIndices) {
            const auto& object = phase13SceneCatalog.objects[objectIndex];
            const auto query = mw::battle::battleOriginalTerrainSceneQuery(
                phase13SceneCatalog,
                object.rawX,
                object.rawZ,
                0u,
                0u,
                0u);
            if (query.exact && query.hit && query.selectedPriority == 0u) {
                phase13ComposedSceneHit = query;
                phase13ComposedQueryX = object.rawX;
                phase13ComposedQueryZ = object.rawZ;
                break;
            }
        }
        expect(phase13ComposedSceneHit.has_value(),
               "composed D5CE/D3E6/D330/c42e query found no priority-zero center hit");
        const auto& phase13ComposedHit = *phase13ComposedSceneHit;
        const auto phase13ComposedCachedHit =
            mw::battle::battleOriginalTerrainSceneQuery(
                phase13SceneCatalog,
                phase13ComposedQueryX,
                phase13ComposedQueryZ,
                0u,
                0u,
                0u,
                phase13ComposedHit.selectedSceneObjectIndex,
                phase13ComposedHit.selectedSubrecordIndex);
        const auto phase13InvalidHalfCache =
            mw::battle::battleOriginalTerrainSceneQuery(
                phase13SceneCatalog,
                0,
                0,
                0u,
                0u,
                0u,
                phase13ComposedHit.selectedSceneObjectIndex,
                std::nullopt);
        expect(phase13ComposedHit.exact &&
                   phase13ComposedHit.selectedRecordIndex < 30u &&
                   phase13ComposedHit.selectedSubrecordIndex != 0xffu &&
                   phase13ComposedCachedHit.exact &&
                   phase13ComposedCachedHit.cachedObjectTested &&
                   phase13ComposedCachedHit.cachedObjectAccepted &&
                   phase13ComposedCachedHit.hit &&
                   phase13ComposedCachedHit.selectedSceneObjectIndex ==
                       phase13ComposedHit.selectedSceneObjectIndex &&
                   !phase13InvalidHalfCache.exact &&
                   !phase13InvalidHalfCache.cacheContractValid,
               "composed original terrain query cache or fail-closed contract changed");

        mw::battle::BattleStartParams phase13OwnedSceneParams = params;
        phase13OwnedSceneParams.originalTerrainSceneCatalog = phase13SceneCatalog;
        const mw::battle::BattleSnapshot phase13OwnedSceneA =
            mw::battle::runBattleReplay(phase13OwnedSceneParams, {}, 0u);
        const mw::battle::BattleSnapshot phase13OwnedSceneB =
            mw::battle::runBattleReplay(phase13OwnedSceneParams, {}, 0u);
        expect(phase13OwnedSceneA.terrain.originalTerrainSceneValid &&
                   phase13OwnedSceneA.terrain.originalTerrainCollisionRecordCount == 30u &&
                   phase13OwnedSceneA.terrain.originalTerrainSceneObjectCount == 14u &&
                   phase13OwnedSceneA.terrain.originalTerrainQueryObjectCount == 14u &&
                   phase13OwnedSceneA.terrain.originalTerrainSceneFingerprint != 0u &&
                   mw::battle::battleSnapshotFingerprint(phase13OwnedSceneA) ==
                       mw::battle::battleSnapshotFingerprint(phase13OwnedSceneB),
               "BattleWorld did not own the deterministic original scene catalog");
        mw::battle::BattleStartParams phase13MutatedSceneParams =
            phase13OwnedSceneParams;
        ++phase13MutatedSceneParams.originalTerrainSceneCatalog.objects.front().rawX;
        const mw::battle::BattleSnapshot phase13MutatedScene =
            mw::battle::runBattleReplay(phase13MutatedSceneParams, {}, 0u);
        expect(phase13OwnedSceneA.terrain.originalTerrainSceneFingerprint !=
                   phase13MutatedScene.terrain.originalTerrainSceneFingerprint &&
                   mw::battle::battleSnapshotFingerprint(phase13OwnedSceneA) !=
                       mw::battle::battleSnapshotFingerprint(phase13MutatedScene),
               "original scene catalog mutation did not affect replay fingerprint");

        const auto phase13FullPosePlanar =
            mw::battle::battleOriginalAiPoseStep(
                10, 20, 30, 0, 0, 0, 0, 0, 0, 100, 1, 2, 3);
        const auto phase13FullPoseVertical =
            mw::battle::battleOriginalAiPoseStep(
                0, 0, 0, 0, 0, 0, 0x4000, 0, 0, 100, 0, 0, 0);
        const auto phase13FullPoseRollOnly =
            mw::battle::battleOriginalAiPoseStep(
                0, 0, 0, 0, 0, 0, 0, 0x2000, 0, 100, 0, 0, 0);
        expect(phase13FullPosePlanar.exact &&
                   phase13FullPosePlanar.xAfter == 11 &&
                   phase13FullPosePlanar.yAfter == 23 &&
                   phase13FullPosePlanar.zAfter == 132 &&
                   phase13FullPoseVertical.pitchAfter == 0x4000 &&
                   phase13FullPoseVertical.speedDeltaX == 0 &&
                   phase13FullPoseVertical.speedDeltaY == 100 &&
                   phase13FullPoseVertical.speedDeltaZ == 0 &&
                   phase13FullPoseRollOnly.rollAfter == 0x2000 &&
                   phase13FullPoseRollOnly.speedDeltaY == 0 &&
                   phase13FullPoseRollOnly.speedDeltaZ == 100,
               "BTECH c5ac full Q14 pose integration changed");

        mw::battle::BattleOriginalTerrainSceneCatalog phase13EmptySceneCatalog =
            phase13SceneCatalog;
        phase13EmptySceneCatalog.objects.clear();
        phase13EmptySceneCatalog.queryObjectIndices.clear();

        const auto phase13WorldTransformFromRaw = [](
                                                    int32_t rawX,
                                                    int32_t rawZ) {
            constexpr double cellSize = 500.0;
            constexpr double rawCellSize = 512.0;
            mw::battle::Transform transform;
            transform.x =
                (static_cast<double>(rawX) + 44160.0) *
                cellSize / rawCellSize;
            transform.z =
                (24000.0 - static_cast<double>(rawZ)) *
                cellSize / rawCellSize;
            return transform;
        };
        const mw::battle::OriginalBattlefieldSetup phase13LiveSourceSetup =
            mw::battle::decodeOriginalBattlefieldSetup(
                options.snarioPath, options.scenarioIndex, 7u, 500.0);
        std::vector<std::filesystem::path> phase13LiveGridPaths;
        for (const std::string& tileName :
             phase13LiveSourceSetup.tileNames) {
            phase13LiveGridPaths.push_back(
                std::filesystem::path("Sorted Original Files/GRD") /
                (tileName + ".GRD"));
        }
        const auto phase13LiveOwnedGrid =
            mw::battle::loadOriginalBattleTerrainCollisionGrid(
                phase13LiveGridPaths, 500.0);
        expect(
            phase13LiveOwnedGrid.valid &&
                phase13LiveOwnedGrid.width == 174 &&
                phase13LiveOwnedGrid.height == 94 &&
                phase13LiveOwnedGrid.rawSamples.size() == 174u * 94u,
            "campaign-owned original four-tile AI GRD binding changed");
        mw::battle::BattleStartParams phase13LiveMovementParams = params;
        phase13LiveMovementParams.mission =
            mw::battle::battleMissionBriefingFromDefinition(
                *mw::battle::battleMissionDefinitionById(7));
        phase13LiveMovementParams.setupMetadata =
            phase13LiveSourceSetup.metadata;
        phase13LiveMovementParams.objective = {};
        phase13LiveMovementParams.battlefieldBoundary = {};
        phase13LiveMovementParams.playerLaunchState.reset();
        phase13LiveMovementParams.playerAlliedLaunchStates.clear();
        phase13LiveMovementParams.combatantLaunchStates.clear();
        phase13LiveMovementParams.playerMechPresetId = "locust";
        phase13LiveMovementParams.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::
                OriginalBtechModeZeroFirstMovementUpdate;
        phase13LiveMovementParams.deterministicCombatRuntimeEnabled = true;
        phase13LiveMovementParams.individualWeaponRuntimeEnabled = true;
        phase13LiveMovementParams.originalProjectileRuntimeEnabled = true;
        phase13LiveMovementParams.originalHeatRuntimeEnabled = true;
        phase13LiveMovementParams.originalMajorSystemRuntimeEnabled = true;
        phase13LiveMovementParams.mechSystemsSnapshotEnabled = true;
        phase13LiveMovementParams.weaponTargetPolicy =
            mw::battle::BattleWeaponTargetPolicy::UnresolvedFailClosed;
        phase13LiveMovementParams.terrainCollisionGrid = phase13ProbeGrid;
        phase13LiveMovementParams.originalTerrainSceneCatalog =
            phase13EmptySceneCatalog;
        phase13LiveMovementParams.deterministicCollisionRuntimeEnabled = false;

        auto& phase13Slot0 =
            phase13LiveMovementParams.setupMetadata.playerSlots[0];
        auto& phase13Slot1 =
            phase13LiveMovementParams.setupMetadata.playerSlots[1];
        auto& phase13Slot4 =
            phase13LiveMovementParams.setupMetadata.opposingSlots[0];
        phase13Slot0.originalRawX = -10000;
        phase13Slot0.originalRawZ = 0;
        phase13Slot0.transform = phase13WorldTransformFromRaw(-10000, 0);
        phase13Slot1.originalRawX = 0;
        phase13Slot1.originalRawZ = 0;
        phase13Slot1.transform = phase13WorldTransformFromRaw(0, 0);
        phase13Slot4.originalRawX = 400;
        phase13Slot4.originalRawZ = 0;
        phase13Slot4.transform = phase13WorldTransformFromRaw(400, 0);
        phase13LiveMovementParams.playerStartTransform =
            phase13Slot0.transform;

        mw::battle::BattleCombatantLaunchState phase13LiveAlly;
        phase13LiveAlly.mechPresetId = "locust";
        phase13LiveAlly.startTransform = phase13Slot1.transform;
        phase13LiveAlly.roster.team = mw::battle::BattleTeam::Player;
        phase13LiveAlly.roster.sourceSlot = "player:1";
        phase13LiveAlly.roster.provenance =
            "phase13_synthetic_exact_coordinate_fixture";
        phase13LiveMovementParams.playerAlliedLaunchStates.push_back(
            phase13LiveAlly);
        mw::battle::BattleCombatantLaunchState phase13LiveTarget;
        phase13LiveTarget.mechPresetId = "locust";
        phase13LiveTarget.startTransform = phase13Slot4.transform;
        phase13LiveTarget.roster.team = mw::battle::BattleTeam::Opposing;
        phase13LiveTarget.roster.sourceSlot = "opposing:0";
        phase13LiveTarget.roster.provenance =
            "phase13_synthetic_exact_coordinate_fixture";
        phase13LiveMovementParams.combatantLaunchStates.push_back(
            phase13LiveTarget);

        const auto phase13LiveAllyFrom = [](
                                              const mw::battle::BattleSnapshot& snapshot)
            -> const mw::battle::CombatantSnapshot& {
            const auto found = std::find_if(
                snapshot.combatants.begin(),
                snapshot.combatants.end(),
                [](const mw::battle::CombatantSnapshot& combatant) {
                    return combatant.roster.originalLiveObjectSlot ==
                        std::optional<uint8_t>{static_cast<uint8_t>(1u)};
                });
            if (found == snapshot.combatants.end()) {
                throw std::runtime_error(
                    "phase13 live movement lost player-side AI slot 1");
            }
            return *found;
        };

        mw::battle::BattleWorld phase13LiveWorld =
            mw::battle::BattleWorld::create(phase13LiveMovementParams);
        const auto phase13LiveStart = phase13LiveWorld.missionStartSnapshot();
        const auto& phase13LiveStartAlly =
            phase13LiveAllyFrom(phase13LiveStart);
        phase13LiveWorld.tick();
        const auto phase13LiveFirst = phase13LiveWorld.snapshot();
        const auto& phase13LiveFirstAlly =
            phase13LiveAllyFrom(phase13LiveFirst);
        expect(
            phase13LiveStartAlly.originalAiMotion.initialized &&
                !phase13LiveStartAlly.originalAiMotion.
                    firstMovementUpdateAttempted &&
                phase13LiveFirstAlly.originalAiMotion.
                    firstMovementUpdateAttempted &&
                phase13LiveFirstAlly.originalAiMotion.
                    firstMovementUpdateCommitted &&
                phase13LiveFirstAlly.originalAiMotion.
                    firstMovementUpdateResult ==
                    mw::battle::BattleOriginalAiFirstMovementUpdateResult::
                        Committed &&
                phase13LiveFirstAlly.originalAiMotion.
                    firstMovementTargetEntityId.value == 3u &&
                phase13LiveFirstAlly.originalAiMotion.
                    firstMovementTargetLiveObjectSlot == 4u &&
                phase13LiveFirstAlly.originalAiMotion.
                    firstMovementRelationWord == 1u &&
                !phase13LiveFirstAlly.originalAiMotion.
                    firstMovementRelationSampled &&
                phase13LiveFirstAlly.originalAiMotion.
                    firstMovementRelationSampleCount == 0u &&
                phase13LiveFirstAlly.originalAiMotion.
                    acceptedPoseCommitCount == 1u &&
                phase13LiveFirstAlly.originalAiMotion.rawSpeedWord3c == 5 &&
                (phase13LiveFirstAlly.originalAiMotion.rawX != 0 ||
                 phase13LiveFirstAlly.originalAiMotion.rawZ != 0) &&
                phase13LiveFirstAlly.transform.x ==
                    phase13WorldTransformFromRaw(
                        phase13LiveFirstAlly.originalAiMotion.rawX,
                        phase13LiveFirstAlly.originalAiMotion.rawZ).x &&
                phase13LiveFirstAlly.transform.z ==
                    phase13WorldTransformFromRaw(
                        phase13LiveFirstAlly.originalAiMotion.rawX,
                        phase13LiveFirstAlly.originalAiMotion.rawZ).z &&
                phase13LiveFirstAlly.throttle == 0.0 &&
                phase13LiveFirstAlly.turn == 0.0 &&
                phase13LiveFirstAlly.forwardSpeed == 0.0,
            "first live BTECH mode-zero movement update did not commit atomically");
        phase13LiveWorld.tick();
        const auto phase13LiveSecond = phase13LiveWorld.snapshot();
        const auto& phase13LiveSecondAlly =
            phase13LiveAllyFrom(phase13LiveSecond);
        expect(
            phase13LiveSecondAlly.originalAiUpdateCount == 1u &&
                phase13LiveSecondAlly.originalAiMotion.
                    acceptedPoseCommitCount == 1u &&
                phase13LiveSecondAlly.originalAiMotion.rawX ==
                    phase13LiveFirstAlly.originalAiMotion.rawX &&
                phase13LiveSecondAlly.originalAiMotion.rawZ ==
                    phase13LiveFirstAlly.originalAiMotion.rawZ,
            "first-update policy repeated across later fixed ticks");

        const auto phase13LiveReplay = mw::battle::runBattleReplay(
            phase13LiveMovementParams, {}, 2u);
        expect(
            mw::battle::battleSnapshotFingerprint(phase13LiveSecond) ==
                mw::battle::battleSnapshotFingerprint(phase13LiveReplay),
            "first live movement update changed under replay");

        mw::battle::BattleStartParams phase13HalfTickParams =
            phase13LiveMovementParams;
        phase13HalfTickParams.fixedTickSeconds = 0.05;
        mw::battle::BattleWorld phase13HalfTickWorld =
            mw::battle::BattleWorld::create(phase13HalfTickParams);
        phase13HalfTickWorld.tick();
        const auto phase13HalfTickFirst = phase13HalfTickWorld.snapshot();
        expect(
            !phase13LiveAllyFrom(phase13HalfTickFirst).originalAiMotion.
                firstMovementUpdateAttempted,
            "provisional 0.1-second bridge dispatched on a half tick");
        phase13HalfTickWorld.tick();
        const auto phase13HalfTickSecond = phase13HalfTickWorld.snapshot();
        const auto& phase13HalfTickAlly =
            phase13LiveAllyFrom(phase13HalfTickSecond);
        expect(
            phase13HalfTickAlly.originalAiMotion.
                firstMovementUpdateCommitted &&
                phase13HalfTickAlly.originalAiMotion.rawX ==
                    phase13LiveFirstAlly.originalAiMotion.rawX &&
                phase13HalfTickAlly.originalAiMotion.rawY ==
                    phase13LiveFirstAlly.originalAiMotion.rawY &&
                phase13HalfTickAlly.originalAiMotion.rawZ ==
                    phase13LiveFirstAlly.originalAiMotion.rawZ &&
                phase13HalfTickAlly.originalAiMotion.heading ==
                    phase13LiveFirstAlly.originalAiMotion.heading,
            "first original movement result changed with fixed-tick subdivision");
        auto phase13LiveFingerprintMutation = phase13LiveSecond;
        ++phase13LiveFingerprintMutation.combatants[1].originalAiMotion.
            acceptedPoseCommitCount;
        expect(
            mw::battle::battleSnapshotFingerprint(phase13LiveSecond) !=
                mw::battle::battleSnapshotFingerprint(
                    phase13LiveFingerprintMutation),
            "first live movement result is missing from replay fingerprint");

        mw::battle::BattleStartParams phase13FarRelationParams =
            phase13LiveMovementParams;
        auto& phase13FarSlot4 =
            phase13FarRelationParams.setupMetadata.opposingSlots[0];
        phase13FarSlot4.originalRawX = 512;
        phase13FarSlot4.transform = phase13WorldTransformFromRaw(512, 0);
        phase13FarRelationParams.combatantLaunchStates[0].startTransform =
            phase13FarSlot4.transform;
        const auto phase13FarRelation = mw::battle::runBattleReplay(
            phase13FarRelationParams, {}, 1u);
        const auto& phase13FarRelationAlly =
            phase13LiveAllyFrom(phase13FarRelation);
        expect(
            phase13FarRelationAlly.originalAiMotion.
                firstMovementUpdateAttempted &&
                phase13FarRelationAlly.originalAiMotion.
                    firstMovementUpdateCommitted &&
                phase13FarRelationAlly.originalAiMotion.
                    firstMovementUpdateResult ==
                    mw::battle::BattleOriginalAiFirstMovementUpdateResult::
                        Committed &&
                phase13FarRelationAlly.originalAiMotion.
                    firstMovementRelationSampled &&
                phase13FarRelationAlly.originalAiMotion.
                    firstMovementRelationSampleCount == 1u &&
                phase13FarRelationAlly.originalAiMotion.
                    firstMovementRelationWord == 1u &&
                phase13FarRelationAlly.originalAiMotion.
                    acceptedPoseCommitCount == 1u,
            "sampled 5206 relation did not enter the first live movement update");

        auto phase13SampledHalfTickParams = phase13FarRelationParams;
        phase13SampledHalfTickParams.fixedTickSeconds = 0.05;
        const auto phase13SampledHalfTick = mw::battle::runBattleReplay(
            phase13SampledHalfTickParams, {}, 2u);
        const auto& phase13SampledHalfTickAlly =
            phase13LiveAllyFrom(phase13SampledHalfTick);
        expect(
            phase13SampledHalfTickAlly.originalAiMotion.
                firstMovementUpdateCommitted &&
                phase13SampledHalfTickAlly.originalAiMotion.
                    firstMovementRelationSampled &&
                phase13SampledHalfTickAlly.originalAiMotion.rawX ==
                    phase13FarRelationAlly.originalAiMotion.rawX &&
                phase13SampledHalfTickAlly.originalAiMotion.rawY ==
                    phase13FarRelationAlly.originalAiMotion.rawY &&
                phase13SampledHalfTickAlly.originalAiMotion.rawZ ==
                    phase13FarRelationAlly.originalAiMotion.rawZ &&
                phase13SampledHalfTickAlly.originalAiMotion.heading ==
                    phase13FarRelationAlly.originalAiMotion.heading,
            "sampled 5206 live result changed with fixed-tick subdivision");

        mw::battle::BattleStartParams phase13BlockedRelationParams =
            phase13FarRelationParams;
        phase13BlockedRelationParams.terrainCollisionGrid.rawSamples[
            87u * 94u + 46u] = 19u;
        const auto phase13BlockedRelation = mw::battle::runBattleReplay(
            phase13BlockedRelationParams, {}, 1u);
        const auto& phase13BlockedRelationAlly =
            phase13LiveAllyFrom(phase13BlockedRelation);
        expect(
            phase13BlockedRelationAlly.originalAiMotion.
                firstMovementUpdateAttempted &&
                !phase13BlockedRelationAlly.originalAiMotion.
                    firstMovementUpdateCommitted &&
                phase13BlockedRelationAlly.originalAiMotion.
                    firstMovementUpdateResult ==
                    mw::battle::BattleOriginalAiFirstMovementUpdateResult::
                        RejectedRelationBlocked &&
                phase13BlockedRelationAlly.originalAiMotion.
                    firstMovementRelationSampled &&
                phase13BlockedRelationAlly.originalAiMotion.
                    firstMovementRelationSampleCount == 1u &&
                phase13BlockedRelationAlly.originalAiMotion.
                    firstMovementRelationWord == 0u &&
                phase13BlockedRelationAlly.originalAiMotion.
                    acceptedPoseCommitCount == 0u &&
                phase13BlockedRelationAlly.transform.x ==
                    phase13LiveStartAlly.transform.x &&
                phase13BlockedRelationAlly.transform.z ==
                    phase13LiveStartAlly.transform.z,
            "sampled 5206 terrain block did not reject before movement");

        auto phase13SampledFingerprintMutation = phase13FarRelation;
        ++phase13SampledFingerprintMutation.combatants[1].originalAiMotion.
            firstMovementRelationSampleCount;
        expect(
            mw::battle::battleSnapshotFingerprint(phase13FarRelation) !=
                mw::battle::battleSnapshotFingerprint(
                    phase13SampledFingerprintMutation),
            "sampled 5206 ownership is missing from replay fingerprint");

        mw::battle::BattleStartParams phase13MultipleTargetParams =
            phase13LiveMovementParams;
        auto phase13SecondTarget =
            phase13MultipleTargetParams.combatantLaunchStates.front();
        phase13SecondTarget.roster.sourceSlot = "opposing:1";
        phase13SecondTarget.startTransform =
            phase13MultipleTargetParams.setupMetadata.opposingSlots[1].
                transform;
        phase13MultipleTargetParams.combatantLaunchStates.push_back(
            phase13SecondTarget);
        const auto phase13MultipleTarget = mw::battle::runBattleReplay(
            phase13MultipleTargetParams, {}, 1u);
        const auto& phase13MultipleTargetAlly =
            phase13LiveAllyFrom(phase13MultipleTarget);
        expect(
            phase13MultipleTargetAlly.originalAiMotion.
                firstMovementUpdateAttempted &&
                !phase13MultipleTargetAlly.originalAiMotion.
                    firstMovementUpdateCommitted &&
                phase13MultipleTargetAlly.originalAiMotion.
                    firstMovementUpdateResult ==
                    mw::battle::BattleOriginalAiFirstMovementUpdateResult::
                        RejectedTargetCardinality,
            "multiple opposing targets did not fail closed before movement");

        const auto phase13B961Diagonal =
            mw::battle::battleOriginalAiApproximatePlanarDistanceB961(
                1000, 400);
        const auto phase13B961Special =
            mw::battle::battleOriginalAiApproximatePlanarDistanceB961(
                0x8000, 0);
        expect(
            phase13B961Diagonal.exact &&
                phase13B961Diagonal.maximumMagnitude == 1000u &&
                phase13B961Diagonal.minimumMagnitude == 400u &&
                phase13B961Diagonal.distance == 1150u &&
                phase13B961Special.exact &&
                phase13B961Special.xMagnitude == 0x7fffu &&
                phase13B961Special.distance == 0x7fffu,
            "BTECH b961 distance-matrix arithmetic changed");

        std::vector<mw::battle::BattleOriginalAiMovementCandidateDiagnosticInput>
            phase13GeneralSelectionCandidates(8u);
        auto& phase13GeneralCandidate4 =
            phase13GeneralSelectionCandidates[4];
        phase13GeneralCandidate4.entityId = {904u};
        phase13GeneralCandidate4.liveSlotOccupied = true;
        phase13GeneralCandidate4.sideWord = 1;
        phase13GeneralCandidate4.cachedPoseStateWord2c = 2;
        phase13GeneralCandidate4.aggregateWord16 = 900;
        phase13GeneralCandidate4.distanceMatrixValueExact = true;
        phase13GeneralCandidate4.approximateDistance = 1000u;
        phase13GeneralCandidate4.cachedRawX = 1000;
        auto& phase13GeneralCandidate5 =
            phase13GeneralSelectionCandidates[5];
        phase13GeneralCandidate5.entityId = {905u};
        phase13GeneralCandidate5.liveSlotOccupied = true;
        phase13GeneralCandidate5.sideWord = 1;
        phase13GeneralCandidate5.cachedPoseStateWord2c = 2;
        phase13GeneralCandidate5.aggregateWord16 = 800;
        phase13GeneralCandidate5.distanceMatrixValueExact = true;
        phase13GeneralCandidate5.approximateDistance = 2000u;
        phase13GeneralCandidate5.cachedRawX = 2000;
        auto& phase13GeneralCandidate6 =
            phase13GeneralSelectionCandidates[6];
        phase13GeneralCandidate6.entityId = {906u};
        phase13GeneralCandidate6.liveSlotOccupied = true;
        phase13GeneralCandidate6.sideWord = 1;
        phase13GeneralCandidate6.cachedPoseStateWord2c = 2;
        phase13GeneralCandidate6.aggregateWord16 = 850;
        phase13GeneralCandidate6.distanceMatrixValueExact = true;
        phase13GeneralCandidate6.approximateDistance = 1500u;
        phase13GeneralCandidate6.cachedRawX = 1500;
        const auto phase13GeneralSelection =
            mw::battle::battleOriginalAiMovementTargetSelection23bf(
                0, 1000, phase13GeneralSelectionCandidates);
        expect(
            phase13GeneralSelection.exact &&
                phase13GeneralSelection.eligibleCandidateCount == 3u &&
                phase13GeneralSelection.maximumDistanceLowWord == 2000u &&
                phase13GeneralSelection.maximumAggregateMagnitudeWord == 200 &&
                phase13GeneralSelection.candidateScores[4].score == 136 &&
                phase13GeneralSelection.candidateScores[5].score == 128 &&
                phase13GeneralSelection.candidateScores[6].score == 131 &&
                phase13GeneralSelection.selected &&
                phase13GeneralSelection.selectedSlot == 4u &&
                phase13GeneralSelection.selectedEntityId.value == 904u &&
                phase13GeneralSelection.bestScore == 136 &&
                phase13GeneralSelection.selectedCountBefore == 0 &&
                phase13GeneralSelection.selectedCountAfter == 1 &&
                phase13GeneralSelection.incrementsSelectedWord1c &&
                phase13GeneralSelection.strictEarlierSlotTie,
            "BTECH 23bf general three-candidate score changed");

        std::vector<mw::battle::BattleOriginalAiMovementCandidateDiagnosticInput>
            phase13OpenRelationCandidates(8u);
        auto& phase13OpenPlayer = phase13OpenRelationCandidates[0];
        phase13OpenPlayer.entityId = {900u};
        phase13OpenPlayer.liveSlotOccupied = true;
        phase13OpenPlayer.sideWord = 0;
        phase13OpenPlayer.aggregateWord16 = 1100;
        phase13OpenPlayer.distanceMatrixValueExact = true;
        phase13OpenPlayer.approximateDistance = 2000u;
        phase13OpenPlayer.cachedRawX = -1000;
        auto& phase13StableCompanion = phase13OpenRelationCandidates[1];
        phase13StableCompanion.entityId = {901u};
        phase13StableCompanion.liveSlotOccupied = true;
        phase13StableCompanion.sideWord = 0;
        phase13StableCompanion.cachedPoseStateWord2c = 2;
        phase13StableCompanion.aggregateWord16 = 900;
        phase13StableCompanion.distanceMatrixValueExact = true;
        phase13StableCompanion.approximateDistance = 1000u;
        phase13StableCompanion.cachedRawX = 1000;
        const auto phase13OpenRelationIndependent =
            mw::battle::
                battleOriginalAiMovementTargetSelection23bfWithOpenRelation(
                    1,
                    1000,
                    phase13OpenRelationCandidates,
                    0u,
                    2);
        auto phase13OpenRelationDependentInputs =
            phase13OpenRelationCandidates;
        phase13OpenRelationDependentInputs[0].aggregateWord16 = 900;
        phase13OpenRelationDependentInputs[0].approximateDistance = 1000u;
        phase13OpenRelationDependentInputs[1].aggregateWord16 = 900;
        phase13OpenRelationDependentInputs[1].approximateDistance = 1000u;
        const auto phase13OpenRelationDependent =
            mw::battle::
                battleOriginalAiMovementTargetSelection23bfWithOpenRelation(
                    1,
                    1000,
                    phase13OpenRelationDependentInputs,
                    0u,
                    2);
        const auto phase13OpenRelationInvalid =
            mw::battle::
                battleOriginalAiMovementTargetSelection23bfWithOpenRelation(
                    1,
                    1000,
                    phase13OpenRelationCandidates,
                    0u,
                    0);
        expect(
            phase13OpenRelationIndependent.exact &&
                phase13OpenRelationIndependent.openSlotValid &&
                phase13OpenRelationIndependent.openSlot == 0u &&
                phase13OpenRelationIndependent.eligibleStateWord2c == 2 &&
                phase13OpenRelationIndependent.relationZero.selected &&
                phase13OpenRelationIndependent.relationNonzero.selected &&
                phase13OpenRelationIndependent.relationZero.selectedSlot ==
                    1u &&
                phase13OpenRelationIndependent.relationNonzero.selectedSlot ==
                    1u &&
                phase13OpenRelationIndependent.targetSelectionIndependent &&
                phase13OpenRelationIndependent.selectedSlot == 1u &&
                phase13OpenRelationIndependent.selectedEntityId.value ==
                    901u &&
                phase13OpenRelationIndependent.selectedCachedRawX == 1000 &&
                phase13OpenRelationIndependent.selectedCountAfter == 1 &&
                phase13OpenRelationDependent.exact &&
                !phase13OpenRelationDependent.
                    targetSelectionIndependent &&
                phase13OpenRelationDependent.relationZero.selectedSlot == 1u &&
                phase13OpenRelationDependent.relationNonzero.selectedSlot ==
                    0u &&
                !phase13OpenRelationInvalid.exact &&
                phase13OpenRelationInvalid.openSlotValid,
            "BTECH 23bf open-relation exhaustive selection boundary changed");

        const std::array<mw::battle::BattleOriginalAiOrderTargetInput, 6>
            phase13ExpectedOrderTargets{{
                mw::battle::BattleOriginalAiOrderTargetInput::None,
                mw::battle::BattleOriginalAiOrderTargetInput::MapLocation,
                mw::battle::BattleOriginalAiOrderTargetInput::MapLocation,
                mw::battle::BattleOriginalAiOrderTargetInput::LiveObjectOrBase,
                mw::battle::BattleOriginalAiOrderTargetInput::MapLocation,
                mw::battle::BattleOriginalAiOrderTargetInput::MapLocation,
            }};
        const std::array<std::string, 6> phase13ExpectedOrderLabels{{
            "ACT ON OWN",
            "AMBUSH",
            "DEFEND",
            "ATTACK ENEMY",
            "MOVE (ATTACK)",
            "MOVE (AVOID)",
        }};
        for (int16_t code = 0; code < 6; ++code) {
            const auto order =
                mw::battle::battleOriginalAiLanceOrder(code);
            expect(
                order.exact && order.selectable &&
                    static_cast<int16_t>(order.order) == code &&
                    order.targetInput ==
                        phase13ExpectedOrderTargets[static_cast<size_t>(code)] &&
                    std::string(order.label) ==
                        phase13ExpectedOrderLabels[static_cast<size_t>(code)],
                "BTECH command-screen order table/target dispatcher changed");
        }
        expect(
            !mw::battle::battleOriginalAiLanceOrder(-1).exact &&
                !mw::battle::battleOriginalAiLanceOrder(6).exact,
            "BTECH command-screen order domain no longer fails closed");

        auto phase13PenalizedCandidates = phase13GeneralSelectionCandidates;
        phase13PenalizedCandidates[4].priorSelectionCountWord1c = 1;
        const auto phase13PenalizedSelection =
            mw::battle::battleOriginalAiMovementTargetSelection23bf(
                0, 1000, phase13PenalizedCandidates);
        expect(
            phase13PenalizedSelection.exact &&
                phase13PenalizedSelection.candidateScores[4].
                    selectionPenalty == 128 &&
                phase13PenalizedSelection.candidateScores[4].score == -6 &&
                phase13PenalizedSelection.selectedSlot == 6u,
            "BTECH 23bf selection-count penalty changed");

        auto phase13TieCandidates = phase13GeneralSelectionCandidates;
        phase13TieCandidates[4].aggregateWord16 = 900;
        phase13TieCandidates[4].approximateDistance = 1000u;
        phase13TieCandidates[5].aggregateWord16 = 900;
        phase13TieCandidates[5].approximateDistance = 1000u;
        phase13TieCandidates[6].liveSlotOccupied = false;
        const auto phase13TieSelection =
            mw::battle::battleOriginalAiMovementTargetSelection23bf(
                0, 1000, phase13TieCandidates);
        phase13TieCandidates[4].cachedPoseStateWord2c = 1;
        const auto phase13CachedStateHalvedSelection =
            mw::battle::battleOriginalAiMovementTargetSelection23bf(
                0, 1000, phase13TieCandidates);
        expect(
            phase13TieSelection.exact &&
                phase13TieSelection.candidateScores[4].score == 128 &&
                phase13TieSelection.candidateScores[5].score == 128 &&
                phase13TieSelection.selectedSlot == 4u &&
                phase13CachedStateHalvedSelection.exact &&
                phase13CachedStateHalvedSelection.candidateScores[4].score ==
                    64 &&
                phase13CachedStateHalvedSelection.selectedSlot == 5u,
            "BTECH 23bf strict tie or cached-state halving changed");

        auto phase13Minus100Candidates = phase13TieCandidates;
        phase13Minus100Candidates[4].cachedPoseStateWord2c = 2;
        phase13Minus100Candidates[4].aggregateWord16 = 1100;
        phase13Minus100Candidates[5].liveSlotOccupied = false;
        const auto phase13Minus100Selection =
            mw::battle::battleOriginalAiMovementTargetSelection23bf(
                0, 1000, phase13Minus100Candidates);
        expect(
            phase13Minus100Selection.exact &&
                phase13Minus100Selection.candidateScores[4].
                    aggregateDifferenceWord == -100 &&
                phase13Minus100Selection.candidateScores[4].
                    rejectedByMinus100Gate &&
                !phase13Minus100Selection.selected &&
                phase13Minus100Selection.writesState46One,
            "BTECH 23bf strict minus-100 gate changed");

        auto phase13DistanceLowCandidates = phase13TieCandidates;
        phase13DistanceLowCandidates[4].cachedPoseStateWord2c = 2;
        phase13DistanceLowCandidates[4].approximateDistance = 40000u;
        phase13DistanceLowCandidates[5].approximateDistance = 1000u;
        const auto phase13DistanceLowSelection =
            mw::battle::battleOriginalAiMovementTargetSelection23bf(
                0, 1000, phase13DistanceLowCandidates);
        expect(
            phase13DistanceLowSelection.exact &&
                phase13DistanceLowSelection.maximumDistanceLowWord == 1000u &&
                phase13DistanceLowSelection.candidateScores[4].distanceTerm ==
                    207 &&
                phase13DistanceLowSelection.candidateScores[4].score == 335 &&
                phase13DistanceLowSelection.selectedSlot == 4u,
            "BTECH 23bf low-word distance wrap changed");

        const auto phase13PostStepExact =
            mw::battle::battleOriginalAiOrdinaryModeZeroPostStep9bfa(
                0, 0x10u, 0, 0);
        const auto phase13PostStepModeOpen =
            mw::battle::battleOriginalAiOrdinaryModeZeroPostStep9bfa(
                1, 0x10u, 0, 0);
        const auto phase13PostStepCommandOpen =
            mw::battle::battleOriginalAiOrdinaryModeZeroPostStep9bfa(
                0, 0x08u, 0, 0);
        const auto phase13PostStepStateOpen =
            mw::battle::battleOriginalAiOrdinaryModeZeroPostStep9bfa(
                0, 0x10u, 3, 0);
        const auto phase13PostStepFrameOpen =
            mw::battle::battleOriginalAiOrdinaryModeZeroPostStep9bfa(
                0, 0x10u, 0, 1);
        expect(
            phase13PostStepExact.exact &&
                phase13PostStepExact.selectedActionCode == 3u &&
                phase13PostStepExact.modeZeroFrameCount == 1u &&
                phase13PostStepExact.frameWord53AfterIncrement == 1 &&
                phase13PostStepExact.animationModeWord4cAfter == 0 &&
                phase13PostStepExact.commandByte50After == 0u &&
                phase13PostStepExact.sharedStateWord51After == 0 &&
                phase13PostStepExact.frameWord53After == 0 &&
                phase13PostStepExact.componentAnimationSideEffectsExcluded &&
                !phase13PostStepModeOpen.exact &&
                !phase13PostStepCommandOpen.exact &&
                !phase13PostStepStateOpen.exact &&
                !phase13PostStepFrameOpen.exact,
            "BTECH 9bfa ordinary mode-zero post-step boundary changed");

        mw::battle::BattleStartParams phase13RepeatedMovementParams =
            phase13FarRelationParams;
        auto& phase13RepeatedSlot4 =
            phase13RepeatedMovementParams.setupMetadata.opposingSlots[0];
        phase13RepeatedSlot4.originalRawX = 2000;
        phase13RepeatedSlot4.transform =
            phase13WorldTransformFromRaw(2000, 0);
        phase13RepeatedMovementParams.combatantLaunchStates[0].startTransform =
            phase13RepeatedSlot4.transform;
        phase13RepeatedMovementParams.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::
                OriginalBtechModeZeroRepeatedMovement;
        mw::battle::BattleWorld phase13RepeatedMovementWorld =
            mw::battle::BattleWorld::create(phase13RepeatedMovementParams);
        phase13RepeatedMovementWorld.runTicks(3u);
        const auto phase13RepeatedMovement =
            phase13RepeatedMovementWorld.snapshot();
        const auto& phase13RepeatedMovementAlly =
            phase13LiveAllyFrom(phase13RepeatedMovement);
        expect(
            phase13RepeatedMovementAlly.originalAiUpdateCount == 3u &&
                phase13RepeatedMovementAlly.originalAiMotion.
                    movementUpdateAttemptCount == 3u &&
                phase13RepeatedMovementAlly.originalAiMotion.
                    acceptedPoseCommitCount == 3u &&
                phase13RepeatedMovementAlly.originalAiMotion.
                    firstMovementUpdateAttempted &&
                phase13RepeatedMovementAlly.originalAiMotion.
                    firstMovementUpdateCommitted &&
                phase13RepeatedMovementAlly.originalAiMotion.
                    firstMovementUpdateResult ==
                    mw::battle::BattleOriginalAiFirstMovementUpdateResult::
                        Committed &&
                phase13RepeatedMovementAlly.originalAiMotion.
                    firstMovementDecisionTickIndex == 0u &&
                phase13RepeatedMovementAlly.originalAiMotion.
                    lastMovementUpdateResult ==
                    mw::battle::BattleOriginalAiFirstMovementUpdateResult::
                        Committed &&
                phase13RepeatedMovementAlly.originalAiMotion.
                    lastMovementTargetEntityId.value == 3u &&
                phase13RepeatedMovementAlly.originalAiMotion.
                    lastMovementTargetLiveObjectSlot == 4u &&
                phase13RepeatedMovementAlly.originalAiMotion.
                    lastMovementRelationWord == 1u &&
                phase13RepeatedMovementAlly.originalAiMotion.
                    lastMovementRelationSampled &&
                phase13RepeatedMovementAlly.originalAiMotion.
                    lastMovementRelationSampleCount > 0u &&
                phase13RepeatedMovementAlly.originalAiMotion.
                    lastMovementDecisionTickIndex == 2u &&
                phase13RepeatedMovementAlly.originalAiMotion.
                    lastMovementPostStepExact &&
                phase13RepeatedMovementAlly.originalAiMotion.
                    lastMovementPostStepActionCode == 3u &&
                phase13RepeatedMovementAlly.originalAiMotion.
                    rawSpeedWord3c == 15 &&
                phase13RepeatedMovementAlly.originalAiMotion.
                    controlRecordBytes[0x4cu] == 0u &&
                phase13RepeatedMovementAlly.originalAiMotion.
                    controlRecordBytes[0x4du] == 0u &&
                phase13RepeatedMovementAlly.originalAiMotion.
                    controlRecordBytes[0x50u] == 0u &&
                phase13RepeatedMovementAlly.originalAiMotion.
                    controlRecordBytes[0x51u] == 0u &&
                phase13RepeatedMovementAlly.originalAiMotion.
                    controlRecordBytes[0x52u] == 0u &&
                phase13RepeatedMovementAlly.originalAiMotion.
                    controlRecordBytes[0x53u] == 0u &&
                phase13RepeatedMovementAlly.originalAiMotion.
                    controlRecordBytes[0x54u] == 0u &&
                phase13RepeatedMovementAlly.throttle == 0.0 &&
                phase13RepeatedMovementAlly.turn == 0.0 &&
                phase13RepeatedMovementAlly.forwardSpeed == 0.0,
            "repeated BTECH mode-zero movement did not preserve exact post-step state");

        const auto phase13RepeatedReplay = mw::battle::runBattleReplay(
            phase13RepeatedMovementParams, {}, 3u);
        expect(
            mw::battle::battleSnapshotFingerprint(phase13RepeatedMovement) ==
                mw::battle::battleSnapshotFingerprint(
                    phase13RepeatedReplay),
            "repeated original movement changed under replay");

        auto phase13RepeatedHalfTickParams = phase13RepeatedMovementParams;
        phase13RepeatedHalfTickParams.fixedTickSeconds = 0.05;
        const auto phase13RepeatedHalfTick = mw::battle::runBattleReplay(
            phase13RepeatedHalfTickParams, {}, 6u);
        const auto& phase13RepeatedHalfTickAlly =
            phase13LiveAllyFrom(phase13RepeatedHalfTick);
        expect(
            phase13RepeatedHalfTickAlly.originalAiUpdateCount == 3u &&
                phase13RepeatedHalfTickAlly.originalAiMotion.
                    movementUpdateAttemptCount == 3u &&
                phase13RepeatedHalfTickAlly.originalAiMotion.
                    acceptedPoseCommitCount == 3u &&
                phase13RepeatedHalfTickAlly.originalAiMotion.rawX ==
                    phase13RepeatedMovementAlly.originalAiMotion.rawX &&
                phase13RepeatedHalfTickAlly.originalAiMotion.rawY ==
                    phase13RepeatedMovementAlly.originalAiMotion.rawY &&
                phase13RepeatedHalfTickAlly.originalAiMotion.rawZ ==
                    phase13RepeatedMovementAlly.originalAiMotion.rawZ &&
                phase13RepeatedHalfTickAlly.originalAiMotion.heading ==
                    phase13RepeatedMovementAlly.originalAiMotion.heading &&
                phase13RepeatedHalfTickAlly.originalAiMotion.
                    rawSpeedWord3c ==
                    phase13RepeatedMovementAlly.originalAiMotion.
                        rawSpeedWord3c,
            "repeated original movement changed with fixed-tick subdivision");

        auto phase13RepeatedFingerprintMutation = phase13RepeatedMovement;
        ++phase13RepeatedFingerprintMutation.combatants[1].originalAiMotion.
            movementUpdateAttemptCount;
        expect(
            mw::battle::battleSnapshotFingerprint(phase13RepeatedMovement) !=
                mw::battle::battleSnapshotFingerprint(
                    phase13RepeatedFingerprintMutation),
            "repeated movement lifetime is missing from replay fingerprint");

        mw::battle::BattleStartParams phase13Mode3NullMovementParams =
            phase13RepeatedMovementParams;
        phase13Mode3NullMovementParams.setupMetadata.playerMode = 3;
        phase13Mode3NullMovementParams.setupMetadata.opposingMode = 3;
        phase13Mode3NullMovementParams.objective.activeObjectProven = false;
        phase13Mode3NullMovementParams.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::
                OriginalBtechModeThreeNullDataPointRepeatedMovement;
        const auto phase13Mode3NullInitial = mw::battle::runBattleReplay(
            phase13Mode3NullMovementParams, {}, 0u);
        const auto phase13Mode3NullMovement = mw::battle::runBattleReplay(
            phase13Mode3NullMovementParams, {}, 3u);
        const auto& phase13Mode3NullInitialAlly =
            phase13LiveAllyFrom(phase13Mode3NullInitial);
        const auto& phase13Mode3NullMovementAlly =
            phase13LiveAllyFrom(phase13Mode3NullMovement);
        const auto phase13Mode3NullMobility = mw::mech3d::catalogMobility(
            phase13Mode3NullMovementAlly.mechPresetId);
        const double phase13Mode3NullExpectedGroundY =
            static_cast<double>(
                phase13Mode3NullMovementAlly.originalAiMotion.rawY -
                phase13Mode3NullMobility.
                    originalBtechDefinitionHeightWord02 / 2) *
            phase13Mode3NullMovementParams.terrainCellSize / 512.0;
        std::ostringstream phase13Mode3NullDiagnostic;
        phase13Mode3NullDiagnostic
            << " initialized="
            << phase13Mode3NullInitialAlly.originalAiMotion.initialized
            << " mode="
            << phase13Mode3NullInitialAlly.originalAiMotion.numericSideModeWord
            << " updates=" << phase13Mode3NullMovementAlly.originalAiUpdateCount
            << " attempts="
            << phase13Mode3NullMovementAlly.originalAiMotion.
                movementUpdateAttemptCount
            << " commits="
            << phase13Mode3NullMovementAlly.originalAiMotion.
                acceptedPoseCommitCount
            << " result="
            << static_cast<int>(phase13Mode3NullMovementAlly.originalAiMotion.
                lastMovementUpdateResult)
            << " first_committed="
            << phase13Mode3NullMovementAlly.originalAiMotion.
                firstMovementUpdateCommitted
            << " first_target="
            << phase13Mode3NullMovementAlly.originalAiMotion.
                firstMovementTargetEntityId.value
            << "/"
            << static_cast<int>(phase13Mode3NullMovementAlly.originalAiMotion.
                firstMovementTargetLiveObjectSlot)
            << " last_target="
            << phase13Mode3NullMovementAlly.originalAiMotion.
                lastMovementTargetEntityId.value
            << "/"
            << static_cast<int>(phase13Mode3NullMovementAlly.originalAiMotion.
                lastMovementTargetLiveObjectSlot)
            << " post="
            << phase13Mode3NullMovementAlly.originalAiMotion.
                lastMovementPostStepExact
            << "/"
            << static_cast<int>(phase13Mode3NullMovementAlly.originalAiMotion.
                lastMovementPostStepActionCode)
            << " control1e="
            << static_cast<int>(phase13Mode3NullMovementAlly.originalAiMotion.
                controlRecordBytes[0x1eu])
            << " control46="
            << static_cast<int>(phase13Mode3NullMovementAlly.originalAiMotion.
                controlRecordBytes[0x46u])
            << " raw="
            << phase13Mode3NullInitialAlly.originalAiMotion.rawX << ","
            << phase13Mode3NullInitialAlly.originalAiMotion.rawZ << "->"
            << phase13Mode3NullMovementAlly.originalAiMotion.rawX << ","
            << phase13Mode3NullMovementAlly.originalAiMotion.rawZ
            << " provenance="
            << phase13Mode3NullMovementAlly.originalAiMotion.provenance
            << " objective="
            << phase13Mode3NullMovement.objective.activeObjectProven
            << " roster=" << phase13Mode3NullInitial.combatants.size() << "/"
            << phase13Mode3NullMovement.combatants.size();
        expect(
            phase13Mode3NullInitialAlly.originalAiMotion.initialized &&
                phase13Mode3NullInitialAlly.originalAiMotion.
                    numericSideModeWord == 3 &&
                phase13Mode3NullMovementAlly.originalAiUpdateCount == 3u &&
                phase13Mode3NullMovementAlly.originalAiMotion.
                    movementUpdateAttemptCount == 3u &&
                phase13Mode3NullMovementAlly.originalAiMotion.
                    acceptedPoseCommitCount == 3u &&
                phase13Mode3NullMovementAlly.originalAiMotion.
                    firstMovementUpdateCommitted &&
                phase13Mode3NullMovementAlly.originalAiMotion.
                    firstMovementTargetEntityId.value == 0u &&
                phase13Mode3NullMovementAlly.originalAiMotion.
                    firstMovementTargetLiveObjectSlot == 8u &&
                phase13Mode3NullMovementAlly.originalAiMotion.
                    lastMovementTargetEntityId.value == 0u &&
                phase13Mode3NullMovementAlly.originalAiMotion.
                    lastMovementTargetLiveObjectSlot == 8u &&
                phase13Mode3NullMovementAlly.originalAiMotion.
                    lastMovementPostStepExact &&
                phase13Mode3NullMovementAlly.originalAiMotion.
                    lastMovementPostStepActionCode == 3u &&
                phase13Mode3NullMovementAlly.originalAiMotion.
                    controlRecordBytes[0x1eu] == 8u &&
                phase13Mode3NullMovementAlly.originalAiMotion.
                    controlRecordBytes[0x1fu] == 0u &&
                phase13Mode3NullMovementAlly.originalAiMotion.
                    controlRecordBytes[0x46u] == 6u &&
                phase13Mode3NullMovementAlly.originalAiMotion.
                    controlRecordBytes[0x47u] == 0u &&
                (phase13Mode3NullMovementAlly.originalAiMotion.rawX !=
                     phase13Mode3NullInitialAlly.originalAiMotion.rawX ||
                 phase13Mode3NullMovementAlly.originalAiMotion.rawZ !=
                     phase13Mode3NullInitialAlly.originalAiMotion.rawZ) &&
                phase13Mode3NullMovementAlly.originalAiMotion.
                    rawSpeedWord3c != 0 &&
                phase13Mode3NullMovementAlly.activeAnimationId == "walk" &&
                phase13Mode3NullMovementAlly.walkAnimationElapsedMs > 0u &&
                std::abs(
                    phase13Mode3NullMovementAlly.transform.y -
                    phase13Mode3NullExpectedGroundY) < 1.0e-9 &&
                !phase13Mode3NullMovementAlly.airborne &&
                !phase13Mode3NullMovement.objective.activeObjectProven &&
                phase13Mode3NullMovement.combatants.size() ==
                    phase13Mode3NullInitial.combatants.size(),
            "fresh mode-3 null-data-point movement did not commit as a typed non-entity destination:" +
                phase13Mode3NullDiagnostic.str());
        const auto phase13Mode3NullReplay = mw::battle::runBattleReplay(
            phase13Mode3NullMovementParams, {}, 3u);
        expect(
            mw::battle::battleSnapshotFingerprint(phase13Mode3NullMovement) ==
                mw::battle::battleSnapshotFingerprint(
                    phase13Mode3NullReplay),
            "mode-3 null-data-point movement changed under replay");
        auto phase13Mode3NullFingerprintMutation =
            phase13Mode3NullMovement;
        ++phase13Mode3NullFingerprintMutation.combatants[1].
            originalAiMotion.rawX;
        expect(
            mw::battle::battleSnapshotFingerprint(phase13Mode3NullMovement) !=
                mw::battle::battleSnapshotFingerprint(
                    phase13Mode3NullFingerprintMutation),
            "mode-3 null-data-point authoritative pose is missing from the replay fingerprint");
        auto phase13Mode3NullHalfTickParams = phase13Mode3NullMovementParams;
        phase13Mode3NullHalfTickParams.fixedTickSeconds = 0.05;
        const auto phase13Mode3NullHalfTick = mw::battle::runBattleReplay(
            phase13Mode3NullHalfTickParams, {}, 6u);
        const auto& phase13Mode3NullHalfTickAlly =
            phase13LiveAllyFrom(phase13Mode3NullHalfTick);
        expect(
            phase13Mode3NullHalfTickAlly.originalAiUpdateCount == 3u &&
                phase13Mode3NullHalfTickAlly.originalAiMotion.
                    acceptedPoseCommitCount == 3u &&
                phase13Mode3NullHalfTickAlly.originalAiMotion.rawX ==
                    phase13Mode3NullMovementAlly.originalAiMotion.rawX &&
                phase13Mode3NullHalfTickAlly.originalAiMotion.rawY ==
                    phase13Mode3NullMovementAlly.originalAiMotion.rawY &&
                phase13Mode3NullHalfTickAlly.originalAiMotion.rawZ ==
                    phase13Mode3NullMovementAlly.originalAiMotion.rawZ &&
                phase13Mode3NullHalfTickAlly.originalAiMotion.heading ==
                    phase13Mode3NullMovementAlly.originalAiMotion.heading,
            "mode-3 null-data-point movement changed with fixed-tick subdivision");

        mw::battle::BattleStartParams phase13MultiTargetMovementParams =
            phase13RepeatedMovementParams;
        auto& phase13MultiSlot1 =
            phase13MultiTargetMovementParams.setupMetadata.playerSlots[1];
        auto& phase13MultiSlot4 =
            phase13MultiTargetMovementParams.setupMetadata.opposingSlots[0];
        auto& phase13MultiSlot5 =
            phase13MultiTargetMovementParams.setupMetadata.opposingSlots[1];
        phase13MultiSlot1.originalRawX = 0;
        phase13MultiSlot1.originalRawZ = 0;
        phase13MultiSlot1.transform = phase13WorldTransformFromRaw(0, 0);
        phase13MultiSlot4.originalRawX = 2000;
        phase13MultiSlot4.originalRawZ = 0;
        phase13MultiSlot4.transform =
            phase13WorldTransformFromRaw(2000, 0);
        phase13MultiSlot5.originalRawX = -10000;
        phase13MultiSlot5.originalRawZ = -10000;
        phase13MultiSlot5.transform =
            phase13WorldTransformFromRaw(-10000, -10000);
        phase13MultiTargetMovementParams.playerAlliedLaunchStates[0].
            startTransform = phase13MultiSlot1.transform;
        phase13MultiTargetMovementParams.combatantLaunchStates[0].
            startTransform = phase13MultiSlot4.transform;
        auto phase13MultiTarget5 =
            phase13MultiTargetMovementParams.combatantLaunchStates[0];
        phase13MultiTarget5.startTransform = phase13MultiSlot5.transform;
        phase13MultiTarget5.roster.sourceSlot = "opposing:1";
        phase13MultiTargetMovementParams.combatantLaunchStates.push_back(
            phase13MultiTarget5);
        phase13MultiTargetMovementParams.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::
                OriginalBtechModeZeroMultiTargetRepeatedMovement;

        const auto phase13MultiTargetMovement = mw::battle::runBattleReplay(
            phase13MultiTargetMovementParams, {}, 1u);
        const auto phase13CombatantAtLiveSlot = [](
                                                   const mw::battle::BattleSnapshot& snapshot,
                                                   uint8_t liveSlot)
            -> const mw::battle::CombatantSnapshot& {
            const auto found = std::find_if(
                snapshot.combatants.begin(),
                snapshot.combatants.end(),
                [liveSlot](const mw::battle::CombatantSnapshot& combatant) {
                    return combatant.roster.originalLiveObjectSlot ==
                        std::optional<uint8_t>{liveSlot};
                });
            if (found == snapshot.combatants.end()) {
                throw std::runtime_error(
                    "phase13 multi-target fixture lost a live slot");
            }
            return *found;
        };
        const auto& phase13MultiAlly1Result =
            phase13CombatantAtLiveSlot(phase13MultiTargetMovement, 1u);
        expect(
            phase13MultiAlly1Result.originalAiMotion.
                    movementUpdateAttemptCount == 1u &&
                phase13MultiAlly1Result.originalAiMotion.
                    lastMovementTargetLiveObjectSlot == 4u &&
                phase13MultiAlly1Result.originalAiMotion.
                    lastMovementPostStepExact &&
                phase13MultiAlly1Result.originalAiMotion.
                    acceptedPoseCommitCount == 1u,
            "live BTECH 23bf did not admit the exact multi-target movement path");
        const auto phase13MultiTargetReplay = mw::battle::runBattleReplay(
            phase13MultiTargetMovementParams, {}, 1u);
        expect(
            mw::battle::battleSnapshotFingerprint(phase13MultiTargetMovement) ==
                mw::battle::battleSnapshotFingerprint(
                    phase13MultiTargetReplay),
            "multi-target original allied movement changed under replay");

        mw::battle::BattleStartParams phase13Mode1MovementParams =
            phase13RepeatedMovementParams;
        phase13Mode1MovementParams.setupMetadata.playerMode = 1;
        phase13Mode1MovementParams.setupMetadata.opposingMode = 1;
        phase13Mode1MovementParams.battlefieldBoundary =
            mode1Setup.battlefieldBoundary;
        phase13Mode1MovementParams.battlefieldBoundary.
            playerAllowedExitMask = 0x08u;
        phase13Mode1MovementParams.battlefieldBoundary.
            opposingAllowedExitMask = 0x02u;
        phase13Mode1MovementParams.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::
                OriginalBtechNonObjectiveModesMultiTargetRepeatedMovement;
        const auto phase13Mode1Movement = mw::battle::runBattleReplay(
            phase13Mode1MovementParams, {}, 3u);
        const auto& phase13Mode1MovementAlly =
            phase13LiveAllyFrom(phase13Mode1Movement);
        expect(
            phase13Mode1MovementAlly.originalAiMotion.initialized &&
                phase13Mode1MovementAlly.originalAiMotion.
                    numericSideModeWord == 1 &&
                phase13Mode1MovementAlly.originalAiMotion.
                    acceptedPoseCommitCount == 3u &&
                phase13Mode1MovementAlly.originalAiMotion.
                    movementUpdateAttemptCount == 3u &&
                phase13Mode1MovementAlly.originalAiMotion.
                    lastMovementUpdateResult ==
                    mw::battle::BattleOriginalAiFirstMovementUpdateResult::
                        Committed &&
                phase13Mode1MovementAlly.originalAiMotion.
                    lastMovementPostStepExact,
            "live BTECH mode-1 allied movement did not reuse the exact ordinary path: init=" +
                std::to_string(phase13Mode1MovementAlly.originalAiMotion.initialized) +
                " mode=" + std::to_string(phase13Mode1MovementAlly.originalAiMotion.numericSideModeWord) +
                " attempts=" + std::to_string(phase13Mode1MovementAlly.originalAiMotion.movementUpdateAttemptCount) +
                " commits=" + std::to_string(phase13Mode1MovementAlly.originalAiMotion.acceptedPoseCommitCount) +
                " result=" + std::to_string(static_cast<int>(phase13Mode1MovementAlly.originalAiMotion.lastMovementUpdateResult)) +
                " post=" + std::to_string(phase13Mode1MovementAlly.originalAiMotion.lastMovementPostStepExact) +
                " provenance=" + phase13Mode1MovementAlly.originalAiMotion.provenance);
        const auto phase13Mode1Replay = mw::battle::runBattleReplay(
            phase13Mode1MovementParams, {}, 3u);
        expect(
            mw::battle::battleSnapshotFingerprint(phase13Mode1Movement) ==
                mw::battle::battleSnapshotFingerprint(phase13Mode1Replay),
            "mode-1 original allied movement changed under replay");

        auto phase13Mode1OtherFlagParams = phase13Mode1MovementParams;
        phase13Mode1OtherFlagParams.battlefieldBoundary.
            playerAllowedExitMask = 0x04u;
        const auto phase13Mode1OtherFlag = mw::battle::runBattleReplay(
            phase13Mode1OtherFlagParams, {}, 0u);
        const auto phase13Mode1Initial = mw::battle::runBattleReplay(
            phase13Mode1MovementParams, {}, 0u);
        expect(
            phase13LiveAllyFrom(phase13Mode1Initial).
                    originalAiMotion.heading == -0x3ffc &&
                phase13LiveAllyFrom(phase13Mode1OtherFlag).
                    originalAiMotion.heading == 0x3ffc &&
                mw::battle::battleSnapshotFingerprint(phase13Mode1Initial) !=
                    mw::battle::battleSnapshotFingerprint(
                        phase13Mode1OtherFlag),
            "mode-1 SNARIO heading flag is missing from owned state/fingerprint");

        mw::battle::BattleStartParams phase13Mode2MovementParams =
            phase13RepeatedMovementParams;
        phase13Mode2MovementParams.setupMetadata.playerMode = 2;
        phase13Mode2MovementParams.setupMetadata.opposingMode = 4;
        phase13Mode2MovementParams.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::
                OriginalBtechNonObjectiveModesMultiTargetRepeatedMovement;
        const auto phase13Mode2Movement = mw::battle::runBattleReplay(
            phase13Mode2MovementParams, {}, 1u);
        const auto& phase13Mode2MovementAlly =
            phase13LiveAllyFrom(phase13Mode2Movement);
        expect(
            phase13Mode2MovementAlly.originalAiMotion.initialized &&
                phase13Mode2MovementAlly.originalAiMotion.
                    numericSideModeWord == 2 &&
                phase13Mode2MovementAlly.originalAiMotion.
                    movementUpdateAttemptCount == 1u &&
                phase13Mode2MovementAlly.originalAiMotion.
                    acceptedPoseCommitCount == 1u &&
                phase13Mode2MovementAlly.originalAiMotion.
                    lastMovementUpdateResult ==
                    mw::battle::BattleOriginalAiFirstMovementUpdateResult::
                        Committed,
            "live BTECH mode-2 allied movement did not reuse the exact ordinary path");

        mw::battle::BattleStartParams phase13SymmetricMovementParams =
            phase13RepeatedMovementParams;
        phase13SymmetricMovementParams.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::
                OriginalBtechNonObjectiveSymmetricRepeatedMovement50eeInvariant;
        const auto phase13SymmetricMovement = mw::battle::runBattleReplay(
            phase13SymmetricMovementParams, {}, 1u);
        const auto& phase13SymmetricAlly =
            phase13CombatantAtLiveSlot(phase13SymmetricMovement, 1u);
        const auto& phase13SymmetricEnemy =
            phase13CombatantAtLiveSlot(phase13SymmetricMovement, 4u);
        expect(
            phase13SymmetricAlly.originalAiMotion.
                    acceptedPoseCommitCount == 1u &&
                !phase13SymmetricAlly.originalAiMotion.
                    lastMovementSelection50eeIndependent &&
                phase13SymmetricEnemy.originalAiMotion.initialized &&
                phase13SymmetricEnemy.originalAiMotion.
                    movementUpdateAttemptCount == 1u &&
                phase13SymmetricEnemy.originalAiMotion.
                    acceptedPoseCommitCount == 1u &&
                phase13SymmetricEnemy.originalAiMotion.
                    firstMovementUpdateCommitted &&
                phase13SymmetricEnemy.originalAiMotion.
                    firstMovementSelection50eeIndependent &&
                phase13SymmetricEnemy.originalAiMotion.
                    lastMovementSelection50eeIndependent &&
                phase13SymmetricEnemy.originalAiMotion.
                    lastMovementUpdateResult ==
                    mw::battle::BattleOriginalAiFirstMovementUpdateResult::
                        Committed &&
                phase13SymmetricEnemy.originalAiMotion.
                    lastMovementTargetLiveObjectSlot == 1u &&
                phase13SymmetricEnemy.originalAiMotion.
                    lastMovementTargetEntityId == phase13SymmetricAlly.id &&
                phase13SymmetricEnemy.originalAiMotion.
                    lastMovementRelationWord == 1u &&
                phase13SymmetricEnemy.originalAiMotion.
                    lastMovementPostStepExact &&
                phase13SymmetricEnemy.originalAiMotion.
                    rawSpeedWord3c == 5 &&
                phase13SymmetricEnemy.originalAiMotion.rawX != 2000,
            "50ee-independent opposing movement did not commit toward the stable companion target");

        auto phase13CampaignMovementParams = phase13SymmetricMovementParams;
        phase13CampaignMovementParams.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::Disabled;
        const auto phase13ActOnOwnMode0 =
            mw::battle::battleOriginalAiActOnOwnRoute7797(0, 0);
        const auto phase13ActOnOwnMode1 =
            mw::battle::battleOriginalAiActOnOwnRoute7797(0, 1);
        const auto phase13ActOnOwnMode2 =
            mw::battle::battleOriginalAiActOnOwnRoute7797(0, 2);
        const auto phase13ActOnOwnMode3 =
            mw::battle::battleOriginalAiActOnOwnRoute7797(0, 3);
        const auto phase13ActOnOwnMode4 =
            mw::battle::battleOriginalAiActOnOwnRoute7797(0, 4);
        const auto phase13ActOnOwnInvalid =
            mw::battle::battleOriginalAiActOnOwnRoute7797(0, 5);
        const auto phase13NonActOnOwn =
            mw::battle::battleOriginalAiActOnOwnRoute7797(1, 0);
        expect(
            phase13ActOnOwnMode0.exact &&
                phase13ActOnOwnMode0.callsTargetSelector23bf &&
                phase13ActOnOwnMode0.callsMovementPoint94a4 &&
                phase13ActOnOwnMode0.route ==
                    mw::battle::BattleOriginalAiActOnOwnRoute::
                        SelectedCachedLiveTarget &&
                phase13ActOnOwnMode1.exact &&
                phase13ActOnOwnMode1.route ==
                    mw::battle::BattleOriginalAiActOnOwnRoute::
                        ModeOneTablePoint &&
                phase13ActOnOwnMode1.requiresSideDirectionFlags &&
                !phase13ActOnOwnMode1.callsTargetSelector23bf &&
                phase13ActOnOwnMode2.exact &&
                phase13ActOnOwnMode2.callsTargetSelector23bf &&
                phase13ActOnOwnMode3.exact &&
                phase13ActOnOwnMode3.route ==
                    mw::battle::BattleOriginalAiActOnOwnRoute::
                        SeparateObjectPoint &&
                phase13ActOnOwnMode3.requiresSeparateObjectPose &&
                phase13ActOnOwnMode3.status46 == 6 &&
                phase13ActOnOwnMode4.exact &&
                phase13ActOnOwnMode4.route ==
                    mw::battle::BattleOriginalAiActOnOwnRoute::
                        StatusSevenReturn &&
                phase13ActOnOwnMode4.status46 == 7 &&
                !phase13ActOnOwnInvalid.exact &&
                !phase13NonActOnOwn.exact,
            "7797 Act On Own mission-route dispatch changed");
        const auto phase13CampaignMovementActivation =
            mw::battle::battleOriginalAiCampaignMovementActivation(
                phase13CampaignMovementParams);
        expect(
            phase13CampaignMovementActivation.exact &&
                phase13CampaignMovementActivation.incomingPolicyDisabled &&
                phase13CampaignMovementActivation.setupAvailable &&
                phase13CampaignMovementActivation.
                    nonObjectiveModesSupported &&
                phase13CampaignMovementActivation.
                    actOnOwnSelectedTargetRoutesSupported &&
                phase13CampaignMovementActivation.playerActOnOwnRoute.
                    callsTargetSelector23bf &&
                phase13CampaignMovementActivation.opposingActOnOwnRoute.
                    callsTargetSelector23bf &&
                phase13CampaignMovementActivation.
                    activeSeparateObjectiveAbsent &&
                phase13CampaignMovementActivation.completePhase12Runtime &&
                phase13CampaignMovementActivation.
                    originalMovementResourcesAvailable &&
                phase13CampaignMovementActivation.
                    activePlayerCompanionPresent &&
                phase13CampaignMovementActivation.activeOpponentPresent &&
                phase13CampaignMovementActivation.defaultActOnOwnOrder &&
                phase13CampaignMovementActivation.freshOrder.order ==
                    mw::battle::BattleOriginalAiLanceOrder::ActOnOwn &&
                phase13CampaignMovementActivation.freshOrder.targetInput ==
                    mw::battle::BattleOriginalAiOrderTargetInput::None &&
                phase13CampaignMovementActivation.activate &&
                phase13CampaignMovementActivation.selectedPolicy ==
                    mw::battle::BattleCombatAiPolicy::
                        OriginalBtechNonObjectiveSymmetricRepeatedMovement50eeInvariant,
            "campaign default Act On Own movement activation changed");
        phase13CampaignMovementParams.combatAiPolicy =
            phase13CampaignMovementActivation.selectedPolicy;
        const auto phase13CampaignMovement = mw::battle::runBattleReplay(
            phase13CampaignMovementParams, {}, 1u);
        expect(
            phase13CombatantAtLiveSlot(phase13CampaignMovement, 1u).
                    originalAiMotion.acceptedPoseCommitCount == 1u &&
                phase13CombatantAtLiveSlot(phase13CampaignMovement, 4u).
                    originalAiMotion.acceptedPoseCommitCount == 1u,
            "campaign-selected default Act On Own movement did not advance both sides");

        auto phase13CampaignExplicitPolicy = phase13CampaignMovementParams;
        phase13CampaignExplicitPolicy.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::Phase10CompatibilityFsm;
        const auto phase13CampaignExplicitActivation =
            mw::battle::battleOriginalAiCampaignMovementActivation(
                phase13CampaignExplicitPolicy);
        auto phase13CampaignObjective = phase13CampaignMovementParams;
        phase13CampaignObjective.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::Disabled;
        phase13CampaignObjective.objective.activeObjectProven = true;
        const auto phase13CampaignObjectiveActivation =
            mw::battle::battleOriginalAiCampaignMovementActivation(
                phase13CampaignObjective);
        auto phase13CampaignMode3 = phase13CampaignMovementParams;
        phase13CampaignMode3.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::Disabled;
        phase13CampaignMode3.setupMetadata.playerMode = 3;
        const auto phase13CampaignMode3Activation =
            mw::battle::battleOriginalAiCampaignMovementActivation(
                phase13CampaignMode3);
        auto phase13CampaignMode3BothNoObjective = phase13CampaignMode3;
        phase13CampaignMode3BothNoObjective.setupMetadata.opposingMode = 3;
        const auto phase13CampaignMode3BothNoObjectiveActivation =
            mw::battle::battleOriginalAiCampaignMovementActivation(
                phase13CampaignMode3BothNoObjective);
        const mw::battle::OriginalBattlefieldSetup
            phase13CampaignMode3ObjectiveSetup =
                mw::battle::decodeOriginalBattlefieldSetup(
                    options.snarioPath,
                    options.scenarioIndex,
                    24u,
                    500.0);
        auto phase13CampaignMode3Objective =
            phase13CampaignMovementParams;
        phase13CampaignMode3Objective.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::Disabled;
        phase13CampaignMode3Objective.terrainCollisionGrid =
            phase13LiveOwnedGrid;
        phase13CampaignMode3Objective.mission =
            mw::battle::battleMissionBriefingFromDefinition(
                *mw::battle::battleMissionDefinitionById(24u));
        phase13CampaignMode3Objective.setupMetadata =
            phase13CampaignMode3ObjectiveSetup.metadata;
        phase13CampaignMode3Objective.objective =
            phase13CampaignMode3ObjectiveSetup.objective;
        phase13CampaignMode3Objective.battlefieldBoundary =
            phase13CampaignMode3ObjectiveSetup.battlefieldBoundary;
        phase13CampaignMode3Objective.playerStartTransform =
            phase13CampaignMode3ObjectiveSetup.playerStartTransform;
        phase13CampaignMode3Objective.playerAlliedLaunchStates[0].
            startTransform =
                phase13CampaignMode3ObjectiveSetup.metadata.playerSlots[1].
                    transform;
        mw::battle::BattleCombatantLaunchState phase13SecondMode3Companion =
            phase13CampaignMode3Objective.playerAlliedLaunchStates[0];
        phase13SecondMode3Companion.startTransform =
            phase13CampaignMode3ObjectiveSetup.metadata.playerSlots[2].
                transform;
        phase13SecondMode3Companion.roster.sourceSlot = "player:2";
        phase13CampaignMode3Objective.playerAlliedLaunchStates.push_back(
            phase13SecondMode3Companion);
        phase13CampaignMode3Objective.combatantLaunchStates[0].startTransform =
            phase13CampaignMode3ObjectiveSetup.metadata.opposingSlots[0].
                transform;
        const auto phase13CampaignMode3ObjectiveActivation =
            mw::battle::battleOriginalAiCampaignMovementActivation(
                phase13CampaignMode3Objective);
        auto phase13CampaignMode1 = phase13CampaignMovementParams;
        phase13CampaignMode1.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::Disabled;
        phase13CampaignMode1.setupMetadata.playerMode = 1;
        const auto phase13CampaignMode1Activation =
            mw::battle::battleOriginalAiCampaignMovementActivation(
                phase13CampaignMode1);
        auto phase13CampaignNoAlly = phase13CampaignMovementParams;
        phase13CampaignNoAlly.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::Disabled;
        phase13CampaignNoAlly.playerAlliedLaunchStates.clear();
        const auto phase13CampaignNoAllyActivation =
            mw::battle::battleOriginalAiCampaignMovementActivation(
                phase13CampaignNoAlly);
        const auto phase13CampaignFixtureForMission = [
            &phase13CampaignMovementParams,
            &options](size_t missionId) {
            const mw::battle::OriginalBattlefieldSetup setup =
                mw::battle::decodeOriginalBattlefieldSetup(
                    options.snarioPath,
                    options.scenarioIndex,
                    missionId,
                    500.0);
            auto fixture = phase13CampaignMovementParams;
            fixture.combatAiPolicy =
                mw::battle::BattleCombatAiPolicy::Disabled;
            fixture.mission = mw::battle::battleMissionBriefingFromDefinition(
                *mw::battle::battleMissionDefinitionById(missionId));
            fixture.setupMetadata = setup.metadata;
            fixture.objective = setup.objective;
            fixture.battlefieldBoundary = setup.battlefieldBoundary;
            fixture.playerStartTransform = setup.playerStartTransform;
            const mw::battle::BattleCombatantLaunchState allyTemplate =
                fixture.playerAlliedLaunchStates.front();
            fixture.playerAlliedLaunchStates.clear();
            for (size_t playerSlot = 1u; playerSlot < 4u; ++playerSlot) {
                auto ally = allyTemplate;
                ally.startTransform = setup.metadata.playerSlots[playerSlot].transform;
                ally.roster.sourceSlot =
                    "player:" + std::to_string(playerSlot);
                fixture.playerAlliedLaunchStates.push_back(std::move(ally));
            }
            fixture.combatantLaunchStates.resize(1u);
            fixture.combatantLaunchStates.front().startTransform =
                setup.metadata.opposingSlots.front().transform;
            fixture.combatantLaunchStates.front().roster.sourceSlot =
                "opposing:0";
            return fixture;
        };
        std::array<size_t, 6> wikiMissionFamilyCounts{};
        for (const auto& definition :
             mw::battle::battleMissionDefinitions()) {
            ++wikiMissionFamilyCounts[static_cast<size_t>(definition.family)];
        }
        expect(mw::battle::battleMissionDefinitions().size() == 34u &&
                   wikiMissionFamilyCounts[static_cast<size_t>(
                       mw::battle::BattleMissionFamily::Deathmatch)] == 6u &&
                   wikiMissionFamilyCounts[static_cast<size_t>(
                       mw::battle::BattleMissionFamily::Assault)] == 8u &&
                   wikiMissionFamilyCounts[static_cast<size_t>(
                       mw::battle::BattleMissionFamily::Retrieval)] == 6u &&
                   wikiMissionFamilyCounts[static_cast<size_t>(
                       mw::battle::BattleMissionFamily::Defense)] == 8u &&
                   wikiMissionFamilyCounts[static_cast<size_t>(
                       mw::battle::BattleMissionFamily::Sprint)] == 3u &&
                   wikiMissionFamilyCounts[static_cast<size_t>(
                       mw::battle::BattleMissionFamily::Extended)] == 3u,
               "Wiki mission catalog must remain 31 single missions plus 3 campaigns");
        const auto wikiGarrisonBriefing =
            mw::battle::battleMissionObjectiveBriefingById(0u);
        const auto wikiSecurityBriefing =
            mw::battle::battleMissionObjectiveBriefingById(1u);
        const auto wikiPrototypeBriefing =
            mw::battle::battleMissionObjectiveBriefingById(28u);
        expect(wikiGarrisonBriefing.intent ==
                   mw::battle::BattleMissionObjectiveIntent::Protect &&
                   wikiSecurityBriefing.intent ==
                       mw::battle::BattleMissionObjectiveIntent::Protect &&
                   wikiPrototypeBriefing.intent ==
                       mw::battle::BattleMissionObjectiveIntent::Destroy &&
                   wikiPrototypeBriefing.targetKind == "stolen_prototypes",
               "Wiki structure intent coverage changed for mission ids 0, 1 or 28");
        const auto wikiExtendedPlanA =
            mw::battle::battleExtendedCampaignPlan(0x12345678u);
        const auto wikiExtendedPlanB =
            mw::battle::battleExtendedCampaignPlan(0x12345678u);
        expect(wikiExtendedPlanA.valid &&
                   wikiExtendedPlanA.stageCount == 3u &&
                   wikiExtendedPlanA.missionIds ==
                       wikiExtendedPlanB.missionIds,
               "Wiki extended campaign plan must contain three replay-stable stages");
        for (size_t i = 0; i < wikiExtendedPlanA.stageCount; ++i) {
            const auto stageDefinition =
                mw::battle::battleMissionDefinitionById(
                    wikiExtendedPlanA.missionIds[i]);
            expect(stageDefinition.has_value() &&
                       !stageDefinition->extended &&
                       stageDefinition->family !=
                           mw::battle::BattleMissionFamily::Extended,
                   "Wiki extended campaign selected a nested extended mission");
        }
        const auto wikiFinalPlan = mw::battle::battleFinalMissionPlan();
        expect(wikiFinalPlan.valid && wikiFinalPlan.stageCount == 2u &&
                   mw::battle::battleMissionDefinitionById(
                       wikiFinalPlan.missionIds[0])->family ==
                       mw::battle::BattleMissionFamily::Deathmatch &&
                   mw::battle::battleMissionDefinitionById(
                       wikiFinalPlan.missionIds[1])->family ==
                       mw::battle::BattleMissionFamily::Retrieval,
               "Wiki final battle must be Deathmatch followed by Retrieval");
        size_t wikiUneventfulRolls = 0u;
        for (uint32_t seed = 0u; seed < 1024u; ++seed) {
            wikiUneventfulRolls +=
                mw::battle::battleUneventfulGarrisonDutyRoll(seed) ? 1u : 0u;
        }
        expect(wikiUneventfulRolls == 32u,
               "uneventful garrison duty must remain a small deterministic chance");

        auto phase14ApproachParams = phase13CampaignFixtureForMission(24u);
        phase14ApproachParams.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::ReplacementDeterministicCombatAi;
        phase14ApproachParams.deterministicCollisionRuntimeEnabled = true;
        phase14ApproachParams.terrainCollisionObstacles = {};
        phase14ApproachParams.playerStartTransform = {60000.0, 0.0, 35000.0, 0.0};
        phase14ApproachParams.objective.transform =
            {68000.0, 0.0, 35000.0, 0.0};
        for (size_t i = 0; i < 3u; ++i) {
            phase14ApproachParams.playerAlliedLaunchStates[i].mechPresetId =
                "locust";
            phase14ApproachParams.playerAlliedLaunchStates[i].startTransform =
                {62000.0, 0.0, 34200.0 + 800.0 * i, 1.5707963267948966};
        }
        phase14ApproachParams.combatantLaunchStates[0].startTransform =
            {95000.0, 0.0, 50000.0, 0.0};
        const auto phase14Three = mw::battle::runBattleReplay(
            phase14ApproachParams, {}, 30u);
        expect(phase14Three.combatAiPolicy ==
                   mw::battle::BattleCombatAiPolicy::
                       ReplacementDeterministicCombatAi &&
                   phase14Three.combatAiPolicyProvenance ==
                       "replacement:phase14_wiki_five_scenarios_v13_damage_retreat" &&
                   phase14Three.combatants.size() == 5u &&
                   phase14Three.objective.valid &&
                   phase14Three.combatants[1].replacementAi.active &&
                   phase14Three.combatants[1].replacementAi.decisionSequence == 30u,
               "phase14 policy, objective ownership or fixed decision cadence changed: " +
                   std::to_string(phase14Three.combatants.size()) + ":" +
                   std::to_string(phase14Three.tickIndex) + ":" +
                   std::to_string(phase14Three.combatants[1].replacementAi.decisionSequence));
        for (size_t i = 0; i < 3u; ++i) {
            const auto& ally = phase14Three.combatants[1u + i];
            expect(ally.replacementAi.missionSlice ==
                       mw::battle::BattleReplacementAiMissionSlice::
                           AssaultEngagement &&
                       ally.replacementAi.approachSlot == i &&
                       ally.replacementAi.approachSlotCount == 3u &&
                       ally.replacementAi.movementDestination.x == 67100.0 &&
                       ally.replacementAi.movementDestination.z ==
                           34200.0 + 800.0 * i &&
                       !mw::battle::isValid(
                           ally.replacementAi.combatTargetEntityId) &&
                       ally.transform.x > 62000.0,
                   "phase14 companions must move to stable individual objective slots");
            if (i > 0u) {
                const auto& previous = phase14Three.combatants[i];
                expect(std::hypot(ally.transform.x - previous.transform.x,
                                  ally.transform.z - previous.transform.z) >=
                           ally.collisionRadiusWorld +
                               previous.collisionRadiusWorld,
                       "phase14 approach companions overlapped");
            }
        }
        auto phase14OneParams = phase14ApproachParams;
        phase14OneParams.playerAlliedLaunchStates.resize(1u);
        const auto phase14One = mw::battle::runBattleReplay(
            phase14OneParams, {}, 1u);
        expect(phase14One.combatants[1].replacementAi.approachSlot == 0u &&
                   phase14One.combatants[1].replacementAi.approachSlotCount == 1u &&
                   phase14One.combatants[1].replacementAi.movementDestination.z ==
                       35000.0,
               "phase14 single companion must use the center approach slot");
        const auto phase14ThreeReplay = mw::battle::runBattleReplay(
            phase14ApproachParams, {}, 30u);
        mw::battle::BattleWorld phase14CadenceWorld =
            mw::battle::BattleWorld::create(phase14ApproachParams);
        phase14CadenceWorld.runTicks(10u);
        for (int frame = 0; frame < 23; ++frame) {
            static_cast<void>(phase14CadenceWorld.snapshot());
        }
        phase14CadenceWorld.runTicks(20u);
        expect(mw::battle::battleSnapshotFingerprint(phase14Three) ==
                   mw::battle::battleSnapshotFingerprint(phase14ThreeReplay) &&
                   mw::battle::battleSnapshotFingerprint(phase14Three) ==
                       mw::battle::battleSnapshotFingerprint(
                           phase14CadenceWorld.snapshot()),
               "phase14 replay, subdivision or render cadence changed the result");
        auto phase14Mutation = phase14Three;
        ++phase14Mutation.combatants[1].replacementAi.detourAttempts;
        expect(mw::battle::battleSnapshotFingerprint(phase14Mutation) !=
                   mw::battle::battleSnapshotFingerprint(phase14Three),
               "phase14 navigation state must enter the fingerprint");
        const auto phase14FieldHashed = [&phase14Three](auto mutate,
                                                       const char* field) {
            auto changed = phase14Three;
            mutate(changed.combatants[1].replacementAi);
            expect(mw::battle::battleSnapshotFingerprint(changed) !=
                       mw::battle::battleSnapshotFingerprint(phase14Three),
                   std::string("phase14 fingerprint omitted ") + field);
        };
        phase14FieldHashed([](auto& s) { s.active = false; }, "active");
        phase14FieldHashed([](auto& s) {
            s.missionSlice = mw::battle::BattleReplacementAiMissionSlice::
                RetrieveArrival;
        }, "missionSlice");
        phase14FieldHashed([](auto& s) { ++s.decisionSequence; }, "decisionSequence");
        phase14FieldHashed([](auto& s) { ++s.lastDecisionTickIndex; }, "decisionTick");
        phase14FieldHashed([](auto& s) { ++s.approachSlot; }, "approachSlot");
        phase14FieldHashed([](auto& s) { ++s.approachSlotCount; }, "approachSlotCount");
        phase14FieldHashed([](auto& s) {
            s.missionTriggered = !s.missionTriggered;
        }, "missionTriggered");
        phase14FieldHashed([](auto& s) { s.missionAnchor.x += 1.0; }, "missionAnchor");
        phase14FieldHashed([](auto& s) { s.movementDestination.x += 1.0; }, "destination");
        phase14FieldHashed([](auto& s) { s.combatTargetEntityId.value = 99u; }, "combatTarget");
        phase14FieldHashed([](auto& s) { ++s.selectedWeaponInstanceId; }, "weapon");
        phase14FieldHashed([](auto& s) { s.desiredCombatDistance += 1.0; }, "combatDistance");
        phase14FieldHashed([](auto& s) { s.navMode = mw::battle::BattleReplacementAiNavMode::Detour; }, "navMode");
        phase14FieldHashed([](auto& s) { ++s.stuckDecisionCount; }, "stuckCount");
        phase14FieldHashed([](auto& s) { ++s.detourDecisionsRemaining; }, "detourRemaining");
        phase14FieldHashed([](auto& s) { ++s.lastDestinationDistance; }, "progressDistance");
        phase14FieldHashed([](auto& s) { s.navigationWaypoint.x += 1.0; }, "pathWaypoint");
        phase14FieldHashed([](auto& s) {
            s.navigationWaypointActive = !s.navigationWaypointActive;
        }, "pathWaypointActive");
        phase14FieldHashed([](auto& s) { ++s.pathReplanCount; }, "pathReplanCount");
        phase14FieldHashed([](auto& s) { ++s.pathFailureCount; }, "pathFailureCount");
        phase14FieldHashed([](auto& s) { ++s.nextPathRetryTickIndex; }, "pathRetryTick");
        phase14FieldHashed([](auto& s) { s.lastMoveRejected = !s.lastMoveRejected; }, "moveRejected");
        phase14FieldHashed([](auto& s) { s.friendlyLaneBlocked = !s.friendlyLaneBlocked; }, "friendlyLane");
        phase14FieldHashed([](auto& s) {
            s.terrainLineOfSightBlocked = !s.terrainLineOfSightBlocked;
        }, "terrainLineOfSightBlocked");
        phase14FieldHashed([](auto& s) { s.aimAligned = !s.aimAligned; }, "aimAligned");
        phase14FieldHashed([](auto& s) {
            s.lastAimErrorRadians += 0.01;
        }, "aimError");
        phase14FieldHashed([](auto& s) {
            ++s.nextFireDecisionTickIndex;
        }, "nextFireDecisionTickIndex");
        phase14FieldHashed([](auto& s) {
            ++s.targetLockUntilTickIndex;
        }, "targetLockUntilTickIndex");
        phase14FieldHashed([](auto& s) {
            ++s.nextTorsoTurnTickIndex;
        }, "nextTorsoTurnTickIndex");
        phase14FieldHashed([](auto& s) {
            s.proximityThreatOverride = !s.proximityThreatOverride;
        }, "proximityThreatOverride");
        phase14FieldHashed([](auto& s) {
            s.aimRecoveryActive = !s.aimRecoveryActive;
        }, "aimRecoveryActive");
        phase14FieldHashed([](auto& s) {
            s.combatOrbitActive = !s.combatOrbitActive;
        }, "combatOrbitActive");
        phase14FieldHashed([](auto& s) {
            s.orbitDirection = s.orbitDirection >= 0 ? -1 : 1;
        }, "orbitDirection");
        phase14FieldHashed([](auto& s) {
            ++s.orbitDirectionHoldUntilTickIndex;
        }, "orbitDirectionHoldUntilTickIndex");
        phase14FieldHashed([](auto& s) {
            s.combatOrbitDestination.x += 1.0;
        }, "combatOrbitDestination");
        phase14FieldHashed([](auto& s) {
            s.retreating = !s.retreating;
        }, "retreating");
        phase14FieldHashed([](auto& s) {
            s.retreatThreatEntityId.value = 99u;
        }, "retreatThreatEntityId");
        phase14FieldHashed([](auto& s) {
            s.retreatDestination.x += 1.0;
        }, "retreatDestination");
        phase14FieldHashed([](auto& s) { s.arrivalReached = !s.arrivalReached; }, "arrivalReached");
        phase14FieldHashed([](auto& s) { ++s.arrivalTickIndex; }, "arrivalTickIndex");
        auto phase14PolicyMutation = phase14Three;
        phase14PolicyMutation.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::Disabled;
        expect(mw::battle::battleSnapshotFingerprint(phase14PolicyMutation) !=
                   mw::battle::battleSnapshotFingerprint(phase14Three),
               "phase14 policy must enter the fingerprint");
        auto phase14ProvenanceMutation = phase14Three;
        phase14ProvenanceMutation.combatAiPolicyProvenance += ":changed";
        expect(mw::battle::battleSnapshotFingerprint(phase14ProvenanceMutation) !=
                   mw::battle::battleSnapshotFingerprint(phase14Three),
               "phase14 policy provenance must enter the fingerprint");

        auto phase14FireParams = phase14OneParams;
        phase14FireParams.playerStartTransform =
            {65400.0, 0.0, 35000.0, 0.0};
        phase14FireParams.playerAlliedLaunchStates[0].startTransform =
            {65000.0, 0.0, 35000.0, 1.5707963267948966};
        phase14FireParams.combatantLaunchStates[0].startTransform =
            {65800.0, 0.0, 35000.0, 0.0};
        phase14FireParams.stationaryTargetHitDiagnosticEnabled = true;
        mw::battle::BattleWorld phase14FireWorld =
            mw::battle::BattleWorld::create(phase14FireParams);
        phase14FireWorld.tick();
        const auto phase14Blocked = phase14FireWorld.snapshot();
        expect(phase14Blocked.combatants[1].replacementAi.combatTargetEntityId ==
                   phase14Blocked.combatants[2].id &&
                   phase14Blocked.combatants[1].replacementAi.friendlyLaneBlocked &&
                   phase14Blocked.combatants[1].weaponShotsFired == 0u,
               "phase14 friendly lane must suppress the assigned obstruction shot");
        const int phase14EnemyArmorBefore = sectionBattleDamage(
            phase14Blocked.combatants[2],
            mw::mech3d::MechArmorSectionId::CenterTorso);
        bool phase14Accepted = false;
        for (uint64_t tick = 1u; tick < 30u; ++tick) {
            mw::battle::BattleInputCommand movePlayer;
            movePlayer.tickIndex = tick;
            movePlayer.entityId = phase14FireWorld.playerEntityId();
            movePlayer.throttle = 1.0;
            phase14FireWorld.enqueueInput(movePlayer);
            phase14FireWorld.tick();
            const auto shot = phase14FireWorld.snapshot();
            if (shot.combatants[1].lastFireRequestAccepted) {
                expect(shot.combatants[1].weaponShotsFired == 1u &&
                           shot.lastShot.shooterEntityId ==
                               shot.combatants[1].id &&
                           shot.lastShot.targetEntityId ==
                               shot.combatants[2].id &&
                           shot.lastShot.deliveryState ==
                               mw::battle::BattleShotDeliveryState::Immediate &&
                           shot.lastShot.provenance.find(
                               "replacement_phase14_assigned_obstruction_common_pipeline") == 0u &&
                           shot.lastShot.runtimeEffect ==
                               mw::battle::BattleShotRuntimeEffect::
                                   DetailedArmorSectionDamage &&
                           !shot.lastShot.criticalResolutionPending &&
                           sectionBattleDamage(
                               shot.combatants[2],
                               mw::mech3d::MechArmorSectionId::CenterTorso) >
                               phase14EnemyArmorBefore,
                       "phase14 clear lane must produce one common immediate delivery and damage");
                phase14Accepted = true;
                break;
            }
        }
        expect(phase14Accepted,
               "phase14 obstruction request did not clear after the friendly moved");

        auto phase14DistributionParams = phase14ApproachParams;
        phase14DistributionParams.combatantLaunchStates[0].startTransform =
            {64000.0, 0.0, 34200.0, 0.0};
        auto phase14SecondEnemy =
            phase14DistributionParams.combatantLaunchStates[0];
        phase14SecondEnemy.startTransform =
            {64000.0, 0.0, 35800.0, 0.0};
        phase14SecondEnemy.roster.sourceSlot = "opposing:1";
        phase14DistributionParams.combatantLaunchStates.push_back(
            phase14SecondEnemy);
        mw::battle::BattleWorld phase14DistributionWorld =
            mw::battle::BattleWorld::create(phase14DistributionParams);
        phase14DistributionWorld.tick();
        const auto phase14Assigned = phase14DistributionWorld.snapshot();
        expect(phase14Assigned.combatants[1].replacementAi.combatTargetEntityId ==
                   phase14Assigned.combatants[4].id &&
                   phase14Assigned.combatants[2].replacementAi.
                           combatTargetEntityId ==
                       phase14Assigned.combatants[5].id &&
                   phase14Assigned.combatants[3].replacementAi.combatTargetEntityId ==
                       phase14Assigned.combatants[5].id,
               "phase14 radar contacts must distribute stably and reinforce the remaining target");
        phase14DistributionWorld.setMissionStatus(
            phase14Assigned.combatants[4].id,
            mw::battle::CombatantMissionStatus::Destroyed);
        phase14DistributionWorld.tick();
        const auto phase14Retarget = phase14DistributionWorld.snapshot();
        expect(phase14Retarget.combatants[1].replacementAi.
                       combatTargetEntityId ==
                   phase14Retarget.combatants[5].id &&
                   phase14Retarget.combatants[1].replacementAi.navMode ==
                       mw::battle::BattleReplacementAiNavMode::EngageObstruction,
               "phase14 lost radar contact must retarget the remaining visible opponent");

        auto phase14CloseThreatParams = phase14DistributionParams;
        phase14CloseThreatParams.playerAlliedLaunchStates.resize(2u);
        phase14CloseThreatParams.playerAlliedLaunchStates[0].startTransform =
            {60000.0, 0.0, 34700.0, 1.5707963267948966};
        phase14CloseThreatParams.playerAlliedLaunchStates[1].startTransform =
            {60000.0, 0.0, 35300.0, 1.5707963267948966};
        phase14CloseThreatParams.combatantLaunchStates[0].startTransform =
            {61000.0, 0.0, 35000.0, -1.5707963267948966};
        phase14CloseThreatParams.combatantLaunchStates[1].startTransform =
            {64000.0, 0.0, 35000.0, -1.5707963267948966};
        const auto phase14CloseThreat = mw::battle::runBattleReplay(
            phase14CloseThreatParams, {}, 1u);
        const auto phase14CloseThreatId =
            phase14CloseThreat.combatants[3].id;
        expect(
            phase14CloseThreat.combatants[1].replacementAi.
                    combatTargetEntityId == phase14CloseThreatId &&
                phase14CloseThreat.combatants[2].replacementAi.
                    combatTargetEntityId == phase14CloseThreatId &&
                phase14CloseThreat.combatants[1].replacementAi.
                    proximityThreatOverride &&
                phase14CloseThreat.combatants[2].replacementAi.
                    proximityThreatOverride &&
                phase14CloseThreat.combatants[1].replacementAi.
                        targetLockUntilTickIndex >
                    phase14CloseThreat.tickIndex,
            "a close hostile must interrupt distant assignment for every mech it passes");

        auto phase14RangeParams = phase14OneParams;
        phase14RangeParams.playerAlliedLaunchStates[0].startTransform =
            {62000.0, 0.0, 35000.0, 1.5707963267948966};
        phase14RangeParams.combatantLaunchStates[0].startTransform =
            {66900.0, 0.0, 35000.0, 0.0};
        const auto phase14OutOfRange = mw::battle::runBattleReplay(
            phase14RangeParams, {}, 1u);
        const auto& phase14RangeAlly = phase14OutOfRange.combatants[1];
        const auto phase14RangeWeapon = std::find_if(
            phase14RangeAlly.weapons.begin(),
            phase14RangeAlly.weapons.end(),
            [&phase14RangeAlly](const auto& weapon) {
                return weapon.weaponInstanceId == phase14RangeAlly.
                    replacementAi.selectedWeaponInstanceId;
            });
        expect(phase14OutOfRange.combatants[1].replacementAi.combatTargetEntityId ==
                   phase14OutOfRange.combatants[2].id &&
                   phase14OutOfRange.combatants[1].weaponShotsFired == 0u &&
                   phase14RangeWeapon != phase14RangeAlly.weapons.end() &&
                   phase14OutOfRange.combatants[1].replacementAi.
                       desiredCombatDistance ==
                       0.7 * phase14RangeWeapon->maximumRange,
               "phase14 out-of-range obstruction must select a usable long-range installation");

        auto phase14UnblockedParams = phase14FireParams;
        phase14UnblockedParams.playerStartTransform =
            {64000.0, 0.0, 35000.0, 0.0};
        const auto phase14FirstShot = mw::battle::runBattleReplay(
            phase14UnblockedParams, {}, 1u);
        expect(phase14FirstShot.combatants[1].weaponShotsFired == 1u &&
                   phase14FirstShot.combatants[1].replacementAi.
                       selectedWeaponInstanceId == 1u &&
                   phase14FirstShot.combatants[1].replacementAi.
                       combatOrbitActive &&
                   phase14FirstShot.combatants[1].replacementAi.
                       orbitDirection != 0,
               "phase14 first eligible installation or mobile combat orbit changed: shots=" +
                   std::to_string(phase14FirstShot.combatants[1].weaponShotsFired) +
                   " weapon=" + std::to_string(phase14FirstShot.combatants[1].
                       replacementAi.selectedWeaponInstanceId) +
                   " orbit=" + std::to_string(phase14FirstShot.combatants[1].
                       replacementAi.combatOrbitActive ? 1 : 0) +
                   " direction=" + std::to_string(static_cast<int>(
                       phase14FirstShot.combatants[1].replacementAi.orbitDirection)) +
                   " distance=" + std::to_string(phase14FirstShot.combatants[1].
                       replacementAi.desiredCombatDistance));
        const auto phase14Cooldown = mw::battle::runBattleReplay(
            phase14UnblockedParams, {}, 2u);
        expect(phase14Cooldown.combatants[1].weaponShotsFired == 1u &&
                   phase14Cooldown.combatants[1].weapons[0].readiness ==
                       mw::battle::BattleWeaponReadiness::Cooldown,
               "phase14 must respect common installation cooldown");

        auto phase14CombatParams = phase14UnblockedParams;
        phase14CombatParams.stationaryTargetHitDiagnosticEnabled = false;
        phase14CombatParams.playerAlliedLaunchStates[0].roster.gunnerySkill =
            3u;
        phase14CombatParams.combatantLaunchStates[0].roster.gunnerySkill = 0u;
        const auto phase14CombatFirst = mw::battle::runBattleReplay(
            phase14CombatParams, {}, 1u);
        const auto phase14AllyFirstShot = std::find_if(
            phase14CombatFirst.shotEvents.begin(),
            phase14CombatFirst.shotEvents.end(),
            [&phase14CombatFirst](const auto& shot) {
                return shot.shooterEntityId ==
                    phase14CombatFirst.combatants[1].id;
            });
        auto phase14PoorGunneryParams = phase14CombatParams;
        phase14PoorGunneryParams.playerAlliedLaunchStates[0].roster.
            gunnerySkill = 0u;
        const auto phase14PoorGunneryFirst = mw::battle::runBattleReplay(
            phase14PoorGunneryParams, {}, 1u);
        const auto phase14PoorGunneryShot = std::find_if(
            phase14PoorGunneryFirst.shotEvents.begin(),
            phase14PoorGunneryFirst.shotEvents.end(),
            [&phase14PoorGunneryFirst](const auto& shot) {
                return shot.shooterEntityId ==
                    phase14PoorGunneryFirst.combatants[1].id;
            });
        expect(phase14AllyFirstShot != phase14CombatFirst.shotEvents.end() &&
                   phase14PoorGunneryShot !=
                       phase14PoorGunneryFirst.shotEvents.end() &&
                   phase14AllyFirstShot->replacementFireSolution &&
                   phase14AllyFirstShot->replacementTerrainLineOfSightClear &&
                   phase14AllyFirstShot->replacementAimAligned &&
                   phase14AllyFirstShot->replacementGunnerySkill == 3u &&
                   phase14PoorGunneryShot->replacementGunnerySkill == 0u &&
                   phase14PoorGunneryShot->replacementTargetNumber >
                       phase14AllyFirstShot->replacementTargetNumber,
               "phase14 combat must resolve aligned AI fire with campaign gunnery skill: events=" +
                   std::to_string(phase14CombatFirst.shotEvents.size()) +
                   ":allyShots=" + std::to_string(
                       phase14CombatFirst.combatants[1].weaponShotsFired) +
                   ":allyAim=" + std::to_string(
                       phase14CombatFirst.combatants[1].replacementAi.aimAligned) +
                   ":allyLosBlocked=" + std::to_string(
                       phase14CombatFirst.combatants[1].replacementAi.
                           terrainLineOfSightBlocked) +
                   ":excellentTN=" + std::to_string(
                       phase14AllyFirstShot ==
                               phase14CombatFirst.shotEvents.end()
                           ? -1
                           : phase14AllyFirstShot->replacementTargetNumber) +
                   ":poorTN=" + std::to_string(
                       phase14PoorGunneryShot ==
                               phase14PoorGunneryFirst.shotEvents.end()
                           ? -1
                           : phase14PoorGunneryShot->replacementTargetNumber));

        mw::battle::BattleWorld phase14CombatWorld =
            mw::battle::BattleWorld::create(phase14CombatParams);
        bool phase14CombatHit = false;
        bool phase14DistributedLocation = false;
        std::vector<uint32_t> phase14AllyWeaponIds;
        for (uint64_t tick = 0u; tick < 160u; ++tick) {
            phase14CombatWorld.tick();
            const auto frame = phase14CombatWorld.snapshot();
            for (const auto& shot : frame.shotEvents) {
                if (shot.shooterEntityId == frame.combatants[1].id) {
                    phase14AllyWeaponIds.push_back(shot.weaponInstanceId);
                }
                if (shot.replacementFireSolution && shot.hit) {
                    phase14CombatHit = true;
                    phase14DistributedLocation =
                        phase14DistributedLocation ||
                        shot.armorSection !=
                            mw::mech3d::MechArmorSectionId::CenterTorso;
                }
            }
            if (frame.result.valid) {
                break;
            }
        }
        std::sort(phase14AllyWeaponIds.begin(), phase14AllyWeaponIds.end());
        phase14AllyWeaponIds.erase(
            std::unique(
                phase14AllyWeaponIds.begin(), phase14AllyWeaponIds.end()),
            phase14AllyWeaponIds.end());
        const auto phase14CombatResolved = phase14CombatWorld.snapshot();
        expect(phase14CombatHit && phase14DistributedLocation &&
                   (phase14CombatResolved.combatants[1].weaponHitsLanded > 0u ||
                    phase14CombatResolved.combatants[2].weaponHitsLanded > 0u),
               "phase14 combat must rotate ready weapons and deliver distributed damage: hit=" +
                   std::to_string(phase14CombatHit) +
                   ":distributed=" + std::to_string(
                       phase14DistributedLocation) +
                   ":weapons=" + std::to_string(
                       phase14AllyWeaponIds.size()) +
                   ":allyLanded=" + std::to_string(
                       phase14CombatResolved.combatants[1].weaponHitsLanded) +
                   ":enemyLanded=" + std::to_string(
                       phase14CombatResolved.combatants[2].weaponHitsLanded) +
                   ":allyShots=" + std::to_string(
                       phase14CombatResolved.combatants[1].weaponShotsFired));

        auto phase14RotationParams = phase14CombatParams;
        phase14RotationParams.playerAlliedLaunchStates[0].mechPresetId =
            "warhammer";
        phase14RotationParams.combatantLaunchStates[0].mechPresetId =
            "battlemaster";
        mw::battle::BattleWorld phase14RotationWorld =
            mw::battle::BattleWorld::create(phase14RotationParams);
        std::vector<uint32_t> phase14RotationWeaponIds;
        for (uint64_t tick = 0u; tick < 12u; ++tick) {
            phase14RotationWorld.tick();
            const auto frame = phase14RotationWorld.snapshot();
            for (const auto& shot : frame.shotEvents) {
                if (shot.shooterEntityId == frame.combatants[1].id) {
                    phase14RotationWeaponIds.push_back(shot.weaponInstanceId);
                }
            }
            if (frame.result.valid) {
                break;
            }
        }
        std::sort(
            phase14RotationWeaponIds.begin(), phase14RotationWeaponIds.end());
        phase14RotationWeaponIds.erase(
            std::unique(
                phase14RotationWeaponIds.begin(),
                phase14RotationWeaponIds.end()),
            phase14RotationWeaponIds.end());
        expect(phase14RotationWeaponIds.size() > 1u,
               "phase14 combat must rotate among ready installations: " +
                   std::to_string(phase14RotationWeaponIds.size()));

        auto phase14LosParams = phase14CombatParams;
        mw::battle::BattleTerrainCollisionObstacle phase14LosObstacle;
        phase14LosObstacle.stableId = 1401u;
        phase14LosObstacle.centerX = 65400.0;
        phase14LosObstacle.centerZ = 35000.0;
        phase14LosObstacle.halfExtentX = 100.0;
        phase14LosObstacle.halfExtentZ = 500.0;
        phase14LosObstacle.height = 2000.0;
        phase14LosObstacle.footprint = {
            {65300.0, 34500.0}, {65500.0, 34500.0},
            {65500.0, 35500.0}, {65300.0, 35500.0}};
        phase14LosParams.terrainCollisionObstacles.valid = true;
        phase14LosParams.terrainCollisionObstacles.provenance =
            "phase14_combat_line_of_sight_probe";
        phase14LosParams.terrainCollisionObstacles.entries =
            {phase14LosObstacle};
        const auto phase14LosBlocked = mw::battle::runBattleReplay(
            phase14LosParams, {}, 1u);
        expect(phase14LosBlocked.combatants[1].replacementAi.
                       terrainLineOfSightBlocked &&
                   phase14LosBlocked.combatants[1].weaponShotsFired == 0u &&
                   phase14LosBlocked.combatants[2].replacementAi.
                       terrainLineOfSightBlocked &&
                   phase14LosBlocked.combatants[2].weaponShotsFired == 0u,
               "phase14 combat must hold fire when a mountain blocks the target");
        auto phase14NonfunctionalParams = phase14UnblockedParams;
        auto& phase14BrokenAlly =
            phase14NonfunctionalParams.playerAlliedLaunchStates[0];
        phase14BrokenAlly.persistentMechState.valid = true;
        phase14BrokenAlly.persistentMechState.weaponConditions.fill(3u);
        const auto phase14Nonfunctional = mw::battle::runBattleReplay(
            phase14NonfunctionalParams, {}, 1u);
        expect(phase14Nonfunctional.combatants[1].weaponShotsFired == 0u &&
                   phase14Nonfunctional.combatants[1].replacementAi.
                       selectedWeaponInstanceId == 0u &&
                   phase14Nonfunctional.combatants[1].replacementAi.
                       combatTargetEntityId ==
                       phase14Nonfunctional.combatants[2].id,
               "phase14 nonfunctional installations must reject fire");
        auto phase14EmptyAmmoParams = phase14UnblockedParams;
        auto& phase14EmptyAlly =
            phase14EmptyAmmoParams.playerAlliedLaunchStates[0];
        phase14EmptyAlly.persistentMechState.valid = true;
        phase14EmptyAlly.persistentMechState.weaponConditions[0] = 3u;
        phase14EmptyAlly.ammunitionStateValid = true;
        phase14EmptyAlly.ammunitionByPool.fill(0);
        const auto phase14EmptyAmmo = mw::battle::runBattleReplay(
            phase14EmptyAmmoParams, {}, 1u);
        expect(phase14EmptyAmmo.combatants[1].weaponShotsFired == 0u &&
                   phase14EmptyAmmo.combatants[1].replacementAi.
                       selectedWeaponInstanceId == 0u &&
                   phase14EmptyAmmo.combatants[1].replacementAi.
                       combatTargetEntityId ==
                       phase14EmptyAmmo.combatants[2].id &&
                   phase14EmptyAmmo.combatants[1].weapons[1].readiness ==
                       mw::battle::BattleWeaponReadiness::NoAmmunition &&
                   phase14EmptyAmmo.combatants[1].weapons[2].readiness ==
                       mw::battle::BattleWeaponReadiness::NoAmmunition,
               "phase14 must reject both installations on the empty shared ammo pool");
        auto phase14ShutdownParams = phase14UnblockedParams;
        auto& phase14ShutdownAlly =
            phase14ShutdownParams.playerAlliedLaunchStates[0];
        phase14ShutdownAlly.persistentMechState.valid = true;
        phase14ShutdownAlly.persistentMechState.engine = 3u;
        const auto phase14Shutdown = mw::battle::runBattleReplay(
            phase14ShutdownParams, {}, 1u);
        expect(phase14Shutdown.combatants[1].weaponShotsFired == 0u &&
                   phase14Shutdown.combatants[1].weapons[0].readiness ==
                       mw::battle::BattleWeaponReadiness::MechOffline,
               "phase14 engine shutdown must reject the common request");
        auto phase14HeatParams = phase14UnblockedParams;
        phase14HeatParams.playerAlliedLaunchStates[0].mechPresetId =
            "warhammer";
        phase14HeatParams.playerAlliedLaunchStates[0].persistentMechState.valid =
            true;
        phase14HeatParams.playerAlliedLaunchStates[0].persistentMechState.
            heatSinksWorking = 0u;
        phase14HeatParams.playerAlliedLaunchStates[0].persistentMechState.
            heatSinksTotal = 18u;
        phase14HeatParams.stationaryTargetHitDiagnosticEnabled = false;
        mw::battle::BattleWorld phase14HeatWorld =
            mw::battle::BattleWorld::create(phase14HeatParams);
        for (uint64_t tick = 0u; tick < 100u; ++tick) {
            phase14HeatWorld.tick();
            if (phase14HeatWorld.snapshot().combatants[1].heat.reactorShutdown) {
                break;
            }
        }
        const auto phase14Heat = phase14HeatWorld.snapshot();
        const uint64_t phase14ShotsAtHeatShutdown =
            phase14Heat.combatants[1].weaponShotsFired;
        phase14HeatWorld.runTicks(5u);
        const auto phase14HeatRejected = phase14HeatWorld.snapshot();
        expect(!phase14Heat.combatants[1].heat.reactorShutdown &&
                   phase14Heat.combatants[1].weaponShotsFired >= 2u &&
                   phase14Heat.combatants[1].heat.rawHeat < 0x5a0 &&
                   phase14HeatRejected.combatants[1].weaponShotsFired ==
                       phase14ShotsAtHeatShutdown,
               "phase14 must hold fire before the recovered AI heat gate: " +
                   std::to_string(phase14Heat.combatants[1].weaponShotsFired) + ":" +
                   std::to_string(phase14Heat.combatants[1].heat.rawHeat) + ":" +
                   std::to_string(phase14Heat.combatants[1].heat.reactorShutdown));

        auto phase14TerrainParams = phase14OneParams;
        phase14TerrainParams.playerAlliedLaunchStates[0].startTransform =
            {62000.0, 0.0, 35000.0, 1.5707963267948966};
        mw::battle::BattleTerrainCollisionObstacle phase14Obstacle;
        phase14Obstacle.stableId = 14u;
        phase14Obstacle.centerX = 62500.0;
        phase14Obstacle.centerZ = 35000.0;
        phase14Obstacle.halfExtentX = 100.0;
        phase14Obstacle.halfExtentZ = 100.0;
        phase14Obstacle.footprint = {
            {62400.0, 34900.0}, {62600.0, 34900.0},
            {62600.0, 35100.0}, {62400.0, 35100.0}};
        phase14TerrainParams.terrainCollisionObstacles.valid = true;
        phase14TerrainParams.terrainCollisionObstacles.provenance =
            "phase14_synthetic_local_navigation_probe";
        phase14TerrainParams.terrainCollisionObstacles.entries =
            {phase14Obstacle};
        const auto phase14Terrain = mw::battle::runBattleReplay(
            phase14TerrainParams, {}, 160u);
        expect(phase14Terrain.combatants[1].replacementAi.pathReplanCount > 0u &&
                   phase14Terrain.combatants[1].transform.x > 62600.0 &&
                   phase14Terrain.combatants[1].replacementAi.navMode !=
                       mw::battle::BattleReplacementAiNavMode::RouteBlocked,
               "phase14 terrain pathfinding must route past the obstacle without a permanent stop: " +
                   std::to_string(phase14Terrain.combatants[1].collisionCount) + ":" +
                   std::to_string(phase14Terrain.combatants[1].replacementAi.detourAttempts) + ":" +
                   std::to_string(phase14Terrain.combatants[1].transform.x) + ":" +
                   std::to_string(phase14Terrain.combatants[1].transform.z) + ":" +
                   std::to_string(phase14Terrain.combatants[1].replacementAi.pathReplanCount) + ":" +
                   std::to_string(phase14Terrain.combatants[1].replacementAi.pathFailureCount) + ":" +
                   std::to_string(phase14Terrain.combatants[1].replacementAi.navigationWaypointActive) + ":" +
                   std::to_string(phase14Terrain.combatants[1].replacementAi.navigationWaypoint.x) + ":" +
                   std::to_string(phase14Terrain.combatants[1].replacementAi.navigationWaypoint.z) + ":" +
                   std::to_string(static_cast<int>(phase14Terrain.combatants[1].replacementAi.navMode)));
        auto phase14BlockedRouteParams = phase14TerrainParams;
        auto& phase14Wall =
            phase14BlockedRouteParams.terrainCollisionObstacles.entries[0];
        phase14Wall.centerX = 63000.0;
        phase14Wall.centerZ = 35000.0;
        phase14Wall.halfExtentX = 600.0;
        phase14Wall.halfExtentZ = 5000.0;
        phase14Wall.footprint = {
            {62400.0, 30000.0}, {63600.0, 30000.0},
            {63600.0, 40000.0}, {62400.0, 40000.0}};
        const auto phase14BlockedRoute = mw::battle::runBattleReplay(
            phase14BlockedRouteParams, {}, 900u);
        const auto phase14BlockedRouteRepeat = mw::battle::runBattleReplay(
            phase14BlockedRouteParams, {}, 900u);
        expect(phase14BlockedRoute.combatants[1].replacementAi.pathReplanCount >
                       0u &&
                   phase14BlockedRoute.combatants[1].replacementAi.navMode !=
                       mw::battle::BattleReplacementAiNavMode::RouteBlocked &&
                   phase14BlockedRoute.combatants[1].transform.x > 63600.0,
               "phase14 pathfinding must keep trying and cross a long mountain wall: " +
                   std::to_string(phase14BlockedRoute.combatants[1].transform.x) + ":" +
                   std::to_string(phase14BlockedRoute.combatants[1].transform.z) + ":" +
                   std::to_string(phase14BlockedRoute.combatants[1].replacementAi.pathReplanCount) + ":" +
                   std::to_string(phase14BlockedRoute.combatants[1].replacementAi.pathFailureCount) + ":" +
                   std::to_string(phase14BlockedRoute.combatants[1].replacementAi.detourAttempts) + ":" +
                   std::to_string(static_cast<int>(phase14BlockedRoute.combatants[1].replacementAi.navMode)));
        expect(
            mw::battle::battleSnapshotFingerprint(phase14BlockedRoute) ==
                mw::battle::battleSnapshotFingerprint(
                    phase14BlockedRouteRepeat),
            "phase14 mountain-wall pathfinding must remain deterministic");

        auto phase14RetrieveParams = phase13CampaignFixtureForMission(11u);
        phase14RetrieveParams.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::ReplacementDeterministicCombatAi;
        phase14RetrieveParams.deterministicCollisionRuntimeEnabled = true;
        phase14RetrieveParams.terrainCollisionObstacles = {};
        phase14RetrieveParams.playerStartTransform =
            {60000.0, 0.0, 35000.0, 0.0};
        phase14RetrieveParams.objective.transform =
            {68000.0, 0.0, 35000.0, 0.0};
        for (size_t i = 0; i < 3u; ++i) {
            phase14RetrieveParams.playerAlliedLaunchStates[i].mechPresetId =
                "locust";
            phase14RetrieveParams.playerAlliedLaunchStates[i].startTransform =
                {67000.0, 0.0, 34400.0 + 600.0 * i, 1.5707963267948966};
        }
        phase14RetrieveParams.combatantLaunchStates[0].startTransform =
            {95000.0, 0.0, 50000.0, 0.0};
        const auto phase14Retrieve = mw::battle::runBattleReplay(
            phase14RetrieveParams, {}, 30u);
        expect(phase14Retrieve.objective.valid &&
                   phase14Retrieve.objective.missionIntent ==
                       mw::battle::BattleMissionObjectiveIntent::Retrieve &&
                   phase14Retrieve.retrieval.active &&
                   phase14Retrieve.retrieval.phase ==
                       mw::battle::BattleRetrievalPhase::AwaitingContact &&
                   !mw::battle::isValid(
                        phase14Retrieve.retrieval.contactEntityId) &&
                   phase14Retrieve.missionRuntimeState ==
                       mw::battle::BattleMissionRuntimeState::InProgress &&
                   !phase14Retrieve.result.valid &&
                   phase14Retrieve.combatants.size() == 5u,
               "phase14 retrieval arrival must not infer pickup or mission completion");
        for (size_t i = 0; i < 3u; ++i) {
            const auto& ally = phase14Retrieve.combatants[1u + i];
            expect(ally.replacementAi.active &&
                       ally.replacementAi.missionSlice ==
                           mw::battle::BattleReplacementAiMissionSlice::
                               RetrieveArrival &&
                       ally.replacementAi.approachSlot == i &&
                       ally.replacementAi.approachSlotCount == 3u &&
                       ally.replacementAi.movementDestination.x == 67400.0 &&
                       ally.replacementAi.movementDestination.z ==
                           34400.0 + 600.0 * i &&
                       ally.replacementAi.arrivalReached &&
                       ally.replacementAi.arrivalTickIndex > 0u &&
                       ally.replacementAi.arrivalTickIndex <
                           phase14Retrieve.tickIndex &&
                       ally.replacementAi.navMode ==
                           mw::battle::BattleReplacementAiNavMode::Arrived &&
                       ally.throttle == 0.0 && ally.turn == 0.0 &&
                       !mw::battle::isValid(
                           ally.replacementAi.combatTargetEntityId),
                   "phase14 retrieval companions must latch stable individual arrival slots");
            if (i > 0u) {
                const auto& previous = phase14Retrieve.combatants[i];
                expect(std::hypot(ally.transform.x - previous.transform.x,
                                  ally.transform.z - previous.transform.z) >=
                           ally.collisionRadiusWorld +
                               previous.collisionRadiusWorld,
                       "phase14 retrieval companions overlapped at arrival");
            }
        }
        const auto phase14RetrieveReplay = mw::battle::runBattleReplay(
            phase14RetrieveParams, {}, 30u);
        mw::battle::BattleWorld phase14RetrieveCadenceWorld =
            mw::battle::BattleWorld::create(phase14RetrieveParams);
        phase14RetrieveCadenceWorld.runTicks(10u);
        for (int frame = 0; frame < 19; ++frame) {
            static_cast<void>(phase14RetrieveCadenceWorld.snapshot());
        }
        phase14RetrieveCadenceWorld.runTicks(20u);
        expect(mw::battle::battleSnapshotFingerprint(phase14Retrieve) ==
                   mw::battle::battleSnapshotFingerprint(phase14RetrieveReplay) &&
                   mw::battle::battleSnapshotFingerprint(phase14Retrieve) ==
                       mw::battle::battleSnapshotFingerprint(
                           phase14RetrieveCadenceWorld.snapshot()),
               "phase14 retrieval replay, subdivision or render cadence changed the result");

        auto phase14RetrieveContactParams = phase14RetrieveParams;
        auto phase14RetrievePlayerLaunch =
            phase14RetrieveContactParams.playerAlliedLaunchStates[0];
        phase14RetrievePlayerLaunch.mechPresetId =
            phase14RetrieveContactParams.playerMechPresetId;
        phase14RetrievePlayerLaunch.startTransform =
            phase14RetrieveContactParams.objective.transform;
        phase14RetrieveContactParams.playerLaunchState =
            phase14RetrievePlayerLaunch;
        const auto phase14RetrieveContact = mw::battle::runBattleReplay(
            phase14RetrieveContactParams, {}, 2u);
        expect(phase14RetrieveContact.retrieval.active &&
                   phase14RetrieveContact.retrieval.phase ==
                       mw::battle::BattleRetrievalPhase::ContactedByPlayer &&
                   phase14RetrieveContact.retrieval.contactEntityId ==
                       phase14RetrieveContact.combatants[0].id &&
                   phase14RetrieveContact.retrieval.contactTickIndex == 1u &&
                   phase14RetrieveContact.retrieval.provenance ==
                       "replacement:wiki_retrieval_structure_contact_v1" &&
                   phase14RetrieveContact.missionRuntimeState ==
                       mw::battle::BattleMissionRuntimeState::Victory &&
                   phase14RetrieveContact.result.valid &&
                   phase14RetrieveContact.result.rawResultCode == 0 &&
                   phase14RetrieveContact.result.reason ==
                       "retrieval_structure_contact" &&
                   phase14RetrieveContact.tickIndex == 1u,
               "phase14 Wiki retrieval contact must immediately complete Victory");
        expectNear(
            phase14RetrieveContact.retrieval.contactAnchor.x,
            68000.0,
            0.001,
            "phase14 retrieval contact anchor x");
        expect(phase14RetrieveContact.retrieval.contactRadius == 250.0 &&
                   phase14RetrieveContact.objective.damage == 0 &&
                   phase14RetrieveContact.objective.maxDamage == 0 &&
                   phase14RetrieveContact.objective.damageSuppressed &&
                   !phase14RetrieveContact.objective.depleted,
               "phase14 Wiki retrieval structure must remain indestructible");

        const auto phase14RetrievalFieldHashed =
            [&phase14RetrieveContact](const auto& mutate,
                                      const std::string& field) {
                auto changed = phase14RetrieveContact;
                mutate(changed.retrieval);
                expect(mw::battle::battleSnapshotFingerprint(changed) !=
                           mw::battle::battleSnapshotFingerprint(
                               phase14RetrieveContact),
                        "phase14 retrieval fingerprint omitted " + field);
            };
        phase14RetrievalFieldHashed(
            [](auto& s) { s.active = false; }, "active");
        phase14RetrievalFieldHashed(
            [](auto& s) {
                s.phase = mw::battle::BattleRetrievalPhase::
                    AwaitingContact;
            },
            "phase");
        phase14RetrievalFieldHashed(
            [](auto& s) { s.provenance.push_back('x'); }, "provenance");
        phase14RetrievalFieldHashed(
            [](auto& s) { s.contactAnchor.x += 1.0; }, "contactAnchor");
        phase14RetrievalFieldHashed(
            [](auto& s) { s.contactEntityId.value += 1u; },
            "contactEntityId");
        phase14RetrievalFieldHashed(
            [](auto& s) { s.contactRadius += 1.0; }, "contactRadius");
        phase14RetrievalFieldHashed(
            [](auto& s) { ++s.contactTickIndex; }, "contactTickIndex");
        const auto phase14RetrieveContactRepeat =
            mw::battle::runBattleReplay(phase14RetrieveContactParams, {}, 2u);
        expect(mw::battle::battleSnapshotFingerprint(phase14RetrieveContact) ==
                   mw::battle::battleSnapshotFingerprint(
                       phase14RetrieveContactRepeat),
               "phase14 retrieval contact changed under replay");

        auto phase14UnprovenRetrieveParams = phase14RetrieveParams;
        phase14UnprovenRetrieveParams.objective.transformProven = false;
        const auto phase14UnprovenRetrieve = mw::battle::runBattleReplay(
            phase14UnprovenRetrieveParams, {}, 1u);
        expect(!phase14UnprovenRetrieve.retrieval.active &&
                   !phase14UnprovenRetrieve.combatants[1].replacementAi.active &&
                   phase14UnprovenRetrieve.missionRuntimeState ==
                       mw::battle::BattleMissionRuntimeState::InProgress,
               "phase14 retrieval runtime must fail closed without a proven marker transform");

        auto phase14RetrieveObstructionParams = phase14RetrieveParams;
        phase14RetrieveObstructionParams.playerAlliedLaunchStates.resize(1u);
        phase14RetrieveObstructionParams.playerAlliedLaunchStates[0].
            startTransform = {67300.0, 0.0, 35000.0, 1.5707963267948966};
        phase14RetrieveObstructionParams.combatantLaunchStates[0].startTransform =
            {67350.0, 0.0, 35000.0, 0.0};
        const auto phase14RetrieveObstruction = mw::battle::runBattleReplay(
            phase14RetrieveObstructionParams, {}, 1u);
        expect(!phase14RetrieveObstruction.combatants[1].replacementAi.
                       arrivalReached &&
                   phase14RetrieveObstruction.combatants[1].replacementAi.
                           combatTargetEntityId ==
                       phase14RetrieveObstruction.combatants[2].id &&
                   phase14RetrieveObstruction.combatants[1].replacementAi.navMode ==
                       mw::battle::BattleReplacementAiNavMode::EngageObstruction,
               "phase14 assigned retrieval route obstruction must prevent arrival latching");

        auto phase14ProtectParams = phase13CampaignFixtureForMission(2u);
        phase14ProtectParams.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::ReplacementDeterministicCombatAi;
        phase14ProtectParams.deterministicCollisionRuntimeEnabled = true;
        phase14ProtectParams.terrainCollisionObstacles = {};
        phase14ProtectParams.stationaryTargetHitDiagnosticEnabled = true;
        phase14ProtectParams.playerStartTransform =
            {60000.0, 0.0, 35000.0, 0.0};
        phase14ProtectParams.objective.transform =
            {65000.0, 0.0, 35000.0, 0.0};
        for (size_t i = 0; i < 3u; ++i) {
            phase14ProtectParams.playerAlliedLaunchStates[i].mechPresetId =
                "locust";
            phase14ProtectParams.playerAlliedLaunchStates[i].startTransform =
                {65700.0, 0.0, 34300.0 + 700.0 * i, 1.5707963267948966};
        }
        phase14ProtectParams.combatantLaunchStates[0].mechPresetId =
            "warhammer";
        phase14ProtectParams.combatantLaunchStates[0].startTransform =
            {68000.0, 0.0, 34300.0, 0.0};
        auto phase14SecondThreat =
            phase14ProtectParams.combatantLaunchStates[0];
        phase14SecondThreat.startTransform =
            {68500.0, 0.0, 35700.0, 0.0};
        phase14SecondThreat.roster.sourceSlot = "opposing:1";
        phase14ProtectParams.combatantLaunchStates.push_back(
            phase14SecondThreat);
        mw::battle::BattleWorld phase14ProtectWorld =
            mw::battle::BattleWorld::create(phase14ProtectParams);
        phase14ProtectWorld.tick();
        const auto phase14Protect = phase14ProtectWorld.snapshot();
        expect(phase14Protect.objective.missionIntent ==
                   mw::battle::BattleMissionObjectiveIntent::Protect &&
                   mw::battle::battleObjectiveProtectedByPlayer(
                       phase14Protect.objective) &&
                   phase14Protect.missionRuntimeState ==
                       mw::battle::BattleMissionRuntimeState::InProgress &&
                   phase14Protect.combatAiPolicyProvenance ==
                       "replacement:phase14_wiki_five_scenarios_v13_damage_retreat",
               "phase14 protect slice must require the bound protected objective: " +
                   std::to_string(static_cast<int>(
                       phase14Protect.objective.missionIntent)) + ":" +
                   std::to_string(mw::battle::battleObjectiveProtectedByPlayer(
                       phase14Protect.objective) ? 1 : 0) + ":" +
                   std::to_string(static_cast<int>(
                       phase14Protect.missionRuntimeState)) + ":" +
                   phase14Protect.combatAiPolicyProvenance);
        for (size_t i = 0; i < 3u; ++i) {
            const auto& defender = phase14Protect.combatants[1u + i];
            expect(defender.replacementAi.active &&
                       defender.replacementAi.missionSlice ==
                           mw::battle::BattleReplacementAiMissionSlice::
                               ProtectGarrisonDefense &&
                       defender.replacementAi.missionAnchor.x == 65000.0 &&
                       defender.replacementAi.missionAnchor.z == 35000.0 &&
                       defender.replacementAi.movementDestination.x == 65700.0 &&
                       defender.replacementAi.movementDestination.z ==
                           34300.0 + 700.0 * i,
                   "phase14 protect defenders must own stable anchor-facing guard slots");
        }
        expect(phase14Protect.combatants[1].replacementAi.combatTargetEntityId ==
                   phase14Protect.combatants[4].id &&
                   phase14Protect.combatants[2].replacementAi.
                           combatTargetEntityId ==
                       phase14Protect.combatants[5].id &&
                   phase14Protect.combatants[3].replacementAi.
                           combatTargetEntityId ==
                       phase14Protect.combatants[4].id &&
                   phase14Protect.combatants[1].replacementAi.navMode ==
                       mw::battle::BattleReplacementAiNavMode::EngageThreat &&
                   phase14Protect.combatants[2].replacementAi.navMode ==
                       mw::battle::BattleReplacementAiNavMode::EngageThreat &&
                   phase14Protect.combatants[3].replacementAi.navMode ==
                       mw::battle::BattleReplacementAiNavMode::EngageThreat &&
                   phase14Protect.combatants[1].weaponShotsFired == 1u &&
                   phase14Protect.combatants[2].weaponShotsFired == 1u,
               "phase14 protect defenders must distribute radar contacts and reinforce the remaining threat");

        auto phase14RadarDefenseParams = phase14ProtectParams;
        phase14RadarDefenseParams.combatantLaunchStates.resize(1u);
        phase14RadarDefenseParams.combatantLaunchStates[0].startTransform =
            {79000.0, 0.0, 35000.0, -1.5707963267948966};
        const auto phase14RadarDefense = mw::battle::runBattleReplay(
            phase14RadarDefenseParams, {}, 1u);
        const auto phase14RadarThreatId =
            phase14RadarDefense.combatants.back().id;
        for (size_t i = 0; i < 3u; ++i) {
            const auto& defender = phase14RadarDefense.combatants[1u + i];
            expect(defender.replacementAi.combatTargetEntityId ==
                       phase14RadarThreatId &&
                       defender.replacementAi.targetKind ==
                           mw::battle::BattleReplacementAiTargetKind::Combatant &&
                       defender.replacementAi.navMode ==
                           mw::battle::BattleReplacementAiNavMode::EngageThreat,
                   "phase14 defense companions must engage a team radar contact beyond the former short defense gate");
        }

        auto phase14ProtectAssaultParams = phase14ProtectParams;
        phase14ProtectAssaultParams.combatantLaunchStates.resize(1u);
        phase14ProtectAssaultParams.deterministicObjectiveRuntimeEnabled = true;
        phase14ProtectAssaultParams.objectiveMaxDamage = 100;
        mw::battle::BattleWorld phase14ProtectAssaultWorld =
            mw::battle::BattleWorld::create(phase14ProtectAssaultParams);
        phase14ProtectAssaultWorld.tick();
        const auto phase14ProtectAssault =
            phase14ProtectAssaultWorld.snapshot();
        const auto& phase14ProtectAttacker =
            phase14ProtectAssault.combatants[4];
        expect(phase14ProtectAttacker.replacementAi.active &&
                   phase14ProtectAttacker.replacementAi.missionSlice ==
                       mw::battle::BattleReplacementAiMissionSlice::
                           ProtectGarrisonDefense &&
                   phase14ProtectAttacker.replacementAi.targetKind ==
                       mw::battle::BattleReplacementAiTargetKind::Objective &&
                   !mw::battle::isValid(phase14ProtectAttacker.replacementAi.
                       combatTargetEntityId) &&
                   phase14ProtectAttacker.replacementAi.navMode ==
                       mw::battle::BattleReplacementAiNavMode::
                           AssaultObjective,
               "phase14 Protect opponent must own a typed objective target");
        expectNear(
            phase14ProtectAttacker.replacementAi.missionAnchor.x,
            65000.0,
            0.001,
            "phase14 Protect assault anchor x");
        expectNear(
            phase14ProtectAttacker.replacementAi.missionAnchor.z,
            35000.0,
            0.001,
            "phase14 Protect assault anchor z");
        expectNear(
            phase14ProtectAttacker.replacementAi.movementDestination.x,
            65000.0 + 3000.0 / std::hypot(3000.0, -700.0) *
                phase14ProtectAttacker.replacementAi.desiredCombatDistance,
            0.001,
            "phase14 Protect weapon-range assault slot x");
        expectNear(
            phase14ProtectAttacker.replacementAi.movementDestination.z,
            35000.0 - 700.0 / std::hypot(3000.0, -700.0) *
                phase14ProtectAttacker.replacementAi.desiredCombatDistance,
            0.001,
            "phase14 Protect weapon-range assault slot z");
        expect(phase14ProtectAttacker.weaponShotsFired == 1u &&
                   phase14ProtectAssault.lastShot.shooterEntityId ==
                       phase14ProtectAttacker.id &&
                   phase14ProtectAssault.lastShot.targetKind ==
                       mw::battle::BattleShotTargetKind::Objective &&
                   phase14ProtectAssault.lastShot.runtimeEffect ==
                       mw::battle::BattleShotRuntimeEffect::ObjectiveDamage &&
                   phase14ProtectAssault.lastShot.damageApplied > 0 &&
                   phase14ProtectAssault.objective.damage ==
                       phase14ProtectAssault.lastShot.damageAfter &&
                   phase14ProtectAssault.lastShot.provenance.find(
                       "replacement_phase14_protect_objective_assault_common_pipeline") !=
                       std::string::npos,
               "phase14 Protect objective assault must use common fire and objective damage ownership");

        auto phase14RetaliationParams = phase14ProtectAssaultParams;
        phase14RetaliationParams.playerLaunchState.reset();
        phase14RetaliationParams.playerAlliedLaunchStates.clear();
        phase14RetaliationParams.playerStartTransform =
            {67500.0, 0.0, 35000.0, 1.5707963267948966};
        phase14RetaliationParams.combatantLaunchStates[0].startTransform =
            {68000.0, 0.0, 35000.0, -1.5707963267948966};
        phase14RetaliationParams.weaponTargetPolicy =
            mw::battle::BattleWeaponTargetPolicy::
                SelectedScannerTargetProvisional;
        mw::battle::BattleWorld phase14RetaliationWorld =
            mw::battle::BattleWorld::create(phase14RetaliationParams);
        mw::battle::BattleInputCommand phase14RetaliationFire;
        phase14RetaliationFire.entityId =
            phase14RetaliationWorld.playerEntityId();
        phase14RetaliationFire.selectWeaponInstanceId = 1u;
        phase14RetaliationFire.cycleTargetScan = true;
        phase14RetaliationFire.fireWeapon = true;
        phase14RetaliationWorld.enqueueInput(phase14RetaliationFire);
        phase14RetaliationWorld.tick();
        const auto phase14RetaliationHit =
            phase14RetaliationWorld.snapshot();
        expect(phase14RetaliationHit.combatants.back().lastDamageSourceEntityId ==
                   phase14RetaliationHit.combatants.front().id,
               "defense attacker must remember the player that interrupted its structure attack");
        phase14RetaliationWorld.tick();
        const auto phase14Retaliation = phase14RetaliationWorld.snapshot();
        const auto& phase14RetaliatingAttacker =
            phase14Retaliation.combatants.back();
        expect(phase14RetaliatingAttacker.replacementAi.targetKind ==
                   mw::battle::BattleReplacementAiTargetKind::Combatant &&
                   phase14RetaliatingAttacker.replacementAi.
                           combatTargetEntityId ==
                       phase14Retaliation.combatants.front().id &&
                   phase14RetaliatingAttacker.replacementAi.
                           retaliationUntilTickIndex >
                       phase14Retaliation.tickIndex,
               "a structure attacker hit by the player must retaliate instead of continuing to fire at the structure");

        auto phase14CommittedRetaliationParams = phase14RetaliationParams;
        auto phase14SecondAttacker =
            phase14CommittedRetaliationParams.combatantLaunchStates[0];
        phase14SecondAttacker.mechPresetId = "locust";
        phase14SecondAttacker.roster.team = mw::battle::BattleTeam::Player;
        phase14SecondAttacker.roster.sourceSlot = "player:1";
        phase14SecondAttacker.startTransform =
            {90000.0, 0.0, 35000.0, -1.5707963267948966};
        phase14CommittedRetaliationParams.playerAlliedLaunchStates.push_back(
            phase14SecondAttacker);
        phase14CommittedRetaliationParams.objectiveRetaliationSeconds = 600.0;
        mw::battle::BattleWorld phase14CommittedRetaliationWorld =
            mw::battle::BattleWorld::create(
                phase14CommittedRetaliationParams);
        mw::battle::BattleInputCommand phase14CommittedRetaliationFire =
            phase14RetaliationFire;
        phase14CommittedRetaliationFire.entityId =
            phase14CommittedRetaliationWorld.playerEntityId();
        phase14CommittedRetaliationWorld.enqueueInput(
            phase14CommittedRetaliationFire);
        phase14CommittedRetaliationWorld.tick();
        auto phase14CommittedRetaliationFirstHit =
            phase14CommittedRetaliationWorld.snapshot();
        for (size_t tick = 0u; tick < 40u &&
             !mw::battle::isValid(
                 phase14CommittedRetaliationFirstHit.combatants.back().
                     replacementAi.retaliationTargetEntityId);
             ++tick) {
            phase14CommittedRetaliationWorld.tick();
            phase14CommittedRetaliationFirstHit =
                phase14CommittedRetaliationWorld.snapshot();
        }
        expect(
            phase14CommittedRetaliationFirstHit.combatants.back().
                    replacementAi.retaliationTargetEntityId ==
                phase14CommittedRetaliationFirstHit.combatants.front().id,
            "the first resolved attacker must establish the retaliation target");
        for (size_t tick = 0u; tick < 1200u; ++tick) {
            phase14CommittedRetaliationWorld.tick();
            const auto frame = phase14CommittedRetaliationWorld.snapshot();
            if (frame.combatants.back().lastDamageSourceEntityId ==
                    frame.combatants[1].id) {
                break;
            }
        }
        const auto phase14CommittedRetaliationHit =
            phase14CommittedRetaliationWorld.snapshot();
        const auto& phase14CommittedRetaliationTarget =
            phase14CommittedRetaliationHit.combatants.back();
        expect(
            phase14CommittedRetaliationTarget.lastDamageSourceEntityId ==
                    phase14CommittedRetaliationHit.combatants[1].id &&
                phase14CommittedRetaliationTarget.replacementAi.
                        retaliationTargetEntityId ==
                    phase14CommittedRetaliationHit.combatants.front().id,
            "retaliation must commit to the first attacker instead of switching torso on every incoming hit: last=" +
                std::to_string(phase14CommittedRetaliationTarget.
                    lastDamageSourceEntityId.value) +
                " ally=" + std::to_string(
                    phase14CommittedRetaliationHit.combatants[1].id.value) +
                " retaliation=" + std::to_string(
                    phase14CommittedRetaliationTarget.replacementAi.
                        retaliationTargetEntityId.value) +
                " player=" + std::to_string(
                    phase14CommittedRetaliationHit.combatants.front().id.value) +
                " ally_shots=" + std::to_string(
                    phase14CommittedRetaliationHit.combatants[1].
                        weaponShotsFired));

        auto phase14StructureCollisionParams = phase14ProtectAssaultParams;
        phase14StructureCollisionParams.playerLaunchState.reset();
        phase14StructureCollisionParams.playerAlliedLaunchStates.clear();
        phase14StructureCollisionParams.playerStartTransform =
            phase14StructureCollisionParams.objective.transform;
        phase14StructureCollisionParams.combatantLaunchStates[0].startTransform =
            {85000.0, 0.0, 35000.0, 0.0};
        mw::battle::BattleWorld phase14StructureCollisionWorld =
            mw::battle::BattleWorld::create(
                phase14StructureCollisionParams);
        phase14StructureCollisionWorld.tick();
        const auto phase14StructureCollision =
            phase14StructureCollisionWorld.snapshot();
        expect(phase14StructureCollision.combatants.front().collisionContact &&
                   phase14StructureCollision.lastCollision.kind ==
                       mw::battle::BattleCollisionKind::ObjectiveStructure &&
                   std::hypot(
                       phase14StructureCollision.combatants.front().transform.x -
                           phase14StructureCollision.objective.transform.x,
                       phase14StructureCollision.combatants.front().transform.z -
                           phase14StructureCollision.objective.transform.z) >=
                       phase14StructureCollisionParams.
                           objectiveCollisionRadiusWorld,
               "the defense structure must have a solid collision footprint");

        auto phase14ProtectDepletionParams = phase14ProtectAssaultParams;
        phase14ProtectDepletionParams.objectiveMaxDamage = 1;
        const auto phase14ProtectDepletion = mw::battle::runBattleReplay(
            phase14ProtectDepletionParams, {}, 1u);
        expect(phase14ProtectDepletion.objective.depleted &&
                   phase14ProtectDepletion.missionRuntimeState ==
                       mw::battle::BattleMissionRuntimeState::Defeat &&
                   phase14ProtectDepletion.objective.lastDamageSourceEntityId ==
                       phase14ProtectDepletion.combatants[4].id,
               "phase14 Protect objective assault must reach common protected-objective defeat");

        auto phase14RealProtectParams = phase14ProtectAssaultParams;
        phase14RealProtectParams.stationaryTargetHitDiagnosticEnabled = false;
        phase14RealProtectParams.playerAlliedLaunchStates.clear();
        phase14RealProtectParams.combatantLaunchStates[0].roster.gunnerySkill =
            3u;
        phase14RealProtectParams.objectiveMaxDamage = 100;
        mw::battle::BattleWorld phase14RealProtectWorld =
            mw::battle::BattleWorld::create(phase14RealProtectParams);
        bool phase14RealProtectShot = false;
        for (uint64_t tick = 0u; tick < 1000u; ++tick) {
            phase14RealProtectWorld.tick();
            const auto frame = phase14RealProtectWorld.snapshot();
            for (const auto& shot : frame.shotEvents) {
                phase14RealProtectShot = phase14RealProtectShot ||
                    (shot.replacementFireSolution &&
                     shot.replacementTerrainLineOfSightClear &&
                     shot.replacementAimAligned &&
                     shot.targetKind ==
                         mw::battle::BattleShotTargetKind::Objective);
            }
            if (frame.objective.damage > 0 || frame.result.valid) {
                break;
            }
        }
        const auto phase14RealProtect = phase14RealProtectWorld.snapshot();
        expect(phase14RealProtectShot &&
                   phase14RealProtect.objective.damage > 0,
               "phase14 real defense attacker must acquire and damage the protected structure: " +
                   std::to_string(phase14RealProtect.objective.damage) + ":" +
                   std::to_string(phase14RealProtect.combatants.back().
                       weaponShotsFired) + ":x=" +
                   std::to_string(phase14RealProtect.combatants.back().
                       transform.x) + ":z=" +
                   std::to_string(phase14RealProtect.combatants.back().
                       transform.z) + ":nav=" +
                   std::to_string(static_cast<int>(
                       phase14RealProtect.combatants.back().replacementAi.
                           navMode)) + ":weapon=" +
                   std::to_string(phase14RealProtect.combatants.back().
                       replacementAi.selectedWeaponInstanceId) + ":aim=" +
                   std::to_string(phase14RealProtect.combatants.back().
                       replacementAi.aimAligned) + ":los=" +
                   std::to_string(phase14RealProtect.combatants.back().
                       replacementAi.terrainLineOfSightBlocked) + ":err=" +
                   std::to_string(phase14RealProtect.combatants.back().
                       replacementAi.lastAimErrorRadians) + ":heading=" +
                   std::to_string(phase14RealProtect.combatants.back().
                       transform.headingRadians) + ":torso=" +
                   std::to_string(phase14RealProtect.combatants.back().
                       torsoYawRadians) + ":result=" +
                   std::to_string(static_cast<int>(
                       phase14RealProtect.missionRuntimeState)));
        const auto phase14ProtectAssaultReplay =
            mw::battle::runBattleReplay(
                phase14ProtectAssaultParams, {}, 12u);
        const auto phase14ProtectAssaultRepeat =
            mw::battle::runBattleReplay(
                phase14ProtectAssaultParams, {}, 12u);
        mw::battle::BattleWorld phase14ProtectAssaultCadenceWorld =
            mw::battle::BattleWorld::create(phase14ProtectAssaultParams);
        phase14ProtectAssaultCadenceWorld.runTicks(5u);
        static_cast<void>(phase14ProtectAssaultCadenceWorld.snapshot());
        phase14ProtectAssaultCadenceWorld.runTicks(7u);
        expect(mw::battle::battleSnapshotFingerprint(
                   phase14ProtectAssaultReplay) ==
                   mw::battle::battleSnapshotFingerprint(
                       phase14ProtectAssaultRepeat) &&
                   mw::battle::battleSnapshotFingerprint(
                       phase14ProtectAssaultReplay) ==
                   mw::battle::battleSnapshotFingerprint(
                       phase14ProtectAssaultCadenceWorld.snapshot()),
               "phase14 Protect objective assault changed under replay or tick subdivision");
        auto phase14ProtectTargetKindMutation = phase14ProtectAssault;
        phase14ProtectTargetKindMutation.combatants[4].replacementAi.targetKind =
            mw::battle::BattleReplacementAiTargetKind::None;
        expect(mw::battle::battleSnapshotFingerprint(phase14ProtectAssault) !=
                   mw::battle::battleSnapshotFingerprint(
                       phase14ProtectTargetKindMutation),
               "phase14 typed replacement target kind must remain fingerprint-owned");

        const auto phase14ProtectReplay = mw::battle::runBattleReplay(
            phase14ProtectParams, {}, 12u);
        mw::battle::BattleWorld phase14ProtectCadenceWorld =
            mw::battle::BattleWorld::create(phase14ProtectParams);
        phase14ProtectCadenceWorld.runTicks(5u);
        for (int frame = 0; frame < 17; ++frame) {
            static_cast<void>(phase14ProtectCadenceWorld.snapshot());
        }
        phase14ProtectCadenceWorld.runTicks(7u);
        expect(mw::battle::battleSnapshotFingerprint(phase14ProtectReplay) ==
                   mw::battle::battleSnapshotFingerprint(
                       phase14ProtectCadenceWorld.snapshot()),
               "phase14 protect subdivision or render cadence changed the result");

        auto phase14ProtectReturnParams = phase14ProtectParams;
        phase14ProtectReturnParams.combatantLaunchStates[1].startTransform =
            {86000.0, 0.0, 35700.0, 0.0};
        mw::battle::BattleWorld phase14ProtectReturnWorld =
            mw::battle::BattleWorld::create(phase14ProtectReturnParams);
        phase14ProtectReturnWorld.tick();
        const auto phase14ThreatBeforeLoss =
            phase14ProtectReturnWorld.snapshot();
        expect(phase14ThreatBeforeLoss.combatants[1].replacementAi.
                       combatTargetEntityId ==
                   phase14ThreatBeforeLoss.combatants[4].id &&
                   phase14ThreatBeforeLoss.combatants[2].replacementAi.
                           combatTargetEntityId ==
                       phase14ThreatBeforeLoss.combatants[4].id &&
                   phase14ThreatBeforeLoss.combatants[3].replacementAi.
                           combatTargetEntityId ==
                       phase14ThreatBeforeLoss.combatants[4].id,
               "phase14 protect team must reinforce the only radar-visible threat");
        phase14ProtectReturnWorld.setMissionStatus(
            phase14ThreatBeforeLoss.combatants[4].id,
            mw::battle::CombatantMissionStatus::Destroyed);
        phase14ProtectReturnWorld.tick();
        const auto phase14ThreatLost = phase14ProtectReturnWorld.snapshot();
        for (size_t i = 0; i < 3u; ++i) {
            expect(!mw::battle::isValid(phase14ThreatLost.combatants[1u + i].
                           replacementAi.combatTargetEntityId) &&
                       phase14ThreatLost.combatants[1u + i].replacementAi.navMode ==
                           mw::battle::BattleReplacementAiNavMode::Guarding &&
                       phase14ThreatLost.missionRuntimeState ==
                           mw::battle::BattleMissionRuntimeState::InProgress,
                   "phase14 lost radar threat must return every defender to its guard slot");
        }

        auto phase14GarrisonParams = phase13CampaignFixtureForMission(0u);
        phase14GarrisonParams.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::ReplacementDeterministicCombatAi;
        phase14GarrisonParams.deterministicCollisionRuntimeEnabled = true;
        phase14GarrisonParams.terrainCollisionObstacles = {};
        phase14GarrisonParams.playerStartTransform =
            {60000.0, 0.0, 35000.0, 0.0};
        phase14GarrisonParams.objective.transform =
            {20000.0, 0.0, 10000.0, 0.0};
        for (size_t i = 0; i < 3u; ++i) {
            phase14GarrisonParams.playerAlliedLaunchStates[i].mechPresetId =
                "locust";
            phase14GarrisonParams.playerAlliedLaunchStates[i].startTransform =
                {60700.0, 0.0, 34300.0 + 700.0 * i, 1.5707963267948966};
        }
        phase14GarrisonParams.combatantLaunchStates[0].startTransform =
            {68000.0, 0.0, 35000.0, 0.0};
        const auto phase14Garrison = mw::battle::runBattleReplay(
            phase14GarrisonParams, {}, 1u);
        expect(phase14Garrison.objective.missionIntentProven &&
                   phase14Garrison.objective.missionIntent ==
                       mw::battle::BattleMissionObjectiveIntent::Protect &&
                   mw::battle::battleObjectiveProtectedByPlayer(
                       phase14Garrison.objective) &&
                   !phase14Garrison.objective.damageSuppressed &&
                   phase14Garrison.objective.maxDamage == 300 &&
                   phase14Garrison.combatants[1].replacementAi.missionSlice ==
                       mw::battle::BattleReplacementAiMissionSlice::
                           ProtectGarrisonDefense,
               "phase14 Garrison Duty must bind and protect its structure");
        for (size_t i = 0; i < 3u; ++i) {
            const auto& defender = phase14Garrison.combatants[1u + i];
            expect(defender.replacementAi.active &&
                       defender.replacementAi.missionAnchor.x == 20000.0 &&
                       defender.replacementAi.missionAnchor.z == 10000.0,
                   "phase14 garrison defenders must guard the Wiki structure");
        }
        const auto& phase14GarrisonAttacker = phase14Garrison.combatants[4];
        expect(phase14GarrisonAttacker.replacementAi.active &&
                   phase14GarrisonAttacker.replacementAi.targetKind ==
                       mw::battle::BattleReplacementAiTargetKind::Objective &&
                   !mw::battle::isValid(phase14GarrisonAttacker.replacementAi.
                       combatTargetEntityId) &&
                   phase14GarrisonAttacker.replacementAi.missionAnchor.x ==
                       20000.0 &&
                   phase14GarrisonAttacker.replacementAi.missionAnchor.z ==
                       10000.0 &&
                   phase14GarrisonAttacker.replacementAi.navMode ==
                       mw::battle::BattleReplacementAiNavMode::AssaultObjective,
               "phase14 garrison opponent must immediately prioritize the structure");

        for (const size_t defenseMissionId : {1u, 7u}) {
            auto wikiDefenseParams =
                phase13CampaignFixtureForMission(defenseMissionId);
            wikiDefenseParams.combatAiPolicy =
                mw::battle::BattleCombatAiPolicy::
                    ReplacementDeterministicCombatAi;
            wikiDefenseParams.deterministicCollisionRuntimeEnabled = true;
            const auto wikiDefense = mw::battle::runBattleReplay(
                wikiDefenseParams, {}, 1u);
            expect(mw::battle::battleObjectiveProtectedByPlayer(
                       wikiDefense.objective) &&
                       wikiDefense.objective.activeObjectProven &&
                       wikiDefense.combatants[1].replacementAi.active &&
                       wikiDefense.combatants.back().replacementAi.targetKind ==
                           mw::battle::BattleReplacementAiTargetKind::Objective,
                   "all Wiki defense titles must bind a protected structure");
        }

        auto phase14DeathmatchParams =
            phase13CampaignFixtureForMission(8u);
        phase14DeathmatchParams.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::ReplacementDeterministicCombatAi;
        phase14DeathmatchParams.deterministicCollisionRuntimeEnabled = true;
        phase14DeathmatchParams.terrainCollisionObstacles = {};
        phase14DeathmatchParams.stationaryTargetHitDiagnosticEnabled = true;
        phase14DeathmatchParams.playerLaunchState.reset();
        phase14DeathmatchParams.playerStartTransform =
            {60000.0, 0.0, 35000.0, 0.0};
        for (size_t i = 0; i < 3u; ++i) {
            phase14DeathmatchParams.playerAlliedLaunchStates[i].mechPresetId =
                "locust";
            phase14DeathmatchParams.playerAlliedLaunchStates[i].startTransform =
                {61000.0, 0.0, 34000.0 + 1000.0 * i,
                 1.5707963267948966};
        }
        phase14DeathmatchParams.combatantLaunchStates[0].mechPresetId =
            "locust";
        phase14DeathmatchParams.combatantLaunchStates[0].startTransform =
            {65000.0, 0.0, 34000.0, -1.5707963267948966};
        for (size_t i = 1u; i < 3u; ++i) {
            auto opposing = phase14DeathmatchParams.combatantLaunchStates[0];
            opposing.startTransform =
                {65000.0, 0.0, 34000.0 + 1000.0 * i,
                 -1.5707963267948966};
            opposing.roster.sourceSlot =
                "opposing:" + std::to_string(i);
            phase14DeathmatchParams.combatantLaunchStates.push_back(opposing);
        }
        mw::battle::BattleWorld phase14DeathmatchWorld =
            mw::battle::BattleWorld::create(phase14DeathmatchParams);
        phase14DeathmatchWorld.tick();
        const auto phase14Deathmatch = phase14DeathmatchWorld.snapshot();
        expect(phase14Deathmatch.mission.family ==
                   mw::battle::BattleMissionFamily::Deathmatch &&
                   phase14Deathmatch.combatants.size() == 7u &&
                   !phase14Deathmatch.combatants[0].replacementAi.active &&
                   phase14Deathmatch.combatAiPolicyProvenance ==
                        "replacement:phase14_wiki_five_scenarios_v13_damage_retreat" &&
                   phase14Deathmatch.lastShot.provenance.find(
                       "replacement_phase14_balanced_opponent_common_pipeline") ==
                       0u,
               "phase14 deathmatch must activate both non-player sides only");
        for (size_t i = 0; i < 3u; ++i) {
            const auto& ally = phase14Deathmatch.combatants[1u + i];
            const auto& opponent = phase14Deathmatch.combatants[4u + i];
            expect(ally.replacementAi.active &&
                       ally.replacementAi.missionSlice ==
                           mw::battle::BattleReplacementAiMissionSlice::
                               DeathmatchEngagement &&
                       ally.replacementAi.approachSlot == i &&
                       ally.replacementAi.approachSlotCount == 3u &&
                       ally.replacementAi.missionAnchor.x == 65000.0 &&
                       ally.replacementAi.missionAnchor.z == 35000.0 &&
                       ally.replacementAi.movementDestination.x == 64100.0 &&
                       ally.replacementAi.movementDestination.z ==
                           34200.0 + 800.0 * i &&
                       ally.replacementAi.combatTargetEntityId == opponent.id &&
                       ally.replacementAi.navMode ==
                           mw::battle::BattleReplacementAiNavMode::
                               EngageOpponent &&
                       ally.weaponShotsFired == 1u,
                   "phase14 player-side deathmatch assignment or engagement line changed");
            expect(opponent.replacementAi.active &&
                       opponent.replacementAi.missionSlice ==
                           mw::battle::BattleReplacementAiMissionSlice::
                               DeathmatchEngagement &&
                       opponent.replacementAi.approachSlot == i &&
                       opponent.replacementAi.approachSlotCount == 3u &&
                       opponent.replacementAi.missionAnchor.x == 60750.0 &&
                       opponent.replacementAi.missionAnchor.z == 35000.0 &&
                       opponent.replacementAi.movementDestination.x == 61650.0 &&
                       opponent.replacementAi.movementDestination.z ==
                           35800.0 - 800.0 * i &&
                       opponent.replacementAi.combatTargetEntityId == ally.id &&
                       opponent.replacementAi.navMode ==
                           mw::battle::BattleReplacementAiNavMode::
                               EngageOpponent &&
                       opponent.weaponShotsFired == 1u,
                     "phase14 opposing-side deathmatch assignment or engagement line changed");
        }

        auto phase14RetreatParams = phase14DeathmatchParams;
        phase14RetreatParams.playerAlliedLaunchStates.clear();
        phase14RetreatParams.combatantLaunchStates.resize(1u);
        phase14RetreatParams.playerStartTransform =
            {65000.0, 0.0, 35000.0, 1.5707963267948966};
        phase14RetreatParams.combatantLaunchStates[0].startTransform =
            {69000.0, 0.0, 35000.0, 1.5707963267948966};
        phase14RetreatParams.combatantLaunchStates[0].persistentMechState.valid =
            true;
        phase14RetreatParams.combatantLaunchStates[0].persistentMechState.
            armorDamage.fill(2u);
        phase14RetreatParams.enemyRetreatOnDamageEnabled = true;
        phase14RetreatParams.enemyRetreatRemainingDurabilityPercent = 35;
        phase14RetreatParams.stationaryTargetHitDiagnosticEnabled = false;
        phase14RetreatParams.battlefieldBoundary.valid = true;
        phase14RetreatParams.battlefieldBoundary.outcomeEvaluationDeferred =
            false;
        phase14RetreatParams.battlefieldBoundary.worldMinX = 55000.0;
        phase14RetreatParams.battlefieldBoundary.worldMaxX = 70000.0;
        phase14RetreatParams.battlefieldBoundary.worldMinZ = 25000.0;
        phase14RetreatParams.battlefieldBoundary.worldMaxZ = 45000.0;
        auto phase14DamagedButFightingParams = phase14RetreatParams;
        phase14DamagedButFightingParams.combatantLaunchStates[0].
            persistentMechState.armorDamage.fill(1u);
        const auto phase14DamagedButFighting = mw::battle::runBattleReplay(
            phase14DamagedButFightingParams, {}, 1u);
        expect(
            !phase14DamagedButFighting.combatants[1].replacementAi.retreating &&
                phase14DamagedButFighting.combatants[1].replacementAi.
                        targetKind ==
                    mw::battle::BattleReplacementAiTargetKind::Combatant,
            "moderately damaged replacement opponent must remain in combat");
        mw::battle::BattleWorld phase14RetreatWorld =
            mw::battle::BattleWorld::create(phase14RetreatParams);
        phase14RetreatWorld.tick();
        const auto phase14RetreatStart = phase14RetreatWorld.snapshot();
        expect(
            phase14RetreatStart.combatants.size() == 2u &&
                phase14RetreatStart.combatants[1].replacementAi.retreating &&
                phase14RetreatStart.combatants[1].replacementAi.navMode ==
                    mw::battle::BattleReplacementAiNavMode::
                        RetreatToBoundary &&
                phase14RetreatStart.combatants[1].enemyAiState ==
                    mw::battle::BattleEnemyAiState::Retreat &&
                phase14RetreatStart.combatants[1].replacementAi.targetKind ==
                    mw::battle::BattleReplacementAiTargetKind::None &&
                phase14RetreatStart.combatants[1].replacementAi.
                        selectedWeaponInstanceId == 0u &&
                phase14RetreatStart.combatants[1].weaponShotsFired == 0u &&
                phase14RetreatStart.combatants[1].replacementAi.
                        retreatDestination.x >
                    phase14RetreatParams.battlefieldBoundary.worldMaxX,
            "heavily damaged replacement opponent must stop firing and retreat away from its threat");
        bool phase14OpponentEscaped = false;
        for (uint64_t tick = 0u; tick < 240u; ++tick) {
            phase14RetreatWorld.tick();
            const auto frame = phase14RetreatWorld.snapshot();
            if (frame.combatants[1].missionStatus ==
                mw::battle::CombatantMissionStatus::Escaped) {
                phase14OpponentEscaped = true;
                expect(!frame.combatants[1].mechDestroyed &&
                           frame.conclusionDelay.active,
                       "boundary retreat must preserve escaped rather than destroyed status");
                break;
            }
        }
        expect(phase14OpponentEscaped,
               "damaged replacement opponent did not reach the battlefield boundary");
        phase14RetreatWorld.runTicks(120u);
        const auto phase14RetreatComplete = phase14RetreatWorld.snapshot();
        expect(phase14RetreatComplete.missionRuntimeState ==
                   mw::battle::BattleMissionRuntimeState::Victory &&
                   phase14RetreatComplete.result.valid &&
                   phase14RetreatComplete.result.reason ==
                       "all_opposing_mechs_destroyed_or_escaped",
               "last escaped opponent must complete the mission after the conclusion delay");

        auto phase14AlliedRetreatParams = phase14RetreatParams;
        phase14AlliedRetreatParams.playerStartTransform =
            {59000.0, 0.0, 35000.0, 1.5707963267948966};
        phase14AlliedRetreatParams.playerAlliedLaunchStates.resize(1u);
        phase14AlliedRetreatParams.playerAlliedLaunchStates[0].mechPresetId =
            "warhammer";
        phase14AlliedRetreatParams.playerAlliedLaunchStates[0].roster.team =
            mw::battle::BattleTeam::Player;
        phase14AlliedRetreatParams.playerAlliedLaunchStates[0].roster.sourceSlot =
            "player:1";
        phase14AlliedRetreatParams.playerAlliedLaunchStates[0].startTransform =
            {69000.0, 0.0, 35000.0, 1.5707963267948966};
        phase14AlliedRetreatParams.playerAlliedLaunchStates[0].persistentMechState.valid =
            true;
        phase14AlliedRetreatParams.playerAlliedLaunchStates[0].persistentMechState.
            armorDamage.fill(2u);
        phase14AlliedRetreatParams.combatantLaunchStates[0].startTransform =
            {64000.0, 0.0, 35000.0, 1.5707963267948966};
        phase14AlliedRetreatParams.combatantLaunchStates[0].persistentMechState = {};
        mw::battle::BattleWorld phase14AlliedRetreatWorld =
            mw::battle::BattleWorld::create(phase14AlliedRetreatParams);
        phase14AlliedRetreatWorld.tick();
        const auto phase14AlliedRetreatStart =
            phase14AlliedRetreatWorld.snapshot();
        expect(
            phase14AlliedRetreatStart.combatants.size() == 3u &&
                phase14AlliedRetreatStart.combatants[1].roster.team ==
                    mw::battle::BattleTeam::Player &&
                !phase14AlliedRetreatStart.combatants[1].playerControlled &&
                phase14AlliedRetreatStart.combatants[1].replacementAi.retreating &&
                phase14AlliedRetreatStart.combatants[1].replacementAi.navMode ==
                    mw::battle::BattleReplacementAiNavMode::RetreatToBoundary &&
                phase14AlliedRetreatStart.combatants[1].replacementAi.
                        selectedWeaponInstanceId == 0u,
            "heavily damaged player companion must stop firing and retreat");
        bool phase14AllyEscaped = false;
        for (uint64_t tick = 0u; tick < 240u; ++tick) {
            phase14AlliedRetreatWorld.tick();
            const auto frame = phase14AlliedRetreatWorld.snapshot();
            if (frame.combatants[1].missionStatus ==
                mw::battle::CombatantMissionStatus::Escaped) {
                phase14AllyEscaped = true;
                expect(!frame.combatants[1].mechDestroyed &&
                           frame.missionRuntimeState ==
                               mw::battle::BattleMissionRuntimeState::InProgress,
                       "companion boundary retreat must preserve the mech and keep the battle running");
                break;
            }
        }
        expect(phase14AllyEscaped,
               "damaged player companion did not reach the battlefield boundary");

        auto phase14FireCadenceParams = phase14DeathmatchParams;
        phase14FireCadenceParams.playerStartTransform =
            {30000.0, 0.0, 35000.0, 0.0};
        phase14FireCadenceParams.playerAlliedLaunchStates.resize(1u);
        phase14FireCadenceParams.playerAlliedLaunchStates[0].mechPresetId =
            "warhammer";
        phase14FireCadenceParams.playerAlliedLaunchStates[0].startTransform =
            {60000.0, 0.0, 35000.0, 1.5707963267948966};
        phase14FireCadenceParams.combatantLaunchStates.resize(1u);
        phase14FireCadenceParams.combatantLaunchStates[0].mechPresetId =
            "warhammer";
        phase14FireCadenceParams.combatantLaunchStates[0].startTransform =
            {63000.0, 0.0, 35000.0, -1.5707963267948966};
        phase14FireCadenceParams.stationaryTargetHitDiagnosticEnabled = false;
        mw::battle::BattleWorld phase14FireCadenceWorld =
            mw::battle::BattleWorld::create(phase14FireCadenceParams);
        const uint64_t phase14FireCadenceTicks =
            static_cast<uint64_t>(std::ceil(
                0.4 / phase14FireCadenceParams.fixedTickSeconds));
        phase14FireCadenceWorld.runTicks(phase14FireCadenceTicks);
        const auto phase14FireCadenceBefore =
            phase14FireCadenceWorld.snapshot();
        phase14FireCadenceWorld.tick();
        const auto phase14FireCadenceAfter =
            phase14FireCadenceWorld.snapshot();
        expect(phase14FireCadenceBefore.combatants[1].weaponShotsFired == 1u &&
                   phase14FireCadenceAfter.combatants[1].weaponShotsFired == 2u &&
                   phase14FireCadenceAfter.combatants[1].replacementAi.
                           nextFireDecisionTickIndex ==
                       2u * phase14FireCadenceTicks,
               "replacement combat AI must stagger ready barrels on a 0.4 second cadence: before=" +
                   std::to_string(phase14FireCadenceBefore.combatants[1].
                       weaponShotsFired) +
                   " after=" + std::to_string(phase14FireCadenceAfter.
                       combatants[1].weaponShotsFired) +
                   " next=" + std::to_string(phase14FireCadenceAfter.
                       combatants[1].replacementAi.nextFireDecisionTickIndex));

        auto phase14OrbitParams = phase14DeathmatchParams;
        phase14OrbitParams.playerStartTransform =
            {30000.0, 0.0, 35000.0, 0.0};
        phase14OrbitParams.playerAlliedLaunchStates.resize(1u);
        phase14OrbitParams.playerAlliedLaunchStates[0].mechPresetId = "locust";
        phase14OrbitParams.playerAlliedLaunchStates[0].startTransform =
            {60000.0, 0.0, 35000.0, 1.5707963267948966};
        phase14OrbitParams.combatantLaunchStates.resize(1u);
        phase14OrbitParams.combatantLaunchStates[0].mechPresetId = "locust";
        phase14OrbitParams.combatantLaunchStates[0].startTransform =
            {62500.0, 0.0, 35000.0, -1.5707963267948966};
        phase14OrbitParams.stationaryTargetHitDiagnosticEnabled = false;
        mw::battle::BattleWorld phase14OrbitWorld =
            mw::battle::BattleWorld::create(phase14OrbitParams);
        phase14OrbitWorld.tick();
        const auto phase14OrbitStart = phase14OrbitWorld.snapshot();
        const auto phase14OrbitStartPosition =
            phase14OrbitStart.combatants[1].transform;
        const int8_t phase14OrbitDirection =
            phase14OrbitStart.combatants[1].replacementAi.orbitDirection;
        phase14OrbitWorld.runTicks(119u);
        const auto phase14OrbitEnd = phase14OrbitWorld.snapshot();
        const auto& phase14OrbitAlly = phase14OrbitEnd.combatants[1];
        expect(phase14OrbitStart.combatants[1].replacementAi.combatOrbitActive &&
                   phase14OrbitDirection != 0 &&
                   phase14OrbitAlly.replacementAi.combatOrbitActive &&
                   phase14OrbitAlly.replacementAi.orbitDirection ==
                       phase14OrbitDirection &&
                   std::hypot(
                       phase14OrbitAlly.transform.x -
                           phase14OrbitStartPosition.x,
                       phase14OrbitAlly.transform.z -
                           phase14OrbitStartPosition.z) > 100.0 &&
                   std::abs(phase14OrbitAlly.torsoYawStep) >
                       phase14OrbitParams.maxPlayerTorsoYawSteps,
               "replacement combat AI must keep a stable moving orbit with torso aim");

        auto phase14AimRecoveryParams = phase14OrbitParams;
        phase14AimRecoveryParams.playerAlliedLaunchStates[0].mechPresetId =
            "marauder";
        phase14AimRecoveryParams.playerAlliedLaunchStates[0].startTransform =
            {60000.0, 0.0, 35000.0, 0.0};
        phase14AimRecoveryParams.combatantLaunchStates[0].mechPresetId =
            "locust";
        phase14AimRecoveryParams.combatantLaunchStates[0].startTransform =
            {62000.0, 0.0, 35000.0, -1.5707963267948966};
        mw::battle::BattleWorld phase14AimRecoveryWorld =
            mw::battle::BattleWorld::create(phase14AimRecoveryParams);
        phase14AimRecoveryWorld.tick();
        const auto phase14AimRecoveryStart =
            phase14AimRecoveryWorld.snapshot();
        const auto& phase14TurningMarauder =
            phase14AimRecoveryStart.combatants[1];
        expect(
            phase14TurningMarauder.replacementAi.aimRecoveryActive &&
                phase14TurningMarauder.throttle == 0.0 &&
                std::abs(phase14TurningMarauder.torsoYawStep) == 1 &&
                phase14TurningMarauder.replacementAi.
                        nextTorsoTurnTickIndex >
                    phase14AimRecoveryStart.tickIndex &&
                phase14TurningMarauder.weaponShotsFired == 0u,
            "an unaligned Marauder must brake and slew its torso at the bounded rate");
        phase14AimRecoveryWorld.tick();
        const auto phase14AimRecoverySecond =
            phase14AimRecoveryWorld.snapshot();
        expect(
            std::abs(phase14AimRecoverySecond.combatants[1].torsoYawStep) ==
                1,
            "AI torso slew must not jump another six-degree step on the next 50 ms tick");
        phase14AimRecoveryWorld.runTicks(198u);
        const auto phase14AimRecoveryEnd =
            phase14AimRecoveryWorld.snapshot();
        expect(
            phase14AimRecoveryEnd.combatants[1].weaponShotsFired > 0u,
            "aim recovery must let the Marauder finish turning and fire before abandoning combat");

        const auto phase14DeathmatchReplay = mw::battle::runBattleReplay(
            phase14DeathmatchParams, {}, 20u);
        const auto phase14DeathmatchRepeat = mw::battle::runBattleReplay(
            phase14DeathmatchParams, {}, 20u);
        mw::battle::BattleWorld phase14DeathmatchCadenceWorld =
            mw::battle::BattleWorld::create(phase14DeathmatchParams);
        phase14DeathmatchCadenceWorld.runTicks(8u);
        for (int frame = 0; frame < 21; ++frame) {
            static_cast<void>(phase14DeathmatchCadenceWorld.snapshot());
        }
        phase14DeathmatchCadenceWorld.runTicks(12u);
        expect(mw::battle::battleSnapshotFingerprint(phase14DeathmatchReplay) ==
                   mw::battle::battleSnapshotFingerprint(
                       phase14DeathmatchRepeat) &&
                   mw::battle::battleSnapshotFingerprint(phase14DeathmatchReplay) ==
                       mw::battle::battleSnapshotFingerprint(
                           phase14DeathmatchCadenceWorld.snapshot()),
               "phase14 deathmatch replay, subdivision or render cadence changed the result");

        phase14DeathmatchWorld.setMissionStatus(
            phase14Deathmatch.combatants[4].id,
            mw::battle::CombatantMissionStatus::Destroyed);
        phase14DeathmatchWorld.tick();
        const auto phase14DeathmatchRetarget =
            phase14DeathmatchWorld.snapshot();
        size_t phase14AssignmentsToSecond = 0u;
        size_t phase14AssignmentsToThird = 0u;
        for (size_t i = 0; i < 3u; ++i) {
            const auto target = phase14DeathmatchRetarget.combatants[1u + i].
                replacementAi.combatTargetEntityId;
            expect(target != phase14DeathmatchRetarget.combatants[4].id,
                   "phase14 deathmatch retained a destroyed target");
            phase14AssignmentsToSecond +=
                target == phase14DeathmatchRetarget.combatants[5].id ? 1u : 0u;
            phase14AssignmentsToThird +=
                target == phase14DeathmatchRetarget.combatants[6].id ? 1u : 0u;
        }
        expect(phase14AssignmentsToSecond + phase14AssignmentsToThird == 3u &&
                   std::max(phase14AssignmentsToSecond,
                            phase14AssignmentsToThird) -
                           std::min(phase14AssignmentsToSecond,
                                    phase14AssignmentsToThird) <= 1u,
               "phase14 deathmatch loss must rebalance surviving targets");

        auto phase14SingleOpponentParams = phase14DeathmatchParams;
        phase14SingleOpponentParams.combatantLaunchStates.resize(1u);
        const auto phase14SingleOpponent = mw::battle::runBattleReplay(
            phase14SingleOpponentParams, {}, 1u);
        for (size_t i = 0; i < 3u; ++i) {
            expect(phase14SingleOpponent.combatants[1u + i].replacementAi.
                       combatTargetEntityId ==
                       phase14SingleOpponent.combatants[4].id,
                   "phase14 single-opponent deathmatch must allow balanced shared focus");
        }

        auto phase14DistantDeathmatchParams = phase14DeathmatchParams;
        for (size_t i = 0; i < 3u; ++i) {
            phase14DistantDeathmatchParams.combatantLaunchStates[i].
                startTransform.x = 74000.0;
        }
        const auto phase14DistantDeathmatch = mw::battle::runBattleReplay(
            phase14DistantDeathmatchParams, {}, 1u);
        for (size_t i = 0; i < 3u; ++i) {
            const auto& ally = phase14DistantDeathmatch.combatants[1u + i];
            const auto& opponent = phase14DistantDeathmatch.combatants[4u + i];
            expect(mw::battle::isValid(
                       ally.replacementAi.combatTargetEntityId) &&
                       ally.replacementAi.navMode ==
                           mw::battle::BattleReplacementAiNavMode::
                               EngageOpponent &&
                       ally.forwardSpeed > 0.0 &&
                       mw::battle::isValid(
                           opponent.replacementAi.combatTargetEntityId) &&
                       opponent.replacementAi.navMode ==
                           mw::battle::BattleReplacementAiNavMode::
                               EngageOpponent &&
                       opponent.forwardSpeed > 0.0,
                   "phase14 Wiki deathmatch actors must charge from any starting distance");
        }

        for (const size_t guardedMissionId : {24u, 11u}) {
            auto guardedFarParams =
                phase13CampaignFixtureForMission(guardedMissionId);
            guardedFarParams.combatAiPolicy =
                mw::battle::BattleCombatAiPolicy::
                    ReplacementDeterministicCombatAi;
            guardedFarParams.deterministicCollisionRuntimeEnabled = true;
            guardedFarParams.terrainCollisionObstacles = {};
            guardedFarParams.playerStartTransform =
                {39000.0, 0.0, 35000.0, 1.5707963267948966};
            guardedFarParams.objective.transform =
                {60000.0, 0.0, 35000.0, 0.0};
            for (size_t i = 0; i < 3u; ++i) {
                guardedFarParams.playerAlliedLaunchStates[i].startTransform =
                    {39000.0, 0.0, 34000.0 + 1000.0 * i,
                     1.5707963267948966};
            }
            guardedFarParams.combatantLaunchStates[0].startTransform =
                {61000.0, 0.0, 35000.0, -1.5707963267948966};
            const auto guardedFar = mw::battle::runBattleReplay(
                guardedFarParams, {}, 1u);
            const auto& farGuard = guardedFar.combatants.back();
            expect(farGuard.replacementAi.active &&
                       !farGuard.replacementAi.missionTriggered &&
                       farGuard.replacementAi.targetKind ==
                           mw::battle::BattleReplacementAiTargetKind::None &&
                       !mw::battle::isValid(
                           farGuard.replacementAi.combatTargetEntityId) &&
                       farGuard.replacementAi.navMode ==
                           mw::battle::BattleReplacementAiNavMode::
                               HoldingObjectiveGuard,
                   "Wiki assault/retrieval guard must hold outside the 4000m radar envelope");

            auto guardedNearParams = guardedFarParams;
            guardedNearParams.playerStartTransform.x = 41000.0;
            const auto guardedNear = mw::battle::runBattleReplay(
                guardedNearParams, {}, 1u);
            const auto& nearGuard = guardedNear.combatants.back();
            expect(nearGuard.replacementAi.missionTriggered &&
                       nearGuard.replacementAi.targetKind ==
                           mw::battle::BattleReplacementAiTargetKind::Combatant &&
                       mw::battle::isValid(
                           nearGuard.replacementAi.combatTargetEntityId) &&
                       nearGuard.replacementAi.navMode ==
                           mw::battle::BattleReplacementAiNavMode::
                               EngageOpponent,
                   "Wiki assault/retrieval guard must charge at the 4000m radar envelope");
        }

        auto wikiAssaultObjectiveParams =
            phase13CampaignFixtureForMission(24u);
        wikiAssaultObjectiveParams.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::ReplacementDeterministicCombatAi;
        wikiAssaultObjectiveParams.deterministicCollisionRuntimeEnabled = true;
        wikiAssaultObjectiveParams.terrainCollisionObstacles = {};
        wikiAssaultObjectiveParams.playerAlliedLaunchStates.clear();
        wikiAssaultObjectiveParams.playerStartTransform =
            {59000.0, 0.0, 35000.0, 1.5707963267948966};
        wikiAssaultObjectiveParams.objective.transform =
            {60000.0, 0.0, 35000.0, 0.0};
        wikiAssaultObjectiveParams.objective.maxDamage = 1;
        wikiAssaultObjectiveParams.combatantLaunchStates[0].startTransform =
            {85000.0, 0.0, 35000.0, -1.5707963267948966};
        wikiAssaultObjectiveParams.weaponTargetPolicy =
            mw::battle::BattleWeaponTargetPolicy::
                SelectedScannerTargetProvisional;
        wikiAssaultObjectiveParams.stationaryTargetHitDiagnosticEnabled = true;
        mw::battle::BattleWorld wikiAssaultObjectiveWorld =
            mw::battle::BattleWorld::create(wikiAssaultObjectiveParams);
        mw::battle::BattleInputCommand wikiAssaultObjectiveFire;
        wikiAssaultObjectiveFire.entityId =
            wikiAssaultObjectiveWorld.playerEntityId();
        wikiAssaultObjectiveFire.selectWeaponInstanceId = 1u;
        wikiAssaultObjectiveFire.cycleTargetScan = true;
        wikiAssaultObjectiveFire.fireWeapon = true;
        wikiAssaultObjectiveWorld.enqueueInput(wikiAssaultObjectiveFire);
        wikiAssaultObjectiveWorld.tick();
        const auto wikiAssaultObjective =
            wikiAssaultObjectiveWorld.snapshot();
        expect(wikiAssaultObjective.objective.depleted &&
                   wikiAssaultObjective.result.valid &&
                   wikiAssaultObjective.result.state ==
                       mw::battle::BattleMissionRuntimeState::Victory &&
                   wikiAssaultObjective.result.reason ==
                       "objective_depletion_player_win" &&
                   wikiAssaultObjective.result.rawResultCode == 0,
               "Wiki assault must allow victory by destroying the structure");

        mw::battle::BattleWorld wikiTeamLossWorld =
            mw::battle::BattleWorld::create(phase14DeathmatchParams);
        const auto wikiTeamLossStart = wikiTeamLossWorld.snapshot();
        wikiTeamLossWorld.setMissionStatus(
            wikiTeamLossStart.combatants[0].id,
            mw::battle::CombatantMissionStatus::Destroyed);
        wikiTeamLossWorld.tick();
        expect(wikiTeamLossWorld.snapshot().missionRuntimeState ==
                   mw::battle::BattleMissionRuntimeState::InProgress,
               "Wiki missions must continue while a player-side lancemate survives");
        const auto wikiTeamLossMid = wikiTeamLossWorld.snapshot();
        for (size_t i = 1u; i <= 3u; ++i) {
            wikiTeamLossWorld.setMissionStatus(
                wikiTeamLossMid.combatants[i].id,
                mw::battle::CombatantMissionStatus::Destroyed);
        }
        wikiTeamLossWorld.tick();
        wikiTeamLossWorld.runTicks(50u);
        const auto wikiTeamLoss = wikiTeamLossWorld.snapshot();
        expect(wikiTeamLoss.result.valid &&
                   wikiTeamLoss.result.state ==
                       mw::battle::BattleMissionRuntimeState::Defeat &&
                   wikiTeamLoss.result.reason ==
                       "all_player_mechs_destroyed",
               "Wiki missions must fail when the whole player lance is destroyed");

        auto wikiFleeParams = phase14DeathmatchParams;
        wikiFleeParams.playerStartTransform =
            {1.0, 0.0, 20000.0, -1.5707963267948966};
        wikiFleeParams.playerLaunchState.reset();
        mw::battle::BattleWorld wikiFleeWorld =
            mw::battle::BattleWorld::create(wikiFleeParams);
        wikiFleeWorld.enqueueInput(mw::battle::BattleInputCommand{
            0, wikiFleeWorld.playerEntityId(), 1.0, 0.0});
        wikiFleeWorld.runTicks(4u);
        const auto wikiFlee = wikiFleeWorld.snapshot();
        expect(wikiFlee.result.valid &&
                   wikiFlee.result.state ==
                       mw::battle::BattleMissionRuntimeState::Defeat &&
                   wikiFlee.result.reason == "player_fled_battlefield" &&
                   !wikiFlee.result.exitAllowed,
               "crossing any map edge must count as fleeing in Wiki missions");

        auto wikiSprintParams = phase13CampaignFixtureForMission(9u);
        wikiSprintParams.combatAiPolicy =
            mw::battle::BattleCombatAiPolicy::ReplacementDeterministicCombatAi;
        wikiSprintParams.deterministicCollisionRuntimeEnabled = true;
        wikiSprintParams.terrainCollisionObstacles = {};
        wikiSprintParams.playerStartTransform =
            {1900.0, 0.0, 100.0, 0.0};
        wikiSprintParams.battlefieldBoundary.valid = true;
        wikiSprintParams.battlefieldBoundary.exitMaskSemanticsProven = true;
        wikiSprintParams.battlefieldBoundary.worldMinX = 0.0;
        wikiSprintParams.battlefieldBoundary.worldMaxX = 2000.0;
        wikiSprintParams.battlefieldBoundary.worldMinZ = 0.0;
        wikiSprintParams.battlefieldBoundary.worldMaxZ = 1000.0;
        wikiSprintParams.battlefieldBoundary.playerAllowedExitMask = 0;
        wikiSprintParams.battlefieldBoundary.opposingAllowedExitMask = 0;
        wikiSprintParams.playerAlliedLaunchStates[0].startTransform =
            {1900.0, 0.0, 900.0, 0.0};
        wikiSprintParams.playerAlliedLaunchStates[1].startTransform =
            {1900.0, 0.0, 900.0, 0.0};
        wikiSprintParams.playerAlliedLaunchStates[2].startTransform =
            {1900.0, 0.0, 100.0, 0.0};
        wikiSprintParams.combatantLaunchStates[0].mechPresetId =
            "battlemaster";
        wikiSprintParams.combatantLaunchStates[0].startTransform =
            {1850.0, 0.0, 500.0, 1.5707963267948966};
        mw::battle::BattleWorld wikiSprintWorld =
            mw::battle::BattleWorld::create(wikiSprintParams);
        const auto wikiSprintStart = wikiSprintWorld.snapshot();
        for (size_t i = 1u; i <= 3u; ++i) {
            wikiSprintWorld.setMissionStatus(
                wikiSprintStart.combatants[i].id,
                mw::battle::CombatantMissionStatus::Disabled);
        }
        wikiSprintWorld.runTicks(120u);
        const auto wikiSprintEscape = wikiSprintWorld.snapshot();
        expect(wikiSprintEscape.result.valid &&
                   wikiSprintEscape.result.state ==
                       mw::battle::BattleMissionRuntimeState::Defeat &&
                   wikiSprintEscape.result.reason ==
                       "sprint_opponent_escaped",
               "Wiki sprint must fail when any opposing mech reaches the far edge");

        mw::battle::BattleWorld wikiSprintWinWorld =
            mw::battle::BattleWorld::create(wikiSprintParams);
        const auto wikiSprintWinStart = wikiSprintWinWorld.snapshot();
        wikiSprintWinWorld.setMissionStatus(
            wikiSprintWinStart.combatants.back().id,
            mw::battle::CombatantMissionStatus::Destroyed);
        wikiSprintWinWorld.tick();
        wikiSprintWinWorld.runTicks(50u);
        const auto wikiSprintWin = wikiSprintWinWorld.snapshot();
        expect(wikiSprintWin.result.valid &&
                   wikiSprintWin.result.state ==
                       mw::battle::BattleMissionRuntimeState::Victory &&
                   wikiSprintWin.result.reason ==
                       "all_opposing_mechs_destroyed",
               "Wiki sprint must win when all runners are destroyed");

        bool phase14RejectedIncompleteRuntime = false;
        try {
            auto invalidPhase14 = phase14OneParams;
            invalidPhase14.deterministicCollisionRuntimeEnabled = false;
            static_cast<void>(mw::battle::BattleWorld::create(invalidPhase14));
        } catch (const std::runtime_error&) {
            phase14RejectedIncompleteRuntime = true;
        }
        expect(phase14RejectedIncompleteRuntime,
               "phase14 policy must require common combat and collision runtime ownership");
        auto phase13GarrisonMission =
            phase13CampaignFixtureForMission(0u);
        const auto phase13GarrisonActivation =
            mw::battle::battleOriginalAiCampaignMovementActivation(
                phase13GarrisonMission);
        auto phase13DefenseMission =
            phase13CampaignFixtureForMission(2u);
        const auto phase13DefenseActivation =
            mw::battle::battleOriginalAiCampaignMovementActivation(
                phase13DefenseMission);
        auto phase13SuppressionMission =
            phase13CampaignFixtureForMission(8u);
        const auto phase13SuppressionActivation =
            mw::battle::battleOriginalAiCampaignMovementActivation(
                phase13SuppressionMission);
        auto phase13RescueMission =
            phase13CampaignFixtureForMission(11u);
        const auto phase13RescueActivation =
            mw::battle::battleOriginalAiCampaignMovementActivation(
                phase13RescueMission);
        expect(
            !phase13CampaignExplicitActivation.activate &&
                phase13CampaignExplicitActivation.selectedPolicy ==
                    mw::battle::BattleCombatAiPolicy::Phase10CompatibilityFsm &&
                !phase13CampaignObjectiveActivation.activate &&
                !phase13CampaignObjectiveActivation.
                    activeSeparateObjectiveAbsent &&
                !phase13CampaignMode3Activation.activate &&
                !phase13CampaignMode3Activation.
                    nonObjectiveModesSupported &&
                !phase13CampaignMode3BothNoObjectiveActivation.activate &&
                phase13CampaignMode3BothNoObjectiveActivation.
                    modeThreeNullDataRouteSupported &&
                phase13CampaignMode3BothNoObjectiveActivation.
                    modeThreeNullDataCampaignRejected &&
                !phase13CampaignMode3BothNoObjectiveActivation.
                    modeThreeSelectedTargetCompatibilityActivated &&
                !phase13CampaignMode3BothNoObjectiveActivation.
                    modeThreeMissionObjectiveCompatibilityActivated &&
                !phase13CampaignMode3BothNoObjectiveActivation.
                    actOnOwnSelectedTargetRoutesSupported &&
                phase13CampaignMode3BothNoObjectiveActivation.selectedPolicy ==
                    mw::battle::BattleCombatAiPolicy::Disabled &&
                phase13CampaignMode3ObjectiveActivation.activate &&
                phase13CampaignMode3ObjectiveActivation.
                    modeThreeNullDataCampaignRejected &&
                !phase13CampaignMode3ObjectiveActivation.
                    modeThreeSelectedTargetCompatibilityActivated &&
                phase13CampaignMode3ObjectiveActivation.
                    modeThreeMissionObjectiveCompatibilityActivated &&
                phase13CampaignMode3ObjectiveActivation.selectedPolicy ==
                    mw::battle::BattleCombatAiPolicy::
                        CompatibilityModeThreePlayerCompanionMissionObjectiveRepeatedMovement &&
                !phase13CampaignMode1Activation.activate &&
                phase13CampaignMode1Activation.playerActOnOwnRoute.route ==
                    mw::battle::BattleOriginalAiActOnOwnRoute::
                        ModeOneTablePoint &&
                !phase13CampaignMode1Activation.
                    actOnOwnSelectedTargetRoutesSupported &&
                !phase13CampaignNoAllyActivation.activate &&
                !phase13CampaignNoAllyActivation.
                    activePlayerCompanionPresent &&
                !phase13GarrisonActivation.activate &&
                phase13GarrisonActivation.playerActOnOwnRoute.route ==
                    mw::battle::BattleOriginalAiActOnOwnRoute::
                        StatusSevenReturn &&
                !phase13DefenseActivation.activate &&
                phase13DefenseActivation.playerActOnOwnRoute.route ==
                    mw::battle::BattleOriginalAiActOnOwnRoute::
                        StatusSevenReturn &&
                phase13SuppressionActivation.activate &&
                phase13SuppressionActivation.
                    playerSelectedTargetRouteSupported &&
                phase13SuppressionActivation.
                    playerOnlySelectedTargetActivated &&
                phase13SuppressionActivation.opposingActOnOwnRoute.route ==
                    mw::battle::BattleOriginalAiActOnOwnRoute::
                        StatusSevenReturn &&
                phase13SuppressionActivation.selectedPolicy ==
                    mw::battle::BattleCombatAiPolicy::
                        OriginalBtechNonObjectiveModesMultiTargetRepeatedMovement &&
                phase13RescueActivation.activate &&
                phase13RescueActivation.
                    modeThreeMissionObjectiveCompatibilityActivated &&
                phase13RescueMission.objective.missionIntent ==
                    mw::battle::BattleMissionObjectiveIntent::Retrieve &&
                phase13RescueMission.objective.missionTargetKind ==
                    "hostages" &&
                phase13RescueActivation.selectedPolicy ==
                    mw::battle::BattleCombatAiPolicy::
                        CompatibilityModeThreePlayerCompanionMissionObjectiveRepeatedMovement,
            "campaign movement activation no longer preserves unsupported/explicit policies");

        phase13SuppressionMission.combatAiPolicy =
            phase13SuppressionActivation.selectedPolicy;
        const auto phase13SuppressionMovement =
            mw::battle::runBattleReplay(
                phase13SuppressionMission, {}, 1u);
        phase13RescueMission.combatAiPolicy =
            phase13RescueActivation.selectedPolicy;
        const auto phase13RescueMovement =
            mw::battle::runBattleReplay(
                phase13RescueMission, {}, 1u);
        const auto phase13SuppressionSustained =
            mw::battle::runBattleReplay(
                phase13SuppressionMission, {}, 10u);
        const auto phase13RescueSustained =
            mw::battle::runBattleReplay(
                phase13RescueMission, {}, 10u);
        size_t phase13SuppressionSelectedCompanions = 0u;
        for (uint8_t playerSlot = 1u; playerSlot < 4u; ++playerSlot) {
            const auto& suppressionCompanion =
                phase13CombatantAtLiveSlot(
                    phase13SuppressionMovement, playerSlot);
            const auto& rescueCompanion =
                phase13CombatantAtLiveSlot(
                    phase13RescueMovement, playerSlot);
            const auto& suppressionSustainedCompanion =
                phase13CombatantAtLiveSlot(
                    phase13SuppressionSustained, playerSlot);
            const auto& rescueSustainedCompanion =
                phase13CombatantAtLiveSlot(
                    phase13RescueSustained, playerSlot);
            const bool suppressionSelected =
                suppressionCompanion.originalAiMotion.
                    lastMovementTargetLiveObjectSlot == 4u;
            if (suppressionSelected) {
                ++phase13SuppressionSelectedCompanions;
            }
            expect(
                suppressionCompanion.originalAiMotion.
                        movementUpdateAttemptCount == 1u &&
                    (suppressionSelected
                         ? suppressionSustainedCompanion.originalAiMotion.
                               acceptedPoseCommitCount >= 1u
                         : suppressionSustainedCompanion.originalAiMotion.
                                   lastMovementUpdateResult ==
                               mw::battle::
                                   BattleOriginalAiFirstMovementUpdateResult::
                                       RejectedTargetSelection) &&
                    rescueCompanion.originalAiMotion.
                        movementUpdateAttemptCount == 1u &&
                    rescueCompanion.originalAiMotion.
                        lastMovementTargetLiveObjectSlot == 8u &&
                    rescueCompanion.originalAiMotion.
                        lastMovementTargetEntityId.value == 0u &&
                    rescueSustainedCompanion.originalAiMotion.
                        acceptedPoseCommitCount >= 1u,
                "three-companion suppression/rescue movement route changed at player slot " +
                    std::to_string(playerSlot) +
                    " suppression_attempts=" +
                    std::to_string(suppressionCompanion.originalAiMotion.
                        movementUpdateAttemptCount) +
                    " suppression_target=" +
                    std::to_string(suppressionCompanion.originalAiMotion.
                        lastMovementTargetLiveObjectSlot) +
                    " suppression_commits10=" +
                    std::to_string(suppressionSustainedCompanion.
                        originalAiMotion.acceptedPoseCommitCount) +
                    " suppression_result10=" +
                    std::to_string(static_cast<int>(
                        suppressionSustainedCompanion.originalAiMotion.
                            lastMovementUpdateResult)) +
                    " rescue_attempts=" +
                    std::to_string(rescueCompanion.originalAiMotion.
                        movementUpdateAttemptCount) +
                    " rescue_target=" +
                    std::to_string(rescueCompanion.originalAiMotion.
                        lastMovementTargetLiveObjectSlot) +
                    " rescue_commits10=" +
                    std::to_string(rescueSustainedCompanion.originalAiMotion.
                        acceptedPoseCommitCount) +
                    " rescue_result10=" +
                    std::to_string(static_cast<int>(
                        rescueSustainedCompanion.originalAiMotion.
                            lastMovementUpdateResult)));
        }
        expect(
            phase13SuppressionSelectedCompanions == 1u &&
                phase13CombatantAtLiveSlot(
                    phase13SuppressionMovement, 1u).
                        originalAiMotion.lastMovementTargetLiveObjectSlot ==
                    4u &&
                phase13CombatantAtLiveSlot(
                phase13SuppressionMovement, 4u).
                    originalAiMotion.acceptedPoseCommitCount == 0u &&
                phase13CombatantAtLiveSlot(
                    phase13RescueMovement, 4u).
                    originalAiMotion.acceptedPoseCommitCount == 0u &&
                mw::battle::battleSnapshotFingerprint(
                    phase13SuppressionSustained) ==
                    mw::battle::battleSnapshotFingerprint(
                        mw::battle::runBattleReplay(
                            phase13SuppressionMission, {}, 10u)) &&
                mw::battle::battleSnapshotFingerprint(
                    phase13RescueSustained) ==
                    mw::battle::battleSnapshotFingerprint(
                        mw::battle::runBattleReplay(
                            phase13RescueMission, {}, 10u)),
            "mission-specific companion movement lost stationary opposition or replay equality");

        phase13CampaignMode3Objective.combatAiPolicy =
            phase13CampaignMode3ObjectiveActivation.selectedPolicy;
        const auto phase13CampaignMode3Compatibility =
            mw::battle::runBattleReplay(
                phase13CampaignMode3Objective, {}, 1u);
        const auto& phase13CampaignMode3CompanionOne =
            phase13CombatantAtLiveSlot(
                phase13CampaignMode3Compatibility, 1u);
        const auto& phase13CampaignMode3CompanionTwo =
            phase13CombatantAtLiveSlot(
                phase13CampaignMode3Compatibility, 2u);
        const auto& phase13CampaignMode3Opponent =
            phase13CombatantAtLiveSlot(
                phase13CampaignMode3Compatibility, 4u);
        expect(
            phase13CampaignMode3CompanionOne.originalAiMotion.
                    lastMovementTargetLiveObjectSlot == 8u &&
                phase13CampaignMode3CompanionTwo.originalAiMotion.
                    lastMovementTargetLiveObjectSlot == 8u &&
                phase13CampaignMode3CompanionOne.originalAiMotion.
                    lastMovementTargetEntityId.value == 0u &&
                phase13CampaignMode3CompanionTwo.originalAiMotion.
                    lastMovementTargetEntityId.value == 0u,
            "mode-3 mission compatibility route lost separate objective ownership");
        const auto& phase13CampaignMode3TargetSlot =
            phase13CampaignMode3Objective.setupMetadata.opposingSlots[
                phase13CampaignMode3Objective.setupMetadata.
                    objectiveOpposingSlotIndex];
        const auto phase13Mode3DistanceSquared = [](
                                                     int32_t fromX,
                                                     int32_t fromZ,
                                                     int32_t toX,
                                                     int32_t toZ) {
            const int64_t deltaX = static_cast<int64_t>(toX) - fromX;
            const int64_t deltaZ = static_cast<int64_t>(toZ) - fromZ;
            return deltaX * deltaX + deltaZ * deltaZ;
        };
        const int64_t phase13CampaignMode3DistanceBeforeOne =
            phase13Mode3DistanceSquared(
                phase13CampaignMode3Objective.setupMetadata.playerSlots[1].
                    originalRawX,
                phase13CampaignMode3Objective.setupMetadata.playerSlots[1].
                    originalRawZ,
                phase13CampaignMode3TargetSlot.originalRawX,
                phase13CampaignMode3TargetSlot.originalRawZ);
        const int64_t phase13CampaignMode3DistanceAfterOne =
            phase13Mode3DistanceSquared(
                phase13CampaignMode3CompanionOne.originalAiMotion.rawX,
                phase13CampaignMode3CompanionOne.originalAiMotion.rawZ,
                phase13CampaignMode3TargetSlot.originalRawX,
                phase13CampaignMode3TargetSlot.originalRawZ);
        const int64_t phase13CampaignMode3DistanceBeforeTwo =
            phase13Mode3DistanceSquared(
                phase13CampaignMode3Objective.setupMetadata.playerSlots[2].
                    originalRawX,
                phase13CampaignMode3Objective.setupMetadata.playerSlots[2].
                    originalRawZ,
                phase13CampaignMode3TargetSlot.originalRawX,
                phase13CampaignMode3TargetSlot.originalRawZ);
        const int64_t phase13CampaignMode3DistanceAfterTwo =
            phase13Mode3DistanceSquared(
                phase13CampaignMode3CompanionTwo.originalAiMotion.rawX,
                phase13CampaignMode3CompanionTwo.originalAiMotion.rawZ,
                phase13CampaignMode3TargetSlot.originalRawX,
                phase13CampaignMode3TargetSlot.originalRawZ);
        expect(
            phase13CampaignMode3CompanionOne.originalAiMotion.initialized &&
                phase13CampaignMode3CompanionTwo.originalAiMotion.initialized &&
                phase13CampaignMode3CompanionOne.originalAiMotion.
                    acceptedPoseCommitCount == 1u &&
                phase13CampaignMode3CompanionTwo.originalAiMotion.
                    acceptedPoseCommitCount == 1u &&
                phase13CampaignMode3CompanionOne.originalAiMotion.provenance.find(
                    "compatibility_provisional") == 0u &&
                phase13CampaignMode3CompanionTwo.originalAiMotion.provenance.find(
                    "compatibility_provisional") == 0u &&
                phase13CampaignMode3CompanionOne.originalAiMotion.
                    rawSpeedWord3c == 5 &&
                phase13CampaignMode3CompanionTwo.originalAiMotion.
                    rawSpeedWord3c == 5 &&
                phase13CampaignMode3DistanceAfterOne <
                    phase13CampaignMode3DistanceBeforeOne &&
                phase13CampaignMode3DistanceAfterTwo <
                    phase13CampaignMode3DistanceBeforeTwo &&
                phase13CampaignMode3Opponent.originalAiMotion.
                    acceptedPoseCommitCount == 0u,
            "mode-3 mission compatibility route did not move both companions toward the separate objective");
        const auto phase13CampaignMode3CompatibilityReplay =
            mw::battle::runBattleReplay(
                phase13CampaignMode3Objective, {}, 1u);
        expect(
            mw::battle::battleSnapshotFingerprint(
                phase13CampaignMode3Compatibility) ==
                mw::battle::battleSnapshotFingerprint(
                    phase13CampaignMode3CompatibilityReplay),
            "mode-3 mission-objective compatibility movement changed under replay");
        auto phase13CampaignMode3HalfTickParams =
            phase13CampaignMode3Objective;
        phase13CampaignMode3HalfTickParams.fixedTickSeconds = 0.05;
        const auto phase13CampaignMode3HalfTick =
            mw::battle::runBattleReplay(
                phase13CampaignMode3HalfTickParams, {}, 2u);
        const auto& phase13CampaignMode3HalfTickCompanionOne =
            phase13CombatantAtLiveSlot(
                phase13CampaignMode3HalfTick, 1u);
        const auto& phase13CampaignMode3HalfTickCompanionTwo =
            phase13CombatantAtLiveSlot(
                phase13CampaignMode3HalfTick, 2u);
        expect(
            phase13CampaignMode3HalfTickCompanionOne.originalAiMotion.rawX ==
                    phase13CampaignMode3CompanionOne.originalAiMotion.rawX &&
                phase13CampaignMode3HalfTickCompanionOne.originalAiMotion.rawZ ==
                    phase13CampaignMode3CompanionOne.originalAiMotion.rawZ &&
                phase13CampaignMode3HalfTickCompanionTwo.originalAiMotion.rawX ==
                    phase13CampaignMode3CompanionTwo.originalAiMotion.rawX &&
                phase13CampaignMode3HalfTickCompanionTwo.originalAiMotion.rawZ ==
                    phase13CampaignMode3CompanionTwo.originalAiMotion.rawZ,
            "mode-3 mission-objective movement changed with fixed-tick subdivision");
        const auto phase13CampaignMode3Sustained =
            mw::battle::runBattleReplay(
                phase13CampaignMode3Objective, {}, 10u);
        const auto& phase13CampaignMode3SustainedCompanionOne =
            phase13CombatantAtLiveSlot(
                phase13CampaignMode3Sustained, 1u);
        const auto& phase13CampaignMode3SustainedCompanionTwo =
            phase13CombatantAtLiveSlot(
                phase13CampaignMode3Sustained, 2u);
        expect(
            phase13CampaignMode3SustainedCompanionOne.originalAiMotion.
                    acceptedPoseCommitCount >= 3u &&
                phase13CampaignMode3SustainedCompanionTwo.originalAiMotion.
                    acceptedPoseCommitCount >= 3u &&
                phase13Mode3DistanceSquared(
                    phase13CampaignMode3SustainedCompanionOne.
                        originalAiMotion.rawX,
                    phase13CampaignMode3SustainedCompanionOne.
                        originalAiMotion.rawZ,
                    phase13CampaignMode3TargetSlot.originalRawX,
                    phase13CampaignMode3TargetSlot.originalRawZ) <
                    phase13CampaignMode3DistanceBeforeOne &&
                phase13Mode3DistanceSquared(
                    phase13CampaignMode3SustainedCompanionTwo.
                        originalAiMotion.rawX,
                    phase13CampaignMode3SustainedCompanionTwo.
                        originalAiMotion.rawZ,
                    phase13CampaignMode3TargetSlot.originalRawX,
                    phase13CampaignMode3TargetSlot.originalRawZ) <
                    phase13CampaignMode3DistanceBeforeTwo,
            "both Fuel Dump companions did not sustain movement toward the mission objective");

        const auto phase13SymmetricReplay = mw::battle::runBattleReplay(
            phase13SymmetricMovementParams, {}, 1u);
        expect(
            mw::battle::battleSnapshotFingerprint(phase13SymmetricMovement) ==
                mw::battle::battleSnapshotFingerprint(
                    phase13SymmetricReplay),
            "50ee-independent symmetric movement changed under replay");

        auto phase13SymmetricHalfTickParams = phase13SymmetricMovementParams;
        phase13SymmetricHalfTickParams.fixedTickSeconds = 0.05;
        const auto phase13SymmetricHalfTick = mw::battle::runBattleReplay(
            phase13SymmetricHalfTickParams, {}, 2u);
        const auto& phase13SymmetricHalfTickEnemy =
            phase13CombatantAtLiveSlot(phase13SymmetricHalfTick, 4u);
        expect(
            phase13SymmetricHalfTickEnemy.originalAiMotion.
                    acceptedPoseCommitCount == 1u &&
                phase13SymmetricHalfTickEnemy.originalAiMotion.
                    lastMovementSelection50eeIndependent &&
                phase13SymmetricHalfTickEnemy.originalAiMotion.rawX ==
                    phase13SymmetricEnemy.originalAiMotion.rawX &&
                phase13SymmetricHalfTickEnemy.originalAiMotion.rawY ==
                    phase13SymmetricEnemy.originalAiMotion.rawY &&
                phase13SymmetricHalfTickEnemy.originalAiMotion.rawZ ==
                    phase13SymmetricEnemy.originalAiMotion.rawZ &&
                phase13SymmetricHalfTickEnemy.originalAiMotion.heading ==
                    phase13SymmetricEnemy.originalAiMotion.heading,
            "50ee-independent movement changed with fixed-tick subdivision");

        auto phase13SymmetricDependentParams = phase13SymmetricMovementParams;
        auto& phase13SymmetricDependentSlot0 =
            phase13SymmetricDependentParams.setupMetadata.playerSlots[0];
        auto& phase13SymmetricDependentSlot1 =
            phase13SymmetricDependentParams.setupMetadata.playerSlots[1];
        phase13SymmetricDependentSlot0.originalRawX = 1900;
        phase13SymmetricDependentSlot0.originalRawZ = 0;
        phase13SymmetricDependentSlot0.transform =
            phase13WorldTransformFromRaw(1900, 0);
        phase13SymmetricDependentSlot1.originalRawX = -10000;
        phase13SymmetricDependentSlot1.originalRawZ = 0;
        phase13SymmetricDependentSlot1.transform =
            phase13WorldTransformFromRaw(-10000, 0);
        phase13SymmetricDependentParams.playerStartTransform =
            phase13SymmetricDependentSlot0.transform;
        phase13SymmetricDependentParams.playerAlliedLaunchStates[0].
            startTransform = phase13SymmetricDependentSlot1.transform;
        const auto phase13SymmetricDependent = mw::battle::runBattleReplay(
            phase13SymmetricDependentParams, {}, 1u);
        const auto& phase13SymmetricDependentEnemy =
            phase13CombatantAtLiveSlot(phase13SymmetricDependent, 4u);
        expect(
            phase13SymmetricDependentEnemy.originalAiMotion.
                    movementUpdateAttemptCount == 1u &&
                phase13SymmetricDependentEnemy.originalAiMotion.
                    acceptedPoseCommitCount == 0u &&
                !phase13SymmetricDependentEnemy.originalAiMotion.
                    lastMovementSelection50eeIndependent &&
                phase13SymmetricDependentEnemy.originalAiMotion.
                    lastMovementUpdateResult ==
                    mw::battle::BattleOriginalAiFirstMovementUpdateResult::
                        RejectedRelationOpen &&
                phase13SymmetricDependentEnemy.originalAiMotion.rawX == 2000,
            "branch-dependent player relation did not fail closed before opposing movement");

        auto phase13SymmetricFingerprintMutation = phase13SymmetricMovement;
        auto phase13SymmetricEnemyMutation = std::find_if(
            phase13SymmetricFingerprintMutation.combatants.begin(),
            phase13SymmetricFingerprintMutation.combatants.end(),
            [](const mw::battle::CombatantSnapshot& combatant) {
                return combatant.roster.originalLiveObjectSlot ==
                    std::optional<uint8_t>{static_cast<uint8_t>(4u)};
            });
        if (phase13SymmetricEnemyMutation ==
            phase13SymmetricFingerprintMutation.combatants.end()) {
            throw std::runtime_error(
                "phase13 symmetric fingerprint fixture lost opposing slot 4");
        }
        phase13SymmetricEnemyMutation->originalAiMotion.
            lastMovementSelection50eeIndependent = false;
        expect(
            mw::battle::battleSnapshotFingerprint(phase13SymmetricMovement) !=
                mw::battle::battleSnapshotFingerprint(
                    phase13SymmetricFingerprintMutation),
            "50ee-independence proof result is missing from replay fingerprint");

        const auto phase13TransactionAccepted =
            mw::battle::battleOriginalAiOrdinaryPoseTransaction(
                phase13EmptySceneCatalog,
                0, 123, 0,
                0, 0, 0,
                0, 0, 0,
                0, 0, 0,
                5, 200,
                0u, 0u,
                7, 9);
        const auto phase13TransactionRejected =
            mw::battle::battleOriginalAiOrdinaryPoseTransaction(
                phase13EmptySceneCatalog,
                0, 123, 0,
                0x0100, 0x0200, 0x0300,
                0, 0, 0,
                1, 2, 3,
                5, 200,
                0u, 1u,
                7, 9);
        const auto phase13TransactionStopped =
            mw::battle::battleOriginalAiOrdinaryPoseTransaction(
                phase13EmptySceneCatalog,
                1, 123, 2,
                0, 0, 0x1000,
                0, 0, 0,
                0, 0, 0,
                0, 201,
                4u, 9u,
                7, 9);
        expect(phase13TransactionAccepted.exact &&
                   phase13TransactionAccepted.commit.poseAccepted &&
                   phase13TransactionAccepted.setsMovingFlagBit4 &&
                   phase13TransactionAccepted.xAfter == 0 &&
                   phase13TransactionAccepted.yAfter == 100 &&
                   phase13TransactionAccepted.zAfter == 5 &&
                   phase13TransactionAccepted.rawSpeedAfter == 5 &&
                   phase13TransactionAccepted.controlState4cAfter == 7 &&
                   phase13TransactionAccepted.controlState53After == 9 &&
                   !phase13TransactionAccepted.sceneCacheValidAfter &&
                   phase13TransactionRejected.exact &&
                   phase13TransactionRejected.commit.poseRestored &&
                   phase13TransactionRejected.commit.clearsMotionWords30Through3c &&
                   phase13TransactionRejected.xAfter == 0 &&
                   phase13TransactionRejected.yAfter == 123 &&
                   phase13TransactionRejected.zAfter == 0 &&
                   phase13TransactionRejected.pitchAfter == 0x0100 &&
                   phase13TransactionRejected.rollAfter == 0x0200 &&
                   phase13TransactionRejected.headingAfter == 0x0028 &&
                   phase13TransactionRejected.controlWord30After == 0 &&
                   phase13TransactionRejected.controlWord32After == 0 &&
                   phase13TransactionRejected.controlWord34After == 0 &&
                   phase13TransactionRejected.driftWord36After == 0 &&
                   phase13TransactionRejected.driftWord38After == 0 &&
                   phase13TransactionRejected.driftWord3aAfter == 0 &&
                   phase13TransactionRejected.rawSpeedAfter == 0 &&
                   phase13TransactionRejected.controlState4cAfter == 0 &&
                   phase13TransactionRejected.controlState53After == 0 &&
                   phase13TransactionStopped.exact &&
                   !phase13TransactionStopped.setsMovingFlagBit4 &&
                   phase13TransactionStopped.commit.poseAccepted &&
                   phase13TransactionStopped.xAfter == 1 &&
                   phase13TransactionStopped.yAfter == 100 &&
                   phase13TransactionStopped.zAfter == 2 &&
                   phase13TransactionStopped.headingAfter == 0x1000 &&
                   phase13TransactionStopped.slotProbeWordAfter == 4u,
               "BTECH ordinary 87c4 pose transaction changed");

        phase13ProbeGrid.rawSamples.assign(174u * 94u, 0u);
        mw::battle::BattleOriginalAiSelectedTargetMovementInput
            phase13SelectedMovementInput;
        phase13SelectedMovementInput.movingSlot = 4u;
        phase13SelectedMovementInput.movingOwnerWord = 1;
        phase13SelectedMovementInput.rawX = 0;
        phase13SelectedMovementInput.rawY = 250;
        phase13SelectedMovementInput.rawZ = 0;
        phase13SelectedMovementInput.heading = 0;
        phase13SelectedMovementInput.definitionWord06 = 72;
        phase13SelectedMovementInput.definitionHeightWord02 = 500;
        phase13SelectedMovementInput.leftLegActuatorWord = 4;
        phase13SelectedMovementInput.rightLegActuatorWord = 4;
        phase13SelectedMovementInput.selectedTargetEntityId = {901u};
        phase13SelectedMovementInput.selectedTargetRawX = 3000;
        phase13SelectedMovementInput.selectedTargetRawY = 250;
        phase13SelectedMovementInput.selectedTargetRawZ = 3000;
        phase13SelectedMovementInput.liveSlots.resize(8u);
        phase13SelectedMovementInput.liveSlots[0] =
            {{901u}, true, 0, 3000, 250, 3000};
        phase13SelectedMovementInput.liveSlots[4] =
            {{904u}, true, 1, 0, 250, 0};
        const auto phase13SelectedMovement =
            mw::battle::battleOriginalAiSelectedTargetMovement(
                phase13ProbeGrid,
                phase13EmptySceneCatalog,
                phase13SelectedMovementInput);
        expect(
            phase13SelectedMovement.exact &&
                phase13SelectedMovement.selectedTargetOwnedByCaller &&
                !phase13SelectedMovement.targetAcquisitionClosed &&
                phase13SelectedMovement.ordinaryStateAccepted &&
                phase13SelectedMovement.liveObjectLaneDomainClosed &&
                phase13SelectedMovement.objectiveLaneDomainClosed &&
                phase13SelectedMovement.probeEndpointDomainClosed &&
                !phase13SelectedMovement.boundaryRelationOpen &&
                !phase13SelectedMovement.liveObjectLaneBlocked &&
                !phase13SelectedMovement.objectiveLaneBlocked &&
                phase13SelectedMovement.steering.exact &&
                phase13SelectedMovement.steering.probeVisitCount == 1u &&
                phase13SelectedMovement.speedCommand.nextRawSpeed == 5 &&
                phase13SelectedMovement.speedCommand.positiveAccelerationCapped &&
                phase13SelectedMovement.turnCommand.exact &&
                phase13SelectedMovement.slotProbe.exact &&
                phase13SelectedMovement.slotProbe.resultWord == 0u &&
                phase13SelectedMovement.poseTransaction.exact &&
                phase13SelectedMovement.poseTransaction.commit.poseAccepted &&
                phase13SelectedMovement.poseTransaction.yAfter == 250 &&
                phase13SelectedMovement.poseTransaction.rawSpeedAfter == 5,
            "selected-target ordinary movement transaction changed");

        auto phase13EarlyBlockedGrid = phase13ProbeGrid;
        phase13EarlyBlockedGrid.rawSamples[
            static_cast<size_t>(86) * 94u + 45u] = 0x63u;
        auto phase13EarlyBlockedMovementInput = phase13SelectedMovementInput;
        phase13EarlyBlockedMovementInput.selectedTargetRawX = 0;
        phase13EarlyBlockedMovementInput.selectedTargetRawZ = 3000;
        phase13EarlyBlockedMovementInput.liveSlots[0].rawX = 0;
        phase13EarlyBlockedMovementInput.liveSlots[0].rawZ = 3000;
        const auto phase13EarlyBlockedMovement =
            mw::battle::battleOriginalAiSelectedTargetMovement(
                phase13EarlyBlockedGrid,
                phase13EmptySceneCatalog,
                phase13EarlyBlockedMovementInput);
        expect(
            phase13EarlyBlockedMovement.exact &&
                phase13EarlyBlockedMovement.probeEndpointDomainClosed &&
                phase13EarlyBlockedMovement.terrainProbes[0].exact &&
                phase13EarlyBlockedMovement.terrainProbes[0].blocked &&
                phase13EarlyBlockedMovement.terrainProbes[0].
                    visitedStepCount == 1u &&
                phase13EarlyBlockedMovement.probeBlocked[0] &&
                phase13EarlyBlockedMovement.steering.exact,
            "selected-target movement rejected an exact early terrain-blocked probe");

        auto phase13LaneBlockedMovementInput = phase13SelectedMovementInput;
        phase13LaneBlockedMovementInput.selectedTargetRawX = 0;
        phase13LaneBlockedMovementInput.selectedTargetRawZ = 1000;
        phase13LaneBlockedMovementInput.liveSlots[0].rawX = 0;
        phase13LaneBlockedMovementInput.liveSlots[0].rawZ = 1000;
        const auto phase13LaneBlockedMovement =
            mw::battle::battleOriginalAiSelectedTargetMovement(
                phase13ProbeGrid,
                phase13EmptySceneCatalog,
                phase13LaneBlockedMovementInput);
        expect(
            phase13LaneBlockedMovement.exact &&
                phase13LaneBlockedMovement.liveObjectLaneBlocked &&
                phase13LaneBlockedMovement.blockingLiveObjectEntityId.value ==
                    901u &&
                std::all_of(
                    phase13LaneBlockedMovement.probeBlocked.begin(),
                    phase13LaneBlockedMovement.probeBlocked.end(),
                    [](bool blocked) { return blocked; }) &&
                phase13LaneBlockedMovement.steering.probeVisitCount == 4u &&
                phase13LaneBlockedMovement.steering.selectorWord3cAfter == 1 &&
                phase13LaneBlockedMovement.turnCommand.turnCommand == 0x0222,
            "selected-target movement live-object lane ordering changed");

        auto phase13RejectedMovementInput = phase13SelectedMovementInput;
        phase13RejectedMovementInput.liveSlots[5] =
            {{905u}, true, 0, 0, 0, 5};
        const auto phase13RejectedMovement =
            mw::battle::battleOriginalAiSelectedTargetMovement(
                phase13ProbeGrid,
                phase13EmptySceneCatalog,
                phase13RejectedMovementInput);
        expect(
            phase13RejectedMovement.exact &&
                phase13RejectedMovement.slotProbe.liveSlotBlocked &&
                phase13RejectedMovement.slotProbe.blockingLiveSlot == 5u &&
                phase13RejectedMovement.slotProbe.resultWord == 1u &&
                phase13RejectedMovement.poseTransaction.commit.poseRestored &&
                phase13RejectedMovement.poseTransaction.rawSpeedAfter == 0,
            "selected-target movement did not preserve 89ea rollback");

        auto phase13BoundaryOpenMovementInput = phase13SelectedMovementInput;
        phase13BoundaryOpenMovementInput.rawX = 43000;
        phase13BoundaryOpenMovementInput.selectedTargetRawX = 40000;
        phase13BoundaryOpenMovementInput.selectedTargetRawZ = 3000;
        phase13BoundaryOpenMovementInput.liveSlots[0].rawX = 40000;
        const auto phase13BoundaryOpenMovement =
            mw::battle::battleOriginalAiSelectedTargetMovement(
                phase13ProbeGrid,
                phase13EmptySceneCatalog,
                phase13BoundaryOpenMovementInput);
        expect(
            !phase13BoundaryOpenMovement.exact &&
                (!phase13BoundaryOpenMovement.probeEndpointDomainClosed ||
                 phase13BoundaryOpenMovement.boundaryRelationOpen),
            "selected-target movement did not fail closed outside probe domain");

        auto phase13ParamsFor = [&](const std::string& enemyPreset,
                                    double targetDistance,
                                    const std::vector<uint8_t>& functionalSlots,
                                    int ammunition) {
            mw::battle::BattleStartParams result = params;
            mw::battle::applyBattleDriveRuntimeTuning(result);
            result.playerMechPresetId = "shadow_hawk";
            result.playerStartTransform = {10000.0, 0.0, 10000.0, 0.0};
            result.playerLaunchState.reset();
            result.playerAlliedLaunchStates.clear();
            result.combatantLaunchStates.clear();
            result.mechSystemsSnapshotEnabled = true;
            result.deterministicCombatRuntimeEnabled = true;
            result.individualWeaponRuntimeEnabled = true;
            result.originalProjectileRuntimeEnabled = true;
            result.originalHeatRuntimeEnabled = true;
            result.originalMajorSystemRuntimeEnabled = true;
            result.stationaryTargetHitDiagnosticEnabled = false;
            result.weaponTargetPolicy =
                mw::battle::BattleWeaponTargetPolicy::UnresolvedFailClosed;
            result.combatAiPolicy = mw::battle::BattleCombatAiPolicy::
                OriginalBtechStationarySingleTargetFire;
            result.mechSystemMaxDamage = 20;
            auto playerHitProfile = syntheticCrosshairHitProfile();
            playerHitProfile.mechPresetId = "shadow_hawk";
            auto enemyHitProfile = syntheticCrosshairHitProfile();
            enemyHitProfile.mechPresetId = enemyPreset;
            result.mechHitProfiles = {
                std::move(playerHitProfile), std::move(enemyHitProfile)};
            result.projectileHitProfiles = weaponParams.projectileHitProfiles;

            mw::battle::BattleCombatantLaunchState playerLaunch;
            playerLaunch.mechPresetId = result.playerMechPresetId;
            playerLaunch.startTransform = result.playerStartTransform;
            playerLaunch.roster = result.playerRoster;
            playerLaunch.persistentMechState.valid = true;
            playerLaunch.persistentMechState.provenance =
                "phase13_stationary_target";
            playerLaunch.persistentMechState.heatSinksWorking = 10u;
            playerLaunch.persistentMechState.heatSinksTotal = 10u;
            playerLaunch.persistentMechState.armorPercent = 100u;
            playerLaunch.ammunitionStateValid = true;
            playerLaunch.ammunitionByPool.fill(20);
            result.playerLaunchState = playerLaunch;

            mw::battle::BattleCombatantLaunchState enemyLaunch;
            enemyLaunch.mechPresetId = enemyPreset;
            enemyLaunch.startTransform = {
                result.playerStartTransform.x,
                0.0,
                result.playerStartTransform.z + targetDistance,
                3.14159265358979323846};
            enemyLaunch.roster.team = mw::battle::BattleTeam::Opposing;
            enemyLaunch.roster.provenance =
                "phase13_stationary_single_target_lab";
            enemyLaunch.roster.sourceSlot = "opposing:phase13:1";
            enemyLaunch.persistentMechState.valid = true;
            enemyLaunch.persistentMechState.provenance =
                "phase13_stationary_single_weapon";
            enemyLaunch.persistentMechState.heatSinksWorking = 0u;
            enemyLaunch.persistentMechState.heatSinksTotal = 0u;
            enemyLaunch.persistentMechState.armorPercent = 100u;
            enemyLaunch.persistentMechState.weaponConditions.fill(3u);
            for (uint8_t slot : functionalSlots) {
                enemyLaunch.persistentMechState.weaponConditions[slot] = 0u;
            }
            enemyLaunch.ammunitionStateValid = true;
            enemyLaunch.ammunitionByPool.fill(ammunition);
            result.combatantLaunchStates.push_back(enemyLaunch);
            return result;
        };
        auto phase13EnemyFrom = [](const mw::battle::BattleSnapshot& snapshot)
            -> const mw::battle::CombatantSnapshot& {
            const auto found = std::find_if(
                snapshot.combatants.begin(),
                snapshot.combatants.end(),
                [](const mw::battle::CombatantSnapshot& combatant) {
                    return !combatant.playerControlled;
                });
            if (found == snapshot.combatants.end()) {
                throw std::runtime_error("phase13 snapshot lost AI shooter");
            }
            return *found;
        };
        auto addPhase13FriendlyCandidate = [](
            mw::battle::BattleStartParams& battleParams,
            double offsetX,
            double offsetZ,
            const std::string& sourceSlot) {
            mw::battle::BattleCombatantLaunchState friendly =
                battleParams.combatantLaunchStates.front();
            friendly.startTransform.x += offsetX;
            friendly.startTransform.z += offsetZ;
            friendly.roster.sourceSlot = sourceSlot;
            friendly.roster.provenance =
                "phase13_same_side_centerline_candidate";
            friendly.persistentMechState.provenance =
                "phase13_nonfiring_friendly_candidate";
            friendly.persistentMechState.weaponConditions.fill(3u);
            battleParams.combatantLaunchStates.push_back(
                std::move(friendly));
        };

        const mw::battle::Transform phase13FixedGeometryShooter{
            0.0, 0.0, 0.0, 3.14159265358979323846};
        const auto phase13VerticalZero =
            mw::battle::battleOriginalAiZeroPitchCandidateGeometry(
                phase13FixedGeometryShooter,
                mw::battle::Transform{0.0, 0.0, -1000.0, 0.0});
        const auto phase13VerticalBelowBoundary =
            mw::battle::battleOriginalAiZeroPitchCandidateGeometry(
                phase13FixedGeometryShooter,
                mw::battle::Transform{0.0, 287.0, -1000.0, 0.0});
        const auto phase13VerticalOnBoundary =
            mw::battle::battleOriginalAiZeroPitchCandidateGeometry(
                phase13FixedGeometryShooter,
                mw::battle::Transform{0.0, 288.0, -1000.0, 0.0});
        const auto phase13ElevatedInsideBoth =
            mw::battle::battleOriginalAiZeroPitchCandidateGeometry(
                phase13FixedGeometryShooter,
                mw::battle::Transform{100.0, 100.0, -1000.0, 0.0});
        const auto phase13ElevatedOutsideVertical =
            mw::battle::battleOriginalAiZeroPitchCandidateGeometry(
                phase13FixedGeometryShooter,
                mw::battle::Transform{100.0, 400.0, -1000.0, 0.0});
        const auto phase13NonIntegralHeight =
            mw::battle::battleOriginalAiZeroPitchCandidateGeometry(
                phase13FixedGeometryShooter,
                mw::battle::Transform{0.0, 0.5, -1000.0, 0.0});
        expect(phase13VerticalZero.exact &&
                   phase13VerticalZero.verticalAngle == 0 &&
                   phase13VerticalZero.verticalDelta == 0 &&
                   phase13VerticalZero.verticalEnvelopePassed &&
                   phase13VerticalZero.insideEnvelope &&
                   phase13VerticalBelowBoundary.exact &&
                   phase13VerticalBelowBoundary.verticalAngle == -2896 &&
                   phase13VerticalBelowBoundary.verticalEnvelopePassed &&
                   phase13VerticalBelowBoundary.insideEnvelope &&
                   phase13VerticalOnBoundary.exact &&
                   phase13VerticalOnBoundary.verticalAngle == -2912 &&
                   !phase13VerticalOnBoundary.verticalEnvelopePassed &&
                   !phase13VerticalOnBoundary.insideEnvelope &&
                   phase13ElevatedInsideBoth.insideEnvelope &&
                   phase13ElevatedInsideBoth.planarEnvelopePassed &&
                   phase13ElevatedInsideBoth.verticalEnvelopePassed &&
                   phase13ElevatedOutsideVertical.planarEnvelopePassed &&
                   !phase13ElevatedOutsideVertical.verticalEnvelopePassed &&
                   !phase13ElevatedOutsideVertical.insideEnvelope &&
                   !phase13NonIntegralHeight.exact,
               "phase13 recovered zero-pitch a660 vertical envelope changed");

        const auto phase13LaserParams = phase13ParamsFor(
            "locust", 1000.0, {0u}, 20);
        const mw::battle::BattleSnapshot phase13Laser =
            mw::battle::runBattleReplay(phase13LaserParams, {}, 2u);
        const auto& phase13LaserEnemy = phase13EnemyFrom(phase13Laser);
        const auto& phase13LaserDecision =
            phase13LaserEnemy.lastOriginalAiDecision;
        expect(phase13Laser.combatants.size() == 2u &&
                   phase13Laser.objective.valid,
               "phase13 objective must remain separate from the mech roster");
        expect(phase13LaserDecision.valid &&
                   phase13LaserDecision.sequence == 1u &&
                   phase13LaserDecision.originalUpdateCount == 1u &&
                   phase13LaserDecision.decisionTickIndex == 1u &&
                   phase13LaserDecision.fireTickIndex == 1u &&
                   phase13LaserDecision.result ==
                       mw::battle::BattleOriginalAiDecisionResult::FireRequested &&
                   phase13LaserDecision.targetKind ==
                       mw::battle::BattleShotTargetKind::Combatant &&
                   phase13LaserDecision.targetEntityId ==
                       phase13Laser.combatants.front().id &&
                   phase13LaserDecision.weaponInstanceId == 1u &&
                   phase13LaserDecision.readyFunctionalWeaponCount == 1u &&
                   phase13LaserDecision.maximumReadyRangeWord == 10u &&
                   phase13LaserDecision.targetSearchRadius == 4400.0 &&
                   phase13LaserDecision.targetAcquisitionRangePassed &&
                   phase13LaserDecision.
                       targetSelectionCenterlineSubsetProven &&
                   phase13LaserDecision.
                       targetSelectionPlanarFixedAngleProven &&
                   phase13LaserDecision.
                       targetSelectionZeroPitchVerticalFixedAngleProven &&
                   phase13LaserDecision.originalApproximateDistanceProven &&
                   phase13LaserDecision.shooterFixedHeading ==
                       std::numeric_limits<int16_t>::min() &&
                   phase13LaserDecision.selectedCandidateFixedHeading ==
                       std::numeric_limits<int16_t>::min() &&
                   phase13LaserDecision.selectedCandidateYawDelta == 0 &&
                   phase13LaserDecision.selectedCandidateHalfWidth == 1840 &&
                   phase13LaserDecision.selectedCandidateVerticalAngle == 0 &&
                   phase13LaserDecision.selectedCandidateVerticalDelta == 0 &&
                   phase13LaserDecision.selectedCandidateVerticalLimit ==
                       0x0b60 &&
                   phase13LaserDecision.
                       selectedCandidateVerticalGatePassed &&
                   !phase13LaserDecision.friendlyFireLaneSuppressed &&
                   phase13LaserDecision.strictRangeGatePassed &&
                   phase13LaserDecision.weaponScore == 5 &&
                   phase13LaserDecision.automaticCallerAndLocationTableProven &&
                   !phase13LaserDecision.originalRandomSequenceProven,
               "phase13 proven target/weapon decision contract changed");
        expect(phase13LaserEnemy.weaponShotsFired == 1u &&
                   phase13LaserEnemy.lastFireRequestAccepted &&
                   phase13LaserEnemy.lastFireRequestTickIndex == 1u &&
                   phase13LaserEnemy.weapons[0].shotsFired == 1u &&
                   phase13LaserEnemy.weapons[0].cooldownTicksRemaining == 60u &&
                   phase13LaserEnemy.heat.lastWeaponHeatAdded == 240,
               "phase13 decision must create exactly one common fire request");
        expect(phase13Laser.lastShot.valid &&
                   phase13Laser.lastShot.sequence == 1u &&
                   phase13Laser.lastShot.shooterEntityId ==
                       phase13LaserEnemy.id &&
                   phase13Laser.lastShot.originalAiDecision.valid &&
                   phase13Laser.lastShot.originalAiDecision.sequence == 1u &&
                   phase13Laser.lastShot.hit &&
                   phase13Laser.lastShot.hitLocation ==
                       mw::battle::BattleShotHitLocation::
                           OriginalAiAspect2d6Table &&
                   phase13Laser.lastShot.hitLocationProven &&
                   phase13Laser.lastShot.deliveryState ==
                       mw::battle::BattleShotDeliveryState::Immediate &&
                   phase13Laser.lastShot.damageApplied == 5,
               "phase13 immediate shot must use AI 2d6 and common damage");

        mw::battle::BattleWorld phase13CooldownWorld =
            mw::battle::BattleWorld::create(phase13LaserParams);
        phase13CooldownWorld.runTicks(4u);
        const auto phase13CooldownSnapshot =
            phase13CooldownWorld.snapshot();
        const auto& phase13CooldownEnemy =
            phase13EnemyFrom(phase13CooldownSnapshot);
        expect(phase13CooldownEnemy.weaponShotsFired == 1u &&
                   phase13CooldownEnemy.originalAiUpdateCount == 2u &&
                   phase13CooldownEnemy.lastOriginalAiDecision.result ==
                       mw::battle::BattleOriginalAiDecisionResult::
                           RejectedNoReadyWeaponAcquisition &&
                   phase13CooldownEnemy.lastOriginalAiDecision.
                           readyFunctionalWeaponCount == 0u &&
                   phase13CooldownEnemy.weapons[0].cooldownTicksRemaining > 0u,
               "phase13 cooldown must remove the only installation from the acquisition radius");

        const auto phase13RangeParams = phase13ParamsFor(
            "locust", 4800.0, {0u}, 20);
        const auto phase13Range =
            mw::battle::runBattleReplay(phase13RangeParams, {}, 2u);
        const auto& phase13RangeDecision =
            phase13EnemyFrom(phase13Range).lastOriginalAiDecision;
        expect(phase13RangeDecision.result ==
                   mw::battle::BattleOriginalAiDecisionResult::
                       RejectedTargetAcquisitionRange &&
                   phase13RangeDecision.targetSearchRadius == 4400.0 &&
                   !phase13RangeDecision.targetAcquisitionRangePassed &&
                   !phase13RangeDecision.strictRangeGatePassed &&
                   phase13Range.lastShot.sequence == 0u,
               "phase13 strict original target-search radius changed");

        auto phase13OffAxisInsideParams = phase13LaserParams;
        phase13OffAxisInsideParams.playerLaunchState->startTransform.x +=
            100.0;
        const auto phase13OffAxisInside =
            mw::battle::runBattleReplay(
                phase13OffAxisInsideParams, {}, 2u);
        const auto& phase13OffAxisInsideDecision =
            phase13EnemyFrom(phase13OffAxisInside).lastOriginalAiDecision;
        expect(phase13OffAxisInsideDecision.result ==
                   mw::battle::BattleOriginalAiDecisionResult::FireRequested &&
                   phase13OffAxisInsideDecision.targetDistance == 1037.0 &&
                   phase13OffAxisInsideDecision.
                       targetSelectionPlanarFixedAngleProven &&
                   !phase13OffAxisInsideDecision.
                       targetSelectionCenterlineSubsetProven &&
                   phase13OffAxisInsideDecision.
                       selectedCandidateYawDelta == 1024 &&
                   phase13OffAxisInsideDecision.
                       selectedCandidateHalfWidth == 1760 &&
                   phase13OffAxisInside.lastShot.targetDistance == 1037.0 &&
                   phase13OffAxisInside.lastShot.rangeGatePassed,
               "phase13 recovered b961/c74e envelope must accept the inside off-axis target");

        auto phase13OffAxisOutsideParams = phase13LaserParams;
        phase13OffAxisOutsideParams.playerLaunchState->startTransform.x +=
            400.0;
        const auto phase13OffAxisOutside =
            mw::battle::runBattleReplay(
                phase13OffAxisOutsideParams, {}, 2u);
        const auto& phase13OffAxisOutsideDecision =
            phase13EnemyFrom(phase13OffAxisOutside).lastOriginalAiDecision;
        expect(phase13OffAxisOutsideDecision.result ==
                   mw::battle::BattleOriginalAiDecisionResult::
                       RejectedTargetAimEnvelope &&
                   phase13OffAxisOutsideDecision.targetDistance == 1150.0 &&
                   phase13OffAxisOutsideDecision.
                       targetSelectionPlanarFixedAngleProven &&
                   !phase13OffAxisOutsideDecision.
                       targetAcquisitionRangePassed &&
                   phase13OffAxisOutside.lastShot.sequence == 0u,
               "phase13 recovered c74e envelope must reject the outside off-axis target");

        auto phase13NonIntegralParams = phase13LaserParams;
        phase13NonIntegralParams.playerLaunchState->startTransform.x += 0.5;
        const auto phase13NonIntegral =
            mw::battle::runBattleReplay(
                phase13NonIntegralParams, {}, 2u);
        expect(phase13EnemyFrom(phase13NonIntegral)
                       .lastOriginalAiDecision.result ==
                   mw::battle::BattleOriginalAiDecisionResult::
                       RejectedCandidateAimGeometryOpen &&
                   phase13NonIntegral.lastShot.sequence == 0u,
               "phase13 non-integral replacement coordinates must remain typed fail-closed");

        mw::battle::BattleReplay phase13VerticalAimReplay;
        mw::battle::BattleInputCommand phase13VerticalAimCommand;
        phase13VerticalAimCommand.tickIndex = 0u;
        phase13VerticalAimCommand.entityId = mw::battle::EntityId{2u};
        phase13VerticalAimCommand.aimPitchStepDelta = 1;
        phase13VerticalAimReplay.commands.push_back(
            phase13VerticalAimCommand);
        const auto phase13VerticalAim =
            mw::battle::runBattleReplay(
                phase13LaserParams, phase13VerticalAimReplay, 2u);
        expect(phase13EnemyFrom(phase13VerticalAim)
                       .lastOriginalAiDecision.result ==
                   mw::battle::BattleOriginalAiDecisionResult::
                       RejectedCandidateAimGeometryOpen &&
                   phase13VerticalAim.lastShot.sequence == 0u,
               "phase13 nonzero vertical aim must remain typed fail-closed");

        const auto phase13BrokenParams = phase13ParamsFor(
            "locust", 1000.0, {}, 20);
        const auto phase13Broken =
            mw::battle::runBattleReplay(phase13BrokenParams, {}, 2u);
        expect(phase13EnemyFrom(phase13Broken)
                       .lastOriginalAiDecision.result ==
                   mw::battle::BattleOriginalAiDecisionResult::
                       RejectedNoReadyWeaponAcquisition &&
                   phase13EnemyFrom(phase13Broken)
                           .lastOriginalAiDecision.
                           readyFunctionalWeaponCount == 0u &&
                   phase13EnemyFrom(phase13Broken).weaponShotsFired == 0u,
               "phase13 nonfunctional installations must not create an acquisition radius");

        const auto phase13MinimumRangeParams = phase13ParamsFor(
            "shadow_hawk", 1000.0, {0u}, 20);
        const auto phase13MinimumRange =
            mw::battle::runBattleReplay(
                phase13MinimumRangeParams, {}, 2u);
        expect(phase13EnemyFrom(phase13MinimumRange)
                       .lastOriginalAiDecision.result ==
                   mw::battle::BattleOriginalAiDecisionResult::
                       RejectedWeaponOutOfRange &&
                   phase13EnemyFrom(phase13MinimumRange)
                           .lastOriginalAiDecision.
                           targetAcquisitionRangePassed &&
                   phase13MinimumRange.lastShot.sequence == 0u,
               "phase13 AC/5 strict minimum range gate changed");

        const auto phase13EmptyAmmoParams = phase13ParamsFor(
            "shadow_hawk", 1000.0, {0u}, 0);
        const auto phase13EmptyAmmo =
            mw::battle::runBattleReplay(phase13EmptyAmmoParams, {}, 2u);
        expect(phase13EnemyFrom(phase13EmptyAmmo)
                       .lastOriginalAiDecision.result ==
                   mw::battle::BattleOriginalAiDecisionResult::
                       RejectedWeaponNoAmmunition &&
                   phase13EnemyFrom(phase13EmptyAmmo).weaponShotsFired == 0u,
               "phase13 empty shared ammunition gate changed");

        auto phase13FriendlyBlockedParams = phase13LaserParams;
        addPhase13FriendlyCandidate(
            phase13FriendlyBlockedParams,
            0.0,
            -500.0,
            "opposing:phase13:friendly_blocker");
        const auto phase13FriendlyBlocked =
            mw::battle::runBattleReplay(
                phase13FriendlyBlockedParams, {}, 2u);
        const auto& phase13FriendlyBlockedEnemy =
            phase13EnemyFrom(phase13FriendlyBlocked);
        const auto& phase13FriendlyBlockedDecision =
            phase13FriendlyBlockedEnemy.lastOriginalAiDecision;
        expect(phase13FriendlyBlockedDecision.result ==
                   mw::battle::BattleOriginalAiDecisionResult::
                       RejectedFriendlyFireLane &&
                   phase13FriendlyBlockedDecision.
                       targetSelectionCenterlineSubsetProven &&
                   phase13FriendlyBlockedDecision.
                       friendlyFireLaneSuppressed &&
                   phase13FriendlyBlockedDecision.blockingCandidateFlags == 1u &&
                   phase13FriendlyBlockedDecision.
                       blockingCombatantEntityId ==
                       phase13FriendlyBlocked.combatants[2].id &&
                   phase13FriendlyBlockedDecision.
                       blockingCombatantDistance == 500.0 &&
                   phase13FriendlyBlockedEnemy.weaponShotsFired == 0u &&
                   phase13FriendlyBlocked.lastShot.sequence == 0u,
               "phase13 nearer same-side centerline candidate must suppress fire");

        auto phase13FriendlyFarParams = phase13LaserParams;
        addPhase13FriendlyCandidate(
            phase13FriendlyFarParams,
            0.0,
            -3000.0,
            "opposing:phase13:friendly_beyond_target");
        const auto phase13FriendlyFar =
            mw::battle::runBattleReplay(
                phase13FriendlyFarParams, {}, 2u);
        const auto& phase13FriendlyFarEnemy =
            phase13EnemyFrom(phase13FriendlyFar);
        expect(phase13FriendlyFarEnemy.lastOriginalAiDecision.result ==
                   mw::battle::BattleOriginalAiDecisionResult::FireRequested &&
                   !phase13FriendlyFarEnemy.lastOriginalAiDecision.
                       friendlyFireLaneSuppressed &&
                   phase13FriendlyFarEnemy.weaponShotsFired == 1u,
               "phase13 farther same-side centerline candidate must not replace the nearer target");

        auto phase13FriendlyOffCenterParams = phase13LaserParams;
        addPhase13FriendlyCandidate(
            phase13FriendlyOffCenterParams,
            400.0,
            -500.0,
            "opposing:phase13:friendly_off_center_open");
        const auto phase13FriendlyOffCenter =
            mw::battle::runBattleReplay(
                phase13FriendlyOffCenterParams, {}, 2u);
        expect(phase13EnemyFrom(phase13FriendlyOffCenter)
                       .lastOriginalAiDecision.result ==
                   mw::battle::BattleOriginalAiDecisionResult::FireRequested &&
                   phase13EnemyFrom(phase13FriendlyOffCenter)
                       .lastOriginalAiDecision.
                           targetSelectionPlanarFixedAngleProven &&
                   !phase13EnemyFrom(phase13FriendlyOffCenter)
                        .lastOriginalAiDecision.friendlyFireLaneSuppressed &&
                   phase13EnemyFrom(phase13FriendlyOffCenter)
                           .weaponShotsFired == 1u &&
                   phase13FriendlyOffCenter.lastShot.sequence == 1u,
               "phase13 same-side candidate outside the recovered envelope must preserve fire");

        auto phase13FriendlyInsideAngleParams = phase13LaserParams;
        addPhase13FriendlyCandidate(
            phase13FriendlyInsideAngleParams,
            100.0,
            -500.0,
            "opposing:phase13:friendly_inside_fixed_angle");
        const auto phase13FriendlyInsideAngle =
            mw::battle::runBattleReplay(
                phase13FriendlyInsideAngleParams, {}, 2u);
        const auto& phase13FriendlyInsideAngleDecision =
            phase13EnemyFrom(phase13FriendlyInsideAngle)
                .lastOriginalAiDecision;
        expect(phase13FriendlyInsideAngleDecision.result ==
                   mw::battle::BattleOriginalAiDecisionResult::
                       RejectedFriendlyFireLane &&
                   phase13FriendlyInsideAngleDecision.
                       targetSelectionPlanarFixedAngleProven &&
                   !phase13FriendlyInsideAngleDecision.
                       targetSelectionCenterlineSubsetProven &&
                   phase13FriendlyInsideAngleDecision.
                       friendlyFireLaneSuppressed &&
                   phase13FriendlyInsideAngleDecision.
                       blockingCombatantDistance == 537.0 &&
                   phase13FriendlyInsideAngleDecision.
                       selectedCandidateYawDelta == 2048 &&
                   phase13FriendlyInsideAngleDecision.
                       selectedCandidateHalfWidth == 3360 &&
                   phase13FriendlyInsideAngle.lastShot.sequence == 0u,
               "phase13 nearer same-side candidate inside the recovered envelope must suppress fire");

        auto phase13FriendlyBoundaryParams = phase13LaserParams;
        addPhase13FriendlyCandidate(
            phase13FriendlyBoundaryParams,
            161.0,
            -500.0,
            "opposing:phase13:friendly_on_fixed_angle_boundary");
        const auto phase13FriendlyBoundary =
            mw::battle::runBattleReplay(
                phase13FriendlyBoundaryParams, {}, 2u);
        const auto& phase13FriendlyBoundaryDecision =
            phase13EnemyFrom(phase13FriendlyBoundary)
                .lastOriginalAiDecision;
        expect(phase13FriendlyBoundaryDecision.result ==
                   mw::battle::BattleOriginalAiDecisionResult::
                       RejectedFriendlyFireLane &&
                   phase13FriendlyBoundaryDecision.
                       selectedCandidateYawDelta == 3232 &&
                   phase13FriendlyBoundaryDecision.
                       selectedCandidateHalfWidth == 3232 &&
                   phase13FriendlyBoundary.lastShot.sequence == 0u,
               "phase13 a660 inclusive fixed-angle boundary must suppress fire");

        auto phase13FriendlyPastBoundaryParams = phase13LaserParams;
        addPhase13FriendlyCandidate(
            phase13FriendlyPastBoundaryParams,
            162.0,
            -500.0,
            "opposing:phase13:friendly_past_fixed_angle_boundary");
        const auto phase13FriendlyPastBoundary =
            mw::battle::runBattleReplay(
                phase13FriendlyPastBoundaryParams, {}, 2u);
        expect(phase13EnemyFrom(phase13FriendlyPastBoundary)
                       .lastOriginalAiDecision.result ==
                   mw::battle::BattleOriginalAiDecisionResult::FireRequested &&
                   !phase13EnemyFrom(phase13FriendlyPastBoundary)
                        .lastOriginalAiDecision.friendlyFireLaneSuppressed &&
                   phase13FriendlyPastBoundary.lastShot.sequence == 1u,
               "phase13 candidate one fixed-angle unit outside a660 must preserve fire");

        const auto phase13FriendlyBlockedReplay =
            mw::battle::runBattleReplay(
                phase13FriendlyBlockedParams, {}, 2u);
        mw::battle::BattleWorld phase13FriendlyCadenceWorld =
            mw::battle::BattleWorld::create(
                phase13FriendlyBlockedParams);
        for (uint64_t tick = 0; tick < 2u; ++tick) {
            (void)phase13FriendlyCadenceWorld.snapshot();
            (void)phase13FriendlyCadenceWorld.snapshot();
            phase13FriendlyCadenceWorld.tick();
            (void)phase13FriendlyCadenceWorld.snapshot();
        }
        expect(mw::battle::battleSnapshotFingerprint(
                   phase13FriendlyBlocked) ==
                   mw::battle::battleSnapshotFingerprint(
                       phase13FriendlyBlockedReplay) &&
                   mw::battle::battleSnapshotFingerprint(
                       phase13FriendlyBlocked) ==
                       mw::battle::battleSnapshotFingerprint(
                           phase13FriendlyCadenceWorld.snapshot()),
               "phase13 friendly-lane decision must be replay and render-cadence independent");
        auto phase13FriendlyDiagnosticMutation = phase13FriendlyBlocked;
        auto mutatedFriendlyShooter = std::find_if(
            phase13FriendlyDiagnosticMutation.combatants.begin(),
            phase13FriendlyDiagnosticMutation.combatants.end(),
            [](const mw::battle::CombatantSnapshot& combatant) {
                return !combatant.playerControlled;
            });
        expect(mutatedFriendlyShooter !=
                   phase13FriendlyDiagnosticMutation.combatants.end(),
               "phase13 friendly-lane fingerprint mutation lost shooter");
        mutatedFriendlyShooter->lastOriginalAiDecision.
            blockingCombatantDistance += 1.0;
        expect(mw::battle::battleSnapshotFingerprint(
                   phase13FriendlyBlocked) !=
                   mw::battle::battleSnapshotFingerprint(
                       phase13FriendlyDiagnosticMutation),
               "phase13 fingerprint must include friendly-lane diagnostics");
        auto phase13AngleDiagnosticMutation = phase13OffAxisInside;
        auto mutatedAngleShooter = std::find_if(
            phase13AngleDiagnosticMutation.combatants.begin(),
            phase13AngleDiagnosticMutation.combatants.end(),
            [](const mw::battle::CombatantSnapshot& combatant) {
                return !combatant.playerControlled;
            });
        expect(mutatedAngleShooter !=
                   phase13AngleDiagnosticMutation.combatants.end(),
               "phase13 fixed-angle fingerprint mutation lost shooter");
        ++mutatedAngleShooter->lastOriginalAiDecision.
            selectedCandidateYawDelta;
        expect(mw::battle::battleSnapshotFingerprint(
                   phase13OffAxisInside) !=
                   mw::battle::battleSnapshotFingerprint(
                       phase13AngleDiagnosticMutation),
               "phase13 fingerprint must include fixed-angle diagnostics");
        auto phase13VerticalDiagnosticMutation = phase13Laser;
        auto mutatedVerticalShooter = std::find_if(
            phase13VerticalDiagnosticMutation.combatants.begin(),
            phase13VerticalDiagnosticMutation.combatants.end(),
            [](const mw::battle::CombatantSnapshot& combatant) {
                return !combatant.playerControlled;
            });
        expect(mutatedVerticalShooter !=
                   phase13VerticalDiagnosticMutation.combatants.end(),
               "phase13 vertical fingerprint mutation lost shooter");
        ++mutatedVerticalShooter->lastOriginalAiDecision.
            selectedCandidateVerticalDelta;
        expect(mw::battle::battleSnapshotFingerprint(phase13Laser) !=
                   mw::battle::battleSnapshotFingerprint(
                       phase13VerticalDiagnosticMutation),
               "phase13 fingerprint must include zero-pitch vertical diagnostics");

        const auto phase13TieParams = phase13ParamsFor(
            "jenner", 1000.0, {1u, 2u}, 20);
        const auto phase13Tie =
            mw::battle::runBattleReplay(phase13TieParams, {}, 2u);
        expect(phase13EnemyFrom(phase13Tie)
                       .lastOriginalAiDecision.weaponInstanceId == 2u,
               "phase13 equal-score tie must retain first installation");

        const auto phase13ProjectileParams = phase13ParamsFor(
            "shadow_hawk", 2000.0, {0u}, 20);
        mw::battle::BattleWorld phase13ProjectileWorld =
            mw::battle::BattleWorld::create(phase13ProjectileParams);
        phase13ProjectileWorld.runTicks(2u);
        const auto phase13ProjectileLaunch = phase13ProjectileWorld.snapshot();
        expect(phase13ProjectileLaunch.projectiles.size() == 1u &&
                   phase13ProjectileLaunch.lastShot.deliveryState ==
                       mw::battle::BattleShotDeliveryState::ProjectileInFlight &&
                   phase13ProjectileLaunch.lastShot.originalAiDecision.valid,
               "phase13 AC/5 decision must enter the common projectile pool: projectiles=" +
                   std::to_string(phase13ProjectileLaunch.projectiles.size()) +
                   " delivery=" + std::to_string(static_cast<int>(
                       phase13ProjectileLaunch.lastShot.deliveryState)) +
                   " decision=" + std::to_string(static_cast<int>(
                       phase13EnemyFrom(phase13ProjectileLaunch)
                           .lastOriginalAiDecision.result)));
        phase13ProjectileWorld.runTicks(20u);
        const auto phase13ProjectileImpact = phase13ProjectileWorld.snapshot();
        expect(phase13ProjectileImpact.lastShot.sequence == 1u &&
                   phase13ProjectileImpact.lastShot.deliveryState ==
                       mw::battle::BattleShotDeliveryState::ProjectileImpact &&
                   phase13ProjectileImpact.lastShot.damageApplied == 5,
               "phase13 AC/5 delivery must resolve through common damage");

        auto phase13HotParams = phase13ParamsFor(
            "warhammer", 2000.0, {0u}, 20);
        phase13HotParams.combatantLaunchStates[0]
            .persistentMechState.engine = 2u;
        mw::battle::BattleWorld phase13HotWorld =
            mw::battle::BattleWorld::create(phase13HotParams);
        phase13HotWorld.runTicks(64u);
        const auto phase13HeatGate = phase13HotWorld.snapshot();
        expect(phase13EnemyFrom(phase13HeatGate).heat.rawHeat >= 800 &&
                   phase13EnemyFrom(phase13HeatGate)
                           .lastOriginalAiDecision.result ==
                       mw::battle::BattleOriginalAiDecisionResult::
                           RejectedWeaponHeatGate,
               "phase13 strict 0x5A0 heat gate or proven heat addition changed");
        phase13HotWorld.runTicks(170u);
        const auto phase13Shutdown = phase13HotWorld.snapshot();
        expect(phase13EnemyFrom(phase13Shutdown).heat.reactorShutdown &&
                   phase13EnemyFrom(phase13Shutdown)
                           .lastOriginalAiDecision.result ==
                       mw::battle::BattleOriginalAiDecisionResult::
                           RejectedShooterOffline,
               "phase13 reactor shutdown must reject AI through common readiness");

        const auto phase13ReplayA =
            mw::battle::runBattleReplay(phase13LaserParams, {}, 4u);
        const auto phase13ReplayB =
            mw::battle::runBattleReplay(phase13LaserParams, {}, 4u);
        mw::battle::BattleWorld phase13CadenceWorld =
            mw::battle::BattleWorld::create(phase13LaserParams);
        for (uint64_t tick = 0; tick < 4u; ++tick) {
            (void)phase13CadenceWorld.snapshot();
            (void)phase13CadenceWorld.snapshot();
            phase13CadenceWorld.tick();
            (void)phase13CadenceWorld.snapshot();
        }
        expect(mw::battle::battleSnapshotFingerprint(phase13ReplayA) ==
                   mw::battle::battleSnapshotFingerprint(phase13ReplayB) &&
                   mw::battle::battleSnapshotFingerprint(phase13ReplayA) ==
                       mw::battle::battleSnapshotFingerprint(
                           phase13CadenceWorld.snapshot()),
               "phase13 replay or render/snapshot cadence independence changed");
        expect(mw::battle::battleSnapshotFingerprint(phase13ReplayA) !=
                   mw::battle::battleSnapshotFingerprint(phase13Range),
               "phase13 fingerprint must include decision-sensitive state");

        std::cout
            << "mw_battle_sim_smoke: ok"
            << " ticks=" << finalA.tickIndex
            << " elapsed_ms=" << finalA.elapsedMs
            << " scenario=" << finalA.terrain.scenarioIndex
            << " terrain_mode=" << static_cast<int>(finalA.terrain.terrainMode)
            << " tiles=" << joinStrings(finalA.terrain.tileNames)
            << " mission_id=" << static_cast<int>(finalA.mission.originalId)
            << " mission_family=" << mw::battle::battleMissionFamilyName(finalA.mission.family)
            << " mission_title=" << mw::battle::battleMissionTitleToken(finalA.mission)
            << " contract=yes"
            << " contract_provenance=" << finalA.contract.provenance
            << " contract_employer=" << finalA.contract.employerHouseName
            << " contract_target_house=" << finalA.contract.targetHouseName
            << " contract_target_planet=" << finalA.contract.targetPlanetName
            << " contract_environment_id=" << (finalA.contract.targetEnvironmentId ? *finalA.contract.targetEnvironmentId : -1)
            << " contract_estimated_force="
            << finalA.contract.estimatedHeavyMechs << ","
            << finalA.contract.estimatedMediumMechs << ","
            << finalA.contract.estimatedLightMechs
            << " contract_btech_count_mapping=proven"
            << " contract_btech_preset_mapping=proven"
            << " contract_btech_slot_mapping=proven"
            << " contract_btech_spawn_requests=" << joinOppositionRequests(finalA.contract.oppositionSpawnPlan)
            << " btech_spawn_probe=" << joinOppositionRequests(cappedOppositionProbe)
            << " btech_spawn_slots=" << joinOppositionSlots(cappedOppositionProbe)
            << " btech_spawn_classes=" << joinOppositionClasses(cappedOppositionProbe)
            << " btech_type_presets=" << joinBtechMechTypePresets()
            << " btech_wasp_wolverine=excluded"
            << " btech_spawn_limit=" << cappedOppositionProbe.objectLimit
            << " btech_zero_opposition=empty_deferred"
            << " contract_garrison_auto_resolve=deferred"
            << " roster_player=" << mw::battle::battleTeamName(player.roster.team)
            << ":" << player.roster.factionHouseName
            << ":" << player.roster.sourceSlot
            << ":" << player.roster.provenance
            << " roster_explicit_enemy="
            << mw::battle::battleTeamName(explicitEnemySnapshot.roster.team)
            << ":" << explicitEnemySnapshot.roster.factionHouseName
            << ":" << explicitEnemySnapshot.roster.sourceSlot
            << ":" << explicitEnemySnapshot.roster.provenance
            << " roster_spawn_count=" << explicitRosterSnapshot.combatants.size()
            << " roster_estimate_spawn=deferred"
            << " enemy_ai=deterministic"
            << " enemy_ai_target_team=player"
            << " enemy_ai_player_activation=search_then_approach"
            << " enemy_ai_search_state="
            << mw::battle::battleEnemyAiStateName(enemyAiSearch.combatants[1].enemyAiState)
            << " enemy_ai_approach_state="
            << mw::battle::battleEnemyAiStateName(aiEnemy.enemyAiState)
            << " enemy_ai_attack_state="
            << mw::battle::battleEnemyAiStateName(enemyAiAttack.combatants[1].enemyAiState)
            << " enemy_ai_protect_target=" << protectAiTargetKind
            << " enemy_ai_protect_state=" << protectAiState
            << " enemy_ai_replay_match=yes"
            << " objective_runtime=" << protectObjectiveState
            << ":code" << protectObjectiveResult.result->rawResultCode
            << ":ticks" << protectObjectiveResult.ticksExecuted
            << " objective_damage="
            << protectObjectiveResult.finalSnapshot.objective.damage
            << "/" << protectObjectiveResult.finalSnapshot.objective.maxDamage
            << ":" << protectObjectiveDepleted
            << " combat_runtime=deterministic"
            << " combat_victory=" << combatVictoryState
            << ":code" << combatVictoryA.result->rawResultCode
            << ":ticks" << combatVictoryA.ticksExecuted
            << " combat_defeat=" << combatDefeatState
            << ":code" << combatDefeat.result->rawResultCode
            << ":ticks" << combatDefeat.ticksExecuted
            << " combat_retreat=" << combatRetreatState
            << ":core" << retreatEnemyCore->damage
            << ":shots" << combatRetreatEnemy.weaponShotsFired
            << " combat_core_damage="
            << victoryEnemyCore->damage << "/" << victoryEnemyCore->maxDamage
            << ":" << combatCoreStatus
            << " final_roster=battlemaster:3,warhammer:1"
            << " final_roster_provenance=" << finalRosterSnapshot.oppositionRoster.provenance
            << " final_roster_context=0x63->selector12"
            << " final_roster_slots=" << joinOppositionRosterSlots(finalRosterSnapshot.oppositionRoster)
            << " final_roster_spawn_order=proven"
            << " final_roster_placement=proven"
            << " final_roster_spawn=explicit_static:4"
            << " final_roster_runtime=deterministic:"
            << finalRuntimeOpposingTargets
            << " placement_mode_table=BTECH.EXE:DS092C+DS08D8"
            << " mission_handoff_selector10=briefing:10,handoff:11,placement:10"
            << " mission_extended_selector17=pre_remap_diagnostic"
            << " placement_selector10_modes=3:3"
            << " placement_mode4_selectors=0,1,2,3,4,5,6,8,14,17,20"
            << " placement_damage_suppressed_selectors=10,11,12,13"
            << " placement_depletion_result=BTECH:mode3->flags:player_win_precedence"
            << " objective_intent_catalog=MW_MAIN.EXE:exact_titles"
            << " objective_retrieve_selectors=10,11,12,13,32,33:title_typed"
            << " objective_protect_selector2=bound:water_factory"
            << " objective_protect_selector7=diagnostic:landing_facilities"
            << " objective_destroy_selector22=diagnostic:water_factory"
            << " objective_disable_selector23=diagnostic:weapons_factory"
            << " battlefield_bounds=-44160..44160,-24000..24000"
            << " battlefield_exit_status=-2:north,-3:south,-4:west,-5:east"
            << " battlefield_exit_bits=1:north,2:south,4:west,8:east"
            << " battlefield_mode1_selector18_masks="
            << static_cast<int>(mode1Setup.battlefieldBoundary.playerAllowedExitMask) << ","
            << static_cast<int>(mode1Setup.battlefieldBoundary.opposingAllowedExitMask)
            << " battlefield_player_exit_results=ordinary:2,allowed:0"
            << " battlefield_outcome_evaluator=active"
            << " mission_runtime_start="
            << mw::battle::battleMissionRuntimeStateName(start.missionRuntimeState)
            << " mission_runtime_boundary_ordinary="
            << mw::battle::battleMissionRuntimeStateName(ordinaryExitSnapshot.missionRuntimeState)
            << ":code" << ordinaryExitSnapshot.result.rawResultCode
            << ":" << mw::battle::battlefieldBoundaryEdgeName(ordinaryExitSnapshot.result.exitEdge)
            << ":status" << ordinaryExitSnapshot.result.actorExitStatus
            << " mission_runtime_boundary_allowed="
            << mw::battle::battleMissionRuntimeStateName(allowedExitSnapshot.missionRuntimeState)
            << ":code" << allowedExitSnapshot.result.rawResultCode
            << ":" << mw::battle::battlefieldBoundaryEdgeName(allowedExitSnapshot.result.exitEdge)
            << ":status" << allowedExitSnapshot.result.actorExitStatus
            << " mission_result_api=available"
            << " battle_launch_result_api=standalone"
            << " dedicated_objective_selector10=absent_diagnostic"
            << " dedicated_objective_selector0=player:15@0x0e38"
            << " dedicated_objective_selector17=opposing:8@0x0e00"
            << " objective=yes"
            << " objective_provenance=" << finalA.objective.provenance
            << " objective_slot=" << finalA.objective.sourceSlot
            << " objective_transform_proven=" << (finalA.objective.transformProven ? "yes" : "no")
            << " objective_active_object_proven=" << (finalA.objective.activeObjectProven ? "yes" : "no")
            << " objective_mission_semantics_proven=" << (finalA.objective.missionSemanticsProven ? "yes" : "no")
            << " objective_damage_policy_proven=" << (finalA.objective.damagePolicyProven ? "yes" : "no")
            << " objective_damage_suppressed=" << (finalA.objective.damageSuppressed ? "yes" : "no")
            << " objective_depletion_policy_proven=" << (finalA.objective.depletionPolicyProven ? "yes" : "no")
            << " objective_depletion_sets_player_win=" << (finalA.objective.depletionSetsPlayerWinCondition ? "yes" : "no")
            << " objective_depletion_sets_player_loss=" << (finalA.objective.depletionSetsPlayerLossCondition ? "yes" : "no")
            << " objective_depletion_result_code=" << finalA.objective.depletionResultCode
            << " objective_model=" << finalA.objective.staticModel.resourceName
            << ":" << finalA.objective.staticModel.recordIndex
            << " objective_model_animation=" << (finalA.objective.staticModel.animated ? "animated" : "static")
            << " player_id=" << player.id.value
            << " preset=" << player.mechPresetId
            << std::fixed << std::setprecision(4)
            << " x=" << player.transform.x
            << " y=" << player.transform.y
            << " z=" << player.transform.z
            << " heading=" << player.transform.headingRadians
            << " walk_ms=" << player.walkAnimationElapsedMs
            << " target_speed=" << player.targetForwardSpeed
            << " current_speed=" << player.forwardSpeed
            << " camera=yes"
            << " camera_y=" << finalA.camera.transform.y
            << " camera_heading=" << finalA.camera.transform.headingRadians
            << " camera_x=" << finalA.camera.transform.x
            << " camera_z=" << finalA.camera.transform.z
            << " mech_systems=whole_runtime_v0"
            << " mech_systems_weapons_after_arm=" << armWeaponsSystemStatus
            << " mech_systems_leg_after_death=" << legMobilitySystemStatus
            << " mech_systems_jump_jets=" << jumpJetSystemStatus
            << " arm_hidden=yes"
            << " leg_death=yes"
            << " extended_campaign_carryover=yes"
            << " next_scenario=" << nextMissionStart.terrain.scenarioIndex
            << " minimal_repair=yes"
            << " fragile_after_minimal_repair=yes"
            << " boundary_clamp=yes"
            << " reverse_motion=yes"
            << " turn_motion=yes"
            << " turn_step_walk=yes"
            << " torso_twist=yes"
            << " aim_pitch=yes"
            << " jump=yes"
            << " jump_landing_momentum=yes"
            << " phase12_weapon_loadouts=8:light,medium,heavy"
            << " phase12_ammo_capacity=BTECH:8rows:gam6x12:warhammer15+200"
            << " phase12_player_friendly_hit=BTECH47f7:immediate+projectile"
            << " phase12_weapon_tick_ownership=yes"
            << " phase12_weapon_cooldown=BTECH:updates30,15,2:compat_seconds3.0,1.5,0.2"
            << " phase12_weapon_ammo_energy=yes"
            << " phase12_weapon_nonfunctional_fail_closed=yes"
            << " phase12_weapon_original_fields=damage+heat+range4"
            << " phase12_heat=BTECH:weapon_rows:jump12+20:ground_speed_0+1+2:normal_sinks4over3:"
               "engine_condition_x5:arctic_x2:max2400:gauge80x30:"
               "cadence_beta:replay_owned"
            << " phase12_reactor_shutdown=BTECH:strict_gt1600:recover_le1600:"
               "movement+jump+weapon_blocked:bit2_warning:replay_owned"
            << " phase12_sgel=green,yellow,red,black:single_detailed_state"
            << " phase12_sensors=rare+frequent_blink:destroyed_blank+no_target"
            << " phase12_gyros=damaged_slow:destroyed_stop"
            << " phase12_engine=damage_heat:destroyed_shutdown"
            << " phase12_life_support=destroyed_death"
            << " phase12_selected_target_damage=MG2,M_LAS5,SRM4_cluster2d6:CT_provisional"
            << " phase12_projectiles=AC5+LRM5+SRM2+SRM4+SRM6:battle_owned"
            << " phase12_projectile_delivery=launch_then_impact:no_splash"
            << " phase12_projectile_terrain=ground+WLD_prism:stop:no_damage:gray5-7"
            << " phase12_projectile_intercept=laser_exact_OTHPCK_missile:no_damage:AC5_immune"
            << " phase12_projectile_replay=yes"
            << " phase12_impact_events=six_stages:two_fixed_ticks_each:"
               "combatant+objective:replay_owned"
            << " phase12_weapon_range_rejection=yes"
            << " phase12_objective_weapon_target=separate:damage5"
            << " phase12_detailed_damage=BTECH:9armor+8internal"
            << " phase12_entry_damage=BTECH:combined_points"
            << " phase12_internal_transition=BTECH:E4C:limb_to_torso+CT_to_rear"
            << " phase12_critical_resolution=BTECH:2d6_8plus:full_ordered_candidates:"
               "ammo_pool_zero+jump_slots+post_internal_attempt+fallback_tables:replay_owned"
            << " phase12_crosshair_hit=torso_partition_provisional:head_proven:leg_proven:miss:no_scan:enemy_request"
            << " phase12_stationary_hit_replay=yes"
            << " phase12_render_cadence_independent=yes"
            << " phase12_target_scan=enter:4000m:stable_cycle:replay_owned"
            << " phase13_ai=stationary_single_target_original_vertical"
            << " phase13_ai_caller=71e1>7d2e>a3ee>b961>a660>c74e>a79f>a86d>aae0"
            << " phase13_ai_policy=typed_with_phase10_compatibility"
            << " phase13_ai_acquisition=ready_range_minus400:strict:integer_zero_pitch_3d_fixed_angle"
            << " phase13_ai_friendly_lane=flag1:inside_nearer_suppresses:outside_or_farther_preserves"
            << " phase13_ai_vertical=strict_b60:zero_pitch:nonzero_pitch_open"
            << " phase13_ai_heading_commands=BTECH8307:cap38e:strict18e2+BTECH84de:c86c:cap222:diagnostic"
            << " phase13_ai_raw_movement=BTECH84de:speed+5:decel-direct+BTECHc5ac:zero-attitude:diagnostic"
            << " phase13_ai_speed_rows=BTECH+06:72,66,54,48,36,36,36,36"
            << " phase13_ai_height_rows=BTECH+02:500,600,700,700,800,800,900,960"
            << " phase13_ai_live_slots=BTECH:player0+allies1-3+opposition4-7:roster_snapshot_fingerprint"
            << " phase13_ai_relation_cell=BTECH5454:5206_or_50ee:clip_bit2+bounds+list_bit1:mirrored"
            << " phase13_ai_50ee=BTECH50f9:strict35000:legacy_display_bit1:previous_frame_typed"
            << " phase13_ai_50ee_invariant_movement=BTECH23bf:slot0_relation0_or2:same_companion_winner:symmetric_lab"
            << " phase13_ai_lance_orders=BTECH02b6+0ba5+6fd8:orders0_5:targets0_2_3:default_act_on_own"
            << " phase13_ai_act_on_own_routes=BTECH7797:mode0_2_23bf:mode1_table_point:mode3_CCEE:mode4_status7"
            << " phase13_ai_mode3_null_point=BTECH:loader_zero_CCEE:DS0008_runtime_bytes:player_companion_repeated"
            << " phase13_ai_mode3_compatibility=typed:destroy_disable_retrieve_objective_point:three_companions:no_live_target"
            << " phase13_ai_campaign_movement=default_act_on_own:symmetric_modes0_2:player_mode0_2_vs_mode4:mode3_mission_objective_point_compatibility"
            << " phase13_ai_live_mission_matrix=garrison_mode4_hold:defense_mode4_hold:suppression_mode2_ordered_live_target:rescue_mode3_objective_point"
            << " phase13_ai_5206_near=BTECH5206:strict_dx_dz_below512:immediate1:no_GRD:height_no_effect"
            << " phase13_ai_5206_sampled=BTECH5206+5559:step512:terminal_probe:GRD_raw_shift4:in_bounds"
            << " phase13_ai_control_init=BTECH6fd8:occupied:zero55+side+slot+minus1+flag1:unoccupied_untouched"
            << " phase13_ai_initial_pose=BTECH1a22:SNARIO_XZ+Y300:modes0_2_4_shared:m1_flags:m3_fresh_null_CCEE_DS0008"
            << " phase13_ai_raw_placement=SNARIO:slots0-7:setup_snapshot_fingerprint"
            << " phase13_ai_motion_state=BTECH1a22+6fd8:modes0_1_2_3_4:combatant_snapshot_fingerprint"
            << " phase13_ai_first_live_movement=BTECH71e1:first:player_slot1_3:5206_in_bounds:one_commit:0.1s_provisional"
            << " phase13_ai_repeated_live_movement=BTECH71e1+9bfa:mode0_action3:player_slot1_3:three_commits:0.1s_provisional"
            << " phase13_ai_multi_target_movement=BTECH4ab5+b961+23bf:ordered_score+count_penalty:mode0_lab"
            << " phase13_ai_nonobjective_modes_movement=BTECH1a22+71e1:modes0_1_2:multi_target_repeated_lab"
            << " phase13_ai_aggregate=BTECH7070+4106+12738:armor9+internal8+damage2x:wrap16:minus1:boundary_minus2to5"
            << " phase13_ai_aggregate_binding=Phase12:detailed9+8+critical+installed_damage:23bf_single_candidate"
            << " phase13_ai_selected_target_movement=BTECH7c3c+7797+94a4+95a1+9700+84de+87c4:caller_owned_point:interior_probe_domain:accept_or_rollback"
            << " phase13_ai_planar_projection=BTECH1b25a+1b280:Q14:SAR14:diagnostic"
            << " phase13_ai_pose_commit=BTECH87c4:ordinary:accept+restore:diagnostic"
            << " phase13_ai_contact_probes=BTECH12738+89ea+8c38+5dea:diagnostic"
            << " phase13_ai_slot_probe=BTECH89ea:boundary>slots0-7>objective:word0_1:zero_fill541A"
            << " phase13_ai_scene_selector=BTECHd5ce+d3f2+d18d:cached_priority:diagnostic"
            << " phase13_ai_movement_target=BTECH94a4+7c7d:strict160:steering_owned"
            << " phase13_ai_steering=BTECH95a1:tables2438-2456:probe0-3:diagnostic"
            << " phase13_ai_terrain_probe=BTECH9700+9939:up_to4x5x5:early_block_exact:raw_GRD_174x94"
            << " phase13_ai_movement_lane=BTECH9700+9a77:strict2048+3ffc+width:diagnostic"
            << " phase13_ai_raw_world_bridge=SNARIO512:invertZ:c5ac:cadence_provisional"
            << " phase13_ai_movement_owner=BTECH27fb+4ab5+23bf+7c3c:multi_cross_side_cached_pose"
            << " phase13_ai_scene_catalog=BTECH242c+afd5+b0af+1e78+d5ce+d3e6+d330+c42e:records30:objects14:query14:owned_fingerprint"
            << " phase13_ai_pose_transaction=BTECHc5ac+c42e+87c4+b377:ordinary:full_q14:cache_persistent:diagnostic"
            << " phase13_ai_gates=functional+cooldown+ammo+strict_range+heat+shutdown"
            << " phase13_ai_delivery=immediate+projectile_common_pipeline"
            << " phase13_ai_location=aspect_2d6_proven:prng_sequence_open"
            << " phase13_ai_replay=yes:render_cadence_independent"
            << " phase14_ai=ReplacementDeterministicCombatAi:replacement"
            << " phase14_ai_cadence=movement_every_fixed_tick:fire400ms:snapshot_owned"
            << " phase14_ai_slots=destroy_disable:stable_1_and_3"
            << " phase14_ai_navigation=terrain_astar_waypoints:stable_orbit12s:damaged_side:no_permanent_stop"
            << " phase14_ai_targeting=team_radar4000m:stable_distribution:reinforce_remaining"
            << " phase14_ai_fire=best_ready_installation:staggered400ms:torso_aim:terrain_los:"
               "friendly_lane:gunnery_range_motion_2d6:aspect_location:common_delivery"
            << " phase14_ai_wiki_catalog=34:single31:extended3"
            << " phase14_ai_retrieval=guard_trigger4000m_radar:indestructible:player_contact_victory"
            << " phase14_ai_defense=all8_bound_structures:immediate_objective_assault:team_radar_response"
            << " phase14_ai_deathmatch=both_nonplayer_sides:balanced_targets:immediate_charge"
            << " phase14_ai_assault=guard_trigger4000m_radar:enemy_or_structure_victory"
            << " phase14_ai_sprint=runners_to_far_edge:any_escape_defeat"
            << " phase14_ai_loss=all_player_mechs_destroyed:any_player_boundary_flee"
            << " phase14_ai_allied_retreat=damage35:boundary_escape:battle_continues"
            << " phase14_wiki_extended=three_random_stages:damage_carry:minimal_repair"
            << " phase14_wiki_final=deathmatch_then_retrieval"
            << " phase14_wiki_uneventful=one_in_32:garrison_duty_paid"
            << " phase14_ai_replay=yes:subdivision_equal:render_cadence_independent"
            << std::hex
            << " start_hash=" << startHash
            << " final_hash=" << hashA
            << std::dec
            << " stepped_match=yes"
            << " replay_match=yes\n";
        return 0;
    } catch (const std::exception& exc) {
        std::cerr << "mw_battle_sim_smoke: " << exc.what() << "\n";
        return 1;
    }
}
