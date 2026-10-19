#include "audio/pc_speaker.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace mw::audio {
namespace {
uint16_t word(const std::vector<uint8_t>& b, size_t p) {
    if (p + 2 > b.size()) throw std::runtime_error("Truncated PC Speaker bank");
    return static_cast<uint16_t>(b[p] | (b[p+1] << 8));
}
uint32_t dword(const std::vector<uint8_t>& b, size_t p) { return word(b,p) | (uint32_t(word(b,p+2))<<16); }
bool tag(const std::vector<uint8_t>& b,size_t p,const char* s) {
    return p+4<=b.size() && std::equal(s,s+4,b.begin()+p);
}
bool opcode(uint8_t b) {
    switch(b) { case 0x82:case 0x84:case 0x86:case 0x8a:case 0x9c:case 0x9e:case 0xa0:case 0xa2:case 0xae:case 0xfe:return true; default:return false; }
}
}
SpeakerBank SpeakerBank::load(const std::filesystem::path& p) {
    std::ifstream f(p,std::ios::binary);
    if(!f) throw std::runtime_error("Cannot open BTECHSND.SND");
    return decode(std::vector<uint8_t>(std::istreambuf_iterator<char>(f),{}));
}
SpeakerBank SpeakerBank::decode(const std::vector<uint8_t>& b) {
    if(b.size()<35 || !tag(b,0,"SND:") || dword(b,4)!=(0x80000000u|uint32_t(b.size()-8)) ||
       !tag(b,8,"INF:") || dword(b,12)!=6 || word(b,18)!=15 || !tag(b,22,"IBM:") ||
       dword(b,26)!=b.size()-30 || b[30]!=0 || dword(b,31)!=b.size()-35)
        throw std::runtime_error("Unsupported PC Speaker container");
    SpeakerBank result;result.bytes.assign(b.begin()+35,b.end());
    auto& data=result.bytes;
    for(size_t i=0;i<15;++i) {
        const auto begin=dword(data,4*i),end=i+1<15?dword(data,4*i+4):uint32_t(data.size());
        if(begin<60 || begin>=end || end>data.size() || end>65535 || end-begin<11 ||
           word(data,begin)!=0 || word(data,begin+2)!=end-begin)
            throw std::runtime_error("Invalid PC Speaker program boundary");
        result.offsets[i]=static_cast<uint16_t>(begin);
        for(size_t p=begin+8;p<end;p+=3) {
            if(p+3>end || !opcode(data[p])) throw std::runtime_error("Invalid PC Speaker command");
            const auto v=word(data,p+1);
            if(data[p]==0xae && v>1) throw std::runtime_error("Unsupported PC Speaker waveform");
            if(data[p]==0xfe) {
                const int target=int(p)+3+int16_t(v);
                if(target<int(begin+8) || target>=int(end) || (target-int(begin+8))%3)
                    throw std::runtime_error("Invalid PC Speaker branch");
            }
            if(data[p]==0x82 && v==0) break; // Program14 has103 unexecuted bytes.
        }
    }
    return result;
}
PcSpeaker::PcSpeaker(SpeakerBank bank):bank_(std::move(bank)) {}
bool PcSpeaker::play(uint16_t effect,uint16_t priority) {
    if(effect>=bank_.offsets.size()) return false;
    size_t chosen=0;
    while(chosen<4 && channels_[chosen].priority!=0) ++chosen;
    if(chosen==4) {
        // Original0AAC scans0..2 on replacement (last slot excluded), ties later.
        chosen=0;
        for(size_t i=0;i<3;++i) if(channels_[i].priority<=channels_[chosen].priority) chosen=i;
    }
    if(channels_[chosen].priority>priority) return false;
    auto& c=channels_[chosen];c={};c.active=true;c.priority=priority;
    c.words[0]=bank_.offsets[effect]+8;c.words[1]=1;
    return true;
}
void PcSpeaker::stop() {
    channels_={};output_={};clock_=0;firstSample_=true;
    phase_=previousInput_=highPass_=lowPass_=0;
}
void PcSpeaker::advance(SpeakerChannel& c) {
    auto& r=c.words;
    if(--r[1]==0 && r[0]!=0) {
        uint16_t pc=r[0];unsigned budget=128;
        while(budget--) {
            if(size_t(pc)+3>bank_.bytes.size()) { c.active=false;c.priority=0;break; }
            const uint8_t op=bank_.bytes[pc];const auto value=word(bank_.bytes,pc+1);pc+=3;
            if(op==0xfe) {
                if(r[16]==0 || --r[16]!=0) pc=static_cast<uint16_t>(pc+int16_t(value));
            } else if(op==0xae) {
                // BTECHSND lacks ITM. Active DOSBox capture: null-handle table0
                // points to492 zero bytes; table1 to seven IVT bytes. Freeze this
                // observed profile instead of reading undefined host memory.
                r[23]=value==0?8010:244;r[21]=value==0?7860:112;
            } else if(opcode(op)) {
                r[(op&0x7e)/2]=value;
                if(op==0x82) {
                    if(value==0) { c.active=false;c.priority=0; }
                    break;
                }
            } else { c.active=false;c.priority=0;break; }
        }
        if(budget==unsigned(-1)) { c.active=false;c.priority=0; }
        r[0]=pc;
    }
    r[5]=static_cast<uint16_t>(r[5]+r[6]);r[2]=static_cast<uint16_t>(r[2]+r[3]);
    uint16_t phase=static_cast<uint16_t>(r[13]+r[14]);int modulation=0;
    if(phase!=0) {
        if(phase>=r[21]) phase=static_cast<uint16_t>(phase-r[21]);
        r[13]=phase;
        static constexpr int8_t wave1[]={96,16,0,-16,96,16,0};
        const int index=int16_t(phase)>>4;
        const int sample=r[23]==244 && index>=0 && index<7?wave1[index]:0;
        modulation=(sample*256*int16_t(r[15]))>>16;
    }
    r[4]=static_cast<uint16_t>(r[2]+modulation);
}
SpeakerOutput PcSpeaker::tick() {
    int selected=-1;uint16_t priority=0;
    for(int i=3;i>=0;--i) if(channels_[i].active) {
        if(channels_[i].priority>=priority) { priority=channels_[i].priority;selected=i; }
        advance(channels_[i]);
    }
    output_={};output_.channel=selected;
    if(selected>=0) { output_.divisor=channels_[selected].words[4];output_.gate=channels_[selected].words[5]!=0; }
    return output_;
}
void PcSpeaker::render(int16_t* out,size_t frames,uint32_t rate) {
    if(rate<8000 || rate>192000) throw std::runtime_error("Unsupported speaker sample rate");
    const uint64_t interval=uint64_t(rate)*kSpeakerCallbackDivisor;
    const double hp=std::exp(-2*3.141592653589793*20/rate),lp=1-std::exp(-2*3.141592653589793*5000/rate);
    for(size_t i=0;i<frames;++i) {
        if(firstSample_) { tick();firstSample_=false; }
        while(clock_>=interval) { clock_-=interval;tick(); }
        const double divisor=output_.divisor?output_.divisor:65536;
        const double step=double(kSpeakerPitHz)/(rate*divisor);
        auto integral=[](double p) { const double f=p-std::floor(p);return f<=.5?f:1-f; };
        const double input=output_.gate?6000*(integral(phase_+step)-integral(phase_))/step:0;
        phase_+=step;phase_-=std::floor(phase_);
        highPass_=hp*(highPass_+input-previousInput_);previousInput_=input;
        lowPass_+=lp*(highPass_-lowPass_);
        const auto sample=static_cast<int16_t>(std::clamp(std::lround(lowPass_),-32768L,32767L));
        out[i*2]=out[i*2+1]=sample;clock_+=kSpeakerPitHz;
    }
}
}
