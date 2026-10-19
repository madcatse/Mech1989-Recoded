#pragma once
#include <cstdint>

namespace mw::battle {
// Raw BTECH fields. These functions contain no world/fixed-tick conversion.
bool originalAiDestinationJumpDue(int installed, int missing, int status,
    int gyro, int posture, int fuel, int32_t distance);
bool originalAiObstacleJumpDue(bool blocked, int reversing, int mode,
    int installed, int playerHeatShutdown, int posture, int fuel, int32_t distance);
struct OriginalAiFlightInput {
    int16_t acceleration=6, speedCap=72;
    int32_t height=0; // center height minus definition.height/2
    int16_t vertical=0, forward=0, fuel=800;
    int status=1, leftLeg=1, rightLeg=1, installed=3, missing=0;
    int movementMode=0, terrainProbe=0;
    uint16_t heading=0, desiredHeading=0;
};
struct OriginalAiFlightOutput {
    int16_t vertical=0, forward=0, fuel=0, turn=0;
    int heat=0;
};
OriginalAiFlightOutput originalAiFlightControl(const OriginalAiFlightInput& input);
void originalAiFlightDrag(int16_t& vertical, int16_t& forward, int16_t& turn);
int16_t originalAiJumpRecharge(int16_t fuel, bool airborne);

enum class BattleAiJumpPhase : uint8_t { Ground, Preparing, Flying, Landing };
enum class BattleAiJumpReason : uint8_t { None, Destination, Obstacle };
struct BattleAiJumpRuntime {
    bool enabled=false;
    BattleAiJumpPhase phase=BattleAiJumpPhase::Ground;
    BattleAiJumpReason lastReason=BattleAiJumpReason::None;
    uint8_t animationSequence=0, frame=0;
    double accumulator=0;
    int16_t fuel=800, vertical=0, forward=0, turn=0;
    int pendingHeat=0;
    uint64_t eligibleDraws=0, launches=0, landings=0;
    uint8_t lastRoll=0;
    // Replacement planner adapter; deliberately separate from source gates.
    bool moveRequested=false, obstacleAhead=false;
    int actionMode=0;
    double destinationX=0, destinationZ=0;
};
} // namespace mw::battle
