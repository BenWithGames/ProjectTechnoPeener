#pragma once
#include "NativeRollingHistory.hpp"
#include "NativeRestoreDiagnostic.hpp"
#include "NativeRollbackTiming.hpp"
#include "NativeStartupTiming.hpp"
namespace sc6::research {
// One restore attempt, using the existing verified adjacent loader. Unsupported
// combat modes still reject during preparation. Caller must replay to the head
// before returning to normal simulation; success here is not replay success.
template<class Workspace=NativeAdjacentRestore> class NativeHistoryRestore {
    std::array<std::unique_ptr<Workspace>,3> work_;
    RecordedInput input_;
public:
    enum class Stage {unused,identity,ownership,prepare,load,restored};
    Stage stage{Stage::unused};unsigned hop{},depth{};bool attempted{},loaded{};
    bool direct_attempted{},direct_used{};unsigned direct_rejection{},direct_guard_reason{},direct_guard_offset{},native_load_attempts{};
    unsigned source_mode{},next_mode{};
    NativeHistoryRestore(){
        NativeStartupScope measured(NativeStartupTiming::workspace_construct);
        if constexpr(std::is_same_v<Workspace,NativeAdjacentRestore>)(void)FullStateProbe::reset_defaults();
        for(auto& w:work_)w=std::make_unique<Workspace>();
    }
    bool prepare_for_reuse(){
        if(!attempted)return true;
        if(!loaded || stage!=Stage::restored || !depth || depth>3)return false;
        if constexpr(requires(Workspace& w){w.detached_load_reusable();w.recycle_detached_load();}){
            for(unsigned i=direct_used?depth-1:0;i<depth;++i)if(!work_[i]->detached_load_reusable())return false;
            for(unsigned i=direct_used?depth-1:0;i<depth;++i)if(!work_[i]->recycle_detached_load())return false;
        }else return false;
        input_.assign({});stage=Stage::unused;hop=depth=source_mode=next_mode=0;
        attempted=loaded=direct_attempted=direct_used=false;direct_rejection=direct_guard_reason=direct_guard_offset=native_load_attempts=0;return true;
    }
    template<class History> bool run(History& history,const RollbackBoundaryKey& target,const RollbackBoundaryKey& live,uintptr_t base){
        if(attempted)return false;
        attempted=true;stage=Stage::identity;
        if(!base || target.thread!=GetCurrentThreadId() || !target.same_round(live) || live.frame<=target.frame || live.frame-target.frame>3)return false;
        depth=live.frame-target.frame;
        std::array<RollbackBoundaryKey,4> keys{};
        for(unsigned i=0;i<=depth;++i){keys[i]=target;keys[i].frame+=i;}
        stage=Stage::ownership;
        {NativeRollbackTiming::Scope measured(NativeRollbackTiming::verification);
        NativeRollbackTiming::Scope ownership_timing(NativeRollbackTiming::restore_ownership);
        for(unsigned i=0;i<=depth;++i)if(!history.validate_owned(keys[i]))return false;}
        stage=Stage::prepare;
        for(hop=0;hop<depth;++hop){
            NativeRollbackTiming::Scope measured(NativeRollbackTiming::prepare);
            if(!history.visit(keys[hop],[&](NativeBoundarySnapshot& a){return history.visit(keys[hop+1],[&](NativeBoundarySnapshot& b){
                if(a.module!=base || b.module!=base)return false;
                if(bool(a.effects)!=bool(b.effects))return false;
                if(a.effects && (!a.effects->owner || a.effects->owner!=b.effects->owner ||
                    !a.effects->owner->validate(*a.effects) || !a.effects->owner->validate(*b.effects)))return false;
                source_mode=static_cast<unsigned>(a.mode);next_mode=static_cast<unsigned>(b.mode);
                if(a.mode==NativeBoundarySnapshot::Mode::general_combat || b.mode==NativeBoundarySnapshot::Mode::general_combat)
                    return NativeRollbackTiming::measure(NativeRollbackTiming::restore_all_hop_checks,[&]{return work_[hop]->preflight_general(a,b);});
                return work_[hop]->prepare_captured(a,b,b.mode==NativeBoundarySnapshot::Mode::direction_observation,
                    b.mode==NativeBoundarySnapshot::Mode::a_observation,NativeAdjacentRestore::combat_mode(a.mode));
            });}))return false;
        }
        // All adjacent structural/ownership guards above remain mandatory.
        // A rejected direct candidate is discarded before any game writes.
        if constexpr(requires(Workspace& w,const NativeBoundarySnapshot& a,const NativeBoundarySnapshot& b){w.prepare_direct(a,b,depth);w.discard_unloaded_candidate();w.direct_diagnostic.reason;w.rejection;}){
            if(depth>1 && source_mode==static_cast<unsigned>(NativeBoundarySnapshot::Mode::general_combat)){
                hop=depth-1;direct_attempted=true;auto& w=*work_[hop];
                bool admitted=history.visit(keys[0],[&](NativeBoundarySnapshot& a){return history.visit(keys[depth],[&](NativeBoundarySnapshot& b){
                    return w.prepare_direct(a,b,depth);
                });});
                if(!admitted){
                    direct_rejection=static_cast<unsigned>(w.rejection);direct_guard_reason=static_cast<unsigned>(w.direct_diagnostic.reason);
                    direct_guard_offset=static_cast<unsigned>(w.direct_diagnostic.offset);
                    if(!w.discard_unloaded_candidate())return false;
                }else{
                    direct_used=true;stage=Stage::load;
                    NativeRollbackTiming::Scope direct_timing(NativeRollbackTiming::restore_direct);
                    for(unsigned j=0;j<=depth;++j)if(!history.validate_owned(keys[j]))return false;
                    const bool ok=history.visit(keys[0],[&](NativeBoundarySnapshot& a){return history.visit(keys[depth],[&](NativeBoundarySnapshot& b){
                        if(bool(a.effects)!=bool(b.effects))return false;
                        if(a.effects && (!a.effects->owner || a.effects->owner!=b.effects->owner ||
                           !a.effects->owner->validate(*a.effects) || !a.effects->owner->validate(*b.effects)))return false;
                        if constexpr(requires {w.probe->defer_inactive_animation_weights;})w.probe->defer_inactive_animation_weights=true;
                        if(a.data->selector_enabled){if constexpr(requires {w.probe->selector_diagnostic;})
                            if(!a.data->selectors[0].dependencies(w.probe->selector_diagnostic))return false;}
                        if(a.effects && !NativeRollbackTiming::measure(NativeRollbackTiming::restore_retained_objects,[&]{return a.effects->owner->restore(*a.effects);}))return false;
                        ++native_load_attempts;
                        if(!w.restore_captured(a,base,input_.assign(b.data->delayed_inputs)))return false;
                        return !a.effects || NativeRollbackTiming::measure(NativeRollbackTiming::restore_retained_compare,[&]{return a.effects->owner->compare(*a.effects);});
                    });});
                    if(!ok)return false; // Never retry after a native load attempt.
                    loaded=true;stage=Stage::restored;return true;
                }
            }
        }
        stage=Stage::load;
        for(unsigned i=depth;i-- >0;){hop=i;
            // Recheck every retained dependency before each native mutation.
            {NativeRollbackTiming::Scope measured(NativeRollbackTiming::verification);
        NativeRollbackTiming::Scope ownership_timing(NativeRollbackTiming::restore_ownership);
            for(unsigned j=0;j<=depth;++j)if(!history.validate_owned(keys[j]))return false;}
            if(!history.visit(keys[i],[&](NativeBoundarySnapshot& a){return history.visit(keys[i+1],[&](NativeBoundarySnapshot& b){
                if(bool(a.effects)!=bool(b.effects))return false;
                if(a.effects && (!a.effects->owner || a.effects->owner!=b.effects->owner ||
                    !a.effects->owner->validate(*a.effects) || !a.effects->owner->validate(*b.effects)))return false;
                source_mode=static_cast<unsigned>(a.mode);next_mode=static_cast<unsigned>(b.mode);
                if(a.mode==NativeBoundarySnapshot::Mode::general_combat){
                    NativeRollbackTiming::Scope measured(NativeRollbackTiming::prepare);
                    stage=Stage::prepare;
                    if(!work_[i]->prepare_captured(a,b,false,false,true))return false;
                    stage=Stage::load;
                }
                if constexpr(requires {work_[i]->probe->defer_inactive_animation_weights;})
                    work_[i]->probe->defer_inactive_animation_weights=true;
                if(a.data->selector_enabled){
                    if constexpr(requires {work_[i]->probe->selector_diagnostic;})
                        if(!a.data->selectors[0].dependencies(work_[i]->probe->selector_diagnostic))return false;
                }
                {NativeRollbackTiming::Scope measured(NativeRollbackTiming::load_fixups);
                if(a.effects && !NativeRollbackTiming::measure(NativeRollbackTiming::restore_retained_objects,[&]{return a.effects->owner->restore(*a.effects);}))return false;}
                ++native_load_attempts;
                if(!work_[i]->restore_captured(a,base,input_.assign(b.data->delayed_inputs)))return false;
                NativeRollbackTiming::Scope measured(NativeRollbackTiming::verification);
                return !a.effects || NativeRollbackTiming::measure(NativeRollbackTiming::restore_retained_compare,[&]{return a.effects->owner->compare(*a.effects);});
            });}))return false;
        }
        loaded=true;stage=Stage::restored;return true;
    }
    Workspace& diagnostic(){return *work_[(std::min)(hop,2u)];}
    NativeRestoreDiagnostic details()const{
        NativeRestoreDiagnostic out;
        out.values[NativeRestoreDiagnostic::direct_attempted]=direct_attempted;
        out.values[NativeRestoreDiagnostic::direct_used]=direct_used;
        out.values[NativeRestoreDiagnostic::direct_rejection]=direct_rejection;
        out.values[NativeRestoreDiagnostic::direct_guard_reason]=direct_guard_reason;
        out.values[NativeRestoreDiagnostic::direct_guard_offset]=direct_guard_offset;
        out.values[NativeRestoreDiagnostic::native_load_attempts]=native_load_attempts;
        out.values[0]=static_cast<unsigned>(stage);out.values[1]=hop;out.values[2]=depth;
        out.values[3]=source_mode;out.values[4]=next_mode;
        const auto& selected=*work_[(std::min)(hop,2u)];
        if constexpr(requires {selected.slot_evidence;})out.camera_slots=selected.slot_evidence;
        if constexpr(requires {selected.shape_module;}){out.values[NativeRestoreDiagnostic::shape_module]=selected.shape_module;out.values[NativeRestoreDiagnostic::shape_source_frame]=selected.shape_source_frame;out.values[NativeRestoreDiagnostic::shape_next_frame]=selected.shape_next_frame;}
        if constexpr(requires {selected.shape_diagnostic;})
            for(unsigned i=0;i<selected.shape_diagnostic.values.size();++i)out.values[NativeRestoreDiagnostic::shape_reason+i]=selected.shape_diagnostic.values[i];
        if constexpr(requires {selected.topology_diagnostic;})out.topology=selected.topology_diagnostic;
        if constexpr(requires {selected.rejection;selected.diagnostic_stage;selected.probe;}){
            out.values[5]=static_cast<unsigned>(selected.rejection);out.values[6]=selected.diagnostic_stage;
            if(selected.probe){out.values[7]=selected.probe->exception;out.values[8]=selected.probe->delayed_failure_line;}
            if(selected.probe){const auto& route=selected.probe->load_route_diagnostic;
                out.values[NativeRestoreDiagnostic::route_reason]=route.reason;
                out.values[NativeRestoreDiagnostic::route_record]=route.record;
                out.values[NativeRestoreDiagnostic::route_expected]=route.expected;
                out.values[NativeRestoreDiagnostic::route_actual]=route.actual;
            }
            if(selected.probe){const auto& camera=selected.probe->camera_readiness;
                out.values[NativeRestoreDiagnostic::camera_reason]=camera.reason;
                out.values[NativeRestoreDiagnostic::camera_record]=camera.record;
                out.values[NativeRestoreDiagnostic::camera_section]=camera.section;
                out.values[NativeRestoreDiagnostic::camera_offset]=camera.offset;
                out.values[NativeRestoreDiagnostic::camera_side]=camera.side;
                out.values[NativeRestoreDiagnostic::camera_saved]=camera.saved;
                out.values[NativeRestoreDiagnostic::camera_next]=camera.next;
            }
            if(selected.probe){const auto& blend=selected.probe->blend_pair_diagnostic;
                out.values[NativeRestoreDiagnostic::blend_reason]=blend.reason;
                out.values[NativeRestoreDiagnostic::blend_side]=blend.side;
                out.values[NativeRestoreDiagnostic::blend_offset]=blend.offset;
                out.values[NativeRestoreDiagnostic::blend_changed]=blend.changed;
                out.values[NativeRestoreDiagnostic::blend_saved]=blend.saved;
                out.values[NativeRestoreDiagnostic::blend_next]=blend.next;
                out.values[NativeRestoreDiagnostic::blend_saved_owner]=blend.saved_owner;
                out.values[NativeRestoreDiagnostic::blend_next_owner]=blend.next_owner;
                out.values[NativeRestoreDiagnostic::blend_table_saved]=blend.table_saved;
                out.values[NativeRestoreDiagnostic::blend_table_next]=blend.table_next;
                out.values[NativeRestoreDiagnostic::blend_wrapper_saved]=blend.wrapper_saved;
                out.values[NativeRestoreDiagnostic::blend_wrapper_next]=blend.wrapper_next;
                out.values[NativeRestoreDiagnostic::blend_resource_saved]=blend.resource_saved;
                out.values[NativeRestoreDiagnostic::blend_resource_next]=blend.resource_next;
                out.values[NativeRestoreDiagnostic::blend_mask_saved]=blend.mask_saved;
                out.values[NativeRestoreDiagnostic::blend_mask_next]=blend.mask_next;
                out.values[NativeRestoreDiagnostic::blend_index_saved]=blend.index_saved;
                out.values[NativeRestoreDiagnostic::blend_index_next]=blend.index_next;

            }
            if(selected.probe){
                out.values[NativeRestoreDiagnostic::intermediate_refreshes]=selected.probe->intermediate_refreshes;
                out.values[NativeRestoreDiagnostic::intermediate_fallbacks]=selected.probe->intermediate_fallbacks;
                out.values[NativeRestoreDiagnostic::intermediate_last_rejection]=selected.probe->intermediate_last_rejection;
                const auto& group=selected.probe->active_group_diagnostic;
                out.values[NativeRestoreDiagnostic::animation_group_schema]=group.schema;
                out.values[NativeRestoreDiagnostic::animation_group_reason]=group.reason;
                out.values[NativeRestoreDiagnostic::animation_group_side]=group.side;
                out.values[NativeRestoreDiagnostic::animation_group_index]=group.group;
                out.values[NativeRestoreDiagnostic::animation_group_restored]=group.restored;
                if(group.side<2){
                    const auto& saved=group.group==1?group.secondary_saved[group.side]:group.saved[group.side];
                    const auto& loaded_group=group.group==1?group.secondary_loaded[group.side]:group.loaded[group.side];
                    const auto& verified=group.group==1?group.secondary_verified[group.side]:group.verified[group.side];
                    out.values[NativeRestoreDiagnostic::animation_group_saved_mask]=ActiveAnimationGroup::value<uint64_t>(saved.data());
                    out.values[NativeRestoreDiagnostic::animation_group_loaded_mask]=ActiveAnimationGroup::value<uint64_t>(loaded_group.data());
                    out.values[NativeRestoreDiagnostic::animation_group_verified_mask]=ActiveAnimationGroup::value<uint64_t>(verified.data());
                    out.values[NativeRestoreDiagnostic::animation_group_saved_weight]=ActiveAnimationGroup::value<uint32_t>(saved.data()+8);
                    out.values[NativeRestoreDiagnostic::animation_group_loaded_weight]=ActiveAnimationGroup::value<uint32_t>(loaded_group.data()+8);
                    out.values[NativeRestoreDiagnostic::animation_group_verified_weight]=ActiveAnimationGroup::value<uint32_t>(verified.data()+8);
                }
            }
            if(selected.probe)out.values[NativeRestoreDiagnostic::preflight_mask]=selected.probe->combat_preflight_pass_mask;
            if(selected.probe)out.values[NativeRestoreDiagnostic::source_guard_reason]=selected.probe->source_guard_reason;
            if(selected.probe)out.values[NativeRestoreDiagnostic::source_guard_record]=selected.probe->source_guard_record;
            if(selected.probe)out.values[NativeRestoreDiagnostic::source_guard_length]=selected.probe->source_guard_length;
            if(selected.probe)out.values[NativeRestoreDiagnostic::source_guard_saved]=selected.probe->source_guard_saved;
            if(selected.probe)out.values[NativeRestoreDiagnostic::source_guard_next]=selected.probe->source_guard_next;
            if(selected.probe)out.values[NativeRestoreDiagnostic::source_guard_writer]=selected.probe->source_guard_writer;
            if(selected.probe){
                const auto& p=*selected.probe;const auto& d=p.timer_diagnostic;
                out.values[NativeRestoreDiagnostic::timer_phase]=d.phase;
                out.values[NativeRestoreDiagnostic::timer_reason]=d.reason;
                out.values[NativeRestoreDiagnostic::timer_side]=d.side;
                out.values[NativeRestoreDiagnostic::timer_slot]=d.slot;
                out.values[NativeRestoreDiagnostic::timer_offset]=d.offset;
                out.values[NativeRestoreDiagnostic::timer_expected]=d.expected;
                out.values[NativeRestoreDiagnostic::timer_actual]=d.actual;
                out.values[NativeRestoreDiagnostic::timer_source_mask]=p.blend_timer_source.valid_mask;
                out.values[NativeRestoreDiagnostic::timer_next_mask]=p.blend_timer_next.valid_mask;
                out.values[NativeRestoreDiagnostic::timer_live_mask]=p.blend_timer_live.valid_mask;
                if(d.reason){
                    out.timer_evidence_valid=true;
                    const FullStateProbe::BlendTimerSample* samples[]={&p.blend_timer_source,&p.blend_timer_next,&p.blend_timer_live};
                    for(unsigned sample=0;sample<3;++sample)for(unsigned side=0;side<2;++side)for(unsigned slot=0;slot<3;++slot)
                        std::memcpy(out.timer_words.data()+((sample*2+side)*3+slot)*32,samples[sample]->slots[side][slot].data(),128);
                }
            }
            if constexpr(requires {selected.payload_diagnostic;}){
                const auto& pd=selected.payload_diagnostic;
                out.values[NativeRestoreDiagnostic::payload_structural]=pd.structural;
                out.values[NativeRestoreDiagnostic::payload_records]=pd.records;
                out.values[NativeRestoreDiagnostic::payload_bytes]=pd.bytes;
                out.values[NativeRestoreDiagnostic::payload_retained]=pd.retained;
                out.values[NativeRestoreDiagnostic::payload_overflow]=pd.overflow;
                out.values[NativeRestoreDiagnostic::payload_deferred_bytes]=pd.deferred_bytes;
                out.payload_details=pd.details;
            }
            if constexpr(requires {selected.completion;}){
                out.values[NativeRestoreDiagnostic::completion_line]=selected.completion.failure_line;
                out.values[NativeRestoreDiagnostic::completion_section]=selected.completion.section;
                out.values[NativeRestoreDiagnostic::completion_record]=selected.completion.record;
                out.values[NativeRestoreDiagnostic::completion_kind]=selected.completion.kind;
                out.values[NativeRestoreDiagnostic::completion_offset]=selected.completion.difference_offset;
            }
            for(const auto& w:work_)if(w->probe && w->probe->restore_invoked)out.values[9]=1;
        }
        return out;
    }
};
}
