#pragma once
#include "NetworkInputComposer.hpp"
#include "LateInputScheduler.hpp"
#include <atomic>
#include <array>
#include <cstdint>
namespace sc6 {
// One producer (network poll), one consumer (battle entry). Validation-only:
// these retained records are NOT native game snapshots and cannot be restored.
struct NetworkGameHandoff {
    struct Request {
        uint64_t local_nonce{},peer_nonce{};
        uint32_t slot{},frame{},round{},boundary{};
        NetworkInputComposer::Envelope input{};
    };
    struct Boundary {uint64_t generation{};uint32_t frame{},round{},event{};NetworkInputComposer::Envelope input{};};
    std::atomic<bool> late_enabled{false};
    LateInputScheduler late;
    std::array<Request,8> queue{};
    std::atomic<unsigned> write{0},read{0};
    std::atomic<uint64_t> local_nonce{0},peer_nonce{0};
    std::atomic<unsigned> slot{0};
    std::atomic<bool> drain_finished{false};
    std::atomic<unsigned> submitted{0},full{0},validated{0},rejected{0};
    std::array<std::atomic<unsigned>,6> rejection_reasons{}; // failed, session, round, age, history, input
    std::array<Boundary,16> history{}; // accessed only by the game thread
    uint32_t owner{},last_frame{};bool started{},failed{};
    bool bind(uint64_t local,uint64_t peer,unsigned side){
        if(!local || !peer || side<1 || side>2)return false;
        if(local_nonce.load(std::memory_order_acquire))return local_nonce==local && peer_nonce==peer && slot==side;
        peer_nonce.store(peer);slot.store(side);local_nonce.store(local,std::memory_order_release);return true;
    }
    bool publish(const Request& request){
        if(request.local_nonce!=local_nonce.load(std::memory_order_acquire) || request.peer_nonce!=peer_nonce || request.slot!=slot)return false;
        const unsigned w=write.load(std::memory_order_relaxed),r=read.load(std::memory_order_acquire);
        if(w-r==queue.size()){++full;return false;}
        queue[w%queue.size()]=request;write.store(w+1,std::memory_order_release);++submitted;return true;
    }
    using Validated=void(*)(void*,const Request&,const Boundary&);
    void boundary(uint64_t session,uint32_t thread,const Boundary& now,Validated callback=nullptr,void* context=nullptr){
        if(!thread || !session || session!=local_nonce.load(std::memory_order_acquire))return;
        if(started && (owner!=thread || last_frame==UINT32_MAX || now.frame!=last_frame+1u))failed=true;
        owner=thread;last_frame=now.frame;started=true;
        history[now.frame%history.size()]=now;
        const bool scheduling=late_enabled.load() && !failed && late.enter(now.generation,now.round,now.event,now.frame,slot.load());
        // Fixed work budget; never wait for network traffic or allocate here.
        for(unsigned budget=0;budget<queue.size();++budget){
            const unsigned r=read.load(std::memory_order_relaxed),w=write.load(std::memory_order_acquire);
            if(r==w)break;
            const auto request=queue[r%queue.size()];read.store(r+1,std::memory_order_release);
            const auto& saved=history[request.frame%history.size()];
            unsigned reason=6;
            if(failed)reason=0;
            else if(request.local_nonce!=session || request.peer_nonce!=peer_nonce || request.slot!=slot)reason=1;
            else if(!request.round || now.round!=request.round || now.event!=request.boundary)reason=2;
            else if(request.frame>=now.frame || now.frame-request.frame>3)reason=3;
            else if(saved.frame!=request.frame || !saved.generation || saved.generation!=now.generation || saved.round!=request.round || saved.event!=request.boundary)reason=4;
            else if(saved.input!=request.input)reason=5;
            if(scheduling && request.local_nonce==session && request.peer_nonce==peer_nonce && request.slot==slot &&
               request.round==now.round && request.boundary==now.event && request.frame<now.frame &&
               now.frame-request.frame<history.size() && saved.frame==request.frame && saved.generation==now.generation &&
               saved.round==request.round && saved.event==request.boundary && saved.input==request.input){
                const unsigned word=(3-request.slot-1)*2;
                late.receive(request.frame,request.input[word],request.input[word+1]);
            }
            if(scheduling && request.local_nonce==session && request.peer_nonce==peer_nonce && request.slot==slot &&
               request.round==now.round && request.boundary==now.event && request.frame<now.frame &&
               now.frame-request.frame>=history.size())++late.expired;
            if(reason==6){++validated;if(callback)callback(context,request,saved);}else {++rejection_reasons[reason];++rejected;}

        }
        if(scheduling)late.decide();
    }
};
inline NetworkGameHandoff network_game_handoff;
}
