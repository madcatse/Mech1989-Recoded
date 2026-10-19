#include "../main.cpp"
#include <iostream>

namespace {
using namespace mw::battle;
using namespace mw::presentation;
size_t checks=0;
void check(bool value,const std::string& why) { ++checks;if(!value)throw std::runtime_error(why); }
void checkClose(double a,double b,const std::string& why) { check(std::abs(a-b)<1e-9,why); }
BattleStartParams fixture(const std::string& preset) {
    BattleStartParams p;p.playerMechPresetId=preset;
    p.terrainScenarioPath="Sorted Original Files/DAT/SNARIO.DAT";
    p.terrainScenarioIndex=2;p.terrainEnvironmentId=1;p.fixedTickSeconds=.05;
    p.playerStartTransform={60000,0,30000,0};
    p.deterministicCombatRuntimeEnabled=p.individualWeaponRuntimeEnabled=p.originalProjectileRuntimeEnabled=true;
    p.originalHeatRuntimeEnabled=p.originalMajorSystemRuntimeEnabled=true;
    p.deterministicCollisionRuntimeEnabled=true;
    p.weaponTargetPolicy=BattleWeaponTargetPolicy::CrosshairRayVisibleCombatantProvisional;
    p.mechHitProfiles.push_back(campaignBattleCatalogHitProfile("Sorted Original Files",preset));
    p.cockpitCameraHeight=p.mechHitProfiles.front().aimOriginHeight;
    BattleCombatantLaunchState player;player.mechPresetId=preset;player.startTransform=p.playerStartTransform;
    player.persistentMechState.valid=true;player.persistentMechState.armorPercent=100;
    player.persistentMechState.heatSinksTotal=player.persistentMechState.heatSinksWorking=10;
    player.ammunitionStateValid=true;player.ammunitionByPool.fill(100);p.playerLaunchState=player;
    BattleCombatantLaunchState enemy;enemy.mechPresetId=preset;enemy.roster.team=BattleTeam::Opposing;
    enemy.startTransform={10000,0,10000,0};p.combatantLaunchStates.push_back(enemy);
    return p;
}
void tick(BattleWorld& world,BattleInputCommand c,BattleReplay* replay=nullptr) {
    c.tickIndex=world.tickIndex();c.entityId=world.playerEntityId();
    if(replay)replay->commands.push_back(c);
    world.enqueueInput(c);world.tick();
}
void ppm(const fs::path& path,const std::vector<uint32_t>& pixels,int width,int height) {
    std::ofstream f(path,std::ios::binary);f<<"P6\n"<<width<<' '<<height<<"\n255\n";
    for(auto c:pixels) {const char rgb[]{char(c>>16),char(c>>8),char(c)};f.write(rgb,3);}
    check(bool(f),"write WGL preview");
}
void runtimeChecks() {
    for(const auto& entry:mw::mech3d::mechCatalogEntries()) {
        const auto p=fixture(entry.presetId);auto world=BattleWorld::create(p);BattleReplay replay;
        const auto limits=battleMouseLookLimits(p,entry.presetId);
        BattleInputCommand c;c.mouseLookEnabled=true;c.mouseLookYawDeltaRadians=-.0015;c.mouseLookPitchDeltaRadians=.0015;
        const auto before=world.snapshot();tick(world,c,&replay);auto after=world.snapshot();
        checkClose(after.combatants[0].torsoYawRadians,-.0015,"sub-step horizontal input: "+entry.presetId);
        checkClose(after.camera.mouseLookPitchRadians,.0015,"sub-step vertical input");
        checkClose(after.combatants[0].transform.headingRadians,before.combatants[0].transform.headingRadians,"mouse leaves legs heading unchanged");
        const auto scene=campaignBattleInterpolatedRenderSceneFromSnapshots(before,after,.5,
            CampaignBattlePresentationMode::Cockpit,"Sorted Original Files");
        checkClose(scene.camera.mouseLookPitchRadians,.00075,"continuous pitch presentation interpolation");
        checkClose(scene.camera.transform.headingRadians,-.00075,"continuous yaw presentation interpolation");
        check(scene.combatantVisuals.front().mouseLookEnabled,"fixed crosshair mode reaches renderer");
        check(campaignBattlePresentationPackageFromSnapshot(after,CampaignBattlePresentationMode::Cockpit).aimPitchStep==0,
            "software HUD retains neutral reticle");
        c={};c.mouseLookYawDeltaRadians=100;c.mouseLookPitchDeltaRadians=100;tick(world,c,&replay);after=world.snapshot();
        checkClose(after.combatants[0].torsoYawRadians,limits.yawRadians,"upper yaw stop");
        checkClose(after.camera.mouseLookPitchRadians,limits.pitchUpRadians,"upper pitch stop");
        c.mouseLookYawDeltaRadians=-.001;c.mouseLookPitchDeltaRadians=-.001;tick(world,c,&replay);after=world.snapshot();
        checkClose(after.combatants[0].torsoYawRadians,limits.yawRadians-.001,"immediate reversal without limit windup");
        checkClose(after.camera.mouseLookPitchRadians,limits.pitchUpRadians-.001,"vertical reversal without limit windup");
        c.mouseLookYawDeltaRadians=-100;c.mouseLookPitchDeltaRadians=-100;tick(world,c,&replay);after=world.snapshot();
        checkClose(after.combatants[0].torsoYawRadians,-limits.yawRadians,"lower yaw stop");
        checkClose(after.camera.mouseLookPitchRadians,-limits.pitchDownRadians,"lower pitch stop");
        c={};c.mouseLookEnabled=false;tick(world,c,&replay);after=world.snapshot();
        check(!after.combatants[0].mouseLook.enabled && after.combatants[0].aimPitchStep==-p.maxPlayerAimPitchDownSteps,"classic mode and limits restored");
        checkClose(after.camera.mouseLookPitchRadians,0,"classic view stays level");
        c={};c.torsoYawStepDelta=1;c.aimPitchStepDelta=1;tick(world,c,&replay);
        check(world.snapshot().combatants[0].aimPitchStep==-p.maxPlayerAimPitchDownSteps+1,"classic pitch keys work again");
        const auto restored=parseBattleReplay(serializeBattleReplay(replay));auto repeated=BattleWorld::create(p);
        for(const auto& command:restored.commands) {repeated.enqueueInput(command);repeated.tick();}
        check(battleSnapshotFingerprint(world.snapshot())==battleSnapshotFingerprint(repeated.snapshot()),"continuous aim replay deterministic");

        // A real accepted shot must follow the camera through its stationary
        // original crosshair, including the half-pixel optical offsets.
        for(int zoom:{1,2,3}) {
        world=BattleWorld::create(p);c={};c.mouseLookEnabled=true;c.mouseLookYawDeltaRadians=-.12;c.mouseLookZoomLevel=zoom;
        c.mouseLookPitchDeltaRadians=.08;c.fireWeapon=true;tick(world,c);after=world.snapshot();
        const auto& shot=after.lastShot;const auto& profile=p.mechHitProfiles.front();
        check(shot.valid && after.combatants[0].weaponShotsFired>0,"real mouselook fire accepted: "+entry.presetId);
        const double h=profile.cockpitViewportHeight*.5,t=std::tan(profile.verticalFieldOfViewDegrees*3.141592653589793/360);
        const double sx=(profile.cockpitViewportWidth*.5-profile.neutralCrosshairX)/h*t/zoom;
        const double sy=(h-profile.neutralCrosshairY)/h*t/zoom;
        const double heading=after.camera.transform.headingRadians,pitch=after.camera.mouseLookPitchRadians;
        const double forward=std::cos(pitch)-sy*std::sin(pitch);
        const double x=std::sin(heading)*forward+std::cos(heading)*sx;
        const double y=std::sin(pitch)+sy*std::cos(pitch);
        const double z=std::cos(heading)*forward-std::sin(heading)*sx;
        const double length=std::sqrt(x*x+y*y+z*z);
        checkClose(shot.aimDirection.x,x/length,"shot/camera horizontal alignment");
        checkClose(shot.aimDirection.y,y/length,"shot/camera vertical alignment");
        checkClose(shot.aimDirection.z,z/length,"shot/camera depth alignment");
        }
    }
    auto p=fixture("jenner");auto world=BattleWorld::create(p);
    BattleInputCommand c;c.mouseLookEnabled=true;tick(world,c);
    c={};c.mouseLookYawDeltaRadians=std::numeric_limits<double>::quiet_NaN();c.mouseLookPitchDeltaRadians=1;tick(world,c);
    checkClose(world.snapshot().camera.mouseLookPitchRadians,0,"invalid input cannot poison view");
    c={};c.mouseLookEnabled=false;tick(world,c);
    const auto a=world.snapshot();c={};c.mouseLookYawDeltaRadians=1;c.mouseLookPitchDeltaRadians=1;tick(world,c);
    checkClose(world.snapshot().combatants[0].torsoYawRadians,a.combatants[0].torsoYawRadians,"disabled ignores mouse deltas");
    p.playerLaunchState->persistentMechState.engine=3;
    world=BattleWorld::create(p);c={};c.mouseLookEnabled=true;c.mouseLookYawDeltaRadians=.3;c.mouseLookPitchDeltaRadians=.2;
    tick(world,c);
    check(world.snapshot().combatants[0].majorSystems.engineShutdown,"actual shutdown fixture");
    checkClose(world.snapshot().camera.mouseLookPitchRadians,0,"shutdown rejects vertical aiming");
    checkClose(world.snapshot().combatants[0].torsoYawRadians,0,"shutdown rejects torso rotation");
}
}
void runMouseLookAppChecks(App& app) {
    app.loadResources();
    fs::create_directories("outputs/mouselook_controls");
    app.settingsPath_="outputs/mouselook_controls/test-settings.ini";
    check(saveDisplaySettings(app.settingsPath_,{3,false,false}),"isolated preferences start with classic controls");
    app.loadPreferences();
    check(!app.mouseLookEnabled_,"old/default preference starts off");
    app.changeState(ScreenState::SystemMenu);
    check(app.kSystemMenuItems[app.kSystemMouseLookMenuIndex-1].action==App::MenuAction::Fullscreen &&
        app.kSystemMenuItems[app.kSystemMouseLookMenuIndex].action==App::MenuAction::MouseLook,
        "Mouselook row immediately follows display options");
    app.handleSystemMenuClick(160,130);
    check(app.mouseLookEnabled_ && app.systemMenuIndex_==app.kSystemMouseLookMenuIndex &&
        loadDisplaySettings(app.settingsPath_).mouseLook,"menu click enables and saves mouselook");
    app.render();ppm("outputs/mouselook_controls/menu.ppm",app.framebuffer_,320,200);
    app.mouseLookEnabled_=false;app.loadPreferences();
    check(app.mouseLookEnabled_ && app.displaySettings_.resolution==3 && !app.displaySettings_.fullscreen,
        "startup preference loading restores mouselook without changing the picture settings");
    app.handleKey(VK_LEFT);check(!app.mouseLookEnabled_,"menu arrow disables mode");
    app.handleKey(VK_RIGHT);check(app.mouseLookEnabled_,"menu arrow enables mode");
    app.handleKey(VK_RETURN);check(!app.mouseLookEnabled_,"Enter toggles the same preference");
    app.systemMenuIndex_=app.kSystemContinueMenuIndex;app.handleKey(VK_DOWN);
    check(app.systemMenuIndex_==app.kSystemSaveMenuIndex,"menu wraps past the added row");
    app.handleKey(VK_UP);check(app.systemMenuIndex_==app.kSystemContinueMenuIndex,"menu wraps back to Continue");
    app.handleKey(VK_RETURN);check(app.state_==ScreenState::MainMenu,"Continue still returns to the planet");
    app.campaignBattleRuntime_={};auto& rt=app.campaignBattleRuntime_;
    rt.world=BattleWorld::create(fixture("jenner"));rt.active=true;
    rt.currentPresentationSnapshot=rt.previousPresentationSnapshot=rt.world->snapshot();rt.presentationSnapshotsValid=true;
    rt.presentationMode=CampaignBattlePresentationMode::Cockpit;
    app.changeState(ScreenState::MissionBattleStub);
    const auto persistent=app.captureCampaignPersistentState().fingerprint;
    app.debugConsoleInput_=L" MouseLook ";app.executeDebugConsoleCommand();
    check(app.mouseLookEnabled_ && app.mouseLookContextActive() && loadDisplaySettings(app.settingsPath_).mouseLook,
        "actual case-insensitive console toggle shares the saved menu setting");
    // Feed the same RAWMOUSE handler/queue without acquiring the user's cursor.
    app.mouseLookCaptured_=true;
    RAWMOUSE mouse{};mouse.lLastX=1;mouse.lLastY=-1;mouse.usButtonFlags=RI_MOUSE_LEFT_BUTTON_DOWN;
    app.handleMouseLookRawInput(mouse);app.enqueueCampaignBattleRuntimeInput(*rt.world);app.mouseLookCaptured_=false;
    rt.world->tick();rt.currentPresentationSnapshot=rt.world->snapshot();
    checkClose(rt.currentPresentationSnapshot.combatants[0].torsoYawRadians,-.0015,"right mouse turns right");
    checkClose(rt.currentPresentationSnapshot.camera.mouseLookPitchRadians,.0015,"up mouse looks up without inversion");
    check(rt.currentPresentationSnapshot.combatants[0].weaponShotsFired>0,"left mouse triggers real weapon");
    const auto firstShotCount=rt.currentPresentationSnapshot.combatants[0].weaponShotsFired;
    check(firstShotCount==1,"one press fires one weapon");
    app.mouseLookCaptured_=true;
    mouse={};mouse.usButtonFlags=RI_MOUSE_LEFT_BUTTON_DOWN;
    for(int i=0;i<200;++i) {
        // Include duplicate down packets; neither these nor the held state may
        // repeat fire when weapons finish their cooldown during the next 10s.
        app.handleMouseLookRawInput(mouse);app.enqueueCampaignBattleRuntimeInput(*rt.world);
        check(!rt.inputReplay.commands.back().fireWeapon,"held/duplicate left down does not request another shot");
        rt.world->tick();rt.currentPresentationSnapshot=rt.world->snapshot();
    }
    check(rt.currentPresentationSnapshot.combatants[0].weaponShotsFired==firstShotCount,"holding left through cooldown fires exactly once");
    mouse.usButtonFlags=RI_MOUSE_LEFT_BUTTON_UP;app.handleMouseLookRawInput(mouse);
    mouse.usButtonFlags=RI_MOUSE_LEFT_BUTTON_DOWN;app.handleMouseLookRawInput(mouse);
    // A complete quick click between fixed ticks must not disappear on button-up.
    mouse.usButtonFlags=RI_MOUSE_LEFT_BUTTON_UP;app.handleMouseLookRawInput(mouse);
    app.enqueueCampaignBattleRuntimeInput(*rt.world);
    check(rt.inputReplay.commands.back().fireWeapon && !app.mouseLookLeftHeld_,"quick up/down/up click survives until one tick");
    rt.world->tick();rt.currentPresentationSnapshot=rt.world->snapshot();
    check(rt.currentPresentationSnapshot.combatants[0].weaponShotsFired==firstShotCount+1,"release and new press produce exactly one more shot");
    app.enqueueCampaignBattleRuntimeInput(*rt.world);
    check(!rt.inputReplay.commands.back().fireWeapon,"quick click cannot replay on the next tick");
    rt.world->tick();rt.currentPresentationSnapshot=rt.world->snapshot();
    mouse.usButtonFlags=RI_MOUSE_LEFT_BUTTON_DOWN|RI_MOUSE_RIGHT_BUTTON_DOWN;app.handleMouseLookRawInput(mouse);
    app.enqueueCampaignBattleRuntimeInput(*rt.world);
    check(!rt.inputReplay.commands.back().fireWeapon && rt.inputReplay.commands.back().fireLinkedWeapons,"right salvo retains priority over simultaneous left click");
    rt.world->tick();rt.currentPresentationSnapshot=rt.world->snapshot();
    mouse.usButtonFlags=RI_MOUSE_RIGHT_BUTTON_UP;app.handleMouseLookRawInput(mouse);
    app.enqueueCampaignBattleRuntimeInput(*rt.world);
    check(!rt.inputReplay.commands.back().fireWeapon,"right release does not resurrect a suppressed left click");
    rt.world->tick();rt.currentPresentationSnapshot=rt.world->snapshot();
    app.mouseLookCaptured_=false;
    app.clearMouseLookInput();app.mouseLookCaptured_=true;
    mouse.usButtonFlags=0;mouse.lLastX=12;mouse.lLastY=-6;app.handleMouseLookRawInput(mouse);
    const double yaw=app.mouseLookPendingYaw_,pitch=app.mouseLookPendingPitch_;
    app.clearMouseLookInput();mouse.lLastX=1;mouse.lLastY=0;
    for(int i=0;i<12;++i)app.handleMouseLookRawInput(mouse);
    mouse.lLastX=0;mouse.lLastY=-1;for(int i=0;i<6;++i)app.handleMouseLookRawInput(mouse);
    checkClose(app.mouseLookPendingYaw_,yaw,"raw packet grouping independent X");checkClose(app.mouseLookPendingPitch_,pitch,"raw packet grouping independent Y");
    app.clearMouseLookInput();rt.cockpitZoomLevel=3;mouse.lLastX=3;mouse.lLastY=-3;app.handleMouseLookRawInput(mouse);
    checkClose(app.mouseLookPendingYaw_,-.0015,"zoom precision scales mouse input");rt.cockpitZoomLevel=1;
    app.mouseLookCaptured_=false;app.clearMouseLookInput();
    app.mouseLookCaptured_=true;mouse={};mouse.usButtonFlags=RI_MOUSE_RIGHT_BUTTON_DOWN;
    app.handleMouseLookRawInput(mouse);app.enqueueCampaignBattleRuntimeInput(*rt.world);app.mouseLookCaptured_=false;
    check(rt.inputReplay.commands.back().fireLinkedWeapons,"right mouse requests linked salvo");
    rt.world->tick();rt.currentPresentationSnapshot=rt.world->snapshot();
    app.clearMouseLookInput();
    for(auto mode:{CampaignBattlePresentationMode::CockpitCommandMap,CampaignBattlePresentationMode::MissionStatus,
                  CampaignBattlePresentationMode::TacticalMap,CampaignBattlePresentationMode::External}) {
        rt.presentationMode=mode;check(!app.mouseLookContextActive(),"auxiliary screens release mouse");
    }
    rt.presentationMode=CampaignBattlePresentationMode::Cockpit;
    app.debugConsoleOpen_=true;check(!app.mouseLookContextActive(),"console releases mouse");app.debugConsoleOpen_=false;
    rt.retreatPromptVisible=true;check(!app.mouseLookContextActive(),"retreat prompt releases mouse");rt.retreatPromptVisible=false;
    rt.currentPresentationSnapshot.combatants[0].heat.reactorShutdown=true;
    check(!app.mouseLookContextActive(),"shutdown disallows mouse control");rt.currentPresentationSnapshot=rt.world->snapshot();
    rt.currentPresentationSnapshot.combatants[0].posture.posture=BattlePosture::Prone;
    check(!app.mouseLookContextActive(),"prone posture disallows mouse control");rt.currentPresentationSnapshot=rt.world->snapshot();
    rt.currentPresentationSnapshot.conclusionDelay.active=rt.currentPresentationSnapshot.conclusionDelay.freezesSimulation=true;
    check(!app.mouseLookContextActive(),"frozen fatal conclusion releases mouse");rt.currentPresentationSnapshot=rt.world->snapshot();
    app.mouseLookPendingPitch_=1;app.mouseLookLeftHeld_=app.mouseLookFirePending_=true;app.handleMessage(nullptr,WM_ACTIVATEAPP,FALSE,0);
    check(app.mouseLookPendingPitch_==0 && !app.mouseLookLeftHeld_ && !app.mouseLookFirePending_,
        "focus loss clears residual motion, held state and queued click");
    app.debugConsoleInput_=L"mouselook";app.executeDebugConsoleCommand();app.enqueueCampaignBattleRuntimeInput(*rt.world);
    rt.world->tick();rt.currentPresentationSnapshot=rt.world->snapshot();
    check(!app.mouseLookEnabled_ && !rt.currentPresentationSnapshot.combatants[0].mouseLook.enabled &&
        !loadDisplaySettings(app.settingsPath_).mouseLook,"second command restores and saves classic controls");
    check(app.captureCampaignPersistentState().fingerprint==persistent,"mode and mouse do not alter campaign/save preferences");
}
namespace {
void renderChecks() {
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"MWMouseLookSmoke";
    check(RegisterClassW(&wc)!=0,"register hidden test window");
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"Mouselook verification",WS_OVERLAPPEDWINDOW,0,0,800,600,nullptr,nullptr,wc.hInstance,nullptr);
    check(window!=nullptr,"create hidden test window");
    RAWINPUTDEVICE device{0x01,0x02,0,window};
    check(RegisterRawInputDevices(&device,1,sizeof(device))!=FALSE,"register native raw mouse target");
    UINT count=0;GetRegisteredRawInputDevices(nullptr,&count,sizeof(device));
    std::vector<RAWINPUTDEVICE> devices(count);
    check(GetRegisteredRawInputDevices(devices.data(),&count,sizeof(device))!=UINT(-1) &&
        std::any_of(devices.begin(),devices.end(),[&](const auto& d){return d.usUsage==2 && d.hwndTarget==window && !(d.dwFlags&RIDEV_INPUTSINK);}),
        "native registration is foreground-only");
    device={0x01,0x02,RIDEV_REMOVE,nullptr};
    check(RegisterRawInputDevices(&device,1,sizeof(device))!=FALSE,"release native raw mouse registration");
    CampaignBattleGlViewport viewport;check(viewport.initialize(wc.hInstance,window),"initialize real WGL");
    for(const std::string preset:{"jenner","shadow_hawk","battlemaster"}) {
        const auto p=fixture(preset);
        for(int pose=-1;pose<=1;++pose) {
            auto world=BattleWorld::create(p);BattleInputCommand c;c.mouseLookEnabled=true;
            c.mouseLookPitchDeltaRadians=pose*.1;c.mouseLookYawDeltaRadians=pose*.15;tick(world,c);
            const auto scene=campaignBattleRenderSceneFromSnapshot(world.snapshot(),CampaignBattlePresentationMode::Cockpit,"Sorted Original Files");
            check(viewport.updateScene(scene),"upload mouselook scene");
            const auto rect=campaignBattleGlVirtualViewportRect(scene.mode,scene.cockpit.family);
            viewport.setBounds({0,0,rect.width*3,rect.height*3});const auto pixels=viewport.captureScenePixels();
            check(pixels.size()==size_t(rect.width*rect.height*9),"capture WGL mouselook image");
            ppm(fs::path("outputs/mouselook")/(preset+"-"+std::to_string(pose)+".ppm"),pixels,rect.width*3,rect.height*3);
        }
    }
    viewport.shutdown();DestroyWindow(window);
}
}
int main() {try {
    fs::create_directories("outputs/mouselook");runtimeChecks();
    App app(findResourceRoot());runMouseLookAppChecks(app);renderChecks();
    std::cout<<"mw_mouselook_smoke: ok "<<checks<<" checks;8 mechs;9 WGL frames\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
