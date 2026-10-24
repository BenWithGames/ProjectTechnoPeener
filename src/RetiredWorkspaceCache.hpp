#pragma once
#include <memory>
#include <cstdint>
namespace sc6::research {
// One detached allocation per owner thread. No session, snapshot or native lease
// is cached. The caller must establish clean retirement before offering storage.
template<class T> class RetiredWorkspaceCache {
    std::unique_ptr<T> item_;uintptr_t base_{};
public:
    // Allocate only detached host scratch. No native state or leases are read.
    bool prepare(uintptr_t base){
        if(!base)return false;
        if(item_)return base_==base;
        auto candidate=std::make_unique<T>();
        if(!candidate->prepare_for_reuse())return false;
        base_=base;item_=std::move(candidate);return true;
    }
    bool put(std::unique_ptr<T>& item,uintptr_t base,bool clean){
        if(!clean || !base || !item || !item->prepare_for_reuse())return false;
        // Bound retained memory to one workspace; a second clean donor is freed.
        if(!item_){base_=base;item_=std::move(item);}else item.reset();
        return true;
    }
    std::unique_ptr<T> take(uintptr_t base){
        if(!base || base!=base_)return {};
        base_=0;return std::move(item_);
    }
};
}
