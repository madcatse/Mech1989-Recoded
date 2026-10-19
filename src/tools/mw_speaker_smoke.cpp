#include "audio/pc_speaker.h"
#include "audio/speaker_player.h"
#include "audio/music_player.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>
using namespace mw::audio;
namespace {
size_t checks=0;
void check(bool b,const std::string& why) { ++checks;if(!b) throw std::runtime_error(why); }
std::vector<uint8_t> read(const char* p) { std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error(p);return {std::istreambuf_iterator<char>(f),{}}; }
uint16_t word(const std::vector<uint8_t>& b,size_t p) { return uint16_t(b.at(p)|(b.at(p+1)<<8)); }
void wav(const std::filesystem::path& p,const std::vector<int16_t>& pcm) {
    std::ofstream f(p,std::ios::binary);auto w=[&](uint32_t n,int bytes){while(bytes--){f.put(char(n));n>>=8;}};
    f.write("RIFF",4);w(36+uint32_t(pcm.size()*2),4);f.write("WAVEfmt ",8);w(16,4);w(1,2);w(2,2);w(48000,4);w(192000,4);w(4,2);w(16,2);f.write("data",4);w(uint32_t(pcm.size()*2),4);f.write(reinterpret_cast<const char*>(pcm.data()),pcm.size()*2);
}
}
int main(int argc,char** argv) { try {
    const auto raw=read("Original/BTECHSND.SND");const auto bank=SpeakerBank::decode(raw);
    const auto trace=read("research/fixtures/dosbox_speaker/TRACE.BIN");
    check(trace.size()==16*800*208,"DOS trace size");
    PcSpeaker vm(bank);
    for(int scenario=0;scenario<16;++scenario) {
        // DOS reset clears allocation flags, not old inactive channel register words.
        if(scenario<15) check(vm.play(uint16_t(scenario),10),"start effect");
        for(int tick=0;tick<800;++tick) {
            if(scenario==15 && tick<8) {
                static constexpr uint16_t e[]={1,4,2,3,10,10,7,5},p[]={10,20,30,5,6,20,60,100};
                vm.play(e[tick],p[tick]);
            }
            const auto output=vm.tick();const size_t a=(scenario*800+tick)*208;
            const int selected=word(trace,a)==65535?-1:(int(word(trace,a))-254)/48;
            const std::string label="scenario="+std::to_string(scenario)+" tick="+std::to_string(tick);
            check(output.channel==selected,label+" selection");
            check(output.divisor==word(trace,a+2),label+" divisor");
            check(output.gate==(word(trace,a+4)!=0),label+" gate");
            for(size_t c=0;c<4;++c) {
                const auto& state=vm.channels()[c];
                check(state.active==bool(word(trace,a+6)&(1<<c)),label+" active");
                check(state.priority==word(trace,a+200+2*c),label+" priority");
                for(size_t r=0;r<24;++r) check(state.words[r]==word(trace,a+8+48*c+2*r),label+" channel="+std::to_string(c)+" word="+std::to_string(r));
            }
        }
        // Long-running effects must be stopped between scenarios, as in DOS.
        // Retain registers for the trace comparison; overwrite by the next start.
        if(scenario==0 || scenario==9 || scenario==12) vm.stop();
    }
    for(size_t n=0;n<raw.size();++n) { bool rejected=false;try{SpeakerBank::decode({raw.begin(),raw.begin()+n});}catch(...){rejected=true;}check(rejected,"truncated bank"); }
    auto invalid=raw;invalid[35+60+8]=0xfd;bool rejected=false;try{SpeakerBank::decode(invalid);}catch(...){rejected=true;}check(rejected,"unknown opcode");
    std::filesystem::create_directories("outputs/phase17_speaker/rendered");
    for(uint16_t e=0;e<15;++e) {
        PcSpeaker a(bank),b(bank);a.play(e,10);b.play(e,10);
        std::vector<int16_t> x(48000*4*2),y(x.size());a.render(x.data(),x.size()/2);
        for(size_t pos=0;pos<y.size()/2;) { const size_t n=std::min<size_t>(137,y.size()/2-pos);b.render(y.data()+2*pos,n);pos+=n; }
        check(x==y,"buffer-size independence");
        int peak=0;for(auto sample:x) peak=std::max(peak,std::abs(int(sample)));
        check(peak>50 && peak<32767,"non-silent and unclipped");
        a.stop();std::array<int16_t,960> silence{};a.render(silence.data(),480);
        check(std::all_of(silence.begin(),silence.end(),[](int16_t v){return v==0;}),"immediate stop");
        if(argc>1 && std::string(argv[1])=="--render") wav("outputs/phase17_speaker/rendered/effect-"+std::to_string(e)+".wav",x);
        std::cout<<"effect="<<e<<" frames="<<x.size()/2<<" peak="<<peak<<" block_differences=0\n";
    }
    if(argc>1 && std::string(argv[1])=="--device") {
        SpeakerPlayer player;check(player.initialize("Original"),player.status());
        MusicPlayer music;check(music.initialize(std::filesystem::absolute("Original"),std::filesystem::absolute("build/Release")),music.status());
        music.play(MusicTrack::Bar);
        player.play(11,1);std::this_thread::sleep_for(std::chrono::milliseconds(100));
        player.play(8,15);std::this_thread::sleep_for(std::chrono::milliseconds(120));
        player.play(1,10);player.play(7,40);std::this_thread::sleep_for(std::chrono::milliseconds(550));
        player.play(10,60);std::this_thread::sleep_for(std::chrono::milliseconds(120));player.stop();
        check(player.stopped() && player.requestCount()==5,"device steps/incoming/damage/preempt/stop");
        check(player.status().find("PC Speaker: ")==0,"speaker worker error");
        check(music.status().find("Munt ")==0,"music worker error");
        music.play(MusicTrack::None);
    }
    std::cout<<"mw_speaker_smoke: ok "<<checks<<" checks;12800 DOS callback rows\n";return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; } }
