#pragma once
#include "StartupCorrectionExchange.hpp"
namespace sc6::research {
// Allocated before the agreed window. All native work and release are owner
// thread only; network reporting reads the atomic diagnostic copy.
class StartupPredictionCorrection {
    OwnedPredictionCorrection backend_;
public:
    StartupCorrectionExchange exchange;
    std::atomic<unsigned> captured{},stage{},predicted{},corrected{},capture_detail{};
    std::atomic<bool> failed{},restored{},injected{},changed{},verified{},retired{},restart{};
    std::array<std::atomic<uint64_t>,std::size(OwnedPredictionCorrection::diagnostic_names)> diagnostic{};
    void publish_diagnostics(){const auto values=backend_.diagnostics();for(unsigned i=0;i<values.size();++i)diagnostic[i]=values[i];}
    bool release(){if(retired.load())return true;const bool ok=backend_.release();if(ok)retired=true;else {failed=true;restart=true;}return ok;}
    void step(const RollbackBoundaryKey& key,uintptr_t base,uintptr_t argument,ReplayDependencySource& source,
              SingleUpdateCall::Routine original,bool confirmation_hold_released){
        if(failed.load() || verified.load() || retired.load())return;
        try {
            if(exchange.failed.load()){failed=true;release();return;}
            if(!backend_.clean.capture(key,base,argument,NativeBoundarySnapshot::Mode::idle,true,&source)){
                capture_detail=backend_.clean.capture_stage;failed=true;release();return;
            }
            const auto count=++captured;
            if(count<=3){OwnedPredictionCorrection::Input input{};
                if(!FullStateProbe::input_bytes(argument,input) || !exchange.publish(key,input)){failed=true;release();}return;
            }
            OwnedPredictionCorrection::Frames frames{};
            if(!confirmation_hold_released || !exchange.frames(frames)){failed=true;release();return;}
            const bool ok=backend_.run_confirmed(frames,key,base,source,original);
            publish_diagnostics();
            stage=static_cast<unsigned>(backend_.stage);predicted=backend_.predicted_updates;corrected=backend_.corrected_updates;
            capture_detail=backend_.capture_detail;restored=backend_.clean.any_restore_invoked();
            injected=backend_.injected;changed=backend_.state_changed;verified=ok;restart=backend_.restart_required;
            failed=!ok;release();
        }catch(...){publish_diagnostics();failed=true;restored=backend_.clean.any_restore_invoked();restart=backend_.clean.rewind_started() || restored.load();release();}
    }
};
}
