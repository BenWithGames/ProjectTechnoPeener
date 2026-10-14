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
    unsigned timeout_{1000},minimum_ms_{100};
    bool touched_{};
    uintptr_t held_object_{};
public:
    enum State:unsigned {idle,queued,holding,released,rejected,restart_required};
    enum Reason:unsigned {none,invalid_context,identity_changed,clock_passed,battle_mismatch,time_overflow,bind_failed,neutral_cancel,frontier_invalid,service_failed};
    std::atomic<unsigned> state{},services{},reason{},observed_identity{},observed_clock{},observed_battle{};
    std::atomic<uint64_t> held_ms{};
    uint64_t started_at_{};
    template<class Stream> void write_diagnostics(Stream& out,const std::string& prefix)const{
        out<<prefix<<"reason="<<reason.load()<<'\n'<<prefix<<"observed_identity="<<observed_identity.load()<<'\n'
           <<prefix<<"observed_clock="<<observed_clock.load()<<'\n'<<prefix<<"observed_battle="<<observed_battle.load()<<'\n'
           <<prefix<<"target_identity="<<identity_<<'\n'<<prefix<<"target_clock="<<target_<<'\n'
           <<prefix<<"target_battle="<<battle_<<'\n'<<prefix<<"held_ms="<<held_ms.load()<<'\n';
    }
    unsigned target_battle()const{return battle_;} // owner-thread use after acquiring state
    unsigned target_clock()const{return target_;} // owner-thread use after acquiring state
    bool request(unsigned identity,unsigned target,unsigned battle,unsigned timeout=1000,unsigned minimum_ms=100){
        if(state.load()!=idle || !target || !battle || timeout<1000 || timeout>10000 || minimum_ms>=timeout)return false;
        timeout_=timeout;minimum_ms_=minimum_ms;
        identity_=identity;target_=target;battle_=battle;
        state.store(queued,std::memory_order_release);return true;
    }
    bool run(uintptr_t base,uintptr_t object,unsigned owner,unsigned identity,unsigned frame,
             unsigned last_battle,uint64_t now,bool valid,bool permit_release=true,bool cancel_unmodified=false){
        const auto s=state.load(std::memory_order_acquire);
        if(s==idle || s==released || s==rejected)return false;
        if(held_object_ && object!=held_object_)return false;
        if(s==restart_required)return touched_;
        observed_identity=identity;observed_clock=frame;observed_battle=last_battle;
        if(s==holding && now>=started_at_)held_ms=now-started_at_;
        if(!valid || identity!=identity_ || frame>target_){
            reason=!valid?invalid_context:identity!=identity_?identity_changed:clock_passed;
            state=touched_?restart_required:rejected;return touched_;
        }
        if(s==queued){
            if(frame<target_)return false;
            const unsigned why=last_battle!=battle_-1?battle_mismatch:now>UINT64_MAX-timeout_?time_overflow:
                !hold_.bind(base,object,owner,identity_,target_,now,timeout_,true,true,true)?bind_failed:none;
            if(why){reason=why;
                state=rejected;return false;
            }
            started_at_=now;held_object_=object;release_at_=now+minimum_ms_;state=holding;
        }
        // A rejected input window has not restored or injected game state.
        // Resume the original routine only while the same held frontier is valid.
        if(cancel_unmodified && hold_.ready(object,now)){reason=neutral_cancel;state=rejected;return false;}
        if(!hold_.ready(object,now)){reason=frontier_invalid;state=touched_?restart_required:rejected;return touched_;}
        if(now>=release_at_ && permit_release){state=released;return false;}
        touched_=true;
        if(!hold_.run(object,now)){reason=service_failed;state=restart_required;return true;}
        ++services;return true;
    }
};
}
