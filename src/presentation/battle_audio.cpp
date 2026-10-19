#include "presentation/battle_audio.h"
#include <algorithm>
namespace mw::presentation {
namespace {
const battle::CombatantSnapshot* player(const battle::BattleSnapshot& s) {
    for(const auto& c:s.combatants) if(c.playerControlled) return &c;
    return nullptr;
}
int totalDamage(const battle::CombatantSnapshot& c) {
    int value=0;
    for(const auto& a:c.detailedDamage.armorSections) value+=a.battleDamage;
    for(const auto& a:c.detailedDamage.internalSections) value+=a.battleDamage;
    return value;
}
uint16_t weaponSound(const std::string& id) {
    // Original token4=PPC,5=AC/5;0..3 are lasers/MG,6..9 are missiles.
    return id=="ppc"?4:id=="ac5"?2:
        (id=="lrm5" || id=="srm2" || id=="srm4" || id=="srm6")?3:1;
}
bool walking(const battle::CombatantSnapshot& c) {
    return c.activeAnimationId=="walk" && !c.airborne && !c.mechDestroyed &&
        c.missionStatus==battle::CombatantMissionStatus::Active &&
        c.posture.posture==battle::BattlePosture::Upright;
}
uint64_t walkFootfalls(uint64_t elapsedMs) {
    // Catalog walk: eight 180ms frames. BTECH A32F..A34B requests 11/1
    // on frames 3 and 7. Use the simulation's speed-scaled animation clock.
    return (elapsedMs/180u+1u)/4u;
}
}
std::vector<audio::SpeakerRequest> battleSpeakerRequests(
    const battle::BattleSnapshot& before,const battle::BattleSnapshot& after) {
    using namespace battle;
    std::vector<audio::SpeakerRequest> requests;
    if(after.tickIndex<=before.tickIndex ||
       (before.conclusionDelay.active && before.conclusionDelay.freezesSimulation)) return requests;
    const auto* old=player(before);const auto* now=player(after);
    if(!old || !now || old->id!=now->id) return requests;
    if(walking(*now) && now->walkAnimationElapsedMs>=old->walkAnimationElapsedMs) {
        const auto previous=walking(*old)?walkFootfalls(old->walkAnimationElapsedMs):0;
        const auto current=walkFootfalls(now->walkAnimationElapsedMs);
        for(uint64_t n=previous;n<current && n-previous<8u;++n) requests.push_back({11,1});
    }
    // Count every accepted weapon in an AWS salvo; lastShot loses earlier shots.
    for(const auto& w:now->weapons) {
        const auto previous=std::find_if(old->weapons.begin(),old->weapons.end(),[&](const auto& p){return p.weaponInstanceId==w.weaponInstanceId;});
        if(previous!=old->weapons.end() && w.shotsFired>previous->shotsFired)
            requests.push_back({weaponSound(w.weaponTypeId),10}); //0E85
    }
    if(now->jumpJetThrusting || now->jumpForwardThrusting) requests.push_back({14,15}); //10A9
    bool playerHit=false;
    for(const auto& s:after.shotEvents) {
        if(s.valid && s.shooterEntityId!=now->id && s.targetEntityId==now->id &&
           s.targetKind==BattleShotTargetKind::Combatant &&
           s.deliveryState==BattleShotDeliveryState::ProjectileInFlight &&
           s.tickIndex>=before.tickIndex && s.tickIndex<after.tickIndex)
            requests.push_back({8,15}); //ABF5..AC03: incoming projectile, before contact.
        const bool contact=s.valid && s.hit && s.result==BattleShotResult::Hit &&
            s.targetKind==BattleShotTargetKind::Combatant && s.impactTickIndex>=before.tickIndex &&
            s.impactTickIndex<after.tickIndex &&
            (s.deliveryState==BattleShotDeliveryState::Immediate || s.deliveryState==BattleShotDeliveryState::ProjectileImpact);
        if(!contact) continue;
        if(s.deliveryState==BattleShotDeliveryState::ProjectileImpact || s.shooterEntityId==now->id)
            requests.push_back({5,5}); //47D2/56E6 physical mech hit
        if(s.targetEntityId==now->id) { requests.push_back({7,40});playerHit=true; } //4F03
    }
    // Also consume the contact history used by the cockpit hit flash. A contact
    // can survive a transient shot-diagnostic list; never play it twice when
    // both lists describe the same impact, or again on its later flash frames.
    for(const auto& hit:after.impactEvents) {
        if(!hit.valid || hit.targetEntityId!=now->id || hit.targetKind!=BattleShotTargetKind::Combatant ||
           hit.impactTickIndex<before.tickIndex || hit.impactTickIndex>=after.tickIndex ||
           (hit.deliveryState!=BattleShotDeliveryState::Immediate && hit.deliveryState!=BattleShotDeliveryState::ProjectileImpact)) continue;
        const bool inShots=std::any_of(after.shotEvents.begin(),after.shotEvents.end(),[&](const auto& s) {
            return s.valid && s.hit && s.result==BattleShotResult::Hit &&
                s.targetKind==BattleShotTargetKind::Combatant &&
                s.sequence==hit.shotSequence && s.impactTickIndex==hit.impactTickIndex &&
                s.targetEntityId==hit.targetEntityId && s.shooterEntityId==hit.shooterEntityId &&
                s.deliveryState==hit.deliveryState;
        });
        if(inShots) continue;
        if(hit.deliveryState==BattleShotDeliveryState::ProjectileImpact) requests.push_back({5,5});
        requests.push_back({7,40});playerHit=true;
    }
    // Collision, landing and ammunition damage share the player-damage helper.
    // Coalesce non-shot damage within one simulation tick.
    if(!playerHit && totalDamage(*now)>totalDamage(*old)) requests.push_back({7,40});
    if(now->majorSystems.enabled && old->majorSystems.enabled) {
        const auto& a=old->majorSystems;const auto& b=now->majorSystems;
        for(const auto& pair:{std::make_pair(a.sensors,b.sensors),std::make_pair(a.gyros,b.gyros),
                            std::make_pair(a.engine,b.engine),std::make_pair(a.lifeSupport,b.lifeSupport)})
            if(static_cast<int>(pair.second)>static_cast<int>(pair.first)) requests.push_back({10,60});
    }
    if(after.conclusionDelay.active && after.conclusionDelay.freezesSimulation &&
       !(before.conclusionDelay.active && before.conclusionDelay.freezesSimulation)) requests.push_back({5,100}); //01D8
    return requests;
}
}
