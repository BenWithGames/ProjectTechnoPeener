#pragma once
#include <Windows.h>
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include <intrin.h>
#include "ResourceLeaseService.hpp"
#include "ResourceLeaseRuntime.hpp"
#include "ResourceDependencyCapture.hpp"
namespace sc6::research::resource_clear {
using Original=uintptr_t(*)(uintptr_t);
inline Original original{};
inline bool installed{};
inline std::atomic<unsigned> events{},same_thread{},other_thread{},unknown_thread{};
inline std::atomic<DWORD> last_thread{};
inline bool try_lock(uintptr_t object){
    if(object<0x10000 || object>UINTPTR_MAX-0x230)return false;
    __try {return TryEnterCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(object+0x208))!=0;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline uintptr_t observe(uintptr_t object,Original invoke,uintptr_t caller=0){
    const auto error=GetLastError();const auto thread=GetCurrentThreadId();
    ++events;last_thread=thread;
    const auto owner=lease_service::owner.load();
    if(!owner)++unknown_thread;else if(owner==thread)++same_thread;else ++other_thread;
    lease_runtime::before_clear();
    if(const auto notify=dependency_events::clear.load())notify();
    if(const auto scoped=dependency_events::scoped_clear.load()){
        // Keep the lock through the native clear (Windows critical sections are
        // recursive), preventing mutation between membership check and removal.
        const bool locked=try_lock(object);
        __try {scoped(object,caller,locked);SetLastError(error);return invoke(object);}
        __finally {if(locked){const auto last=GetLastError();LeaveCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(object+0x208));SetLastError(last);}}
    }
    SetLastError(error);return invoke(object);
}
inline uintptr_t bridge(uintptr_t object){return observe(object,original,reinterpret_cast<uintptr_t>(_ReturnAddress()));}
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
