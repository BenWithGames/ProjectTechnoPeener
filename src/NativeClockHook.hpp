#pragma once
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include "NativeClockDispatch.hpp"
#include "CombatRouteBundle.hpp"
#include "NativeStartHold.hpp"
#include "NativeClockDiscovery.hpp"
#include "CombatResourcePreparation.hpp"
namespace sc6::research::native_clock_hook {
using Dispatch=NativeClockDispatch<>;
inline Dispatch::Original original{};
inline std::atomic<Dispatch*> route{};
inline std::atomic<NativeStartHold<>*> start_hold{};
struct StartupPoll {void* context{};void (*poll)(void*,uintptr_t)noexcept{};};
inline std::atomic<const StartupPoll*> startup_poll{};
inline std::atomic<const StartupPoll*> preparation_poll{};
inline bool installed{};
inline uintptr_t installed_base{};
inline NativeClockDiscovery discovery;
inline void bridge(uintptr_t object) {
    const auto now=GetTickCount64();
    // Owner-thread consumers use this entry's inspection, even when a reader
    // prevents publication to the optional cross-thread discovery cache.
    const auto observed=installed_base?discovery.observe(installed_base,object,now):NativeClockDiscovery::Observation{};
    if(auto* resources=combat_resource_preparation.load(std::memory_order_acquire)){
        if(resources->replay_restart())return;
        resources->clock_entry(observed,object,now);
        if(!combat_routes.load() && !start_hold.load() && !route.load() &&
            !startup_poll.load() && !preparation_poll.load() && resources->run_hold(observed,object,now))return;
    }
    if(const auto* preparation=preparation_poll.load(std::memory_order_acquire))preparation->poll(preparation->context,object);
    // Owner-thread startup polling must precede servicing/advancement. A held
    // clock may produce no battle entry, so battle-only polling deadlocks.
    if(const auto* startup=startup_poll.load(std::memory_order_acquire))startup->poll(startup->context,object);
    if(const auto* routes=combat_routes.load(std::memory_order_acquire)){
        if(routes->retired()){
            if(auto* hold=start_hold.load(std::memory_order_acquire);hold && hold->matches(object))hold->run(object,now);
            else original(object);
        }
        else routes->clock->run(object,original);
    }
    else if(auto* hold=start_hold.load(std::memory_order_acquire);hold && hold->matches(object))hold->run(object,GetTickCount64());
    else if(auto* dispatch=route.load(std::memory_order_acquire))dispatch->run(object,original);
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
