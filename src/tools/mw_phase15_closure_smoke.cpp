// Full-game DOS observations, with explicit adapter boundaries for C++ comparison.
#include "../main.cpp"
#include <iostream>

namespace {
using namespace mw::battle;
size_t checks=0;
void check(bool ok,const std::string& why){++checks;if(!ok)throw std::runtime_error(why);}
void capture(const fs::path& path,const std::vector<uint32_t>& pixels){
    fs::create_directories(path.parent_path());std::ofstream f(path,std::ios::binary);f<<"P6\n320 200\n255\n";
    for(auto p:pixels){const char rgb[]={char(p>>16),char(p>>8),char(p)};f.write(rgb,3);}check(bool(f),"capture failed");
}
template<class Skip> void equalPixels(const std::vector<uint32_t>& pixels,const fs::path& path,Skip skip){
    const auto rgb=readFile(path);check(rgb.size()==64000*3,"missing original pixels");
    size_t count=0;
    for(size_t i=0;i<64000;++i)if(!skip(static_cast<int>(i%320),static_cast<int>(i/320))) {
        const auto expected=(uint32_t{rgb[i*3]}<<16)|(uint32_t{rgb[i*3+1]}<<8)|rgb[i*3+2];
        count+=(pixels[i]&0xffffff)!=expected;
    }
    check(count==0,path.string()+": differing pixels="+std::to_string(count));
}
void importActor(CombatantSnapshot& actor,const std::vector<uint8_t>& ds,size_t slot=0,bool destroyed=false){
    const size_t base=0x4fb6+0x70*slot;
    constexpr std::array<size_t,9> ext{{1,0,3,2,5,6,8,7,4}};
    constexpr std::array<size_t,8> in{{1,0,3,2,5,7,6,4}};
    constexpr std::array<size_t,10> crit{{0x11,0x13,0xf,0xd,0xb,0x68,0x6a,0x6c,0x6e,0x17}};
    auto& d=actor.detailedDamage;
    for(size_t n=0;n<9;++n){auto& a=d.armorSections[ext[n]];a.armorRemaining=readU16Le(ds,base+0x19+n*2);a.battleDamage=a.armorMaximum-a.armorRemaining;}
    for(size_t n=0;n<8;++n){auto& a=d.internalSections[in[n]];a.structureRemaining=readU16Le(ds,base+0x2b+n*2);a.battleDamage=a.structureMaximum-a.structureRemaining;}
    for(size_t n=0;n<10;++n){auto& c=d.criticalComponents[n];const auto v=readU16Le(ds,base+crit[n]);
        if(n==4||n==9)c.workingCount=static_cast<uint8_t>(c.totalCount-v);
        else if(n>=5&&n<=8)c.workingCount=static_cast<uint8_t>(v);
        else{c.condition=static_cast<uint8_t>(v);c.workingCount=static_cast<uint8_t>(v<(n==0?3:2));}
        c.functional=c.totalCount==0||c.workingCount!=0;
    }
    d.originalCockpitCriticalFlag=readU16Le(ds,base+0x15)!=0;
    for(const auto& mount:mw::mech3d::catalogWeaponMounts(actor.mechPresetId))d.installedWeapons[mount.slotIndex].functional=ds.at(base+0x3b+3*mount.slotIndex)!=0;
    for(size_t n=0;n<6;++n)actor.ammunitionByPool[n]=readU16Le(ds,base+0x59+2*n);
    if(destroyed){actor.mechDestroyed=true;actor.missionStatus=CombatantMissionStatus::Destroyed;}
}
}
void runPhase15ClosureSmoke(){
    const fs::path root="research/fixtures/phase15_closure/fatal";
    const auto entry=readFile(root/"live-entry.gam"),ds=readFile(root/"live-final.ds.bin"),returned=readFile(root/"fatal-returned.gam");
    check(entry.size()==1816 && ds.size()==65536 && returned[9]==1 && returned[0x1f]==1,"missing live fatal fixture");
    check(readU16Le(ds,0x4fcb)==1,"original cockpit critical missing");
    App app(findResourceRoot());app.loadResources();app.loadGameFromFile(root/"live-entry.gam");
    app.beginAcceptedContractMissionSequence();app.missionParticipants_=app.currentMissionParticipants();
    BattleStartParams p;p.constrainToTerrainBounds=false;p.playerMechPresetId="battlemaster";
    p.deterministicCombatRuntimeEnabled=p.individualWeaponRuntimeEnabled=p.mechSystemsSnapshotEnabled=true;
    p.setupMetadata.valid=true;p.setupMetadata.playerMode=4;p.setupMetadata.playerSlots.resize(4);
    p.setupMetadata.playerSlots[0].transform={10000,0,10000,0};app.applyCampaignPlayerLaunchStates(p);
    p.contract=app.battleContractMetadataFromOffer(app.acceptedContract_);app.acceptedBattleContract_=p.contract;
    p.combatConclusionDelaySeconds=0;auto world=BattleWorld::create(p);const auto start=world.snapshot();auto final=start;
    importActor(final.combatants[0],ds,0,true);check(validatePersistentMechDamage(campaignMechStateFromCombatant(final.combatants[0]),"battlemaster"),"invalid fatal return");
    world.setMissionStatus(world.playerEntityId(),CombatantMissionStatus::Destroyed);world.tick();
    auto& rt=app.campaignBattleRuntime_;rt.world=std::move(world);rt.startSnapshot=start;rt.currentPresentationSnapshot=final;
    rt.presentationSnapshotsValid=true;rt.missionParticipantCount=1;rt.persistentStateBefore=app.captureCampaignPersistentState();
    rt.persistentRosterBindings.push_back({"player:0",0,0});app.missionSequence_.employerPayoutPercent=uint16_t{100};
    app.missionSequence_.campaignModuleReturned=true;const auto before=app.captureCampaignPersistentState().fingerprint;
    app.finalizeCampaignBattleRuntimeOutcome();check(app.missionDebriefOutcome_==MissionOutcome::Death && app.missionParticipants_[0].killed,"fatal DOS actor did not select death");
    check(app.playerWealth_==1117024 && app.currentYear_==3024 && app.currentMonth_==3 && app.currentMonthDayCounter_==2,"death applied payroll/calendar");
    app.render();capture("outputs/phase15_closure/cpp/fatal.ppm",app.framebuffer_);
    const auto condition=[](int x,int y){return x>=264 && x<288 && y>=70 && y<102;};
    equalPixels(app.framebuffer_,root/"original-fatal-result.rgb",condition);
    app.dismissMissionDebrief();check(app.state_==ScreenState::MissionDebrief,"dead commander continued campaign");
    check(!app.commitAcceptedBattleConsequencePlan().committed,"dead commander committed ordinary result");
    check(app.captureCampaignPersistentState().fingerprint==before,"death guard changed campaign persistence");
    app.missionDeathMenuIndex_=1;app.render();capture("outputs/phase15_closure/cpp/fatal-quit.ppm",app.framebuffer_);
    equalPixels(app.framebuffer_,root/"original-fatal-quit.rgb",condition);
    app.handleKey(VK_UP);check(app.missionDeathMenuIndex_==0,"death selection did not move");
    app.handleKey(VK_RETURN);check(app.ownedMechs_.size()==1 && app.hiredCrewCount()==1 && !app.contractAccepted_ &&
        app.currentYear_==3024 && app.currentMonth_==3 && app.currentMonthDayCounter_==1,"PLAY AGAIN state: owned="+
        std::to_string(app.ownedMechs_.size())+" crew="+std::to_string(app.hiredCrewCount())+" contract="+std::to_string(app.contractAccepted_)+
        " date="+std::to_string(app.currentYear_)+"/"+std::to_string(app.currentMonth_)+"/"+std::to_string(app.currentMonthDayCounter_));
    app.endingFrame_=44;app.renderCampaignEnding();capture("outputs/phase15_closure/cpp/ending-final.ppm",app.framebuffer_);
    const auto nothing=[](int,int){return false;};
    equalPixels(app.framebuffer_,"research/fixtures/phase15_closure/ending/original-ending-stable.rgb",nothing);
    app.endingFrame_=45;app.finishCampaignEnding();app.renderStoryScene();capture("outputs/phase15_closure/cpp/ending-choice.ppm",app.framebuffer_);
    equalPixels(app.framebuffer_,"research/fixtures/phase15_closure/ending/original-ending-choice.rgb",nothing);
    app.currentStoryChoiceIndex_=1;app.renderStoryScene();
    equalPixels(app.framebuffer_,"research/fixtures/phase15_closure/ending/original-ending-quit.rgb",nothing);

    // Natural three-stage DOS chain. Import its terminal actor/cumulative trophy
    // boundary; earlier service/random trajectories are not replayed here.
    const fs::path extended="research/fixtures/phase15_closure/extended";
    const auto exEntry=readFile(extended/"prepared-entry.gam"),exDs=readFile(extended/"live-final.ds.bin");
    const auto transfer=readFile(extended/"live-exit-transfer.gam"),disk=readFile(extended/"live-dos.GAM");
    check(exEntry==readFile(extended/"initial-observed.gam"),"extended input/observed entry differ");
    App ex(findResourceRoot());ex.loadResources();ex.loadGameFromFile(extended/"prepared-entry.gam");
    ex.beginAcceptedContractMissionSequence();ex.missionParticipants_=ex.currentMissionParticipants();
    const auto seq=originalBattleSequencePlan(3);check(seq.stageCount==3,"extended row changed");
    for(size_t n=0;n<3;++n)check(originalBattlePlacementSelectorForMission(seq.missionIds[n])==std::array<size_t,3>{16,7,29}[n],"live stage selector differs");
    ex.missionSequence_.originalRow=3;ex.missionSequence_.missionIds=seq.missionIds;ex.missionSequence_.stageIndex=2;
    ex.missionSequence_.salvageRecords=decodeOriginalCampaignResult(transfer).salvage;
    BattleStartParams ep=p;ep.playerMechPresetId="jenner";ep.setupMetadata.playerSlots.resize(4);
    for(size_t n=0;n<4;++n)ep.setupMetadata.playerSlots[n].transform={10000+1500.*n,0,10000,0};
    ex.applyCampaignPlayerLaunchStates(ep);ep.contract=ex.battleContractMetadataFromOffer(ex.acceptedContract_);
    ep.combatAiPolicy=BattleCombatAiPolicy::ReplacementDeterministicCombatAi;
    ep.originalMajorSystemRuntimeEnabled=ep.originalHeatRuntimeEnabled=true;
    ep.originalProjectileRuntimeEnabled=ep.deterministicCollisionRuntimeEnabled=true;
    auto delayedParams=ep;delayedParams.combatConclusionDelaySeconds=5;
    auto delayed=BattleWorld::create(delayedParams);delayed.tick();
    check(!delayed.battleResult(),"empty-side result bypassed accepted5s delay");
    const auto firstElapsed=delayed.snapshot().elapsedMs;
    while(delayed.snapshot().elapsedMs-firstElapsed<5000-static_cast<uint64_t>(delayed.fixedTickSeconds()*500)) {
        check(!delayed.battleResult(),"empty-side delay ended early");delayed.tick();
    }
    check(delayed.battleResult() && delayed.battleResult()->rawResultCode==0,"empty-side delayed victory missing");
    ex.acceptedBattleContract_=ep.contract;auto ew=BattleWorld::create(ep);const auto es=ew.snapshot();auto ef=es;
    for(size_t n=0;n<4;++n){importActor(ef.combatants[n],exDs,n);
        auto bytes=exEntry;encodeOriginalGamMechDamage(bytes,0x102+29*n,campaignMechStateFromCombatant(ef.combatants[n]));
        for(size_t i=0x102+29*n;i<0x102+29*(n+1);++i)check(bytes[i]==transfer[i],"extended terminal damage export differs");}
    ew.tick();check(ew.battleResult() && ew.battleResult()->rawResultCode==0,"empty final opposition must win");
    auto& er=ex.campaignBattleRuntime_;er.world=std::move(ew);er.startSnapshot=es;er.currentPresentationSnapshot=ef;
    er.presentationSnapshotsValid=true;er.missionParticipantCount=4;er.persistentStateBefore=ex.captureCampaignPersistentState();
    for(int n=0;n<4;++n)er.persistentRosterBindings.push_back({"player:"+std::to_string(n),n,n});
    ex.missionSequence_.employerPayoutPercent=uint16_t{100};ex.missionSequence_.campaignModuleReturned=true;ex.repairSession_.rng={5703,6536};
    ex.finalizeCampaignBattleRuntimeOutcome();
    check(ex.missionDebriefPayment_==141000 && ex.missionDebriefSalvage_==67000,"extended live payout differs");
    ex.render();capture("outputs/phase15_closure/cpp/extended-result.ppm",ex.framebuffer_);
    equalPixels(ex.framebuffer_,extended/"original-extended-result.rgb",condition);
    ex.dismissMissionDebrief();auto actual=exEntry;ex.writeRuntimeToGamSave(actual);
    const std::array<std::pair<size_t,size_t>,8> ranges{{{0xd,0x21},{0x31,0x38},{0x49,0x77},
        {0xa2,0xd6},{0xe8,0x2a6},{0x2fa,0x342},{0x645,0x66f},{0x712,0x714}}};
    for(const auto& range:ranges)for(size_t i=range.first;i<range.second;++i)
        check(actual[i]==disk[i],"extended deterministic result field "+std::to_string(i)+" differs");
    check(ex.playerWealth_==1310624,"extended wallet/payroll differs");
    ex.loadGameFromFile(extended/"live-dos.GAM");check(ex.saveCurrentGame("outputs/phase15_closure/cpp/extended.GAM"),"extended Save failed");
    check(readFile("outputs/phase15_closure/cpp/extended.GAM")==disk && readFile(extended/"live-cpp.GAM")==disk &&
        readFile(extended/"live-dos-back.GAM")==disk,"extended1816-byte roundtrip differs");
    for(const auto& preset:mw::mech3d::mechCatalogEntries()) {
        App visual(findResourceRoot());visual.loadResources();visual.loadGameFromFile("research/fixtures/dosbox_live_victory/live-entry.gam");
        const auto chassis=App::battleChassisForMechPreset(preset.presetId);if(!chassis)continue;
        visual.ownedMechs_[0]=App::makeMech(*chassis,0);
        if(preset.presetId=="phoenix_hawk" || preset.presetId=="marauder")visual.ownedMechs_[0].ammoPacks.fill(0);
        visual.missionParticipants_=visual.currentMissionParticipants();
        visual.acceptedBattleContract_=visual.battleContractMetadataFromOffer(visual.acceptedContract_);
        visual.beginAcceptedContractMissionSequence();visual.beginCampaignBattleRuntime();
        check(visual.campaignBattleRuntime_.active,"visual battle setup failed");
        visual.campaignBattleRuntime_.world->tick();
        const auto snapshot=visual.campaignBattleRuntime_.world->snapshot();
        for(auto mode:{CampaignBattlePresentationMode::MissionStatus,CampaignBattlePresentationMode::Cockpit,CampaignBattlePresentationMode::CockpitCommandMap}){
            visual.campaignBattleRuntime_.presentationMode=mode;
            visual.renderCampaignBattleRuntime(snapshot);
            capture(fs::path("outputs/phase15_closure/cpp")/(std::string(preset.presetId)+"-"+std::to_string(static_cast<int>(mode))+".ppm"),visual.framebuffer_);
            const auto fingerprint=battleSnapshotFingerprint(visual.campaignBattleRuntime_.world->snapshot());
            visual.renderCampaignBattleRuntime(snapshot);
            check(battleSnapshotFingerprint(visual.campaignBattleRuntime_.world->snapshot())==fingerprint,"presentation changed authoritative state");
            if(campaignBattlePresentationPausesSimulation(mode)) {
                visual.updateCampaignBattleRuntime(visual.campaignBattleRuntime_.lastStepTick+10000);
                check(battleSnapshotFingerprint(visual.campaignBattleRuntime_.world->snapshot())==fingerprint,"paused STATUS/MAP advanced combat");
            }
            if(preset.presetId=="jenner" && mode==CampaignBattlePresentationMode::MissionStatus)
                equalPixels(visual.framebuffer_,"research/fixtures/phase15_closure/visual/light-original-status.rgb",
                    [](int x,int y){return x>=5 && x<202 && y>=15 && y<122;});
            if(mode==CampaignBattlePresentationMode::Cockpit) {
                const std::string family=preset.presetId=="jenner"?"light":preset.presetId=="phoenix_hawk"?"medium":preset.presetId=="marauder"?"heavy":"";
                if(!family.empty())equalPixels(visual.framebuffer_,fs::path("research/fixtures/phase15_closure/visual")/
                    (family+"-original-cockpit"+(family=="light"?"-paused":"")+".rgb"),
                    [&](int x,int y){return y<115 || (x>=110 && x<215) || (family=="medium" && x>=301 && x<304 && y>=124 && y<127);});
            }
        }
        if(preset.presetId=="marauder") {
            auto& w=*visual.campaignBattleRuntime_.world;BattleInputCommand input;
            input.tickIndex=w.tickIndex();input.entityId=w.playerEntityId();input.toggleCockpitRadar=true;
            w.enqueueInput(input);w.tick();const auto radar=w.snapshot();check(radar.cockpitRadar.active,"T did not activate radar");
            visual.campaignBattleRuntime_.presentationMode=CampaignBattlePresentationMode::Cockpit;
            visual.renderCampaignBattleRuntime(radar);capture("outputs/phase15_closure/cpp/radar.ppm",visual.framebuffer_);
            const fs::path radarPath="research/fixtures/phase15_closure/visual/heavy-original-radar.rgb";
            const auto originalRadar=readFile(radarPath);
            equalPixels(visual.framebuffer_,radarPath,[&](int x,int y){
                if(x<120 || x>=192 || y<117 || y>=184)return true;
                const auto i=static_cast<size_t>(y*320+x);
                const auto original=(uint32_t{originalRadar[i*3]}<<16)|(uint32_t{originalRadar[i*3+1]}<<8)|originalRadar[i*3+2];
                const auto current=visual.framebuffer_[i]&0xffffff;
                // Live contact positions belong to independently running AI.
                return original==0xff5555 || original==0x5555ff || current==0xff5555 || current==0x5555ff;
            });
        }
    }
    std::cout<<"mw_phase15_closure_smoke: ok checks="<<checks<<" live_fatal=1\n";
}
int main(){try{runPhase15ClosureSmoke();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

