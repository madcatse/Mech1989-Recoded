#include "battle/campaign_repair.h"
#include <algorithm>
#include <stdexcept>
namespace mw::battle {
OriginalRepairTables decodeOriginalRepairTables(const std::vector<uint8_t>& exe) {
    const auto byte = [&](size_t ds) { return exe.at(0x8d00 + ds); };
    const auto word = [&](size_t ds) { return static_cast<uint16_t>(byte(ds) | (byte(ds+1)<<8)); };
    OriginalRepairTables t;
    for (size_t tier=0;tier<4;++tier) t.availabilityThresholds[tier]=byte(0x3a6b+tier);
    for (size_t chassis=0; chassis<10; ++chassis) {
        t.weaponCounts[chassis]=byte(0x2aed+chassis);
        if (t.weaponCounts[chassis]>10) throw std::runtime_error("invalid original repair weapon count");
        t.armor[chassis]=byte(0x3cab+chassis);
        for (size_t component=0; component<10; ++component)
            t.components[component][chassis]=word(0x3b53+component*20+chassis*2);
        const auto pointer=word(0x3c1b+chassis*2);
        for (size_t slot=0; slot<t.weaponCounts[chassis]; ++slot)
            t.weapons[chassis][slot]=word(pointer+slot*2);
    }
    return t;
}
bool originalRepairAvailable(const OriginalRepairTables& t, OriginalRepairSession& session,
    uint16_t chassis, uint8_t tier, size_t field, bool fullRepair) {
    if (chassis>=10 || tier<1 || tier>4 || field>=20)
        throw std::out_of_range("original repair availability context");
    auto& failed=session.unavailable[chassis*22u+field];
    if (fullRepair ? failed==1 : failed!=0) {
        if (!fullRepair) failed=1; // 3F6E also normalizes an already failed byte.
        return false;
    }
    if (session.rng.inclusive(100)<t.availabilityThresholds[tier-1]) return true;
    failed=1;
    return false;
}
bool applyOriginalFullRepair(const OriginalRepairTables& t, OriginalRepairSession& session,
    uint16_t chassis, uint8_t tier, OriginalMechCondition& condition) {
    bool failed=false;
    for (size_t field=0;field<condition.size();++field) {
        if (!condition[field]) continue;
        if (field<20 && !originalRepairAvailable(t,session,chassis,tier,field,true)) failed=true;
        else condition[field]=0;
    }
    return failed;
}
uint32_t originalRepairUnitCost(const OriginalRepairTables& t, uint16_t chassis,
    uint16_t factor, size_t selector, const OriginalMechCondition& condition) {
    if (chassis>=10 || selector>20) throw std::out_of_range("original repair selector/chassis");
    uint16_t base=0;
    if (selector==10) {
        if (std::none_of(condition.begin()+20,condition.end(),[](uint8_t n){return n!=0;})) return 0;
        base=t.armor[chassis];
    } else {
        const size_t field=selector>10 ? selector-1 : selector;
        if (!condition[field]) return 0;
        if (selector<10) base=t.components[selector][chassis];
        else {
            if (selector-11>=t.weaponCounts[chassis]) return 0;
            base=t.weapons[chassis][selector-11];
        }
    }
    // 3E9C..3ECF: unsigned MUL/DIV, then WORD addition, then 32-bit MUL.
    const uint32_t surcharge=uint32_t(base)*factor/100u;
    if (surcharge>65535u) throw std::runtime_error("original repair DIV overflow");
    const auto adjusted=static_cast<uint16_t>(base+surcharge);
    return uint32_t(adjusted)*((selector==4 || selector==9 || selector==10) ? 1000u : 333u);
}
uint32_t originalRepairTotalCost(const OriginalRepairTables& t, uint16_t chassis,
    uint16_t factor, const OriginalMechCondition& condition) {
    if (chassis>=10) throw std::out_of_range("original repair chassis");
    uint32_t total=0;
    for (size_t field=0; field<10u+t.weaponCounts[chassis]; ++field)
        total+=condition[field]*originalRepairUnitCost(t,chassis,factor,field<10 ? field : field+1,condition);
    const auto sections=std::count_if(condition.begin()+20,condition.end(),[](uint8_t n){return n!=0;});
    return total+static_cast<uint32_t>(sections)*originalRepairUnitCost(t,chassis,factor,10,condition);
}
uint16_t originalRepairStepTicks(size_t ownedSlot, size_t selector,
    const OriginalMechCondition& condition) {
    if (ownedSlot>=12 || selector>20) throw std::out_of_range("original repair slot/selector");
    if (selector==10) {
        const auto section=std::find_if(condition.begin()+20,condition.end(),[](uint8_t n){return n!=0;});
        return section==condition.end() ? 0 : *section;
    }
    const auto old=condition[selector>10 ? selector-1 : selector];
    if (!old) return 0;
    // 3EF4 MUL BL; 3F0C writes AL only; PUSH/POP AX preserves AH across RNG.
    return static_cast<uint16_t>(((ownedSlot*29u)&0xff00u)|old);
}
bool applyOriginalRepairStep(OriginalMechCondition& condition, size_t selector) {
    if (selector>20) throw std::out_of_range("original repair selector");
    if (selector==10) {
        const auto section=std::find_if(condition.begin()+20,condition.end(),[](uint8_t n){return n!=0;});
        if (section==condition.end()) return false;
        *section=0;
    } else {
        auto& value=condition[selector>10 ? selector-1 : selector];
        if (!value) return false;
        --value;
    }
    return true;
}
uint16_t generateOriginalServiceFactor(uint8_t tier, OriginalContractRng& rng) {
    if (tier<1 || tier>4) throw std::out_of_range("original repair economy tier");
    return static_cast<uint16_t>(tier*18u+rng.inclusive(7));
}
} // namespace mw::battle
