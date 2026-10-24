#pragma once
#include "NativeHistoryRestore.hpp"
#include "RetiredWorkspaceCache.hpp"
#include "NetworkInputComposer.hpp"
#include "NativeRollbackTiming.hpp"
namespace sc6::research {
// Native adapter storage. Requires a live boundary envelope before reconcile.
// Continuous captures use the explicit general-state restore contract.
template<class History=NativeRollingHistory<4>,class Restore=NativeHistoryRestore<>> class NativeSchedulerStorage {
    History& history_;ReplayDependencySource& source_;uintptr_t base_;
    RollbackBoundaryKey identity_;
    RecordedInput input_;
    unsigned live_{};bool boundary_{},failed_{},approved_{};
    std::array<std::byte,40> endpoint_{};
    uint64_t serial_{};
    unsigned startup_captures_{};
    struct Token {unsigned frame{};uint64_t serial{};};
    std::array<Token,4> tokens_{};
    // Cache only detached allocations after verified clean match retirement.
    inline static thread_local RetiredWorkspaceCache<Restore> retired_cache_;
    bool workspace_reused_{};
    std::unique_ptr<Restore> restore_;
    RollbackBoundaryKey key(unsigned frame)const{auto k=identity_;k.frame=frame;return k;}
    bool fail(){failed_=true;return false;}
    bool capture_boundary(const RollbackBoundaryKey& boundary,unsigned oldest,uintptr_t argument){
        NativeRollbackTiming::Binding binding(&timing,boundary.frame);
        NativeRollbackTiming::Scope measured(NativeRollbackTiming::capture);
        const auto capture_now=[&]{
            if constexpr(requires {history_.capture_general_owned(boundary,oldest,base_,argument,source_);})
                return history_.capture_general_owned(boundary,oldest,base_,argument,source_);
            else return history_.capture_owned(boundary,oldest,base_,argument,source_,approved_);
        };
        if(startup_captures_<4){
            NativeStartupScope startup_measure(startup_captures_++?NativeStartupTiming::initial_ring_capture:NativeStartupTiming::first_capture,identity_.round);
            return capture_now();
        }
        return capture_now();
    }
public:
    static bool prepare_workspace(uintptr_t base){return retired_cache_.prepare(base);}
    NativeRollbackTiming timing;
    NativeRollbackTiming* timing_sink(){return &timing;}
    NativeRestoreDiagnostic restore_diagnostic;
    std::array<uint64_t,4> restore_attempt_diagnostic{};
    struct Snapshot {RollbackBoundaryKey key{};uint64_t serial{};};
    NativeSchedulerStorage(History& history,ReplayDependencySource& source,uintptr_t base,const RollbackBoundaryKey& identity,bool approved=false,std::unique_ptr<Restore> recycled={})
        :history_(history),source_(source),base_(base),identity_(identity),approved_(approved),workspace_reused_(bool(recycled)),restore_(std::move(recycled)){
        if(!restore_ && identity.thread==GetCurrentThreadId())restore_=retired_cache_.take(base);
        workspace_reused_=bool(restore_);
        if(!restore_)restore_=std::make_unique<Restore>();
    }
    std::unique_ptr<Restore> take_retired_workspace(bool retired){
        if(!retired || failed_ || !restore_ || identity_.thread!=GetCurrentThreadId())return {};
        if constexpr(requires {restore_->prepare_for_reuse();}){
            if(!restore_->prepare_for_reuse())return {};
            return std::move(restore_);
        }else return {};
    }
    bool discard_retired_workspace(bool retain_allocation=false){
        if(failed_ || identity_.thread!=GetCurrentThreadId())return false;
        if(!restore_)return true;
        if constexpr(requires {restore_->prepare_for_reuse();}){
            if(!restore_->prepare_for_reuse())return false;
            if(retain_allocation)return retired_cache_.put(restore_,base_,true);
            restore_.reset();return true;
        }else return false;
    }
    bool begin_boundary(unsigned frame,const NetworkInputComposer::Envelope& local){
        if(!restore_ || failed_ || !base_ || !identity_.same_round(identity_) || identity_.thread!=GetCurrentThreadId())return fail();
        std::memcpy(endpoint_.data(),local.data(),40);
        if(!RecordedInput::valid_envelope(endpoint_))return fail();
        live_=frame;boundary_=true;return true;
    }
    bool capture(Snapshot& out,unsigned frame,const std::array<std::byte,40>& bytes){
        if(!restore_ || failed_ || !boundary_ || frame>live_ || live_-frame>3 || !RecordedInput::valid_envelope(bytes) || serial_==UINT64_MAX)return fail();
        const auto boundary=key(frame);
        if(!capture_boundary(boundary,frame>3?frame-3:0,input_.assign(bytes)))return fail();
        out={boundary,++serial_};tokens_[frame%4]={frame,serial_};return true;
    }
    bool restore(const Snapshot& saved){
        NativeRollbackTiming::Binding binding(&timing,saved.key.frame);
        NativeRollbackTiming::Scope measured(NativeRollbackTiming::restore);
        restore_attempt_diagnostic={1,saved.key.frame,live_,saved.serial};
        const auto& token=tokens_[saved.key.frame%4];
        if(!restore_ || failed_ || !boundary_ || !saved.serial || !saved.key.same_round(identity_) || token.frame!=saved.key.frame ||
            token.serial!=saved.serial || saved.key.frame>=live_ || live_-saved.key.frame>3)return fail();
        // The scheduler stores pre-frame states; add the current endpoint so
        // every adjacent reverse-load hop has its required reference state.
        restore_attempt_diagnostic[0]=2;
        if(!capture_boundary(key(live_),saved.key.frame,input_.assign(endpoint_)))return fail();
        restore_attempt_diagnostic[0]=3;
        {NativeRollbackTiming::Scope reset(NativeRollbackTiming::workspace);
        if constexpr(requires {restore_->prepare_for_reuse();}){
            if(!restore_->prepare_for_reuse())return fail();
        }else restore_=std::make_unique<Restore>();}
        restore_attempt_diagnostic[0]=4;
        const bool restored=restore_->run(history_,saved.key,key(live_),base_);
        if constexpr(requires {restore_->details();})restore_diagnostic=restore_->details();
        if(!restored)return fail();
        restore_attempt_diagnostic[0]=5;
        {NativeRollbackTiming::Scope cleanup(NativeRollbackTiming::cleanup);
        if(!history_.rewind_for_replay(saved.key,[](auto&){return true;}))return fail();}
        for(auto& t:tokens_)if(t.serial && t.frame>=saved.key.frame)t={};
        restore_attempt_diagnostic[0]=0;return true;
    }
    unsigned frame(const Snapshot& saved)const{return saved.key.frame;}
    bool workspace_reused()const{return workspace_reused_;}
    bool failed()const{return failed_;}
};
}
