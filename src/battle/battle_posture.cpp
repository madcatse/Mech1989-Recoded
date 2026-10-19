#include "battle/battle_posture.h"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace mw::battle {
uint8_t originalPlayerStandUpFlag(int posture, int leftLeg, int rightLeg,
                                int leftActuator, int rightActuator, int roll) {
    if (posture != 1 || !leftLeg || !rightLeg || !leftActuator || !rightActuator) return 0;
    return roll < 5 ? 2 : 0x40;
}
bool originalHardLanding(int16_t verticalVelocity) { return verticalVelocity < -100; }
uint8_t originalAiLandingFlag(int status, int leftLeg, int rightLeg) {
    return status == -1 || !leftLeg || !rightLeg ? 1 : 0x20;
}
bool battleActuatorLossDisables(BattlePosture posture, int leftActuator, int rightActuator) {
    return posture == BattlePosture::Prone && (!leftActuator || !rightActuator);
}
OriginalCollisionFall originalCollisionFall(int massA, int massB,
    int16_t speedA, int16_t speedB, uint16_t headingA, uint16_t headingB,
    int otherStatus) {
    const auto word = [](int value) { const int w = value & 65535; return w >= 32768 ? w - 65536 : w; };
    const int angle = word(headingA - headingB);
    const int absolute = angle == -32768 ? 32767 : std::abs(angle);
    OriginalCollisionFall result;
    if (absolute <= 0x5ffa || massA <= 0 || massB <= 0) return result;
    const int a2 = word(massA * massA), b2 = word(massB * massB);
    if (a2 <= 0 || b2 <= 0) return result; // Outside the eight original chassis.
    const int difference = word(a2 * speedA) - word(b2 * speedB);
    if (difference > 0) result.otherFalls = difference / b2 > (otherStatus == -1 ? 5 : 20);
    if (difference < 0) result.moverFalls = -difference / a2 > 20;
    return result;
}
std::array<OriginalCollisionDamageCall, 6> originalCollisionDamageCalls(
    int massA, int massB, int16_t speedA, int16_t speedB,
    uint16_t headingA, uint16_t headingB) {
    const auto word = [](int64_t value) {
        const int w = static_cast<int>(static_cast<uint64_t>(value) & 65535u);
        return w >= 32768 ? w - 65536 : w;
    };
    const auto primary = [&](int mass, int speed) -> int16_t {
        // IMUL's high word is overwritten by CWD before signed >>15.
        const int square = word(static_cast<int64_t>(mass) * mass);
        return word(static_cast<int64_t>(square) * speed) < 0 ? -1 : 0;
    };
    const int angle = word(static_cast<int>(headingA) - headingB);
    const int absolute = angle == -32768 ? 32767 : std::abs(angle);
    if (absolute > 24570) {
        return {{{1,4,primary(massA,speedA)}, {1,2,0}, {1,3,0},
                 {0,4,primary(massB,speedB)}, {0,2,0}, {0,3,0}}};
    }
    if (absolute < 8190) {
        const auto damage = primary(massA,word(static_cast<int>(speedA)-speedB));
        // SAR(-1,1) remains -1, while the CWD/SUB/SAR halves round to zero.
        return {{{1,5,damage}, {1,2,0}, {1,3,0}, {0,4,damage}, {0,2,0}, {0,3,0}}};
    }
    const uint8_t side = angle > 0 ? 1 : 0;
    const auto damage = primary(massA,speedA);
    return {{{1,static_cast<uint8_t>(6+side),damage}, {1,side,0},
             {1,static_cast<uint8_t>(2+side),0}, {0,4,damage}, {0,3,0}, {0,2,0}}};
}
bool originalAiStandUpDue(int posture, int status, int leftLeg, int rightLeg,
                        int leftActuator, int rightActuator, uint64_t tick) {
    return status >= 0 && posture == 1 && leftLeg && rightLeg &&
        leftActuator && rightActuator && (tick & 7) == 0;
}
void updateAiBattlePosture(BattlePostureRuntime& s, uint64_t tick,
                          int status, int leftLeg, int rightLeg,
                          int leftActuator, int rightActuator, uint8_t roll) {
    if (status < -1) return;
    if (originalAiStandUpDue(static_cast<int>(s.posture), status, leftLeg,
                            rightLeg, leftActuator, rightActuator, tick)) {
        ++s.standAttempts; s.lastStandRoll = roll;
        // Failure is accepted whenever prone; success requires the last fall frame.
        if (roll >= 2 || (s.sequence == 4 && s.frame == 4)) {
            s.sequence = roll < 2 ? 5 : 7; s.frame = 0; s.changedTick = tick;
        }
    }
    if (tick != s.changedTick) {
        if (s.sequence == 4) s.frame = std::min<uint8_t>(4, s.frame + 1);
        else if (s.sequence == 5 && ++s.frame == 4) { s.sequence = 0; s.frame = 0; }
        else if (s.sequence == 7) ++s.frame;
    }
    if (s.sequence == 4) s.posture = s.frame == 4 ? BattlePosture::Prone : BattlePosture::Falling;
    else if (s.sequence == 7 && s.frame == 4) { s.sequence = 4; s.posture = BattlePosture::Prone; }
    else if (s.sequence == 5) s.posture = BattlePosture::Rising;
    else if (s.sequence == 0) s.posture = BattlePosture::Upright;
}
uint8_t originalLandingLegDamage(uint8_t roll) {
    if (roll > 3) throw std::invalid_argument("landing roll outside original R(4)");
    return static_cast<uint8_t>(roll + 3);
}
BattlePostureCameraFrame originalPostureCameraFrame(uint8_t sequence, uint8_t frame) {
    // DS:254E, 257E, 25A6; X is zero in all these source records.
    constexpr std::array<BattlePostureCameraFrame, 6> fall{{
        {0,-10,910}, {-50,-30,1820}, {-100,-130,4550},
        {-300,-210,7280}, {-350,-340,10920}, {-400,-550,16380}}};
    constexpr std::array<BattlePostureCameraFrame, 5> rise{{
        {-350,-340,10920}, {-300,-210,7280}, {-100,-130,4550},
        {-50,-30,1820}, {0,-10,910}}};
    constexpr std::array<BattlePostureCameraFrame, 6> failed{{
        {-350,-340,10920}, {-300,-210,7280}, {-100,-130,4550},
        {-300,-210,7280}, {-350,-340,10920}, {-400,-550,16380}}};
    if (sequence == 4) return fall[std::min<size_t>(frame, fall.size()-1)];
    if (sequence == 5) return rise[std::min<size_t>(frame, rise.size()-1)];
    if (sequence == 7) return failed[std::min<size_t>(frame, failed.size()-1)];
    return {};
}
void beginBattleFall(BattlePostureRuntime& s, uint64_t tick) {
    if (s.sequence == 4) return;
    s.posture = BattlePosture::Falling;
    s.sequence = 4; s.frame = 0; s.changedTick = tick; ++s.falls;
}
void attemptBattleStandUp(BattlePostureRuntime& s, uint64_t tick,
                         int leftLeg, int rightLeg, int leftActuator, int rightActuator,
                         uint8_t roll) {
    const auto flag = originalPlayerStandUpFlag(static_cast<int>(s.posture),
        leftLeg, rightLeg, leftActuator, rightActuator, roll);
    if (!flag) return;
    ++s.standAttempts; s.lastStandRoll = roll;
    // Input may consume a draw during a failed attempt; the camera handler
    // accepts the resulting flag only on the settled last fall frame (A0D6).
    if (s.sequence != 4 || s.frame != 5) return;
    s.sequence = flag == 2 ? 5 : 7;
    // The cockpit path keeps D385=1 throughout sequence 5 (also live-verified).
    // Repeated U consumes a draw but cannot restart that animation.
    s.posture = BattlePosture::Prone;
    s.frame = 0; s.changedTick = tick;
}
void advanceBattlePosture(BattlePostureRuntime& s, uint64_t tick) {
    if (!s.sequence || tick == s.changedTick) return;
    // Player camera frame counts DS:265E. One step per replacement fixed tick;
    // this cadence is an adaptation, not a recovered DOS wall-clock frequency.
    if (s.sequence == 4) {
        if (s.frame < 5) ++s.frame;
        else s.posture = BattlePosture::Prone;
    } else if (s.sequence == 5) {
        if (++s.frame == 5) { s.sequence = 0; s.frame = 0; s.posture = BattlePosture::Upright; }
    } else if (s.sequence == 7) {
        if (++s.frame == 6) { s.sequence = 4; s.frame = 5; s.posture = BattlePosture::Prone; }
    }
}
const char* battlePostureAnimation(const BattlePostureRuntime& s) {
    return s.sequence == 4 ? "fall" : s.sequence == 5 ? "stand_up" :
           s.sequence == 7 ? "stand_fail" : "idle";
}
uint8_t battlePostureRandom(uint32_t actor, uint64_t sequence, uint8_t draw, uint8_t bound) {
    if (!bound) throw std::invalid_argument("zero posture random bound");
    uint64_t x = (static_cast<uint64_t>(actor) << 32) ^ sequence ^
        ((static_cast<uint64_t>(draw) + 1) * 0x9e3779b97f4a7c15ull);
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return static_cast<uint8_t>((x ^ (x >> 31)) % bound);
}
} // namespace mw::battle
