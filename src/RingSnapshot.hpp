#pragma once
#include <array>
#include <cstdint>
#include <cstring>
namespace sc6::research {
// Physical slots, including bytes outside the native serializer prefix.
struct RingSnapshot {
    static constexpr unsigned byte_count=307200;
    struct Header { uintptr_t player{},object{},vtable{},slots[3]{},current{},previous{}; unsigned index{}; };
    std::array<Header,4> headers{};
    std::array<std::byte,byte_count> bytes{};
    bool valid{};
    bool capture(uintptr_t base) noexcept {
        valid=false;
        __try {
            unsigned out=0;
            for(unsigned side=0;side<2;++side)for(unsigned kind=0;kind<2;++kind){
                auto& h=headers[side*2+kind];
                h.player=*reinterpret_cast<uintptr_t*>(base+0x470DE90+side*8);
                if(h.player<0x10000)return false;
                h.object=h.player+(kind?0x27760:0x35A0);
                h.vtable=*reinterpret_cast<uintptr_t*>(h.object);
                if(h.vtable!=base+(kind?0x3E87700:0x3E87638))return false;
                std::memcpy(h.slots,reinterpret_cast<void*>(h.object+8),24);
                h.index=*reinterpret_cast<unsigned*>(h.object+0x20);
                h.current=*reinterpret_cast<uintptr_t*>(h.object+0x28);
                h.previous=*reinterpret_cast<uintptr_t*>(h.object+0x30);
                unsigned stride=kind?0x800:0xC000;
                auto first=(h.object+0xBF)&~uintptr_t(0x7F);
                if(h.index>=3 || h.current!=h.slots[h.index])return false;
                bool previous_ok=false;
                for(unsigned slot=0;slot<3;++slot){
                    if(h.slots[slot]!=first+slot*stride)return false;
                    previous_ok=previous_ok || h.previous==h.slots[slot];
                }
                if(!previous_ok)return false;
                for(unsigned slot=0;slot<3;++slot){
                    std::memcpy(bytes.data()+out,reinterpret_cast<void*>(h.slots[slot]),stride);out+=stride;
                }
                if(h.current!=*reinterpret_cast<uintptr_t*>(h.object+0x28) || h.previous!=*reinterpret_cast<uintptr_t*>(h.object+0x30) || h.index!=*reinterpret_cast<unsigned*>(h.object+0x20))return false;
            }
            valid=true;return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool same_owners(const RingSnapshot& other)const{
        if(!valid || !other.valid)return false;
        for(unsigned i=0;i<4;++i){auto& a=headers[i];auto& b=other.headers[i];
            if(a.player!=b.player || a.object!=b.object || a.vtable!=b.vtable || std::memcmp(a.slots,b.slots,sizeof(a.slots)))return false;
        }
        return true;
    }
    // Validate every owner before the first write; caller must hold the update boundary.
    bool restore(uintptr_t base,RingSnapshot& scratch)const noexcept {
        if(!scratch.capture(base) || !same_owners(scratch))return false;
        __try {
            unsigned offset=0;
            for(unsigned i=0;i<4;++i){auto& h=headers[i];unsigned stride=i%2?0x800:0xC000;
                for(unsigned slot=0;slot<3;++slot){
                    std::memcpy(reinterpret_cast<void*>(h.slots[slot]),bytes.data()+offset,stride);offset+=stride;
                }
                *reinterpret_cast<unsigned*>(h.object+0x20)=h.index;
                *reinterpret_cast<uintptr_t*>(h.object+0x28)=h.current;
                *reinterpret_cast<uintptr_t*>(h.object+0x30)=h.previous;
            }
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
        return scratch.capture(base) && same(scratch);
    }
    bool same(const RingSnapshot& other)const{
        if(!valid || !other.valid || bytes!=other.bytes)return false;
        for(unsigned i=0;i<4;++i){auto& a=headers[i];auto& b=other.headers[i];
            if(a.player!=b.player || a.object!=b.object || a.vtable!=b.vtable || a.index!=b.index || a.current!=b.current || a.previous!=b.previous || std::memcmp(a.slots,b.slots,sizeof(a.slots)))return false;
        }
        return true;
    }
    bool rotated_from(const RingSnapshot& prior)const{
        if(!valid || !prior.valid)return false;
        for(unsigned i=0;i<4;++i){auto& a=prior.headers[i];auto& b=headers[i];
            if(a.player!=b.player || a.object!=b.object || a.vtable!=b.vtable || std::memcmp(a.slots,b.slots,sizeof(a.slots)) || b.index!=(a.index+2)%3 || b.previous!=a.current)return false;
        }
        return true;
    }
};
}
