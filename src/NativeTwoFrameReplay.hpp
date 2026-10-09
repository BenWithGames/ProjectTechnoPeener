#pragma once
#include "NativeAdjacentRestore.hpp"
namespace sc6::research {
// Two-frame bounded reference experiment, not a synchronous catch-up loop.
struct NativeTwoFrameReplay {
    std::array<std::unique_ptr<NativeBoundarySnapshot>,3> frames;
    std::array<std::unique_ptr<NativeAdjacentRestore>,2> hops;
    RecordedInput delivery,guard;
    unsigned captured{},restored{},compared{},delivered{};
    uintptr_t next_argument{};bool prepared{},done{},fault{};
    NativeTwoFrameReplay(){
        for(auto& frame:frames)frame=std::make_unique<NativeBoundarySnapshot>(NativeBoundarySnapshot::Mode::idle);
        for(auto& hop:hops)hop=std::make_unique<NativeAdjacentRestore>();
    }
    __declspec(noinline) bool step(uintptr_t base,uintptr_t argument,bool bridge,
        FullStateProbe::Routine writer=nullptr,FullStateProbe::Routine reader=nullptr,
        FullStateProbe::Preflight check=nullptr) noexcept {
        next_argument=0;if(fault)return false;if(done)return true;
        if(!bridge)return fail();
        if(captured<3){
            // Identical capture call path for all native temporary addresses.
            if(!frames[captured]->capture(base,argument,writer,check))return fail();
            ++captured;if(captured<3)return true;
            for(unsigned i=0;i<2;++i)if(!hops[i]->prepare_captured(*frames[i],*frames[i+1]))return fail();
            prepared=true;
            for(unsigned i=2;i-- >0;){
                auto input=guard.assign(frames[i+1]->data->delayed_inputs);
                if(!hops[i]->restore_captured(*frames[i],base,input,writer,reader,check))return fail();
                ++restored;
            }
            next_argument=delivery.assign(frames[0]->data->delayed_inputs);++delivered;
            return true;
        }
        auto& hop=*hops[compared];
        // Delivery occurred through this controller's owned bridge envelope.
        hop.replay_delivered=true;
        if(!hop.replay_step(*frames[compared],base,argument,bridge,writer,reader,check))return fail();
        ++compared;
        if(compared==2){done=true;return true;}
        next_argument=delivery.assign(frames[compared]->data->delayed_inputs);++delivered;
        return true;
    }
private:
    bool fail(){fault=true;return false;}
};
}
