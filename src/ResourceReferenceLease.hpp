#pragma once
#include <Windows.h>
#include <climits>
#include <cstdint>
namespace sc6::research {
// A lease must be acquired while the caller already owns the native pair.
// Integration supplies audited disposal routines and a verified release thread.
// No destructor auto-release: hidden release on an arbitrary thread is forbidden.
class ResourceReferenceLease {
public:
    using Dispose=void(*)(uintptr_t);
    enum class Release { rejected, shared, object_destroyed, control_destroyed };
private:
    uintptr_t control_{};
    DWORD thread_{};
    uint64_t frame_{};
    Dispose object_dispose_{},control_dispose_{};
public:
    ResourceReferenceLease()=default;
    ResourceReferenceLease(const ResourceReferenceLease&)=delete;
    ResourceReferenceLease& operator=(const ResourceReferenceLease&)=delete;
    bool active()const noexcept{return control_!=0;}
    bool acquire(uintptr_t control,DWORD release_thread,uint64_t frame,Dispose object_dispose,Dispose control_dispose) noexcept {
        if(active() || !control || !release_thread || !object_dispose || !control_dispose)return false;
        auto strong=reinterpret_cast<volatile LONG*>(control+8);
        LONG count=InterlockedCompareExchange(strong,0,0);
        for(unsigned i=0;i<16;++i){
            if(count<=0 || count==LONG_MAX)return false;
            const auto prior=InterlockedCompareExchange(strong,count+1,count);
            if(prior==count){control_=control;thread_=release_thread;frame_=frame;object_dispose_=object_dispose;control_dispose_=control_dispose;return true;}
            count=prior;
        }
        return false;
    }
    // Takes exactly one already-owned reference, without incrementing it.
    bool adopt(uintptr_t control,DWORD thread,uint64_t frame,Dispose object_dispose,Dispose control_dispose){
        if(active() || !control || thread!=GetCurrentThreadId() || !object_dispose || !control_dispose)return false;
        control_=control;thread_=thread;frame_=frame;object_dispose_=object_dispose;control_dispose_=control_dispose;return true;
    }
    Release release(uint64_t frame,bool cancel=false){
        if(!active() || GetCurrentThreadId()!=thread_ || (!cancel && (frame<frame_ || frame-frame_<3)))return Release::rejected;
        const auto control=control_;const auto destroy_object=object_dispose_;const auto destroy_control=control_dispose_;
        // Clear before callbacks so reentry cannot double-release this lease.
        control_=0;
        const auto previous=InterlockedExchangeAdd(reinterpret_cast<volatile LONG*>(control+8),-1);
        if(previous!=1)return Release::shared;
        destroy_object(control);
        if(InterlockedExchangeAdd(reinterpret_cast<volatile LONG*>(control+12),-1)!=1)return Release::object_destroyed;
        destroy_control(control);
        return Release::control_destroyed;
    }
};
}
