#pragma once
#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <shared_mutex>
#include "EffectMemoryGate.hpp"
namespace sc6::research::effect_release {
using NativeRelease=uintptr_t(*)(uintptr_t,uintptr_t);
// Immutable route, owned for the entire hook lifetime. Publish/remove only while
// game-thread hook users are quiescent; withdrawal waits for routed callbacks.
// defer must return true only after taking ownership of an exact tracked token.
// It must not call an allocator, reenter the hook, or throw.
struct Route {
    DWORD owner{};
    uintptr_t return_address{};
    void* context{};
    bool (*defer)(void*,uintptr_t,uintptr_t) noexcept{};
    // Thread-safe callback, runs before every forwarded native release, including
    // other callers/threads and nested dispatch. Must not allocate or reenter.
    void (*before_native)(void*,uintptr_t) noexcept{};
    const std::atomic<bool>* stopped{};
    EffectMemoryGate* gate{};
    void (*allocation_return)(void*,uintptr_t,uintptr_t,uintptr_t){};
};
inline std::atomic<const Route*> route{};
inline std::shared_mutex route_mutex;
inline bool publish(const Route* value){std::unique_lock lock(route_mutex);if(route.load())return false;route.store(value,std::memory_order_release);return true;}
inline bool withdraw(const Route* value){std::unique_lock lock(route_mutex);if(route.load()!=value)return false;route.store(nullptr,std::memory_order_release);return true;}
inline thread_local bool dispatching{};
inline uintptr_t dispatch(uintptr_t address,uintptr_t argument,uintptr_t caller,NativeRelease native){
    const auto error=GetLastError();
    bool deferred=false;
    {
    std::shared_lock route_lock(route_mutex);
    const auto* selected=route.load(std::memory_order_acquire);
    const auto decision=[&]{
    if(!dispatching && selected && selected->gate && selected->owner==GetCurrentThreadId() &&
       selected->return_address==caller && selected->defer && address &&
       selected->before_native && selected->stopped && !selected->stopped->load(std::memory_order_acquire)){
        dispatching=true;
        const bool deferred=selected->defer(selected->context,address,argument);
        dispatching=false;SetLastError(error);
        // Verified caller discards RAX. This branch is forbidden at other sites.
        if(deferred)return true;
    }
    if(selected && selected->before_native)selected->before_native(selected->context,address);
    return false;
    };
    deferred=selected && selected->gate?selected->gate->release_decision(decision):decision();
    }
    if(deferred){SetLastError(error);return 0;}
    SetLastError(error);return native(address,argument);
}
}
