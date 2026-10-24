#pragma once
#include "CombatStartAgreement.hpp"
#include "CombatStartSync.hpp"
#include <Windows.h>
#include <atomic>
namespace sc6::research {
// One network producer, one game-thread consumer, one immutable completion.
class CombatStartMailbox {
    CombatStartAgreement completed_;
    std::atomic<unsigned> state_{0};
    DWORD owner_;
    uint64_t release_at_{};
    std::atomic<uint64_t> released_at_{};
public:
    explicit CombatStartMailbox(DWORD owner):owner_(owner){}
    bool publish(const CombatStartAgreement& agreement,uint64_t release_at=0){
        if(!owner_ || !agreement.activation_ready() || state_.load(std::memory_order_acquire)!=0)return false;
        completed_=agreement;release_at_=release_at;state_.store(1,std::memory_order_release);return true;
    }
    bool take(CombatStartAgreement& out,uint64_t now=CombatStartSync::now()){
        if(GetCurrentThreadId()!=owner_)return false;
        if(state_.load(std::memory_order_acquire)!=1 || now<release_at_)return false;
        unsigned expected=1;
        if(!state_.compare_exchange_strong(expected,2,std::memory_order_acq_rel))return false;
        released_at_.store(now,std::memory_order_release);out=completed_;return true;
    }
    uint64_t released_at()const{return released_at_.load(std::memory_order_acquire);}
};
}
