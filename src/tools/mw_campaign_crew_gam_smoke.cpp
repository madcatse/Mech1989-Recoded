#include "../main.cpp"
#include <iostream>
namespace {
using namespace mw::battle;
size_t checks=0;
void check(bool ok,const std::string& why){++checks;if(!ok)throw std::runtime_error(why);}
void identical(const std::vector<uint8_t>& a,const std::vector<uint8_t>& b,const std::string& why) {
    ++checks;for(size_t p=0;p<a.size();++p)if(a[p]!=b[p])
        throw std::runtime_error(why+" offset="+std::to_string(p)+" actual="+std::to_string(a[p])+" expected="+std::to_string(b[p]));
}
}
void runCampaignCrewGamSmoke() {
    App app(findResourceRoot());app.loadResources();
    const auto ins=readFile("research/fixtures/dosbox_crew_gam/IN.BIN");
    const auto outs=readFile("research/fixtures/dosbox_crew_gam/OUT.BIN");
    check(ins.size()==653*1822 && outs.size()==653*1820,"incomplete DOS crew corpus");
    const auto directory=fs::path("outputs/crew_gam_audit")/("save_"+std::to_string(GetTickCount64()));
    fs::create_directories(directory);size_t savedCount=0;
    for(size_t i=0;i<653;++i) {
        const std::vector<uint8_t> initial(ins.begin()+i*1822,ins.begin()+i*1822+1816);
        std::vector<uint8_t> expected(outs.begin()+i*1820,outs.begin()+i*1820+1816);
        const auto op=readU16Le(ins,i*1822+1816),slot=readU16Le(ins,i*1822+1818),pid=readU16Le(ins,i*1822+1820);
        if(op==4) {
            // Dropship10 is not a supported owned chassis in the replacement.
            // Its adjacent word is decoded and preserved, without inventing a ship.
            auto raw=expected;encodeOriginalCrewGam(raw,decodeOriginalCrewGam(raw));identical(raw,expected,"dropship storage");continue;
        }
        app.applyGamSaveToRuntime(initial);app.gamRawState_=initial;app.gamRawStateValid_=true;
        app.crewPayrollNotice_=CrewPayrollNotice::None;
        auto noAction=initial;app.writeRuntimeToGamSave(noAction);identical(noAction,initial,"no-action case="+std::to_string(i));
        const auto rng=app.repairSession_.rng;
        const auto owned=app.ownedMechs_.size();
        if(op==0)check(app.hireRecruit(pid-1),"source hire rejected case="+std::to_string(i));
        if(op==1)app.dismissCrewSlot(slot);
        if(op==2 && initial[9]!=1) {
            compactOriginalCampaignResultCrew(app.campaignResultState_,app.hiredCrewCount());
            size_t removed=0;
            for(size_t s=1;s<readU16Le(initial,0x6d);++s)
                if(initial[9+s]){app.dismissCrewSlot(s-removed,initial[9+s]==1);++removed;}
        }
        if(op==3 || op==5) {
            app.payMonthlyCrewWages(readU16Le(initial,0x39));
            const auto notice=(op==5 || outs[i*1820+1816]) ? CrewPayrollNotice::Departed :
                outs[i*1820+1818] ? CrewPayrollNotice::Warning : CrewPayrollNotice::None;
            if(op==5) {
                check(App::readGamU32Le(expected,0x49)==36 && readU16Le(expected,0x6d)==4,"DOS overflow case did not wrap72000 to6464");
                expected=initial;App::writeU16Le(expected,0x6d,1);
                for(size_t s=1;s<4;++s)App::writeU16Le(expected,0xc2+2*s,65535);
            }
            check(app.crewPayrollNotice_==notice,"DOS payroll notice differs");
            if(notice==CrewPayrollNotice::Departed) {
                // Explicit original bug correction: source clears pilotId+1,
                // including stale inactive IDs. Free only actual departing pilots.
                std::copy(initial.begin()+0x77,initial.begin()+0xa1,expected.begin()+0x77);
                for(size_t s=1;s<readU16Le(initial,0x6d);++s)expected[0x76+readU16Le(initial,0x6f+2*s)]=0;
            }
        }
        auto actual=initial;app.writeRuntimeToGamSave(actual);
        identical(actual,expected,"source operation case="+std::to_string(i));
        check(app.ownedMechs_.size()==owned && app.repairSession_.rng.word3f5==rng.word3f5 && app.repairSession_.rng.word3f7==rng.word3f7,"crew operation changed owned mechs/RNG");
        std::ostringstream name;name<<'C'<<std::uppercase<<std::hex<<std::setw(4)<<std::setfill('0')<<i<<".GAM";
        const auto path=directory/name.str();check(app.saveCurrentGame(path),"crew disk Save failed");
        identical(readFile(path),expected,"crew disk content");
        // Notices are RAM-only and consumed on menu return, not encoded in GAM.
        app.crewPayrollNotice_=CrewPayrollNotice::None;
        app.loadGameFromFile(path);auto restored=expected;app.writeRuntimeToGamSave(restored);
        identical(restored,expected,"crew Restore/repeated Save");++savedCount;
    }
    check(savedCount==637,"incomplete replacement disk coverage");
    // One unpaid month, explicit Save/Restore, paid month (marker retained),
    // then another unpaid month: actual calendar + notification + departure.
    const std::vector<uint8_t> fresh(ins.begin()+126*1822,ins.begin()+126*1822+1816);
    app.applyGamSaveToRuntime(fresh);app.gamRawState_=fresh;app.gamRawStateValid_=true;
    app.crewPayrollNotice_=CrewPayrollNotice::None;app.currentMonthDayCounter_=59;app.playerWealth_=1;
    const auto bill=app.monthlyCrewPayroll();app.advanceCampaignDays(1);
    check(app.playerWealth_==1 && app.crewGamState_.unpaidCrewPayroll==bill && app.hiredCrewCount()==2,"first unpaid calendar month differs");
    app.returnToMainMenu(false);check(app.state_==ScreenState::CampaignMessage && !app.storyPages_.empty(),"payroll warning is not visible");
    check(app.saveCurrentGame(directory/"WARNING.GAM"),"warning Save failed");app.loadGameFromFile(directory/"WARNING.GAM");
    app.playerWealth_=bill;app.advanceCampaignDays(60);
    check(!app.playerWealth_ && app.crewGamState_.unpaidCrewPayroll==bill && app.hiredCrewCount()==2,"paid month cleared original unpaid marker");
    app.advanceCampaignDays(60);
    check(app.hiredCrewCount()==1 && !app.crewGamState_.unpaidCrewPayroll && app.crewPayrollNotice_==CrewPayrollNotice::Departed,"saved unpaid marker lost departure branch");
    app.returnToMainMenu(false);check(app.state_==ScreenState::CampaignMessage && app.storyPages_[0].lines.back()==L"YOUR CREW SO THEY LEFT.","departure notice missing");
    // Full long-journey bill deliberately avoids DOS MUL low-word overflow.
    uint64_t money=6500;uint16_t unpaid=0;
    check(applyOriginalCrewPayroll(money,unpaid,6000,12)==CrewPayrollNotice::Departed && money==6500,"long journey wrapped payroll");
    // Failed result verification must roll back payroll, tail words and notices.
    app.applyGamSaveToRuntime(fresh);app.currentMonthDayCounter_=59;app.playerWealth_=0;
    app.crewGamState_.unpaidCrewPayroll=1;app.crewPayrollNotice_=CrewPayrollNotice::None;
    app.currentYear_=70000; // Force the existing WORD-year Save verification to fail after payroll.
    app.acceptedBattleConsequencePlan_={};auto& plan=app.acceptedBattleConsequencePlan_;
    plan.valid=plan.commitEligible=true;plan.planFingerprint=0xc001;plan.campaignDateTicks=1;
    app.acceptedBattleStateGuard_.before=app.captureCampaignPersistentState();
    app.acceptedBattleStateGuard_.after=app.acceptedBattleStateGuard_.before;
    app.acceptedBattleStateGuard_.valid=app.acceptedBattleStateGuard_.unchanged=true;
    const auto before=app.captureCampaignPersistentState().fingerprint;
    const auto failed=app.commitAcceptedBattleConsequencePlan();
    check(!failed.committed && failed.reason=="campaign_consequence_commit_verification_failed" && app.captureCampaignPersistentState().fingerprint==before &&
        app.hiredCrewCount()==2 && app.crewGamState_.unpaidCrewPayroll==1 && app.crewPayrollNotice_==CrewPayrollNotice::None,
        "failed payroll transaction: "+failed.reason+" committed="+std::to_string(failed.committed)+" crew="+std::to_string(app.hiredCrewCount())+" unpaid="+std::to_string(app.crewGamState_.unpaidCrewPayroll));
    app.currentYear_=3024;app.acceptedBattleStateGuard_.before=app.captureCampaignPersistentState();
    app.acceptedBattleStateGuard_.after=app.acceptedBattleStateGuard_.before;
    check(app.commitAcceptedBattleConsequencePlan().committed && app.hiredCrewCount()==1,"payroll result commit failed");
    check(app.commitAcceptedBattleConsequencePlan().duplicateRejected,"duplicate payroll result accepted");
    std::ofstream(directory.parent_path()/"latest-cpp-dir.txt")<<directory.string();
    std::cout<<"mw_campaign_crew_gam_smoke: ok checks="<<checks<<" DOS_cases=653 Save_Restore="<<savedCount<<'\n';
}
int main(){try{runCampaignCrewGamSmoke();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
