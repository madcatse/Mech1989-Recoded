// Real campaign launch handlers, original resource bytes, no save/window needed.
#include "../main.cpp"
#include <iostream>

namespace {
using namespace mw::battle;
size_t checks = 0;
void check(bool ok, const std::string& message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
uint16_t word(const std::vector<uint8_t>& b, size_t o) {
    return static_cast<uint16_t>(b.at(o) | (b.at(o+1) << 8));
}
int32_t dword(const std::vector<uint8_t>& b, size_t o) {
    return static_cast<int32_t>(static_cast<uint32_t>(word(b,o)) |
        (static_cast<uint32_t>(word(b,o+2)) << 16));
}
double distance(const Transform& a, const Transform& b) {
    return std::hypot(a.x-b.x,a.z-b.z);
}
Transform sourcePosition(const std::vector<uint8_t>& b, size_t offset) {
    return {(dword(b,offset)+44160.0)*500.0/512.0,0,
        (24000.0-dword(b,offset+4))*500.0/512.0,0};
}
void samePosition(const Transform& actual, const Transform& expected, const std::string& context) {
    check(distance(actual,expected)<1e-8,context+" position differs from SNARIO bytes");
}
void capture(const BattleSnapshot& s, size_t scenario) {
    using namespace mw::presentation;
    const auto scene = campaignBattleRenderSceneFromSnapshot(s,CampaignBattlePresentationMode::CockpitCommandMap,"Sorted Original Files");
    const auto backdrop=loadCampaignCockpitBackdrop(scene.cockpit);
    const auto layers=loadCampaignCockpitDynamicLayers(scene.cockpit);
    check(backdrop.valid && layers.valid,"original MAP.SCR/font unavailable");
    auto pixels=backdrop.bgraPixels;
    CampaignCommandScreenState ui;
    ui.selectedCompanion=s.combatants.at(1).id;
    composeCampaignCommandPanels(pixels,layers.font,s,ui,{{"player:1","DEFENDER 1"},{"player:2","DEFENDER 2"},{"player:3","DEFENDER 3"}});
    HWND parent=CreateWindowExW(0,L"STATIC",L"Placement render probe",WS_OVERLAPPEDWINDOW,0,0,320,200,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    CampaignBattleGlViewport viewport;
    check(parent && viewport.initialize(GetModuleHandleW(nullptr),parent),"placement WGL initialize failed");
    const auto rect=campaignCockpitCommandMapRect();
    viewport.setBounds(RECT{0,0,rect.width,rect.height});
    check(viewport.updateScene(scene),"placement WGL scene failed");
    CampaignBattleGlCockpitOptions options;
    options.commandOverlay=campaignCommandMapOverlay(s,ui);
    // Audit-only view reveals all initial slots, independently of radar discovery.
    for(const auto& actor:s.combatants) options.knownCommandMapEnemies.push_back(actor.id);
    viewport.setCockpitOptions(options);
    const auto map=viewport.captureCommandMapPixels();
    check(map.size()==static_cast<size_t>(rect.width*rect.height),"placement map readback failed");
    for(size_t i=0;i<map.size();++i) pixels[(rect.y+i/rect.width)*320+rect.x+i%rect.width]=map[i];
    fs::create_directories("outputs/placement_audit");
    std::ofstream out("outputs/placement_audit/landing_"+std::to_string(scenario)+".ppm",std::ios::binary);
    out<<"P6\n320 200\n255\n";
    for(auto c:pixels){const char rgb[]={static_cast<char>(c>>16),static_cast<char>(c>>8),static_cast<char>(c)};out.write(rgb,3);}
    viewport.shutdown(); DestroyWindow(parent);
}
}

void runBattleStartPlacementSmoke(bool visual) {
    App app(findResourceRoot()); app.loadResources();
    const auto snario=readFile(app.battleScenarioPath());
    const auto btech=readFile(app.resourceRoot_/L"BTECH.EXE");
    const auto catalog=decodeOriginalContractTables(app.mwMainData_);
    check(btech.size()==174267u,"unexpected BTECH build for verified packed table span");
    // Raw instructions independently distinguish shared player-mode bank lookup
    // from the side-specific coordinate-mode lookup (packed file 1D78..).
    check(word(btech,0x1d88)==0x1e8b && word(btech,0x1d8a)==0x0ed0 &&
        word(btech,0x1daf)==0x878b && word(btech,0x1db1)==0x0ed0,
        "original 1a22 indexed-load evidence changed");
    for(size_t selector=0;selector<36;++selector) {
        const auto m=originalBattleMissionPlacementModes(selector);
        const int pm=word(btech,0x2737e+selector*2);
        check(m.playerMode==pm && m.opposingMode==word(btech,0x2732a+pm*2),"mode table differs from EXE");
    }
    check(word(btech,0x2737e+36*2)==0xffff,"selector table sentinel changed");
    bool rejected=false;
    try { originalBattleMissionPlacementModes(36); } catch(const std::exception&) { rejected=true; }
    check(rejected,"selector sentinel accepted as mode");
    app.ownedMechs_.clear(); app.missionParticipants_.clear();
    for(int i=0;i<4;++i) {
        app.ownedMechs_.push_back(App::makeMech(App::ChassisId::Battlemaster,i));
        App::MissionParticipant p; p.crewSlot=i; p.mechIndex=i;
        app.missionParticipants_.push_back(p);
    }
    app.acceptedBattleContract_.valid=true;
    app.acceptedBattleContract_.estimatedHeavyMechs=12;
    app.acceptedBattleContract_.oppositionSpawnPlan=decodeFreshBtechOppositionSpawnPlan({{12,0,0}},"placement_audit");
    size_t launches=0,defenseOpenings=0,sequences=0;
    for(size_t scenario=0;scenario<20;++scenario) {
        app.acceptedBattleContract_.terrainScenarioIndex=scenario;
        for(const auto& definition:battleMissionDefinitions()) {
            const std::wstring missionName=widen(definition.title);
            app.acceptedContract_.missionName=missionName;
            app.beginAcceptedContractMissionSequence();
            const size_t stages=app.missionSequence_.active()?app.missionSequence_.stageCount:1u;
            if(definition.extended) ++sequences;
            for(size_t stage=0;stage<stages;++stage) {
                app.missionSequence_.stageIndex=stage;
                auto p=app.battleStartParamsForAcceptedContract();
                const auto row=app.missionSequence_.originalRow;
                const auto expectedScenario=(scenario+((!definition.extended || row==4 || row==5)?0:stage))%20u;
                check(p.terrainScenarioIndex==expectedScenario,"original sequence map progression");
                const auto id=p.mission.originalId;
                if(!definition.extended) check(id==definition.originalId,"campaign title selected wrong mission expected="+std::to_string(definition.originalId)+" actual="+std::to_string(id)+" title="+narrowAscii(app.acceptedContract_.missionName));
                const size_t selector=static_cast<size_t>(catalog.missions.at(id).id)-1u;
                check(originalBattlePlacementSelectorForMission(id)==selector,"catalog/descriptor mapping differs from MW_MAIN");
                check(battleMissionIdForOriginalPlacementSelector(selector)==std::optional<size_t>{id},"selector inverse mapping");
                const int pm=word(btech,0x2737e+selector*2),om=word(btech,0x2732a+pm*2);
                const auto& setup=p.setupMetadata;
                check(setup.valid && setup.missionSelector==selector && setup.handoffMissionId==selector+1 &&
                    setup.briefingMissionId==id && setup.playerMode==pm && setup.opposingMode==om,"campaign selected wrong placement");
                check(p.playerAlliedLaunchStates.size()==3 && p.combatantLaunchStates.size()==4,"campaign lost lance slots");
                p.combatAiPolicy=BattleCombatAiPolicy::ReplacementDeterministicCombatAi;
                app.configureCampaignBattleCollisionRuntime(p,"Sorted Original Files");
                auto world=BattleWorld::create(p);
                const auto start=world.missionStartSnapshot();
                check(start.combatants.size()==8,"launch snapshot roster size");
                std::array<Transform,8> expected;
                for(size_t side=0;side<2;++side) {
                    const int mode=side?om:pm;
                    const auto bank=snario.at(expectedScenario*16+snario.at(expectedScenario*16+4+side*5+pm));
                    check((side?setup.opposingBankId:setup.playerBankId)==bank,"campaign used side mode to select bank");
                    for(size_t slot=0;slot<4;++slot) {
                        expected[side*4+slot]=sourcePosition(snario,0x3c0+bank*160+mode*32+slot*8);
                        samePosition(start.combatants.at(side*4+slot).transform,expected[side*4+slot],"mission "+std::to_string(id));
                        check(std::abs(start.combatants.at(side*4+slot).transform.headingRadians-
                            start.combatants.at(side*4).transform.headingRadians)<1e-8,"side members lack shared initial facing");
                    }
                    if(mode==4) {
                        samePosition(start.objective.transform,sourcePosition(snario,0xdc0+bank*8),"objective");
                        check(start.objective.activeObjectProven && setup.dedicatedObjective.valid,"lost dedicated objective");
                    }
                }
                for(size_t i=0;i<8;++i) for(size_t j=0;j<i;++j)
                    check(distance(expected[i],expected[j])>start.combatants[i].collisionRadiusWorld+start.combatants[j].collisionRadiusWorld,
                        "initial mechs overlap");
                auto stationary=p; stationary.combatAiPolicy=BattleCombatAiPolicy::Disabled;
                auto clearance=BattleWorld::create(stationary); clearance.tick();
                const auto clearSnapshot=clearance.snapshot();
                for(size_t i=0;i<8;++i) check(clearSnapshot.combatants[i].collisionCount==0,
                    "initial terrain/object overlap scenario="+std::to_string(scenario)+" mission="+std::to_string(id)+" slot="+std::to_string(i));
                if(p.mission.family==BattleMissionFamily::Defense) {
                    check(pm==4 && om==3 && start.objective.sourceSlot=="dedicated:player","Defense object source");
                    for(size_t i=4;i<8;++i) check(distance(expected[0],start.objective.transform)<distance(expected[i],start.objective.transform),
                        "attacker starts closer than player to protected object");
                    world.runTicks(id==7?200:20);
                    check(world.snapshot().objective.damage==0 && world.snapshot().missionRuntimeState==BattleMissionRuntimeState::InProgress,
                        "Defense objective damaged immediately at original start");
                    ++defenseOpenings;
                    if(id==7 && (scenario==2 || scenario==18)) {
                        if(visual) capture(start,scenario);
                        auto repeated=BattleWorld::create(p);
                        for(size_t tick=0;tick<200;++tick) {
                            const auto frame=repeated.snapshot();
                            for(size_t render=0;render<tick%4;++render) {
                                mw::presentation::CampaignCommandScreenState ui;
                                mw::presentation::campaignCommandMapOverlay(frame,ui);
                            }
                            repeated.tick();
                        }
                        check(battleSnapshotFingerprint(world.snapshot())==battleSnapshotFingerprint(repeated.snapshot()),"opening replay/render cadence differs");
                    }
                }
                ++launches;
            }
        }
    }
    check(!battleMissionIdForOriginalPlacementSelector(27) && !battleMissionIdForOriginalPlacementSelector(28),"unused selectors gained catalog titles");
    std::cout<<"mw_battle_start_placement_smoke: ok checks="<<checks<<" maps=20 catalog=34 launches="<<launches
        <<" extended_sequences="<<sequences<<" defense_openings="<<defenseOpenings<<"\n";
}

int main(int argc,char** argv) {
    try { runBattleStartPlacementSmoke(argc>1 && std::string(argv[1])=="--visual"); return 0; }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
