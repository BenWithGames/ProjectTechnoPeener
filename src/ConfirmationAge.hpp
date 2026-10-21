#pragma once
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <ostream>
namespace sc6::research {
// One owner-thread writer. Atomic aggregates allow deferred worker reporting.
// Every timestamp is local QPC; never subtract timestamps from different PCs.
struct ConfirmationAge {
    enum Kind {receive_to_drain,drain_to_scheduler,local_publish_to_receive,local_publish_to_scheduler,count};
    inline static constexpr const char* names[]={"receive_to_drain","drain_to_scheduler","local_publish_to_receive","local_publish_to_scheduler"};
    struct Bucket {std::atomic<uint64_t> samples{},total{},maximum{};std::array<std::atomic<uint64_t>,8> bins{};};
    std::array<Bucket,count> buckets{};
    std::array<std::atomic<uint64_t>,5> behind_frames{};
    std::atomic<uint64_t> early{},unpublished{},unmeasured{},invalid{};
    uint64_t frequency{};
    ConfirmationAge(){LARGE_INTEGER q{};if(QueryPerformanceFrequency(&q)&&q.QuadPart>0)frequency=q.QuadPart;}
    void add(Kind kind,uint64_t begin,uint64_t end){
        if(!frequency || !begin || end<begin){++invalid;return;}
        const auto ticks=end-begin;auto& b=buckets[kind];b.total.fetch_add(ticks,std::memory_order_relaxed);
        if(ticks>b.maximum.load(std::memory_order_relaxed))b.maximum.store(ticks,std::memory_order_relaxed);
        const double us=static_cast<double>(ticks)*1e6/static_cast<double>(frequency);
        constexpr double edges[]={1000,4000,8000,16666.6666667,33333.3333333,66666.6666667,100000};
        unsigned bin=0;while(bin<7 && us>edges[bin])++bin;
        b.bins[bin].fetch_add(1,std::memory_order_relaxed);b.samples.fetch_add(1,std::memory_order_release);
    }
    void observe(uint64_t frame,uint64_t head,uint64_t published,uint64_t received,uint64_t drained,uint64_t submitted){
        // Native-authoritative truth has no transport timestamp; do not call it zero latency.
        if(!received || !drained){++unmeasured;return;}
        if(!frequency || drained<received || submitted<drained){++invalid;return;}
        add(receive_to_drain,received,drained);add(drain_to_scheduler,drained,submitted);
        const auto behind=head>frame?head-frame:0;behind_frames[behind<4?behind:4].fetch_add(1,std::memory_order_relaxed);
        if(!published){++unpublished;return;}
        if(received<published)++early;else add(local_publish_to_receive,published,received);
        add(local_publish_to_scheduler,published,submitted);
    }
    void write(std::ostream& out)const{
        out<<"confirmation_age_schema=1\nconfirmation_age_local_clock_only=1\nconfirmation_age_network_latency=0\nconfirmation_age_frequency="<<frequency
           <<"\nconfirmation_age_early="<<early.load()<<"\nconfirmation_age_unpublished="<<unpublished.load()
           <<"\nconfirmation_age_unmeasured="<<unmeasured.load()<<"\nconfirmation_age_invalid="<<invalid.load()<<'\n';
        for(unsigned i=0;i<5;++i)out<<"confirmation_age_behind_frames_"<<i<<'='<<behind_frames[i].load()<<'\n';
        for(unsigned i=0;i<count;++i){const auto& b=buckets[i];const auto n=b.samples.load(std::memory_order_acquire);
            const auto us=[&](uint64_t t){return frequency?static_cast<double>(t)*1e6/static_cast<double>(frequency):0.;};
            out<<"confirmation_age_"<<names[i]<<"_samples="<<n<<"\nconfirmation_age_"<<names[i]<<"_mean_us="<<(n?us(b.total.load())/n:0.)
               <<"\nconfirmation_age_"<<names[i]<<"_max_us="<<us(b.maximum.load())<<'\n';
            for(unsigned bin=0;bin<8;++bin)out<<"confirmation_age_"<<names[i]<<"_bin_"<<bin<<'='<<b.bins[bin].load()<<'\n';
        }
    }
};
}
