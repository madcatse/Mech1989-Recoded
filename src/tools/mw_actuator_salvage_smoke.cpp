#include "battle/campaign_salvage.h"
#include "battle/campaign_mech_persistence.h"
#include "mech3d/mech_catalog.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace mw::battle;
namespace {
size_t checks=0, differences=0;
void check(bool ok,const char* why) {++checks;if(!ok) throw std::runtime_error(why);}
const std::array<const char*,8> presets{{"locust","jenner","phoenix_hawk","shadow_hawk","rifleman","warhammer","marauder","battlemaster"}};
const std::array<uint16_t,8> chassis{{0,2,3,4,6,7,8,9}};
void oracle() {
    std::ifstream input("research/fixtures/original_actuator_salvage.txt");check(input.good(),"missing actuator oracle");
    std::string line;size_t predicates=0,aggregates=0;
    while(std::getline(input,line)) {
        if(line.empty() || line[0]=='#')continue;
        std::istringstream row(line);char kind;row>>kind;
        if(kind=='P') {
            BattleOriginalAiAggregateDiagnosticInput in;int ll,rl,viable;
            in.occupied=true;
            row>>in.engineConditionWord11>>in.lifeSupportConditionWord0d>>in.opaqueWord15>>ll>>rl
               >>in.sharedCurrentControlStateWord51>>in.leftLegActuatorWord6c>>in.rightLegActuatorWord6e>>viable;
            in.internalWords[2]=static_cast<int16_t>(ll);in.internalWords[3]=static_cast<int16_t>(rl);
            const auto out=battleOriginalAiAggregate7070(in);
            check(out.exact && out.viability4106Passed==(viable!=0),"original 4112 predicate mismatch");++predicates;
        } else if(kind=='A') {
            int model,slot,pp,tp,la,ra,bind,boundary,failure,present,aggregate,originalChassis,quality,pointer;
            row>>model>>slot>>pp>>tp>>la>>ra>>bind>>boundary>>failure>>present>>aggregate>>originalChassis>>quality>>pointer;
            BattleStartParams p;p.playerMechPresetId=presets.at(model);p.mechSystemsSnapshotEnabled=true;
            auto s=BattleWorld::create(p).snapshot();s.setup.valid=true;s.setup.playerMode=0;
            auto& target=s.combatants[0];target.playerControlled=false;target.roster.team=BattleTeam::Opposing;
            target.roster.originalLiveObjectSlot=static_cast<uint8_t>(slot);target.posture.posture=static_cast<BattlePosture>(tp);
            auto& d=target.detailedDamage;
            d.criticalComponents[7].workingCount=static_cast<uint8_t>(la);d.criticalComponents[8].workingCount=static_cast<uint8_t>(ra);
            if(failure==1)d.criticalComponents[0].condition=3;
            if(failure==2)d.criticalComponents[3].condition=2;
            if(failure==3)d.originalCockpitCriticalFlag=true;
            if(failure==4)d.internalSections[2].structureRemaining=0;
            if(failure==5)d.internalSections[3].structureRemaining=0;
            BattleOriginalAiAggregateDiagnosticInput in;in.occupied=present!=0;in.liveObjectSlot=static_cast<uint8_t>(slot);
            in.engineConditionWord11=d.criticalComponents[0].condition;in.lifeSupportConditionWord0d=d.criticalComponents[3].condition;
            in.opaqueWord15=d.originalCockpitCriticalFlag?1:0;
            in.leftLegActuatorWord6c=static_cast<int16_t>(la);in.rightLegActuatorWord6e=static_cast<int16_t>(ra);
            in.sharedCurrentControlStateWord51=static_cast<int16_t>(bind?pp:tp);
            if(boundary==0)in.rawZ=24001;if(boundary==1)in.rawZ=-24001;
            if(boundary==2)in.rawX=-44161;if(boundary==3)in.rawX=44161;
            int points=0,maximum=0;
            for(size_t i=0;i<9;++i){in.armorWords[i]=static_cast<int16_t>(d.armorSections[i].armorRemaining);points+=in.armorWords[i];maximum+=d.armorSections[i].armorMaximum;}
            for(size_t i=0;i<8;++i){in.internalWords[i]=static_cast<int16_t>(d.internalSections[i].structureRemaining);points+=in.internalWords[i];maximum+=d.internalSections[i].structureMaximum;}
            const auto mounts=mw::mech3d::catalogWeaponMounts(presets[model]);
            // Read proven source weapon damage from the world's initialized instances.
            for(const auto& w:target.weapons){in.weapons.at(w.slotIndex).installationByte=1;in.weapons.at(w.slotIndex).originalDamageWord=static_cast<int16_t>(w.originalDamage);}
            const auto out=battleOriginalAiAggregate7070(in);
            check(out.exact && out.finalAggregateWord16==aggregate,"original aggregate mismatch");
            check(pointer==(bind?0xd334:0xd334+slot*0x55),"original player binding mismatch");
            const auto q=originalSalvageCondition(in.engineConditionWord11,0,in.opaqueWord15,0,
                static_cast<int>(mounts.size()),static_cast<int>(mounts.size()),points,maximum);
            OriginalSalvageRecords original;std::array<OriginalSalvageCandidate,4> candidates{};
            candidates.at(slot-4)={present!=0,static_cast<int16_t>(aggregate),{chassis[model],q}};
            appendOriginalSalvage(original,candidates,0,0);
            check(original[0].chassis==originalChassis && (originalChassis==65535 || original[0].condition==quality),"original trophy mismatch");
            if(!present)target.roster.originalLiveObjectSlot.reset();
            if(boundary<4)target.missionStatus=CombatantMissionStatus::Escaped;
            // Add a real, separately posed player to ensure production does not
            // inherit the shared-player bug or depend on vector order.
            auto player=target;player.roster.team=BattleTeam::Player;player.playerControlled=true;
            player.roster.originalLiveObjectSlot=uint8_t{0};player.posture.posture=static_cast<BattlePosture>(pp);s.combatants.push_back(player);
            OriginalSalvageRecords corrected;appendCampaignSalvage(corrected,s,true);
            const bool expected=present && boundary==4 && (failure!=0 || (tp==1 && (!la || !ra)));
            check((corrected[0].chassis!=65535)==expected,"corrected actor-local binding mismatch");
            if(expected)check(corrected[0].condition==q,"actuator failure changed trophy price");
            if(bind && (corrected[0].chassis!=original[0].chassis))++differences;
            ++aggregates;
        } else throw std::runtime_error("unknown actuator oracle kind");
        check(bool(row),"malformed actuator oracle");
    }
    check(predicates==5184 && aggregates==5600,"incomplete actuator oracle");
    check(differences>0,"original shared-player bug no longer distinguished");
}
void runtime() {
    // An actual collision makes an intact-legged AI prone. Its exhausted
    // actuator then prevents recovery and disables it without killing the pilot.
    for(int side=0;side<2;++side) {
        BattleStartParams p;applyBattleDriveRuntimeTuning(p);p.playerMechPresetId="locust";
        p.playerStartTransform={};
        p.playerRoster.team=BattleTeam::Player;p.mechSystemsSnapshotEnabled=true;
        p.combatAiPolicy=BattleCombatAiPolicy::Disabled;p.deterministicCollisionRuntimeEnabled=true;
        p.deterministicCombatRuntimeEnabled=true;p.combatConclusionDelaySeconds=0;
        p.constrainToTerrainBounds=false;p.setupMetadata.valid=true;
        BattleCombatantLaunchState enemy;enemy.mechPresetId="locust";enemy.roster.team=BattleTeam::Opposing;
        enemy.startTransform={0,0,1800,3.14159265358979323846};enemy.roster.pilotingSkill=3;
        auto initial=BattleWorld::create(p).snapshot().combatants[0];
        enemy.persistentMechState=campaignMechStateFromCombatant(initial);
        auto& actuator=enemy.persistentMechState.exactDamage->detailed.criticalComponents[7+side];
        actuator.workingCount=0;actuator.functional=false;actuator.condition=3;
        p.combatantLaunchStates.push_back(enemy);auto w=BattleWorld::create(p);bool sawFalling=false;
        for(int i=0;i<600 && !w.battleResult();++i) {
            BattleInputCommand c;c.tickIndex=w.tickIndex();c.throttle=sawFalling?0:1;w.enqueueInput(c);w.tick();
            const auto s=w.snapshot();const auto& a=s.combatants[1];
            if(a.posture.posture==BattlePosture::Falling){sawFalling=true;check(a.missionStatus==CombatantMissionStatus::Active,"disabled before prone1");}
            if(!a.posture.falls)check(a.missionStatus==CombatantMissionStatus::Active,"upright actuator failure disabled actor");
        }
        const auto s=w.snapshot();const auto& a=s.combatants[1];
        if(!sawFalling || !w.battleResult()) {
            for(const auto& c:s.combatants)std::cerr<<" actor="<<c.mechPresetId<<" z="<<c.transform.z
                <<" speed="<<c.forwardSpeed<<" posture="<<int(c.posture.posture)<<" falls="<<c.posture.falls
                <<" status="<<int(c.missionStatus)<<" actuator="<<int(c.detailedDamage.criticalComponents[7+side].workingCount);
            std::cerr<<'\n';
        }
        check(sawFalling && w.battleResult() && w.battleResult()->state==BattleMissionRuntimeState::Victory,"prone actuator enemy blocked victory");
        check(a.posture.posture==BattlePosture::Prone && a.posture.standAttempts==0,"actuator-disabled AI recovered");
        check(a.missionStatus==CombatantMissionStatus::Disabled && !a.mechDestroyed,"actuator loss killed chassis");
        check(a.detailedDamage.internalSections[2].structureRemaining && a.detailedDamage.internalSections[3].structureRemaining,"test accidentally removed leg");
        OriginalSalvageRecords r;appendCampaignSalvage(r,s,true);check(r[0].chassis==0 && r[0].condition==100,"prone actuator trophy lost value");
        const auto returned=campaignMechStateFromCombatant(a);check(returned.exactDamage->mechDisabled && !returned.exactDamage->mechDestroyed,"disabled return lost nonfatal state");
    }
    for(const auto pose:{BattlePosture::Upright,BattlePosture::Prone,BattlePosture::Falling,BattlePosture::Rising})
        for(int left=0;left<=4;++left)for(int right=0;right<=4;++right)
            check(battleActuatorLossDisables(pose,left,right)==(pose==BattlePosture::Prone && (!left || !right)),"partial damage/posture gate");
}
}
int main(){try{oracle();runtime();std::cout<<"mw_actuator_salvage_smoke: ok checks="<<checks<<" oracle=10784 intentional_pointer_bug_differences="<<differences<<'\n';return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
