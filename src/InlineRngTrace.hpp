#pragma once
#include "RngCallerTrace.hpp"
#include <cstring>
namespace sc6::research::inline_rng {
// All registers are raw observations, never assumed to be valid object pointers.
// Sites 0..7 are immediately BEFORE a native RNG store; after is its source register.
// Sites 8/9 are the reference replay boundary before/after the native update.
struct Event {uint64_t sequence{};std::array<uint64_t,16> registers{};unsigned site{},thread{},phase{},frame{},before{},after{},expected{},valid{};};
static_assert(sizeof(Event)==168);
struct Snapshot {unsigned schema{1},installed{},count{},dropped{};uint64_t total{};std::array<Event,8192> events{};};
inline uintptr_t module{};
inline SRWLOCK lock=SRWLOCK_INIT;
inline Snapshot trace;
inline std::atomic<unsigned> drops{};
inline void record(Event e) noexcept {
 if(!TryAcquireSRWLockExclusive(&lock)){++drops;return;}
 e.sequence=trace.total++;trace.events[e.sequence%trace.events.size()]=e;
 if(trace.count<trace.events.size())++trace.count;
 ReleaseSRWLockExclusive(&lock);
}
inline void observe(const uint64_t* registers,unsigned site) noexcept {
 const auto error=GetLastError();Event e{};e.site=site;e.thread=GetCurrentThreadId();e.phase=rng_trace::phase;
 __try {if(site<8 && module){std::memcpy(e.registers.data(),registers,128);e.before=*reinterpret_cast<const unsigned*>(module+0x485EB28);e.frame=*reinterpret_cast<const unsigned*>(module+0x470D0C4);e.after=static_cast<unsigned>(registers[site==6?3:site==7?8:0]);e.valid=1;}}
 __except(EXCEPTION_EXECUTE_HANDLER){}
 record(e);SetLastError(error);
}
inline bool boundary(uintptr_t base,unsigned expected,bool after) noexcept {
 const auto error=GetLastError();Event e{};e.site=after?9u:8u;e.thread=GetCurrentThreadId();e.phase=rng_trace::phase;e.expected=expected;
 __try {e.before=e.after=*reinterpret_cast<const unsigned*>(base+0x485EB28);e.frame=*reinterpret_cast<const unsigned*>(base+0x470D0C4);e.valid=1;}
 __except(EXCEPTION_EXECUTE_HANDLER){}
 const bool equal=e.valid && e.before==expected;record(e);SetLastError(error);return equal;
}
inline void snapshot(Snapshot& out) noexcept {AcquireSRWLockShared(&lock);out=trace;out.dropped=drops.load();ReleaseSRWLockShared(&lock);}
}
