#include "../main.cpp"
#include <iostream>

namespace {
using namespace mw::battle;
size_t checks=0;
void check(bool ok,const std::string& why) {++checks;if(!ok) throw std::runtime_error(why);}
OriginalSalvageRecords recordsFromHex(const std::string& hex) {
    OriginalSalvageRecords records;
    const auto word=[&](size_t offset) {return static_cast<uint16_t>(std::stoul(hex.substr(offset,2),nullptr,16)|
        (std::stoul(hex.substr(offset+2,2),nullptr,16)<<8));};
    for(size_t i=0;i<4;++i) records[i]={word(i*8),word(i*8+4)};
    return records;
}
bool equal(const OriginalSalvageRecords& a,const OriginalSalvageRecords& b) {
    for(size_t i=0;i<4;++i) if(a[i].chassis!=b[i].chassis || a[i].condition!=b[i].condition) return false;
    return true;
}
const std::array<const char*,8> presets{{"locust","jenner","phoenix_hawk","shadow_hawk","rifleman","warhammer","marauder","battlemaster"}};
}

void runCampaignSalvageSmoke() {
    App app(findResourceRoot());app.loadResources();const auto fresh=app.baseGamSaveData();
    std::array<int,8> maxima{},weapons{};
    for(size_t i=0;i<8;++i) {
        BattleStartParams p;p.playerMechPresetId=presets[i];p.mechSystemsSnapshotEnabled=true;
        const auto snapshot=BattleWorld::create(p).snapshot();const auto& d=snapshot.combatants.at(0).detailedDamage;
        check(d.valid,"missing source damage profile");
        for(const auto& a:d.armorSections) maxima[i]+=a.armorMaximum;
        for(const auto& a:d.internalSections) maxima[i]+=a.structureMaximum;
        weapons[i]=static_cast<int>(mw::mech3d::catalogWeaponMounts(presets[i]).size());
    }
    std::ifstream input("research/fixtures/original_salvage.txt");check(bool(input),"missing instruction oracle");
    std::string line;size_t qCount=0,cCount=0,pCount=0,bugDifferences=0;
    while(std::getline(input,line)) {
        if(line.empty() || line[0]=='#')continue;
        std::istringstream row(line);char kind;row>>kind;
        if(kind=='Q') {
            int m,e,s,c,g,w,hp,expected;row>>m>>e>>s>>c>>g>>w>>hp>>expected;
            check(originalSalvageCondition(e,s,c,g,w,weapons.at(m),hp,maxima.at(m))==expected,"BTECH condition mismatch");++qCount;
        } else if(kind=='C') {
            int initial,result,mode,mask,count;std::array<int,4> aggregates{};std::string hex;
            row>>initial>>result>>mode>>mask;for(auto& a:aggregates) row>>a;row>>count>>hex;
            OriginalSalvageRecords actual;std::array<OriginalSalvageCandidate,4> candidates{};
            constexpr std::array<uint16_t,4> ids{{0,2,3,4}};
            for(size_t i=0;i<4;++i) {
                actual[i]=static_cast<int>(i)<initial?OriginalSalvageRecord{9,42}:OriginalSalvageRecord{65535,65535};
                candidates[i]={bool(mask&(1<<i)),static_cast<int16_t>(aggregates[i]),{ids[i],static_cast<uint16_t>(100-10*i)}};
            }
            appendOriginalSalvage(actual,candidates,result,mode);
            check(equal(actual,recordsFromHex(hex)),"BTECH collection mismatch");
            check(std::count_if(actual.begin(),actual.end(),[](const auto& r){return r.chassis!=65535;})==count,"BTECH cap mismatch");++cCount;
        } else {
            unsigned mission,result,tier,house,roll,share,price,factor;uint64_t salvage,payment;std::string hex;
            row>>mission>>result>>tier>>house>>roll>>share>>price>>hex>>factor>>salvage>>payment;
            const auto records=recordsFromHex(hex);
            const auto f=originalEmployerPayoutPercent(app.salvageTables_,static_cast<uint8_t>(house),static_cast<uint16_t>(roll),mission==99);
            check(f==factor,"original employer threshold mismatch");
            const auto amount=originalSalvageAward(app.salvageTables_,records,static_cast<uint8_t>(tier+1),
                static_cast<uint16_t>(share),f,result,mission==99);
            check(amount==salvage,"MW_MAIN payout mismatch");
            CampaignBattleOutcomePackage outcome;outcome.valid=outcome.terminal=outcome.contractMetadataValid=true;
            outcome.launchSource=CampaignBattleLaunchSource::AcceptedContract;
            outcome.outcome=result==0?CampaignBattleOutcome::Victory:CampaignBattleOutcome::Defeat;
            BattleContractMetadata contract;contract.valid=true;contract.priceK=price;contract.employerHouseId=static_cast<uint8_t>(house);
            CampaignBattleConsequenceContext context;context.employerPayoutPercent=f;context.salvage=amount;context.salvageEstimateAvailable=true;
            const auto plan=campaignBattleConsequencePlanFromOutcome(outcome,contract,context);
            check(plan.payment==payment && plan.salvage==salvage,"shared employer factor not applied to plan");
            const auto corrected=originalSalvageAward(app.salvageTables_,records,static_cast<uint8_t>(tier+1),
                static_cast<uint16_t>(share),f,result,mission==99,true);
            if(corrected!=amount) ++bugDifferences;
            ++pCount;
        }
        check(bool(row),"malformed instruction fixture");
    }
    check(qCount==13815 && cCount==2160 && pCount==52520,"incomplete instruction cases");
    const auto mixed=recordsFromHex("00002300020024000700430009006400");
    check(originalSalvageAward(app.salvageTables_,mixed,1,100,100,0)==7569000,"source DI clobber no longer modeled");
    check(originalSalvageAward(app.salvageTables_,mixed,1,100,100,0,false,true)==12431000,"DI bug was ported into corrected sum");

    // Authoritative snapshot adapter: every supported model and every
    // unconditional viability failure, including loss of a leg, not flags.
    for(size_t model=0;model<8;++model) {
        BattleStartParams params;params.playerMechPresetId=presets[model];params.mechSystemsSnapshotEnabled=true;
        auto base=BattleWorld::create(params).snapshot();base.setup.valid=true;base.setup.playerMode=0;
        auto& a=base.combatants[0];a.playerControlled=false;a.roster.team=BattleTeam::Opposing;a.roster.originalLiveObjectSlot=uint8_t{4};
        for(int failure=0;failure<10;++failure) {
            auto snapshot=base;auto& actor=snapshot.combatants[0];auto& d=actor.detailedDamage;
            if(failure==0)d.criticalComponents[0].condition=3;
            if(failure==1)d.criticalComponents[3].condition=2;
            if(failure==2)d.originalCockpitCriticalFlag=true;
            if(failure==3)d.internalSections[2].structureRemaining=0;
            if(failure==4)d.internalSections[3].structureRemaining=0;
            if(failure==5)actor.mechDestroyed=true; // abstract debug flag alone isn't evidence.
            if(failure==6)d.criticalComponents[1].condition=2;
            if(failure==7)d.criticalComponents[2].condition=2;
            if(failure==8)d.criticalComponents[7].workingCount=0; // upright alone isn't salvage.
            if(failure==9){d.criticalComponents[0].condition=3;actor.missionStatus=CombatantMissionStatus::Escaped;}
            OriginalSalvageRecords records;appendCampaignSalvage(records,snapshot,true);
            check((records[0].chassis!=65535)==(failure<5),"runtime collection guessed abstract status");
            if(failure<5)check(records[0].chassis==App::gamChassisId(*App::battleChassisForMechPreset(presets[model])),"salvage chassis mapping");
            OriginalSalvageRecords none;appendCampaignSalvage(none,snapshot,false);
            check(none[0].chassis==65535,"failure awarded salvage");
            snapshot.setup.playerMode=1;appendCampaignSalvage(none,snapshot,true);
            check(none[0].chassis==65535,"mode1 awarded salvage");
        }
    }
    // Snapshot vector order must not reorder original slots, and later
    // stages cannot replace the first four captured trophies.
    {
        BattleStartParams p;p.playerMechPresetId="jenner";p.mechSystemsSnapshotEnabled=true;
        auto s=BattleWorld::create(p).snapshot();auto a=s.combatants[0];s.combatants.clear();
        s.setup.valid=true;s.setup.playerMode=0;a.roster.team=BattleTeam::Opposing;
        a.detailedDamage.criticalComponents[0].condition=3;
        for(int slot=7;slot>=4;--slot) {a.roster.originalLiveObjectSlot=static_cast<uint8_t>(slot);
            a.detailedDamage.criticalComponents[2].condition=static_cast<uint8_t>((slot-4)%3);s.combatants.push_back(a);}
        OriginalSalvageRecords r;appendCampaignSalvage(r,s,true);
        check(r[0].condition==70 && r[1].condition==67 && r[2].condition==65 && r[3].condition==70,"snapshot order changed trophies");
        const auto full=r;appendCampaignSalvage(r,s,true);check(equal(r,full),"later stage replaced trophies");
    }
    // Real terminal handler, immutable preview, guarded money+damage commit,
    // repeat acknowledgment, and explicit Save/Restore. No final-story battle.
    app.applyGamSaveToRuntime(fresh);app.missionSequence_={};app.acceptedBattlePersistenceReport_={};
    app.acceptedBattleMechReturnPlan_={};app.campaignBattleRuntime_={};app.lastCommittedBattleConsequencePlanFingerprint_=0;
    app.ownedMechs_.clear();app.resetCrewRosterForLoadedMechs();
    auto owned=App::makeMech(*App::battleChassisForMechPreset("locust"));owned.assignedCrewSlot=0;
    owned.ammoPacks=App::ammunitionCapacityForMech(owned);app.finalizeLoadedMech(owned);app.ownedMechs_.push_back(owned);
    app.acceptedContract_.employerHouse=4;app.acceptedContract_.priceK=1234;app.acceptedContract_.salvagePercent=37;
    app.acceptedContract_.missionName=L"MECH COMBAT";
    app.acceptedContract_.generationSlot=std::numeric_limits<size_t>::max();app.contractAccepted_=true;
    app.missionParticipants_=app.currentMissionParticipants();
    BattleStartParams params;applyBattleDriveRuntimeTuning(params);
    params.contract.valid=true;params.contract.provenance="campaign_contract";
    params.contract.priceK=1234;params.contract.employerHouseId=4;params.contract.salvagePercent=37;
    params.mission=defaultBattleMissionBriefing();params.mechSystemsSnapshotEnabled=true;params.combatConclusionDelaySeconds=0;
    params.deterministicCombatRuntimeEnabled=true;
    params.deterministicCollisionRuntimeEnabled=true;params.combatAiPolicy=BattleCombatAiPolicy::Disabled;
    params.constrainToTerrainBounds=false;
    params.playerMechPresetId="locust";params.playerRoster.team=BattleTeam::Player;params.playerRoster.sourceSlot="player:0";
    BattleCombatantLaunchState playerLaunch;playerLaunch.mechPresetId="locust";playerLaunch.roster=params.playerRoster;
    params.playerStartTransform={};playerLaunch.startTransform={};
    playerLaunch.persistentMechState=App::battlePersistentMechStateFromOwnedMech(app.ownedMechs_.at(0),"salvage_test");
    playerLaunch.ammunitionStateValid=true;playerLaunch.ammunitionByPool=app.ownedMechs_[0].ammoPacks;
    params.playerLaunchState=playerLaunch;
    BattleCombatantLaunchState enemy;enemy.mechPresetId="locust";enemy.roster.team=BattleTeam::Opposing;
    enemy.roster.originalLiveObjectSlot=uint8_t{4};enemy.roster.sourceSlot="opposing:0";
    enemy.startTransform={0,0,1800,3.14159265358979323846};
    params.combatantLaunchStates.push_back(enemy);
    auto intactEnemy=BattleWorld::create(params).snapshot().combatants[1];
    params.combatantLaunchStates[0].persistentMechState=campaignMechStateFromCombatant(intactEnemy);
    auto& failedActuator=params.combatantLaunchStates[0].persistentMechState.exactDamage->detailed.criticalComponents[7];
    failedActuator.workingCount=0;failedActuator.condition=3;failedActuator.functional=false;
    auto world=BattleWorld::create(params);auto start=world.snapshot();
    bool fallen=false;
    for(int i=0;i<600 && !world.battleResult();++i) {
        BattleInputCommand c;c.tickIndex=world.tickIndex();c.throttle=fallen?0:1;world.enqueueInput(c);world.tick();
        fallen=world.snapshot().combatants[1].posture.falls!=0;
    }
    check(world.battleResult().has_value(),"no actuator collision terminal result");
    auto end=world.snapshot();end.setup.valid=true;end.setup.playerMode=0;
    check(end.combatants[1].missionStatus==CombatantMissionStatus::Disabled &&
        !end.combatants[1].mechDestroyed && end.combatants[1].posture.posture==BattlePosture::Prone,
        "collision did not produce a nonfatal actuator trophy");
    app.acceptedBattleContract_=params.contract;app.campaignBattleRuntime_.world=std::move(world);
    app.campaignBattleRuntime_.startSnapshot=start;app.campaignBattleRuntime_.currentPresentationSnapshot=end;
    app.campaignBattleRuntime_.presentationSnapshotsValid=true;
    app.campaignBattleRuntime_.persistentRosterBindings.push_back({"player:0",0,0});
    app.campaignBattleRuntime_.missionParticipantCount=1;
    app.campaignBattleRuntime_.persistentStateBefore=app.captureCampaignPersistentState();
    app.missionSequence_.salvageRecords[0]={0,35}; // trophy carried from prior stage
    const auto before=app.playerWealth_;app.finalizeCampaignBattleRuntimeOutcome();
    check(app.acceptedBattleConsequencePlan_.commitEligible,"real terminal payout not eligible");
    check(app.acceptedBattleMechReturnPlan_.valid,"real damage return unavailable: "+app.acceptedBattleMechReturnPlan_.reason);
    check(app.missionSequence_.salvageRecords[0].chassis==0 && app.missionSequence_.salvageRecords[1].chassis==0 &&
        app.missionSequence_.salvageRecords[1].condition==100,"stage/actuator trophies lost");
    const auto context=app.campaignBattleConsequenceContextForAcceptedContract();
    check(context.salvage==app.missionDebriefSalvage_ && context.salvage>0 && !context.salvageBetaValidationRequired,"debrief still estimates salvage");
    check(app.playerWealth_==before,"preview changed money");
    app.render();const auto frame=app.framebuffer_;app.render();check(app.framebuffer_==frame,"render rerolled payout");
    fs::create_directories("outputs/salvage_audit");std::ofstream ppm("outputs/salvage_audit/debrief.ppm",std::ios::binary);ppm<<"P6\n320 200\n255\n";
    for(auto p:frame){const char rgb[]={char(p>>16),char(p>>8),char(p)};ppm.write(rgb,3);}ppm.close();
    // Isolate the financial transaction from already tested date/payroll/XP.
    app.acceptedBattleConsequencePlan_.campaignDateTicks=0;app.acceptedBattleConsequencePlan_.pilotExperienceAward=false;
    const auto money=app.missionDebriefPayment_+app.missionDebriefSalvage_;
    app.acceptedBattleStateGuard_.after.fingerprint^=1;
    check(!app.commitAcceptedBattleConsequencePlan().committed && app.playerWealth_==before,"stale payout committed");
    app.acceptedBattleStateGuard_.after.fingerprint^=1;
    const auto validReturn=app.acceptedBattleMechReturnPlan_;
    const auto campaignBefore=app.captureCampaignPersistentState().fingerprint;
    app.acceptedBattleMechReturnPlan_.entries[0].ammunitionByPool[0]=255;
    check(!app.commitAcceptedBattleConsequencePlan().committed &&
        app.captureCampaignPersistentState().fingerprint==campaignBefore,"failed trophy/damage commit did not roll back");
    app.acceptedBattleMechReturnPlan_=validReturn;
    check(app.commitAcceptedBattleConsequencePlan().committed && app.playerWealth_==before+money,"trophy money commit failed");
    check(app.commitAcceptedBattleConsequencePlan().duplicateRejected && app.playerWealth_==before+money,"duplicate trophies paid");
    const auto directory=fs::path("outputs/salvage_audit")/("save_"+std::to_string(GetTickCount64()));fs::create_directories(directory);
    const auto path=directory/"SALVAGE.GAM";check(app.saveCurrentGame(path),"salvage Save failed");
    const auto bytes=readFile(path);check(bytes.size()==1816,"GAM layout changed");app.loadGameFromFile(path);
    check(app.playerWealth_==before+money,"salvage Restore lost money");
    auto again=bytes;app.writeRuntimeToGamSave(again);check(bytes==again,"salvage GAM round trip changed bytes");
    check(app.missionSequence_.salvageRecords[0].chassis==65535,"Restore retained pending trophies");
    std::cout<<"mw_campaign_salvage_smoke: ok checks="<<checks<<" oracle="<<qCount+cCount+pCount
        <<" intentional_DI_bug_differences="<<bugDifferences<<" save_restore=1\n";
}
int main(){try{runCampaignSalvageSmoke();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
