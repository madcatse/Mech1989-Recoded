#pragma once

#include <cstdint>
#include <array>

namespace mw::battle {
// BTECH DS:D385 values. This transient state never enters GAM or its sidecar.
enum class BattlePosture : uint8_t { Upright = 0, Prone = 1, Falling = 4, Rising = 5 };
struct BattlePostureRuntime {
    BattlePosture posture = BattlePosture::Upright;
    uint8_t sequence = 0; // BTECH 4 fall, 5 rise, 7 failed rise
    uint8_t frame = 0;
    uint64_t changedTick = 0;
    uint64_t falls = 0;
    uint64_t standAttempts = 0;
    uint8_t lastStandRoll = 0;
    uint8_t leftLandingDamage = 0;
    uint8_t rightLandingDamage = 0;
};
struct BattlePostureCameraFrame { int16_t forward, height, pitch; };

// Exact decision at unpacked 1000:0BE7. Random draw is R(12), 0..11.
uint8_t originalPlayerStandUpFlag(int posture, int leftLeg, int rightLeg,
                                int leftActuator, int rightActuator, int roll);
bool originalHardLanding(int16_t verticalVelocity);
uint8_t originalLandingLegDamage(uint8_t roll);
// 8895..88BD: generic AI landing is independent of impact velocity and
// requests no damage. Disabled status (-1) or a depleted leg requests a fall.
uint8_t originalAiLandingFlag(int status, int leftLeg, int rightLeg);
// Corrected runtime binding of BTECH 4112's conditional actuator clause.
// Original 707C reads the PLAYER's shared control+51 for every candidate;
// production deliberately uses this actor's posture (original cross-actor bug).
// Partial leg damage, arm actuators, falling4 and AI rising5 do not qualify.
bool battleActuatorLossDisables(BattlePosture posture, int leftActuator, int rightActuator);
BattlePostureCameraFrame originalPostureCameraFrame(uint8_t sequence, uint8_t frame);
void beginBattleFall(BattlePostureRuntime& state, uint64_t tick);
void attemptBattleStandUp(BattlePostureRuntime& state, uint64_t tick,
                         int leftLeg, int rightLeg, int leftActuator, int rightActuator,
                         uint8_t roll);
void advanceBattlePosture(BattlePostureRuntime& state, uint64_t tick);
// Generic actor path (7326/9C06), distinct from the player's cockpit path.
bool originalAiStandUpDue(int posture, int status, int leftLeg, int rightLeg,
                        int leftActuator, int rightActuator, uint64_t tick);
void updateAiBattlePosture(BattlePostureRuntime& state, uint64_t tick,
                          int status, int leftLeg, int rightLeg,
                          int leftActuator, int rightActuator, uint8_t roll);
struct OriginalCollisionFall { bool moverFalls = false; bool otherFalls = false; };
// Unpacked 8D0E: signed WORD product truncation is intentional.
OriginalCollisionFall originalCollisionFall(int massA, int massB,
    int16_t speedA, int16_t speedB, uint16_t headingA, uint16_t headingB,
    int otherStatus);
struct OriginalCollisionDamageCall {
    uint8_t actor; // 0 mover, 1 other; original external armor numbering.
    uint8_t externalArmor;
    int16_t damage;
};
// Exact 8D0E call arguments, including signed overflow and zero calls.
// Audit model only. By user decision, negative collision damage is an original
// bug and will not be applied to contacts (no armor gain or overflow save data).
std::array<OriginalCollisionDamageCall, 6> originalCollisionDamageCalls(
    int massA, int massB, int16_t speedA, int16_t speedB,
    uint16_t headingA, uint16_t headingB);
const char* battlePostureAnimation(const BattlePostureRuntime& state);
// Deterministic replacement stream; original global PRNG sequencing is unproven.
uint8_t battlePostureRandom(uint32_t actor, uint64_t sequence, uint8_t draw, uint8_t bound);
} // namespace mw::battle
