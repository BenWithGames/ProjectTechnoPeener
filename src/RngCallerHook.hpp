#pragma once
#include "RngCallerTrace.hpp"
#include <MinHook.h>
#include <intrin.h>
#include <cstring>
namespace sc6::research::rng_trace {
using Routine=float(*)();

inline uintptr_t module{};
inline bool attempted{},installed{};
extern "C" {
inline Routine SC6RngOriginal{};
inline const unsigned* SC6RngSeed{};
inline const unsigned* SC6RngFrame{};
void SC6RngBridge();
inline void (*SC6RngCallback)(uintptr_t,unsigned,unsigned,unsigned){};
inline void SC6RngRecord(uintptr_t caller,unsigned frame,unsigned before,unsigned after){
    const auto error=GetLastError();
    record(caller>=module?caller-module:caller,frame,before,after);
    SetLastError(error);
}
}
inline bool install(uintptr_t base){
    if(attempted)return installed && module==base;
    attempted=true;module=base;
    // Verified no-argument float-returning helper; pinned first two instructions.
    const unsigned char signature[]={0x44,0x8B,0x05,0xD1,0xF5,0x50,0x04,0xB8,0x5F,0x0B,0x4E,0x83};
    if(std::memcmp(reinterpret_cast<const void*>(base+0x34F550),signature,sizeof(signature)))return false;
    const auto init=MH_Initialize();if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED)return false;
    auto target=reinterpret_cast<void*>(base+0x34F550);
    if(MH_CreateHook(target,reinterpret_cast<void*>(&SC6RngBridge),reinterpret_cast<void**>(&SC6RngOriginal))!=MH_OK)return false;
    SC6RngCallback=&SC6RngRecord;
    SC6RngSeed=reinterpret_cast<const unsigned*>(base+0x485EB28);
    SC6RngFrame=reinterpret_cast<const unsigned*>(base+0x470D0C4);
    if(MH_EnableHook(target)!=MH_OK)return false;
    installed=true;
    AcquireSRWLockExclusive(&trace_lock);trace.installed=1;ReleaseSRWLockExclusive(&trace_lock);
    return true;
}
}
