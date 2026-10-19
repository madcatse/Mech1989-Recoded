#include "battle/battle_setup.h"
#include "battle/mission_contract.h"
#include "battle/campaign_mech_persistence.h"
#include "mech3d/mech_catalog.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <cmath>

namespace {
using namespace mw::battle;
namespace mech=mw::mech3d;
size_t checks=0;
const char* models[]={"locust","jenner","phoenix_hawk","shadow_hawk","rifleman","warhammer","marauder","battlemaster"};
const int jets[]={0,3,2,3,0,0,0,0};
void check(bool ok,const std::string& why){++checks;if(!ok)throw std::runtime_error(why);}
void oracle(){
    std::ifstream input("research/fixtures/original_btech_ai_flight.txt");check(input.good(),"missing flight oracle");
    std::string line;size_t count=0;
    while(std::getline(input,line)){
        if(line.empty()||line[0]=='#')continue;
        std::istringstream r(line);char kind;r>>kind;
        if(kind=='F'){
            int model,h,v,f,fuel,mode,probe,status,left,right,missing,heading,desired,ev,ef,eu,et,eh;
            r>>model>>h>>v>>f>>fuel>>mode>>probe>>status>>left>>right>>missing>>heading>>desired>>ev>>ef>>eu>>et>>eh;
            OriginalAiFlightInput i;const auto m=mech::catalogMobility(models[model]);
            i.acceleration=static_cast<int16_t>(m.originalBtechJumpAccelerationWord0a);
            i.speedCap=static_cast<int16_t>(m.originalBtechJumpSpeedWord0c);
            i.height=h;i.vertical=static_cast<int16_t>(v);i.forward=static_cast<int16_t>(f);i.fuel=static_cast<int16_t>(fuel);
            i.movementMode=mode;i.terrainProbe=probe;i.status=status;i.leftLeg=left;i.rightLeg=right;i.missing=missing;
            i.installed=jets[model];i.heading=static_cast<uint16_t>(heading);i.desiredHeading=static_cast<uint16_t>(desired);
            const auto o=originalAiFlightControl(i);
            check(o.vertical==ev&&o.forward==ef&&o.fuel==eu&&o.turn==et&&o.heat==eh,"flight mismatch: "+line);
        }else if(kind=='D'){
            int v,f,t,ev,ef,et;r>>v>>f>>t>>ev>>ef>>et;
            auto vv=static_cast<int16_t>(v),ff=static_cast<int16_t>(f),tt=static_cast<int16_t>(t);
            originalAiFlightDrag(vv,ff,tt);check(vv==ev&&ff==ef&&tt==et,"drag mismatch: "+line);
        }else if(kind=='R'){
            int fuel,pose,expected;r>>fuel>>pose>>expected;
            check(originalAiJumpRecharge(static_cast<int16_t>(fuel),pose==3)==expected,"recharge mismatch: "+line);
        }else if(kind=='A'){
            int model,seq,frame;r>>model>>seq>>frame;
            const auto animation=mech::makeCatalogAssemblyFrameAnimationDefinition(models[model],seq==2?"jump_start":"jump_land");
            for(size_t part=0;part<6;++part){int record;r>>record;
                check(animation.frames.at(frame).sourceRecordIndices.at(part)==record,"original jump frame mismatch");}
        }else throw std::runtime_error("unknown oracle row");
        check(!r.fail(),"malformed flight oracle");++count;
    }
    check(count==14408,"truncated flight oracle");
    std::ifstream gates("research/fixtures/original_btech_ai_jump.txt");check(gates.good(),"missing jump gates oracle");count=0;
    while(std::getline(gates,line)){
        if(line.empty()||line[0]=='#'||line[0]=='L')continue;
        std::istringstream r(line);char kind;r>>kind;
        if(kind=='J'){
            int m,b,reverse,mode,pose,fuel,heat,distance,flag;r>>m>>b>>reverse>>mode>>pose>>fuel>>heat>>distance>>flag;
            check(originalAiObstacleJumpDue(b!=0,reverse,mode,jets[m],heat,pose,fuel,distance)==(flag==8),"obstacle gate: "+line);
        }else{
            int m,status,gyro,pose,fuel,distance,roll,missing,flag,draws;
            r>>m>>status>>gyro>>pose>>fuel>>distance>>roll>>missing>>flag>>draws;
            const bool due=originalAiDestinationJumpDue(jets[m],missing,status,gyro,pose,fuel,distance);
            check(due==(draws==1)&&((due&&roll==0)?8:0)==flag,"destination gate: "+line);
        }
        check(!r.fail(),"malformed gate oracle");++count;
    }
    check(count==5584,"truncated gate oracle");
}
BattleStartParams fixture(const char* model,BattleTeam team=BattleTeam::Player){
    BattleStartParams p;p.terrainScenarioPath="Sorted Original Files/DAT/SNARIO.DAT";p.terrainScenarioIndex=2;
    const auto setup=decodeOriginalBattlefieldSetup(p.terrainScenarioPath,2,originalBattlePlacementSelectorForMission(8),500.0);
    p.mission=battleMissionBriefingFromDefinition(*battleMissionDefinitionById(8));
    p.setupMetadata=setup.metadata;p.objective=setup.objective;p.battlefieldBoundary=setup.battlefieldBoundary;
    p.playerStartTransform={60000,0,30000,0};p.combatAiPolicy=BattleCombatAiPolicy::ReplacementDeterministicCombatAi;
    p.mechSystemsSnapshotEnabled=true;p.originalHeatRuntimeEnabled=true;p.originalMajorSystemRuntimeEnabled=true;
    p.deterministicCombatRuntimeEnabled=true;p.individualWeaponRuntimeEnabled=true;
    p.originalProjectileRuntimeEnabled=true;
    p.deterministicCollisionRuntimeEnabled=true;
    p.constrainToTerrainBounds=false;
    BattleCombatantLaunchState a;a.mechPresetId=model;a.roster.team=team;
    a.roster.sourceSlot=team==BattleTeam::Player?"player:1":"opposing:0";
    a.persistentMechState.valid=true;
    a.persistentMechState.heatSinksWorking=a.persistentMechState.heatSinksTotal=10;
    for(int m=0;m<8;++m)if(a.mechPresetId==models[m])
        a.persistentMechState.jumpJetsWorking=a.persistentMechState.jumpJetsTotal=static_cast<uint8_t>(jets[m]);
    a.startTransform={62000,0,35000,1.5707963267948966};
    if(team==BattleTeam::Player)p.playerAlliedLaunchStates.push_back(a);else p.combatantLaunchStates.push_back(a);
    BattleCombatantLaunchState e;e.roster.team=BattleTeam::Opposing;e.roster.sourceSlot="opposing:1";
    e.startTransform={95000,0,50000,0};e.persistentMechState.valid=true;e.persistentMechState.engine=3;
    p.combatantLaunchStates.push_back(e);return p;
}
void order(BattleWorld& w,Transform destination){
    const auto s=w.snapshot();BattleInputCommand c;c.tickIndex=s.tickIndex;c.entityId=s.combatants[0].id;
    BattleLanceOrderCommand o;o.recipient=s.combatants[1].id;o.issuer=c.entityId;o.issueTickIndex=s.tickIndex;
    o.kind=BattleLanceOrderKind::MoveAvoid;o.targetKind=BattleLanceTargetKind::WorldPoint;o.destination=destination;
    c.lanceOrder=o;check(battleLanceOrderCommandValid(s,c),"invalid test order state="+std::to_string(static_cast<int>(s.missionRuntimeState))+" maxX="+std::to_string(s.battlefieldBoundary.worldMaxX));w.enqueueInput(c);
}
uint64_t damageHash(const CombatantSnapshot& a){return persistentMechDamageFingerprint(*campaignMechStateFromCombatant(a).exactDamage);}
void runtime(){
    for(int model=0;model<8;++model){
        auto p=fixture(models[model]);auto w=BattleWorld::create(p);const auto before=w.snapshot().combatants[1];
        check(before.aiJump.enabled==(jets[model]!=0),"jet chassis activation");
        order(w,{84000,0,35000,0});
        double maxHeight=0;bool prepare=false,land=false,heated=false;size_t ticks=0;
        for(;ticks<2200;++ticks){
            w.tick();const auto a=w.snapshot().combatants[1];
            maxHeight=std::max(maxHeight,a.transform.y);prepare|=a.activeAnimationId=="jump_start";
            land|=a.activeAnimationId=="jump_land";heated|=a.heat.lastJumpJetHeatAdded>0;
            if(a.aiJump.landings>=1&&a.aiJump.phase==BattleAiJumpPhase::Ground)break;
            if(!jets[model]&&ticks>300)break;
        }
        const auto a=w.snapshot().combatants[1];
        if(jets[model]){
            std::cout<<models[model]<<" launches="<<a.aiJump.launches<<" landings="<<a.aiJump.landings<<" ticks="<<ticks<<" height="<<maxHeight<<" draws="<<a.aiJump.eligibleDraws<<" jets="<<int(a.detailedDamage.criticalComponents[9].workingCount)<<"/"<<int(a.detailedDamage.criticalComponents[9].totalCount)<<" active="<<a.replacementAi.active<<" move="<<a.aiJump.moveRequested<<" status="<<int(a.missionStatus)<<" x="<<a.transform.x<<"\n";
            check(a.aiJump.launches>=1&&a.aiJump.landings>=1&&maxHeight>1000&&prepare&&land&&heated,"AI flight lifecycle");
            check(!a.knockdownCandidate&&a.posture.falls==0,"healthy AI fell at landing");
            check(damageHash(a)==damageHash(before)&&a.ammunitionByPool==before.ammunitionByPool,"AI landing invented damage/ammo consumption");
            const auto saved=campaignMechStateFromCombatant(a);auto next=p;next.playerAlliedLaunchStates[0].persistentMechState=saved;
            const auto returned=BattleWorld::create(next).snapshot().combatants[1];
            check(returned.aiJump.launches==0&&returned.aiJump.fuel==800&&damageHash(returned)==damageHash(a),"transient jump state leaked to campaign");
            order(w,{60000,0,30000,0});
            for(size_t t=0;t<2200&&w.snapshot().combatants[1].aiJump.launches<2;++t)w.tick();
            {const auto q=w.snapshot().combatants[1];std::cout<<"repeat phase="<<int(q.aiJump.phase)<<" fuel="<<q.aiJump.fuel<<" heat="<<q.heat.rawHeat<<" draws="<<q.aiJump.eligibleDraws<<" move="<<q.aiJump.moveRequested<<" x="<<q.transform.x<<" state="<<int(w.snapshot().missionRuntimeState)<<"\n";}
            check(w.snapshot().combatants[1].aiJump.launches>=2,"AI cannot jump again");
        }else check(a.aiJump.launches==0&&maxHeight==0,"non-jet chassis jumped");
    }
    // Identical input histories must retain identical state independent of snapshots.
    auto p=fixture("jenner");auto a=BattleWorld::create(p),b=BattleWorld::create(p);
    order(a,{84000,0,35000,0});order(b,{84000,0,35000,0});
    for(int t=0;t<900;++t){a.tick();for(int j=0;j<t%7;++j)(void)b.snapshot();b.tick();}
    auto s=a.snapshot();const auto hash=battleSnapshotFingerprint(s);
    check(hash==battleSnapshotFingerprint(b.snapshot()),"jump replay drift");
    auto changed=s;changed.combatants[1].aiJump.fuel--;
    check(hash!=battleSnapshotFingerprint(changed),"raw AI fuel not hashed");
    changed=s;changed.combatants[1].aiJump.accumulator+=0.01;
    check(hash!=battleSnapshotFingerprint(changed),"AI cadence not hashed");
    // Opposing AI uses the same source flight path while pursuing the player.
    auto enemy=BattleWorld::create(fixture("jenner",BattleTeam::Opposing));
    for(int t=0;t<2200&&enemy.snapshot().combatants[1].aiJump.launches==0;++t)enemy.tick();
    check(enemy.snapshot().combatants[1].aiJump.launches>0,"opposing AI did not jump");
    // Close obstacle: destination RNG is ineligible, so this isolates the other gate.
    auto blocked=fixture("jenner");
    BattleTerrainCollisionObstacle wall;wall.stableId=901;wall.centerX=63800;wall.centerZ=35000;
    wall.halfExtentX=200;wall.halfExtentZ=2000;
    wall.footprint={{63600,33000},{64000,33000},{64000,37000},{63600,37000}};
    blocked.terrainCollisionObstacles.valid=true;blocked.terrainCollisionObstacles.entries={wall};
    auto obstacle=BattleWorld::create(blocked);order(obstacle,{66000,0,35000,0});
    obstacle.runTicks(3);auto os=obstacle.snapshot().combatants[1];
    check(os.aiJump.lastReason==BattleAiJumpReason::Obstacle && os.aiJump.eligibleDraws==0 &&
        os.aiJump.phase==BattleAiJumpPhase::Preparing,"obstacle request not connected");
    // An updated nearby destination cancels preparation without charging launch fuel.
    order(obstacle,{os.transform.x,0,os.transform.z,0});obstacle.runTicks(3);os=obstacle.snapshot().combatants[1];
    check(os.aiJump.launches==0&&os.aiJump.phase==BattleAiJumpPhase::Ground&&os.aiJump.fuel==800,"cancelled preparation launched");
    for(int failure=0;failure<2;++failure){
        auto damaged=fixture("jenner");auto& saved=damaged.playerAlliedLaunchStates[0].persistentMechState;
        if(failure==0)saved.jumpJetsWorking=0;else saved.gyros=3;
        auto dw=BattleWorld::create(damaged);order(dw,{84000,0,35000,0});dw.runTicks(200);
        check(dw.snapshot().combatants[1].aiJump.launches==0 && dw.snapshot().combatants[1].aiJump.eligibleDraws==0,
            "destination ignored damaged jets/gyro");
    }
    // In-flight disabling must still integrate descent, then use the AI fall flag.
    auto disabled=BattleWorld::create(fixture("jenner"));order(disabled,{84000,0,35000,0});
    while(disabled.tickIndex()<200&&disabled.snapshot().combatants[1].transform.y<1000)disabled.tick();
    auto ds=disabled.snapshot().combatants[1];check(ds.transform.y>=1000,"disable fixture never flew");
    disabled.setMissionStatus(ds.id,CombatantMissionStatus::Disabled);
    const auto health=damageHash(disabled.snapshot().combatants[1]);
    while(disabled.tickIndex()<1000&&disabled.snapshot().combatants[1].aiJump.landings==0)disabled.tick();
    ds=disabled.snapshot().combatants[1];
    check(ds.aiJump.landings==1&&ds.knockdownCandidate&&ds.posture.falls==1&&!ds.airborne,"disabled airborne AI failed to fall");
    check(damageHash(ds)==health,"disabled AI landing invented impact damage");
}
}
int main(){try{oracle();runtime();std::cout<<"mw_ai_jump_smoke: ok checks="<<checks<<"\n";return 0;}
catch(const std::exception& e){std::cerr<<"ai_jump: "<<e.what()<<"\n";return 1;}}
