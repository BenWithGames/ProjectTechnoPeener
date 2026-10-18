#include "RngCallerTrace.hpp"
#pragma once
#include "ConfirmedReplayWindow.hpp"
#include "SingleUpdateCall.hpp"
#include "ReplayFailureEvidence.hpp"
namespace sc6::research {
// Bounded suite backend. Both native
// windows are constructed before entry. The caller supplies a drained,
// authenticated handoff and owns the update boundary for the entire call.
template<class Window=NativeRollbackWindow,class Update=SingleUpdateCall,class Source=ReplayDependencySource>
class OwnedPredictionCorrectionBackend {
public:
    inline static constexpr const char* diagnostic_names[]={
        "window","plan","prepare_stage","hop","rejection","restore_stage","restore_invoked","restore_verified",
        "failure_line","exception","probe_fault","replay_stage","comparison_mask","wind_failure_line","wind_record",
        "wind_kind","wind_size","wind_offset","wind_saved_word","wind_next_word","topology_mask","source_frame","next_frame",
        "source_guard_reason","source_guard_record","source_guard_length","source_guard_saved","source_guard_next","source_guard_writer",
        "completion_line","completion_section","completion_record","completion_kind","completion_offset",
        "load_entry_rng","load_entry_rng_valid","expected_next_rng","preflight_mask",
        "blend_reason","blend_side","blend_offset","blend_changed","blend_saved","blend_next","blend_saved_owner","blend_next_owner",
        "camera_reason","camera_record","camera_section","camera_offset","camera_side","camera_saved","camera_next",
        "topology_index","topology_before_count","topology_after_count",
        "topology_before_address","topology_before_side","topology_before_kind","topology_before_list","topology_before_parent","topology_before_size",
        "topology_after_address","topology_after_side","topology_after_kind","topology_after_list","topology_after_parent","topology_after_size",
        "scalar_measurement_read","scalar_rng_equal","scalar_players_equal","scalar_rng_expected","scalar_rng_actual",
        "scalar_p1_differences","scalar_p1_first_offset","scalar_p1_expected_byte","scalar_p1_actual_byte",
        "scalar_p2_differences","scalar_p2_first_offset","scalar_p2_expected_byte","scalar_p2_actual_byte",
        "topology_removed_nodes","topology_added_nodes","topology_removed_address","topology_removed_vtable_rva","topology_removed_next","topology_removed_parent",
        "topology_added_address","topology_added_vtable_rva","topology_added_next","topology_added_parent","postload_pass_mask","loaded_blend_reason","loaded_blend_side","loaded_blend_block","loaded_blend_offset","loaded_blend_saved","loaded_blend_next","loaded_blend_actual","loaded_blend_pointer_valid","loaded_blend_pointer_saved","loaded_blend_pointer_next","loaded_blend_pointer_actual","loaded_blend_wrapper_saved","loaded_blend_wrapper_next","loaded_blend_wrapper_actual",
        "topology_removed_0_address","topology_removed_0_vtable_rva","topology_removed_0_next","topology_removed_0_parent","topology_removed_0_child_count","topology_removed_0_child_address","topology_removed_0_child_kind","topology_removed_0_child_size","topology_removed_0_child_bytes",
        "topology_removed_1_address","topology_removed_1_vtable_rva","topology_removed_1_next","topology_removed_1_parent","topology_removed_1_child_count","topology_removed_1_child_address","topology_removed_1_child_kind","topology_removed_1_child_size","topology_removed_1_child_bytes",
        "topology_added_0_address","topology_added_0_vtable_rva","topology_added_0_next","topology_added_0_parent","topology_added_0_child_count","topology_added_0_child_address","topology_added_0_child_kind","topology_added_0_child_size","topology_added_0_child_bytes",
        "topology_added_1_address","topology_added_1_vtable_rva","topology_added_1_next","topology_added_1_parent","topology_added_1_child_count","topology_added_1_child_address","topology_added_1_child_kind","topology_added_1_child_size","topology_added_1_child_bytes",
        "payload_structural","payload_records","payload_bytes","payload_retained","payload_overflow","payload_0_record","payload_0_length","payload_0_writer","payload_0_source_saved","payload_0_source_loaded","payload_0_first","payload_0_last","payload_0_expected","payload_0_actual","payload_1_record","payload_1_length","payload_1_writer","payload_1_source_saved","payload_1_source_loaded","payload_1_first","payload_1_last","payload_1_expected","payload_1_actual","payload_2_record","payload_2_length","payload_2_writer","payload_2_source_saved","payload_2_source_loaded","payload_2_first","payload_2_last","payload_2_expected","payload_2_actual","payload_3_record","payload_3_length","payload_3_writer","payload_3_source_saved","payload_3_source_loaded","payload_3_first","payload_3_last","payload_3_expected","payload_3_actual","payload_4_record","payload_4_length","payload_4_writer","payload_4_source_saved","payload_4_source_loaded","payload_4_first","payload_4_last","payload_4_expected","payload_4_actual","payload_5_record","payload_5_length","payload_5_writer","payload_5_source_saved","payload_5_source_loaded","payload_5_first","payload_5_last","payload_5_expected","payload_5_actual","payload_6_record","payload_6_length","payload_6_writer","payload_6_source_saved","payload_6_source_loaded","payload_6_first","payload_6_last","payload_6_expected","payload_6_actual","payload_7_record","payload_7_length","payload_7_writer","payload_7_source_saved","payload_7_source_loaded","payload_7_first","payload_7_last","payload_7_expected","payload_7_actual","payload_8_record","payload_8_length","payload_8_writer","payload_8_source_saved","payload_8_source_loaded","payload_8_first","payload_8_last","payload_8_expected","payload_8_actual","payload_9_record","payload_9_length","payload_9_writer","payload_9_source_saved","payload_9_source_loaded","payload_9_first","payload_9_last","payload_9_expected","payload_9_actual","payload_10_record","payload_10_length","payload_10_writer","payload_10_source_saved","payload_10_source_loaded","payload_10_first","payload_10_last","payload_10_expected","payload_10_actual","payload_11_record","payload_11_length","payload_11_writer","payload_11_source_saved","payload_11_source_loaded","payload_11_first","payload_11_last","payload_11_expected","payload_11_actual","payload_12_record","payload_12_length","payload_12_writer","payload_12_source_saved","payload_12_source_loaded","payload_12_first","payload_12_last","payload_12_expected","payload_12_actual","payload_13_record","payload_13_length","payload_13_writer","payload_13_source_saved","payload_13_source_loaded","payload_13_first","payload_13_last","payload_13_expected","payload_13_actual","payload_14_record","payload_14_length","payload_14_writer","payload_14_source_saved","payload_14_source_loaded","payload_14_first","payload_14_last","payload_14_expected","payload_14_actual","payload_15_record","payload_15_length","payload_15_writer","payload_15_source_saved","payload_15_source_loaded","payload_15_first","payload_15_last","payload_15_expected","payload_15_actual","replay_ring_status","replay_ring_validity","replay_ring_group","replay_ring_slot","replay_ring_offset","replay_ring_field","replay_ring_expected","replay_ring_actual","replay_ring_expected_word","replay_ring_actual_word","replay_native_reason","replay_native_record","replay_native_offset","replay_native_length","replay_native_writer","replay_native_source","replay_native_expected","replay_native_actual","replay_native_word_offset","replay_native_expected_word","replay_native_actual_word","replay_native_changed_bytes","camera_restore_line","camera_restore_record","camera_restore_section","camera_restore_offset","camera_restore_saved","camera_restore_next","camera_restore_loaded","camera_restore_live","deferred_animation_bytes","motion_zero_count","motion_zero_record","motion_zero_offset","motion_zero_expected_bits","motion_zero_actual_bits","postload_failure_check","reset54_p1_side","reset54_p1_saved","reset54_p1_loaded","reset54_p1_live","reset54_p1_reason","reset54_p1_restored","reset54_p2_side","reset54_p2_saved","reset54_p2_loaded","reset54_p2_live","reset54_p2_reason","reset54_p2_restored","selector_enabled","selector_phase","selector_reason","selector_side","selector_offset","selector_expected","selector_actual","selector_written","selector_restored","selector_endpoint_verified"};
    using Diagnostics=std::array<uint64_t,std::size(diagnostic_names)>;
    Diagnostics diagnostics(){
        auto& window=(stage==Stage::clean_rewind || stage==Stage::reference_replay)?clean:predicted_;
        auto& hop=window.diagnostic();const auto& p=*hop.probe;const auto& w=p.wind_diagnostic;
        Diagnostics result{(stage==Stage::clean_rewind || stage==Stage::reference_replay)?0u:1u,window.plan_result,window.prepare_stage,window.last_hop,
            static_cast<unsigned>(hop.rejection),hop.diagnostic_stage,p.restore_invoked,hop.verified,
            p.delayed_failure_line,p.exception,p.fault,hop.replay_stage,hop.replay_comparison_mask,
            w.failure_line,w.record,w.kind,w.size,w.offset,w.saved_word,w.next_word,window.topology.mask,
            window.source_frame,window.next_frame,
            p.source_guard_reason,p.source_guard_record,p.source_guard_length,p.source_guard_saved,p.source_guard_next,p.source_guard_writer,
            hop.completion.failure_line,hop.completion.section,hop.completion.record,hop.completion.kind,hop.completion.difference_offset,
            p.load_entry_rng,p.load_entry_rng_valid,p.original_next.rng,p.combat_preflight_pass_mask,
            p.blend_pair_diagnostic.reason,p.blend_pair_diagnostic.side,p.blend_pair_diagnostic.offset,p.blend_pair_diagnostic.changed,
            p.blend_pair_diagnostic.saved,p.blend_pair_diagnostic.next,p.blend_pair_diagnostic.saved_owner,p.blend_pair_diagnostic.next_owner,
            p.camera_readiness.reason,p.camera_readiness.record,p.camera_readiness.section,p.camera_readiness.offset,
            p.camera_readiness.side,p.camera_readiness.saved,p.camera_readiness.next,
            window.topology.index,window.topology.before_count,window.topology.after_count,
            window.topology.before[0],window.topology.before[1],window.topology.before[2],window.topology.before[3],window.topology.before[4],window.topology.before[5],
            window.topology.after[0],window.topology.after[1],window.topology.after[2],window.topology.after[3],window.topology.after[4],window.topology.after[5],
            hop.replay_measurement_read,hop.replay_rng_equal,hop.replay_players_equal,hop.replay_rng_expected,hop.replay_rng_actual,
            hop.replay_scalar_differences[0],hop.replay_first_offset[0],hop.replay_expected_byte[0],hop.replay_live_byte[0],
            hop.replay_scalar_differences[1],hop.replay_first_offset[1],hop.replay_expected_byte[1],hop.replay_live_byte[1],
            window.topology.removed_nodes,window.topology.added_nodes,
            window.topology.removed_node[0],window.topology.removed_node[1],window.topology.removed_node[2],window.topology.removed_node[3],
            window.topology.added_node[0],window.topology.added_node[1],window.topology.added_node[2],window.topology.added_node[3],p.combat_postload_pass_mask,p.combat_loaded_blend.reason,p.combat_loaded_blend.side,p.combat_loaded_blend.block,p.combat_loaded_blend.offset,p.combat_loaded_blend.saved,p.combat_loaded_blend.next,p.combat_loaded_blend.loaded,
            p.combat_loaded_blend.pointer_valid,p.combat_loaded_blend.pointer_saved,p.combat_loaded_blend.pointer_next,p.combat_loaded_blend.pointer_loaded,
            p.measurements[0].valid?CombatBlend::wide(p.measurements[0].fields[p.combat_loaded_blend.side].data()+128):0,
            p.measurements[1].valid?CombatBlend::wide(p.measurements[1].fields[p.combat_loaded_blend.side].data()+128):0,
            p.measurements[2].valid?CombatBlend::wide(p.measurements[2].fields[p.combat_loaded_blend.side].data()+128):0,
            window.topology.removed_details[0][0],window.topology.removed_details[0][1],window.topology.removed_details[0][2],window.topology.removed_details[0][3],window.topology.removed_details[0][4],window.topology.removed_details[0][5],window.topology.removed_details[0][6],window.topology.removed_details[0][7],window.topology.removed_details[0][8],
            window.topology.removed_details[1][0],window.topology.removed_details[1][1],window.topology.removed_details[1][2],window.topology.removed_details[1][3],window.topology.removed_details[1][4],window.topology.removed_details[1][5],window.topology.removed_details[1][6],window.topology.removed_details[1][7],window.topology.removed_details[1][8],
            window.topology.added_details[0][0],window.topology.added_details[0][1],window.topology.added_details[0][2],window.topology.added_details[0][3],window.topology.added_details[0][4],window.topology.added_details[0][5],window.topology.added_details[0][6],window.topology.added_details[0][7],window.topology.added_details[0][8],
            window.topology.added_details[1][0],window.topology.added_details[1][1],window.topology.added_details[1][2],window.topology.added_details[1][3],window.topology.added_details[1][4],window.topology.added_details[1][5],window.topology.added_details[1][6],window.topology.added_details[1][7],window.topology.added_details[1][8]};
        size_t payload_index=142;
        const auto& pd=hop.payload_diagnostic;
        for(auto value:{pd.structural,pd.records,pd.bytes,pd.retained,pd.overflow})result[payload_index++]=value;
        for(const auto& row:pd.details)for(auto value:row)result[payload_index++]=value;
        if(hop.replay_checked){
            const auto rd=p.rings[2].first_difference(p.replay_rings);
            const auto& nd=hop.replay_native_difference;
            const uint64_t values[]={rd.status,rd.validity,rd.group,rd.slot,rd.offset,rd.field,rd.expected,rd.actual,rd.expected_word,rd.actual_word,
                nd.reason,nd.record,nd.offset,nd.length,nd.writer,nd.source,nd.expected,nd.actual,nd.word_offset,nd.expected_word,nd.actual_word,nd.changed_bytes};
            for(auto value:values)result[payload_index++]=value;
        }
        const auto& camera=p.camera_diagnostic;
        const uint64_t camera_values[]={camera.failure_line,camera.record,camera.section,camera.offset,camera.a,camera.b,camera.c,camera.live};
        std::copy(std::begin(camera_values),std::end(camera_values),result.begin()+313);
        result[321]=hop.payload_diagnostic.deferred_bytes;
        std::copy(p.motion_zero_evidence.begin(),p.motion_zero_evidence.end(),result.begin()+322);
        if constexpr(requires {clean.motion_zero_summary;}){
            if(clean.motion_zero_summary[0])std::copy(clean.motion_zero_summary.begin(),clean.motion_zero_summary.end(),result.begin()+322);
        }
        const auto& sr=p.scalar_resets[0];const auto& sr2=p.scalar_resets[1];
        const uint64_t reset_values[]={p.postload_failure_check,sr.side,sr.saved,sr.loaded,sr.live,sr.reason,sr.restored,sr2.side,sr2.saved,sr2.loaded,sr2.live,sr2.reason,sr2.restored};
        std::copy(std::begin(reset_values),std::end(reset_values),result.begin()+327);
        const auto& sd=p.selector_diagnostic;
        const uint64_t selector_values[]={p.selector_enabled,sd.phase,sd.reason,sd.side,sd.offset,sd.expected,sd.actual,sd.written,p.selector_restored,p.selector_endpoint_verified};
        std::copy(std::begin(selector_values),std::end(selector_values),result.begin()+340);
        return result;
    }
    void capture_failure_evidence(ReplayFailureEvidence& evidence){
        auto& window=(stage==Stage::clean_rewind || stage==Stage::reference_replay)?clean:predicted_;
        const void* wind=nullptr;size_t wind_size=0;const void* emitters=nullptr;size_t emitter_size=0;
        const void* births=nullptr;size_t birth_size=0;
        if constexpr(requires {window.effect_session_;}){
            if(window.effect_session_){wind=&window.effect_session_->evidence;wind_size=sizeof(window.effect_session_->evidence);emitters=&window.effect_session_->emitter_evidence;emitter_size=sizeof(window.effect_session_->emitter_evidence);}
            if(window.effect_session_){births=&window.effect_session_->birth_evidence();birth_size=sizeof(window.effect_session_->birth_evidence());}
        }
        evidence.capture(*window.diagnostic().probe,evidence_inputs_.data(),sizeof(evidence_inputs_),clean.base_,wind,wind_size,emitters,emitter_size,births,birth_size);
    }
    bool capture_owned_recording(std::array<std::unique_ptr<ReplayFailureEvidence>,4>& output){
        if(clean.count_!=4 || !clean.dependencies_valid() || clean.rewind_started())return false;
        for(unsigned i=0;i<4;++i){
            auto e=std::make_unique<ReplayFailureEvidence>();
            if(!clean.pool_.visit(clean.handles_[i],[&](NativeBoundarySnapshot& snapshot){
                if(!snapshot.valid || snapshot.fault)return false;
                const uint64_t magic=0x3144564543364353ULL;e->append(&magic,8);
                const unsigned schema=1;e->section("owned_recording_schema",schema);
                e->section("boundary",clean.keys_[i]);e->section("module",snapshot.module);
                e->section("capture_valid",snapshot.valid);e->section("capture_stage",snapshot.capture_stage);e->section("exception",snapshot.exception);
                const auto& q=*snapshot.data;e->section("input",q.delayed_inputs);e->section("measurements",q.measurements);e->section("keys",q.before_keys);
                if(i<3){evidence_inputs_[0][i]=q.delayed_inputs;evidence_inputs_[1][i]=q.delayed_inputs;}
                e->section("native.saved.lengths",q.saved.lengths);e->section("native.saved.traces",q.saved.traces);
                e->section("native.saved.size",q.saved.stream.write_position);e->section("native.saved.payload",q.saved.stream.payload);
                e->section("ring.source",q.rings[0]);e->section("blend.source",q.blends[0]);
                e->section("camera.source",q.cameras[0]);e->section("transform.source",q.transforms[0]);
                const auto& guard=clean.dependency_guards_[i];
                e->section("dependency.root",guard.root);e->section("dependency.control",guard.control);e->section("dependency.descriptors",guard.descriptors);
                if(snapshot.effects){e->section("wind.emitters",snapshot.effects->emitters);e->section("effects.image",snapshot.effects->image);e->section("effects.binding_valid",snapshot.effects->valid);}
                // Archive is diagnostic only; ownership stays in the live pool.
                e->state.store(1,std::memory_order_release);return true;
            }))return false;
            output[i]=std::move(e);
        }
        return true;
    }
    const auto& blend_evidence(){return ((stage==Stage::clean_rewind || stage==Stage::reference_replay)?clean:predicted_).diagnostic().probe->blend_pair_evidence;}
    using Input=std::array<std::byte,40>;
    using Frames=std::array<CorrectedHistoryReplay::Frame,3>;
    std::array<std::array<Input,3>,3> evidence_inputs_{};
    enum class Stage {unused,confirmation,clean_rewind,prediction,capture,divergence,correction_rewind,replay,complete,reference_replay};
    Stage stage{Stage::unused};
    unsigned predicted_updates{},corrected_updates{},capture_detail{};
    bool reference_attempted{},reference_verified{};
    bool attempted{},injected{},state_changed{},verified{},restart_required{},retired{};
    Window clean;
    bool attack_fixture{},combat_inputs{};
private:
    Window predicted_;
    RecordedInput delivery_,guard_;
public:
    // A reference comparison needs independently recorded truth. A peer pair
    // which disagrees with the clean reference cannot be used as its oracle.
    static bool inputs(const Frames& frames,const std::array<RollbackBoundaryKey,4>& keys,
                       const std::array<Input,3>& reference,std::array<Input,3>& actual,std::array<Input,3>& wrong,bool attack=false,bool combat=false){
        actual={};wrong={};std::array<Input,3> staged{},fixtures{};bool prediction_differs=false;
        for(unsigned i=0;i<3;++i){const auto& f=frames[i];
            if(!f.confirmed || !keys[i].same_round(keys[3]) || keys[i].frame>=keys[3].frame ||
               keys[3].frame-keys[i].frame!=3-i || !f.key.same_round(keys[i]) || f.key.frame!=keys[i].frame)return false;
            const auto composed=NetworkInputComposer::compose(f.local,f.peer,3-keys[i].slot,f.key.frame,f.key.frame);
            if(!composed)return false;
            std::memcpy(staged[i].data(),composed->data(),40);
            if(staged[i]!=reference[i] || !RecordedInput::valid_envelope(staged[i]))return false;
            // Confirmed truth is neutral. Explicit attack mode pulses measured A
            // for one frame; the default retains the screen-right fixture.
            if(!combat)for(unsigned j=0;j<24;++j)if(staged[i][j]!=std::byte{})return false;
            fixtures[i]=staged[i];const unsigned remote=3-keys[i].slot,bits=combat?0u:attack?(i==0?1u:0u):0x400u;
            for(unsigned offset:{8u*(remote-1),8u*(remote-1)+4u,16u+4u*(remote-1)})
                std::memcpy(fixtures[i].data()+offset,&bits,4);
            prediction_differs=prediction_differs || fixtures[i]!=staged[i];
        }
        if(combat && !prediction_differs)return false;
        actual=staged;wrong=fixtures;return true;
    }
    bool release() noexcept {
        if(retired)return true;
        // Each window owns independent references. An exception in one drain
        // must not prevent the other drain, or permit this backend's reuse.
        bool a=false,b=false;
        try {a=predicted_.release_owned_dependencies();}catch(...){ }
        try {b=clean.release_owned_dependencies();}catch(...){ }
        retired=a&&b;return retired;
    }
    bool run(const NetworkGameHandoff& handoff,const RollbackBoundaryKey& live,uintptr_t base,
             Source& source,SingleUpdateCall::Routine original){
        Frames frames{};
        if(clean.count_==4)confirmed_replay_window(handoff,clean.keys_[0],live,frames);
        return run_confirmed(frames,live,base,source,original);
    }
    // Alternative caller is the startup suite's authenticated three-record
    // exchange. It copies immutable confirmations at the held owner boundary.
    bool run_confirmed(const Frames& frames,const RollbackBoundaryKey& live,uintptr_t base,
             Source& source,SingleUpdateCall::Routine original){
        if(attempted)return false;attempted=true;stage=Stage::confirmation;
        if(clean.count_!=4 || clean.fault_ || clean.rewind_started() || !base || base!=clean.base_ ||
           !original || reinterpret_cast<uintptr_t>(original)==base+0x2DBC60 ||
           live.thread!=GetCurrentThreadId() || !live.same_round(clean.keys_[3]) ||
           live.frame!=clean.keys_[3].frame || !source.available() || !clean.dependencies_valid())return false;
        std::array<Input,3> reference{},actual{},wrong{};
        for(unsigned i=0;i<3;++i)if(!clean.pool_.visit(clean.handles_[i],[&](NativeBoundarySnapshot& s){reference[i]=s.data->delayed_inputs;return true;}))return false;
        if(!inputs(frames,clean.keys_,reference,actual,wrong,attack_fixture,combat_inputs))return false;
        evidence_inputs_={reference,actual,wrong};
        if constexpr(requires {clean.defer_derived;})clean.defer_derived=predicted_.defer_derived=combat_inputs;
        try {
            stage=Stage::clean_rewind;
            if(!clean.rewind(live)){restart_required=clean.rewind_started() || clean.any_restore_invoked();return false;}
            restart_required=true;
            if(combat_inputs){
                stage=Stage::reference_replay;reference_attempted=true;
                if(!clean.replay_reference(original))return false;
                reference_verified=true;
                if(!clean.prepare_prediction_after_reference())return false;
                stage=Stage::clean_rewind;
                if(!clean.rewind(live))return false;
            }
            // Preserve owned references for the whole predicted branch; use
            // the same capture entry for its initial and advanced snapshots.
            for(unsigned i=0;i<4;++i){
                stage=Stage::capture;Input envelope{};
                if(!clean.pool_.visit(clean.handles_[i],[&](NativeBoundarySnapshot& s){envelope=s.data->delayed_inputs;return true;}))return false;
                const auto mode=(attack_fixture || combat_inputs)?NativeBoundarySnapshot::Mode::general_combat:
                    (i?NativeBoundarySnapshot::Mode::direction_observation:NativeBoundarySnapshot::Mode::idle);
                if(!predicted_.capture(clean.keys_[i],base,guard_.assign(envelope),mode,true,&source)){
                    capture_detail=predicted_.capture_stage;return false;
                }
                if(i==3)break;
                stage=Stage::prediction;rng_trace::Scope rng_phase(3);Update call;
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
                    if(ref.blend_timer_count)q.blend_timer_expected=ref.blend_timer_history[0];
                    std::memcpy(q.after_players,ref.before_players,sizeof(q.after_players));
                    q.selectors[1]=ref.selectors[0];q.original_next=ref.measurements[0];q.rings[2]=ref.rings[0];q.blends[1]=ref.blends[0];q.transforms[1]=ref.transforms[0];q.cameras[1]=ref.cameras[0];
                    rng_trace::Scope rng_phase(4);Update call;
                    if(!call.run(original,delivery_.assign(actual[i]),reinterpret_cast<volatile uint32_t*>(base+0x470D0C4),clean.keys_[i].frame,live.thread))return false;
                    hop.replay_delivered=true;
                    const bool matched=hop.replay_step(a,base,guard_.assign(combat_inputs?actual[i]:ref.delayed_inputs),true);
                    if constexpr(requires {Window::verify_effects(b);})return matched && Window::verify_effects(b);
                    else return matched;
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
using OwnedPredictionCorrection=OwnedPredictionCorrectionBackend<>;
}

