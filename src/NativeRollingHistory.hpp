#pragma once
#include "CameraSlotEvidence.hpp"
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
    NativeEffectSession* effects_{};
    std::optional<RollbackBoundaryKey> replay_next_;
    static bool descriptors(uintptr_t base,std::array<uintptr_t,2>& out) noexcept {
        __try {out={*reinterpret_cast<uintptr_t*>(base+0x470D0D0),*reinterpret_cast<uintptr_t*>(base+0x470D0F8)};return out[0]!=0;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool compact_births(){
        if(!effects_ || !count_)return true;
        unsigned first=UINT_MAX;
        for(unsigned i=0;i<count_;++i)if(!pool_.visit(handles_[i],[&](NativeBoundarySnapshot& s){
            if(!s.effects || s.effects->owner!=effects_ || !s.effects->valid)return false;
            first=(std::min)(first,s.effects->birth_cursor);return true;
        }))return false;
        bool compacted=false;
        if(!effects_->compact_birth_history(first,count_,compacted))return false;
        if(!compacted)return true; // Busy optional maintenance: preserve every cursor.
        for(unsigned i=0;i<count_;++i)if(!pool_.visit(handles_[i],[&](NativeBoundarySnapshot& s){
            s.effects->birth_cursor-=first;return true;
        }))return false;
        return true;
    }
public:
    // Owner-thread evidence, copied to report atomics before teardown.
    std::array<uint64_t,10> capture_diagnostic{};
    CameraSlotEvidence capture_slot_evidence;
    unsigned count()const{return count_;}
    bool failed()const{return failed_;}
    bool discard_released_storage(){return failed_ && pool_.discard_drained_storage();}
    bool capture_owned(const RollbackBoundaryKey& key,unsigned oldest_required,uintptr_t base,uintptr_t argument,ReplayDependencySource& source,bool approved_profile=false,bool general_restore=false){
        capture_diagnostic={1,key.frame,oldest_required,count_,0,0,0,0,0,0};
        if(failed_ || !key.same_round(key) || key.thread!=GetCurrentThreadId() || !base || !source.available())return false;
        capture_diagnostic[0]=2;
        if(effects_ && effects_!=source.effects)return false;
        if(general_restore && source.effects){
            capture_diagnostic[0]=3;
            if(!source.effects->begin(base,key.session,key.round,source.effect_references))return false;
            effects_=source.effects;
        }
        return append(key,oldest_required,[&](auto& pool,const auto& handle,const auto& boundary){
            std::array<uintptr_t,2> before{},after{};
            capture_diagnostic[0]=4;
            if(!descriptors(base,before))return false;
            ResourceDependencyCapture references(ResourceDependencyCapture::native_object_dispose,ResourceDependencyCapture::native_control_dispose);
            capture_diagnostic[0]=5;
            if(!source.seed(source.context,boundary.frame))return false;
            capture_diagnostic[0]=6;
            const auto fingerprint=GeneratedBufferFingerprint::read(source.generated->buffer,source.generated->size);
            if(!fingerprint.valid)return false;
            capture_diagnostic[0]=7;
            const bool captured=pool.visit(handle,[&](NativeBoundarySnapshot& snapshot){
                if(general_restore && effects_){
                    snapshot.effects=std::make_unique<NativeEffectSession::Binding>();
                    capture_diagnostic[0]=8;
                    if(!effects_->capture(*snapshot.effects))return false;
                    snapshot.data->external_wind_lifecycle=true;
                }
                capture_diagnostic[0]=9;
                const bool saved=(general_restore?snapshot.capture_general(base,argument):snapshot.capture_supported(base,argument,approved_profile)) && snapshot.data->before_keys[0]==boundary.frame;
                capture_diagnostic[4]=snapshot.capture_stage;
                capture_diagnostic[5]=snapshot.exception;
                capture_diagnostic[6]=snapshot.fault;
                capture_diagnostic[7]=snapshot.valid;
                capture_diagnostic[8]=snapshot.data->before_keys[0];
                capture_diagnostic[9]=saved;
                if(!saved){
                    if(snapshot.data->camera_slots[0].enabled)capture_slot_evidence.capture(snapshot.data->camera_slots[0],snapshot.data->saved,snapshot.data->camera_slots[0],snapshot.data->saved);
                    return false;
                }
                capture_diagnostic[0]=10;
                return !snapshot.effects || effects_->compare(*snapshot.effects);
            });
            if(!captured)return false;
            capture_diagnostic[0]=11;
            if(!descriptors(base,after))return false;
            capture_diagnostic[0]=14;if(before!=after)return false;
            capture_diagnostic[0]=15;if(!references.covers(before[0],before[1]))return false;
            capture_diagnostic[0]=16;
            if(!fingerprint.matches(GeneratedBufferFingerprint::read(source.generated->buffer,source.generated->size)))return false;
            capture_diagnostic[0]=12;
            if(!references.bind(pool,handle))return false;
            capture_diagnostic[0]=17;if(!source.generated->bind(pool,handle))return false;
            guards_[count_-1]={base,source.generated->root,source.generated->control,before,fingerprint};
            capture_diagnostic[0]=13;
            return valid_guard(guards_[count_-1]);
        });
    }
    bool capture_general_owned(const RollbackBoundaryKey& key,unsigned oldest,uintptr_t base,uintptr_t argument,ReplayDependencySource& source){
        return capture_owned(key,oldest,base,argument,source,false,true);
    }
    template<class Capture> bool append(const RollbackBoundaryKey& key,unsigned oldest_required,Capture capture){
        if(failed_ || !key.same_round(key) || key.thread!=GetCurrentThreadId())return false;
        capture_diagnostic[0]=21;
        if(replay_next_ && (!replay_next_->same_round(key) || replay_next_->frame!=key.frame))return false;
        capture_diagnostic[0]=22;
        if(count_ && (!keys_[count_-1].same_round(key) || keys_[count_-1].frame==UINT32_MAX || key.frame!=keys_[count_-1].frame+1))return false;
        // Never evict a frame still required by an unresolved correction.
        capture_diagnostic[0]=23;
        if(count_==Capacity && keys_[0].frame>=oldest_required)return false;
        capture_diagnostic[0]=24;
        if(!pool_.bind_current_thread())return false;
        if(count_==Capacity){
            capture_diagnostic[0]=25;
            if(!pool_.retire(handles_[0])){failed_=true;return false;}
            for(unsigned i=1;i<count_;++i){handles_[i-1]=handles_[i];keys_[i-1]=keys_[i];guards_[i-1]=guards_[i];}
            --count_;
        }
        capture_diagnostic[0]=26;
        if(!compact_births()){failed_=true;return false;}
        capture_diagnostic[0]=27;
        auto h=pool_.acquire(key.session,key.round,key.frame,NativeBoundarySnapshot::Mode::combat_capture_only,key.slot);
        if(!h){failed_=true;return false;}
        guards_[count_]={};handles_[count_]=*h;keys_[count_++]=key;
        bool ok=false;
        try {ok=capture(pool_,*h,key);} catch(...){failed_=true;return false;}
        if(!ok || !pool_.visit(*h,[](NativeBoundarySnapshot& s){return s.valid && !s.fault;})){failed_=true;return false;}
        capture_diagnostic[0]=0;
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
        unsigned birth_cursor=0;
        if(effects_ && !pool_.visit(handles_[first],[&](NativeBoundarySnapshot& s){
            if(!s.effects || s.effects->owner!=effects_ || !s.effects->valid)return false;
            birth_cursor=s.effects->birth_cursor;return true;
        }))return false;
        bool restored=false;
        try {restored=pool_.visit(handles_[first],restore);}catch(...){failed_=true;return false;}
        if(!restored){failed_=true;return false;}
        while(count_>first){
            const unsigned i=count_-1;
            if(!pool_.retire(handles_[i])){failed_=true;return false;}
            handles_[i]={};keys_[i]={};guards_[i]={};--count_;
        }
        if(effects_ && !effects_->branch_birth_history(birth_cursor,count_)){failed_=true;return false;}
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
    bool validate_owned(const RollbackBoundaryKey& key){
        if(failed_ || key.thread!=GetCurrentThreadId())return false;
        for(unsigned i=0;i<count_;++i)if(keys_[i].same_round(key) && keys_[i].frame==key.frame)return valid_guard(guards_[i]) && pool_.visit(handles_[i],[&](NativeBoundarySnapshot& snapshot){
            return !effects_ || (snapshot.effects && snapshot.effects->owner==effects_ && effects_->validate(*snapshot.effects));
        });
        return false;
    }
    // Explicit owner-thread teardown, including a failed capture. No recovery
    // to stock simulation is implied by releasing the references.
    bool release(){
        if(count_ && keys_[0].thread!=GetCurrentThreadId())return false;
        failed_=true;
        for(unsigned i=0;i<count_;++i)if(!pool_.release_dependencies(handles_[i]))return false;
        return !effects_ || effects_->finish();
    }
};
}
