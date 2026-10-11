#pragma once
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>
namespace sc6::research {
// Diagnostic sparse reads only, never a copy of the registered span.
struct StageRangeDescriptor {
    static constexpr unsigned limit=4096;
    uintptr_t address{};int16_t columns{},records{},candidates{};
    std::array<uint16_t,limit> counts{};
    uint64_t computed{};unsigned exception{},read_count{};bool valid{},bounded_out{};
    bool capture(uintptr_t source) noexcept {
        *this={};address=source;
        if(!source || source>UINTPTR_MAX-0x30)return false;
        __try {
            int16_t header[3]{};std::memcpy(header,reinterpret_cast<void*>(source+0x28),6);
            columns=header[0];records=header[1];candidates=header[2];
            if(columns<0 || records<0 || candidates<0)return false;
            if(static_cast<unsigned>(records)>limit){bounded_out=true;return false;}
            const uint64_t initial=(uint64_t(columns)+3)*16;
            const uint64_t extent=initial+8+uint64_t(records)*24;
            if(extent>UINTPTR_MAX-source)return false;
            computed=initial+uint64_t(candidates)*64;
            for(unsigned i=0;i<static_cast<unsigned>(records);++i){
                std::memcpy(&counts[i],reinterpret_cast<void*>(source+initial+8+uint64_t(i)*24),2);
                ++read_count;
                computed+=((uint64_t(counts[i])*8+31)/16)*16;
            }
            int16_t after[3]{};std::memcpy(after,reinterpret_cast<void*>(source+0x28),6);
            if(std::memcmp(header,after,6))return false;
            for(unsigned i=0;i<read_count;++i){uint16_t value{};
                std::memcpy(&value,reinterpret_cast<void*>(source+initial+8+uint64_t(i)*24),2);
                if(value!=counts[i])return false;
            }
            valid=true;return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){exception=GetExceptionCode();return false;}
    }
};
}
