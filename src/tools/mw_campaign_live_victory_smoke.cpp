// A naturally completed DOS fight from a prepared ordinary-contract fixture.
// Import the observed final actors at the adapter boundary; this does not
// claim identical C++ combat trajectories, RNG history or frame timing.
#include "../main.cpp"
#include <iostream>

namespace {
using namespace mw::battle;
size_t checks=0;
void check(bool ok,const std::string& why) {++checks;if(!ok)throw std::runtime_error(why);}
constexpr std::array<size_t,9> external{{1,0,3,2,5,6,8,7,4}};
constexpr std::array<size_t,8> internal{{1,0,3,2,5,7,6,4}};
constexpr std::array<size_t,10> critical{{0x11,0x13,0xf,0xd,0xb,0x68,0x6a,0x6c,0x6e,0x17}};
void importActor(CombatantSnapshot& actor,const std::vector<uint8_t>& ds,size_t slot) {
    const size_t base=0x4fb6+slot*0x70;
    auto& d=actor.detailedDamage;
    for(size_t n=0;n<9;++n) {
        auto& a=d.armorSections[external[n]];
        a.armorRemaining=readU16Le(ds,base+0x19+n*2);
        check(a.armorRemaining<=a.armorMaximum,"captured armor outside model capacity");
        a.battleDamage=a.armorMaximum-a.armorRemaining;
    }
    for(size_t n=0;n<8;++n) {
        auto& a=d.internalSections[internal[n]];
        a.structureRemaining=readU16Le(ds,base+0x2b+n*2);
        check(a.structureRemaining<=a.structureMaximum,"captured internal outside model capacity");
        a.battleDamage=a.structureMaximum-a.structureRemaining;
    }
    for(size_t n=0;n<10;++n) {
        auto& c=d.criticalComponents[n];const auto value=readU16Le(ds,base+critical[n]);
        if(n==4 || n==9) {
            check(value<=c.totalCount,"captured destroyed count outside capacity: actor="+
                std::to_string(slot)+" component="+std::to_string(n));
            c.workingCount=static_cast<uint8_t>(c.totalCount-value);
        } else if(n>=5 && n<=8) c.workingCount=static_cast<uint8_t>(value);
        else {c.condition=static_cast<uint8_t>(value);c.workingCount=static_cast<uint8_t>(value<(n==0?3:2));}
        c.functional=c.totalCount==0 || c.workingCount!=0;
    }
    d.originalCockpitCriticalFlag=readU16Le(ds,base+0x15)!=0;
    for(const auto& mount:mw::mech3d::catalogWeaponMounts(actor.mechPresetId))
        d.installedWeapons[mount.slotIndex].functional=ds.at(base+0x3b+3*mount.slotIndex)!=0;
    for(size_t n=0;n<6;++n) actor.ammunitionByPool[n]=readU16Le(ds,base+0x59+2*n);
}
void capture(const fs::path& path,const std::vector<uint32_t>& pixels) {
    std::ofstream f(path,std::ios::binary);f<<"P6\n320 200\n255\n";
    for(auto p:pixels) {const char rgb[]={char(p>>16),char(p>>8),char(p)};f.write(rgb,3);}
    check(bool(f),"cannot write result image");
}
}

void runCampaignLiveVictorySmoke() {
    const fs::path root="research/fixtures/dosbox_live_victory";
    const auto entry=readFile(root/"live-entry.gam"),before=readFile(root/"live-entry.ds.bin");
    const auto final=readFile(root/"live-final.ds.bin"),transfer=readFile(root/"live-exit-transfer.gam");
    const auto disk=readFile(root/"live-dos.GAM");
    const auto percentages=readFile(root/"PIN.BIN"),originalPercent=readFile(root/"POUT.BIN"),
        correctedPercent=readFile(root/"PCORRECT.BIN");
    const auto tables=decodeOriginalDebriefTables(readFile("Original/MW_MAIN.EXE"));
    check(percentages.size()==3524*31 && originalPercent.size()==3524*2 &&
        correctedPercent.size()==originalPercent.size(),"incomplete percentage instruction corpus");
    size_t correctedCases=0;
    for(size_t row=0;row<3524;++row) {
        std::array<uint8_t,29> condition{};
        std::copy_n(percentages.begin()+row*31+2,29,condition.begin());
        const auto actual=campaignDebriefMechConditionPercent(tables,percentages[row*31+1],condition);
        check(actual==readU16Le(correctedPercent,2*row),"chassis-index percentage differs from instruction oracle");
        correctedCases+=actual!=readU16Le(originalPercent,2*row);
    }
    check(correctedCases>0,"original crew-index bug is no longer exercised");
    const auto houseInput=readFile(root/"HIN.BIN"),houseOutput=readFile(root/"HOUT.BIN");
    check(houseInput.size()==120*4 && houseOutput.size()==120*40,"incomplete house procedure corpus");
    for(size_t row=0;row<120;++row) {
        const auto p=row*4;CampaignBattleOutcomePackage outcome;
        outcome.valid=outcome.terminal=outcome.contractMetadataValid=true;
        outcome.launchSource=CampaignBattleLaunchSource::AcceptedContract;
        outcome.rawResultCode=houseInput[p];outcome.outcome=houseInput[p]==0?CampaignBattleOutcome::Victory:
            houseInput[p]==2?CampaignBattleOutcome::Withdraw:CampaignBattleOutcome::Defeat;
        BattleContractMetadata contract;contract.valid=true;contract.employerHouseId=houseInput[p+1];
        contract.targetHouseId=houseInput[p+2];contract.hasHostileTargetHouse=contract.targetHouseId<5;
        const auto plan=campaignBattleConsequencePlanFromOutcome(outcome,contract);
        for(size_t house=0;house<5;++house) {
            check(plan.housePositiveDelta[house]==readU16Le(houseOutput,row*40+10+2*house),"DOS employer award differs");
            check(plan.houseNegativeDelta[house]==readU16Le(houseOutput,row*40+20+2*house),"DOS raw-result penalty differs");
        }
    }
    check(readFile(root/"TROPHY.BIN")==std::vector<uint8_t>(16,255),"original simulator sentinel changed");
    check(entry.size()==1816 && transfer.size()==1816 && disk.size()==1816 &&
        before.size()==65536 && final.size()==65536,"incomplete live victory fixture");
    check(entry[7]==1 && entry[0x20]==1 && final[0x7d88]==1 && final[0x7d89]==0 &&
        transfer[0x1f]==0 && transfer[0x1d]==1,"fixture is not observed ordinary garrison victory");
    check(readU16Le(final,0xd204)==991 && readU16Le(final,0xd34a)==122,
        "last complete source frame/aggregate changed");
    App app(findResourceRoot());app.loadResources();app.loadGameFromFile(root/"live-entry.gam");
    check(app.contractAccepted_ && app.acceptedContract_.originalMissionId==1,"Restore lost ordinary accepted contract");
    app.beginAcceptedContractMissionSequence();
    app.missionParticipants_=app.currentMissionParticipants();
    check(app.missionParticipants_.size()==4,"live roster missing crew");
    BattleStartParams params;
    params.constrainToTerrainBounds=false;params.deterministicCombatRuntimeEnabled=true;
    params.individualWeaponRuntimeEnabled=true;params.mechSystemsSnapshotEnabled=true;
    params.setupMetadata.valid=true;params.setupMetadata.playerMode=4;params.setupMetadata.playerSlots.resize(4);
    for(size_t n=0;n<4;++n)params.setupMetadata.playerSlots[n].transform={10000+1500.*n,0,10000,0};
    app.applyCampaignPlayerLaunchStates(params);
    auto enemyLaunch=*params.playerLaunchState;
    enemyLaunch.roster.team=BattleTeam::Opposing;enemyLaunch.roster.sourceSlot="enemy:0";
    enemyLaunch.roster.originalLiveObjectSlot=uint8_t{4};enemyLaunch.startTransform={50000,0,50000,0};
    params.combatantLaunchStates.push_back(enemyLaunch);
    params.contract=app.battleContractMetadataFromOffer(app.acceptedContract_);
    app.acceptedBattleContract_=params.contract;
    params.combatConclusionDelaySeconds=0; // Only manufacture the adapter's raw0 boundary.
    auto world=BattleWorld::create(params);auto start=world.snapshot();auto snapshot=start;
    for(size_t n=0;n<4;++n) {
        auto& actor=snapshot.combatants.at(n);
        check(actor.roster.sourceSlot=="player:"+std::to_string(n),"App actor order changed");
        check(readU16Le(before,0x4fa6+2*n)==n && readU16Le(final,0x4fa6+2*n)==n,"DOS owned binding changed");
        for(size_t part=0;part<9;++part)check(actor.detailedDamage.armorSections[external[part]].armorRemaining==
            readU16Le(before,0x4fb6+n*0x70+0x19+2*part),"actual App entry armor differs");
        importActor(actor,final,n);
        const auto state=campaignMechStateFromCombatant(actor);
        check(validatePersistentMechDamage(state,actor.mechPresetId),"invalid live participant import");
        auto bytes=entry;encodeOriginalGamMechDamage(bytes,0x102+29*n,state);
        for(size_t at=0x102+29*n;at<0x102+29*(n+1);++at)
            check(bytes[at]==transfer[at],"DOS live export differs at "+std::to_string(at));
    }
    // The captured fifth actor is an opposing Jenner with destroyed engine.
    check(readU16Le(before,0x4fb6+4*0x70)==1 && start.combatants.front().mechPresetId=="jenner",
        "captured opponent is not the same pristine Jenner profile");
    auto& opponent=snapshot.combatants.at(4);
    opponent.missionStatus=CombatantMissionStatus::Destroyed;opponent.mechDestroyed=true;
    importActor(opponent,final,4);
    check(campaignDefeatedOpponents(snapshot)==1,"observed defeated count differs");
    OriginalSalvageRecords trophies;appendCampaignSalvage(trophies,snapshot,true);
    check(trophies[0].chassis==readU16Le(transfer,0xd) && trophies[0].condition==readU16Le(transfer,0xf),
        "observed live trophy differs");
    world.setMissionStatus(opponent.id,CombatantMissionStatus::Destroyed);
    world.tick();check(world.battleResult() && world.battleResult()->rawResultCode==0,"adapter fixture needs raw0 result");
    auto& runtime=app.campaignBattleRuntime_;
    runtime.world=std::move(world);runtime.startSnapshot=start;runtime.currentPresentationSnapshot=snapshot;
    runtime.presentationSnapshotsValid=true;runtime.missionParticipantCount=4;
    runtime.persistentStateBefore=app.captureCampaignPersistentState();
    for(int n=0;n<4;++n)runtime.persistentRosterBindings.push_back({"player:"+std::to_string(n),n,n});
    // Factor100 is observed from full balance141K on the DOS result screen.
    // Its random draw and the post-return startup seed are outside this comparison.
    app.missionSequence_.employerPayoutPercent=uint16_t{100};app.missionSequence_.campaignModuleReturned=true;
    app.repairSession_.rng={5703,6536};
    app.finalizeCampaignBattleRuntimeOutcome();
    check(app.missionDebriefOutcome_==MissionOutcome::Victory,"live victory presented as failure");
    check(app.missionDebriefPayment_==141000 && app.missionDebriefSalvage_==85000,"observed payout differs");
    check(app.missionParticipants_[0].armorPercent==43 && app.missionParticipants_[0].conditionPercent==79,
        "debrief confuses armor with whole-mech condition or retains the original crew-index bug");
    check(app.acceptedBattleMechReturnPlan_.valid,"missing live return plan");
    const auto out=fs::path("outputs/phase15_acceptance_audit/cpp");fs::create_directories(out);
    app.render();capture(out/"live-result.ppm",app.framebuffer_);
    const auto originalRgb=readFile("research/fixtures/dosbox_live_failure/original-victory-result.rgb");
    check(originalRgb.size()==320*200*3,"missing original victory pixels");
    auto visualMismatch=[&](bool includeCondition) {
        size_t mismatches=0;
        for(size_t y=0;y<200;++y)for(size_t x=0;x<320;++x) {
            if(!includeCondition && x>=264 && x<288 && y>=70 && y<78) continue;
            const auto i=y*320+x;const auto p=app.framebuffer_[i];
            const auto expected=(uint32_t(originalRgb[i*3])<<16)|(uint32_t(originalRgb[i*3+1])<<8)|originalRgb[i*3+2];
            mismatches+=(p&0xffffff)!=expected;
        }
        return mismatches;
    };
    check(visualMismatch(false)==0,"victory pixels differ outside the intentional condition correction");
    app.missionParticipants_[0].conditionPercent=81;app.render();
    check(visualMismatch(true)==0,"original-value victory visual control differs");
    app.missionParticipants_[0].conditionPercent=79;app.render();
    app.dismissMissionDebrief();check(app.state_==ScreenState::MainMenu,"result acknowledgment failed");
    auto actual=entry;app.writeRuntimeToGamSave(actual);
    // Generated market/recruit/contract/news rows use a different permitted RNG
    // history. Compare deterministic result, wallet, date, roster and owned data.
    const std::array<std::pair<size_t,size_t>,8> ranges{{{0xd,0x21},{0x31,0x38},{0x49,0x77},
        {0xa2,0xd6},{0xe8,0x2a6},{0x2fa,0x342},{0x645,0x66f},{0x712,0x714}}};
    for(const auto& range:ranges)for(size_t at=range.first;at<range.second;++at)
        check(actual[at]==disk[at],"live campaign consequence differs at "+std::to_string(at)+
            ": actual="+std::to_string(actual[at])+" expected="+std::to_string(disk[at]));
    check(app.playerWealth_==1328624,"live wallet/payroll differs");
    check(app.saveCurrentGame(out/"from-live.GAM"),"live result Save failed");
    app.loadGameFromFile(out/"from-live.GAM");
    auto restored=entry;app.writeRuntimeToGamSave(restored);check(restored==actual,"result Restore changed state");
    app.loadGameFromFile(root/"live-dos.GAM");
    check(app.saveCurrentGame(out/"no-action.GAM") && readFile(out/"no-action.GAM")==disk,
        "new organic DOS Save lost byte compatibility");
    check(readFile(root/"live-cpp.GAM")==disk,"pinned actual C++ roundtrip differs");
    check(readFile(root/"live-dos-back.GAM")==disk,"pinned DOS -> C++ -> DOS roundtrip differs");
    app.drawStatusInfoPanel(0,0,157);
    auto viewed=disk;app.writeRuntimeToGamSave(viewed);
    check(viewed==readFile(root/"postwin-status.gam"),"STATUS cache refresh differs from live DOS");
    std::cout<<"mw_campaign_live_victory_smoke: ok checks="<<checks<<" live_dos_win=1 actors=5 gam_bytes=1816\n";
}
int main(){try{runCampaignLiveVictorySmoke();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
