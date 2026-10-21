#pragma once
#include <Windows.h>
#ifdef interface
#undef interface
#endif
#include <memory>
#include "ResourceReferenceLease.hpp"
#include <exception>
#include <algorithm>
#include "NativeBoundarySnapshot.hpp"
#include <optional>
#include <memory>
namespace sc6::research {
// Simulation-thread-only storage. Construction allocates; acquire/retire do not.
// No automatic recycling: a retained rollback reference must first be retired.
// This storage does not widen native capture/restore readiness guards.
template<unsigned Capacity=4> class NativeSnapshotPool {
public:
    struct Handle {unsigned index{};uint64_t serial{},session{},round{},frame{};};
private:
    struct Slot {
        NativeBoundarySnapshot snapshot{NativeBoundarySnapshot::Mode::idle};
        Handle key{};bool occupied{},borrowed{},poisoned{};
        std::array<ResourceReferenceLease,8> dependencies;
        std::array<uintptr_t,8> dependency_controls{};
    };
    std::array<std::unique_ptr<Slot>,Capacity> slots_;
    DWORD owner_=GetCurrentThreadId();uint64_t serial_{};bool discarded_{};
    Slot* find(const Handle& h){
        if(discarded_ || GetCurrentThreadId()!=owner_ || h.index>=Capacity)return nullptr;
        auto& s=*slots_[h.index];const auto& k=s.key;
        return s.occupied && k.serial==h.serial && k.session==h.session && k.round==h.round && k.frame==h.frame?&s:nullptr;
    }
    bool drain(Slot& s){
        if(!s.snapshot.release_effects()){s.poisoned=true;return false;}
        s.borrowed=true;
        for(unsigned i=0;i<s.dependencies.size();++i){
            if(s.dependencies[i].active())s.dependencies[i].release(0,true);
            s.dependency_controls[i]=0;
        }
        s.borrowed=false;
        return true;
    }
public:
    NativeSnapshotPool(){(void)FullStateProbe::reset_defaults();for(auto& s:slots_)s=std::make_unique<Slot>();}
    bool discard_drained_storage(){
        if(GetCurrentThreadId()!=owner_)return false;
        if(discarded_)return true;
        for(const auto& s:slots_){
            if(s->borrowed || (s->occupied && !s->poisoned) || (s->snapshot.effects && s->snapshot.effects->snapshot.generation))return false;
            for(const auto& dependency:s->dependencies)if(dependency.active())return false;
        }
        discarded_=true;for(auto& s:slots_)s->snapshot.data.reset();return true;
    }
    ~NativeSnapshotPool(){
        // Native disposal must never migrate to an arbitrary destructor thread.
        // Production users must drain on the simulation thread before teardown.
        for(auto& s:slots_){
            if(s->snapshot.effects && s->snapshot.effects->snapshot.generation){
                if(GetCurrentThreadId()!=owner_ || s->borrowed || !s->snapshot.release_effects())std::terminate();
            }
        }
        for(auto& s:slots_)for(auto& dependency:s->dependencies)if(dependency.active()){
            if(GetCurrentThreadId()!=owner_ || s->borrowed)std::terminate();
            s->borrowed=true;dependency.release(0,true);s->borrowed=false;
        }
    }
    // Caller must already own this validated native pair for the duration of
    // acquisition. Never pass cached borrowed pointers. No live caller yet.
    bool retain_dependency(const Handle& h,uintptr_t control,
        ResourceReferenceLease::Dispose dispose_object,ResourceReferenceLease::Dispose dispose_control){
        auto* s=find(h);if(!s || s->borrowed || s->poisoned || !control)return false;
        for(const auto existing:s->dependency_controls)if(existing==control)return false;
        for(unsigned i=0;i<s->dependencies.size();++i)if(!s->dependencies[i].active()){
            if(!s->dependencies[i].acquire(control,owner_,0,dispose_object,dispose_control))return false;
            s->dependency_controls[i]=control;return true;
        }
        return false;
    }
    bool release_dependencies(const Handle& h){
        auto* s=find(h);if(!s || s->borrowed)return false;
        // Explicit teardown makes further visits impossible, even if a caller
        // keeps the handle. Normal retirement uses drain then reconstructs.
        s->poisoned=true;s->snapshot.valid=false;return drain(*s);
    }
    // One-time transfer of pristine storage from setup to simulation thread.
    bool bind_current_thread(){if(!serial_)owner_=GetCurrentThreadId();return owner_==GetCurrentThreadId();}
    std::optional<Handle> acquire(uint64_t session,uint64_t round,uint64_t frame,
        NativeBoundarySnapshot::Mode mode=NativeBoundarySnapshot::Mode::idle,unsigned side=1){
        if(discarded_ || GetCurrentThreadId()!=owner_ || !session || !round || side<1 || side>2 || serial_==UINT64_MAX)return {};
        for(const auto& s:slots_)if(s->occupied && s->key.session==session && s->key.round==round && s->key.frame==frame)return {};
        for(unsigned i=0;i<Capacity;++i){auto& s=*slots_[i];
            if(s.occupied || s.poisoned)continue;
            s.snapshot.mode=mode;s.snapshot.direction_slot=side;
            auto& q=*s.snapshot.data;
            q.throw_requested=mode==NativeBoundarySnapshot::Mode::throw_move;
            q.charge_requested=mode==NativeBoundarySnapshot::Mode::charge;
            q.attack_requested=mode==NativeBoundarySnapshot::Mode::attack;
            q.edge_requested=mode==NativeBoundarySnapshot::Mode::edge;
            q.reversal_requested=mode==NativeBoundarySnapshot::Mode::reversal;
            s.key={i,++serial_,session,round,frame};s.occupied=true;return s.key;
        }
        return {};
    }
    // Callback may not retain references. Reentrant retirement is rejected.
    template<class Callback> bool visit(const Handle& h,Callback callback){
        auto* s=find(h);if(!s || s->borrowed || s->poisoned)return false;
        s->borrowed=true;
        struct Release {Slot* slot;~Release(){slot->borrowed=false;}} release{s};
        return callback(s->snapshot);
    }
    bool retire(const Handle& h){
        auto* s=find(h);if(!s || s->borrowed)return false;
        // Even poisoned snapshots relinquish their resource ownership. Their
        // capture storage remains quarantined under the existing policy.
        if(!drain(*s))return false;
        auto& snapshot=s->snapshot;auto& q=*snapshot.data;
        // A failed or loaded snapshot is not a clean reusable capture slot.
        if(snapshot.fault || snapshot.exception || q.fault || q.restore_invoked || q.saved.stream.failures || q.after_save.stream.failures){
            s->poisoned=true;return false;
        }
        // Ownership has drained and all quarantine guards passed. Reset metadata
        // and defaults while retaining count-gated transform buffer storage.
        snapshot.data->reset_for_reuse();
        snapshot.direction_bits=0x400;snapshot.direction_slot=1;snapshot.capture_stage=0;snapshot.mode_values_valid=false;snapshot.mode_values={};
        snapshot.mode=NativeBoundarySnapshot::Mode::idle;snapshot.module=0;snapshot.thread=0;
        snapshot.attempted=snapshot.valid=snapshot.fault=false;snapshot.exception=0;
        snapshot.effects.reset();
        s->occupied=false;return true;
    }
};
}


