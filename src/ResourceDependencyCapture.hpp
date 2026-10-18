#pragma once
#include "ResourceReferenceLease.hpp"
#include <array>
#include <atomic>
namespace sc6::research {
namespace dependency_events {
using Owned=void(*)(uintptr_t,uintptr_t,uintptr_t);
inline std::atomic<Owned> owned{};
inline std::atomic<void(*)()> clear{};
inline std::atomic<void(*)(uintptr_t,uintptr_t,bool)> scoped_clear{};
}
// Scoped collection from verified native ownership callbacks during save.
// Raw identities recorded here are backed by owned references until teardown.
class ResourceDependencyCapture {
    inline static thread_local ResourceDependencyCapture* current_{};
    std::array<ResourceReferenceLease,8> leases_;
    std::array<uintptr_t,8> controls_{},payloads_{};
    ResourceReferenceLease::Dispose object_dispose_,control_dispose_;
    unsigned count_{};bool failed_{};
public:
    static bool active(){return current_!=nullptr;}
    unsigned count()const{return count_;}
    static void native_object_dispose(uintptr_t control){
        const auto table=*reinterpret_cast<uintptr_t**>(control);
        reinterpret_cast<void(*)(uintptr_t)>(table[0])(control);
    }
    static void native_control_dispose(uintptr_t control){
        const auto table=*reinterpret_cast<uintptr_t**>(control);
        reinterpret_cast<uintptr_t(*)(uintptr_t,unsigned)>(table[1])(control,1);
    }
    ResourceDependencyCapture(ResourceReferenceLease::Dispose object_dispose,ResourceReferenceLease::Dispose control_dispose)
        :object_dispose_(object_dispose),control_dispose_(control_dispose){
        if(current_ || !object_dispose || !control_dispose){failed_=true;return;}
        current_=this;
    }
    ResourceDependencyCapture(const ResourceDependencyCapture&)=delete;
    ResourceDependencyCapture& operator=(const ResourceDependencyCapture&)=delete;
    ~ResourceDependencyCapture(){
        if(current_==this)current_=nullptr;
        for(auto& lease:leases_)if(lease.active())lease.release(0,true);
    }
    static void observe_owned(uintptr_t control,uintptr_t payload){
        auto* scope=current_;if(!scope || scope->failed_)return;
        if(!control || !payload){scope->failed_=true;return;}
        for(unsigned i=0;i<scope->count_;++i)if(scope->controls_[i]==control){
            if(scope->payloads_[i]!=payload)scope->failed_=true;
            return;
        }
        const auto i=scope->count_;
        if(i==scope->leases_.size() || !scope->leases_[i].acquire(control,GetCurrentThreadId(),0,
            scope->object_dispose_,scope->control_dispose_)){scope->failed_=true;return;}
        scope->controls_[i]=control;scope->payloads_[i]=payload;++scope->count_;
    }
    bool covers(uintptr_t first,uintptr_t second)const{
        if(failed_ || !first)return false;
        bool found_first=false,found_second=!second;
        for(unsigned i=0;i<count_;++i){found_first|=payloads_[i]==first;found_second|=payloads_[i]==second;}
        return found_first && found_second;
    }
    template<class Pool,class Handle> bool bind(Pool& pool,const Handle& handle){
        if(failed_ || !count_)return false;
        for(unsigned i=0;i<count_;++i)if(!pool.retain_dependency(handle,controls_[i],object_dispose_,control_dispose_)){
            pool.release_dependencies(handle);return false;
        }
        return true;
    }
};
}
