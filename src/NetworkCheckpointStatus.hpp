#pragma once
#include <atomic>
#include <array>
namespace sc6 {
struct NetworkCheckpointStatus {
    std::array<std::atomic<unsigned>,2> routing_valid{},dispatch_stage{};
    std::array<std::array<std::atomic<unsigned>,18>,2> routing_values{};
    std::array<std::array<std::atomic<unsigned>,4>,3> confirmation_attempts{};
    std::atomic<unsigned> comparison_stage{},comparison_flags{};
    std::array<std::atomic<unsigned>,8> comparison_scalars{};
    std::atomic<unsigned long long> comparison_bytes{};
    std::atomic<unsigned> battle_gate{},battle_vtable{},battle_handler{};
    std::atomic<unsigned> completion_line{},completion_section{},completion_record{},completion_kind{},completion_offset{},completion_saved{},completion_live{};
    std::atomic<unsigned> wind_offset{},wind_saved_word{},wind_next_word{},wind_control_saved{},wind_control_next{};
    std::atomic<unsigned> capture_stage{},capture_exception{},capture_phase{},capture_mode_values_valid{};
    std::array<std::atomic<unsigned>,6> capture_mode_values{};
    std::atomic<unsigned> source_mismatch_found{},source_record{},source_stack_mask{};
    std::atomic<unsigned long long> source_size{},source_saved{},source_next{},source_writer_rva{};
    std::atomic<unsigned> prediction_stage{},prediction_invoked{},prediction_captured{},prediction_state_changed{},prediction_rewound{};
    std::atomic<unsigned> network_input_delivered{},confirmation_misses{},confirmation_retries{};
    std::atomic<unsigned> enabled{},phase{},captured{},matched{},failure{},first_frame{},second_frame{};
    std::atomic<unsigned> readiness_attempted{},readiness_prepared{},readiness_rejection{};
    std::atomic<unsigned> replay_enabled{},replay_attempted{},restore_invoked{},restore_verified{},replay_invoked{},replay_verified{};
    std::atomic<unsigned> restore_stage{},restore_failure_line{},restore_exception{},restore_flags{};
    std::atomic<unsigned> wind_verified{},wind_capture_valid{},wind_failure_line{},wind_record{},wind_kind{},wind_size{};
    std::atomic<unsigned long long> wind_vtable_rva{};
    std::atomic<unsigned> camera_line{},camera_record{},camera_section{},camera_offset{},camera_a{},camera_b{},camera_c{},camera_live{};
};
inline NetworkCheckpointStatus network_checkpoint_status;
}
