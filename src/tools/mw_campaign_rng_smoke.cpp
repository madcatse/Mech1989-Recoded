#include "../main.cpp"
#include <iostream>
namespace {
using namespace mw::battle;
size_t checks=0;
void check(bool value,const std::string& message) { ++checks; if(!value)throw std::runtime_error(message); }
std::vector<uint8_t> fixture(const std::string& name) {return readFile("research/fixtures/dosbox_rng/"+name+".BIN");}
bool same(OriginalContractRng a,OriginalContractRng b) {return a.word3f5==b.word3f5 && a.word3f7==b.word3f7;}
void checkRng(OriginalContractRng r,const std::vector<uint8_t>& bytes,size_t offset) {
    check(r.word3f5==readU16Le(bytes,offset) && r.word3f7==readU16Le(bytes,offset+2),"DOS final MW_MAIN RNG differs");
}
}
void runCampaignRngSmoke() {
    const auto mi=fixture("MIN"),mo=fixture("MOUT");
    for(size_t i=0;i<mi.size()/12;++i) {
        OriginalContractRng r{readU16Le(mi,12*i),readU16Le(mi,12*i+2)};
        r.mixBiosTicks((uint32_t{readU16Le(mi,12*i+8)}<<16)|readU16Le(mi,12*i+10));
        for(unsigned j=0;j<readU16Le(mi,12*i+6);++j)r.inclusive(255);
        check(r.inclusive(readU16Le(mi,12*i+4))==readU16Le(mo,6*i),"DOS inclusive draw differs");checkRng(r,mo,6*i+2);
    }
    const auto bi=fixture("BIN"),bo=fixture("BOUT");
    for(size_t i=0;i<bi.size()/10;++i) {
        OriginalBtechRng r;r.addSeed(readU16Le(bi,10*i));r.first=readU16Le(bi,10*i+6);r.second=readU16Le(bi,10*i+8);
        uint16_t last=0;
        for(unsigned j=0;j<readU16Le(bi,10*i+4);++j)last=r.exclusive(readU16Le(bi,10*i+2));
        check(last==readU16Le(bo,70*i),"DOS exclusive draw differs");
        for(size_t j=0;j<32;++j)check(r.words[j]==readU16Le(bo,70*i+2+2*j),"DOS additive word differs");
        check(r.first==readU16Le(bo,70*i+66) && r.second==readU16Le(bo,70*i+68),"DOS RNG indexes differ");
    }
    const auto gi=fixture("GIN"),go=fixture("GOUT"),hi=fixture("HIN"),ho=fixture("HOUT");
    const auto exe=readFile("Original/MW_MAIN.EXE");const auto salvage=decodeOriginalSalvageTables(exe);
    for(size_t i=0;i<gi.size()/6;++i) {
        OriginalContractRng r{readU16Le(gi,6*i),readU16Le(gi,6*i+2)};
        check(originalUneventfulContract(static_cast<uint8_t>(readU16Le(gi,6*i+4)),r)==(readU16Le(go,6*i)!=0),"DOS no-battle branch differs");checkRng(r,go,6*i+2);
    }
    for(size_t i=0;i<hi.size()/8;++i) {
        OriginalContractRng r{readU16Le(hi,8*i),readU16Le(hi,8*i+2)};
        const auto value=readU16Le(hi,8*i+4)==99 ? 100 : originalEmployerPayoutPercent(salvage,
            static_cast<uint8_t>(readU16Le(hi,8*i+6)),r.inclusive(100));
        check(value==readU16Le(ho,6*i),"DOS employer gate differs");checkRng(r,ho,6*i+2);
    }
    App app(findResourceRoot());app.loadResources();
    const auto ni=fixture("NIN"),no=fixture("NOUT");
    const auto contractTables=decodeOriginalContractTables(exe);
    // Run the actual production new-game sequence from each controlled DOS state.
    for(size_t i=0;i<ni.size()/4;++i) {
        app.currentYear_=3024;app.currentMonth_=3;app.currentMonthDayCounter_=1;app.currentPeriodic14DayCounter_=0;
        app.resetReputationAndHouseStanding();app.resetCrewRoster();
        app.recruitPilots_=app.loadRecruitPilots(exe);
        app.repairSession_.rng={readU16Le(ni,4*i),readU16Le(ni,4*i+2)};
        app.initializeOriginalCampaignGeneration();
        const size_t o=1820*i;std::vector<uint8_t> gam(no.begin()+o,no.begin()+o+1816);
        const auto context=" new-game case="+std::to_string(i);
        checkRng(app.repairSession_.rng,no,o+1816);
        check(app.planets_[static_cast<size_t>(app.currentPlanetIndex_)].tableOrder==readU16Le(gam,0x21)+1,"DOS starting planet"+context);
        check(app.serviceFactor_==readU16Le(gam,0x2f),"DOS service factor"+context);
        check(app.contactNegotiationSkill_==readU16Le(gam,0x59e),"DOS contact skill"+context);
        check(app.playerWealth_==App::readGamU32Le(gam,0x49),"DOS new-game money"+context);
        for(size_t d=0;d<6;++d)check(app.storyDestinationSaveIds_[d]==readU16Le(gam,0x6fe+2*d),"DOS story destination"+context+" field="+std::to_string(d)+" expected="+std::to_string(readU16Le(gam,0x6fe+2*d))+" actual="+std::to_string(app.storyDestinationSaveIds_[d]));
        std::vector<uint8_t> own(29);App::writeGamMechRecord(own,0,app.ownedMechs_[0]);
        check(std::equal(own.begin(),own.end(),gam.begin()+0x102),"DOS starting damage"+context);
        const auto ammo=decodeOriginalGamOwnedMechAmmunition(gam,0,App::ammunitionCapacityForMech(app.ownedMechs_[0]));
        check(app.ownedMechs_[0].ammoPacks==ammo,"DOS starting ammunition"+context);
        check(app.recruitBoard_==decodeOriginalRecruitment(gam).board,"DOS recruit board"+context);
        const auto market=decodeOriginalMechMarket(gam);const auto& actual=app.mechMarket_.original;
        check(market.saleQuota==actual.saleQuota && market.count()==actual.count(),"DOS market count/quota"+context);
        for(size_t slot=0;slot<market.count();++slot) {
            const auto& a=actual.slots[slot];const auto& b=market.slots[slot];
            check(a.chassis==b.chassis && a.condition==b.condition,"DOS market condition"+context);
            for(size_t j=0;j<app.marketTables_.ammoCounts[a.chassis];++j)
                check(a.ammunition[j]==b.ammunition[j] && a.ammoTypes[j]==b.ammoTypes[j],"DOS market ammo"+context);
        }
        auto projected=gam;app.writeRuntimeToGamSave(projected);
        auto boardOnly=gam;encodeOriginalContractBoard(boardOnly,contractTables,decodeOriginalContractBoard(projected,contractTables));
        check(boardOnly==gam,"DOS complete contract board differs"+context);
    }
    const auto fresh=readFile("research/fixtures/dosbox_story/start.gam");
    // Replay the original calendar's traced call order using real generators.
    // The DOS trace hooks use small bounds; N above verifies real board bodies.
    App reference(findResourceRoot());reference.loadResources();
    const auto ci=fixture("CIN"),co=fixture("COUT");
    for(size_t i=0;i<ci.size()/14;++i) {
        app.applyGamSaveToRuntime(fresh);reference.applyGamSaveToRuntime(fresh);
        app.currentMonthDayCounter_=readU16Le(ci,i*14+4);app.currentMonth_=readU16Le(ci,i*14+6);
        app.currentYear_=readU16Le(ci,i*14+8);app.currentPeriodic14DayCounter_=readU16Le(ci,i*14+10);
        app.repairSession_.rng={readU16Le(ci,i*14),readU16Le(ci,i*14+2)};
        reference.repairSession_.rng=app.repairSession_.rng;
        reference.currentMonthDayCounter_=readU16Le(co,i*24);reference.currentMonth_=readU16Le(co,i*24+2);
        reference.currentYear_=readU16Le(co,i*24+4);reference.currentPeriodic14DayCounter_=readU16Le(co,i*24+6);
        const auto trace=readU16Le(co,i*24+16);
        check(trace==4 || trace==0x1234,"DOS calendar trace changed");
        for(int shift=12;shift>=0;shift-=4) switch((trace>>shift)&15) {
            case 1:reference.generateCurrentRecruitBoard();break;
            case 2:reference.generateCurrentMechMarket();break;
            case 3:reference.generateCurrentPlanetContracts();break;
            default:break;
        }
        app.advanceCampaignDays(readU16Le(ci,i*14+12));
        check(same(app.repairSession_.rng,reference.repairSession_.rng),"calendar shared draw order differs from DOS");
        check(app.currentMonthDayCounter_==reference.currentMonthDayCounter_ && app.currentMonth_==reference.currentMonth_ &&
            app.currentYear_==reference.currentYear_ && app.currentPeriodic14DayCounter_==reference.currentPeriodic14DayCounter_,"DOS calendar update differs");
        auto actual=fresh,expectedGam=fresh;app.writeRuntimeToGamSave(actual);reference.writeRuntimeToGamSave(expectedGam);
        check(actual==expectedGam,"calendar generators observed wrong updated date/state");
    }
    app.applyGamSaveToRuntime(fresh);app.repairSession_.rng={5703,474};
    auto expected=app.repairSession_.rng;
    generateOriginalMechMarket(app.marketTables_,app.currentPlanetEconomyTier(),expected,app.mechMarket_.original);
    app.generateCurrentMechMarket();check(same(expected,app.repairSession_.rng),"market didn't share campaign stream");
    auto board=generateOriginalRecruitBoard(app.originalRecruitmentState().pilots,app.currentPlanetEconomyTier(),app.playerReputationTier_,{},expected);
    app.generateCurrentRecruitBoard();check(same(expected,app.repairSession_.rng) && board==app.recruitBoard_,"recruitment restarted shared stream");
    auto local=app.repairSession_;local.rng=expected;
    const auto available=originalRepairAvailable(app.repairTables_,local,2,app.currentPlanetEconomyTier(),2);
    const auto actualAvailable=originalRepairAvailable(app.repairTables_,app.repairSession_,2,app.currentPlanetEconomyTier(),2);
    check(available==actualAvailable && same(local.rng,app.repairSession_.rng),"repair didn't continue board stream");
    const auto after=app.repairSession_.rng;
    app.initializeRepairSession();check(same(after,app.repairSession_.rng),"cache clearing reseeded RNG");
    app.applyGamSaveToRuntime(fresh);check(same(after,app.repairSession_.rng),"Restore rewound process RNG");
    const auto saveDir=fs::path("outputs/rng_audit")/("save_"+std::to_string(GetTickCount64()));fs::create_directories(saveDir);
    check(app.saveCurrentGame(saveDir/"RNG.GAM"),"Save failed");check(same(after,app.repairSession_.rng),"Save advanced RNG");
    check(fs::file_size(saveDir/"RNG.GAM")==1816,"GAM layout changed");
    app.restartCampaignModuleRng(0x123456);OriginalContractRng restart{1557,6536};restart.mixBiosTicks(0x123456);
    check(same(restart,app.repairSession_.rng),"module restart didn't restore executable state + BIOS ticks");
    // Render/preview must not perturb RNG; rejected final99/unsupported has no employer draw.
    const auto beforePreview=app.repairSession_.rng;
    for(int i=0;i<10;++i)app.campaignBattleConsequenceContextForAcceptedContract();
    check(same(beforePreview,app.repairSession_.rng),"preview consumed employer RNG");
    // Exact shared state survives both world copies and snapshots. Ineligible U
    // consumes nothing, even though it is a valid queued input command.
    BattleStartParams p;p.originalRng.addSeed(1234);auto world=BattleWorld::create(p);
    const auto start=world.snapshot();BattleInputCommand input;input.entityId=world.playerEntityId();input.standUp=true;
    world.enqueueInput(input);world.tick();check(world.snapshot().originalRng==start.originalRng,"upright U consumed RNG");
    auto clone=world;clone.tick();world.tick();check(battleSnapshotFingerprint(clone.snapshot())==battleSnapshotFingerprint(world.snapshot()),"world copy lost shared RNG");
    auto different=start;different.originalRng.exclusive(6);
    check(battleSnapshotFingerprint(start)!=battleSnapshotFingerprint(different),"snapshot hash ignored RNG state");
    std::cout<<"mw_campaign_rng_smoke: ok checks="<<checks<<" DOS_cases=3416\n";
}
int main() {try{runCampaignRngSmoke();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
