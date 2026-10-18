#pragma once
#include <array>
#include <cstdint>
namespace sc6::research {
// Network-worker owned. Final results are immutable and scoped by the outer
// authenticated Steam session. Profile 5 explicitly selects controlled correction.
class StartupSuite {
public:
    using Words=std::array<uint32_t,10>;
    unsigned profile{3},seconds{60};
    uint64_t observing{},agreed_at{},last_send{};
    unsigned round_changes{},rematches{},last_round{},last_event{};
    bool began{},sealed{},peer_seen{},peer_ack{};
    Words local{},peer{};
    const char* reason="none";
    bool configure(unsigned p,unsigned s){if(p<1 || p>16 || s>180)return false;profile=p;seconds=s;return true;}
    bool first_match_start()const{return profile==15 || profile==16;}
    unsigned mask()const{return profile>=5 && profile!=11?255u:127u;}
    void observe(uint64_t now,unsigned round,unsigned event){
        if(sealed)return;
        if(!began){began=true;observing=now;last_round=round;last_event=event;return;}
        if(round && last_round && (round!=last_round || event!=last_event)){
            ++round_changes;if(round<last_round)++rematches;
        }
        if(round){last_round=round;last_event=event;}
    }
    bool elapsed(uint64_t now)const{return began && now>=observing && now-observing>=uint64_t(seconds)*1000;}
    bool seal(unsigned passed,unsigned failed,unsigned skipped){
        if(sealed || (passed&failed) || (passed&skipped) || (failed&skipped) ||
            ((passed|failed|skipped)!=mask()))return false;
        local={1,profile,seconds,passed,failed,skipped,0,round_changes,rematches,0};sealed=true;return true;
    }
    Words outgoing()const{auto out=local;out[6]=peer_seen?1u:0u;return out;}
    bool receive(const Words& value){
        if(value[0]!=1 || value[1]!=profile || value[2]!=seconds || value[6]>1 || value[9] ||
            (value[3]&value[4]) || (value[3]&value[5]) || (value[4]&value[5]) ||
            ((value[3]|value[4]|value[5])!=mask()))return false;
        auto fixed=value;fixed[6]=0;
        if(peer_seen && fixed!=peer)return false;
        peer=fixed;peer_seen=true;if(value[6])peer_ack=true;return true;
    }
    bool complete()const{return sealed && peer_seen && peer_ack;}
};
}
