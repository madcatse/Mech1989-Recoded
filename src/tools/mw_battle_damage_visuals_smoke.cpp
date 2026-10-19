#include "battle/battle_module.h"
#include "battle/battle_setup.h"
#include "battle/mission_contract.h"
#include "presentation/campaign_battle_gl_viewport.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace mw;
using namespace battle;
using namespace presentation;
using Section = mech3d::MechInternalSectionId;
size_t checks = 0;
void require(bool ok, const std::string& message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
BattleStartParams fixture(const std::string& preset = "locust") {
    BattleStartParams p;
    p.terrainScenarioPath = "Sorted Original Files/DAT/SNARIO.DAT";
    p.terrainScenarioIndex = 2; p.terrainEnvironmentId = 1;
    p.fixedTickSeconds = .05;
    p.playerMechPresetId = preset;
    p.playerStartTransform = {60000,0,30000,0};
    p.mission = battleMissionBriefingFromDefinition(*battleMissionDefinitionById(8));
    p.mechSystemsSnapshotEnabled = true;
    BattleCombatantLaunchState enemy;
    enemy.mechPresetId = "warhammer";
    enemy.roster.team = BattleTeam::Opposing; enemy.roster.sourceSlot = "opposing:0";
    enemy.startTransform = {60000,0,32000,3.141592653589793};
    p.combatantLaunchStates.push_back(enemy);
    return p;
}
CampaignBattleRenderScenePackage sceneFor(const BattleSnapshot& s,
    CampaignBattlePresentationMode mode = CampaignBattlePresentationMode::Cockpit) {
    const auto scene = campaignBattleRenderSceneFromSnapshot(s,mode,"Sorted Original Files");
    require(scene.valid && scene.snapshotOwned, "scene: " + scene.reason);
    return scene;
}
bool nearPoint(const BattleHitPoint& a, const BattleHitPoint& b) {
    return std::abs(a.x-b.x)<.001 && std::abs(a.y-b.y)<.001 && std::abs(a.z-b.z)<.001;
}
int componentId(const std::string& preset, const std::string& label) {
    const auto model=mech3d::makeCatalogMechModelDefinition(preset);
    for (const auto& part:model.components)
        if (part.debugLabel.find(label)!=std::string::npos) return part.componentId;
    throw std::runtime_error("missing catalog part: "+preset+":"+label);
}
void hitChecks() {
    auto s = BattleWorld::create(fixture()).snapshot();
    s.tickIndex = 20; s.elapsedMs = 1000;
    require(!sceneFor(s).playerWeaponHitFlash,"intact player flashes");
    BattleImpactEvent hit;
    hit.valid = true; hit.shotSequence = 11; hit.impactTickIndex = 19;
    hit.targetKind = BattleShotTargetKind::Combatant;
    hit.targetEntityId = s.combatants.front().id;
    hit.deliveryState = BattleShotDeliveryState::Immediate;
    s.impactEvents = {hit};
    const auto fingerprint = battleSnapshotFingerprint(s);
    for (const auto delivery : {BattleShotDeliveryState::Immediate, BattleShotDeliveryState::ProjectileImpact}) {
        s.impactEvents[0].deliveryState = delivery;
        for (uint64_t age = 0; age <= 5; ++age) {
            s.tickIndex = 19+age;
            require(sceneFor(s).playerWeaponHitFlash == (age>=1 && age<=3), "hit flash age/arrival wrong");
        }
    }
    s.tickIndex = 20;
    for (const auto delivery : {BattleShotDeliveryState::None, BattleShotDeliveryState::ProjectileInFlight,
            BattleShotDeliveryState::ProjectileTerrainImpact, BattleShotDeliveryState::ProjectileIntercepted,
            BattleShotDeliveryState::ProjectileExpired}) {
        s.impactEvents[0].deliveryState = delivery;
        require(!sceneFor(s).playerWeaponHitFlash,"non-contact flashes");
    }
    s.impactEvents = {hit}; s.impactEvents[0].targetEntityId = s.combatants.back().id;
    require(!sceneFor(s).playerWeaponHitFlash,"opponent contact flashes player");
    s.impactEvents = {hit}; s.impactEvents[0].valid = false;
    require(!sceneFor(s).playerWeaponHitFlash,"invalid contact flashes");
    s.impactEvents = {hit}; s.impactEvents[0].targetKind = BattleShotTargetKind::Objective;
    require(!sceneFor(s).playerWeaponHitFlash,"structure contact flashes player");
    s.impactEvents = {hit};
    for (int frame = 0; frame < 20; ++frame) require(sceneFor(s).playerWeaponHitFlash,"render consumed contact");
    require(battleSnapshotFingerprint(s)==fingerprint,"flash mutated snapshot");
    s.tickIndex = 24;
    require(!sceneFor(s).playerWeaponHitFlash,"stale contact flashes");
    hit.shotSequence = 12; hit.impactTickIndex = 23; s.impactEvents.push_back(hit);
    require(sceneFor(s).playerWeaponHitFlash,"subsequent contact did not renew flash");
}
void damageChecks() {
    for (int pixelSize : {1,2,4,8}) {
        std::array<uint8_t,128> previous{};
        for (int coverage=0; coverage<=16; ++coverage) {
            const auto mask=campaignBattleDamageSmokeStipple(coverage,pixelSize);
            int bits=0;
            for (size_t i=0; i<mask.size(); ++i) {
                require((mask[i]&previous[i])==previous[i],"smoke coverage masks are not nested");
                for (int bit=0; bit<8; ++bit) bits+=(mask[i]>>bit)&1;
            }
            require(bits==coverage*64,"smoke dithering has incorrect area coverage");
            require(mask==campaignBattleDamageSmokeStipple(coverage,pixelSize),"smoke mask is not deterministic");
            previous=mask;
        }
    }
    for (const auto& entry : mech3d::mechCatalogEntries()) {
        auto s = BattleWorld::create(fixture(entry.presetId)).snapshot();
        auto& actor = s.combatants.front();
        require(sceneFor(s).damageSmokeEmitters.empty(),"intact actor smokes");
        require(actor.detailedDamage.valid,"detailed fixture unavailable");
        actor.detailedDamage.armorSections[0].armorRemaining = 0;
        require(sceneFor(s).damageSmokeEmitters.empty(),"armor-only breach smokes");
        for (auto part : {Section::RightArm, Section::LeftArm, Section::LeftTorso, Section::RightTorso}) {
            actor.detailedDamage.internalSections[static_cast<size_t>(part)].structureRemaining = 0;
        }
        auto scene = sceneFor(s,CampaignBattlePresentationMode::External);
        require(scene.damageSmokeEmitters.size()==4,"destroyed section emitters missing");
        const auto probe = probeCampaignBattleGlResources(scene);
        require(probe.valid && probe.damageSmokePuffs.size()==16 && probe.damageSmokeSphereCount>=16,
            entry.presetId+" smoke resource loading: "+probe.reason);
        require(probe.damageSmokeRecordMask==15,"smoke stages 10-13 missing");
        for (const auto& puff : probe.damageSmokePuffs) {
            require(puff.opacity>=0.0f && puff.opacity<0.6f,"smoke opacity is not translucent");
            require(puff.stageFraction>=0.0f && puff.stageFraction<1.0f,"invalid smoke stage interpolation");
        }
        const auto mature=std::find_if(probe.damageSmokePuffs.begin(),probe.damageSmokePuffs.end(),
            [](const auto& puff) { return puff.recordIndex==11; });
        const auto old=std::find_if(probe.damageSmokePuffs.begin(),probe.damageSmokePuffs.end(),
            [](const auto& puff) { return puff.recordIndex==13; });
        require(old->opacity<mature->opacity*.5f,"old smoke does not dissipate");
        require(probe.gpuBatchContractPreserved,"smoke changed batch contract");
        require(probeCampaignBattleGlResources(sceneFor(s)).damageSmokePuffs.empty(),"own smoke obscures cockpit");
        const auto firstAnchor = probe.damageSmokePuffs.front().anchor;
        scene.combatantVisuals.front().transform.x += 700;
        scene.combatantVisuals.front().transform.z -= 1100;
        const auto translated = probeCampaignBattleGlResources(scene);
        require(nearPoint(translated.damageSmokePuffs.front().anchor,
            {firstAnchor.x+700, firstAnchor.y, firstAnchor.z-1100}),"smoke does not follow mech translation");
        // All section identities must resolve in moving/rotated and death models.
        scene.damageSmokeEmitters.clear();
        for (int part = 0; part < 8; ++part)
            scene.damageSmokeEmitters.push_back({actor.id,static_cast<Section>(part),false});
        auto& v = scene.combatantVisuals.front();
        v.animationId = "walk"; v.animationElapsedMs = 540; v.forwardSpeed = -100;
        v.torsoYawRadians = .6; v.transform.headingRadians = .8;
        const auto moving = probeCampaignBattleGlResources(scene);
        require(moving.valid && moving.damageSmokePuffs.size()==32,"moving section anchors missing");
        v.torsoYawRadians = -.6;
        const auto twisted = probeCampaignBattleGlResources(scene);
        require(!nearPoint(moving.damageSmokePuffs[0].anchor,twisted.damageSmokePuffs[0].anchor),"arm smoke ignores torso slew");
        const auto legSpan = [](const auto& probe) {
            const auto& a=probe.damageSmokePuffs[8].anchor;
            const auto& b=probe.damageSmokePuffs[12].anchor;
            return BattleHitPoint{a.x-b.x,a.y-b.y,a.z-b.z};
        };
        // The renderer recenters the whole posed bounds after torso slew;
        // that shared translation must move smoke too, without rotating legs.
        require(nearPoint(legSpan(moving),legSpan(twisted)),"leg smoke incorrectly inherits torso slew");
        // Time is not a render counter; restarting presentation reproduces puff positions.
        scene.presentationElapsedMs = 2731;
        const auto later = probeCampaignBattleGlResources(scene);
        require(!nearPoint(later.damageSmokePuffs[0].position,twisted.damageSmokePuffs[0].position),"smoke does not rise");
        const auto repeated = probeCampaignBattleGlResources(scene);
        require(nearPoint(later.damageSmokePuffs[0].position,repeated.damageSmokePuffs[0].position) &&
            later.damageSmokePuffs[0].opacity==repeated.damageSmokePuffs[0].opacity,"smoke depends on cadence");
        actor.mechDestroyed = true; actor.destroyedComponentIds = {2}; actor.activeAnimationId = "death";
        scene = sceneFor(s,CampaignBattlePresentationMode::External);
        require(scene.damageSmokeEmitters.size()==1 && scene.damageSmokeEmitters[0].wreck &&
            scene.damageSmokeEmitters[0].section==Section::CenterTorso,"wreck must smoke only from CT");
        const auto death=mech3d::makeCatalogAssemblyFrameAnimationDefinition(entry.presetId,"death");
        for (size_t frame=0; frame<death.frames.size(); ++frame) {
            scene.combatantVisuals.front().animationElapsedMs = frame*death.frameDurationMs;
            const auto fallen = probeCampaignBattleGlResources(scene);
            require(fallen.valid && fallen.damageSmokePuffs.size()==4,"death pose lost torso anchor");
            require(std::isfinite(fallen.damageSmokePuffs[0].anchor.y),"death anchor non-finite");
        }
        actor.missionStatus = CombatantMissionStatus::Escaped;
        const auto escaped = campaignBattleRenderSceneFromSnapshot(s,CampaignBattlePresentationMode::External,"Sorted Original Files");
        require(escaped.damageSmokeEmitters.empty(),"escaped actor still smokes");
    }
}
void runtimeChecks() {
    auto p = fixture("battlemaster");
    p.combatAiPolicy = BattleCombatAiPolicy::ReplacementDeterministicCombatAi;
    p.deterministicCombatRuntimeEnabled = true; p.individualWeaponRuntimeEnabled = true;
    p.originalProjectileRuntimeEnabled = true; p.stationaryTargetHitDiagnosticEnabled = true;
    p.originalHeatRuntimeEnabled = true; p.originalMajorSystemRuntimeEnabled = true;
    p.deterministicCollisionRuntimeEnabled = true;
    size_t hits = 0, immediate = 0, projectile = 0;
    for (bool missilesOnly : {false,true}) {
        if (missilesOnly) {
            const auto initial = BattleWorld::create(p).snapshot();
            auto& enemy = p.combatantLaunchStates.front();
            enemy.startTransform.z = p.playerStartTransform.z+1000;
            enemy.persistentMechState.valid = true;
            enemy.persistentMechState.heatSinksWorking = 10;
            enemy.persistentMechState.heatSinksTotal = 10;
            enemy.ammunitionStateValid = true; enemy.ammunitionByPool.fill(100);
            for (const auto& weapon : initial.combatants.back().weapons) {
                if (weapon.weaponTypeId.find("srm")==std::string::npos && weapon.weaponTypeId.find("lrm")==std::string::npos)
                    enemy.persistentMechState.weaponConditions[weapon.slotIndex] = 3;
            }
        }
        auto world = BattleWorld::create(p);
        auto plain = BattleWorld::create(p);
        for (int tick = 0; tick < 600; ++tick) {
            const auto before = world.snapshot(); world.tick(); plain.tick();
            const auto after = world.snapshot();
            bool contact = false;
            for (const auto& event : after.impactEvents) {
                if (event.targetKind!=BattleShotTargetKind::Combatant || event.targetEntityId!=world.playerEntityId() ||
                    event.impactTickIndex+1!=after.tickIndex) continue;
                ++hits; contact = true;
                if (event.deliveryState==BattleShotDeliveryState::Immediate) ++immediate;
                if (event.deliveryState==BattleShotDeliveryState::ProjectileImpact) ++projectile;
            }
            for (double alpha : {0.,.3,1.}) {
                const auto scene = campaignBattleInterpolatedRenderSceneFromSnapshots(before,after,alpha,
                    CampaignBattlePresentationMode::Cockpit,"Sorted Original Files");
                if (contact) require(scene.playerWeaponHitFlash,"actual incoming hit did not flash");
            }
            require(battleSnapshotFingerprint(after)==battleSnapshotFingerprint(plain.snapshot()),"presentation changed battle/replay");
        }
        require(battleSnapshotFingerprint(world.snapshot())==battleSnapshotFingerprint(runBattleReplay(p,{},600)),"repeat replay differs");
    }
    require(hits>0 && immediate>0,"AI fixture never hit player with immediate weapon");
    require(projectile>0,"AI fixture never landed projectile on player");
    std::cout << " actual_player_hits=" << hits << " immediate=" << immediate << " projectile=" << projectile;
}
void savePpm(const std::string& name, const std::vector<uint32_t>& pixels) {
    std::filesystem::create_directories("outputs/damage_visual_audit");
    std::ofstream f("outputs/damage_visual_audit/"+name+".ppm",std::ios::binary);
    f << "P6\n640 480\n255\n";
    for (auto c : pixels) { const char bytes[] = {char(c>>16),char(c>>8),char(c)}; f.write(bytes,3); }
    require(bool(f),"capture write failed");
}
void visualChecks() {
    HWND parent = CreateWindowExW(0,L"STATIC",L"Damage visuals probe",WS_OVERLAPPEDWINDOW,0,0,640,480,
        nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    CampaignBattleGlViewport viewport;
    require(parent && viewport.initialize(GetModuleHandleW(nullptr),parent),"WGL initialize failed");
    viewport.setBounds(RECT{0,0,640,480});
    auto s = BattleWorld::create(fixture()).snapshot();
    auto scene = sceneFor(s);
    scene.camera.transform.y += 2000; // expose sky above the fixture's nearby ridge
    require(viewport.updateScene(scene),viewport.lastError());
    const auto normal = viewport.captureScenePixels(); savePpm("cockpit_normal",normal);
    scene.playerWeaponHitFlash = true;
    require(viewport.updateScene(scene),viewport.lastError());
    const auto flash = viewport.captureScenePixels(); savePpm("cockpit_hit",flash);
    require(flash==viewport.captureScenePixels(),"capture consumed flash");
    size_t changed = 0;
    bool onlyRedSky = true;
    for (size_t i=0; i<normal.size(); ++i) {
        if (normal[i]==flash[i]) continue;
        ++changed;
        onlyRedSky = onlyRedSky && (flash[i]&0xffffffu)==0xff5555u;
    }
    require(onlyRedSky,"hit recolored non-sky pixels");
    require(changed>5000 && changed<normal.size(),"red sky missing or whole viewport recolored");
    scene.playerWeaponHitFlash = false;
    require(viewport.updateScene(scene),viewport.lastError());
    require(normal==viewport.captureScenePixels(),"flash did not clear");
    for (auto mode : {CampaignBattlePresentationMode::External,CampaignBattlePresentationMode::CockpitCommandMap,
            CampaignBattlePresentationMode::MissionStatus}) {
        auto other = sceneFor(s,mode);
        require(viewport.updateScene(other),viewport.lastError());
        const auto original = viewport.captureScenePixels(); other.playerWeaponHitFlash = true;
        require(viewport.updateScene(other),viewport.lastError());
        require(original==viewport.captureScenePixels(),"hit leaked into another view");
    }
    // Front three-quarter view, isolated on the flat ground apron so nearby
    // terrain cannot conceal the attachment or fallen torso under test.
    const auto child=FindWindowExW(parent,nullptr,L"MWCampaignBattleGlViewport",nullptr);
    SendMessageW(child,WM_RBUTTONDOWN,0,MAKELPARAM(0,0));
    SendMessageW(child,WM_MOUSEMOVE,MK_RBUTTON,MAKELPARAM(-536,-45));
    SendMessageW(child,WM_RBUTTONUP,0,MAKELPARAM(-536,-45));
    for (const auto& entry : mech3d::mechCatalogEntries()) {
        s = BattleWorld::create(fixture(entry.presetId)).snapshot();
        auto& actor = s.combatants.front();
        actor.detailedDamage.internalSections[static_cast<size_t>(Section::LeftArm)].structureRemaining = 0;
        actor.destroyedComponentIds = {componentId(entry.presetId,"left_arm")};
        s.elapsedMs = 950;
        for (bool wreck : {false,true}) {
            if (wreck) { actor.mechDestroyed = true; actor.destroyedComponentIds = {2};
                actor.activeAnimationId = "death";
                const auto death=mech3d::makeCatalogAssemblyFrameAnimationDefinition(entry.presetId,"death");
                actor.walkAnimationElapsedMs = (death.frames.size()-1)*death.frameDurationMs; }
            scene = sceneFor(s,CampaignBattlePresentationMode::External);
            auto& v=scene.combatantVisuals.front();
            v.transform={scene.terrain.boundsMinX+2000,0,scene.terrain.boundsMinZ-8000,0};
            require(viewport.updateScene(scene),viewport.lastError());
            const auto smoke=viewport.captureScenePixels();
            require(smoke==viewport.captureScenePixels(),"smoke capture changed without tick");
            savePpm(entry.presetId+(wreck?"_wreck":"_arm"),smoke);
            scene.damageSmokeEmitters.clear();
            require(viewport.updateScene(scene),viewport.lastError());
            const auto background=viewport.captureScenePixels();
            require(smoke!=background,"smoke not visible: "+entry.presetId);
            size_t ditherPixels=0, exposedPixels=0;
            for (size_t i=0; i<smoke.size(); ++i) {
                if ((background[i]&0xffffffu)!=0x00aa00u || smoke[i]==background[i]) continue;
                require((smoke[i]&0xffffffu)==0x555555u,"dithered smoke introduced blended RGB colors");
                ++ditherPixels;
                if (i+2<smoke.size() && (background[i+2]&0xffffffu)==0x00aa00u && smoke[i+2]==background[i+2])
                    ++exposedPixels;
            }
            require(ditherPixels>5 && exposedPixels>2,
                "smoke lacks visible dither coverage/holes: "+entry.presetId);
        }
    }
    scene=sceneFor(BattleWorld::create(fixture()).snapshot());
    scene.camera.transform.y+=2000;
    require(viewport.updateScene(scene),viewport.lastError());
    require(normal==viewport.captureScenePixels(),"smoke leaked GL state into cockpit/world rendering");
    viewport.shutdown(); DestroyWindow(parent);
}
}
int main(int argc, char** argv) {
    try {
        hitChecks(); damageChecks(); runtimeChecks();
        if (argc>1 && std::string(argv[1])=="--visual") visualChecks();
        std::cout << " mw_battle_damage_visuals_smoke: ok checks=" << checks << '\n'; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
