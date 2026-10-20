#pragma once
#include "PredictionInputDelivery.hpp"
#include <atomic>
#include <Windows.h>
#include <memory>
namespace sc6::research {
// Typed raw input, deliberately separate from the filtered-envelope protocol.
// The transport authenticates Steam identity before submit; nonces prevent
// accidental cross-session routing, not cryptographic authentication.
struct RawInputConfirmation {
    uint64_t sender{},recipient{},round{},event{},frame{};
    uint32_t slot{},raw{};
};
// Serialized transport producers; owner-thread consumption and reconciliation. The
// route owns its truth archive until retirement so retransmits cannot alias a
// reused prediction slot. Construction does not publish a native hook.
template<unsigned Records=7200,unsigned Queue=32> class RawPredictionRoute {
public:
    using Delivery=PredictionInputDelivery<>;
    using Filter=typename Delivery::Filter;
    enum class Submit {accepted,full,rejected,retired};
    enum class Failure {none,owner,conflict,delivery,apply,publish,overflow};
    struct Counters {uint64_t drained{},duplicates{},corrections{},appended{},applied{},published{};};
private:
    struct Truth {uint32_t raw{};bool seen{};};
    struct ArrivalTime {uint64_t enqueued{},drained{};};
    std::unique_ptr<std::array<ArrivalTime,Records>> arrival_times_=std::make_unique<std::array<ArrivalTime,Records>>();
    static uint64_t timestamp(){LARGE_INTEGER q{};QueryPerformanceCounter(&q);return q.QuadPart;}
    // Producer-only first accepted words. Identity is fixed for this route;
    // conflicting retransmissions must still reach owner-side validation.
    std::unique_ptr<std::array<Truth,Records>> enqueued_truth_=std::make_unique<std::array<Truth,Records>>();
    std::atomic<uint64_t> coalesced_{},queue_full_{};
    std::array<Truth,Records> truth_{};
    std::array<uint32_t,Records> outgoing_{};
    std::unique_ptr<std::array<uint64_t,Records>> published_times_=std::make_unique<std::array<uint64_t,Records>>();
    std::unique_ptr<std::array<std::atomic<uint64_t>,Records>> first_send_=std::make_unique<std::array<std::atomic<uint64_t>,Records>>();
    std::atomic<unsigned> outgoing_count_{};
    std::array<RawInputConfirmation,Queue> queue_{};
    std::array<uint64_t,Queue> queue_times_{};
    std::atomic<unsigned> write_{},read_{};
    std::atomic<bool> bound_{};
    std::atomic<bool> retired_{};
    std::atomic_flag producer_=ATOMIC_FLAG_INIT;
    const RawPredictionRoute* previous_{};
    unsigned depth_{};
    uint64_t local_{},peer_{},round_{},event_{},origin_{},owner_{};
    unsigned slot_{};
    uint64_t highest_{};
    Delivery delivery_;
    Failure failure_{};
    bool handoff_{};
    Counters counters_;
    bool fail(Failure f){if(failure_==Failure::none)failure_=f;return false;}
    bool owner(uint64_t thread){return bound_ && !failed() && thread==owner_?true:fail(Failure::owner);}
    bool identity(const RawInputConfirmation& p)const{
        return p.sender==peer_ && p.recipient==local_ && p.round==round_ && p.event==event_ && p.slot==3-slot_ &&
            p.frame>=origin_ && p.frame-origin_<Records;
    }
    bool archived(const RawInputConfirmation& p)const{
        if(!retired_.load(std::memory_order_acquire) || !identity(p))return false;
        const auto& t=truth_[p.frame-origin_];return t.seen && t.raw==p.raw;
    }
public:
    bool bind(uint64_t local,uint64_t peer,uint64_t round,uint64_t event,uint64_t origin,
              unsigned slot,uint64_t thread,std::array<uint32_t,2> previous,const RawPredictionRoute* prior=nullptr){
        if(bound_ || failed() || !local || !peer || local==peer || !round || !event || !thread ||
           slot<1 || slot>2 || origin>UINT64_MAX-Records)return false;
        if(prior && (!prior->retired_.load(std::memory_order_acquire) || prior->local_!=local || prior->peer_!=peer ||
           prior->slot_!=slot || prior->depth_>=15))return false;
        for(auto* old=prior;old;old=old->previous_)if(old->round_==round && old->event_==event)return false;
        if(!delivery_.begin(local,round,3-slot,previous))return false;
        previous_=prior;depth_=prior?prior->depth_+1:0;
        local_=local;peer_=peer;round_=round;event_=event;origin_=origin;slot_=slot;owner_=thread;
        bound_.store(true,std::memory_order_release);return true;
    }
    Submit submit(const RawInputConfirmation& p){
        if(!bound_.load(std::memory_order_acquire))return Submit::rejected;
        if(!identity(p)){
            for(auto* old=previous_;old;old=old->previous_)if(old->archived(p))return Submit::retired;
            return Submit::rejected;
        }
        if(producer_.test_and_set(std::memory_order_acquire))return Submit::full;
        struct Unlock {std::atomic_flag& gate;~Unlock(){gate.clear(std::memory_order_release);}} unlock{producer_};
        if(retired_.load(std::memory_order_acquire))return archived(p)?Submit::retired:Submit::rejected;
        auto& accepted=(*enqueued_truth_)[p.frame-origin_];
        if(accepted.seen && accepted.raw==p.raw){coalesced_.fetch_add(1,std::memory_order_relaxed);return Submit::accepted;}
        const unsigned w=write_.load(std::memory_order_relaxed),r=read_.load(std::memory_order_acquire);
        if(w-r==Queue){queue_full_.fetch_add(1,std::memory_order_relaxed);return Submit::full;}
        if(!accepted.seen)accepted={p.raw,true};
        queue_[w%Queue]=p;queue_times_[w%Queue]=timestamp();write_.store(w+1,std::memory_order_release);return Submit::accepted;
    }
    bool drain(uint64_t thread){
        if(!owner(thread))return false;
        // Do not change a raw word between the two native player consumers.
        if(delivery_.head() && !delivery_.frame(delivery_.head()-1)->complete)return true;
        for(unsigned n=0;n<Queue;++n){
            const unsigned r=read_.load(std::memory_order_relaxed),w=write_.load(std::memory_order_acquire);
            if(r==w)break;
            const auto p=queue_[r%Queue];const auto enqueued=queue_times_[r%Queue];read_.store(r+1,std::memory_order_release);
            ++counters_.drained;const auto index=p.frame-origin_;auto& truth=truth_[index];
            if(truth.seen){if(truth.raw!=p.raw)return fail(Failure::conflict);++counters_.duplicates;continue;}
            // A never-confirmed expired frame cannot be silently accepted.
            if(index<delivery_.first())return fail(Failure::delivery);
            truth={p.raw,true};(*arrival_times_)[index]={enqueued,timestamp()};
            if(highest_<index+1)highest_=index+1;
            if(index<delivery_.head()){
                if(delivery_.confirm(local_,round_,index,p.raw)!=Delivery::Arrival::accepted)
                    return fail(Failure::delivery);
                ++counters_.corrections;
            }
        }
        return true;
    }
    bool append(uint64_t thread,uint32_t local_raw,const std::array<Filter,2>& filters,
                const std::array<uint32_t,10>& auxiliary,bool pending=false,std::optional<uint32_t> native_confirmed=std::nullopt){
        if(!owner(thread) || handoff_ || !drain(thread))return false;
        const auto index=delivery_.head();if(index>=Records)return fail(Failure::overflow);
        auto& t=truth_[index];
        if(native_confirmed){
            if(t.seen && t.raw!=*native_confirmed)return fail(Failure::conflict);
            t.raw=*native_confirmed;t.seen=true;
        }
        if(!delivery_.append(local_raw,t.seen?std::optional<uint32_t>(t.raw):std::nullopt,filters,auxiliary,pending))return false;
        outgoing_[index]=local_raw;(*published_times_)[index]=timestamp();
        outgoing_count_.store(static_cast<unsigned>(index+1),std::memory_order_release);
        ++counters_.appended;return true;
    }
    // Immutable owner-to-transport archive. Retransmission never reads the
    // mutable prediction ring and never publishes a predicted remote input.
    uint64_t coalesced()const{return coalesced_.load(std::memory_order_relaxed);}
    uint64_t queue_full()const{return queue_full_.load(std::memory_order_relaxed);}
    unsigned outgoing_count()const{return outgoing_count_.load(std::memory_order_acquire);}
    bool seed_before_first_frame(uint64_t thread,std::array<uint32_t,2> previous){
        return owner(thread) && delivery_.seed_before_first_frame(previous);
    }
    bool mark_first_send(unsigned index,uint64_t& age){
        if(index>=outgoing_count())return false;
        const auto now=timestamp();uint64_t empty=0;
        if(!(*first_send_)[index].compare_exchange_strong(empty,now))return false;
        age=now-(*published_times_)[index];return true;
    }
    bool outgoing(unsigned index,RawInputConfirmation& out)const{
        if(!bound_.load(std::memory_order_acquire) || index>=outgoing_count())return false;
        out={local_,peer_,round_,event_,origin_+index,slot_,outgoing_[index]};return true;
    }
    bool arrival_times(uint64_t thread,uint64_t index,uint64_t& enqueued,uint64_t& drained)const{
        if(thread!=owner_ || index>=Records || !truth_[index].seen)return false;
        enqueued=(*arrival_times_)[index].enqueued;drained=(*arrival_times_)[index].drained;return true;
    }
    bool truth(uint64_t thread,uint64_t index,uint32_t& raw)const{
        if(thread!=owner_ || !bound_ || failed() || index>=Records || !truth_[index].seen)return false;
        raw=truth_[index].raw;return true;
    }
    // Acknowledge only the revision used by a completed scheduler update.
    // Do not drain here: an arrival after replay is not evidence of application.
    bool acknowledge(uint64_t thread,uint64_t revision,uint64_t head){
        if(!owner(thread) || !delivery_.reconciled(revision,head))return fail(Failure::apply);
        ++counters_.applied;return true;
    }
    bool resolve_filter(uint64_t thread,uint64_t number,unsigned slot,const Filter& filter){
        return owner(thread) && (delivery_.resolve_filter(number,slot,filter) || fail(Failure::delivery));
    }
    bool complete_frame(uint64_t thread,uint64_t number,const std::array<uint32_t,10>& native){
        return owner(thread) && (delivery_.complete_frame(number,native) || fail(Failure::delivery));
    }
    // Apply receives the ENTIRE retained, recomposed suffix. It must verify
    // native replay before returning true. It may not call back into this route.
    // No arrival alone proves that corrected game state has been restored.
    template<class Apply> bool reconcile(uint64_t thread,Apply apply){
        if(!owner(thread))return false;
        if(delivery_.head() && !delivery_.frame(delivery_.head()-1)->complete)return false;
        if(!drain(thread))return false;
        const auto revision=delivery_.revision(),head=delivery_.head();
        try {if(!apply(static_cast<const Delivery&>(delivery_)))return fail(Failure::apply);}
        catch(...){return fail(Failure::apply);}
        if(!delivery_.reconciled(revision,head))return fail(Failure::apply);
        ++counters_.applied;return true;
    }
    template<class Publish> bool publish_confirmed(uint64_t thread,Publish publish){
        if(!owner(thread))return false;
        const bool ok=delivery_.publish_confirmed([&](uint64_t,uint64_t,uint64_t n,const auto& raw){
            if(!publish(local_,round_,event_,origin_+n,raw))return false;
            ++counters_.published;return true;
        });
        return ok?true:fail(Failure::publish);
    }
    bool request_handoff(uint64_t thread){
        if(!owner(thread))return false;handoff_=true;return delivery_.request_handoff();
    }
    bool retire(uint64_t thread){
        if(!owner(thread))return false;
        if(retired_.load(std::memory_order_acquire))return true;
        if(producer_.test_and_set(std::memory_order_acquire))return false;
        struct Unlock {std::atomic_flag& gate;~Unlock(){gate.clear(std::memory_order_release);}} unlock{producer_};
        if(!drain(thread) || !handoff_ready())return false;
        retired_.store(true,std::memory_order_release);return true;
    }
    bool retired()const{return retired_.load(std::memory_order_acquire);}
    bool handoff_ready()const{return !failed() && handoff_ && highest_<=delivery_.head() && delivery_.handoff_ready() &&
        read_.load(std::memory_order_acquire)==write_.load(std::memory_order_acquire);}
    bool failed()const{return failure_!=Failure::none || delivery_.failed();}
    Failure failure()const{return failure_;}
    const Delivery& delivery()const{return delivery_;}
    const Counters& counters()const{return counters_;}
    // Owner-thread diagnostic snapshot; do not read mutable counters from the
    // transport worker while the simulation is updating this route.
    template<class Stream> bool report(uint64_t thread,Stream& out){
        if(!bound_ || thread!=owner_)return false;
        out<<"raw_prediction_route_schema=1\nraw_prediction_failure="<<static_cast<unsigned>(failure_)
           <<"\nraw_prediction_drained="<<counters_.drained
           <<"\nraw_prediction_duplicates="<<counters_.duplicates
           <<"\nraw_prediction_confirmed_after_append="<<counters_.corrections
           <<"\nraw_prediction_appended="<<counters_.appended
           <<"\nraw_prediction_apply_callbacks="<<counters_.applied
           <<"\nraw_prediction_published="<<counters_.published
           <<"\nraw_prediction_handoff_ready="<<handoff_ready()<<'\n';
        return true;
    }
};
}
