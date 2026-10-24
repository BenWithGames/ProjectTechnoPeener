#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include "RollbackJournal.hpp"
namespace sc6::research {
// Adapter-owned reusable snapshots; deliberately not wired to the SC6 hook.
// All calls belong to one simulation thread. Adapter failures are terminal:
// switching to stock simulation after a partially restored state is not valid.
template<class Input,class Snapshot,unsigned Limit=3,unsigned Capacity=16>
class BoundedRollbackSession {
    static_assert(Limit>0 && Capacity>Limit+1);
    struct Frame {
        uint64_t number{}; bool valid{},simulated{},confirmed{};
        Input local{},used{},actual{}; Snapshot before{};
    };
    std::array<Frame,Capacity> frames_{};
    uint64_t session_{},round_{},head_{},settled_{};
    Input seed_{};
    std::optional<uint64_t> dirty_;
    bool draining_{};
    RollbackJournal<>* journal_{};
    void event(unsigned kind,uint64_t frame,unsigned detail=0){
        if(journal_)journal_->push(session_,round_,frame,head_,kind,detail);
    }
    bool fail(uint64_t frame,unsigned reason){
        event(RollbackEvent::failure,frame,reason);state=State::restart_required;return false;
    }
public:
    enum class State {idle,running,waiting,restart_required};
    enum class Arrival {accepted,duplicate,wrong_identity,too_far,conflict,expired};
    State state{State::idle};
    uint64_t corrections{},replayed{},stalls{};
    uint64_t head()const{return head_;}
    bool seed_before_first_frame(const Input& seed){
        if(state!=State::running || head_ || draining_ || dirty_)return false;
        seed_=seed;return true;
    }
    // A round reset must not discard speculative state or an unapplied
    // correction. Receipt alone is insufficient: reconcile must have retired
    // the confirmed prefix on the simulation thread first.
    bool settled()const{
        return state!=State::idle && state!=State::restart_required && !dirty_ && settled_==head_;
    }
    // Request BEFORE native result/playback cleanup. Keep accepting old-round
    // confirmations, but prohibit new speculative frames while settling.
    bool request_handoff(uint64_t session,uint64_t round){
        if(session!=session_ || round!=round_ || state==State::idle || state==State::restart_required)return false;
        draining_=true;return true;
    }
    // Cancel a speculative exit only after every simulated input was reconciled.
    // Identity/history are retained; this never recovers a failed restoration.
    bool resume_settled_handoff(uint64_t session,uint64_t round){
        if(session!=session_ || round!=round_ || !handoff_ready())return false;
        draining_=false;state=State::running;return true;
    }
    bool handoff_ready()const{return draining_ && settled();}
    template<class Adapter> bool settle_handoff(Adapter& adapter){
        return draining_ && reconcile(adapter) && settled();
    }
    enum class Admission {ready,waiting,restart_required};
    // Read-only forecast of advance after reconcile. No native work belongs in
    // clock admission; confirmations arriving later can only free more space.
    struct AdmissionEvidence {Admission result{Admission::restart_required};unsigned reason{1};uint64_t confirmed{},settled{},head{},dirty{UINT64_MAX};};
    AdmissionEvidence admission_evidence(uint64_t session,uint64_t round)const{
        AdmissionEvidence e;e.confirmed=e.settled=settled_;e.head=head_;if(dirty_)e.dirty=*dirty_;
        if(session!=session_ || round!=round_ || state==State::idle || state==State::restart_required || head_==UINT64_MAX)return e;
        if(draining_){e.result=Admission::waiting;e.reason=2;return e;}
        if(dirty_){
            if(*dirty_>=head_ || head_-*dirty_>Limit){e.reason=3;return e;}
            for(auto n=*dirty_;n<head_;++n){const auto& f=frames_[n%Capacity];
                if(!f.valid || f.number!=n || !f.simulated){e.reason=4;return e;}
            }
        }
        while(e.confirmed<head_){const auto& f=frames_[e.confirmed%Capacity];
            if(!f.valid || f.number!=e.confirmed || !f.confirmed)break;
            ++e.confirmed;
        }
        e.result=head_-e.confirmed<Limit?Admission::ready:Admission::waiting;
        e.reason=e.result==Admission::ready?0:5;return e;
    }
    Admission admission(uint64_t session,uint64_t round)const{return admission_evidence(session,round).result;}
    void trace_admission(uint64_t session,uint64_t round)const{
        if(!journal_)return;const auto e=admission_evidence(session,round);
        journal_->push(session_,round_,e.confirmed,e.head,RollbackEvent::admission,e.reason,e.settled,e.dirty);
    }
    void trace_confirmation(uint64_t frame,uint64_t enqueued,uint64_t drained){
        if(journal_)journal_->push(session_,round_,frame,head_,RollbackEvent::confirmation_delivery,0,enqueued,drained);
    }
    void trace_receipt_grace(bool starting,bool callback_ok=true)const{
        if(!journal_)return;const auto e=admission_evidence(session_,round_);
        journal_->push(session_,round_,e.confirmed,e.head,
            starting?RollbackEvent::receipt_grace_begin:RollbackEvent::receipt_grace_end,e.reason,callback_ok,e.settled);
    }
    void trace_missed_recovery(unsigned outcome)const{
        if(!journal_)return;const auto e=admission_evidence(session_,round_);
        journal_->push(session_,round_,e.confirmed,e.head,RollbackEvent::missed_update_recovery,outcome,e.reason,e.settled);
    }
    // Attach before begin; journal must outlive this simulation session.
    bool attach_journal(RollbackJournal<>& journal){if(state!=State::idle)return false;journal_=&journal;return true;}
    // Only at an independently established clean round boundary.
    bool begin(uint64_t session,uint64_t round,const Input& neutral={}){
        if(!session || !round || state==State::restart_required)return false;
        if(state!=State::idle && (!settled() || (session==session_ && round==round_)))return false;
        frames_={};session_=session;round_=round;head_=settled_=0;seed_=neutral;dirty_.reset();draining_=false;
        state=State::running;event(RollbackEvent::begin,0);return true;
    }
    Arrival receive(uint64_t session,uint64_t round,uint64_t frame,const Input& input){
        if(state==State::idle || state==State::restart_required || session!=session_ || round!=round_)return Arrival::wrong_identity;
        // Authenticated retransmissions of retained confirmed input are harmless,
        // even after it has settled beyond the correction horizon. Compare the
        // complete retained identity and input; never bless an aliased slot.
        const auto& retained=frames_[frame%Capacity];
        if(retained.valid && retained.number==frame && retained.confirmed && retained.actual==input)
            return Arrival::duplicate;
        if(frame<head_ && head_-frame>Limit){
            // Never silently discard a correction to already simulated state.
            fail(frame,1);return Arrival::expired;
        }
        if(frame>=head_ && frame-head_>=Capacity-Limit)return Arrival::too_far;
        auto& f=frames_[frame%Capacity];
        if(!f.valid || f.number!=frame){f={};f.valid=true;f.number=frame;}
        if(f.confirmed){
            if(f.actual==input)return Arrival::duplicate;
            fail(frame,2);return Arrival::conflict;
        }
        f.confirmed=true;f.actual=input;event(RollbackEvent::arrival,frame);
        if(f.simulated && !(f.used==input) && (!dirty_ || frame<*dirty_))dirty_=frame;
        return Arrival::accepted;
    }
    template<class Adapter> bool reconcile(Adapter& adapter){
        if(state==State::idle || state==State::restart_required)return false;
        if(dirty_){
            const auto first=*dirty_;
            if(first>=head_ || head_-first>Limit)return fail(first,3);
            event(RollbackEvent::correction_begin,first,static_cast<unsigned>(head_-first));
            auto& start=frames_[first%Capacity];
            if(!start.valid || start.number!=first || !start.simulated || !adapter.load(start.before)){
                return fail(first,4);
            }
            Input held=seed_;
            if(first>settled_){const auto& prior=frames_[(first-1)%Capacity];
                if(!prior.valid || prior.number!=first-1)return fail(first,5);
                held=prior.used;
            }
            for(auto n=first;n<head_;++n){auto& f=frames_[n%Capacity];
                if(!f.valid || f.number!=n || !f.simulated)return fail(n,6);
                f.used=f.confirmed?f.actual:adapter.predict(held);held=f.used;
                if(!save_inputs(adapter,f))return fail(n,7);
                if(!adapter.step(f.local,f.used))return fail(n,8);
                ++replayed;
            }
            ++corrections;event(RollbackEvent::correction_end,first,static_cast<unsigned>(head_-first));dirty_.reset();
        }
        // Retire only a contiguous, corrected, confirmed prefix.
        while(settled_<head_){auto& f=frames_[settled_%Capacity];
            if(!f.valid || f.number!=settled_ || !f.confirmed)break;
            seed_=f.actual;++settled_;
        }
        state=State::running;return true;
    }
    template<class Adapter> bool advance(Adapter& adapter,const Input& local){
        if(!reconcile(adapter))return false;
        if(draining_){state=State::waiting;return false;}
        // Stall BEFORE an unconfirmed frame could become four frames old.
        // A real adapter must coordinate both simulation clocks; do not sleep
        // or skip the original SC6 update based on this controller alone.
        if(head_-settled_>=Limit){if(state!=State::waiting)event(RollbackEvent::wait,head_);state=State::waiting;++stalls;return false;}
        if(head_==UINT64_MAX)return fail(head_,9);
        auto& f=frames_[head_%Capacity];
        if(!f.valid || f.number!=head_){f={};f.valid=true;f.number=head_;}
        Input held=seed_;
        if(head_>settled_)held=frames_[(head_-1)%Capacity].used;
        f.local=local;f.used=f.confirmed?f.actual:adapter.predict(held);
        if(!save_inputs(adapter,f))return fail(head_,10);
        if(!adapter.step(f.local,f.used))return fail(head_,11);
        f.simulated=true;event(RollbackEvent::advance,head_,unsigned(!f.confirmed));++head_;return true;
    }
private:
    template<class Adapter> static bool save_inputs(Adapter& adapter,Frame& f){
        if constexpr(requires {adapter.save(f.before,f.local,f.used);})return adapter.save(f.before,f.local,f.used);
        else return adapter.save(f.before);
    }
};
}
