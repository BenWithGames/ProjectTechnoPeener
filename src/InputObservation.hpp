#pragma once
#include <Windows.h>
#include <array>
#include <cstdint>
#include <string>
namespace sc6::observation {
constexpr unsigned limit=120,words=14,parts=7;
constexpr unsigned capacity=7200;
constexpr unsigned magic=0x5343364f,version=4;
constexpr unsigned owned_mode=2; // exclusive scheduler writer, never observer-armed
// Wire words: outer counter, ten raw input words, QPC low/high, reserved zero.
// No pointers or mutable game state are exposed to the network process.
struct Record {std::array<std::uint32_t,words> data{};};
static_assert(sizeof(Record)==56);
struct Round {uint32_t number{},event_counter{};};
struct Shared {
    unsigned signature{},schema{},pid{},reserved{};
    alignas(8) std::uint64_t nonce{},frequency{};
    volatile LONG count{},status{}; // 0 idle, 1 recording, 2 complete, 3 failed, 4 preparing
    std::array<Record,capacity> records{};
    std::array<Round,capacity> rounds{};
};
// Caller serializes recorder ownership before claiming this idle mapping.
// Never erase a previous recording or reuse its nonce/buffer.
inline bool prepare_owned(Shared& shared,uint64_t nonce){
    if(!nonce || shared.signature!=magic || shared.schema!=version || shared.pid!=GetCurrentProcessId() ||
        !shared.frequency || shared.count!=0 || shared.nonce || shared.reserved)return false;
    if(InterlockedCompareExchange(&shared.status,4,0)!=0)return false;
    shared.reserved=owned_mode;shared.nonce=nonce;
    MemoryBarrier();InterlockedExchange(&shared.status,1);return true;
}
inline std::wstring mapping_name(unsigned pid){return L"Local\\SC6InputObservation-"+std::to_wstring(pid);}
inline std::wstring automatic_mapping_name(unsigned pid,uint64_t nonce){return mapping_name(pid)+L"-"+std::to_wstring(nonce);}
template<size_t N> inline unsigned checksum(const std::array<Record,N>& records){
    unsigned h=2166136261u;
    for(const auto& r:records)for(auto word:r.data)for(unsigned shift=0;shift<32;shift+=8)h=(h^((word>>shift)&255u))*16777619u;
    return h;
}
}
