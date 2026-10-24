#pragma once
#include <Windows.h>
#include "CombatBlend.hpp"
namespace sc6::research {
// Pinned 2F77C0 / 37C230 / 37C2F0 / 37C130. The native player
// serializer omits this range. First 32 bytes are dependencies, not writable state.
struct AnimationControllerSnapshot {
    static constexpr unsigned offset=0x95EC0, size=0x48, state=0x20;
    struct Diagnostic {unsigned phase{},reason{},side{},offset{};uint64_t expected{},actual{};unsigned restored{};};
    uintptr_t players[2]{};
    std::array<std::array<std::byte,size>,2> bytes{};
    bool valid{};
    bool operator==(const AnimationControllerSnapshot&) const = default;
    static bool reject(Diagnostic& d,unsigned reason,unsigned side,unsigned off,uint64_t expected=0,uint64_t actual=0){
        d.reason=reason;d.side=side;d.offset=off;d.expected=expected;d.actual=actual;return false;
    }
    bool capture(const uintptr_t* owners,Diagnostic& d) noexcept {
        valid=false;
        __try {
            for(unsigned side=0;side<2;++side){
                const auto player=owners[side];
                if(player<0x10000 || player>UINTPTR_MAX-offset-size || (side && player==owners[0]))return reject(d,1,side,0);
                players[side]=player;
                std::memcpy(bytes[side].data(),reinterpret_cast<const void*>(player+offset),size);
            }
            valid=true;return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){return reject(d,2,0,0);}
    }
    bool compatible(const AnimationControllerSnapshot& next,Diagnostic& d) const noexcept {
        if(!valid || !next.valid)return reject(d,3,0,0,valid,next.valid);
        for(unsigned side=0;side<2;++side){
            if(players[side]!=next.players[side])return reject(d,4,side,0,players[side],next.players[side]);
            for(unsigned i=0;i<state;++i)if(bytes[side][i]!=next.bytes[side][i])
                return reject(d,5,side,i,std::to_integer<unsigned>(bytes[side][i]),std::to_integer<unsigned>(next.bytes[side][i]));
        }
        return true;
    }
    bool same(const AnimationControllerSnapshot& other,Diagnostic& d) const noexcept {
        if(!compatible(other,d))return false;
        for(unsigned side=0;side<2;++side)for(unsigned i=state;i<size;++i)if(bytes[side][i]!=other.bytes[side][i])
            return reject(d,6,side,i,std::to_integer<unsigned>(bytes[side][i]),std::to_integer<unsigned>(other.bytes[side][i]));
        return true;
    }
    bool dependencies(Diagnostic& d) const noexcept {
        if(!valid)return reject(d,3,0,0);
        for(unsigned side=0;side<2;++side){
            const auto* p=bytes[side].data();const auto resource=CombatBlend::wide(p),owner=CombatBlend::wide(p+8);
            const auto controller=CombatBlend::wide(p+16),table=CombatBlend::wide(p+24);
            if((owner && owner!=players[side]) || (controller && controller!=players[side]))return reject(d,7,side,8,players[side],owner);
            if(table && (owner!=players[side] || controller!=players[side] || !CombatBlend::selected_table_member(resource,table)))
                return reject(d,8,side,24,resource,table);
        }
        return true;
    }
    bool ready(const AnimationControllerSnapshot& next,Diagnostic& d) const noexcept {
        if(!compatible(next,d) || !dependencies(d))return false;
        AnimationControllerSnapshot live;
        return live.capture(players,d) && next.same(live,d);
    }
    bool restore(const AnimationControllerSnapshot& next,Diagnostic& d) const noexcept {
        // Validate both players and the exact load-entry state before any writes.
        if(!ready(next,d))return false;
        __try {
            for(unsigned side=0;side<2;++side)
                std::memcpy(reinterpret_cast<void*>(players[side]+offset+state),bytes[side].data()+state,size-state);
        }__except(EXCEPTION_EXECUTE_HANDLER){return reject(d,9,0,0);}
        AnimationControllerSnapshot live;
        if(!live.capture(players,d) || !same(live,d))return false;
        d.restored=1;return true;
    }
};
}
