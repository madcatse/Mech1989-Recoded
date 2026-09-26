#include "battle/battle_placement.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace mw::battle {
namespace {

constexpr size_t kScenarioRecordSize = 16;
constexpr size_t kScenarioTileCount = 4;
constexpr size_t kPositionBankBase = 0x03c0;
constexpr size_t kPositionBankStride = 0x00a0;
constexpr size_t kPositionModeStride = 0x20;
constexpr size_t kDedicatedObjectiveBase = 0x0dc0;
constexpr size_t kDedicatedObjectiveStride = 8;
constexpr int kOriginalGridWorldXBase = 0xac80;
constexpr int kOriginalGridWorldZBase = 24000;
constexpr int kOriginalGridCellSize = 0x200;

constexpr std::array<int, 35> kPlayerPlacementModeByMissionSelector{
    4, 4, 4, 4, 4, 4, 4, 0, 2, 3, 3, 3, 3, 3, 4, 0, 3, 2,
    1, 1, 2, 3, 3, 3, 3, 3, 3, 3, 0, 3, 3, 0, 3, 3, 3,
};

constexpr std::array<int, 5> kOpposingPlacementModeByPlayerMode{
    2, 1, 4, 3, 0,
};

std::vector<uint8_t> readFileBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("could not open SNARIO.DAT for battle placement: " + path.string());
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

int32_t readI32Le(const std::vector<uint8_t>& bytes, size_t offset) {
    if (offset + 3u >= bytes.size()) {
        throw std::runtime_error("battle placement coordinate read is outside SNARIO.DAT");
    }
    const uint32_t value =
        static_cast<uint32_t>(bytes[offset]) |
        (static_cast<uint32_t>(bytes[offset + 1u]) << 8u) |
        (static_cast<uint32_t>(bytes[offset + 2u]) << 16u) |
        (static_cast<uint32_t>(bytes[offset + 3u]) << 24u);
    return static_cast<int32_t>(value);
}

double headingRadiansToward(double fromX, double fromZ, double toX, double toZ) {
    const double dx = toX - fromX;
    const double dz = toZ - fromZ;
    if (std::abs(dx) < 0.0001 && std::abs(dz) < 0.0001) {
        return 0.0;
    }
    return std::atan2(dx, dz);
}

int opposingModeForPlayerMode(int playerMode) {
    if (playerMode < 0) {
        return 2;
    }
    if (static_cast<size_t>(playerMode) >= kOpposingPlacementModeByPlayerMode.size()) {
        throw std::runtime_error("battle placement player mode is outside the BTECH opposing-mode table");
    }
    return kOpposingPlacementModeByPlayerMode[static_cast<size_t>(playerMode)];
}

OriginalBattlePlacementPosition decodePosition(
    const std::vector<uint8_t>& snario,
    size_t offset,
    double terrainCellSize) {
    OriginalBattlePlacementPosition position;
    position.originalWorldX = readI32Le(snario, offset);
    position.originalWorldZ = readI32Le(snario, offset + 4u);
    position.gridX = static_cast<double>(position.originalWorldX + kOriginalGridWorldXBase) /
                     static_cast<double>(kOriginalGridCellSize);
    position.gridY = static_cast<double>(kOriginalGridWorldZBase - position.originalWorldZ) /
                     static_cast<double>(kOriginalGridCellSize);
    position.transform.x = position.gridX * terrainCellSize;
    position.transform.y = 0.0;
    position.transform.z = position.gridY * terrainCellSize;
    position.transform.headingRadians = 0.0;
    return position;
}

OriginalBattlePlacementSide decodePlacementSide(
    const std::vector<uint8_t>& snario,
    const std::array<uint8_t, kScenarioRecordSize>& record,
    int side,
    int mode,
    double terrainCellSize) {
    OriginalBattlePlacementSide result;
    result.mode = mode;
    result.selectorOffset = 4 + side * 5 + mode;
    if (result.selectorOffset < 0 || result.selectorOffset >= static_cast<int>(record.size())) {
        throw std::runtime_error("battle placement selector offset is outside SNARIO record");
    }
    result.bankIndexSource = record[static_cast<size_t>(result.selectorOffset)];
    if (result.bankIndexSource >= record.size()) {
        throw std::runtime_error("battle placement bank index source is outside SNARIO record");
    }
    result.bankId = record[result.bankIndexSource];
    const int coordinateOffset =
        static_cast<int>(kPositionBankBase) +
        static_cast<int>(result.bankId) * static_cast<int>(kPositionBankStride) +
        mode * static_cast<int>(kPositionModeStride);
    if (coordinateOffset < 0) {
        throw std::runtime_error("battle placement coordinate block starts before SNARIO.DAT");
    }
    result.coordinateOffset = static_cast<size_t>(coordinateOffset);
    if (result.coordinateOffset + kPositionModeStride > snario.size()) {
        throw std::runtime_error("battle placement coordinate block is outside SNARIO.DAT");
    }

    for (size_t i = 0; i < result.positions.size(); ++i) {
        const size_t offset = result.coordinateOffset + i * 8u;
        result.positions[i] = decodePosition(snario, offset, terrainCellSize);
    }
    return result;
}

OriginalBattleDedicatedObjectivePlacement decodeDedicatedObjective(
    const std::vector<uint8_t>& snario,
    const OriginalBattlePlacementSide& playerSide,
    const OriginalBattlePlacementSide& opposingSide,
    double terrainCellSize) {
    OriginalBattleDedicatedObjectivePlacement result;
    const OriginalBattlePlacementSide* source = nullptr;
    if (playerSide.mode == 4) {
        result.sourceSide = 0;
        source = &playerSide;
    } else if (opposingSide.mode == 4) {
        result.sourceSide = 1;
        source = &opposingSide;
    } else {
        return result;
    }

    result.valid = true;
    result.sourceMode = source->mode;
    result.bankId = source->bankId;
    result.coordinateOffset =
        kDedicatedObjectiveBase + static_cast<size_t>(result.bankId) * kDedicatedObjectiveStride;
    if (result.coordinateOffset + kDedicatedObjectiveStride > snario.size()) {
        throw std::runtime_error("dedicated objective transform is outside SNARIO.DAT");
    }
    result.position = decodePosition(snario, result.coordinateOffset, terrainCellSize);
    return result;
}

} // namespace

OriginalBattleMissionPlacementModes originalBattleMissionPlacementModes(size_t missionSelector) {
    if (missionSelector >= kPlayerPlacementModeByMissionSelector.size()) {
        throw std::runtime_error("battle placement mission selector is outside decoded BTECH table");
    }
    OriginalBattleMissionPlacementModes result;
    result.missionSelector = missionSelector;
    result.playerMode = kPlayerPlacementModeByMissionSelector[missionSelector];
    result.opposingMode = opposingModeForPlayerMode(result.playerMode);
    result.specialObjectDamagePolicyProven = true;
    result.specialObjectDamageSuppressed = missionSelector >= 10u && missionSelector <= 13u;
    result.specialObjectDepletionPolicyProven = true;
    result.specialObjectDepletionSetsPlayerWinCondition = result.playerMode == 3;
    result.specialObjectDepletionSetsPlayerLossCondition = result.opposingMode == 3;
    if (result.specialObjectDepletionSetsPlayerWinCondition ||
        result.specialObjectDepletionSetsPlayerLossCondition) {
        // BTECH resolves simultaneous flags through the player-win flag first.
        result.specialObjectDepletionResultCode =
            result.specialObjectDepletionSetsPlayerWinCondition ? 0 : 1;
    }
    if (result.playerMode == 4) {
        result.dedicatedObjectiveAllocated = true;
        result.dedicatedObjectiveSourceSide = 0;
    } else if (result.opposingMode == 4) {
        result.dedicatedObjectiveAllocated = true;
        result.dedicatedObjectiveSourceSide = 1;
    }
    return result;
}

OriginalBattlePlacement decodeOriginalBattlePlacement(
    const std::filesystem::path& snarioPath,
    size_t scenarioIndex,
    size_t missionSelector,
    double terrainCellSize) {
    if (terrainCellSize <= 0.0) {
        throw std::runtime_error("battle placement terrain cell size must be positive");
    }
    const OriginalBattleMissionPlacementModes modes = originalBattleMissionPlacementModes(missionSelector);

    const std::vector<uint8_t> snario = readFileBytes(snarioPath);
    const size_t recordOffset = scenarioIndex * kScenarioRecordSize;
    if (recordOffset + kScenarioRecordSize > snario.size()) {
        throw std::runtime_error("battle placement scenario index is outside SNARIO.DAT");
    }

    OriginalBattlePlacement result;
    result.scenarioIndex = scenarioIndex;
    result.missionSelector = missionSelector;
    std::copy_n(snario.begin() + static_cast<std::ptrdiff_t>(recordOffset), kScenarioRecordSize, result.scenarioRaw.begin());
    std::copy_n(result.scenarioRaw.begin(), kScenarioTileCount, result.tileIds.begin());

    result.playerSide = decodePlacementSide(snario, result.scenarioRaw, 0, modes.playerMode, terrainCellSize);
    result.opposingSide = decodePlacementSide(snario, result.scenarioRaw, 1, modes.opposingMode, terrainCellSize);
    result.dedicatedObjective =
        decodeDedicatedObjective(snario, result.playerSide, result.opposingSide, terrainCellSize);

    const Transform& player = result.playerSide.positions[0].transform;
    const Transform& opposing0 = result.opposingSide.positions[0].transform;
    const Transform& opposing1 = result.opposingSide.positions[1].transform;
    result.playerHeadingToOpposingSlot0Radians =
        headingRadiansToward(player.x, player.z, opposing0.x, opposing0.z);
    result.playerHeadingToOpposingSlot1Radians =
        headingRadiansToward(player.x, player.z, opposing1.x, opposing1.z);
    return result;
}

} // namespace mw::battle
