#pragma once
#include <Windows.h>
#include <atomic>
#include <cstdint>
namespace sc6::research::lease_service {
using Handler=bool(*)(uint32_t,bool);
inline std::atomic<DWORD> owner{};
inline std::atomic<uint64_t> events{},wrong_thread{};
inline std::atomic<bool> cancellation{};
inline std::atomic<Handler> handler{};
inline bool dispatch(uint32_t frame){
    DWORD expected=0;const auto tid=GetCurrentThreadId();
    owner.compare_exchange_strong(expected,tid);
    if(owner.load()!=tid){++wrong_thread;return false;}
    ++events;
    const auto callback=handler.load();
    if(!callback)return true; // Pending cancellation remains until a handler drains.
    const bool cancel=cancellation.exchange(false);
    if(!callback(frame,cancel)){if(cancel)cancellation=true;return false;}
    return true;
}
inline void request_cancel(){cancellation=true;}
}
