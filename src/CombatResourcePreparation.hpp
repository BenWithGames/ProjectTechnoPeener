#pragma once
#include "ReplayOwnedResources.hpp"
#include "NativeClockDiscovery.hpp"
#include "StartupBoundaryPairs.hpp"
#include "NativeHoldRehearsal.hpp"
#include "StartupSnapshotCapture.hpp"
#include "StartupReferenceReplay.hpp"
#include "StartupPredictionCorrection.hpp"
namespace sc6::research {
// Resource callbacks acquire references. Only the game-frame thread inspects
// readiness or releases them; the network worker can only request retirement.
template<class Resources=ReplayOwnedResources> class CombatResourcePreparation {
    Resources resources_;
    std::atomic<bool> stop_{};
public:
    StartupBoundaryPairs pairs;
    NativeHoldRehearsal<> hold_test;
    StartupSnapshotCapture<> snapshots;
    std::atomic<bool> capture_enabled{true};
    std::unique_ptr<StartupReferenceReplay<>> replay;
    std::atomic<bool> replay_enabled{};
    std::unique_ptr<StartupPredictionCorrection> correction;
    std::atomic<bool> correction_enabled{};
    NativeHoldRehearsal<> confirmation_hold;
    void enable_correction(){if(!correction_enabled.load()){correction=std::make_unique<StartupPredictionCorrection>();correction_enabled.store(true,std::memory_order_release);}}
    bool request_confirmation_hold(unsigned identity,unsigned clock,unsigned battle,unsigned slot){
        if(!correction_enabled.load(std::memory_order_acquire) || clock>UINT32_MAX-3 || battle>UINT32_MAX-3)return false;
        if(!correction->exchange.configure(battle,slot))return false;
        return confirmation_hold.state.load()!=0 || confirmation_hold.request(identity,clock+3,battle+3,2000);
    }
    void enable_replay(){if(!replay_enabled.load()){replay=std::make_unique<StartupReferenceReplay<>>();replay_enabled.store(true,std::memory_order_release);}}
    bool replay_restart()const{return (replay_enabled.load(std::memory_order_acquire) && replay->restart.load()) ||
        (correction_enabled.load(std::memory_order_acquire) && (correction->restart.load() || confirmation_hold.state.load()==5));}
    void replay_boundary(const RollbackBoundaryKey& key,uintptr_t base,uintptr_t argument,SingleUpdateCall::Routine original){
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
        const bool valid=observed.valid && observed.object==object && observed.thread==GetCurrentThreadId() &&
            observed.thread==owner_thread.load() && now>=observed.tick && now-observed.tick<=250 &&
            !stop_.load() && !retired.load() && !failed.load() && !resources_.cancelled() && resources_.ready();
        if(hold_test.run(observed.base,object,GetCurrentThreadId(),observed.clock.identity,
            observed.clock.frame,boundary_frame.load(),now,valid))return true;
        if(correction_enabled.load(std::memory_order_acquire))return confirmation_hold.run(observed.base,object,GetCurrentThreadId(),observed.clock.identity,
            observed.clock.frame,boundary_frame.load(),now,valid && !correction->failed.load() && !correction->exchange.failed.load(),
            correction->exchange.ready.load(std::memory_order_acquire));
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
