#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace sc6::research {
// Bookkeeping only: no game hooks, memory reads, list writes or allocator calls.
// A future owner must prove all creation/removal paths before deferring frees.
// Single game-thread owner; release callbacks must not reenter this ledger.
template<std::size_t Capacity=128, unsigned SnapshotSlots=16>
class EffectLifetimeLedger {
    static_assert(Capacity>0 && SnapshotSlots>0 && SnapshotSlots<=64);
public:
    struct Token { uintptr_t address{}; uint64_t generation{}; };
    struct Allocation { uintptr_t address{},vtable{}; std::size_t bytes{}; };
private:
    struct Entry { Allocation allocation{}; uint64_t generation{},pins{}; bool retired{}; };
    std::array<Entry,Capacity> entries_{};
    uint64_t generation_{};
    Entry* find(Token token) noexcept {
        if(!token.address || !token.generation)return nullptr;
        for(auto& e:entries_)if(e.allocation.address==token.address && e.generation==token.generation)return &e;
        return nullptr;
    }
public:
    bool pinned(Token token) noexcept {auto* e=find(token);return e && e->pins!=0;}
    bool live(Token token) noexcept {
        auto* e=find(token);return e && !e->retired;
    }
    // Native allocator has ownership again. Never issue a second release.
    // Caller must invalidate every referring snapshot before forgetting a token.
    bool forget_unpinned(Token token) noexcept {
        auto* e=find(token);if(!e || e->pins)return false;
        *e={};return true;
    }
    // Admit only a verified allocation. Reused addresses get a new generation.
    Token track(Allocation allocation) noexcept {
        if(!allocation.address || !allocation.vtable || !allocation.bytes ||
           allocation.address>UINTPTR_MAX-allocation.bytes ||
           generation_==(std::numeric_limits<uint64_t>::max)())return {};
        Entry* available=nullptr;
        for(auto& e:entries_){
            if(e.allocation.address && allocation.address<e.allocation.address+e.allocation.bytes &&
               e.allocation.address<allocation.address+allocation.bytes)return {};
            if(!e.allocation.address && !available)available=&e;
        }
        if(!available)return {};
        *available={allocation,++generation_,0,false};
        return {allocation.address,available->generation};
    }
    bool pin(Token token,unsigned snapshot) noexcept {
        auto* e=find(token);
        if(!e || e->retired || snapshot>=SnapshotSlots)return false;
        e->pins|=uint64_t{1}<<snapshot;return true;
    }
    // Called only after a verified native unlink, in place of its allocator free.
    bool retire(Token token) noexcept {
        auto* e=find(token);if(!e || e->retired)return false;
        e->retired=true;return true;
    }
    // Eligibility is not permission to write memory: payload/list restoration
    // still requires its own complete preflight. Revive only after it succeeds.
    bool retained_by(Token token,unsigned snapshot) noexcept {
        auto* e=find(token);return e && snapshot<SnapshotSlots && (e->pins&(uint64_t{1}<<snapshot));
    }
    bool revive(Token token,unsigned snapshot) noexcept {
        if(!retained_by(token,snapshot))return false;
        find(token)->retired=false;return true;
    }
    bool release_snapshot(unsigned snapshot) noexcept {
        if(snapshot>=SnapshotSlots)return false;
        for(auto& e:entries_)e.pins&=~(uint64_t{1}<<snapshot);
        return true;
    }
    template<class Release> std::size_t collect(Release&& release) noexcept {
        static_assert(noexcept(release(Allocation{})),"Allocator release callback must be noexcept");
        std::size_t count=0;
        for(auto& e:entries_)if(e.allocation.address && e.retired && !e.pins){
            const auto allocation=e.allocation;
            e={}; // Invalidate tokens before handing memory back to the allocator.
            release(allocation);++count;
        }
        return count;
    }
    // Only at a quiescent session boundary, after snapshots and hook users stop.
    // Refuse the whole operation before any callback if history still pins data.
    // Retired allocations belong to us; live allocations remain owned by SC6.
    template<class Release> bool finish_session(Release&& release) noexcept {
        static_assert(noexcept(release(Allocation{})),"Allocator release callback must be noexcept");
        for(const auto& e:entries_)if(e.pins)return false;
        collect(release);
        for(auto& e:entries_)e={};
        // Do not reset generation_: old tokens must not become valid next session.
        return true;
    }
    std::size_t size()const noexcept {
        std::size_t count=0;for(const auto& e:entries_)if(e.allocation.address)++count;return count;
    }
};
}
