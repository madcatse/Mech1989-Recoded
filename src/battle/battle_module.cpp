#include "battle/battle_module.h"

#include "legacy3d/shape_parser.h"
#include "mech3d/mech_catalog.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace mw::battle {

std::filesystem::path campaignBattleOriginalResourcePath(
    const std::filesystem::path& originalFilesRoot,
    const std::filesystem::path& categorizedRelativePath) {
    if (originalFilesRoot.empty() || categorizedRelativePath.empty()) {
        return {};
    }
    const std::filesystem::path flatPath =
        originalFilesRoot / categorizedRelativePath.filename();
    std::error_code error;
    if (std::filesystem::is_regular_file(flatPath, error) && !error) {
        return flatPath;
    }
    return originalFilesRoot / categorizedRelativePath;
}

namespace {

// MW_MAIN.EXE DS:8E46, indexed by the original one-based mission id.  Values
// are passed directly to FUN_101b_0c3e, whose campaign clock unit is half a day.
constexpr std::array<uint16_t, 37> kOriginalMissionDurationDateTicks = {{
    0,
    360, 60,
    28, 28, 28, 28,
    60, 60, 180, 60,
    2, 2, 2, 2,
    360, 360, 28, 360, 60,
    2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2,
}};

CampaignBattleOutcome campaignOutcomeFromRuntimeState(BattleMissionRuntimeState state) {
    switch (state) {
    case BattleMissionRuntimeState::Victory:
        return CampaignBattleOutcome::Victory;
    case BattleMissionRuntimeState::Defeat:
        return CampaignBattleOutcome::Defeat;
    case BattleMissionRuntimeState::Withdraw:
        return CampaignBattleOutcome::Withdraw;
    case BattleMissionRuntimeState::InProgress:
    case BattleMissionRuntimeState::Unsupported:
        break;
    }
    return CampaignBattleOutcome::Unsupported;
}

CampaignBattleLaunchSource campaignLaunchSourceFromSnapshot(
    const BattleSnapshot& startSnapshot) {
    if (startSnapshot.contract.valid) {
        return CampaignBattleLaunchSource::AcceptedContract;
    }
    if (startSnapshot.oppositionRoster.valid &&
        startSnapshot.oppositionRoster.compositionProven &&
        startSnapshot.oppositionRoster.sourceContextMissionByte == 0x63u &&
        startSnapshot.oppositionRoster.initialMissionSelector == 12u) {
        return CampaignBattleLaunchSource::DarkWingFinal;
    }
    return CampaignBattleLaunchSource::Unknown;
}

uint64_t campaignBattleConsequencePlanFingerprint(
    const CampaignBattleConsequencePlan& plan) {
    uint64_t hash = 1469598103934665603ull;
    const auto append = [&hash](uint64_t value) {
        for (int byte = 0; byte < 8; ++byte) {
            hash ^= (value >> (byte * 8)) & 0xffu;
            hash *= 1099511628211ull;
        }
    };
    append(static_cast<uint64_t>(plan.launchSource));
    append(static_cast<uint64_t>(plan.outcome));
    append(plan.commitEligible ? 1u : 0u);
    append(plan.payment);
    append(plan.salvage);
    append(static_cast<uint64_t>(static_cast<int64_t>(plan.reputationDelta)));
    for (int delta : plan.housePositiveDelta) {
        append(static_cast<uint64_t>(static_cast<int64_t>(delta)));
    }
    for (int delta : plan.houseNegativeDelta) {
        append(static_cast<uint64_t>(static_cast<int64_t>(delta)));
    }
    append(plan.campaignDateTicks);
    append(plan.pilotExperienceAward ? 1u : 0u);
    append(plan.salvageBetaValidationRequired ? 1u : 0u);
    append(plan.salvageDeferred ? 1u : 0u);
    append(plan.campaignTimeDeferred ? 1u : 0u);
    append(plan.pilotExperienceDeferred ? 1u : 0u);
    append(plan.persistentMechDamageDeferred ? 1u : 0u);
    append(plan.repairCostDeferred ? 1u : 0u);
    append(plan.pilotDeathDeferred ? 1u : 0u);
    append(plan.outcomeFingerprint);
    return hash;
}

int headingDegrees(double headingRadians) {
    constexpr double kPi = 3.14159265358979323846;
    int degrees = static_cast<int>(headingRadians * 180.0 / kPi + (headingRadians >= 0.0 ? 0.5 : -0.5));
    degrees %= 360;
    if (degrees < 0) {
        degrees += 360;
    }
    return degrees;
}

int signedDegrees(double radians) {
    constexpr double kPi = 3.14159265358979323846;
    return static_cast<int>(radians * 180.0 / kPi + (radians >= 0.0 ? 0.5 : -0.5));
}

const CombatantSnapshot* playerPresentationCombatant(const BattleSnapshot& snapshot) {
    for (const CombatantSnapshot& combatant : snapshot.combatants) {
        if (combatant.playerControlled || combatant.roster.team == BattleTeam::Player) {
            return &combatant;
        }
    }
    return nullptr;
}

const CombatantSnapshot* combatantByEntityId(
    const BattleSnapshot& snapshot,
    EntityId entityId) {
    const auto found = std::find_if(
        snapshot.combatants.begin(),
        snapshot.combatants.end(),
        [entityId](const CombatantSnapshot& combatant) {
            return combatant.id == entityId;
        });
    return found == snapshot.combatants.end() ? nullptr : &*found;
}

const BattleProjectileState* projectileById(
    const BattleSnapshot& snapshot,
    uint32_t projectileId) {
    const auto found = std::find_if(
        snapshot.projectiles.begin(),
        snapshot.projectiles.end(),
        [projectileId](const BattleProjectileState& projectile) {
            return projectile.valid && projectile.projectileId == projectileId;
        });
    return found == snapshot.projectiles.end() ? nullptr : &*found;
}

BattleHitPoint normalizedHitPoint(const BattleHitPoint& value);

const BattleWeaponInstanceState* weaponByInstanceId(
    const CombatantSnapshot& combatant,
    uint32_t weaponInstanceId) {
    const auto found = std::find_if(
        combatant.weapons.begin(),
        combatant.weapons.end(),
        [weaponInstanceId](const BattleWeaponInstanceState& weapon) {
            return weapon.weaponInstanceId == weaponInstanceId;
        });
    return found == combatant.weapons.end() ? nullptr : &*found;
}

bool immediateBeamWeaponType(const std::string& weaponTypeId) {
    return weaponTypeId == "small_laser" ||
        weaponTypeId == "medium_laser" ||
        weaponTypeId == "large_laser" ||
        weaponTypeId == "ppc";
}

bool acceptedShotResult(BattleShotResult result) {
    switch (result) {
    case BattleShotResult::Hit:
    case BattleShotResult::HitProjectile:
    case BattleShotResult::MissOutOfRange:
    case BattleShotResult::MissUnresolved:
    case BattleShotResult::MissCrosshairRay:
        return true;
    default:
        return false;
    }
}

double hitPointLengthSquared(const BattleHitPoint& point) {
    return point.x * point.x + point.y * point.y + point.z * point.z;
}

std::optional<CampaignBattleRenderBeamInstance> immediateBeamFromSnapshot(
    const BattleSnapshot& snapshot,
    const BattleShotDiagnostic& shot) {
    constexpr uint64_t kProvisionalLifetimeTicks = 2u;
    const uint64_t ageTicks = snapshot.tickIndex >= shot.tickIndex
        ? snapshot.tickIndex - shot.tickIndex
        : 0u;
    if (!shot.valid || ageTicks == 0u ||
        ageTicks > kProvisionalLifetimeTicks ||
        !acceptedShotResult(shot.result)) {
        return std::nullopt;
    }
    const CombatantSnapshot* shooter = combatantByEntityId(
        snapshot, shot.shooterEntityId);
    if (shooter == nullptr) {
        return std::nullopt;
    }
    const BattleWeaponInstanceState* weapon = weaponByInstanceId(
        *shooter, shot.weaponInstanceId);
    if (weapon == nullptr ||
        weapon->deliveryMode != BattleWeaponDeliveryMode::Immediate ||
        !immediateBeamWeaponType(weapon->weaponTypeId)) {
        return std::nullopt;
    }
    BattleHitPoint rawDirection = shot.aimDirection;
    if (hitPointLengthSquared(rawDirection) <=
        std::numeric_limits<double>::epsilon()) {
        rawDirection = {
            shot.impactPoint.x - shot.aimOrigin.x,
            shot.impactPoint.y - shot.aimOrigin.y,
            shot.impactPoint.z - shot.aimOrigin.z,
        };
    }
    if (hitPointLengthSquared(shot.aimOrigin) <=
            std::numeric_limits<double>::epsilon() ||
        hitPointLengthSquared(rawDirection) <=
            std::numeric_limits<double>::epsilon()) {
        return std::nullopt;
    }
    const BattleHitPoint direction = normalizedHitPoint(rawDirection);

    CampaignBattleRenderBeamInstance beam;
    beam.shotSequence = shot.sequence;
    beam.shotTickIndex = shot.tickIndex;
    beam.shooterEntityId = shot.shooterEntityId;
    beam.weaponInstanceId = shot.weaponInstanceId;
    beam.weaponTypeId = weapon->weaponTypeId;
    beam.locationId = weapon->locationId;
    beam.start = shot.aimOrigin;

    // Captured DOS frames close the launch side to the installed location,
    // while the exact original hardpoint coordinates remain unidentified.
    // Keep the small compatibility offset presentation-only and explicitly
    // provisional instead of moving the authoritative aim ray.
    double lateralSign = 0.0;
    if (!weapon->locationId.empty() && weapon->locationId.front() == 'L') {
        lateralSign = -1.0;
    } else if (!weapon->locationId.empty() &&
               weapon->locationId.front() == 'R') {
        lateralSign = 1.0;
    }
    constexpr double kProvisionalMountLateralOffset = 70.0;
    const double heading = shooter->transform.headingRadians +
        shooter->torsoYawRadians;
    // The cockpit camera looks along (+sin(h), +cos(h)); its screen-right
    // basis is (-cos(h), +sin(h)) in battle X/Z coordinates.
    beam.start.x -= std::cos(heading) * lateralSign *
        kProvisionalMountLateralOffset;
    beam.start.z += std::sin(heading) * lateralSign *
        kProvisionalMountLateralOffset;

    // User-selected presentation tuning: keep the near end closer to the
    // lower edge of the cockpit viewport.  This does not move the shot's
    // authoritative aim origin or impact point.
    constexpr double kProvisionalMountVerticalOffset = 14.0;
    beam.start.y -= kProvisionalMountVerticalOffset;

    if (shot.hit && hitPointLengthSquared(shot.impactPoint) >
            std::numeric_limits<double>::epsilon()) {
        beam.end = shot.impactPoint;
    } else {
        const double length = std::max(shot.maximumRange, 500.0);
        beam.end = {
            shot.aimOrigin.x + direction.x * length,
            shot.aimOrigin.y + direction.y * length,
            shot.aimOrigin.z + direction.z * length,
        };
    }
    // FUN_1000_bc5e proves a four-point base converging on one endpoint.  Its
    // width table at DS:2802 is not yet mapped to replacement world units.
    beam.halfWidth = 21.0;
    beam.color = weapon->weaponTypeId == "ppc"
        ? CampaignBattleBeamColor::PpcCyan
        : CampaignBattleBeamColor::LaserYellow;
    beam.originalTopologyProven = true;
    beam.widthScaleProven = false;
    beam.originalOneTickLifetimeProven = false;
    beam.originalColorClassProven = true;
    beam.mountOffsetProven = false;
    return beam;
}

std::optional<CampaignBattleRenderMachineGunFlashInstance>
machineGunFlashFromSnapshot(
    const BattleSnapshot& snapshot,
    const BattleShotDiagnostic& shot,
    CampaignCockpitFamily family) {
    if (!shot.valid || snapshot.tickIndex != shot.tickIndex + 1u ||
        !acceptedShotResult(shot.result)) {
        return std::nullopt;
    }
    const CombatantSnapshot* shooter = combatantByEntityId(
        snapshot, shot.shooterEntityId);
    if (shooter == nullptr || !shooter->playerControlled) {
        return std::nullopt;
    }
    const BattleWeaponInstanceState* weapon = weaponByInstanceId(
        *shooter, shot.weaponInstanceId);
    if (weapon == nullptr || weapon->weaponTypeId != "machine_gun" ||
        weapon->deliveryMode != BattleWeaponDeliveryMode::Immediate) {
        return std::nullopt;
    }
    const int spriteIndex = campaignBattleMachineGunFlashSpriteIndex(
        family, weapon->locationId);
    if (spriteIndex < 0) {
        return std::nullopt;
    }
    CampaignBattleRenderMachineGunFlashInstance flash;
    flash.shotSequence = shot.sequence;
    flash.shotTickIndex = shot.tickIndex;
    flash.shooterEntityId = shot.shooterEntityId;
    flash.weaponInstanceId = shot.weaponInstanceId;
    flash.locationId = weapon->locationId;
    flash.spriteIndex = spriteIndex;
    flash.rightSide = !weapon->locationId.empty() &&
        weapon->locationId.front() == 'R';
    flash.originalResourceMappingProven = true;
    flash.originalPlacementProven = false;
    flash.originalOneTickLifetimeProven = false;
    return flash;
}

double interpolateScalar(double from, double to, double alpha) {
    return from + (to - from) * alpha;
}

BattleHitPoint interpolateHitPoint(
    const BattleHitPoint& previous,
    const BattleHitPoint& current,
    double alpha) {
    return {
        interpolateScalar(previous.x, current.x, alpha),
        interpolateScalar(previous.y, current.y, alpha),
        interpolateScalar(previous.z, current.z, alpha),
    };
}

BattleHitPoint normalizedHitPoint(const BattleHitPoint& value) {
    const double length = std::sqrt(
        value.x * value.x + value.y * value.y + value.z * value.z);
    if (length <= std::numeric_limits<double>::epsilon()) {
        return {0.0, 0.0, 1.0};
    }
    return {value.x / length, value.y / length, value.z / length};
}

double interpolateHeadingRadians(double from, double to, double alpha) {
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kTwoPi = kPi * 2.0;
    const double shortestDelta = std::remainder(to - from, kTwoPi);
    return std::remainder(from + shortestDelta * alpha, kTwoPi);
}

Transform interpolateTransform(
    const Transform& previous,
    const Transform& current,
    double alpha) {
    Transform result = current;
    result.x = interpolateScalar(previous.x, current.x, alpha);
    result.y = interpolateScalar(previous.y, current.y, alpha);
    result.z = interpolateScalar(previous.z, current.z, alpha);
    result.headingRadians = interpolateHeadingRadians(
        previous.headingRadians,
        current.headingRadians,
        alpha);
    return result;
}

CampaignCockpitFamily cockpitFamilyForMechPreset(const std::string& presetId) {
    if (presetId == "locust" || presetId == "jenner" || presetId == "wasp") {
        return CampaignCockpitFamily::Light;
    }
    if (presetId == "phoenix_hawk" || presetId == "shadow_hawk" || presetId == "wolverine") {
        return CampaignCockpitFamily::Medium;
    }
    return CampaignCockpitFamily::Heavy;
}

const char* cockpitBackdropName(CampaignCockpitFamily family) {
    switch (family) {
    case CampaignCockpitFamily::Light:
        return "LIGHT.SCR";
    case CampaignCockpitFamily::Medium:
        return "MEDIUM.SCR";
    case CampaignCockpitFamily::Heavy:
        return "HEAVY.SCR";
    }
    return "LIGHT.SCR";
}

const char* cockpitPaletteName(const std::optional<int>& environmentId) {
    if (!environmentId.has_value()) {
        return "TROPIC.PAL";
    }
    switch (*environmentId) {
    case 0:
        return "DESERT.PAL";
    case 2:
        return "ARCTIC.PAL";
    default:
        return "TROPIC.PAL";
    }
}

std::filesystem::path catalogResourcePathAtRoot(
    const std::filesystem::path& sortedOriginalFilesRoot,
    const std::string& mechPresetId) {
    const std::filesystem::path catalogPath = mech3d::catalogDefaultResourcePath(mechPresetId);
    return campaignBattleOriginalResourcePath(
        sortedOriginalFilesRoot,
        std::filesystem::path("TBL") / "viewer8" / catalogPath.filename());
}

double cockpitBobOffsetForScene(const CampaignBattleRenderScenePackage& scene) {
    if (scene.mode != CampaignBattlePresentationMode::Cockpit) {
        return 0.0;
    }
    const auto player = std::find_if(
        scene.combatantVisuals.begin(),
        scene.combatantVisuals.end(),
        [](const CampaignBattleRenderVisualInstance& visual) {
            return visual.playerControlled || visual.team == BattleTeam::Player;
        });
    if (player == scene.combatantVisuals.end()) {
        return 0.0;
    }
    return battleCockpitBobOffsetWorldUnits(
        player->forwardSpeed,
        player->maxForwardSpeed,
        player->maxReverseSpeed,
        static_cast<double>(player->animationElapsedMs),
        player->mechDestroyed);
}

} // namespace

int campaignBattleProjectileRecordIndex(uint16_t originalVisualClass) {
    // BTECH.EXE projectile rows use visual class 0 for AC/5 and class 1 for
    // every LRM/SRM launcher. User-captured DOS frames close those classes to
    // OTHPCK.TBL records 000 and 001 respectively.
    return originalVisualClass <= 1u
        ? static_cast<int>(originalVisualClass)
        : -1;
}

int campaignBattleMachineGunFlashSpriteIndex(
    CampaignCockpitFamily family,
    const std::string& locationId) {
    if (locationId.empty() ||
        (locationId.front() != 'L' && locationId.front() != 'R')) {
        return -1;
    }
    const int familyOffset = static_cast<int>(family);
    if (familyOffset < 0 || familyOffset > 2) {
        return -1;
    }
    // COCKPIT.BMP records 006..008 are the left lower-corner flashes for
    // light/medium/heavy respectively; 009..011 are the mirrored right set.
    return (locationId.front() == 'R' ? 9 : 6) + familyOffset;
}

double campaignBattleProjectileSpinDegrees(
    uint64_t launchTickIndex,
    uint64_t snapshotTickIndex) {
    if (snapshotTickIndex <= launchTickIndex) {
        return 0.0;
    }
    // Clockwise axial rotation is captured in the DOS evidence. Its exact
    // wall-clock rate is not, so this remains presentation-only beta tuning.
    constexpr double kCompatibilityDegreesPerFixedTick = -22.5;
    return static_cast<double>(snapshotTickIndex - launchTickIndex) *
        kCompatibilityDegreesPerFixedTick;
}

uint16_t campaignBattleOriginalMissionDurationDateTicks(uint8_t originalMissionId) {
    if (originalMissionId >= kOriginalMissionDurationDateTicks.size()) {
        return 0;
    }
    return kOriginalMissionDurationDateTicks[originalMissionId];
}

uint32_t campaignBattleOriginalMissionElapsedDateTicks(
    uint16_t jumpCount,
    uint8_t originPlanetTimeFactor,
    uint8_t targetPlanetTimeFactor,
    uint8_t originalMissionId) {
    const uint64_t provenJumpCount = std::max<uint16_t>(jumpCount, 1u);
    const uint64_t elapsed =
        provenJumpCount * 28u +
        static_cast<uint64_t>(originPlanetTimeFactor) * 4u +
        static_cast<uint64_t>(targetPlanetTimeFactor) * 2u +
        campaignBattleOriginalMissionDurationDateTicks(originalMissionId);
    return static_cast<uint32_t>(std::min<uint64_t>(
        elapsed,
        std::numeric_limits<uint32_t>::max()));
}

CampaignPilotExperienceState campaignPilotExperienceAfterSurvivedMission(
    uint8_t skill,
    uint16_t missionCounter,
    bool commander) {
    CampaignPilotExperienceState result;
    result.skill = std::min<uint8_t>(skill, 3u);
    if (commander) {
        result.missionCounter = missionCounter < std::numeric_limits<uint16_t>::max()
            ? static_cast<uint16_t>(missionCounter + 1u)
            : missionCounter;
    } else if (result.skill >= 3u) {
        result.missionCounter = 0;
    } else {
        result.missionCounter = static_cast<uint16_t>(
            std::min<uint32_t>(missionCounter + 1u, std::numeric_limits<uint8_t>::max()));
    }

    constexpr std::array<uint16_t, 3> kPromotionMissionThresholds = {{3, 10, 15}};
    if (result.skill < kPromotionMissionThresholds.size() &&
        result.missionCounter >= kPromotionMissionThresholds[result.skill]) {
        ++result.skill;
        result.missionCounter = 0;
        result.promoted = true;
    }
    return result;
}

double battleCockpitBobSpeedRatio(
    double forwardSpeed,
    double maxForwardSpeed,
    double maxReverseSpeed) {
    const double maxSpeed = forwardSpeed < 0.0 ? maxReverseSpeed : maxForwardSpeed;
    if (maxSpeed <= 0.0) {
        return 0.0;
    }
    return std::clamp(std::abs(forwardSpeed) / maxSpeed, 0.0, 1.0);
}

double battleCockpitBobOffsetWorldUnits(
    double forwardSpeed,
    double maxForwardSpeed,
    double maxReverseSpeed,
    double animationPhaseElapsedMs,
    bool mechDestroyed,
    const BattleCockpitBobTuning& tuning) {
    if (mechDestroyed || tuning.cycleMs <= 0.0 || tuning.maxWorldUnits <= 0.0) {
        return 0.0;
    }
    const double speedRatio = battleCockpitBobSpeedRatio(
        forwardSpeed,
        maxForwardSpeed,
        maxReverseSpeed);
    if (speedRatio <= 0.001) {
        return 0.0;
    }
    constexpr double kPi = 3.14159265358979323846;
    const double phase = 2.0 * kPi * animationPhaseElapsedMs / tuning.cycleMs;
    return std::sin(phase) * tuning.maxWorldUnits * speedRatio;
}

double campaignBattleCatalogCockpitCameraHeight(
    const std::filesystem::path& sortedOriginalFilesRoot,
    const std::string& mechPresetId) {
    if (sortedOriginalFilesRoot.empty()) {
        throw std::runtime_error("campaign battle render asset root missing");
    }
    const std::filesystem::path resourcePath =
        catalogResourcePathAtRoot(sortedOriginalFilesRoot, mechPresetId);
    const std::vector<legacy3d::RuntimeRecord> records =
        legacy3d::loadRuntimeShapeRecords(resourcePath);
    return static_cast<double>(mech3d::catalogCockpitCameraHeight(mechPresetId, records));
}

double campaignBattleCatalogCollisionRadius(
    const std::filesystem::path& sortedOriginalFilesRoot,
    const std::string& mechPresetId) {
    if (sortedOriginalFilesRoot.empty()) {
        throw std::runtime_error("campaign battle render asset root missing");
    }
    const std::filesystem::path resourcePath =
        catalogResourcePathAtRoot(sortedOriginalFilesRoot, mechPresetId);
    const std::vector<legacy3d::RuntimeRecord> records =
        legacy3d::loadRuntimeShapeRecords(resourcePath);
    return static_cast<double>(
        mech3d::catalogHorizontalCollisionRadius(mechPresetId, records));
}

BattleMechHitProfile campaignBattleCatalogHitProfile(
    const std::filesystem::path& sortedOriginalFilesRoot,
    const std::string& mechPresetId) {
    if (sortedOriginalFilesRoot.empty()) {
        throw std::runtime_error("campaign battle render asset root missing");
    }
    const std::filesystem::path resourcePath =
        catalogResourcePathAtRoot(sortedOriginalFilesRoot, mechPresetId);
    const std::vector<legacy3d::RuntimeRecord> records =
        legacy3d::loadRuntimeShapeRecords(resourcePath);
    const mech3d::MechRenderInstance instance = mech3d::buildMechRenderInstance(
        mech3d::makeCatalogMechModelDefinition(mechPresetId),
        records,
        {});
    if (!instance.assembly.bounds.valid) {
        throw std::runtime_error(
            "catalog mech hit profile has no assembly bounds: " + mechPresetId);
    }

    const auto transformPoint = [](
        const mech3d::Mat4f& matrix,
        const BattleHitPoint& point) {
        return BattleHitPoint{
            matrix[0] * point.x + matrix[4] * point.y +
                matrix[8] * point.z + matrix[12],
            matrix[1] * point.x + matrix[5] * point.y +
                matrix[9] * point.z + matrix[13],
            matrix[2] * point.x + matrix[6] * point.y +
                matrix[10] * point.z + matrix[14],
        };
    };
    const auto componentForId = [&instance](int componentId) {
        const auto found = std::find_if(
            instance.assembly.components.begin(),
            instance.assembly.components.end(),
            [componentId](const mech3d::ModelAssemblyComponent& component) {
                return component.componentId == componentId;
            });
        return found == instance.assembly.components.end() ? nullptr : &*found;
    };
    const auto sectionForLabel = [](const std::string& label) {
        if (label.find("left_arm") != std::string::npos) {
            return mech3d::MechArmorSectionId::LeftArm;
        }
        if (label.find("right_arm") != std::string::npos) {
            return mech3d::MechArmorSectionId::RightArm;
        }
        if (label.find("left_leg") != std::string::npos) {
            return mech3d::MechArmorSectionId::LeftLeg;
        }
        if (label.find("right_leg") != std::string::npos) {
            return mech3d::MechArmorSectionId::RightLeg;
        }
        if (label.find("cockpit") != std::string::npos) {
            return mech3d::MechArmorSectionId::Head;
        }
        return mech3d::MechArmorSectionId::CenterTorso;
    };

    BattleMechHitProfile profile;
    profile.valid = true;
    profile.mechPresetId = mechPresetId;
    profile.aimOriginHeight = static_cast<double>(
        mech3d::catalogCockpitCameraHeight(mechPresetId, records));
    const CampaignCockpitFamily family = cockpitFamilyForMechPreset(mechPresetId);
    profile.cockpitViewportHeight =
        family == CampaignCockpitFamily::Light ? 92 : 103;
    profile.neutralCrosshairY = 51.0;
    profile.provenance =
        "original_component_mesh_stationary_bind_pose_triangles_"
        "FUN_1000_47f7_identity_"
        "replacement_projection_provisional_torso_partition";

    const mech3d::Bounds& bounds = instance.assembly.bounds;
    const BattleHitPoint centerGroundOffset{
        -static_cast<double>(bounds.center.x),
        -static_cast<double>(bounds.min.y),
        -static_cast<double>(bounds.center.z),
    };
    const auto vertexAt = [&instance](uint32_t index) {
        const size_t offset = static_cast<size_t>(index) * 6u;
        if (offset + 2u >= instance.batch.vertices.size()) {
            throw std::runtime_error("catalog mech hit triangle vertex is out of range");
        }
        return BattleHitPoint{
            instance.batch.vertices[offset],
            instance.batch.vertices[offset + 1u],
            instance.batch.vertices[offset + 2u],
        };
    };
    for (const mech3d::ComponentRenderRange& range : instance.ranges) {
        const mech3d::ModelAssemblyComponent* component =
            componentForId(range.componentId);
        if (component == nullptr) {
            throw std::runtime_error("catalog mech hit component is missing");
        }
        const bool torsoComposite =
            component->debugLabel.find("torso") != std::string::npos;
        const mech3d::MechArmorSectionId section =
            sectionForLabel(component->debugLabel);
        const size_t begin = range.triangleIndexOffset;
        const size_t end = std::min(
            instance.batch.triangleIndices.size(),
            static_cast<size_t>(range.triangleIndexOffset +
                                range.triangleIndexCount));
        for (size_t index = begin; index + 2u < end; index += 3u) {
            BattleMechHitTriangle triangle;
            triangle.componentId = range.componentId;
            triangle.directArmorSection = section;
            triangle.torsoComposite = torsoComposite;
            BattleHitPoint* points[] = {&triangle.a, &triangle.b, &triangle.c};
            for (size_t corner = 0; corner < 3u; ++corner) {
                const BattleHitPoint local = vertexAt(
                    instance.batch.triangleIndices[index + corner]);
                *points[corner] = transformPoint(component->worldMatrix, local);
                points[corner]->x += centerGroundOffset.x;
                points[corner]->y += centerGroundOffset.y;
                points[corner]->z += centerGroundOffset.z;
            }
            profile.triangles.push_back(triangle);
        }
    }
    if (profile.triangles.empty()) {
        throw std::runtime_error(
            "catalog mech hit profile has no triangles: " + mechPresetId);
    }

    uint64_t hash = 1469598103934665603ull;
    const auto append = [&hash](uint64_t value) {
        for (int byte = 0; byte < 8; ++byte) {
            hash ^= (value >> (byte * 8)) & 0xffu;
            hash *= 1099511628211ull;
        }
    };
    append(static_cast<uint64_t>(profile.triangles.size()));
    for (const BattleMechHitTriangle& triangle : profile.triangles) {
        append(static_cast<uint64_t>(triangle.componentId));
        append(static_cast<uint64_t>(triangle.directArmorSection));
        append(triangle.torsoComposite ? 1u : 0u);
        for (const BattleHitPoint* point : {&triangle.a, &triangle.b, &triangle.c}) {
            append(static_cast<uint64_t>(static_cast<int64_t>(
                std::llround(point->x * 1000.0))));
            append(static_cast<uint64_t>(static_cast<int64_t>(
                std::llround(point->y * 1000.0))));
            append(static_cast<uint64_t>(static_cast<int64_t>(
                std::llround(point->z * 1000.0))));
        }
    }
    profile.geometryFingerprint = hash;
    return profile;
}

StandaloneBattleRunResult runStandaloneBattle(
    const BattleStartParams& params,
    const BattleReplay& replay,
    StandaloneBattleRunOptions options) {
    BattleWorld world = BattleWorld::create(params);
    for (BattleInputCommand command : replay.commands) {
        if (!isValid(command.entityId)) {
            command.entityId = world.playerEntityId();
        }
        world.enqueueInput(command);
    }

    StandaloneBattleRunResult result;
    result.startSnapshot = world.missionStartSnapshot();
    for (uint64_t tick = 0; tick < options.maxTicks; ++tick) {
        if (options.stopOnTerminalResult && world.battleResult().has_value()) {
            break;
        }
        world.tick();
        if (options.stopOnTerminalResult && world.battleResult().has_value()) {
            break;
        }
    }

    result.finalSnapshot = world.snapshot();
    result.result = world.battleResult();
    result.terminal = result.result.has_value();
    result.ticksExecuted = result.finalSnapshot.tickIndex - result.startSnapshot.tickIndex;
    return result;
}

const char* campaignBattleOutcomeName(CampaignBattleOutcome outcome) {
    switch (outcome) {
    case CampaignBattleOutcome::Victory:
        return "victory";
    case CampaignBattleOutcome::Defeat:
        return "defeat";
    case CampaignBattleOutcome::Withdraw:
        return "withdraw";
    case CampaignBattleOutcome::Unsupported:
        return "unsupported";
    }
    return "unsupported";
}

bool campaignBattleOutcomeIsMissionFailure(CampaignBattleOutcome outcome) {
    return outcome == CampaignBattleOutcome::Defeat ||
           outcome == CampaignBattleOutcome::Withdraw;
}

CampaignBattleDebriefPresentation campaignBattleDebriefPresentationForOutcome(
    const CampaignBattleOutcomePackage& outcome) {
    if (!outcome.valid || !outcome.terminal ||
        outcome.launchSource != CampaignBattleLaunchSource::AcceptedContract) {
        return CampaignBattleDebriefPresentation::None;
    }
    if (outcome.outcome == CampaignBattleOutcome::Victory) {
        return CampaignBattleDebriefPresentation::Victory;
    }
    if (campaignBattleOutcomeIsMissionFailure(outcome.outcome)) {
        return CampaignBattleDebriefPresentation::Defeat;
    }
    return CampaignBattleDebriefPresentation::None;
}

void beginCampaignContractVisit(
    CampaignContractVisitLedger& ledger,
    int visitSerial) {
    if (ledger.visitSerial == visitSerial) {
        return;
    }
    ledger.visitSerial = visitSerial;
    ledger.completedOfferSlots.clear();
}

bool campaignContractOfferAvailableForVisit(
    const CampaignContractVisitLedger& ledger,
    int visitSerial,
    size_t offerSlot) {
    if (ledger.visitSerial != visitSerial) {
        return true;
    }
    return std::find(
               ledger.completedOfferSlots.begin(),
               ledger.completedOfferSlots.end(),
               offerSlot) == ledger.completedOfferSlots.end();
}

bool completeCampaignContractOfferForVisit(
    CampaignContractVisitLedger& ledger,
    int visitSerial,
    size_t offerSlot) {
    beginCampaignContractVisit(ledger, visitSerial);
    if (!campaignContractOfferAvailableForVisit(ledger, visitSerial, offerSlot)) {
        return false;
    }
    ledger.completedOfferSlots.push_back(offerSlot);
    return true;
}

bool campaignBattleDiagnosticTickLimitReached(
    std::optional<uint64_t> tickLimit,
    uint64_t ticksExecuted) {
    return tickLimit.has_value() && ticksExecuted >= *tickLimit;
}

const std::array<size_t, 20>& campaignBattleTerrainScenarioIndices() {
    static constexpr std::array<size_t, 20> kIndices{{
        0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u,
        10u, 11u, 12u, 13u, 14u, 15u, 16u, 17u, 18u, 19u,
    }};
    return kIndices;
}

size_t campaignBattleTerrainScenarioIndexFromTargetPlanetTableOrder(
    uint16_t oneBasedTableOrder) {
    if (oneBasedTableOrder == 0u) {
        throw std::invalid_argument("campaign target planet table order must be one-based");
    }
    // MW_MAIN stores zero-based target planet index in DS:0956, writes
    // DS:0956 + 1 to battle context[6], and BTECH reduces context[6] modulo
    // 20 before indexing SNARIO.DAT.
    return static_cast<size_t>(oneBasedTableOrder) % 20u;
}

const char* campaignBattleLaunchSourceName(CampaignBattleLaunchSource source) {
    switch (source) {
    case CampaignBattleLaunchSource::Unknown:
        return "unknown";
    case CampaignBattleLaunchSource::AcceptedContract:
        return "accepted_contract";
    case CampaignBattleLaunchSource::DarkWingFinal:
        return "dark_wing_final";
    }
    return "unknown";
}

const char* campaignBattlePostResultDestinationName(
    CampaignBattlePostResultDestination destination) {
    switch (destination) {
    case CampaignBattlePostResultDestination::Unsupported:
        return "unsupported";
    case CampaignBattlePostResultDestination::CampaignMainMenu:
        return "campaign_main_menu";
    }
    return "unsupported";
}

const char* campaignBattlePresentationModeName(CampaignBattlePresentationMode mode) {
    switch (mode) {
    case CampaignBattlePresentationMode::MissionStatus:
        return "mission_status";
    case CampaignBattlePresentationMode::CockpitCommandMap:
        return "cockpit_command_map";
    case CampaignBattlePresentationMode::TacticalMap:
        return "tactical_map";
    case CampaignBattlePresentationMode::Cockpit:
        return "cockpit";
    case CampaignBattlePresentationMode::External:
        return "external";
    }
    return "tactical_map";
}

bool campaignBattlePresentationPausesSimulation(CampaignBattlePresentationMode mode) {
    return mode == CampaignBattlePresentationMode::MissionStatus;
}

const char* campaignCockpitFamilyName(CampaignCockpitFamily family) {
    switch (family) {
    case CampaignCockpitFamily::Light:
        return "light";
    case CampaignCockpitFamily::Medium:
        return "medium";
    case CampaignCockpitFamily::Heavy:
        return "heavy";
    }
    return "light";
}

CampaignBattleOutcomePackage campaignBattleOutcomePackageFromSnapshots(
    const BattleSnapshot& startSnapshot,
    const BattleSnapshot& finalSnapshot,
    const std::optional<BattleResult>& result,
    uint64_t ticksExecuted) {
    CampaignBattleOutcomePackage package;
    package.valid = true;
    package.launchSource = campaignLaunchSourceFromSnapshot(startSnapshot);
    package.terminal = result.has_value();
    package.ticksExecuted = ticksExecuted;
    package.startSnapshotFingerprint = battleSnapshotFingerprint(startSnapshot);
    package.finalSnapshotFingerprint = battleSnapshotFingerprint(finalSnapshot);
    package.startCombatantCount = startSnapshot.combatants.size();
    package.finalCombatantCount = finalSnapshot.combatants.size();
    package.objectiveValid = finalSnapshot.objective.valid;
    package.contractMetadataValid = finalSnapshot.contract.valid;

    if (!result.has_value()) {
        package.outcome = CampaignBattleOutcome::Unsupported;
        package.reason = "battle_runtime_no_terminal_result";
        return package;
    }

    package.outcome = campaignOutcomeFromRuntimeState(result->state);
    package.reason = result->reason;
    package.rawResultCode = result->rawResultCode;
    package.terminalTickIndex = result->terminalTickIndex;
    package.terminalElapsedMs = result->terminalElapsedMs;
    if (package.outcome == CampaignBattleOutcome::Unsupported && package.reason.empty()) {
        package.reason = "battle_runtime_unsupported_result";
    }
    return package;
}

CampaignBattleConsequencePlan campaignBattleConsequencePlanFromOutcome(
    const CampaignBattleOutcomePackage& outcome,
    const BattleContractMetadata& contract) {
    return campaignBattleConsequencePlanFromOutcome(outcome, contract, {});
}

CampaignBattleConsequencePlan campaignBattleConsequencePlanFromOutcome(
    const CampaignBattleOutcomePackage& outcome,
    const BattleContractMetadata& contract,
    const CampaignBattleConsequenceContext& context) {
    CampaignBattleConsequencePlan plan;
    plan.launchSource = outcome.launchSource;
    plan.outcome = outcome.outcome;
    plan.outcomeFingerprint = outcome.finalSnapshotFingerprint;

    if (!outcome.valid) {
        plan.reason = "campaign_battle_outcome_missing";
        return plan;
    }

    plan.valid = true;
    if (outcome.launchSource != CampaignBattleLaunchSource::AcceptedContract) {
        plan.reason = outcome.launchSource == CampaignBattleLaunchSource::DarkWingFinal
            ? "dark_wing_final_consequences_deferred"
            : "campaign_battle_source_has_no_consequence_plan";
    } else if (!contract.valid || !outcome.contractMetadataValid) {
        plan.reason = "accepted_contract_metadata_missing";
    } else if (!outcome.terminal || outcome.outcome == CampaignBattleOutcome::Unsupported) {
        plan.reason = "accepted_contract_outcome_not_terminal";
    } else if (outcome.outcome == CampaignBattleOutcome::Victory ||
               campaignBattleOutcomeIsMissionFailure(outcome.outcome)) {
        plan.commitEligible = true;
        if (context.salvageEstimateAvailable) {
            plan.salvageDeferred = false;
            plan.salvageBetaValidationRequired = context.salvageBetaValidationRequired;
            if (outcome.outcome == CampaignBattleOutcome::Victory) {
                plan.salvage = context.salvage;
            }
        }
        if (context.campaignTimeProven) {
            plan.campaignDateTicks = context.campaignDateTicks;
            plan.campaignTimeDeferred = false;
        }
        if (context.pilotExperienceProven) {
            plan.pilotExperienceAward = context.pilotExperienceAward;
            plan.pilotExperienceDeferred = false;
        }
        const size_t employerIndex = std::min<size_t>(contract.employerHouseId, 4u);
        const size_t targetIndex = std::min<size_t>(contract.targetHouseId, 4u);
        if (contract.hasHostileTargetHouse) {
            plan.houseNegativeDelta[targetIndex] += 2;
        }
        if (outcome.outcome == CampaignBattleOutcome::Victory) {
            plan.payment = static_cast<uint64_t>(std::max(0, contract.priceK)) * 1000ull;
            const int64_t estimatedOpposition =
                static_cast<int64_t>(std::max(0, contract.estimatedHeavyMechs)) +
                static_cast<int64_t>(std::max(0, contract.estimatedMediumMechs)) +
                static_cast<int64_t>(std::max(0, contract.estimatedLightMechs));
            plan.reputationDelta = static_cast<int>(std::min<int64_t>(
                estimatedOpposition,
                std::numeric_limits<int>::max()));
            plan.housePositiveDelta[employerIndex] += 2;
        } else {
            plan.houseNegativeDelta[employerIndex] += 1;
        }
        plan.reason = "accepted_contract_proven_consequences_ready";
    } else {
        plan.reason = "accepted_contract_outcome_not_supported";
    }

    plan.planFingerprint = campaignBattleConsequencePlanFingerprint(plan);
    return plan;
}

CampaignBattleConsequenceApplyGuard campaignBattleConsequencePlanApplyGuard(
    const CampaignBattleConsequencePlan& plan,
    uint64_t expectedStateFingerprint,
    uint64_t currentStateFingerprint,
    uint64_t lastCommittedPlanFingerprint) {
    CampaignBattleConsequenceApplyGuard guard;
    if (!plan.valid) {
        guard.reason = "campaign_consequence_plan_missing";
        return guard;
    }
    guard.valid = true;
    if (!plan.commitEligible) {
        guard.reason = "campaign_consequence_plan_not_commit_eligible";
        return guard;
    }
    guard.preconditionMatched =
        expectedStateFingerprint != 0 &&
        expectedStateFingerprint == currentStateFingerprint;
    if (!guard.preconditionMatched) {
        guard.reason = "campaign_consequence_precondition_mismatch";
        return guard;
    }
    guard.duplicateRejected =
        plan.planFingerprint != 0 &&
        plan.planFingerprint == lastCommittedPlanFingerprint;
    if (guard.duplicateRejected) {
        guard.reason = "campaign_consequence_duplicate_plan";
        return guard;
    }
    guard.allowed = true;
    guard.reason = "campaign_consequence_apply_allowed";
    return guard;
}

CampaignBattlePersistenceReport campaignBattlePersistenceReportFromSnapshot(
    const BattleSnapshot& finalSnapshot,
    const std::vector<CampaignBattlePersistentRosterBinding>& bindings,
    size_t missionParticipantCount) {
    CampaignBattlePersistenceReport report;
    report.finalSnapshotFingerprint = battleSnapshotFingerprint(finalSnapshot);
    report.requestedBindingCount = bindings.size();
    report.missionParticipantCount = missionParticipantCount;

    std::unordered_map<std::string, size_t> bindingCounts;
    for (const CampaignBattlePersistentRosterBinding& binding : bindings) {
        const size_t count = ++bindingCounts[binding.sourceSlot];
        if (binding.sourceSlot.empty() || count > 1u) {
            ++report.duplicateBindingCount;
        }
    }

    std::unordered_set<std::string> boundSourceSlots;
    for (const CampaignBattlePersistentRosterBinding& binding : bindings) {
        if (binding.sourceSlot.empty() ||
            binding.ownedMechIndex < 0 ||
            binding.crewSlot < 0 ||
            bindingCounts[binding.sourceSlot] != 1u) {
            ++report.unmappedBindingCount;
            continue;
        }
        boundSourceSlots.insert(binding.sourceSlot);
        const CombatantSnapshot* matchedCombatant = nullptr;
        size_t matchCount = 0;
        for (const CombatantSnapshot& combatant : finalSnapshot.combatants) {
            if ((combatant.roster.team == BattleTeam::Player || combatant.playerControlled) &&
                combatant.roster.sourceSlot == binding.sourceSlot) {
                matchedCombatant = &combatant;
                ++matchCount;
            }
        }
        if (matchCount != 1u || matchedCombatant == nullptr) {
            if (matchCount > 1u) {
                report.duplicateCombatantCount += matchCount - 1u;
            }
            ++report.unmappedBindingCount;
            continue;
        }

        CampaignBattlePersistentCombatantReport combatantReport;
        combatantReport.valid = true;
        combatantReport.sourceSlot = binding.sourceSlot;
        combatantReport.ownedMechIndex = binding.ownedMechIndex;
        combatantReport.crewSlot = binding.crewSlot;
        combatantReport.entityId = matchedCombatant->id;
        combatantReport.mechPresetId = matchedCombatant->mechPresetId;
        combatantReport.missionStatus = matchedCombatant->missionStatus;
        combatantReport.mechDestroyed = matchedCombatant->mechDestroyed;
        combatantReport.mechSystems = matchedCombatant->mechSystems;
        combatantReport.persistentMechState = matchedCombatant->persistentMechState;
        combatantReport.destroyedComponentIds = matchedCombatant->destroyedComponentIds;
        combatantReport.disabledComponentIds = matchedCombatant->disabledComponentIds;
        report.combatants.push_back(std::move(combatantReport));
        ++report.mappedBindingCount;
    }

    for (const CombatantSnapshot& combatant : finalSnapshot.combatants) {
        if ((combatant.roster.team == BattleTeam::Player || combatant.playerControlled) &&
            boundSourceSlots.find(combatant.roster.sourceSlot) == boundSourceSlots.end()) {
            ++report.unboundPlayerCombatantCount;
        }
    }

    report.unlaunchedMissionParticipantCount =
        missionParticipantCount > report.mappedBindingCount
            ? missionParticipantCount - report.mappedBindingCount
            : 0u;
    report.valid =
        report.finalSnapshotFingerprint != 0 &&
        report.duplicateBindingCount == 0u &&
        report.duplicateCombatantCount == 0u;
    report.allBindingsMapped =
        report.valid &&
        report.mappedBindingCount == report.requestedBindingCount &&
        report.unmappedBindingCount == 0u &&
        report.unboundPlayerCombatantCount == 0u;
    report.allMissionParticipantsLaunched =
        report.allBindingsMapped && report.unlaunchedMissionParticipantCount == 0u;

    if (!report.valid) {
        report.reason = "campaign_persistence_binding_ambiguous";
    } else if (!report.allBindingsMapped) {
        report.reason = "campaign_persistence_binding_incomplete";
    } else if (!report.allMissionParticipantsLaunched) {
        report.reason = "campaign_mission_participants_not_launched";
    } else {
        report.reason = "campaign_persistence_binding_ready_translation_deferred";
    }

    uint64_t hash = 1469598103934665603ull;
    const auto append = [&hash](uint64_t value) {
        for (int byte = 0; byte < 8; ++byte) {
            hash ^= (value >> (byte * 8)) & 0xffu;
            hash *= 1099511628211ull;
        }
    };
    const auto appendString = [&append](const std::string& value) {
        append(value.size());
        for (unsigned char ch : value) {
            append(ch);
        }
    };
    append(report.finalSnapshotFingerprint);
    append(report.requestedBindingCount);
    append(report.mappedBindingCount);
    append(report.unmappedBindingCount);
    append(report.duplicateBindingCount);
    append(report.duplicateCombatantCount);
    append(report.unboundPlayerCombatantCount);
    append(report.missionParticipantCount);
    append(report.unlaunchedMissionParticipantCount);
    for (const CampaignBattlePersistentCombatantReport& combatant : report.combatants) {
        appendString(combatant.sourceSlot);
        append(static_cast<uint64_t>(static_cast<int64_t>(combatant.ownedMechIndex)));
        append(static_cast<uint64_t>(static_cast<int64_t>(combatant.crewSlot)));
        append(combatant.entityId.value);
        append(static_cast<uint64_t>(combatant.missionStatus));
        append(combatant.mechDestroyed ? 1u : 0u);
        append(combatant.mechSystems.valid ? 1u : 0u);
        append(battlePersistentMechStateFingerprint(combatant.persistentMechState));
        for (const BattleMechSystemSnapshot& system : combatant.mechSystems.systems) {
            appendString(system.systemId);
            append(static_cast<uint64_t>(system.status));
            append(static_cast<uint64_t>(static_cast<int64_t>(system.damage)));
            append(static_cast<uint64_t>(static_cast<int64_t>(system.maxDamage)));
        }
    }
    report.reportFingerprint = hash;
    return report;
}

CampaignBattleEntryMechStateGuard campaignBattleEntryMechStateGuardFromSnapshots(
    const BattleSnapshot& startSnapshot,
    const BattleSnapshot& finalSnapshot) {
    CampaignBattleEntryMechStateGuard guard;
    guard.startSnapshotFingerprint = battleSnapshotFingerprint(startSnapshot);
    guard.finalSnapshotFingerprint = battleSnapshotFingerprint(finalSnapshot);
    guard.playerStatesPreserved = true;
    guard.opposingStatesPristine = true;
    guard.allCombatantStatesPreserved = true;

    std::unordered_map<std::string, size_t> finalKeyCounts;
    for (const CombatantSnapshot& combatant : finalSnapshot.combatants) {
        const std::string key =
            std::to_string(static_cast<int>(combatant.roster.team)) + ":" +
            combatant.roster.sourceSlot;
        ++finalKeyCounts[key];
    }

    for (const CombatantSnapshot& startCombatant : startSnapshot.combatants) {
        if (startCombatant.roster.team == BattleTeam::Player) {
            ++guard.playerCombatantCount;
        } else if (startCombatant.roster.team == BattleTeam::Opposing) {
            ++guard.opposingCombatantCount;
        }
        if (!startCombatant.persistentMechState.valid) {
            ++guard.invalidEntryStateCount;
            guard.allCombatantStatesPreserved = false;
            if (startCombatant.roster.team == BattleTeam::Player) {
                guard.playerStatesPreserved = false;
            }
            if (startCombatant.roster.team == BattleTeam::Opposing) {
                guard.opposingStatesPristine = false;
            }
            continue;
        }
        if (startCombatant.roster.team == BattleTeam::Opposing &&
            !battlePersistentMechStateIsPristine(
                startCombatant.persistentMechState)) {
            ++guard.nonPristineOpposingCount;
            guard.opposingStatesPristine = false;
        }

        const std::string key =
            std::to_string(static_cast<int>(startCombatant.roster.team)) + ":" +
            startCombatant.roster.sourceSlot;
        const CombatantSnapshot* finalCombatant = nullptr;
        if (finalKeyCounts[key] == 1u) {
            for (const CombatantSnapshot& candidate : finalSnapshot.combatants) {
                if (candidate.roster.team == startCombatant.roster.team &&
                    candidate.roster.sourceSlot == startCombatant.roster.sourceSlot) {
                    finalCombatant = &candidate;
                    break;
                }
            }
        }
        if (finalCombatant == nullptr) {
            ++guard.missingFinalCombatantCount;
            guard.allCombatantStatesPreserved = false;
            if (startCombatant.roster.team == BattleTeam::Player) {
                guard.playerStatesPreserved = false;
            }
            continue;
        }
        if (battlePersistentMechStateFingerprint(
                startCombatant.persistentMechState) !=
            battlePersistentMechStateFingerprint(
                finalCombatant->persistentMechState)) {
            guard.allCombatantStatesPreserved = false;
            if (startCombatant.roster.team == BattleTeam::Player) {
                guard.playerStatesPreserved = false;
            }
            continue;
        }
        ++guard.preservedCombatantCount;
    }

    guard.valid =
        guard.startSnapshotFingerprint != 0u &&
        guard.finalSnapshotFingerprint != 0u &&
        guard.playerCombatantCount > 0u &&
        guard.opposingCombatantCount > 0u &&
        guard.invalidEntryStateCount == 0u &&
        guard.missingFinalCombatantCount == 0u;
    if (!guard.valid) {
        guard.reason = "campaign_entry_mech_state_guard_invalid";
    } else if (!guard.opposingStatesPristine) {
        guard.reason = "campaign_opposition_entry_state_not_pristine";
    } else if (!guard.playerStatesPreserved ||
               !guard.allCombatantStatesPreserved) {
        guard.reason = "campaign_entry_mech_state_changed_in_runtime";
    } else {
        guard.reason = "campaign_entry_mech_states_preserved_opposition_pristine";
    }

    uint64_t hash = 1469598103934665603ull;
    const auto append = [&hash](uint64_t value) {
        for (int byte = 0; byte < 8; ++byte) {
            hash ^= (value >> (byte * 8)) & 0xffu;
            hash *= 1099511628211ull;
        }
    };
    append(guard.startSnapshotFingerprint);
    append(guard.finalSnapshotFingerprint);
    append(guard.valid ? 1u : 0u);
    append(guard.playerStatesPreserved ? 1u : 0u);
    append(guard.opposingStatesPristine ? 1u : 0u);
    append(guard.allCombatantStatesPreserved ? 1u : 0u);
    append(guard.playerCombatantCount);
    append(guard.opposingCombatantCount);
    append(guard.preservedCombatantCount);
    append(guard.missingFinalCombatantCount);
    append(guard.invalidEntryStateCount);
    append(guard.nonPristineOpposingCount);
    guard.guardFingerprint = hash;
    return guard;
}

CampaignBattlePersistentDamageTranslationPlan
campaignBattlePersistentDamageTranslationPlanFromReport(
    const CampaignBattlePersistenceReport& report) {
    CampaignBattlePersistentDamageTranslationPlan plan;
    plan.persistenceReportFingerprint = report.reportFingerprint;
    plan.allBindingsMapped = report.allBindingsMapped;
    plan.allMissionParticipantsLaunched = report.allMissionParticipantsLaunched;
    plan.unlaunchedMissionParticipantCount = report.unlaunchedMissionParticipantCount;

    const auto systemForRole = [](const CampaignBattlePersistentCombatantReport& combatant,
                                  BattleMechSystemRole role,
                                  bool& duplicate) {
        const BattleMechSystemSnapshot* matched = nullptr;
        for (const BattleMechSystemSnapshot& system : combatant.mechSystems.systems) {
            if (system.role != role) {
                continue;
            }
            if (matched != nullptr) {
                duplicate = true;
            } else {
                matched = &system;
            }
        }
        return matched;
    };
    const auto addAudit = [](CampaignBattlePersistentDamageCombatantPlan& combatantPlan,
                             CampaignBattlePersistentDamageField field,
                             size_t persistentFieldCount,
                             BattleMechSystemRole sourceRole,
                             const BattleMechSystemSnapshot* source,
                             std::string reason) {
        CampaignBattlePersistentDamageFieldAudit audit;
        audit.field = field;
        audit.persistentFieldCount = persistentFieldCount;
        audit.sourceRole = sourceRole;
        audit.sourceObserved = source != nullptr;
        if (source != nullptr) {
            audit.observedStatus = source->status;
        }
        audit.exactMappingProven = false;
        audit.reason = std::move(reason);
        combatantPlan.persistentFieldCount += persistentFieldCount;
        if (audit.sourceObserved) {
            combatantPlan.ambiguousPersistentFieldCount += persistentFieldCount;
        } else {
            combatantPlan.missingSourcePersistentFieldCount += persistentFieldCount;
        }
        combatantPlan.fields.push_back(std::move(audit));
    };

    for (const CampaignBattlePersistentCombatantReport& combatant : report.combatants) {
        CampaignBattlePersistentDamageCombatantPlan combatantPlan;
        combatantPlan.sourceSlot = combatant.sourceSlot;
        combatantPlan.ownedMechIndex = combatant.ownedMechIndex;
        combatantPlan.crewSlot = combatant.crewSlot;
        combatantPlan.entityId = combatant.entityId;
        combatantPlan.missionStatus = combatant.missionStatus;
        combatantPlan.mechDestroyed = combatant.mechDestroyed;

        bool duplicateRole = false;
        const BattleMechSystemSnapshot* core =
            systemForRole(combatant, BattleMechSystemRole::Core, duplicateRole);
        const BattleMechSystemSnapshot* cockpit =
            systemForRole(combatant, BattleMechSystemRole::Cockpit, duplicateRole);
        const BattleMechSystemSnapshot* mobility =
            systemForRole(combatant, BattleMechSystemRole::Mobility, duplicateRole);
        const BattleMechSystemSnapshot* weapons =
            systemForRole(combatant, BattleMechSystemRole::Weapons, duplicateRole);
        const BattleMechSystemSnapshot* jumpJets =
            systemForRole(combatant, BattleMechSystemRole::JumpJets, duplicateRole);

        addAudit(
            combatantPlan,
            CampaignBattlePersistentDamageField::Engine,
            1u,
            BattleMechSystemRole::Core,
            core,
            "battle_core_aggregates_engine_gyros_and_structure");
        addAudit(
            combatantPlan,
            CampaignBattlePersistentDamageField::Gyros,
            1u,
            BattleMechSystemRole::Core,
            core,
            "battle_core_aggregates_engine_gyros_and_structure");
        addAudit(
            combatantPlan,
            CampaignBattlePersistentDamageField::Sensors,
            1u,
            BattleMechSystemRole::Cockpit,
            cockpit,
            "battle_cockpit_aggregates_sensors_life_support_and_pilot_space");
        addAudit(
            combatantPlan,
            CampaignBattlePersistentDamageField::LifeSupport,
            1u,
            BattleMechSystemRole::Cockpit,
            cockpit,
            "battle_cockpit_aggregates_sensors_life_support_and_pilot_space");
        addAudit(
            combatantPlan,
            CampaignBattlePersistentDamageField::HeatSinkCount,
            1u,
            BattleMechSystemRole::Unknown,
            nullptr,
            "battle_snapshot_has_no_heat_sink_state");
        addAudit(
            combatantPlan,
            CampaignBattlePersistentDamageField::LeftArmActuator,
            1u,
            BattleMechSystemRole::Weapons,
            weapons,
            "battle_weapons_aggregates_both_arms_and_weapon_mounts");
        addAudit(
            combatantPlan,
            CampaignBattlePersistentDamageField::RightArmActuator,
            1u,
            BattleMechSystemRole::Weapons,
            weapons,
            "battle_weapons_aggregates_both_arms_and_weapon_mounts");
        addAudit(
            combatantPlan,
            CampaignBattlePersistentDamageField::LeftLegActuator,
            1u,
            BattleMechSystemRole::Mobility,
            mobility,
            "battle_mobility_aggregates_both_legs_and_movement_state");
        addAudit(
            combatantPlan,
            CampaignBattlePersistentDamageField::RightLegActuator,
            1u,
            BattleMechSystemRole::Mobility,
            mobility,
            "battle_mobility_aggregates_both_legs_and_movement_state");
        addAudit(
            combatantPlan,
            CampaignBattlePersistentDamageField::JumpJetCount,
            1u,
            BattleMechSystemRole::JumpJets,
            jumpJets,
            "battle_jump_jet_damage_has_no_proven_missing_count_scale");
        addAudit(
            combatantPlan,
            CampaignBattlePersistentDamageField::WeaponConditions,
            10u,
            BattleMechSystemRole::Weapons,
            weapons,
            "battle_weapons_has_no_per_weapon_slot_damage");
        addAudit(
            combatantPlan,
            CampaignBattlePersistentDamageField::ArmorSections,
            9u,
            BattleMechSystemRole::Unknown,
            nullptr,
            "battle_snapshot_has_no_nine_section_armor_state");

        combatantPlan.valid =
            combatant.valid && combatant.mechSystems.valid && !duplicateRole;
        combatantPlan.mutationEligible = false;
        combatantPlan.noMutationGuaranteed = true;
        combatantPlan.reason = duplicateRole
            ? "campaign_damage_translation_duplicate_battle_system_role"
            : (combatantPlan.valid
                   ? "campaign_damage_translation_audited_no_exact_mapping"
                   : "campaign_damage_translation_invalid_combatant_report");

        plan.persistentFieldCount += combatantPlan.persistentFieldCount;
        plan.exactPersistentFieldCount += combatantPlan.exactPersistentFieldCount;
        plan.ambiguousPersistentFieldCount += combatantPlan.ambiguousPersistentFieldCount;
        plan.missingSourcePersistentFieldCount +=
            combatantPlan.missingSourcePersistentFieldCount;
        plan.combatants.push_back(std::move(combatantPlan));
    }

    plan.combatantCount = plan.combatants.size();
    plan.valid =
        report.valid &&
        report.allBindingsMapped &&
        report.reportFingerprint != 0u &&
        std::all_of(
            plan.combatants.begin(),
            plan.combatants.end(),
            [](const CampaignBattlePersistentDamageCombatantPlan& combatant) {
                return combatant.valid;
            });
    plan.mutationEligible = false;
    plan.noMutationGuaranteed = true;
    if (!report.valid || report.reportFingerprint == 0u) {
        plan.reason = "campaign_damage_translation_invalid_persistence_report";
    } else if (!report.allBindingsMapped) {
        plan.reason = "campaign_damage_translation_incomplete_persistence_binding";
    } else if (!plan.valid) {
        plan.reason = "campaign_damage_translation_ambiguous_battle_system_report";
    } else if (!report.allMissionParticipantsLaunched) {
        plan.reason = "campaign_damage_translation_audited_unlaunched_participants_deferred";
    } else {
        plan.reason = "campaign_damage_translation_audited_no_exact_mapping";
    }

    uint64_t hash = 1469598103934665603ull;
    const auto append = [&hash](uint64_t value) {
        for (int byte = 0; byte < 8; ++byte) {
            hash ^= (value >> (byte * 8)) & 0xffu;
            hash *= 1099511628211ull;
        }
    };
    const auto appendString = [&append](const std::string& value) {
        append(value.size());
        for (unsigned char ch : value) {
            append(ch);
        }
    };
    append(plan.persistenceReportFingerprint);
    append(plan.valid ? 1u : 0u);
    append(plan.allBindingsMapped ? 1u : 0u);
    append(plan.allMissionParticipantsLaunched ? 1u : 0u);
    append(plan.persistentFieldCount);
    append(plan.exactPersistentFieldCount);
    append(plan.ambiguousPersistentFieldCount);
    append(plan.missingSourcePersistentFieldCount);
    append(plan.unlaunchedMissionParticipantCount);
    appendString(plan.reason);
    for (const CampaignBattlePersistentDamageCombatantPlan& combatant : plan.combatants) {
        appendString(combatant.sourceSlot);
        append(static_cast<uint64_t>(static_cast<int64_t>(combatant.ownedMechIndex)));
        append(static_cast<uint64_t>(static_cast<int64_t>(combatant.crewSlot)));
        append(combatant.entityId.value);
        append(static_cast<uint64_t>(combatant.missionStatus));
        append(combatant.mechDestroyed ? 1u : 0u);
        for (const CampaignBattlePersistentDamageFieldAudit& field : combatant.fields) {
            append(static_cast<uint64_t>(field.field));
            append(field.persistentFieldCount);
            append(static_cast<uint64_t>(field.sourceRole));
            append(static_cast<uint64_t>(field.observedStatus));
            append(field.sourceObserved ? 1u : 0u);
            append(field.exactMappingProven ? 1u : 0u);
            appendString(field.reason);
        }
    }
    plan.planFingerprint = hash;
    return plan;
}

CampaignBattlePostResultReceipt campaignBattlePostResultReceiptFromOutcome(
    const CampaignBattleOutcomePackage& outcome,
    bool persistentStateUnchanged) {
    return campaignBattlePostResultReceiptFromOutcome(
        outcome,
        persistentStateUnchanged,
        CampaignBattleConsequenceCommitReceipt{});
}

CampaignBattlePostResultReceipt campaignBattlePostResultReceiptFromOutcome(
    const CampaignBattleOutcomePackage& outcome,
    bool persistentStateUnchanged,
    const CampaignBattleConsequenceCommitReceipt& consequenceCommit) {
    CampaignBattlePostResultReceipt receipt;
    if (!outcome.valid) {
        receipt.reason = "campaign_battle_outcome_missing";
        return receipt;
    }

    receipt.valid = true;
    receipt.launchSource = outcome.launchSource;
    receipt.outcome = outcome.outcome;
    receipt.destination = CampaignBattlePostResultDestination::CampaignMainMenu;
    receipt.outcomeReason = outcome.reason;
    receipt.outcomeAcknowledged = true;
    receipt.persistentStateUnchanged = persistentStateUnchanged;
    receipt.settlementCommitted =
        consequenceCommit.valid &&
        consequenceCommit.committed &&
        consequenceCommit.verified &&
        consequenceCommit.saveRoundTripVerified;
    receipt.deferredConsequencesPresent =
        consequenceCommit.valid && consequenceCommit.deferredConsequencesPresent;
    receipt.consequencePlanFingerprint = consequenceCommit.planFingerprint;
    receipt.consequenceSaveRoundTripVerified = consequenceCommit.saveRoundTripVerified;
    receipt.safeCampaignReturn = persistentStateUnchanged || receipt.settlementCommitted;
    receipt.extendedEndingSequenceExecuted = false;
    receipt.extendedEndingSequenceDeferred =
        outcome.launchSource == CampaignBattleLaunchSource::DarkWingFinal;
    receipt.terminal = outcome.terminal;
    receipt.rawResultCode = outcome.rawResultCode;
    receipt.finalSnapshotFingerprint = outcome.finalSnapshotFingerprint;
    receipt.terminalTickIndex = outcome.terminalTickIndex;
    receipt.terminalElapsedMs = outcome.terminalElapsedMs;
    receipt.ticksExecuted = outcome.ticksExecuted;

    if (!receipt.safeCampaignReturn) {
        receipt.reason = consequenceCommit.attempted && !consequenceCommit.reason.empty()
            ? consequenceCommit.reason
            : "campaign_state_changed_while_acknowledging_battle_result";
    } else if (receipt.settlementCommitted) {
        receipt.reason = receipt.deferredConsequencesPresent
            ? "accepted_contract_proven_consequences_committed_deferred_persistence"
            : "accepted_contract_consequences_committed";
    } else if (!persistentStateUnchanged) {
        receipt.reason = "campaign_state_changed_while_acknowledging_battle_result";
    } else if (receipt.extendedEndingSequenceDeferred) {
        receipt.reason = "dark_wing_final_safe_return_extended_ending_deferred";
    } else if (outcome.launchSource == CampaignBattleLaunchSource::AcceptedContract) {
        receipt.reason = "accepted_contract_safe_return_settlement_deferred";
    } else {
        receipt.reason = "campaign_battle_safe_return_no_settlement";
    }
    return receipt;
}

CampaignBattleOutcomePackage runCampaignBattleLaunchAdapter(
    const CampaignBattleLaunchInput& input) {
    CampaignBattleOutcomePackage package;
    package.valid = true;
    if (!input.acceptedContract) {
        package.outcome = CampaignBattleOutcome::Unsupported;
        package.reason = "campaign_contract_not_accepted";
        return package;
    }

    try {
        const StandaloneBattleRunResult run =
            runStandaloneBattle(input.startParams, input.replay, input.runOptions);
        return campaignBattleOutcomePackageFromSnapshots(
            run.startSnapshot,
            run.finalSnapshot,
            run.result,
            run.ticksExecuted);
    } catch (const std::exception& error) {
        package.outcome = CampaignBattleOutcome::Unsupported;
        package.reason = std::string("battle_launch_exception:") + error.what();
        return package;
    }
}

CampaignBattlePresentationPackage campaignBattlePresentationPackageFromSnapshot(
    const BattleSnapshot& snapshot,
    CampaignBattlePresentationMode mode) {
    CampaignBattlePresentationPackage package;
    package.mode = mode;
    package.snapshotFingerprint = battleSnapshotFingerprint(snapshot);
    package.combatantCount = snapshot.combatants.size();
    package.objectiveValid = snapshot.objective.valid;
    package.battlefieldBoundaryValid = snapshot.battlefieldBoundary.valid;
    package.terrainValid = snapshot.terrain.scenarioLoaded;
    package.cameraValid = snapshot.camera.valid;

    for (const CombatantSnapshot& combatant : snapshot.combatants) {
        if (combatant.roster.team == BattleTeam::Player || combatant.playerControlled) {
            ++package.playerCombatantCount;
        } else if (combatant.roster.team == BattleTeam::Opposing) {
            ++package.opposingCombatantCount;
        } else {
            ++package.neutralCombatantCount;
        }
    }

    if (const CombatantSnapshot* player = playerPresentationCombatant(snapshot)) {
        package.playerValid = true;
        package.playerEntityId = player->id;
        package.playerX = player->transform.x;
        package.playerZ = player->transform.z;
        package.playerHeadingRadians = player->transform.headingRadians;
        package.cameraHeadingRadians = snapshot.camera.valid
            ? snapshot.camera.transform.headingRadians
            : player->transform.headingRadians;
        package.torsoYawRadians = player->torsoYawRadians;
        package.playerHeadingDegrees = headingDegrees(package.playerHeadingRadians);
        package.cameraHeadingDegrees = headingDegrees(package.cameraHeadingRadians);
        package.torsoYawDegrees = signedDegrees(package.torsoYawRadians);
        package.aimPitchStep = player->aimPitchStep;
        package.speed = static_cast<int>(player->forwardSpeed + (player->forwardSpeed >= 0.0 ? 0.5 : -0.5));
        package.jumpCapable = player->jumpCapable;
        package.jumpJetReady = player->jumpJetReady;
    }

    const bool hasProjection = package.battlefieldBoundaryValid || package.terrainValid || package.combatantCount > 0;
    package.tacticalMapReady = package.playerValid && hasProjection;
    package.cockpitReady = package.playerValid && package.cameraValid;
    package.externalReady = package.playerValid && package.cameraValid && package.opposingCombatantCount > 0;
    package.snapshotOwned = package.snapshotFingerprint != 0 &&
                            package.playerValid &&
                            package.combatantCount == snapshot.combatants.size();

    switch (mode) {
    case CampaignBattlePresentationMode::MissionStatus:
    case CampaignBattlePresentationMode::CockpitCommandMap:
        package.valid = package.tacticalMapReady;
        break;
    case CampaignBattlePresentationMode::TacticalMap:
        package.valid = package.tacticalMapReady;
        break;
    case CampaignBattlePresentationMode::Cockpit:
        package.valid = package.cockpitReady;
        break;
    case CampaignBattlePresentationMode::External:
        package.valid = package.externalReady;
        break;
    }
    if (!package.valid) {
        package.reason = std::string("campaign_presentation_not_ready:") +
                         campaignBattlePresentationModeName(mode);
    }
    return package;
}

CampaignBattlePresentationPackage campaignBattleInterpolatedPresentationPackageFromSnapshots(
    const BattleSnapshot& previousSnapshot,
    const BattleSnapshot& currentSnapshot,
    double interpolationAlpha,
    CampaignBattlePresentationMode mode) {
    CampaignBattlePresentationPackage package =
        campaignBattlePresentationPackageFromSnapshot(currentSnapshot, mode);
    if (!package.valid || previousSnapshot.tickIndex == currentSnapshot.tickIndex) {
        return package;
    }
    const CombatantSnapshot* currentPlayer = playerPresentationCombatant(currentSnapshot);
    const CombatantSnapshot* previousPlayer = currentPlayer != nullptr
        ? combatantByEntityId(previousSnapshot, currentPlayer->id)
        : nullptr;
    if (previousPlayer == nullptr || currentPlayer == nullptr) {
        return package;
    }

    const double alpha = std::clamp(interpolationAlpha, 0.0, 1.0);
    const Transform playerTransform = interpolateTransform(
        previousPlayer->transform,
        currentPlayer->transform,
        alpha);
    package.playerX = playerTransform.x;
    package.playerZ = playerTransform.z;
    package.playerHeadingRadians = playerTransform.headingRadians;
    package.torsoYawRadians = interpolateHeadingRadians(
        previousPlayer->torsoYawRadians,
        currentPlayer->torsoYawRadians,
        alpha);
    package.playerHeadingDegrees = headingDegrees(package.playerHeadingRadians);
    package.torsoYawDegrees = signedDegrees(package.torsoYawRadians);
    if (previousSnapshot.camera.valid && currentSnapshot.camera.valid &&
        previousSnapshot.camera.attachedEntityId == currentSnapshot.camera.attachedEntityId) {
        package.cameraHeadingRadians = interpolateHeadingRadians(
            previousSnapshot.camera.transform.headingRadians,
            currentSnapshot.camera.transform.headingRadians,
            alpha);
        package.cameraHeadingDegrees = headingDegrees(package.cameraHeadingRadians);
    }
    return package;
}

CampaignBattleRenderScenePackage campaignBattleRenderSceneFromSnapshot(
    const BattleSnapshot& snapshot,
    CampaignBattlePresentationMode mode,
    const std::filesystem::path& sortedOriginalFilesRoot) {
    CampaignBattleRenderScenePackage scene;
    scene.mode = mode;
    scene.snapshotFingerprint = battleSnapshotFingerprint(snapshot);
    scene.authoritativeTickIndex = snapshot.tickIndex;
    scene.camera = snapshot.camera;
    scene.mission = snapshot.mission;
    scene.setup = snapshot.setup;
    scene.battlefieldBoundary = snapshot.battlefieldBoundary;
    scene.scannedTargetKind = snapshot.targetScan.selectedTargetKind;
    scene.scannedTargetEntityId = snapshot.targetScan.selectedTargetEntityId;

    const CampaignBattlePresentationPackage presentation =
        campaignBattlePresentationPackageFromSnapshot(snapshot, mode);
    if (!presentation.valid) {
        scene.reason = presentation.reason;
        return scene;
    }
    if (sortedOriginalFilesRoot.empty()) {
        scene.reason = "campaign_render_asset_root_missing";
        return scene;
    }

    scene.terrain.valid = snapshot.terrain.scenarioLoaded && !snapshot.terrain.tileNames.empty();
    scene.terrain.scenarioIndex = snapshot.terrain.scenarioIndex;
    scene.terrain.terrainMode = snapshot.terrain.terrainMode;
    scene.terrain.environmentId = snapshot.terrain.environmentId;
    scene.terrain.boundsMinX = snapshot.terrain.boundsMinX;
    scene.terrain.boundsMaxX = snapshot.terrain.boundsMaxX;
    scene.terrain.boundsMinZ = snapshot.terrain.boundsMinZ;
    scene.terrain.boundsMaxZ = snapshot.terrain.boundsMaxZ;
    scene.terrain.terrainShapePath = campaignBattleOriginalResourcePath(
        sortedOriginalFilesRoot,
        std::filesystem::path("TBL") / "viewer8" / "TERPCK.TBL");
    scene.terrain.palettePath = campaignBattleOriginalResourcePath(
        sortedOriginalFilesRoot,
        std::filesystem::path("PAL") /
            cockpitPaletteName(snapshot.terrain.environmentId));
    for (const std::string& tileName : snapshot.terrain.tileNames) {
        scene.terrain.gridPaths.push_back(
            campaignBattleOriginalResourcePath(
                sortedOriginalFilesRoot,
                std::filesystem::path("GRD") / (tileName + ".GRD")));
        scene.terrain.worldPaths.push_back(
            campaignBattleOriginalResourcePath(
                sortedOriginalFilesRoot,
                std::filesystem::path("WLD") / (tileName + ".WLD")));
    }

    const CombatantSnapshot* player = playerPresentationCombatant(snapshot);
    if (player == nullptr) {
        scene.reason = "campaign_render_player_missing";
        return scene;
    }
    scene.playerCollisionFlashEvent =
        player->collisionContact &&
        player->collisionCount > 0u &&
        snapshot.lastCollision.valid &&
        (snapshot.lastCollision.entityId == player->id ||
         snapshot.lastCollision.otherEntityId == player->id);
    scene.playerCollisionSequence = scene.playerCollisionFlashEvent
        ? snapshot.lastCollision.sequence
        : 0u;
    scene.cockpit.valid = snapshot.camera.valid;
    scene.cockpit.family = cockpitFamilyForMechPreset(player->mechPresetId);
    scene.cockpitCrosshairVisible =
        !player->majorSystems.enabled || player->majorSystems.crosshairVisible;
    scene.cockpitReactorShutdown =
        player->heat.enabled && player->heat.reactorShutdown;
    scene.cockpitReactorShutdownMessageVisible =
        battleReactorShutdownMessageVisible(player->heat);
    const char* backdropName =
        mode == CampaignBattlePresentationMode::MissionStatus
            ? "STATUS.SCR"
            : mode == CampaignBattlePresentationMode::CockpitCommandMap
                  ? "MAP.SCR"
                  : cockpitBackdropName(scene.cockpit.family);
    scene.cockpit.backdropPath = campaignBattleOriginalResourcePath(
        sortedOriginalFilesRoot,
        std::filesystem::path("SCR") / backdropName);
    scene.cockpit.palettePath = campaignBattleOriginalResourcePath(
        sortedOriginalFilesRoot,
        std::filesystem::path("PAL") /
            cockpitPaletteName(snapshot.terrain.environmentId));
    scene.cockpit.strutsPath = campaignBattleOriginalResourcePath(
        sortedOriginalFilesRoot,
        std::filesystem::path("BMP") / "STRUTS.BMP");
    scene.cockpit.widgetsPath = campaignBattleOriginalResourcePath(
        sortedOriginalFilesRoot,
        std::filesystem::path("BMP") / "COCKPIT.BMP");
    scene.cockpit.hudNumbersPath = campaignBattleOriginalResourcePath(
        sortedOriginalFilesRoot,
        std::filesystem::path("BMP") / "HUD_NUMS.BMP");
    scene.cockpit.hudFontPath = campaignBattleOriginalResourcePath(
        sortedOriginalFilesRoot,
        std::filesystem::path("FNT") / "6X6B.FNT");
    scene.cockpit.smallMechsPath = campaignBattleOriginalResourcePath(
        sortedOriginalFilesRoot,
        std::filesystem::path("BMP") / "SM_MECHS.BMP");

    scene.combatantVisuals.reserve(snapshot.combatants.size());
    try {
        for (const CombatantSnapshot& combatant : snapshot.combatants) {
            if (combatant.missionStatus ==
                CombatantMissionStatus::Escaped) {
                continue;
            }
            CampaignBattleRenderVisualInstance visual;
            visual.entityId = combatant.id;
            visual.team = combatant.roster.team;
            visual.playerControlled = combatant.playerControlled;
            visual.rosterSourceSlot = combatant.roster.sourceSlot;
            visual.mechPresetId = combatant.mechPresetId;
            visual.modelResourcePath =
                catalogResourcePathAtRoot(sortedOriginalFilesRoot, combatant.mechPresetId);
            visual.transform = combatant.transform;
            visual.forwardSpeed = combatant.forwardSpeed;
            visual.maxForwardSpeed = combatant.maxForwardSpeed;
            visual.maxReverseSpeed = combatant.maxReverseSpeed;
            visual.torsoYawRadians = combatant.torsoYawRadians;
            visual.aimPitchStep = combatant.aimPitchStep;
            visual.animationId = combatant.activeAnimationId;
            visual.animationElapsedMs = combatant.walkAnimationElapsedMs;
            visual.mechDestroyed = combatant.mechDestroyed;
            visual.destroyedComponentIds = combatant.destroyedComponentIds;
            visual.hiddenComponentIds = combatant.hiddenComponentIds;
            visual.disabledComponentIds = combatant.disabledComponentIds;
            scene.combatantVisuals.push_back(std::move(visual));
            if (combatant.playerControlled || combatant.roster.team == BattleTeam::Player) {
                ++scene.playerVisualCount;
            } else if (combatant.roster.team == BattleTeam::Opposing) {
                ++scene.opposingVisualCount;
            }
        }
    } catch (const std::exception& error) {
        scene.reason = std::string("campaign_render_mech_resource_unavailable:") + error.what();
        return scene;
    }

    scene.projectileVisuals.reserve(snapshot.projectiles.size());
    for (const BattleProjectileState& projectile : snapshot.projectiles) {
        if (!projectile.valid) {
            continue;
        }
        const int recordIndex = campaignBattleProjectileRecordIndex(
            projectile.originalVisualClass);
        if (recordIndex < 0) {
            scene.reason = "campaign_render_projectile_visual_class_unresolved";
            return scene;
        }
        CampaignBattleRenderProjectileInstance visual;
        visual.projectileId = projectile.projectileId;
        visual.launchTickIndex = projectile.launchTickIndex;
        visual.shooterEntityId = projectile.shooterEntityId;
        visual.weaponInstanceId = projectile.weaponInstanceId;
        visual.weaponTypeId = projectile.weaponTypeId;
        visual.launchShooterTransform = projectile.launchShooterTransform;
        visual.launchTorsoYawRadians = projectile.launchTorsoYawRadians;
        visual.launchModelMount = projectile.launchModelMount;
        visual.launchModelMountValid = projectile.launchModelMountValid;
        if (const CombatantSnapshot* shooter = combatantByEntityId(
                snapshot, projectile.shooterEntityId)) {
            if (const BattleWeaponInstanceState* weapon = weaponByInstanceId(
                    *shooter, projectile.weaponInstanceId)) {
                visual.locationId = weapon->locationId;
            }
        }
        visual.modelResourcePath = campaignBattleOriginalResourcePath(
            sortedOriginalFilesRoot,
            std::filesystem::path("TBL") / "viewer8" / "OTHPCK.TBL");
        visual.recordIndex = recordIndex;
        visual.position = projectile.position;
        visual.direction = projectile.direction;
        visual.targetBound = projectile.targetBound;
        visual.launchAimOrigin = projectile.pendingShot.aimOrigin;
        visual.launchAimOriginValid = projectile.pendingShot.valid;
        if (projectile.targetBound &&
            projectile.targetKind == BattleShotTargetKind::Combatant) {
            if (const CombatantSnapshot* target = combatantByEntityId(
                    snapshot, projectile.targetEntityId)) {
                const double cosine = std::cos(target->transform.headingRadians);
                const double sine = std::sin(target->transform.headingRadians);
                visual.visualTargetPoint = {
                    target->transform.x +
                        cosine * projectile.targetLocalPoint.x +
                        sine * projectile.targetLocalPoint.z,
                    target->transform.y + projectile.targetLocalPoint.y,
                    target->transform.z -
                        sine * projectile.targetLocalPoint.x +
                        cosine * projectile.targetLocalPoint.z,
                };
                visual.visualTargetPointValid = true;
            }
        } else if (projectile.targetBound &&
                   projectile.targetKind == BattleShotTargetKind::Objective &&
                   snapshot.objective.valid) {
            const double cosine = std::cos(
                snapshot.objective.transform.headingRadians);
            const double sine = std::sin(
                snapshot.objective.transform.headingRadians);
            visual.visualTargetPoint = {
                snapshot.objective.transform.x +
                    cosine * projectile.targetLocalPoint.x +
                    sine * projectile.targetLocalPoint.z,
                snapshot.objective.transform.y + projectile.targetLocalPoint.y,
                snapshot.objective.transform.z -
                    sine * projectile.targetLocalPoint.x +
                    cosine * projectile.targetLocalPoint.z,
            };
            visual.visualTargetPointValid = true;
        }
        if (!visual.visualTargetPointValid && projectile.pendingShot.valid) {
            if (projectile.targetBound &&
                hitPointLengthSquared(projectile.pendingShot.impactPoint) >
                    std::numeric_limits<double>::epsilon()) {
                visual.visualTargetPoint = projectile.pendingShot.impactPoint;
                visual.visualTargetPointValid = true;
            } else if (hitPointLengthSquared(
                           projectile.pendingShot.aimDirection) >
                       std::numeric_limits<double>::epsilon()) {
                const BattleHitPoint direction = normalizedHitPoint(
                    projectile.pendingShot.aimDirection);
                const double range = std::max(
                    projectile.pendingShot.maximumRange, 500.0);
                visual.visualTargetPoint = {
                    projectile.pendingShot.aimOrigin.x + direction.x * range,
                    projectile.pendingShot.aimOrigin.y + direction.y * range,
                    projectile.pendingShot.aimOrigin.z + direction.z * range,
                };
                visual.visualTargetPointValid = true;
            }
        }
        visual.launchForwardOffset = projectile.launchForwardOffset;
        visual.spinDegrees = campaignBattleProjectileSpinDegrees(
            projectile.launchTickIndex,
            snapshot.tickIndex);
        visual.originalResourceMappingProven = true;
        visual.spinTimingProven = false;
        visual.modelMountPolicyProven = false;
        scene.projectileVisuals.push_back(std::move(visual));
    }

    if (!snapshot.shotEvents.empty()) {
        scene.beamVisuals.reserve(snapshot.shotEvents.size());
        for (const BattleShotDiagnostic& shot : snapshot.shotEvents) {
            if (const auto beam = immediateBeamFromSnapshot(snapshot, shot)) {
                scene.beamVisuals.push_back(*beam);
            }
            if (const auto flash = machineGunFlashFromSnapshot(
                    snapshot, shot, scene.cockpit.family)) {
                scene.machineGunFlashVisuals.push_back(*flash);
            }
        }
    } else if (const auto beam = immediateBeamFromSnapshot(
                   snapshot, snapshot.lastShot)) {
        // Compatibility for diagnostic/synthetic snapshots created before
        // the per-tick authoritative shot event list was introduced.
        scene.beamVisuals.push_back(*beam);
    } else if (const auto flash = machineGunFlashFromSnapshot(
                   snapshot, snapshot.lastShot, scene.cockpit.family)) {
        scene.machineGunFlashVisuals.push_back(*flash);
    }

    scene.impactVisuals.reserve(snapshot.impactEvents.size());
    for (const BattleImpactEvent& event : snapshot.impactEvents) {
        if (!event.valid || snapshot.tickIndex <= event.impactTickIndex) {
            continue;
        }
        const CombatantSnapshot* shooter = combatantByEntityId(
            snapshot, event.shooterEntityId);
        const BattleWeaponInstanceState* weapon = shooter == nullptr
            ? nullptr
            : weaponByInstanceId(*shooter, event.weaponInstanceId);
        if (weapon == nullptr) {
            continue;
        }
        const uint64_t age = snapshot.tickIndex - event.impactTickIndex;
        const bool machineGunSequence = event.visualSequence ==
            BattleImpactVisualSequence::OriginalMachineGunTwoStage;
        const bool terrainSmokeSequence = event.visualSequence ==
            BattleImpactVisualSequence::TerrainSmokeLastThree;
        constexpr uint64_t kPresentationTicksPerStage = 2u;
        constexpr uint64_t kImpactStageCount = 6u;
        constexpr uint64_t kMachineGunStageCount = 2u;
        constexpr uint64_t kTerrainSmokeStageCount = 3u;
        const uint64_t stageCount = machineGunSequence
            ? kMachineGunStageCount
            : (terrainSmokeSequence
                   ? kTerrainSmokeStageCount
                   : kImpactStageCount);
        if (age > kPresentationTicksPerStage * stageCount) {
            continue;
        }
        CampaignBattleRenderImpactInstance visual;
        visual.shotSequence = event.shotSequence;
        visual.impactTickIndex = event.impactTickIndex;
        visual.targetKind = event.targetKind;
        visual.targetEntityId = event.targetEntityId;
        visual.position = event.point;
        visual.modelResourcePath = campaignBattleOriginalResourcePath(
            sortedOriginalFilesRoot,
            std::filesystem::path("TBL") / "viewer8" / "OTHPCK.TBL");
        const int stageIndex = static_cast<int>(
            (age - 1u) / kPresentationTicksPerStage);
        visual.recordIndex = machineGunSequence
            ? 8 + stageIndex
            : (terrainSmokeSequence ? 5 + stageIndex : 2 + stageIndex);
        visual.machineGunSequence = machineGunSequence;
        visual.terrainSmokeSequence = terrainSmokeSequence;
        visual.originalResourceMappingProven = true;
        visual.originalSixUpdateSequenceProven =
            !machineGunSequence && !terrainSmokeSequence;
        visual.originalMachineGunTwoUpdateSequenceProven =
            machineGunSequence;
        visual.stageDurationProven = false;
        visual.sphereRasterizationProven = false;
        scene.impactVisuals.push_back(std::move(visual));
    }

    scene.objective.valid = snapshot.objective.valid;
    scene.objective.playerProtected =
        battleObjectiveProtectedByPlayer(snapshot.objective);
    scene.objective.transform = snapshot.objective.transform;
    scene.objective.staticModel = snapshot.objective.staticModel;
    scene.objective.damage = snapshot.objective.damage;
    scene.objective.maxDamage = snapshot.objective.maxDamage;
    scene.objective.depleted = snapshot.objective.depleted;
    if (snapshot.objective.staticModel.valid) {
        scene.objective.modelResourcePath = campaignBattleOriginalResourcePath(
            sortedOriginalFilesRoot,
            std::filesystem::path("TBL") / "viewer8" /
                snapshot.objective.staticModel.resourceName);
    }

    const bool terrainPathsResolved =
        scene.terrain.valid &&
        !scene.terrain.terrainShapePath.empty() &&
        !scene.terrain.palettePath.empty() &&
        scene.terrain.gridPaths.size() == snapshot.terrain.tileNames.size() &&
        scene.terrain.worldPaths.size() == snapshot.terrain.tileNames.size();
    const bool combatantPathsResolved =
        scene.combatantVisuals.size() == snapshot.combatants.size() &&
        std::all_of(
            scene.combatantVisuals.begin(),
            scene.combatantVisuals.end(),
            [](const CampaignBattleRenderVisualInstance& visual) {
                return isValid(visual.entityId) && !visual.modelResourcePath.empty();
            });
    const bool projectilePathsResolved =
        scene.projectileVisuals.size() ==
            static_cast<size_t>(std::count_if(
                snapshot.projectiles.begin(),
                snapshot.projectiles.end(),
                [](const BattleProjectileState& projectile) {
                    return projectile.valid;
                })) &&
        std::all_of(
            scene.projectileVisuals.begin(),
            scene.projectileVisuals.end(),
            [](const CampaignBattleRenderProjectileInstance& projectile) {
                return projectile.projectileId != 0u &&
                    projectile.recordIndex >= 0 &&
                    !projectile.modelResourcePath.empty();
            });
    const bool impactPathsResolved = std::all_of(
        scene.impactVisuals.begin(),
        scene.impactVisuals.end(),
        [](const CampaignBattleRenderImpactInstance& impact) {
            return impact.shotSequence != 0u &&
                impact.recordIndex >= 2 && impact.recordIndex <= 9 &&
                !impact.modelResourcePath.empty();
        });
    const bool cockpitPathsResolved =
        scene.cockpit.valid &&
        !scene.cockpit.backdropPath.empty() &&
        !scene.cockpit.palettePath.empty() &&
        !scene.cockpit.smallMechsPath.empty();
    const bool objectivePathResolved =
        !scene.objective.valid ||
        !scene.objective.staticModel.valid ||
        !scene.objective.modelResourcePath.empty();
    scene.resourcePathsResolved =
        terrainPathsResolved && combatantPathsResolved &&
        projectilePathsResolved && impactPathsResolved && cockpitPathsResolved &&
        objectivePathResolved;
    scene.snapshotOwned =
        presentation.snapshotOwned &&
        scene.snapshotFingerprint == presentation.snapshotFingerprint &&
        scene.authoritativeTickIndex == snapshot.tickIndex &&
        scene.scannedTargetKind == snapshot.targetScan.selectedTargetKind &&
        scene.scannedTargetEntityId == snapshot.targetScan.selectedTargetEntityId &&
        scene.battlefieldBoundary.valid == snapshot.battlefieldBoundary.valid &&
        scene.combatantVisuals.size() == snapshot.combatants.size() &&
        scene.projectileVisuals.size() ==
            static_cast<size_t>(std::count_if(
                snapshot.projectiles.begin(),
                snapshot.projectiles.end(),
                [](const BattleProjectileState& projectile) {
                    return projectile.valid;
                })) &&
        scene.beamVisuals.size() <=
            std::max<size_t>(snapshot.shotEvents.size(), 1u) &&
        scene.machineGunFlashVisuals.size() <=
            std::max<size_t>(snapshot.shotEvents.size(), 1u) &&
        scene.impactVisuals.size() <= snapshot.impactEvents.size() &&
        scene.objective.valid == snapshot.objective.valid &&
        scene.objective.playerProtected ==
            battleObjectiveProtectedByPlayer(snapshot.objective);
    scene.valid = scene.resourcePathsResolved && scene.snapshotOwned;
    if (!scene.valid) {
        scene.reason = "campaign_render_scene_not_ready";
    }
    scene.cockpitCameraBobOffsetY = cockpitBobOffsetForScene(scene);
    return scene;
}

CampaignBattleRenderScenePackage campaignBattleInterpolatedRenderSceneFromSnapshots(
    const BattleSnapshot& previousSnapshot,
    const BattleSnapshot& currentSnapshot,
    double interpolationAlpha,
    CampaignBattlePresentationMode mode,
    const std::filesystem::path& sortedOriginalFilesRoot) {
    CampaignBattleRenderScenePackage scene = campaignBattleRenderSceneFromSnapshot(
        currentSnapshot,
        mode,
        sortedOriginalFilesRoot);
    scene.previousSnapshotTickIndex = previousSnapshot.tickIndex;
    scene.currentSnapshotTickIndex = currentSnapshot.tickIndex;
    scene.interpolationAlpha = std::clamp(interpolationAlpha, 0.0, 1.0);
    if (!scene.valid || previousSnapshot.tickIndex == currentSnapshot.tickIndex) {
        return scene;
    }

    if (previousSnapshot.camera.valid && currentSnapshot.camera.valid &&
        previousSnapshot.camera.attachedEntityId == currentSnapshot.camera.attachedEntityId) {
        scene.camera.transform = interpolateTransform(
            previousSnapshot.camera.transform,
            currentSnapshot.camera.transform,
            scene.interpolationAlpha);
    }

    for (CampaignBattleRenderVisualInstance& visual : scene.combatantVisuals) {
        const CombatantSnapshot* previous = combatantByEntityId(
            previousSnapshot,
            visual.entityId);
        const CombatantSnapshot* current = combatantByEntityId(
            currentSnapshot,
            visual.entityId);
        if (previous == nullptr || current == nullptr) {
            continue;
        }
        visual.transform = interpolateTransform(
            previous->transform,
            current->transform,
            scene.interpolationAlpha);
        visual.forwardSpeed = interpolateScalar(
            previous->forwardSpeed,
            current->forwardSpeed,
            scene.interpolationAlpha);
        visual.torsoYawRadians = interpolateHeadingRadians(
            previous->torsoYawRadians,
            current->torsoYawRadians,
            scene.interpolationAlpha);
        if (previous->activeAnimationId == current->activeAnimationId &&
            current->walkAnimationElapsedMs >= previous->walkAnimationElapsedMs) {
            const double animationElapsed = interpolateScalar(
                static_cast<double>(previous->walkAnimationElapsedMs),
                static_cast<double>(current->walkAnimationElapsedMs),
                scene.interpolationAlpha);
            visual.animationElapsedMs = static_cast<uint64_t>(
                std::llround(std::max(0.0, animationElapsed)));
        }
        ++scene.interpolatedVisualCount;
    }
    for (CampaignBattleRenderProjectileInstance& visual :
         scene.projectileVisuals) {
        const BattleProjectileState* previous = projectileById(
            previousSnapshot,
            visual.projectileId);
        const BattleProjectileState* current = projectileById(
            currentSnapshot,
            visual.projectileId);
        if (previous == nullptr || current == nullptr) {
            continue;
        }
        visual.position = interpolateHitPoint(
            previous->position,
            current->position,
            scene.interpolationAlpha);
        visual.direction = normalizedHitPoint(interpolateHitPoint(
            previous->direction,
            current->direction,
            scene.interpolationAlpha));
        visual.spinDegrees = interpolateScalar(
            campaignBattleProjectileSpinDegrees(
                previous->launchTickIndex,
                previousSnapshot.tickIndex),
            campaignBattleProjectileSpinDegrees(
                current->launchTickIndex,
                currentSnapshot.tickIndex),
            scene.interpolationAlpha);
        ++scene.interpolatedProjectileCount;
    }

    // A beam is deliberately visible for two authoritative ticks.  The
    // current per-tick event list may already contain a different shot, so
    // recover still-live beams from the immutable previous snapshot instead
    // of introducing renderer-owned wall-clock state.
    const auto retainPreviousBeam = [&](const BattleShotDiagnostic& shot) {
        const auto duplicate = std::find_if(
            scene.beamVisuals.begin(),
            scene.beamVisuals.end(),
            [&shot](const CampaignBattleRenderBeamInstance& beam) {
                return beam.shotSequence == shot.sequence;
            });
        if (duplicate != scene.beamVisuals.end()) {
            return;
        }
        if (const auto beam = immediateBeamFromSnapshot(currentSnapshot, shot)) {
            scene.beamVisuals.push_back(*beam);
        }
    };
    if (!previousSnapshot.shotEvents.empty()) {
        for (const BattleShotDiagnostic& shot : previousSnapshot.shotEvents) {
            retainPreviousBeam(shot);
        }
    } else {
        retainPreviousBeam(previousSnapshot.lastShot);
    }
    scene.presentationInterpolated =
        scene.interpolatedVisualCount > 0u ||
        scene.interpolatedProjectileCount > 0u ||
        (previousSnapshot.camera.valid && currentSnapshot.camera.valid);
    scene.cockpitCameraBobOffsetY = cockpitBobOffsetForScene(scene);
    return scene;
}

} // namespace mw::battle
