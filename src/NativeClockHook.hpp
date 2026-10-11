#pragma once
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include "NativeClockDispatch.hpp"
namespace sc6::research::native_clock_hook {
using Dispatch=NativeClockDispatch<>;
inline Dispatch::Original original{};
inline std::atomic<Dispatch*> route{};
inline bool installed{};
inline uintptr_t installed_base{};
inline void bridge(uintptr_t object) {
    if(auto* dispatch=route.load(std::memory_order_acquire))dispatch->run(object,original);
    else original(object);
}
// The route must have process lifetime, with an already-bound game-thread
// clock and a battle-boundary reservation owner. Publication is one-way; a
// modified simulation cannot silently fall back to stock after a fault.
// No production caller publishes a route until the combat backend is ready.
inline bool publish(Dispatch& dispatch){
    if(!installed || !dispatch.active() || dispatch.restart_required())return false;
    Dispatch* empty=nullptr;
    return route.compare_exchange_strong(empty,&dispatch,std::memory_order_release,std::memory_order_relaxed);
}
inline bool install(uintptr_t base){
    if(installed)return installed_base==base;
    // recorder_init has already verified the complete executable hash.
    constexpr unsigned char bytes[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0x01,0x48,0x8B,0xD9};
    auto target=reinterpret_cast<void*>(base+0x3F5D20);
    if(std::memcmp(target,bytes,sizeof(bytes)))return false;
    const auto init=MH_Initialize();if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED)return false;
    if(MH_CreateHook(target,reinterpret_cast<void*>(&bridge),reinterpret_cast<void**>(&original))!=MH_OK)return false;
    if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);original=nullptr;return false;}
    installed_base=base;installed=true;return true;
}
}
