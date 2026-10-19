#pragma once
#include "ReplayOwnedResources.hpp"
#include "NativeClockDiscovery.hpp"
#include "NativePredictionFrontier.hpp"
#include "StartupBoundaryPairs.hpp"
#include "RetryBoundaryObservation.hpp"
#include "NativeHoldRehearsal.hpp"
#include "StartupSnapshotCapture.hpp"
#include "TargetedRetentionCapture.hpp"
#include "RetainedObjectReplay.hpp"
#include "StartupReferenceReplay.hpp"
#include "StartupPredictionCorrection.hpp"
#include "CorrectionCycleBank.hpp"
#include <sstream>
namespace sc6::research {
// Resource callbacks acquire references. Only the game-frame thread inspects
// readiness or releases them; the network worker can only request retirement.
template<class Resources=ReplayOwnedResources> class CombatResourcePreparation {
    Resources resources_;
    std::array<std::unique_ptr<Resources>,15> successors_;
    std::atomic<unsigned> successor_count_{};
    std::atomic<Resources*> current_{};
    Resources& resource(){auto* value=current_.load(std::memory_order_acquire);return value?*value:resources_;}
    const Resources& resource()const{auto* value=current_.load(std::memory_order_acquire);return value?*value:resources_;}
    std::atomic<bool> stop_{};
public:
    std::atomic<bool> live_prediction_enabled{};
    bool enable_live_prediction(){
        if(failed.load() || stop_.load() || retired.load())return false;
        if(live_prediction_enabled.load())return true;
        if constexpr(requires {resource().enable_wind();resource().enable_animation_copy();}){
            if(!resource().enable_wind() || !resource().enable_animation_copy()){failed=true;return false;}
        }else {failed=true;return false;}
        capture_enabled=false;
        live_prediction_enabled.store(true,std::memory_order_release);return true;
    }
    template<class Stream> void wind_report(Stream& out)const{
        if constexpr(requires {resource().wind_report(out);}){
            const auto count=successor_count_.load(std::memory_order_acquire);
            out<<"continuous_resource_generations="<<count+1<<'\n';
            for(unsigned i=0;i<count;++i){
                const auto& old=i?*successors_[i-1]:resources_;
                std::ostringstream text;old.wind_report(text);std::istringstream lines(text.str());std::string line;
                while(std::getline(lines,line))out<<"retired_resources_"<<i<<'_'<<line<<'\n';
            }
            (count?*successors_[count-1]:resources_).wind_report(out);
        }
    }
    std::unique_ptr<RetainedObjectReplay<>> retained_replay;
    std::atomic<bool> retained_replay_enabled{};
    void enable_retained_replay(bool same_session_recording=false,bool repeated_recording=false,bool multi_round=false,bool continuous_match=false){
        if(retained_replay_enabled.load())return;
        if constexpr(requires {resource().enable_wind();}){
            if(!resource().enable_wind()){failed=true;return;}
        }else {failed=true;return;}
        if constexpr(requires {resource().enable_animation_copy();})if(!resource().enable_animation_copy()){failed=true;return;}
        retained_replay=std::make_unique<RetainedObjectReplay<>>(same_session_recording,repeated_recording,multi_round,multi_round,continuous_match);
        retained_replay->prepare_diagnostic_storage();
        retained_replay_enabled.store(true,std::memory_order_release);
    }
    NativePredictionFrontier::Observation prediction_frontier;
    StartupBoundaryPairs pairs;
    RetryBoundaryObservation retry_observation;
    NativeHoldRehearsal<> hold_test;
    StartupSnapshotCapture<> snapshots;
    TargetedRetentionCapture<> retention;
    std::atomic<bool> retention_enabled{};
    void enable_retention(){
        if(retention_enabled.load())return;
        if constexpr(requires {resource().enable_wind(false);}){
            if(!resource().enable_wind(false)){failed=true;return;}
        }else {failed=true;return;}
        retention_enabled.store(true,std::memory_order_release);
    }
    unsigned retention_result()const{
        if constexpr(requires {resource().retention_result();})return resource().retention_result();
        return 0;
    }
    std::atomic<bool> capture_enabled{true};
    std::unique_ptr<StartupReferenceReplay<>> replay;
    std::atomic<bool> replay_enabled{};
    std::unique_ptr<StartupPredictionCorrection> correction;
    std::atomic<bool> correction_enabled{};
    std::unique_ptr<CorrectionCycleBank<>> cycles;
    std::atomic<bool> cycles_enabled{};
    unsigned owner_cycle_{};
    RollbackBoundaryKey cycle_identity_; // owner thread only
    void enable_cycles(bool attack=false,bool combat=false,unsigned hold_ms=0){if(!cycles_enabled.load()){
        if(combat){if constexpr(requires {resource().enable_wind();})if(!resource().enable_wind()){failed=true;return;}}
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
        if(retained_replay_enabled.load(std::memory_order_acquire) && retained_replay->restart.load())return true;
        if(cycles_enabled.load(std::memory_order_acquire)){
            for(unsigned i=0;i<cycles->active();++i){const auto& c=cycles->at(i);
                if(c.runner.restart.load() || c.entry.state.load()==5 || c.confirmation.state.load()==5)return true;
            }
        }
        return (replay_enabled.load(std::memory_order_acquire) && replay->restart.load()) ||
        (correction_enabled.load(std::memory_order_acquire) && (correction->restart.load() || confirmation_hold.state.load()==5));}
    void replay_boundary(const RollbackBoundaryKey& key,uintptr_t base,uintptr_t argument,SingleUpdateCall::Routine original,const RoundFightReadiness& fight={}){
        if(retained_replay_enabled.load(std::memory_order_acquire)){
            if(hold_test.state.load()!=NativeHoldRehearsal<>::released || stop_.load() || retired.load() || failed.load() || retained_replay->retired.load())return;
            auto source=resource().source();retained_replay->step(key,base,argument,source,original,fight);return;
        }
        if(cycles_enabled.load(std::memory_order_acquire)){
            auto* c=cycles->current();
            if(!c || c->entry.state.load()!=NativeHoldRehearsal<>::released || stop_.load() || retired.load() || failed.load())return;
            // Terminal evidence belongs to the completed cycle. A later round
            // must not mutate it or re-enter its released resources.
            if(c->runner.retired.load(std::memory_order_acquire) || c->runner.failed.load() ||
               c->runner.restart.load())return;
            if(owner_cycle_!=cycles->active()){owner_cycle_=cycles->active();cycle_identity_=key;}
            if(!cycle_identity_.same_round(key)){c->runner.failure_reason=8;c->runner.failed=true;c->runner.release();return;}
            if(!c->runner.captured.load() && key.frame!=c->entry.target_battle()){
                c->runner.failure_reason=9;c->runner.failed=true;c->runner.release();return;
            }
            auto source=resource().source();c->runner.step(key,base,argument,source,original,c->confirmation.state.load()==3);return;
        }
        if(correction_enabled.load(std::memory_order_acquire) && hold_test.state.load()==NativeHoldRehearsal<>::released &&
           !stop_.load() && !retired.load() && !failed.load()){
            if(!correction->captured.load() && key.frame!=hold_test.target_battle()){correction->failed=true;correction->release();return;}
            auto source=resource().source();correction->step(key,base,argument,source,original,confirmation_hold.state.load()==3);return;
        }
        if(!replay_enabled.load(std::memory_order_acquire) || hold_test.state.load()!=NativeHoldRehearsal<>::released ||
            stop_.load() || retired.load() || failed.load())return;
        if(!replay->captured.load() && key.frame!=hold_test.target_battle()){replay->failed=true;replay->release();return;}
        auto source=resource().source();replay->step(key,base,argument,source,original);
    }
    void capture_boundary(const RollbackBoundaryKey& key,uintptr_t base,uintptr_t argument){
        if(retention_enabled.load(std::memory_order_acquire)){
            if(hold_test.state.load()!=NativeHoldRehearsal<>::released || stop_.load() || retired.load() || failed.load() || retention.retired.load())return;
            auto source=resource().source();retention.step(key,base,argument,source);return;
        }
        if(!capture_enabled.load() || hold_test.state.load()!=NativeHoldRehearsal<>::released || stop_.load() || retired.load() || failed.load() ||
            snapshots.retired.load() || snapshots.failed.load())return;
        if(!snapshots.captured.load() && key.frame!=hold_test.target_battle()){
            snapshots.failed=true;snapshots.release();return;
        }
        auto source=resource().source();snapshots.capture(key,base,argument,source);
    }
    bool run_hold(const NativeClockDiscovery::Observation& observed,uintptr_t object,uint64_t now){
        const unsigned context_mask=(!observed.valid?1u:0u) | (observed.object!=object?2u:0u) |
            (observed.thread!=GetCurrentThreadId()?4u:0u) | (observed.thread!=owner_thread.load()?8u:0u) |
            (now<observed.tick || now-observed.tick>250?16u:0u) | (retired.load()?32u:0u) |
            (failed.load()?64u:0u) | (resource().cancelled()?128u:0u) | (!resource().ready()?256u:0u);
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
            const bool held=c->confirmation.run(observed.base,object,GetCurrentThreadId(),observed.clock.identity,
                observed.clock.frame,boundary_frame.load(),now,cancel?core_valid:(valid && !c->runner.failed.load() && !c->runner.exchange.failed.load()),
                c->runner.exchange.ready.load(),cancel);
            // A boundary can invalidate an unstarted attempt. Retire on the owner
            // thread before publishing permission to negotiate a fresh window.
            if(c->entry.state.load()==4 && c->confirmation.state.load()==4 &&
               !c->runner.captured.load() && !c->runner.stage.load() && !c->runner.restored.load() &&
               !c->runner.injected.load() && !c->runner.restart.load() && c->runner.release()){
                c->runner.safe_rejection=true;c->runner.failed=false;c->runner.stop_outcome=2;
            }
            return held;
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
                !resource().cancelled() && resource().ready();
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
            resource().cancelled() || !resource().ready())return;
        if(retained_replay_enabled.load(std::memory_order_acquire))prediction_frontier.observe(observed.base,object);
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
        retry_observation.clock(observed.clock.identity,observed.clock.frame);
        clock_identity=observed.clock.identity;clock_frame=observed.clock.frame;
        clock_slot=observed.clock.local_mask;clock_ready.store(true,std::memory_order_release);
    }
    std::atomic<bool> first_match_requested{},first_match_verified{};
    std::atomic<unsigned> first_match_thread{};
    std::atomic<bool> match_end_requested{},match_end_clean{};
    std::atomic<unsigned> match_end_vtable{},match_end_handler{},match_end_thread{},match_end_frame{},match_end_state_query{};
    std::atomic<bool> ready{},retired{},failed{};
    std::atomic<unsigned> cancellation{},boundary_frame{},owner_thread{};
    std::atomic<unsigned> cancel_thread{},cancel_expected{},cancel_had_refs{},deferred{},deferred_thread{};
    bool start(uintptr_t base,bool first_match=false){
        first_match_requested=first_match;
        if constexpr(requires {resource().start(base,first_match);})return resource().start(base,first_match);
        else return !first_match && resource().start(base);
    }
    ReplayDependencySource source_on_owner(){
        if(GetCurrentThreadId()!=owner_thread.load() || stop_.load() || retired.load() || failed.load() || !ready.load())return {};
        if constexpr(requires {resource().source();})return resource().source();
        else return {};
    }
    ReplayDependencySource successor_source_on_owner(){
        if(GetCurrentThreadId()!=owner_thread.load() || stop_.load() || retired.load() || failed.load() || !ready.load())return {};
        if constexpr(requires(Resources& next,Resources& prior){next.start_successor(prior);}){
            const auto count=successor_count_.load();
            if(count==successors_.size())return {};
            auto& prior=resource();
            auto next=std::make_unique<Resources>();
            if(!next->start_successor(prior))return {};
            auto* pointer=next.get();successors_[count]=std::move(next);
            successor_count_.store(count+1,std::memory_order_release);
            current_.store(pointer,std::memory_order_release);
            // Fresh leases are already held and callback ownership transferred.
            // Preserve the old object for diagnostics, not duplicate live leases.
            if(!prior.stop()){failed=true;return {};}
            return pointer->source();
        }else return {};
    }
    bool stop_requested()const{return stop_.load(std::memory_order_acquire);}
    void request_stop(){stop_.store(true,std::memory_order_release);}
    void frame(unsigned frame,bool boundary_valid){
        if(retired.load())return;
        deferred=resource().deferred_callbacks.load();deferred_thread=resource().last_deferred_thread.load();
        if(boundary_valid){
            if constexpr(requires {resource().confirm_boundary();})resource().confirm_boundary();
        }
        if constexpr(requires {resource().provisional_thread();resource().first_boundary_verified.load();}){
            first_match_thread=resource().provisional_thread();first_match_verified=resource().first_boundary_verified.load();
        }
        bool normal_end=false;
        if constexpr(requires {resource().match_end_observation();}){
            if(retained_replay_enabled.load() && retained_replay->multi_round){
                const auto end=resource().match_end_observation();
                match_end_vtable=end.vtable;match_end_handler=end.handler;match_end_thread=end.thread;match_end_state_query=end.state_query;
                normal_end=end.cleanup_allowed(owner_thread.load(),GetCurrentThreadId(),failed.load(),
                    retained_replay->clean_match_end_boundary(),resource().cancelled()?resource().cancellation().reason.load():0);
                if(normal_end){match_end_frame=frame;match_end_requested=true;request_stop();}
            }
        }
        if(resource().cancelled()){
            const auto& trace=resource().cancellation();
            cancellation=trace.reason.load();cancel_thread=trace.thread.load();cancel_expected=trace.expected.load();cancel_had_refs=trace.had_owned.load();
            if(!normal_end)failed=true;request_stop();
        }
        if(stop_.load(std::memory_order_acquire)){
            if(cycles_enabled.load(std::memory_order_acquire) && !cycles->release()){failed=true;return;}
            if(correction_enabled.load(std::memory_order_acquire) && !correction->release()){failed=true;return;}
            if(replay_enabled.load(std::memory_order_acquire) && !replay->release()){failed=true;return;}
            if(retained_replay_enabled.load() && !(normal_end?retained_replay->finish_match():retained_replay->release())){failed=true;return;}
            if(retention_enabled.load() && !retention.release()){failed=true;return;}
            if(!snapshots.release()){failed=true;return;}
            if(resource().stop()){
                if(match_end_requested.load() && !failed.load())match_end_clean=true;
                retired.store(true,std::memory_order_release);
            }
            else failed=true;
            return;
        }
        if(boundary_valid && resource().ready()){
            if(clock_ready.load()){pairs.battle(frame);retry_observation.battle(frame,GetTickCount64());}
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

