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
template<unsigned Capacity=4,class Boundary=NativeBoundarySnapshot> class NativeSnapshotPool {
public:
    struct Handle {unsigned index{};uint64_t serial{},session{},round{},frame{};};
private:
    using State=typename Boundary::StateType;
    struct Slot {
        Boundary snapshot{NativeBoundarySnapshot::Mode::idle};
        Slot()=default;
        explicit Slot(std::unique_ptr<State> storage):snapshot(NativeBoundarySnapshot::Mode::idle,std::move(storage)){}
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
    bool storage_reused{};
    std::array<uint64_t,2> reuse_diagnostic{}; // rejection, slot
    // Only detached byte storage transfers. Native leases and effect bindings
    // remain with their old owner and must already have been released.
    bool reusable_storage(){
        if(!serial_ && !discarded_)bind_current_thread();
        reuse_diagnostic={1,UINT_MAX};
        if(discarded_ || GetCurrentThreadId()!=owner_)return false;
        for(unsigned i=0;i<Capacity;++i){const auto& s=*slots_[i];reuse_diagnostic[1]=i;
            reuse_diagnostic[0]=2;if(s.borrowed || (s.occupied && !s.poisoned))return false;
            reuse_diagnostic[0]=3;if(s.snapshot.effects && s.snapshot.effects->snapshot.generation)return false;
            if(s.snapshot.spare_effects && (s.snapshot.spare_effects->snapshot.generation || s.snapshot.spare_effects->valid))return false;
            reuse_diagnostic[0]=4;for(const auto& lease:s.dependencies)if(lease.active())return false;
            reuse_diagnostic[0]=5;const auto* q=s.snapshot.data.get();
            if(!q || s.snapshot.fault || s.snapshot.exception || q->fault || q->restore_invoked ||
                q->saved.stream.failures || q->after_save.stream.failures)return false;
        }
        reuse_diagnostic[0]=0;return true;
    }
    explicit NativeSnapshotPool(NativeSnapshotPool* previous=nullptr){
        (void)State::reset_defaults();
        if(previous && previous->reusable_storage()){
            previous->discarded_=true;
            for(unsigned i=0;i<Capacity;++i){
                previous->slots_[i]->snapshot.data->reset_for_reuse();
                slots_[i]=std::make_unique<Slot>(std::move(previous->slots_[i]->snapshot.data));
                slots_[i]->snapshot.spare_effects=std::move(previous->slots_[i]->snapshot.spare_effects);
            }
            storage_reused=true;
        }else {if(previous)reuse_diagnostic=previous->reuse_diagnostic;for(auto& s:slots_)s=std::make_unique<Slot>();}
    }
    std::array<uint64_t,5> discard_diagnostic{}; // reason, owner, caller, acquisitions, slot
    bool discard_drained_storage(){
        // Pristine setup storage has never acquired native ownership. Transfer
        // it once, just as first capture does, before terminal game-thread cleanup.
        if(!serial_ && !discarded_)bind_current_thread();
        discard_diagnostic={1,owner_,GetCurrentThreadId(),serial_,UINT_MAX};
        if(GetCurrentThreadId()!=owner_)return false;
        if(discarded_){discard_diagnostic[0]=0;return true;}
        unsigned index=0;
        for(const auto& s:slots_){
            discard_diagnostic[4]=index++;
            if(s->borrowed){discard_diagnostic[0]=2;return false;}
            if(s->occupied && !s->poisoned){discard_diagnostic[0]=3;return false;}
            if(s->snapshot.effects && s->snapshot.effects->snapshot.generation){discard_diagnostic[0]=4;return false;}
            if(s->snapshot.spare_effects && (s->snapshot.spare_effects->snapshot.generation || s->snapshot.spare_effects->valid)){discard_diagnostic[0]=4;return false;}
            for(const auto& dependency:s->dependencies)if(dependency.active()){discard_diagnostic[0]=5;return false;}
        }
        discarded_=true;for(auto& s:slots_){s->snapshot.data.reset();s->snapshot.effects.reset();s->snapshot.spare_effects.reset();}discard_diagnostic[0]=0;return true;
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
    // Owner evidence retained across failure cleanup: reason, frame, slot,
    // borrowed, poisoned, snapshot fault/exception, probe fault/loaded, stream failures.
    std::array<uint64_t,10> retirement_diagnostic{};
    bool retire(const Handle& h){
        retirement_diagnostic={1,h.frame,h.index,0,0,0,0,0,0,0};
        auto* s=find(h);if(!s)return false;
        retirement_diagnostic[3]=s->borrowed;retirement_diagnostic[4]=s->poisoned;
        retirement_diagnostic[0]=2;if(s->borrowed)return false;
        // Even poisoned snapshots relinquish their resource ownership. Their
        // capture storage remains quarantined under the existing policy.
        retirement_diagnostic[0]=3;if(!drain(*s))return false;
        auto& snapshot=s->snapshot;auto& q=*snapshot.data;
        // A failed or loaded snapshot is not a clean reusable capture slot.
        retirement_diagnostic[5]=snapshot.fault;retirement_diagnostic[6]=snapshot.exception;
        retirement_diagnostic[7]=q.fault;retirement_diagnostic[8]=q.restore_invoked;
        retirement_diagnostic[9]=(uint64_t(q.saved.stream.failures)<<32)|q.after_save.stream.failures;
        const unsigned rejection=snapshot.fault?4:snapshot.exception?5:q.fault?6:q.restore_invoked?7:q.saved.stream.failures?8:q.after_save.stream.failures?9:0;
        if(rejection){retirement_diagnostic[0]=rejection;s->poisoned=true;return false;}
        // Ownership has drained and all quarantine guards passed. Reset metadata
        // and defaults while retaining count-gated transform buffer storage.
        snapshot.data->reset_for_reuse();
        snapshot.direction_bits=0x400;snapshot.direction_slot=1;snapshot.capture_stage=0;snapshot.mode_values_valid=false;snapshot.mode_values={};
        snapshot.mode=NativeBoundarySnapshot::Mode::idle;snapshot.module=0;snapshot.thread=0;
        snapshot.attempted=snapshot.valid=snapshot.fault=false;snapshot.exception=0;
        if(!snapshot.recycle_effect_binding()){s->poisoned=true;retirement_diagnostic[0]=10;return false;}
        s->occupied=false;retirement_diagnostic[0]=0;return true;
    }
};
}


