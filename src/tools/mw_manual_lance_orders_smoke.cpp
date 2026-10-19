#include "battle/battle_setup.h"
#include "battle/mission_contract.h"
#include "presentation/campaign_command_screen.h"
#include "presentation/campaign_battle_gl_viewport.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace {
using namespace mw::battle;
using namespace mw::presentation;
using Kind = BattleLanceOrderKind;
using Target = BattleLanceTargetKind;
using Phase = BattleLanceOrderPhase;
using Key = CampaignCommandKey;
using Pointer = CampaignCommandPointerEvent;
size_t checks=0;
bool wglVisual=false;
void require(bool ok, const std::string& message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
BattleStartParams fixture(size_t missionId=8, size_t allies=1) {
    BattleStartParams p;
    p.terrainScenarioPath="Sorted Original Files/DAT/SNARIO.DAT";
    p.terrainScenarioIndex=2;
    const auto setup=decodeOriginalBattlefieldSetup(p.terrainScenarioPath,2,originalBattlePlacementSelectorForMission(missionId),500.0);
    p.mission=battleMissionBriefingFromDefinition(*battleMissionDefinitionById(missionId));
    p.setupMetadata=setup.metadata; p.objective=setup.objective; p.battlefieldBoundary=setup.battlefieldBoundary;
    p.playerStartTransform={60000,0,30000,0};
    p.combatAiPolicy=BattleCombatAiPolicy::ReplacementDeterministicCombatAi;
    p.mechSystemsSnapshotEnabled=true;
    p.deterministicCombatRuntimeEnabled=true;
    p.individualWeaponRuntimeEnabled=true;
    p.originalProjectileRuntimeEnabled=true;
    p.originalHeatRuntimeEnabled=true;
    p.originalMajorSystemRuntimeEnabled=true;
    p.deterministicCollisionRuntimeEnabled=true;
    p.deterministicObjectiveRuntimeEnabled=false; // Wiki objective binding is performed by create().
    p.stationaryTargetHitDiagnosticEnabled=true;
    for(size_t i=0;i<allies;++i){
        BattleCombatantLaunchState a;
        a.roster.team=BattleTeam::Player; a.roster.sourceSlot="player:"+std::to_string(i+1);
        a.startTransform={62000,0,35000+1000.0*i,1.5707963267948966};
        p.playerAlliedLaunchStates.push_back(a);
    }
    BattleCombatantLaunchState e;
    e.roster.team=BattleTeam::Opposing; e.roster.sourceSlot="opposing:0";
    e.startTransform={66000,0,35000,0}; e.persistentMechState.valid=true; e.persistentMechState.engine=3;
    p.combatantLaunchStates.push_back(e);
    auto distant=e; distant.roster.sourceSlot="opposing:1"; distant.startTransform={95000,0,50000,0};
    p.combatantLaunchStates.push_back(distant);
    return p;
}
BattleInputCommand order(const BattleSnapshot& s, Kind kind, Target target=Target::None,
    Transform destination={}, EntityId targetId={}, size_t companion=1) {
    BattleInputCommand c; c.tickIndex=s.tickIndex; c.entityId=s.combatants.front().id;
    BattleLanceOrderCommand o; o.recipient=s.combatants[companion].id; o.issuer=c.entityId;
    o.issueTickIndex=s.tickIndex; o.kind=kind; o.targetKind=target; o.destination=destination; o.targetEntityId=targetId;
    c.lanceOrder=o; return c;
}
void issue(BattleWorld& w, Kind kind, Target target=Target::None, Transform dest={}, EntityId id={}, size_t companion=1) {
    auto c=order(w.snapshot(),kind,target,dest,id,companion);
    require(battleLanceOrderCommandValid(w.snapshot(),c),"fixture command rejected"); w.enqueueInput(c); w.tick();
    require(w.snapshot().combatants[companion].lanceOrder.sequence>0,"order not accepted");
}
double distance(const Transform& a,const Transform& b){return std::hypot(a.x-b.x,a.z-b.z);}
void untilArrived(BattleWorld& w,uint64_t limit=1000){
    while(limit-- && !w.snapshot().combatants[1].lanceOrder.arrived) w.tick();
    const auto s=w.snapshot();
    require(s.combatants[1].lanceOrder.arrived,"order failed to arrive x="+std::to_string(s.combatants[1].transform.x)+" z="+std::to_string(s.combatants[1].transform.z));
}
void savePpm(const std::filesystem::path& path,const std::vector<uint32_t>& p){
    std::ofstream out(path,std::ios::binary); out<<"P6\n320 200\n255\n";
    for(auto c:p){const char rgb[]={static_cast<char>(c>>16),static_cast<char>(c>>8),static_cast<char>(c)};out.write(rgb,3);}
    require(bool(out),"visual artifact write failed");
}
void commandAndReplayChecks(){
    auto p=fixture(8,3); auto w=BattleWorld::create(p); auto s=w.snapshot();
    auto c=order(s,Kind::MoveAvoid,Target::WorldPoint,{68000,0,35000,0}); c.tickIndex=5;
    const auto initial=battleSnapshotFingerprint(s); w.enqueueInput(c);
    auto changedDefault=s;changedDefault.combatants[1].lanceOrder.issued.kind=Kind::Ambush;
    require(battleSnapshotFingerprint(changedDefault)!=initial,"canonical no-order hash masked changed fields");
    require(battleSnapshotFingerprint(w.snapshot())!=initial,"pending order omitted from fingerprint");
    w.runTicks(5); require(w.snapshot().combatants[1].lanceOrder.sequence==0,"order accepted early");
    w.tick(); s=w.snapshot(); require(s.combatants[1].lanceOrder.acceptTickIndex==5,"wrong accept tick");
    require(s.combatants[1].lanceOrder.issued.issueTickIndex==0,"issue tick overwritten");
    for(size_t i=0;i<s.combatants.size();++i) if(i!=1) require(s.combatants[i].lanceOrder.sequence==0,"wrong recipient mutated");
    BattleReplay replay; replay.commands.push_back(c);
    for(size_t i=2;i<=3;++i){auto n=order(s,Kind::Defend,Target::WorldPoint,{68000,0,35000+1000.0*i,0},{},i);n.tickIndex=7;replay.commands.push_back(n);}
    const auto text=serializeBattleReplay(replay);
    const std::filesystem::path path="outputs/manual_lance_audit/manual_orders.replay";
    {std::ofstream out(path);out<<text;require(bool(out),"replay save failed");}
    std::ifstream in(path); const std::string saved{std::istreambuf_iterator<char>(in),{}};
    const auto restored=parseBattleReplay(saved); require(serializeBattleReplay(restored)==text,"replay roundtrip changed commands");
    const auto expected=runBattleReplay(p,replay,100);
    require(battleSnapshotFingerprint(expected)==battleSnapshotFingerprint(runBattleReplay(p,restored,100)),"saved replay differs");
    auto split=BattleWorld::create(p);for(auto& input:restored.commands)split.enqueueInput(input);
    split.runTicks(13);split.runTicks(29);split.runTicks(58);
    require(battleSnapshotFingerprint(expected)==battleSnapshotFingerprint(split.snapshot()),"run subdivision differs");
    auto rendered=BattleWorld::create(p);for(auto& input:restored.commands)rendered.enqueueInput(input);
    CampaignCommandScreenState ui;
    for(size_t i=0;i<100;++i){for(size_t j=0;j<i%7;++j){const auto frame=rendered.snapshot();campaignCommandMapOverlay(frame,ui);campaignCommandScreenKey(frame,ui,Key::Right,true);}rendered.tick();}
    require(battleSnapshotFingerprint(expected)==battleSnapshotFingerprint(rendered.snapshot()),"render cadence affected simulation");
    for(size_t i=1;i<=3;++i) require(expected.combatants[i].lanceOrder.sequence==1,"three companion replay missing recipient");
    const auto hash=battleSnapshotFingerprint(expected);
    const auto changed=[&](auto mutate){auto m=expected;mutate(m.combatants[1].lanceOrder);require(battleSnapshotFingerprint(m)!=hash,"manual order field not hashed");};
    changed([](auto&o){++o.sequence;});changed([](auto&o){++o.acceptTickIndex;});changed([](auto&o){++o.issued.issueTickIndex;});
    changed([](auto&o){++o.issued.issuer.value;});changed([](auto&o){++o.issued.recipient.value;});
    changed([](auto&o){o.issued.kind=Kind::Ambush;});changed([](auto&o){o.issued.targetKind=Target::Combatant;});
    changed([](auto&o){++o.issued.targetEntityId.value;});changed([](auto&o){o.issued.destination.x+=1;});changed([](auto&o){o.issued.destination.z+=1;});
    changed([](auto&o){o.phase=Phase::Waiting;});changed([](auto&o){o.reason=BattleLanceOrderReason::Spotted;});
    changed([](auto&o){o.activated=!o.activated;});changed([](auto&o){++o.activationTickIndex;});
    changed([](auto&o){o.arrived=!o.arrived;});changed([](auto&o){++o.arrivalTickIndex;});changed([](auto&o){++o.releaseTickIndex;});
    s=BattleWorld::create(p).snapshot(); const auto valid=order(s,Kind::AttackEnemy,Target::Combatant,{},s.combatants[4].id);
    require(battleLanceOrderCommandValid(s,valid),"known enemy unavailable");
    const auto invalid=[&](auto mutate){auto bad=valid;mutate(*bad.lanceOrder);require(!battleLanceOrderCommandValid(s,bad),"invalid order accepted by validator");auto world=BattleWorld::create(p);world.enqueueInput(bad);world.tick();require(world.snapshot().combatants[1].lanceOrder.sequence==0,"invalid order applied");};
    invalid([](auto&o){o.kind=static_cast<Kind>(255);});invalid([](auto&o){o.targetKind=static_cast<Target>(255);});
    invalid([&](auto&o){o.recipient=s.combatants[0].id;});invalid([&](auto&o){o.recipient=s.combatants[4].id;});
    invalid([&](auto&o){o.targetEntityId=s.combatants[1].id;});invalid([&](auto&o){o.targetEntityId=s.combatants[5].id;});
    invalid([](auto&o){o.targetEntityId={9999};});invalid([](auto&o){o.destination.x=1;});
    invalid([](auto&o){o.destination.x=std::numeric_limits<double>::quiet_NaN();});invalid([](auto&o){o.issueTickIndex=99;});
    auto invalidEnvelope=valid;invalidEnvelope.entityId={};auto closed=BattleWorld::create(p);closed.enqueueInput(invalidEnvelope);closed.tick();require(closed.snapshot().combatants[1].lanceOrder.sequence==0,"invalid issuer envelope normalized into authority");
    auto ordered=BattleWorld::create(p);auto first=order(s,Kind::MoveAvoid,Target::WorldPoint,{65000,0,35000,0});
    auto last=order(s,Kind::Defend,Target::WorldPoint,{66000,0,35000,0});
    ordered.enqueueInput(first);ordered.enqueueInput(invalidEnvelope);ordered.enqueueInput(last);ordered.tick();
    require(ordered.snapshot().combatants[1].lanceOrder.sequence==2 && ordered.snapshot().combatants[1].lanceOrder.issued.kind==Kind::Defend,"same-tick last valid order did not win");
    auto dead=BattleWorld::create(p);dead.setMissionStatus(s.combatants[1].id,CombatantMissionStatus::Disabled);dead.enqueueInput(valid);dead.tick();require(dead.snapshot().combatants[1].lanceOrder.sequence==0,"inactive recipient accepted");
    bool rejected=false;try{parseBattleReplay(text+"bad\n");}catch(const std::runtime_error&){rejected=true;}require(rejected,"corrupt replay accepted");
}
void behaviorChecks(){
    {
        auto p=fixture();p.playerAlliedLaunchStates[0].persistentMechState.valid=true;
        p.playerAlliedLaunchStates[0].persistentMechState.sensors=3;
        auto w=BattleWorld::create(p);issue(w,Kind::AttackEnemy,Target::Combatant,{},w.snapshot().combatants[2].id);
        w.setMissionStatus(w.playerEntityId(),CombatantMissionStatus::Disabled);w.tick();
        const auto o=w.snapshot().combatants[1].lanceOrder;
        require(o.phase==Phase::Released && o.reason==BattleLanceOrderReason::ContactLost,"lost radar contact not released");
    }
    {
        auto p=fixture();p.combatantLaunchStates[0].startTransform.x=70000;
        auto w=BattleWorld::create(p);issue(w,Kind::Ambush,Target::WorldPoint,{62000,0,35000,0});
        const auto s=w.snapshot();require(s.combatants[1].lanceOrder.activated && s.combatants[1].lanceOrder.reason==BattleLanceOrderReason::Spotted,"ambush spotted trigger missing");
    }
    for(const auto kind:{Kind::ActOnOwn,Kind::AttackEnemy}){
        auto p=fixture();p.combatantLaunchStates[0].startTransform.x=66000;
        auto second=p.combatantLaunchStates[0];second.roster.sourceSlot="opposing:2";second.startTransform.x=62700;p.combatantLaunchStates.push_back(second);
        auto w=BattleWorld::create(p);const auto target=w.snapshot().combatants[2].id;
        issue(w,kind,Target::Combatant,{},target);w.runTicks(10);
        require(w.snapshot().combatants[1].replacementAi.combatTargetEntityId==target,"manual target stolen by proximity assignment");
        w.setMissionStatus(target,CombatantMissionStatus::Destroyed);w.tick();
        require(w.snapshot().combatants[1].lanceOrder.phase==Phase::Released,"destroyed explicit target not released");
        require(w.snapshot().combatants[1].lanceOrder.issued.targetEntityId==target,"released order history lost");
        issue(w,Kind::ActOnOwn);require(w.snapshot().combatants[1].lanceOrder.phase==Phase::MissionDefault,"Act On Own did not restore mission AI");
    }
    for(const auto kind:{Kind::MoveAvoid,Kind::MoveAttack,Kind::Defend,Kind::Ambush}){
        auto p=fixture();p.combatantLaunchStates[0].startTransform={95000,0,45000,0};auto w=BattleWorld::create(p);
        const Transform destination{65000,0,35000,0};issue(w,kind,Target::WorldPoint,destination);untilArrived(w);
        const auto s=w.snapshot();require(distance(s.combatants[1].transform,destination)<=250,"wrong destination");
        require(s.combatants[1].weaponShotsFired==0,"travel acquired distant target");
        if(kind==Kind::Ambush) require(!s.combatants[1].lanceOrder.activated && s.combatants[1].lanceOrder.phase==Phase::Waiting,"ambush activated without trigger");
        w.runTicks(30);require(distance(w.snapshot().combatants[1].transform,destination)<=250,"arrived order drifted");
    }
    {
        auto p=fixture();p.combatantLaunchStates[0].startTransform={65000,0,35000,0};
        auto w=BattleWorld::create(p);issue(w,Kind::MoveAvoid,Target::WorldPoint,{67000,0,35000,0});untilArrived(w);
        require(w.snapshot().combatants[1].weaponShotsFired==0 && w.snapshot().combatants[1].replacementAi.targetKind==BattleReplacementAiTargetKind::None,"Move avoid acquired/fired");
    }
    {
        auto p=fixture();p.combatantLaunchStates[0].startTransform={65000,0,35000,0};auto w=BattleWorld::create(p);
        issue(w,Kind::MoveAttack,Target::WorldPoint,{68000,0,35000,0});require(w.snapshot().combatants[1].lanceOrder.phase==Phase::Engaging,"Move attack did not engage");
        const auto enemy=w.snapshot().combatants[2].id;w.setMissionStatus(enemy,CombatantMissionStatus::Destroyed);w.tick();untilArrived(w);
        require(w.snapshot().combatants[1].lanceOrder.issued.destination.x==68000,"Move attack lost route");
    }
    {
        auto p=fixture();p.combatantLaunchStates[0].priorMissionStatus=CombatantMissionStatus::Disabled;
        auto w=BattleWorld::create(p);const Transform home=p.playerAlliedLaunchStates[0].startTransform;
        issue(w,Kind::Defend,Target::WorldPoint,{home.x,0,home.z,0});require(w.snapshot().combatants[1].lanceOrder.phase==Phase::Waiting,"Defend did not wait");
        const auto enemy=w.snapshot().combatants[2].id;w.setMissionStatus(enemy,CombatantMissionStatus::Active);w.tick();
        require(w.snapshot().combatants[1].lanceOrder.phase==Phase::Engaging,"Defend ignored threat");
        w.runTicks(10);
        w.setMissionStatus(enemy,CombatantMissionStatus::Destroyed);w.tick();untilArrived(w);
        require(distance(w.snapshot().combatants[1].transform,home)<=250,"Defend failed return");
    }
    {
        auto p=fixture();p.combatantLaunchStates[0].startTransform={67000,0,35000,0};p.combatantLaunchStates[0].priorMissionStatus=CombatantMissionStatus::Disabled;
        auto second=p.combatantLaunchStates[0];second.roster.sourceSlot="opposing:2";second.startTransform.x=57000;p.combatantLaunchStates.push_back(second);
        auto w=BattleWorld::create(p);issue(w,Kind::Ambush,Target::WorldPoint,{62000,0,35000,0});require(!w.snapshot().combatants[1].lanceOrder.activated,"ambush premature");
        auto s=w.snapshot();w.setMissionStatus(s.combatants[2].id,CombatantMissionStatus::Active);w.setMissionStatus(s.combatants[4].id,CombatantMissionStatus::Active);w.tick();s=w.snapshot();
        require(s.combatants[1].lanceOrder.activated && s.combatants[1].replacementAi.combatTargetEntityId==s.combatants[2].id,"ambush nearest/id tie failed");
    }
    {
        auto p=fixture();p.enemyRetreatOnDamageEnabled=true;p.battlefieldBoundary.worldMinX=55000;p.battlefieldBoundary.worldMaxX=70000;
        p.battlefieldBoundary.worldMinZ=25000;p.battlefieldBoundary.worldMaxZ=45000;
        auto& a=p.playerAlliedLaunchStates[0];a.mechPresetId="warhammer";a.startTransform={69000,0,35000,1.5707963267948966};a.persistentMechState.valid=true;a.persistentMechState.armorDamage.fill(2);
        auto w=BattleWorld::create(p);issue(w,Kind::AttackEnemy,Target::Combatant,{},w.snapshot().combatants[2].id);
        require(w.snapshot().combatants[1].replacementAi.retreating,"manual order overrode damage retreat");
        w.runTicks(400);const auto s=w.snapshot();require(s.combatants[1].missionStatus==CombatantMissionStatus::Escaped,"retreat did not escape");
        require(s.combatants[1].lanceOrder.sequence==1 && s.combatants[1].lanceOrder.issued.kind==Kind::AttackEnemy,"retreat erased order history");
        require(s.combatants[0].missionStatus==CombatantMissionStatus::Active,"companion retreat counted as player flight");
    }
    {
        auto p=fixture();p.combatantLaunchStates[0].startTransform={95000,0,45000,0};
        BattleTerrainCollisionObstacle wall;wall.stableId=14;wall.centerX=64000;wall.centerZ=35000;wall.halfExtentX=600;wall.halfExtentZ=5000;
        wall.footprint={{63400,30000},{64600,30000},{64600,40000},{63400,40000}};
        p.terrainCollisionObstacles.valid=true;p.terrainCollisionObstacles.entries={wall};p.terrainCollisionObstacles.provenance="manual_order_wall_probe";
        auto w=BattleWorld::create(p);issue(w,Kind::MoveAvoid,Target::WorldPoint,{68000,0,35000,0});untilArrived(w,2000);
        require(w.snapshot().combatants[1].replacementAi.pathReplanCount>0,"manual order bypassed A*");
    }
    for(size_t missionId:{8u,24u,11u,0u,9u}){
        auto p=fixture(missionId);auto w=BattleWorld::create(p);issue(w,Kind::ActOnOwn);
        const auto ordinary=runBattleReplay(p,{},1);const auto manual=w.snapshot();
        require(manual.combatants[1].replacementAi.missionSlice==ordinary.combatants[1].replacementAi.missionSlice &&
            manual.combatants[1].replacementAi.targetKind==ordinary.combatants[1].replacementAi.targetKind,"Act On Own mission route changed");
        if(manual.objective.valid && battleObjectiveProtectedByPlayer(manual.objective)){
            auto c=order(manual,Kind::AttackEnemy,Target::Objective);require(!battleLanceOrderCommandValid(manual,c),"friendly objective allowed");
        }
    }
}
void weaponAndObjectiveChecks(){
    for(const auto kind:{Kind::AttackEnemy,Kind::ActOnOwn}) {
        auto p=fixture(24);p.objective.transform={64000,0,35000,0};p.combatantLaunchStates[0].startTransform={95000,0,45000,0};
        p.stationaryTargetHitDiagnosticEnabled=false;
        auto w=BattleWorld::create(p);issue(w,kind,Target::Objective);w.runTicks(200);
        auto s=w.snapshot();require(s.combatants[1].weaponShotsFired>0 && s.objective.damage>0,"manual structure target did not damage through common pipeline");
    }
    for(int gate=0;gate<4;++gate){
        auto p=fixture();p.combatantLaunchStates[0].startTransform.x=62800;
        auto& a=p.playerAlliedLaunchStates[0];a.persistentMechState.valid=true;
        if(gate==0)a.persistentMechState.weaponConditions.fill(3);
        if(gate==1){a.persistentMechState.weaponConditions[0]=3;a.ammunitionStateValid=true;a.ammunitionByPool.fill(0);}
        if(gate==2)a.persistentMechState.engine=3;
        if(gate==3)p.playerStartTransform={62400,0,35000,0};
        auto w=BattleWorld::create(p);issue(w,Kind::AttackEnemy,Target::Combatant,{},w.snapshot().combatants[2].id);
        require(w.snapshot().combatants[1].weaponShotsFired==0,"manual order bypassed weapon gate "+std::to_string(gate));
        if(gate==3)require(w.snapshot().combatants[1].replacementAi.friendlyLaneBlocked,"manual friendly lane not evaluated");
    }
    {
        auto p=fixture();p.stationaryTargetHitDiagnosticEnabled=false;p.combatantLaunchStates[0].mechPresetId="battlemaster";
        p.combatantLaunchStates[0].startTransform.x=64500;
        auto& a=p.playerAlliedLaunchStates[0];a.mechPresetId="warhammer";a.persistentMechState.valid=true;
        a.persistentMechState.heatSinksWorking=0;a.persistentMechState.heatSinksTotal=18;
        auto w=BattleWorld::create(p);uint64_t lastShot=0;bool fired=false;uint64_t previous=0;
        for(int i=0;i<160;++i){
            const auto before=w.snapshot();auto c=order(before,Kind::AttackEnemy,Target::Combatant,{},before.combatants[2].id);
            w.enqueueInput(c);w.tick();const auto s=w.snapshot();const auto& actor=s.combatants[1];
            if(actor.weaponShotsFired>previous){require(!fired || before.tickIndex-lastShot>=4,"reissuing orders bypassed fire cadence");lastShot=before.tickIndex;fired=true;previous=actor.weaponShotsFired;}
            require(actor.heat.rawHeat<0x5a0,"manual order bypassed heat gate");
            for(const auto& weapon:actor.weapons) if(weapon.cooldownTicksRemaining>0)
                require(weapon.cooldownTicksRemaining<=weapon.cooldownTicksOnFire,"invalid weapon cooldown");
        }
        require(fired,"manual mobile combat never fired");
    }
    {
        auto p=fixture(8,2);p.combatantLaunchStates[0].startTransform={95000,0,45000,0};
        p.playerAlliedLaunchStates[1].startTransform.z=35010;
        auto w=BattleWorld::create(p);auto s=w.snapshot();
        w.enqueueInput(order(s,Kind::MoveAvoid,Target::WorldPoint,{66000,0,35000,0},{},1));
        w.enqueueInput(order(s,Kind::MoveAvoid,Target::WorldPoint,{66000,0,35000,0},{},2));w.runTicks(50);s=w.snapshot();
        require(distance(s.combatants[1].transform,s.combatants[2].transform)>=s.combatants[1].collisionRadiusWorld+s.combatants[2].collisionRadiusWorld,"manual orders bypassed mech separation");
    }
    {
        auto p=fixture();p.combatantLaunchStates[0].startTransform={95000,0,45000,0};
        BattleTerrainCollisionObstacle obstacle;obstacle.stableId=90;obstacle.centerX=65000;obstacle.centerZ=35000;obstacle.halfExtentX=1000;obstacle.halfExtentZ=1000;
        obstacle.footprint={{64000,34000},{66000,34000},{66000,36000},{64000,36000}};
        p.terrainCollisionObstacles.valid=true;p.terrainCollisionObstacles.entries={obstacle};
        auto w=BattleWorld::create(p);issue(w,Kind::MoveAvoid,Target::WorldPoint,{65000,0,35000,0});w.runTicks(500);
        const auto s=w.snapshot();require(!s.combatants[1].lanceOrder.arrived && s.combatants[1].replacementAi.pathFailureCount>1 && s.combatants[1].replacementAi.navMode!=BattleReplacementAiNavMode::RouteBlocked,"unreachable order did not retain continuous recovery");
    }
}
void mouseInputChecks(){
    auto p=fixture(24,3);p.objective.transform={68000,0,35000,0};
    auto w=BattleWorld::create(p);w.tick();const auto s=w.snapshot();
    const auto before=battleSnapshotFingerprint(s);
    CampaignCommandScreenState ui;
    const auto point=[&](Pointer event,int x,int y){return campaignCommandScreenPointer(s,ui,event,x,y);};
    const auto rect=campaignCockpitCommandMapRect();const auto& b=s.battlefieldBoundary;
    const double cx=(b.worldMinX+b.worldMaxX)/2,cz=(b.worldMinZ+b.worldMaxZ)/2;
    const auto pixel=[&](const Transform& t){return std::pair<int,int>{rect.x+rect.width/2+static_cast<int>(std::lround((t.x-cx)/500)),
        rect.y+rect.height/2+static_cast<int>(std::lround((t.z-cz)/500))};};
    for(int i=0;i<3;++i){
        point(Pointer::Select,20+i*106,131);
        require(ui.selectedCompanion==s.combatants[1+i].id && ui.page==CampaignCommandPage::Crew,"mouse crew panel selected wrong actor");
    }
    const auto ally=pixel(s.combatants[1].transform);point(Pointer::Select,ally.first,ally.second);
    require(ui.selectedCompanion==s.combatants[1].id,"mouse companion marker selection failed");
    const auto player=pixel(s.combatants[0].transform);point(Pointer::Select,player.first,player.second);
    require(ui.selectedCompanion==s.combatants[1].id,"mouse selected player");
    const auto enemy=pixel(s.combatants[4].transform);point(Pointer::Select,enemy.first,enemy.second);
    require(ui.selectedCompanion==s.combatants[1].id,"mouse selected enemy recipient");
    const auto openMove=[&]{
        point(Pointer::Select,20,158);require(ui.page==CampaignCommandPage::Orders,"mouse order row failed");
        point(Pointer::Select,20,170);require(ui.page==CampaignCommandPage::Location && ui.draft.kind==Kind::MoveAvoid,"mouse order list failed");
    };
    openMove();
    const auto destination=pixel({68000,0,38000,0});
    require(!point(Pointer::Move,destination.first,destination.second).command,"mouse hover issued command");
    require(ui.draft.destination.x==cx+(destination.first-rect.x-rect.width/2)*500 &&
        ui.draft.destination.z==cz+(destination.second-rect.y-rect.height/2)*500,"mouse/world projection mismatch");
    const auto overlay=campaignCommandMapOverlay(s,ui);
    const auto pixels=campaignCommandOverlayPixels(overlay,cx,cz,rect.width,rect.height);
    require(overlay.targeting && pixels[(destination.second-rect.y-3)*rect.width+destination.first-rect.x]==0xffffff55u,"mouse yellow box disagrees with pointer");
    const auto saved=ui.draft.destination;
    require(!point(Pointer::Select,rect.x-1,rect.y+10).command && distance(saved,ui.draft.destination)==0,"outside-map click changed draft");
    campaignCommandScreenKey(s,ui,Key::Right);
    require(ui.draft.destination.x==saved.x+500,"keyboard stopped working after mouse");
    auto keyboardUi=ui;
    const auto keyboard=campaignCommandScreenKey(s,keyboardUi,Key::Enter);
    const auto moved=pixel(ui.draft.destination);
    const auto confirmed=point(Pointer::Select,moved.first,moved.second);
    require(confirmed.command && keyboard.command && serializeBattleReplay(BattleReplay{{*confirmed.command}})==
        serializeBattleReplay(BattleReplay{{*keyboard.command}}),"mouse and keyboard confirmation differ");
    require(battleSnapshotFingerprint(w.snapshot())==before,"mouse mutated world before tick");
    w.enqueueInput(*confirmed.command);
    require(w.snapshot().tickIndex==s.tickIndex && w.snapshot().combatants[1].lanceOrder.sequence==0,
        "mouse command accepted before tick");
    w.tick();const auto accepted=w.snapshot();
    require(accepted.combatants[1].lanceOrder.issued.kind==Kind::MoveAvoid &&
        distance(accepted.combatants[1].lanceOrder.issued.destination,ui.draft.destination)==0,"mouse order failed fixed-tick acceptance");
    for(size_t i=0;i<accepted.combatants.size();++i)if(i!=1)require(accepted.combatants[i].lanceOrder.sequence==0,"mouse issued to another actor");
    openMove();point(Pointer::Move,rect.x,rect.y);
    require(ui.draft.destination.x>=b.worldMinX && ui.draft.destination.z>=b.worldMinZ,"mouse destination outside battlefield");
    point(Pointer::Move,rect.x+rect.width-1,rect.y+rect.height-1);
    require(ui.draft.destination.x<=b.worldMaxX && ui.draft.destination.z<=b.worldMaxZ,"mouse destination past battlefield");
    const auto cancel=point(Pointer::Back,160,60);
    require(!cancel.command && !cancel.cockpit && ui.page==CampaignCommandPage::Orders,"mouse cancel committed draft");
    point(Pointer::Select,20,170);point(Pointer::Select,126,131);
    require(ui.selectedCompanion==s.combatants[2].id && ui.page==CampaignCommandPage::Crew && ui.draft.targetKind==Target::None,"crew switch retained draft");
    point(Pointer::Select,20,131);point(Pointer::Select,20,158);point(Pointer::Select,20,153);
    require(ui.page==CampaignCommandPage::Target && ui.draft.kind==Kind::AttackEnemy,"mouse attack selection failed");
    const auto unknown=pixel(s.combatants[5].transform);
    require(!point(Pointer::Select,unknown.first,unknown.second).command,"mouse attacked unknown enemy");
    const auto objective=pixel(s.objective.transform);
    point(Pointer::Move,objective.first,objective.second);
    require(ui.draft.targetKind==Target::Objective,"mouse objective hover missing");
    const auto targeted=point(Pointer::Select,enemy.first,enemy.second);
    require(targeted.command && targeted.command->lanceOrder->targetKind==Target::Combatant && targeted.command->lanceOrder->targetEntityId==s.combatants[4].id,"mouse attack target failed");
    point(Pointer::Select,20,166);
    require(ui.draft.kind==Kind::ActOnOwn,"mouse Act On Own target row failed");
    require(point(Pointer::Select,objective.first,objective.second).command.has_value(),"mouse structure confirmation failed");
    auto inactive=s;inactive.combatants[2].missionStatus=CombatantMissionStatus::Destroyed;
    campaignCommandScreenPointer(inactive,ui,Pointer::Select,126,131);
    require(ui.selectedCompanion==s.combatants[1].id,"mouse selected destroyed companion");
    point(Pointer::Select,20,174);require(ui.page==CampaignCommandPage::Damage,"mouse damage row failed");
    point(Pointer::Select,20,145);require(ui.detailPage==1,"mouse detail paging failed");
    point(Pointer::Back,160,60);point(Pointer::Select,20,182);require(ui.page==CampaignCommandPage::Weapons,"mouse weapons row failed");
    point(Pointer::Back,160,60);require(point(Pointer::Back,160,60).cockpit,"mouse return to cockpit failed");
}
WNDPROC probeParentProc=nullptr;
UINT probeMouseMessage=0;
POINT probeMousePoint{};
LRESULT CALLBACK probeMouseProc(HWND window,UINT message,WPARAM wparam,LPARAM lparam){
    if(message==WM_MOUSEMOVE || message==WM_LBUTTONDOWN || message==WM_RBUTTONDOWN){
        probeMouseMessage=message;probeMousePoint={static_cast<short>(LOWORD(lparam)),static_cast<short>(HIWORD(lparam))};return 0;
    }
    return CallWindowProcW(probeParentProc,window,message,wparam,lparam);
}
void visualAndInputChecks(){
    auto p=fixture(24,3);p.terrainEnvironmentId=1;p.objective.transform={68000,0,35000,0};
    p.playerAlliedLaunchStates[0].startTransform={40000,0,30000,1.5707963267948966};
    p.playerAlliedLaunchStates[0].mechPresetId="phoenix_hawk";
    p.playerAlliedLaunchStates[1].mechPresetId="jenner";
    auto w=BattleWorld::create(p);w.tick();const auto s=w.snapshot();
    CampaignCommandScreenState ui;
    campaignCommandScreenKey(s,ui,Key::Right);require(ui.selectedCompanion==s.combatants[2].id,"second crew selection");
    campaignCommandScreenKey(s,ui,Key::Right);require(ui.selectedCompanion==s.combatants[3].id,"third crew selection");
    campaignCommandScreenKey(s,ui,Key::Right);require(ui.selectedCompanion==s.combatants[1].id,"crew cycle included player/enemy");
    const auto before=battleSnapshotFingerprint(s);
    const auto scene=campaignBattleRenderSceneFromSnapshot(s,CampaignBattlePresentationMode::CockpitCommandMap,"Sorted Original Files");
    const auto backdrop=loadCampaignCockpitBackdrop(scene.cockpit);const auto layers=loadCampaignCockpitDynamicLayers(scene.cockpit);
    require(backdrop.valid && layers.valid,"MAP.SCR/font load failed: "+backdrop.reason+layers.reason);
    CampaignBattleGlViewport viewport;
    HWND parent=nullptr;
    if(wglVisual){
        parent=CreateWindowExW(0,L"STATIC",L"Manual orders render probe",WS_OVERLAPPEDWINDOW,0,0,320,200,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(parent && viewport.initialize(GetModuleHandleW(nullptr),parent),"WGL visual probe initialize failed");
        viewport.setBounds(RECT{0,0,197,107});
        require(viewport.updateScene(scene),"WGL scene load failed: "+viewport.lastError());
        probeParentProc=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(parent,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&probeMouseProc)));
        const HWND child=FindWindowExW(parent,nullptr,L"MWCampaignBattleGlViewport",nullptr);
        require(child!=nullptr,"map child missing for mouse routing probe");
        require(SendMessageW(child,WM_MOUSEACTIVATE,reinterpret_cast<WPARAM>(parent),0)==MA_ACTIVATE,
            "command map click would leave keyboard focus in another app");
        // Nonuniform integer pixel scaling and letterbox offset, as in the real main window.
        viewport.setBounds(RECT{18,27,609,455});
        for(UINT message:{WM_MOUSEMOVE,WM_LBUTTONDOWN,WM_RBUTTONDOWN}){
            probeMouseMessage=0;SendMessageW(child,message,0,MAKELPARAM(147,199));
            require(probeMouseMessage==message && probeMousePoint.x==165 && probeMousePoint.y==226,"GL child mouse coordinate forwarding failed");
        }
        auto outside=scene;outside.mode=CampaignBattlePresentationMode::External;
        require(viewport.updateScene(outside),"external scene for mouse isolation failed");
        require(SendMessageW(child,WM_MOUSEACTIVATE,reinterpret_cast<WPARAM>(parent),0)==MA_NOACTIVATE,
            "external camera mouse activation policy changed");
        probeMouseMessage=0;SendMessageW(child,WM_LBUTTONDOWN,0,MAKELPARAM(147,199));
        require(probeMouseMessage==0,"command pointer routing leaked into external view");
        require(viewport.updateScene(scene),"command scene restore failed");
        viewport.setBounds(RECT{0,0,197,107});
    }
    const auto capture=[&](const char* name){auto pixels=backdrop.bgraPixels;composeCampaignCommandPanels(pixels,layers.font,s,ui,{{"player:1","ELLI GUJAR"},{"player:2","TOM SEERS"},{"player:3","TARIS REN"}});
        const auto rect=campaignCockpitCommandMapRect();const auto& b=s.battlefieldBoundary;
        auto overlay=campaignCommandOverlayPixels(campaignCommandMapOverlay(s,ui),(b.worldMinX+b.worldMaxX)/2,(b.worldMinZ+b.worldMaxZ)/2,rect.width,rect.height);
        if(wglVisual){
            CampaignBattleGlCockpitOptions options;options.commandOverlay=campaignCommandMapOverlay(s,ui);
            for(const auto& actor:s.combatants)if(battleLanceOrderTargetKnown(s,actor.id))options.knownCommandMapEnemies.push_back(actor.id);
            viewport.setCockpitOptions(options);overlay=viewport.captureCommandMapPixels();
            require(overlay.size()==static_cast<size_t>(rect.width*rect.height),"WGL map readback failed");
        }
        for(size_t i=0;i<overlay.size();++i)if(overlay[i])pixels[(rect.y+i/rect.width)*320+rect.x+i%rect.width]=overlay[i];
        savePpm(std::filesystem::path("outputs/manual_lance_audit")/(std::string(wglVisual?"gl_":"")+name+".ppm"),pixels);
    };
    capture("crew");campaignCommandScreenKey(s,ui,Key::Enter);require(ui.page==CampaignCommandPage::Orders,"order list failed");capture("orders");
    for(int i=0;i<5;++i)campaignCommandScreenKey(s,ui,Key::Down);
    campaignCommandScreenKey(s,ui,Key::Enter);require(ui.page==CampaignCommandPage::Location,"location cursor failed");
    for(int i=0;i<25;++i)campaignCommandScreenKey(s,ui,Key::Right);capture("location");
    campaignCommandScreenPointer(s,ui,Pointer::Move,176,74);capture("mouse_location");
    auto confirmed=campaignCommandScreenKey(s,ui,Key::Enter);require(confirmed.command && confirmed.command->lanceOrder->kind==Kind::MoveAvoid,"UI confirmation failed");
    require(battleSnapshotFingerprint(w.snapshot())==before,"UI mutated world");w.enqueueInput(*confirmed.command);w.tick();require(w.snapshot().combatants[1].lanceOrder.issued.kind==Kind::MoveAvoid,"UI command not authoritative");
    ui={};campaignCommandScreenKey(s,ui,Key::Down);campaignCommandScreenKey(s,ui,Key::Enter);require(ui.page==CampaignCommandPage::Target,"Act On Own explicit target missing");capture("target");
    const auto target=ui.draft.targetEntityId;campaignCommandScreenKey(s,ui,Key::Down);require(ui.draft.targetKind==Target::Objective,"target cycle missing structure");
    campaignCommandScreenKey(s,ui,Key::Down);require(ui.draft.targetEntityId==target,"target cycle unstable");
    const auto cancel=campaignCommandScreenKey(s,ui,Key::Back);require(!cancel.command && !cancel.cockpit && ui.page==CampaignCommandPage::Crew,"target cancel failed");
    campaignCommandScreenKey(s,ui,Key::Down);campaignCommandScreenKey(s,ui,Key::Enter);capture("damage");
    campaignCommandScreenKey(s,ui,Key::Back);campaignCommandScreenKey(s,ui,Key::Down);campaignCommandScreenKey(s,ui,Key::Enter);capture("weapons");
    require(campaignCommandScreenKey(s,ui,Key::Status).status,"G status transition");
    require(campaignCommandScreenKey(s,ui,Key::Back).cockpit,"root Back cockpit transition");
    require(!campaignCommandScreenKey(s,ui,Key::Enter,true).command && ui.page==CampaignCommandPage::Crew,"key repeat changed UI");
    require(campaignBattlePresentationPausesSimulation(CampaignBattlePresentationMode::CockpitCommandMap),"menu time affects battle");
    auto empty=s;for(auto& actor:empty.combatants)if(actor.roster.team==BattleTeam::Opposing)actor.missionStatus=CombatantMissionStatus::Destroyed;
    empty.objective.depleted=true;ui={};campaignCommandScreenKey(empty,ui,Key::Down);campaignCommandScreenKey(empty,ui,Key::Enter);
    require(!campaignCommandScreenKey(empty,ui,Key::Enter).command,"empty target selection silently issued default order");
    viewport.shutdown();if(parent)DestroyWindow(parent);
}
}
int main(int argc,char** argv){try{
    wglVisual=argc==2 && std::string(argv[1])=="--visual-gl";
    std::filesystem::create_directories("outputs/manual_lance_audit");
    commandAndReplayChecks();behaviorChecks();weaponAndObjectiveChecks();mouseInputChecks();visualAndInputChecks();
    std::cout<<"mw_manual_lance_orders_smoke: ok checks="<<checks<<" orders=6 replay=roundtrip mouse=yes visual_states=7 wgl="<<(wglVisual?"yes":"no")<<"\n";return 0;
}catch(const std::exception&e){std::cerr<<"manual orders: "<<e.what()<<"\n";return 1;}}
