// Controlled accepted payloads: neither original nor replacement UI offers
// ordinary Save while a contract is accepted. Exercise production storage.
#include "../main.cpp"
#include <iostream>
namespace {
using namespace mw::battle;
size_t checks=0;
void check(bool ok,const std::string& why) {++checks;if(!ok)throw std::runtime_error(why);}
uint32_t dword(const std::vector<uint8_t>& b,size_t p) {return uint32_t{readU16Le(b,p)}|(uint32_t{readU16Le(b,p+2)}<<16);}
bool same(OriginalContractRng a,OriginalContractRng b) {return a.word3f5==b.word3f5 && a.word3f7==b.word3f7;}
}
void runCampaignAcceptedGamSmoke() {
    App app(findResourceRoot());app.loadResources();
    App restored(findResourceRoot());restored.loadResources();
    const auto input=readFile("research/fixtures/dosbox_accepted_gam/AIN.BIN");
    const auto output=readFile("research/fixtures/dosbox_accepted_gam/AOUT.BIN");
    const auto ram=readFile("research/fixtures/dosbox_accepted_gam/RLOG.BIN");
    const auto disk=readFile("research/fixtures/dosbox_accepted_gam/ROUT.BIN");
    check(input.size()==136*1820 && output.size()==136*1825 && ram.size()==408*9 && disk.size()==408*1816,"incomplete accepted GAM DOS corpus");
    const auto directory=fs::path("outputs/accepted_gam_audit")/("save_"+std::to_string(GetTickCount64()));
    fs::create_directories(directory);size_t resumes=0,launches=0;
    for (size_t i=0;i<136;++i) {
        const std::vector<uint8_t> initial(input.begin()+i*1820,input.begin()+i*1820+1816);
        const std::vector<uint8_t> expected(output.begin()+i*1825,output.begin()+i*1825+1816);
        app.applyGamSaveToRuntime(initial);app.gamRawState_=initial;app.gamRawStateValid_=true;
        app.repairSession_.rng={readU16Le(input,i*1820+1816),readU16Le(input,i*1820+1818)};
        app.deploymentFailures_=127;app.activeContractIndex_=0;app.acceptActiveContract();
        auto actual=initial;app.writeRuntimeToGamSave(actual);
        if(actual!=expected) {
            for(size_t p=0;p<1816;++p) if(actual[p]!=expected[p])
                throw std::runtime_error("acceptance differs case="+std::to_string(i)+" offset="+std::to_string(p)+" actual="+std::to_string(actual[p])+" expected="+std::to_string(expected[p]));
        }
        check(actual==expected,"full original acceptance differs");
        const auto advance=dword(output,i*1825+1816);
        check(app.acceptedContractFinance_ && app.acceptedContractFinance_->advanceCbills==advance &&
            !app.deploymentFailures_ && same(app.repairSession_.rng,{readU16Le(output,i*1825+1821),readU16Le(output,i*1825+1823)}),"acceptance finance/counter/RNG differs");
        std::ostringstream filename;filename<<'C'<<std::uppercase<<std::hex<<std::setw(4)<<std::setfill('0')<<i<<".GAM";
        const auto path=directory/filename.str();
        check(app.saveCurrentGame(path) && readFile(path)==expected,"accepted disk Save differs");
        for(size_t history=0;history<3;++history) {
            const auto p=(i*3+history)*9;
            restored.acceptedContractFinance_=OriginalContractFinance{dword(ram,p),1};
            restored.deploymentFailures_=ram[p+4];
            restored.repairSession_.rng={readU16Le(ram,p+5),readU16Le(ram,p+7)};
            const auto rng=restored.repairSession_.rng;
            restored.loadGameFromFile(path);
            check(restored.contractAccepted_ && restored.campaignResultState_.accepted==1 && restored.acceptedContractFinance_,"Restore discarded pending contract");
            check(restored.acceptedContractFinance_->advanceCbills==advance && restored.acceptedContractFinance_->balanceK==readU16Le(expected,0x5b8),"Restore retained unrelated RAM advance or lost balance");
            check(restored.playerWealth_==dword(expected,0x49) && same(restored.repairSession_.rng,rng) && restored.deploymentFailures_==ram[p+4],"Restore recredited advance or reset RAM RNG/counter");
            const auto& a=app.acceptedContract_;const auto& b=restored.acceptedContract_;
            check(a.originalMissionId==b.originalMissionId && a.originalMissionClass==b.originalMissionClass &&
                a.targetPlanetTableOrder==b.targetPlanetTableOrder && a.targetPlanet==b.targetPlanet &&
                a.targetEnvironmentId==b.targetEnvironmentId && a.targetCategory==b.targetCategory &&
                a.hasHostileTargetHouse==b.hasHostileTargetHouse && a.targetHouse==b.targetHouse &&
                a.heavyCount==b.heavyCount && a.mediumCount==b.mediumCount && a.lightCount==b.lightCount &&
                a.priceK==b.priceK && a.salvagePercent==b.salvagePercent && a.advancePercent==b.advancePercent,"restored chosen contract differs");
            check(!restored.pendingBattleStartParams_ && !restored.campaignBattleRuntime_.active && !restored.acceptedBattleOutcome_.valid && restored.missionParticipants_.empty(),"Restore retained old transition/outcome");
            auto again=expected;restored.writeRuntimeToGamSave(again);
            const std::vector<uint8_t> dos(disk.begin()+(i*3+history)*1816,disk.begin()+(i*3+history+1)*1816);
            check(again==dos,"repeated accepted Save differs from original disk round trip");
            restored.acceptActiveContract();
            check(restored.playerWealth_==dword(expected,0x49) && same(restored.repairSession_.rng,rng),"resumed contract accepted/credited twice");
            ++resumes;
        }
        // Third rejected launch refunds the recovered advance exactly once.
        restored.deploymentFailures_=2;
        for(auto& mech:restored.ownedMechs_) mech.assignedCrewSlot=-1;
        restored.beginMissionLaunch();
        check(!restored.contractAccepted_ && !restored.acceptedContractFinance_ && !restored.campaignResultState_.accepted &&
            restored.playerWealth_==dword(expected,0x49)-advance,"restored cancellation refund differs");
        auto cancelled=expected;restored.writeRuntimeToGamSave(cancelled);restored.applyGamSaveToRuntime(cancelled);
        check(!restored.contractAccepted_,"cancelled contract resurrected");
        restored.loadGameFromFile(path);restored.repairSession_.rng={5703,6536};
        auto payoutRng=restored.repairSession_.rng;
        const auto factor=originalEmployerPayoutPercent(restored.salvageTables_,restored.acceptedContract_.employerHouse,payoutRng.inclusive(100));
        restored.resolveUneventfulGarrisonDuty(); // Controlled existing settlement handler.
        check(restored.missionDebriefPayment_==uint64_t{readU16Le(expected,0x5b8)}*factor/100*1000 &&
            !restored.contractAccepted_ && !restored.campaignResultState_.accepted,"resumed settlement lost balance");
        if(expected[7]==9) { // Ordinary mission, excludes the no-battle gate.
            restored.loadGameFromFile(path);
            for(size_t slot=0;slot<restored.ownedMechs_.size();++slot)
                restored.ownedMechs_[slot]=App::makeMech(App::ChassisId::Jenner,slot<4?static_cast<int>(slot):-1);
            restored.beginMissionLaunch();auto launched=expected;restored.writeRuntimeToGamSave(launched);
            check(restored.missionLaunchPending_ && restored.pendingBattleStartParams_ && !restored.campaignBattleRuntime_.active &&
                launched[1]==3 && launched[8]==restored.detailLevel_ && launched[7]==9,"restored ordinary launch lost context");
            ++launches;
        }
    }
    check(resumes==408 && launches==4,"incomplete continuation coverage");
    auto unknown=std::vector<uint8_t>(output.begin(),output.begin()+1816);unknown[7]=255;
    restored.applyGamSaveToRuntime(unknown);auto roundtrip=unknown;restored.writeRuntimeToGamSave(roundtrip);
    check(!restored.contractAccepted_ && roundtrip==unknown,"unsupported accepted context fabricated or lost");
    restored.restartCampaign();
    check(!restored.contractAccepted_ && !restored.acceptedContractSaveState_ && !restored.campaignResultState_.accepted && !restored.deploymentFailures_,"restart retained pending contract");
    std::ofstream(directory.parent_path()/"latest-cpp-dir.txt")<<directory.string();
    std::cout<<"mw_campaign_accepted_gam_smoke: ok checks="<<checks<<" acceptances=136 restores="<<resumes<<" launch_setups="<<launches<<'\n';
}
int main(){try{runCampaignAcceptedGamSmoke();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
