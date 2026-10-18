#pragma once
#include "SelectorLifetime.hpp"
#include <array>
#include <cstring>
#include <type_traits>
namespace sc6::research {
// Ghidra 38EFA0/38F180/38F2F0. Only mutable counters, previous choice and
// pending entries are restored. Allocation/header/table pointers are identities.
struct SelectorSnapshot {
    static constexpr unsigned table_limit=256*1024;
    struct Entry {
        uintptr_t player{},object{},header{},allocation{},groups_pointer{},variants_pointer{};
        unsigned active{},previous{},groups{},variants{},prefix{},bytes{};
        std::array<std::byte,24> header_bytes{};
        std::array<std::byte,0x240> queue{};
        std::array<std::byte,table_limit> table{};
    };
    struct Diagnostic {unsigned schema{1},phase{},reason{},side{},offset{},expected{},actual{},written{},verified{};};
    unsigned schema{1},valid{};uint64_t epoch{};
    std::array<Entry,2> entries;
    template<class T> static T load(uintptr_t address){T value;std::memcpy(&value,reinterpret_cast<const void*>(address),sizeof(T));return value;}
    template<class T> static T word(const std::byte* address){T value;std::memcpy(&value,address,sizeof(T));return value;}
    static bool reject(Diagnostic& d,unsigned reason,unsigned side=0,unsigned offset=0,unsigned expected=0,unsigned actual=0){
        d.reason=reason;d.side=side;d.offset=offset;d.expected=expected;d.actual=actual;return false;
    }
    static bool mutable_table_byte(const Entry& e,unsigned i){return i<e.groups*8 && (i%8==2 || i%8==3);}
    static bool header(Entry& e,uintptr_t player,Diagnostic& d,unsigned side){
        if(player<0x10000 || player>UINTPTR_MAX-0x95A00)return reject(d,1,side);
        e.player=player;e.object=player+0x95788;e.active=load<unsigned>(e.object+0x258);
        e.previous=load<unsigned>(e.object+0x25C);
        if(e.active>1)return reject(d,2,side,0x258,1,e.active);
        e.header=load<uintptr_t>(e.object+0x240);e.groups_pointer=load<uintptr_t>(e.object+0x248);
        e.variants_pointer=load<uintptr_t>(e.object+0x250);e.allocation=load<uintptr_t>(e.object+0x260);
        e.groups=e.variants=e.prefix=e.bytes=0;
        if(!e.active)return true;
        if(e.header<0x10000 || e.header>UINTPTR_MAX-24 || e.allocation<0x10000)return reject(d,3,side);
        std::memcpy(e.header_bytes.data(),reinterpret_cast<const void*>(e.header),24);
        const auto* h=e.header_bytes.data();
        if(word<unsigned>(h)!=0x00627476 || word<unsigned>(h+8)!=0x1002)return reject(d,4,side);
        e.variants=word<unsigned>(h+12);const auto offset=word<unsigned>(h+16);e.groups=word<unsigned>(h+20);
        if(offset<24 || offset-24>table_limit || e.variants>(table_limit-(offset-24))/0x84 ||
           e.groups>(offset-24)/8)return reject(d,5,side);
        e.prefix=offset-24;e.bytes=e.prefix+e.variants*0x84;
        if(!e.bytes || e.allocation>UINTPTR_MAX-e.bytes || e.groups_pointer!=e.allocation ||
           e.variants_pointer!=e.allocation+e.prefix)return reject(d,6,side);
        return true;
    }
    static bool identity(const Entry& a,const Entry& b){
        return a.player==b.player && a.object==b.object && a.active==b.active && a.header==b.header &&
            a.allocation==b.allocation && a.groups_pointer==b.groups_pointer && a.variants_pointer==b.variants_pointer &&
            a.groups==b.groups && a.variants==b.variants && a.prefix==b.prefix && a.bytes==b.bytes &&
            (!a.active || a.header_bytes==b.header_bytes);
    }
    static bool queue_safe(const Entry& e,const uintptr_t* players,Diagnostic& d,unsigned side){
        for(unsigned i=0;i<24;++i){const auto* p=e.queue.data()+i*24;
            if(word<int16_t>(p)==-1)continue;
            const auto owner=word<uintptr_t>(p+8);
            if(owner && owner!=players[0] && owner!=players[1])return reject(d,7,side,i*24+8);
        }return true;
    }
    bool capture(const uintptr_t* players,Diagnostic& d) noexcept {
        valid=0;d.phase=1;d.reason=0;
        AcquireSRWLockShared(&selector_lifetime::lock);
        bool ok=false;
        __try {
            epoch=selector_lifetime::epoch;
            for(unsigned side=0;side<2;++side){auto& e=entries[side];
                if(!header(e,players[side],d,side))__leave;
                std::memcpy(e.queue.data(),reinterpret_cast<const void*>(e.object),e.queue.size());
                if(!queue_safe(e,players,d,side))__leave;
                if(e.active)std::memcpy(e.table.data(),reinterpret_cast<const void*>(e.allocation),e.bytes);
                if(side==1)ok=true;
            }
        }__except(EXCEPTION_EXECUTE_HANDLER){reject(d,8);}
        ReleaseSRWLockShared(&selector_lifetime::lock);valid=ok;return ok;
    }
    bool compatible(const SelectorSnapshot& b,Diagnostic& d)const {
        d.phase=2;d.reason=0;
        if(!valid || !b.valid || schema!=1 || b.schema!=1 || epoch!=b.epoch)return reject(d,9);
        for(unsigned side=0;side<2;++side){const auto& a=entries[side];const auto& other=b.entries[side];
            if(a.bytes>table_limit || a.groups>a.prefix/8 || !identity(a,other))return reject(d,10,side);
            for(unsigned i=0;i<a.bytes;++i)if(!mutable_table_byte(a,i) && a.table[i]!=other.table[i])
                return reject(d,11,side,i,std::to_integer<unsigned>(a.table[i]),std::to_integer<unsigned>(other.table[i]));
        }return true;
    }
    bool same(const SelectorSnapshot& b,Diagnostic& d)const {
        if(!compatible(b,d))return false;
        d.phase=3;
        for(unsigned side=0;side<2;++side){const auto& a=entries[side];const auto& other=b.entries[side];
            if(a.previous!=other.previous)return reject(d,12,side,0x25C,a.previous,other.previous);
            for(unsigned i=0;i<a.queue.size();++i)if(a.queue[i]!=other.queue[i])return reject(d,13,side,i,std::to_integer<unsigned>(a.queue[i]),std::to_integer<unsigned>(other.queue[i]));
            for(unsigned i=0;i<a.bytes;++i)if(a.table[i]!=other.table[i])return reject(d,14,side,i,std::to_integer<unsigned>(a.table[i]),std::to_integer<unsigned>(other.table[i]));
        }return true;
    }
    // Called under the lifetime read lock, only at an owned game-thread boundary.
    bool live_dependencies(Diagnostic& d)const {
        if(!valid || epoch!=selector_lifetime::epoch)return reject(d,9);
        for(unsigned side=0;side<2;++side){const auto& a=entries[side];
            // Header reading does not need a full table-sized scratch object.
            if(a.bytes>table_limit || a.groups>a.prefix/8)return reject(d,5,side);
            if(load<unsigned>(a.object+0x258)!=a.active || load<uintptr_t>(a.object+0x240)!=a.header ||
               load<uintptr_t>(a.object+0x248)!=a.groups_pointer || load<uintptr_t>(a.object+0x250)!=a.variants_pointer ||
               load<uintptr_t>(a.object+0x260)!=a.allocation)return reject(d,10,side);
            if(a.active && std::memcmp(reinterpret_cast<const void*>(a.header),a.header_bytes.data(),24))return reject(d,10,side);
            for(unsigned i=0;i<a.bytes;++i)if(!mutable_table_byte(a,i) && load<std::byte>(a.allocation+i)!=a.table[i])return reject(d,11,side,i);
        }return true;
    }
    bool dependencies(Diagnostic& d)const noexcept {
        bool ok=false;d.phase=4;AcquireSRWLockShared(&selector_lifetime::lock);
        __try {ok=live_dependencies(d);}__except(EXCEPTION_EXECUTE_HANDLER){reject(d,8);}
        ReleaseSRWLockShared(&selector_lifetime::lock);return ok;
    }
    bool restore(Diagnostic& d)const noexcept {
        bool ok=false;d.phase=5;d.written=d.verified=0;
        AcquireSRWLockShared(&selector_lifetime::lock);
        __try {
            if(!live_dependencies(d))__leave;
            // Both sides are validated before the first write. No allocation,
            // pointer, enable flag, immutable table field or header is restored.
            for(const auto& e:entries){
                d.written=1;
                std::memcpy(reinterpret_cast<void*>(e.object),e.queue.data(),e.queue.size());
                std::memcpy(reinterpret_cast<void*>(e.object+0x25C),&e.previous,4);
                for(unsigned i=0;i<e.groups;++i)std::memcpy(reinterpret_cast<void*>(e.allocation+i*8+2),e.table.data()+i*8+2,2);
            }
            for(unsigned side=0;side<2;++side){const auto& e=entries[side];
                if(load<unsigned>(e.object+0x25C)!=e.previous || std::memcmp(reinterpret_cast<void*>(e.object),e.queue.data(),e.queue.size()) ||
                   (e.bytes && std::memcmp(reinterpret_cast<void*>(e.allocation),e.table.data(),e.bytes))) {reject(d,15,side);__leave;}
                if(side==1)ok=true;
            }
        }__except(EXCEPTION_EXECUTE_HANDLER){reject(d,8);}
        ReleaseSRWLockShared(&selector_lifetime::lock);d.verified=ok;return ok;
    }
};
static_assert(std::is_trivially_copyable_v<SelectorSnapshot>);
static_assert(sizeof(SelectorSnapshot::Entry)==262816);
static_assert(sizeof(SelectorSnapshot)==525648);
static_assert(sizeof(SelectorSnapshot::Diagnostic)==36);
}
