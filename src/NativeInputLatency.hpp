#pragma once
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <ostream>
namespace sc6::research {
// Owner-thread observations; only atomic summaries are read by the report worker.
// Publication is bracketed by native service, NOT a physical controller timestamp.
class NativeInputLatency {
    struct Sample {uint32_t frame{},raw{};uint64_t before{},after{};bool valid{},consumed{};};
    std::array<Sample,512> samples_{};
    struct Metric {
        std::atomic<uint64_t> count{},sum{},maximum{};
        void add(uint64_t ticks){sum.fetch_add(ticks,std::memory_order_relaxed);auto m=maximum.load();while(m<ticks&&!maximum.compare_exchange_weak(m,ticks)){};++count;}
        void write(std::ostream& out,const char* name,uint64_t hz)const{
            const auto n=count.load();out<<"input_latency_"<<name<<"_count="<<n<<'\n';
            out<<"input_latency_"<<name<<"_mean_us="<<(n&&hz?double(sum.load())*1e6/double(hz)/double(n):0)<<'\n';
            out<<"input_latency_"<<name<<"_max_us="<<(hz?double(maximum.load())*1e6/double(hz):0)<<'\n';
        }
    } lower_,upper_,bracket_,service_,wait_;
    uint64_t wait_begin_{};
    std::atomic<bool> wait_open_{};
    std::atomic<uint64_t> matched_{},missing_{},duplicate_{},overwritten_{},invalid_{},native_wait_calls_{},bypassed_{},withheld_{},wait_reason2_{},wait_other_{};
    uint64_t hz_{};
public:
    NativeInputLatency(){LARGE_INTEGER f{};if(QueryPerformanceFrequency(&f)&&f.QuadPart>0)hz_=static_cast<uint64_t>(f.QuadPart);}
    static uint64_t now()noexcept{LARGE_INTEGER q{};return QueryPerformanceCounter(&q)&&q.QuadPart>0?static_cast<uint64_t>(q.QuadPart):0;}
    void publication(uint32_t frame,uint32_t raw,uint64_t before,uint64_t after){
        if(!before||after<before){++invalid_;return;}auto& s=samples_[frame&511u];
        if(s.valid&&s.frame==frame)return; // Immutable first publication.
        if(s.valid&&!s.consumed)++overwritten_;
        s={frame,raw,before,after,true,false};bracket_.add(after-before);
    }
    void consumed(uint32_t frame,uint32_t raw,uint64_t tick){
        auto& s=samples_[frame&511u];
        if(!s.valid||s.frame!=frame){++missing_;return;}
        if(s.consumed){++duplicate_;return;}
        s.consumed=true;
        if(s.raw!=raw||tick<s.after){++invalid_;return;}
        lower_.add(tick-s.after);upper_.add(tick-s.before);++matched_;
    }
    void service(uint64_t begin,uint64_t end){if(begin&&end>=begin)service_.add(end-begin);else ++invalid_;}
    void decision(bool native_wait,bool advanced,unsigned reason,uint64_t tick){
        if(!tick){++invalid_;return;}
        if(native_wait){++native_wait_calls_;if(reason==2)++wait_reason2_;else ++wait_other_;if(advanced)++bypassed_;}
        if(native_wait&&!advanced){++withheld_;if(!wait_open_.load()){wait_begin_=tick;wait_open_=true;}}
        else if(wait_open_.load()){
            if(tick>=wait_begin_)wait_.add(tick-wait_begin_);else ++invalid_;wait_open_=false;
        }
    }
    void write(std::ostream& out)const{
        out<<"input_latency_schema=1\ninput_latency_clock=qpc\ninput_latency_physical_sampling=0\ninput_latency_endpoint=native_input_formation_complete\ninput_latency_publication_bracketed=1\ninput_latency_qpc_frequency="<<hz_<<'\n';
        out<<"input_latency_matched="<<matched_.load()<<"\ninput_latency_missing="<<missing_.load()<<"\ninput_latency_duplicate="<<duplicate_.load()<<"\ninput_latency_overwritten="<<overwritten_.load()<<"\ninput_latency_invalid="<<invalid_.load()<<'\n';
        lower_.write(out,"publication_to_consumption_lower",hz_);upper_.write(out,"publication_to_consumption_upper",hz_);bracket_.write(out,"publication_bracket",hz_);service_.write(out,"native_service",hz_);wait_.write(out,"native_wait_episode",hz_);
        out<<"input_latency_native_wait_calls="<<native_wait_calls_.load()<<"\ninput_latency_native_wait_bypassed_calls="<<bypassed_.load()<<"\ninput_latency_native_wait_withheld_calls="<<withheld_.load()<<"\ninput_latency_native_wait_reason2_calls="<<wait_reason2_.load()<<"\ninput_latency_native_wait_other_calls="<<wait_other_.load()<<"\ninput_latency_native_wait_open_censored="<<wait_open_.load()<<'\n';
    }
};
}
