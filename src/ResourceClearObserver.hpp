#pragma once
#include <Windows.h>
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include "ResourceLeaseService.hpp"
#include "ResourceLeaseRuntime.hpp"
#include "ResourceDependencyCapture.hpp"
namespace sc6::research::resource_clear {
using Original=uintptr_t(*)(uintptr_t);
inline Original original{};
inline bool installed{};
inline std::atomic<unsigned> events{},same_thread{},other_thread{},unknown_thread{};
inline std::atomic<DWORD> last_thread{};
inline uintptr_t observe(uintptr_t object,Original invoke){
    const auto error=GetLastError();const auto thread=GetCurrentThreadId();
    ++events;last_thread=thread;
    const auto owner=lease_service::owner.load();
    if(!owner)++unknown_thread;else if(owner==thread)++same_thread;else ++other_thread;
    lease_runtime::before_clear();
    if(const auto notify=dependency_events::clear.load())notify();
    SetLastError(error);return invoke(object);
}
inline uintptr_t bridge(uintptr_t object){return observe(object,original);}
inline bool install(uintptr_t base){
    if(installed)return true;
    constexpr unsigned char prefix[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57};
    auto target=reinterpret_cast<void*>(base+0x899110);
    if(std::memcmp(target,prefix,sizeof(prefix)))return false;
    const auto init=MH_Initialize();if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED)return false;
    if(MH_CreateHook(target,reinterpret_cast<void*>(&bridge),reinterpret_cast<void**>(&original))!=MH_OK)return false;
    if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);return false;}
    installed=true;return true;
}
}
