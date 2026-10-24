#pragma once
#include <Windows.h>
#include "LoadingSynchronization.hpp"
#include <MinHook.h>
#include <intrin.h>
#include <atomic>
#include <array>
#include <cstdint>
#include <cstring>
namespace sc6::research::loading_readiness_observer {
using Original=bool(*)(uintptr_t);
inline Original original{},alternate_ready{},alternate_start{};inline uintptr_t base{};
inline std::atomic<uint64_t> last_loading_tick{};
inline std::atomic<uint64_t> alternate_withheld{};
inline std::atomic<bool> installed{};
inline std::atomic<uint64_t> calls{},ready_calls{},invalid{},dropped{},changes{},last_key{};
struct Event {std::atomic<bool> published{};uint64_t tick{};uintptr_t object{};unsigned thread{},state{},caller{},frame{};bool ready{};};
inline std::array<Event,128> events;
inline bool inspect(uintptr_t object,unsigned& state,unsigned& frame)noexcept{
    __try {state=*reinterpret_cast<const unsigned char*>(object+0x468);frame=*reinterpret_cast<const unsigned*>(base+0x470D0C4);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline bool gate_result(uintptr_t object,bool result,uintptr_t caller,unsigned state){
    if(caller==base+0x3FBE50 && state==1){
        if(result){
            if(auto* sync=loading_sync::current.load(std::memory_order_acquire))
                if(sync->hold(object,GetCurrentThreadId(),GetTickCount64()))return false;
            // Only a native TRUE that escaped the gate means loading was missed.
            last_loading_tick.store(GetTickCount64(),std::memory_order_release);
        }
    }
    return result;
}
inline bool bridge(uintptr_t object){
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    const bool result=original(object); // Always execute the native predicate once.
    calls.fetch_add(1,std::memory_order_relaxed);if(result)ready_calls.fetch_add(1,std::memory_order_relaxed);
    unsigned state{},frame{};if(!inspect(object,state,frame)){++invalid;return result;}
    const uint64_t key=(uint64_t(object)<<9) ^ (uint64_t(state)<<1) ^ uint64_t(result);
    if(last_key.exchange(key,std::memory_order_relaxed)!=key){
        const auto index=changes.fetch_add(1,std::memory_order_relaxed);
        if(index>=events.size()){++dropped;}else{
        auto& e=events[size_t(index)];e.tick=GetTickCount64();e.object=object;e.thread=GetCurrentThreadId();
        e.state=state;e.frame=frame;e.ready=result;e.caller=caller>=base && caller-base<0x5000000?unsigned(caller-base):0;
        e.published.store(true,std::memory_order_release);
        }
    }
    return gate_result(object,result,caller,state);
}

inline bool alternate_result(uintptr_t object,bool result,uintptr_t caller,uintptr_t expected){
    if(!result || caller!=base+expected)return result;
    auto* sync=loading_sync::current.load(std::memory_order_acquire);
    if(!sync || sync->object.load()!=object || !sync->entered.load())return result;
    unsigned state{},frame{};
    if(!inspect(object,state,frame)){sync->cancel(loading_sync::Session::identity);return result;}
    if(state==1 && sync->hold(object,GetCurrentThreadId(),GetTickCount64())){++alternate_withheld;return false;}
    return result;
}
inline bool ready_bridge(uintptr_t object){const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    return alternate_result(object,alternate_ready(object),caller,0x1E67216);}
inline bool start_bridge(uintptr_t object){const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    return alternate_result(object,alternate_start(object),caller,0x1E6723E);}
inline bool install(uintptr_t module){
    if(installed.load())return base==module;
    // Called only after full executable hash verification by recorder_init.
    constexpr unsigned char expected[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8d,0x99,0x88,0x04,0x00,0x00};
    constexpr unsigned char alternate_expected[]={0x48,0x89,0x4c,0x24,0x08,0x48,0x83,0xec,0x38,0x48,0x8d,0x4c,0x24,0x20};
    auto target=reinterpret_cast<void*>(module+0x3F1870);
    auto ready=reinterpret_cast<void*>(module+0x23C5110),start=reinterpret_cast<void*>(module+0x23C50A0);
    if(std::memcmp(target,expected,sizeof(expected)) || std::memcmp(ready,alternate_expected,sizeof(alternate_expected)) ||
       std::memcmp(start,alternate_expected,sizeof(alternate_expected)))return false;
    const auto init=MH_Initialize();if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED)return false;
    if(MH_CreateHook(target,reinterpret_cast<void*>(&bridge),reinterpret_cast<void**>(&original))!=MH_OK)return false;
    if(MH_CreateHook(ready,reinterpret_cast<void*>(&ready_bridge),reinterpret_cast<void**>(&alternate_ready))!=MH_OK){MH_RemoveHook(target);return false;}
    if(MH_CreateHook(start,reinterpret_cast<void*>(&start_bridge),reinterpret_cast<void**>(&alternate_start))!=MH_OK){MH_RemoveHook(ready);MH_RemoveHook(target);return false;}
    base=module;
    // No synchronization session is published until all hooks are available.
    if(MH_EnableHook(target)!=MH_OK || MH_EnableHook(ready)!=MH_OK || MH_EnableHook(start)!=MH_OK){
        MH_DisableHook(target);MH_DisableHook(ready);MH_DisableHook(start);
        MH_RemoveHook(target);MH_RemoveHook(ready);MH_RemoveHook(start);return false;
    }
    installed.store(true,std::memory_order_release);return true;
}
template<class Stream>void report(Stream& out){
    out<<"loading_readiness_schema=2\nloading_gate_enabled=1\nloading_readiness_process_cumulative=1\nloading_readiness_installed="<<installed.load()
       <<"\nloading_readiness_calls="<<calls.load()<<"\nloading_readiness_ready="<<ready_calls.load()
       <<"\nloading_gate_alternate_withheld="<<alternate_withheld.load()
       <<"\nloading_readiness_invalid="<<invalid.load()<<"\nloading_readiness_dropped="<<dropped.load()<<'\n';
    if(auto* sync=loading_sync::current.load())sync->report(out);
    for(size_t i=0;i<events.size();++i){const auto& e=events[i];if(!e.published.load(std::memory_order_acquire))continue;
        out<<"loading_readiness_event_"<<i<<"="<<e.tick<<','<<e.thread<<','<<e.object<<','<<e.caller<<','<<e.state<<','<<e.frame<<','<<e.ready<<'\n';}
}
}
