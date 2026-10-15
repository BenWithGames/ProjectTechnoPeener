#pragma once
#include "ReplayOwnedResources.hpp"
#include "NativeClockDiscovery.hpp"
#include "StartupBoundaryPairs.hpp"
#include "NativeHoldRehearsal.hpp"
#include "StartupSnapshotCapture.hpp"
#include "StartupReferenceReplay.hpp"
#include "StartupPredictionCorrection.hpp"
#include "CorrectionCycleBank.hpp"
namespace sc6::research {
// Resource callbacks acquire references. Only the game-frame thread inspects
// readiness or releases them; the network worker can only request retirement.
template<class Resources=ReplayOwnedResources> class CombatResourcePreparation {
    Resources resources_;
    std::atomic<bool> stop_{};
public:
    template<class Stream> void wind_report(Stream& out)const{if constexpr(requires {resources_.wind_report(out);})resources_.wind_report(out);}
    StartupBoundaryPairs pairs;
    NativeHoldRehearsal<> hold_test;
    StartupSnapshotCapture<> snapshots;
    std::atomic<bool> capture_enabled{true};
    std::unique_ptr<StartupReferenceReplay<>> replay;
    std::atomic<bool> replay_enabled{};
    std::unique_ptr<StartupPredictionCorrection> correction;
    std::atomic<bool> correction_enabled{};
    std::unique_ptr<CorrectionCycleBank<>> cycles;
    std::atomic<bool> cycles_enabled{};
    RollbackBoundaryKey cycle_identity_; // owner thread only
    void enable_cycles(bool attack=false,bool combat=false,unsigned hold_ms=0){if(!cycles_enabled.load()){
        if(combat){if constexpr(requires {resources_.enable_wind();})if(!resources_.enable_wind()){failed=true;return;}}
        cycles=std::make_unique<CorrectionCycleBank<>>(attack,combat,hold_ms);cycles_enabled.store(true,std::memory_order_release);
    }}
    NativeHoldRehearsal<> confirmation_hold;
    void enable_correction(bool attack=false,bool combat=false){if(!correction_enabled.load()){correction=std::make_unique<StartupPredictionCorrection>();if(attack)correction->configure_attack();if(combat)correction->configure_combat();correction_enabled.store(true,std::memory_order_release);}}
    bool request_confirmation_hold(unsigned identity,unsigned clock,unsigned battle,unsigned slot){
        if(!correction_enabled.load(std::memory_order_acquire) || clock>UINT32_MAX-3 || battle>UINT32_MAX-3)return false;
        if(!correction->exchange.configure(battle,slot))return false;
        return confirmation_hold.state.load()!=0 || confirmation_hold.request(identity,clock+3,battle+3,2000);
    }
    void enable_replay(){if(!replay_enabled.load()){replay=std::make_unique<StartupReferenceReplay<>>();replay_enabled.store(true,std::memory_order_release);}}
    bool replay_restart()const{
        if(cycles_enabled.load(std::memory_order_acquire)){
            for(unsigned i=0;i<cycles->active();++i){const auto& c=cycles->at(i);
                if(c.runner.restart.load() || c.entry.state.load()==5 || c.confirmation.state.load()==5)return true;
            }
        }
        return (replay_enabled.load(std::memory_order_acquire) && replay->restart.load()) ||
        (correction_enabled.load(std::memory_order_acquire) && (correction->restart.load() || confirmation_hold.state.load()==5));}
    void replay_boundary(const RollbackBoundaryKey& key,uintptr_t base,uintptr_t argument,SingleUpdateCall::Routine original){
        if(cycles_enabled.load(std::memory_order_acquire)){
            auto* c=cycles->current();
            if(!c || c->entry.state.load()!=NativeHoldRehearsal<>::released || stop_.load() || retired.load() || failed.load())return;
            if(!cycle_identity_.session)cycle_identity_=key;
            if(!cycle_identity_.same_round(key)){c->runner.failure_reason=8;c->runner.failed=true;c->runner.release();return;}
            if(!c->runner.captured.load() && key.frame!=c->entry.target_battle()){
                c->runner.failure_reason=9;c->runner.failed=true;c->runner.release();return;
            }
            auto source=resources_.source();c->runner.step(key,base,argument,source,original,c->confirmation.state.load()==3);return;
        }
        if(correction_enabled.load(std::memory_order_acquire) && hold_test.state.load()==NativeHoldRehearsal<>::released &&
           !stop_.load() && !retired.load() && !failed.load()){
            if(!correction->captured.load() && key.frame!=hold_test.target_battle()){correction->failed=true;correction->release();return;}
            auto source=resources_.source();correction->step(key,base,argument,source,original,confirmation_hold.state.load()==3);return;
        }
        if(!replay_enabled.load(std::memory_order_acquire) || hold_test.state.load()!=NativeHoldRehearsal<>::released ||
            stop_.load() || retired.load() || failed.load())return;
        if(!replay->captured.load() && key.frame!=hold_test.target_battle()){replay->failed=true;replay->release();return;}
        auto source=resources_.source();replay->step(key,base,argument,source,original);
    }
    void capture_boundary(const RollbackBoundaryKey& key,uintptr_t base,uintptr_t argument){
        if(!capture_enabled.load() || hold_test.state.load()!=NativeHoldRehearsal<>::released || stop_.load() || retired.load() || failed.load() ||
            snapshots.retired.load() || snapshots.failed.load())return;
        if(!snapshots.captured.load() && key.frame!=hold_test.target_battle()){
            snapshots.failed=true;snapshots.release();return;
        }
        auto source=resources_.source();snapshots.capture(key,base,argument,source);
    }
    bool run_hold(const NativeClockDiscovery::Observation& observed,uintptr_t object,uint64_t now){
        const unsigned context_mask=(!observed.valid?1u:0u) | (observed.object!=object?2u:0u) |
            (observed.thread!=GetCurrentThreadId()?4u:0u) | (observed.thread!=owner_thread.load()?8u:0u) |
            (now<observed.tick || now-observed.tick>250?16u:0u) | (retired.load()?32u:0u) |
            (failed.load()?64u:0u) | (resources_.cancelled()?128u:0u) | (!resources_.ready()?256u:0u);
        const bool core_valid=context_mask==0;
        const bool valid=core_valid && !stop_.load();
        if(cycles_enabled.load(std::memory_order_acquire)){
            auto* c=cycles->current();if(!c)return false;
            const bool clean_stop=NativeHoldRehearsal<>::clean_cancel(
                stop_.load() || c->runner.failed.load() || c->runner.exchange.failed.load(),
                c->runner.stage.load(),c->runner.restored.load(),c->runner.injected.load(),c->runner.restart.load());
            if(c->entry.state.load()==NativeHoldRehearsal<>::queued || c->entry.state.load()==NativeHoldRehearsal<>::holding)c->entry.context_mask=context_mask | (stop_.load()?512u:0u);
            if(c->entry.run(observed.base,object,GetCurrentThreadId(),observed.clock.identity,
                observed.clock.frame,boundary_frame.load(),now,clean_stop?core_valid:valid,true,clean_stop))return true;
            // A hold cannot produce a missing local battle record. Reject
            // before touching the clock instead of waiting for ourselves.
            if(c->confirmation.state.load(std::memory_order_acquire)==NativeHoldRehearsal<>::queued &&
               observed.clock.frame>=c->confirmation.target_clock() && c->runner.exchange.local_count.load()!=3){
                c->runner.failure_reason=10;c->runner.failed=true;c->runner.release();
            }
            const bool neutral=c->runner.exchange.input_rejection.load()==2 && c->runner.stage.load()==0 &&
                !c->runner.restored.load() && !c->runner.injected.load() && !c->runner.restart.load();
            const bool cancel=neutral || NativeHoldRehearsal<>::clean_cancel(
                stop_.load() || c->runner.failed.load() || c->runner.exchange.failed.load(),
                c->runner.stage.load(),c->runner.restored.load(),c->runner.injected.load(),c->runner.restart.load());
            if(c->confirmation.state.load()==NativeHoldRehearsal<>::queued || c->confirmation.state.load()==NativeHoldRehearsal<>::holding)c->confirmation.context_mask=context_mask | (stop_.load()?512u:0u) |
                (c->runner.failed.load()?1024u:0u) | (c->runner.exchange.failed.load()?2048u:0u);
            return c->confirmation.run(observed.base,object,GetCurrentThreadId(),observed.clock.identity,
                observed.clock.frame,boundary_frame.load(),now,cancel?core_valid:(valid && !c->runner.failed.load() && !c->runner.exchange.failed.load()),
                c->runner.exchange.ready.load(),cancel);
        }
        if(hold_test.run(observed.base,object,GetCurrentThreadId(),observed.clock.identity,
            observed.clock.frame,boundary_frame.load(),now,valid))return true;
        if(correction_enabled.load(std::memory_order_acquire)){
            if(confirmation_hold.state.load()==NativeHoldRehearsal<>::queued &&
               observed.clock.frame>=confirmation_hold.target_clock() && correction->exchange.local_count.load()!=3){
                correction->failure_reason=10;correction->failed=true;correction->release();
            }
            // Worker rejection may request stop before this owner callback. Stop
            // is not a native-state fault: cancel at the unchanged frontier so
            // the next frame can retire the captured snapshots and resources.
            const bool input_cancel=correction->exchange.failed.load() &&
                correction->exchange.input_rejection.load()!=0 && correction->stage.load()==0 &&
                !correction->restored.load() && !correction->injected.load() && !correction->restart.load();
            const bool cancel_valid=observed.valid && observed.object==object &&
                observed.thread==GetCurrentThreadId() && observed.thread==owner_thread.load() &&
                now>=observed.tick && now-observed.tick<=250 && !retired.load() && !failed.load() &&
                !resources_.cancelled() && resources_.ready();
            return confirmation_hold.run(observed.base,object,GetCurrentThreadId(),observed.clock.identity,
            observed.clock.frame,boundary_frame.load(),now,
            input_cancel?cancel_valid:(valid && !correction->failed.load() && !correction->exchange.failed.load()),
            correction->exchange.ready.load(std::memory_order_acquire),input_cancel);
        }
        return false;
    }
    // Historical evidence only: this does not authorize clock ownership.
    std::atomic<bool> clock_ready{};
    std::atomic<unsigned> clock_identity{},clock_frame{},clock_slot{};
    std::atomic<uint64_t> clock_started{};
    std::atomic<unsigned> clock_samples{},clock_changes{},clock_resets{},clock_object_changes{};
    std::atomic<unsigned> clock_first{},clock_last{},cursor_first{},cursor_last{},all_cursor_first{},all_cursor_last{};
    std::atomic<unsigned> battle_samples{},battle_first{},battle_last{};
private:
    uintptr_t sampled_object_{};
    unsigned sampled_identity_{};
public:
    void clock_entry(const NativeClockDiscovery::Observation& observed,uintptr_t object,uint64_t now){
        if(stop_.load(std::memory_order_acquire) || retired.load() || failed.load() ||
            !ready.load(std::memory_order_acquire) || !observed.valid || observed.object!=object ||
            now<observed.tick || now-observed.tick>250 || observed.thread!=GetCurrentThreadId() ||
            observed.thread!=owner_thread.load() ||
            (observed.clock.local_mask!=1 && observed.clock.local_mask!=2) ||
            resources_.cancelled() || !resources_.ready())return;
        // Bounded owner-thread observations. Never infer frame mapping merely
        // from the two counters having similar values.
        const auto samples=clock_samples.load();
        if(samples<512){
            if(!samples){
                clock_started=now;clock_first=observed.clock.frame;
                cursor_first=observed.clock.local_cursor;all_cursor_first=observed.clock.all_cursor;
            }else{
                if(sampled_object_!=object)++clock_object_changes;
                if(sampled_identity_!=observed.clock.identity || observed.clock.frame<clock_last.load())++clock_resets;
                if(observed.clock.frame!=clock_last.load())++clock_changes;
            }
            sampled_object_=object;sampled_identity_=observed.clock.identity;
            clock_last=observed.clock.frame;cursor_last=observed.clock.local_cursor;all_cursor_last=observed.clock.all_cursor;
            clock_samples=samples+1;
        }
        pairs.clock(observed.clock.identity,observed.clock.frame);
        clock_identity=observed.clock.identity;clock_frame=observed.clock.frame;
        clock_slot=observed.clock.local_mask;clock_ready.store(true,std::memory_order_release);
    }
    std::atomic<bool> first_match_requested{},first_match_verified{};
    std::atomic<unsigned> first_match_thread{};
    std::atomic<bool> ready{},retired{},failed{};
    std::atomic<unsigned> cancellation{},boundary_frame{},owner_thread{};
    std::atomic<unsigned> cancel_thread{},cancel_expected{},cancel_had_refs{},deferred{},deferred_thread{};
    bool start(uintptr_t base,bool first_match=false){
        first_match_requested=first_match;
        if constexpr(requires {resources_.start(base,first_match);})return resources_.start(base,first_match);
        else return !first_match && resources_.start(base);
    }
    ReplayDependencySource source_on_owner(){
        if(GetCurrentThreadId()!=owner_thread.load() || stop_.load() || retired.load() || failed.load() || !ready.load())return {};
        if constexpr(requires {resources_.source();})return resources_.source();
        else return {};
    }
    bool stop_requested()const{return stop_.load(std::memory_order_acquire);}
    void request_stop(){stop_.store(true,std::memory_order_release);}
    void frame(unsigned frame,bool boundary_valid){
        if(retired.load())return;
        deferred=resources_.deferred_callbacks.load();deferred_thread=resources_.last_deferred_thread.load();
        if(boundary_valid){
            if constexpr(requires {resources_.confirm_boundary();})resources_.confirm_boundary();
        }
        if constexpr(requires {resources_.provisional_thread();resources_.first_boundary_verified.load();}){
            first_match_thread=resources_.provisional_thread();first_match_verified=resources_.first_boundary_verified.load();
        }
        if(resources_.cancelled()){
            const auto& trace=resources_.cancellation();
            cancellation=trace.reason.load();cancel_thread=trace.thread.load();cancel_expected=trace.expected.load();cancel_had_refs=trace.had_owned.load();
            failed=true;request_stop();
        }
        if(stop_.load(std::memory_order_acquire)){
            if(cycles_enabled.load(std::memory_order_acquire) && !cycles->release()){failed=true;return;}
            if(correction_enabled.load(std::memory_order_acquire) && !correction->release()){failed=true;return;}
            if(replay_enabled.load(std::memory_order_acquire) && !replay->release()){failed=true;return;}
            if(!snapshots.release()){failed=true;return;}
            if(resources_.stop())retired.store(true,std::memory_order_release);
            else failed=true;
            return;
        }
        if(boundary_valid && resources_.ready()){
            if(clock_ready.load())pairs.battle(frame);
            if(clock_ready.load() && battle_samples.load()<512){
                if(!battle_samples.load())battle_first=frame;
                battle_last=frame;++battle_samples;
            }
            boundary_frame=frame;owner_thread=GetCurrentThreadId();ready.store(true,std::memory_order_release);
        }
    }
};
inline std::atomic<CombatResourcePreparation<>*> combat_resource_preparation{};
}
