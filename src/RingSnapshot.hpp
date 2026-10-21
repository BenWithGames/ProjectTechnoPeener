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
    // Diagnostic only. Status: 0 unavailable, 1 equal, 2 payload, 3 header.
    struct Difference {
        unsigned status{},validity{},group{UINT32_MAX},slot{UINT32_MAX},offset{UINT32_MAX},field{UINT32_MAX};
        uint64_t expected{},actual{};
        unsigned expected_word{},actual_word{};
    };
    Difference first_difference(const RingSnapshot& actual) const {
        Difference d;d.validity=unsigned(valid)|(unsigned(actual.valid)<<1);
        if(d.validity!=3)return d;
        unsigned start=0;
        for(unsigned group=0;group<4;++group){
            const unsigned stride=group%2?0x800:0xC000;
            for(unsigned slot=0;slot<3;++slot)for(unsigned offset=0;offset<stride;++offset){
                const auto at=start+slot*stride+offset;
                if(bytes[at]!=actual.bytes[at]){
                    d.status=2;d.group=group;d.slot=slot;d.offset=offset;
                    d.expected=std::to_integer<unsigned>(bytes[at]);d.actual=std::to_integer<unsigned>(actual.bytes[at]);
                    const auto aligned=at&~3u;
                    std::memcpy(&d.expected_word,bytes.data()+aligned,4);std::memcpy(&d.actual_word,actual.bytes.data()+aligned,4);return d;
                }
            }
            start+=3*stride;
        }
        for(unsigned group=0;group<4;++group){
            const auto& a=headers[group];const auto& b=actual.headers[group];
            const uint64_t expected[]={a.player,a.object,a.vtable,a.slots[0],a.slots[1],a.slots[2],a.current,a.previous,a.index};
            const uint64_t live[]={b.player,b.object,b.vtable,b.slots[0],b.slots[1],b.slots[2],b.current,b.previous,b.index};
            for(unsigned field=0;field<9;++field)if(expected[field]!=live[field]){
                d.status=3;d.group=group;d.field=field;d.expected=expected[field];d.actual=live[field];return d;
            }
        }
        d.status=1;return d;
    }
private:
    // Native accessors select through +28; ownership is fully described by
    // the header. Pre-write validation need not copy the soon-overwritten slots.
    bool capture_impl(uintptr_t base,bool payload) noexcept {
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
                    if(payload)std::memcpy(bytes.data()+out,reinterpret_cast<void*>(h.slots[slot]),stride);out+=stride;
                }
                if(h.current!=*reinterpret_cast<uintptr_t*>(h.object+0x28) || h.previous!=*reinterpret_cast<uintptr_t*>(h.object+0x30) || h.index!=*reinterpret_cast<unsigned*>(h.object+0x20))return false;
            }
            valid=payload;return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool same_owner_headers(const RingSnapshot& other)const{
        for(unsigned i=0;i<4;++i){auto& a=headers[i];auto& b=other.headers[i];
            if(a.player!=b.player || a.object!=b.object || a.vtable!=b.vtable || std::memcmp(a.slots,b.slots,sizeof(a.slots)))return false;
        }
        return true;
    }
public:
    bool capture(uintptr_t base) noexcept {return capture_impl(base,true);}
    bool same_owners(const RingSnapshot& other)const{return valid && other.valid && same_owner_headers(other);}
    // Header-only scratch is deliberately NOT a valid payload snapshot.
    bool validate_owners(uintptr_t base,RingSnapshot& scratch)const noexcept {
        return this!=&scratch && valid && scratch.capture_impl(base,false) && same_owner_headers(scratch);
    }
    // Validate every owner before the first write; caller must hold the update boundary.
    bool restore(uintptr_t base,RingSnapshot& scratch)const noexcept {
        if(!validate_owners(base,scratch))return false;
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
    bool rotated_from(const RingSnapshot& prior,unsigned span=1)const{
        if(!valid || !prior.valid || !span || span>3)return false;
        for(unsigned i=0;i<4;++i){auto& a=prior.headers[i];auto& b=headers[i];
            if(a.player!=b.player || a.object!=b.object || a.vtable!=b.vtable || std::memcmp(a.slots,b.slots,sizeof(a.slots)) || a.index>=3 || b.index!=(a.index+2*span)%3 || b.previous!=(span==1?a.current:a.slots[(a.index+2*(span-1))%3]))return false;
        }
        return true;
    }
};
}
