#pragma once
#include <Windows.h>
#include <cstdint>
namespace sc6::research {
// GameSet can branch through MotionChange back to Start for another round.
// Only pinned HgBattleModeEnd (state-code accessor returns 10) is terminal.
struct MatchEndObservation {
    unsigned vtable{},handler{},thread{},state_query{};
    bool confirmed(unsigned owner)const{return owner && thread==owner && vtable==0x3E87428 && handler==0x2D2BC0 && state_query==0x301590;}
    bool cleanup_allowed(unsigned owner,unsigned current,bool previous_failure,bool between_cycles,unsigned cancellation)const{
        return confirmed(owner) && current==owner && !previous_failure && between_cycles &&
            (cancellation==0 || cancellation==7);
    }
    static MatchEndObservation read(uintptr_t base) noexcept {
        MatchEndObservation out{};out.thread=GetCurrentThreadId();
        __try {
            if(!base)return out;
            auto state=*reinterpret_cast<volatile uintptr_t*>(base+0x4843ED0);
            if(!state)return out;
            auto vt=*reinterpret_cast<volatile uintptr_t*>(state);
            if(vt>=base && vt-base<0x5000000)out.vtable=static_cast<unsigned>(vt-base);
            if(vt!=base+0x3E87428 && vt!=base+0x3E87530)return out;
            auto fn=*reinterpret_cast<volatile uintptr_t*>(vt+0x10);
            if(fn>=base && fn-base<0x5000000)out.handler=static_cast<unsigned>(fn-base);
            const auto query=*reinterpret_cast<volatile uintptr_t*>(vt+0x18);
            if(query>=base && query-base<0x5000000)out.state_query=static_cast<unsigned>(query-base);
        }__except(EXCEPTION_EXECUTE_HANDLER){}
        return out;
    }
};
}
