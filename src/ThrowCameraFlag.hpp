#pragma once
#include "FullRestoreStream.hpp"
namespace sc6::research {
struct ThrowCameraFlag {
    bool attempted{},verified{},unchanged_verified{};
    uintptr_t address{};
    // General path: absent flag records need no patch. An unchanged record
    // must be byte-exact in live memory; known loader normalization still uses
    // the audited dedicated restore below. Never infer a move from an ID.
    bool restore_general(const FullRestoreStream& original,const FullRestoreStream& loaded,uintptr_t base) noexcept {
        if(attempted || !original.sealed || !loaded.sealed || original.writes!=loaded.writes)return false;
        __try {
            uint64_t offset=0;unsigned matches=0;bool unchanged=true;
            for(unsigned i=0;i<original.writes;++i){const auto n=original.lengths[i];
                if(n!=loaded.lengths[i] || original.traces[i].writer!=loaded.traces[i].writer ||
                   offset>original.stream.write_position || n>original.stream.write_position-offset ||
                   offset>loaded.stream.write_position || n>loaded.stream.write_position-offset)return false;
                if(original.traces[i].writer==base+0x324942){
                    if(++matches!=1 || n!=0x41E0 || original.traces[i].source!=loaded.traces[i].source ||
                       original.traces[i].source<0x10000 || original.traces[i].source>UINTPTR_MAX-n)return false;
                    const auto a=original.stream.payload.data()+offset,c=loaded.stream.payload.data()+offset;
                    if(std::memcmp(reinterpret_cast<void*>(original.traces[i].source),c,n))return false;
                    unchanged=!std::memcmp(a,c,n);
                }
                offset+=n;
            }
            if(!matches || unchanged){attempted=verified=unchanged_verified=true;return true;}
            return restore(original,loaded,base,true);
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool restore(const FullRestoreStream& original,const FullRestoreStream& loaded,uintptr_t base,bool allow_unchanged=false,bool edge_unchanged=false,bool reversal_unchanged=false) noexcept {
        if(attempted)return false;
        attempted=true;
        if(unsigned(allow_unchanged)+unsigned(edge_unchanged)+unsigned(reversal_unchanged)>1)return false;
        __try {
            if(!original.sealed || !loaded.sealed || original.writes!=loaded.writes)return false;
            std::uint64_t offset=0,selected_offset=0;unsigned matches=0;uintptr_t source=0;
            for(unsigned i=0;i<original.writes;++i){
                auto n=original.lengths[i];
                if(n!=loaded.lengths[i] || original.traces[i].writer!=loaded.traces[i].writer ||
                   offset>original.stream.write_position || n>original.stream.write_position-offset ||
                   offset>loaded.stream.write_position || n>loaded.stream.write_position-offset)return false;
                if(original.traces[i].writer==base+0x324942){
                    if(n!=0x41E0 || original.traces[i].source!=loaded.traces[i].source)return false;
                    selected_offset=offset;source=original.traces[i].source;++matches;
                }
                offset+=n;
            }
            if(matches!=1 || source<0x10000 || source>UINTPTR_MAX-0x41E0)return false;
            auto a=original.stream.payload.data()+selected_offset;
            auto c=loaded.stream.payload.data()+selected_offset;
            uintptr_t vt{};unsigned before{},after{};
            std::memcpy(&vt,a+0xF80,8);std::memcpy(&before,a+0x1350,4);std::memcpy(&after,c+0x1350,4);
            if((((allow_unchanged || reversal_unchanged) && vt==base+0x3E88858) || (edge_unchanged && vt==base+0x3E88B28)) && before==0 && after==0){
                if(std::memcmp(a,c,0x41E0) || std::memcmp(reinterpret_cast<void*>(source),c,0x41E0))return false;
                address=source+0x1350;unchanged_verified=verified=true;return true;
            }
            if(edge_unchanged || reversal_unchanged || vt!=base+0x3E88858 || before!=0 || after!=1)return false;
            for(unsigned j=0;j<0x41E0;++j)if((j<0x1350 || j>=0x1354) && a[j]!=c[j])return false;
            if(std::memcmp(reinterpret_cast<void*>(source),c,0x41E0))return false;
            address=source+0x1350;
            std::memcpy(reinterpret_cast<void*>(address),a+0x1350,4);
            verified=!std::memcmp(reinterpret_cast<void*>(source),a,0x41E0);
            return verified;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
};
}
