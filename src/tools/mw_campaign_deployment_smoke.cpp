// Real campaign handlers against isolated original DOS procedure outputs.
#include "../main.cpp"
#include <iostream>
namespace {
using namespace mw::battle;
size_t checks=0;
void check(bool value,const std::string& message) {++checks;if(!value)throw std::runtime_error(message);}
bool same(OriginalContractRng a,OriginalContractRng b) {return a.word3f5==b.word3f5 && a.word3f7==b.word3f7;}
std::vector<uint8_t> fixture(const char* name) {return readFile(std::string("research/fixtures/dosbox_deployment/")+name+".BIN");}
uint32_t u32(const std::vector<uint8_t>& b,size_t p) {return uint32_t{readU16Le(b,p)}|(uint32_t{readU16Le(b,p+2)}<<16);}
}
void runCampaignDeploymentSmoke() {
    App app(findResourceRoot());app.loadResources();
    const auto fresh=readFile("research/fixtures/dosbox_story/start.gam");
    app.applyGamSaveToRuntime(fresh);
    const auto ai=fixture("AIN"),ao=fixture("AOUT");
    check(ai.size()==3600*18 && ao.size()==ai.size(),"incomplete DOS acceptance fixtures");
    for(size_t i=0;i<ai.size()/18;++i) {
        const auto p=i*18;
        app.contractAccepted_=false;app.activeContractIndex_=0;app.activeContracts_={App::ContractOffer{}};
        auto& offer=app.activeContracts_[0];offer.priceK=readU16Le(ai,p+4);offer.advancePercent=readU16Le(ai,p+6);
        offer.employerHouse=static_cast<uint8_t>(readU16Le(ai,p+8));
        offer.heavyCount=ai[p+10];offer.mediumCount=ai[p+11];offer.lightCount=ai[p+12];
        app.playerWealth_=u32(ai,p+14);app.deploymentFailures_=127;
        app.repairSession_.rng={readU16Le(ai,p),readU16Le(ai,p+2)};
        app.acceptActiveContract();
        check(app.playerWealth_==u32(ao,p+6),"DOS credited advance differs");
        check(app.acceptedContractFinance_ && app.acceptedContractFinance_->advanceCbills==u32(ao,p) &&
            app.acceptedContractFinance_->balanceK==readU16Le(ao,p+4),"DOS advance truncation/balance differs");
        check(app.acceptedContract_.heavyCount==ao[p+10] && app.acceptedContract_.mediumCount==ao[p+11] &&
            app.acceptedContract_.lightCount==ao[p+12],"DOS actual opposition differs");
        check(same(app.repairSession_.rng,{readU16Le(ao,p+14),readU16Le(ao,p+16)}) && app.deploymentFailures_==0,"DOS acceptance RNG/reset differs");
        const auto money=app.playerWealth_,rng=uint64_t{app.repairSession_.rng.word3f5}<<16|app.repairSession_.rng.word3f7;
        app.acceptActiveContract();
        check(app.playerWealth_==money && (uint64_t{app.repairSession_.rng.word3f5}<<16|app.repairSession_.rng.word3f7)==rng,"duplicate acceptance credited/drew twice");
        for(uint16_t factor : std::array<uint16_t,4>{{0,50,75,100}}) for(auto result : {CampaignBattleOutcome::Victory,CampaignBattleOutcome::Defeat,CampaignBattleOutcome::Withdraw}) {
            app.missionSequence_.employerPayoutPercent=factor;
            CampaignBattleOutcomePackage out;out.valid=out.terminal=out.contractMetadataValid=true;
            out.launchSource=CampaignBattleLaunchSource::AcceptedContract;out.outcome=result;
            auto contract=app.acceptedBattleContract_;contract.valid=true;
            const auto plan=campaignBattleConsequencePlanFromOutcome(out,contract,app.campaignBattleConsequenceContextForAcceptedContract());
            const auto expected=result==CampaignBattleOutcome::Victory ? uint64_t{readU16Le(ao,p+4)}*factor/100*1000 : 0;
            check(plan.payment==expected,"settlement paid full price or refunded advance on failure");
        }
    }
    const auto ri=fixture("RIN"),ro=fixture("ROUT");
    check(ri.size()==5244*372 && ro.size()==5244*24,"incomplete DOS readiness fixtures");
    const auto tables=decodeOriginalContractTables(readFile("Original/MW_MAIN.EXE"));
    for(size_t i=0;i<ri.size()/372;++i) {
        app.applyGamSaveToRuntime(fresh);const auto p=i*372,o=i*24;
        const auto house=readU16Le(ri,p+360);
        for(size_t planet=0;planet<app.planets_.size();++planet) if(app.planets_[planet].houseId==house) {app.currentPlanetIndex_=static_cast<int>(planet);break;}
        app.ownedMechs_.clear();
        for(size_t mech=0;mech<12;++mech) {
            app.ownedMechs_.push_back(App::makeMech(App::ChassisId::Jenner,-1));
            App::readGamMechRecord(ri,p+10+29*mech,app.ownedMechs_.back());
        }
        for(size_t crew=0;crew<4;++crew) {
            app.crewMembers_[crew].hired=crew<readU16Le(ri,p+8);
            const auto slot=readU16Le(ri,p+2*crew);
            if(slot!=65535)app.ownedMechs_.at(slot).assignedCrewSlot=static_cast<int>(crew);
        }
        app.contractAccepted_=true;app.acceptedContractFinance_=OriginalContractFinance{u32(ri,p+366),123};
        app.deploymentFailures_=ri[p+358];app.playerWealth_=u32(ri,p+362);
        const auto negative=readU16Le(ri,p+370);
        app.houseNegativeCounters_[house]=negative<0x8000 ? negative : static_cast<int>(negative)-65536;
        app.repairSession_.rng={1557,6536};auto expectedRng=app.repairSession_.rng;
        const auto date=std::array<int,3>{{app.currentYear_,app.currentMonth_,app.currentMonthDayCounter_}};
        const auto family=app.familyAttitudes_,positive=app.housePositiveCounters_;
        const bool cancel=readU16Le(ro,o+2)==0x1031;
        OriginalContractBoard expectedBoard;
        if(cancel) {
            ContractGenerationContext c;c.employerHouse=static_cast<uint8_t>(house);c.year=date[0];c.month=date[1];c.dayCounter=date[2];
            c.forceScore=app.contractForceScore();c.crewCount=readU16Le(ri,p+8);c.ownedMechCount=12;
            c.positiveStanding=positive[house];c.negativeStanding=app.houseNegativeCounters_[house];
            expectedBoard=generateOriginalContractBoard(tables,c,expectedRng);
        }
        const bool ready=app.checkMissionDeployment(ri[p+359]!=0);
        check(ready==(readU16Le(ro,o+2)==0),"DOS readiness differs case="+std::to_string(i));
        check(app.deploymentFailures_==readU16Le(ro,o+4) && app.contractAccepted_==(readU16Le(ro,o+6)!=0),"DOS rejection counter/cancellation differs");
        check(app.playerWealth_==u32(ro,o+8) && static_cast<uint16_t>(app.houseNegativeCounters_[house])==readU16Le(ro,o+12),"DOS refund/standing differs");
        check(same(app.repairSession_.rng,expectedRng),"rejection consumed RNG or cancellation used updated standing");
        check(family==app.familyAttitudes_ && positive==app.housePositiveCounters_ && date==std::array<int,3>{{app.currentYear_,app.currentMonth_,app.currentMonthDayCounter_}},"rejection applied unrelated campaign consequences");
        if(!ready)check(app.state_==ScreenState::CampaignMessage && !app.storyPages_[0].lines.empty(),"original warning not visible");
        if(cancel) {
            std::vector<uint8_t> expected(1816),actual(1816);
            encodeOriginalContractBoard(expected,tables,expectedBoard);
            encodeOriginalContractBoard(actual,tables,app.originalContractBoardForSave(tables));
            check(expected==actual,"cancellation board wasn't generated from pre-penalty standing");
            check(!app.acceptedContractFinance_ && !app.pendingBattleStartParams_ && !app.missionLaunchPending_,"cancellation left pending state");
        }
    }
    // Actual ordinary launch: two warnings, repair/assignment, then successful setup.
    app.applyGamSaveToRuntime(fresh);app.ownedMechs_[0]=App::makeMech(App::ChassisId::Jenner,-1);
    app.activeContracts_={App::ContractOffer{}};app.activeContractIndex_=0;
    app.activeContracts_[0].priceK=137;app.activeContracts_[0].advancePercent=33;
    app.activeContracts_[0].originalMissionId=9; // excludes no-battle gate
    app.acceptActiveContract();const auto money=app.playerWealth_;const auto rng=app.repairSession_.rng;
    app.beginMissionLaunch();app.beginMissionLaunch();
    check(app.deploymentFailures_==2 && same(app.repairSession_.rng,rng) && app.playerWealth_==money,"warnings changed financial/RNG state");
    app.ownedMechs_[0].assignedCrewSlot=0;app.beginMissionLaunch();
    check(app.missionLaunchPending_ && app.pendingBattleStartParams_ && app.acceptedContractFinance_->balanceK==92,"repair/assignment did not permit ordinary launch or lost advance");
    // Final entry rejected repeatedly without creating a final world or penalty.
    app.applyGamSaveToRuntime(fresh);app.ownedMechs_[0].assignedCrewSlot=-1;app.deploymentFailures_=2;
    const auto before=app.captureCampaignPersistentState().fingerprint;
    for(int n=0;n<4;++n)app.executeStoryAction(App::StoryAction::FinalAttack);
    check(app.deploymentFailures_==2 && !app.campaignBattleRuntime_.active && !app.pendingBattleStartParams_ &&
        app.captureCampaignPersistentState().fingerprint==before,"failed final attack changed state/launched battle");
    // Settlement clears runtime finance; accepted Save/Restore recovers terms
    // and balance without paying the advance twice.
    for(uint8_t house=0;house<5;++house) for(uint16_t seed : std::array<uint16_t,4>{{1,474,6536,65535}}) {
        app.applyGamSaveToRuntime(fresh);app.activeContractIndex_=0;
        check(!app.activeContracts_.empty(),"source fixture lacks offers");
        app.activeContracts_[0].priceK=137;app.activeContracts_[0].advancePercent=33;
        app.activeContracts_[0].employerHouse=house;app.acceptActiveContract();
        app.repairSession_.rng={5703,seed};auto expectedRng=app.repairSession_.rng;
        const auto factor=originalEmployerPayoutPercent(app.salvageTables_,house,expectedRng.inclusive(100));
        app.resolveUneventfulGarrisonDuty();
        check(app.missionDebriefPayment_==uint64_t{92}*factor/100*1000 && !app.acceptedContractFinance_,"uneventful settlement used full contract price/retained finance");
    }
    app.applyGamSaveToRuntime(fresh);app.activeContractIndex_=0;
    app.activeContracts_[0].priceK=137;app.activeContracts_[0].advancePercent=33;app.acceptActiveContract();
    auto gam=fresh;app.writeRuntimeToGamSave(gam);const auto savedMoney=app.playerWealth_;
    app.applyGamSaveToRuntime(gam);
    check(gam.size()==1816 && app.contractAccepted_ && app.acceptedContractFinance_ &&
        app.acceptedContractFinance_->balanceK==92 && app.acceptedContractFinance_->advanceCbills==45000 &&
        !app.deploymentFailures_ && app.playerWealth_==savedMoney,"Restore lost accepted finance or paid the advance again");
    std::cout<<"mw_campaign_deployment_smoke: ok checks="<<checks<<" DOS_cases=8844\n";
}
int main(){try{runCampaignDeploymentSmoke();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
