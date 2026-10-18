#pragma once
#include "SharedRngTrace.hpp"
#include <MinHook.h>
namespace sc6::research::shared_rng {
extern "C" {
inline void (*SC6SharedRngOriginal1)(){};
inline void (*SC6SharedRngOriginal2)(){};
void SC6SharedRngBridge1();void SC6SharedRngBridge2();
inline void (*SC6SharedRngCallback)(uintptr_t,unsigned,uint64_t){};
}
inline bool attempted{},installed{};
inline bool install(uintptr_t base){
 if(attempted)return installed && module==base;
 attempted=true;module=base;SC6SharedRngCallback=&record;
 const unsigned char first[]={0x8b,0x15,0x5e,0xfa,0x50,0x04,0x4c,0x8d,0x15,0xf3,0xf9,0x50,0x04,0x83,0xfa,0x19};
 const unsigned char second[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18};
 if(std::memcmp(reinterpret_cast<void*>(base+0x34F130),first,sizeof(first)) || std::memcmp(reinterpret_cast<void*>(base+0x34F3C0),second,sizeof(second)))return false;
 const auto init=MH_Initialize();if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED)return false;
 void* a=reinterpret_cast<void*>(base+0x34F130);void* b=reinterpret_cast<void*>(base+0x34F3C0);
 if(MH_CreateHook(a,reinterpret_cast<void*>(&SC6SharedRngBridge1),reinterpret_cast<void**>(&SC6SharedRngOriginal1))!=MH_OK)return false;
 if(MH_CreateHook(b,reinterpret_cast<void*>(&SC6SharedRngBridge2),reinterpret_cast<void**>(&SC6SharedRngOriginal2))!=MH_OK){MH_RemoveHook(a);return false;}
 if(MH_EnableHook(a)!=MH_OK){MH_RemoveHook(b);MH_RemoveHook(a);return false;}
 if(MH_EnableHook(b)!=MH_OK){MH_DisableHook(a);MH_RemoveHook(a);MH_RemoveHook(b);return false;}
 installed=true;AcquireSRWLockExclusive(&lock);trace.installed=1;ReleaseSRWLockExclusive(&lock);return true;
}
}
