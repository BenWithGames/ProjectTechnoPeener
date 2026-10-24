#pragma once
#include <cstdint>
namespace sc6::research {
// Preserve short-circuit evaluation: never run later probes after rejection.
struct PostloadReadiness {
    unsigned stage{},passed{};
    bool enter(unsigned value)noexcept{stage=value;return true;}
    bool pass(unsigned value)noexcept{passed|=1u<<(value-1);return true;}
    unsigned rejected()const noexcept{return stage && !(passed&(1u<<(stage-1)))?stage:0;}
};
}
