#pragma once
#include "FullRestoreStream.hpp"
namespace sc6::research {
// Ghidra RVA30AE80 preserves these live player words around the 0x3510
// load (writer return RVA30A9CE). Direct loading is admitted only when
// target and live archives agree; no new writes to these fields are invented.
struct DirectLoadCompatibility {
    struct Diagnostic {uint64_t reason{},side{},offset{},saved{},live{};};
    static bool ready(const FullRestoreStream& a,const FullRestoreStream& b,uintptr_t base,
                      const uintptr_t* players,Diagnostic& d){
        d={};
        if(!base || base>UINTPTR_MAX-0x30A9CE || !a.sealed || !b.sealed || a.stream.failures || b.stream.failures ||
           a.writes>FullRestoreStream::transfer_limit || b.writes>FullRestoreStream::transfer_limit ||
           a.stream.write_position>a.stream.payload.size() || b.stream.write_position>b.stream.payload.size()) {d.reason=1;return false;}
        const std::byte* images[2][2]{};
        const FullRestoreStream* streams[]={&a,&b};
        for(unsigned sample=0;sample<2;++sample){const auto& s=*streams[sample];uint64_t at=0;
            for(unsigned i=0;i<s.writes;++i){const auto n=s.lengths[i];
                if(!n || at>s.stream.write_position || n>s.stream.write_position-at){d.reason=2;return false;}
                for(unsigned side=0;side<2;++side){
                    if(players[side]<0x10000 || players[side]>UINTPTR_MAX-0x35A0){d.reason=3;return false;}
                    if(s.traces[i].writer!=base+0x30A9CE || s.traces[i].source!=players[side]+0x90)continue;
                    if(n!=0x3510 || images[sample][side]){d.reason=4;return false;}
                    images[sample][side]=s.stream.payload.data()+at;
                }at+=n;
            }
            if(at!=s.stream.write_position){d.reason=2;return false;}
        }
        for(unsigned side=0;side<2;++side){d.side=side;
            if(!images[0][side] || !images[1][side]){d.reason=5;return false;}
            for(unsigned offset:{0x244u,0x248u,0x324u}){uint32_t x{},y{};
                std::memcpy(&x,images[0][side]+offset-0x90,4);std::memcpy(&y,images[1][side]+offset-0x90,4);
                if(x!=y){d.reason=6;d.offset=offset;d.saved=x;d.live=y;return false;}
            }
        }d={};return true;
    }
};
}
