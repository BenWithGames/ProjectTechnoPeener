#pragma once
#include "NativeRollbackWindow.hpp"
namespace sc6::research {
// Simulation-thread storage for the combat adapter. Capture callbacks must bind
// their owned dependencies before returning success. This does not authorize load.
template<unsigned Capacity=4> class NativeRollingHistory {
    static_assert(Capacity>=4);
    using Pool=NativeSnapshotPool<Capacity>;
    Pool pool_;
    std::array<typename Pool::Handle,Capacity> handles_{};
    std::array<RollbackBoundaryKey,Capacity> keys_{};
    struct Guard {uintptr_t module{},root{},control{};std::array<uintptr_t,2> descriptors{};GeneratedBufferFingerprint buffer;};
    std::array<Guard,Capacity> guards_{};
    static bool valid_guard(const Guard& g)noexcept{
        if(!g.module || !g.root || !g.control || !g.buffer.valid)return false;
        __try {
            if(*reinterpret_cast<uintptr_t*>(g.module+0x470D0D0)!=g.descriptors[0] || *reinterpret_cast<uintptr_t*>(g.module+0x470D0F8)!=g.descriptors[1])return false;
            const auto descriptor=*reinterpret_cast<uintptr_t*>(g.root);
            if(descriptor!=g.descriptors[0] && descriptor!=g.descriptors[1])return false;
            if(*reinterpret_cast<uintptr_t*>(g.root+8)!=g.buffer.buffer || *reinterpret_cast<uintptr_t*>(g.root+0x430)!=g.buffer.buffer ||
               *reinterpret_cast<uintptr_t*>(g.root+0x438)!=g.control)return false;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
        return g.buffer.matches(GeneratedBufferFingerprint::read(g.buffer.buffer,g.buffer.size));
    }
    unsigned count_{};
    bool failed_{};
    std::optional<RollbackBoundaryKey> replay_next_;
    static bool descriptors(uintptr_t base,std::array<uintptr_t,2>& out) noexcept {
        __try {out={*reinterpret_cast<uintptr_t*>(base+0x470D0D0),*reinterpret_cast<uintptr_t*>(base+0x470D0F8)};return out[0]!=0;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
public:
    unsigned count()const{return count_;}
    bool failed()const{return failed_;}
    bool capture_owned(const RollbackBoundaryKey& key,unsigned oldest_required,uintptr_t base,uintptr_t argument,ReplayDependencySource& source,bool approved_profile=false){
        if(!base || !source.available())return false;
        return append(key,oldest_required,[&](auto& pool,const auto& handle,const auto& boundary){
            std::array<uintptr_t,2> before{},after{};
            if(!descriptors(base,before))return false;
            ResourceDependencyCapture references(ResourceDependencyCapture::native_object_dispose,ResourceDependencyCapture::native_control_dispose);
            if(!source.seed(source.context,boundary.frame))return false;
            const auto fingerprint=GeneratedBufferFingerprint::read(source.generated->buffer,source.generated->size);
            if(!fingerprint.valid)return false;
            const bool captured=pool.visit(handle,[&](NativeBoundarySnapshot& snapshot){
                return snapshot.capture_supported(base,argument,approved_profile) && snapshot.data->before_keys[0]==boundary.frame;
            });
            if(!captured || !descriptors(base,after) || before!=after || !references.covers(before[0],before[1]) ||
                !fingerprint.matches(GeneratedBufferFingerprint::read(source.generated->buffer,source.generated->size)))return false;
            if(!references.bind(pool,handle) || !source.generated->bind(pool,handle))return false;
            guards_[count_-1]={base,source.generated->root,source.generated->control,before,fingerprint};
            return valid_guard(guards_[count_-1]);
        });
    }
    template<class Capture> bool append(const RollbackBoundaryKey& key,unsigned oldest_required,Capture capture){
        if(failed_ || !key.same_round(key) || key.thread!=GetCurrentThreadId())return false;
        if(replay_next_ && (!replay_next_->same_round(key) || replay_next_->frame!=key.frame))return false;
        if(count_ && (!keys_[count_-1].same_round(key) || keys_[count_-1].frame==UINT32_MAX || key.frame!=keys_[count_-1].frame+1))return false;
        // Never evict a frame still required by an unresolved correction.
        if(count_==Capacity && keys_[0].frame>=oldest_required)return false;
        if(!pool_.bind_current_thread())return false;
        if(count_==Capacity){
            if(!pool_.retire(handles_[0])){failed_=true;return false;}
            for(unsigned i=1;i<count_;++i){handles_[i-1]=handles_[i];keys_[i-1]=keys_[i];guards_[i-1]=guards_[i];}
            --count_;
        }
        auto h=pool_.acquire(key.session,key.round,key.frame,NativeBoundarySnapshot::Mode::combat_capture_only,key.slot);
        if(!h){failed_=true;return false;}
        guards_[count_]={};handles_[count_]=*h;keys_[count_++]=key;
        bool ok=false;
        try {ok=capture(pool_,*h,key);} catch(...){failed_=true;return false;}
        if(!ok || !pool_.visit(*h,[](NativeBoundarySnapshot& s){return s.valid && !s.fault;})){failed_=true;return false;}
        replay_next_.reset();
        return true;
    }
    // The backend must complete and verify native loading inside restore.
    // Keep all references alive until it returns; only then retire the old
    // predicted suffix so replay can replace it with corrected captures.
    template<class Restore> bool rewind_for_replay(const RollbackBoundaryKey& key,Restore restore){
        if(failed_ || replay_next_ || key.thread!=GetCurrentThreadId())return false;
        unsigned first=count_;
        for(unsigned i=0;i<count_;++i)if(keys_[i].same_round(key) && keys_[i].frame==key.frame){first=i;break;}
        if(first==count_)return false;
        bool restored=false;
        try {restored=pool_.visit(handles_[first],restore);}catch(...){failed_=true;return false;}
        if(!restored){failed_=true;return false;}
        while(count_>first){
            const unsigned i=count_-1;
            if(!pool_.retire(handles_[i])){failed_=true;return false;}
            handles_[i]={};keys_[i]={};guards_[i]={};--count_;
        }
        replay_next_=key;return true;
    }
    template<class Visitor> bool visit(const RollbackBoundaryKey& key,Visitor visitor){
        if(failed_ || key.thread!=GetCurrentThreadId())return false;
        for(unsigned i=0;i<count_;++i)if(keys_[i].same_round(key) && keys_[i].frame==key.frame)return pool_.visit(handles_[i],visitor);
        return false;
    }
    bool assess_pair(const RollbackBoundaryKey& first,const RollbackBoundaryKey& next,NativeBoundarySnapshot::Rejection& reason){
        reason=NativeBoundarySnapshot::Rejection::frame;
        if(!first.same_round(next) || first.frame==UINT32_MAX || next.frame!=first.frame+1)return false;
        reason=NativeBoundarySnapshot::Rejection::invalid_capture;
        return visit(first,[&](NativeBoundarySnapshot& a){
            return visit(next,[&](NativeBoundarySnapshot& b){reason=a.structural_readiness(b);return true;});
        });
    }
    bool validate_owned(const RollbackBoundaryKey& key)const{
        if(failed_ || key.thread!=GetCurrentThreadId())return false;
        for(unsigned i=0;i<count_;++i)if(keys_[i].same_round(key) && keys_[i].frame==key.frame)return valid_guard(guards_[i]);
        return false;
    }
    // Explicit owner-thread teardown, including a failed capture. No recovery
    // to stock simulation is implied by releasing the references.
    bool release(){
        if(count_ && keys_[0].thread!=GetCurrentThreadId())return false;
        failed_=true;
        for(unsigned i=0;i<count_;++i)if(!pool_.release_dependencies(handles_[i]))return false;
        return true;
    }
};
}
