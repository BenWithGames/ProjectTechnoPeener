#pragma once
#include "RngCallerTrace.hpp"
#include <cstring>
namespace sc6::research::shared_rng {
// Separate from scalar helper 34F550. Fixed storage; no allocation or I/O in hooks.
struct Event {uint64_t sequence{},caller{},result{};unsigned thread{},phase{},frame{},stream{};std::array<unsigned,32> state{};};
struct Snapshot {unsigned schema{1},installed{},count{},dropped{};uint64_t total{};std::array<Event,8192> events{};};
static_assert(sizeof(Event)==168);
inline SRWLOCK lock=SRWLOCK_INIT;
inline Snapshot trace;
inline std::atomic<unsigned> dropped{};
inline uintptr_t module{};
inline bool sample(Event& e) noexcept {
 __try {
  e.frame=*reinterpret_cast<const unsigned*>(module+0x470D0C4);
  std::memcpy(e.state.data(),reinterpret_cast<const void*>(module+0x485EB30),104);
  std::memcpy(e.state.data()+26,reinterpret_cast<const void*>(module+0x470E2B0),24);
  return true;
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline void record(uintptr_t caller,unsigned stream,uint64_t result) noexcept {
 const auto error=GetLastError();Event e{};
 e.caller=caller>=module?caller-module:caller;e.stream=stream;e.result=result;
 e.thread=GetCurrentThreadId();e.phase=rng_trace::phase;
 if(!module || !TryAcquireSRWLockExclusive(&lock)){++dropped;SetLastError(error);return;}
 if(sample(e)) {e.sequence=trace.total++;trace.events[e.sequence%trace.events.size()]=e;if(trace.count<trace.events.size())++trace.count;}
 else ++dropped;
 ReleaseSRWLockExclusive(&lock);SetLastError(error);
}
inline void snapshot(Snapshot& out) noexcept {AcquireSRWLockShared(&lock);out=trace;out.dropped=dropped.load();ReleaseSRWLockShared(&lock);}
}
