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
public:
    explicit SchedulerConfirmationQueue(Session& session):session_(session){}
    bool bind(uint64_t local,uint64_t peer,unsigned round,unsigned event,unsigned slot,unsigned origin){
        if(bound_ || !local || !peer || !round || slot<1 || slot>2)return false;
        local_=local;peer_=peer;round_=round;event_=event;slot_=slot;origin_=origin;owner_=GetCurrentThreadId();
        bound_.store(true,std::memory_order_release);return true;
    }
    SchedulerPublish submit(const NetworkGameHandoff::Request& input){
        if(!bound_.load(std::memory_order_acquire) || input.local_nonce!=local_ || input.peer_nonce!=peer_ ||
            input.round!=round_ || input.boundary!=event_ || input.slot!=slot_ || input.frame<origin_)return SchedulerPublish::rejected;
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
            const auto result=session_.receive(local_,round_,uint64_t(input.frame-origin_),CombatFrameInput::remote(input.input,3-slot_));
            if(result!=Session::Arrival::accepted && result!=Session::Arrival::duplicate){failed_=true;return false;}
        }
        return true;
    }
    static bool service(void* context){return static_cast<SchedulerConfirmationQueue*>(context)->drain();}
};
}
