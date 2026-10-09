#pragma once
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include "TrainingFieldReset.hpp"
namespace sc6::research {
struct FieldUpdateTrace {
    struct Event {uintptr_t player{},caller{};unsigned counter{},phase{},thread{},index{},flags{},assignment{};short delta{},before{};bool readable{};};
    struct Setter {uintptr_t object{},vtable{},target{},site{};unsigned value{},counter{};std::array<uintptr_t,8> words{};};
    std::array<Setter,16> setters{};unsigned setter_count{};bool setter_fault{},setter_overflow{};
    void record_setter(const uintptr_t* registers,uintptr_t site) noexcept {
        const DWORD error=GetLastError();
        if(enabled.load(std::memory_order_acquire) && GetCurrentThreadId()==owner){
            training_field_reset.observe(base,registers[0],unsigned(registers[1]));
            bool duplicate=false;
            for(unsigned i=0;i<setter_count;++i)if(setters[i].object==registers[0] && setters[i].value==unsigned(registers[1]) && setters[i].site==site)duplicate=true;
            if(!duplicate){
                if(setter_count==setters.size())setter_overflow=true;
                else {auto& item=setters[setter_count++];item={};item.object=registers[0];item.value=unsigned(registers[1]);item.site=site;
                    __try {item.vtable=*reinterpret_cast<uintptr_t*>(item.object);item.target=*reinterpret_cast<uintptr_t*>(item.vtable+0x110);
                        item.counter=*reinterpret_cast<unsigned*>(base+0x470D0C4);
                        for(unsigned j=0;j<item.words.size();++j)item.words[j]=*reinterpret_cast<uintptr_t*>(item.object+j*8);
                    }__except(EXCEPTION_EXECUTE_HANDLER){setter_fault=true;}
                }
            }
        }
        SetLastError(error);
    }
    struct Callback {uintptr_t object{},vtable{},target{},resolved_object{},resolved_target{};unsigned index{},active{};std::array<uintptr_t,4> words{};};
    std::array<Callback,64> callbacks{};
    unsigned callback_count{};int callback_total{};bool callbacks_attempted{},callbacks_valid{};
    void inspect_callbacks(uintptr_t outer) noexcept {
        if(callbacks_attempted)return;
        callbacks_attempted=true;callback_count=0;callbacks_valid=false;
        __try {
            const auto list=outer+0xA30;
            callback_total=*reinterpret_cast<int*>(list+0x50);
            if(callback_total<0 || callback_total>64)return;
            const auto heap=*reinterpret_cast<uintptr_t*>(list+0x40);
            const auto storage=heap?heap:list;
            for(int i=callback_total-1;i>=0;--i){
                auto& c=callbacks[callback_count++];c={};c.index=static_cast<unsigned>(i);
                const auto entry=storage+static_cast<uintptr_t>(i)*0x40;
                c.active=*reinterpret_cast<unsigned*>(entry+0x30);
                if(!c.active)continue;
                const auto pointer=*reinterpret_cast<uintptr_t*>(entry+0x20);
                c.object=pointer?pointer:entry;
                c.vtable=*reinterpret_cast<uintptr_t*>(c.object);
                c.target=*reinterpret_cast<uintptr_t*>(c.vtable+0x68);
                for(unsigned j=0;j<c.words.size();++j)c.words[j]=*reinterpret_cast<uintptr_t*>(c.object+j*8);
                // Read-only equivalent of the pinned weak-reference checks; never call game code.
                const auto object_index=static_cast<int32_t>(c.words[1]);
                const auto serial=static_cast<uint32_t>(c.words[1]>>32);
                const auto object_count=*reinterpret_cast<int32_t*>(base+0x42A115C);
                if(serial && object_index>=0 && object_index<object_count){
                    const auto items=*reinterpret_cast<uintptr_t*>(base+0x42A1150);
                    if(items){
                        const auto item=items+static_cast<uintptr_t>(object_index)*24;
                        if(*reinterpret_cast<uint32_t*>(item+0x10)==serial && !(*reinterpret_cast<uint32_t*>(item+8)&0x30000000)){
                            const auto object=*reinterpret_cast<uintptr_t*>(item);
                            if(object){
                                c.resolved_object=object+static_cast<int32_t>(c.words[3]);
                                c.resolved_target=c.words[2];
                                if(c.words[2]==base+0x8954A4){
                                    const auto table=*reinterpret_cast<uintptr_t*>(c.resolved_object);
                                    c.resolved_target=*reinterpret_cast<uintptr_t*>(table+0x5F8);
                                }
                            }
                        }
                    }
                }
            }
            callbacks_valid=true;
        }__except(EXCEPTION_EXECUTE_HANDLER){callbacks_valid=false;}
    }
    std::array<Event,256> events{};
    std::atomic<bool> enabled{false};
    SRWLOCK lock=SRWLOCK_INIT;
    DWORD owner{};uintptr_t base{};unsigned phase{},count{},dropped{};
    void start(uintptr_t module){base=module;owner=GetCurrentThreadId();phase=1;count=dropped=0;training_field_reset.latest={};callback_count=0;callback_total=0;callbacks_attempted=callbacks_valid=false;setter_count=0;setter_fault=setter_overflow=false;enabled.store(true,std::memory_order_release);}
    void stop(){enabled.store(false,std::memory_order_release);AcquireSRWLockExclusive(&lock);ReleaseSRWLockExclusive(&lock);}
    void record(const uintptr_t* registers,uintptr_t caller,unsigned assignment=0) noexcept {
        const DWORD error=GetLastError();
        if(!enabled.load(std::memory_order_acquire) || !TryAcquireSRWLockExclusive(&lock)){SetLastError(error);return;}
        if(enabled.load(std::memory_order_acquire) && GetCurrentThreadId()==owner && unsigned(registers[1])==2){
            if(count==events.size())++dropped;
            else {
                auto& e=events[count++];e={};e.player=registers[0];e.caller=caller;
                e.thread=owner;e.phase=phase;e.index=unsigned(registers[1]);e.flags=unsigned(registers[3]);e.delta=short(registers[2]);
                e.assignment=assignment;
                __try {e.before=*reinterpret_cast<short*>(e.player+0x1B3C);
                    e.counter=*reinterpret_cast<unsigned*>(base+0x470D0C4);e.readable=true;
                }__except(EXCEPTION_EXECUTE_HANDLER){e.readable=false;}
            }
        }
        ReleaseSRWLockExclusive(&lock);SetLastError(error);
    }
    void record_reset(const uintptr_t* registers,uintptr_t caller) noexcept {
        const DWORD error=GetLastError();
        if(enabled.load(std::memory_order_acquire) && GetCurrentThreadId()==owner){
            __try {
                if(registers[0] && registers[1]){
                    const short requested=*reinterpret_cast<short*>(registers[1]+0xC);
                    // The native routine skips the assignment for negative values.
                    if(requested>=0){uintptr_t args[]={registers[0],2,static_cast<uintptr_t>(requested),0};record(args,caller,true);}
                }
            }__except(EXCEPTION_EXECUTE_HANDLER){}
        }
        SetLastError(error);
    }
};
inline FieldUpdateTrace field_update_trace;
}
