#pragma once
#include "InputObservation.hpp"
#include <map>
#include <stdexcept>
namespace sc6 {
// Observational scheduler: these decisions never reach the game's input path.
struct LiveShadowPrediction {
    static constexpr unsigned capacity=observation::capacity,delay=4,window=3;
    struct Choice {bool predicted=false,seeded=false,settled=false;uint32_t held=0,edge=0;};
    std::array<Choice,capacity> choices{};
    std::array<bool,capacity> imported{};
    struct Remote {observation::Record record;uint64_t arrival;};
    std::map<uint32_t,Remote> history;
    unsigned remote_slot=2,scheduled=0,validated=0,evaluated=0,predicted=0,unseeded=0,wrong=0,over_limit=0,max_depth=0,censored=0,truth_mismatches=0,no_overlap=0;
    bool invalid=false;
    static uint64_t tick(const observation::Record& r){return (uint64_t(r.data[12])<<32)|r.data[11];}
    void update(const std::array<observation::Record,capacity>& local,unsigned published,
                const std::array<observation::Record,capacity>& remote,const std::array<bool,capacity>& seen,
                const std::array<uint64_t,capacity>& arrivals,bool all_received) {
        if(remote_slot<1||remote_slot>2||published>capacity)throw std::runtime_error("shadow_invalid_configuration");
        const unsigned w=remote_slot==1?1:3;
        while(validated<published){
            if(validated && (local[validated].data[0]!=local[validated-1].data[0]+1 || tick(local[validated])<=tick(local[validated-1])))invalid=true;
            ++validated;
        }
        for(unsigned i=0;i<capacity;++i)if(seen[i]&&!imported[i]){
            if(!arrivals[i] || history.contains(remote[i].data[0]))invalid=true;
            history.emplace(remote[i].data[0],Remote{remote[i],arrivals[i]});imported[i]=true;
        }
        if(invalid)return;
        while(scheduled+delay<published){
            auto& choice=choices[scheduled];const auto counter=local[scheduled].data[0];
            const uint64_t deadline=tick(local[scheduled+delay]);
            auto exact=history.find(counter);
            if(exact!=history.end() && exact->second.arrival<=deadline){choice.seeded=true;choice.held=exact->second.record.data[w];choice.edge=exact->second.record.data[w+1];}
            else {
                choice.predicted=true;
                auto prior=history.lower_bound(counter);
                while(prior!=history.begin()){
                    --prior;
                    if(prior->second.arrival<=deadline){choice.seeded=true;choice.held=prior->second.record.data[w];choice.edge=0;break;}
                }
            }
            ++scheduled;
        }
        for(unsigned i=0;i<scheduled;++i){
            auto& choice=choices[i];if(choice.settled)continue;
            auto truth=history.find(local[i].data[0]);
            if(truth==history.end()){if(all_received){choice.settled=true;++no_overlap;}continue;}
            const auto& actual=truth->second.record;
            if(actual.data[w]!=local[i].data[w]||actual.data[w+1]!=local[i].data[w+1]){choice.settled=true;++truth_mismatches;continue;}
            const bool mismatch=choice.seeded && (choice.held!=actual.data[w]||choice.edge!=actual.data[w+1]);
            const bool beyond=truth->second.arrival>tick(local[scheduled-1+delay]);
            if(mismatch && beyond && published<capacity)continue;
            choice.settled=true;++evaluated;
            if(!choice.seeded){++unseeded;continue;}
            if(choice.predicted)++predicted;
            if(mismatch){
                ++wrong;unsigned end=i;
                while(end<scheduled && tick(local[end+delay])<=truth->second.arrival)++end;
                const unsigned depth=end-i;
                if(depth>max_depth)max_depth=depth;
                if(depth>window)++over_limit;
                if(beyond)++censored;
            }
        }
    }
    bool within_observed_bounds()const{return evaluated>0&&!invalid&&!truth_mismatches&&!over_limit&&!censored;}
};
}
