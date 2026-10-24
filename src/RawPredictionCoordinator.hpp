#pragma once
#include "RawPredictionRoute.hpp"
#include "CombatUpdateAdapter.hpp"
namespace sc6::research {
// Joins authoritative raw history to the scheduler. The scheduler compares raw
// markers, never filtered pressed edges; the native adapter consumes the shared
// recomposed envelopes. Only the game owner calls this coordinator.
template<class Session> class RawPredictionCoordinator {
    RawPredictionRoute<>& route_;
    Session& session_;
    std::array<bool,7200> submitted_{};
    uint64_t local_{},round_{},owner_{},revision_{},through_{};
    unsigned remote_{};
    bool bound_{},prepared_{},failed_{};
    std::atomic<uint64_t> completed_checks_{},completed_drained_{},completed_submitted_{};
    uint64_t submitted_count_{};
    std::atomic<uint64_t> pre_execute_checks_{},pre_execute_drained_{};
    bool fail(){failed_=true;return false;}
    CombatFrameInput marker(uint32_t raw)const{
        CombatFrameInput value;value.envelope[(remote_-1)*2]=raw;return value;
    }
public:
    RawPredictionCoordinator(RawPredictionRoute<>& route,Session& session):route_(route),session_(session){}
    bool bind(uint64_t local,uint64_t round,uint64_t owner,unsigned remote){
        if(bound_ || !local || !round || !owner || remote<1 || remote>2 || !route_.delivery().identity(local,round))return false;
        local_=local;round_=round;owner_=owner;remote_=remote;bound_=true;return true;
    }
    bool seed(uint64_t owner,std::array<uint32_t,2> raw){
        if(!bound_ || failed_ || owner!=owner_ || prepared_)return false;
        return route_.seed_before_first_frame(owner,raw) && session_.seed_before_first_frame(marker(raw[remote_-1]));
    }
    bool drain(uint64_t owner){
        if(!bound_ || failed_ || owner!=owner_ || prepared_ || !route_.drain(owner))return fail();
        // Future truth stays in the route archive until the scheduler has room.
        const auto last=session_.head();
        if(last>submitted_.size())return fail();
        const auto first=last>3?last-3:0;
        for(auto n=first;n<=last && n<submitted_.size();++n){uint32_t raw{};
            if(submitted_[n] || !route_.truth(owner,n,raw))continue;
            if constexpr(requires {session_.trace_confirmation(n,uint64_t{},uint64_t{});}){
                uint64_t enqueued{},drained{};if(route_.arrival_times(owner,n,enqueued,drained))session_.trace_confirmation(n,enqueued,drained);
            }
            const auto result=session_.receive(local_,round_,n,marker(raw));
            if(result!=Session::Arrival::accepted && result!=Session::Arrival::duplicate)return fail();
            route_.measure_confirmation(owner,n,session_.head());
            submitted_[n]=true;
            ++submitted_count_;
        }
        return true;
    }
    // Called after both native input filters, before scheduler.advance().
    bool before_step(uint64_t owner){
        if(!drain(owner))return false;
        const auto& history=route_.delivery();
        const auto* f=history.frame(session_.head());
        if(history.head()!=session_.head()+1 || !f || !f->complete)return fail();
        revision_=history.revision();through_=history.head();prepared_=true;return true;
    }
    // Last owner-thread drain after both filters and before any snapshot/load.
    // Refresh the acknowledgment token only for this still-unexecuted update.
    bool before_execute(uint64_t owner){
        if(!prepared_ || failed_ || owner!=owner_ || through_!=session_.head()+1 ||
           route_.delivery().head()!=through_)return fail();
        ++pre_execute_checks_;
        if(!route_.pending_receipts())return true;
        const auto prior=route_.counters().drained;
        prepared_=false;
        if(!drain(owner))return false;
        const auto& history=route_.delivery();const auto* f=history.frame(session_.head());
        if(history.head()!=through_ || !f || !f->complete)return fail();
        revision_=history.revision();prepared_=true;
        pre_execute_drained_+=route_.counters().drained-prior;
        return true;
    }
    uint64_t pre_execute_checks()const{return pre_execute_checks_;}
    uint64_t pre_execute_drained()const{return pre_execute_drained_;}
    template<class Publish> bool after_step(uint64_t owner,Publish publish){
        if(!prepared_ || failed_ || owner!=owner_ || session_.head()!=through_ ||
           session_.state==Session::State::restart_required)return fail();
        prepared_=false;
        if(!route_.acknowledge(owner,revision_,through_) || !route_.publish_confirmed(owner,publish))return fail();
        // The completed update owns only revision_/through_. Accept late truth
        // AFTER acknowledging it, never as evidence that late corrections ran.
        // Both input consumers have finished and the scheduler has advanced, so
        // this also admits archived truth for its newly available head frame.
        ++completed_checks_;
        const auto drained=route_.counters().drained,submitted=submitted_count_;
        const bool ok=drain(owner);
        completed_drained_+=route_.counters().drained-drained;
        completed_submitted_+=submitted_count_-submitted;
        return ok;
    }
    uint64_t completed_checks()const{return completed_checks_;}
    uint64_t completed_drained()const{return completed_drained_;}
    uint64_t completed_submitted()const{return completed_submitted_;}
    // Settlement replay runs at a captured endpoint, not from the clock drain.
    template<class Apply,class Publish> bool settle(uint64_t owner,Apply apply,Publish publish){
        if(!drain(owner) || !route_.request_handoff(owner))return false;
        if(!route_.reconcile(owner,[&](const auto& history){return history.head()==session_.head() && apply();}))return fail();
        return route_.publish_confirmed(owner,publish);
    }
    bool failed()const{return failed_ || route_.failed();}
};
}
