#pragma once
#include "CorrectedHistoryReplay.hpp"
#include "ConfirmedReplayWindow.hpp"
namespace sc6::research {
// Caller owns the game update boundary and keeps the dependency source alive
// throughout this transaction. Construction alone does not install any hook.
class NativeCorrectedReplayBackend {
    uintptr_t base_;
    SingleUpdateCall::Routine original_;
    ReplayDependencySource& source_;
    RollbackBoundaryKey target_{},head_{};
    std::array<std::byte,40> endpoint_{};
    RecordedInput delivery_;
    unsigned expected_{};
    bool approved_profile_{},bound_{},captured_{},failed_{},finished_{};
    bool fail(){failed_=true;return false;}
    bool counter_is(unsigned frame) noexcept {
        __try {return *reinterpret_cast<volatile uint32_t*>(base_+0x470D0C4)==frame;}
        __except(EXCEPTION_EXECUTE_HANDLER){exception=GetExceptionCode();return false;}
    }
    bool identity(const RollbackBoundaryKey& key)const {
        return bound_ && !failed_ && !finished_ && key.same_round(target_) && key.thread==GetCurrentThreadId() && key.frame==expected_;
    }
public:
    unsigned exception{},updates{};
    NativeCorrectedReplayBackend(uintptr_t base,SingleUpdateCall::Routine original,
        ReplayDependencySource& source,const std::array<std::byte,40>& endpoint,bool approved_profile=false)
        :base_(base),original_(original),source_(source),endpoint_(endpoint),approved_profile_(approved_profile){}
    bool failed()const{return failed_;}
    bool preflight(const RollbackBoundaryKey& target,const RollbackBoundaryKey& head){
        if(bound_ || failed_ || !base_ || !original_ || reinterpret_cast<uintptr_t>(original_)==base_+0x2DBC60 ||
            !target.same_round(head) || target.thread!=GetCurrentThreadId() || target.frame>=head.frame ||
            head.frame-target.frame>3 || !source_.available() || !RecordedInput::valid_envelope(endpoint_) || !counter_is(head.frame))return fail();
        target_=target;head_=head;expected_=target.frame;bound_=true;return true;
    }
    template<class History> bool capture(History& history,const RollbackBoundaryKey& key,const std::array<std::byte,40>& input){
        if(!identity(key) || captured_ || key.frame>=head_.frame || !RecordedInput::valid_envelope(input) || !counter_is(key.frame))return fail();
        if(!history.capture_owned(key,target_.frame,base_,delivery_.assign(input),source_,approved_profile_) ||
            !counter_is(key.frame) || !history.validate_owned(key))return fail();
        captured_=true;return true;
    }
    bool update(const RollbackBoundaryKey& key,const std::array<std::byte,40>& input){
        if(!identity(key) || !captured_ || key.frame>=head_.frame || input!=delivery_.bytes)return fail();
        SingleUpdateCall call;
        const bool ok=call.run(original_,delivery_.assign(input),reinterpret_cast<volatile uint32_t*>(base_+0x470D0C4),key.frame,key.thread);
        if(call.invoked)++updates;
        if(!ok){exception=call.exception;return fail();}
        ++expected_;captured_=false;return true;
    }
    template<class History> bool capture_endpoint(History& history,const RollbackBoundaryKey& key){
        if(!identity(key) || captured_ || key.frame!=head_.frame || !counter_is(key.frame))return fail();
        if(!history.capture_owned(key,target_.frame,base_,delivery_.assign(endpoint_),source_,approved_profile_) ||
            !counter_is(key.frame) || !history.validate_owned(key))return fail();
        finished_=true;return true;
    }
};
// Explicit native instantiation, still requiring an externally owned boundary.
// Resource teardown remains the owner's responsibility even on failure.
template<unsigned Capacity> bool replay_corrected_native(CorrectedHistoryReplay& transaction,
    NativeRollingHistory<Capacity>& history,NativeHistoryRestore<>& restore,NativeCorrectedReplayBackend& backend,
    const std::array<CorrectedHistoryReplay::Frame,3>& frames,unsigned depth,const RollbackBoundaryKey& head,uintptr_t base){
    return transaction.run(history,restore,backend,frames,depth,head,base);
}
template<unsigned Capacity> bool replay_confirmed_native(const NetworkGameHandoff& handoff,
    CorrectedHistoryReplay& transaction,NativeRollingHistory<Capacity>& history,NativeHistoryRestore<>& restore,
    NativeCorrectedReplayBackend& backend,const RollbackBoundaryKey& target,const RollbackBoundaryKey& head,uintptr_t base){
    std::array<CorrectedHistoryReplay::Frame,3> frames{};
    if(!confirmed_replay_window(handoff,target,head,frames))return false;
    return replay_corrected_native(transaction,history,restore,backend,frames,head.frame-target.frame,head,base);
}
}
