#pragma once
#include "RngCallerTrace.hpp"
#include <cstring>
namespace sc6::research::camera_calc {
struct Child {uint64_t address{};std::array<std::byte,456> bytes{};};
struct Event {
 uint64_t sequence{},qpc{};unsigned thread{},phase{},frame{},site{},valid{},children_valid{};
 std::array<uint64_t,16> registers{};std::array<std::byte,512> fx{};
 std::array<std::byte,864> controller{};std::array<std::byte,464> temporary{};
 std::array<Child,16> children{};std::array<unsigned,33> rng{};
};
struct Snapshot {unsigned schema{1},installed{},count{},dropped{};uint64_t total{};std::array<Event,512> events{};};
static_assert(sizeof(Event)==9568);
static_assert(sizeof(Snapshot)==4898840);
inline SRWLOCK lock=SRWLOCK_INIT;inline Snapshot trace;inline std::atomic<unsigned> dropped{};inline uintptr_t module{};
inline bool copy(void* to,uintptr_t from,size_t bytes)noexcept{__try{std::memcpy(to,reinterpret_cast<void*>(from),bytes);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
inline void observe(const uint64_t* r,unsigned site,const void* fx)noexcept{
 auto error=GetLastError();
 if(!module || site>=6 || !r || !TryAcquireSRWLockExclusive(&lock)){++dropped;SetLastError(error);return;}
 // Fill fixed ring storage directly: no large hook stack frame, heap allocation, or I/O.
 auto& e=trace.events[trace.total%trace.events.size()];e={};e.sequence=trace.total++;e.site=site;e.thread=GetCurrentThreadId();e.phase=rng_trace::phase;
 LARGE_INTEGER q{};QueryPerformanceCounter(&q);e.qpc=q.QuadPart;
 if(copy(e.registers.data(),reinterpret_cast<uintptr_t>(r),128))e.valid|=1;
 if(fx && copy(e.fx.data(),reinterpret_cast<uintptr_t>(fx),512))e.valid|=2;
 if(copy(&e.frame,module+0x470D0C4,4))e.valid|=4;
 if(copy(e.controller.data(),module+0x470E9F0,864))e.valid|=8;
 if(site>=3 && (e.valid&1) && copy(e.temporary.data(),e.registers[4]+0x20,464))e.valid|=16;
 if(copy(e.rng.data(),module+0x485EB30,104) && copy(e.rng.data()+26,module+0x470E2B0,24) && copy(e.rng.data()+32,module+0x485EB28,4))e.valid|=32;
 if(e.valid&8)for(unsigned i=0;i<16;++i){auto& c=e.children[i];std::memcpy(&c.address,e.controller.data()+0x270+i*8,8);if(!c.address || copy(c.bytes.data(),c.address,c.bytes.size()))e.children_valid|=1u<<i;}
 if(trace.count<trace.events.size())++trace.count;
 ReleaseSRWLockExclusive(&lock);SetLastError(error);
}
struct Stats {unsigned installed,count,dropped;uint64_t total;};
inline Stats stats(){AcquireSRWLockShared(&lock);Stats s{trace.installed,trace.count,dropped.load(),trace.total};ReleaseSRWLockShared(&lock);return s;}
inline void snapshot(Snapshot& out){AcquireSRWLockShared(&lock);out=trace;out.dropped=dropped.load();ReleaseSRWLockShared(&lock);}
}
