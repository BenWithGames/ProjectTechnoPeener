#pragma once
#include <atomic>
#include <mutex>
namespace sc6::research {
// Protects ONLY direct memory access and release bookkeeping. Never invoke game
// functions, allocator functions, worker waits or this gate recursively inside
// either callback. Native free runs after release bookkeeping unlocks.
class EffectMemoryGate {
    std::mutex mutex_;
public:
    enum class AccessOutcome : unsigned {not_attempted=0,entered=1,contended=2,stopped=3};
    template<class Access> bool try_access(const std::atomic<bool>& stopped,Access&& access,AccessOutcome* outcome=nullptr) {
        std::unique_lock guard(mutex_,std::try_to_lock);
        if(!guard.owns_lock()){if(outcome)*outcome=AccessOutcome::contended;return false;}
        if(stopped.load(std::memory_order_acquire)){if(outcome)*outcome=AccessOutcome::stopped;return false;}
        if(outcome)*outcome=AccessOutcome::entered;
        access();return true;
    }
    template<class Decision> bool release_decision(Decision&& decision) {
        std::lock_guard guard(mutex_);
        return decision();
    }
};
}
