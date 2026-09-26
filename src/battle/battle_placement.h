#pragma once

#include "battle/battle_world.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace mw::battle {

struct OriginalBattlePlacementPosition {
    int32_t originalWorldX = 0;
    int32_t originalWorldZ = 0;
    double gridX = 0.0;
    double gridY = 0.0;
    Transform transform{};
};

struct OriginalBattlePlacementSide {
    int mode = 0;
    int selectorOffset = 0;
    uint8_t bankIndexSource = 0;
    uint8_t bankId = 0;
    size_t coordinateOffset = 0;
    std::array<OriginalBattlePlacementPosition, 4> positions{};
};

struct OriginalBattleDedicatedObjectivePlacement {
    bool valid = false;
    int sourceSide = -1;
    int sourceMode = -1;
    uint8_t bankId = 0;
    size_t coordinateOffset = 0;
    OriginalBattlePlacementPosition position{};
};

struct OriginalBattlePlacement {
    size_t scenarioIndex = 0;
    size_t missionSelector = 0;
    std::array<uint8_t, 16> scenarioRaw{};
    std::array<uint8_t, 4> tileIds{};
    OriginalBattlePlacementSide playerSide{};
    OriginalBattlePlacementSide opposingSide{};
    OriginalBattleDedicatedObjectivePlacement dedicatedObjective{};
    double playerHeadingToOpposingSlot0Radians = 0.0;
    double playerHeadingToOpposingSlot1Radians = 0.0;
};

struct OriginalBattleMissionPlacementModes {
    size_t missionSelector = 0;
    int playerMode = 0;
    int opposingMode = 0;
    bool dedicatedObjectiveAllocated = false;
    int dedicatedObjectiveSourceSide = -1;
    bool specialObjectDamagePolicyProven = false;
    bool specialObjectDamageSuppressed = false;
    bool specialObjectDepletionPolicyProven = false;
    bool specialObjectDepletionSetsPlayerWinCondition = false;
    bool specialObjectDepletionSetsPlayerLossCondition = false;
    int specialObjectDepletionResultCode = -1;
};

OriginalBattleMissionPlacementModes originalBattleMissionPlacementModes(size_t missionSelector);

OriginalBattlePlacement decodeOriginalBattlePlacement(
    const std::filesystem::path& snarioPath,
    size_t scenarioIndex,
    size_t missionSelector,
    double terrainCellSize);

} // namespace mw::battle
