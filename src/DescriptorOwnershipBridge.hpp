#pragma once
#include "ResourceDependencyCapture.hpp"
namespace sc6::research {
// Owner-thread-only bounded handoff. Entries remain owned until reset; callers
// must cancel on resource clear and reject stale or changed payload identity.
class DescriptorOwnershipBridge {
    struct Entry {ResourceReferenceLease lease;uintptr_t object{},control{},payload{};};
    std::array<Entry,2> entries_;
    unsigned count_{};DWORD owner_{};uint32_t first_frame_{};
public:
    unsigned count()const{return count_;}
    bool add(uintptr_t object,uintptr_t control,uintptr_t payload,uint32_t frame,
        ResourceReferenceLease::Dispose destroy_object,ResourceReferenceLease::Dispose destroy_control){
        if(!object || !control || !payload || (owner_ && owner_!=GetCurrentThreadId()))return false;
        for(unsigned i=0;i<count_;++i)if(entries_[i].control==control)
            return entries_[i].object==object && entries_[i].payload==payload;
        if(count_==entries_.size())return false;
        auto& entry=entries_[count_];
        if(!entry.lease.acquire(control,GetCurrentThreadId(),frame,destroy_object,destroy_control))return false;
        if(!count_){owner_=GetCurrentThreadId();first_frame_=frame;}
        entry.object=object;entry.control=control;entry.payload=payload;++count_;return true;
    }
    bool fresh(uint32_t frame)const{return count_ && owner_==GetCurrentThreadId() && frame>=first_frame_ && frame-first_frame_<=3;}
    template<class Validate> bool seed(uint32_t frame,Validate validate){
        if(!fresh(frame))return false;
        for(unsigned i=0;i<count_;++i){const auto& entry=entries_[i];if(!validate(entry.object,entry.payload))return false;}
        for(unsigned i=0;i<count_;++i)ResourceDependencyCapture::observe_owned(entries_[i].control,entries_[i].payload);
        return true;
    }
    bool reset(){
        if(owner_ && owner_!=GetCurrentThreadId())return false;
        for(auto& entry:entries_)if(entry.lease.active())entry.lease.release(0,true);
        count_=0;owner_=0;first_frame_=0;return true;
    }
};
}
