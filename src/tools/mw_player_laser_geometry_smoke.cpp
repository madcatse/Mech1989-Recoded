#include "battle/battle_module.h"
#include "presentation/campaign_cockpit_compositor.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

namespace {
using namespace mw;
using P = battle::BattleHitPoint;
size_t checks = 0, shots = 0, failures = 0;
void require(bool ok, const std::string& message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
P add(P a, P b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
P sub(P a, P b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
P mul(P a, double n) { return {a.x*n,a.y*n,a.z*n}; }
double dot(P a,P b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
P cross(P a,P b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
P transform(const mech3d::Mat4f& m, P p) {
    return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],
            m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
            m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
}
struct Surface { P a,b,c; int component; bool leg; };
// Independent oracle: assemble the draw ranges just as the GL viewport does,
// including the animated bounds used by its final centering translation.
std::vector<Surface> drawnSurface(const battle::CombatantSnapshot& target) {
    const auto entries = mech3d::mechCatalogEntries();
    const auto& entry = *std::find_if(entries.begin(),entries.end(),[&](const auto& e) { return e.presetId==target.mechPresetId; });
    const auto records = legacy3d::loadRuntimeShapeRecords(entry.defaultResourcePath);
    auto definition = mech3d::makeCatalogMechModelDefinition(target.mechPresetId);
    if (target.activeAnimationId == "walk") {
        const auto animation = mech3d::makeCatalogAnimationDefinition(target.mechPresetId,"walk");
        int frame = mech3d::animationFrameForElapsedMs(animation,target.walkAnimationElapsedMs);
        if (target.forwardSpeed < -0.0001) frame = mech3d::animationFrameCount(animation)-1-frame;
        definition = mech3d::applyAnimationFrame(definition,animation,frame);
    }
    auto instance = mech3d::buildMechRenderInstance(definition,records,{});
    mech3d::MechPose pose;
    mech3d::applyPlayerTorsoYawPose(pose,instance.assembly,static_cast<float>(target.torsoYawRadians));
    mech3d::updateMechRenderInstancePose(instance,pose);
    const auto& bounds = instance.assembly.bounds;
    const double s=std::sin(target.transform.headingRadians), c=std::cos(target.transform.headingRadians);
    std::vector<Surface> result;
    for (const auto& range : instance.ranges) {
        const auto component = std::find_if(instance.assembly.components.begin(),instance.assembly.components.end(),
            [&](const auto& item) { return item.componentId == range.componentId; });
        const auto vertex = [&](uint32_t i) {
            const size_t k=instance.batch.triangleIndices.at(i)*6u;
            auto p=transform(component->worldMatrix,{instance.batch.vertices[k],instance.batch.vertices[k+1],instance.batch.vertices[k+2]});
            p=sub(p,{bounds.center.x,bounds.min.y,bounds.center.z});
            return add({c*p.x+s*p.z,p.y,-s*p.x+c*p.z},
                {target.transform.x,target.transform.y,target.transform.z});
        };
        for (uint32_t i=range.triangleIndexOffset; i<range.triangleIndexOffset+range.triangleIndexCount; i+=3)
            result.push_back({vertex(i),vertex(i+1),vertex(i+2),range.componentId,
                component->debugLabel.find("leg") != std::string::npos});
    }
    return result;
}
// Plane intersection + edge half spaces, deliberately not the runtime's
// Moller-Trumbore implementation. Both sides of authored faces are hittable.
int nearest(const std::vector<Surface>& surface,P origin,P direction,double& distance) {
    int id=-1; distance=1e30;
    for (const auto& t : surface) {
        const auto normal=cross(sub(t.b,t.a),sub(t.c,t.a));
        const double divisor=dot(normal,direction);
        if (std::abs(divisor)<1e-9) continue;
        const double d=dot(normal,sub(t.a,origin))/divisor;
        if (d<=1e-9 || d>=distance) continue;
        const auto p=add(origin,mul(direction,d));
        if (dot(normal,cross(sub(t.b,t.a),sub(p,t.a))) < -1e-5 ||
            dot(normal,cross(sub(t.c,t.b),sub(p,t.b))) < -1e-5 ||
            dot(normal,cross(sub(t.a,t.c),sub(p,t.c))) < -1e-5) continue;
        distance=d; id=t.component;
    }
    return id;
}
battle::BattleStartParams fixture(const std::string& preset) {
    battle::BattleStartParams p;
    p.fixedTickSeconds=.18; p.constrainToTerrainBounds=false;
    p.maxForwardSpeed=250; p.maxReverseSpeed=250; p.acceleration=100000;
    p.deterministicCombatRuntimeEnabled=true; p.individualWeaponRuntimeEnabled=true;
    p.originalProjectileRuntimeEnabled=true;
    p.weaponTargetPolicy=battle::BattleWeaponTargetPolicy::CrosshairRayVisibleCombatantProvisional;
    p.playerStartTransform={0,0,0,0};
    p.mechHitProfiles.push_back(battle::campaignBattleCatalogHitProfile("Sorted Original Files","locust"));
    if (preset!="locust") p.mechHitProfiles.push_back(battle::campaignBattleCatalogHitProfile("Sorted Original Files",preset));
    battle::BattleCombatantLaunchState target;
    target.mechPresetId=preset; target.startTransform={10000,0,10000,0};
    target.persistentMechState.valid=true; target.persistentMechState.armorPercent=100;
    p.combatantLaunchStates={target};
    return p;
}
battle::BattleReplay motion(int steps,double throttle,int twist) {
    battle::BattleReplay r;
    for (int i=0;i<steps;++i) {
        battle::BattleInputCommand c;
        c.entityId={2}; c.tickIndex=i; c.throttle=throttle;
        c.torsoYawStepDelta=i==0?twist:0; r.commands.push_back(c);
    }
    return r;
}
void catalogShots() {
    for (const auto& entry : mech3d::mechCatalogEntries()) {
        std::set<int> checkedLegs;
        auto params=fixture(entry.presetId);
        const auto& camera=params.mechHitProfiles.front();
        for (double throttle : {0.,1.,-1.}) for (int frame=0;frame<(throttle==0?1:8);++frame)
        for (int twist : {0,7,-7}) for (double facing : {0.,1.1,3.14159265358979323846}) {
            params.combatantLaunchStates[0].startTransform.headingRadians=facing;
            const int steps=frame+1;
            auto replay=motion(steps,throttle,twist);
            const auto dry=battle::runBattleReplay(params,replay,steps);
            const auto surface=drawnSurface(dry.combatants.at(1));
            size_t tested=0;
            std::map<int,int> perComponent;
            for (const auto& triangle : surface) {
                if (perComponent[triangle.component] >= (triangle.leg?4:2)) continue;
                const P point=perComponent[triangle.component]%2
                    ? mul(add(triangle.a,triangle.b),.5)
                    : mul(add(add(triangle.a,triangle.b),triangle.c),1./3.);
                constexpr int pitch=-9;
                const double slope=(camera.cockpitViewportHeight*.5-camera.neutralCrosshairY+pitch*3.)/
                    (camera.cockpitViewportHeight*.5)*std::tan(3.14159265358979323846/8.);
                const double distance=(point.y-camera.aimOriginHeight)/slope;
                if (distance<100 || distance>4400) continue;
                const double side=(camera.cockpitViewportWidth*.5-camera.neutralCrosshairX)/
                    (camera.cockpitViewportHeight*.5)*std::tan(3.14159265358979323846/8.);
                const P origin={point.x-side*distance,camera.aimOriginHeight,point.z-distance};
                const P direction=mul(P{side,slope,1},1./std::sqrt(1+slope*slope+side*side));
                double expectedDistance;
                const int expected=nearest(surface,origin,direction,expectedDistance);
                if (expected!=triangle.component) continue; // a different limb occludes this face
                params.playerStartTransform={origin.x,0,origin.z-params.cockpitCameraForwardOffset,0};
                battle::BattleInputCommand fire;
                fire.tickIndex=steps-1; fire.aimPitchStepDelta=pitch;
                fire.selectWeaponInstanceId=1; fire.fireWeapon=true;
                replay.commands.push_back(fire);
                const auto actual=battle::runBattleReplay(params,replay,steps);
                replay.commands.pop_back();
                ++shots;
                const bool ok=actual.lastShot.hit && actual.lastShot.hitComponentId==expected &&
                    std::abs(actual.lastShot.impactPoint.y-(origin.y+direction.y*expectedDistance))<.01;
                if (!ok) {
                    if (failures<12) std::cerr << entry.presetId << " throttle=" << throttle << " frame=" << frame
                        << " twist=" << twist << " expected=" << expected << " actual=" << actual.lastShot.hitComponentId
                        << " result=" << static_cast<int>(actual.lastShot.result)
                        << " expectedY=" << origin.y+direction.y*expectedDistance << " actualY=" << actual.lastShot.impactPoint.y << '\n';
                    ++failures;
                }
                if (ok && triangle.leg) checkedLegs.insert(expected);
                if (triangle.leg && throttle==1 && frame==0 && twist==0 && facing==0 && perComponent[triangle.component]==0) {
                    auto world=battle::BattleWorld::create(params);
                    world.hideComponent({2},expected);
                    for (auto c : replay.commands) world.enqueueInput(c);
                    fire.entityId=world.playerEntityId(); world.enqueueInput(fire); world.runTicks(steps);
                    auto visible=surface;
                    visible.erase(std::remove_if(visible.begin(),visible.end(),[&](const auto& t) { return t.component==expected; }),visible.end());
                    double hiddenDistance;
                    const int remaining=nearest(visible,origin,direction,hiddenDistance);
                    const auto hiddenShot=world.snapshot().lastShot;
                    require(remaining<0 ? !hiddenShot.hit : hiddenShot.hit && hiddenShot.hitComponentId==remaining,
                        "invisible animated leg still blocks the ray: "+entry.presetId);
                }
                ++tested; ++perComponent[triangle.component];
            }
            require(tested>0,"no visible leg probes: "+entry.presetId);
        }
        require(checkedLegs.size()==2,"both legs were not exercised: "+entry.presetId);
    }
    require(failures==0,"visible component hits disagree with rendered pose: "+std::to_string(failures)+"/"+std::to_string(shots));
}
void movingShooter() {
    for (const std::string shooter : {"locust","phoenix_hawk","warhammer"})
    for (double throttle : {1.,-1.}) for (int steps : {1,4,9,15}) {
        auto params=fixture("locust");
        params.fixedTickSeconds=.05;
        params.playerMechPresetId=shooter;
        const auto camera=battle::campaignBattleCatalogHitProfile("Sorted Original Files",shooter);
        params.cockpitCameraHeight=camera.aimOriginHeight;
        if (shooter!="locust") params.mechHitProfiles.push_back(camera);
        params.playerStartTransform.headingRadians=.4;
        const auto mounts=mech3d::catalogWeaponMounts(shooter);
        const auto laser=std::find_if(mounts.begin(),mounts.end(),[](const auto& m) { return m.weaponTypeId=="medium_laser"; });
        require(laser!=mounts.end(),"moving shooter fixture has no medium laser");
        auto replay=motion(steps,1.,-5);
        for (int tick=0;tick<steps;++tick) {
            battle::BattleInputCommand c;
            c.tickIndex=tick; c.throttle=throttle;
            c.torsoYawStepDelta=tick==0?3:0; c.aimPitchStepDelta=tick==0?-9:0;
            replay.commands.push_back(c);
        }
        const auto dry=battle::runBattleReplay(params,replay,steps);
        const auto& player=dry.combatants.front();
        const double bob=battle::battleCockpitBobOffsetWorldUnits(player.forwardSpeed,player.maxForwardSpeed,
            player.maxReverseSpeed,static_cast<double>(player.walkAnimationElapsedMs),false);
        const auto family=shooter=="locust"?battle::CampaignCockpitFamily::Light:
            shooter=="phoenix_hawk"?battle::CampaignCockpitFamily::Medium:battle::CampaignCockpitFamily::Heavy;
        const auto& layout=presentation::campaignCockpitLayout(family);
        const auto hud=presentation::campaignCockpitViewportHudLayout(family,player.aimPitchStep);
        require(hud.crosshairCenterY-layout.viewport.y == camera.neutralCrosshairY-player.aimPitchStep*3.,
            "catalog aim projection disagrees with cockpit HUD");
        const double slope=(layout.viewport.height*.5-(hud.crosshairCenterY-layout.viewport.y))/
            (layout.viewport.height*.5)*std::tan(3.14159265358979323846/8.);
        const double heading=player.transform.headingRadians+player.torsoYawRadians;
        const double side=(layout.viewport.width*.5-(hud.crosshairCenterX-layout.viewport.x))/
            (layout.viewport.height*.5)*std::tan(3.14159265358979323846/8.);
        const P ray={std::sin(heading)+std::cos(heading)*side,slope,std::cos(heading)-std::sin(heading)*side};
        const P direction=mul(ray,1./std::sqrt(dot(ray,ray)));
        const auto surface=drawnSurface(dry.combatants.at(1));
        bool tested=false;
        for (const auto& triangle : surface) {
            if (!triangle.leg) continue;
            const auto point=mul(add(add(triangle.a,triangle.b),triangle.c),1./3.);
            const double height=camera.aimOriginHeight+bob;
            const double distance=(point.y-height)/slope;
            if (distance<100 || distance>4400) continue;
            const P origin={point.x-ray.x*distance,height,point.z-ray.z*distance};
            double expectedDistance;
            const int expected=nearest(surface,origin,direction,expectedDistance);
            if (expected!=triangle.component) continue;
            params.playerStartTransform.x=origin.x-player.transform.x-std::sin(heading)*params.cockpitCameraForwardOffset;
            params.playerStartTransform.z=origin.z-player.transform.z-std::cos(heading)*params.cockpitCameraForwardOffset;
            replay.commands.back().fireWeapon=true;
            replay.commands.back().selectWeaponInstanceId=laser->slotIndex+1u;
            const auto actual=battle::runBattleReplay(params,replay,steps);
            require(actual.lastShot.hit && actual.lastShot.hitComponentId==expected &&
                std::abs(actual.lastShot.aimOrigin.y-height)<1e-8 &&
                std::abs(actual.lastShot.impactPoint.y-(height+direction.y*expectedDistance))<.01,
                "moving shooter/bob/torso ray differs from visible leg: "+shooter);
            ++shots;
            const auto restored=battle::parseBattleReplay(battle::serializeBattleReplay(replay));
            const auto repeated=battle::runBattleReplay(params,restored,steps);
            const auto fingerprint=battle::battleSnapshotFingerprint(actual);
            require(fingerprint==battle::battleSnapshotFingerprint(repeated),"moving laser replay reconstruction differs");
            auto world=battle::BattleWorld::create(params);
            for (auto c : restored.commands) { if (!battle::isValid(c.entityId)) c.entityId=world.playerEntityId(); world.enqueueInput(c); }
            for (int tick=0;tick<steps;++tick) {
                const auto before=world.snapshot(); world.tick(); const auto after=world.snapshot();
                for (double alpha : {0.,.1,.5,.99,1.}) {
                    const auto scene=battle::campaignBattleInterpolatedRenderSceneFromSnapshots(before,after,alpha,
                        battle::CampaignBattlePresentationMode::Cockpit,"Sorted Original Files");
                    require(scene.snapshotOwned,"render scene lost snapshot ownership");
                    if (tick==steps-1 && alpha==1.) require(
                        std::abs(scene.camera.transform.y+scene.cockpitCameraBobOffsetY-actual.lastShot.aimOrigin.y)<1e-8,
                        "authoritative aim origin differs from current cockpit camera");
                }
            }
            require(fingerprint==battle::battleSnapshotFingerprint(world.snapshot()),"render cadence or tick subdivision changed moving shot");
            // Moving outside the silhouette must miss; no inflated limb hitboxes.
            params.playerStartTransform.x+=3000;
            const auto miss=battle::runBattleReplay(params,replay,steps);
            require(!miss.lastShot.hit && miss.lastShot.result==battle::BattleShotResult::MissCrosshairRay,
                "empty space acquired a hidden ordinary target");
            tested=true; break;
        }
        require(tested,"no moving shooter leg probe: "+shooter);
    }
}
}
int main() {
    try { catalogShots(); movingShooter(); std::cout << "mw_player_laser_geometry_smoke: ok checks=" << checks << " shots=" << shots << '\n'; return 0; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
