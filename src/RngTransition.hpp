#pragma once
#include <cstdint>
namespace sc6::research {
// Diagnostic only. Pinned RVA34F550 advances this recurrence twice per call.
// A matching distance does not authorize restore or identify the caller.
struct RngTransition {
    static constexpr unsigned unknown=UINT32_MAX;
    static bool valid(uint32_t value){return value>0 && value<2147483647u;}
    static uint32_t step(uint32_t value){return static_cast<uint32_t>(uint64_t(value)*16807u%2147483647u);}
    static unsigned distance(uint32_t before,uint32_t after){
        if(!valid(before) || !valid(after))return unknown;
        for(unsigned n=0;n<=64;++n){if(before==after)return n;before=step(before);}
        return unknown;
    }
};
}
