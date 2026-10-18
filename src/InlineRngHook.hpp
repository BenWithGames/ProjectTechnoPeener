#pragma once
#include "InlineRngTrace.hpp"
#include <MinHook.h>
#include <cstring>
namespace sc6::research::inline_rng {
extern "C" {
inline void (*SC6InlineRngCallback)(const uint64_t*,unsigned){};
inline void* SC6InlineRngOriginal0{}; void SC6InlineRngBridge0();
inline void* SC6InlineRngOriginal1{}; void SC6InlineRngBridge1();
inline void* SC6InlineRngOriginal2{}; void SC6InlineRngBridge2();
inline void* SC6InlineRngOriginal3{}; void SC6InlineRngBridge3();
inline void* SC6InlineRngOriginal4{}; void SC6InlineRngBridge4();
inline void* SC6InlineRngOriginal5{}; void SC6InlineRngBridge5();
inline void* SC6InlineRngOriginal6{}; void SC6InlineRngBridge6();
inline void* SC6InlineRngOriginal7{}; void SC6InlineRngBridge7();
}
inline bool attempted{},installed{};
inline bool install(uintptr_t base){
if(attempted)return installed && module==base; attempted=true;module=base;SC6InlineRngCallback=&observe;
const unsigned rvas[]={0x2f9f1e,0x2fa159,0x33154e,0x331927,0x33220d,0x3330a4,0x3346b3,0x34f867};
const unsigned char signatures[][6]={{0x89,0x5,0x4,0x4c,0x56,0x4},{0x89,0x5,0xc9,0x49,0x56,0x4},{0x89,0x5,0xd4,0xd5,0x52,0x4},{0x89,0x5,0xfb,0xd1,0x52,0x4},{0x89,0x5,0x15,0xc9,0x52,0x4},{0x89,0x5,0x7e,0xba,0x52,0x4},{0x89,0x1d,0x6f,0xa4,0x52,0x4},{0x44,0x89,0x5,0xba,0xf2,0x50}};
void* bridges[]={reinterpret_cast<void*>(&SC6InlineRngBridge0),reinterpret_cast<void*>(&SC6InlineRngBridge1),reinterpret_cast<void*>(&SC6InlineRngBridge2),reinterpret_cast<void*>(&SC6InlineRngBridge3),reinterpret_cast<void*>(&SC6InlineRngBridge4),reinterpret_cast<void*>(&SC6InlineRngBridge5),reinterpret_cast<void*>(&SC6InlineRngBridge6),reinterpret_cast<void*>(&SC6InlineRngBridge7)};
void** originals[]={&SC6InlineRngOriginal0,&SC6InlineRngOriginal1,&SC6InlineRngOriginal2,&SC6InlineRngOriginal3,&SC6InlineRngOriginal4,&SC6InlineRngOriginal5,&SC6InlineRngOriginal6,&SC6InlineRngOriginal7};
for(unsigned i=0;i<8;++i)if(std::memcmp(reinterpret_cast<void*>(base+rvas[i]),signatures[i],6))return false;
auto init=MH_Initialize();if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED)return false;
unsigned made=0;for(;made<8;++made)if(MH_CreateHook(reinterpret_cast<void*>(base+rvas[made]),bridges[made],originals[made])!=MH_OK)break;
if(made!=8){for(unsigned i=0;i<made;++i)MH_RemoveHook(reinterpret_cast<void*>(base+rvas[i]));return false;}
bool ok=true;for(unsigned i=0;i<8;++i)ok=MH_QueueEnableHook(reinterpret_cast<void*>(base+rvas[i]))==MH_OK && ok;
if(!ok || MH_ApplyQueued()!=MH_OK){for(unsigned i=0;i<8;++i){MH_QueueDisableHook(reinterpret_cast<void*>(base+rvas[i]));MH_DisableHook(reinterpret_cast<void*>(base+rvas[i]));MH_RemoveHook(reinterpret_cast<void*>(base+rvas[i]));}return false;}
AcquireSRWLockExclusive(&lock);trace.installed=1;ReleaseSRWLockExclusive(&lock);installed=true;return true;
}
}
