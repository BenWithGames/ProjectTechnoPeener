#pragma once
#include "StartupCorrectionExchange.hpp"
namespace sc6::research {
// Allocated before the agreed window. All native work and release are owner
// thread only; network reporting reads the atomic diagnostic copy.
template<class Backend=OwnedPredictionCorrection,class Exchange=StartupCorrectionExchange>
class StartupPredictionCorrectionRunner {
    Backend backend_;
public:
    Exchange exchange;
    ReplayFailureEvidence failure_evidence;
    bool combat_inputs{};
    void configure_combat(){combat_inputs=true;backend_.combat_inputs=true;exchange.require_combat_inputs=true;}
    bool attack_fixture{}; // configured before publication to the owner thread
    void configure_attack(){attack_fixture=true;backend_.attack_fixture=true;}
    std::atomic<unsigned> captured{},stage{},predicted{},corrected{},capture_detail{},failure_reason{};
    std::atomic<bool> failed{},restored{},injected{},changed{},verified{},retired{},restart{};
    std::atomic<bool> reference_attempted{},reference_verified{},safe_rejection{};
    // 0 not exercised, 1 verified endpoint/cleanup, 2 clean pre-mutation stop,
    // 3 unsafe stop: restart required, recovery is NOT claimed.
    std::atomic<unsigned> stop_outcome{};
    std::atomic<bool> no_divergence{}; // Published only after verified endpoint and cleanup.
    std::array<std::atomic<uint64_t>,std::size(OwnedPredictionCorrection::diagnostic_names)> diagnostic{};
    std::atomic<bool> blend_evidence_valid{};
    std::array<std::atomic<unsigned char>,1372> blend_saved{},blend_next{};
    void publish_diagnostics(){
        const auto values=backend_.diagnostics();for(unsigned i=0;i<values.size();++i)diagnostic[i]=values[i];
        if constexpr(requires {backend_.blend_evidence();}){
            const auto& evidence=backend_.blend_evidence();
            if(evidence.valid && !blend_evidence_valid.load()){
                for(unsigned i=0;i<1372;++i){blend_saved[i]=std::to_integer<unsigned char>(evidence.saved[i]);blend_next[i]=std::to_integer<unsigned char>(evidence.next[i]);}
                blend_evidence_valid.store(true,std::memory_order_release);
            }
        }
    }
    bool release() noexcept {
        if(retired.load())return true;
        bool ok=false;try {ok=backend_.release();}catch(...){ok=false;}
        if(ok)retired=true;else {verified=false;failed=true;restart=true;}
        return ok;
    }
    void step(const RollbackBoundaryKey& key,uintptr_t base,uintptr_t argument,ReplayDependencySource& source,
              SingleUpdateCall::Routine original,bool confirmation_hold_released){
        if(failed.load() || verified.load() || retired.load())return;
        try {
            if(exchange.failed.load()){failure_reason=1;failed=true;release();return;}
            if(!backend_.clean.capture(key,base,argument,(attack_fixture || combat_inputs)?NativeBoundarySnapshot::Mode::general_combat:NativeBoundarySnapshot::Mode::idle,true,&source)){
                capture_detail=backend_.clean.capture_stage;failure_reason=2;failed=true;release();return;
            }
            const auto count=++captured;
            if(count<=3){OwnedPredictionCorrection::Input input{};
                if(!FullStateProbe::input_bytes(argument,input) || !exchange.publish(key,input)){failure_reason=3;failed=true;release();}return;
            }
            OwnedPredictionCorrection::Frames frames{};
            if(!confirmation_hold_released || !exchange.frames(frames)){failure_reason=confirmation_hold_released?5:4;failed=true;release();return;}
            const bool ok=backend_.run_confirmed(frames,key,base,source,original);
            publish_diagnostics();
            if constexpr(requires {backend_.reference_attempted;}){
                reference_attempted=backend_.reference_attempted;reference_verified=backend_.reference_verified;
            }
            stage=static_cast<unsigned>(backend_.stage);predicted=backend_.predicted_updates;corrected=backend_.corrected_updates;
            capture_detail=backend_.capture_detail;restored=backend_.clean.any_restore_invoked();
            injected=backend_.injected;changed=backend_.state_changed;
            const bool needs_restart=backend_.restart_required;
            const bool unchanged=combat_inputs && !ok && !backend_.state_changed &&
                static_cast<unsigned>(backend_.stage)==static_cast<unsigned>(OwnedPredictionCorrection::Stage::complete) && backend_.predicted_updates==3 &&
                backend_.corrected_updates==3 && backend_.retired && !backend_.restart_required;
            if(!ok && !unchanged){if constexpr(requires {backend_.capture_failure_evidence(failure_evidence);})backend_.capture_failure_evidence(failure_evidence);}
            const bool cleaned=release();
            if(!ok && !unchanged)failure_reason=6;
            restart=needs_restart || !cleaned;
            failed=(!ok && !unchanged) || !cleaned;verified=ok && cleaned;
            no_divergence=unchanged && cleaned && !restart.load();
            // Only a pre-mutation failure with verified ownership retirement is retryable.
            safe_rejection=combat_inputs && !ok && !unchanged && cleaned && !needs_restart && !injected.load() && !restored.load();
            if(safe_rejection.load())failed=false;
            stop_outcome=restart.load()?3u:(safe_rejection.load()?2u:((ok || unchanged)&&cleaned?1u:0u));
        }catch(...){
            failure_reason=7;publish_diagnostics();restored=backend_.clean.any_restore_invoked();
            const bool needs_restart=backend_.clean.rewind_started() || restored.load();
            if constexpr(requires {backend_.capture_failure_evidence(failure_evidence);})backend_.capture_failure_evidence(failure_evidence);
            const bool cleaned=release();restart=needs_restart || !cleaned;failed=true;stop_outcome=restart.load()?3u:0u;
        }
    }
};
using StartupPredictionCorrection=StartupPredictionCorrectionRunner<>;
}
