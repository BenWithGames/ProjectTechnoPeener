#pragma once
#include "OwnedPredictionCorrection.hpp"
#include <atomic>
#include "RoundFightReadiness.hpp"
#include "ReplayTiming.hpp"
#include "CaptureStorage.hpp"
namespace sc6::research {
// Reference inputs only; no prediction, no live-network input replacement.
template<class Backend=OwnedPredictionCorrection> class RetainedObjectReplay {
    Backend backend_;
    ReplayTiming timing_;
    RollbackBoundaryKey first_{};
    unsigned in_window_{},cycle_windows_{},next_capture_{};
    RollbackBoundaryKey last_cycle_{};
public:
    static constexpr unsigned limit=32,cycle_target=3;
    const bool same_session_recording,repeated_recording,multi_round,repeated_combat,continuous_match;
    bool waiting_round_{};
    std::atomic<unsigned> round_transitions{},partial_round_discards{},fight_wait_frames{},fight_partial_discards{};
    std::atomic<unsigned> fight_native_playback{},fight_round_result{},fight_playback_wait_frames{},fight_result_wait_frames{};
    std::atomic<unsigned> fight_status{},fight_vtable{},fight_handler{},fight_ready_frame{},fight_capture_frame{};
    explicit RetainedObjectReplay(bool recording=false,bool repeated=false,bool across_rounds=false,bool combat_cycles=false,bool full_match=false):same_session_recording(recording),repeated_recording(recording && repeated),multi_round(recording && repeated && across_rounds),repeated_combat(recording && repeated && across_rounds && combat_cycles),continuous_match(recording && repeated && across_rounds && combat_cycles && full_match){
        // Only the guarded inactive-weight reset is deferred before replay.
        // Native endpoint verification remains exact for these bytes.
        backend_.clean.defer_derived=true;
    }
    void prepare_diagnostic_storage(){
        auto scratch=std::make_shared<ReplayFailureEvidence::TraceScratch>();
        evidence.prepare_deferred(scratch);
        for(auto& cycle:cycles){
            cycle.replay_evidence.prepare_deferred(scratch);
            cycle.animation=std::make_unique<animation_copy::Snapshot>();
            cycle.rng=std::make_unique<inline_rng::Snapshot>();
        }
    }
    bool clean_match_end_boundary()const{
        return multi_round && (continuous_match || !in_window_) && (continuous_match || completed_cycles.load()>0) &&
            (waiting_round_ || repeated_combat || verified.load()) && !failed.load() && !restart.load();
    }
    unsigned target_cycles()const{if(continuous_match)return 0;return same_session_recording && !repeated_recording?1u:cycle_target;}
    std::array<std::unique_ptr<ReplayFailureEvidence>,4> recorded_snapshots;
    std::atomic<bool> recording_ready{};
    static constexpr unsigned recording_spacing_frames=120;
    struct CycleEvidence {
        std::array<std::unique_ptr<ReplayFailureEvidence>,4> snapshots;
        std::atomic<bool> snapshots_ready{};
        ReplayFailureEvidence replay_evidence;
        std::atomic<bool> ready{};
        unsigned number{},first_frame{},last_frame{},windows{},round{},event{},fight_frame{};
        std::atomic<bool> available{true};
        uint64_t generation{};
        uint64_t revived{};
        std::array<uint64_t,std::size(OwnedPredictionCorrection::diagnostic_names)> diagnostics{};
        std::unique_ptr<animation_copy::Snapshot> animation;
        std::unique_ptr<inline_rng::Snapshot> rng;
        mutable bool written{};
    };
    mutable std::array<CycleEvidence,cycle_target> cycles;
    mutable std::atomic<unsigned> evidence_flushed{},evidence_io_failures{};
    mutable std::atomic<bool> storage_blocked{};
    std::atomic<unsigned> evidence_wait_frames{},match_end_discards{};
    std::atomic<bool> match_completed{};
    CycleEvidence& active_cycle(){return cycles[completed_cycles.load()%cycle_target];}
    // Called only on the game owner thread after native terminal-state validation.
    bool finish_match(){
        if(!continuous_match)return release();
        if(failed.load() || restart.load())return release();
        if(in_window_){
            if(!backend_.clean.discard_unmodified()){stop(15);return false;}
            in_window_=0;++match_end_discards;
        }
        if(!backend_.clean.finish_effect_round()){stop(16);return false;}
        if(!release(false)){terminal_ready.store(true,std::memory_order_release);return false;}
        match_completed=true;verified=completed_cycles.load()>0;stage=4;
        terminal_ready.store(true,std::memory_order_release);return true;
    }
    std::atomic<unsigned> completed_cycles{};
    std::atomic<unsigned> windows{},captured{},no_event{},failure_reason{},stage{};
    std::atomic<bool> failed{},retired{},restored{},verified{},restart{},removal{},round_cancelled{};
    std::atomic<uint64_t> revived{};
    std::atomic<bool> terminal_ready{}; // acquire before publishing a terminal report
    bool terminal()const{return terminal_ready.load(std::memory_order_acquire);}
    ReplayFailureEvidence evidence;
    std::unique_ptr<animation_copy::Snapshot> animation_trace;
    std::unique_ptr<inline_rng::Snapshot> inline_trace;
    std::unique_ptr<timer_lookup::Snapshot> timer_trace;
    mutable bool timer_written{};
    mutable bool inline_written{};
    mutable bool animation_written{};
    std::atomic<bool> animation_ready{},animation_failed{};
    std::array<std::atomic<uint64_t>,std::size(OwnedPredictionCorrection::diagnostic_names)> diagnostics{};
    void publish(){
        if constexpr(requires {backend_.diagnostics();}){
            const auto values=backend_.diagnostics();
            for(unsigned i=0;i<values.size();++i)diagnostics[i]=values[i];
        }
    }
    bool release(bool finish=true){
        ReplayTiming::Scope timing_scope{timing_.rows[3][ReplayTiming::release]};
        if(retired.load())return true;
        if(!animation_ready.load() && !animation_failed.load()){
            try {auto captured_trace=std::make_unique<animation_copy::Snapshot>();animation_copy::snapshot(*captured_trace);animation_trace=std::move(captured_trace);inline_trace=std::make_unique<inline_rng::Snapshot>();inline_rng::snapshot(*inline_trace);timer_trace=std::make_unique<timer_lookup::Snapshot>();timer_lookup::snapshot(*timer_trace);animation_ready.store(true,std::memory_order_release);}
            catch(...){animation_failed=true;}
        }
        if(!backend_.clean.release_owned_dependencies()){failed=true;restart=true;if(finish)terminal_ready.store(true,std::memory_order_release);return false;}
        retired=true;if(finish)terminal_ready.store(true,std::memory_order_release);return true;
    }
    void stop(unsigned reason){
        failure_reason=reason;
        restored=restored.load() || backend_.clean.any_restore_invoked();
        restart=backend_.clean.rewind_started() || restored.load();
        publish();
        // Keep the safety restart latch immediate, but do not publish completion
        // until the evidence and retirement attempt are immutable and visible.
        try {if constexpr(requires {backend_.capture_failure_evidence(evidence);})backend_.capture_failure_evidence(evidence);}
        catch(...){evidence.state.store(2,std::memory_order_release);}
        release(false);
        failed=true;
        terminal_ready.store(true,std::memory_order_release);
    }
    void step(const RollbackBoundaryKey& key,uintptr_t base,uintptr_t argument,ReplayDependencySource& source,SingleUpdateCall::Routine original,const RoundFightReadiness& fight={}){
        if(retired.load() || failed.load())return;
        const auto timing_cycle=completed_cycles.load();
        timing_.arrival(timing_cycle);
        ReplayTiming::Scope timing_scope{timing_.rows[timing_cycle<3?timing_cycle:3][ReplayTiming::step]};
        try {
            backend_.stage=Backend::Stage::clean_rewind;
            if(!key.same_round(key) || key.thread!=GetCurrentThreadId()){stop(1);return;}
            if(!first_.session)first_=key;
            if(!first_.same_round(key)){
                const bool next_round=multi_round && key.session==first_.session && key.peer==first_.peer &&
                    key.slot==first_.slot && key.thread==first_.thread && first_.round!=UINT32_MAX &&
                    key.round==first_.round+1 && key.generation>first_.generation;
                if(!next_round){round_cancelled=true;release();return;}
                // Never carry a partial snapshot window into the next round.
                if(in_window_){if(!timing_.measure(timing_cycle,ReplayTiming::discard,[&]{return backend_.clean.discard_unmodified();})){stop(15);return;}++partial_round_discards;}
                if(!timing_.measure(timing_cycle,ReplayTiming::round_cleanup,[&]{return backend_.clean.finish_effect_round();})){stop(16);return;}
                first_=key;in_window_=cycle_windows_=next_capture_=0;waiting_round_=false;++round_transitions;
            }
            if(multi_round && waiting_round_)return;
            if(multi_round){
                fight_native_playback=fight.native_playback;fight_round_result=fight.round_result;
                fight_status=fight.status;fight_vtable=fight.vtable;fight_handler=fight.handler;
                if(!fight.permits(key.round,key.generation,key.frame,key.thread)){
                    ++fight_wait_frames;
                    if(fight.native_playback)++fight_playback_wait_frames;
                    if(fight.round_result)++fight_result_wait_frames;
                    if(in_window_){
                        if(!timing_.measure(timing_cycle,ReplayTiming::discard,[&]{return backend_.clean.discard_unmodified();})){stop(15);return;}
                        in_window_=0;++fight_partial_discards;
                        if(!timing_.measure(timing_cycle,ReplayTiming::round_cleanup,[&]{return backend_.clean.finish_effect_round();})){stop(16);return;}
                    }
                    return;
                }
                if(!in_window_){fight_ready_frame=fight.frame;fight_capture_frame=key.frame;}
            }
            if(repeated_recording && (!multi_round || repeated_combat) && !in_window_ && completed_cycles.load()){
                if(key.same_round(first_) && key.round==last_cycle_.round && key.frame<=last_cycle_.frame){stop(13);return;}
                if(key.frame<next_capture_)return;
            }
            if(continuous_match && !in_window_ && (storage_blocked.load() || !active_cycle().available.load(std::memory_order_acquire))){++evidence_wait_frames;return;}
            if(continuous_match && !in_window_)active_cycle().number=completed_cycles.load()+1;
            stage=1;
            if(!timing_.measure(timing_cycle,ReplayTiming::capture,[&]{return backend_.clean.capture(key,base,argument,NativeBoundarySnapshot::Mode::general_combat,true,&source);})){stop(2);return;}
            ++captured;if(++in_window_!=4)return;
            ++windows;++cycle_windows_;const int observed=backend_.clean.retained_removal();
            if(observed<0){stop(3);return;}
            if(!observed && !same_session_recording){
                if(!timing_.measure(timing_cycle,ReplayTiming::discard,[&]{return backend_.clean.discard_unmodified();})){stop(4);return;}
                ++no_event;in_window_=0;
                if(cycle_windows_==limit)release();return;
            }
            removal=observed!=0;
            if(same_session_recording){
                if constexpr(requires {backend_.capture_owned_recording(recorded_snapshots);}){
                    auto& cycle=active_cycle();
                    auto& snapshots=(continuous_match || completed_cycles.load())?cycle.snapshots:recorded_snapshots;
                    if(!timing_.measure(timing_cycle,ReplayTiming::archive,[&]{return backend_.capture_owned_recording(snapshots);})){stop(11);return;}
                    if(!continuous_match && !completed_cycles.load())recording_ready.store(true,std::memory_order_release);
                    else cycle.snapshots_ready.store(true,std::memory_order_release);
                }else {stop(11);return;}
            }
            stage=2;const auto before=backend_.clean.retained_revivals();
            const bool rewound=timing_.measure(timing_cycle,ReplayTiming::restore,[&]{return backend_.clean.rewind(key);});
            restored=restored.load() || backend_.clean.any_restore_invoked();
            const auto cycle_revivals=backend_.clean.retained_revivals()-before;
            revived.fetch_add(cycle_revivals);
            publish();if(!rewound){stop(5);return;}
            // Always replay after a successful rewind, even if revival evidence
            // is unexpectedly absent; do not leave the game in the past.
            stage=3;backend_.stage=Backend::Stage::reference_replay;
            if(!timing_.measure(timing_cycle,ReplayTiming::replay,[&]{return backend_.clean.replay_reference(original);})){stop(6);return;}
            publish();if(!cycle_revivals && !same_session_recording){stop(7);return;}
            if(same_session_recording){
                // Preserve the native expected/actual streams even on success.
                auto& captured_evidence=active_cycle().replay_evidence;
                if constexpr(requires {backend_.capture_failure_evidence(captured_evidence);})timing_.measure(timing_cycle,ReplayTiming::evidence,[&]{backend_.capture_failure_evidence(captured_evidence);});
                if(captured_evidence.state.load(std::memory_order_acquire)!=1){stop(12);return;}
            }
            auto& cycle=active_cycle();
            cycle.number=completed_cycles.load()+1;
            cycle.fight_frame=multi_round?fight_ready_frame.load():0;
            cycle.round=key.round;cycle.event=key.event;cycle.generation=key.generation;
            cycle.first_frame=key.frame-3;cycle.last_frame=key.frame;cycle.windows=cycle_windows_;cycle.revived=cycle_revivals;
            for(unsigned i=0;i<diagnostics.size();++i)cycle.diagnostics[i]=diagnostics[i].load();
            {ReplayTiming::Scope trace_scope{timing_.rows[timing_cycle<3?timing_cycle:3][ReplayTiming::traces]};
            if(!cycle.animation)cycle.animation=std::make_unique<animation_copy::Snapshot>();animation_copy::snapshot(*cycle.animation);
            if(!cycle.rng)cycle.rng=std::make_unique<inline_rng::Snapshot>();inline_rng::snapshot(*cycle.rng);}
            // Retire each verified window on its owner thread before capturing another.
            // recycle retires snapshots AND their dependencies. Terminal release
            // poisons pool slots and must never precede reusable retirement.
            if(!timing_.measure(timing_cycle,ReplayTiming::retire,[&]{return backend_.clean.recycle();})){stop(10);return;}
            if(multi_round && !timing_.measure(timing_cycle,ReplayTiming::round_cleanup,[&]{return backend_.clean.finish_effect_round();})){stop(16);return;}
            if(continuous_match)cycle.available.store(false,std::memory_order_relaxed);
            last_cycle_=key;
            cycle.ready.store(true,std::memory_order_release);
            ++completed_cycles;in_window_=0;cycle_windows_=0;
            if(continuous_match || completed_cycles.load()<target_cycles()){
                if(multi_round && !repeated_combat){waiting_round_=true;}
                else if(repeated_recording){
                    if(key.frame>UINT32_MAX-recording_spacing_frames){stop(14);return;}
                    next_capture_=key.frame+recording_spacing_frames;
                }
                stage=1;return;
            }
            if(!release(false)){terminal_ready.store(true,std::memory_order_release);return;}
            verified=true;stage=4;terminal_ready.store(true,std::memory_order_release);
        }catch(...){stop(8);}
    }
    template<class Stream> void report(Stream& out,const std::filesystem::path& report_path)const{
        {ReplayTiming::Scope io_scope{timing_.rows[3][ReplayTiming::report_io]};
        out<<"suite_retained_replay_terminal_ready="<<terminal()<<"\nsuite_retained_replay_evidence_state="<<evidence.state.load(std::memory_order_acquire)<<'\n';
        out<<"suite_evidence_deferred="<<evidence.deferred()<<"\nsuite_evidence_arena_limit="<<ReplayFailureEvidence::limit<<'\n';
        out<<"suite_repeated_combat_replay="<<repeated_combat<<'\n';
        out<<"suite_multi_round_replay="<<multi_round<<"\nsuite_replay_round_transitions="<<round_transitions.load()<<"\nsuite_partial_round_discards="<<partial_round_discards.load()<<'\n';
        {const auto camera=camera_calc::stats();out<<"suite_camera_calc_schema=1\nsuite_camera_calc_installed="<<camera.installed<<"\nsuite_camera_calc_events="<<camera.total<<"\nsuite_camera_calc_dropped="<<camera.dropped<<"\nsuite_camera_calc_overwritten="<<(camera.total-camera.count)<<'\n';}
        out<<"suite_fight_native_playback="<<fight_native_playback.load()<<"\nsuite_fight_round_result="<<fight_round_result.load()<<"\nsuite_fight_playback_wait_frames="<<fight_playback_wait_frames.load()<<"\nsuite_fight_result_wait_frames="<<fight_result_wait_frames.load()<<'\n';
        out<<"suite_fight_gate_schema=2\nsuite_fight_gate_enabled="<<multi_round<<"\nsuite_fight_wait_frames="<<fight_wait_frames.load()<<"\nsuite_fight_partial_discards="<<fight_partial_discards.load()<<"\nsuite_fight_status="<<fight_status.load()<<"\nsuite_fight_vtable_rva="<<fight_vtable.load()<<"\nsuite_fight_handler_rva="<<fight_handler.load()<<"\nsuite_fight_ready_frame="<<fight_ready_frame.load()<<"\nsuite_fight_capture_frame="<<fight_capture_frame.load()<<'\n';
        out<<"suite_recording_spacing_frames="<<(repeated_recording && (!multi_round || repeated_combat)?recording_spacing_frames:0)<<'\n';
        out<<"suite_same_session_recording="<<same_session_recording<<"\nsuite_recorded_snapshots_ready="<<recording_ready.load(std::memory_order_acquire)<<'\n';
        if(recording_ready.load(std::memory_order_acquire))for(unsigned i=0;i<recorded_snapshots.size();++i){
            out<<"suite_recorded_snapshot_"<<i<<"_written="<<recorded_snapshots[i]->write(report_path.parent_path()/(report_path.stem().string()+"-owned-snapshot-"+std::to_string(i)+".bin"))<<'\n';
        }
        out<<"suite_retained_replay_cycle_target="<<target_cycles()<<"\nsuite_retained_replay_cycles_completed="<<completed_cycles.load()<<'\n';
        for(unsigned n=0;n<cycles.size();++n){
            auto& c=cycles[n];
            if(continuous_match && !c.ready.load(std::memory_order_acquire) && !terminal())continue;
            if(continuous_match && c.available.load(std::memory_order_acquire) && !terminal())continue;
            const auto number=continuous_match?c.number:n+1;
            bool io_ok=true;
            if(continuous_match && (c.snapshots_ready.load() || c.ready.load())){
                if(!prune_verified_cycle(report_path,number) || !capture_storage_available(report_path.parent_path())){
                    storage_blocked=true;++evidence_io_failures;continue;
                }
            }
            // Snapshot evidence may be published before a later replay failure.
            if(c.snapshots_ready.load(std::memory_order_acquire))for(unsigned i=0;i<c.snapshots.size();++i)
                out<<"suite_recorded_cycle_"<<number<<"_snapshot_"<<i<<"_written="<<(io_ok=c.snapshots[i]->write(report_path.parent_path()/(report_path.stem().string()+"-cycle-"+std::to_string(number)+"-owned-snapshot-"+std::to_string(i)+".bin")) && io_ok)<<'\n';
            if(c.replay_evidence.state.load(std::memory_order_acquire)==1)
                out<<"suite_recorded_cycle_"<<number<<"_replay_evidence_written="<<(io_ok=c.replay_evidence.write(report_path.parent_path()/(report_path.stem().string()+"-cycle-"+std::to_string(number)+"-replay-evidence.bin")) && io_ok)<<'\n';
            if(!c.ready.load(std::memory_order_acquire))continue;
            const auto prefix="suite_retained_cycle_"+std::to_string(number)+"_";
            out<<prefix<<"fight_ready_frame="<<c.fight_frame<<'\n';
            out<<prefix<<"round="<<c.round<<'\n'<<prefix<<"generation="<<c.generation<<'\n'<<prefix<<"event="<<c.event<<'\n';
            out<<prefix<<"first_frame="<<c.first_frame<<'\n'<<prefix<<"last_frame="<<c.last_frame<<'\n'
               <<prefix<<"windows="<<c.windows<<'\n'<<prefix<<"revived="<<c.revived<<'\n'<<prefix<<"verified=1\n";
            for(unsigned i=0;i<c.diagnostics.size();++i)out<<prefix<<OwnedPredictionCorrection::diagnostic_names[i]<<'='<<c.diagnostics[i]<<'\n';
            if(!c.written){
                const auto stem=report_path.stem().string()+"-cycle-"+std::to_string(number);
                std::ofstream a(report_path.parent_path()/(stem+"-animation-copy.bin"),std::ios::binary);
                a.write(reinterpret_cast<const char*>(c.animation.get()),sizeof(*c.animation));
                std::ofstream r(report_path.parent_path()/(stem+"-inline-rng.bin"),std::ios::binary);
                r.write(reinterpret_cast<const char*>(c.rng.get()),sizeof(*c.rng));a.close();r.close();c.written=bool(a)&&bool(r);
            }
            out<<prefix<<"traces_written="<<c.written<<'\n';
            if(continuous_match){
                // Keep every cycle's numeric diagnostics after recycling its bounded slot.
                std::ofstream row(report_path.parent_path()/(report_path.stem().string()+"-cycle-"+std::to_string(number)+"-summary.txt"));
                row<<"cycle="<<number<<"\nfirst_frame="<<c.first_frame<<"\nlast_frame="<<c.last_frame<<"\nround="<<c.round<<"\ngeneration="<<c.generation<<"\nverified=1\n";
                for(unsigned i=0;i<c.diagnostics.size();++i)row<<OwnedPredictionCorrection::diagnostic_names[i]<<'='<<c.diagnostics[i]<<'\n';
                row.close();io_ok=io_ok && c.written && bool(row);
                if(!io_ok){storage_blocked=true;++evidence_io_failures;continue;}
                storage_blocked=false;
                for(auto& snapshot:c.snapshots)snapshot.reset();
                c.snapshots_ready=false;c.replay_evidence.reset_after_write();c.written=false;c.ready=false;
                ++evidence_flushed;
                // Last worker access. Producer may now reuse the slot and its arena.
                c.available.store(true,std::memory_order_release);
            }
        }
        out<<"suite_continuous_match="<<continuous_match<<"\nsuite_match_completed="<<match_completed.load()<<"\nsuite_match_end_discards="<<match_end_discards.load()<<"\nsuite_evidence_flushed="<<evidence_flushed.load()<<"\nsuite_evidence_wait_frames="<<evidence_wait_frames.load()<<"\nsuite_evidence_io_failures="<<evidence_io_failures.load()<<'\n';
        out<<"suite_storage_blocked="<<storage_blocked.load()<<"\nsuite_storage_recent_cycles=3\nsuite_storage_reference_cycle=1\n";
        out<<"suite_retained_replay_schema=2\nsuite_retained_replay_windows="<<windows.load()
           <<"\nsuite_retained_replay_captured="<<captured.load()<<"\nsuite_retained_replay_no_event="<<no_event.load()
           <<"\nsuite_retained_replay_removed="<<removal.load()<<"\nsuite_retained_replay_revived="<<revived.load()
           <<"\nsuite_retained_replay_restored="<<restored.load()<<"\nsuite_retained_replay_verified="<<verified.load()
           <<"\nsuite_retained_replay_retired="<<retired.load()<<"\nsuite_retained_replay_failed="<<failed.load()
           <<"\nsuite_retained_replay_restart="<<restart.load()<<"\nsuite_retained_replay_failure_reason="<<failure_reason.load()
           <<"\nsuite_retained_replay_round_cancelled="<<round_cancelled.load()<<"\nsuite_retained_replay_stage="<<stage.load()<<'\n';
        if(animation_ready.load(std::memory_order_acquire) && timer_trace){
            unsigned invalid=0;std::array<unsigned,10> sites{};
            for(uint64_t n=timer_trace->total-timer_trace->count;n<timer_trace->total;++n){const auto& e=timer_trace->events[n%timer_trace->events.size()];if(e.site<sites.size())++sites[e.site];if(!(e.valid&4) || (e.valid&0x80000000u))++invalid;}
            out<<"suite_timer_lookup_schema=1\nsuite_timer_lookup_installed="<<timer_trace->installed<<"\nsuite_timer_lookup_events="<<timer_trace->total<<"\nsuite_timer_lookup_dropped="<<timer_trace->dropped<<"\nsuite_timer_lookup_overwritten="<<(timer_trace->total-timer_trace->count)<<"\nsuite_timer_lookup_invalid="<<invalid<<'\n';
            for(unsigned i=0;i<sites.size();++i)out<<"suite_timer_lookup_site_"<<i<<'='<<sites[i]<<'\n';
            if(!timer_written){std::ofstream t(report_path.parent_path()/(report_path.stem().string()+"-timer-lookup.bin"),std::ios::binary);t.write(reinterpret_cast<const char*>(timer_trace.get()),sizeof(*timer_trace));timer_written=bool(t);}
            out<<"suite_timer_lookup_written="<<timer_written<<'\n';
        }
        out<<"suite_animation_copy_capture_failed="<<animation_failed.load()<<'\n';
        if(animation_ready.load(std::memory_order_acquire)){
            if(inline_trace){
                unsigned pre=0,pre_bad=0,post=0,post_bad=0,invalid=0;
                for(uint64_t seq=inline_trace->total-inline_trace->count;seq<inline_trace->total;++seq){const auto& e=inline_trace->events[seq%inline_trace->events.size()];
                    invalid+=!e.valid;if(e.site==8){++pre;pre_bad+=!e.valid || e.before!=e.expected;}if(e.site==9){++post;post_bad+=!e.valid || e.before!=e.expected;}}
                out<<"suite_rng_seed_pre_checks="<<pre<<"\nsuite_rng_seed_pre_mismatches="<<pre_bad<<"\nsuite_rng_seed_post_checks="<<post<<"\nsuite_rng_seed_post_mismatches="<<post_bad<<"\nsuite_inline_rng_invalid="<<invalid<<'\n';
                out<<"suite_inline_rng_schema=1\nsuite_inline_rng_events="<<inline_trace->total<<"\nsuite_inline_rng_dropped="<<inline_trace->dropped<<"\nsuite_inline_rng_installed="<<inline_trace->installed<<'\n';
                if(!inline_written){std::ofstream file(report_path.parent_path()/(report_path.stem().string()+"-inline-rng.bin"),std::ios::binary);file.write(reinterpret_cast<const char*>(inline_trace.get()),sizeof(*inline_trace));inline_written=bool(file);}
                out<<"suite_inline_rng_written="<<inline_written<<'\n';
            }
            out<<"suite_animation_copy_schema=2\nsuite_animation_copy_installed="<<animation_trace->installed
               <<"\nsuite_animation_copy_events="<<animation_trace->total<<"\nsuite_animation_copy_dropped="<<animation_trace->dropped<<'\n';
            unsigned sites[3]{},natural{},replay{},invalid{};
            for(uint64_t seq=animation_trace->total-animation_trace->count;seq<animation_trace->total;++seq){
                const auto& event=animation_trace->events[seq%animation_trace->events.size()];
                if(event.site<3)++sites[event.site];if(!event.valid)++invalid;
                if(event.phase==0)++natural;if(event.phase==2)++replay;
            }
            out<<"suite_animation_copy_entry="<<sites[0]<<"\nsuite_animation_copy_before="<<sites[1]
               <<"\nsuite_animation_copy_after="<<sites[2]<<"\nsuite_animation_copy_natural="<<natural
               <<"\nsuite_animation_copy_replay="<<replay<<"\nsuite_animation_copy_invalid="<<invalid<<'\n';
            if(!animation_written){std::ofstream file(report_path.parent_path()/(report_path.stem().string()+"-animation-copy.bin"),std::ios::binary);
                file.write(reinterpret_cast<const char*>(animation_trace.get()),sizeof(*animation_trace));file.close();animation_written=bool(file);}
            out<<"suite_animation_copy_written="<<animation_written<<'\n';
        }
        for(unsigned i=0;i<diagnostics.size();++i)out<<"suite_retained_replay_"<<OwnedPredictionCorrection::diagnostic_names[i]<<'='<<diagnostics[i].load()<<'\n';
        if(evidence.state.load()==1)out<<"suite_retained_replay_evidence_written="<<evidence.write(report_path.parent_path()/(report_path.stem().string()+"-retained-evidence.bin"))<<'\n';
        }
        timing_.report(out);
    }
};
}
