#include "battle/battle_world.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace mw::battle {

const char* battleLanceOrderLabel(BattleLanceOrderKind kind) {
    return battleOriginalAiLanceOrder(static_cast<int16_t>(kind)).label;
}

const char* battleLanceOrderStatusLabel(const BattleLanceOrderState& state) {
    switch (state.phase) {
    case BattleLanceOrderPhase::MissionDefault: return "ACTING ON OWN";
    case BattleLanceOrderPhase::Travelling: return "MOVING";
    case BattleLanceOrderPhase::Waiting: return "WAITING";
    case BattleLanceOrderPhase::Engaging: return "CHARGING";
    case BattleLanceOrderPhase::Returning: return "RETURNING";
    case BattleLanceOrderPhase::Arrived: return "AT LOCATION";
    case BattleLanceOrderPhase::Released: return "ACTING ON OWN";
    case BattleLanceOrderPhase::Retreating: return "FLEEING";
    case BattleLanceOrderPhase::Inactive: return "INACTIVE";
    }
    return "UNENGAGED";
}

bool battleLanceOrderRecipientEligible(const CombatantSnapshot& actor) {
    return actor.roster.team == BattleTeam::Player && !actor.playerControlled &&
        actor.missionStatus == CombatantMissionStatus::Active && !actor.mechDestroyed;
}

bool battleLanceOrderTargetKnown(const BattleSnapshot& snapshot, EntityId id) {
    const auto target = std::find_if(snapshot.combatants.begin(), snapshot.combatants.end(),
        [id](const auto& actor) { return actor.id == id; });
    if (target == snapshot.combatants.end() || target->roster.team != BattleTeam::Opposing ||
        target->missionStatus != CombatantMissionStatus::Active || target->mechDestroyed) return false;
    for (const auto& observer : snapshot.combatants) {
        if (observer.roster.team == BattleTeam::Player && !observer.mechDestroyed &&
            observer.missionStatus == CombatantMissionStatus::Active &&
            (!observer.majorSystems.enabled || observer.majorSystems.radarContactsVisible) &&
            std::hypot(observer.transform.x - target->transform.x,
                       observer.transform.z - target->transform.z) <= battleRadarContactRangeWorldUnits()) return true;
    }
    return false;
}

bool battleLanceOrderObjectiveEligible(const BattleSnapshot& snapshot) {
    const auto& objective = snapshot.objective;
    return snapshot.mission.family == BattleMissionFamily::Assault &&
        objective.valid && objective.transformProven && !objective.depleted &&
        objective.damagePolicyProven && !objective.damageSuppressed &&
        !battleObjectiveProtectedByPlayer(objective);
}

bool battleLanceOrderCommandValid(const BattleSnapshot& snapshot, const BattleInputCommand& envelope) {
    if (!envelope.lanceOrder || snapshot.combatAiPolicy != BattleCombatAiPolicy::ReplacementDeterministicCombatAi ||
        snapshot.missionRuntimeState != BattleMissionRuntimeState::InProgress ||
        !snapshot.mission.valid || snapshot.mission.extended ||
        snapshot.mission.family == BattleMissionFamily::Extended) return false;
    const auto& command = *envelope.lanceOrder;
    if (envelope.tickIndex < snapshot.tickIndex || command.issueTickIndex > envelope.tickIndex || !isValid(command.issuer) ||
        envelope.entityId != command.issuer || !battleOriginalAiLanceOrder(static_cast<int16_t>(command.kind)).selectable)
        return false;
    const auto issuer = std::find_if(snapshot.combatants.begin(), snapshot.combatants.end(),
        [&](const auto& actor) { return actor.id == command.issuer; });
    const auto recipient = std::find_if(snapshot.combatants.begin(), snapshot.combatants.end(),
        [&](const auto& actor) { return actor.id == command.recipient; });
    if (issuer == snapshot.combatants.end() || !issuer->playerControlled || issuer->mechDestroyed ||
        issuer->missionStatus != CombatantMissionStatus::Active || recipient == snapshot.combatants.end() ||
        !battleLanceOrderRecipientEligible(*recipient)) return false;
    const auto& p = command.destination;
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || !std::isfinite(p.headingRadians)) return false;
    const bool location = command.kind == BattleLanceOrderKind::Ambush || command.kind == BattleLanceOrderKind::Defend ||
        command.kind == BattleLanceOrderKind::MoveAttack || command.kind == BattleLanceOrderKind::MoveAvoid;
    if (location) {
        const double minX = snapshot.battlefieldBoundary.valid ? snapshot.battlefieldBoundary.worldMinX : snapshot.terrain.boundsMinX;
        const double maxX = snapshot.battlefieldBoundary.valid ? snapshot.battlefieldBoundary.worldMaxX : snapshot.terrain.boundsMaxX;
        const double minZ = snapshot.battlefieldBoundary.valid ? snapshot.battlefieldBoundary.worldMinZ : snapshot.terrain.boundsMinZ;
        const double maxZ = snapshot.battlefieldBoundary.valid ? snapshot.battlefieldBoundary.worldMaxZ : snapshot.terrain.boundsMaxZ;
        return command.targetKind == BattleLanceTargetKind::WorldPoint && !isValid(command.targetEntityId) &&
            p.y == 0.0 && p.headingRadians == 0.0 && p.x >= minX && p.x <= maxX && p.z >= minZ && p.z <= maxZ;
    }
    // Unused fields are canonical zero: malformed tagged commands fail closed.
    if (p.x != 0.0 || p.y != 0.0 || p.z != 0.0 || p.headingRadians != 0.0) return false;
    switch (command.targetKind) {
    case BattleLanceTargetKind::None:
        return command.kind == BattleLanceOrderKind::ActOnOwn && !isValid(command.targetEntityId);
    case BattleLanceTargetKind::Combatant: return battleLanceOrderTargetKnown(snapshot, command.targetEntityId);
    case BattleLanceTargetKind::Objective: return !isValid(command.targetEntityId) && battleLanceOrderObjectiveEligible(snapshot);
    default: return false;
    }
}

std::string serializeBattleReplay(const BattleReplay& replay) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    const bool mouseLookFields=std::any_of(replay.commands.begin(),replay.commands.end(),[](const auto& c) {
        return c.mouseLookEnabled.has_value() || c.mouseLookYawDeltaRadians!=0 || c.mouseLookPitchDeltaRadians!=0 || c.centerTorso || c.mouseLookZoomLevel;
    });
    out << (mouseLookFields ? "MW_BATTLE_REPLAY 6\n" : "MW_BATTLE_REPLAY 5\n") << std::setprecision(17);
    for (const auto& c : replay.commands) {
        out << c.tickIndex << ' ' << c.entityId.value << ' ' << c.throttle << ' ' << c.turn << ' '
            << c.torsoYawStepDelta << ' ' << c.aimPitchStepDelta << ' ' << c.jumpJetToggle << ' '
            << c.jumpForwardThrust << ' ' << c.jumpVerticalThrust << ' ' << c.fireWeapon << ' '
            << c.selectedWeaponStepDelta << ' ' << c.selectWeaponInstanceId << ' ' << c.toggleCockpitRadar << ' '
            << c.cycleCockpitRadarRange << ' ' << c.cycleTargetScan << ' ' << c.lanceOrder.has_value();
        if (c.lanceOrder) {
            const auto& o = *c.lanceOrder;
            out << ' ' << o.recipient.value << ' ' << o.issuer.value << ' ' << o.issueTickIndex << ' '
                << static_cast<int>(o.kind) << ' ' << static_cast<int>(o.targetKind) << ' ' << o.targetEntityId.value
                << ' ' << o.destination.x << ' ' << o.destination.y << ' ' << o.destination.z << ' ' << o.destination.headingRadians;
        }
        out << ' ' << c.toggleAutomaticWeapons << ' ' << c.toggleSelectedWeaponLink << ' ' << c.fireLinkedWeapons << ' ' << c.standUp << ' ' << c.acknowledgeConclusion << ' ' << c.confirmRetreat;
        if (mouseLookFields) out << ' ' << (c.mouseLookEnabled ? int(*c.mouseLookEnabled) : -1) << ' '
            << c.mouseLookYawDeltaRadians << ' ' << c.mouseLookPitchDeltaRadians << ' ' << c.centerTorso << ' ' << c.mouseLookZoomLevel;
        out << '\n';
    }
    return out.str();
}

BattleReplay parseBattleReplay(const std::string& text) {
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    std::string line;
    if (!std::getline(in, line) || (line != "MW_BATTLE_REPLAY 1" && line != "MW_BATTLE_REPLAY 2" && line != "MW_BATTLE_REPLAY 3" && line != "MW_BATTLE_REPLAY 4" && line != "MW_BATTLE_REPLAY 5" && line != "MW_BATTLE_REPLAY 6"))
        throw std::runtime_error("unsupported battle replay header");
    const bool mouseLookFields = line == "MW_BATTLE_REPLAY 6";
    const bool retreatFields = mouseLookFields || line == "MW_BATTLE_REPLAY 5";
    const bool conclusionFields = retreatFields || line == "MW_BATTLE_REPLAY 4";
    const bool postureFields = conclusionFields || line == "MW_BATTLE_REPLAY 3";
    const bool awsFields = postureFields || line == "MW_BATTLE_REPLAY 2";
    BattleReplay replay;
    while (std::getline(in, line)) {
        std::istringstream row(line);
        row.imbue(std::locale::classic());
        BattleInputCommand c;
        bool hasOrder = false;
        row >> c.tickIndex >> c.entityId.value >> c.throttle >> c.turn >> c.torsoYawStepDelta >> c.aimPitchStepDelta
            >> c.jumpJetToggle >> c.jumpForwardThrust >> c.jumpVerticalThrust >> c.fireWeapon
            >> c.selectedWeaponStepDelta >> c.selectWeaponInstanceId >> c.toggleCockpitRadar
            >> c.cycleCockpitRadarRange >> c.cycleTargetScan >> hasOrder;
        if (hasOrder) {
            BattleLanceOrderCommand o;
            int kind = -1, target = -1;
            row >> o.recipient.value >> o.issuer.value >> o.issueTickIndex >> kind >> target >> o.targetEntityId.value
                >> o.destination.x >> o.destination.y >> o.destination.z >> o.destination.headingRadians;
            if (kind < 0 || kind > 5 || target < 0 || target > 3) throw std::runtime_error("invalid replay order tag");
            o.kind = static_cast<BattleLanceOrderKind>(kind);
            o.targetKind = static_cast<BattleLanceTargetKind>(target);
            c.lanceOrder = o;
        }
        if (awsFields) row >> c.toggleAutomaticWeapons >> c.toggleSelectedWeaponLink >> c.fireLinkedWeapons;
        if (postureFields) row >> c.standUp;
        if (conclusionFields) row >> c.acknowledgeConclusion;
        if (retreatFields) row >> c.confirmRetreat;
        if (mouseLookFields) {
            int enabled=-1;
            row >> enabled >> c.mouseLookYawDeltaRadians >> c.mouseLookPitchDeltaRadians >> c.centerTorso >> c.mouseLookZoomLevel;
            if (enabled < -1 || enabled > 1 || c.mouseLookZoomLevel<0 || c.mouseLookZoomLevel>3 || !std::isfinite(c.mouseLookYawDeltaRadians) || !std::isfinite(c.mouseLookPitchDeltaRadians))
                throw std::runtime_error("invalid mouse look replay input");
            if (enabled>=0)c.mouseLookEnabled=enabled!=0;
        }
        if (!row || !std::isfinite(c.throttle) || !std::isfinite(c.turn)) throw std::runtime_error("invalid battle replay command");
        row >> std::ws;
        if (!row.eof()) throw std::runtime_error("trailing battle replay data");
        replay.commands.push_back(c);
    }
    return replay;
}
} // namespace mw::battle
