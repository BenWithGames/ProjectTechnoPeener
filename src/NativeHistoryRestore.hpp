#pragma once
#include "NativeRollingHistory.hpp"
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
                return work_[hop]->prepare_captured(a,b,b.mode==NativeBoundarySnapshot::Mode::direction_observation,b.mode==NativeBoundarySnapshot::Mode::a_observation);
            });}))return false;
        }
        stage=Stage::load;
        for(unsigned i=depth;i-- >0;){hop=i;
            // Recheck every retained dependency before each native mutation.
            for(unsigned j=0;j<=depth;++j)if(!history.validate_owned(keys[j]))return false;
            if(!history.visit(keys[i],[&](NativeBoundarySnapshot& a){return history.visit(keys[i+1],[&](NativeBoundarySnapshot& b){
                return work_[i]->restore_captured(a,base,input_.assign(b.data->delayed_inputs));
            });}))return false;
        }
        loaded=true;stage=Stage::restored;return true;
    }
    Workspace& diagnostic(){return *work_[(std::min)(hop,2u)];}
};
}
