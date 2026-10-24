#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <optional>
#include "AdaptiveInputDelay.hpp"
namespace sc6 {
// RTT estimate: subtract only responder-measured handling duration. Unknown
// Steam/OS queueing and receive polling remain; never subtract clock offsets.
struct SteamPing {
    AdaptiveInputDelay adaptive;
    uint64_t worst_raw_us{};
    std::array<uint64_t,5> raw_samples{};
    static constexpr unsigned burst_size=5;
    struct Probe {uint32_t id{};uint64_t sent{};bool pending{};};
    std::array<Probe,burst_size> probes{};
    uint32_t sequence=0;
    uint64_t next_us=0,sample_due=0,deadline=0,received_us=0;
    unsigned attempted=0,accepted=0,latest_samples=0;
    bool active=false;
    std::optional<unsigned> latest;
    std::optional<uint64_t> best_us;
    uint64_t best_raw_us=0,best_handling_us=0,latest_raw_us=0,latest_handling_us=0;
    void update(uint64_t now){
        if(!active || (now<deadline && accepted<burst_size))return;
        auto sorted=raw_samples;
        std::sort(sorted.begin(),sorted.end());
        // Fourth of five (nearest-rank 80th percentile), raw RTT: no guessed
        // clock/processing subtraction and no optimistic overlay minimum.
        adaptive.observe(now,accepted,worst_raw_us,accepted==burst_size?sorted[3]:worst_raw_us);
        if(best_us){latest=static_cast<unsigned>((*best_us+500)/1000);received_us=now;
            latest_samples=accepted;latest_raw_us=best_raw_us;latest_handling_us=best_handling_us;}
        active=false;for(auto& p:probes)p.pending=false;
    }
    bool due(uint64_t now)const{return active?(attempted<burst_size && now>=sample_due && now<deadline):now>=next_us;}
    uint32_t begin(uint64_t now){
        if(!due(now))return 0;
        if(!active){active=true;attempted=accepted=0;best_us.reset();
            worst_raw_us=0;raw_samples={};
            probes={};next_us=now+3000000;deadline=now+500000;}
        if(++sequence==0)++sequence;
        probes[attempted++]={sequence,now,true};sample_due=now+20000;return sequence;
    }
    void send_failed(uint32_t id){for(auto& p:probes)if(p.id==id)p.pending=false;}
    bool pong(uint32_t id,uint64_t now,uint64_t handling_us){
        if(!active || now>=deadline)return false;
        for(auto& p:probes)if(p.pending && p.id==id){
            if(now<p.sent || handling_us>now-p.sent)return false;
            const auto raw=now-p.sent,adjusted=raw-handling_us;p.pending=false;raw_samples[accepted++]=raw;
            if(raw>worst_raw_us)worst_raw_us=raw;
            if(!best_us || adjusted<*best_us){best_us=adjusted;best_raw_us=raw;best_handling_us=handling_us;}
            return true;
        }
        return false;
    }
    std::optional<unsigned> value(uint64_t now)const{
        return latest && now>=received_us && now-received_us<=9000000?latest:std::nullopt;
    }
};
}
