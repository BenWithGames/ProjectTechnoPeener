#pragma once
#include "NativeClockAccess.hpp"
#include "NativeOuterTiming.hpp"
namespace sc6::research {
// Routes a whole native dispatcher call. The admission callback must reserve
// one matching battle-boundary update before returning true; it must NOT run
// the battle update from this earlier clock boundary.
template<class Clock=NativeClockAccess<>> class NativeClockDispatch {
public:
    bool recovery_pending()const{return active_ && !failed_ && recovery_pending_;}
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
    Service service_{};Service late_service_{};
    Prediction prediction_{};
    bool prediction_authorization_{true};
    Gate gate_{};
    Service recovery_candidate_{};
    Admit recovery_admit_{};
    void (*recovery_trace_)(void*,unsigned)noexcept{};
    bool recovery_pending_{},recovery_native_wait_{};
    std::atomic<uint64_t> recovery_attempts_{},recovery_advanced_{},recovery_stale_{},recovery_blocked_{};
    std::atomic<uint64_t> admission_rechecks_{},admission_recovered_{};
    bool active_{},entered_{},failed_{},pending_prediction_{};
    Failure failure_{};
    void fail(Failure reason)noexcept{if(failure_==Failure::none)failure_=reason;failed_=true;}
public:
    bool attach_recovery(Service candidate,Admit admit,void (*trace)(void*,unsigned)noexcept=nullptr){
        if(active_ || recovery_candidate_ || !candidate || !admit)return false;
        recovery_candidate_=candidate;recovery_admit_=admit;recovery_trace_=trace;return true;
    }
    void write_recovery(std::ostream& out)const{
        out<<"missed_update_recovery_schema=1\nmissed_update_recovery_wait_us=0\n"
           <<"missed_update_recovery_attempts="<<recovery_attempts_.load()
           <<"\nmissed_update_recovery_advanced="<<recovery_advanced_.load()
           <<"\nmissed_update_recovery_stale="<<recovery_stale_.load()
           <<"\nmissed_update_recovery_blocked="<<recovery_blocked_.load()
           <<"\nadmission_receipt_rechecks="<<admission_rechecks_.load()
           <<"\nadmission_receipt_recovered="<<admission_recovered_.load()<<'\n';
    }
    // Called once at the owned outer entry, never from the transport worker.
    bool recover(uintptr_t object){
        if(!active_ || object!=object_ || !recovery_pending_)return !failed_;
        recovery_pending_=false;
        if(failed_)return false;
        if(entered_){fail(Failure::reentry);return false;}
        entered_=true;++recovery_attempts_;
        unsigned outcome=4;
        if(recovery_trace_)recovery_trace_(context_,0);
        struct Exit {NativeClockDispatch& self;unsigned& outcome;~Exit(){if(self.recovery_trace_)self.recovery_trace_(self.context_,outcome);self.entered_=false;}} exit{*this,outcome};
        using R=typename Clock::Result;
        auto result=clock_.reopen_withheld();
        if(result==R::withheld){++recovery_stale_;outcome=3;return true;}
        if(result!=R::ready){fail(Failure::native_clock);return false;}
        // Same reservation and full native/round prediction gate as ordinary admission.
        if(service_ && !service_(context_))fail(Failure::service_callback);
        const bool available=!failed_ && (!gate_ || gate_(context_,clock_.last_serviced_state()));
        bool eligible=available && prediction_ && prediction_authorization_;
        if(eligible)eligible=prediction_(context_,clock_.last_wait_state(),recovery_native_wait_);
        if(!failed_ && eligible && late_service_ && !late_service_(context_))fail(Failure::service_callback);
        const bool admit=!failed_ && eligible && recovery_admit_(context_,clock_.last_serviced_state());
        if(!failed_)result=clock_.finish(admit,admit,false);
        if(failed_ || result==R::restart_required || result==R::unbound){fail(Failure::native_clock);return false;}
        if(result==R::advanced){pending_prediction_=true;++recovery_advanced_;NativeOuterTiming::admitted(object_,clock_.last_serviced_state().identity,clock_.last_serviced_state().frame+1);outcome=1;}
        else {++recovery_blocked_;outcome=2;}
        notify_(context_,result);
        return !failed_;
    }
    bool attach_gate(Gate gate){if(active_ || failed_ || gate_ || !gate)return false;gate_=gate;return true;}
    bool attach_prediction(Prediction prediction,bool authorize=true){if(active_ || failed_ || prediction_ || !prediction)return false;prediction_=prediction;prediction_authorization_=authorize;return true;}
    bool attach_late_service(Service service){if(active_ || failed_ || late_service_ || !service)return false;late_service_=service;return true;}
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
        recovery_pending_=false;
        if(entered_){fail(Failure::reentry);return;}
        entered_=true;
        auto result=clock_.service();
        recovery_native_wait_=result==R::native_wait;
        if(result==R::ready || result==R::native_wait){
            // Drain authoritative confirmations even while native SC6 is waiting.
            // This never reserves a step or overrides the native wait decision.
            if(service_ && !service_(context_))fail(Failure::service_callback);
            // Always validate the clock/battle mapping, including native waits.
            // An outstanding battle reservation keeps service alive but must
            // not advance the clock or be interpreted as a new completion.
            const bool available=!failed_ && (!gate_ || gate_(context_,clock_.last_serviced_state()));
            if(available)pending_prediction_=false; // previous reservation consumed
            bool eligible=available && result==R::ready;
            if(available && !failed_ && prediction_)eligible=prediction_(context_,clock_.last_wait_state(),result==R::native_wait);
            // Catch arrivals during prediction checks before reserving the update.
            if(!failed_ && eligible && late_service_ && !late_service_(context_))fail(Failure::service_callback);
            bool admit=!failed_ && eligible && admit_(context_,clock_.last_serviced_state());
            // Close the final receipt/admission race without sleeping or re-running
            // native service. Only a quiescent prediction-budget rejection qualifies.
            if(!failed_ && eligible && !admit && prediction_authorization_ && late_service_ &&
               recovery_candidate_ && recovery_candidate_(context_)){
                ++admission_rechecks_;
                if(!late_service_(context_))fail(Failure::service_callback);
                const bool still_available=!failed_ && (!gate_ || gate_(context_,clock_.last_serviced_state()));
                const bool still_eligible=still_available && prediction_ && prediction_(context_,clock_.last_wait_state(),result==R::native_wait);
                if(!failed_ && still_eligible)admit=recovery_admit_(context_,clock_.last_serviced_state());
                if(admit)++admission_recovered_;
                eligible=still_eligible;
            }
            if(!available)++NativeOuterTiming::clock_counts.unavailable;
            else if(!eligible)++NativeOuterTiming::clock_counts.ineligible;
            else if(!admit)++NativeOuterTiming::clock_counts.unadmitted;
            if(!failed_){
                const bool authorized=admit && prediction_ && eligible && prediction_authorization_;
                const bool budget_wait=prediction_authorization_ && eligible && !admit && recovery_candidate_ && recovery_candidate_(context_);
                if constexpr(requires {clock_.finish(admit,false,false,false);})result=clock_.finish(admit,authorized,!available && pending_prediction_,budget_wait);
                else if constexpr(requires {clock_.finish(admit,false,false);})result=clock_.finish(admit,authorized,!available && pending_prediction_);
                else if constexpr(requires {clock_.finish(admit,false);})result=clock_.finish(admit,authorized);
                else result=clock_.finish(admit);
                if(result==R::advanced)pending_prediction_=authorized;
                recovery_pending_=budget_wait && (result==R::withheld || result==R::native_wait);
            }
        }
        if(failed_ || result==R::restart_required || result==R::unbound){
            fail(Failure::native_clock);result=R::restart_required;
        }
        if(result==R::advanced)NativeOuterTiming::admitted(object_,clock_.last_serviced_state().identity,clock_.last_serviced_state().frame+1);
        notify_(context_,result);
        entered_=false;
        // Never call original here, including after partial servicing/failure.
    }
};
}
