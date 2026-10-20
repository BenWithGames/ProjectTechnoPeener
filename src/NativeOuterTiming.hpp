#pragma once
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <ostream>
namespace sc6::research {
// Immutable bounded publication. No allocation or I/O in game callbacks.
struct NativeOuterTiming {
    static constexpr unsigned capacity=16384;
    struct ClockCounts {uint64_t calls{},advanced{},unavailable{},ineligible{},unadmitted{};};
    inline static thread_local ClockCounts clock_counts;
    struct State {ClockCounts dispatch{}; uint64_t clock{}; uint32_t identity{},frame{},consumed_identity{},consumed_frame{}; unsigned phase{},blocked{},alternate{},valid{}; };
    static State observe(uintptr_t outer)noexcept{
        State s{};if(!outer)return s;
        __try {
            s.phase=*reinterpret_cast<const unsigned char*>(outer+0x1461);
            s.blocked=*reinterpret_cast<const unsigned char*>(outer+0x12F3);
            s.alternate=*reinterpret_cast<const unsigned char*>(outer+0x1463);
            s.consumed_identity=*reinterpret_cast<const uint32_t*>(outer+0x1488);
            s.consumed_frame=*reinterpret_cast<const uint32_t*>(outer+0x148C);
            s.clock=*reinterpret_cast<const uintptr_t*>(outer+0x478);
            if(s.clock){s.identity=*reinterpret_cast<const uint32_t*>(s.clock+0x3A0);s.frame=*reinterpret_cast<const uint32_t*>(s.clock+0x3A4);}
            s.valid=1;s.dispatch=clock_counts;
        }__except(EXCEPTION_EXECUTE_HANDLER){s={};}
        return s;
    }
    struct Row {State before{},after{};unsigned marked{};uint64_t outer{},thread{},begin{},end{},previous_begin{},previous_end{},first_frame{},last_frame{},live{},capture_ticks{},restore_ticks{},replay_ticks{},corrections{},previous_corrections{};};
    struct Slot {Row row;std::atomic<bool> ready{};};
    std::array<Slot,capacity> rows{};
    std::atomic<unsigned> reserved{},dropped{},invalid{};
    struct Local {Row row{},previous{};unsigned depth{};bool marked{},bad{},tracking{};};
    inline static thread_local Local local;
    uint64_t frequency{};
    NativeOuterTiming(){LARGE_INTEGER q{};if(QueryPerformanceFrequency(&q))frequency=q.QuadPart;}
    static uint64_t now(){LARGE_INTEGER q{};return QueryPerformanceCounter(&q)?q.QuadPart:0;}
    void enter(uintptr_t outer,uint64_t stamp,State state={}){
        auto& l=local;if(l.depth++){l.bad=true;return;}
        l.row={};l.row.outer=outer;l.row.thread=GetCurrentThreadId();l.row.begin=stamp;l.row.before=state;l.marked=false;l.bad=false;
    }
    // kind values mirror NativeRollbackTiming; inclusive spans must not be summed.
    void phase(unsigned kind,uint64_t frame,uint64_t ticks){
        auto& l=local;if(!l.depth || l.bad)return;
        if(!l.marked)l.row.first_frame=frame;l.marked=true;l.row.last_frame=frame;
        if(kind==0)l.row.capture_ticks+=ticks;
        if(kind==1){l.row.restore_ticks+=ticks;++l.row.corrections;}
        if(kind==5)l.row.replay_ticks+=ticks;
        if(kind==6)++l.row.live;
    }
    void returned(uintptr_t outer,uint64_t stamp,State state={}){
        auto& l=local;if(!l.depth){++invalid;return;}if(--l.depth)return;
        if(l.bad || outer!=l.row.outer || !l.row.begin || stamp<l.row.begin){++invalid;l.previous={};return;}
        if(l.marked)l.tracking=true;
        if(!l.tracking){l.previous={};return;}
        l.row.marked=l.marked?1u:0u;l.row.after=state;
        l.row.end=stamp;
        if(l.previous.outer==outer && l.previous.end<=l.row.begin){l.row.previous_begin=l.previous.begin;l.row.previous_end=l.previous.end;l.row.previous_corrections=l.previous.corrections;}
        const auto n=reserved.fetch_add(1);if(n<capacity){rows[n].row=l.row;rows[n].ready.store(true,std::memory_order_release);}else ++dropped;
        l.previous=l.row;
    }
    struct Worker {
        unsigned cursor{};bool header_written{};
        struct Totals {uint64_t calls{},ticks{},maximum{},slow{},live{};};
        std::array<Totals,2> duration{},interval{},outside{};
        static void add(Totals& t,uint64_t ticks,uint64_t frequency,uint64_t live=0){++t.calls;t.ticks+=ticks;t.live+=live;if(ticks>t.maximum)t.maximum=ticks;if(ticks>frequency/60)++t.slow;}
        void drain(const NativeOuterTiming& source,std::ostream& csv){
            if(!header_written){header_written=true;csv<<"index,outer,thread,begin_qpc,end_qpc,previous_begin_qpc,previous_end_qpc,first_frame,last_frame,live_updates,capture_ticks,restore_ticks,replay_ticks,corrections,previous_corrections,marked,before_valid,before_clock,before_identity,before_frame,before_consumed_identity,before_consumed_frame,before_phase,before_blocked,before_alternate,before_clock_calls,before_advances,before_unavailable,before_ineligible,before_unadmitted,after_valid,after_clock,after_identity,after_frame,after_consumed_identity,after_consumed_frame,after_phase,after_blocked,after_alternate,after_clock_calls,after_advances,after_unavailable,after_ineligible,after_unadmitted\n";}
            auto n=source.reserved.load();if(n>capacity)n=capacity;
            while(cursor<n){const auto& slot=source.rows[cursor];if(!slot.ready.load(std::memory_order_acquire))break;const auto& r=slot.row;
                csv<<cursor<<','<<r.outer<<','<<r.thread<<','<<r.begin<<','<<r.end<<','<<r.previous_begin<<','<<r.previous_end<<','<<r.first_frame<<','<<r.last_frame<<','<<r.live<<','<<r.capture_ticks<<','<<r.restore_ticks<<','<<r.replay_ticks<<','<<r.corrections<<','<<r.previous_corrections<<','<<r.marked;
                for(const auto* s:{&r.before,&r.after})csv<<','<<s->valid<<','<<s->clock<<','<<s->identity<<','<<s->frame<<','<<s->consumed_identity<<','<<s->consumed_frame<<','<<s->phase<<','<<s->blocked<<','<<s->alternate<<','<<s->dispatch.calls<<','<<s->dispatch.advanced<<','<<s->dispatch.unavailable<<','<<s->dispatch.ineligible<<','<<s->dispatch.unadmitted;
                csv<<'\n';
                add(duration[r.corrections!=0],r.end-r.begin,source.frequency,r.live);
                if(r.previous_begin){add(interval[r.previous_corrections!=0],r.begin-r.previous_begin,source.frequency);add(outside[r.previous_corrections!=0],r.begin-r.previous_end,source.frequency);}
                ++cursor;
            }
        }
        void write(const NativeOuterTiming& source,std::ostream& out)const{
            out<<"outer_timing_schema=2\nouter_timing_process_cumulative=1\nouter_timing_frequency="<<source.frequency<<"\nouter_timing_rows="<<cursor<<"\nouter_timing_dropped="<<source.dropped.load()<<"\nouter_timing_invalid="<<source.invalid.load()<<'\n';
            const auto emit=[&](const char* name,const auto& groups){for(unsigned i=0;i<2;++i){const auto& t=groups[i];const double scale=source.frequency?1000000.0/source.frequency:0;
                out<<"outer_"<<name<<'_'<<(i?"corrected":"uncorrected")<<"_calls="<<t.calls<<'\n'
                   <<"outer_"<<name<<'_'<<(i?"corrected":"uncorrected")<<"_mean_us="<<(t.calls?t.ticks*scale/t.calls:0)<<'\n'
                   <<"outer_"<<name<<'_'<<(i?"corrected":"uncorrected")<<"_max_us="<<t.maximum*scale<<'\n'
                   <<"outer_"<<name<<'_'<<(i?"corrected":"uncorrected")<<"_over_budget="<<t.slow<<'\n';}};
            emit("duration",duration);emit("following_interval",interval);emit("following_outside_gap",outside);
        }
    };
};
inline NativeOuterTiming native_outer_timing;
}
