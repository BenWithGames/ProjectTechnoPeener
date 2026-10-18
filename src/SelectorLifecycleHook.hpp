#pragma once
#include "SelectorLifetime.hpp"
#include <MinHook.h>
#include <cstring>
namespace sc6::research::selector_lifetime {
using Initialize=uintptr_t(*)(uintptr_t,uintptr_t);
using Dispose=void(*)(uintptr_t);
inline Initialize initialize_original{};
inline Dispose dispose_original{};
inline bool attempted{};
inline uintptr_t initialize(uintptr_t object,uintptr_t resource){
    const auto incoming=GetLastError();AcquireSRWLockExclusive(&lock);++epoch;
    uintptr_t value{};DWORD error=incoming;
    __try {SetLastError(incoming);value=initialize_original(object,resource);error=GetLastError();}
    __finally {++epoch;ReleaseSRWLockExclusive(&lock);SetLastError(error);}
    return value;
}
inline void dispose(uintptr_t object){
    const auto incoming=GetLastError();AcquireSRWLockExclusive(&lock);++epoch;
    DWORD error=incoming;
    __try {SetLastError(incoming);dispose_original(object);error=GetLastError();}
    __finally {++epoch;ReleaseSRWLockExclusive(&lock);SetLastError(error);}
}
inline bool install(uintptr_t base){
    if(attempted)return installed && module==base;
    attempted=true;module=base;
    const unsigned char init[]={0x48,0x89,0x5c,0x24,0x20,0x57,0x48,0x83,0xec,0x20,0xc7,0x81,0x58,0x02,0x00,0x00};
    const unsigned char free[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x91,0x60,0x02,0x00,0x00,0x48,0x8b,0xd9};
    if(!base || std::memcmp(reinterpret_cast<void*>(base+0x38EFA0),init,sizeof(init)) ||
       std::memcmp(reinterpret_cast<void*>(base+0x38F110),free,sizeof(free)))return false;
    const auto status=MH_Initialize();if(status!=MH_OK && status!=MH_ERROR_ALREADY_INITIALIZED)return false;
    void* a=reinterpret_cast<void*>(base+0x38EFA0);void* b=reinterpret_cast<void*>(base+0x38F110);
    if(MH_CreateHook(a,reinterpret_cast<void*>(&initialize),reinterpret_cast<void**>(&initialize_original))!=MH_OK)return false;
    if(MH_CreateHook(b,reinterpret_cast<void*>(&dispose),reinterpret_cast<void**>(&dispose_original))!=MH_OK){MH_RemoveHook(a);return false;}
    if(MH_QueueEnableHook(a)!=MH_OK || MH_QueueEnableHook(b)!=MH_OK || MH_ApplyQueued()!=MH_OK){
        MH_QueueDisableHook(a);MH_QueueDisableHook(b);MH_ApplyQueued();MH_RemoveHook(a);MH_RemoveHook(b);return false;
    }
    installed=true;return true;
}
}
