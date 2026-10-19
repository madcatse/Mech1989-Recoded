#include "../main.cpp"
#include <iostream>
namespace {
using namespace mw::battle;
size_t checks=0;
void check(bool ok,const std::string& message) {++checks;if(!ok)throw std::runtime_error(message);}
bool same(OriginalContractRng a,OriginalContractRng b) {return a.word3f5==b.word3f5 && a.word3f7==b.word3f7;}
}
void runCampaignLaunchContextSmoke() {
    App app(findResourceRoot());app.loadResources();
    const auto ni=readFile("research/fixtures/dosbox_rng/NOUT.BIN");
    const std::vector<uint8_t> fresh(ni.begin(),ni.begin()+1816);
    const auto li=readFile("research/fixtures/dosbox_launch/LIN.BIN"),lo=readFile("research/fixtures/dosbox_launch/LOUT.BIN");
    check(li.size()==128*1820 && lo.size()==128*3638,"incomplete original launch corpus");
    for(size_t i=0;i<128;++i) {
        const std::vector<uint8_t> input(li.begin()+1820*i,li.begin()+1820*i+1816);
        const size_t out=i*3638;
        check(lo[out+29]==0 && lo[out+30]==0,"original launch did not clear defeated count");
        const bool noBattle=lo[out+3636]!=0;
        OriginalContractRng rng{readU16Le(li,1820*i+1816),readU16Le(li,1820*i+1818)};
        check(originalUneventfulContract(input[7],rng)==noBattle && same(rng,{readU16Le(lo,out+3632),readU16Le(lo,out+3634)}),"DOS launch decision/RNG mismatch");
        if(!noBattle) {
            check(std::equal(lo.begin()+out,lo.begin()+out+1816,lo.begin()+out+1816),"original RAM packet differs from local context");
            check(lo[out+1]==input[0x715] && lo[out+8]==input[0x70d],"loader mode/detail handoff mismatch");
        }
        app.applyGamSaveToRuntime(input);auto saved=input;app.writeRuntimeToGamSave(saved);
        check(saved==input,"no-action save rewrote launch/result context");
    }
    // Imported result counters are raw bytes, independent of both reputation
    // fields. Include inconsistent storage probes without inventing repair rules.
    for(unsigned defeated=0;defeated<256;++defeated) for(uint16_t tier=0;tier<4;++tier)
        for(uint16_t points:std::array<uint16_t,4>{{0,1,40,1000}}) {
            auto input=fresh;input[0x1d]=static_cast<uint8_t>(defeated);
            App::writeU16Le(input,0xa2,tier);App::writeU16Le(input,0x6b,points);
            app.applyGamSaveToRuntime(input);auto saved=input;app.writeRuntimeToGamSave(saved);
            check(app.playerReputationTier_==tier && app.playerReputationPoints_==points,"Restore inferred rank/points from returned defeats");
            check(saved==input && app.campaignResultState_.defeatedOpponents==defeated,"no-action save changed raw result byte");
            app.addCompanyReputationPoints(1);saved=input;app.writeRuntimeToGamSave(saved);
            check(saved[0x1d]==defeated,"reputation change overwrote defeated opponents");
        }
    // Original BTECH 383A count is already pinned across all16 enemy masks,
    // five results and five stages. Propagate it separately from the award.
    const auto gi=readFile("research/fixtures/dosbox_sequence/GIN.BIN"),go=readFile("research/fixtures/dosbox_sequence/GOUT.BIN");
    for(size_t i=0;i<gi.size()/8;++i) {
        CampaignBattleOutcomePackage out;out.valid=out.terminal=out.contractMetadataValid=true;
        out.launchSource=readU16Le(gi,8*i+2) ? CampaignBattleLaunchSource::DarkWingFinal : CampaignBattleLaunchSource::AcceptedContract;
        out.rawResultCode=readU16Le(gi,8*i+4);out.outcome=out.rawResultCode==0 ? CampaignBattleOutcome::Victory : CampaignBattleOutcome::Defeat;
        CampaignBattleConsequenceContext c;c.defeatedOpponents=static_cast<uint8_t>(readU16Le(go,6*i+4));
        BattleContractMetadata contract;contract.valid=true;
        const auto plan=campaignBattleConsequencePlanFromOutcome(out,contract,c);
        check(plan.defeatedOpponents==c.defeatedOpponents,"DOS result count dropped from plan");
        if(out.launchSource==CampaignBattleLaunchSource::DarkWingFinal && out.rawResultCode==0)
            check(plan.reputationDelta==0,"ending awarded reputation instead of only preserving result byte");
    }
    // Actual guarded ordinary consequence commit, rollback and duplicate. No
    // final world, synthetic control outcomes only, real campaign handlers.
    const auto dir=fs::path("outputs/launch_audit")/("save_"+std::to_string(GetTickCount64()));fs::create_directories(dir);
    for(unsigned defeated=0;defeated<=4;++defeated) for(auto result:{CampaignBattleOutcome::Victory,CampaignBattleOutcome::Defeat,CampaignBattleOutcome::Withdraw}) {
        app.applyGamSaveToRuntime(fresh);app.campaignResultState_.defeatedOpponents=4;
        app.acceptedBattleOutcome_={};auto& out=app.acceptedBattleOutcome_;
        out.valid=out.terminal=out.contractMetadataValid=true;out.launchSource=CampaignBattleLaunchSource::AcceptedContract;
        out.outcome=result;out.rawResultCode=result==CampaignBattleOutcome::Victory ? 0 : 1;
        app.acceptedBattleContract_={};app.acceptedBattleContract_.valid=true;
        CampaignBattleConsequenceContext c;c.defeatedOpponents=static_cast<uint8_t>(defeated);
        app.acceptedBattleConsequencePlan_=campaignBattleConsequencePlanFromOutcome(out,app.acceptedBattleContract_,c);
        app.lastCommittedBattleConsequencePlanFingerprint_=0;
        app.acceptedBattleStateGuard_.before=app.captureCampaignPersistentState();
        app.acceptedBattleStateGuard_.after=app.acceptedBattleStateGuard_.before;
        app.acceptedBattleStateGuard_.valid=app.acceptedBattleStateGuard_.unchanged=true;
        const auto before=app.captureCampaignPersistentState().fingerprint;
        // Force existing save/commit verification to fail after mutation.
        const auto wealth=app.playerWealth_;app.playerWealth_=0x100000000ull;
        app.acceptedBattleStateGuard_.after=app.captureCampaignPersistentState();
        check(!app.commitAcceptedBattleConsequencePlan().committed && app.campaignResultState_.defeatedOpponents==4,"rollback retained pending result count");
        app.playerWealth_=wealth;app.acceptedBattleStateGuard_.after=app.captureCampaignPersistentState();
        check(app.captureCampaignPersistentState().fingerprint==before,"failed commit leaked persistent changes");
        const auto receipt=app.commitAcceptedBattleConsequencePlan();
        check(receipt.committed && app.campaignResultState_.defeatedOpponents==defeated,"result counter not committed: "+receipt.reason);
        check(app.commitAcceptedBattleConsequencePlan().duplicateRejected,"duplicate result count commit");
        check(app.saveCurrentGame(dir/"RESULT.GAM"),"result Save failed");
        const auto saved=readFile(dir/"RESULT.GAM");check(saved[0x1d]==defeated,"Save stored rank instead of result count");
        const auto rank=app.playerReputationTier_;const auto points=app.playerReputationPoints_;
        app.loadGameFromFile(dir/"RESULT.GAM");
        check(app.campaignResultState_.defeatedOpponents==defeated && app.playerReputationTier_==rank && app.playerReputationPoints_==points,"result Restore conflated rank/count");
    }
    // Rejected launch preserves prior result. Accepted launch clears it even
    // when the commander company rank is nonzero. No battle world is started.
    app.applyGamSaveToRuntime(fresh);app.campaignResultState_.defeatedOpponents=4;app.playerReputationTier_=3;
    app.ownedMechs_[0]=App::makeMech(App::ChassisId::Jenner,-1);
    app.activeContracts_[0].originalMissionId=9;app.activeContractIndex_=0;app.acceptActiveContract();
    app.beginMissionLaunch();check(app.campaignResultState_.defeatedOpponents==4,"rejected launch erased prior result");
    app.ownedMechs_[0].assignedCrewSlot=0;app.beginMissionLaunch();
    check(app.campaignResultState_.defeatedOpponents==0 && app.playerReputationTier_==3 && app.missionLaunchPending_,"accepted launch did not separate result reset from rank");
    std::cout<<"mw_campaign_launch_context_smoke: ok checks="<<checks<<" DOS_launch_cases=128\n";
}
int main(){try{runCampaignLaunchContextSmoke();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
