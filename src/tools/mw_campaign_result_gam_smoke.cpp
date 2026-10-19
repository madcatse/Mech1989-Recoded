#include "../main.cpp"
#include <iostream>
namespace {
using namespace mw::battle;
size_t checks=0;
void check(bool ok,const std::string& why) {++checks;if(!ok)throw std::runtime_error(why);}
std::array<uint8_t,24> bytes(const OriginalCampaignResultState& s) {return originalCampaignResultBytes(s);}
}

void runCampaignResultGamSmoke() {
    App app(findResourceRoot());app.loadResources();
    const auto input=readFile("research/fixtures/dosbox_result_gam/BIN.BIN");
    const auto btech=readFile("research/fixtures/dosbox_result_gam/BOUT.BIN");
    const auto parent=readFile("research/fixtures/dosbox_result_gam/MOUT.BIN");
    check(input.size()==240*1832 && btech.size()==240*1816 && parent.size()==240*1816,"incomplete DOS result corpus");
    const auto directory=fs::path("outputs/result_gam_audit")/("save_"+std::to_string(GetTickCount64()));
    fs::create_directories(directory);
    for (size_t i=0;i<240;++i) {
        const auto record=[&](const auto& buffer,size_t stride) {
            return std::vector<uint8_t>(buffer.begin()+i*stride,buffer.begin()+i*stride+1816);
        };
        const auto initial=record(input,1832),returned=record(btech,1816),settled=record(parent,1816);
        const auto sourceResult=decodeOriginalCampaignResult(returned);
        const auto expected=decodeOriginalCampaignResult(settled);
        auto compacted=sourceResult;compacted.accepted=0;compactOriginalCampaignResultCrew(compacted,4);
        check(bytes(compacted)==bytes(expected),"original post-result crew status compaction differs");
        app.applyGamSaveToRuntime(initial);app.gamRawState_=initial;app.gamRawStateValid_=true;
        auto noAction=initial;app.writeRuntimeToGamSave(noAction);
        check(noAction==initial,"no-action changed opaque imported result bytes");

        // Production commit consumes the already independently verified return
        // plan. The DOS scope here is the result packet, not the damage exporter.
        app.acceptedBattleOutcome_={};auto& outcome=app.acceptedBattleOutcome_;
        outcome.valid=outcome.terminal=outcome.contractMetadataValid=true;
        outcome.launchSource=CampaignBattleLaunchSource::AcceptedContract;
        outcome.rawResultCode=sourceResult.result;
        outcome.outcome=sourceResult.result==0 ? CampaignBattleOutcome::Victory :
            sourceResult.result==2 ? CampaignBattleOutcome::Withdraw : CampaignBattleOutcome::Defeat;
        app.acceptedBattleContract_={};app.acceptedBattleContract_.valid=true;
        CampaignBattleConsequenceContext context;context.defeatedOpponents=sourceResult.defeatedOpponents;
        app.acceptedBattleConsequencePlan_=campaignBattleConsequencePlanFromOutcome(outcome,app.acceptedBattleContract_,context);
        app.missionSequence_.salvageRecords=sourceResult.salvage;
        app.acceptedBattleMechReturnPlan_={};auto& returnedPlan=app.acceptedBattleMechReturnPlan_;
        returnedPlan.valid=true;returnedPlan.fingerprint=i+1;
        for (size_t slot=0;slot<4;++slot) {
            returnedPlan.killedCrewSlots[slot]=sourceResult.crewStatus[slot]==1;
            CampaignMechReturnEntry entry;entry.ownedMechIndex=slot;entry.mechPresetId="jenner";
            entry.crewSlot=static_cast<int>(slot);entry.pilotKilled=returnedPlan.killedCrewSlots[slot];
            entry.state=App::battlePersistentMechStateFromOwnedMech(app.ownedMechs_[slot],"result_gam");
            entry.ammunitionStateValid=true;entry.ammunitionByPool=app.ownedMechs_[slot].ammoPacks;
            returnedPlan.entries.push_back(entry);
        }
        app.lastCommittedBattleConsequencePlanFingerprint_=0;
        const auto guard=[&] {
            app.acceptedBattleStateGuard_.before=app.captureCampaignPersistentState();
            app.acceptedBattleStateGuard_.after=app.acceptedBattleStateGuard_.before;
            app.acceptedBattleStateGuard_.valid=app.acceptedBattleStateGuard_.unchanged=true;
        };
        guard();const auto before=app.captureCampaignPersistentState().fingerprint;
        const auto beforePacket=bytes(app.campaignResultState_);
        if (sourceResult.crewStatus[0]==1) {
            check(!app.commitAcceptedBattleConsequencePlan().committed &&
                app.captureCampaignPersistentState().fingerprint==before,"commander-dead result committed");
            continue; // Original UI cannot Save/Continue this controlled dead packet.
        }
        const auto wealth=app.playerWealth_;app.playerWealth_=0x100000000ull;guard();
        check(!app.commitAcceptedBattleConsequencePlan().committed && bytes(app.campaignResultState_)==beforePacket,
            "failed result commit leaked GAM context");
        app.playerWealth_=wealth;guard();
        check(app.captureCampaignPersistentState().fingerprint==before,"failed result commit leaked state/RNG");
        const auto receipt=app.commitAcceptedBattleConsequencePlan();
        check(receipt.committed && receipt.saveRoundTripVerified,"result commit failed: "+receipt.reason);
        check(bytes(app.campaignResultState_)==bytes(expected),"committed packet differs from original");
        check(app.hiredCrewCount()==readU16Le(settled,0x6d) && app.ownedMechs_.size()==4,"casualty result lost crew/mech consistency");
        check(app.commitAcceptedBattleConsequencePlan().duplicateRejected,"duplicate result applied twice");
        std::ostringstream filename;filename<<'C'<<std::uppercase<<std::hex<<std::setw(4)<<std::setfill('0')<<i<<".GAM";
        const auto path=directory/filename.str();
        check(app.saveCurrentGame(path),"post-result Save failed");const auto saved=readFile(path);
        check(bytes(decodeOriginalCampaignResult(saved))==bytes(expected),"disk retained old result fields");
        const auto savedWealth=app.playerWealth_;const auto rng=app.repairSession_.rng;
        app.loadGameFromFile(path);
        check(bytes(app.campaignResultState_)==bytes(expected) && app.playerWealth_==savedWealth,"Restore lost/reapplied result");
        check(app.repairSession_.rng.word3f5==rng.word3f5 && app.repairSession_.rng.word3f7==rng.word3f7,"Restore changed RAM RNG");
        check(!app.contractAccepted_ && !app.acceptedBattleOutcome_.valid,"Restore reopened completed result");
        auto roundtrip=saved;app.writeRuntimeToGamSave(roundtrip);check(roundtrip==saved,"post-result repeated Save is not byte stable");
    }
    std::ofstream(directory.parent_path()/"latest-cpp-dir.txt")<<directory.string();
    // Existing DOS launch corpus proves reset boundaries, including rejected
    // launches (production handler checks below) and the local no-battle path.
    const auto li=readFile("research/fixtures/dosbox_launch/LIN.BIN");
    const auto lo=readFile("research/fixtures/dosbox_launch/LOUT.BIN");
    for (size_t i=0;i<128;++i) {
        const std::vector<uint8_t> in(li.begin()+i*1820,li.begin()+i*1820+1816);
        auto state=decodeOriginalCampaignResult(in);clearOriginalCampaignResultForLaunch(state);
        if (lo[i*3638+3636]) {state.salvage={};state.accepted=0;}
        const std::vector<uint8_t> out(lo.begin()+i*3638,lo.begin()+i*3638+1816);
        check(bytes(state)==bytes(decodeOriginalCampaignResult(out)),"DOS launch reset boundary differs");
    }
    const std::vector<uint8_t> fresh(input.begin(),input.begin()+1816);
    app.applyGamSaveToRuntime(fresh);app.ownedMechs_[0].assignedCrewSlot=-1;
    app.activeContractIndex_=0;app.activeContracts_[0].originalMissionId=9;app.acceptActiveContract();
    const auto rejectedPacket=bytes(app.campaignResultState_);app.beginMissionLaunch();
    check(bytes(app.campaignResultState_)==rejectedPacket,"rejected launch erased previous result");
    app.ownedMechs_[0]=App::makeMech(App::ChassisId::Jenner,0);
    // Remaining assigned mechs need valid critical conditions for readiness.
    for (size_t slot=1;slot<4;++slot) app.ownedMechs_[slot]=App::makeMech(App::ChassisId::Jenner,static_cast<int>(slot));
    app.beginMissionLaunch();
    auto cleared=decodeOriginalCampaignResult(fresh);clearOriginalCampaignResultForLaunch(cleared);
    check(app.missionLaunchPending_ && bytes(app.campaignResultState_)==bytes(cleared),"real launch retained old packet");
    app.resolveUneventfulGarrisonDuty();cleared.salvage={};cleared.accepted=0;
    check(bytes(app.campaignResultState_)==bytes(cleared),"uneventful return retained old packet");
    std::cout<<"mw_campaign_result_gam_smoke: ok checks="<<checks<<" DOS_packets=240 Save_Restore=200\n";
}
int main(){try{runCampaignResultGamSmoke();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
