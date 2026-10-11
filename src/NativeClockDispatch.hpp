#pragma once
#include "NativeClockAccess.hpp"
namespace sc6::research {
// Routes a whole native dispatcher call. The admission callback must reserve
// one matching battle-boundary update before returning true; it must NOT run
// the battle update from this earlier clock boundary.
template<class Clock=NativeClockAccess<>> class NativeClockDispatch {
public:
    using Original=void(*)(uintptr_t);
    using Admit=bool(*)(void*,const typename Clock::State&) noexcept;
    using Notify=void(*)(void*,typename Clock::Result) noexcept;
private:
    Clock clock_;
    uintptr_t object_{};
    void* context_{};
    Admit admit_{};
    Notify notify_{};
    bool active_{},entered_{},failed_{};
public:
    bool bind(uintptr_t base,uintptr_t object,DWORD owner,bool image_verified,
              bool bilateral_private,bool boundaries_owned,void* context,Admit admit,Notify notify){
        if(active_ || failed_ || !admit || !notify)return false;
        if(!clock_.bind(base,object,owner,image_verified,bilateral_private,boundaries_owned))return false;
        object_=object;context_=context;admit_=admit;notify_=notify;active_=true;return true;
    }
    bool active()const{return active_;}
    bool at_boundary(unsigned identity,unsigned frame){return active_ && !entered_ && !failed_ && clock_.at_boundary(identity,frame);}
    bool restart_required()const{return failed_ || clock_.restart_required();}
    void stop()noexcept{failed_=true;}
    void run(uintptr_t object,Original original) {
        if(!active_ || object!=object_){original(object);return;}
        using R=typename Clock::Result;
        if(failed_)return;
        if(entered_){failed_=true;return;}
        entered_=true;
        auto result=clock_.service();
        if(result==R::ready || result==R::native_wait){
            // Do not consume a scheduler reservation while stock SC6 is waiting.
            const bool admit=result==R::ready && admit_(context_,clock_.last_serviced_state());
            if(!failed_)result=clock_.finish(admit);
        }
        if(failed_ || result==R::restart_required || result==R::unbound){
            failed_=true;result=R::restart_required;
        }
        notify_(context_,result);
        entered_=false;
        // Never call original here, including after partial servicing/failure.
    }
};
}
