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
#include "NetworkCheckpointStatus.hpp"
#include "StatusOverlay.hpp"
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
    static bool input_mode=false,checkpoint_mode=false,replay_mode=false,late_mode=false;
    static steam_inputs::Capture capture;
    static ULONGLONG completed=0;
    static bool data_done=false;
    static uint64_t initial_round_generation=0;
    static std::array<bool,steam_inputs::limit> handed_off{};
    static ULONGLONG last_peer=0,last_announcement=0;
    const auto now=GetTickCount64();
    if(stopped||now<next)return;
    next=now+(running?(input_mode?0:250):1000);
    ping.update(clock_us());
    const auto pump_start=steam_inputs::Capture::timestamp();
    auto report=[&](const char* status){
        std::ofstream out(report_path);
        for(unsigned phase=0;phase<2;++phase){
            out<<"native_routing_"<<phase<<"_valid="<<network_checkpoint_status.routing_valid[phase].load()<<"\n";
            out<<"native_routing_"<<phase<<"_dispatch_stage="<<network_checkpoint_status.dispatch_stage[phase].load()<<"\n";
            constexpr const char* names[]={"copied_p1_0","copied_p1_1","copied_p2_0","copied_p2_1","p1_selector","p1_mode","p1_route","p1_source","p1_input","p2_selector","p2_mode","p2_route","p2_source","p2_input","state_vtable_rva","state_handler_rva","state_counter","state_previous"};
            for(unsigned i=0;i<18;++i)out<<"native_routing_"<<phase<<"_"<<names[i]<<"="<<network_checkpoint_status.routing_values[phase][i].load()<<"\n";
        }
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
        out<<"schema=1\nscope="<<(input_mode?(late_mode?"steam_live_late_input_scheduler":replay_mode?"steam_native_three_frame_remote_correction":checkpoint_mode?"steam_native_checkpoint_save":"steam_recorded_inputs"):"steam_handshake_only")<<"\nrollback_active=0\ninjection="<<network_checkpoint_status.prediction_invoked.load()<<"\nrestore="<<network_checkpoint_status.restore_invoked.load()<<"\ngame_alignment_verified=0\nstatus="<<status
            <<"\nchannel="<<channel<<"\npeer="<<protocol.expected_peer<<"\nsent="<<sent<<"\nreceived="<<received
            <<"\nrejected="<<rejected<<"\nsend_failed="<<send_failed<<"\nroundtrip_proof="<<protocol.proof
            <<"\nmutual_confirmation="<<protocol.mutual<<"\nsteam_using_relay="<<unsigned(session.relay)<<'\n';
        out<<"ping_method=minimum_of_5_minus_measured_reply_handling\nping_network_only=0\nping_burst_samples="<<ping.latest_samples
           <<"\nping_selected_raw_us="<<ping.latest_raw_us<<"\nping_selected_reply_handling_us="<<ping.latest_handling_us<<'\n';
        auto measured=ping.value(clock_us());out<<"ping_refresh_seconds=3\nping_rtt_ms="<<(measured?std::to_string(*measured):"unavailable")<<'\n';
        if(input_mode)out<<"target_records="<<steam_inputs::limit<<"\nslot_mapping_assumed=1\nlocal_records="<<capture.exchange.published<<"\nremote_records="<<capture.exchange.received
            <<"\npeer_checksum_match="<<capture.exchange.peer_match<<"\nlocal_checksum="<<observation::checksum(capture.exchange.local)
            <<"\nremote_checksum="<<observation::checksum(capture.exchange.remote)<<"\nqpc_frequency="<<(capture.shared?capture.shared->frequency:0)<<'\n';
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
            <<"\nnative_replay_enabled="<<replay_mode
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
        if(input_mode){const auto& s=capture.shadow;out<<"shadow_enabled=1\nshadow_delay_frames=4\nshadow_limit_frames=3\nshadow_remote_slot="<<s.remote_slot
            <<"\nshadow_scheduled="<<s.scheduled<<"\nshadow_evaluated="<<s.evaluated<<"\nshadow_predicted="<<s.predicted
            <<"\nshadow_errors="<<s.wrong<<"\nshadow_over_limit="<<s.over_limit<<"\nshadow_max_depth="<<s.max_depth
            <<"\nshadow_unseeded="<<s.unseeded<<"\nshadow_censored="<<s.censored<<"\nshadow_truth_mismatches="<<s.truth_mismatches
            <<"\nshadow_invalid="<<s.invalid<<"\nshadow_no_overlap="<<s.no_overlap<<"\nshadow_tail_excluded=4\nshadow_within_observed_bounds="<<s.within_observed_bounds()<<'\n';}
    };
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
        late_mode=input_mode && mode=="PRIVATE_STEAM_LATE";
        replay_mode=input_mode && mode=="PRIVATE_STEAM_REPLAY";
        checkpoint_mode=input_mode && (mode=="PRIVATE_STEAM_CHECKPOINTS" || replay_mode);
        bool trailing=bool(config>>extra);config.close();std::filesystem::remove(arm);
        if(!valid||trailing||mode!=(input_mode?(late_mode?"PRIVATE_STEAM_LATE":replay_mode?"PRIVATE_STEAM_REPLAY":checkpoint_mode?"PRIVATE_STEAM_CHECKPOINTS":"PRIVATE_STEAM_INPUTS"):"PRIVATE_STEAM_HANDSHAKE")||pid!=GetCurrentProcessId()||peer<=76561197960265728ULL||peer>76561202255233023ULL){report("invalid_arm");return;}
        if(input_mode){initial_round_generation=round_event::snapshot().generation;protocol.expected_signature=late_mode?0x001F53433648534bULL:replay_mode?0x001E53433648534bULL:checkpoint_mode?0x000C53433648534bULL:0x000B53433648534bULL;capture.shadow.remote_slot=3-local_slot;capture.composition.configure(local_slot);}
        protocol.expected_peer=peer;
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
        start=now;running=true;stopped=false;report("running");return;
    }
    const auto phase_start=input_mode && capture.armed?capture.armed_at:start;
    const auto phase_limit=input_mode?(capture.armed?240000u:600000u):60000u;
    if(!data_done && now-phase_start>=phase_limit){running=false;stopped=true;report(!input_mode&&protocol.mutual?"PASS":input_mode&&!capture.armed?"lobby_wait_timeout":"timeout");return;}
    auto stop_file=report_path.parent_path()/L"steam-telemetry-stop.arm";
    if(std::filesystem::exists(stop_file)){std::filesystem::remove(stop_file);running=false;stopped=true;if(!data_done)report("stopped_by_user");return;}
    if(!state(net,protocol.expected_peer,&session)||!session.active){running=false;stopped=true;if(!data_done)report("peer_disconnected");return;}
    if(data_done && last_peer && now-last_peer>15000){running=false;stopped=true;return;}
    // Bounded work once per host update; no additional 16ms input-mode gate.
    for(unsigned i=0;i<(input_mode?128u:8u);++i){
        uint32_t bytes=0;if(!available(net,&bytes,channel))break;
        if(input_mode && bytes==sizeof(steam_inputs::Packet)){
            steam_inputs::Packet packet{};uint64_t sender=0;uint32_t size=0;
            if(!read(net,&packet,sizeof(packet),&size,&sender,channel))break;
            const auto packet_received_us=clock_us();
            if(!protocol.mutual||sender!=protocol.expected_peer||size!=sizeof(packet)||packet.nonce!=protocol.remote_nonce||packet.echo!=protocol.local_nonce){++rejected;continue;}
            if(packet.magic!=steam_inputs::signature){++rejected;continue;}
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
    const auto packet=protocol.outgoing();
    // UnreliableNoDelay prevents deferred sends after the bounded test ends.
    if(!data_done){if(send(net,protocol.expected_peer,&packet,sizeof(packet),1,channel))++sent;else ++send_failed;}
    peer_live=protocol.mutual && last_peer && now-last_peer<=9000;
    ping.update(clock_us());
    if(input_mode && protocol.mutual && ping.due(clock_us())){
        steam_inputs::Packet probe;probe.nonce=protocol.local_nonce;probe.echo=protocol.remote_nonce;probe.kind=3;probe.own_hash=1;probe.index=ping.begin(clock_us());
        if(send(net,protocol.expected_peer,&probe,sizeof(probe),1,channel))++sent;else {++send_failed;ping.send_failed(probe.index);}
    }
    if(input_mode && protocol.mutual && !data_done){
        try {
            if(now-last_announcement>=1000){
            last_announcement=now;steam_inputs::Packet announcement;announcement.nonce=protocol.local_nonce;announcement.echo=protocol.remote_nonce;
            announcement.kind=5;announcement.index=1;announcement.record.data[0]=capture.composition.local_slot;
            if(send(net,protocol.expected_peer,&announcement,sizeof(announcement),1,channel))++sent;else ++send_failed;
            }
            if(!network_game_handoff.bind(protocol.local_nonce,protocol.remote_nonce,capture.composition.local_slot))
                throw std::runtime_error("game_handoff_session_conflict");
            if(!capture.armed){
                const auto marker=round_event::snapshot();
                if(!marker.round || marker.generation<=initial_round_generation){
                    if(now-last_report>=1000){last_report=now;report("waiting_for_local_round_marker");}
                    return;
                }
                capture.arm(report_path.parent_path(),protocol.local_nonce,checkpoint_mode,replay_mode,late_mode);
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
            for(unsigned i=0;i<exchange.published;++i)if(capture.composition.staged[i] && !handed_off[i]){
                const auto round=exchange.local_rounds[i];
                NetworkGameHandoff::Request request{protocol.local_nonce,protocol.remote_nonce,capture.composition.local_slot,
                    exchange.local[i].data[0],round.number,round.event_counter,capture.composition.envelopes[i]};
                if(!network_game_handoff.publish(request))break;
                handed_off[i]=true;
            }
            capture.shadow.update(exchange.local,exchange.published,exchange.remote,exchange.seen,capture.arrival_qpc,exchange.received==steam_inputs::limit);
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
                    capture.export_csv(report_path.parent_path()/report_path.stem());data_done=true;report("PASS");return;}
            }
        }catch(const std::exception& e){running=false;stopped=true;report(e.what());return;}
    }
    if(!data_done && now-last_report>=1000){last_report=now;report("running");}
}
}

