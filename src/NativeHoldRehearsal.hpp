#pragma once
#include "NativeStartHold.hpp"
#include <atomic>
namespace sc6::research {
// One immutable request from the network worker, executed only by the clock
// owner. True means the caller must NOT dispatch the original routine again.
template<class Hold=NativeStartHold<>> class NativeHoldRehearsal {
    Hold hold_;
    unsigned identity_{},target_{},battle_{};
    uint64_t release_at_{};
    unsigned timeout_{1000};
    bool touched_{};
    uintptr_t held_object_{};
public:
    enum State:unsigned {idle,queued,holding,released,rejected,restart_required};
    std::atomic<unsigned> state{},services{};
    unsigned target_battle()const{return battle_;} // owner-thread use after acquiring state
    bool request(unsigned identity,unsigned target,unsigned battle,unsigned timeout=1000){
        if(state.load()!=idle || !target || !battle || timeout<1000 || timeout>10000)return false;
        timeout_=timeout;
        identity_=identity;target_=target;battle_=battle;
        state.store(queued,std::memory_order_release);return true;
    }
    bool run(uintptr_t base,uintptr_t object,unsigned owner,unsigned identity,unsigned frame,
             unsigned last_battle,uint64_t now,bool valid,bool permit_release=true){
        const auto s=state.load(std::memory_order_acquire);
        if(s==idle || s==released || s==rejected)return false;
        if(held_object_ && object!=held_object_)return false;
        if(s==restart_required)return touched_;
        if(!valid || identity!=identity_ || frame>target_){
            state=touched_?restart_required:rejected;return touched_;
        }
        if(s==queued){
            if(frame<target_)return false;
            if(last_battle!=battle_-1 || now>UINT64_MAX-timeout_ ||
                !hold_.bind(base,object,owner,identity_,target_,now,timeout_,true,true,true)){
                state=rejected;return false;
            }
            held_object_=object;release_at_=now+100;state=holding;
        }
        if(!hold_.ready(object,now)){state=touched_?restart_required:rejected;return touched_;}
        if(now>=release_at_ && permit_release){state=released;return false;}
        touched_=true;
        if(!hold_.run(object,now)){state=restart_required;return true;}
        ++services;return true;
    }
};
}
