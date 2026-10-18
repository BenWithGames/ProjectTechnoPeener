#pragma once
#include <Windows.h>
#include <intrin.h>
#include <cstdint>
#include <cfenv>
namespace sc6::research {
// Same-thread stack retention. This executes the supplied native routine,
// rather than reconstructing its stack or jumping into its epilogue.
// Not installed in SC6: integration must establish a fiber-capable host and
// audit TLS/FLS, clock gating and engine work that runs while suspended.
class OuterStackContinuation {
public:
    using Routine=void(*)(void*,uintptr_t);
    enum class State {unbound,idle,running,suspended,complete,failed};
    enum class Failure {none,configuration,thread,host,reentry,argument,exception,parked,allocation};
private:
    DWORD owner_{};
    void* host_{};void* fiber_{};void* context_{};
    Routine routine_{};
    uintptr_t argument_{};
    State state_{State::unbound};Failure failure_{};
    DWORD host_error_{},native_error_{};
    std::fenv_t entry_environment_{};
    bool entered_{},native_frames_{};
    uint64_t starts_{},resumes_{},suspensions_{};
    bool fail(Failure reason){if(failure_==Failure::none)failure_=reason;state_=State::failed;return false;}
    static void WINAPI entry(void* pointer){
        auto& self=*static_cast<OuterStackContinuation*>(pointer);
        for(;;){
            SetLastError(self.native_error_);
            if(std::fesetenv(&self.entry_environment_)!=0){
                self.fail(Failure::configuration);SetLastError(self.host_error_);
                SwitchToFiber(self.host_);__fastfail(7);
            }
            self.native_frames_=true;
            try {self.routine_(self.context_,self.argument_);}
            catch(...){self.fail(Failure::exception);}
            self.native_frames_=false;
            if(self.state_!=State::failed)self.state_=State::complete;
            self.native_error_=GetLastError();
            SetLastError(self.host_error_);
            SwitchToFiber(self.host_);
            SetLastError(self.native_error_);
        }
    }
public:
    OuterStackContinuation()=default;
    OuterStackContinuation(const OuterStackContinuation&)=delete;
    OuterStackContinuation& operator=(const OuterStackContinuation&)=delete;
    // A suspended/parked instance must have process lifetime. Deleting a fiber
    // with native frames would abandon destructors and retained game ownership.
    // Explicit release() refuses it; no destructor hides that operation.
    State state()const{return state_;}
    Failure failure()const{return failure_;}
    bool has_native_frames()const{return native_frames_;}
    uint64_t starts()const{return starts_;}
    uint64_t resumes()const{return resumes_;}
    uint64_t suspensions()const{return suspensions_;}
    // 5,000,000 is the pinned SC6 PE's SizeOfStackReserve, not the host test
    // executable's default. A different native owner requires its own audit.
    bool bind(DWORD owner,void* context,Routine routine,size_t reserve=5000000){
        const DWORD error=GetLastError();
        if(failure_!=Failure::none || state_!=State::unbound || !owner || owner!=GetCurrentThreadId() || !context || !routine ||
            reserve<65536 || reserve>64*1024*1024)
            return fail(Failure::configuration);
        if(!IsThreadAFiber())return fail(Failure::host);
        owner_=owner;host_=GetCurrentFiber();context_=context;routine_=routine;
        fiber_=CreateFiberEx(0,reserve,FIBER_FLAG_FLOAT_SWITCH,entry,this);
        if(!fiber_)return fail(Failure::allocation);
        state_=State::idle;SetLastError(error);return true;
    }
    bool run(uintptr_t argument){
        if(GetCurrentThreadId()!=owner_)return false; // foreign callers cannot race the owner state
        if(state_==State::failed || state_==State::unbound)return false;
        if(entered_)return fail(Failure::reentry);
        if(!IsThreadAFiber() || GetCurrentFiber()!=host_)return fail(Failure::host);
        if(state_==State::suspended && argument!=argument_)return fail(Failure::argument);
        if(state_!=State::idle && state_!=State::complete && state_!=State::suspended)return fail(Failure::reentry);
        host_error_=GetLastError();
        if(state_==State::suspended)++resumes_;else {
            if(std::fegetenv(&entry_environment_)!=0)return fail(Failure::configuration);
            ++starts_;argument_=argument;native_error_=host_error_;
        }
        entered_=true;state_=State::running;
        SwitchToFiber(fiber_);
        entered_=false;
        SetLastError(host_error_);
        return state_==State::suspended || state_==State::complete;
    }
    bool suspend(){
        if(GetCurrentThreadId()!=owner_)return false;
        if(!entered_ || state_!=State::running || GetCurrentFiber()!=fiber_)return fail(Failure::host);
        native_error_=GetLastError();++suspensions_;state_=State::suspended;
        SetLastError(host_error_);SwitchToFiber(host_);
        SetLastError(native_error_);
        return state_==State::running;
    }
    // Called only on the native fiber. Never returns to a possibly partial
    // native continuation; retains its stack for restart and diagnostics.
    void park_failure(){
        if(GetCurrentThreadId()!=owner_ || !entered_ || GetCurrentFiber()!=fiber_)return;
        fail(Failure::parked);SetLastError(host_error_);SwitchToFiber(host_);
        // run() rejects failed state, so this point is deliberately unreachable.
        __fastfail(7);
    }
    bool release(){
        if(GetCurrentThreadId()!=owner_ || entered_ || native_frames_ ||
            (state_!=State::idle && state_!=State::complete && state_!=State::failed) ||
            !IsThreadAFiber() || GetCurrentFiber()!=host_)return false;
        const DWORD error=GetLastError();
        if(fiber_)DeleteFiber(fiber_);
        fiber_=nullptr;state_=State::unbound;SetLastError(error);return true;
    }
};
}
