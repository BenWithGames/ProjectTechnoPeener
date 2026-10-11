#pragma once
#include "NativeClockDispatch.hpp"
#include "CombatBoundaryReservation.hpp"
namespace sc6::research {
// Wires native clock callbacks to rollback budget and battle reservations.
// Bind only after independently verifying the clock/battle frame mapping.
// No production route is published by construction or binding.
template<class Session,class Clock=NativeClockAccess<>> class CombatClockBridge {
    Session& session_;
    NativeClockDispatch<Clock> dispatch_;
    CombatBoundaryReservation reservation_;
    uint64_t session_id_{},round_{};
    bool bound_{};
    void* confirmations_{};bool (*drain_)(void*){};
    static bool admit(void* context,const typename Clock::State& state) noexcept {
        auto& self=*static_cast<CombatClockBridge*>(context);
        try {if(self.drain_ && !self.drain_(self.confirmations_)){self.stop();return false;}}
        catch(...){self.stop();return false;}
        const bool accepted=self.reservation_.reserve_from(self.session_,self.session_id_,self.round_,GetCurrentThreadId(),state.identity,state.frame);
        if(self.reservation_.restart_required())self.stop();
        return accepted;
    }
    static void notify(void* context,typename Clock::Result result) noexcept {
        auto& self=*static_cast<CombatClockBridge*>(context);using R=typename Clock::Result;
        if(result!=R::advanced && result!=R::withheld && result!=R::native_wait){self.stop();return;}
        if(!self.reservation_.clock_completed(result==R::advanced))self.stop();
    }
public:
    bool attach_confirmations(void* context,bool (*drain)(void*)){
        if(bound_ || drain_ || !context || !drain)return false;
        confirmations_=context;drain_=drain;return true;
    }
    void stop()noexcept{reservation_.stop();dispatch_.stop();}
    explicit CombatClockBridge(Session& session):session_(session){}
    CombatClockBridge(const CombatClockBridge&)=delete;
    CombatClockBridge& operator=(const CombatClockBridge&)=delete;
    bool bind(uintptr_t base,uintptr_t object,unsigned owner,bool image_verified,bool bilateral_private,bool boundaries_owned,
        uint64_t session,uint64_t round,unsigned identity,unsigned clock_frame,unsigned battle_frame){
        if(bound_ || dispatch_.restart_required() || !image_verified || !bilateral_private || !boundaries_owned || owner!=GetCurrentThreadId())return false;
        if(!reservation_.bind(session,round,owner,identity,clock_frame,battle_frame))return false;
        session_id_=session;round_=round;
        if(!dispatch_.bind(base,object,owner,image_verified,bilateral_private,boundaries_owned,this,admit,notify)){stop();return false;}
        bound_=true;return true;
    }
    NativeClockDispatch<Clock>& dispatch(){return dispatch_;}
    CombatBoundaryReservation& reservation(){return reservation_;}
    bool restart_required()const{return dispatch_.restart_required() || reservation_.restart_required();}
};
}
