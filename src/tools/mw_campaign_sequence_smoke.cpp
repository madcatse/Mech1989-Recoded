// Controlled procedures/results; never loads or plays the saved final battle.
#include "../main.cpp"
#include <iostream>
namespace {
using namespace mw;
using namespace mw::battle;
size_t checks=0;
void check(bool value,const std::string& reason) {++checks;if(!value)throw std::runtime_error(reason);}
constexpr std::array<const char*,8> presets{{"locust","jenner","phoenix_hawk","shadow_hawk","rifleman","warhammer","marauder","battlemaster"}};
constexpr std::array<size_t,9> armor{{1,0,3,2,5,6,8,7,4}};
constexpr std::array<size_t,8> internal{{1,0,3,2,5,7,6,4}};
std::vector<uint8_t> bytes(const std::string& name) {return readFile("research/fixtures/dosbox_sequence/"+name+".BIN");}
CombatantSnapshot rawActor(const std::vector<uint8_t>& b,size_t offset,size_t slot) {
    BattleStartParams p;p.mechSystemsSnapshotEnabled=true;p.playerMechPresetId=presets.at(readU16Le(b,offset));
    auto actor=BattleWorld::create(p).snapshot().combatants[0];
    actor.roster.team=BattleTeam::Player;actor.roster.sourceSlot="player:"+std::to_string(slot);
    auto& d=actor.detailedDamage;
    for (size_t i=0;i<9;++i)d.armorSections[armor[i]].armorRemaining=readU16Le(b,offset+25+2*i);
    for (size_t i=0;i<8;++i)d.internalSections[internal[i]].structureRemaining=readU16Le(b,offset+43+2*i);
    constexpr std::array<size_t,4> fields{{17,19,15,13}};
    for (size_t i=0;i<4;++i) {
        auto& c=d.criticalComponents[i];c.condition=static_cast<uint8_t>(readU16Le(b,offset+fields[i]));
        c.functional=c.condition<(i==0?3:2);c.workingCount=c.functional?1:0;c.totalCount=1;
    }
    d.criticalComponents[4].workingCount=static_cast<uint8_t>(10-readU16Le(b,offset+11));d.criticalComponents[4].totalCount=10;
    d.criticalComponents[9].totalCount=static_cast<uint8_t>(mech3d::catalogMobility(actor.mechPresetId).jumpJetCount);
    d.criticalComponents[9].workingCount=static_cast<uint8_t>(d.criticalComponents[9].totalCount-readU16Le(b,offset+23));
    d.originalCockpitCriticalFlag=readU16Le(b,offset+21)!=0;
    for (size_t i=0;i<4;++i) {
        auto& c=d.criticalComponents[5+i];c.workingCount=static_cast<uint8_t>(readU16Le(b,offset+104+2*i));
        c.totalCount=4;c.functional=c.workingCount>0;
    }
    for (const auto& mount:mech3d::catalogWeaponMounts(actor.mechPresetId)) {
        auto& w=d.installedWeapons[mount.slotIndex];w.functional=b[offset+59+3*mount.slotIndex]!=0;w.condition=w.functional?0:3;
    }
    actor.ammunitionStateValid=true;
    for(size_t i=0;i<6;++i)actor.ammunitionByPool[i]=readU16Le(b,offset+89+2*i);
    return actor;
}
void compareDamage(const CombatantSnapshot& actor,const std::vector<uint8_t>& b,size_t offset) {
    const auto& d=actor.detailedDamage;
    for(size_t i=0;i<9;++i)check(d.armorSections[armor[i]].armorRemaining==readU16Le(b,offset+25+2*i),"DOS armor repair");
    for(size_t i=0;i<8;++i)check(d.internalSections[internal[i]].structureRemaining==readU16Le(b,offset+43+2*i),"DOS structure repair");
    constexpr std::array<size_t,4> fields{{17,19,15,13}};
    for(size_t i=0;i<4;++i)check(d.criticalComponents[i].condition==readU16Le(b,offset+fields[i]),"DOS critical repair");
    for(size_t i=0;i<4;++i)check(d.criticalComponents[5+i].workingCount==readU16Le(b,offset+104+2*i),"DOS actuator repair");
    for(const auto& mount:mech3d::catalogWeaponMounts(actor.mechPresetId))
        check(d.installedWeapons[mount.slotIndex].functional==(b[offset+59+3*mount.slotIndex]!=0),"DOS weapon repair");
    check(d.originalCockpitCriticalFlag==(readU16Le(b,offset+21)!=0),"DOS cockpit repair");
    check(d.criticalComponents[9].totalCount-d.criticalComponents[9].workingCount==readU16Le(b,offset+23),"jump jets unexpectedly repaired");
    check(d.criticalComponents[4].totalCount-d.criticalComponents[4].workingCount==readU16Le(b,offset+11),"DOS heat-sink repair");
}
}
void runCampaignSequenceSmoke() {
    const auto sin=bytes("SIN"),sout=bytes("SOUT");
    check(sin.size()==1560*14 && sout.size()==1560*8,"sequence DOS fixture length");
    for(size_t i=0;i<sin.size()/14;++i) {
        const auto row=static_cast<uint8_t>(readU16Le(sout,8*i+2));const auto stage=readU16Le(sout,8*i);
        const auto plan=originalBattleSequencePlan(row);
        check(plan.valid && plan.originalRow==row && stage<plan.stageCount,"sequence row/count");
        check(originalBattlePlacementSelectorForMission(plan.missionIds[stage])==readU16Le(sout,8*i+4),"DOS sequence selector");
        check(originalBattleSequenceScenario(readU16Le(sin,14*i+8),row,stage)==readU16Le(sout,8*i+6),"DOS scenario advance");
    }
    check(!originalBattleSequencePlan(9).valid,"invalid sequence row accepted");
    const auto gin=bytes("GIN"),gout=bytes("GOUT");
    for(size_t i=0;i<gin.size()/8;++i) {
        const auto stage=static_cast<int16_t>(readU16Le(gin,i*8));
        const auto result=readU16Le(gin,i*8+4);
        check(originalMissionSequenceContinues(stage,readU16Le(gin,i*8+2)!=0,result)==(readU16Le(gout,i*6)!=0),"DOS continue gate");
        check(originalMissionStageServiceRequired(stage,result)==(readU16Le(gout,i*6+2)!=0),"DOS service gate");
    }
    for(uint32_t seed=0;seed<256;++seed) {
        const auto p=battleExtendedCampaignPlan(seed);check(p.originalRow<8 && p.stageCount==3,"extended selected final row");
    }
    const auto rin=bytes("RIN"),rout=bytes("ROUT");
    for(size_t i=0;i<rin.size()/114;++i) {
        BattleSnapshot s;s.combatants={rawActor(rin,114*i+2,0)};
        const auto entry=s.combatants[0].persistentMechState;
        const auto before=s.combatants[0].detailedDamage.criticalComponents[4].workingCount;
        std::array<int,6> hold{};unsigned calls=0,broken=0;
        for(const auto& m:mech3d::catalogWeaponMounts(s.combatants[0].mechPresetId))broken+=!s.combatants[0].detailedDamage.installedWeapons[m.slotIndex].functional;
        serviceOriginalMissionStage(s,hold,[&]{++calls;return readU16Le(rin,114*i);});
        compareDamage(s.combatants[0],rout,112*i);
        check(calls==broken,"weapon RNG draw count");
        check(s.combatants[0].detailedDamage.criticalComponents[4].workingCount>=before,"service destroyed working sinks");
        check(battlePersistentMechStateFingerprint(entry)==battlePersistentMechStateFingerprint(s.combatants[0].persistentMechState),"service mutated immutable entry");
        check(validatePersistentMechDamage(campaignMechStateFromCombatant(s.combatants[0]),s.combatants[0].mechPresetId),"invalid serviced exact state");
    }
    const auto ain=bytes("AIN"),aout=bytes("AOUT");
    for(size_t i=0;i<ain.size()/462;++i) {
        BattleSnapshot s;const auto mask=readU16Le(ain,i*462);std::array<int,6> hold{};
        for(size_t slot=4;slot-->0;)if(mask&(1u<<slot))s.combatants.push_back(rawActor(ain,i*462+2+112*slot,slot));
        for(size_t pool=0;pool<6;++pool)hold[pool]=readU16Le(ain,i*462+450+2*pool);
        serviceOriginalMissionStage(s,hold,[]{return 0u;});
        for(const auto& actor:s.combatants) {
            const size_t slot=static_cast<size_t>(actor.roster.sourceSlot.back()-'0');
            for(size_t pool=0;pool<6;++pool)check(actor.ammunitionByPool[pool]==readU16Le(aout,i*460+slot*112+89+2*pool),"DOS round-robin refill");
        }
        for(size_t pool=0;pool<6;++pool)check(hold[pool]==readU16Le(aout,i*460+448+2*pool),"DOS hold consumption");
    }
    const auto pin=bytes("PIN"),pout=bytes("POUT");
    for(size_t i=0;i<pin.size()/12;++i) {
        CampaignBattleOutcomePackage outcome;outcome.valid=outcome.terminal=true;outcome.launchSource=CampaignBattleLaunchSource::DarkWingFinal;
        outcome.outcome=CampaignBattleOutcome::Defeat;outcome.rawResultCode=readU16Le(pin,i*12);
        CampaignBattleConsequenceContext c;c.defeatedOpponents=static_cast<uint8_t>(readU16Le(pin,i*12+10));
        const auto p=campaignBattleConsequencePlanFromOutcome(outcome,{},c);
        check(p.commitEligible && !p.payment && !p.salvage && p.pilotExperienceAward,"Dark Wing failure plan");
        if(readU16Le(pin,i*12+2))continue; // commander death guarded by App, tested below
        check(p.campaignDateTicks==readU16Le(pout,i*22+10) &&
            readU16Le(pin,i*12+4)+p.reputationDelta==readU16Le(pout,i*22),"DOS failure time/reputation");
        const auto xp=campaignPilotExperienceAfterSurvivedMission(static_cast<uint8_t>(readU16Le(pin,i*12+8)),readU16Le(pin,i*12+6),true);
        check(xp.skill==readU16Le(pout,i*22+6) && xp.missionCounter==readU16Le(pout,i*22+4),"DOS failure XP");
    }
    App app(findResourceRoot());app.loadResources();
    for(unsigned defeated=0;defeated<=4;++defeated)for(auto result:{CampaignBattleOutcome::Victory,CampaignBattleOutcome::Defeat,CampaignBattleOutcome::Withdraw}) {
        CampaignBattleOutcomePackage o;o.valid=o.terminal=o.contractMetadataValid=true;
        o.launchSource=CampaignBattleLaunchSource::AcceptedContract;o.outcome=result;
        BattleContractMetadata c;c.valid=true;c.estimatedHeavyMechs=100;c.estimatedMediumMechs=100;c.estimatedLightMechs=100;
        CampaignBattleConsequenceContext context;context.defeatedOpponents=static_cast<uint8_t>(defeated);
        check(campaignBattleConsequencePlanFromOutcome(o,c,context).reputationDelta==static_cast<int>(defeated),"contract reputation still uses HML estimate");
    }
    const auto fresh=readFile("research/fixtures/dosbox_story/start.gam");
    auto prepare=[&] {
        app.applyGamSaveToRuntime(fresh);app.clearStoryScene();app.acceptedBattleContract_={};app.acceptedBattleOutcome_={};
        app.campaignBattleRuntime_={};app.acceptedBattleMechReturnPlan_={};app.acceptedBattlePersistenceReport_={};
        app.lastCommittedBattleConsequencePlanFingerprint_=0;
        app.currentPlanetIndex_=app.findPlanetIndexByName("RADSTADT");
        app.extraAmmoInHold_.fill(0);app.extraAmmoInHold_[3]=7;
        app.ownedMechs_[0].ammoPacks[3]=10;app.missionParticipants_=app.currentMissionParticipants();
        app.beginFinalMissionSequence();app.finalMissionStubActive_=true;
    };
    prepare();
    auto first=app.battleStartParamsForDarkWingFinalMission();
    const auto initialScenario=app.planets_[static_cast<size_t>(app.currentPlanetIndex_)].tableOrder%20u;
    check(first.mission.originalId==16 && first.setupMetadata.missionSelector==16 && first.terrainScenarioIndex==initialScenario,"App Dark Wing first setup");
    auto snapshot=BattleWorld::create(first).snapshot();
    check(snapshot.combatants.size()==5,"Dark Wing initial roster");
    check(campaignBattleOutcomePackageFromSnapshots(snapshot,snapshot,{},0).launchSource==CampaignBattleLaunchSource::DarkWingFinal,"stage0 source classification");
    app.missionSequence_.stageIndex=1;
    auto second=app.battleStartParamsForDarkWingFinalMission();
    check(second.mission.originalId==12 && second.setupMetadata.missionSelector==12 && second.terrainScenarioIndex==(initialScenario+1u)%20u,"App Dark Wing second setup");
    check(second.setupMetadata.playerMode==3 && first.setupMetadata.playerMode==0,"stage placement banks not changed");
    // Real result handler and guarded transaction from controlled terminal
    // snapshots. No saved final battle, combat replay or pixel fidelity claim.
    for(size_t stage=0;stage<2;++stage)for(bool dead:{false,true}) {
        prepare();app.missionSequence_.stageIndex=stage;
        auto params=app.battleStartParamsForDarkWingFinalMission();params.combatConclusionDelaySeconds=0;
        auto world=BattleWorld::create(params);const auto start=world.snapshot();
        world.setMissionStatus(world.playerEntityId(),CombatantMissionStatus::Destroyed);world.tick();
        check(world.battleResult().has_value(),"controlled final failure result missing");
        auto end=world.snapshot();end.combatants[0].detailedDamage.criticalComponents[0].condition=3;
        if(dead)end.combatants[0].detailedDamage.originalCockpitCriticalFlag=true;
        end.combatants[1].detailedDamage.criticalComponents[0].condition=3;
        end.combatants[2].detailedDamage.criticalComponents[0].condition=3;
        const auto oldDate=app.currentMonthDayCounter_,oldMonth=app.currentMonth_;
        const auto oldXP=app.crewMembers_[0].missionExperience;const auto oldRep=app.playerReputationPoints_;
        const auto oldHouses=app.houseNegativeCounters_;const auto oldWealth=app.playerWealth_;
        app.campaignBattleRuntime_.world=std::move(world);app.campaignBattleRuntime_.startSnapshot=start;
        app.campaignBattleRuntime_.currentPresentationSnapshot=end;app.campaignBattleRuntime_.presentationSnapshotsValid=true;
        app.campaignBattleRuntime_.persistentStateBefore=app.captureCampaignPersistentState();
        app.campaignBattleRuntime_.missionParticipantCount=1;app.campaignBattleRuntime_.persistentRosterBindings={{"player:0",0,0}};
        app.finalizeCampaignBattleRuntimeOutcome();
        check(app.missionSequence_.stageIndex==stage && app.acceptedBattleMechReturnPlan_.valid,"failure advanced stage/lost damage");
        check(app.acceptedBattleMechReturnPlan_.entries[0].state.engine==3,"failure repaired engine");
        const auto before=app.captureCampaignPersistentState().fingerprint;
        if(dead) {
            check(!app.commitAcceptedBattleConsequencePlan().committed && before==app.captureCampaignPersistentState().fingerprint,"dead commander continued");
            continue;
        }
        check(app.acceptedBattleConsequencePlan_.reputationDelta==2,"last-stage defeated count");
        check(app.acceptedBattleConsequencePlan_.defeatedOpponents==2,"last-stage raw result count");
        app.acceptedBattleStateGuard_.after.fingerprint^=1;
        check(!app.commitAcceptedBattleConsequencePlan().committed,"stale failure committed");
        app.acceptedBattleStateGuard_.after.fingerprint^=1;
        const auto goodReturn=app.acceptedBattleMechReturnPlan_;
        const auto beforeFailedCommitRng=app.repairSession_.rng;
        app.acceptedBattleMechReturnPlan_.entries[0].ammunitionByPool[0]=255;
        check(!app.commitAcceptedBattleConsequencePlan().committed && before==app.captureCampaignPersistentState().fingerprint,"failure transaction rollback");
        check(app.repairSession_.rng.word3f5==beforeFailedCommitRng.word3f5 &&
            app.repairSession_.rng.word3f7==beforeFailedCommitRng.word3f7,"rollback advanced shared campaign RNG");
        app.acceptedBattleMechReturnPlan_=goodReturn;
        check(app.commitAcceptedBattleConsequencePlan().committed,"failure commit");
        check(app.campaignResultState_.defeatedOpponents==2,"failure commit lost GAM1D count");
        check(app.playerReputationPoints_==oldRep+2 && app.crewMembers_[0].missionExperience==oldXP+1,"failure XP/rep");
        check(app.currentMonth_==oldMonth && app.currentMonthDayCounter_==oldDate+28,"failure elapsed half-days");
        check(app.playerWealth_==oldWealth && app.houseNegativeCounters_==oldHouses,"failure payment/houses");
        check(!app.campaignCompleted_ && !app.storyFlag(0x72) && !app.storyFlag(0x75),"failure marked complete");
        check(app.extraAmmoInHold_[3]==7 && app.ownedMechs_[0].ammoPacks[3]==10,"failure refilled ammunition");
        const auto committedRng=app.repairSession_.rng;
        check(app.commitAcceptedBattleConsequencePlan().duplicateRejected,"duplicate failure commit");
        check(app.repairSession_.rng.word3f5==committedRng.word3f5 &&
            app.repairSession_.rng.word3f7==committedRng.word3f7,"duplicate commit consumed shared RNG");
        const auto dir=fs::path("outputs/sequence_audit")/("save_"+std::to_string(GetTickCount64()));fs::create_directories(dir);
        check(app.saveCurrentGame(dir/"FAIL.GAM"),"failure Save");const auto gam=readFile(dir/"FAIL.GAM");
        check(gam[0x1d]==2,"failure Save wrote rank over returned count");
        app.loadGameFromFile(dir/"FAIL.GAM");auto again=gam;app.writeRuntimeToGamSave(again);
        check(gam.size()==1816 && gam==again && !app.campaignCompleted_,"failure GAM Restore");
    }
    for(size_t stage=0;stage<2;++stage) {
        prepare();app.missionSequence_.stageIndex=stage;
        auto params=app.battleStartParamsForDarkWingFinalMission();params.combatConclusionDelaySeconds=0;
        auto world=BattleWorld::create(params);const auto start=world.snapshot();
        for(const auto& actor:start.combatants)if(actor.roster.team==BattleTeam::Opposing)
            world.setMissionStatus(actor.id,CombatantMissionStatus::Destroyed);
        world.tick();check(world.battleResult() && world.battleResult()->rawResultCode==0,"controlled victory missing");
        auto end=world.snapshot();end.combatants[0].detailedDamage.armorSections[0].armorRemaining-=3;
        const auto expectedArmor=end.combatants[0].detailedDamage.armorSections[0].armorRemaining+1;
        // Service precedes the casualty predicate: source clears the cockpit
        // flag even on the terminal stage1 win, without another launch.
        end.combatants[0].detailedDamage.originalCockpitCriticalFlag=true;
        const auto before=app.captureCampaignPersistentState().fingerprint;
        app.campaignBattleRuntime_.world=std::move(world);app.campaignBattleRuntime_.startSnapshot=start;
        app.campaignBattleRuntime_.currentPresentationSnapshot=end;app.campaignBattleRuntime_.presentationSnapshotsValid=true;
        app.campaignBattleRuntime_.persistentStateBefore=app.captureCampaignPersistentState();
        app.campaignBattleRuntime_.missionParticipantCount=1;app.campaignBattleRuntime_.persistentRosterBindings={{"player:0",0,0}};
        app.finalizeCampaignBattleRuntimeOutcome();
        if(stage==0) {
            check(app.missionSequence_.stageIndex==1 && app.campaignBattleRuntime_.world.has_value(),
                "first win did not advance: "+app.acceptedBattleOutcome_.reason+" stage="+std::to_string(app.missionSequence_.stageIndex));
            check(app.captureCampaignPersistentState().fingerprint==before,"interstage wrote campaign");
            const auto& next=app.campaignBattleRuntime_.startSnapshot.combatants[0];
            check(next.ammunitionByPool[3]==17 && next.detailedDamage.armorSections[0].armorRemaining==expectedArmor &&
                !next.detailedDamage.originalCockpitCriticalFlag,"automatic stage service/carryover");
            check(app.missionSequence_.remainingExtraAmmo->at(3)==0 && app.extraAmmoInHold_[3]==7,"interstage hold not in RAM");
            // Fail the next controlled stage: the previously spent hold and
            // repaired damage must still return in the one final transaction.
            auto& nextWorld=*app.campaignBattleRuntime_.world;
            nextWorld.setMissionStatus(nextWorld.playerEntityId(),CombatantMissionStatus::Destroyed);
            for(int tick=0;tick<500 && !nextWorld.battleResult();++tick)nextWorld.tick();
            check(nextWorld.battleResult().has_value(),"second-stage failure missing");
            app.campaignBattleRuntime_.currentPresentationSnapshot=nextWorld.snapshot();
            app.campaignBattleRuntime_.currentPresentationSnapshot.combatants[0].detailedDamage.criticalComponents[0].condition=3;
            app.finalizeCampaignBattleRuntimeOutcome();
            check(app.acceptedBattleMechReturnPlan_.valid && app.commitAcceptedBattleConsequencePlan().committed,"stage1 failure after victory failed commit");
            check(app.extraAmmoInHold_[3]==0 && app.ownedMechs_[0].ammoPacks[3]==17,"previous-stage hold consumption lost");
            app.acknowledgeAcceptedBattleOutcome();
            check(app.state_==ScreenState::MainMenu && !app.lastBattlePostResultReceipt_.extendedEndingSequenceDeferred && !app.campaignCompleted_,"failed final return routing");
        } else {
            check(app.state_==ScreenState::CampaignEnding && app.campaignCompleted_==1,"stage1 victory ending not committed");
            check(app.extraAmmoInHold_[3]==0 && app.ownedMechs_[0].ammoPacks[3]==17 &&
                app.ownedMechs_[0].exactDamage->detailed.armorSections[0].armorRemaining==expectedArmor &&
                !app.ownedMechs_[0].exactDamage->detailed.originalCockpitCriticalFlag,"terminal service not returned");
        }
    }
    // Carryover cannot promote an ally, refill from the old owned record, or
    // apply the obsolete one-point minimal repair a second time.
    prepare();auto s=BattleWorld::create(app.battleStartParamsForDarkWingFinalMission()).snapshot();
    auto& a=s.combatants[0];a.ammunitionByPool[3]=3;a.detailedDamage.armorSections[0].armorRemaining-=3;
    a.missionStatus=CombatantMissionStatus::Disabled;
    auto ally=a;ally.roster.sourceSlot="player:1";ally.missionStatus=CombatantMissionStatus::Active;
    s.combatants={ally,a};serviceOriginalMissionStage(s,*app.missionSequence_.remainingExtraAmmo,[]{return 0u;});
    const auto serviced=s.combatants[1];app.captureMissionSequencePlayerCarryover(s);app.missionSequence_.stageIndex=1;
    const auto carry=app.battleStartParamsForDarkWingFinalMission();
    check(carry.playerRoster.sourceSlot=="player:0" && carry.playerAlliedLaunchStates[0].roster.sourceSlot=="player:1","stage promoted another actor");
    check(carry.playerLaunchState->ammunitionByPool[3]==7 && carry.playerAlliedLaunchStates[0].ammunitionByPool[3]==6 && app.extraAmmoInHold_[3]==7,"carryover/refill/hold RAM");
    check(!carry.playerLaunchState->minimalRepairApplied && carry.playerLaunchState->persistentMechState.exactDamage->detailed.armorSections[0].armorRemaining==serviced.detailedDamage.armorSections[0].armorRemaining,"extra minimal repair");
    std::cout<<"mw_campaign_sequence_smoke: ok checks="<<checks<<" DOS_cases=13560 saved_final_battle_not_started=1\n";
}
int main(){try{runCampaignSequenceSmoke();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
