#pragma once
#include <array>
#include <atomic>
#include <cstdint>
namespace sc6::research {
// One game-thread writer. Published rows are immutable; report readers acquire
// count before reading them. Observation only, never activation authority.
class StartupBoundaryPairs {
public:
    struct Row {uint32_t identity{},clock{},battle{};};
    static constexpr unsigned capacity=120;
    std::array<Row,capacity> rows{};
    std::atomic<unsigned> count{},overwritten{},unpaired{},discontinuities{};
private:
    Row pending_{};bool pending_valid_{};
public:
    void clock(uint32_t identity,uint32_t frame){
        if(count.load(std::memory_order_relaxed)==capacity)return;
        if(pending_valid_)++overwritten;
        pending_={identity,frame,0};pending_valid_=true;
    }
    void battle(uint32_t frame){
        const auto n=count.load(std::memory_order_relaxed);if(n==capacity)return;
        if(!pending_valid_){++unpaired;return;}
        pending_valid_=false;pending_.battle=frame;
        if(n){const auto& previous=rows[n-1];
            if(pending_.identity!=previous.identity || previous.clock==UINT32_MAX || previous.battle==UINT32_MAX ||
                pending_.clock!=previous.clock+1 || frame!=previous.battle+1)++discontinuities;
        }
        rows[n]=pending_;count.store(n+1,std::memory_order_release);
    }
};
}
