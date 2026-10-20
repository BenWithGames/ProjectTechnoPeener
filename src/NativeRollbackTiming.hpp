#pragma once
#include "NativeOuterTiming.hpp"
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <ostream>
namespace sc6::research {
// One simulation-thread writer per session; report worker reads atomics only.
// Inclusive spans intentionally overlap. No allocation, formatting or I/O here.
struct NativeRollbackTiming {
    enum Kind : unsigned {capture,restore,prepare,load_fixups,verification,replay,live_update,cleanup,workspace,count};
    inline static constexpr const char* names[]={"capture","restore","prepare","load_fixups","verification","replay","live_update","cleanup","workspace"};
    struct Bucket {
        std::atomic<uint64_t> calls{},ticks{},maximum{},max_frame{},over_frame{},over_50ms{};
    };
    std::array<Bucket,count> buckets;
    uint64_t frequency{};
    std::atomic<uint64_t> invalid_clock{};
    NativeRollbackTiming(){LARGE_INTEGER q{};if(QueryPerformanceFrequency(&q) && q.QuadPart>0)frequency=static_cast<uint64_t>(q.QuadPart);}
    static uint64_t now()noexcept{LARGE_INTEGER q{};return QueryPerformanceCounter(&q) && q.QuadPart>0?static_cast<uint64_t>(q.QuadPart):0;}
    void add(Kind kind,uint64_t frame,uint64_t begin,uint64_t end)noexcept{
        if(kind>=count || !frequency || !begin || end<begin){invalid_clock.fetch_add(1,std::memory_order_relaxed);return;}
        const auto elapsed=end-begin;native_outer_timing.phase(kind,frame,elapsed);auto& b=buckets[kind];
        b.ticks.fetch_add(elapsed,std::memory_order_relaxed);
        if(elapsed>b.maximum.load(std::memory_order_relaxed)){
            b.max_frame.store(frame,std::memory_order_relaxed);b.maximum.store(elapsed,std::memory_order_relaxed);
        }
        if(elapsed>frequency/60)b.over_frame.fetch_add(1,std::memory_order_relaxed);
        if(elapsed>frequency/20)b.over_50ms.fetch_add(1,std::memory_order_relaxed);
        b.calls.fetch_add(1,std::memory_order_release);
    }
    inline static thread_local NativeRollbackTiming* active{};
    inline static thread_local uint64_t active_frame{};
    struct Binding {
        NativeRollbackTiming* prior;uint64_t frame;
        Binding(NativeRollbackTiming* timing,uint64_t value):prior(active),frame(active_frame){active=timing;active_frame=value;}
        ~Binding(){active=prior;active_frame=frame;}
        Binding(const Binding&)=delete;Binding& operator=(const Binding&)=delete;
    };
    struct Scope {
        NativeRollbackTiming* timing;Kind kind;uint64_t frame,begin;
        explicit Scope(Kind value):timing(active),kind(value),frame(active_frame),begin(timing?now():0){}
        ~Scope(){if(timing)timing->add(kind,frame,begin,now());}
        Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
    };
    void write(std::ostream& out)const{
        out<<"continuous_timing_schema=1\ncontinuous_timing_inclusive=1\ncontinuous_timing_frequency="<<frequency
           <<"\ncontinuous_timing_invalid_clock="<<invalid_clock.load()<<'\n';
        for(unsigned i=0;i<count;++i){const auto& b=buckets[i];const auto n=b.calls.load(std::memory_order_acquire);
            const auto total=b.ticks.load(),maximum=b.maximum.load();
            const auto us=[&](uint64_t ticks){return frequency?static_cast<double>(ticks)*1000000.0/static_cast<double>(frequency):0.0;};
            out<<"continuous_timing_"<<names[i]<<"_calls="<<n<<'\n'
               <<"continuous_timing_"<<names[i]<<"_total_us="<<us(total)<<'\n'
               <<"continuous_timing_"<<names[i]<<"_mean_us="<<(n?us(total)/static_cast<double>(n):0.0)<<'\n'
               <<"continuous_timing_"<<names[i]<<"_max_us="<<us(maximum)<<'\n'
               <<"continuous_timing_"<<names[i]<<"_max_frame="<<b.max_frame.load()<<'\n'
               <<"continuous_timing_"<<names[i]<<"_over_16_67ms="<<b.over_frame.load()<<'\n'
               <<"continuous_timing_"<<names[i]<<"_over_50ms="<<b.over_50ms.load()<<'\n';
        }
    }
};
}
