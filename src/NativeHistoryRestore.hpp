#pragma once
#include "NativeRollingHistory.hpp"
#include "NativeRestoreDiagnostic.hpp"
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
    NativeHistoryRestore(){for(auto& w:work_)w=std::make_unique<Workspace>();}
    template<class History> bool run(History& history,const RollbackBoundaryKey& target,const RollbackBoundaryKey& live,uintptr_t base){
        if(attempted)return false;
        attempted=true;stage=Stage::identity;
        if(!base || target.thread!=GetCurrentThreadId() || !target.same_round(live) || live.frame<=target.frame || live.frame-target.frame>3)return false;
        depth=live.frame-target.frame;
        std::array<RollbackBoundaryKey,4> keys{};
        for(unsigned i=0;i<=depth;++i){keys[i]=target;keys[i].frame+=i;}
        stage=Stage::ownership;
        for(unsigned i=0;i<=depth;++i)if(!history.validate_owned(keys[i]))return false;
        stage=Stage::prepare;
        for(hop=0;hop<depth;++hop){
            if(!history.visit(keys[hop],[&](NativeBoundarySnapshot& a){return history.visit(keys[hop+1],[&](NativeBoundarySnapshot& b){
                if(a.module!=base || b.module!=base)return false;
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
            for(unsigned j=0;j<=depth;++j)if(!history.validate_owned(keys[j]))return false;
            if(!history.visit(keys[i],[&](NativeBoundarySnapshot& a){return history.visit(keys[i+1],[&](NativeBoundarySnapshot& b){
                source_mode=static_cast<unsigned>(a.mode);next_mode=static_cast<unsigned>(b.mode);
                if(a.mode==NativeBoundarySnapshot::Mode::general_combat){
                    stage=Stage::prepare;
                    if(!work_[i]->prepare_captured(a,b,false,false,true))return false;
                    stage=Stage::load;
                }
                return work_[i]->restore_captured(a,base,input_.assign(b.data->delayed_inputs));
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
        if constexpr(requires {selected.rejection;selected.diagnostic_stage;selected.probe;}){
            out.values[5]=static_cast<unsigned>(selected.rejection);out.values[6]=selected.diagnostic_stage;
            if(selected.probe){out.values[7]=selected.probe->exception;out.values[8]=selected.probe->delayed_failure_line;}
            if(selected.probe)out.values[NativeRestoreDiagnostic::preflight_mask]=selected.probe->combat_preflight_pass_mask;
            if(selected.probe)out.values[NativeRestoreDiagnostic::source_guard_reason]=selected.probe->source_guard_reason;
            if(selected.probe)out.values[NativeRestoreDiagnostic::source_guard_record]=selected.probe->source_guard_record;
            if(selected.probe)out.values[NativeRestoreDiagnostic::source_guard_length]=selected.probe->source_guard_length;
            if(selected.probe)out.values[NativeRestoreDiagnostic::source_guard_saved]=selected.probe->source_guard_saved;
            if(selected.probe)out.values[NativeRestoreDiagnostic::source_guard_next]=selected.probe->source_guard_next;
            if(selected.probe)out.values[NativeRestoreDiagnostic::source_guard_writer]=selected.probe->source_guard_writer;
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
