#include "battle/campaign_salvage.h"
#include <algorithm>
#include <stdexcept>

namespace mw::battle {
static bool defeatedOpponent(const CombatantSnapshot& actor) {
    if (actor.roster.team!=BattleTeam::Opposing || !actor.roster.originalLiveObjectSlot ||
        *actor.roster.originalLiveObjectSlot<4 || *actor.roster.originalLiveObjectSlot>7 ||
        actor.missionStatus==CombatantMissionStatus::Escaped || !actor.detailedDamage.valid) return false;
    const auto& d=actor.detailedDamage;
    return d.criticalComponents[0].condition>=3 || d.criticalComponents[3].condition>=2 ||
        d.originalCockpitCriticalFlag || d.internalSections[2].structureRemaining==0 ||
        d.internalSections[3].structureRemaining==0 ||
        battleActuatorLossDisables(actor.posture.posture,
            d.criticalComponents[7].workingCount,d.criticalComponents[8].workingCount);
}
uint8_t campaignDefeatedOpponents(const BattleSnapshot& snapshot) {
    std::array<bool,4> lost{};
    for (const auto& actor:snapshot.combatants)
        if (defeatedOpponent(actor)) lost[*actor.roster.originalLiveObjectSlot-4]=true;
    return static_cast<uint8_t>(std::count(lost.begin(),lost.end(),true));
}
OriginalSalvageTables decodeOriginalSalvageTables(const std::vector<uint8_t>& exe) {
    OriginalSalvageTables t; t.market=decodeOriginalMarketTables(exe);
    const auto word=[&](size_t p) {return static_cast<uint16_t>(exe.at(0x8d00+p)|(exe.at(0x8d01+p)<<8));};
    for (size_t i=0;i<10;++i) t.scrapTons[i]=word(0x122b+2*i);
    for (size_t i=0;i<5;++i) {t.employerThresholds[i]=word(0x1241+2*i);t.employerPercents[i]=word(0x124b+2*i);}
    return t;
}
uint16_t originalSalvageCondition(int e,int s,int c,int g,int working,int installed,int points,int maximum) {
    if (e<0 || e>3 || s<0 || s>2 || c<0 || c>1 || g<0 || g>2 ||
        installed<=0 || installed>10 || working<0 || working>installed ||
        maximum<=0 || maximum>1638 || points<0 || points>maximum)
        throw std::runtime_error("invalid original salvage condition domain");
    return static_cast<uint16_t>((3-e)*30/3+(2-s)*5/2+(1-c)*30+(2-g)*10/2+
        working*5/installed+points*20/maximum);
}
void appendOriginalSalvage(OriginalSalvageRecords& records,
    const std::array<OriginalSalvageCandidate,4>& candidates,int result,int mode) {
    if (result!=0 || mode==1) return;
    size_t count=0;while(count<records.size() && records[count].chassis!=0xffff) ++count;
    for (const auto& candidate:candidates) {
        if (count==records.size()) break;
        if (candidate.occupied && candidate.aggregate==-1) records[count++]=candidate.record;
    }
}
uint16_t originalEmployerPayoutPercent(const OriginalSalvageTables& t,uint8_t house,uint16_t roll,bool finalMission) {
    if (roll>100) throw std::runtime_error("invalid original employer roll");
    return finalMission || roll>t.employerThresholds.at(house)?100:t.employerPercents.at(house);
}
uint64_t originalSalvageAward(const OriginalSalvageTables& t,const OriginalSalvageRecords& records,
    uint8_t tier,uint16_t share,uint16_t employer,int result,bool finalMission,bool correctAccumulatorBug) {
    if (tier<1 || tier>4 || share>100 || employer>100) throw std::runtime_error("invalid salvage terms");
    if (finalMission || result!=0 || share==0) return 0;
    uint32_t sumK=0;
    for (const auto& r:records) {
        if (r.chassis==0xffff) continue;
        if (r.condition>100) throw std::runtime_error("invalid salvage record condition");
        const uint32_t valueK=r.condition<=35 ? (t.scrapTons.at(r.chassis)&255u)*10u :
            ((t.market.priceCoefficients[tier-1]-1u)*r.condition*t.market.basePrices.at(r.chassis)+500u)/1000u;
        if (r.condition>35 && !correctAccumulatorBug) sumK=tier-1u; // original DI clobber
        sumK=static_cast<uint16_t>(sumK+valueK);
    }
    return uint64_t{(sumK*share/100u)*employer/100u}*1000u;
}
void appendCampaignSalvage(OriginalSalvageRecords& records,const BattleSnapshot& snapshot,bool victory) {
    if (!victory || !snapshot.setup.valid) return;
    std::array<OriginalSalvageCandidate,4> candidates{};
    constexpr std::array<const char*,8> presets{{"locust","jenner","phoenix_hawk","shadow_hawk","rifleman","warhammer","marauder","battlemaster"}};
    constexpr std::array<uint16_t,8> chassis{{0,2,3,4,6,7,8,9}}; // BTECH DS09CC.
    for (const auto& actor:snapshot.combatants) {
        if (actor.roster.team!=BattleTeam::Opposing || !actor.roster.originalLiveObjectSlot ||
            *actor.roster.originalLiveObjectSlot<4 || *actor.roster.originalLiveObjectSlot>7 ||
            actor.missionStatus==CombatantMissionStatus::Escaped || !actor.detailedDamage.valid) continue;
        const auto model=std::find(presets.begin(),presets.end(),actor.mechPresetId);
        if (model==presets.end()) continue;
        const auto& d=actor.detailedDamage;
        // Five unconditional 4112 failures plus its conditional actuator rule.
        // Deliberately fix the original shared-player-posture bug by binding
        // the clause to this candidate; see PHASE15_ACTUATOR_SALVAGE_EVIDENCE.
        if (!defeatedOpponent(actor)) continue;
        int points=0,maximum=0,working=0;
        for (const auto& a:d.armorSections) {points+=a.armorRemaining;maximum+=a.armorMaximum;}
        for (const auto& a:d.internalSections) {points+=a.structureRemaining;maximum+=a.structureMaximum;}
        const auto mounts=mech3d::catalogWeaponMounts(actor.mechPresetId);
        for (const auto& mount:mounts) working+=d.installedWeapons.at(mount.slotIndex).functional?1:0;
        auto& candidate=candidates[*actor.roster.originalLiveObjectSlot-4];
        candidate.occupied=true;candidate.aggregate=-1;
        candidate.record.chassis=chassis[static_cast<size_t>(model-presets.begin())];
        // Old exact companions may contain condition3 outside the source's
        // sensor/gyro domain. Match the existing coarse-return clamp policy.
        candidate.record.condition=originalSalvageCondition(std::min<int>(3,d.criticalComponents[0].condition),
            std::min<int>(2,d.criticalComponents[2].condition),d.originalCockpitCriticalFlag?1:0,
            std::min<int>(2,d.criticalComponents[1].condition),working,static_cast<int>(mounts.size()),points,maximum);
    }
    appendOriginalSalvage(records,candidates,0,snapshot.setup.playerMode);
}
} // namespace mw::battle
