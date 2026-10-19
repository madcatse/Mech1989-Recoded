#include "battle/campaign_recruitment.h"
#include <algorithm>
#include <stdexcept>

namespace mw::battle {
namespace {
uint16_t word(const std::vector<uint8_t>& b, size_t p) {
    return static_cast<uint16_t>(b[p] | (b[p+1] << 8));
}
void put(std::vector<uint8_t>& b, size_t p, uint16_t value) {
    b[p] = static_cast<uint8_t>(value); b[p+1] = static_cast<uint8_t>(value >> 8);
}
uint16_t pairKey(uint16_t id) { return static_cast<uint16_t>((id - 1u) & 0xfffeu); }
void validate(const OriginalRecruitmentState& s) {
    for (const auto& p : s.pilots)
        if (p.gunnery > 3 || p.piloting > 3) throw std::runtime_error("invalid recruit skills");
    for (auto id : s.board)
        if (id > kOriginalRecruitCount) throw std::runtime_error("invalid recruit board ID");
}
void validateSize(const std::vector<uint8_t>& gam) {
    if (gam.size() != 0x718) throw std::runtime_error("invalid recruitment GAM size");
}
}

size_t originalRecruitBoardCount(const OriginalRecruitBoard& board) {
    return static_cast<size_t>(std::count_if(board.begin(), board.end(), [](auto id) { return id != 0; }));
}

OriginalRecruitBoard generateOriginalRecruitBoard(
    const std::array<OriginalRecruitPilotState, kOriginalRecruitCount>& pilots,
    uint8_t economyTier, uint8_t reputation, const std::vector<uint16_t>& companions,
    OriginalContractRng& rng, bool* safetyLimitReached) {
    if (safetyLimitReached) *safetyLimitReached = false;
    validate({pilots, {}});
    if (economyTier < 1 || economyTier > 4 || reputation > 3 || companions.size() > 3)
        throw std::runtime_error("invalid recruitment generation context");
    for (auto id : companions)
        if (id == 0 || id > kOriginalRecruitCount) throw std::runtime_error("invalid companion ID");
    OriginalRecruitBoard board{};
    const auto requested = rng.inclusive(static_cast<uint16_t>(9 - 2 * economyTier)) + 1u;
    size_t count = 0;
    uint16_t bp = 0;
    // Safety policy for pathological RNG/context cycles; never substitutes a
    // different draw/selection. BP reuse can otherwise cause extreme runtimes.
    for (size_t draws = 0; draws < 16384; ++draws) {
        const uint16_t index = rng.inclusive(41);
        const uint16_t id = index + 1;
        const auto& pilot = pilots[index];
        bool allowed = pilot.unavailable == 0 &&
            (pilot.gunnery == reputation || pilot.gunnery + 1 == reputation);
        if (allowed) {
            for (auto previous : board)
                if (previous == id || pairKey(previous) == pairKey(id)) { allowed = false; break; }
        }
        if (allowed) {
            bp = static_cast<uint16_t>(companions.size() + 1);
            if (bp != 1) {
                --bp;
                for (auto companion : companions) {
                    if (pairKey(companion) == pairKey(id)) { allowed = false; break; }
                    --bp;
                }
            }
        }
        if (allowed) board[count++] = id;
        if (count == requested) return board;
        if (++bp == 100) return board;
    }
    if (safetyLimitReached) *safetyLimitReached = true;
    return board; // Preserve accepted offers instead of reproducing a DOS hang.
}

void encodeOriginalRecruitment(std::vector<uint8_t>& gam, const OriginalRecruitmentState& state) {
    validateSize(gam); validate(state);
    for (size_t i = 0; i < state.pilots.size(); ++i) {
        gam[0x77+i] = state.pilots[i].unavailable;
        gam[0x5f0+i] = state.pilots[i].gunnery;
        gam[0x61b+i] = state.pilots[i].piloting;
        gam[0x646+i] = state.pilots[i].experience;
    }
    put(gam, 0xd6, static_cast<uint16_t>(originalRecruitBoardCount(state.board)));
    for (size_t i = 0; i < state.board.size(); ++i) put(gam, 0xd8+2*i, state.board[i]);
}

OriginalRecruitmentState decodeOriginalRecruitment(const std::vector<uint8_t>& gam) {
    validateSize(gam);
    OriginalRecruitmentState state;
    for (size_t i = 0; i < state.pilots.size(); ++i)
        state.pilots[i] = {gam[0x5f0+i], gam[0x61b+i], gam[0x646+i], gam[0x77+i]};
    for (size_t i = 0; i < state.board.size(); ++i) state.board[i] = word(gam, 0xd8+2*i);
    validate(state);
    return state;
}

} // namespace mw::battle
