// Controlled post-result tests; never starts the saved Radstadt final battle.
#include "../main.cpp"
#include <iostream>
namespace {
size_t checks=0;
void check(bool value,const char* reason) { ++checks; if(!value) throw std::runtime_error(reason); }
}
void runCampaignEndingSmoke() {
    using namespace mw::battle;
    const auto gate=readFile("research/fixtures/dosbox_ending/GATE.BIN");
    check(gate.size()==65536u*4,"DOS gate fixture missing");
    for(size_t i=0;i<65536;++i) {
        const bool win=originalDarkWingEnding(static_cast<uint8_t>(i),static_cast<uint8_t>(i>>8));
        check(gate[i*4]==int(win) && gate[i*4+1]==int(win) && gate[i*4+2]==int(win) && gate[i*4+3]==int(!win),
            "DOS ending gate/flags differ");
    }
    const auto trace=readFile("research/fixtures/dosbox_ending/TRACE.BIN");
    size_t frame=0,sprite=0,draws=0,backgrounds=0,choices=0;
    for(size_t i=0;i<trace.size();i+=8) {
        const auto event=readU16Le(trace,i),si=readU16Le(trace,i+2),ax=readU16Le(trace,i+4),bx=readU16Le(trace,i+6);
        if(event==1) {
            const auto f=originalDarkWingEndingFrame(frame);
            check(sprite<f.count,"extra original sprite");
            const auto expected=f.sprites[sprite++];
            check(si==expected.entry && ax==expected.x && bx==expected.y,"DOS animation command differs");
            ++draws;
            if(sprite==f.count) { ++frame;sprite=0; }
        } else if(event==2) { check(si==77 && ax==0 && bx==0,"DOS background command");++backgrounds; }
        else if(event==3) check(ax==14 && bx==140,"original prologue position");
        else if(event==5) { check(bx==0x6353,"original continue/quit record");++choices; }
    }
    check(frame==45 && draws==108 && backgrounds==46 && choices==1,"DOS ending sequence incomplete");
    App app(findResourceRoot());app.loadResources();
    const auto start=readFile("research/fixtures/dosbox_story/start.gam");
    app.applyGamSaveToRuntime(start);
    check(app.endingArchive_.images.size()==9 && app.endingBackground_.images.size()==1,"ending images not decoded");
    CampaignBattleOutcomePackage outcome;
    outcome.valid=true;outcome.terminal=true;outcome.launchSource=CampaignBattleLaunchSource::DarkWingFinal;
    outcome.outcome=CampaignBattleOutcome::Victory;outcome.rawResultCode=0;outcome.finalSnapshotFingerprint=12345;
    CampaignBattleConsequenceContext hostileContext;
    hostileContext.salvage=999999;hostileContext.campaignDateTicks=50;hostileContext.pilotExperienceAward=true;
    auto plan=campaignBattleConsequencePlanFromOutcome(outcome,{},hostileContext);
    check(plan.commitEligible && plan.payment==0 && plan.salvage==0 && plan.campaignDateTicks==0 &&
        !plan.pilotExperienceAward && plan.reputationDelta==0 && !plan.salvageDeferred &&
        !plan.campaignTimeDeferred && !plan.pilotExperienceDeferred,"ending applied ordinary settlement");
    for(auto result:{CampaignBattleOutcome::Defeat,CampaignBattleOutcome::Unsupported}) {
        auto other=outcome;other.outcome=result;other.rawResultCode=2;
        check(campaignBattleConsequencePlanFromOutcome(other,{},{}).commitEligible == (result==CampaignBattleOutcome::Defeat) && !App::isDarkWingVictory(other),
            "failed/unsupported final entered victory ending");
    }
    auto nonterminal=outcome;nonterminal.terminal=false;
    check(!App::isDarkWingVictory(nonterminal),"nonterminal victory entered ending");
    auto prepare=[&] {
        app.applyGamSaveToRuntime(start);app.clearStoryScene();
        app.acceptedBattleOutcome_=outcome;app.acceptedBattleConsequencePlan_=plan;
        app.acceptedBattlePersistenceReport_={};app.acceptedBattleMechReturnPlan_={};
        app.lastCommittedBattleConsequencePlanFingerprint_=0;
        app.acceptedBattleStateGuard_.before=app.captureCampaignPersistentState();
        app.acceptedBattleStateGuard_.after=app.acceptedBattleStateGuard_.before;
        app.acceptedBattleStateGuard_.valid=true;app.acceptedBattleStateGuard_.unchanged=true;
    };
    prepare();
    const auto before=app.captureCampaignPersistentState().fingerprint;
    app.acceptedBattleStateGuard_.after.fingerprint^=1;
    app.acknowledgeAcceptedBattleOutcome();
    check(app.acceptedBattleOutcome_.valid && !app.campaignCompleted_ &&
        app.captureCampaignPersistentState().fingerprint==before,"stale ending committed");
    app.acceptedBattleStateGuard_.after.fingerprint^=1;
    // Force a post-mutation validation failure and verify flags roll back too.
    app.playerWealth_=0x100000000ull;
    app.acceptedBattleStateGuard_.after=app.captureCampaignPersistentState();
    const auto badBefore=app.captureCampaignPersistentState().fingerprint;
    auto failed=app.commitAcceptedBattleConsequencePlan();
    check(failed.attempted && !failed.committed && !app.campaignCompleted_ &&
        app.captureCampaignPersistentState().fingerprint==badBefore,"ending flag rollback failed");
    prepare();
    auto beforeGam=app.baseGamSaveData();app.writeRuntimeToGamSave(beforeGam);
    auto committed=app.commitAcceptedBattleConsequencePlan();
    check(committed.committed && committed.verified && committed.saveRoundTripVerified,"ending commit failed");
    check(app.commitAcceptedBattleConsequencePlan().duplicateRejected,"duplicate ending committed");
    app.acknowledgeAcceptedBattleOutcome();
    check(app.state_==ScreenState::CampaignEnding && !app.acceptedBattleOutcome_.valid && app.campaignCompleted_==1 &&
        app.storyFlag(0x72) && app.storyFlag(0x75) && !app.storyFlag(0x76),"ending routing/flags failed");
    auto afterGam=beforeGam;app.writeRuntimeToGamSave(afterGam);
    // BTECH's returned packet persists before22A9; it is not an unrelated
    // field. This controlled outcome has no casualties/trophies/defeats.
    auto expectedResult=decodeOriginalCampaignResult(beforeGam);
    expectedResult.crewStatus.fill(0);expectedResult.salvage={};
    expectedResult.defeatedOpponents=0;expectedResult.result=0;expectedResult.accepted=0;
    auto expectedGam=beforeGam;encodeOriginalCampaignResult(expectedGam,expectedResult);
    for(size_t i=0;i<afterGam.size();++i)
        if(i!=0x714 && i!=0x672+0x72 && i!=0x672+0x75) check(afterGam[i]==expectedGam[i],"ending altered unrelated GAM field/result packet");
    app.currentPlanetIndex_=app.findPlanetIndexByName("RADSTADT");app.setStoryFlag(0x43);
    check(!app.tryStartFinalBasePrompt(),"completed campaign repeated final encounter");
    fs::create_directories("outputs/ending_audit/screens");
    fs::create_directories("outputs/ending_animation_validation/cpp");
    std::array<std::vector<uint8_t>,7> originalFrames;
    for(size_t n=0;n<originalFrames.size();++n) {
        originalFrames[n]=readFile("research/fixtures/dosbox_ending_animation/dos-"+std::to_string(n)+".rgb");
        check(originalFrames[n].size()==320u*200u*3u,"original ending frame missing");
    }
    // Exercise the production clock and input path across DWORD wrap, rather
    // than manually selecting frames. Every displayed phase has a DOS oracle.
    app.stateStartedTick_=0xfffff000u;
    for(size_t n=0;n<45;++n) {
        app.update(app.stateStartedTick_+static_cast<DWORD>(n*275));
        check(app.endingFrame_==n,"ending clock skipped a frame boundary");
        app.render();const auto previous=app.framebuffer_;app.render();
        check(app.framebuffer_==previous,"ending render mutates state");
        const auto& original=originalFrames[n<5?n:5+(n-5)%2];
        size_t different=0;
        for(size_t pixel=0;pixel<app.framebuffer_.size();++pixel) {
            const auto rgb=app.framebuffer_[pixel];
            different+=((rgb>>16)&255)!=original[pixel*3] ||
                ((rgb>>8)&255)!=original[pixel*3+1] || (rgb&255)!=original[pixel*3+2];
        }
        check(different==0,"ending frame differs from original DOS pixels");
        std::ofstream animationFrame("outputs/ending_animation_validation/cpp/frame-"+std::to_string(n)+".ppm",std::ios::binary);
        animationFrame<<"P6\n320 200\n255\n";
        for(auto pixel:app.framebuffer_) {const char rgb[]={char(pixel>>16),char(pixel>>8),char(pixel)};animationFrame.write(rgb,3);}
        app.update(app.stateStartedTick_+static_cast<DWORD>(n*275+274));
        app.handleKey(VK_SPACE);
        check(app.endingFrame_==n && app.state_==ScreenState::CampaignEnding && !app.storyFlag(0x76),
            "ending frame shortened or early input skipped the animation");
        if(n==0 || n==4 || n==5 || n==44) {
            std::ofstream ppm("outputs/ending_audit/screens/frame-"+std::to_string(n)+".ppm",std::ios::binary);
            ppm<<"P6\n320 200\n255\n";
            for(auto pixel:app.framebuffer_) {const char rgb[]={char(pixel>>16),char(pixel>>8),char(pixel)};ppm.write(rgb,3);}
        }
    }
    app.finishCampaignEnding();check(app.state_==ScreenState::CampaignEnding,"ending input bypassed animation");
    app.update(app.stateStartedTick_+45*275);
    check(app.endingFrame_==45 && app.state_==ScreenState::CampaignEnding,"ending did not wait for final input");
    app.render();const auto finalFrame=app.framebuffer_;
    app.update(app.stateStartedTick_+90000);app.render();
    check(app.endingFrame_==45 && app.framebuffer_==finalFrame,"completed ending did not hold its final frame");
    app.handleKey(VK_SPACE);
    check(app.storyFlag(0x76) && app.isStorySceneActive() && app.storyPages_[0].choices.size()==2 &&
        app.storyPages_[0].choices[1].action==App::StoryAction::QuitToDos &&
        app.lastBattlePostResultReceipt_.extendedEndingSequenceExecuted,"ending choice missing");
    app.advanceStoryScene();check(app.state_==ScreenState::MainMenu,"CONTINUE did not return to campaign");
    const auto save=fs::path("outputs/ending_audit")/("save_"+std::to_string(GetTickCount64()));fs::create_directories(save);
    check(app.saveCurrentGame(save/"END.GAM"),"ending Save failed");
    const auto saved=readFile(save/"END.GAM");check(saved.size()==1816,"GAM format changed");
    app.loadGameFromFile(save/"END.GAM");
    auto restored=saved;app.writeRuntimeToGamSave(restored);
    check(saved==restored && !app.tryStartFinalBasePrompt() && app.campaignCompleted_==1,"ending Restore failed");
    prepare();
    CampaignMechReturnEntry returned;
    returned.ownedMechIndex=0;returned.crewSlot=0;
    returned.mechPresetId=App::battleMechPresetForChassis(app.ownedMechs_[0].chassis).value();
    returned.state=App::battlePersistentMechStateFromOwnedMech(app.ownedMechs_[0],"controlled_final_return");
    returned.state.sensors=2;returned.state.armorDamage[7]=2;
    returned.ammunitionStateValid=true;returned.ammunitionByPool=app.ownedMechs_[0].ammoPacks;
    size_t pool=0;while(pool<returned.ammunitionByPool.size() && returned.ammunitionByPool[pool]==0)++pool;
    check(pool<returned.ammunitionByPool.size(),"fixture has no ammunition");--returned.ammunitionByPool[pool];
    app.acceptedBattleMechReturnPlan_.valid=true;app.acceptedBattleMechReturnPlan_.fingerprint=9876;
    app.acceptedBattleMechReturnPlan_.entries={returned};
    app.acceptedBattleMechReturnPlan_.killedCrewSlots[0]=true;
    app.acknowledgeAcceptedBattleOutcome();
    check(app.acceptedBattleOutcome_.valid && !app.campaignCompleted_,"commander death continued campaign");
    app.acceptedBattleMechReturnPlan_.killedCrewSlots[0]=false;
    app.acceptedBattleMechReturnPlan_.entries[0].ammunitionByPool[pool]=999;
    const auto beforeDamage=app.captureCampaignPersistentState().fingerprint;
    check(!app.commitAcceptedBattleConsequencePlan().committed && !app.campaignCompleted_ &&
        app.captureCampaignPersistentState().fingerprint==beforeDamage,"damage and ending flags did not roll back together");
    app.acceptedBattleMechReturnPlan_.entries[0]=returned;
    app.acknowledgeAcceptedBattleOutcome();
    check(app.state_==ScreenState::CampaignEnding && app.campaignCompleted_==1 && app.status_.empty() &&
        app.ownedMechs_[0].sensors==App::runtimeDamageState(2) &&
        app.ownedMechs_[0].armorDamage[8]==2 && app.ownedMechs_[0].ammoPacks==returned.ammunitionByPool,
        "ending lost returned damage/ammunition or retained an error");
    check(app.saveCurrentGame(save/"DAMAGE.GAM"),"ending damage Save failed");
    app.loadGameFromFile(save/"DAMAGE.GAM");
    check(app.campaignCompleted_==1 && app.ownedMechs_[0].sensors==App::runtimeDamageState(2) &&
        app.ownedMechs_[0].ammoPacks==returned.ammunitionByPool,"ending damage Restore failed");
    for(int marker=0;marker<256;++marker) {
        auto input=start;input[0x714]=static_cast<uint8_t>(marker);app.applyGamSaveToRuntime(input);
        auto output=input;app.writeRuntimeToGamSave(output);check(output[0x714]==marker,"imported completion byte normalized");
    }
    app.restartCampaign();check(!app.campaignCompleted_ && !app.storyFlag(0x72) && !app.storyFlag(0x75),"restart retained ending");
    std::cout<<"mw_campaign_ending_smoke: ok checks="<<checks<<" dos_gate=65536 frames=45 dos_pixel_frames=7 timeline_pixel_comparisons=45 clock_wrap=1 early_input=45 save_restore=1 final_battle_started=0\n";
}
int main(){try{runCampaignEndingSmoke();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
