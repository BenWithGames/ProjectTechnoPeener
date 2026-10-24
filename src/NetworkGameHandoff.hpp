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
    mutable std::atomic<unsigned> readers{0};
    std::atomic<bool> retiring{false};
    std::atomic<uint64_t> retired_session{0},retirements{0},retirement_waits{0},discarded_requests{0};
    std::atomic<unsigned> retirement_check{0};
    struct ReadGuard {
        const NetworkGameHandoff& state;bool entered{};
        explicit ReadGuard(const NetworkGameHandoff& s):state(s){
            state.readers.fetch_add(1);
            entered=!state.retiring.load();
        }
        ~ReadGuard(){state.readers.fetch_sub(1);}
    };
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
    struct Confirmation {Boundary boundary{};NetworkInputComposer::Envelope input{};bool valid{};};
    std::array<Confirmation,16> confirmations{}; // owner-thread only; permits remote prediction differences
    bool confirmation_conflict{};
    uint32_t owner{},last_frame{};bool started{},failed{};
    bool bind(uint64_t local,uint64_t peer,unsigned side){
        if(!local || !peer || side<1 || side>2 || retiring.load() || local==retired_session.load())return false;
        if(local_nonce.load(std::memory_order_acquire))return local_nonce==local && peer_nonce==peer && slot==side;
        peer_nonce.store(peer);slot.store(side);local_nonce.store(local,std::memory_order_release);return true;
    }
    bool publish(const Request& request){
        if(retiring.load() || !request.local_nonce || request.local_nonce!=local_nonce.load(std::memory_order_acquire) || request.peer_nonce!=peer_nonce || request.slot!=slot)return false;
        const unsigned w=write.load(std::memory_order_relaxed),r=read.load(std::memory_order_acquire);
        if(w-r==queue.size()){++full;return false;}
        queue[w%queue.size()]=request;write.store(w+1,std::memory_order_release);++submitted;return true;
    }
    using Validated=void(*)(void*,const Request&,const Boundary&);
    // Producer-thread only, after native routes released and the report was saved.
    // Never reset live reader storage or reinterpret a failed match as clean.
    bool retire_completed(uint64_t session,uint64_t released,uint64_t saved){
        retirement_check=1;
        if(!session || released!=session || saved!=session)return false;
        const auto bound=local_nonce.load();
        retirement_check=2;if(bound && bound!=session)return false;
        retirement_check=3;if(late_enabled.load())return false;
        retiring=true;
        retirement_check=4;if(readers.load()){++retirement_waits;return false;}
        if(retired_session.load()!=session){
            discarded_requests.fetch_add(write.load()-read.load());
            queue={};history={};confirmations={};
            write=0;read=0;drain_finished=false;submitted=0;full=0;validated=0;rejected=0;
            for(auto& reason:rejection_reasons)reason=0;
            confirmation_conflict=false;owner=last_frame=0;started=failed=false;
            peer_nonce=0;slot=0;local_nonce=0;retired_session=session;++retirements;
        }
        retirement_check=0;retiring=false;return true;
    }
    bool confirmed(uint64_t session,uint64_t peer,uint32_t thread,const Boundary& key,Confirmation& out)const{
        ReadGuard guard(*this);if(!guard.entered)return false;
        if(failed || confirmation_conflict || !started || thread!=owner || session!=local_nonce.load(std::memory_order_acquire) || peer!=peer_nonce.load())return false;
        const auto& c=confirmations[key.frame%confirmations.size()];
        if(!c.valid || c.boundary.frame!=key.frame || c.boundary.generation!=key.generation ||
            c.boundary.round!=key.round || c.boundary.event!=key.event)return false;
        out=c;return true;
    }
    void boundary(uint64_t session,uint32_t thread,const Boundary& now,Validated callback=nullptr,void* context=nullptr){
        ReadGuard guard(*this);if(!guard.entered)return;
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
            // Request envelopes are already composed by the authenticated
            // transport. Only the remote pair may differ from this PC's history.
            // Do not weaken the legacy exact-input callback below.
            if(reason==5 || reason==6){
                auto local=request.input;const unsigned remote=(2-request.slot)*2;
                local[remote]=saved.input[remote];local[remote+1]=saved.input[remote+1];
                if(local==saved.input){
                    auto& c=confirmations[request.frame%confirmations.size()];
                    const bool same=c.valid && c.boundary.frame==saved.frame && c.boundary.generation==saved.generation &&
                        c.boundary.round==saved.round && c.boundary.event==saved.event;
                    if(same && c.input!=request.input)confirmation_conflict=true;
                    else c={saved,request.input,true};
                }
            }
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
