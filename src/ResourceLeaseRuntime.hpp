#pragma once
#include "ResourceOwnedQueue.hpp"
#include "ResourceReferenceLease.hpp"
#include "ResourceLeaseService.hpp"
namespace sc6::research::lease_runtime {
inline ResourceOwnedQueue<16> queue;
inline std::array<ResourceReferenceLease,16> leases;
inline std::atomic<bool> used{},enabled{};
inline std::atomic<unsigned> acquired{},released{},cancelled{},pending{},wrong_factory_thread{},transferred{};
inline std::atomic<unsigned> gate_disabled{},gate_no_owner{},gate_queue_rejected{},first_stop{};
inline std::atomic<uint32_t> reset_from{},reset_to{};
// First reason wins: 1 external stop, 2 resource clear, 3 frame reset, 4 service cancellation.
inline void note_stop(unsigned reason){unsigned none=0;first_stop.compare_exchange_strong(none,reason);}
inline uintptr_t module{};
inline bool busy{},have_frame{};
inline uint32_t last_frame{};
// Optional diagnostic runs only while each reference is still owned.
inline void (*observe_held)(uint32_t){};
inline void dispose_object(uintptr_t control){reinterpret_cast<void(*)(uintptr_t)>(module+0x29B9940)(control);}
inline void dispose_control(uintptr_t control){reinterpret_cast<uintptr_t(*)(uintptr_t,unsigned)>(module+0x216D8C0)(control,1);}
inline bool step(uint32_t frame,bool cancel){
    if(GetCurrentThreadId()!=lease_service::owner.load() || busy)return false;
    busy=true;
    if(cancel)note_stop(4);
    if(have_frame && frame<last_frame){
        if(!first_stop.load()){reset_from=last_frame;reset_to=frame;}
        note_stop(3);
    }
    cancel=cancel || !enabled.load() || (have_frame && frame<last_frame);
    if(cancel){enabled=false;queue.close();}
    for(auto& lease:leases)if(!lease.active()){
        const auto control=queue.pop();if(!control)break;
        // Empty slot, current owner and fixed nonnull callbacks make adoption valid.
        lease.adopt(control,GetCurrentThreadId(),frame,dispose_object,dispose_control);++transferred;
    }
    unsigned held=0;
    for(auto& lease:leases)if(lease.active()){
        if(!cancel && observe_held)observe_held(frame);
        if(lease.release(frame,cancel)!=ResourceReferenceLease::Release::rejected){++released;if(cancel)++cancelled;}
        else ++held;
    }
    acquired=queue.total();pending=held+queue.pending();
    have_frame=true;last_frame=frame;busy=false;
    return !cancel || pending.load()==0;
}
inline bool prepare(uintptr_t base){
    if(used.exchange(true))return false;
    module=base;if(!queue.open())return false;
    enabled=true;lease_service::handler=step;return true;
}
inline void stop(){note_stop(1);enabled=false;queue.close();lease_service::request_cancel();}
inline void before_clear(){
    if(!used.load() || !queue.total())return;
    note_stop(2);stop();if(GetCurrentThreadId()==lease_service::owner.load())step(last_frame,true);
}
inline bool capture(uintptr_t control){
    if(!enabled.load()){++gate_disabled;return false;}
    if(!lease_service::owner.load()){++gate_no_owner;return false;}
    if(GetCurrentThreadId()!=lease_service::owner.load())++wrong_factory_thread;
    const bool accepted=queue.push(control);if(!accepted)++gate_queue_rejected;return accepted;
}
}
