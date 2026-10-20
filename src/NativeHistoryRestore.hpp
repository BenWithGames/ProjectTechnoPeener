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
            for(unsigned i=0;i<depth;++i)if(!work_[i]->detached_load_reusable())return false;
            for(unsigned i=0;i<depth;++i)if(!work_[i]->recycle_detached_load())return false;
        }else return false;
        input_.assign({});stage=Stage::unused;hop=depth=source_mode=next_mode=0;
        attempted=loaded=false;return true;
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
                    return work_[hop]->preflight_general(a,b);
                return work_[hop]->prepare_captured(a,b,b.mode==NativeBoundarySnapshot::Mode::direction_observation,
                    b.mode==NativeBoundarySnapshot::Mode::a_observation,NativeAdjacentRestore::combat_mode(a.mode));
            });}))return false;
        }
        stage=Stage::load;
        for(unsigned i=depth;i-- >0;){hop=i;
            // Recheck every retained dependency before each native mutation.
            {NativeRollbackTiming::Scope measured(NativeRollbackTiming::verification);
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
                if(a.effects && !a.effects->owner->restore(*a.effects))return false;}
                if(!work_[i]->restore_captured(a,base,input_.assign(b.data->delayed_inputs)))return false;
                NativeRollbackTiming::Scope measured(NativeRollbackTiming::verification);
                return !a.effects || a.effects->owner->compare(*a.effects);
            });}))return false;
        }
        loaded=true;stage=Stage::restored;return true;
    }
    Workspace& diagnostic(){return *work_[(std::min)(hop,2u)];}
    NativeRestoreDiagnostic details()const{
        NativeRestoreDiagnostic out;
        out.values[0]=static_cast<unsigned>(stage);out.values[1]=hop;out.values[2]=depth;
        out.values[3]=source_mode;out.values[4]=next_mode;
        const auto& selected=*work_[(std::min)(hop,2u)];
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
