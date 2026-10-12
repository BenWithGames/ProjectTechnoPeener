#pragma once
#include "NativeRollbackWindow.hpp"
#include <atomic>
namespace sc6::research {
// Heap-owned workspace prepared before the first capture. Reference inputs
// only: this does not predict remote input or enable continuous rollback.
template<class Window=NativeRollbackWindow> class StartupReferenceReplay {
    Window window_;
public:
    std::atomic<unsigned> captured{},stage{},capture_detail{},prepare_detail{},hop{},plan{};
    std::atomic<bool> failed{},restored{},verified{},retired{},restart{};
    bool release(){if(retired.load())return true;const bool ok=window_.release_owned_dependencies();if(ok)retired=true;else failed=true;return ok;}
    void step(const RollbackBoundaryKey& key,uintptr_t base,uintptr_t argument,ReplayDependencySource& source,SingleUpdateCall::Routine original){
        if(failed.load() || verified.load() || retired.load())return;
        try {
            stage=1;
            if(!window_.capture(key,base,argument,NativeBoundarySnapshot::Mode::idle,true,&source)){
                capture_detail=window_.capture_stage;failed=true;release();return;
            }
            ++captured;if(captured.load()!=4)return;
            stage=2;const bool rewound=window_.rewind(key);
            prepare_detail=window_.prepare_stage;hop=window_.last_hop;plan=window_.plan_result;
            restored=window_.any_restore_invoked();
            if(!rewound){failed=true;restart=window_.rewind_started() || restored.load();release();return;}
            stage=3;
            if(!window_.replay_reference(original)){failed=true;restart=true;release();return;}
            verified=true;stage=4;if(!release()){restart=true;return;}stage=5;
        }catch(...){failed=true;restored=window_.any_restore_invoked();restart=window_.rewind_started() || restored.load();release();}
    }
};
}
