#include "battle/battle_ai_jump.h"
#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace mw::battle {
namespace {
int16_t word(int v) { const int w=v&65535; return static_cast<int16_t>(w>=32768?w-65536:w); }
}
bool originalAiDestinationJumpDue(int installed,int missing,int status,
    int gyro,int posture,int fuel,int32_t distance) {
    return installed!=missing && status>0 && gyro<2 && posture!=3 && fuel>500 && distance>5000;
}
bool originalAiObstacleJumpDue(bool blocked,int reversing,int mode,
    int installed,int playerHeatShutdown,int posture,int fuel,int32_t distance) {
    return (blocked || reversing==1) && mode!=2 && mode!=6 && installed!=0 &&
        playerHeatShutdown==0 && posture!=3 && fuel>500 && distance>2000;
}
OriginalAiFlightOutput originalAiFlightControl(const OriginalAiFlightInput& i) {
    if(i.acceleration<=0) throw std::invalid_argument("AI flight requires original jump acceleration");
    OriginalAiFlightOutput o;
    o.vertical=i.vertical;o.forward=i.forward;o.fuel=i.fuel;
    const int delta=word(i.desiredHeading-i.heading);
    const int absolute=delta==-32768?32767:std::abs(delta);
    o.turn=static_cast<int16_t>(std::min(absolute,364)*(delta>0?1:-1));
    // Arithmetic shift floors negatives; the subsequent division truncates.
    const int32_t shifted=i.height>=0?i.height/32:-static_cast<int32_t>((-static_cast<int64_t>(i.height)+31)/32);
    const int16_t reserve=word(shifted/i.acceleration+100);
    const bool disabled=i.status==-1 || !i.leftLeg || !i.rightLeg || i.installed==i.missing;
    if(i.movementMode==2 || i.fuel<reserve || disabled) {
        int16_t target=0;
        if(i.terrainProbe==2) {
            const int16_t next=word(o.forward+2);
            if(next<i.speedCap) {o.forward=next;o.fuel=word(o.fuel-2);}
        } else { target=word(-(i.speedCap<<4));o.forward=0; }
        if(o.vertical<target)o.vertical=word(o.vertical+i.acceleration);
    } else {
        const int16_t next=word(o.forward+2);
        if(i.height>0 && next<i.speedCap) {o.forward=next;o.fuel=word(o.fuel-2);o.heat+=8;}
        const int16_t vertical=word(o.vertical+i.acceleration);
        if(i.height<4000 && vertical<i.speedCap) {o.vertical=vertical;o.fuel=word(o.fuel-2);o.heat+=12;}
    }
    o.fuel=std::max<int16_t>(0,o.fuel);
    return o;
}
void originalAiFlightDrag(int16_t& vertical,int16_t& forward,int16_t& turn) {
    const int absolute=turn==-32768?32767:std::abs(static_cast<int>(turn));
    if(absolute<128)turn=0;
    else turn=word(turn+(turn<0?128:-128));
    forward=word(forward+(forward<0?1:forward>0?-1:0));
    vertical=word(vertical-3);
}
int16_t originalAiJumpRecharge(int16_t fuel,bool airborne) {
    return !airborne && fuel<800 && word(fuel+2)<=800?word(fuel+2):fuel;
}
} // namespace mw::battle
