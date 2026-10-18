#pragma once
#include <Windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include "SteamHandshakeProtocol.hpp"
#include "SteamInputExchange.hpp"
#include "SteamPing.hpp"
#include "RoundEventObserver.hpp"
#include "NetworkGameHandoff.hpp"
#include "SchedulerInputSink.hpp"
#include "CombatRouteBundle.hpp"
#include "CombatRuntimeStatus.hpp"
#include "RollbackJournal.hpp"
#include "CombatStartPacket.hpp"
#include "CombatPeerContext.hpp"
#include "NativeClockHook.hpp"
#include "CombatResourcePreparation.hpp"
#include "StartupBoundaryAgreement.hpp"
#include "StartupSuite.hpp"
#include "RepeatedCorrectionSession.hpp"
#include "NetworkCheckpointStatus.hpp"
#include "StatusOverlay.hpp"
#include "FrameRecorder.hpp"
#include <chrono>

namespace sc6::steam_handshake {
using Send=bool(*)(void*,uint64_t,const void*,uint32_t,int,int);
using Available=bool(*)(void*,uint32_t*,int);
using Read=bool(*)(void*,void*,uint32_t,uint32_t*,uint64_t*,int);
struct Session {unsigned char active,connecting,error,relay;int bytes,packets;uint32_t ip;uint16_t port;};
static_assert(sizeof(Session)==20);
using State=bool(*)(void*,uint64_t,Session*);
inline bool stopped=false,running=false;
inline SteamPing ping;
inline bool peer_live=false;
inline uint64_t clock_us(){return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
inline OverlayStats overlay_stats(){OverlayStats stats;stats.connected=running&&!stopped&&peer_live;if(stats.connected)stats.ping_ms=ping.value(clock_us());return stats;}
inline void poll() {
    static ULONGLONG next=0,start=0,last_report=0;
    static std::filesystem::path report_path;
    static Protocol protocol;
    static Send send=nullptr;static Available available=nullptr;static Read read=nullptr;static State state=nullptr;
    static void* net=nullptr;
    static unsigned sent=0,received=0,rejected=0,send_failed=0;
    static Session session{};
    static bool input_mode=false,checkpoint_mode=false,replay_mode=false,late_mode=false,reuse_mode=false;
    static bool prepare_mode=false;
    static bool continuous_mode=false,continuous_queued=false;
    static bool suite_mode=false,suite_finishing=false;
    static sc6::research::StartupSuite suite;
    static sc6::research::RepeatedCorrectionSession repeated;
    static sc6::research::StartupBoundaryAgreement boundary_agreement;
    static unsigned boundary_rejection_mask{},boundary_rejection_live_identity{},boundary_rejection_startup_identity{},boundary_rejection_hold{};
    static ULONGLONG boundary_sent=0,boundary_agreed_at=0;
    static ULONGLONG correction_sent=0;
    static steam_inputs::Capture capture;
    static ULONGLONG completed=0;
    static bool data_done=false;
    static uint64_t initial_round_generation=0;
    static std::array<bool,steam_inputs::limit> handed_off{};
    static std::array<bool,steam_inputs::limit> scheduler_handed_off{};
    static ULONGLONG last_peer=0,last_announcement=0;
    const auto now=GetTickCount64();
    // Keep the last fresh authenticated value throughout an active poll.
    // publish() replaces it atomically and clears it on lost liveness.
    if(stopped || !running || data_done)sc6::research::combat_peer_context.clear();
    // Continue flushing after a terminal transport report: owner-thread
    // cleanup may publish its result on a later clock callback.
    if(auto* runtime=sc6::research::combat_runtime_status.load(std::memory_order_acquire);
       runtime && runtime->session==protocol.local_nonce){
        if(stopped || !running || data_done)runtime->request_stop();
        static ULONGLONG continuous_flushed=0;
        static std::ofstream continuous_events;
        if(now-continuous_flushed>=250 && !report_path.empty()){
            continuous_flushed=now;
            if(!continuous_events.is_open()){
                continuous_events.open(report_path.parent_path()/(report_path.stem().string()+"-continuous-events.csv"));
                if(continuous_events)sc6::research::RollbackJournal<>::header(continuous_events);
            }
            std::ofstream summary(report_path.parent_path()/(report_path.stem().string()+"-continuous.txt"));
            if(continuous_events && summary){runtime->write_history(continuous_events,summary);continuous_events.flush();}
        }
    }
    if(prepare_mode && stopped && capture.owned_nonce && abort_combat_recording)abort_combat_recording(capture.owned_nonce);
    if(stopped || !running || data_done)sc6::research::owned_transport_attachment.detach(capture.owned_nonce);
    if(stopped||now<next)return;
    next=now+(running?(input_mode?0:250):1000);
    ping.update(clock_us());
    const auto pump_start=steam_inputs::Capture::timestamp();
    auto report=[&](const char* status){
        std::ofstream out(report_path);
        const auto* runtime=sc6::research::combat_runtime_status.load(std::memory_order_acquire);
        const bool live_runtime=continuous_mode && runtime && runtime->session==protocol.local_nonce;
        if(continuous_mode)out<<"continuous_restore_supported="<<sc6::research::NativeBoundarySnapshot::general_restore_implemented
            <<"\ncontinuous_activation_blocker="<<(sc6::research::NativeBoundarySnapshot::general_restore_implemented?"native_input_delivery_and_round_commit_not_connected":"general_combat_restore_unimplemented")
            <<"\ncontinuous_native_wait_bypass=0\ncontinuous_round_rearm=0"
            <<"\ncontinuous_requested=1\ncontinuous_start_state="<<(continuous_start_state?continuous_start_state():0)
            <<"\ncontinuous_report="<<report_path.stem().string()<<"-continuous.txt\n";
        if(continuous_mode){
            const auto* start=sc6::research::combat_start_exchange.load(std::memory_order_acquire);
            out<<"continuous_retired_start_packets="<<(start?start->retired_packets():0)<<'\n';
        }
        const auto* prepared=sc6::research::combat_resource_preparation.load();
        bool correction_restore=prepared && prepared->correction_enabled.load() && prepared->correction->restored.load();
        bool correction_injected=prepared && prepared->correction_enabled.load() && prepared->correction->injected.load();
        if(prepared && prepared->cycles_enabled.load())for(unsigned i=0;i<prepared->cycles->active();++i){
            correction_restore=correction_restore || prepared->cycles->at(i).runner.restored.load();
            correction_injected=correction_injected || prepared->cycles->at(i).runner.injected.load();
        }
        const bool reference_restore=(prepared && prepared->retained_replay_enabled.load() && prepared->retained_replay->restored.load()) || correction_restore || (prepared && prepared->replay_enabled.load() && prepared->replay->restored.load());
        if(suite_mode){
            if(prepared && suite.profile==11)prepared->retention.report(out);
            out<<"suite_prediction_fixture="<<((suite.profile==9 || suite.profile==10)?"remote_neutral_real_inputs":(suite.profile==7 || suite.profile==8)?"one_frame_A":suite.profile>=5 && suite.profile!=11 && suite.profile!=12 && suite.profile!=13 && suite.profile!=14 && suite.profile!=15 && suite.profile!=16?"screen_right":"none")<<"\n";
            out<<"suite_minimum_hold_ms="<<(suite.profile==9?0:100)<<"\nsuite_timing_mode="<<(suite.profile==9?"immediate":"baseline_100ms")<<"\n";
            out<<"suite_schema=1\nsuite_profile="<<suite.profile<<"\nsuite_observe_seconds="<<suite.seconds
               <<"\nsuite_reason="<<suite.reason<<"\nsuite_replay="<<(suite.profile==11?"disabled_retention_only":(suite.profile>=13 && suite.profile<=16)?"owned_recording_reference_replay":"controlled_correction_available")<<"\nsuite_restart_required="
               <<(prepared && (prepared->hold_test.state.load()==5 || prepared->replay_restart()))
               <<"\nsuite_local_pass="<<suite.local[3]<<"\nsuite_local_fail="<<suite.local[4]<<"\nsuite_local_skip="<<suite.local[5]
               <<"\nsuite_peer_received="<<suite.peer_seen<<"\nsuite_peer_ack="<<suite.peer_ack
               <<"\nsuite_peer_pass="<<suite.peer[3]<<"\nsuite_peer_fail="<<suite.peer[4]<<"\nsuite_peer_skip="<<suite.peer[5]
               <<"\nsuite_round_changes="<<suite.round_changes<<"\nsuite_rematch_candidates="<<suite.rematches<<'\n';
        }
        if(prepare_mode && !continuous_mode)out<<"startup_buffer_attached="<<bool(capture.owned_nonce)
            <<"\nstartup_activation_blocker=replay_activation_not_connected\n"
            <<"startup_boundary_rejection_mask="<<boundary_rejection_mask<<"\nstartup_boundary_rejection_live_identity="<<boundary_rejection_live_identity<<"\nstartup_boundary_rejection_startup_identity="<<boundary_rejection_startup_identity<<"\nstartup_boundary_rejection_hold="<<boundary_rejection_hold<<'\n'
            <<"startup_boundary_agreed="<<boundary_agreement.ready()
            <<"\nstartup_boundary_failed="<<boundary_agreement.failed()
            <<"\nstartup_target_clock="<<boundary_agreement.target_clock()
            <<"\nstartup_target_battle="<<boundary_agreement.target_battle()<<'\n';
        if(prepare_mode){if(const auto* resources=sc6::research::combat_resource_preparation.load()){
            out<<"startup_first_match_requested="<<resources->first_match_requested.load()
               <<"\nstartup_first_match_thread="<<resources->first_match_thread.load()
               <<"\nstartup_first_match_verified="<<resources->first_match_verified.load()<<'\n';
            if(resources->correction_enabled.load(std::memory_order_acquire)){
                const auto& c=*resources->correction;
                out<<"suite_correction_diagnostic_schema=15\n";
                for(unsigned i=0;i<c.diagnostic.size();++i)
                    out<<"suite_correction_detail_"<<sc6::research::OwnedPredictionCorrection::diagnostic_names[i]<<'='<<c.diagnostic[i].load()<<'\n';
                out<<"suite_correction_blend_evidence_valid="<<c.blend_evidence_valid.load(std::memory_order_acquire)<<'\n';
                if(c.blend_evidence_valid.load(std::memory_order_acquire)){
                    constexpr char digits[]="0123456789abcdef";
                    out<<"suite_correction_blend_saved_hex=";
                    for(const auto& cell:c.blend_saved){const auto value=cell.load();out<<digits[value>>4]<<digits[value&15];}out<<'\n';
                    out<<"suite_correction_blend_next_hex=";
                    for(const auto& cell:c.blend_next){const auto value=cell.load();out<<digits[value>>4]<<digits[value&15];}out<<'\n';
                }
                out<<"suite_correction_captured="<<c.captured.load()<<"\nsuite_correction_stage="<<c.stage.load()
                   <<"\nsuite_correction_failed="<<c.failed.load()<<"\nsuite_correction_restore_invoked="<<c.restored.load()
                   <<"\nsuite_correction_injected="<<c.injected.load()<<"\nsuite_correction_state_changed="<<c.changed.load()
                   <<"\nsuite_correction_predicted_updates="<<c.predicted.load()<<"\nsuite_correction_verified_updates="<<c.corrected.load()
                   <<"\nsuite_correction_verified="<<c.verified.load()<<"\nsuite_correction_retired="<<c.retired.load()
                   <<"\nsuite_correction_restart_required="<<c.restart.load()<<"\nsuite_correction_capture_detail="<<c.capture_detail.load()
                   <<"\nsuite_confirmation_local="<<c.exchange.local_count.load()<<"\nsuite_confirmation_remote="<<c.exchange.remote_count.load()
                   <<"\nsuite_confirmation_input_rejection="<<c.exchange.input_rejection.load()<<"\nsuite_confirmation_ready="<<c.exchange.ready.load()<<"\nsuite_confirmation_failed="<<c.exchange.failed.load()
                   <<"\nsuite_confirmation_hold_state="<<resources->confirmation_hold.state.load()
                   <<"\nsuite_confirmation_hold_services="<<resources->confirmation_hold.services.load()<<'\n';
            }
            if(resources->cycles_enabled.load()){resources->cycles->write_report(out);resources->cycles->write_evidence(out,report_path);out<<"suite_fresh_retry_boundaries="<<repeated.fresh_retries<<"\nsuite_retry_proposal_cycle="<<repeated.proposal_cycle<<"\nsuite_retry_proposal_phase="<<repeated.proposal_phase<<'\n';}
            resources->wind_report(out);
            if(resources->retained_replay_enabled.load())resources->retained_replay->report(out,report_path);
            if(resources->replay_enabled.load(std::memory_order_acquire)){
                const auto& replay=*resources->replay;
                out<<"suite_reference_captured="<<replay.captured.load()<<"\nsuite_reference_stage="<<replay.stage.load()
                   <<"\nsuite_reference_failed="<<replay.failed.load()<<"\nsuite_reference_restore_invoked="<<replay.restored.load()
                   <<"\nsuite_reference_verified="<<replay.verified.load()<<"\nsuite_reference_retired="<<replay.retired.load()
                   <<"\nsuite_reference_restart_required="<<replay.restart.load()<<"\nsuite_reference_capture_detail="<<replay.capture_detail.load()
                   <<"\nsuite_reference_prepare_detail="<<replay.prepare_detail.load()<<"\nsuite_reference_hop="<<replay.hop.load()
                   <<"\nsuite_reference_plan="<<replay.plan.load()<<'\n';
            }
            out<<"startup_resource_stop_requested="<<resources->stop_requested()<<"\n";
            out<<"startup_resource_ready="<<resources->ready.load()<<"\nstartup_resource_retired="<<resources->retired.load()
               <<"\nsuite_match_end_schema=2\nsuite_match_end_state_query="<<resources->match_end_state_query.load()
               <<"\nsuite_match_end_requested="<<resources->match_end_requested.load()<<"\nsuite_match_end_clean="<<resources->match_end_clean.load()
               <<"\nsuite_match_end_vtable="<<resources->match_end_vtable.load()<<"\nsuite_match_end_handler="<<resources->match_end_handler.load()
               <<"\nsuite_match_end_thread="<<resources->match_end_thread.load()<<"\nsuite_match_end_frame="<<resources->match_end_frame.load()
               <<"\nstartup_resource_failed="<<resources->failed.load()<<"\nstartup_resource_cancel_reason="<<resources->cancellation.load()
               <<"\nstartup_resource_frame="<<resources->boundary_frame.load()<<"\nstartup_resource_thread="<<resources->owner_thread.load()
               <<"\nstartup_resource_cancel_thread="<<resources->cancel_thread.load()<<"\nstartup_resource_cancel_expected="<<resources->cancel_expected.load()
               <<"\nstartup_resource_cancel_had_refs="<<resources->cancel_had_refs.load()<<"\nstartup_resource_deferred="<<resources->deferred.load()
               <<"\nstartup_resource_deferred_thread="<<resources->deferred_thread.load()
               <<"\nstartup_clock_resource_ready="<<resources->clock_ready.load()
               <<"\nstartup_clock_identity="<<resources->clock_identity.load()
               <<"\nstartup_clock_frame="<<resources->clock_frame.load()
               <<"\nstartup_clock_slot="<<resources->clock_slot.load()
               <<"\nstartup_hold_state="<<resources->hold_test.state.load()
               <<"\nstartup_hold_services="<<resources->hold_test.services.load()
               <<"\nstartup_snapshots_captured="<<resources->snapshots.captured.load()
               <<"\nstartup_snapshots_failed="<<resources->snapshots.failed.load()
               <<"\nstartup_snapshots_retired="<<resources->snapshots.retired.load()
               <<"\nstartup_clock_progress_schema=1"
               <<"\nstartup_clock_samples="<<resources->clock_samples.load()
               <<"\nstartup_clock_changes="<<resources->clock_changes.load()
               <<"\nstartup_clock_resets="<<resources->clock_resets.load()
               <<"\nstartup_clock_object_changes="<<resources->clock_object_changes.load()
               <<"\nstartup_clock_first="<<resources->clock_first.load()
               <<"\nstartup_clock_last="<<resources->clock_last.load()
               <<"\nstartup_local_cursor_first="<<resources->cursor_first.load()
               <<"\nstartup_local_cursor_last="<<resources->cursor_last.load()
               <<"\nstartup_all_cursor_first="<<resources->all_cursor_first.load()
               <<"\nstartup_all_cursor_last="<<resources->all_cursor_last.load()
               <<"\nstartup_battle_samples="<<resources->battle_samples.load()
               <<"\nstartup_battle_first="<<resources->battle_first.load()
               <<"\nstartup_battle_last="<<resources->battle_last.load()<<'\n';
            resources->prediction_frontier.report(out);
            const auto count=resources->pairs.count.load(std::memory_order_acquire);
            out<<"startup_pairs_schema=1\nstartup_pairs_count="<<count
               <<"\nstartup_pairs_duplicates="<<resources->pairs.duplicates.load()
               <<"\nstartup_pairs_overwritten="<<resources->pairs.overwritten.load()
               <<"\nstartup_pairs_unpaired="<<resources->pairs.unpaired.load()
               <<"\nstartup_pairs_discontinuities="<<resources->pairs.discontinuities.load()<<'\n';
            for(unsigned i=0;i<count;++i){const auto& row=resources->pairs.rows[i];
                out<<"startup_pair_"<<i<<'='<<row.identity<<','<<row.clock<<','<<row.battle<<'\n';}
        }}
        for(unsigned phase=0;phase<2;++phase){
            out<<"native_routing_"<<phase<<"_valid="<<network_checkpoint_status.routing_valid[phase].load()<<"\n";
            out<<"native_routing_"<<phase<<"_dispatch_stage="<<network_checkpoint_status.dispatch_stage[phase].load()<<"\n";
            constexpr const char* names[]={"copied_p1_0","copied_p1_1","copied_p2_0","copied_p2_1","p1_selector","p1_mode","p1_route","p1_source","p1_input","p2_selector","p2_mode","p2_route","p2_source","p2_input","state_vtable_rva","state_handler_rva","state_counter","state_previous"};
            for(unsigned i=0;i<18;++i)out<<"native_routing_"<<phase<<"_"<<names[i]<<"="<<network_checkpoint_status.routing_values[phase][i].load()<<"\n";
        }
        out<<"native_comparison_diagnostic_schema=4\nnative_comparison_rng_expected="<<network_checkpoint_status.comparison_rng_expected.load()
           <<"\nnative_comparison_rng_actual="<<network_checkpoint_status.comparison_rng_actual.load()<<"\n";
        {constexpr const char* fields[]={"status","validity","group","slot","offset","field","expected","actual","expected_word","actual_word"};
            for(unsigned i=0;i<10;++i)out<<"native_comparison_ring_"<<fields[i]<<"="<<network_checkpoint_status.comparison_ring[i].load()<<"\n";}
        {constexpr const char* fields[]={"reason","record","offset","length","writer_rva","source","expected","actual","word_offset","expected_word","actual_word","changed_bytes"};
            for(unsigned i=0;i<12;++i)out<<"native_comparison_serialized_"<<fields[i]<<"="<<network_checkpoint_status.comparison_native[i].load()<<"\n";}
        {constexpr const char* fields[]={"stage","side","block","offset","expected","actual","index","loaded_index","mask","loaded_mask"};
            for(unsigned i=0;i<10;++i)out<<"native_blend_block_"<<fields[i]<<"="<<network_checkpoint_status.blend_block_diagnostic[i].load()<<"\n";}
        out<<"native_comparison_pass_mask="<<network_checkpoint_status.comparison_pass_mask.load()<<"\n";
        out<<"native_comparison_stage="<<network_checkpoint_status.comparison_stage.load()
           <<"\nnative_comparison_flags="<<network_checkpoint_status.comparison_flags.load()
           <<"\nnative_comparison_bytes="<<network_checkpoint_status.comparison_bytes.load()<<"\n";
        for(unsigned side=0;side<2;++side){constexpr const char* fields[]={"differences","offset","expected_byte","live_byte"};
            for(unsigned j=0;j<4;++j)out<<"native_comparison_p"<<side+1<<"_"<<fields[j]<<"="<<network_checkpoint_status.comparison_scalars[side*4+j].load()<<"\n";}
        for(unsigned i=0;i<3;++i){constexpr const char* fields[]={"target","current","last_confirmed","queue_pending"};
            for(unsigned j=0;j<4;++j)out<<"native_confirmation_attempt_"<<i<<"_"<<fields[j]<<"="<<network_checkpoint_status.confirmation_attempts[i][j].load()<<"\n";}
        out<<"native_battle_gate="<<network_checkpoint_status.battle_gate.load()<<"\nnative_battle_vtable_rva="<<network_checkpoint_status.battle_vtable.load()<<"\nnative_battle_handler_rva="<<network_checkpoint_status.battle_handler.load()<<"\n";
        out<<"native_completion_line="<<network_checkpoint_status.completion_line.load()
           <<"\nnative_completion_section="<<network_checkpoint_status.completion_section.load()
           <<"\nnative_completion_record="<<network_checkpoint_status.completion_record.load()
           <<"\nnative_completion_kind="<<network_checkpoint_status.completion_kind.load()
           <<"\nnative_completion_offset="<<network_checkpoint_status.completion_offset.load()
           <<"\nnative_completion_saved="<<network_checkpoint_status.completion_saved.load()
           <<"\nnative_completion_live="<<network_checkpoint_status.completion_live.load()<<"\n";
        out<<"schema=1\nscope="<<(continuous_mode?"steam_continuous_experimental":prepare_mode?"steam_combat_startup_preparation":input_mode?(reuse_mode?"steam_native_window_reuse":late_mode?"steam_live_late_input_scheduler":replay_mode?"steam_native_three_frame_remote_correction":checkpoint_mode?"steam_native_checkpoint_save":"steam_recorded_inputs"):"steam_handshake_only")<<"\nrollback_active="<<(live_runtime && runtime->active.load())<<"\ninjection="<<(network_checkpoint_status.prediction_invoked.load() || correction_injected || (live_runtime && runtime->updates.load()))<<"\nrestore="<<(network_checkpoint_status.restore_invoked.load() || reference_restore || (live_runtime && runtime->restored.load()))<<"\ngame_alignment_verified=0\nstatus="<<status
            <<"\nchannel="<<channel<<"\npeer="<<protocol.expected_peer<<"\nsent="<<sent<<"\nreceived="<<received
            <<"\nrejected="<<rejected<<"\nsend_failed="<<send_failed<<"\nroundtrip_proof="<<protocol.proof
            <<"\nmutual_confirmation="<<protocol.mutual<<"\nsteam_using_relay="<<unsigned(session.relay)<<'\n';
        out<<"ping_method=minimum_of_5_minus_measured_reply_handling\nping_network_only=0\nping_burst_samples="<<ping.latest_samples
           <<"\nping_selected_raw_us="<<ping.latest_raw_us<<"\nping_selected_reply_handling_us="<<ping.latest_handling_us<<'\n';
        auto measured=ping.value(clock_us());out<<"ping_refresh_seconds=3\nping_rtt_ms="<<(measured?std::to_string(*measured):"unavailable")<<'\n';
        if(input_mode)out<<"target_records="<<steam_inputs::limit<<"\nslot_mapping_assumed=1\nlocal_records="<<capture.exchange.published<<"\nremote_records="<<capture.exchange.received
            <<"\npeer_checksum_match="<<capture.exchange.peer_match<<"\nlocal_checksum="<<observation::checksum(capture.exchange.local)
            <<"\nremote_checksum="<<observation::checksum(capture.exchange.remote)<<"\nqpc_frequency="<<(capture.shared?capture.shared->frequency:0)<<'\n';
        if(reuse_mode){out<<"native_topology_mask="<<network_checkpoint_status.topology_mask.load()<<"\nnative_topology_index="<<network_checkpoint_status.topology_index.load()<<"\nnative_topology_before_count="<<network_checkpoint_status.topology_before_count.load()<<"\nnative_topology_after_count="<<network_checkpoint_status.topology_after_count.load()<<"\n";const char* fields[]={"address","side","kind","list","parent","size"};for(unsigned i=0;i<6;++i)out<<"native_topology_before_"<<fields[i]<<"="<<network_checkpoint_status.topology_before[i].load()<<"\nnative_topology_after_"<<fields[i]<<"="<<network_checkpoint_status.topology_after[i].load()<<"\n";for(unsigned cycle=0;cycle<2;++cycle){for(unsigned frame=0;frame<4;++frame)out<<"native_window_rng_"<<cycle<<"_"<<frame<<"="<<network_checkpoint_status.window_rng[cycle][frame].load()<<"\nnative_window_rng_after_save_"<<cycle<<"_"<<frame<<"="<<network_checkpoint_status.window_rng_after_save[cycle][frame].load()<<"\n";for(unsigned hop=0;hop<3;++hop)out<<"native_window_rng_steps_"<<cycle<<"_"<<hop<<"="<<network_checkpoint_status.window_rng_steps[cycle][hop].load()<<"\n";}}
        if(late_mode){const auto& l=network_game_handoff.late;
            out<<"late_scheduler_schema=1\nlate_prediction_applied=0\nlate_correction_executed=0\nlate_decision_delay_frames=0\nlate_snapshot_available=0\nlate_observed_horizon_frames=15\nlate_correction_limit_frames=3\n";
            out<<"late_decisions="<<l.decisions.load()<<"\n";
            out<<"late_seeded="<<l.seeded.load()<<"\n";
            out<<"late_unseeded="<<l.unseeded.load()<<"\n";
            out<<"late_confirmed="<<l.confirmed.load()<<"\n";
            out<<"late_errors="<<l.errors.load()<<"\n";
            out<<"late_over_limit="<<l.over_limit.load()<<"\n";
            out<<"late_max_depth="<<l.max_depth.load()<<"\n";
            out<<"late_requests="<<l.requests.load()<<"\n";
            out<<"late_resets="<<l.resets.load()<<"\n";
            out<<"late_invalid="<<l.invalid.load()<<"\n";
            out<<"late_expired="<<l.expired.load()<<"\n";
            out<<"late_event_count="<<l.event_count.load()<<"\n";
            out<<"late_event_dropped="<<l.event_dropped.load()<<"\n";
            out<<"late_last_target="<<l.last_target.load()<<"\n";
            out<<"late_last_depth="<<l.last_depth.load()<<"\n";
            out<<"late_remote_slot="<<l.remote_slot.load()<<"\n";
            std::ofstream events(report_path.parent_path()/(report_path.stem().string()+"-late.csv"));l.csv(events);
        }
        if(input_mode){const auto d=round_event::diagnostics();
            out<<"round_identity_source="<<(round_event::direct_identity?"guarded_upstream_raw":"notification")<<"\nround_observer_schema=3\nround_hook_installed="<<d.installed
               <<"\nround_hook_events="<<d.count<<"\nround_hook_dropped="<<d.dropped
               <<"\nround_hook_accepted="<<d.accepted<<"\nround_hook_other_callers="<<d.foreign
               <<"\nupstream_hook_installed="<<d.upstream_installed<<"\nupstream_events="<<d.upstream_count<<"\nupstream_dropped="<<d.upstream_dropped
               <<"\nround_hook_failed="<<d.failed<<'\n';
            round_event::export_events(report_path.parent_path()/(report_path.stem().string()+"-round-events.csv"));
        }
        if(input_mode)out<<"handoff_validation_only=1\nhandoff_native_checkpoints="<<network_checkpoint_status.captured.load()<<"\nhandoff_submitted="<<network_game_handoff.submitted.load()
            <<"\nhandoff_queue_full="<<network_game_handoff.full.load()<<"\nhandoff_validated="<<network_game_handoff.validated.load()
            <<"\nhandoff_rejected="<<network_game_handoff.rejected.load()<<'\n';
        if(input_mode){
            const char* names[]={"failed","session","round","age","history","input"};
            for(unsigned i=0;i<6;++i)out<<"handoff_rejected_"<<names[i]<<'='<<network_game_handoff.rejection_reasons[i].load()<<'\n';
            const auto consumed=network_game_handoff.read.load();const auto published=network_game_handoff.write.load();
            out<<"handoff_queue_pending="<<(published-consumed)<<'\n';
        }
        if(input_mode)out<<"native_checkpoint_save_enabled="<<checkpoint_mode<<"\nnative_checkpoint_phase="<<network_checkpoint_status.phase.load()
            <<"\nnative_checkpoints_captured="<<network_checkpoint_status.captured.load()<<"\nnative_checkpoints_linked="<<network_checkpoint_status.matched.load()
            <<"\nnative_checkpoint_failure="<<network_checkpoint_status.failure.load()<<"\nnative_checkpoint_first_frame="<<network_checkpoint_status.first_frame.load()
            <<"\nnative_readiness_attempted="<<network_checkpoint_status.readiness_attempted.load()
            <<"\nnative_readiness_prepared="<<network_checkpoint_status.readiness_prepared.load()
            <<"\nnative_readiness_rejection="<<network_checkpoint_status.readiness_rejection.load()
            <<"\nnative_readiness_restore_invoked="<<network_checkpoint_status.restore_invoked.load()
            <<"\nnative_window_topology_retries="<<network_checkpoint_status.window_topology_retries.load()<<"\nnative_window_topology_retry_first="<<network_checkpoint_status.window_topology_retry_first.load()<<"\nnative_window_topology_retry_last="<<network_checkpoint_status.window_topology_retry_last.load()<<"\nnative_window_missing_mask="<<network_checkpoint_status.window_missing_mask.load()<<"\nnative_window_retry_frame="<<network_checkpoint_status.window_retry_frame.load()<<"\nnative_window_plan_result="<<network_checkpoint_status.window_plan_result.load()<<"\nnative_window_prepare_stage="<<network_checkpoint_status.window_prepare_stage.load()<<"\nnative_window_hop="<<network_checkpoint_status.window_hop.load()<<"\nnative_window_source_frame="<<network_checkpoint_status.window_source_frame.load()<<"\nnative_window_next_frame="<<network_checkpoint_status.window_next_frame.load()<<"\nnative_window_workspace_flags="<<network_checkpoint_status.window_workspace_flags.load()<<"\nnative_owned_cancel_reason="<<network_checkpoint_status.owned_cancel_reason.load()<<"\nnative_owned_cancel_thread="<<network_checkpoint_status.owned_cancel_thread.load()<<"\nnative_owned_cancel_expected="<<network_checkpoint_status.owned_cancel_expected.load()<<"\nnative_owned_cancel_had_refs="<<network_checkpoint_status.owned_cancel_had_refs.load()<<"\nnative_owned_replay_started="<<network_checkpoint_status.owned_replay_started.load()<<"\nnative_owned_replay_captures="<<network_checkpoint_status.owned_replay_captures.load()<<"\nnative_owned_replay_cleanup="<<network_checkpoint_status.owned_replay_cleanup.load()<<"\nnative_window_reuse_completed="<<network_checkpoint_status.window_reuse_completed.load()<<"\nnative_replay_enabled="<<(replay_mode || reuse_mode)
            <<"\nnative_replay_attempted="<<network_checkpoint_status.replay_attempted.load()
            <<"\nnative_restore_diagnostic_schema=5"
            <<"\nnative_wind_verified="<<network_checkpoint_status.wind_verified.load()
            <<"\nnative_wind_capture_valid="<<network_checkpoint_status.wind_capture_valid.load()
            <<"\nnative_wind_offset="<<network_checkpoint_status.wind_offset.load()
            <<"\nnative_wind_saved_word="<<network_checkpoint_status.wind_saved_word.load()
            <<"\nnative_wind_next_word="<<network_checkpoint_status.wind_next_word.load()
            <<"\nnative_wind_control_saved="<<network_checkpoint_status.wind_control_saved.load()
            <<"\nnative_wind_control_next="<<network_checkpoint_status.wind_control_next.load()
            <<"\nnative_wind_failure_line="<<network_checkpoint_status.wind_failure_line.load()
            <<"\nnative_wind_record="<<network_checkpoint_status.wind_record.load()
            <<"\nnative_wind_vtable_rva="<<network_checkpoint_status.wind_vtable_rva.load()
            <<"\nnative_wind_size="<<network_checkpoint_status.wind_size.load()
            <<"\nnative_wind_kind="<<network_checkpoint_status.wind_kind.load()
            <<"\nnative_camera_failure_line="<<network_checkpoint_status.camera_line.load()
            <<"\nnative_camera_record="<<network_checkpoint_status.camera_record.load()
            <<"\nnative_camera_section="<<network_checkpoint_status.camera_section.load()
            <<"\nnative_camera_offset="<<network_checkpoint_status.camera_offset.load()
            <<"\nnative_camera_saved="<<network_checkpoint_status.camera_a.load()
            <<"\nnative_camera_next="<<network_checkpoint_status.camera_b.load()
            <<"\nnative_camera_live="<<network_checkpoint_status.camera_live.load()
            <<"\nnative_camera_loaded="<<network_checkpoint_status.camera_c.load()
            <<"\nnative_restore_stage="<<network_checkpoint_status.restore_stage.load()
            <<"\nnative_restore_failure_line="<<network_checkpoint_status.restore_failure_line.load()
            <<"\nnative_restore_exception="<<network_checkpoint_status.restore_exception.load()
            <<"\nnative_restore_flags="<<network_checkpoint_status.restore_flags.load()
            <<"\nnative_restore_verified="<<network_checkpoint_status.restore_verified.load()
            <<"\nnative_replay_invoked="<<network_checkpoint_status.replay_invoked.load()
            <<"\nnative_source_mismatch_found="<<network_checkpoint_status.source_mismatch_found.load()
            <<"\nnative_source_record="<<network_checkpoint_status.source_record.load()
            <<"\nnative_source_size="<<network_checkpoint_status.source_size.load()
            <<"\nnative_source_saved="<<network_checkpoint_status.source_saved.load()
            <<"\nnative_source_next="<<network_checkpoint_status.source_next.load()
            <<"\nnative_source_writer_rva="<<network_checkpoint_status.source_writer_rva.load()
            <<"\nnative_source_stack_mask="<<network_checkpoint_status.source_stack_mask.load()
            <<"\nnative_capture_phase="<<network_checkpoint_status.capture_phase.load()
            <<"\nnative_capture_stage="<<network_checkpoint_status.capture_stage.load()
            <<"\nnative_capture_exception="<<network_checkpoint_status.capture_exception.load()
            <<"\nnative_capture_mode_values_valid="<<network_checkpoint_status.capture_mode_values_valid.load()
            <<"\nnative_capture_p1_input="<<network_checkpoint_status.capture_mode_values[0].load()
            <<"\nnative_capture_p2_input="<<network_checkpoint_status.capture_mode_values[1].load()
            <<"\nnative_capture_p1_move="<<network_checkpoint_status.capture_mode_values[2].load()
            <<"\nnative_capture_p2_move="<<network_checkpoint_status.capture_mode_values[3].load()
            <<"\nnative_capture_scale_bits="<<network_checkpoint_status.capture_mode_values[4].load()
            <<"\nnative_capture_gate="<<network_checkpoint_status.capture_mode_values[5].load()
            <<"\nnative_depth_target="<<network_checkpoint_status.depth_target.load()
            <<"\nnative_depth_phase="<<network_checkpoint_status.depth_phase.load()
            <<"\nnative_depth_hop="<<network_checkpoint_status.depth_hop.load()
            <<"\nnative_depth_predicted="<<network_checkpoint_status.depth_predicted.load()
            <<"\nnative_depth_clean_restored="<<network_checkpoint_status.depth_clean_restored.load()
            <<"\nnative_depth_rewound="<<network_checkpoint_status.depth_rewound.load()
            <<"\nnative_depth_delivered="<<network_checkpoint_status.depth_delivered.load()
            <<"\nnative_depth_compared="<<network_checkpoint_status.depth_compared.load()
            <<"\nnative_depth_frame_0="<<network_checkpoint_status.depth_frames[0].load()
            <<"\nnative_depth_frame_1="<<network_checkpoint_status.depth_frames[1].load()
            <<"\nnative_depth_frame_2="<<network_checkpoint_status.depth_frames[2].load()
            <<"\nnative_depth_frame_3="<<network_checkpoint_status.depth_frames[3].load()
            <<"\nnative_depth_verified_1="<<network_checkpoint_status.depth_verified[0].load()
            <<"\nnative_depth_verified_2="<<network_checkpoint_status.depth_verified[1].load()
            <<"\nnative_depth_verified_3="<<network_checkpoint_status.depth_verified[2].load()
            <<"\nnative_repeated_target="<<network_checkpoint_status.repeated_target.load()
            <<"\nnative_repeated_completed="<<network_checkpoint_status.repeated_completed.load()
            <<"\nnative_repeated_cycle="<<network_checkpoint_status.repeated_cycle.load()
            <<"\nnative_repeated_failed="<<network_checkpoint_status.repeated_failed.load()
            <<"\nnative_cycle_1_first="<<network_checkpoint_status.repeated_results[0][0].load()
            <<"\nnative_cycle_1_second="<<network_checkpoint_status.repeated_results[0][1].load()
            <<"\nnative_cycle_1_slot="<<network_checkpoint_status.repeated_results[0][2].load()
            <<"\nnative_cycle_1_verified="<<network_checkpoint_status.repeated_results[0][3].load()
            <<"\nnative_cycle_2_first="<<network_checkpoint_status.repeated_results[1][0].load()
            <<"\nnative_cycle_2_second="<<network_checkpoint_status.repeated_results[1][1].load()
            <<"\nnative_cycle_2_slot="<<network_checkpoint_status.repeated_results[1][2].load()
            <<"\nnative_cycle_2_verified="<<network_checkpoint_status.repeated_results[1][3].load()
            <<"\nnative_prediction_fixture_slot="<<network_checkpoint_status.prediction_fixture_slot.load()<<"\nnative_prediction_fixture_buttons=1024"
            <<"\nnative_prediction_stage="<<network_checkpoint_status.prediction_stage.load()
            <<"\nnative_prediction_invoked="<<network_checkpoint_status.prediction_invoked.load()
            <<"\nnative_prediction_captured="<<network_checkpoint_status.prediction_captured.load()
            <<"\nnative_prediction_state_changed="<<network_checkpoint_status.prediction_state_changed.load()
            <<"\nnative_prediction_rewound="<<network_checkpoint_status.prediction_rewound.load()
            <<"\nnative_confirmation_misses="<<network_checkpoint_status.confirmation_misses.load()
            <<"\nnative_confirmation_retries="<<network_checkpoint_status.confirmation_retries.load()
            <<"\nnative_network_input_delivered="<<network_checkpoint_status.network_input_delivered.load()
            <<"\nnative_replay_verified="<<network_checkpoint_status.replay_verified.load()
            <<"\nnative_restart_required="<<(network_checkpoint_status.failure.load() && network_checkpoint_status.restore_invoked.load())
            <<"\nnative_checkpoint_second_frame="<<network_checkpoint_status.second_frame.load()<<'\n';
        if(input_mode)out<<"pipeline_schema=1\ninput_poll_gate_ms=0\npipeline_pumps="<<capture.pump_count<<"\npipeline_dropped="<<capture.pump_dropped<<'\n';
        if(input_mode){const auto& c=capture.composition;
            out<<"composition_schema=1\ncomposition_local_slot="<<c.local_slot<<"\ncomposition_peer_slot="<<c.peer_slot
               <<"\ncomposition_staged="<<c.matched<<"\ncomposition_local_transitions="<<c.transitions
               <<"\ncomposition_auxiliary_differences="<<c.auxiliary_differences<<"\ncomposition_failure="<<c.failure
               <<"\nround_boundary_matches="<<c.round_matches<<"\nround_boundary_missing="<<c.round_missing<<"\nround_boundary_mismatches="<<c.round_mismatches
               <<"\npeer_checksum_includes_round_metadata=1"
               <<"\nreplay_eligibility_schema=1\nreplay_eligibility_metadata_only=1"
               <<"\nreplay_eligible_windows="<<c.replay_eligible<<"\nreplay_cross_round_rejected="<<c.replay_round_rejected
               <<"\nreplay_other_rejected="<<c.replay_other_rejected
               <<"\ncomposition_round_identity_verified=0\ncomposition_game_handoff=1\n";}
        if(input_mode){const auto& s=capture.shadow;out<<"shadow_enabled="<<!continuous_mode<<"\nshadow_delay_frames=4\nshadow_limit_frames=3\nshadow_remote_slot="<<s.remote_slot
            <<"\nshadow_scheduled="<<s.scheduled<<"\nshadow_evaluated="<<s.evaluated<<"\nshadow_predicted="<<s.predicted
            <<"\nshadow_errors="<<s.wrong<<"\nshadow_over_limit="<<s.over_limit<<"\nshadow_max_depth="<<s.max_depth
            <<"\nshadow_unseeded="<<s.unseeded<<"\nshadow_censored="<<s.censored<<"\nshadow_truth_mismatches="<<s.truth_mismatches
            <<"\nshadow_invalid="<<s.invalid<<"\nshadow_no_overlap="<<s.no_overlap<<"\nshadow_tail_excluded=4\nshadow_within_observed_bounds="<<s.within_observed_bounds()<<'\n';}
    };
    if(running && continuous_mode){
        const auto* runtime=sc6::research::combat_runtime_status.load(std::memory_order_acquire);
        if(runtime && runtime->session==protocol.local_nonce && runtime->restart_required.load()){
            capture.export_csv(report_path.parent_path()/report_path.stem());
            running=false;stopped=true;report("continuous_stopped_restart_required");return;
        }
    }
    if(!running) {
        wchar_t exe[32768]{};if(!GetModuleFileNameW(nullptr,exe,32768))return;
        auto dir=std::filesystem::path(exe).parent_path()/L"Mods/SC6Rollback/captures";
        input_mode=std::filesystem::exists(dir/L"steam-inputs.arm");
        auto arm=dir/(input_mode?L"steam-inputs.arm":L"steam-handshake.arm");
        if(!std::filesystem::exists(arm))return;
        report_path=dir/((input_mode?"steam-inputs-":"steam-handshake-")+std::to_string(GetCurrentProcessId())+".txt");
        stopped=true;
        std::ifstream config(arm);std::string mode,extra;uint64_t pid=0,peer=0;
        unsigned local_slot=0;
        bool valid=bool(config>>mode>>pid>>peer);
        if(input_mode)valid=valid && bool(config>>local_slot) && (local_slot==1||local_slot==2);
        suite_mode=input_mode && mode=="PRIVATE_STEAM_SUITE";
        continuous_mode=input_mode && mode=="PRIVATE_STEAM_CONTINUOUS";
        prepare_mode=input_mode && (mode=="PRIVATE_STEAM_PREPARE" || suite_mode || continuous_mode);
        if(suite_mode){unsigned profile{},seconds{};valid=valid && bool(config>>profile>>seconds) && suite.configure(profile,seconds);}
        reuse_mode=input_mode && mode=="PRIVATE_STEAM_REUSE";late_mode=input_mode && mode=="PRIVATE_STEAM_LATE";
        replay_mode=input_mode && mode=="PRIVATE_STEAM_REPLAY";
        checkpoint_mode=input_mode && (mode=="PRIVATE_STEAM_CHECKPOINTS" || replay_mode || reuse_mode);
        bool trailing=bool(config>>extra);config.close();std::filesystem::remove(arm);
        if(!valid||trailing||mode!=(continuous_mode?"PRIVATE_STEAM_CONTINUOUS":suite_mode?"PRIVATE_STEAM_SUITE":prepare_mode?"PRIVATE_STEAM_PREPARE":input_mode?(reuse_mode?"PRIVATE_STEAM_REUSE":late_mode?"PRIVATE_STEAM_LATE":replay_mode?"PRIVATE_STEAM_REPLAY":checkpoint_mode?"PRIVATE_STEAM_CHECKPOINTS":"PRIVATE_STEAM_INPUTS"):"PRIVATE_STEAM_HANDSHAKE")||pid!=GetCurrentProcessId()||peer<=76561197960265728ULL||peer>76561202255233023ULL){report("invalid_arm");return;}
        if(input_mode){initial_round_generation=round_event::snapshot().generation;protocol.expected_signature=reuse_mode?0x002853433648534bULL:late_mode?0x001F53433648534bULL:replay_mode?0x001E53433648534bULL:checkpoint_mode?0x000C53433648534bULL:0x000B53433648534bULL;capture.shadow.remote_slot=3-local_slot;capture.composition.configure(local_slot,continuous_mode?LiveInputComposition::Purpose::authoritative_peer:LiveInputComposition::Purpose::comparison);}
        protocol.expected_peer=peer;
        if(prepare_mode)protocol.expected_signature=0x003053433648534bULL;
        if(continuous_mode)protocol.expected_signature=0x004753433648534bULL;
        if(suite_mode)protocol.expected_signature=0x004253433648534bULL^(uint64_t(suite.profile)<<40)^(uint64_t(suite.seconds)<<48);
        if(suite_mode && (suite.profile>=12 && suite.profile<=16))protocol.expected_signature^=0x0000000800000000ULL; // reusable retirement between retained replay cycles
        if(suite_mode && suite.first_match_start())protocol.expected_signature^=0x0000180000000000ULL; // first-match ownership plus emitter restoration; reject older replay peers
        if(suite_mode && suite.profile==16)protocol.expected_signature^=0x0000B40000000000ULL; // rolling effect retention and confirmation servicing; native wait remains enabled
        if(suite_mode && (suite.profile==15 || suite.profile==16))protocol.expected_signature^=0x0000040000000000ULL; // repeated combat windows; reject older one-cycle-per-round peers
        if(suite_mode && suite.profile==13)protocol.expected_signature^=0x0000001000000000ULL; // owned same-session recorder
        if(suite_mode && (suite.profile==14 || (suite.profile==15 || suite.profile==16)))protocol.expected_signature^=0x0000002000000000ULL; // spaced owned recordings, per-cycle immutable evidence
        if(suite_mode && suite.profile==11)protocol.expected_signature^=0x0000000200000000ULL; // retention coverage revision 2
        if(suite_mode && (suite.profile==9 || suite.profile==10))protocol.expected_signature^=0x0000001600000000ULL; // reference-first methodology; reject older builds
        HMODULE api=GetModuleHandleW(L"steam_api64.dll");if(!api){report("steam_missing");return;}
        using Handle=int(*)();using Factory=void*(*)(const char*);using Network=void*(*)(void*,int,int,const char*);
        auto user=reinterpret_cast<Handle>(GetProcAddress(api,"SteamAPI_GetHSteamUser"));
        auto pipe=reinterpret_cast<Handle>(GetProcAddress(api,"SteamAPI_GetHSteamPipe"));
        auto factory=reinterpret_cast<Factory>(GetProcAddress(api,"SteamInternal_CreateInterface"));
        auto network=reinterpret_cast<Network>(GetProcAddress(api,"SteamAPI_ISteamClient_GetISteamNetworking"));
        send=reinterpret_cast<Send>(GetProcAddress(api,"SteamAPI_ISteamNetworking_SendP2PPacket"));
        available=reinterpret_cast<Available>(GetProcAddress(api,"SteamAPI_ISteamNetworking_IsP2PPacketAvailable"));
        read=reinterpret_cast<Read>(GetProcAddress(api,"SteamAPI_ISteamNetworking_ReadP2PPacket"));
        state=reinterpret_cast<State>(GetProcAddress(api,"SteamAPI_ISteamNetworking_GetP2PSessionState"));
        if(!user||!pipe||!factory||!network||!send||!available||!read||!state){report("exports_missing");return;}
        int u=user(),p=pipe();if(!u||!p){report("steam_not_ready");return;}
        auto client=factory("SteamClient017");if(!client){report("client_missing");return;}
        net=network(client,u,p,"SteamNetworking005");if(!net){report("network_missing");return;}
        if(!state(net,peer,&session)||!session.active){report("peer_connection_not_active_join_private_lobby_first");return;}
        // The other player may start first. Validate queued messages through the
        // normal size, mode, peer and fresh-nonce checks instead of rejecting them.
        if(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&protocol.local_nonce),sizeof(protocol.local_nonce),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0||!protocol.local_nonce){report("random_failed");return;}
        if(reuse_mode && (!prepare_owned_replay || !prepare_owned_replay())){report("ownership_prepare_failed");return;}
        start=now;running=true;stopped=false;report("running");return;
    }
    const auto phase_start=input_mode && capture.armed?capture.armed_at:start;
    const auto phase_limit=input_mode?(capture.armed?(suite_mode && (suite.profile==15 || suite.profile==16)?600000u:240000u):600000u):60000u;
    if(!data_done && now-phase_start>=phase_limit){if(suite_mode && suite.profile==16)abort_combat_recording(protocol.local_nonce);running=false;stopped=true;report(!input_mode&&protocol.mutual?"PASS":input_mode&&!capture.armed?"lobby_wait_timeout":"timeout");return;}
    auto stop_file=report_path.parent_path()/L"steam-telemetry-stop.arm";
    if(std::filesystem::exists(stop_file)){std::filesystem::remove(stop_file);if(suite_mode && suite.profile==16)abort_combat_recording(protocol.local_nonce);running=false;stopped=true;if(!data_done)report("stopped_by_user");return;}
    if(!state(net,protocol.expected_peer,&session)||!session.active){if(suite_mode && suite.profile==16)abort_combat_recording(protocol.local_nonce);running=false;stopped=true;if(!data_done)report("peer_disconnected");return;}
    if(data_done && last_peer && now-last_peer>15000){running=false;stopped=true;return;}
    // Bounded work once per host update; no additional 16ms input-mode gate.
    for(unsigned i=0;i<(input_mode?128u:8u);++i){
        uint32_t bytes=0;if(!available(net,&bytes,channel))break;
        if(auto* start=sc6::research::combat_start_exchange.load(std::memory_order_acquire);
            start && input_mode && bytes==sc6::research::CombatStartPacket::Bytes{}.size()){
            sc6::research::CombatStartPacket::Bytes packet{};uint64_t sender=0;uint32_t size=0;
            if(!read(net,packet.data(),static_cast<uint32_t>(packet.size()),&size,&sender,channel))break;
            if(start->receive(sender,protocol.mutual,packet,size)){++received;last_peer=now;}else ++rejected;
            continue;
        }
        if(input_mode && bytes==sizeof(steam_inputs::Packet)){
            steam_inputs::Packet packet{};uint64_t sender=0;uint32_t size=0;
            if(!read(net,&packet,sizeof(packet),&size,&sender,channel))break;
            const auto packet_received_us=clock_us();
            if(!protocol.mutual||sender!=protocol.expected_peer||size!=sizeof(packet)||packet.nonce!=protocol.remote_nonce||packet.echo!=protocol.local_nonce){++rejected;continue;}
            if(packet.magic!=steam_inputs::signature){++rejected;continue;}
            if(suite_mode && (suite.profile==6 || suite.profile==8 || (suite.profile==9 || suite.profile==10)) && (packet.kind==8 || packet.kind==9 || packet.kind==10)){
                auto* r=sc6::research::combat_resource_preparation.load();bool accepted=false;
                if(r && r->cycles_enabled.load(std::memory_order_acquire) && !packet.own_hash && !packet.received_hash){
                    accepted=packet.kind==10?repeated.receive(packet.index,packet.record.data):
                        r->cycles->receive(packet.kind==9,packet.index,packet.record.data);
                }
                if(accepted){++received;last_peer=now;}else ++rejected;
                continue;
            }
            if(suite_mode && (suite.profile==5 || suite.profile==7) && (packet.kind==8 || packet.kind==9)){
                auto* resources=sc6::research::combat_resource_preparation.load();bool accepted=false;
                if(resources && resources->correction_enabled.load(std::memory_order_acquire) && !packet.own_hash && !packet.received_hash){
                    auto& exchange=resources->correction->exchange;
                    accepted=packet.kind==8?exchange.receive(packet.index,packet.record.data):
                        packet.index==3 && exchange.receive_certificate(packet.record.data);
                    exchange.update();
                }
                if(accepted){++received;last_peer=now;}else ++rejected;
                continue;
            }
            if(suite_mode && packet.kind==7){
                sc6::research::StartupSuite::Words words{};for(unsigned w=0;w<10;++w)words[w]=packet.record.data[w];
                if(packet.index==1 && !packet.own_hash && !packet.received_hash && suite.receive(words)){++received;last_peer=now;}else ++rejected;
                continue;
            }
            if(prepare_mode && packet.kind==6){
                sc6::research::StartupBoundaryAgreement::Words words{};
                for(unsigned w=0;w<words.size();++w)words[w]=packet.record.data[w];
                if(packet.index!=1 || packet.own_hash || packet.received_hash ||
                    !boundary_agreement.receive(words)){++rejected;continue;}
                last_peer=now;++received;continue;
            }
            if(packet.kind==5){
                bool valid=packet.index==1 && packet.own_hash==0 && packet.received_hash==0;
                for(unsigned w=1;w<observation::words;++w)valid=valid && packet.record.data[w]==0;
                if(!valid){++rejected;continue;}
                if(!capture.composition.announce(packet.record.data[0])){capture.export_csv(report_path.parent_path()/report_path.stem());
                running=false;stopped=true;report(capture.composition.failure);return;}
                last_peer=now;++received;continue;
            }
            if(packet.kind==3){
                // Echo only an authenticated-session peer's small diagnostic ping.
                if(packet.own_hash!=1){++rejected;continue;}
                packet.kind=4;packet.nonce=protocol.local_nonce;packet.echo=protocol.remote_nonce;
                packet.record={};const auto handling=clock_us()-packet_received_us;
                packet.record.data[0]=static_cast<uint32_t>(handling);packet.record.data[1]=static_cast<uint32_t>(handling>>32);
                if(send(net,protocol.expected_peer,&packet,sizeof(packet),1,channel))++sent;else ++send_failed;
                last_peer=now;++received;continue;
            }
            if(packet.kind==4){if(packet.own_hash==1 && ping.pong(packet.index,packet_received_us,(uint64_t(packet.record.data[1])<<32)|packet.record.data[0])){last_peer=now;++received;}else ++rejected;continue;}
            if(data_done)continue;
            try {if(capture.accept(packet))++received;else ++rejected;}
            catch(const std::exception& e){running=false;stopped=true;report(e.what());return;}
            continue;
        }
        if(bytes!=sizeof(Packet)){running=false;stopped=true;report("unexpected_channel_packet_size");return;}
        Packet packet{};uint64_t sender=0;uint32_t size=0;
        if(!read(net,&packet,sizeof(packet),&size,&sender,channel))break;
        if(protocol.receive(sender,packet,size)){++received;if(packet.echo==protocol.local_nonce)last_peer=now;}else ++rejected;
    }
    const auto receive_end=steam_inputs::Capture::timestamp();
    if(auto* start=sc6::research::combat_start_exchange.load(std::memory_order_acquire);start && input_mode && protocol.mutual && !data_done){
        static uint64_t last_start_send=0;
        if(now-last_start_send>=250){
            const auto start_packet=start->outgoing();last_start_send=now;
            if(send(net,protocol.expected_peer,start_packet.data(),static_cast<uint32_t>(start_packet.size()),1,channel))++sent;else ++send_failed;
        }
    }
    const auto packet=protocol.outgoing();
    // UnreliableNoDelay prevents deferred sends after the bounded test ends.
    if(!data_done){if(send(net,protocol.expected_peer,&packet,sizeof(packet),1,channel))++sent;else ++send_failed;}
    peer_live=protocol.mutual && last_peer && now-last_peer<=9000;
    if(input_mode && !data_done)sc6::research::combat_peer_context.publish(protocol,capture.composition.local_slot,now,peer_live);
    ping.update(clock_us());
    if(input_mode && protocol.mutual && ping.due(clock_us())){
        steam_inputs::Packet probe;probe.nonce=protocol.local_nonce;probe.echo=protocol.remote_nonce;probe.kind=3;probe.own_hash=1;probe.index=ping.begin(clock_us());
        if(send(net,protocol.expected_peer,&probe,sizeof(probe),1,channel))++sent;else {++send_failed;ping.send_failed(probe.index);}
    }
    if(input_mode && protocol.mutual && !data_done){
        if(continuous_mode && !sc6::research::NativeBoundarySnapshot::general_restore_implemented){
            running=false;stopped=true;report("continuous_unavailable_default_networking");return;
        }
        try {
            if(now-last_announcement>=1000){
            last_announcement=now;steam_inputs::Packet announcement;announcement.nonce=protocol.local_nonce;announcement.echo=protocol.remote_nonce;
            announcement.kind=5;announcement.index=1;announcement.record.data[0]=capture.composition.local_slot;
            if(send(net,protocol.expected_peer,&announcement,sizeof(announcement),1,channel))++sent;else ++send_failed;
            }
            if(prepare_mode && !(continuous_mode && sc6::research::combat_routes.load(std::memory_order_acquire))){
                if(suite_mode && (suite.profile==6 || suite.profile==8 || (suite.profile==9 || suite.profile==10))){auto* r=sc6::research::combat_resource_preparation.load();
                    if(r && r->cycles_enabled.load(std::memory_order_acquire) && r->cycles->active()){
                        auto& bank=*r->cycles;auto* c=bank.current();c->runner.exchange.update();
                        repeated.update(bank,r->clock_identity.load(),r->clock_frame.load(),r->retry_observation.read(now));
                        c=bank.current();
                        if(now-correction_sent>=20){correction_sent=now;
                            for(unsigned i=0;i<4;++i){sc6::research::StartupCorrectionExchange::Words words{};
                                if(!(i<3?c->runner.exchange.local(i,words):c->runner.exchange.certificate(words)))continue;
                                steam_inputs::Packet message;message.nonce=protocol.local_nonce;message.echo=protocol.remote_nonce;
                                message.kind=i<3?8:9;message.index=bank.packet_index(bank.active(),i);message.record.data=words;
                                if(send(net,protocol.expected_peer,&message,sizeof(message),1,channel))++sent;else ++send_failed;
                            }
                            for(unsigned i=1;i<=bank.limit;++i){sc6::research::StartupCorrectionExchange::Words words{};
                                if(!repeated.outgoing(i,words))continue;
                                steam_inputs::Packet message;message.nonce=protocol.local_nonce;message.echo=protocol.remote_nonce;
                                message.kind=10;message.index=i;message.record.data=words;
                                if(send(net,protocol.expected_peer,&message,sizeof(message),1,channel))++sent;else ++send_failed;
                            }
                        }
                        if(repeated.failed){suite.reason=r->replay_restart()?"repeated_correction_failed_restart_required":"correction_protocol_failed";suite_finishing=true;abort_combat_recording(protocol.local_nonce);}
                    }
                }
                if(suite_mode && (suite.profile==5 || suite.profile==7)){auto* r=sc6::research::combat_resource_preparation.load();
                    if(r && r->correction_enabled.load(std::memory_order_acquire)){
                        auto& exchange=r->correction->exchange;exchange.update();
                        if(now-correction_sent>=20){correction_sent=now;
                            for(unsigned i=0;i<4;++i){sc6::research::StartupCorrectionExchange::Words words{};
                                if(!(i<3?exchange.local(i,words):exchange.certificate(words)))continue;
                                steam_inputs::Packet message;message.nonce=protocol.local_nonce;message.echo=protocol.remote_nonce;
                                message.kind=i<3?8:9;message.index=i;message.record.data=words;
                                if(send(net,protocol.expected_peer,&message,sizeof(message),1,channel))++sent;else ++send_failed;
                            }
                        }
                    }
                }
                if(suite_mode && (suite.profile>=12 && suite.profile<=16)){auto* r=sc6::research::combat_resource_preparation.load();
                    if(r && r->retained_replay_enabled.load(std::memory_order_acquire)){
                        const auto& rr=*r->retained_replay;
                        if((rr.restart.load() || rr.failed.load() || rr.retired.load()) && !rr.terminal()){
                            if(now-last_report>=1000){last_report=now;report("waiting_for_retained_failure_evidence");}return;
                        }
                    }
                }
                if(suite_mode){auto* r=sc6::research::combat_resource_preparation.load();if(r && r->replay_restart()){
                    suite.reason=(suite.profile>=12 && suite.profile<=16)?"retained_object_replay_failed_restart_required":suite.profile>=5?"prediction_correction_failed_restart_required":"reference_replay_failed_restart_required";running=false;stopped=true;
                    abort_combat_recording(protocol.local_nonce);
                    report(suite.reason);return;
                }}
                // Completion must not depend on the combat clock remaining alive at match end.
                if(suite_mode){auto* r=sc6::research::combat_resource_preparation.load();
                    if(r && r->match_end_requested.load()){
                        suite.reason=r->failed.load()?"match_end_cleanup_failed":
                            (r->retained_replay && r->retained_replay->verified.load()?"match_ended_after_verified_cycles":"match_ended_before_target");
                        suite_finishing=true;
                    }
                }
                if(suite_mode && suite_finishing){
                    auto* r=sc6::research::combat_resource_preparation.load();
                    abort_combat_recording(protocol.local_nonce);
                    if(r && !r->retired.load()){
                        if(now-last_report>=1000){last_report=now;report("waiting_for_resource_retirement");}return;
                    }
                    const auto round=round_event::snapshot();suite.observe(now,round.round,round.counter);
                    if(!suite.elapsed(now)){if(now-last_report>=1000){last_report=now;report("observing_suite_gameplay");}return;}
                    if(suite.profile==16 && r && r->retained_replay){
                        auto& rr=*r->retained_replay;
                        if(rr.evidence_flushed.load()<rr.completed_cycles.load()){
                            report("flushing_full_match_evidence");return;
                        }
                    }
                    if(!suite.sealed){
                        unsigned pass=1,fail=0,skip=0;
                        if(r && r->ready.load() && !r->failed.load())pass|=2;else fail|=2;
                        if(r && r->pairs.count.load()==120 && !r->pairs.overwritten.load() && !r->pairs.unpaired.load() && !r->pairs.discontinuities.load())pass|=4;else fail|=4;
                        if(boundary_agreement.ready())pass|=8;else fail|=8;
                        if(suite.profile<2)skip|=16;else if(r && r->hold_test.state.load()==3)pass|=16;else if(r && r->hold_test.state.load()>=4)fail|=16;else skip|=16;
                        if(suite.profile<3)skip|=32;else if(r && r->snapshots.captured.load()==4 && r->snapshots.retired.load() && !r->snapshots.failed.load())pass|=32;
                        else if(r && r->snapshots.failed.load())fail|=32;else if(suite.profile==4 && r && r->replay_enabled.load()){if(r->replay->captured.load()==4)pass|=32;else fail|=32;}else skip|=32;
                        if(suite.profile<4 || suite.profile==11)skip|=64;else if(r && r->replay_enabled.load() && r->replay->verified.load())pass|=64;else fail|=64;
                        if((suite.profile==5 || suite.profile==7)){
                            pass&=~(32u|64u);fail&=~(32u|64u);skip&=~(32u|64u);
                            if(r && r->correction_enabled.load()){
                                const auto& c=*r->correction;
                                if(c.captured.load()==4)pass|=32;else fail|=32;
                                if(c.corrected.load()==3 && c.retired.load() && !c.restart.load())pass|=64;else fail|=64;
                                if(c.verified.load() && c.changed.load() && c.exchange.ready.load() && !c.exchange.failed.load())pass|=128;else fail|=128;
                            }else fail|=32|64|128;
                        }
                        if((suite.profile==6 || suite.profile==8 || (suite.profile==9 || suite.profile==10))){
                            pass&=~(16u|32u|64u|128u);fail&=~(16u|32u|64u|128u);skip&=~(16u|32u|64u|128u);
                            bool holds=true,saves=true;
                            if(!r || !r->cycles_enabled.load())holds=saves=false;
                            else for(unsigned i=0;i<r->cycles->active();++i){if(r->cycles->skipped(i))continue;const auto& c=r->cycles->at(i);
                                holds=holds && c.entry.state.load()==3 && c.confirmation.state.load()==3;
                                saves=saves && c.runner.captured.load()==4;
                            }
                            (holds?pass:fail)|=16;(saves?pass:fail)|=32;
                            const bool verified=r && r->cycles_enabled.load() && r->cycles->all_verified() && repeated.complete && !repeated.failed;
                            (verified?pass:fail)|=64|128;
                            if((suite.profile==9 || suite.profile==10) && repeated.exhausted){pass&=~(16u|32u|64u|128u);fail&=~(16u|32u|64u|128u);skip|=16|32|64|128;}
                        }
                        if(suite.profile==11){
                            pass&=~(32u|64u);fail&=~(32u|64u);skip&=~(32u|64u);
                            if(!r || r->retention.failed.load() || !r->retired.load())fail|=32|64;
                            else {
                                if(r->retention.windows.load())pass|=32;else skip|=32;
                                const auto result=r->retention_result();
                                if(result==1)pass|=64;else if(result==2)fail|=64;else skip|=64;
                            }
                        }
                        if((suite.profile>=12 && suite.profile<=16)){
                            pass&=~(32u|64u|128u);fail&=~(32u|64u|128u);skip&=~(32u|64u|128u);
                            if(!r || !r->retained_replay_enabled.load())fail|=32|64|128;
                            else {
                                const auto& rr=*r->retained_replay;
                                if(rr.captured.load()>=4 && rr.retired.load())pass|=32;else if(rr.failed.load())fail|=32;else skip|=32;
                                if(rr.verified.load() && !rr.failed.load() && !rr.restart.load() && !rr.evidence_io_failures.load() && (suite.profile!=16 || (rr.match_completed.load() && r->match_end_clean.load())))pass|=64|128;
                                else if(rr.failed.load() || rr.evidence_io_failures.load())fail|=64|128;else skip|=64|128;
                            }
                        }
                        suite.seal(pass,fail,skip);
                    }
                    if(now-suite.last_send>=100){
                        suite.last_send=now;steam_inputs::Packet result;result.nonce=protocol.local_nonce;result.echo=protocol.remote_nonce;
                        result.kind=7;result.index=1;const auto words=suite.outgoing();for(unsigned w=0;w<10;++w)result.record.data[w]=words[w];
                        if(send(net,protocol.expected_peer,&result,sizeof(result),1,channel))++sent;else ++send_failed;
                    }
                    if(!suite.complete()){if(now-last_report>=1000){last_report=now;report("waiting_for_suite_peer_result");}return;}
                    if(!suite.agreed_at)suite.agreed_at=now;
                    if(now-suite.agreed_at<1000)return;
                    running=false;stopped=true;report((suite.local[4] || suite.peer[4])?"SUITE_COMPLETED_WITH_FAILURES":((suite.profile==15 || suite.profile==16) && r && r->match_end_clean.load() && ((suite.local[5]|suite.peer[5])&192))?"SUITE_MATCH_ENDED_INCOMPLETE":((suite.profile==9 || suite.profile==10) && repeated.exhausted)?"SUITE_NO_SUITABLE_WINDOW":((suite.profile==11 || (suite.profile>=12 && suite.profile<=16)) && ((suite.local[5]|suite.peer[5])&224))?"SUITE_RETENTION_NOT_OBSERVED":"SUITE_COMPLETE");return;
                }
                if(!capture.armed){
                    const auto prepared=capture.prepare_owned(protocol.local_nonce,continuous_mode || (suite_mode && suite.first_match_start()));
                    if(prepared!=steam_inputs::Capture::OwnedStart::attached){running=false;stopped=true;report("combat_buffer_start_failed");return;}
                }
                sc6::research::NativeClockDiscovery::Observation clock;
                if(!capture.composition.peer_slot || !sc6::research::native_clock_hook::discovery.read(now,clock)){
                    if(now-last_report>=1000){last_report=now;report("waiting_for_combat_clock");}return;
                }
                if(clock.clock.local_mask!=capture.composition.local_slot){running=false;stopped=true;abort_combat_recording(protocol.local_nonce);report("combat_clock_slot_mismatch");return;}
                auto* resources=sc6::research::combat_resource_preparation.load();
                if(resources && continuous_mode)resources->capture_enabled=false;
                if(resources && suite_mode){resources->capture_enabled.store(suite.profile==3);if(suite.profile==11)resources->enable_retention();if((suite.profile>=12 && suite.profile<=16))resources->enable_retained_replay(suite.profile>=13 && suite.profile<=16,(suite.profile==14 || (suite.profile==15 || suite.profile==16)),(suite.profile==15 || suite.profile==16),suite.profile==16);if(suite.profile==4)resources->enable_replay();if((suite.profile==5 || suite.profile==7))resources->enable_correction(suite.profile==7,(suite.profile==9 || suite.profile==10));if((suite.profile==6 || suite.profile==8 || (suite.profile==9 || suite.profile==10)))resources->enable_cycles(suite.profile==8,(suite.profile==9 || suite.profile==10),suite.profile==10?100:0);}
                if(!resources || resources->failed.load()){
                    abort_combat_recording(protocol.local_nonce);if(suite_mode){suite.reason="startup_resource_failed";suite_finishing=true;return;}running=false;stopped=true;report("startup_resource_failed");return;
                }
                if(!resources->ready.load() || !resources->clock_ready.load()){
                    if(now-last_report>=1000){last_report=now;report("waiting_for_combat_resources");}return;
                }
                if(now-capture.armed_at<10000)return;
                if(now<resources->clock_started.load() || now-resources->clock_started.load()<5000)return;
                if(!boundary_agreement.bound() && !boundary_agreement.begin(resources->pairs,capture.composition.local_slot)){
                    abort_combat_recording(protocol.local_nonce);if(suite_mode){suite.reason="boundary_pairs_rejected";suite_finishing=true;return;}running=false;stopped=true;report("boundary_pairs_rejected");return;
                }
                const auto boundary_hold=resources->hold_test.state.load();
                const auto boundary_rejection=boundary_agreement.runtime_rejection(suite_mode?suite.profile:0,
                    boundary_hold,repeated.started,continuous_queued,clock.clock.identity,
                    resources->pairs.rows[0].identity,clock.clock.frame);
                if(boundary_rejection){
                    boundary_rejection_mask=boundary_rejection;boundary_rejection_live_identity=clock.clock.identity;
                    boundary_rejection_startup_identity=resources->pairs.rows[0].identity;boundary_rejection_hold=boundary_hold;
                    abort_combat_recording(protocol.local_nonce);if(suite_mode){suite.reason="boundary_agreement_rejected_or_expired";suite_finishing=true;return;}running=false;stopped=true;report("boundary_agreement_rejected_or_expired");return;
                }
                if(now-boundary_sent>=100){
                    boundary_sent=now;steam_inputs::Packet proposal;proposal.nonce=protocol.local_nonce;
                    proposal.echo=protocol.remote_nonce;proposal.kind=6;proposal.index=1;
                    const auto words=boundary_agreement.outgoing();
                    for(unsigned w=0;w<words.size();++w)proposal.record.data[w]=words[w];
                    if(send(net,protocol.expected_peer,&proposal,sizeof(proposal),1,channel))++sent;else ++send_failed;
                }
                if(!boundary_agreement.ready()){
                    if(now-last_report>=1000){last_report=now;report("waiting_for_boundary_agreement");}return;
                }
                if(!boundary_agreed_at)boundary_agreed_at=now;
                if(continuous_mode){
                    if(!continuous_queued){
                        ContinuousStartRequest request{protocol.local_nonce,protocol.remote_nonce,protocol.expected_peer,
                            protocol.expected_signature,protocol.local_nonce,capture.composition.local_slot,
                            resources->pairs.rows[0].identity,boundary_agreement.target_clock(),boundary_agreement.target_battle(),true};
                        if(!queue_continuous_start || !queue_continuous_start(request)){
                            running=false;stopped=true;report("continuous_start_request_rejected");return;
                        }
                        continuous_queued=true;
                    }
                    if(continuous_start_state && continuous_start_state()==4){running=false;stopped=true;report("continuous_start_rejected_restart_required");return;}
                    if(now-last_report>=1000){last_report=now;report("waiting_for_continuous_activation");}return;
                }
                if(suite_mode && (suite.profile==5 || suite.profile==7) && !resources->request_confirmation_hold(resources->pairs.rows[0].identity,
                    boundary_agreement.target_clock(),boundary_agreement.target_battle(),capture.composition.local_slot)){
                    suite.reason="confirmation_hold_request_failed";suite_finishing=true;abort_combat_recording(protocol.local_nonce);return;
                }
                if(suite_mode && suite.profile==1){
                    if(now-boundary_agreed_at<1000)return;
                    suite_finishing=true;abort_combat_recording(protocol.local_nonce);return;
                }
                if(suite_mode && (suite.profile==6 || suite.profile==8 || (suite.profile==9 || suite.profile==10))){
                    if(!repeated.begin(*resources->cycles,resources->pairs.rows[0].identity,boundary_agreement.target_clock(),
                        boundary_agreement.target_battle(),capture.composition.local_slot,(suite.profile==9 || suite.profile==10))){
                        suite.reason="repeated_correction_start_failed";suite_finishing=true;abort_combat_recording(protocol.local_nonce);return;
                    }
                    if(repeated.complete){if(repeated.exhausted)suite.reason="no_suitable_window_observed";suite_finishing=true;abort_combat_recording(protocol.local_nonce);return;}
                    if(now-last_report>=1000){last_report=now;report("waiting_for_repeated_correction");}return;
                }
                if(resources->hold_test.state.load()==0){
                    if(!resources->hold_test.request(resources->pairs.rows[0].identity,boundary_agreement.target_clock(),boundary_agreement.target_battle())){
                        running=false;stopped=true;abort_combat_recording(protocol.local_nonce);report("hold_request_failed");return;
                    }
                }
                const auto hold_state=resources->hold_test.state.load();
                if(suite_mode && hold_state==4){suite.reason="hold_boundary_rejected";suite_finishing=true;abort_combat_recording(protocol.local_nonce);return;}
                if(hold_state==4 || hold_state==5){
                    running=false;stopped=true;abort_combat_recording(protocol.local_nonce);
                    report(hold_state==5?"hold_failed_restart_required":"hold_boundary_rejected");return;
                }
                if(hold_state!=3){if(now-last_report>=1000){last_report=now;report("waiting_for_boundary_hold");}return;}
                if(suite_mode && suite.profile==2){suite_finishing=true;abort_combat_recording(protocol.local_nonce);return;}
                if(suite_mode && (suite.profile==5 || suite.profile==7)){
                    auto& c=*resources->correction;
                    if(c.failed.load() || c.verified.load() || c.exchange.failed.load() || resources->confirmation_hold.state.load()==4){
                        if(!c.verified.load())suite.reason="prediction_correction_rejected";
                        suite_finishing=true;abort_combat_recording(protocol.local_nonce);return;
                    }
                    if(now-last_report>=1000){last_report=now;report("waiting_for_prediction_correction");}return;
                }
                if(suite_mode && suite.profile==4){
                    if(resources->replay->failed.load() || resources->replay->verified.load()){
                        if(resources->replay->failed.load())suite.reason="reference_replay_preflight_or_capture_failed";
                        suite_finishing=true;abort_combat_recording(protocol.local_nonce);return;
                    }
                    if(now-last_report>=1000){last_report=now;report("waiting_for_reference_replay");}return;
                }
                if(suite_mode && (suite.profile>=12 && suite.profile<=16)){
                    const auto& rr=*resources->retained_replay;
                    if(rr.terminal() && (rr.retired.load() || rr.failed.load())){
                        if(rr.failed.load())suite.reason="retained_object_replay_failed";
                        suite_finishing=true;abort_combat_recording(protocol.local_nonce);return;
                    }
                    if(now-last_report>=1000){last_report=now;report("observing_retained_object_replay");}return;
                }
                if(suite_mode && suite.profile==11){
                    if(resources->retention.retired.load() || resources->retention.failed.load()){
                        if(resources->retention.failed.load())suite.reason="retention_capture_or_cleanup_failed";
                        suite_finishing=true;abort_combat_recording(protocol.local_nonce);return;
                    }
                    if(now-last_report>=1000){last_report=now;report("observing_targeted_retention");}return;
                }
                if(resources->snapshots.failed.load()){
                    if(suite_mode){suite.reason="snapshot_capture_failed";suite_finishing=true;abort_combat_recording(protocol.local_nonce);return;}
                    abort_combat_recording(protocol.local_nonce);
                    if(!resources->retired.load())return;
                    running=false;stopped=true;report("startup_snapshot_capture_failed");return;
                }
                if(resources->snapshots.captured.load()!=4 || !resources->snapshots.retired.load()){
                    if(now-last_report>=1000){last_report=now;report("waiting_for_boundary_snapshots");}return;
                }
                if(suite_mode){suite_finishing=true;abort_combat_recording(protocol.local_nonce);return;}
                abort_combat_recording(protocol.local_nonce);
                if(!resources->retired.load()){
                    if(now-last_report>=1000){last_report=now;report("waiting_for_resource_retirement");}return;
                }
                sc6::research::owned_transport_attachment.detach(capture.owned_nonce);
                running=false;stopped=true;report("PREPARED_NOT_ACTIVATED");return;
            }
            if(!network_game_handoff.bind(protocol.local_nonce,protocol.remote_nonce,capture.composition.local_slot))
                throw std::runtime_error("game_handoff_session_conflict");
            if(!capture.armed){
                const auto marker=round_event::snapshot();
                if(!marker.round || marker.generation<=initial_round_generation){
                    if(now-last_report>=1000){last_report=now;report("waiting_for_local_round_marker");}
                    return;
                }
                capture.arm(report_path.parent_path(),protocol.local_nonce,checkpoint_mode,replay_mode,late_mode,reuse_mode);
            }
            if(!capture.composition.bind_replay_session(protocol.local_nonce,protocol.remote_nonce)){
                running=false;stopped=true;report(capture.composition.failure);return;
            }
            capture.update(protocol.local_nonce);
            auto& exchange=capture.exchange;
            if(!capture.composition.update(exchange.local,exchange.published,exchange.remote,exchange.seen,&exchange.local_rounds,&exchange.remote_rounds)){
                capture.export_csv(report_path.parent_path()/report_path.stem());
                running=false;stopped=true;report(capture.composition.failure);return;
            }
            for(unsigned i=0;!continuous_mode && i<exchange.published;++i)if(capture.composition.staged[i] && !handed_off[i]){
                const auto round=exchange.local_rounds[i];
                NetworkGameHandoff::Request request{protocol.local_nonce,protocol.remote_nonce,capture.composition.local_slot,
                    exchange.local[i].data[0],round.number,round.event_counter,capture.composition.envelopes[i]};
                if(!network_game_handoff.publish(request))break;
                handed_off[i]=true;
            }
            const auto* combat=sc6::research::combat_routes.load(std::memory_order_acquire);
            if(const auto* sink=combat?combat->inputs:sc6::scheduler_input_sink.load(std::memory_order_acquire)){
                unsigned budget=8;
                for(unsigned i=0;i<exchange.published && budget;++i)if(capture.composition.staged[i] && !scheduler_handed_off[i]){
                    --budget;const auto round=exchange.local_rounds[i];
                    NetworkGameHandoff::Request request{protocol.local_nonce,protocol.remote_nonce,capture.composition.local_slot,
                        exchange.local[i].data[0],round.number,round.event_counter,capture.composition.envelopes[i]};
                    const auto result=sc6::submit_scheduler_input(*sink,request,scheduler_handed_off[i]);
                    if(result==sc6::SchedulerPublish::full)break;
                    if(result==sc6::SchedulerPublish::rejected){running=false;stopped=true;report("scheduler_confirmation_rejected");return;}
                }
            }
            if(!continuous_mode)capture.shadow.update(exchange.local,exchange.published,exchange.remote,exchange.seen,capture.arrival_qpc,exchange.received==steam_inputs::limit);
            const auto send_begin=steam_inputs::Capture::timestamp();
            for(unsigned budget=0;budget<12 && exchange.published;++budget){
                steam_inputs::Packet data;data.nonce=protocol.local_nonce;data.echo=protocol.remote_nonce;data.kind=1;
                const bool fresh=budget<8 && exchange.fresh<exchange.published;
                if(budget<8 && !fresh)continue;
                data.index=fresh?exchange.fresh:exchange.cursor++%exchange.published;data.record=exchange.local[data.index];
                data.own_hash=exchange.local_rounds[data.index].number;data.received_hash=exchange.local_rounds[data.index].event_counter;
                const auto attempt_begin=steam_inputs::Capture::timestamp();
                if(send(net,protocol.expected_peer,&data,sizeof(data),1,channel)){
                    ++sent;if(fresh)++exchange.fresh;
                    if(!capture.first_send_qpc[data.index]){LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);capture.first_send_qpc[data.index]=stamp.QuadPart;capture.first_send_begin_qpc[data.index]=attempt_begin;}
                }else ++send_failed;
            }
            if(exchange.published==steam_inputs::limit && exchange.received==steam_inputs::limit){
                steam_inputs::Packet summary;summary.nonce=protocol.local_nonce;summary.echo=protocol.remote_nonce;summary.kind=2;
                summary.own_hash=steam_inputs::Exchange::checksum(exchange.local,exchange.local_rounds);summary.received_hash=steam_inputs::Exchange::checksum(exchange.remote,exchange.remote_rounds);
                if(send(net,protocol.expected_peer,&summary,sizeof(summary),1,channel))++sent;else ++send_failed;
            }
            capture.record_pump(pump_start,receive_end,send_begin);
            if(capture.complete() && capture.composition.peer_slot){
                if(!completed)completed=now;
                if(now-completed>=10000){
                    if(network_game_handoff.read.load()!=network_game_handoff.write.load()){
                        if(now-completed<15000){report("running");return;}
                        network_game_handoff.drain_finished.store(true,std::memory_order_release);
                        capture.export_csv(report_path.parent_path()/report_path.stem());running=false;stopped=true;report("handoff_drain_timeout");return;
                    }
                    network_game_handoff.drain_finished.store(true,std::memory_order_release);
                    capture.export_csv(report_path.parent_path()/report_path.stem());data_done=true;report(continuous_mode?"continuous_capture_ended_restart_required":"PASS");return;}
            }
        }catch(const std::exception& e){running=false;stopped=true;report(e.what());return;}
    }
    if(!data_done && now-last_report>=1000){last_report=now;report(continuous_mode && sc6::research::combat_routes.load()?"continuous_running":"running");}
}
}








