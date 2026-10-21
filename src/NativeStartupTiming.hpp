#pragma once
#include "NativeRollbackTiming.hpp"
namespace sc6::research {
// Process-cumulative, bounded diagnostics. No allocation or logging in scopes.
// Durations overlap; hold_wall measures elapsed waiting, not CPU execution.
struct NativeStartupTiming {
    enum Kind {resources_first,resources_rearm,session_first,session_rearm,
        prepare_first,prepare_rearm,activation,hold_wall,rearm_poll,workspace_construct,descriptor_acquire,buffer_acquire,effect_session,hook_install,session_bind,first_capture,initial_ring_capture,settlement_wait,route_retire,rearm_provider,count};
    inline static constexpr const char* names[]={"resources_first","resources_rearm","session_first","session_rearm",
        "prepare_first","prepare_rearm","activation","hold_wall","rearm_poll","workspace_construct","descriptor_acquire","buffer_acquire","effect_session","hook_install","session_bind","first_capture","initial_ring_capture","settlement_wait","route_retire","rearm_provider"};
    struct Bucket {std::atomic<uint64_t> calls{},ticks{},maximum{},max_round{},max_thread{},last_begin{},last_end{};};
    std::array<Bucket,count> buckets;
    // Append-only bounded trace: a writer publishes once; readers never see an
    // entry until its release flag. No locks, allocation, or file I/O on hooks.
    struct Event {std::atomic<bool> ready{};unsigned kind{},thread{};uint64_t round{},begin{},end{};};
    std::array<Event,256> events{};
    std::atomic<uint64_t> event_count{},event_omitted{};
    uint64_t frequency{};
    std::atomic<uint64_t> invalid{};
    NativeStartupTiming(){LARGE_INTEGER q{};if(QueryPerformanceFrequency(&q) && q.QuadPart>0)frequency=q.QuadPart;}
    void add(Kind kind,uint64_t round,uint64_t begin,uint64_t end,unsigned thread)noexcept{
        if(kind>=count || !frequency || !begin || end<begin){++invalid;return;}
        if(kind==prepare_first || kind==prepare_rearm || kind==session_first || kind==session_rearm ||
           kind==activation || kind==hold_wall || kind==session_bind || kind==first_capture ||
           kind==initial_ring_capture || kind==settlement_wait || kind==route_retire){
            const auto index=event_count.fetch_add(1,std::memory_order_relaxed);
            if(index<events.size()) {auto& e=events[index];e.kind=kind;e.thread=thread;e.round=round;e.begin=begin;e.end=end;e.ready.store(true,std::memory_order_release);}
            else event_omitted.fetch_add(1,std::memory_order_relaxed);
        }
        auto& b=buckets[kind];const auto duration=end-begin;
        b.ticks.fetch_add(duration,std::memory_order_relaxed);
        auto max=b.maximum.load(std::memory_order_relaxed);
        while(duration>max){if(b.maximum.compare_exchange_weak(max,duration,std::memory_order_relaxed)){
            b.max_round=round;b.max_thread=thread;break;
        }}
        b.last_begin=begin;b.last_end=end;b.calls.fetch_add(1,std::memory_order_release);
    }
    void write(std::ostream& out)const{
        out<<"startup_timing_schema=2\nstartup_timing_process_cumulative=1\nstartup_timing_inclusive=1\nstartup_timing_frequency="
           <<frequency<<"\nstartup_timing_invalid="<<invalid.load()<<'\n';
        out<<"startup_trace_capacity="<<events.size()<<"\nstartup_trace_omitted="<<event_omitted.load()<<'\n';
        for(unsigned i=0;i<events.size();++i){const auto& e=events[i];if(!e.ready.load(std::memory_order_acquire))continue;
            out<<"startup_trace_"<<i<<"="<<names[e.kind]<<','<<e.round<<','<<e.thread<<','<<e.begin<<','<<e.end<<'\n';
        }
        for(unsigned i=0;i<count;++i){const auto& b=buckets[i];const auto n=b.calls.load(std::memory_order_acquire);
            const auto us=[&](uint64_t ticks){return frequency?double(ticks)*1000000.0/double(frequency):0.0;};
            out<<"startup_timing_"<<names[i]<<"_calls="<<n<<'\n'
               <<"startup_timing_"<<names[i]<<"_mean_us="<<(n?us(b.ticks.load())/double(n):0)<<'\n'
               <<"startup_timing_"<<names[i]<<"_max_us="<<us(b.maximum.load())<<'\n'
               <<"startup_timing_"<<names[i]<<"_max_round="<<b.max_round.load()<<'\n'
               <<"startup_timing_"<<names[i]<<"_max_thread="<<b.max_thread.load()<<'\n'
               <<"startup_timing_"<<names[i]<<"_last_begin_qpc="<<b.last_begin.load()<<'\n'
               <<"startup_timing_"<<names[i]<<"_last_end_qpc="<<b.last_end.load()<<'\n';
        }
    }
};
inline NativeStartupTiming native_startup_timing;
struct NativeStartupScope {
    NativeStartupTiming::Kind kind;uint64_t round,begin;unsigned thread;
    NativeStartupScope(NativeStartupTiming::Kind k,uint64_t r=0):kind(k),round(r),begin(NativeRollbackTiming::now()),thread(GetCurrentThreadId()){}
    ~NativeStartupScope(){native_startup_timing.add(kind,round,begin,NativeRollbackTiming::now(),thread);}
    NativeStartupScope(const NativeStartupScope&)=delete;
    NativeStartupScope& operator=(const NativeStartupScope&)=delete;
};
}
