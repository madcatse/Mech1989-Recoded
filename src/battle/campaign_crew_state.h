#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mw::battle {
// MW_MAIN223F/29E3 shift every plane through slot3, preserving the stale tail.
// 0502 (GAM00BA) has no proved gameplay interpretation; do not name it a skill.
struct OriginalCrewGamState {
    enum Plane : size_t { Pilot, WageRank, Gunnery, Piloting, Opaque, Assignment, Portrait };
    std::array<std::array<uint16_t,4>,7> words{};
    uint16_t dropshipMissedPayments=0;
    uint16_t unpaidCrewPayroll=0;
};
inline constexpr std::array<size_t,7> originalCrewGamOffsets{{0x6f,0xa2,0xaa,0xb2,0xba,0xc2,0xca}};
inline OriginalCrewGamState decodeOriginalCrewGam(const std::vector<uint8_t>& b) {
    const auto word=[&](size_t p){return static_cast<uint16_t>(b.at(p)|(b.at(p+1)<<8));};
    OriginalCrewGamState s;
    for(size_t plane=0;plane<7;++plane) for(size_t slot=0;slot<4;++slot)
        s.words[plane][slot]=word(originalCrewGamOffsets[plane]+2*slot);
    s.dropshipMissedPayments=word(0xd2);s.unpaidCrewPayroll=word(0xd4);return s;
}
inline void encodeOriginalCrewGam(std::vector<uint8_t>& b,const OriginalCrewGamState& s) {
    const auto word=[&](size_t p,uint16_t v){b.at(p)=static_cast<uint8_t>(v);b.at(p+1)=static_cast<uint8_t>(v>>8);};
    for(size_t plane=0;plane<7;++plane) for(size_t slot=0;slot<4;++slot)
        word(originalCrewGamOffsets[plane]+2*slot,s.words[plane][slot]);
    word(0xd2,s.dropshipMissedPayments);word(0xd4,s.unpaidCrewPayroll);
}
inline void removeOriginalCrewGamSlot(OriginalCrewGamState& s,size_t slot) {
    if(slot==0 || slot>=4)return;
    for(auto& plane:s.words) for(size_t i=slot;i<3;++i)plane[i]=plane[i+1];
    s.words[OriginalCrewGamState::Assignment][3]=0xffff;
}
enum class CrewPayrollNotice : uint8_t { None, Warning, Departed };
inline CrewPayrollNotice applyOriginalCrewPayroll(uint64_t& wealth,uint16_t& unpaid,
    uint64_t monthlyWages,uint32_t elapsedMonths) {
    if(!elapsedMonths || !monthlyWages)return CrewPayrollNotice::None;
    // Use the full bill: DOS discards MUL's high word for very long journeys.
    // This overflow is an intentional bug correction, not persisted arithmetic.
    const uint64_t bill=monthlyWages*elapsedMonths;
    if(wealth>=bill){wealth-=bill;return CrewPayrollNotice::None;}
    // Original leaves money unchanged and retains an old unpaid marker even
    // after a later successful payment. It is not a consecutive-month counter.
    if(elapsedMonths==1 && !unpaid){unpaid=static_cast<uint16_t>(monthlyWages);return CrewPayrollNotice::Warning;}
    unpaid=0;return CrewPayrollNotice::Departed;
}
} // namespace mw::battle
