#pragma once
#include "EffectRetentionRuntime.hpp"
namespace sc6::research {
// A bounded list-lifecycle transaction. Its runtime must already own allocation
// tracking and the release route. This is not permission to publish that route,
// call native loading without synchronization, or accept unknown dependencies.
template<unsigned Capacity=64,unsigned Frames=4> class EffectLifecycleWindow {
    static_assert(Frames>0 && Frames<=32);
public:
    using Runtime=EffectRetentionRuntime<Capacity,Frames*2>;
    using Image=NativeEffectImage<Capacity>;
    using Snapshot=typename Runtime::Snapshot;
    enum class Failure : unsigned {none,capture,identity,dependencies,restore,comparison,cleanup};
    struct Diagnostic {
        unsigned captured{},restores{},verified{},added{},removed{},preserved{};
        unsigned mismatch_object{UINT32_MAX},mismatch_offset{UINT32_MAX};
        Failure failure{};bool mutated{},retired{};
    } diagnostic;
private:
    Runtime& runtime_;
    std::array<Image,Frames> images_{};
    std::array<Snapshot,Frames> snapshots_{};
    Snapshot verification_{};
    Image current_{};
    bool fail(Failure failure){diagnostic.failure=failure;return false;}
public:
    explicit EffectLifecycleWindow(Runtime& runtime):runtime_(runtime){}
    bool capture(uint64_t frame,uintptr_t manager){
        if(diagnostic.failure!=Failure::none || diagnostic.retired || diagnostic.mutated || diagnostic.captured==Frames)return false;
        const auto i=diagnostic.captured;
        if(i && (images_[i-1].frame==UINT64_MAX || frame!=images_[i-1].frame+1 || manager!=images_[i-1].manager))return fail(Failure::identity);
        snapshots_[i]=runtime_.capture_image(frame,manager,images_[i]);
        if(!snapshots_[i].generation)return fail(Failure::capture);
        ++diagnostic.captured;return true;
    }
    // All saved allocations remain pinned across multiple reverse and forward
    // restores. Objects absent from the target become retired, not freed here.
    template<class References> bool restore(unsigned index,References&& references){
        if(diagnostic.failure!=Failure::none || diagnostic.retired || index>=diagnostic.captured)return false;
        EffectRestorePlan<Capacity> plan;
        if(!runtime_.prepare_effect_restore(snapshots_[index],images_[index],current_,plan))return fail(Failure::identity);
        // apply rechecks the plan, generations, environment and references under
        // the runtime gate before writing. The preliminary plan is diagnostic.
        bool references_rejected=false;
        if(!runtime_.apply_effect_image(snapshots_[index],images_[index],current_,[&](const Image& saved,const Image& live){
            const bool valid=references(saved,live);references_rejected=!valid;return valid;
        })){
            diagnostic.mutated=diagnostic.mutated || runtime_.restart_required();
            return fail(references_rejected?Failure::dependencies:Failure::restore);
        }
        diagnostic.mutated=true;++diagnostic.restores;
        diagnostic.added+=plan.revive_count;diagnostic.removed+=plan.remove_count;diagnostic.preserved+=plan.preserve_count;
        return true;
    }
    // Replay may allocate a new address for the same logical object. Compare
    // ordered type/full payload, excluding ONLY list links reconstructed from
    // the validated order. No other pointers, padding, counters or flags masked.
    static bool equivalent(const Image& expected,const Image& actual,unsigned& object,unsigned& offset){
        object=offset=UINT32_MAX;
        if(!expected.valid || !actual.valid || expected.manager!=actual.manager || expected.count>Capacity || actual.count>Capacity || expected.count!=actual.count ||
           !expected.environment.same(actual.environment))return false;
        const auto module=static_cast<uintptr_t>(expected.environment.values[0]);
        if(!EffectRestorePlan<Capacity>::shape(expected,module) || !EffectRestorePlan<Capacity>::shape(actual,module))return false;
        for(unsigned i=0;i<expected.count;++i){const auto& saved_object=expected.objects[i];const auto& live_object=actual.objects[i];
            object=i;
            if(saved_object.vtable!=live_object.vtable || saved_object.size!=live_object.size || saved_object.size>saved_object.bytes.size())return false;
            for(unsigned j=0;j<saved_object.size;++j){
                if(j>=0x10 && j<0x20)continue;
                if(saved_object.bytes[j]!=live_object.bytes[j]){offset=j;return false;}
            }
        }
        object=UINT32_MAX;return true;
    }
    bool verify(unsigned index,uint64_t replay_frame){
        if(diagnostic.failure!=Failure::none || diagnostic.retired || index>=diagnostic.captured)return false;
        // A separate frame key avoids aliasing a reference snapshot's pin slot.
        verification_=runtime_.capture_image(replay_frame,images_[index].manager,current_);
        if(!verification_.generation)return fail(Failure::capture);
        const bool same=equivalent(images_[index],current_,diagnostic.mismatch_object,diagnostic.mismatch_offset);
        if(!runtime_.expire(verification_))return fail(Failure::cleanup);
        verification_={};
        if(!same)return fail(Failure::comparison);
        ++diagnostic.verified;return true;
    }
    // On partial native write, runtime refuses expiration: do not pretend that
    // unsafe memory is cleaned up. Otherwise drain every independently held pin.
    bool retire(){
        if(diagnostic.retired)return true;
        bool ok=true;
        if(verification_.generation){if(runtime_.expire(verification_))verification_={};else ok=false;}
        for(auto& snapshot:snapshots_)if(snapshot.generation){
            if(runtime_.expire(snapshot))snapshot={};else ok=false;
        }
        if(!ok)return fail(Failure::cleanup);
        diagnostic.retired=true;return true;
    }
};
}
