#pragma once
#include "NativeImmediateReplay.hpp"
#include "FieldUpdateTrace.hpp"
namespace sc6::research {
// Four natural idle boundaries, adjacent guarded rewind to the first, then
// three synchronous updates with a reference comparison after every update.
struct NativeThreeImmediateReplay {
    std::array<std::unique_ptr<NativeBoundarySnapshot>,7> frames;
    std::array<std::unique_ptr<NativeAdjacentRestore>,6> hops;
    std::array<SingleUpdateCall,6> calls;
    RecordedInput delivery,guard;
    unsigned steps{3};long long replay_ticks{},clock_frequency{};
    std::array<TrainingFieldReset::Samples,6> resets{};
    unsigned resets_applied{},waited{};bool trace_started{};
    unsigned captured{},prepared{},restored{},invoked{},compared{},hop_index{};
    bool done{},fault{};const char* stage="capture";
    explicit NativeThreeImmediateReplay(unsigned depth=3):steps(depth==6?6u:3u){
        for(unsigned i=0;i<=steps;++i)frames[i]=std::make_unique<NativeBoundarySnapshot>(NativeBoundarySnapshot::Mode::idle);
        for(unsigned i=0;i<steps;++i)hops[i]=std::make_unique<NativeAdjacentRestore>();
    }
    __declspec(noinline) bool step(uintptr_t base,uintptr_t argument,uintptr_t caller,SingleUpdateCall::Routine original) noexcept {
        if(fault)return false;if(done)return true;
        stage="entry_guard";
        if(caller!=base+0x3FE816 || !original || reinterpret_cast<uintptr_t>(original)==base+0x2DBC60)return fail();
        std::array<std::byte,40> inputs{};
        if(!FullStateProbe::input_bytes(argument,inputs))return fail();
        for(unsigned i=0;i<24;++i)if(inputs[i]!=std::byte{})return fail();
        if(!trace_started){field_update_trace.start(base);trace_started=true;}
        // Capture a three-frame window containing the observed periodic increment.
        if(!captured){
            stage="wait_for_increment";
            if(++waited>600)return fail();
            const auto& trace=field_update_trace;
            if(!trace.count || trace.events[trace.count-1].assignment!=0)return true;
            const auto last=trace.events[trace.count-1];
            const auto now=*reinterpret_cast<unsigned*>(base+0x470D0C4);
            // The next increment is observed every 40 updates in this idle test.
            if(now-last.counter!=39)return true;
        }
        field_update_trace.phase=1;stage="capture";
        if(captured>=steps+1 || !frames[captured]->capture(base,argument))return fail();
        if(captured){
            resets[captured-1]=training_field_reset.latest;
            if(!TrainingFieldReset::validate(resets[captured-1],base,frames[captured]->data->before_keys[0]))return fail();
        }
        if(++captured<steps+1)return true;
        stage="prepare";
        for(hop_index=0;hop_index<steps;++hop_index){
            if(!hops[hop_index]->prepare_captured(*frames[hop_index],*frames[hop_index+1]))return fail();
            ++prepared;
        }
        field_update_trace.phase=2;stage="restore";
        for(unsigned n=steps;n>0;--n){
            hop_index=n-1;
            if(!hops[hop_index]->restore_captured(*frames[hop_index],base,
                guard.assign(frames[hop_index+1]->data->delayed_inputs)))return fail();
            ++restored;
        }
        LARGE_INTEGER replay_start{},replay_end{},frequency{};
        QueryPerformanceFrequency(&frequency);clock_frequency=frequency.QuadPart;QueryPerformanceCounter(&replay_start);
        for(hop_index=0;hop_index<steps;++hop_index){
            field_update_trace.phase=3;stage="update";auto& source=*frames[hop_index];auto& call=calls[hop_index];
            const bool advanced=call.run(original,delivery.assign(source.data->delayed_inputs),
                reinterpret_cast<volatile uint32_t*>(base+0x470D0C4),source.data->before_keys[0],source.thread);
            if(call.invoked)++invoked;
            if(!advanced)return fail();
            stage="training_setter";
            if(!TrainingFieldReset::apply(resets[hop_index],base,call.after))return fail();
            ++resets_applied;
            field_update_trace.phase=4;stage="compare";auto& hop=*hops[hop_index];hop.replay_delivered=true;
            if(!hop.replay_step(source,base,guard.assign(frames[hop_index+1]->data->delayed_inputs),true))return fail();
            ++compared;
        }
        QueryPerformanceCounter(&replay_end);replay_ticks=replay_end.QuadPart-replay_start.QuadPart;
        field_update_trace.stop();done=true;stage="passed";return true;
    }
private:
    bool fail(){field_update_trace.stop();fault=true;return false;}
};
}
