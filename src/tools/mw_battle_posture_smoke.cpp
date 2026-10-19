#include "battle/battle_world.h"
#include "battle/campaign_mech_persistence.h"
#include "mech3d/mech_catalog.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cmath>

using namespace mw::battle;
namespace {
size_t checks=0;
void check(bool ok, const char* message) { ++checks; if (!ok) throw std::runtime_error(message); }
BattleStartParams params() {
    BattleStartParams p;
    applyBattleDriveRuntimeTuning(p);
    p.playerMechPresetId="jenner";
    p.playerStartTransform.y=3000;
    p.constrainToTerrainBounds=false;
    p.cockpitCameraHeight=450;
    return p;
}
void oracle() {
    std::ifstream input("research/fixtures/original_btech_posture.txt");
    check(input.good(),"missing original posture fixture");
    std::string line; size_t rows=0;
    while (std::getline(input,line)) {
        if (line.empty() || line[0]=='#') continue;
        std::istringstream row(line); char type; row>>type;
        if (type=='U') {
            int p,l,r,la,ra,roll,flag; row>>p>>l>>r>>la>>ra>>roll>>flag;
            check(originalPlayerStandUpFlag(p,l,r,la,ra,roll)==flag,"original U decision mismatch");
        } else if (type=='L') {
            int v,a,b,p,flag,ld,rd,after; row>>v>>a>>b>>p>>flag>>ld>>rd>>after;
            const bool hard=originalHardLanding(static_cast<int16_t>(v));
            check((hard?4:6)==p && (hard?1:32)==flag && after==0,"original landing threshold mismatch");
            check((hard?originalLandingLegDamage(static_cast<uint8_t>(a)):0)==ld &&
                  (hard?originalLandingLegDamage(static_cast<uint8_t>(b)):0)==rd,"original landing damage mismatch");
        } else if (type=='C') {
            int seq,frame,y,z,p; row>>seq>>frame>>y>>z>>p;
            const auto pose=originalPostureCameraFrame(static_cast<uint8_t>(seq),static_cast<uint8_t>(frame));
            check(pose.forward==y && pose.height==z && pose.pitch==p,"original camera table mismatch");
        } else if (type=='T') {
            int seq,frame,next,nextFrame,pose; row>>seq>>frame>>next>>nextFrame>>pose;
            BattlePostureRuntime s; s.sequence=static_cast<uint8_t>(seq);s.frame=static_cast<uint8_t>(frame);
            s.posture=seq==4?BattlePosture::Falling:BattlePosture::Prone;
            advanceBattlePosture(s,1);
            check(s.sequence==next && s.frame==nextFrame && static_cast<int>(s.posture)==pose,"original camera step mismatch");
        } else if (type=='A') {
            std::string preset;int seq,frame;row>>preset>>seq>>frame;
            const auto a=mw::mech3d::makeCatalogAssemblyFrameAnimationDefinition(preset,
                seq==4?"fall":seq==5?"stand_up":"stand_fail");
            for (int slot=0;slot<6;++slot) { int record;row>>record;
                check(a.frames.at(frame).sourceRecordIndices.at(slot)==record,"original assembly frame mismatch"); }
        } else throw std::runtime_error("unknown posture oracle row");
        check(!row.fail(),"malformed posture oracle"); ++rows;
    }
    check(rows==8978,"truncated posture oracle");
}
void runtime() {
    auto p=params();auto world=BattleWorld::create(p);
    const auto initial=world.snapshot().combatants.at(0);
    BattleReplay replay;
    auto tick=[&](bool up=false, bool drive=false) {
        BattleInputCommand c;c.tickIndex=world.tickIndex();c.standUp=up;
        c.throttle=drive?1.0:0.0;c.turn=drive?1.0:0.0;c.jumpJetToggle=drive;
        replay.commands.push_back(c);world.enqueueInput(c);world.tick();return world.snapshot();
    };
    while (!world.snapshot().combatants.at(0).hardLanding && world.tickIndex()<500) tick();
    auto landed=world.snapshot();auto actor=landed.combatants.at(0);
    check(actor.posture.posture==BattlePosture::Falling && !actor.mechDestroyed,"hard landing did not cause recoverable fall");
    check(actor.forwardSpeed==0 && actor.missionStatus==CombatantMissionStatus::Active,"fall changed mission life status");
    for (auto leg : {mw::mech3d::MechArmorSectionId::LeftLeg,mw::mech3d::MechArmorSectionId::RightLeg}) {
        const size_t i=static_cast<size_t>(leg);
        const int delta=initial.detailedDamage.armorSections[i].armorRemaining-actor.detailedDamage.armorSections[i].armorRemaining;
        check(delta>=3 && delta<=6,"landing failed to damage each leg");
    }
    const auto ammo=actor.ammunitionByPool;
    const auto x=actor.transform.x,z=actor.transform.z,heading=actor.transform.headingRadians;
    for(int i=0;i<12;++i)tick(false,true);
    auto prone=world.snapshot();actor=prone.combatants.at(0);
    check(actor.posture.posture==BattlePosture::Prone && actor.posture.frame==5,"fall did not settle");
    check(actor.transform.x==x && actor.transform.z==z && actor.transform.headingRadians==heading && !actor.airborne,"fallen mech drove/turned/jumped");
    check(prone.camera.localHeight<p.cockpitCameraHeight && prone.camera.posturePitchRadians>1.5,"fallen camera did not follow source pose");
    check(actor.ammunitionByPool==ammo && actor.weaponHitsLanded==0,"landing changed ammunition or weapon hit stats");
    // Transient posture never crosses the campaign boundary, exact damage does.
    auto next=params();next.playerStartTransform.y=0;
    BattleCombatantLaunchState launch;launch.mechPresetId="jenner";launch.startTransform=next.playerStartTransform;
    launch.persistentMechState=campaignMechStateFromCombatant(actor);next.playerLaunchState=launch;
    auto reentry=BattleWorld::create(next).snapshot();
    check(reentry.combatants[0].posture.posture==BattlePosture::Upright && reentry.combatants[0].posture.falls==0,"posture leaked into campaign reentry");
    check(persistentMechDamageFingerprint(*launch.persistentMechState.exactDamage)==
          persistentMechDamageFingerprint(*campaignMechStateFromCombatant(reentry.combatants[0]).exactDamage),"landing exact damage lost at reentry");
    // Normal movement input and repeated U cannot restart an in-progress successful rise.
    bool sawFailure=false,sawRise=false;
    for (int attempt=0;attempt<40 && world.snapshot().combatants[0].posture.posture!=BattlePosture::Upright;++attempt) {
        auto s=tick(true);sawFailure|=s.combatants[0].posture.sequence==7;sawRise|=s.combatants[0].posture.sequence==5;
        for(int frame=0;frame<7;++frame)tick();
    }
    actor=world.snapshot().combatants[0];
    check(sawRise && actor.posture.posture==BattlePosture::Upright,"U never recovered player");
    check(world.snapshot().camera.posturePitchRadians==0 && world.snapshot().camera.localHeight==p.cockpitCameraHeight,"camera failed to recover");
    const auto parsed=parseBattleReplay(serializeBattleReplay(replay));
    check(parsed.commands.size()==replay.commands.size(),"posture replay lost commands");
    check(battleSnapshotFingerprint(runBattleReplay(p,parsed,world.tickIndex()))==battleSnapshotFingerprint(world.snapshot()),"posture replay differs");
    check(!parseBattleReplay("MW_BATTLE_REPLAY 2\n0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n").commands[0].standUp,"v2 replay synthesized U");
    // Both random outcomes and denied damaged-leg attempts are also instruction-oracle covered.
    BattlePostureRuntime state;beginBattleFall(state,0);for(uint64_t t=1;t<=6;++t)advanceBattlePosture(state,t);
    attemptBattleStandUp(state,7,1,1,1,1,11);
    check(state.sequence==7 && state.standAttempts==1,"failed U missing");
    for(uint64_t t=8;t<=13;++t)advanceBattlePosture(state,t);
    check(state.posture==BattlePosture::Prone && state.frame==5,"failed rise did not settle again");
    attemptBattleStandUp(state,14,1,1,0,1,0);
    check(state.standAttempts==1 && state.sequence==4,"broken actuator allowed U");
    attemptBattleStandUp(state,15,1,1,1,1,0);
    check(state.sequence==5 && state.posture==BattlePosture::Prone,"successful U missing");
    const auto attempts=state.standAttempts;
    attemptBattleStandUp(state,16,1,1,1,1,11);
    check(state.sequence==5 && state.frame==0 && state.changedTick==15 && state.standAttempts==attempts+1,
          "repeated U restarted rise or lost original RNG consumption");
    for(uint64_t t=16;t<=20;++t)advanceBattlePosture(state,t);
    check(state.posture==BattlePosture::Upright,"successful rise did not finish");
    // Soft landing must remain free of fall damage.
    auto soft=params();soft.playerStartTransform.y=1;auto w=BattleWorld::create(soft);w.tick();
    check(w.snapshot().combatants[0].posture.falls==0 && !w.snapshot().combatants[0].hardLanding,"soft landing fell");
    std::cout<<" replay_ticks="<<world.tickIndex()<<" observed_random_failure="<<sawFailure;
}
void collisionOracle() {
    std::ifstream input("research/fixtures/original_btech_collision_posture.txt");
    check(input.good(), "missing collision/recovery oracle");
    std::string line; size_t rows=0;
    while(std::getline(input,line)) {
        if(line.empty() || line[0]=='#') continue;
        std::istringstream row(line); char kind; row>>kind;
        if(kind=='C') {
            int a,b,sa,sb,h,status,fa,fb;row>>a>>b>>sa>>sb>>h>>status>>fa>>fb;
            const auto result=originalCollisionFall(a,b,static_cast<int16_t>(sa),static_cast<int16_t>(sb),static_cast<uint16_t>(h),0,status);
            check(result.moverFalls==(fa!=0) && result.otherFalls==(fb!=0),"original collision fall mismatch");
        } else if(kind=='R') {
            int p,status,l,r,la,ra,t,roll,flag,draws;row>>p>>status>>l>>r>>la>>ra>>t>>roll>>flag>>draws;
            const bool due=originalAiStandUpDue(p,status,l,r,la,ra,t);
            check(due==(draws!=0) && (due?(roll<2?2:64):0)==flag,"original AI recovery gate mismatch");
        } else if(kind=='F') {
            int seq,frame,pose,flag,ns,nf,np;row>>seq>>frame>>pose>>flag>>ns>>nf>>np;
            // Reachable fall/rise/failure states; other source rows remain audit data.
            const bool reachable=(seq==4 && pose==(frame==4?1:4)) ||
                (seq==5 && frame<4 && pose==5) || (seq==7 && frame<4 && pose==1);
            if(reachable && (flag==0 || (pose==1 && (flag==2 || flag==64)))) {
                BattlePostureRuntime s;s.sequence=static_cast<uint8_t>(seq);s.frame=static_cast<uint8_t>(frame);s.posture=static_cast<BattlePosture>(pose);
                updateAiBattlePosture(s,8,flag?0:-1,1,1,1,1,flag==64?4:0);
                check(s.sequence==ns && s.frame==nf && static_cast<int>(s.posture)==np,"original actor FSM mismatch");
            }
        } else if(kind=='L') {
            int l,r,h,flag,heading;row>>l>>r>>h>>flag>>heading;
            check(flag==((!l||!r)?1:0) && heading==((!l||!r)?((h+0x7ff8)&65535):0),"original leg fall gate mismatch");
        } else throw std::runtime_error("unknown collision oracle row");
        check(!row.fail(),"malformed collision oracle");++rows;
    }
    check(rows==21327,"truncated collision oracle");
}
void collisionRuntime() {
    auto p=params();p.playerMechPresetId="locust";p.playerStartTransform={};
    p.deterministicCollisionRuntimeEnabled=true;
    p.combatAiPolicy=BattleCombatAiPolicy::Disabled;
    p.playerRoster.team=BattleTeam::Player;
    BattleCombatantLaunchState enemy;enemy.mechPresetId="locust";
    enemy.roster.team=BattleTeam::Opposing;enemy.roster.pilotingSkill=3;
    enemy.startTransform={0,0,1800,3.14159265358979323846};
    p.combatantLaunchStates.push_back(enemy);
    auto w=BattleWorld::create(p);bool fell=false,rose=false;
    for(int i=0;i<600;++i) {
        BattleInputCommand c;c.tickIndex=w.tickIndex();c.throttle=fell?0:1;w.enqueueInput(c);w.tick();
        const auto s=w.snapshot();
        if(s.combatants[1].posture.falls)fell=true;
        if(fell && s.combatants[1].posture.standAttempts && s.combatants[1].posture.posture==BattlePosture::Upright){rose=true;break;}
    }
    if (!fell || !rose) {
        for (const auto& c : w.snapshot().combatants) std::cerr << " actor=" << c.mechPresetId << " z=" << c.transform.z << " v=" << c.forwardSpeed << " contacts=" << c.collisionCount << " falls=" << c.posture.falls << " attempts=" << c.posture.standAttempts;
    }
    check(fell && rose,"world collision/AI recovery failed");
    p.combatantLaunchStates[0].roster.team=BattleTeam::Player;
    p.playerAlliedLaunchStates=p.combatantLaunchStates;p.combatantLaunchStates.clear();
    auto friendly=BattleWorld::create(p);
    for(int i=0;i<100;++i){BattleInputCommand c;c.tickIndex=friendly.tickIndex();c.throttle=1;friendly.enqueueInput(c);friendly.tick();}
    check(friendly.snapshot().combatants[1].posture.falls==0,"friendly collision caused fall");
    // Exact launch with a depleted leg: no generic fatal component marker.
    auto base=BattleWorld::create(p).snapshot();
    auto state=campaignMechStateFromCombatant(base.combatants[1]);
    state.exactDamage->detailed.internalSections[static_cast<size_t>(mw::mech3d::MechInternalSectionId::LeftLeg)].structureRemaining=0;
    p.playerAlliedLaunchStates[0].persistentMechState=state;
    auto leg=BattleWorld::create(p);leg.runTicks(32);const auto fallen=leg.snapshot().combatants[1];
    check(!fallen.mechDestroyed && fallen.missionStatus==CombatantMissionStatus::Disabled,"leg loss killed chassis");
    check(fallen.posture.posture==BattlePosture::Prone && fallen.posture.standAttempts==0,"leg loss did not remain prone");
    auto returned=campaignMechStateFromCombatant(fallen);
    repairCampaignMechDamage(returned,"locust",CampaignMechRepairKind::ArmorSection,0);
    check(!returned.exactDamage->mechDestroyed && returned.exactDamage->mechDisabled,"unrelated repair reclassified leg loss as fatal");
    // Exercise actual damage application, including the old fatal-leg visual path.
    auto landing=params();auto intact=BattleWorld::create(landing).snapshot().combatants[0];
    BattleCombatantLaunchState fragile;fragile.mechPresetId="jenner";fragile.startTransform=landing.playerStartTransform;
    fragile.persistentMechState=campaignMechStateFromCombatant(intact);
    auto& d=fragile.persistentMechState.exactDamage->detailed;
    d.armorSections[static_cast<size_t>(mw::mech3d::MechArmorSectionId::LeftLeg)].armorRemaining=0;
    d.internalSections[static_cast<size_t>(mw::mech3d::MechInternalSectionId::LeftLeg)].structureRemaining=1;
    landing.playerLaunchState=fragile;auto hit=BattleWorld::create(landing);hit.runTicks(120);
    const auto broken=hit.snapshot().combatants[0];
    check(broken.detailedDamage.internalSections[static_cast<size_t>(mw::mech3d::MechInternalSectionId::LeftLeg)].structureRemaining==0,"landing did not destroy fragile leg");
    check(!broken.mechDestroyed && broken.posture.posture==BattlePosture::Prone,"actual leg hit still killed chassis or skipped fall");
    check(!broken.hiddenComponentIds.empty(),"lost leg geometry remained attached");
}
void aiLandingOracle() {
    std::ifstream input("research/fixtures/original_btech_ai_jump.txt");
    check(input.good(),"missing AI landing/jump source fixture");
    std::string line; size_t rows=0;
    while(std::getline(input,line)) {
        if(line.empty() || line[0]!='L') continue;
        std::istringstream row(line);char kind;
        int model,status,l,r,v,clearance,flag,afterV,afterHeight;
        row>>kind>>model>>status>>l>>r>>v>>clearance>>flag>>afterV>>afterHeight;
        const bool contact=clearance<0; // Oracle uses zero terrain height.
        check(flag==(contact?originalAiLandingFlag(status,l,r):0),"original AI landing decision mismatch");
        check(afterV==(contact?0:v) && afterHeight==(contact?0:clearance),"original AI contact clamp mismatch");
        check(!row.fail(),"malformed AI landing oracle");++rows;
    }
    check(rows==1920,"truncated AI landing oracle");
}
void allChassisLandings() {
    const char* models[]={"locust","jenner","phoenix_hawk","shadow_hawk","rifleman","warhammer","marauder","battlemaster"};
    const auto points=[](const CombatantSnapshot& c) {
        int result=0;
        for(const auto& a:c.detailedDamage.armorSections) result+=a.armorRemaining;
        for(const auto& s:c.detailedDamage.internalSections) result+=s.structureRemaining;
        return result;
    };
    for(const auto* model:models) for(const auto team:{BattleTeam::Player,BattleTeam::Opposing}) {
        auto p=params();p.playerMechPresetId=model;p.deterministicCollisionRuntimeEnabled=false;
        p.combatAiPolicy=BattleCombatAiPolicy::Disabled;
        BattleCombatantLaunchState ai;ai.mechPresetId=model;ai.roster.team=team;
        ai.startTransform={0,3000,0,0};
        if(team==BattleTeam::Player)p.playerAlliedLaunchStates.push_back(ai);
        else p.combatantLaunchStates.push_back(ai);
        auto w=BattleWorld::create(p);const auto before=w.snapshot();
        while(!w.snapshot().combatants[0].hardLanding && w.tickIndex()<200)w.tick();
        const auto landed=w.snapshot();
        const auto& player=landed.combatants[0];const auto& other=landed.combatants[1];
        const int expected=player.posture.leftLandingDamage+player.posture.rightLandingDamage;
        check(expected>=6 && expected<=12 && points(before.combatants[0])-points(player)==expected,
              "chassis-specific player landing damage mismatch");
        check(player.posture.falls==1 && player.knockdownCandidate,"player landing did not fall");
        check(other.hardLanding && !other.knockdownCandidate && other.posture.falls==0,
              "healthy AI inherited player hard-landing knockdown");
        check(points(before.combatants[1])==points(other),"AI inherited player landing damage");
        for(size_t i=0;i<2;++i) {
            check(landed.combatants[i].ammunitionByPool==before.combatants[i].ammunitionByPool &&
                  landed.combatants[i].weaponHitsLanded==0,"landing consumed ammunition or scored a hit");
            auto next=params();next.playerMechPresetId=model;
            BattleCombatantLaunchState returned;returned.mechPresetId=model;returned.startTransform={};
            returned.persistentMechState=campaignMechStateFromCombatant(landed.combatants[i]);
            next.playerLaunchState=returned;
            const auto restored=BattleWorld::create(next).snapshot().combatants[0];
            check(persistentMechDamageFingerprint(*returned.persistentMechState.exactDamage)==
                  persistentMechDamageFingerprint(*campaignMechStateFromCombatant(restored).exactDamage),
                  "chassis landing damage lost in campaign round trip");
        }
        w.runTicks(20);
        check(points(w.snapshot().combatants[0])==points(player),"settling into prone added invented impact damage");
        // Disabled AI must start falling at contact, even on a gentle landing.
        auto disabled=p;
        if(team==BattleTeam::Player)disabled.playerAlliedLaunchStates[0].startTransform.y=1;
        else disabled.combatantLaunchStates[0].startTransform.y=1;
        auto d=BattleWorld::create(disabled);const auto original=d.snapshot().combatants[1];
        d.setMissionStatus(original.id,CombatantMissionStatus::Disabled);
        for(int step=0;step<5 && d.snapshot().combatants[1].transform.y>0;++step)d.tick();
        const auto contact=d.snapshot().combatants[1];
        check(!contact.airborne && !contact.hardLanding && contact.knockdownCandidate &&
              contact.posture.posture==BattlePosture::Falling,"disabled AI soft landing failed to fall immediately");
        check(points(contact)==points(original),"AI disabled landing added unsupported damage");
    }
}
}
int main(){try{oracle();collisionOracle();aiLandingOracle();runtime();collisionRuntime();allChassisLandings();std::cout<<" mw_battle_posture_smoke: ok checks="<<checks<<"\n";return 0;}
catch(const std::exception& e){std::cerr<<"posture: "<<e.what()<<"\n";return 1;}}
