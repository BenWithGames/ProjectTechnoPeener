#pragma once
#include <Windows.h>
#include <cstdint>
#include <limits>
#include <memory>
#include "NativeInputLatency.hpp"

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
    mutable NativeInputLatency latency_;
    std::unique_ptr<NativeInputLatency> fresh_latency_=std::make_unique<NativeInputLatency>();
    uint64_t service_begin_{},service_end_{};
    static bool local_entry(uintptr_t object,const State& state,uint32_t frame,bool& present,uint32_t& raw)noexcept{
        __try {
            const auto entry=object+0x3C0+(state.local_mask==1?0:0x2000)+(frame&511u)*16;
            present=*reinterpret_cast<const uint32_t*>(entry)==state.identity &&
                *reinterpret_cast<const uint32_t*>(entry+4)==frame && *reinterpret_cast<const uint8_t*>(entry+12)==1;
            raw=present?*reinterpret_cast<const uint32_t*>(entry+8):0;return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    uintptr_t base_{},object_{};
    DWORD owner_{};
    uint32_t identity_{};
    bool bound_{},failed_{},serviced_{},native_wait_{};
    State serviced_state_{};
    State wait_state_{};
    State observed_state_{};
    unsigned stage_{};
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
            observed_state_=out;
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
    enum class Inspection {invalid,waiting,ready};
    // Ghidra RVA3FDFA0 handles phases 0..5; RVA3FC920 performs transitions.
    // Only rearming may wait on other valid phases. Bind/active access stay strict.
    static Inspection inspect_rearm(uintptr_t base,uintptr_t object,State& out,Failure& failure,unsigned& exception){
        out={};failure=Failure::none;exception=0;
        if(!base || !object){failure=Failure::layout;return Inspection::invalid;}
        NativeClockAccess probe;probe.base_=base;probe.object_=object;
        if(!probe.read(out)){failure=probe.failure_;exception=probe.exception_;return Inspection::invalid;}
        if(out.phase>5){failure=Failure::phase;return Inspection::invalid;}
        return out.phase==2?Inspection::ready:Inspection::waiting;
    }
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
        stage_=1;if(!validate(before))return Result::restart_required;
        // 3F2B60 publishes local_cursor into the tagged 512-entry input ring.
        // Only newly valid entries are bracketed; pre-existing entries remain missing.
        std::array<bool,4> existed{},readable{};uint32_t ignored{};
        for(unsigned i=0;i<4&&before.local_cursor<=INT32_MAX-i;++i)
            readable[i]=local_entry(object_,before,before.local_cursor+i,existed[i],ignored);
        const auto started=NativeInputLatency::now();
        stage_=2;if(!invoke_service())return Result::restart_required;
        const auto ended=NativeInputLatency::now();latency_.service(started,ended);fresh_latency_->service(started,ended);
        service_begin_=started;service_end_=ended;
        stage_=3;if(!validate(after))return Result::restart_required;
        if(after.frame!=before.frame){fail(Failure::service_advanced);return Result::restart_required;}
        for(unsigned i=0;i<4&&before.local_cursor<=INT32_MAX-i;++i){
            bool present{};uint32_t raw{};
            if(readable[i]&&!existed[i]&&local_entry(object_,after,before.local_cursor+i,present,raw)&&present)
                latency_.publication(before.local_cursor+i,raw,started,ended);
        }
        serviced_state_=after;serviced_=true;
        return native_wait_?Result::native_wait:Result::ready;
    }
    // Close EVERY successful service cycle, even when withholding admission.
    // Thus a prediction-budget wait does not stop subsequent network servicing.
    Result finish(bool scheduler_admits,bool prediction_authorized=false){
        if(failed_)return Result::restart_required;
        if(!bound_)return Result::unbound;
        if(!serviced_){fail(Failure::order);return Result::restart_required;}
        State before{},after{};
        stage_=4;if(!validate(before))return Result::restart_required;
        serviced_=false;
        if(before.frame!=serviced_state_.frame){fail(Failure::advance_count);return Result::restart_required;}
        if(native_wait_ && !prediction_authorized){latency_.decision(true,false,wait_state_.wait_reason,NativeInputLatency::now());fresh_latency_->decision(true,false,wait_state_.wait_reason,NativeInputLatency::now());return Result::native_wait;}
        if(!scheduler_admits){latency_.decision(native_wait_,false,wait_state_.wait_reason,NativeInputLatency::now());fresh_latency_->decision(native_wait_,false,wait_state_.wait_reason,NativeInputLatency::now());return Result::withheld;}
        if(before.frame==static_cast<uint32_t>(INT32_MAX)){fail(Failure::advance_count);return Result::restart_required;}
        stage_=5;if(!invoke_advance())return Result::restart_required;
        stage_=6;if(!validate(after))return Result::restart_required;
        if(after.frame!=before.frame+1){fail(Failure::advance_count);return Result::restart_required;}
        latency_.decision(native_wait_,true,wait_state_.wait_reason,NativeInputLatency::now());fresh_latency_->decision(native_wait_,true,wait_state_.wait_reason,NativeInputLatency::now());
        return Result::advanced;
    }
    // 3FDF30 samples 3B8/3BC during this service, before native future-ring publication.
    // Read only; never rewrite native cursors, rings, or native confirmations.
    bool fresh_local(uint32_t frame,uint32_t& raw)const noexcept {
        if(!bound_ || failed_ || !serviced_ || GetCurrentThreadId()!=owner_ || serviced_state_.frame!=frame || !service_begin_)return false;
        __try {
            if(*reinterpret_cast<const uint32_t*>(object_+0x3A0)!=identity_ ||
               *reinterpret_cast<const uint32_t*>(object_+0x3A4)!=frame ||
               *reinterpret_cast<const uint8_t*>(object_+0x4424)!=2)return false;
            raw=*reinterpret_cast<const uint32_t*>(object_+0x3B8+(serviced_state_.local_mask==1?0:4));
            fresh_latency_->publication(frame,raw,service_begin_,service_end_);return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    void input_consumed(uint32_t frame,uint32_t raw,bool fresh=false)const{(fresh?*fresh_latency_:latency_).consumed(frame,raw,NativeInputLatency::now());}
    void write_input_latency(std::ostream& out,bool fresh=false)const{out<<"input_latency_fresh_local="<<fresh<<'\n';(fresh?*fresh_latency_:latency_).write(out);}
    const State& last_serviced_state()const{return serviced_state_;}
    const State& last_wait_state()const{return wait_state_;}
    const State& last_observed_state()const{return observed_state_;}
    unsigned stage()const{return stage_;}
    Failure failure()const{return failure_;}
    unsigned exception()const{return exception_;}
    bool restart_required()const{return failed_;}
};
}
