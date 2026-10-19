// Live DOS loss: import the observed final actor at the App adapter boundary.
// Combat trajectories and random market regeneration are not compared.
#include "../main.cpp"
#include <iostream>

namespace {
using namespace mw::battle;
size_t checks=0;
void check(bool ok,const std::string& why) {++checks;if(!ok)throw std::runtime_error(why);}
void importActor(CombatantSnapshot& actor,const std::vector<uint8_t>& ds) {
    constexpr size_t base=0x4fb6;
    constexpr std::array<size_t,9> external{{1,0,3,2,5,6,8,7,4}};
    constexpr std::array<size_t,8> internal{{1,0,3,2,5,7,6,4}};
    constexpr std::array<size_t,10> critical{{0x11,0x13,0xf,0xd,0xb,0x68,0x6a,0x6c,0x6e,0x17}};
    auto& d=actor.detailedDamage;
    for(size_t n=0;n<9;++n) {
        auto& a=d.armorSections[external[n]];a.armorRemaining=readU16Le(ds,base+0x19+n*2);
        check(a.armorRemaining<=a.armorMaximum,"captured armor exceeds capacity");a.battleDamage=a.armorMaximum-a.armorRemaining;
    }
    for(size_t n=0;n<8;++n) {
        auto& a=d.internalSections[internal[n]];a.structureRemaining=readU16Le(ds,base+0x2b+n*2);
        check(a.structureRemaining<=a.structureMaximum,"captured internal exceeds capacity");a.battleDamage=a.structureMaximum-a.structureRemaining;
    }
    for(size_t n=0;n<10;++n) {
        auto& c=d.criticalComponents[n];const auto value=readU16Le(ds,base+critical[n]);
        if(n==4 || n==9) {check(value<=c.totalCount,"captured component count exceeds capacity");c.workingCount=static_cast<uint8_t>(c.totalCount-value);}
        else if(n>=5 && n<=8)c.workingCount=static_cast<uint8_t>(value);
        else {c.condition=static_cast<uint8_t>(value);c.workingCount=static_cast<uint8_t>(value<(n==0?3:2));}
        c.functional=c.totalCount==0 || c.workingCount!=0;
    }
    d.originalCockpitCriticalFlag=readU16Le(ds,base+0x15)!=0;
    for(const auto& mount:mw::mech3d::catalogWeaponMounts(actor.mechPresetId))
        d.installedWeapons[mount.slotIndex].functional=ds.at(base+0x3b+3*mount.slotIndex)!=0;
    for(size_t n=0;n<6;++n)actor.ammunitionByPool[n]=readU16Le(ds,base+0x59+2*n);
    actor.mechDestroyed=true;actor.missionStatus=CombatantMissionStatus::Destroyed;
}
void capture(const fs::path& path,const std::vector<uint32_t>& pixels) {
    std::ofstream f(path,std::ios::binary);f<<"P6\n320 200\n255\n";
    for(auto p:pixels){const char rgb[]={char(p>>16),char(p>>8),char(p)};f.write(rgb,3);}
    check(bool(f),"cannot save comparison image");
}
}

void runCampaignLiveFailureSmoke() {
    const fs::path root="research/fixtures/dosbox_live_failure";
    const auto entry=readFile(root/"live-entry.gam"),before=readFile(root/"live-entry.ds.bin");
    const auto final=readFile(root/"live-final.ds.bin"),transfer=readFile(root/"live-exit-transfer.gam");
    const auto disk=readFile(root/"live-dos.GAM");
    check(entry.size()==1816 && final.size()==65536 && disk.size()==1816,"incomplete live loss fixture");
    check(transfer[0x1f]==1 && transfer[9]==0 && transfer[0x1d]==0 &&
        readU16Le(before,0x4fc7)==2 && readU16Le(final,0x4fc7)==3,"fixture is not a surviving engine-loss commander");
    App app(findResourceRoot());app.loadResources();app.loadGameFromFile(root/"live-entry.gam");
    app.beginAcceptedContractMissionSequence();app.missionParticipants_=app.currentMissionParticipants();
    check(app.hiredCrewCount()==4 && app.missionParticipants_.size()==1,"unassigned crew incorrectly deployed");
    BattleStartParams params;params.constrainToTerrainBounds=false;
    params.deterministicCombatRuntimeEnabled=params.individualWeaponRuntimeEnabled=params.mechSystemsSnapshotEnabled=true;
    params.setupMetadata.valid=true;params.setupMetadata.playerMode=4;params.setupMetadata.playerSlots.resize(4);
    params.setupMetadata.playerSlots[0].transform={10000,0,10000,0};app.applyCampaignPlayerLaunchStates(params);
    params.contract=app.battleContractMetadataFromOffer(app.acceptedContract_);app.acceptedBattleContract_=params.contract;
    params.combatConclusionDelaySeconds=0; // Supplied terminal boundary, not combat replay.
    auto world=BattleWorld::create(params);const auto start=world.snapshot();auto snapshot=start;
    check(snapshot.combatants.size()==1,"reserve pilots acquired combat actors");
    auto& actor=snapshot.combatants[0];importActor(actor,final);
    const auto state=campaignMechStateFromCombatant(actor);check(validatePersistentMechDamage(state,actor.mechPresetId),"invalid captured return");
    auto exported=entry;encodeOriginalGamMechDamage(exported,0x102,state);
    for(size_t at=0x102;at<0x11f;++at)check(exported[at]==transfer[at],"live damage export differs");
    world.setMissionStatus(world.playerEntityId(),CombatantMissionStatus::Destroyed);world.tick();
    check(world.battleResult() && world.battleResult()->rawResultCode==1,"controlled adapter needs raw1");
    auto& rt=app.campaignBattleRuntime_;rt.world=std::move(world);rt.startSnapshot=start;
    rt.currentPresentationSnapshot=snapshot;rt.presentationSnapshotsValid=true;rt.missionParticipantCount=1;
    rt.persistentStateBefore=app.captureCampaignPersistentState();rt.persistentRosterBindings.push_back({"player:0",0,0});
    app.missionSequence_.employerPayoutPercent=uint16_t{100};app.missionSequence_.campaignModuleReturned=true;
    app.repairSession_.rng={5703,6536};app.finalizeCampaignBattleRuntimeOutcome();
    check(app.missionDebriefOutcome_==MissionOutcome::Defeat && !app.missionParticipants_[0].killed,"engine loss killed commander");
    check(app.missionDebriefPayment_==0 && app.missionDebriefSalvage_==0,"loss paid money");
    check(app.missionParticipants_[0].conditionPercent==56,"wrong chassis-based condition percentage");
    const auto out=fs::path("outputs/phase15_failure_acceptance/cpp");fs::create_directories(out);
    app.render();capture(out/"loss-result.ppm",app.framebuffer_);
    const auto originalRgb=readFile(root/"original-loss-result.rgb");
    check(originalRgb.size()==320*200*3,"missing original full-color result");
    auto visualMismatch=[&](bool includeCondition) {
        size_t mismatches=0;
        for(size_t y=0;y<200;++y)for(size_t x=0;x<320;++x) {
            if(!includeCondition && x>=264 && x<288 && y>=70 && y<77) continue;
            const auto i=y*320+x;const auto p=app.framebuffer_[i];
            const auto expected=(uint32_t(originalRgb[i*3])<<16)|(uint32_t(originalRgb[i*3+1])<<8)|originalRgb[i*3+2];
            mismatches+=(p&0xffffff)!=expected;
        }
        return mismatches;
    };
    check(visualMismatch(false)==0,"live DOS result differs outside intentional condition correction");
    // Use the observed buggy percentage solely to isolate renderer acceptance.
    app.missionParticipants_[0].conditionPercent=59;app.render();
    check(visualMismatch(true)==0,"original-value visual control differs from DOS result");
    app.missionParticipants_[0].conditionPercent=56;app.render();
    const auto houseBefore=app.acceptedContract_.employerHouse;
    for(const int house:{0,3})for(const bool win:{false,true}) {
        app.acceptedContract_.employerHouse=static_cast<decltype(houseBefore)>(house);
        app.missionDebriefOutcome_=win?MissionOutcome::Victory:MissionOutcome::Defeat;
        std::wstring message;
        for(const auto& line:app.missionDebriefMessageLines())message+=line;
        const auto expected=house==0?(win?L"EXCELLENT VICTORY":L"WITHOUT HONOR"):
            (win?L"CHANCELLOR LIAO":L"HOUSE LIAO");
        check(message.find(expected)!=std::wstring::npos,"employer-specific original message replaced by generic text");
    }
    app.acceptedContract_.employerHouse=houseBefore;app.missionDebriefOutcome_=MissionOutcome::Defeat;
    app.dismissMissionDebrief();check(app.state_==ScreenState::MainMenu,"surviving commander cannot continue");
    auto actual=entry;app.writeRuntimeToGamSave(actual);
    const std::array<std::pair<size_t,size_t>,8> ranges{{{9,0x21},{0x31,0x38},{0x49,0x77},
        {0xa2,0xd6},{0xe8,0x2a6},{0x2fa,0x342},{0x645,0x66f},{0x712,0x714}}};
    for(const auto& range:ranges)for(size_t at=range.first;at<range.second;++at)
        check(actual[at]==disk[at],"live defeat consequence differs at "+std::to_string(at)+
            ": actual="+std::to_string(actual[at])+" expected="+std::to_string(disk[at]));
    check(app.playerWealth_==1102624 && app.ownedMechs_.size()==4 && app.hiredCrewCount()==4,"loss lost reserve crew/mechs or wallet");
    check(app.saveCurrentGame(out/"from-live.GAM"),"loss Save failed");app.loadGameFromFile(out/"from-live.GAM");
    auto restored=entry;app.writeRuntimeToGamSave(restored);check(restored==actual,"loss Restore changed state");
    app.loadGameFromFile(root/"live-dos.GAM");check(app.saveCurrentGame(out/"no-action.GAM"),"DOS loss Save failed");
    check(readFile(out/"no-action.GAM")==disk && readFile(root/"live-cpp.GAM")==disk &&
        readFile(root/"live-dos-back.GAM")==disk,"live DOS/App/DOS disk identity differs");
    std::cout<<"mw_campaign_live_failure_smoke: ok checks="<<checks<<" live_dos_loss=1 gam_bytes=1816\n";
}
int main(){try{runCampaignLiveFailureSmoke();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
