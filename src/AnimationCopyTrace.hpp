#pragma once
#include "RngCallerTrace.hpp"
#include <cstring>
namespace sc6::research::animation_copy {
// Sites: 0 function entry gate; 1 before matrix-30 conditional copy; 2 after copy.
// phase uses the existing RNG execution scopes. Values are raw float bits.
struct Event {
 uint64_t sequence{},cache{},ring{},entry_mask{};
 unsigned thread{},phase{},frame{},site{},valid{},condition{UINT32_MAX};
 std::array<unsigned,4> cache_translation{},ring_translation{};
 std::array<std::byte,960> layers{};
};
static_assert(sizeof(Event)==1048);
struct Snapshot {unsigned schema{2},installed{},count{},dropped{};uint64_t total{};std::array<Event,4096> events{};};
inline SRWLOCK lock=SRWLOCK_INIT;
inline Snapshot trace;
inline std::atomic<unsigned> dropped{};
inline uintptr_t module{};
inline void record(Event e) noexcept {
 if(!TryAcquireSRWLockExclusive(&lock)){++dropped;return;}
 e.sequence=trace.total++;trace.events[e.sequence%trace.events.size()]=e;
 if(trace.count<trace.events.size())++trace.count;
 ReleaseSRWLockExclusive(&lock);
}
inline void observe(uintptr_t cache,uintptr_t ring,uintptr_t stack,unsigned site) noexcept {
 const auto error=GetLastError();Event e;e.cache=cache;e.ring=ring;e.site=site;
 e.thread=GetCurrentThreadId();e.phase=rng_trace::phase;
 __try {
  if(module)e.frame=*reinterpret_cast<const unsigned*>(module+0x470D0C4);
  if(site>5 || cache<0x10000 || ring<0x10000 || ((site==1 || site==2 || site==4 || site==5) && stack<0x10000))__leave;
  e.entry_mask=*reinterpret_cast<const uint64_t*>(cache+0xB18);
  std::memcpy(e.cache_translation.data(),reinterpret_cast<const void*>(cache+0x7B0),16);
  std::memcpy(e.ring_translation.data(),reinterpret_cast<const void*>(ring+0x7B0),16);
  if(site==3)e.condition=static_cast<unsigned>(stack);
  else if(site)e.condition=*reinterpret_cast<const unsigned char*>(stack+(site>=4?0x3728:0x3758));
  if(site>=3)std::memcpy(e.layers.data(),reinterpret_cast<const void*>(cache+0x800),e.layers.size());
  e.valid=1;
 }__except(EXCEPTION_EXECUTE_HANDLER){}
 record(e);SetLastError(error);
}
inline void snapshot(Snapshot& out) noexcept {
 AcquireSRWLockShared(&lock);out=trace;out.dropped=dropped.load();ReleaseSRWLockShared(&lock);
}
}
