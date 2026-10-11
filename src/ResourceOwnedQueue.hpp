#pragma once
#include <Windows.h>
#include <array>
#include <climits>
#include <cstdint>
namespace sc6::research {
template<unsigned Capacity=16> class ResourceOwnedQueue {
    SRWLOCK lock_=SRWLOCK_INIT;
    std::array<uintptr_t,Capacity> controls_{};
    unsigned count_{},total_{};bool accepting_{};
public:
    bool open(){AcquireSRWLockExclusive(&lock_);const bool ok=!count_ && !total_;if(ok)accepting_=true;ReleaseSRWLockExclusive(&lock_);return ok;}
    void close(){AcquireSRWLockExclusive(&lock_);accepting_=false;ReleaseSRWLockExclusive(&lock_);}
    unsigned pending(){AcquireSRWLockShared(&lock_);const auto n=count_;ReleaseSRWLockShared(&lock_);return n;}
    unsigned total(){AcquireSRWLockShared(&lock_);const auto n=total_;ReleaseSRWLockShared(&lock_);return n;}
    // Caller owns a native reference. Reserve capacity before touching the count.
    bool push(uintptr_t control){
        if(!control || !TryAcquireSRWLockExclusive(&lock_))return false;
        bool added=false;
        if(accepting_ && total_<Capacity){
            auto strong=reinterpret_cast<volatile LONG*>(control+8);
            LONG observed=InterlockedCompareExchange(strong,0,0);
            for(unsigned i=0;i<16 && observed>0 && observed<LONG_MAX;++i){
                const auto prior=InterlockedCompareExchange(strong,observed+1,observed);
                if(prior==observed){controls_[count_++]=control;++total_;added=true;break;}
                observed=prior;
            }
        }
        ReleaseSRWLockExclusive(&lock_);return added;
    }
    // Ownership transfers to caller; it must release or adopt, never discard.
    uintptr_t pop(){
        if(!TryAcquireSRWLockExclusive(&lock_))return 0;
        const auto value=count_?controls_[--count_]:0;
        ReleaseSRWLockExclusive(&lock_);return value;
    }
};
}
