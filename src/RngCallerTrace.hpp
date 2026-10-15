#pragma once
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
namespace sc6::research::rng_trace {
// Helper-only coverage: inline RNG writers are not intercepted.
struct Event {uint64_t sequence{},caller{};unsigned thread{},phase{},frame{},before{},after{},reserved{};};
struct Snapshot {unsigned schema{1},installed{},count{},dropped{};uint64_t total{};std::array<Event,8192> events{};};
inline SRWLOCK trace_lock=SRWLOCK_INIT;
inline Snapshot trace;
inline std::atomic<unsigned> dropped{};
inline thread_local unsigned phase{}; // 0 natural/unclassified,1 load,2 reference,3 prediction,4 correction
struct Scope {unsigned previous;explicit Scope(unsigned p):previous(phase){phase=p;}~Scope(){phase=previous;}};
inline void record(uintptr_t caller,unsigned frame,unsigned before,unsigned after) noexcept {
    if(!TryAcquireSRWLockExclusive(&trace_lock)){++dropped;return;}
    const auto seq=trace.total++;
    trace.events[seq%trace.events.size()]={seq,caller,GetCurrentThreadId(),phase,frame,before,after,0};
    if(trace.count<trace.events.size())++trace.count;
    ReleaseSRWLockExclusive(&trace_lock);
}
template<class Invoke> float observe(Invoke invoke,const unsigned* seed,const unsigned* counter,uintptr_t caller){
    const auto before=*seed,frame=*counter;
    const float result=invoke();const auto error=GetLastError();
    record(caller,frame,before,*seed);SetLastError(error);return result;
}
inline void snapshot(Snapshot& out) noexcept {
    AcquireSRWLockShared(&trace_lock);out=trace;out.dropped=dropped.load();ReleaseSRWLockShared(&trace_lock);
}
}
