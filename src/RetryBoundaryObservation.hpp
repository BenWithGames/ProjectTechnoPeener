#pragma once
#include "StartupBoundaryPairs.hpp"
#include <mutex>
namespace sc6::research {
// Owner publishes paired observations; worker reads an immutable copy under lock.
class RetryBoundaryObservation {
    StartupBoundaryPairs::Row pending_{},last_{};
    bool pending_valid_{};unsigned stable_{};uint64_t tick_{};std::mutex mutex_;
public:
    struct Sample {unsigned identity{},clock{},battle{},stable{};uint64_t tick{};};
    void clock(unsigned id,unsigned frame){pending_={id,frame,0};pending_valid_=frame!=0;}
    void battle(unsigned frame,uint64_t now){
        std::lock_guard<std::mutex> lock(mutex_);
        if(!pending_valid_){stable_=0;return;}pending_valid_=false;pending_.battle=frame;
        const bool adjacent=pending_.identity==last_.identity && last_.clock<UINT32_MAX && last_.battle<UINT32_MAX &&
            pending_.clock==last_.clock+1 && frame==last_.battle+1;
        stable_=adjacent?(stable_<120?stable_+1:120):1;last_=pending_;tick_=now;
    }
    Sample read(uint64_t now){std::lock_guard<std::mutex> lock(mutex_);
        if(now<tick_ || now-tick_>250 || stable_<4)return {};
        return {last_.identity,last_.clock,last_.battle,stable_,tick_};
    }
};
}
