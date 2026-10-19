#pragma once
#include "FullRestoreStream.hpp"
namespace sc6::research {
// Ghidra root save/load 3841E0/384540: these live globals select load
// destinations; they are not themselves restored by these records.
struct NativeLoadRoute {
    struct Diagnostic {uint64_t reason{},record{},expected{},actual{};};
    static bool validate(const FullRestoreStream& saved,uintptr_t base,bool alternate,
                         uintptr_t optional,uintptr_t manager,Diagnostic& d){
        d={};
        if(!base || base>UINTPTR_MAX-0x4850000 || !saved.sealed || !saved.writes ||
           saved.writes>FullRestoreStream::transfer_limit || manager<0x10000 || manager>UINTPTR_MAX-0xA8){d.reason=1;return false;}
        const uintptr_t writers[]={0x391DEA,0x3906CC,0x38448D,0x321861,0x384403};
        const uint64_t sizes[]={2,0xC60,4,4,0x360};
        const uintptr_t sources[]={base+(alternate?0x4845E80:0x4844DD0)+0x414,
            base+(alternate?0x4845220:0x4844170),base+(alternate?0x48462C8:0x4845218),manager+0xA8,optional};
        unsigned seen[5]{};
        for(unsigned i=0;i<saved.writes;++i)for(unsigned j=0;j<5;++j){
            if(saved.traces[i].writer!=base+writers[j])continue;
            d.record=i;d.expected=sources[j];d.actual=saved.traces[i].source;
            if(++seen[j]!=1 || saved.lengths[i]!=sizes[j]){d.reason=2;return false;}
            if(!sources[j] || saved.traces[i].source!=sources[j]){d.reason=3;return false;}
        }
        for(unsigned j=0;j<5;++j)if(seen[j]!=(j==4?unsigned(optional!=0):1u)){
            d.reason=4;d.record=UINT32_MAX;d.expected=base+writers[j];d.actual=seen[j];return false;
        }
        d={};return true;
    }
    static bool ready(const FullRestoreStream& saved,uintptr_t base,Diagnostic& d) noexcept {
        __try {
            if(!base || base>UINTPTR_MAX-0x4850000){d={1,0,0,0};return false;}
            return validate(saved,base,*reinterpret_cast<const unsigned char*>(base+0x470DEDC)!=0,
                *reinterpret_cast<const uintptr_t*>(base+0x470DEE8),*reinterpret_cast<const uintptr_t*>(base+0x470DF28),d);
        }__except(EXCEPTION_EXECUTE_HANDLER){d={5,0,0,GetExceptionCode()};return false;}
    }
};
}
