// Exercise the actual campaign keyboard/panel along with authoritative firing.
#include "../main.cpp"
#include <iostream>

namespace {
using namespace mw::battle;
using namespace mw::presentation;
size_t checks = 0;
void check(bool ok, const std::string& message) {
    ++checks; if (!ok) throw std::runtime_error(message);
}
BattleStartParams fixture(const std::string& preset = "locust") {
    BattleStartParams p;
    p.playerMechPresetId = preset;
    p.terrainScenarioPath = "Sorted Original Files/DAT/SNARIO.DAT";
    p.terrainScenarioIndex = 2; p.terrainEnvironmentId = 1;
    p.playerStartTransform = {60000,0,30000,0};
    p.fixedTickSeconds = .05;
    p.deterministicCombatRuntimeEnabled = true;
    p.individualWeaponRuntimeEnabled = true;
    p.originalProjectileRuntimeEnabled = true;
    p.originalHeatRuntimeEnabled = true;
    p.originalMajorSystemRuntimeEnabled = true;
    p.weaponTargetPolicy = BattleWeaponTargetPolicy::CrosshairRayVisibleCombatantProvisional;
    BattleCombatantLaunchState player;
    player.mechPresetId = preset; player.startTransform = p.playerStartTransform;
    player.persistentMechState.valid = true;
    player.persistentMechState.armorPercent = 100;
    player.persistentMechState.heatSinksTotal = 10;
    player.persistentMechState.heatSinksWorking = 10;
    player.ammunitionStateValid = true; player.ammunitionByPool.fill(100);
    p.playerLaunchState = player;
    auto other = player; other.roster.team = BattleTeam::Opposing;
    other.startTransform = {10000,0,10000,0};
    p.combatantLaunchStates = {other};
    return p;
}
CombatantSnapshot player(const BattleWorld& world) { return world.snapshot().combatants.front(); }
void input(BattleWorld& w, BattleInputCommand c) {
    c.entityId = w.playerEntityId(); c.tickIndex = w.tickIndex(); w.enqueueInput(c);
}
void key(BattleWorld& w, unsigned int k) {
    const auto c = campaignCockpitWeaponKey(k,false); check(c.has_value(),"key mapping"); input(w,*c);
}
void runtimeChecks() {
    auto p = fixture(); auto w = BattleWorld::create(p);
    const auto initial = w.snapshot();
    check(player(w).automaticWeaponsEnabled,"original default AWS must be on");
    for (const auto& gun : player(w).weapons) check(gun.awsLinked,"functional initial slot not linked");
    check(!initial.combatants.back().automaticWeaponsEnabled,"AWS applied to opponent");
    auto command = *campaignCockpitWeaponKey('W',false);
    command.tickIndex = 3; command.entityId = w.playerEntityId(); w.enqueueInput(command);
    w.runTicks(3); check(player(w).automaticWeaponsEnabled,"AWS applied before command tick");
    w.tick(); check(!player(w).automaticWeaponsEnabled,"AWS did not apply on tick 3");
    key(w,VK_SPACE); w.tick();
    check(player(w).weaponShotsFired==1 && player(w).selectedWeaponInstanceId==1,"AWS off primary selection");
    key(w,VK_TAB); w.tick(); check(player(w).weaponShotsFired==1,"disabled AWS fired group");

    w = BattleWorld::create(p);
    key(w,'2'); key(w,'O'); key(w,'1'); key(w,VK_SPACE); w.tick();
    check(!player(w).weapons[1].awsLinked && player(w).selectedWeaponInstanceId==3,"next ready linked slot skipped incorrectly");
    key(w,'2'); key(w,VK_SPACE); w.tick();
    check(player(w).weapons[1].shotsFired==1 && player(w).selectedWeaponInstanceId==3,"manual unlinked override");
    key(w,VK_SPACE); w.tick();
    check(player(w).selectedWeaponInstanceId==3,"no ready linked candidate must retain selection");
    w.runTicks(60); check(player(w).weaponShotsFired==3,"AWS fired without trigger");
    check(player(w).selectedWeaponInstanceId==3,"ready selection unexpectedly changed");
    key(w,'W'); w.tick();
    check(!player(w).automaticWeaponsEnabled && player(w).weapons[1].awsLinked,"W did not reset membership");
    key(w,'W'); w.tick();
    key(w,'3'); key(w,VK_SPACE); w.tick();
    check(player(w).selectedWeaponInstanceId==1,"AWS did not wrap slot order");
    for (unsigned int k : {'1','2','3'}) { key(w,k); key(w,'O'); w.tick(); }
    const auto shots = player(w).weaponShotsFired;
    key(w,VK_TAB); w.tick(); check(player(w).weaponShotsFired==shots,"empty group fired");
    key(w,'1'); key(w,VK_SPACE); w.tick();
    check(player(w).weaponShotsFired==shots+1 && player(w).selectedWeaponInstanceId==1,"empty group broke manual fire");

    for (const auto& entry : mw::mech3d::mechCatalogEntries()) {
        auto all = BattleWorld::create(fixture(entry.presetId));
        const auto before = player(all);
        int heat = 0; auto ammo = before.ammunitionByPool;
        for (const auto& gun : before.weapons) {
            heat += gun.originalHeat;
            if (gun.ammunitionPoolIndex>=0) --ammo[gun.ammunitionPoolIndex];
        }
        key(all,VK_SPACE); key(all,VK_TAB); all.tick();
        const auto after = player(all);
        check(after.weaponShotsFired==before.weapons.size(),"salvo count: "+entry.presetId);
        check(after.ammunitionByPool==ammo,"salvo shared ammunition: "+entry.presetId);
        check(after.heat.lastWeaponHeatAdded==heat,"salvo heat sum: "+entry.presetId);
        for (const auto& gun : after.weapons)
            check(gun.shotsFired==1 && gun.cooldownTicksRemaining==gun.cooldownTicksOnFire,"per-slot salvo cooldown");
        key(all,VK_TAB); all.tick();
        check(player(all).weaponShotsFired==after.weaponShotsFired,"cooling weapon fired again");
        uint32_t lastSlot = 0;
        for (const auto& shot : all.snapshot().shotEvents) {
            check(shot.tickIndex==0 && shot.weaponInstanceId>lastSlot,"salvo order/tick"); lastSlot=shot.weaponInstanceId;
        }
    }
    p.playerLaunchState->ammunitionByPool[5] = 1;
    w=BattleWorld::create(p); key(w,VK_TAB); w.tick();
    check(player(w).weaponShotsFired==2 && player(w).weapons[1].shotsFired==1 &&
        player(w).weapons[2].shotsFired==0 && player(w).ammunitionByPool[5]==0,"shared last round overspent/tie wrong");
    p.playerLaunchState->persistentMechState.weaponConditions[0]=3;
    p.playerLaunchState->ammunitionByPool[5]=0;
    w=BattleWorld::create(p); key(w,VK_TAB); key(w,VK_SPACE); w.tick();
    check(player(w).weaponShotsFired==0 && !player(w).weapons[0].awsLinked,"damaged/empty gate");
    for (auto status : {CombatantMissionStatus::Destroyed,CombatantMissionStatus::Disabled,CombatantMissionStatus::Escaped}) {
        w=BattleWorld::create(fixture()); w.setMissionStatus(w.playerEntityId(),status);
        key(w,VK_TAB); key(w,VK_SPACE); w.tick(); check(player(w).weaponShotsFired==0,"inactive mech fired");
    }
    auto hot=fixture("warhammer"); hot.playerLaunchState->persistentMechState.heatSinksWorking=0;
    w=BattleWorld::create(hot); bool shutdown=false;
    for (int tick=0;tick<600 && !shutdown;++tick) { key(w,VK_TAB); w.tick(); shutdown=player(w).heat.reactorShutdown; }
    check(shutdown,"salvo failed to reach original heat shutdown");
    const auto count=player(w).weaponShotsFired;
    key(w,VK_TAB); key(w,VK_SPACE); w.tick(); check(player(w).weaponShotsFired==count,"shutdown salvo bypass");

    w=BattleWorld::create(fixture()); command={}; command.entityId={2};
    command.toggleAutomaticWeapons=true; command.toggleSelectedWeaponLink=true; command.fireLinkedWeapons=true;
    w.enqueueInput(command); w.tick();
    const auto enemy=w.snapshot().combatants.back();
    check(!enemy.automaticWeaponsEnabled && enemy.weaponShotsFired==0,"enemy received AWS controls");
    auto changed=initial; changed.combatants.front().automaticWeaponsEnabled=false;
    check(battleSnapshotFingerprint(initial)!=battleSnapshotFingerprint(changed),"AWS missing from fingerprint");
    changed=initial; changed.combatants.front().weapons.front().awsLinked=false;
    check(battleSnapshotFingerprint(initial)!=battleSnapshotFingerprint(changed),"membership missing from fingerprint");
}
void replayChecks() {
    BattleReplay r;
    const unsigned int keys[]={'W','W','2','O','1',VK_SPACE,VK_TAB,'3',VK_SPACE,'O',VK_ADD,VK_SUBTRACT,'W'};
    for (size_t i=0;i<std::size(keys);++i) {
        auto c=*campaignCockpitWeaponKey(keys[i],false); c.tickIndex=i*5; r.commands.push_back(c);
        check(!campaignCockpitWeaponKey(keys[i],true),"Windows repeat not suppressed");
    }
    auto p=fixture(); const auto serialized=serializeBattleReplay(r); auto restored=parseBattleReplay(serialized);
    check(serializeBattleReplay(restored)==serialized,"replay reconstruction changed commands");
    const auto expected=runBattleReplay(p,r,100);
    check(battleSnapshotFingerprint(expected)==battleSnapshotFingerprint(runBattleReplay(p,restored,100)),"AWS replay mismatch");
    for (int chunk : {1,2,5,10}) {
        auto w=BattleWorld::create(p);
        for (auto c : restored.commands) { c.entityId=w.playerEntityId(); w.enqueueInput(c); }
        for (int step=0;step<100/chunk;++step) {
            w.runTicks(chunk);
            for (int frame=0;frame<step%4;++frame) {
                const auto s=w.snapshot(); const auto before=battleSnapshotFingerprint(s);
                const auto scene=campaignBattleRenderSceneFromSnapshot(s,CampaignBattlePresentationMode::Cockpit,"Sorted Original Files");
                check(scene.cockpitAutomaticWeaponsEnabled==s.combatants.front().automaticWeaponsEnabled,"scene state differs");
                check(battleSnapshotFingerprint(s)==before,"presentation mutated AWS");
            }
        }
        check(battleSnapshotFingerprint(w.snapshot())==battleSnapshotFingerprint(expected),"subdivision/render cadence mismatch");
    }
    const auto old=parseBattleReplay("MW_BATTLE_REPLAY 1\n0 0 0 0 0 0 0 0 0 1 0 1 0 0 0 0\n");
    check(old.commands.size()==1 && old.commands[0].fireWeapon && !old.commands[0].fireLinkedWeapons,"v1 replay lost compatibility");
    bool invalid=false; try { parseBattleReplay("MW_BATTLE_REPLAY 2\n0 0 0 0 0 0 0 0 0 1 0 1 0 0 0 0 2 0 0\n"); }
    catch (const std::exception&) { invalid=true; } check(invalid,"invalid AWS bool accepted");
}
void savePpm(const std::string& name,const std::vector<uint32_t>& pixels) {
    fs::create_directories("outputs/aws_audit");
    std::ofstream f("outputs/aws_audit/"+name+".ppm",std::ios::binary);
    f<<"P6\n320 200\n255\n";
    for (auto p:pixels) { const char rgb[]={char(p>>16),char(p>>8),char(p)}; f.write(rgb,3); }
    check(bool(f),"capture write failed");
}
}

void runBattleAwsSmoke(bool visual) {
    runtimeChecks(); replayChecks();
    App app(findResourceRoot()); app.loadResources();
    auto& rt=app.campaignBattleRuntime_;
    rt.active=true; rt.world=BattleWorld::create(fixture());
    rt.presentationMode=CampaignBattlePresentationMode::Cockpit;
    rt.currentPresentationSnapshot=rt.world->snapshot();
    // Actual key routing must pause weapons on both auxiliary screens.
    app.handleCampaignBattleRuntimeKeyDown('C'); app.handleCampaignBattleRuntimeKeyDown('W');
    check(rt.pendingWeaponControls.empty(),"Command Screen changed AWS");
    app.handleCampaignBattleRuntimeKeyDown('C');
    rt.mechStatusVisible=true; app.handleCampaignBattleRuntimeKeyDown('W');
    check(rt.pendingWeaponControls.empty(),"Mech Status changed AWS");
    app.handleCampaignBattleRuntimeKeyDown('G');
    app.handleCampaignBattleRuntimeKeyDown('2'); app.handleCampaignBattleRuntimeKeyDown('O');
    app.currentKeyDownRepeat_=true; app.handleCampaignBattleRuntimeKeyDown('O'); app.currentKeyDownRepeat_=false;
    check(rt.pendingWeaponControls.size()==2,"key edges lost or repeated");
    app.enqueueCampaignBattleRuntimeInput(*rt.world); rt.world->tick();
    check(!player(*rt.world).weapons[1].awsLinked && player(*rt.world).selectedWeaponInstanceId==2,"campaign queue reorders selection/link");
    check(rt.pendingWeaponControls.empty(),"pending controls not consumed");
    auto replayed=runBattleReplay(fixture(),parseBattleReplay(serializeBattleReplay(rt.inputReplay)),1);
    check(battleSnapshotFingerprint(replayed)==battleSnapshotFingerprint(rt.world->snapshot()),"actual campaign replay mismatch");
    app.handleCampaignBattleRuntimeKeyDown(VK_TAB); app.enqueueCampaignBattleRuntimeInput(*rt.world); rt.world->tick();
    check(player(*rt.world).weaponShotsFired==2,"campaign Tab did not fire linked set");

    HWND parent=CreateWindowExW(0,L"STATIC",L"AWS probe",WS_OVERLAPPEDWINDOW,0,0,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    CampaignBattleGlViewport viewport;
    check(parent && viewport.initialize(GetModuleHandleW(nullptr),parent),"WGL initialization");
    for (const std::string preset : {"locust","shadow_hawk","warhammer"}) {
        auto s=BattleWorld::create(fixture(preset)).snapshot();
        std::vector<uint32_t> onPixels;
        for (int mode=0;mode<3;++mode) {
            s.combatants.front().automaticWeaponsEnabled=mode!=1;
            s.combatants.front().weapons[1].awsLinked=mode!=2;
            const auto scene=campaignBattleRenderSceneFromSnapshot(s,CampaignBattlePresentationMode::Cockpit,"Sorted Original Files");
            check(scene.valid,scene.reason);
            app.renderCampaignBattleCockpitPresentation(s,{},scene);
            const auto& panel=campaignCockpitWeaponPanelLayout(scene.cockpit.family);
            const int y=campaignCockpitWeaponPanelRowY(panel,1);
            const auto lamp=campaignCockpitWeaponLamp(false,mode!=2,mode!=1);
            for(int py=0;py<7;++py) for(int px=0;px<8;++px)
                check(app.framebuffer_[(y-1+py)*320+panel.statusX-1+px]==lamp[py*8+px],"actual weapon panel membership pixels");
            const auto rect=campaignCockpitLayout(scene.cockpit.family).viewport;
            viewport.setBounds(RECT{0,0,rect.width,rect.height});
            check(viewport.updateScene(scene),viewport.lastError()); const auto pixels=viewport.captureScenePixels();
            if (mode==0) onPixels=pixels;
            else check((pixels==onPixels)==(mode==2),"AWS indicator off/on pixels");
            check(pixels.size()==static_cast<size_t>(rect.width*rect.height),"capture dimensions");
            for(int py=0;py<rect.height;++py) for(int px=0;px<rect.width;++px)
                app.framebuffer_[(rect.y+py)*320+rect.x+px]=pixels[py*rect.width+px];
            if(visual) savePpm(preset+"_"+std::to_string(mode),app.framebuffer_);
        }
    }
    // The modal spans the GL child and CPU cockpit: check every covered pixel
    // at their seam for all three cockpit families, and restore on cancellation.
    for(const std::string preset:{"locust","shadow_hawk","battlemaster"}) {
        auto s=BattleWorld::create(fixture(preset)).snapshot();
        const auto scene=campaignBattleRenderSceneFromSnapshot(s,CampaignBattlePresentationMode::Cockpit,"Sorted Original Files");
        const auto rect=campaignCockpitLayout(scene.cockpit.family).viewport;
        viewport.setBounds(RECT{0,0,rect.width,rect.height});
        CampaignBattleGlCockpitOptions options;viewport.setCockpitOptions(options);
        check(viewport.updateScene(scene),viewport.lastError());
        const auto unprompted=viewport.captureScenePixels();
        rt.retreatPromptVisible=true;app.renderCampaignBattleCockpitPresentation(s,{},scene);
        const auto& prompt=app.campaignCockpitDynamicLayers_.retreatPrompt;
        check(prompt.valid,"retreat sprite: "+prompt.reason);
        options.retreatPromptVisible=true;viewport.setCockpitOptions(options);
        const auto pixels=viewport.captureScenePixels();
        check(pixels!=unprompted,"retreat invisible in GL child");
        for(int y=0;y<27;++y)for(int x=0;x<107;++x) {
            const auto expected=prompt.bgraPixels[y*107+x];
            check(app.framebuffer_[(87+y)*320+107+x]==expected,"retreat CPU pixels differ");
            const int vx=107+x-rect.x,vy=87+y-rect.y;
            if(vx>=0 && vx<rect.width && vy>=0 && vy<rect.height)
                check((pixels[vy*rect.width+vx]&0xffffff)==(expected&0xffffff),"retreat GL/CPU seam differs");
        }
        if(visual) {
            for(int y=0;y<rect.height;++y)for(int x=0;x<rect.width;++x)
                app.framebuffer_[(rect.y+y)*320+rect.x+x]=pixels[y*rect.width+x];
            savePpm(preset+"_retreat",app.framebuffer_);
        }
        rt.retreatPromptVisible=false;options.retreatPromptVisible=false;viewport.setCockpitOptions(options);
        check(viewport.captureScenePixels()==unprompted,"cancelled retreat changed GL state/frame");
    }
    viewport.shutdown(); DestroyWindow(parent);
    std::cout<<"mw_battle_aws_smoke: ok checks="<<checks<<" all_mechs=8 replay=yes campaign_keys=yes WGL=yes\n";
}
int main(int argc,char** argv) {
    try { runBattleAwsSmoke(argc>1 && std::string(argv[1])=="--visual"); return 0; }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
