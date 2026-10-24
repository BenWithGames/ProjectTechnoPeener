#pragma once
#include <cstdint>
namespace sc6::research {
// Native 30EC90/30EDC0 terminate at null, not at 64 nodes. This is a
// defensive work budget, not a claimed native capacity. Brent cycle detection
// uses the already-read links: no allocation or speculative pointer reads.
struct AnimationListGuard {
    static constexpr unsigned limit=4096;
    enum class Result {accepted,cycle,budget};
    unsigned count{},power{1},distance{};
    uintptr_t anchor{};
    Result visit(uintptr_t node) noexcept {
        if(count && node==anchor)return Result::cycle;
        if(count==limit)return Result::budget;
        if(!count){anchor=node;}else{
            if(++distance==power){anchor=node;power*=2;distance=0;}
        }
        ++count;return Result::accepted;
    }
};
}
