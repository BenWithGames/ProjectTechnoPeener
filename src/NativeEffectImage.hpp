#pragma once
#include <Windows.h>
#include <cstring>
#include <cstddef>
#include "EffectAllocationSites.hpp"
#include "EffectEnvironment.hpp"
namespace sc6::research {
// Exact allocation-byte copy, not a validated restorer. Caller must hold the
// lifetime gate and bind successful images to allocation generations immediately.
template<unsigned Capacity=64> struct NativeEffectImage {
    struct Object {uintptr_t address{},vtable{};unsigned size{};uint64_t generation{};std::array<std::byte,0x1E0> bytes{};};
    std::array<Object,Capacity> objects{};
    EffectEnvironment environment{};
    uint64_t snapshot_serial{},session{},round{},frame{};unsigned snapshot_slot{};
    uintptr_t manager{},head{};unsigned count{},exception{};bool valid{};
    static uintptr_t pointer(const void* source) noexcept {uintptr_t value{};std::memcpy(&value,source,sizeof(value));return value;}
    bool capture(uintptr_t manager_address,uintptr_t module) noexcept {
        environment={};snapshot_serial=session=round=frame=0;snapshot_slot=0;
        valid=false;count=0;exception=0;manager=manager_address;head=0;
        if(!manager || !module || module>UINTPTR_MAX-0x3E88D18)return false;
        __try {
            head=pointer(reinterpret_cast<void*>(manager));auto address=head;uintptr_t previous=0;
            while(address){
                if(count==Capacity)return false;
                for(unsigned i=0;i<count;++i)if(objects[i].address==address)return false;
                const auto type=pointer(reinterpret_cast<void*>(address));unsigned bytes=0;
                for(const auto& site:effect_allocation_sites)if(type==module+site.vtable)bytes=static_cast<unsigned>(site.bytes);
                if(!bytes || address>UINTPTR_MAX-bytes)return false;
                auto& object=objects[count];object.address=address;object.vtable=type;object.size=bytes;object.generation=0;
                std::memcpy(object.bytes.data(),reinterpret_cast<void*>(address),bytes);
                if(pointer(object.bytes.data())!=type || pointer(object.bytes.data()+0x18)!=previous || pointer(object.bytes.data()+0x28)!=manager)return false;
                previous=address;address=pointer(object.bytes.data()+0x10);++count;
            }
            if(pointer(reinterpret_cast<void*>(manager))!=head)return false;
            for(unsigned i=0;i<count;++i){const auto& object=objects[i];
                if(std::memcmp(object.bytes.data(),reinterpret_cast<void*>(object.address),object.size))return false;
            }
            valid=true;return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){exception=GetExceptionCode();return false;}
    }
};
}
