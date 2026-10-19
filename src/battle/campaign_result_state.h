#pragma once
#include "battle/campaign_salvage.h"
#include <algorithm>

namespace mw::battle {
struct OriginalDebriefTables {
    std::array<uint8_t,10> components{},weapons{},denominators{};
};
inline OriginalDebriefTables decodeOriginalDebriefTables(const std::vector<uint8_t>& exe) {
    OriginalDebriefTables t;
    for(size_t i=0;i<10;++i) {
        t.components[i]=exe.at(0x8d00+0x2ae3+i);
        t.weapons[i]=exe.at(0x8d00+0x2aed+i);
        t.denominators[i]=exe.at(0x8d00+0x2b18+i);
    }
    return t;
}
inline int campaignDebriefMechConditionPercent(const OriginalDebriefTables& t,
    uint16_t chassis,const std::array<uint8_t,29>& condition) {
    // MW_MAIN1E1A..1E6C: component + installed-weapon + nine armor grades,
    // divided by the chassis-specific maximum; this is not armor points.
    // Correct its crewSlot*2 count-table index to chassis. Otherwise changing
    // pilots changes which damaged components/weapons are counted.
    unsigned damage=0;
    for(size_t i=0;i<t.components.at(chassis);++i) damage+=condition.at(i);
    for(size_t i=0;i<t.weapons.at(chassis);++i) damage+=condition.at(10+i);
    for(size_t i=20;i<29;++i) damage+=condition[i];
    const unsigned maximum=t.denominators.at(chassis);
    if(!maximum) return 0;
    return static_cast<int>(100u*(maximum-std::min(damage,maximum))/maximum);
}
// Existing GAM09..20, also the BTECH RAM return packet. Unknown/imported
// values survive a no-action Save; decoding does not settle a battle again.
struct OriginalCampaignResultState {
    std::array<uint8_t,4> crewStatus{};
    OriginalSalvageRecords salvage{};
    uint8_t defeatedOpponents = 0;
    uint8_t defeatedHighByte = 0; // MW_MAIN200C reads a word; launch clears it.
    uint8_t result = 0;
    uint8_t accepted = 0;
};

inline std::array<uint8_t,24> originalCampaignResultBytes(const OriginalCampaignResultState& s) {
    std::array<uint8_t,24> bytes{};
    std::copy(s.crewStatus.begin(),s.crewStatus.end(),bytes.begin());
    for (size_t i=0;i<4;++i) {
        const auto& r=s.salvage[i];
        bytes[4+i*4]=static_cast<uint8_t>(r.chassis);
        bytes[5+i*4]=static_cast<uint8_t>(r.chassis>>8);
        bytes[6+i*4]=static_cast<uint8_t>(r.condition);
        bytes[7+i*4]=static_cast<uint8_t>(r.condition>>8);
    }
    bytes[20]=s.defeatedOpponents; bytes[21]=s.defeatedHighByte;
    bytes[22]=s.result; bytes[23]=s.accepted;
    return bytes;
}

inline OriginalCampaignResultState decodeOriginalCampaignResult(const std::vector<uint8_t>& gam) {
    OriginalCampaignResultState s;
    for (size_t i=0;i<4;++i) {
        s.crewStatus[i]=gam.at(9+i);
        s.salvage[i].chassis=static_cast<uint16_t>(gam.at(13+4*i)|(gam.at(14+4*i)<<8));
        s.salvage[i].condition=static_cast<uint16_t>(gam.at(15+4*i)|(gam.at(16+4*i)<<8));
    }
    s.defeatedOpponents=gam.at(29); s.defeatedHighByte=gam.at(30);
    s.result=gam.at(31); s.accepted=gam.at(32);
    return s;
}

inline void encodeOriginalCampaignResult(std::vector<uint8_t>& gam,const OriginalCampaignResultState& s) {
    const auto bytes=originalCampaignResultBytes(s);
    for (size_t i=0;i<bytes.size();++i) gam.at(9+i)=bytes[i];
}

inline void clearOriginalCampaignResultForLaunch(OriginalCampaignResultState& s) {
    // MW_MAIN1C41 clears exactly23 bytes. Accepted flag20 is outside the fill.
    s.crewStatus.fill(0);
    for (auto& r:s.salvage) r={0,0};
    s.defeatedOpponents=s.defeatedHighByte=s.result=0;
}

inline void compactOriginalCampaignResultCrew(OriginalCampaignResultState& s,size_t count) {
    // MW_MAIN1FC3..200C and223F: remove from the front and shift through slot3.
    // The last byte is deliberately retained, including in inactive slots.
    if (s.crewStatus[0]==1) return;
    count=std::min(count,s.crewStatus.size());
    for (size_t slot=1;slot<count; ) {
        if (!s.crewStatus[slot]) {++slot;continue;}
        --count;
        for (size_t j=slot;j+1<s.crewStatus.size();++j) s.crewStatus[j]=s.crewStatus[j+1];
    }
}
} // namespace mw::battle
