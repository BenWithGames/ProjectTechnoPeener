#pragma once
#include "NativeClockAccess.hpp"
#include "RoundFightReadiness.hpp"
namespace sc6::research {
// Observations are hints for preparation, never authorization to take ownership.
// The game hook never waits for a reader and performs no allocation.
class NativeClockDiscovery {
public:
    struct Observation {
        uintptr_t base{},object{};DWORD thread{};uint64_t tick{};
        NativeClockAccess<>::State clock{};
        RoundFightReadiness fight{};
        bool valid{};
        unsigned preparation_side(uint32_t round,uint64_t generation)const noexcept {
            if(automatic_side(round,generation))return clock.local_mask;
            return valid && (clock.local_mask==1 || clock.local_mask==2) &&
                fight.intro(round,generation,thread)?clock.local_mask:0;
        }
        unsigned automatic_side(uint32_t round,uint64_t generation)const noexcept {
            return valid && (clock.local_mask==1 || clock.local_mask==2) &&
                fight.permits(round,generation,fight.frame,thread)?clock.local_mask:0;
        }
    };
private:
    friend struct NativeClockDiscoveryTestAccess;
    SRWLOCK lock_=SRWLOCK_INIT;
    Observation latest_{};
public:
    Observation observe(uintptr_t base,uintptr_t object,uint64_t now,const RoundFightReadiness& fight={}){
        Observation next;next.base=base;next.object=object;next.thread=GetCurrentThreadId();next.tick=now;
        next.valid=NativeClockAccess<>::inspect(base,object,next.clock);
        // The evidence must originate on this hook's thread. A UI/control
        // reader may consume it, but cannot fabricate owner-thread readiness.
        if(fight.thread==next.thread)next.fight=fight;
        if(!TryAcquireSRWLockExclusive(&lock_))return next;
        latest_=next;ReleaseSRWLockExclusive(&lock_);
        return next;
    }
    bool read(uint64_t now,Observation& out){
        out={};if(!TryAcquireSRWLockShared(&lock_))return false;
        const auto value=latest_;ReleaseSRWLockShared(&lock_);
        if(!value.valid || now<value.tick || now-value.tick>250)return false;
        out=value;return true;
    }
};
}
