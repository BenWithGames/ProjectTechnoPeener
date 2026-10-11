#pragma once
#include "ResourceReferenceLease.hpp"
#include <array>
namespace sc6::research {
// Called exclusively from one verified game thread. Native disposal executes
// outside observer/report locks. No timer-worker or DLL-unload release is allowed.
template<unsigned Capacity=16> class ResourceLeaseBoundary {
    struct Slot {ResourceReferenceLease lease;uint64_t frame{};};
    std::array<Slot,Capacity> slots_;
    DWORD owner_{};
    uint64_t frame_{};
    bool accepting_{},have_frame_{};
    unsigned acquired_{},released_{},cancelled_{};
public:
    bool begin(){
        if(pending())return false;
        owner_=GetCurrentThreadId();frame_=0;have_frame_=false;accepting_=true;
        acquired_=released_=cancelled_=0;return true;
    }
    unsigned pending()const {unsigned result=0;for(const auto& s:slots_)if(s.lease.active())++result;return result;}
    unsigned acquired()const{return acquired_;}
    unsigned released()const{return released_;}
    unsigned cancelled()const{return cancelled_;}
    bool capture(uintptr_t control,ResourceReferenceLease::Dispose object_dispose,ResourceReferenceLease::Dispose control_dispose){
        if(!accepting_ || !have_frame_ || GetCurrentThreadId()!=owner_)return false;
        for(auto& s:slots_)if(!s.lease.active()){
            if(!s.lease.acquire(control,owner_,frame_,object_dispose,control_dispose))return false;
            s.frame=frame_;++acquired_;return true;
        }
        return false;
    }
    bool boundary(uint64_t frame){
        if(GetCurrentThreadId()!=owner_)return false;
        if(have_frame_ && frame<frame_){cancel();return false;}
        frame_=frame;have_frame_=true;
        for(auto& s:slots_)if(s.lease.active() && frame-s.frame>=3){
            if(s.lease.release(frame)!=ResourceReferenceLease::Release::rejected)++released_;
        }
        return true;
    }
    bool cancel(){
        if(GetCurrentThreadId()!=owner_)return false;
        accepting_=false;
        for(auto& s:slots_)if(s.lease.active()){
            if(s.lease.release(frame_,true)!=ResourceReferenceLease::Release::rejected){++released_;++cancelled_;}
        }
        return pending()==0;
    }
};
}
