// The user authorized the saved final battle and cheats on 2026-10-18.
// Two complete App chains: replacement combat with kill cheats, then captured
// DOS terminal actors through the real service/return/presentation pipeline.
#include "../main.cpp"
#include <iostream>
namespace {
using namespace mw::battle;
size_t checks=0;
bool prepareOnly=false;
void check(bool ok,const std::string& why){++checks;if(!ok)throw std::runtime_error(why);}
const fs::path fixture="research/fixtures/dosbox_final_battle";
void capture(const fs::path& p,const std::vector<uint32_t>& pixels){
    std::ofstream f(p,std::ios::binary);f<<"P6\n320 200\n255\n";
    for(auto v:pixels){const char rgb[]={char(v>>16),char(v>>8),char(v)};f.write(rgb,3);}
    check(bool(f),"cannot write capture");
}
void equalPixels(const std::vector<uint32_t>& pixels,const std::string& name){
    const auto rgb=readFile(fixture/(name+".rgb"));check(rgb.size()==pixels.size()*3,"capture dimensions");
    size_t different=0;
    for(size_t i=0;i<pixels.size();++i)
        different+=((pixels[i]>>16)&255)!=rgb[3*i] || ((pixels[i]>>8)&255)!=rgb[3*i+1] || (pixels[i]&255)!=rgb[3*i+2];
    check(!different,name+" different pixels="+std::to_string(different));
}
constexpr std::array<size_t,9> external{{1,0,3,2,5,6,8,7,4}};
constexpr std::array<size_t,8> internal{{1,0,3,2,5,7,6,4}};
constexpr std::array<size_t,10> critical{{0x11,0x13,0xf,0xd,0xb,0x68,0x6a,0x6c,0x6e,0x17}};
void importActor(CombatantSnapshot& actor,const std::vector<uint8_t>& ds,size_t slot) {
    const size_t base=0x4fb6+slot*0x70;
    auto& d=actor.detailedDamage;
    for(size_t n=0;n<9;++n) {
        auto& a=d.armorSections[external[n]];
        a.armorRemaining=readU16Le(ds,base+0x19+n*2);
        check(a.armorRemaining<=a.armorMaximum,"captured armor outside model capacity slot="+std::to_string(slot)+" preset="+actor.mechPresetId+" section="+std::to_string(n)+" remaining="+std::to_string(a.armorRemaining)+" max="+std::to_string(a.armorMaximum));
        a.battleDamage=a.armorMaximum-a.armorRemaining;
    }
    for(size_t n=0;n<8;++n) {
        auto& a=d.internalSections[internal[n]];
        a.structureRemaining=readU16Le(ds,base+0x2b+n*2);
        check(a.structureRemaining<=a.structureMaximum,"captured internal outside model capacity");
        a.battleDamage=a.structureMaximum-a.structureRemaining;
    }
    for(size_t n=0;n<10;++n) {
        auto& c=d.criticalComponents[n];const auto value=readU16Le(ds,base+critical[n]);
        if(n==4 || n==9) {
            check(value<=c.totalCount,"captured destroyed count outside capacity: actor="+
                std::to_string(slot)+" component="+std::to_string(n));
            c.workingCount=static_cast<uint8_t>(c.totalCount-value);
        } else if(n>=5 && n<=8) c.workingCount=static_cast<uint8_t>(value);
        else {c.condition=static_cast<uint8_t>(value);c.workingCount=static_cast<uint8_t>(value<(n==0?3:2));}
        c.functional=c.totalCount==0 || c.workingCount!=0;
    }
    d.originalCockpitCriticalFlag=readU16Le(ds,base+0x15)!=0;
    for(const auto& mount:mw::mech3d::catalogWeaponMounts(actor.mechPresetId))
        d.installedWeapons[mount.slotIndex].functional=ds.at(base+0x3b+3*mount.slotIndex)!=0;
    for(size_t n=0;n<6;++n) actor.ammunitionByPool[n]=readU16Le(ds,base+0x59+2*n);
}
void checkLegProfiles() {
    const auto ds=readFile(fixture/"stage0-ready.ds.bin");
    const std::array<const char*,8> presets{{"locust","jenner","phoenix_hawk","shadow_hawk","rifleman","warhammer","marauder","battlemaster"}};
    for(size_t model=0;model<presets.size();++model) {
        const auto ordinary=mw::mech3d::catalogDamageProfile(presets[model]);
        const auto boosted=mw::mech3d::catalogDamageProfile(presets[model],true);
        for(size_t n=0;n<9;++n)check(boosted.externalArmorMaximumBtechOrder[n]==readU16Le(ds,0x1178+model*0xe0+0xb2+2*n),"live DOS profile armor differs");
        for(size_t n=0;n<8;++n)check(boosted.internalStructureMaximumBtechOrder[n]==readU16Le(ds,0x1178+model*0xe0+0xc4+2*n),"live DOS profile structure differs");
        for(uint8_t skill=0;skill<4;++skill) {
            BattleStartParams p;p.playerMechPresetId=presets[model];p.playerRoster.gunnerySkill=skill;
            auto actor=BattleWorld::create(p).snapshot().combatants[0];
            const auto& expected=skill>1?boosted:ordinary;
            for(size_t n=0;n<9;++n)check(actor.detailedDamage.armorSections[external[n]].armorMaximum==expected.externalArmorMaximumBtechOrder[n],"commander skill profile gate");
            auto state=campaignMechStateFromCombatant(actor);
            check(validatePersistentMechDamage(state,presets[model]),"valid profile rejected");
            auto corrupt=state;++corrupt.exactDamage->detailed.armorSections[external[2]].armorMaximum;
            check(!validatePersistentMechDamage(corrupt,presets[model]),"arbitrary enlarged profile accepted");
            auto& leg=state.exactDamage->detailed.armorSections[external[2]];
            leg.armorRemaining=1;leg.battleDamage=leg.armorMaximum-1;
            BattleCombatantLaunchState launch;launch.mechPresetId=presets[model];launch.persistentMechState=state;
            p.playerLaunchState=launch;
            const auto same=BattleWorld::create(p).snapshot().combatants[0];
            check(same.detailedDamage.armorSections[external[2]].armorRemaining==1,"same-profile exact entry lost points");
            p.playerRoster.gunnerySkill=skill>1?1:3;
            const auto switched=BattleWorld::create(p).snapshot().combatants[0];
            const auto& changed=switched.detailedDamage.armorSections[external[2]];
            check(changed.armorRemaining==changed.armorMaximum/leg.armorMaximum,"profile change did not preserve remaining fraction");
            check(validatePersistentMechDamage(campaignMechStateFromCombatant(switched),presets[model]),"profile change invalidated exact save");
        }
    }
}
}
void runFinalBattleValidationSmoke(){
    const auto output=fs::path("outputs/final_battle_validation")/("cpp_"+std::to_string(GetCurrentProcessId())+"_"+std::to_string(GetTickCount64()));
    fs::create_directories(output);
    std::ofstream("outputs/final_battle_validation/latest-cpp.txt")<<output.string();
    if(!prepareOnly)checkLegProfiles();
    for(bool importDos:{false,true}) {
        App app(findResourceRoot());app.loadResources();app.loadGameFromFile(fixture/(prepareOnly?"prepared-final.gam":"canonical-prepared.gam"));
        if(prepareOnly) {check(app.saveCurrentGame("outputs/final_battle_validation/CANON.GAM"),"canonical prepared Save failed");return;}
        check(app.isStorySceneActive() && app.storyPages_.back().choices.size()==2,"saved Radstadt prompt missing");
        check(app.currentMissionParticipants().size()==4,"prepared four-mech deployment missing");
        const auto wealth=app.playerWealth_;const auto day=app.currentMonthDayCounter_;
        const auto rep=app.playerReputationPoints_;const auto xp=app.crewMembers_[0].missionExperience;
        app.currentStoryChoiceIndex_=0;app.advanceStoryScene();
        check(app.campaignBattleRuntime_.active && app.missionSequence_.stageIndex==0,"ATTACK did not launch battle");
        const auto prefix=std::string(importDos?"dos-actors-":"replacement-");
        for(size_t stage=0;stage<2;++stage) {
            auto& rt=app.campaignBattleRuntime_;check(rt.world.has_value(),"stage world missing");
            const auto initial=rt.world->snapshot();
            check(initial.combatants.size()==8,"final roster differs");
            const auto params=app.battleStartParamsForDarkWingFinalMission();
            check(params.mission.originalId==(stage?12:16) && params.setupMetadata.missionSelector==(stage?12:16),"final stage selector differs");
            check(params.terrainScenarioIndex==2+stage && params.combatConclusionDelaySeconds==5,"final map or ordinary five-second delay differs");
            std::vector<std::string> enemies;
            for(const auto& a:initial.combatants)if(a.roster.team==BattleTeam::Opposing)enemies.push_back(a.mechPresetId);
            check(enemies==std::vector<std::string>{"battlemaster","battlemaster","battlemaster","warhammer"},"Dark Wing composition differs");
            app.renderCampaignBattleRuntime(initial);capture(output/(prefix+"stage"+std::to_string(stage)+"-ready.ppm"),app.framebuffer_);
            // Exercise the real AI/movement/projectile tick before the cheat.
            rt.world->runTicks(importDos?1:40);
            for(const auto& a:initial.combatants)if(a.roster.team==BattleTeam::Opposing)
                rt.world->setMissionStatus(a.id,CombatantMissionStatus::Destroyed);
            const auto begin=rt.world->elapsedMs();
            for(int i=0;i<200 && !rt.world->battleResult();++i)rt.world->tick();
            check(rt.world->battleResult() && rt.world->battleResult()->rawResultCode==0,"final victory predicate did not resolve");
            check(rt.world->elapsedMs()-begin>=5000,"five-second delay shortened");
            rt.currentPresentationSnapshot=rt.world->snapshot();rt.presentationSnapshotsValid=true;
            if(importDos) {
                const auto ds=readFile(fixture/(stage?"stage1-after-2.ds.bin":"stage0-after-2.ds.bin"));
                for(size_t n=0;n<8;++n)importActor(rt.currentPresentationSnapshot.combatants[n],ds,n);
            }
            const auto before=app.captureCampaignPersistentState().fingerprint;
            app.finalizeCampaignBattleRuntimeOutcome();
            if(!stage) {
                check(app.missionSequence_.stageIndex==1 && app.campaignBattleRuntime_.active,"stage0 did not advance directly to stage1");
                check(app.captureCampaignPersistentState().fingerprint==before && !app.campaignCompleted_,"interstage settled campaign");
            }
        }
        check(app.state_==ScreenState::CampaignEnding && app.campaignCompleted_==1,"terminal final victory did not enter ending");
        check(app.storyFlag(0x72) && app.storyFlag(0x75) && !app.storyFlag(0x76),"ending flags differ");
        check(app.playerWealth_==wealth && app.currentMonthDayCounter_==day && app.playerReputationPoints_==rep && app.crewMembers_[0].missionExperience==xp,"final applied ordinary settlement");
        app.endingFrame_=44;app.render();capture(output/(prefix+"ending.ppm"),app.framebuffer_);
        // Stable ending pixels are checked after the original wait-for-key capture.
        // The choice screen below also includes the complete ending background.
        app.update(app.stateStartedTick_+45*275);app.finishCampaignEnding();app.renderStoryScene();
        capture(output/(prefix+"ending-choice.ppm"),app.framebuffer_);equalPixels(app.framebuffer_,"ending-choice");
        check(app.storyFlag(0x76) && app.storyPages_[0].choices.size()==2,"ending choices missing");
        app.currentStoryChoiceIndex_=0;app.advanceStoryScene();
        check(app.state_==ScreenState::MainMenu && !app.tryStartFinalBasePrompt(),"CONTINUE repeated final encounter");
        const auto save=output/(prefix+"WIN.GAM");check(app.saveCurrentGame(save),"victory Save failed");
        const auto gam=readFile(save);app.loadGameFromFile(save);
        auto again=gam;app.writeRuntimeToGamSave(again);
        check(gam==again && app.campaignCompleted_==1 && !app.tryStartFinalBasePrompt(),"victory Restore lost completed state");
        check(fs::exists(save.wstring()+L".mwstate"),"exact companion missing");
        if(importDos) {
            const auto dos=readFile(fixture/"CANWIN.GAM");
            std::ostringstream differences;
            for(size_t n=0;n<gam.size();++n)if(gam[n]!=dos[n])differences<<std::hex<<n<<":"<<unsigned(gam[n])<<"/"<<unsigned(dos[n])<<" ";
            std::ofstream(output/"dos-differences.txt")<<differences.str();
            check(gam==dos,"DOS final Save differs: "+differences.str());
        }
    }
    App restored(findResourceRoot());restored.loadResources();restored.loadGameFromFile(fixture/"CANWIN.GAM");
    check(restored.saveCurrentGame(output/"DOS-RESTORED.GAM"),"DOS completed Save import failed");
    check(readFile(output/"DOS-RESTORED.GAM")==readFile(fixture/"RECAN.GAM"),"DOS -> App re-save differs");
    std::cout<<"mw_final_battle_validation_smoke: ok checks="<<checks<<" saved_final=1 stages=2 delay=5 cheats=1 DOS_roundtrip=1\n";
}
int main(int argc,char** argv){try{prepareOnly=argc==2 && std::string(argv[1])=="--prepare";runFinalBattleValidationSmoke();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
