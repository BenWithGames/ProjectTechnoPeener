#pragma once
#include <Windows.h>
#include "FullRestoreStream.hpp"
namespace sc6::research {
// Pinned 349290 resets secondary groups 3 and 1; 3495B0 rebuilds
// them from +2030 and +2074 respectively before consumption.
// Restore data, not comparison exceptions. No pointers are written.
struct ActiveAnimationGroup {
    struct Diagnostic {unsigned reason{},side{},record{},restored{};
        std::array<std::array<std::byte,68>,2> saved{},loaded{},verified{};
        unsigned schema{2},group{};
        std::array<std::array<std::byte,68>,2> secondary_saved{},secondary_loaded{},secondary_verified{};} ;
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
                    groups[side]=s.stream.payload.data()+at;
                }
                if(s.traces[i].source==player+0x90 && n==0x3510 && s.traces[i].writer==base+0x30A9CE){
                    if(prefix[side]){d.reason=2;return false;}prefix[side]=s.stream.payload.data()+at;
                }
            }at+=n;
        }
        if(at!=s.stream.write_position){d.reason=1;return false;}
        std::array<bool,4> active{};
        constexpr unsigned offsets[]={0x1118,0x1058};
        constexpr unsigned slots[]={0x2020,0x2064};
        constexpr unsigned controls[]={0x2030,0x2074};
        // Validate BOTH groups on BOTH players before writing anything.
        for(unsigned group=0;group<2;++group)for(unsigned side=0;side<2;++side){
            d.side=side;d.group=group?1:3;
            const auto* g=groups[side]?groups[side]+offsets[group]:nullptr;
            if(!g || !value<uint64_t>(g))continue;
            if(players[0]==players[1] || !prefix[side] || value<uint64_t>(g)!=0x7fff){d.reason=3;return false;}
            auto slot=value<int16_t>(prefix[side]+slots[group]-0x90);
            auto weight=value<uint32_t>(prefix[side]+controls[group]-0x90);
            if(slot<0 || weight<0x358637BDu || weight>=0x7f800000u){d.reason=4;return false;}
            for(unsigned j=0;j<15;++j)if(value<uint32_t>(g+8+j*4)!=weight){d.reason=5;return false;}
            const auto* live=reinterpret_cast<const std::byte*>(players[side]);
            if(value<int16_t>(live+slots[group])!=slot || value<uint32_t>(live+controls[group])!=weight){d.reason=6;return false;}
            auto& saved=group?d.secondary_saved[side]:d.saved[side];
            auto& loaded=group?d.secondary_loaded[side]:d.loaded[side];
            std::memcpy(saved.data(),g,68);std::memcpy(loaded.data(),live+0x95FA0+offsets[group],68);
            bool zero=true;for(auto byte:loaded)zero=zero && byte==std::byte{};
            if(!zero && loaded!=saved){d.reason=7;return false;}
            active[group*2+side]=true;
        }
        for(unsigned group=0;group<2;++group)for(unsigned side=0;side<2;++side)if(active[group*2+side]){
            d.side=side;d.group=group?1:3;
            auto* live=reinterpret_cast<std::byte*>(players[side])+0x95FA0+offsets[group];
            auto& saved=group?d.secondary_saved[side]:d.saved[side];
            auto& verified=group?d.secondary_verified[side]:d.verified[side];
            std::memcpy(live,saved.data(),68);std::memcpy(verified.data(),live,68);
            if(verified!=saved){d.reason=8;return false;}++d.restored;
        }return true;
    }
};
}

