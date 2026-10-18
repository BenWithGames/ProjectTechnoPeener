#pragma once
#include "NetworkGameHandoff.hpp"
#include "CombatUpdateAdapter.hpp"
#include "SchedulerInputSink.hpp"
namespace sc6::research {
// Single authenticated transport producer, single simulation-thread consumer.
// Unlike observer history, draining does not require a battle-frame advance.
template<class Session,unsigned Capacity=16> class SchedulerConfirmationQueue {
    Session& session_;
    std::array<NetworkGameHandoff::Request,Capacity> queue_{};
    std::atomic<unsigned> write_{0},read_{0};
    std::atomic<bool> bound_{false};
    uint64_t local_{},peer_{};unsigned round_{},event_{},slot_{},origin_{},owner_{};
    bool failed_{};
    // Retired queues remain alive with their routes. Archive only authoritative
    // remote words; local/auxiliary words are not remote correction authority.
    struct Truth {uint32_t held{},pressed{};bool seen{};};
    std::array<Truth,7200> truth_{};
    unsigned highest_{},retired_count_{},depth_{};
    const SchedulerConfirmationQueue* previous_{};
    std::atomic<bool> retired_{};
    std::atomic<uint64_t> retired_packets_{};
    std::atomic_flag producer_gate_=ATOMIC_FLAG_INIT;
    bool identity(const NetworkGameHandoff::Request& input)const{
        return input.local_nonce==local_ && input.peer_nonce==peer_ && input.round==round_ &&
            input.boundary==event_ && input.slot==slot_;
    }
    bool archived(const NetworkGameHandoff::Request& input)const{
        if(!retired_.load(std::memory_order_acquire) || !identity(input) || input.frame<origin_)return false;
        const auto index=input.frame-origin_,off=(2-slot_)*2;
        return index<retired_count_ && truth_[index].seen && truth_[index].held==input.input[off] &&
            truth_[index].pressed==input.input[off+1];
    }
public:
    explicit SchedulerConfirmationQueue(Session& session):session_(session){}
    bool bind(uint64_t local,uint64_t peer,unsigned round,unsigned event,unsigned slot,unsigned origin,
        const SchedulerConfirmationQueue* previous=nullptr){
        if(bound_ || !local || !peer || !round || slot<1 || slot>2)return false;
        if(previous && (!previous->retired_.load(std::memory_order_acquire) || previous->local_!=local ||
            previous->peer_!=peer || previous->slot_!=slot || previous->depth_>=15))return false;
        for(auto* old=previous;old;old=old->previous_)if(old->round_==round && old->event_==event)return false;
        previous_=previous;depth_=previous?previous->depth_+1:0;
        local_=local;peer_=peer;round_=round;event_=event;slot_=slot;origin_=origin;owner_=GetCurrentThreadId();
        bound_.store(true,std::memory_order_release);return true;
    }
    SchedulerPublish submit(const NetworkGameHandoff::Request& input){
        if(!bound_.load(std::memory_order_acquire))return SchedulerPublish::rejected;
        if(!identity(input)){
            for(auto* old=previous_;old;old=old->previous_)if(old->archived(input)){++retired_packets_;return SchedulerPublish::retired;}
            return SchedulerPublish::rejected;
        }
        if(producer_gate_.test_and_set(std::memory_order_acquire))return SchedulerPublish::full;
        struct Unlock {std::atomic_flag& gate;~Unlock(){gate.clear(std::memory_order_release);}} unlock{producer_gate_};
        if(retired_.load(std::memory_order_acquire)){
            if(!archived(input))return SchedulerPublish::rejected;
            ++retired_packets_;return SchedulerPublish::retired;
        }
        if(input.frame<origin_ || input.frame-origin_>=truth_.size())return SchedulerPublish::rejected;
        const unsigned w=write_.load(std::memory_order_relaxed),r=read_.load(std::memory_order_acquire);
        if(w-r==Capacity)return SchedulerPublish::full;
        queue_[w%Capacity]=input;write_.store(w+1,std::memory_order_release);return SchedulerPublish::accepted;
    }
    bool publish(const NetworkGameHandoff::Request& input){return submit(input)==SchedulerPublish::accepted;}
    static SchedulerPublish send(void* context,const NetworkGameHandoff::Request& input){return static_cast<SchedulerConfirmationQueue*>(context)->submit(input);}
    bool drain(){
        if(!bound_.load(std::memory_order_acquire) || failed_ || owner_!=GetCurrentThreadId()){failed_=true;return false;}
        for(unsigned budget=0;budget<Capacity;++budget){
            const unsigned r=read_.load(std::memory_order_relaxed),w=write_.load(std::memory_order_acquire);
            if(r==w)break;
            const auto input=queue_[r%Capacity];read_.store(r+1,std::memory_order_release);
            const auto index=input.frame-origin_,off=(2-slot_)*2;
            auto& truth=truth_[index];
            if(truth.seen){
                if(truth.held!=input.input[off] || truth.pressed!=input.input[off+1]){failed_=true;return false;}
                continue;
            }
            const auto result=session_.receive(local_,round_,uint64_t(input.frame-origin_),CombatFrameInput::remote(input.input,3-slot_));
            if(result!=Session::Arrival::accepted && result!=Session::Arrival::duplicate){failed_=true;return false;}
            truth={input.input[off],input.input[off+1],true};
            if(highest_<index+1)highest_=index+1;
        }
        return true;
    }
    static bool service(void* context){return static_cast<SchedulerConfirmationQueue*>(context)->drain();}
    // Owner thread only, after settlement. Never wait for the producer, reset
    // its ring, or destroy the old queue. Retry after any in-flight producer.
    bool retire(){
        if(!bound_ || failed_ || owner_!=GetCurrentThreadId())return false;
        if(retired_.load(std::memory_order_acquire))return true;
        if(producer_gate_.test_and_set(std::memory_order_acquire))return false;
        struct Unlock {std::atomic_flag& gate;~Unlock(){gate.clear(std::memory_order_release);}} unlock{producer_gate_};
        if(!drain() || !session_.handoff_ready() || !session_.head() || session_.head()!=highest_)return false;
        for(unsigned i=0;i<highest_;++i)if(!truth_[i].seen)return false;
        retired_count_=highest_;retired_.store(true,std::memory_order_release);return true;
    }
    bool retired()const{return retired_.load(std::memory_order_acquire);}
    uint64_t retired_packets()const{return retired_packets_.load(std::memory_order_relaxed);}
};
}
