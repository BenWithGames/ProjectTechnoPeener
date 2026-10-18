#pragma once
#include <Windows.h>
#include <cstdint>
namespace sc6 {
// Hash-pinned native state: HgBattleModeStart (3E87378/384040) locks
// startup input; HgBattleModeNormal (3E873D0/384EE0) runs normal combat.
// Read at the current owner-thread boundary, never from a cached event/time.
struct RoundFightReadiness {
    uint64_t generation{};
    uint32_t round{},frame{},thread{},vtable{},handler{};
    unsigned status{}; // 0 unreadable, 1 non-Normal, 2 wrong handler, 3 ready, 4 playback, 5 round ended
    uint32_t native_playback{};
    uint16_t round_result{};
    bool permits(uint32_t value,uint64_t boundary,uint32_t outer,uint32_t owner)const{
        return status==3 && !native_playback && !round_result && vtable==0x3E873D0 && handler==0x384EE0 && boundary && value && owner &&
            generation==boundary && round==value && thread==owner && outer==frame;
    }
    static RoundFightReadiness read(uintptr_t base,uint32_t round,uint64_t generation,uint32_t frame) noexcept {
        RoundFightReadiness result{};result.round=round;result.generation=generation;
        result.frame=frame;result.thread=GetCurrentThreadId();
        __try {
            if(!base)return result;
            const auto state=*reinterpret_cast<volatile uintptr_t*>(base+0x4843ED0);
            if(!state)return result;
            const auto vt=*reinterpret_cast<volatile uintptr_t*>(state);
            if(vt>=base && vt-base<0x5000000)result.vtable=static_cast<uint32_t>(vt-base);
            if(vt!=base+0x3E873D0){result.status=1;return result;}
            const auto update=*reinterpret_cast<volatile uintptr_t*>(vt+0x10);
            if(update>=base && update-base<0x5000000)result.handler=static_cast<uint32_t>(update-base);
            result.status=update==base+0x384EE0?3u:2u;
            if(result.status!=3)return result;
            // Native highlight loads historical Normal state, so mode alone is insufficient.
            result.native_playback=*reinterpret_cast<volatile uint32_t*>(base+0x48463F8);
            result.round_result=*reinterpret_cast<volatile uint16_t*>(base+0x4846408);
            if(result.native_playback)result.status=4;
            else if(result.round_result)result.status=5;
        } __except(EXCEPTION_EXECUTE_HANDLER){result.status=0;}
        return result;
    }
};
}
