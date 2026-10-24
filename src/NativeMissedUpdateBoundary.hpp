#pragma once
#include <Windows.h>
#include <cstdint>
namespace sc6::research {
// Ghidra 3FBF30 -> 3FE520. Recovery may create precisely one delta only
// before the ordinary combat loop, with the previous delta fully consumed.
struct NativeMissedUpdateBoundary {
    enum Reason:unsigned {ready,wrong_clock,phase,suspended,alternate,pending_delta,invalid};
    static Reason inspect(uintptr_t outer,uintptr_t expected_clock)noexcept{
        if(!outer || !expected_clock)return invalid;
        __try {
            const auto clock=*reinterpret_cast<const uintptr_t*>(outer+0x478);
            if(clock!=expected_clock)return wrong_clock;
            if(*reinterpret_cast<const uint8_t*>(outer+0x1461)!=2)return phase;
            if(*reinterpret_cast<const uint8_t*>(outer+0x12F3)!=0)return suspended;
            if(*reinterpret_cast<const uint8_t*>(outer+0x1463)!=0)return alternate;
            if(*reinterpret_cast<const uint32_t*>(outer+0x1488)!=*reinterpret_cast<const uint32_t*>(clock+0x3A0) ||
               *reinterpret_cast<const uint32_t*>(outer+0x148C)!=*reinterpret_cast<const uint32_t*>(clock+0x3A4))return pending_delta;
            return ready;
        }__except(EXCEPTION_EXECUTE_HANDLER){return invalid;}
    }
};
}
