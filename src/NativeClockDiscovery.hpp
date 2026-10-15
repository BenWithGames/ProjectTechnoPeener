#pragma once
#include "NativeClockAccess.hpp"
namespace sc6::research {
// Observations are hints for preparation, never authorization to take ownership.
// The game hook never waits for a reader and performs no allocation.
class NativeClockDiscovery {
public:
    struct Observation {
        uintptr_t base{},object{};DWORD thread{};uint64_t tick{};
        NativeClockAccess<>::State clock{};
        bool valid{};
    };
private:
    friend struct NativeClockDiscoveryTestAccess;
    SRWLOCK lock_=SRWLOCK_INIT;
    Observation latest_{};
public:
    Observation observe(uintptr_t base,uintptr_t object,uint64_t now){
        Observation next;next.base=base;next.object=object;next.thread=GetCurrentThreadId();next.tick=now;
        next.valid=NativeClockAccess<>::inspect(base,object,next.clock);
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
