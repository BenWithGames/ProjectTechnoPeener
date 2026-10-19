#pragma once
#include "NativeClockAccess.hpp"
namespace sc6::research {
// Routes a whole native dispatcher call. The admission callback must reserve
// one matching battle-boundary update before returning true; it must NOT run
// the battle update from this earlier clock boundary.
template<class Clock=NativeClockAccess<>> class NativeClockDispatch {
public:
    enum class Failure {none,external_stop,reentry,service_callback,native_clock};
    using Original=void(*)(uintptr_t);
    using Admit=bool(*)(void*,const typename Clock::State&) noexcept;
    using Notify=void(*)(void*,typename Clock::Result) noexcept;
    using Service=bool(*)(void*) noexcept;
    using Prediction=bool(*)(void*,const typename Clock::State&,bool) noexcept;
    using Gate=bool(*)(void*,const typename Clock::State&) noexcept;
private:
    Clock clock_;
    uintptr_t object_{};
    void* context_{};
    Admit admit_{};
    Notify notify_{};
    Service service_{};
    Prediction prediction_{};
    Gate gate_{};
    bool active_{},entered_{},failed_{};
    Failure failure_{};
    void fail(Failure reason)noexcept{if(failure_==Failure::none)failure_=reason;failed_=true;}
public:
    bool attach_gate(Gate gate){if(active_ || failed_ || gate_ || !gate)return false;gate_=gate;return true;}
    bool attach_prediction(Prediction prediction){if(active_ || failed_ || prediction_ || !prediction)return false;prediction_=prediction;return true;}
    bool attach_service(Service service){if(active_ || failed_ || service_ || !service)return false;service_=service;return true;}
    bool bind(uintptr_t base,uintptr_t object,DWORD owner,bool image_verified,
              bool bilateral_private,bool boundaries_owned,void* context,Admit admit,Notify notify){
        if(active_ || failed_ || !admit || !notify)return false;
        if(!clock_.bind(base,object,owner,image_verified,bilateral_private,boundaries_owned))return false;
        object_=object;context_=context;admit_=admit;notify_=notify;active_=true;return true;
    }
    bool active()const{return active_;}
    bool at_boundary(unsigned identity,unsigned frame){return active_ && !entered_ && !failed_ && clock_.at_boundary(identity,frame);}
    bool restart_required()const{return failed_ || clock_.restart_required();}
    void stop()noexcept{fail(Failure::external_stop);}
    Failure failure()const{return failure_;}
    const Clock& access()const{return clock_;}
    void run(uintptr_t object,Original original) {
        if(!active_ || object!=object_){original(object);return;}
        using R=typename Clock::Result;
        if(failed_)return;
        if(entered_){fail(Failure::reentry);return;}
        entered_=true;
        auto result=clock_.service();
        if(result==R::ready || result==R::native_wait){
            // Drain authoritative confirmations even while native SC6 is waiting.
            // This never reserves a step or overrides the native wait decision.
            if(service_ && !service_(context_))fail(Failure::service_callback);
            // Always validate the clock/battle mapping, including native waits.
            // An outstanding battle reservation keeps service alive but must
            // not advance the clock or be interpreted as a new completion.
            const bool available=!failed_ && (!gate_ || gate_(context_,clock_.last_serviced_state()));
            bool eligible=available && result==R::ready;
            if(available && !failed_ && prediction_)eligible=prediction_(context_,clock_.last_wait_state(),result==R::native_wait);
            const bool admit=!failed_ && eligible && admit_(context_,clock_.last_serviced_state());
            if(!failed_){
                if constexpr(requires {clock_.finish(admit,false);})result=clock_.finish(admit,admit && prediction_ && eligible);
                else result=clock_.finish(admit);
            }
        }
        if(failed_ || result==R::restart_required || result==R::unbound){
            fail(Failure::native_clock);result=R::restart_required;
        }
        notify_(context_,result);
        entered_=false;
        // Never call original here, including after partial servicing/failure.
    }
};
}
