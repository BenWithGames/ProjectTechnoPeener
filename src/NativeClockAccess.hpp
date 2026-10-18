#pragma once
#include <Windows.h>
#include <cstdint>
#include <limits>

namespace sc6::research {
struct NativeClockCalls {
    static void service(uintptr_t base,uintptr_t object){
        reinterpret_cast<void(*)(uintptr_t)>(base+0x3FDB80)(object);
    }
    static bool waiting(uintptr_t base,uintptr_t object){
        return reinterpret_cast<bool(*)(uintptr_t)>(base+0x3E9EE0)(object);
    }
    static void advance(uintptr_t base,uintptr_t object){
        reinterpret_cast<void(*)(uintptr_t)>(base+0x3E2000)(object);
    }
};

// Native half of the dispatcher at RVA 3F5D20. It does not run battle updates,
// restore snapshots, intercept a hook, or override SC6's own wait decision.
// The owning hook must replace the ENTIRE dispatcher for a bound object;
// calling the original dispatcher afterward would process/advance twice.
// Calls is injectable for unit tests; the production default invokes the
// pinned native routines established by disassembly and runtime inspection.
template<class Calls=NativeClockCalls> class NativeClockAccess {
public:
    bool at_boundary(unsigned identity,unsigned frame){State state{};return validate(state) && state.identity==identity && state.frame==frame;}
    struct State {
        uint32_t identity{},frame{},local_cursor{},all_cursor{},lead{};
        unsigned slots{},local_mask{},all_mask{},remote_mask{},mode{},phase{};
        unsigned wait_reason{},wait_count{},published{};
    };
    enum class Result {unbound,ready,native_wait,withheld,advanced,restart_required};
    enum class Failure {none,identity,layout,phase,order,exception,service_advanced,advance_count};
private:
    uintptr_t base_{},object_{};
    DWORD owner_{};
    uint32_t identity_{};
    bool bound_{},failed_{},serviced_{},native_wait_{};
    State serviced_state_{};
    State wait_state_{};
    Failure failure_{};
    unsigned exception_{};
    bool fail(Failure reason){failed_=true;failure_=reason;return false;}
    bool read(State& out) noexcept {
        __try {
            const auto table=*reinterpret_cast<const uintptr_t*>(object_);
            if(table!=base_+0x3283578 ||
                *reinterpret_cast<const uintptr_t*>(table+0x620)!=base_+0x3FDB80 ||
                *reinterpret_cast<const uintptr_t*>(table+0x628)!=base_+0x3E9EE0 ||
                *reinterpret_cast<const uintptr_t*>(table+0x630)!=base_+0x3E2000)return fail(Failure::layout);
            const auto word=[&](uintptr_t offset){return *reinterpret_cast<const uint32_t*>(object_+offset);};
            out.identity=word(0x3A0);out.frame=word(0x3A4);out.slots=word(0x398);
            out.local_mask=word(0x39C);out.remote_mask=word(0x4400);out.all_mask=word(0x43FC);
            out.local_cursor=word(0x4410);out.all_cursor=word(0x4414);out.lead=word(0x440C);
            out.mode=*reinterpret_cast<const unsigned char*>(object_+0x4404);
            out.phase=*reinterpret_cast<const unsigned char*>(object_+0x4424);
            out.wait_reason=*reinterpret_cast<const unsigned char*>(object_+0x4425);
            out.wait_count=word(0x3A8);out.published=word(0x3B0);
            if(out.slots!=2 || (out.local_mask!=1 && out.local_mask!=2) ||
                out.remote_mask!=(out.local_mask^3u) || out.all_mask!=3 || out.mode!=1 ||
                out.frame>static_cast<uint32_t>(INT32_MAX) || out.identity>static_cast<uint32_t>(INT32_MAX))return fail(Failure::layout);
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER){exception_=GetExceptionCode();return fail(Failure::exception);}
    }
    bool validate(State& out){
        if(failed_)return false;
        if(!bound_ || GetCurrentThreadId()!=owner_)return fail(Failure::identity);
        if(!read(out))return false;
        if(out.identity!=identity_)return fail(Failure::identity);
        if(out.phase!=2)return fail(Failure::phase);
        return true;
    }
    bool invoke_service() noexcept {
        __try {
            Calls::service(base_,object_);
            // 3E9EE0 clears 4425 and changes 3A8. Preserve the actual
            // post-service reason BEFORE that stateful native call. An
            // observation made before service or after waiting is stale.
            if(!read(wait_state_))return false;
            native_wait_=Calls::waiting(base_,object_);return true;
        }
        __except(EXCEPTION_EXECUTE_HANDLER){exception_=GetExceptionCode();return fail(Failure::exception);}
    }
    bool invoke_advance() noexcept {
        __try {Calls::advance(base_,object_);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER){exception_=GetExceptionCode();return fail(Failure::exception);}
    }
public:
    // Read-only discovery; no ownership claim and no native calls.
    static bool inspect(uintptr_t base,uintptr_t object,State& out){
        out={};if(!base || !object)return false;
        NativeClockAccess probe;probe.base_=base;probe.object_=object;
        State value{};if(!probe.read(value) || value.phase!=2)return false;
        out=value;return true;
    }
    // Only after executable validation, bilateral private-session admission,
    // and exclusive ownership of this dispatcher and the battle boundary.
    // No installed code calls bind yet: combat snapshot support remains needed.
    bool bind(uintptr_t base,uintptr_t object,DWORD owner,bool image_verified,
              bool bilateral_private,bool boundaries_owned){
        if(bound_ || failed_ || !base || !object || !owner || owner!=GetCurrentThreadId() ||
            !image_verified || !bilateral_private || !boundaries_owned)return false;
        base_=base;object_=object;owner_=owner;
        State state{};if(!read(state))return false;
        if(state.phase!=2)return fail(Failure::phase);
        identity_=state.identity;bound_=true;return true;
    }
    Result service(){
        if(failed_)return Result::restart_required;
        if(!bound_)return Result::unbound;
        if(serviced_){fail(Failure::order);return Result::restart_required;}
        State before{},after{};
        if(!validate(before) || !invoke_service() || !validate(after))return Result::restart_required;
        if(after.frame!=before.frame){fail(Failure::service_advanced);return Result::restart_required;}
        serviced_state_=after;serviced_=true;
        return native_wait_?Result::native_wait:Result::ready;
    }
    // Close EVERY successful service cycle, even when withholding admission.
    // Thus a prediction-budget wait does not stop subsequent network servicing.
    Result finish(bool scheduler_admits){
        if(failed_)return Result::restart_required;
        if(!bound_)return Result::unbound;
        if(!serviced_){fail(Failure::order);return Result::restart_required;}
        State before{},after{};
        if(!validate(before))return Result::restart_required;
        serviced_=false;
        if(before.frame!=serviced_state_.frame){fail(Failure::advance_count);return Result::restart_required;}
        if(native_wait_)return Result::native_wait;
        if(!scheduler_admits)return Result::withheld;
        if(before.frame==static_cast<uint32_t>(INT32_MAX)){fail(Failure::advance_count);return Result::restart_required;}
        if(!invoke_advance() || !validate(after))return Result::restart_required;
        if(after.frame!=before.frame+1){fail(Failure::advance_count);return Result::restart_required;}
        return Result::advanced;
    }
    const State& last_serviced_state()const{return serviced_state_;}
    const State& last_wait_state()const{return wait_state_;}
    Failure failure()const{return failure_;}
    unsigned exception()const{return exception_;}
    bool restart_required()const{return failed_;}
};
}
