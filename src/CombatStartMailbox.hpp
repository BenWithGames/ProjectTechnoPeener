#pragma once
#include "CombatStartAgreement.hpp"
#include <Windows.h>
#include <atomic>
namespace sc6::research {
// One network producer, one game-thread consumer, one immutable completion.
class CombatStartMailbox {
    CombatStartAgreement completed_;
    std::atomic<unsigned> state_{0};
    DWORD owner_;
public:
    explicit CombatStartMailbox(DWORD owner):owner_(owner){}
    bool publish(const CombatStartAgreement& agreement){
        if(!owner_ || !agreement.activation_ready() || state_.load(std::memory_order_acquire)!=0)return false;
        completed_=agreement;state_.store(1,std::memory_order_release);return true;
    }
    bool take(CombatStartAgreement& out){
        if(GetCurrentThreadId()!=owner_)return false;
        unsigned expected=1;
        if(!state_.compare_exchange_strong(expected,2,std::memory_order_acq_rel))return false;
        out=completed_;return true;
    }
};
}
