#pragma once
#include "StartupBoundaryPairs.hpp"
namespace sc6::research {
// Network-thread rehearsal only. Session/peer authentication is performed by
// the enclosing transport. No held-clock claim or activation token is issued.
class StartupBoundaryAgreement {
public:
    using Words=std::array<uint32_t,10>;
private:
    Words local_{};bool bound_{},received_{},ack_{},failed_{};
public:
    bool begin(const StartupBoundaryPairs& pairs,unsigned slot){
        if(bound_ || slot<1 || slot>2 || pairs.count.load(std::memory_order_acquire)!=120 ||
            pairs.overwritten.load() || pairs.unpaired.load() || pairs.discontinuities.load())return false;
        const auto& first=pairs.rows[0];
        if(first.clock>UINT32_MAX-600 || first.battle>UINT32_MAX-600)return false;
        local_={1,first.identity,first.clock,first.battle,first.clock+600,first.battle+600,slot,0,120,0};
        bound_=true;return true;
    }
    bool bound()const{return bound_;}
    bool failed()const{return failed_;}
    bool ready()const{return bound_ && received_ && ack_ && !failed_;}
    unsigned target_clock()const{return local_[4];}
    unsigned target_battle()const{return local_[5];}
    Words outgoing()const{auto out=local_;out[7]=received_?1u:0u;return out;}
    bool receive(const Words& peer){
        if(!bound_ || failed_)return false;
        for(unsigned i=0;i<peer.size();++i){
            const bool valid=i==6?peer[i]==3-local_[6]:i==7?peer[i]<=1:peer[i]==local_[i];
            if(!valid){failed_=true;return false;}
        }
        received_=true;if(peer[7])ack_=true;return true;
    }
};
}
