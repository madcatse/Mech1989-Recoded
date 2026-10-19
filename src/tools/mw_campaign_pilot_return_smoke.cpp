// Original instruction oracle + production result/crew/Save/Restore handlers.
#include "../main.cpp"
#include <iostream>

namespace {
using namespace mw::battle;
size_t checks=0;
void check(bool ok,const std::string& message) {
    ++checks; if (!ok) throw std::runtime_error(message);
}
std::vector<uint8_t> unhex(const std::string& text) {
    std::vector<uint8_t> bytes;
    for (size_t i=0;i<text.size();i+=2) bytes.push_back(static_cast<uint8_t>(std::stoul(text.substr(i,2),nullptr,16)));
    return bytes;
}
void capture(const std::string& name,const std::vector<uint32_t>& pixels) {
    const auto dir=fs::path("outputs/pilot_return_audit");fs::create_directories(dir);
    std::ofstream out(dir/(name+".ppm"),std::ios::binary);out<<"P6\n320 200\n255\n";
    for (auto p:pixels) {const char rgb[]={char(p>>16),char(p>>8),char(p)};out.write(rgb,3);}
    check(bool(out),"frame capture failed");
}
}

void runCampaignPilotReturnSmoke() {
    App app(findResourceRoot()); app.loadResources();
    const auto fresh=app.baseGamSaveData();
    // Exhaustive valid component states, missing actors, nonzero cockpit high
    // byte, and all independent actor->crew output bindings in original bytes.
    std::ifstream file("research/fixtures/original_pilot_return.txt");
    check(bool(file),"missing original pilot fixture");
    std::string line;size_t predicates=0,rosters=0;
    while (std::getline(file,line)) {
        if (line.empty() || line[0]=='#') continue;
        std::istringstream row(line);char kind;row>>kind;
        if (kind=='P') {
            unsigned actor,crew,present,life,cockpit,engine;std::array<unsigned,4> expected{};
            row>>actor>>crew>>present>>life>>cockpit>>engine;
            for (auto& flag:expected) row>>flag;
            check(bool(row),"bad predicate fixture");
            std::array<unsigned,4> actual{};
            if (present) actual.at(crew)=originalBtechPilotKilled(actor==0,
                static_cast<uint16_t>(life),static_cast<uint16_t>(cockpit),static_cast<uint16_t>(engine))?1u:0u;
            check(actual==expected,"original pilot predicate differs");++predicates;
        } else {
            unsigned count;std::string flagsHex,dataHex;row>>count>>flagsHex>>dataHex;
            check(bool(row),"bad roster fixture");
            const auto flags=unhex(flagsHex),expected=unhex(dataHex);
            auto data=fresh;
            // Recreate cleanup_input's roster, leaving the actual campaign
            // resources/date/mechs valid. Compare fields the routine changes.
            App::writeU16Le(data,App::kGamOffsetCrewCount,static_cast<uint16_t>(count));
            std::fill(data.begin()+0x77,data.begin()+0xa1,uint8_t(1));
            for (size_t slot=0;slot<4;++slot) {
                App::writeU16Le(data,App::kGamOffsetCrewPilotIds+2*slot,static_cast<uint16_t>(slot?slot*2-1:0));
                App::writeU16Le(data,App::kGamOffsetReputationTier+2*slot,static_cast<uint16_t>(slot));
                App::writeU16Le(data,App::kGamOffsetCrewGunnerySkills+2*slot,static_cast<uint16_t>(slot));
                App::writeU16Le(data,App::kGamOffsetCrewPilotingSkills+2*slot,static_cast<uint16_t>(slot));
                App::writeU16Le(data,App::kGamOffsetCrewAssignedMechs+2*slot,0xffff);
            }
            app.applyGamSaveToRuntime(data);
            const auto ownedBefore=app.ownedMechs_.size();
            if (flags[0]!=1)
                for (size_t slot=count;--slot>0;)
                    if (flags[slot]) app.dismissCrewSlot(slot,flags[slot]==1);
            auto actual=data;app.writeRuntimeToGamSave(actual);
            const auto survivors=readU16Le(expected,App::kGamOffsetCrewCount);
            check(app.hiredCrewCount()==survivors,"original cleanup count differs");
            for (size_t slot=1;slot<survivors;++slot)
                for (size_t table:{App::kGamOffsetCrewPilotIds,App::kGamOffsetReputationTier,
                                  App::kGamOffsetCrewGunnerySkills,App::kGamOffsetCrewPilotingSkills})
                    check(readU16Le(actual,table+slot*2)==readU16Le(expected,table+slot*2),"original active crew compaction differs");
            check(std::equal(actual.begin()+0x77,actual.begin()+0xa1,expected.begin()+0x77),"dead/missing recruitment flag differs");
            check(app.ownedMechs_.size()==ownedBefore,"cleanup discarded an owned mech");++rosters;
        }
    }
    check(predicates==1152 && rosters==120,"incomplete source oracle");

    const std::array<int,4> owned{{2,0,4,1}};
    const std::vector<std::string> presets{"locust","jenner","phoenix_hawk","warhammer","battlemaster"};
    auto setup=[&] {
        app.applyGamSaveToRuntime(fresh);
        app.acceptedBattleMechReturnPlan_={};app.acceptedBattlePersistenceReport_={};
        app.acceptedBattleConsequencePlan_={};app.lastCommittedBattleConsequencePlanFingerprint_=0;
        app.acceptedBattleOutcome_={};app.missionSequence_={};app.campaignBattleRuntime_={};
        app.ownedMechs_.clear();app.resetCrewRosterForLoadedMechs();
        for (size_t recruit:{size_t(0),size_t(2),size_t(4)}) {
            app.recruitPilots_[recruit].unavailable=0;
            check(app.hireRecruit(recruit),"cannot construct four-pilot fixture");
        }
        for (const auto& preset:presets) {
            auto mech=App::makeMech(*App::battleChassisForMechPreset(preset));
            mech.ammoPacks=App::ammunitionCapacityForMech(mech);mech.assignedCrewSlot=-1;
            app.finalizeLoadedMech(mech);app.ownedMechs_.push_back(mech);
        }
        BattleStartParams params;params.constrainToTerrainBounds=false;
        params.deterministicCombatRuntimeEnabled=true;params.mechSystemsSnapshotEnabled=true;
        params.individualWeaponRuntimeEnabled=true;
        for (size_t slot=0;slot<4;++slot) {
            app.crewMembers_[slot].missionExperience=0;
            auto& mech=app.ownedMechs_[owned[slot]];mech.assignedCrewSlot=static_cast<int>(slot);
            BattleCombatantLaunchState launch;launch.mechPresetId=presets[owned[slot]];
            launch.roster.team=BattleTeam::Player;launch.roster.sourceSlot="player:"+std::to_string(slot);
            launch.startTransform={10000.+slot*2000.,0,10000.,0};
            launch.persistentMechState=App::battlePersistentMechStateFromOwnedMech(mech,"pilot_test");
            launch.ammunitionStateValid=true;launch.ammunitionByPool=mech.ammoPacks;
            if (!slot) {params.playerMechPresetId=launch.mechPresetId;params.playerLaunchState=launch;params.playerRoster=launch.roster;}
            else params.playerAlliedLaunchStates.push_back(launch);
        }
        app.playerWealth_=10000000;app.currentMonthDayCounter_=59;
        app.currentMonth_=0;app.currentYear_=3024;app.currentDay_=30;
        app.missionParticipants_=app.currentMissionParticipants();
        app.acceptedBattleConsequencePlan_.pilotExperienceAward=true;
        app.acceptedBattleConsequencePlan_.campaignDateTicks=1;
        return params;
    };
    auto prepare=[&](BattleSnapshot snapshot) {
        std::vector<CampaignBattlePersistentRosterBinding> bindings;
        for (size_t slot=0;slot<4;++slot) bindings.push_back({"player:"+std::to_string(slot),owned[slot],static_cast<int>(slot)});
        std::reverse(snapshot.combatants.begin(),snapshot.combatants.end());
        app.acceptedBattlePersistenceReport_=campaignBattlePersistenceReportFromSnapshot(snapshot,bindings,4);
        app.acceptedBattleMechReturnPlan_=campaignMechReturnPlan(app.acceptedBattlePersistenceReport_,presets);
        check(app.acceptedBattleMechReturnPlan_.valid,"pilot return plan invalid");
        app.acceptedBattleStateGuard_.before=app.captureCampaignPersistentState();
        app.acceptedBattleStateGuard_.after=app.acceptedBattleStateGuard_.before;
        app.acceptedBattleStateGuard_.valid=app.acceptedBattleStateGuard_.unchanged=true;
    };
    const auto directory=fs::path("outputs/pilot_return_audit")/("save_"+std::to_string(GetTickCount64()));
    fs::create_directories(directory);
    for (unsigned mask=0;mask<8;++mask) {
        const auto params=setup();auto snapshot=BattleWorld::create(params).snapshot();
        uint64_t wages=0;unsigned dead=0;
        for (size_t slot=1;slot<4;++slot) {
            if (mask&(1u<<(slot-1))) {
                ++dead;auto& d=snapshot.combatants.at(slot).detailedDamage;
                if (slot==1) {d.criticalComponents[3].condition=2;d.criticalComponents[3].workingCount=0;d.criticalComponents[3].functional=false;}
                if (slot==2) d.originalCockpitCriticalFlag=true;
                if (slot==3) {d.criticalComponents[0].condition=3;d.criticalComponents[0].workingCount=0;d.criticalComponents[0].functional=false;}
            } else wages+=app.crewMembers_[slot].wage;
        }
        prepare(snapshot);
        const auto initial=app.captureCampaignPersistentState().fingerprint;
        auto valid=app.acceptedBattleMechReturnPlan_;
        app.acceptedBattleMechReturnPlan_.entries.at(1).ammunitionByPool[0]=255; // Locust has no AC5.
        check(!app.commitAcceptedBattleConsequencePlan().committed &&
              app.captureCampaignPersistentState().fingerprint==initial,"failed casualty commit did not roll back");
        app.acceptedBattleMechReturnPlan_=valid;
        const auto receipt=app.commitAcceptedBattleConsequencePlan();
        check(receipt.committed && receipt.saveRoundTripVerified,"casualty commit failed: "+receipt.reason);
        check(app.hiredCrewCount()==4-static_cast<int>(dead) && app.ownedMechs_.size()==5,"dead pilots/mechs removed incorrectly");
        check(app.playerWealth_==10000000-wages,"dead pilot received monthly wages");
        check(app.crewMembers_[0].missionExperience==1,"commander survivor XP missing");
        int compact=1;
        for (size_t slot=1;slot<4;++slot) {
            const bool killed=(mask&(1u<<(slot-1)))!=0;
            const auto& pilot=app.recruitPilots_[(slot-1)*2];
            check(pilot.unavailable==1,"dead or hired pilot became recruitable");
            check(pilot.missionExperience==(killed?0:1),"dead pilot XP or survivor XP incorrect");
            check(app.ownedMechs_[owned[slot]].assignedCrewSlot==(killed?-1:compact),"survivor mech rebound incorrectly");
            if (!killed) {check(app.crewMembers_[compact].recruitIndex==static_cast<int>((slot-1)*2),"survivor identity changed");++compact;}
            else check(!app.hireRecruit((slot-1)*2),"dead pilot can be rehired");
        }
        check(app.ownedMechs_[3].assignedCrewSlot==-1 && !app.ownedMechs_[3].exactDamage,"reserve changed");
        const auto committed=app.captureCampaignPersistentState().fingerprint;
        check(app.commitAcceptedBattleConsequencePlan().duplicateRejected &&
              app.captureCampaignPersistentState().fingerprint==committed,"duplicate casualty removed another pilot");
        const auto path=directory/("MASK"+std::to_string(mask)+".GAM");
        check(app.saveCurrentGame(path),"casualty Save failed");
        const auto gam=readFile(path);check(gam.size()==1816,"GAM size changed");
        app.loadGameFromFile(path);
        check(app.hiredCrewCount()==4-static_cast<int>(dead) && app.ownedMechs_.size()==5,"casualty Restore lost crew/mechs");
        auto restored=gam;app.writeRuntimeToGamSave(restored);
        check(restored==gam,"casualty Save/Restore changed original GAM bytes");
    }
    // Exercise the real App Escape routing, its presentation gates and the
    // recorded command; no live-window timing or organic kill is claimed.
    size_t acknowledgmentCases=0;
    for(bool win:{false,true})for(int mode=0;mode<6;++mode)for(bool repeat:{false,true}) {
        auto p=setup();p.combatConclusionDelaySeconds=2.;
        p.combatAiPolicy=BattleCombatAiPolicy::ReplacementDeterministicCombatAi;
        p.originalMajorSystemRuntimeEnabled=p.originalHeatRuntimeEnabled=
            p.originalProjectileRuntimeEnabled=p.deterministicCollisionRuntimeEnabled=true;
        BattleCombatantLaunchState enemy;enemy.mechPresetId="jenner";
        enemy.roster.team=BattleTeam::Opposing;enemy.roster.sourceSlot="opposing:0";
        enemy.startTransform={40000,0,40000,0};p.combatantLaunchStates.push_back(enemy);
        auto& rt=app.campaignBattleRuntime_;rt.world=BattleWorld::create(p);
        for(const auto& a:rt.world->snapshot().combatants)
            if((a.roster.team==BattleTeam::Opposing)==win)
                rt.world->setMissionStatus(a.id,CombatantMissionStatus::Destroyed);
        rt.world->tick();rt.currentPresentationSnapshot=rt.world->snapshot();
        check(rt.currentPresentationSnapshot.conclusionDelay.active,"App acknowledgment fixture has no pending outcome");
        rt.active=true;app.state_=ScreenState::MissionBattleStub;
        rt.presentationMode=mode<5?static_cast<CampaignBattlePresentationMode>(mode):CampaignBattlePresentationMode::Cockpit;
        rt.mechStatusVisible=mode==5;app.currentKeyDownRepeat_=repeat;
        const auto persistentBefore=app.captureCampaignPersistentState().fingerprint;
        app.handleKey(VK_ESCAPE);
        const bool eligible=mode==static_cast<int>(CampaignBattlePresentationMode::Cockpit);
        check(rt.pendingConclusionAcknowledgment==eligible,"App Escape used wrong screen/repeat gate");
        check(!rt.world->battleResult(),"App mutated outcome outside simulation tick");
        app.enqueueCampaignBattleRuntimeInput(*rt.world);rt.world->tick();
        check(rt.inputReplay.commands.front().acknowledgeConclusion==eligible,"App replay lost Escape");
        check(!rt.pendingConclusionAcknowledgment,"App Escape stayed queued after consumption");
        check(rt.world->battleResult().has_value()==eligible,"App Escape did not match pending outcome");
        if(eligible)check(rt.world->battleResult()->rawResultCode==(win?0:1),"Escape changed outcome into retreat");
        check(app.captureCampaignPersistentState().fingerprint==persistentBefore,"Escape wrote campaign state before finalize");
        ++acknowledgmentCases;
    }
    {auto& rt=app.campaignBattleRuntime_;app.currentKeyDownRepeat_=false;
        rt.mechStatusVisible=false;rt.presentationMode=CampaignBattlePresentationMode::Cockpit;
        app.handleKey(VK_ESCAPE);check(rt.pendingConclusionAcknowledgment,"pending Escape missing before screen switch");
        app.currentKeyDownRepeat_=true;app.handleKey('C');
        check(rt.pendingConclusionAcknowledgment && rt.presentationMode==CampaignBattlePresentationMode::Cockpit,
            "ignored map-key repeat discarded Escape");
        app.currentKeyDownRepeat_=false;
        app.handleKey('C');app.handleKey('C');
        check(!rt.pendingConclusionAcknowledgment,"screen round trip resurrected pending Escape");
    }
    app.currentKeyDownRepeat_=false;
    // Paused modal uses the real App routing. Cancellation keys must not reach
    // cockpit controls; repeated opening Escape is not a fresh answer.
    for(WPARAM answer:{WPARAM('Y'),WPARAM('N'),WPARAM(VK_ESCAPE),WPARAM(VK_RETURN),
                       WPARAM(VK_SPACE),WPARAM('C'),WPARAM('G'),WPARAM('W'),WPARAM(VK_F3)}) {
        auto p=setup();auto& rt=app.campaignBattleRuntime_;rt.world=BattleWorld::create(p);
        rt.currentPresentationSnapshot=rt.world->snapshot();rt.previousPresentationSnapshot=rt.currentPresentationSnapshot;
        rt.active=true;rt.presentationSnapshotsValid=true;app.state_=ScreenState::MissionBattleStub;
        rt.presentationMode=CampaignBattlePresentationMode::Cockpit;
        const auto before=battleSnapshotFingerprint(rt.world->snapshot());
        const auto campaignBefore=app.captureCampaignPersistentState().fingerprint;
        app.handleKey(VK_ESCAPE);check(rt.retreatPromptVisible,"Escape did not open retreat prompt");
        app.currentKeyDownRepeat_=true;app.handleKey(VK_ESCAPE);app.handleKey('Y');
        check(rt.retreatPromptVisible && !rt.pendingConfirmedRetreat,"held key answered modal");
        app.currentKeyDownRepeat_=false;
        rt.lastStepTick=0;app.updateCampaignBattleRuntime(100000);
        check(battleSnapshotFingerprint(rt.world->snapshot())==before,"modal advanced combat");
        app.handleKey(answer);
        check(!rt.retreatPromptVisible && rt.pendingConfirmedRetreat==(answer=='Y'),"wrong modal answer");
        check(rt.presentationMode==CampaignBattlePresentationMode::Cockpit && !rt.mechStatusVisible &&
            rt.pendingWeaponControls.empty(),"modal key leaked into cockpit controls");
        check(rt.lastStepTick!=0,"modal answer left catch-up time debt");
        check(app.captureCampaignPersistentState().fingerprint==campaignBefore,"modal wrote campaign state");
        if(answer=='Y') {
            app.enqueueCampaignBattleRuntimeInput(*rt.world);rt.world->tick();
            check(rt.inputReplay.commands.back().confirmRetreat && rt.world->battleResult()->rawResultCode==2,"App retreat missing from replay/result");
            check(!rt.pendingConfirmedRetreat,"retreat not consumed");
            prepare(rt.world->snapshot());
            check(app.commitAcceptedBattleConsequencePlan().committed,"retreat mech/crew commit failed");
            const auto path=directory/"RETREAT.GAM";
            check(app.saveCurrentGame(path),"retreat explicit Save failed");
            const auto gam=readFile(path);app.loadGameFromFile(path);auto restored=gam;app.writeRuntimeToGamSave(restored);
            check(gam.size()==1816 && gam==restored,"retreat Save/Restore changed GAM");
        }
    }
    // Engine destruction kills an ally but explicitly spares actor zero.
    auto params=setup();auto snapshot=BattleWorld::create(params).snapshot();
    snapshot.combatants[0].detailedDamage.criticalComponents[0].condition=3;
    snapshot.combatants[0].mechDestroyed=true;
    prepare(snapshot);check(!app.acceptedBattleMechReturnPlan_.killedCrewSlots[0],"player engine-only death invented");
    check(app.commitAcceptedBattleConsequencePlan().committed,"surviving commander cannot return wreck for repair");
    // Actual world fatal-pilot conclusion with living allies must reach the
    // death presentation and reject Continue. Engine-only control remains a
    // supplied terminal snapshot; no organic combat fidelity claim.
    for (int mode:{0,1,2}) {
        params=setup();params.contract.valid=true;params.combatConclusionDelaySeconds=params.fatalConclusionDelaySeconds=0;
        if(mode<2) {
            params.combatAiPolicy=BattleCombatAiPolicy::ReplacementDeterministicCombatAi;
            params.originalMajorSystemRuntimeEnabled=params.originalHeatRuntimeEnabled=
                params.originalProjectileRuntimeEnabled=params.deterministicCollisionRuntimeEnabled=true;
            const auto intact=BattleWorld::create(params).snapshot().combatants[0];
            params.playerLaunchState->persistentMechState=campaignMechStateFromCombatant(intact);
            auto& damage=params.playerLaunchState->persistentMechState.exactDamage->detailed;
            if(mode==1)damage.originalCockpitCriticalFlag=true;
            else {damage.criticalComponents[3].condition=2;damage.criticalComponents[3].workingCount=0;damage.criticalComponents[3].functional=false;}
        }
        auto world=BattleWorld::create(params);
        if(mode==2)world.setMissionStatus(world.playerEntityId(),CombatantMissionStatus::Destroyed);
        world.tick();
        check(world.battleResult().has_value(),"controlled death has no terminal battle result");
        snapshot=world.snapshot();
        auto& player=snapshot.combatants[0];
        if(mode<2) {
            check(world.battleResult()->reason=="commander_killed" && snapshot.combatants[1].missionStatus==CombatantMissionStatus::Active,
                "live ally incorrectly kept fatal commander in battle");
        } else {
            player.detailedDamage.criticalComponents[0].condition=3;
            for (size_t slot=1;slot<4;++slot) snapshot.combatants[slot].detailedDamage.originalCockpitCriticalFlag=true;
        }
        app.campaignBattleRuntime_.world=std::move(world);
        app.campaignBattleRuntime_.startSnapshot=snapshot;
        app.campaignBattleRuntime_.currentPresentationSnapshot=snapshot;
        app.campaignBattleRuntime_.presentationSnapshotsValid=true;
        app.campaignBattleRuntime_.missionParticipantCount=4;
        app.campaignBattleRuntime_.persistentStateBefore=app.captureCampaignPersistentState();
        for (size_t slot=0;slot<4;++slot)
            app.campaignBattleRuntime_.persistentRosterBindings.push_back({"player:"+std::to_string(slot),owned[slot],static_cast<int>(slot)});
        app.finalizeCampaignBattleRuntimeOutcome();
        check(app.acceptedBattleMechReturnPlan_.valid,"real finalize has no return plan");
        if (mode==2) {
            check(app.missionDebriefOutcome_==MissionOutcome::Defeat && !app.missionParticipants_[0].killed,
                "engine-only disabled commander shown killed");
            check(app.missionParticipants_.size()==4 && app.missionParticipants_[3].killed,
                "fourth pilot casualty missing");
            app.render();const auto killed=app.framebuffer_;
            app.missionParticipants_[3].killed=false;app.render();
            bool changed=false;
            for (size_t y=44;y<50;++y) for (size_t x=248;x<295;++x)
                changed=changed || killed[y*320+x]!=app.framebuffer_[y*320+x];
            check(changed,"fourth pilot status not rendered");
            app.missionParticipants_[3].killed=true;app.render();capture("three-allies-killed",app.framebuffer_);
            app.dismissMissionDebrief();
            check(app.state_==ScreenState::MainMenu && app.hiredCrewCount()==1 && app.ownedMechs_.size()==5,
                "real Continue did not remove dead allies and retain mechs");
            continue;
        }
        check(app.acceptedBattleMechReturnPlan_.killedCrewSlots[0],"real finalize lost commander death");
        check(app.missionParticipants_[0].killed,"debrief does not show killed commander");
        const auto before=app.captureCampaignPersistentState().fingerprint;
        check(!app.commitAcceptedBattleConsequencePlan().committed,"dead commander result committed");
        app.handleKey(VK_ESCAPE);
        check(app.state_==ScreenState::MissionDebrief && app.captureCampaignPersistentState().fingerprint==before,
            "global Escape bypassed death screen");
        app.acknowledgeAcceptedBattleOutcome();
        check(app.captureCampaignPersistentState().fingerprint==before && app.acceptedBattleMechReturnPlan_.valid,"dead commander continued campaign");
        check(app.missionDebriefOutcome_==MissionOutcome::Death && app.state_==ScreenState::MissionDebrief,
            "real finalize did not show PLAY AGAIN / QUIT");
        app.render();
        capture(mode==1?"cockpit-death":"life-support-death",app.framebuffer_);
    }
    std::cout<<"mw_campaign_pilot_return_smoke: ok checks="<<checks<<" oracle=1272 masks=8 save_restore=8 acknowledgment_cases="<<acknowledgmentCases<<"\n";
}

int main() {try {runCampaignPilotReturnSmoke();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
