#pragma once
#include <atomic>
#include <array>
#include <cstdint>
namespace sc6 {
struct NetworkCheckpointStatus {
    std::atomic<unsigned> window_reuse_completed{};
    std::atomic<unsigned> window_topology_retries{},window_topology_retry_first{},window_topology_retry_last{};
    std::atomic<unsigned> owned_cancel_reason{},owned_cancel_thread{},owned_cancel_expected{},owned_cancel_had_refs{};
    std::atomic<unsigned> owned_replay_started{},owned_replay_captures{},owned_replay_cleanup{};
    std::atomic<unsigned> window_missing_mask{},window_retry_frame{};
    std::atomic<unsigned> topology_mask{},topology_index{UINT32_MAX},topology_before_count{},topology_after_count{};
    std::array<std::atomic<uint64_t>,6> topology_before{},topology_after{};
    std::array<std::array<std::atomic<unsigned>,4>,2> window_rng{},window_rng_after_save{};
    std::array<std::array<std::atomic<unsigned>,3>,2> window_rng_steps{};
    std::atomic<unsigned> window_plan_result{},window_prepare_stage{},window_hop{},window_source_frame{},window_next_frame{},window_workspace_flags{};
    std::array<std::atomic<unsigned>,2> routing_valid{},dispatch_stage{};
    std::array<std::array<std::atomic<unsigned>,18>,2> routing_values{};
    std::array<std::array<std::atomic<unsigned>,4>,3> confirmation_attempts{};
    std::atomic<unsigned> comparison_stage{},comparison_flags{},comparison_rng_expected{},comparison_rng_actual{},comparison_pass_mask{};
    std::array<std::atomic<uint64_t>,10> comparison_ring{};
    std::array<std::atomic<uint64_t>,12> comparison_native{};
    std::array<std::atomic<uint64_t>,10> blend_block_diagnostic{};
    std::array<std::atomic<unsigned>,8> comparison_scalars{};
    std::atomic<unsigned long long> comparison_bytes{};
    std::atomic<unsigned> battle_gate{},battle_vtable{},battle_handler{};
    std::atomic<unsigned> completion_line{},completion_section{},completion_record{},completion_kind{},completion_offset{},completion_saved{},completion_live{};
    std::atomic<unsigned> wind_offset{},wind_saved_word{},wind_next_word{},wind_control_saved{},wind_control_next{};
    std::atomic<unsigned> capture_stage{},capture_exception{},capture_phase{},capture_mode_values_valid{};
    std::array<std::atomic<unsigned>,6> capture_mode_values{};
    std::atomic<unsigned> source_mismatch_found{},source_record{},source_stack_mask{};
    std::atomic<unsigned long long> source_size{},source_saved{},source_next{},source_writer_rva{};
    std::atomic<unsigned> prediction_fixture_slot{};
    std::atomic<unsigned> prediction_stage{},prediction_invoked{},prediction_captured{},prediction_state_changed{},prediction_rewound{};
    std::atomic<unsigned> network_input_delivered{},confirmation_misses{},confirmation_retries{};
    std::atomic<unsigned> enabled{},phase{},captured{},matched{},failure{},first_frame{},second_frame{};
    std::atomic<unsigned> readiness_attempted{},readiness_prepared{},readiness_rejection{};
    std::atomic<unsigned> replay_enabled{},replay_attempted{},restore_invoked{},restore_verified{},replay_invoked{},replay_verified{};
    std::atomic<unsigned> restore_stage{},restore_failure_line{},restore_exception{},restore_flags{};
    std::atomic<unsigned> wind_verified{},wind_capture_valid{},wind_failure_line{},wind_record{},wind_kind{},wind_size{};
    std::atomic<unsigned long long> wind_vtable_rva{};
    std::atomic<unsigned> camera_line{},camera_record{},camera_section{},camera_offset{},camera_a{},camera_b{},camera_c{},camera_live{};
    std::atomic<unsigned> depth_target{},depth_phase{},depth_hop{},depth_predicted{},depth_clean_restored{},depth_rewound{},depth_delivered{},depth_compared{};
    std::array<std::atomic<unsigned>,4> depth_frames{};
    std::array<std::atomic<unsigned>,3> depth_verified{};
    std::atomic<unsigned> repeated_target{},repeated_completed{},repeated_cycle{},repeated_failed{};
    std::array<std::array<std::atomic<unsigned>,4>,2> repeated_results{}; // first, second, slot, verified
    template<class T> static void clear(std::atomic<T>& value){value.store(0);}
    template<class T,size_t N> static void clear(std::array<T,N>& values){for(auto& value:values)clear(value);}
    void reset_attempt(){
        clear(routing_valid);
        clear(dispatch_stage);
        clear(routing_values);
        clear(confirmation_attempts);
        clear(comparison_rng_expected);clear(comparison_rng_actual);clear(comparison_ring);
        clear(blend_block_diagnostic);clear(comparison_native);clear(comparison_pass_mask);
        clear(comparison_stage);
        clear(comparison_flags);
        clear(comparison_scalars);
        clear(comparison_bytes);
        clear(battle_gate);
        clear(battle_vtable);
        clear(battle_handler);
        clear(completion_line);
        clear(completion_section);
        clear(completion_record);
        clear(completion_kind);
        clear(completion_offset);
        clear(completion_saved);
        clear(completion_live);
        clear(wind_offset);
        clear(wind_saved_word);
        clear(wind_next_word);
        clear(wind_control_saved);
        clear(wind_control_next);
        clear(capture_stage);
        clear(capture_exception);
        clear(capture_phase);
        clear(capture_mode_values_valid);
        clear(capture_mode_values);
        clear(source_mismatch_found);
        clear(source_record);
        clear(source_stack_mask);
        clear(source_size);
        clear(source_saved);
        clear(source_next);
        clear(source_writer_rva);
        clear(prediction_fixture_slot);
        clear(prediction_stage);
        clear(prediction_invoked);
        clear(prediction_captured);
        clear(prediction_state_changed);
        clear(prediction_rewound);
        clear(network_input_delivered);
        clear(confirmation_misses);
        clear(confirmation_retries);
        clear(enabled);
        clear(phase);
        clear(captured);
        clear(matched);
        clear(failure);
        clear(first_frame);
        clear(second_frame);
        clear(readiness_attempted);
        clear(readiness_prepared);
        clear(readiness_rejection);
        clear(replay_enabled);
        clear(replay_attempted);
        clear(restore_invoked);
        clear(restore_verified);
        clear(replay_invoked);
        clear(replay_verified);
        clear(restore_stage);
        clear(restore_failure_line);
        clear(restore_exception);
        clear(restore_flags);
        clear(wind_verified);
        clear(wind_capture_valid);
        clear(wind_failure_line);
        clear(wind_record);
        clear(wind_kind);
        clear(wind_size);
        clear(wind_vtable_rva);
        clear(camera_line);
        clear(camera_record);
        clear(camera_section);
        clear(camera_offset);
        clear(camera_a);
        clear(camera_b);
        clear(camera_c);
        clear(camera_live);
    }

};
inline NetworkCheckpointStatus network_checkpoint_status;
}
