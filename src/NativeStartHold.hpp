#pragma once
#include "NativeClockAccess.hpp"
namespace sc6::research {
// Bound only at the agreed clock frontier. Continues native servicing while
// withholding advancement. Does not itself authorize either peer to start.
template<class Clock=NativeClockAccess<>> class NativeStartHold {
    Clock clock_;
    uintptr_t object_{};
    unsigned identity_{},frame_{};
    uint64_t deadline_{};
    unsigned owner_{};
    bool bound_{},failed_{},retired_{};
public:
    uint64_t serviced{};
    bool bind(uintptr_t base,uintptr_t object,unsigned owner,unsigned identity,unsigned frame,
        uint64_t now,uint64_t timeout,bool image_verified,bool bilateral_private,bool boundary_owned){
        if(bound_ || failed_ || !timeout || timeout>10000 || now>UINT64_MAX-timeout)return false;
        if(!clock_.bind(base,object,owner,image_verified,bilateral_private,boundary_owned) || !clock_.at_boundary(identity,frame))return false;
        object_=object;identity_=identity;frame_=frame;deadline_=now+timeout;owner_=owner;bound_=true;return true;
    }
    bool retire(){if(!bound_ || GetCurrentThreadId()!=owner_)return false;retired_=true;return true;}
    bool matches(uintptr_t object)const{return bound_ && !retired_ && object==object_;}
    bool failed()const{return failed_;}
    bool ready(uintptr_t object,uint64_t now){return bound_ && !retired_ && !failed_ && object==object_ && now<deadline_ && clock_.at_boundary(identity_,frame_);}
    template<class Exchange> bool announce(Exchange& exchange,uint64_t ticket,uintptr_t object,uint64_t now){
        return ready(object,now) && exchange.report_local_hold(ticket);
    }
    bool run(uintptr_t object,uint64_t now){
        if(!bound_ || retired_ || failed_ || object!=object_ || now>=deadline_){failed_=true;return false;}
        if(!clock_.at_boundary(identity_,frame_)){failed_=true;return false;}
        using R=typename Clock::Result;
        const auto result=clock_.service();++serviced;
        if(result!=R::ready && result!=R::native_wait){failed_=true;return false;}
        const auto finished=clock_.finish(false);
        if((finished!=R::withheld && finished!=R::native_wait) || !clock_.at_boundary(identity_,frame_)){failed_=true;return false;}
        return true;
    }
};
}
