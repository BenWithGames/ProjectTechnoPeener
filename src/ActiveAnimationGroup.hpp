#pragma once
#include <Windows.h>
#include "FullRestoreStream.hpp"
namespace sc6::research {
// Pinned 349290 resets group3; 3495B0 rebuilds it from +2030 before use.
// Restore data, not comparison exceptions. No pointers are written.
struct ActiveAnimationGroup {
    struct Diagnostic {unsigned reason{},side{},record{},restored{};
        std::array<std::array<std::byte,68>,2> saved{},loaded{},verified{};} ;
    template<class T> static T value(const std::byte* p){T v{};std::memcpy(&v,p,sizeof(v));return v;}
    static bool restore(const FullRestoreStream& s,const uintptr_t* players,uintptr_t base,Diagnostic& d){
        d={};
        __try{return apply(s,players,base,d);}__except(EXCEPTION_EXECUTE_HANDLER){d.reason=9;return false;}
    }
    static bool apply(const FullRestoreStream& s,const uintptr_t* players,uintptr_t base,Diagnostic& d){
        if(!s.sealed || s.writes>s.transfer_limit || s.stream.write_position>s.stream.payload.size()){d.reason=1;return false;}
        std::array<const std::byte*,2> groups{},prefix{};
        uint64_t at=0;
        for(unsigned i=0;i<s.writes;++i){const auto n=s.lengths[i];
            if(at>s.stream.write_position || n>s.stream.write_position-at){d.reason=1;return false;}
            for(unsigned side=0;side<2;++side){auto player=players[side];if(!player || player>UINTPTR_MAX-0x98000)continue;
                if(s.traces[i].source==player+0x95FA0 && n==0x11F0){
                    d.side=side;d.record=i;
                    if(groups[side] || s.traces[i].writer!=base+0x30AA96 || s.traces[i].reader!=base+0x30AFD8){d.reason=2;return false;}
                    groups[side]=s.stream.payload.data()+at+0x1118;
                }
                if(s.traces[i].source==player+0x90 && n==0x3510 && s.traces[i].writer==base+0x30A9CE){
                    if(prefix[side]){d.reason=2;return false;}prefix[side]=s.stream.payload.data()+at;
                }
            }at+=n;
        }
        if(at!=s.stream.write_position){d.reason=1;return false;}
        std::array<bool,2> active{};
        // Validate both sides before writing either side.
        for(unsigned side=0;side<2;++side){d.side=side;const auto* g=groups[side];
            if(!g || !value<uint64_t>(g))continue;
            if(players[0]==players[1] || !prefix[side] || value<uint64_t>(g)!=0x7fff){d.reason=3;return false;}
            auto slot=value<int16_t>(prefix[side]+0x2020-0x90);
            auto weight=value<uint32_t>(prefix[side]+0x2030-0x90);
            if(slot<0 || weight<0x358637BDu || weight>=0x7f800000u){d.reason=4;return false;}
            for(unsigned j=0;j<15;++j)if(value<uint32_t>(g+8+j*4)!=weight){d.reason=5;return false;}
            const auto* live=reinterpret_cast<const std::byte*>(players[side]);
            if(value<int16_t>(live+0x2020)!=slot || value<uint32_t>(live+0x2030)!=weight){d.reason=6;return false;}
            std::memcpy(d.saved[side].data(),g,68);std::memcpy(d.loaded[side].data(),live+0x970B8,68);
            bool zero=true;for(auto b:d.loaded[side])zero=zero && b==std::byte{};
            if(!zero && d.loaded[side]!=d.saved[side]){d.reason=7;return false;}
            active[side]=true;
        }
        for(unsigned side=0;side<2;++side)if(active[side]){
            auto* live=reinterpret_cast<std::byte*>(players[side])+0x970B8;
            std::memcpy(live,d.saved[side].data(),68);std::memcpy(d.verified[side].data(),live,68);
            if(d.verified[side]!=d.saved[side]){d.reason=8;return false;}++d.restored;
        }return true;
    }
};
}

