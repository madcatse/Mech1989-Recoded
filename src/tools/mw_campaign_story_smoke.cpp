// Real campaign handlers; original RAM captures supply a shared new-game start.
#include "../main.cpp"
#include <iostream>
#include <iomanip>
#include <set>

namespace {
size_t checks=0;
bool visuals=false;
void check(bool ok,const std::string& message) { ++checks;if(!ok)throw std::runtime_error(message); }
std::string ascii(const std::wstring& s) {
    std::string result;result.reserve(s.size());
    for(const wchar_t c:s)result.push_back(c<=127 ? static_cast<char>(c) : '?');
    return result;
}
}
void runCampaignStorySmoke() {
    App app(findResourceRoot());app.loadResources();
    const fs::path start="research/fixtures/dosbox_story/start.gam";
    check(fs::exists(start),"missing original story start");
    const auto original=readFile(start);
    app.loadGameFromFile(start);
    std::ifstream travelOracle("research/fixtures/original_story_travel.txt");
    check(bool(travelOracle),"missing original travel instruction oracle");
    std::string travelHeader;std::getline(travelOracle,travelHeader);
    int fromId=0,toId=0,jumps=0;size_t pairs=0;
    while(travelOracle>>fromId>>toId>>jumps) {
        const int from=app.planetIndexFromTableOrder(static_cast<uint16_t>(fromId));
        const int to=app.planetIndexFromTableOrder(static_cast<uint16_t>(toId));
        check(app.travelJumpCount(from,to)==(fromId==toId ? 0 : jumps),"original travel pair mismatch");++pairs;
    }
    check(pairs==21025,"incomplete travel oracle");
    check(app.currentPlanetName()==L"TANCREDI IV" && app.grigDestinationPlanet()==L"TABAYAMA" &&
          app.wendallDestinationPlanet()==L"MOSIRO" && app.kearneyDestinationPlanet()==L"TANCREDI IV" &&
          app.blackWidowDestinationPlanet()==L"MISERY" && app.darkWingDestinationPlanet()==L"RADSTADT",
          "original generated destinations lost");
    const fs::path output=fs::path("outputs/grig_story_audit")/("cpp_"+std::to_string(GetTickCount64()));
    fs::create_directories(output);std::ofstream trace(output/"trace.tsv");
    size_t page=0;
    auto scene=[&] {
        if(!app.isStorySceneActive()) return;
        const auto& p=app.storyPages_[app.currentStoryPageIndex_];
        trace<<"PAGE\t"<<page<<'\t';for(const auto& l:p.lines)trace<<ascii(l)<<" | ";trace<<'\n';
        if(visuals) {
            app.render();std::ofstream image(output/("page-"+std::to_string(page)+".ppm"),std::ios::binary);
            image<<"P6\n320 200\n255\n";for(auto c:app.framebuffer_) {
                const char rgb[]={static_cast<char>(c>>16),static_cast<char>(c>>8),static_cast<char>(c)};image.write(rgb,3);
            }
        }
        ++page;
    };
    auto finish=[&] {
        for(int n=0;app.isStorySceneActive() && n<100;++n) {
            if(!app.storyPages_[app.currentStoryPageIndex_].choices.empty())break;
            scene();app.advanceStoryScene();
        }
    };
    auto choose=[&](const std::wstring& label) {
        finish();check(app.isStorySceneActive(),"choice without active scene: "+ascii(label));
        const auto& choices=app.storyPages_[app.currentStoryPageIndex_].choices;
        const auto it=std::find_if(choices.begin(),choices.end(),[&](const auto& c){return c.label==label;});
        check(it!=choices.end(),"missing story choice: "+ascii(label));
        scene();app.currentStoryChoiceIndex_=static_cast<size_t>(it-choices.begin());app.advanceStoryScene();
    };
    auto checkpoint=[&](const std::string& name) {
        auto data=app.baseGamSaveData();app.writeRuntimeToGamSave(data);
        std::ofstream gam(output/(name+".gam"),std::ios::binary);
        gam.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));
        trace<<"STATE\t"<<name<<'\t'<<ascii(app.currentPlanetName())<<'\t'<<app.playerWealth_
             <<'\t'<<app.currentYear_<<'/'<<app.currentMonth_<<'/'<<app.currentMonthDayCounter_<<'\t';
        for(size_t i=0;i<0x8c;++i)if(app.storyMessageFlags_[i])trace<<std::hex<<i<<',';
        trace<<std::dec<<'\n';
        check(data.size()==1816,"GAM layout changed");
        const auto dos=readFile(fs::path("research/fixtures/dosbox_story")/(name+".gam"));
        check(dos.size()==data.size(),"original checkpoint size");
        for(const auto range: {std::pair<size_t,size_t>{0x21,2},{0x31,6},{0x49,4},{0xE8,2},{0x6FE,12}})
            for(size_t i=range.first;i<range.first+range.second;++i)
                check(data[i]==dos[i],"DOS checkpoint field mismatch: "+name+" offset "+std::to_string(i));
        const bool expiryDifference=name=="black-widow-lead" || name=="tasha" || name=="albiero-lead" ||
            name=="tasha-reward" || name=="final-prompt";
        for(size_t i=0;i<0x8c;++i) {
            // Original6488 uses month as the expiry day. We retain the stated
            // date instead, so this non-plot article remains readable Feb9.
            if(i==0x2A && expiryDifference)
                check(data[0x672+i]==1 && dos[0x672+i]==0,"unexpected expiry-policy difference");
            else check(data[0x672+i]==dos[0x672+i],"DOS story flag mismatch: "+name+" flag "+std::to_string(i));
        }
    };
    auto reset=[&] {app.clearStoryScene();app.loadGameFromFile(start);};
    auto revenge=[&] {
        reset();app.storyMessageFlags_.fill(0);app.setStoryFlag(0x17);app.setStoryFlag(0x64);
        app.currentPlanetIndex_=app.findPlanetIndexByName("TABAYAMA");
        check(app.tryStartPostTravelStory() && app.storyFlag(0x45),"revenge arrival gate");
    };
    for(int branch=0;branch<5;++branch) {
        revenge();const auto before=app.playerWealth_;
        if(branch==0) choose(L"NOT INTERESTED");
        else {
            choose(L"GO ON");
            if(branch==1) choose(L"NO DEAL");
            else {
                choose(L"DEAL");
                if(branch==2) choose(L"FORGET IT");
                else { choose(L"WAIT");
                    if(branch==3) choose(L"WAIT");
                    else {choose(L"STRIKE NOW");choose(L"ACCEPT HIS OFFER");}
                }
            }
        }
        finish();check(app.playerWealth_==before,"non-success revenge paid bounty");
        if(branch==4)check(app.storyFlag(0x4D) && app.isStorySceneActive(),"Grig death prompt missing");
        app.clearStoryScene();check(!app.tryStartPostTravelStory(),"abandoned revenge repeated");
    }
    revenge();choose(L"GO ON");choose(L"DEAL");choose(L"WAIT");choose(L"STRIKE NOW");
    const auto cash=app.playerWealth_;choose(L"PULL THE TRIGGER");finish();
    check(app.playerWealth_==cash+200000 && app.storyFlag(0x4E) && !app.storyFlag(0x43),"bounty/finale flag collision");
    check(!app.tryStartPostTravelStory(),"bounty arrival repeated");
    const auto saved=output/"BOUNTY.GAM";check(app.saveCurrentGame(saved),"bounty Save failed");
    reset();app.loadGameFromFile(saved);
    check(app.storyFlag(0x45) && app.storyFlag(0x4E) && !app.storyFlag(0x43) &&
          app.playerWealth_==cash+200000 && !app.tryStartPostTravelStory(),"bounty Restore/re-entry failed");
    app.executeStoryAction(App::StoryAction::GrigRevengeKill);finish();
    check(app.playerWealth_==cash+200000,"duplicate reward callback paid twice");
    // Identifiable old provisional states migrate once; original45 alone never does.
    for(int completed=0;completed<2;++completed) {
        reset();app.storyMessageFlags_.fill(0);app.setStoryFlag(0x37);app.setStoryFlag(0x39);
        app.setStoryFlag(0x41);app.setStoryFlag(0x42);if(completed)app.setStoryFlag(0x45);
        app.applyOriginalStoryCompatibilityFlags();
        check(app.storyFlag(0x38) && app.storyFlag(0x40) && app.storyFlag(0x3E) &&
              app.storyFlag(0x43)==bool(completed) && !app.storyFlag(0x45) &&
              !app.storyFlag(0x41) && !app.storyFlag(0x42),"legacy late-story migration failed");
        const auto once=app.storyMessageFlags_;app.applyOriginalStoryCompatibilityFlags();
        check(app.storyMessageFlags_==once,"migration not idempotent");
    }
    reset();app.storyMessageFlags_.fill(0);app.setStoryFlag(0x45);
    const auto untouched=app.storyMessageFlags_;app.applyOriginalStoryCompatibilityFlags();
    check(app.storyMessageFlags_==untouched,"original Grig flag was reinterpreted");
    // Same original new-game start, then ordinary handlers through the whole plot.
    reset();checkpoint("start");
    app.selectedMechIndex_=0;app.mechStatusMenuIndex_=3;app.activateMechStatusSelection();
    check(app.state_==ScreenState::MechSellOffer,"starting sale refused");
    app.activateMechSellOfferSelection();check(app.ownedMechs_.empty() && app.playerWealth_==4037000,"original starting sale differs");
    checkpoint("sold");app.returnToMainMenu(false);
    auto drink=[&] {app.activatePlanetIcon(App::kPlanetBarIconIndex);app.activateBarMenuItem(0);finish();};
    auto leaveBar=[&] {app.activateBarMenuItem(2);finish();};
    auto readNews=[&] {
        const bool firstVisit=!app.storyFlag(0x36);
        app.openNewsNet();
        if(firstVisit)check(!app.storyFlag(0x36),"unread story letter marked as read");
        for(int n=0;n<100 && !app.newsNetShowingNoOther_;++n) {
            const auto index=app.activeNewsNetMessageIndexes_.at(static_cast<size_t>(app.newsNetMessageIndex_));
            trace<<"NEWS\t"<<std::hex<<static_cast<int>(app.newsNetMessageId(index))<<std::dec<<'\n';
            app.activateNewsNetButton(1);
        }
        check(app.newsNetShowingNoOther_,"NewsNet traversal did not terminate");app.closeNewsNet();
    };
    auto travel=[&](const std::wstring& name) {
        app.returnToMainMenu(false);app.activatePlanetIcon(App::kPlanetStarmapIconIndex);
        app.selectedPlanetIndex_=app.findPlanetIndexByName(ascii(name));
        check(app.planets_.at(static_cast<size_t>(app.selectedPlanetIndex_)).name==ascii(name),"unknown destination");
        app.beginTravelToSelectedPlanet();check(app.state_==ScreenState::TravelRoutePreview,"travel preview missing");
        check(app.playerWealth_>=app.pendingTravelCost_,"story travel exceeds wealth");
        app.changeState(ScreenState::TravelAnimation);app.updateTravelAnimation(100000);
    };
    drink();leaveBar();drink();leaveBar();checkpoint("early-clues");
    travel(L"GALEDON V");finish();app.activatePlanetIcon(App::kPlanetContractIconIndex);finish();checkpoint("galedon");
    travel(L"LAND'S END");finish();drink();
    app.activateBarMenuItem(0);finish(); // A second drink alone does not produce0C.
    check(!app.storyFlag(0x0C),"Lands End contact incorrectly uses ORDER DRINK");
    leaveBar();app.activatePlanetIcon(App::kPlanetBarIconIndex);finish();leaveBar();checkpoint("lands-end");
    const auto grig=app.grigDestinationPlanet();travel(grig);choose(L"YES");finish();checkpoint("grig-offer");
    travel(L"DUSTBALL");choose(L"RUN");choose(L"RUN");finish();checkpoint("dustball");
    drink();leaveBar();drink();leaveBar();checkpoint("stone-arrow");
    travel(grig);choose(L"GO ON");choose(L"DEAL");choose(L"WAIT");choose(L"STRIKE NOW");
    choose(L"PULL THE TRIGGER");finish();checkpoint("grig-bounty");
    travel(app.wendallDestinationPlanet());finish();app.activatePlanetIcon(App::kPlanetMechLabIconIndex);finish();
    checkpoint("wendall");app.activateMechLabMenuItem(3);choose(L"RUN");finish();checkpoint("sniper");
    travel(app.kearneyDestinationPlanet());finish();drink();leaveBar();
    choose(L"FOLLOW ADDRESS");choose(L"HIDE");choose(L"RUN");finish();checkpoint("office");
    readNews();checkpoint("black-widow-lead");
    travel(app.blackWidowDestinationPlanet());finish();app.activatePlanetIcon(App::kPlanetBarIconIndex);
    choose(L"CHALLENGE HER STORY");finish();leaveBar();checkpoint("tasha");
    readNews();checkpoint("albiero-lead");
    check(app.storyFlag(0x3B) && app.storyFlag(0x3C) && !app.storyFlag(0x3D) && !app.storyFlag(0x3E),"NewsNet pages polluted scene flags");
    travel(L"ALBIERO");choose(L"FOLLOW HER");choose(L"TRUST TASHA");finish();checkpoint("tasha-reward");
    travel(app.darkWingDestinationPlanet());finish();checkpoint("final-prompt");
    check(app.isStorySceneActive() && app.storyFlag(0x43) && app.storyFlag(0x4E),"final encounter/bounty missing");
    check(app.storyPages_[app.currentStoryPageIndex_].choices.at(0).label==L"ATTACK" &&
          !app.finalMissionStubActive_,"walkthrough entered final battle");scene();
    const auto end=output/"FINAL.GAM";check(app.saveCurrentGame(end),"final Save failed");
    reset();app.loadGameFromFile(end);check(app.tryStartFinalBasePrompt(),"final prompt lost on Restore");
    std::cout<<"mw_campaign_story_smoke: ok checks="<<checks<<" pages="<<page<<" output="<<output.string()<<'\n';
}
int main(int argc,char** argv) {visuals=argc>1 && std::string(argv[1])=="--visual";
    try {runCampaignStorySmoke();return 0;} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
