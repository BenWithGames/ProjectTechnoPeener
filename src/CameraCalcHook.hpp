#pragma once
#include "CameraCalcTrace.hpp"
#include <MinHook.h>
#include <cstring>
namespace sc6::research::camera_calc {
extern "C" {
inline void (*SC6CameraCalcCallback)(const uint64_t*,unsigned,const void*){};
inline void* SC6CameraCalcOriginal0{}; void SC6CameraCalcBridge0();
inline void* SC6CameraCalcOriginal1{}; void SC6CameraCalcBridge1();
inline void* SC6CameraCalcOriginal2{}; void SC6CameraCalcBridge2();
inline void* SC6CameraCalcOriginal3{}; void SC6CameraCalcBridge3();
inline void* SC6CameraCalcOriginal4{}; void SC6CameraCalcBridge4();
inline void* SC6CameraCalcOriginal5{}; void SC6CameraCalcBridge5();


}
inline bool attempted{},installed{};
inline bool install(uintptr_t base){
if(attempted)return installed && module==base; attempted=true;module=base;SC6CameraCalcCallback=&observe;
const unsigned rvas[]={0x31ea88,0x31ea9b,0x31eaa0,0x31eac8,0x31eb5a,0x31ec12};
const unsigned char signatures[][16]={{0x33,0xd2,0xe8,0xf1,0xf5,0xff,0xff,0x48,0x8d,0x15,0x6a,0xff,0x3e,0x4,0xe8,0x75},{0xe8,0xf0,0x3,0x0,0x0,0x48,0x8b,0x5,0x59,0xff,0x3e,0x4,0x48,0x8d,0x15,0x22},{0x48,0x8b,0x5,0x59,0xff,0x3e,0x4,0x48,0x8d,0x15,0x22,0x1,0x3f,0x4,0x48,0x8d},{0x83,0x3d,0x75,0x2,0x3f,0x4,0x0,0x48,0x8d,0xd,0x82,0x8f,0xb6,0x3,0xf3,0xf},{0xf3,0xf,0x10,0x8c,0x24,0x50,0x1,0x0,0x0,0x48,0x8d,0xd,0xa6,0x0,0x3f,0x4},{0x48,0x8b,0x8c,0x24,0xf0,0x1,0x0,0x0,0x48,0x33,0xcc,0xe8,0x6e,0xb3,0xdf,0x2}};
void* bridges[]={reinterpret_cast<void*>(&SC6CameraCalcBridge0),reinterpret_cast<void*>(&SC6CameraCalcBridge1),reinterpret_cast<void*>(&SC6CameraCalcBridge2),reinterpret_cast<void*>(&SC6CameraCalcBridge3),reinterpret_cast<void*>(&SC6CameraCalcBridge4),reinterpret_cast<void*>(&SC6CameraCalcBridge5)};
void** originals[]={&SC6CameraCalcOriginal0,&SC6CameraCalcOriginal1,&SC6CameraCalcOriginal2,&SC6CameraCalcOriginal3,&SC6CameraCalcOriginal4,&SC6CameraCalcOriginal5};
for(unsigned i=0;i<6;++i)if(std::memcmp(reinterpret_cast<void*>(base+rvas[i]),signatures[i],16))return false;
auto init=MH_Initialize();if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED)return false;
unsigned made=0;for(;made<6;++made)if(MH_CreateHook(reinterpret_cast<void*>(base+rvas[made]),bridges[made],originals[made])!=MH_OK)break;
if(made!=6){for(unsigned i=0;i<made;++i)MH_RemoveHook(reinterpret_cast<void*>(base+rvas[i]));return false;}
bool ok=true;for(unsigned i=0;i<6;++i)ok=MH_QueueEnableHook(reinterpret_cast<void*>(base+rvas[i]))==MH_OK && ok;
if(!ok || MH_ApplyQueued()!=MH_OK){for(unsigned i=0;i<6;++i){MH_QueueDisableHook(reinterpret_cast<void*>(base+rvas[i]));MH_DisableHook(reinterpret_cast<void*>(base+rvas[i]));MH_RemoveHook(reinterpret_cast<void*>(base+rvas[i]));}return false;}
AcquireSRWLockExclusive(&lock);trace.installed=1;ReleaseSRWLockExclusive(&lock);installed=true;return true;
}
}
