#include "battle/battle_hit_geometry.h"
#include <algorithm>
#include <cmath>

namespace mw::battle {
namespace {
BattleHitPoint transform(const mech3d::Mat4f& m, BattleHitPoint p, bool direction=false) {
    return {m[0]*p.x+m[4]*p.y+m[8]*p.z+(direction?0:m[12]),
            m[1]*p.x+m[5]*p.y+m[9]*p.z+(direction?0:m[13]),
            m[2]*p.x+m[6]*p.y+m[10]*p.z+(direction?0:m[14])};
}
const mech3d::ModelAssemblyComponent* component(const mech3d::ModelAssembly& assembly, int id) {
    const auto it=std::find_if(assembly.components.begin(),assembly.components.end(),
        [id](const auto& c) { return c.componentId==id; });
    return it==assembly.components.end()?nullptr:&*it;
}
}
BattleMechHitPose battleMechHitPose(
    const BattleMechHitProfile& profile, bool walking, uint64_t elapsedMs,
    double forwardSpeed, double torsoYaw) {
    if (profile.bindPose.triangles.empty()) return {{},profile.triangles};
    const auto* source=&profile.bindPose;
    if (walking && !profile.walkPoses.empty()) {
        int frame=mech3d::animationFrameForElapsedMs(profile.walkAnimation,elapsedMs);
        if (forwardSpeed < -0.0001) frame=static_cast<int>(profile.walkPoses.size())-1-frame;
        source=&profile.walkPoses.at(static_cast<size_t>(frame));
    }
    BattleMechHitPose result=*source;
    mech3d::MechPose pose;
    mech3d::applyPlayerTorsoYawPose(pose,result.assembly,static_cast<float>(torsoYaw));
    mech3d::updateModelAssemblyPose(result.assembly,pose);
    const auto& bounds=result.assembly.bounds;
    for (auto& t : result.triangles) {
        const auto* c=component(result.assembly,t.componentId);
        for (auto* p : {&t.a,&t.b,&t.c}) {
            *p=transform(c->worldMatrix,*p);
            p->x-=bounds.center.x; p->y-=bounds.min.y; p->z-=bounds.center.z;
        }
    }
    return result;
}
BattleHitPoint battleMechHitPointToBind(
    BattleHitPoint p, int id, const BattleMechHitPose& posed,
    const BattleMechHitPose& bind, bool direction) {
    const auto* from=component(posed.assembly,id);
    const auto* to=component(bind.assembly,id);
    if (!from || !to) return p; // older diagnostic triangle-only profiles
    if (!direction) {
        p.x+=posed.assembly.bounds.center.x-from->worldMatrix[12];
        p.y+=posed.assembly.bounds.min.y-from->worldMatrix[13];
        p.z+=posed.assembly.bounds.center.z-from->worldMatrix[14];
    }
    // Component pose matrices are rigid: inverse rotation is its transpose.
    const auto& m=from->worldMatrix;
    p={m[0]*p.x+m[1]*p.y+m[2]*p.z,
       m[4]*p.x+m[5]*p.y+m[6]*p.z,
       m[8]*p.x+m[9]*p.y+m[10]*p.z};
    p=transform(to->worldMatrix,p,direction);
    if (!direction) {
        p.x-=bind.assembly.bounds.center.x;
        p.y-=bind.assembly.bounds.min.y;
        p.z-=bind.assembly.bounds.center.z;
    }
    return p;
}
double battleCockpitBobSpeedRatio(double speed,double forward,double reverse) {
    const double maximum=speed<0?reverse:forward;
    return maximum>0?std::clamp(std::abs(speed)/maximum,0.,1.):0.;
}
double battleCockpitBobOffsetWorldUnits(
    double speed,double forward,double reverse,double elapsed,bool destroyed,
    const BattleCockpitBobTuning& tuning) {
    if (destroyed || tuning.cycleMs<=0 || tuning.maxWorldUnits<=0) return 0;
    const double ratio=battleCockpitBobSpeedRatio(speed,forward,reverse);
    if (ratio<=.001) return 0;
    return std::sin(2.*3.14159265358979323846*elapsed/tuning.cycleMs)*tuning.maxWorldUnits*ratio;
}
}
