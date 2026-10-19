#pragma once
#include "battle/battle_world.h"

namespace mw::battle {
// Fixed-tick pose. Uses the same assembly matrices and bounds as presentation;
// no file reads, interpolation, or render-owned state enters the simulation.
BattleMechHitPose battleMechHitPose(
    const BattleMechHitProfile& profile, bool walking, uint64_t elapsedMs,
    double forwardSpeed, double torsoYaw);
BattleHitPoint battleMechHitPointToBind(
    BattleHitPoint point, int componentId,
    const BattleMechHitPose& posed, const BattleMechHitPose& bind, bool direction);
}
