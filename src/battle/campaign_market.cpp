#include "battle/campaign_market.h"

#include <algorithm>
#include <stdexcept>

namespace mw::battle {
namespace {
uint16_t word(const std::vector<uint8_t>& b, size_t p) {
    return static_cast<uint16_t>(b.at(p) | (b.at(p+1) << 8));
}
void put(std::vector<uint8_t>& b, size_t p, uint16_t v) {
    b.at(p) = static_cast<uint8_t>(v); b.at(p+1) = static_cast<uint8_t>(v >> 8);
}
void validateTier(uint8_t tier) {
    if (tier < 1 || tier > 4) throw std::runtime_error("invalid market economy tier");
}
void validate(const OriginalMechMarket& m) {
    for (const auto& s : m.slots)
        if (s.chassis != 0xffff && s.chassis >= 10)
            throw std::runtime_error("invalid market chassis");
    // Inactive bytes and unused ammo descriptors can contain original scratch
    // data. Preserve them; do not validate them as active weapons/conditions.
}
}

size_t OriginalMechMarket::count() const {
    return static_cast<size_t>(std::count_if(slots.begin(),slots.end(),
        [](const auto& s) { return s.chassis != 0xffff; }));
}

OriginalMarketTables decodeOriginalMarketTables(const std::vector<uint8_t>& exe) {
    constexpr size_t ds = 0x8d00;
    OriginalMarketTables t;
    for (size_t i=0; i<3; ++i) {
        t.classMaximums[i]=exe.at(ds+0x3a37+i);
        t.classStarts[i]=exe.at(ds+0x3a3a+i);
        if (t.classStarts[i]+t.classMaximums[i]>=10)
            throw std::runtime_error("invalid market chassis class");
    }
    for (size_t i=0; i<10; ++i) {
        t.weaponCounts[i]=exe.at(ds+0x2aed+i);
        t.jumpJets[i]=exe.at(ds+0x3696+i);
        t.ammoCounts[i]=exe.at(ds+0x2eee+i);
        t.basePrices[i]=word(exe,ds+0x305f+i*2);
        if (t.weaponCounts[i]>10 || t.ammoCounts[i]==0 || t.ammoCounts[i]>3)
            throw std::runtime_error("invalid market equipment count");
        const size_t p=ds+word(exe,ds+0x2f28+i*2);
        for (size_t a=0; a<t.ammoCounts[i]; ++a) {
            t.ammoTypes[i][a]=word(exe,p+a*4);
            t.ammoCapacities[i][a]=word(exe,p+a*4+2);
            if (t.ammoTypes[i][a]>=6 || t.ammoCapacities[i][a]==0)
                throw std::runtime_error("invalid market ammunition table");
        }
    }
    for (size_t i=0; i<4; ++i) {
        t.priceCoefficients[i]=exe.at(ds+0x3057+i);
        if (t.priceCoefficients[i]<2) throw std::runtime_error("invalid market price table");
    }
    return t;
}

OriginalMarketMech generateOriginalStartingMech(const OriginalMarketTables& t, OriginalContractRng& rng) {
    // 18AA -> 3BF7: forced Jenner2, four light component writes; engine/gyro
    // cleared AFTER random ammunition and armor. Repeated fields overwrite.
    OriginalMarketMech s; s.chassis=2;
    for (int i=0;i<4;++i) s.condition.at(rng.inclusive(t.weaponCounts[2]+9))=1;
    s.condition[9] >>= 1;
    for (size_t a=0;a<t.ammoCounts[2];++a) {
        s.ammoTypes[a]=t.ammoTypes[2][a];
        s.ammunition[a]=rng.inclusive(t.ammoCapacities[2][a]);
    }
    const auto writes=rng.inclusive(5)+2;
    for (int i=0;i<writes;++i) {
        const auto section=rng.inclusive(8);
        s.condition[20+section]=static_cast<uint8_t>((rng.inclusive(3)|1)&3);
    }
    s.condition[0]=s.condition[1]=0;
    return s;
}

OriginalMechMarket generateOriginalMechMarket(const OriginalMarketTables& t,
    uint8_t tier, OriginalContractRng& rng, OriginalMechMarket m) {
    validateTier(tier);
    m.saleQuota=static_cast<uint16_t>(rng.inclusive(5-tier)+1);
    m.sales=0;
    for (auto& s : m.slots) { s.chassis=0xffff; s.condition.fill(0); }
    const auto count=rng.inclusive(12/tier);
    std::array<uint16_t,3> scratchTypes{}, scratchAmmo{};
    for (size_t i=0; i<count; ++i) {
        auto& s=m.slots[i];
        const auto light=rng.inclusive(tier), heavy=rng.inclusive(tier), junk=rng.inclusive(tier);
        const auto roll=rng.inclusive(100);
        const size_t c=roll<60 ? 0 : roll<90 ? 1 : 2;
        s.chassis=static_cast<uint16_t>(rng.inclusive(t.classMaximums[c])+t.classStarts[c]);
        if (s.chassis==1) s.chassis=0;
        if (s.chassis==5) s.chassis=3;
        const auto id=s.chassis;
        const auto maxField=static_cast<uint16_t>(t.weaponCounts.at(id)+9);
        for (auto n=junk; n>0; --n) s.condition.at(rng.inclusive(maxField))=3;
        for (auto n=heavy; n>0; --n) s.condition.at(rng.inclusive(maxField))=2;
        for (auto n=light; n>0; --n) s.condition.at(rng.inclusive(maxField))=1;
        s.condition[9]=t.jumpJets.at(id)==0 ? 0 : s.condition[9]>>1;
        for (size_t a=0; a<t.ammoCounts.at(id); ++a) {
            scratchTypes[a]=t.ammoTypes.at(id)[a];
            scratchAmmo[a]=rng.inclusive(t.ammoCapacities.at(id)[a]);
        }
        s.ammoTypes=scratchTypes; s.ammunition=scratchAmmo;
        const auto writes=rng.inclusive(5)+2;
        for (int n=0; n<writes; ++n) {
            const auto section=rng.inclusive(8);
            s.condition[20+section]=static_cast<uint8_t>((rng.inclusive(3)|1)&3);
        }
    }
    return m;
}

OriginalMechMarket decodeOriginalMechMarket(const std::vector<uint8_t>& gam) {
    if (gam.size()!=0x718) throw std::runtime_error("invalid GAM market save size");
    OriginalMechMarket m;
    m.saleQuota=word(gam,0x70e); m.sales=word(gam,0x710);
    for (size_t i=0; i<m.slots.size(); ++i) {
        auto& s=m.slots[i]; s.chassis=word(gam,0x38c+i*2);
        std::copy_n(gam.begin()+0x3a4+i*29,29,s.condition.begin());
        for (size_t a=0; a<3; ++a) {
            s.ammoTypes[a]=word(gam,0x342+a*24+i*2);
            s.ammunition[a]=word(gam,0x2a6+a*24+i*2);
        }
    }
    validate(m);
    if (m.count()!=word(gam,0x38a)) throw std::runtime_error("inconsistent GAM market count");
    return m;
}

void encodeOriginalMechMarket(std::vector<uint8_t>& gam, const OriginalMechMarket& m) {
    if (gam.size()!=0x718) throw std::runtime_error("invalid GAM market save size");
    validate(m); // Fail before writing anything.
    put(gam,0x70e,m.saleQuota); put(gam,0x710,m.sales);
    put(gam,0x38a,static_cast<uint16_t>(m.count()));
    for (size_t i=0; i<m.slots.size(); ++i) {
        const auto& s=m.slots[i]; put(gam,0x38c+i*2,s.chassis);
        std::copy(s.condition.begin(),s.condition.end(),gam.begin()+0x3a4+i*29);
        for (size_t a=0; a<3; ++a) {
            put(gam,0x342+a*24+i*2,s.ammoTypes[a]);
            put(gam,0x2a6+a*24+i*2,s.ammunition[a]);
        }
    }
}

bool originalMarketAcceptsSale(const OriginalMechMarket& m, uint8_t tier) {
    validateTier(tier);
    return tier<3 || m.sales!=m.saleQuota; // Original equality branch, not >=.
}

uint32_t originalMarketPrice(const OriginalMarketTables& t, uint16_t chassis,
    uint8_t tier, uint32_t repairCost, bool buying) {
    validateTier(tier);
    const uint32_t full=uint32_t{t.basePrices.at(chassis)}*100u*
        (t.priceCoefficients[tier-1]-(buying ? 0u : 1u));
    // Fail closed for replacement damage whose repair bill exceeds full value;
    // never reproduce DOS unsigned underflow as a huge positive shop price.
    return full>repairCost ? ((full-repairCost)/1000u)*1000u : 0u;
}

} // namespace mw::battle
