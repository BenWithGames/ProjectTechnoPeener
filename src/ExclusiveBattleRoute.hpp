#pragma once
#include <Windows.h>
#include <atomic>
#include <cstdint>
namespace sc6::research {
// Process-lifetime route. Once published, failures suppress the stock update;
// they cannot silently resume a simulation that may already have changed.
class ExclusiveBattleRoute {
public:
    using Tick=bool(*)(void*,uintptr_t,uintptr_t);
private:
    DWORD owner_{};void* context_{};Tick tick_{};
    std::atomic<bool> entered_{false},failed_{false};
    bool bound_{};
public:
    bool bind(DWORD owner,void* context,Tick tick,bool verified_void_entry,bool clock_owned){
        if(bound_ || !owner || !context || !tick || !verified_void_entry || !clock_owned)return false;
        owner_=owner;context_=context;tick_=tick;bound_=true;return true;
    }
    bool ready()const{return bound_ && !failed_.load();}
    bool failed()const{return failed_.load();}
    void run(uintptr_t argument,uintptr_t caller)noexcept{
        if(!ready())return;
        if(owner_!=GetCurrentThreadId() || entered_.exchange(true)){failed_=true;return;}
        try {if(!tick_(context_,argument,caller))failed_=true;}catch(...){failed_=true;}
        entered_=false;
    }
};
inline std::atomic<ExclusiveBattleRoute*> exclusive_battle_route{};
}
