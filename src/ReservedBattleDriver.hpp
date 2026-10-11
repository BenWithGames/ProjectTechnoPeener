#pragma once
#include "ExclusiveBattleRoute.hpp"
#include "SimulationBoundaryDriver.hpp"
#include "CombatUpdateAdapter.hpp"
namespace sc6::research {
// Joins the exclusive hook to a reserved scheduler tick. Reader must obtain
// current identity and input from the owned boundary, not cached network state.
// Lifetime must cover publication of route(); no automatic route publication.
template<class Session,class Adapter,class ClockBridge> class ReservedBattleDriver {
public:
    struct Boundary {uint64_t session{},round{};unsigned frame{};CombatFrameInput local;};
    using Reader=bool(*)(void*,uintptr_t,uintptr_t,Boundary&);
    using Publisher=bool(*)(void*,const Boundary&);
private:
    Session& session_;Adapter& adapter_;ClockBridge& clock_;
    SimulationBoundaryDriver<Session> driver_;
    ExclusiveBattleRoute route_;
    void* context_{};Reader reader_{};
    bool bound_{};
    void* publication_{};Publisher publish_{};
    static bool tick(void* context,uintptr_t argument,uintptr_t caller){
        auto& self=*static_cast<ReservedBattleDriver*>(context);
        try {
            Boundary boundary;
            if(self.clock_.restart_required() || !self.reader_(self.context_,argument,caller,boundary) ||
                boundary.frame!=self.adapter_.expected_frame())return self.stop();
            if(!self.adapter_.begin_boundary(boundary.local))return self.stop();
            const auto result=self.driver_.tick_reserved(self.clock_.reservation(),boundary.session,boundary.round,
                GetCurrentThreadId(),boundary.frame,self.adapter_,boundary.local);
            if(result!=SimulationBoundaryDriver<Session>::Disposition::consumed)return self.stop();
            if(self.publish_ && !self.publish_(self.publication_,boundary))return self.stop();
            return true;
        }catch(...){return self.stop();}
    }
    bool stop()noexcept{clock_.stop();return false;}
public:
    ReservedBattleDriver(Session& session,Adapter& adapter,ClockBridge& clock)
        :session_(session),adapter_(adapter),clock_(clock),driver_(session_){}
    ReservedBattleDriver(const ReservedBattleDriver&)=delete;
    ReservedBattleDriver& operator=(const ReservedBattleDriver&)=delete;
    bool attach_publication(void* context,Publisher publish){
        if(bound_ || publish_ || !context || !publish)return false;
        publication_=context;publish_=publish;return true;
    }
    bool bind(uint64_t session,uint64_t round,void* context,Reader reader,bool bilateral_private,
        bool native_ready,bool verified_void_entry){
        if(bound_ || !context || !reader || !verified_void_entry || !clock_.dispatch().active() || clock_.restart_required())return false;
        context_=context;reader_=reader;
        if(!driver_.arm(session,round,GetCurrentThreadId(),bilateral_private,bilateral_private,native_ready,true))return false;
        if(!route_.bind(GetCurrentThreadId(),this,tick,verified_void_entry,true)){stop();return false;}
        bound_=true;return true;
    }
    ExclusiveBattleRoute& route(){return route_;}
    bool restart_required()const{return route_.failed() || driver_.restart_required() || clock_.restart_required();}
};
}
