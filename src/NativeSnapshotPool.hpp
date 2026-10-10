#pragma once
#include <Windows.h>
#ifdef interface
#undef interface
#endif
#include <memory>
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
    };
    std::array<std::unique_ptr<Slot>,Capacity> slots_;
    DWORD owner_=GetCurrentThreadId();uint64_t serial_{};
    Slot* find(const Handle& h){
        if(GetCurrentThreadId()!=owner_ || h.index>=Capacity)return nullptr;
        auto& s=*slots_[h.index];const auto& k=s.key;
        return s.occupied && k.serial==h.serial && k.session==h.session && k.round==h.round && k.frame==h.frame?&s:nullptr;
    }
public:
    NativeSnapshotPool(){for(auto& s:slots_)s=std::make_unique<Slot>();}
    std::optional<Handle> acquire(uint64_t session,uint64_t round,uint64_t frame,
        NativeBoundarySnapshot::Mode mode=NativeBoundarySnapshot::Mode::idle,unsigned side=1){
        if(GetCurrentThreadId()!=owner_ || !session || !round || side<1 || side>2 || serial_==UINT64_MAX)return {};
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
        auto& b=s->snapshot;auto& q=*b.data;
        // A failed or loaded snapshot is not a clean reusable capture slot.
        if(b.fault || b.exception || q.fault || q.restore_invoked || q.saved.stream.failures || q.after_save.stream.failures){
            s->poisoned=true;return false;
        }
        // Reconstruct in the original allocation, avoiding a huge stack
        // temporary and restoring stream function tables/copier defaults.
        std::destroy_at(b.data.get());std::construct_at(b.data.get());
        b.direction_bits=0x400;b.direction_slot=1;b.capture_stage=0;b.mode_values_valid=false;b.mode_values={};
        b.mode=NativeBoundarySnapshot::Mode::idle;b.module=0;b.thread=0;
        b.attempted=b.valid=b.fault=false;b.exception=0;
        s->occupied=false;return true;
    }
};
}


