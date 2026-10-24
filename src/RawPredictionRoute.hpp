#pragma once
#include "PredictionInputDelivery.hpp"
#include "ConfirmationAge.hpp"
#include "InputTimeline.hpp"
#include "RawPublicationSignal.hpp"
#include "ConfirmationReceiptGrace.hpp"
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
    InputTimeline<> input_timeline;
    ConfirmationAge confirmation_age;
    ConfirmationReceiptGrace receipt_grace;
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
    std::array<std::atomic<uint64_t>,9> receipt_frontier_{};
    uint64_t last_unique_receipt_{};
    std::atomic<uint64_t> unique_receipt_gap_max_{},unique_receipt_gaps_{};
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
    unsigned slot_{},delay_{};
    bool delay_selected_{};
    uint64_t highest_{};
    Delivery delivery_;
    Failure failure_{};
    bool handoff_{};
    Counters counters_;
    void trace(unsigned kind,uint64_t index,unsigned slot,uint64_t raw,uint64_t qpc=0,uint64_t context=0){
        input_timeline.push(local_,round_,origin_+index,event_,kind,slot,qpc?qpc:timestamp(),raw,context);
    }
    bool fail(Failure f){if(failure_==Failure::none)failure_=f;return false;}
    bool owner(uint64_t thread){return bound_ && !failed() && thread==owner_?true:fail(Failure::owner);}
    bool identity(const RawInputConfirmation& p)const{
        return p.sender==peer_ && p.recipient==local_ && p.round==round_ && p.event==event_ && p.slot==3-slot_ &&
            p.frame>=origin_ && p.frame-origin_<Records;
    }
    bool archived(const RawInputConfirmation& p)const{
        if(!retired_.load(std::memory_order_acquire) || !identity(p))return false;
        const auto index=p.frame-origin_;const auto& t=truth_[index];
        // Only never-simulated delayed tail words may arrive after retirement.
        // Consumed frames still require identical archived truth.
        return t.seen?t.raw==p.raw:(delay_ && index>=delivery_.head() && index<delivery_.head()+delay_+1);
    }
public:
    bool bind(uint64_t local,uint64_t peer,uint64_t round,uint64_t event,uint64_t origin,
              unsigned slot,uint64_t thread,std::array<uint32_t,2> previous,const RawPredictionRoute* prior=nullptr,unsigned input_delay=0){
        if(bound_ || failed() || !local || !peer || local==peer || !round || !event || !thread ||
           slot<1 || slot>2 || origin>UINT64_MAX-Records || input_delay>2 || input_delay>=Records)return false;
        if(prior && (!prior->retired_.load(std::memory_order_acquire) || prior->local_!=local || prior->peer_!=peer ||
           prior->slot_!=slot || prior->depth_>=15))return false;
        for(auto* old=prior;old;old=old->previous_)if(old->round_==round && old->event_==event)return false;
        if(!delivery_.begin(local,round,3-slot,previous))return false;
        previous_=prior;depth_=prior?prior->depth_+1:0;
        local_=local;peer_=peer;round_=round;event_=event;origin_=origin;slot_=slot;owner_=thread;delay_=input_delay;
        // Protocol-agreed neutral prefix; sampled words are sent for N+delay.
        for(unsigned n=0;n<delay_;++n){truth_[n]={0,true};outgoing_[n]=0;(*published_times_)[n]=timestamp();}
        outgoing_count_.store(delay_,std::memory_order_release);
        bound_.store(true,std::memory_order_release);return true;
    }
    Submit submit(const RawInputConfirmation& p,uint64_t received=0){
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
        if(!accepted.seen){
            accepted={p.raw,true};
            const auto now=timestamp();
            if(last_unique_receipt_ && now>=last_unique_receipt_){
                const auto gap=now-last_unique_receipt_;++unique_receipt_gaps_;
                if(gap>unique_receipt_gap_max_.load(std::memory_order_relaxed))unique_receipt_gap_max_=gap;
            }
            last_unique_receipt_=now;
            // Receipt-time publication frontier, NOT remote simulation lead or
            // one-way latency. Positive means peer input index is further ahead.
            const auto delta=static_cast<int64_t>(p.frame-origin_+1)-static_cast<int64_t>(outgoing_count());
            const unsigned bin=delta<=-4?0:delta>=4?8:static_cast<unsigned>(delta+4);
            ++receipt_frontier_[bin];
        }
        const auto queued=timestamp();if(received)confirmation_age.add(ConfirmationAge::receive_to_enqueue,received,queued);
        if(received)trace(InputTimeline<>::received,p.frame-origin_,p.slot,p.raw,received);
        trace(InputTimeline<>::enqueued,p.frame-origin_,p.slot,p.raw,queued,outgoing_count());
        queue_[w%Queue]=p;queue_times_[w%Queue]=received?received:queued;write_.store(w+1,std::memory_order_release);receipt_grace.notify();return Submit::accepted;
    }
    bool pending_receipts()const{return write_.load(std::memory_order_acquire)!=read_.load(std::memory_order_acquire);}
    void write_receipt_frontier(std::ostream& out)const{
        out<<"receipt_frontier_schema=1\nreceipt_frontier_is_simulation_lead=0\n";
        for(unsigned i=0;i<9;++i)out<<"receipt_frontier_delta_"<<static_cast<int>(i)-4<<'='<<receipt_frontier_[i].load()<<'\n';
        out<<"receipt_unique_gap_samples="<<unique_receipt_gaps_.load()
           <<"\nreceipt_unique_gap_max_ticks="<<unique_receipt_gap_max_.load()<<'\n';
    }
    bool await_receipts(uint64_t thread){
        if(!owner(thread) || retired_.load(std::memory_order_acquire) || handoff_)return false;
        receipt_grace.wait([&]{return pending_receipts();});return true;
    }
    bool drain(uint64_t thread){
        if(!owner(thread))return false;
        // Do not change a raw word between the two native player consumers.
        if(delivery_.head() && !delivery_.frame(delivery_.head()-1)->complete){
            if(pending_receipts())trace(InputTimeline<>::deferred,delivery_.head()-1,slot_,0,0,1);
            return true;
        }
        for(unsigned n=0;n<Queue;++n){
            const unsigned r=read_.load(std::memory_order_relaxed),w=write_.load(std::memory_order_acquire);
            if(r==w)break;
            const auto p=queue_[r%Queue];const auto enqueued=queue_times_[r%Queue];read_.store(r+1,std::memory_order_release);
            ++counters_.drained;const auto index=p.frame-origin_;auto& truth=truth_[index];
            if(truth.seen){if(truth.raw!=p.raw)return fail(Failure::conflict);++counters_.duplicates;continue;}
            // A never-confirmed expired frame cannot be silently accepted.
            if(index<delivery_.first())return fail(Failure::delivery);
            truth={p.raw,true};(*arrival_times_)[index]={enqueued,timestamp()};
            trace(InputTimeline<>::drained,index,p.slot,p.raw,(*arrival_times_)[index].drained,delivery_.head());
            if(highest_<index+1)highest_=index+1;
            if(index<delivery_.head()){
                if(delivery_.confirm(local_,round_,index,p.raw)!=Delivery::Arrival::accepted)
                    return fail(Failure::delivery);
                confirmation_age.add(ConfirmationAge::receive_to_prediction,enqueued,timestamp());
                ++counters_.corrections;
            }
        }
        return true;
    }
    // Sample at simulation N, publish immediately for consumption N+delay.
    // A stalled clock never replaces its first sample. No sleeping or delayed send.
    unsigned input_delay()const{return delay_;}
    bool select_agreed_delay(uint64_t thread,unsigned delay){
        if(!owner(thread) || delay_selected_ || delay>2 || delay>=Records || delay_ || outgoing_count() || delivery_.head() ||
           write_.load() || read_.load() || handoff_)return false;
        delay_selected_=true;delay_=delay;
        for(unsigned n=0;n<delay_;++n){truth_[n]={0,true};outgoing_[n]=0;(*published_times_)[n]=timestamp();}
        outgoing_count_.store(delay_,std::memory_order_release);return true;
    }
    bool needs_local(uint64_t index)const{return index==delivery_.head() && index<Records-delay_ && outgoing_count()==index+delay_;}
    // The native filter consumes outgoing[N], never the future sample.
    uint64_t early_publications{},early_rechecks{};
    bool publish_local(uint64_t thread,uint64_t index,uint32_t raw,uint64_t sampled=0,uint64_t sample_end=0){
        if(!owner(thread) || handoff_ || index!=delivery_.head() || index>=Records-delay_)return false;
        index+=delay_;
        const auto count=outgoing_count();
        if(count==index+1){
            if(outgoing_[index]!=raw)return fail(Failure::conflict);
            ++early_rechecks;return true;
        }
        if(count!=index)return fail(Failure::overflow);
        outgoing_[index]=raw;(*published_times_)[index]=timestamp();
        if(sampled)confirmation_age.add(ConfirmationAge::sample_to_publish,sampled,(*published_times_)[index]);
        if(sample_end)confirmation_age.add(ConfirmationAge::sample_end_to_publish,sample_end,(*published_times_)[index]);
        if(sampled)trace(InputTimeline<>::sample_begin,index,slot_,raw,sampled);
        if(sample_end)trace(InputTimeline<>::sample_end,index,slot_,raw,sample_end);
        trace(InputTimeline<>::published,index,slot_,raw,(*published_times_)[index],delay_);
        outgoing_count_.store(static_cast<unsigned>(index+1),std::memory_order_release);
        raw_publication_signal.notify();++early_publications;return true;
    }
    bool append(uint64_t thread,uint32_t local_raw,const std::array<Filter,2>& filters,
                const std::array<uint32_t,10>& auxiliary,bool pending=false,std::optional<uint32_t> native_confirmed=std::nullopt){
        if(!owner(thread) || handoff_ || !drain(thread))return false;
        const auto index=delivery_.head();if(index>=Records)return fail(Failure::overflow);
        const auto published=outgoing_count();
        if(delay_){
            if(published!=index+delay_+1)return fail(Failure::overflow);
            if(outgoing_[index]!=local_raw)return fail(Failure::conflict);
        }else{
            if(published!=index && published!=index+1)return fail(Failure::overflow);
            if(published==index+1 && outgoing_[index]!=local_raw)return fail(Failure::conflict);
        }
        auto& t=truth_[index];
        if(native_confirmed){
            if(t.seen && t.raw!=*native_confirmed)return fail(Failure::conflict);
            t.raw=*native_confirmed;t.seen=true;
        }
        if(!delivery_.append(local_raw,t.seen?std::optional<uint32_t>(t.raw):std::nullopt,filters,auxiliary,pending))return false;
        if(published==index){
            outgoing_[index]=local_raw;(*published_times_)[index]=timestamp();
            outgoing_count_.store(static_cast<unsigned>(index+1),std::memory_order_release);
            raw_publication_signal.notify();
        }
        const auto received=(*arrival_times_)[index].enqueued;
        if(received)confirmation_age.add(ConfirmationAge::receive_to_prediction,received,timestamp());
        const auto* selected=delivery_.frame(index);
        trace(InputTimeline<>::selected,index,3-slot_,selected->raw[2-slot_],0,selected->confirmed?1:0);
        if(!pending)trace(InputTimeline<>::consumed,index,slot_,local_raw);
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
    bool mark_first_send(unsigned index,uint64_t& age,uint64_t begin=0){
        if(index>=outgoing_count())return false;
        const auto now=timestamp();uint64_t empty=0;
        if(!(*first_send_)[index].compare_exchange_strong(empty,now))return false;
        if(begin)trace(InputTimeline<>::send_begin,index,slot_,outgoing_[index],begin);
        trace(InputTimeline<>::send_end,index,slot_,outgoing_[index],now);
        age=now-(*published_times_)[index];
        if(begin){confirmation_age.add(ConfirmationAge::publish_to_send_begin,(*published_times_)[index],begin);confirmation_age.add(ConfirmationAge::send_call,begin,now);}
        return true;
    }
    bool outgoing(unsigned index,RawInputConfirmation& out)const{
        if(!bound_.load(std::memory_order_acquire) || index>=outgoing_count())return false;
        out={local_,peer_,round_,event_,origin_+index,slot_,outgoing_[index]};return true;
    }
    bool arrival_times(uint64_t thread,uint64_t index,uint64_t& enqueued,uint64_t& drained)const{
        if(thread!=owner_ || index>=Records || !truth_[index].seen)return false;
        enqueued=(*arrival_times_)[index].enqueued;drained=(*arrival_times_)[index].drained;return true;
    }
    void measure_confirmation(uint64_t thread,uint64_t index,uint64_t head){
        if(thread!=owner_ || index>=Records || !truth_[index].seen)return;
        const auto& times=(*arrival_times_)[index];
        trace(InputTimeline<>::admitted,index,3-slot_,truth_[index].raw,0,head);
        confirmation_age.observe(index,head,(*published_times_)[index],times.enqueued,times.drained,timestamp());
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
        if(!owner(thread) || !(delivery_.complete_frame(number,native) || fail(Failure::delivery)))return false;
        trace(InputTimeline<>::consumed,number,slot_,delivery_.frame(number)->raw[slot_-1]);
        return true;
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
    bool handoff_ready()const{return !failed() && handoff_ && highest_<=delivery_.head()+(delay_?delay_+1:0) && delivery_.handoff_ready() &&
        read_.load(std::memory_order_acquire)==write_.load(std::memory_order_acquire);}
    bool failed()const{return failure_!=Failure::none || delivery_.failed();}
    Failure failure()const{return failure_;}
    const Delivery& delivery()const{return delivery_;}
    const Counters& counters()const{return counters_;}
    // Owner-thread diagnostic snapshot; do not read mutable counters from the
    // transport worker while the simulation is updating this route.
    template<class Stream> bool report(uint64_t thread,Stream& out){
        if(!bound_ || thread!=owner_)return false;
        out<<"raw_prediction_input_delay_frames="<<delay_<<"\nraw_prediction_unused_local_tail="<<(outgoing_count()-delivery_.head())<<"\nraw_prediction_route_schema=1\nraw_prediction_failure="<<static_cast<unsigned>(failure_)
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
