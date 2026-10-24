#pragma once
#include "NativeClockDispatch.hpp"
#include "CombatBoundaryReservation.hpp"
namespace sc6::research {
struct CombatClockTime {static uint64_t now(){return GetTickCount64();}};
// Wires native clock callbacks to rollback budget and battle reservations.
// Bind only after independently verifying the clock/battle frame mapping.
// No production route is published by construction or binding.
template<class Session,class Clock=NativeClockAccess<>,class Time=CombatClockTime> class CombatClockBridge {
public:
    enum class Failure {none,external_stop,prediction_exception,confirmation_drain,reservation,native_clock,completion,pending_timeout};
private:
    Failure failure_{};
    void fail(Failure reason)noexcept{if(failure_==Failure::none)failure_=reason;reservation_.stop();dispatch_.stop();}
    Session& session_;
    NativeClockDispatch<Clock> dispatch_;
    CombatBoundaryReservation reservation_;
    uint64_t session_id_{},round_{};
    bool bound_{};
    bool pending_service_{},pending_timed_{};
    uint64_t pending_since_{},pending_services_{},pending_elapsed_ms_{},pending_max_ms_{};
    static bool gate(void* context,const typename Clock::State& state)noexcept{
        auto& self=*static_cast<CombatClockBridge*>(context);
        using G=CombatBoundaryReservation::ClockGate;
        const auto result=self.reservation_.clock_gate(self.session_id_,self.round_,GetCurrentThreadId(),state.identity,state.frame);
        self.pending_service_=result==G::pending;
        if(result==G::restart_required){self.fail(Failure::reservation);return false;}
        if(result==G::ready){self.pending_timed_=false;self.pending_elapsed_ms_=0;return true;}
        ++self.pending_services_;
        const auto now=Time::now();
        if(!self.pending_timed_){self.pending_since_=now;self.pending_timed_=true;}
        if(now<self.pending_since_){self.fail(Failure::pending_timeout);return false;}
        self.pending_elapsed_ms_=now-self.pending_since_;
        if(self.pending_elapsed_ms_>self.pending_max_ms_)self.pending_max_ms_=self.pending_elapsed_ms_;
        if(self.pending_elapsed_ms_>=10000)self.fail(Failure::pending_timeout);
        return false;
    }
    void* confirmations_{};bool (*drain_)(void*){};bool (*late_drain_)(void*){};bool (*grace_)(void*){};
    std::atomic<uint64_t> grace_attempts_{},grace_recovered_{};
    bool grace_used_{};unsigned grace_identity_{},grace_frame_{};
    static void recovery_trace(void* context,unsigned outcome)noexcept{
        auto& self=*static_cast<CombatClockBridge*>(context);
        if constexpr(requires {self.session_.trace_missed_recovery(outcome);})self.session_.trace_missed_recovery(outcome);
    }
    static bool recovery_candidate(void* context)noexcept{
        auto& self=*static_cast<CombatClockBridge*>(context);
        if constexpr(requires {self.session_.admission_evidence(self.session_id_,self.round_);}){
            const auto e=self.session_.admission_evidence(self.session_id_,self.round_);
            return e.result==Session::Admission::waiting && e.reason==5 && self.reservation_.quiescent();
        }else return false;
    }
    static bool recovery_admit(void* context,const typename Clock::State& state)noexcept{
        auto& self=*static_cast<CombatClockBridge*>(context);
        // Deliberately bypass the optional grace callback: retry never waits.
        const bool ok=self.reservation_.reserve_from(self.session_,self.session_id_,self.round_,GetCurrentThreadId(),state.identity,state.frame);
        if(self.reservation_.restart_required())self.fail(Failure::reservation);
        return ok;
    }
    void* prediction_context_{};bool (*prediction_)(void*,const typename Clock::State&,bool){};
    static bool prediction(void* context,const typename Clock::State& state,bool native_waiting)noexcept{
        auto& self=*static_cast<CombatClockBridge*>(context);
        try{return self.prediction_ && self.prediction_(self.prediction_context_,state,native_waiting);}
        catch(...){self.fail(Failure::prediction_exception);return false;}
    }
    static bool service_confirmations(void* context) noexcept {
        auto& self=*static_cast<CombatClockBridge*>(context);
        try {if(self.drain_ && !self.drain_(self.confirmations_)){self.fail(Failure::confirmation_drain);return false;}}
        catch(...){self.fail(Failure::confirmation_drain);return false;}
        return true;
    }
    static bool late_confirmations(void* context) noexcept {
        auto& self=*static_cast<CombatClockBridge*>(context);
        try {if(self.late_drain_ && !self.late_drain_(self.confirmations_)){self.fail(Failure::confirmation_drain);return false;}}
        catch(...){self.fail(Failure::confirmation_drain);return false;}
        return true;
    }
    static bool admit(void* context,const typename Clock::State& state) noexcept {
        auto& self=*static_cast<CombatClockBridge*>(context);
        bool retried=false;
        if constexpr(requires {self.session_.admission_evidence(self.session_id_,self.round_);}){
            const auto evidence=self.session_.admission_evidence(self.session_id_,self.round_);
            if(self.grace_ && evidence.result==Session::Admission::waiting && evidence.reason==5 &&
               (!self.grace_used_ || self.grace_identity_!=state.identity || self.grace_frame_!=state.frame)){
                self.grace_used_=true;self.grace_identity_=state.identity;self.grace_frame_=state.frame;
                ++self.grace_attempts_;retried=true;
                if constexpr(requires {self.session_.trace_receipt_grace(true);})self.session_.trace_receipt_grace(true);
                bool ok=false;
                try{ok=self.grace_(self.confirmations_);}catch(...){ok=false;}
                if constexpr(requires {self.session_.trace_receipt_grace(false,ok);})self.session_.trace_receipt_grace(false,ok);
                if(!ok){self.fail(Failure::confirmation_drain);return false;}
            }
        }
        const bool accepted=self.reservation_.reserve_from(self.session_,self.session_id_,self.round_,GetCurrentThreadId(),state.identity,state.frame);
        if(retried && accepted)++self.grace_recovered_;
        if(self.reservation_.restart_required())self.fail(Failure::reservation);
        return accepted;
    }
    static void notify(void* context,typename Clock::Result result) noexcept {
        auto& self=*static_cast<CombatClockBridge*>(context);using R=typename Clock::Result;
        if(result!=R::advanced && result!=R::withheld && result!=R::native_wait){self.fail(Failure::native_clock);return;}
        if(self.pending_service_){
            if(result==R::advanced)self.fail(Failure::completion);
            return; // Existing completed reservation still belongs to battle.
        }
        if(!self.reservation_.clock_completed(result==R::advanced))self.fail(Failure::completion);
    }
public:
    bool attach_prediction(void* context,bool (*callback)(void*,const typename Clock::State&,bool),bool authorize=true){
        if(bound_ || !context || !callback || prediction_)return false;
        prediction_context_=context;prediction_=callback;return dispatch_.attach_prediction(prediction,authorize);
    }
    bool attach_confirmations(void* context,bool (*drain)(void*),bool (*late)(void*)=nullptr,bool (*grace)(void*)=nullptr){
        if(bound_ || drain_ || !context || !drain)return false;
        confirmations_=context;drain_=drain;late_drain_=late;grace_=grace;return true;
    }
    void stop()noexcept{fail(Failure::external_stop);}
    Failure failure()const{return failure_;}
    uint64_t pending_services()const{return pending_services_;}
    uint64_t pending_elapsed_ms()const{return pending_elapsed_ms_;}
    uint64_t pending_max_ms()const{return pending_max_ms_;}
    uint64_t grace_attempts()const{return grace_attempts_.load();}
    uint64_t grace_recovered()const{return grace_recovered_.load();}
    explicit CombatClockBridge(Session& session):session_(session){}
    CombatClockBridge(const CombatClockBridge&)=delete;
    CombatClockBridge& operator=(const CombatClockBridge&)=delete;
    bool bind(uintptr_t base,uintptr_t object,unsigned owner,bool image_verified,bool bilateral_private,bool boundaries_owned,
        uint64_t session,uint64_t round,unsigned identity,unsigned clock_frame,unsigned battle_frame){
        if(bound_ || dispatch_.restart_required() || !image_verified || !bilateral_private || !boundaries_owned || owner!=GetCurrentThreadId())return false;
        if(!reservation_.bind(session,round,owner,identity,clock_frame,battle_frame))return false;
        session_id_=session;round_=round;
        if(!dispatch_.attach_recovery(recovery_candidate,recovery_admit,recovery_trace) || !dispatch_.attach_gate(gate) || !dispatch_.attach_service(service_confirmations) || !dispatch_.attach_late_service(late_confirmations) || !dispatch_.bind(base,object,owner,image_verified,bilateral_private,boundaries_owned,this,admit,notify)){stop();return false;}
        bound_=true;return true;
    }
    NativeClockDispatch<Clock>& dispatch(){return dispatch_;}
    CombatBoundaryReservation& reservation(){return reservation_;}
    bool restart_required()const{return dispatch_.restart_required() || reservation_.restart_required();}
};
}
