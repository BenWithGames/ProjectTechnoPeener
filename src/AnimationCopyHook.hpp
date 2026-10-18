#pragma once
#include "AnimationCopyTrace.hpp"
#include <MinHook.h>
namespace sc6::research::animation_copy {
extern "C" {
 inline void* SC6AnimationEntryOriginal{};
 inline void* SC6AnimationBeforeOriginal{};
 inline void* SC6AnimationAfterOriginal{};
 inline void* SC6MatrixEntryOriginal{}; inline void* SC6MatrixBeforeOriginal{}; inline void* SC6MatrixAfterOriginal{};
 void SC6MatrixEntryBridge();void SC6MatrixBeforeBridge();void SC6MatrixAfterBridge();
 inline void (*SC6AnimationCallback)(uintptr_t,uintptr_t,uintptr_t,unsigned){};
 void SC6AnimationEntryBridge();void SC6AnimationBeforeBridge();void SC6AnimationAfterBridge();
}
inline bool attempted{},installed{};
inline bool install(uintptr_t base){
 if(attempted)return installed && module==base;
 attempted=true;module=base;SC6AnimationCallback=&observe;
 const unsigned rvas[]={0x2F0F57,0x2F355D,0x2F35E0,0x305D97,0x2F0CF0,0x2F0D73};
 const unsigned char signatures[][7]={{0x48,0x83,0x7F,0x18,0,0x4C,0x8B},{0x80,0xBD,0x58,0x37,0,0,0},{0x80,0xBD,0x59,0x37,0,0,0},{0xE8,0xF4,0x7D,0xFE,0xFF,0xF3,0x0F},{0x80,0xBD,0x28,0x37,0,0,0},{0x80,0xBD,0x29,0x37,0,0,0}};
 void* bridges[]={reinterpret_cast<void*>(&SC6AnimationEntryBridge),reinterpret_cast<void*>(&SC6AnimationBeforeBridge),reinterpret_cast<void*>(&SC6AnimationAfterBridge),reinterpret_cast<void*>(&SC6MatrixEntryBridge),reinterpret_cast<void*>(&SC6MatrixBeforeBridge),reinterpret_cast<void*>(&SC6MatrixAfterBridge)};
 void** originals[]={&SC6AnimationEntryOriginal,&SC6AnimationBeforeOriginal,&SC6AnimationAfterOriginal,&SC6MatrixEntryOriginal,&SC6MatrixBeforeOriginal,&SC6MatrixAfterOriginal};
 for(unsigned i=0;i<6;++i)if(std::memcmp(reinterpret_cast<void*>(base+rvas[i]),signatures[i],7))return false;
 auto init=MH_Initialize();if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED)return false;
 unsigned made=0;
 for(;made<6;++made)if(MH_CreateHook(reinterpret_cast<void*>(base+rvas[made]),bridges[made],originals[made])!=MH_OK)break;
 if(made!=6){for(unsigned i=0;i<made;++i)MH_RemoveHook(reinterpret_cast<void*>(base+rvas[i]));return false;}
 // Enable together: a failed install must not leave partial observation active.
 for(unsigned i=0;i<6;++i)if(MH_QueueEnableHook(reinterpret_cast<void*>(base+rvas[i]))!=MH_OK){
  for(unsigned j=0;j<6;++j){MH_QueueDisableHook(reinterpret_cast<void*>(base+rvas[j]));MH_RemoveHook(reinterpret_cast<void*>(base+rvas[j]));}return false;}
 if(MH_ApplyQueued()!=MH_OK){for(unsigned i=0;i<6;++i){MH_DisableHook(reinterpret_cast<void*>(base+rvas[i]));MH_RemoveHook(reinterpret_cast<void*>(base+rvas[i]));}return false;}
 AcquireSRWLockExclusive(&lock);trace.installed=1;ReleaseSRWLockExclusive(&lock);installed=true;return true;
}
}
