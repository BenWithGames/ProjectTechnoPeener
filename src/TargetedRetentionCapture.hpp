#pragma once
#include "StartupSnapshotCapture.hpp"
#include <memory>
namespace sc6::research {
// Effect payloads and their validated external references only. No native
// combat serializer, load routine, RNG hook or input prediction is needed.
class TargetedEffectHistory {
    std::array<NativeEffectSession::Binding,4> bindings_;
    std::array<RollbackBoundaryKey,4> keys_{};
    unsigned count_{};
public:
    bool capture_owned(const RollbackBoundaryKey& key,unsigned,uintptr_t base,uintptr_t,
                       ReplayDependencySource& source,bool){
        if(count_==4 || !source.available() || !source.effects ||
           key.thread!=GetCurrentThreadId())return false;
        auto& binding=bindings_[count_];keys_[count_++]=key;
        return source.effects->begin(base,key.session,key.round,source.effect_references) &&
               source.effects->capture(binding);
    }
    bool validate_owned(const RollbackBoundaryKey& key){
        for(unsigned i=0;i<count_;++i)if(keys_[i].same_round(key) && keys_[i].frame==key.frame){
            auto& b=bindings_[i];return b.owner && b.owner->validate(b);
        }
        return false;
    }
    bool release(){
        for(unsigned i=0;i<count_;++i){auto& b=bindings_[i];if(b.owner && !b.owner->release(b))return false;}
        return true;
    }
};
// Owner-thread only: bounded capture/retire windows, never restoration or injection.
template<class History=TargetedEffectHistory> class TargetedRetentionCapture {
    std::unique_ptr<StartupSnapshotCapture<History>> window_;
    RollbackBoundaryKey first_{};
    unsigned next_{};
public:
    static constexpr unsigned window_limit=300, next_window_frames=1, hold_frames=3;
    std::atomic<unsigned> windows{},captured{};
    std::atomic<bool> failed{},retired{},round_cancelled{};
    bool release(){
        if(retired.load())return true;
        if(window_ && !window_->release()){failed=true;return false;}
        window_.reset();retired=true;return true;
    }
    void step(const RollbackBoundaryKey& key,uintptr_t base,uintptr_t argument,ReplayDependencySource& source){
        if(retired.load() || failed.load())return;
        if(!key.same_round(key) || key.thread!=GetCurrentThreadId()){failed=true;release();return;}
        if(!first_.session){first_=key;next_=key.frame;}
        if(!first_.same_round(key)){round_cancelled=true;release();return;}
        if(!window_){
            if(key.frame<next_)return;
            window_=std::make_unique<StartupSnapshotCapture<History>>();
        }
        const auto before=window_->captured.load();
        const bool ok=window_->capture(key,base,argument,source);
        captured+=window_->captured.load()-before;
        if(!ok){failed=true;release();return;}
        if(window_->retired.load()){
            ++windows;window_.reset();
            if(windows.load()==window_limit || key.frame>UINT32_MAX-next_window_frames){release();return;}
            next_=key.frame+next_window_frames;
        }
    }
    template<class Stream> void report(Stream& out)const{
        out<<"suite_retention_coverage_revision=2\nsuite_retention_window_limit="<<window_limit
           <<"\nsuite_retention_hold_frames="<<hold_frames<<"\nsuite_retention_next_window_frames="<<next_window_frames
           <<"\nsuite_retention_windows="<<windows.load()<<"\nsuite_retention_snapshots="<<captured.load()
           <<"\nsuite_retention_capture_failed="<<failed.load()<<"\nsuite_retention_capture_retired="<<retired.load()
           <<"\nsuite_retention_round_cancelled="<<round_cancelled.load()<<'\n';
    }
};
}
