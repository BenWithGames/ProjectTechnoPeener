#pragma once
#include "NativeClockAccess.hpp"
#include "RoundEventObserver.hpp"
#include "RoundFightReadiness.hpp"
namespace sc6::research {
struct NativeFilterAccess {
    template<class C> static bool confirmed_remote(const C& c,unsigned local,uint32_t frame,uint32_t& raw)noexcept{
        __try{
            if(local<1||local>2||GetCurrentThreadId()!=c.owner ||
               *reinterpret_cast<const uint32_t*>(c.clock+0x3A0)!=c.identity)return false;
            const auto p=c.clock+0x3C0+(2-local)*0x2000+(frame&511u)*16;
            if(*reinterpret_cast<const uint32_t*>(p)!=c.identity || *reinterpret_cast<const uint32_t*>(p+4)!=frame ||
               *reinterpret_cast<const uint8_t*>(p+12)!=1)return false;
            raw=*reinterpret_cast<const uint32_t*>(p+8);return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    template<class C> static bool seed(const C& c,std::array<uint32_t,2>& raw)noexcept{
        __try{
            if(GetCurrentThreadId()!=c.owner || *reinterpret_cast<const uintptr_t*>(c.outer+0x478)!=c.clock)return false;
            const auto p=*reinterpret_cast<const uintptr_t*>(c.outer+0x1498);if(!p)return false;
            raw={*reinterpret_cast<const uint32_t*>(p),*reinterpret_cast<const uint32_t*>(p+4)};return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    template<class C> static bool boundary(const C& c,uint32_t frame){
        if(!c.base || GetCurrentThreadId()!=c.owner)return false;
        const auto before=round_event::snapshot();
        const auto fight=RoundFightReadiness::read(c.base,c.round,c.generation,frame);
        const auto after=round_event::snapshot();
        return before.round==c.round && before.counter==c.event && before.generation==c.generation &&
            after.round==before.round && after.counter==before.counter && after.generation==before.generation &&
            fight.permits(c.round,c.generation,frame,c.owner);
    }
    template<class C,class S> static bool read(const C& c,unsigned local,unsigned slot,uint32_t reverse,S& out,uint32_t& frame)noexcept{
        __try {
            if(*reinterpret_cast<const uintptr_t*>(c.outer+0x478)!=c.clock ||
               *reinterpret_cast<const uintptr_t*>(c.clock)!=c.base+0x3283578 ||
               *reinterpret_cast<const uint32_t*>(c.clock+0x3A0)!=c.identity ||
               *reinterpret_cast<const uint8_t*>(c.clock+0x4404)!=1 ||
               *reinterpret_cast<const uint8_t*>(c.clock+0x4424)!=2 ||
               *reinterpret_cast<const uint32_t*>(c.clock+0x39C)!=(1u<<(local-1)))return false;
            const auto cursor=*reinterpret_cast<const uint32_t*>(c.clock+0x3A4);
            if(cursor>INT32_MAX || reverse>=cursor)return false;
            frame=cursor-reverse-1;
            if(frame<c.clock_origin || frame-c.clock_origin>UINT32_MAX-c.battle_origin ||
               *reinterpret_cast<const uint32_t*>(c.base+0x470D0C4)!=c.battle_origin+(frame-c.clock_origin))return false;
            const auto entry=c.clock+0x3C0+(local-1)*0x2000+(frame&511u)*16;
            if(*reinterpret_cast<const uint32_t*>(entry)!=c.identity ||
               *reinterpret_cast<const uint32_t*>(entry+4)!=frame ||
               *reinterpret_cast<const uint8_t*>(entry+12)!=1)return false;
            out.local_raw=*reinterpret_cast<const uint32_t*>(entry+8);
            const auto previous=*reinterpret_cast<const uintptr_t*>(c.outer+0x1498);
            if(!previous)return false;
            out.previous_address=previous+slot*4;
            out.previous=*reinterpret_cast<const uint32_t*>(out.previous_address);
            out.disabled=*reinterpret_cast<const uint32_t*>(c.outer+0x12F8);
            return *reinterpret_cast<const uint32_t*>(c.clock+0x3A4)==cursor;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    template<class C,class S> static bool sample(const C& c,unsigned local,unsigned slot,uint32_t reverse,S& out,uint32_t& frame){
        if(local<1 || local>2 || slot>1 || GetCurrentThreadId()!=c.owner || !read(c,local,slot,reverse,out,frame))return false;
        return boundary(c,c.battle_origin+(frame-c.clock_origin));
    }
    template<class C,class S> static bool previous(const C& c,unsigned slot,const S& sample,uint32_t raw)noexcept{
        __try {
            if(GetCurrentThreadId()!=c.owner || *reinterpret_cast<const uintptr_t*>(c.outer+0x478)!=c.clock ||
               *reinterpret_cast<const uintptr_t*>(c.outer+0x1498)+slot*4!=sample.previous_address ||
               *reinterpret_cast<const uint32_t*>(sample.previous_address)!=sample.previous)return false;
            *reinterpret_cast<uint32_t*>(sample.previous_address)=raw;return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
};
}
