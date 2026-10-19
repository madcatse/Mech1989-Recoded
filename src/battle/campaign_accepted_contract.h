#pragma once
#include "battle/campaign_contracts.h"
#include <algorithm>
#include <optional>
#include <stdexcept>

namespace mw::battle {
// MW_MAIN5542..55D3. These are existing GAM fields, not a new save section.
// Keep the raw copy after settlement; a cleared flag20 makes it inactive.
struct OriginalAcceptedContractState {
    uint8_t missionClass = 0, biome = 0;
    std::array<uint8_t,3> opposition{};
    uint8_t targetTableOrder = 0, missionId = 0, detail = 0;
    uint16_t priceK = 0, salvagePercent = 0, advancePercent = 0, balanceK = 0;
    uint16_t targetCategory = 0;
};
inline std::array<uint8_t,18> originalAcceptedContractBytes(const OriginalAcceptedContractState& s) {
    std::array<uint8_t,18> b{{s.missionClass,s.biome,s.opposition[0],s.opposition[1],s.opposition[2],
        s.targetTableOrder,s.missionId,s.detail}};
    const std::array<uint16_t,5> words{{s.priceK,s.salvagePercent,s.advancePercent,s.balanceK,s.targetCategory}};
    for (size_t i=0;i<words.size();++i) {b[8+2*i]=static_cast<uint8_t>(words[i]);b[9+2*i]=static_cast<uint8_t>(words[i]>>8);}
    return b;
}
inline OriginalAcceptedContractState decodeOriginalAcceptedContract(const std::vector<uint8_t>& gam) {
    const auto word=[&](size_t p){return static_cast<uint16_t>(gam.at(p)|(gam.at(p+1)<<8));};
    return {gam.at(1),gam.at(2),{{gam.at(3),gam.at(4),gam.at(5)}},gam.at(6),gam.at(7),gam.at(8),
        word(0x5b2),word(0x5b4),word(0x5b6),word(0x5b8),word(0x70b)};
}
inline void encodeOriginalAcceptedContract(std::vector<uint8_t>& gam,const OriginalAcceptedContractState& s) {
    const auto b=originalAcceptedContractBytes(s);
    for (size_t i=0;i<8;++i) gam.at(1+i)=b[i];
    for (size_t i=0;i<8;++i) gam.at(0x5b2+i)=b[8+i];
    gam.at(0x70b)=b[16];gam.at(0x70c)=b[17];
}
inline std::optional<OriginalContractOffer> originalAcceptedContractOffer(
    const OriginalAcceptedContractState& s,const OriginalContractTables& tables) {
    const auto mission=std::find_if(tables.missions.begin(),tables.missions.end(),
        [&](const auto& m){return m.id==s.missionId;});
    // Unknown/imported scratch stays byte-preserved, never fabricated into a
    // playable mission. No new constraints on the original H/M/L bytes.
    if (mission==tables.missions.end() || s.missionClass<1 || s.missionClass>4 ||
        s.biome>2 || !s.targetTableOrder || s.targetTableOrder>146 || s.targetCategory>7 ||
        s.salvagePercent>100 || s.advancePercent>100 || s.balanceK>s.priceK) return std::nullopt;
    OriginalContractOffer o;
    o.missionIndex=static_cast<size_t>(mission-tables.missions.begin());
    o.targetPlanetIndex=s.targetTableOrder-1u;o.category=static_cast<uint8_t>(s.targetCategory);
    o.missionClass=s.missionClass;
    std::copy(s.opposition.begin(),s.opposition.end(),o.opposition.begin());
    o.price=o.priceCeiling=s.priceK;o.salvage=o.salvageCeiling=s.salvagePercent;
    o.advance=o.advanceCeiling=s.advancePercent;
    return o;
}
inline OriginalContractFinance restoredAcceptedContractFinance(const OriginalAcceptedContractState& s) {
    if (s.balanceK>s.priceK) throw std::runtime_error("invalid saved contract balance");
    // Intentional original-bug correction: DOS Restore leaves unrelated RAM
    // DS4B73 untouched (zero in a fresh process). The saved balance instead
    // identifies the already credited advance; do not credit it again.
    return {static_cast<uint32_t>(s.priceK-s.balanceK)*1000u,s.balanceK};
}
} // namespace mw::battle
