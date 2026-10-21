#pragma once
#include "NativeRollingHistory.hpp"
#include <atomic>
namespace sc6::research {
template<class History=NativeRollingHistory<4>> class StartupSnapshotCapture {
    History history_;
    RollbackBoundaryKey first_{};
public:
    std::atomic<unsigned> captured{};
    std::atomic<bool> failed{},retired{};
    bool discard_released_storage(){
        if(!retired.load() || failed.load())return false;
        if constexpr(requires {history_.discard_released_storage();})return history_.discard_released_storage();
        else return true;
    }
    bool release(){
        if(retired.load())return true;
        const bool ok=history_.release();if(!ok)failed=true;
        if(ok)retired=true;return ok;
    }
    bool capture(const RollbackBoundaryKey& key,uintptr_t base,uintptr_t argument,ReplayDependencySource& source){
        if(failed.load() || retired.load())return false;
        const auto n=captured.load();
        if(n>=4 || !key.same_round(key) || (n && (!first_.same_round(key) || key.frame!=first_.frame+n))){
            failed=true;release();return false;
        }
        if(!n)first_=key;
        bool ok=false;
        try{ok=history_.capture_owned(key,first_.frame,base,argument,source,false);}catch(...){ok=false;}
        if(!ok){failed=true;release();return false;}
        captured=n+1;
        if(n==3){
            for(unsigned i=0;i<4;++i){auto saved=first_;saved.frame+=i;if(!history_.validate_owned(saved))failed=true;}
            release();
        }
        return !failed.load();
    }
};
}
