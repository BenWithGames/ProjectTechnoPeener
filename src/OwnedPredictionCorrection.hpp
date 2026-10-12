#pragma once
#include "ConfirmedReplayWindow.hpp"
#include "SingleUpdateCall.hpp"
namespace sc6::research {
// Explicit experiment backend, not registered with the live suite. Both native
// windows are constructed before entry. The caller supplies a drained,
// authenticated handoff and owns the update boundary for the entire call.
class OwnedPredictionCorrection {
public:
    inline static constexpr const char* diagnostic_names[]={
        "window","plan","prepare_stage","hop","rejection","restore_stage","restore_invoked","restore_verified",
        "failure_line","exception","probe_fault","replay_stage","comparison_mask","wind_failure_line","wind_record",
        "wind_kind","wind_size","wind_offset","wind_saved_word","wind_next_word","topology_mask","source_frame","next_frame"};
    using Diagnostics=std::array<uint64_t,std::size(diagnostic_names)>;
    Diagnostics diagnostics(){
        auto& window=stage==Stage::clean_rewind?clean:predicted_;
        auto& hop=window.diagnostic();const auto& p=*hop.probe;const auto& w=p.wind_diagnostic;
        return {stage==Stage::clean_rewind?0u:1u,window.plan_result,window.prepare_stage,window.last_hop,
            static_cast<unsigned>(hop.rejection),hop.diagnostic_stage,p.restore_invoked,hop.verified,
            p.delayed_failure_line,p.exception,p.fault,hop.replay_stage,hop.replay_comparison_mask,
            w.failure_line,w.record,w.kind,w.size,w.offset,w.saved_word,w.next_word,window.topology.mask,
            window.source_frame,window.next_frame};
    }
    using Input=std::array<std::byte,40>;
    using Frames=std::array<CorrectedHistoryReplay::Frame,3>;
    enum class Stage {unused,confirmation,clean_rewind,prediction,capture,divergence,correction_rewind,replay,complete};
    Stage stage{Stage::unused};
    unsigned predicted_updates{},corrected_updates{},capture_detail{};
    bool attempted{},injected{},state_changed{},verified{},restart_required{},retired{};
    NativeRollbackWindow clean;
private:
    NativeRollbackWindow predicted_;
    RecordedInput delivery_,guard_;
public:
    // A reference comparison needs independently recorded truth. A peer pair
    // which disagrees with the clean reference cannot be used as its oracle.
    static bool inputs(const Frames& frames,const std::array<RollbackBoundaryKey,4>& keys,
                       const std::array<Input,3>& reference,std::array<Input,3>& actual,std::array<Input,3>& wrong){
        actual={};wrong={};std::array<Input,3> staged{},fixtures{};
        for(unsigned i=0;i<3;++i){const auto& f=frames[i];
            if(!f.confirmed || !keys[i].same_round(keys[3]) || keys[i].frame>=keys[3].frame ||
               keys[3].frame-keys[i].frame!=3-i || !f.key.same_round(keys[i]) || f.key.frame!=keys[i].frame)return false;
            const auto composed=NetworkInputComposer::compose(f.local,f.peer,3-keys[i].slot,f.key.frame,f.key.frame);
            if(!composed)return false;
            std::memcpy(staged[i].data(),composed->data(),40);
            if(staged[i]!=reference[i] || !RecordedInput::valid_envelope(staged[i]))return false;
            // Only the measured neutral-to-screen-right fixture is supported.
            for(unsigned j=0;j<24;++j)if(staged[i][j]!=std::byte{})return false;
            fixtures[i]=staged[i];const unsigned remote=3-keys[i].slot,bits=0x400;
            for(unsigned offset:{8u*(remote-1),8u*(remote-1)+4u,16u+4u*(remote-1)})
                std::memcpy(fixtures[i].data()+offset,&bits,4);
        }
        actual=staged;wrong=fixtures;return true;
    }
    bool release(){
        if(retired)return true;
        // Attempt both drains even if one fails.
        const bool a=predicted_.release_owned_dependencies(),b=clean.release_owned_dependencies();
        retired=a&&b;return retired;
    }
    bool run(const NetworkGameHandoff& handoff,const RollbackBoundaryKey& live,uintptr_t base,
             ReplayDependencySource& source,SingleUpdateCall::Routine original){
        Frames frames{};
        if(clean.count_==4)confirmed_replay_window(handoff,clean.keys_[0],live,frames);
        return run_confirmed(frames,live,base,source,original);
    }
    // Alternative caller is the startup suite's authenticated three-record
    // exchange. It copies immutable confirmations at the held owner boundary.
    bool run_confirmed(const Frames& frames,const RollbackBoundaryKey& live,uintptr_t base,
             ReplayDependencySource& source,SingleUpdateCall::Routine original){
        if(attempted)return false;attempted=true;stage=Stage::confirmation;
        if(clean.count_!=4 || clean.fault_ || clean.rewind_started() || !base || base!=clean.base_ ||
           !original || reinterpret_cast<uintptr_t>(original)==base+0x2DBC60 ||
           live.thread!=GetCurrentThreadId() || !live.same_round(clean.keys_[3]) ||
           live.frame!=clean.keys_[3].frame || !source.available() || !clean.dependencies_valid())return false;
        std::array<Input,3> reference{},actual{},wrong{};
        for(unsigned i=0;i<3;++i)if(!clean.pool_.visit(clean.handles_[i],[&](NativeBoundarySnapshot& s){reference[i]=s.data->delayed_inputs;return true;}))return false;
        if(!inputs(frames,clean.keys_,reference,actual,wrong))return false;
        try {
            stage=Stage::clean_rewind;
            if(!clean.rewind(live)){restart_required=clean.rewind_started() || clean.any_restore_invoked();return false;}
            restart_required=true;
            // Preserve owned references for the whole predicted branch; use
            // the same capture entry for its initial and advanced snapshots.
            for(unsigned i=0;i<4;++i){
                stage=Stage::capture;Input envelope{};
                if(!clean.pool_.visit(clean.handles_[i],[&](NativeBoundarySnapshot& s){envelope=s.data->delayed_inputs;return true;}))return false;
                const auto mode=i?NativeBoundarySnapshot::Mode::direction_observation:NativeBoundarySnapshot::Mode::idle;
                if(!predicted_.capture(clean.keys_[i],base,guard_.assign(envelope),mode,true,&source)){
                    capture_detail=predicted_.capture_stage;return false;
                }
                if(i==3)break;
                stage=Stage::prediction;SingleUpdateCall call;
                const bool ok=call.run(original,delivery_.assign(wrong[i]),reinterpret_cast<volatile uint32_t*>(base+0x470D0C4),clean.keys_[i].frame,live.thread);
                injected=injected||call.invoked;if(!ok)return false;++predicted_updates;
            }
            stage=Stage::divergence;
            if(!clean.pool_.visit(clean.handles_[3],[&](NativeBoundarySnapshot& a){return predicted_.pool_.visit(predicted_.handles_[3],[&](NativeBoundarySnapshot& b){
                // Require player-state divergence, not just a changed input
                // envelope or unrelated camera/visual bookkeeping.
                for(unsigned side=0;side<2;++side)state_changed=state_changed || std::memcmp(a.data->measurements[0].fields[side].data()+8,b.data->measurements[0].fields[side].data()+8,216)!=0;
                return true;
            });}))return false;
            stage=Stage::correction_rewind;if(!predicted_.rewind(live))return false;
            stage=Stage::replay;
            for(unsigned i=0;i<3;++i){
                predicted_.last_hop=i;
                if(!clean.dependencies_valid() || !predicted_.dependencies_valid())return false;
                const bool ok=clean.pool_.visit(clean.handles_[i],[&](NativeBoundarySnapshot& a){return clean.pool_.visit(clean.handles_[i+1],[&](NativeBoundarySnapshot& b){
                    auto& hop=*predicted_.hops_[i];auto& q=*hop.probe;const auto& ref=*b.data;
                    q.after_save=ref.saved;q.after_keys=ref.before_keys;
                    std::memcpy(q.after_players,ref.before_players,sizeof(q.after_players));
                    q.original_next=ref.measurements[0];q.rings[2]=ref.rings[0];q.blends[1]=ref.blends[0];q.transforms[1]=ref.transforms[0];q.cameras[1]=ref.cameras[0];
                    SingleUpdateCall call;
                    if(!call.run(original,delivery_.assign(actual[i]),reinterpret_cast<volatile uint32_t*>(base+0x470D0C4),clean.keys_[i].frame,live.thread))return false;
                    hop.replay_delivered=true;
                    return hop.replay_step(a,base,guard_.assign(ref.delayed_inputs),true);
                });});
                if(!ok)return false;++corrected_updates;
            }
            // Even a no-divergence fixture is returned to the clean endpoint,
            // but cannot be reported as a successful prediction correction.
            if(!release())return false;
            restart_required=false;verified=state_changed;stage=Stage::complete;return verified;
        }catch(...){restart_required=clean.rewind_started() || clean.any_restore_invoked();return false;}
    }
};
}
