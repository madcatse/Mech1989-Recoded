#pragma once

#include "battle/campaign_contracts.h"
#include <array>
#include <cstdint>
#include <vector>

namespace mw::battle {

constexpr size_t kOriginalRecruitCount = 42;
using OriginalRecruitBoard = std::array<uint16_t, 8>; // one-based IDs; zero = empty
struct OriginalRecruitPilotState {
    uint8_t gunnery = 0;
    uint8_t piloting = 0;
    uint8_t experience = 0;
    uint8_t unavailable = 0;
};
struct OriginalRecruitmentState {
    std::array<OriginalRecruitPilotState, kOriginalRecruitCount> pilots{};
    OriginalRecruitBoard board{};
};

// MW_MAIN 101B:6C0C, including BP's reuse in the crew portrait loop.
OriginalRecruitBoard generateOriginalRecruitBoard(
    const std::array<OriginalRecruitPilotState, kOriginalRecruitCount>& pilots,
    uint8_t economyTier, uint8_t reputation, const std::vector<uint16_t>& companions,
    OriginalContractRng& rng, bool* safetyLimitReached = nullptr);
size_t originalRecruitBoardCount(const OriginalRecruitBoard& board);
// Only proven fields are touched; index-zero bytes and unrelated GAM bytes survive.
void encodeOriginalRecruitment(std::vector<uint8_t>& gam, const OriginalRecruitmentState& state);
// IDs/skills are validated before use. Count is derived from slots, repairing old
// replacement saves with a stale count without generating any new candidates.
OriginalRecruitmentState decodeOriginalRecruitment(const std::vector<uint8_t>& gam);

} // namespace mw::battle
