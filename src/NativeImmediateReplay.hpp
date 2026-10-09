#pragma once
#include "NativeAdjacentRestore.hpp"
#include "SingleUpdateCall.hpp"
namespace sc6::research {
// Offline idle only: capture A, let one natural update produce B, restore A,
// synchronously simulate A->B once, compare B, then let the original B update
// proceed normally. This does not replace online networking or skip an update.
struct NativeImmediateReplay {
    NativeBoundarySnapshot source{NativeBoundarySnapshot::Mode::idle};
    NativeAdjacentRestore restore;
    SingleUpdateCall invocation;
    RecordedInput delivery;
    bool done{},fault{};
    const char* stage="capture";
    bool step(uintptr_t base,uintptr_t argument,uintptr_t caller,SingleUpdateCall::Routine original) noexcept {
        if(fault)return false;
        if(done)return true;
        stage="entry_guard";
        if(caller!=base+0x3FE816 || !original || reinterpret_cast<uintptr_t>(original)==base+0x2DBC60)return fail();
        std::array<std::byte,40> inputs{};
        if(!FullStateProbe::input_bytes(argument,inputs))return fail();
        for(unsigned i=0;i<24;++i)if(inputs[i]!=std::byte{})return fail();
        stage="capture_and_restore";
        if(!restore.step(source,base,argument))return fail();
        if(!restore.verified)return true; // First natural reference frame.
        stage="single_update";
        if(!invocation.run(original,delivery.assign(source.data->delayed_inputs),
            reinterpret_cast<volatile uint32_t*>(base+0x470D0C4),
            source.data->before_keys[0],source.thread))return fail();
        stage="compare";
        restore.replay_delivered=true;
        if(!restore.replay_step(source,base,argument,true))return fail();
        done=true;stage="passed";return true;
    }
private:
    bool fail(){fault=true;return false;}
};
}
