#pragma once
#include <Windows.h>
#include <array>
#include <cstdint>
namespace sc6::research {
// Identity guard for the external stage-query roots read by 3300C0 and the
// effect manager read by 331180. Not a snapshot of the pointed-to stage data.
struct EffectEnvironment {
    std::array<uint64_t,8> values{};bool valid{};
    bool same(const EffectEnvironment& other)const noexcept {return valid && other.valid && values==other.values;}
    static bool capture(uintptr_t module,EffectEnvironment& output) noexcept {
        output={};if(!module || module>UINTPTR_MAX-0x48462CB)return false;
        __try {
            const auto selector=*reinterpret_cast<const unsigned char*>(module+0x470DEDC);
            const auto descriptor=module+(selector?0x48462C8:0x4845218);
            const auto query=module+(selector?0x4845E80:0x4844DD0);
            output.values[0]=module;output.values[1]=selector;
            output.values[2]=*reinterpret_cast<const uintptr_t*>(module+0x470E038);
            output.values[3]=*reinterpret_cast<const uintptr_t*>(query);
            output.values[4]=*reinterpret_cast<const unsigned char*>(descriptor+1) |
                (uint64_t{*reinterpret_cast<const unsigned char*>(descriptor+2)}<<8);
            output.values[5]=*reinterpret_cast<const unsigned char*>(query+0x410);
            // Shared query scratch-buffer identities; contents are not retained.
            output.values[6]=*reinterpret_cast<const uintptr_t*>(module+0x40FBC38);
            output.values[7]=*reinterpret_cast<const uintptr_t*>(module+0x40F7688);
            output.valid=*reinterpret_cast<const unsigned char*>(module+0x470DEDC)==selector;
            return output.valid;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
};
}
