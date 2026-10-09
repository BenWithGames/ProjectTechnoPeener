#pragma once
#include "NativeTwoMovement.hpp"
namespace sc6::research {
// Three independent correction cycles; retain every result, stop on first failure.
struct RepeatedMovement {
    std::array<std::unique_ptr<NativeTwoMovement>,3> cycles;
    unsigned active{},completed{},cooldown{};
    uintptr_t next_argument{};bool done{},fault{};
    RepeatedMovement(){cycles[0]=std::make_unique<NativeTwoMovement>(3);}
    bool step(uintptr_t base,uintptr_t argument,bool bridge,FullStateProbe::Routine writer=nullptr,
              FullStateProbe::Routine reader=nullptr,FullStateProbe::Preflight check=nullptr) noexcept {
        next_argument=0;if(fault)return false;if(done)return true;
        if(!bridge){fault=true;return false;}
        if(cooldown){--cooldown;return true;}
        auto& cycle=*cycles[active];
        if(!cycle.step(base,argument,bridge,writer,reader,check)){fault=true;return false;}
        next_argument=cycle.next_argument;
        if(cycle.done){
            if(!cycle.corrected || cycle.compared!=3){fault=true;next_argument=0;return false;}
            ++completed;next_argument=0;
            if(completed==cycles.size()){done=true;return true;}
            ++active;cycles[active]=std::make_unique<NativeTwoMovement>(3);cooldown=60;
        }
        return true;
    }
};
}
