#pragma once
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
namespace sc6::research {
struct ReplayTiming {
    enum Phase {step,capture,archive,restore,replay,evidence,traces,retire,release,discard,round_cleanup,report_io,gap,phases};
    static constexpr const char* names[]={"step","capture","archive","restore","replay","evidence","traces","retire","release","discard","round_cleanup","report_io","callback_gap"};
    struct Sample {std::atomic<uint64_t> count{},ticks{},maximum{};
        void add(uint64_t elapsed){++count;ticks.fetch_add(elapsed);auto old=maximum.load();while(old<elapsed && !maximum.compare_exchange_weak(old,elapsed)){} }
    };
    mutable std::array<std::array<Sample,phases>,4> rows{};
    uint64_t previous{};
    static uint64_t now(){LARGE_INTEGER v{};return QueryPerformanceCounter(&v)?static_cast<uint64_t>(v.QuadPart):0;}
    static uint64_t frequency(){LARGE_INTEGER v{};return QueryPerformanceFrequency(&v)?static_cast<uint64_t>(v.QuadPart):0;}
    struct Scope {Sample& sample;uint64_t start=now();~Scope(){const auto end=now();if(start && end>=start)sample.add(end-start);} };
    template<class F> decltype(auto) measure(unsigned cycle,Phase phase,F&& f)const {Scope scope{rows[cycle<3?cycle:3][phase]};return f();}
    void arrival(unsigned cycle){const auto time=now();if(previous && time>=previous)rows[cycle<3?cycle:3][gap].add(time-previous);previous=time;}
    template<class Stream> void report(Stream& out)const {
        out<<"suite_timing_diagnostics_schema=1\nsuite_timing_qpc_frequency="<<frequency()<<'\n';
        for(unsigned c=0;c<rows.size();++c)for(unsigned p=0;p<phases;++p){const auto& s=rows[c][p];
            out<<"suite_timing_"<<c+1<<'_'<<names[p]<<"_count="<<s.count.load()<<'\n'
               <<"suite_timing_"<<c+1<<'_'<<names[p]<<"_total_ticks="<<s.ticks.load()<<'\n'
               <<"suite_timing_"<<c+1<<'_'<<names[p]<<"_max_ticks="<<s.maximum.load()<<'\n';
        }
    }
};
}
