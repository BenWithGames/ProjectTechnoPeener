#pragma once
#include <array>
#include <atomic>
#include <cstdint>
namespace sc6::research {
// Address set must be immutable to concurrent readers. Setup/reset/drain belong
// to the owner; runtime may update addresses under the same external gate used
// by every notice caller.
// Other threads only mark notices; they never access the ledger or game payload.
// This is a stop notification, NOT a lock protecting an already-running restore.
template<unsigned Capacity=64> struct EffectReleaseNotice {
    static_assert(Capacity>0 && Capacity<=64);
    std::array<uintptr_t,Capacity> addresses{};
    std::atomic<bool> stopped{};
    std::atomic<uint64_t> pending{};
    void before_native(uintptr_t address) noexcept {
        if(!address)return;
        for(unsigned i=0;i<Capacity;++i)if(addresses[i]==address){
            stopped.store(true,std::memory_order_release);
            pending.fetch_or(uint64_t{1}<<i,std::memory_order_release);
            return;
        }
    }
    template<class Invalidate> void drain(Invalidate&& invalidate) noexcept {
        static_assert(noexcept(invalidate(uintptr_t{})));
        auto mask=pending.exchange(0,std::memory_order_acq_rel);
        for(unsigned i=0;i<Capacity;++i)if(mask&(uint64_t{1}<<i))invalidate(addresses[i]);
        // stopped deliberately stays latched until an explicit quiescent reset.
    }
};
}
