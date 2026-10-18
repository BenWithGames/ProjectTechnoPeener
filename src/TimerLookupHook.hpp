#pragma once
#include "TimerLookupTrace.hpp"
#include <MinHook.h>
#include <cstring>
namespace sc6::research::timer_lookup {
extern "C" {
inline void (*SC6TimerLookupCallback)(const uint64_t*,unsigned,const void*){};
inline void* SC6TimerLookupOriginal0{}; void SC6TimerLookupBridge0();
inline void* SC6TimerLookupOriginal1{}; void SC6TimerLookupBridge1();
inline void* SC6TimerLookupOriginal2{}; void SC6TimerLookupBridge2();
inline void* SC6TimerLookupOriginal3{}; void SC6TimerLookupBridge3();
inline void* SC6TimerLookupOriginal4{}; void SC6TimerLookupBridge4();
inline void* SC6TimerLookupOriginal5{}; void SC6TimerLookupBridge5();
inline void* SC6TimerLookupOriginal6{}; void SC6TimerLookupBridge6();
inline void* SC6TimerLookupOriginal7{}; void SC6TimerLookupBridge7();
}
inline bool attempted{},installed{};
inline bool install(uintptr_t base){
if(attempted)return installed && module==base; attempted=true;module=base;SC6TimerLookupCallback=&observe;
const unsigned rvas[]={0x349840,0x3498eb,0x3499d0,0x349a59,0x349ad9,0x349b4b,0x2f943e,0x2f90d0};
const unsigned char signatures[][16]={{0x48,0x89,0x5c,0x24,0x8,0x45,0x8b,0xd8,0x48,0x63,0xda,0x41,0xb9,0x3e,0x0,0x0},{0xf3,0xf,0x11,0x9c,0xa,0xac,0x72,0x9,0x0,0xc7,0x84,0xa,0xa8,0x72,0x9,0x0},{0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x41,0x56,0x48,0x83,0xec,0x20},{0x49,0x63,0x42,0x38,0xf3,0x41,0xf,0x10,0x52,0x40,0x48,0x89,0x6c,0x24,0x30,0x49},{0xf3,0xf,0x5e,0x25,0x33,0xa,0xb4,0x3,0xf,0x2f,0xc4,0x76,0x5,0xf,0x28,0xc8},{0x48,0x8b,0x5c,0x24,0x40,0x48,0x8b,0x7c,0x24,0x48,0x48,0x83,0xc4,0x20,0x41,0x5e},{0xf3,0xf,0x10,0x44,0x24,0x70,0x44,0x8b,0x4c,0x24,0x78,0x48,0x8b,0x45,0x8,0xf},{0x48,0x8b,0xc4,0x55,0x41,0x55,0x48,0x83,0xec,0x58,0x48,0x89,0x70,0x20,0x4c,0x8b}};
void* bridges[]={reinterpret_cast<void*>(&SC6TimerLookupBridge0),reinterpret_cast<void*>(&SC6TimerLookupBridge1),reinterpret_cast<void*>(&SC6TimerLookupBridge2),reinterpret_cast<void*>(&SC6TimerLookupBridge3),reinterpret_cast<void*>(&SC6TimerLookupBridge4),reinterpret_cast<void*>(&SC6TimerLookupBridge5),reinterpret_cast<void*>(&SC6TimerLookupBridge6),reinterpret_cast<void*>(&SC6TimerLookupBridge7)};
void** originals[]={&SC6TimerLookupOriginal0,&SC6TimerLookupOriginal1,&SC6TimerLookupOriginal2,&SC6TimerLookupOriginal3,&SC6TimerLookupOriginal4,&SC6TimerLookupOriginal5,&SC6TimerLookupOriginal6,&SC6TimerLookupOriginal7};
for(unsigned i=0;i<8;++i)if(std::memcmp(reinterpret_cast<void*>(base+rvas[i]),signatures[i],16))return false;
auto init=MH_Initialize();if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED)return false;
unsigned made=0;for(;made<8;++made)if(MH_CreateHook(reinterpret_cast<void*>(base+rvas[made]),bridges[made],originals[made])!=MH_OK)break;
if(made!=8){for(unsigned i=0;i<made;++i)MH_RemoveHook(reinterpret_cast<void*>(base+rvas[i]));return false;}
bool ok=true;for(unsigned i=0;i<8;++i)ok=MH_QueueEnableHook(reinterpret_cast<void*>(base+rvas[i]))==MH_OK && ok;
if(!ok || MH_ApplyQueued()!=MH_OK){for(unsigned i=0;i<8;++i){MH_QueueDisableHook(reinterpret_cast<void*>(base+rvas[i]));MH_DisableHook(reinterpret_cast<void*>(base+rvas[i]));MH_RemoveHook(reinterpret_cast<void*>(base+rvas[i]));}return false;}
AcquireSRWLockExclusive(&lock);trace.installed=1;ReleaseSRWLockExclusive(&lock);installed=true;return true;
}
}
