#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mw::battle {

// MW_MAIN 101B:0C17. State words are DS:03F5 and DS:03F7, outside GAM.
struct OriginalContractRng {
    uint16_t word3f5 = 0;
    uint16_t word3f7 = 1;
    uint16_t inclusive(uint16_t maximum);
    // 0C0A: BIOS INT1A CX:DX is XORed into the executable's initial words.
    void mixBiosTicks(uint32_t ticks) {
        word3f5 ^= static_cast<uint16_t>(ticks >> 16);
        word3f7 ^= static_cast<uint16_t>(ticks);
    }
};

// MW_MAIN 55A5..55D3: truncate the advance in K-Cbills, then convert.
// Terms come from the original word arrays; advance percentage is 0..100.
struct OriginalContractFinance {
    uint32_t advanceCbills = 0;
    uint16_t balanceK = 0;
};
inline OriginalContractFinance originalContractFinance(uint16_t priceK, uint16_t percent) {
    const uint32_t advanceK = uint32_t{priceK} * percent / 100u;
    return {advanceK * 1000u, static_cast<uint16_t>(priceK - advanceK)};
}

// 55D8..561C; DS4BF0 is ordered Kurita, Steiner, Marik, Liao, Davion.
// Even a full/extended force consumes the one acceptance roll.
inline void originalAcceptedOpposition(std::array<uint8_t,3>& hml,
    uint8_t house, OriginalContractRng& rng) {
    constexpr std::array<uint16_t,5> thresholds{{20,15,40,30,10}};
    const auto roll = rng.inclusive(100);
    if (house >= thresholds.size() || roll > thresholds[house]) return;
    const auto total = static_cast<uint8_t>(hml[0] + hml[1] + hml[2]);
    if (static_cast<int8_t>(total) >= 4) return;
    ++hml[hml[0] ? 0 : hml[1] ? 1 : 2];
}

enum class OriginalDeploymentReadiness { Ready, MissingCommander, NonfunctionalMech };
inline bool originalMechCanDeploy(const std::array<uint8_t,29>& condition) {
    // 1BE8..1C09: sensors, weapons, armor and arm actuators do not block.
    for (size_t field : {0u,1u,3u,7u,8u}) if (condition[field] > 2) return false;
    return true;
}
inline bool originalDeploymentRejectionCancels(uint8_t& failures, bool finalMission) {
    // 6913/1CCC final99 uses the same gate without counting failures.
    if (finalMission) return false;
    failures = static_cast<uint8_t>(failures + 1);
    return failures == 3;
}

// MW_MAIN 1C4E..1C65, signed byte comparison, inclusive R(100)<=15.
inline bool originalUneventfulContract(uint8_t context, OriginalContractRng& rng) {
    return static_cast<int8_t>(context)<=10 && context!=9 && rng.inclusive(100)<=15;
}

struct OriginalContractMission {
    uint16_t descriptor = 0;
    uint8_t id = 0; // executable ID, not the contiguous title/catalog index
    uint8_t missionClass = 0;
    bool extended = false;
};

struct OriginalContractTables {
    std::array<std::array<uint8_t, 8>, 5> categoryClasses{};
    std::array<std::array<std::array<uint8_t, 8>, 8>, 5> directTargets{};
    std::array<uint16_t, 5> targetStarts{};
    std::array<uint16_t, 5> targetMaximums{};
    std::array<uint8_t, 146> targetRatings{};
    std::array<OriginalContractMission, 34> missions{};
    std::array<std::vector<size_t>, 5> classBuckets;
};

OriginalContractTables decodeOriginalContractTables(const std::vector<uint8_t>& exe);

struct ContractCalendarPolicy {
    uint8_t classFourThreshold = 0;
    bool guaranteedOffers = false;
};
ContractCalendarPolicy originalContractCalendarPolicy(int year, int month, int dayCounter);

struct ContractGenerationContext {
    uint8_t employerHouse = 0;
    int year = 3024;
    int month = 0;
    int dayCounter = 0;
    int forceScore = 10;
    uint16_t crewCount = 1;
    uint16_t ownedMechCount = 1;
    int positiveStanding = 0;
    int negativeStanding = 0;
};

struct OriginalContractOffer {
    size_t missionIndex = 0;
    uint16_t targetPlanetIndex = 0; // raw DS:6570 table index, not UI planet index
    uint8_t category = 0;
    uint8_t missionClass = 1;
    std::array<uint16_t, 3> opposition{};
    uint16_t price = 0;
    uint16_t salvage = 0;
    uint16_t advance = 0;
    uint16_t priceCeiling = 0;
    uint16_t salvageCeiling = 0;
    uint16_t advanceCeiling = 0;
};

enum class ContractBoardStatus { Ready, NoOffers, HouseRefused, Invalid };
struct OriginalContractBoard {
    ContractBoardStatus status = ContractBoardStatus::NoOffers;
    uint8_t classFourThreshold = 0;
    std::vector<OriginalContractOffer> offers;
};

OriginalContractBoard generateOriginalContractBoard(
    const OriginalContractTables& tables, const ContractGenerationContext& context,
    OriginalContractRng& rng);

// Only the proven offer arrays and war-threshold byte are changed. Inactive
// slots and every unrelated byte remain untouched; GAM stays exactly 0x718.
void encodeOriginalContractBoard(std::vector<uint8_t>& gam,
    const OriginalContractTables& tables, const OriginalContractBoard& board);
OriginalContractBoard decodeOriginalContractBoard(const std::vector<uint8_t>& gam,
    const OriginalContractTables& tables);

} // namespace mw::battle
