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
    uint32_t start_elapsed{},start_duration{};
    uintptr_t pending_state{};
    // Preparation permission only: never authorizes a snapshot, hold, or update.
    bool intro(uint32_t value,uint64_t boundary,uint32_t owner)const{
        return status==1 && vtable==0x3E87378 && handler==0x384040 && !native_playback && !round_result &&
            value && boundary && owner && round==value && generation==boundary && thread==owner;
    }
    bool permits(uint32_t value,uint64_t boundary,uint32_t outer,uint32_t owner)const{
        return status==3 && !native_playback && !round_result && vtable==0x3E873D0 && handler==0x384EE0 && boundary && value && owner &&
            generation==boundary && round==value && thread==owner && outer==frame;
    }
    struct PlayerResults {
        uint16_t first{},second{};
        bool valid{};
        // RVA386530: either KO/ring-out/special result, or both timer-expired.
        // This only requests reconciliation; it never authorizes retirement.
        bool may_adjudicate()const{
            return valid && (first==1 || second==1 || first==3 || second==3 ||
                first==11 || second==11 || (first==4 && second==4));
        }
        static PlayerResults read(uintptr_t base)noexcept{
            PlayerResults out{};
            __try {
                if(!base)return out;
                const auto p1=*reinterpret_cast<volatile uintptr_t*>(base+0x470DE90);
                const auto p2=*reinterpret_cast<volatile uintptr_t*>(base+0x470DE98);
                if(!p1 || !p2)return out;
                out.first=*reinterpret_cast<volatile uint16_t*>(p1+0x3D0);
                out.second=*reinterpret_cast<volatile uint16_t*>(p2+0x3D0);out.valid=true;
            }__except(EXCEPTION_EXECUTE_HANDLER){out.valid=false;}
            return out;
        }
    };
    static RoundFightReadiness read(uintptr_t base,uint32_t round,uint64_t generation,uint32_t frame) noexcept {
        RoundFightReadiness result{};result.round=round;result.generation=generation;
        result.frame=frame;result.thread=GetCurrentThreadId();
        __try {
            if(!base)return result;
            const auto state=*reinterpret_cast<volatile uintptr_t*>(base+0x4843ED0);
            if(!state)return result;
            const auto vt=*reinterpret_cast<volatile uintptr_t*>(state);
            if(vt>=base && vt-base<0x5000000)result.vtable=static_cast<uint32_t>(vt-base);
            // These fields are evidence in every mode, especially GameSet.
            // Previously they were reported as zero without being read outside Normal/Start.
            const auto update=*reinterpret_cast<volatile uintptr_t*>(vt+0x10);
            if(update>=base && update-base<0x5000000)result.handler=static_cast<uint32_t>(update-base);
            result.native_playback=*reinterpret_cast<volatile uint32_t*>(base+0x48463F8);
            result.round_result=*reinterpret_cast<volatile uint16_t*>(base+0x4846408);
            result.pending_state=*reinterpret_cast<volatile uintptr_t*>(base+0x4843ED8);
            if(vt!=base+0x3E873D0){
                result.status=1;
                if(vt==base+0x3E87378){
                    result.start_elapsed=*reinterpret_cast<volatile uint32_t*>(state+8);
                    result.start_duration=*reinterpret_cast<volatile uint32_t*>(state+12);
                }
                return result;
            }
            result.status=update==base+0x384EE0?3u:2u;
            if(result.status!=3)return result;
            if(result.native_playback)result.status=4;
            else if(result.round_result)result.status=5;
        } __except(EXCEPTION_EXECUTE_HANDLER){result.status=0;}
        return result;
    }
};
}
