#include "../main.cpp"
#include <iostream>

namespace {
size_t checks=0;
void check(bool ok,const std::string& message) {++checks;if(!ok)throw std::runtime_error(message);}
void bytesToFile(const fs::path& path,const std::vector<uint8_t>& bytes) {
    std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    if(!file)throw std::runtime_error("cannot write isolated test input");
}
std::string caseName(size_t i) {std::ostringstream s;s<<'C'<<std::uppercase<<std::hex<<std::setw(4)<<std::setfill('0')<<i<<".GAM";return s.str();}
}

void runCampaignGamCorpusSmoke() {
    App app(findResourceRoot());app.loadResources();
    const auto all=readFile("research/fixtures/dosbox_gam_corpus/INPUT.BIN");
    check(all.size()==309*App::kGamSaveSize,"invalid GAM corpus");
    check(all==readFile("research/fixtures/dosbox_gam_corpus/ORIGINAL.BIN"),"original DOS file round trip differs");
    const auto returned=readFile("research/fixtures/dosbox_gam_corpus/RETURNED.BIN");
    check(all==returned,"DOS re-save of App output differs");
    const auto dir=fs::path("outputs/gam_corpus_audit")/("cpp_"+std::to_string(GetCurrentProcessId())+"_"+std::to_string(GetTickCount64()));
    fs::create_directories(dir);
    std::ofstream("outputs/gam_corpus_audit/latest-cpp-dir.txt")<<dir.generic_string()<<'\n';
    const auto input=dir/"INPUT.GAM";
    const auto finalDir=fs::path("outputs/phase16_validation")/dir.filename();
    fs::create_directories(finalDir);
    const size_t cases=all.size()/App::kGamSaveSize;
    for(size_t i=0;i<cases;++i) {
        const auto tag=" case="+std::to_string(i);
        std::vector<uint8_t> gam(all.begin()+i*App::kGamSaveSize,all.begin()+(i+1)*App::kGamSaveSize);
        bytesToFile(input,gam);app.gamRawStateValid_=false;app.loadGameFromFile(input);
        check(app.gamRawStateValid_ && app.gamRawState_==gam,"actual original-only Restore failed"+tag);
        check(app.ownedMechs_.size()==App::readGamU16Le(gam,App::kGamOffsetMechCount),"owned roster changed"+tag);
        for(size_t m=0;m<app.ownedMechs_.size();++m)
            check(App::gamChassisId(app.ownedMechs_[m].chassis)==App::readGamU16Le(gam,0xea+2*m),"chassis/slot changed"+tag);
        const auto crewCount=App::readGamU16Le(gam,App::kGamOffsetCrewCount);
        for(size_t s=0;s<crewCount;++s) {
            check(App::skillRank(app.crewMembers_[s].gunnery)==App::readGamU16Le(gam,0xaa+2*s),"gunnery reset"+tag);
            check(App::skillRank(app.crewMembers_[s].piloting)==App::readGamU16Le(gam,0xb2+2*s),"piloting reset"+tag);
            const auto assigned=app.assignedMechIndexForCrewSlot(s);
            check((assigned<0?65535:assigned)==App::readGamU16Le(gam,0xc2+2*s),"assignment changed"+tag);
        }
        const auto state=app.captureCampaignPersistentState();const auto rng=app.repairSession_.rng;
        const auto output=dir/caseName(i);
        check(app.saveCurrentGame(output),"actual Save failed"+tag);
        const auto saved=readFile(output);
        check(saved.size()==gam.size(),"Save length changed"+tag);
        for(size_t p=0;p<gam.size();++p)check(saved[p]==gam[p],"no-action byte changed at "+std::to_string(p)+tag);
        check(app.captureCampaignPersistentState().fingerprint==state.fingerprint,"Save changed runtime"+tag);
        app.loadGameFromFile(output);check(app.gamRawState_==gam,"matching companion Restore failed"+tag);
        check(app.saveCurrentGame(output) && readFile(output)==gam,"second disk Save changed bytes"+tag);
        check(app.repairSession_.rng.word3f5==rng.word3f5 && app.repairSession_.rng.word3f7==rng.word3f7,"Save/Restore consumed RNG"+tag);
        // Byte preservation must not discard a real runtime edit.
        const auto oldMoney=app.playerWealth_;app.playerWealth_^=0x10203u;
        auto edited=gam;app.writeRuntimeToGamSave(edited);
        check(App::readGamU32Le(edited,0x49)==app.playerWealth_,"money edit not saved"+tag);
        std::copy(gam.begin()+0x49,gam.begin()+0x4d,edited.begin()+0x49);
        check(edited==gam,"money edit changed unrelated bytes"+tag);app.playerWealth_=oldMoney;
        // Close the last DOS -> App leg with the pinned original DOS output,
        // not merely a byte comparison against an earlier App save.
        const auto dosInput=finalDir/("D"+caseName(i));
        bytesToFile(dosInput,{returned.begin()+i*App::kGamSaveSize,returned.begin()+(i+1)*App::kGamSaveSize});
        app.playerWealth_^=0x10203u;
        app.loadGameFromFile(dosInput);
        check(app.gamRawState_==gam && app.playerWealth_==oldMoney,"final DOS -> App Restore failed"+tag);
        check(std::none_of(app.ownedMechs_.begin(),app.ownedMechs_.end(),[](const auto& m){return m.exactDamage.has_value();}),
            "DOS-only Restore retained an earlier companion"+tag);
        const auto finalSave=finalDir/caseName(i);
        check(app.saveCurrentGame(finalSave) && readFile(finalSave)==gam,"final App Save changed DOS-returned bytes"+tag);
    }
    // Mech slots initialize/clear their owned fields. Crew removal instead
    // shifts the complete original planes and retains the inactive tail.
    auto zero=std::vector<uint8_t>(all.begin()+128*App::kGamSaveSize,all.begin()+129*App::kGamSaveSize);
    app.applyGamSaveToRuntime(zero);
    auto mech=App::makeMech(App::ChassisId::Jenner);mech.ammoPacks=App::ammunitionCapacityForMech(mech);
    const auto capacity=mech.ammoPacks;app.ownedMechs_.push_back(mech);
    auto added=zero;app.writeRuntimeToGamSave(added);
    check(App::readGamU16Le(added,0xe8)==1 && App::readGamU16Le(added,0xea)==2,"new slot not initialized");
    const auto first=std::find_if(capacity.begin(),capacity.end(),[](int n){return n>0;});
    check(first!=capacity.end() && App::readGamU16Le(added,0x25e)==*first &&
        App::readGamU16Le(added,0x2fa)==static_cast<uint16_t>(first-capacity.begin()),"new slot inherited dormant ammunition");
    app.ownedMechs_.clear();auto removed=added;app.writeRuntimeToGamSave(removed);
    check(App::readGamU16Le(removed,0xe8)==0 && App::readGamU16Le(removed,0xea)==65535,"removed chassis retained");
    check(App::readGamU16Le(removed,0x25e)==0 && App::readGamU16Le(removed,0x2fa)==0,"removed slot not cleared");
    auto crewGam=std::vector<uint8_t>(all.begin()+259*App::kGamSaveSize,all.begin()+260*App::kGamSaveSize);
    app.applyGamSaveToRuntime(crewGam);
    for(auto& owned:app.ownedMechs_)if(owned.assignedCrewSlot==0)owned.assignedCrewSlot=-1;
    app.contractAccepted_=true;app.acceptedContract_.originalMissionId=1;
    const auto launchRng=app.repairSession_.rng;
    app.beginMissionLaunch();
    check(!app.missionLaunchPending_ && !app.pendingBattleStartParams_ && app.contractAccepted_,"launch accepted an unassigned commander");
    check(app.repairSession_.rng.word3f5==launchRng.word3f5 && app.repairSession_.rng.word3f7==launchRng.word3f7,"rejected launch consumed RNG");
    while(app.hiredCrewCount()>1)app.dismissCrewSlot(1);
    for(auto& owned:app.ownedMechs_)owned.assignedCrewSlot=-1;
    auto dismissed=crewGam;app.writeRuntimeToGamSave(dismissed);
    for(size_t s=1;s<4;++s)
        check(App::readGamU16Le(dismissed,0x6f+2*s)==App::readGamU16Le(crewGam,0x75) && App::readGamU16Le(dismissed,0xc2+2*s)==65535,
            "original inactive crew tail/assignment differs");
    // Companion integrity still takes precedence over any loaded fields.
    const auto last=dir/caseName(cases-1);app.loadGameFromFile(last);
    const auto before=app.captureCampaignPersistentState().fingerprint;
    auto extra=readFile(last.wstring()+L".mwstate");extra.back()^=1;
    const auto bad=dir/"BAD.GAM";bytesToFile(bad,readFile(last));bytesToFile(bad.wstring()+L".mwstate",extra);
    app.loadGameFromFile(bad);check(app.captureCampaignPersistentState().fingerprint==before && app.status_.find(L"Load failed:")==0,"corrupt companion mutated campaign");
    std::cout<<"mw_campaign_gam_corpus_smoke: ok checks="<<checks<<" cases="<<cases<<" bytes=1816 original_only_matching_companion_repeat_edit=yes\n";
}
int main() {
    try {runCampaignGamCorpusSmoke();return 0;}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
