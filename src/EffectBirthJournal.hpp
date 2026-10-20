#pragma once
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include "EffectAllocationSites.hpp"
#include "EffectMemoryGate.hpp"
namespace sc6::research {
// Owner-thread, bounded original-input replay journal. Preserve allocation-entry
// inputs, never copy an expected endpoint to manufacture a successful comparison.
struct EffectBirthJournal {
    static constexpr unsigned capacity=256;
    enum class Mode : unsigned {off,record,sealed,replay};
    struct Event {uint64_t caller{},bytes{},address{};uint32_t seed{},type{};};
    struct Applied {uint64_t address{};uint32_t index{},before{},saved{},after{};};
    unsigned schema=1;Mode mode=Mode::off;
    unsigned count{},cursor{},end{},failure{},applied_count{},intervals{};
    uint64_t session{},round{},window{},discarded{},branches{};
    std::array<Event,capacity> events{};
    std::array<Applied,capacity> applied{};
    bool fail(unsigned code){if(!failure)failure=code;return false;}
    bool start(uint64_t s,uint64_t r,uint64_t w){
        if(mode!=Mode::off || !w)return fail(1);
        *this={};session=s;round=r;window=w;mode=Mode::record;return true;
    }
    // Rolling histories discard only events older than every retained snapshot.
    // Caller must rebase all retained cursors in the same owner-thread operation.
    bool discard_prefix(unsigned amount){
        if(failure || mode!=Mode::record || amount>count)return false;
        for(unsigned i=amount;i<count;++i)events[i-amount]=events[i];
        count-=amount;discarded+=amount;cursor=end=0;return true;
    }
    enum class Compaction : unsigned {applied=1,deferred=2,stopped=3,rejected=4};
    // Optional maintenance: a contended lock must leave journal and retained
    // cursors unchanged. Mandatory capture/restore accesses remain fail-closed.
    Compaction try_discard_prefix(EffectMemoryGate& gate,const std::atomic<bool>& stopped,unsigned amount){
        EffectMemoryGate::AccessOutcome outcome{};bool accepted=false;
        const bool entered=gate.try_access(stopped,[&]{accepted=discard_prefix(amount);},&outcome);
        if(entered)return accepted?Compaction::applied:Compaction::rejected;
        return outcome==EffectMemoryGate::AccessOutcome::contended?Compaction::deferred:Compaction::stopped;
    }
    // Branch from a restored snapshot. Future allocation entries are newly
    // recorded; never apply the obsolete prediction's allocation-entry seed.
    bool resume_recording(unsigned at){
        if(failure || mode!=Mode::sealed || at>count)return false;
        count=at;cursor=end=0;mode=Mode::record;++branches;return true;
    }
    void seal(){if(mode==Mode::record)mode=Mode::sealed;}
    void stop(){mode=Mode::off;} // Preserve evidence until the next window.
    bool begin(unsigned first,unsigned last,uint64_t s,uint64_t r,uint64_t w){
        if(failure || mode!=Mode::sealed || s!=session || r!=round || w!=window ||
           first>last || last>count)return fail(2);
        cursor=first;end=last;mode=Mode::replay;return true;
    }
    bool finish(){
        if(mode!=Mode::replay)return fail(3);
        mode=Mode::sealed;
        if(failure)return false;
        if(cursor!=end)return fail(4);
        ++intervals;return true;
    }
    static bool word(uintptr_t address,uint32_t& value,bool write) noexcept {
        __try {
            if(write)std::memcpy(reinterpret_cast<void*>(address),&value,4);
            else std::memcpy(&value,reinterpret_cast<const void*>(address),4);
            return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool allocation(uintptr_t address,uint64_t bytes,uint64_t caller){
        if(mode==Mode::off || mode==Mode::sealed)return true;
        if(failure)return false;
        const auto* site=effect_allocation_site(static_cast<uintptr_t>(caller));
        if(!address || !site || bytes!=site->bytes || address>UINTPTR_MAX-bytes)return fail(5);
        const bool stage=caller==0x334A1E && bytes==0x1E0 && site->vtable==0x3E88CE8;
        uint32_t initial{};
        if(mode==Mode::record){
            if(count==capacity)return fail(6);
            if(stage && !word(address+0x138,initial,false))return fail(7);
            events[count++]={caller,bytes,address,initial,site->type};return true;
        }
        // Check the complete observed allocation sequence, including other types.
        if(cursor>=end)return fail(8);
        const auto& event=events[cursor];
        if(event.caller!=caller || event.bytes!=bytes || event.type!=site->type)return fail(9);
        if(stage){
            if(applied_count==capacity)return fail(10);
            auto& item=applied[applied_count++];item.address=address;item.index=cursor;item.saved=event.seed;
            if(!word(address+0x138,item.before,false))return fail(11);
            auto saved=event.seed;
            if(!word(address+0x138,saved,true) || !word(address+0x138,item.after,false) || item.after!=saved)return fail(12);
        }
        ++cursor;return true;
    }
    template<class Stream> void report(Stream& out)const{
        out<<"suite_wind_birth_schema=1\nsuite_wind_birth_events="<<count
           <<"\nsuite_wind_birth_applied="<<applied_count<<"\nsuite_wind_birth_intervals="<<intervals
           <<"\nsuite_wind_birth_failure="<<failure<<"\nsuite_wind_birth_cursor="<<cursor
           <<"\nsuite_wind_birth_discarded="<<discarded<<"\nsuite_wind_birth_branches="<<branches
           <<"\nsuite_wind_birth_end="<<end<<'\n';
    }
};
}
