#pragma once
#include "NativeAdjacentRestore.hpp"
#include "DelayedInputHistory.hpp"
#include "IdleBlendSettle.hpp"
#include "SingleUpdateCall.hpp"
#include "FieldUpdateTrace.hpp"
#include "NetworkInputComposer.hpp"
namespace sc6::research {
// One bounded auxiliary-float or observed Player1 screen-right prediction.
// Optional synchronous correction; all modes remain offline experiments.
struct NativePredictionProbe {
    using Input=std::array<std::byte,40>;
    using History=DelayedInputHistory<Input,4>;
    std::array<std::unique_ptr<NativeBoundarySnapshot>,4> frames;
    std::array<std::unique_ptr<NativeAdjacentRestore>,2> restores;
    std::unique_ptr<History> history;
    RecordedInput delivery,guard;
    Input predicted{},actual{};
    IdleBlendSettle settle;
    uintptr_t waiting_camera{};unsigned camera_wait_samples{},camera_stable{};
    std::array<std::byte,128> previous_targets{};
    bool camera_settled(const CameraSnapshot::Record& r){
        return r.smoothing_valid && CameraSnapshot::word(r.targets.data()+104)==0 &&
            CameraSnapshot::word(r.targets.data()+108)==0 && CameraSnapshot::word(r.response.data())==0 &&
            CameraSnapshot::word(r.response.data()+8)==0;
    }
    bool wait_camera() noexcept {
        __try {
            if(++camera_wait_samples>600)return false;
            std::array<std::byte,128> targets{};
            std::memcpy(targets.data(),reinterpret_cast<void*>(waiting_camera+0x1E8),116);
            std::memcpy(targets.data()+116,reinterpret_cast<void*>(waiting_camera+0x2A4),12);
            bool neutral=CameraSnapshot::word(targets.data()+104)==0 && CameraSnapshot::word(targets.data()+108)==0 &&
                *reinterpret_cast<unsigned*>(waiting_camera+0x1D0)==0 && *reinterpret_cast<unsigned*>(waiting_camera+0x1D8)==0;
            camera_stable=neutral?(targets==previous_targets?camera_stable+1:1):0;
            previous_targets=targets;return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    unsigned captured{},loads{},deliveries{};uintptr_t next_argument{};
    bool dirty_detected{},state_changed{},done{},fault{},corrected{};
    bool two_frame_capture{},a_capture{},a_restore{},synchronous{},trace_started{},training_reset_applied{};
    SingleUpdateCall immediate_call;
    TrainingFieldReset::Samples reference_resets{};
    bool compose_inputs{},composition_passed{},peer_aux_different{};
    bool direction_mode{},movement_restore{},prediction_observed{};unsigned prediction_player_input{},prediction_player_move{};
    const char* failure_stage="none";
    explicit NativePredictionProbe(bool direction=false,bool restore_movement=false,bool capture_two=false,bool capture_a=false,bool restore_a=false,bool immediate=false,bool compose=false):two_frame_capture(capture_two),a_capture(capture_a),a_restore(restore_a),synchronous(immediate),compose_inputs(compose),direction_mode(direction),movement_restore(restore_movement){
        if(compose_inputs && !synchronous)fault=true;
        if(synchronous && !a_restore)fault=true;
        if(a_restore && !a_capture)fault=true;
        if(a_capture && (direction_mode || movement_restore || two_frame_capture))fault=true;
        if(two_frame_capture && (!direction_mode || movement_restore))fault=true;
        if(movement_restore && !direction_mode)fault=true;
        for(auto& f:frames)f=std::make_unique<NativeBoundarySnapshot>(NativeBoundarySnapshot::Mode::idle);
        for(auto& r:restores)r=std::make_unique<NativeAdjacentRestore>();
        if(a_capture)frames[2]->mode=NativeBoundarySnapshot::Mode::a_observation;
        if(direction_mode){frames[2]->mode=NativeBoundarySnapshot::Mode::direction_observation;frames[3]->mode=NativeBoundarySnapshot::Mode::direction_observation;}
    }
    static bool make_prediction(const Input& original,Input& result,bool direction=false,unsigned direction_bits=0x400){
        if(!RecordedInput::compatible(original,original))return false;
        result=original;
        if(direction){
            // Exact press envelope observed at sample313 of the confirmed
            // screen-right capture. Require neutral buttons/edges beforehand.
            for(unsigned i=0;i<24;++i)if(original[i]!=std::byte{})return false;
            if(direction_bits!=0x400 && direction_bits!=0x800)return false;
            const unsigned right=direction_bits;
            for(unsigned offset:{0u,4u,16u})std::memcpy(result.data()+offset,&right,4);
            return true;
        }
        float value=0.25f;
        if(!std::memcmp(original.data()+24,&value,4))value=-0.25f;
        std::memcpy(result.data()+24,&value,4);
        return result!=original && RecordedInput::compatible(original,result);
    }
    static bool make_a_prediction(const Input& original,Input& result){
        if(!make_prediction(original,result,true))return false;
        const unsigned a=1;for(unsigned off:{0u,4u,16u})std::memcpy(result.data()+off,&a,4);
        return true;
    }
    __declspec(noinline) bool step(uintptr_t base,uintptr_t argument,bool bridge,
        FullStateProbe::Routine writer=nullptr,FullStateProbe::Routine reader=nullptr,
        FullStateProbe::Preflight check=nullptr,SingleUpdateCall::Routine original=nullptr,uintptr_t caller=0) noexcept {
        next_argument=0;if(fault)return false;if(done)return true;if(!bridge)return fail();
        if(synchronous && (!original || reinterpret_cast<uintptr_t>(original)==base+0x2DBC60 || caller!=base+0x3FE816)){failure_stage="immediate_entry_guard";return fail();}
        if((direction_mode || a_capture) && !captured && !settle.ready){
            failure_stage="waiting_for_stable_idle";
            if(!settle.step(base))return fail();
            if(!settle.ready)return true;
            failure_stage="none";
        }
        if(waiting_camera){
            failure_stage="waiting_for_idle_camera";
            if(!wait_camera())return fail();
            if(camera_stable<10)return true;
            waiting_camera=0;
            frames[0]=std::make_unique<NativeBoundarySnapshot>(NativeBoundarySnapshot::Mode::idle);
        }
        if(two_frame_capture && captured==3){
            failure_stage="second_prediction_capture";
            if(!observe_prediction(base) || !frames[3]->capture(base,argument,writer,check))return fail();
            ++captured;
            if(!history || history->receive(0,actual)!=History::Arrival::accepted || history->correction()!=std::optional<std::size_t>{0})return fail();
            dirty_detected=true;failure_stage="none";done=true;return true;
        }
        if(synchronous && !trace_started){field_update_trace.start(base);trace_started=true;}
        if(captured<3){
            if(captured==2 && !observe_prediction(base)){failure_stage="prediction_observation";return fail();}
            failure_stage="boundary_capture";
            if(!frames[captured]->capture(base,argument,writer,check))return fail();
            if((two_frame_capture || a_capture) && captured==0){
                for(unsigned i=0;i<frames[0]->data->cameras[0].count;++i){
                    const auto& camera=frames[0]->data->cameras[0].records[i];
                    if(camera.smoothing_valid && !camera_settled(camera)){
                        waiting_camera=camera.owner;camera_stable=0;failure_stage="waiting_for_idle_camera";return true;
                    }
                }
            }
            failure_stage="none";
            ++captured;
            if(captured==1){
                actual=frames[0]->data->delayed_inputs;
                if(!(a_capture?make_a_prediction(actual,predicted):make_prediction(actual,predicted,direction_mode)))return fail();
                return true; // Clean reference update with the original argument.
            }
            if(captured==2 && synchronous){
                reference_resets=training_field_reset.latest;
                if(!TrainingFieldReset::validate(reference_resets,base,frames[1]->data->before_keys[0])){failure_stage="reference_setter_guard";return fail();}
            }
            if(captured==2){
                for(unsigned side=0;side<2;++side){
                    const auto& a=frames[0]->data->blends[0];const auto& b=frames[1]->data->blends[0];
                    if(!a.valid || !b.valid || a.owners[side]!=b.owners[side] ||
                       std::memcmp(a.bytes[side].data(),b.bytes[side].data(),1372)){
                        failure_stage="unstable_reference_blend";return fail();
                    }
                }
            }
            auto& restore=*restores[captured-2];
            if(captured==3){
                if(compose_inputs){
                    // Synthetic received P1 pair, with deliberately different
                    // non-owned words. Preserve this PC's historical auxiliary
                    // block and P2 pair; never send this fixture over Steam.
                    sc6::NetworkInputComposer::Envelope local{},peer{};
                    std::memcpy(local.data(),actual.data(),40);peer=local;
                    std::memcpy(local.data(),predicted.data(),8);
                    for(unsigned w=2;w<10;++w)peer[w]^=1;
                    peer_aux_different=std::memcmp(peer.data()+4,actual.data()+16,24)!=0;
                    const auto frame=frames[0]->data->before_keys[0];
                    const auto composed=sc6::NetworkInputComposer::compose(local,peer,1,frame,frame);
                    if(!composed || !peer_aux_different || std::memcmp(composed->data(),actual.data(),40)){
                        failure_stage="input_composition";return fail();
                    }
                    // This composed result, not a bypass copy of actual, is
                    // accepted by history and consumed by the corrected update.
                    std::memcpy(actual.data(),composed->data(),40);composition_passed=true;
                }
                if(!two_frame_capture){
                    if(!history || history->receive(0,actual)!=History::Arrival::accepted || history->correction()!=std::optional<std::size_t>{0})return fail();
                    dirty_detected=true;
                }
                const auto& b=*frames[1]->data;const auto& p=*frames[2]->data;
                state_changed=!TwoFrameIdleReplay::transforms_equal(b.transforms[0],p.transforms[0]) ||
                    !TwoFrameIdleReplay::cameras_equal(b.cameras[0],p.cameras[0]) || b.blends[0].bytes!=p.blends[0].bytes || b.blends[0].controller!=p.blends[0].controller ||
                    b.measurements[0].rng!=p.measurements[0].rng;
                for(unsigned side=0;side<2;++side)state_changed=state_changed ||
                    std::memcmp(b.measurements[0].fields[side].data()+8,p.measurements[0].fields[side].data()+8,216)!=0;
            }
            if(captured==3 && a_capture && !a_restore){done=true;return true;}
            // Movement observation is not permission to load this new state.
            // Retain the immutable clean reference and export the divergent save.
            if(captured==3 && direction_mode && !movement_restore){
                if(two_frame_capture){Input used{};
                    if(!history || !history->advance(used) || used!=predicted)return fail();
                    // Explicit second wrong press, not an inferred held-input envelope.
                    next_argument=delivery.assign(predicted);++deliveries;return true;
                }
                done=true;return true;
            }
            if(captured==3 && movement_restore && !state_changed){failure_stage="no_movement_divergence";return fail();}
            failure_stage="restore_preparation";
            if(!restore.prepare_captured(*frames[0],*frames[captured-1],captured==3 && movement_restore,captured==3 && a_restore))return fail();
            failure_stage="native_restoration";
            if(!restore.restore_captured(*frames[0],base,guard.assign(frames[captured-1]->data->delayed_inputs),writer,reader,check))return fail();
            ++loads;failure_stage="none";
            if(captured==2){
                history=std::make_unique<History>(predicted);Input used{};
                if(!history->advance(used) || used!=predicted)return fail();
                next_argument=delivery.assign(used);++deliveries;return true;
            }
            // Restoration used the actually observed predicted branch. Compare
            // the corrected replay against the independent clean reference.
            auto& q=*restore.probe;const auto& b=*frames[1]->data;
            q.after_save=b.saved;q.after_keys=b.before_keys;
            std::memcpy(q.after_players,b.before_players,sizeof(q.after_players));
            q.original_next=b.measurements[0];q.rings[2]=b.rings[0];
            q.blends[1]=b.blends[0];q.transforms[1]=b.transforms[0];q.cameras[1]=b.cameras[0];
            restore.replay_delivered=true;
            if(synchronous){
                if(!state_changed){failure_stage="no_attack_divergence";return fail();}
                failure_stage="immediate_corrected_update";
                if(!immediate_call.run(original,delivery.assign(history->input_for(0)),reinterpret_cast<volatile uint32_t*>(base+0x470D0C4),frames[0]->data->before_keys[0],frames[0]->thread))return fail();
                ++deliveries;failure_stage="immediate_training_setter";
                if(!TrainingFieldReset::apply(reference_resets,base,immediate_call.after))return fail();
                training_reset_applied=true;failure_stage="immediate_comparison";
                if(!restore.replay_step(*frames[0],base,guard.assign(b.delayed_inputs),bridge,writer,reader,check))return fail();
                if(!history->finish_correction(0))return fail();
                field_update_trace.stop();failure_stage="none";corrected=done=true;return true;
            }
            next_argument=delivery.assign(history->input_for(0));++deliveries;return true;
        }
        failure_stage="corrected_replay";
        if(!restores[1]->replay_step(*frames[0],base,argument,bridge,writer,reader,check))return fail();
        if(!history->finish_correction(0))return fail();
        failure_stage="none";corrected=done=true;return true;
    }
private:
    bool observe_prediction(uintptr_t base) noexcept {
        __try {
            prediction_player_input=*reinterpret_cast<std::uint16_t*>(base+0x4717840);
            prediction_player_move=*reinterpret_cast<std::uint16_t*>(base+0x475A04A);
            prediction_observed=true;return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool fail(){if(synchronous)field_update_trace.stop();fault=true;return false;}
};
}
