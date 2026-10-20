#pragma once
#include <array>
#include <cstdint>
namespace sc6::research {
struct NativeRestoreDiagnostic {
    enum Field : unsigned {stage,hop,depth,source_mode,next_mode,rejection,load_stage,exception,failure_line,invoked,preflight_mask,completion_line,completion_section,completion_record,completion_kind,completion_offset,source_guard_reason,source_guard_record,source_guard_length,source_guard_saved,source_guard_next,source_guard_writer,timer_phase,timer_reason,timer_side,timer_slot,timer_offset,timer_expected,timer_actual,timer_source_mask,timer_next_mask,timer_live_mask,payload_structural,payload_records,payload_bytes,payload_retained,payload_overflow,payload_deferred_bytes,route_reason,route_record,route_expected,route_actual,camera_reason,camera_record,camera_section,camera_offset,camera_side,camera_saved,camera_next,count};
    inline static constexpr const char* names[]={"stage","hop","depth","source_mode","next_mode","rejection","load_stage","exception","failure_line","invoked","preflight_mask","completion_line","completion_section","completion_record","completion_kind","completion_offset","source_guard_reason","source_guard_record","source_guard_length","source_guard_saved","source_guard_next","source_guard_writer","timer_phase","timer_reason","timer_side","timer_slot","timer_offset","timer_expected","timer_actual","timer_source_mask","timer_next_mask","timer_live_mask","payload_structural","payload_records","payload_bytes","payload_retained","payload_overflow","payload_deferred_bytes","route_reason","route_record","route_expected","route_actual","camera_reason","camera_record","camera_section","camera_offset","camera_side","camera_saved","camera_next"};
    std::array<uint64_t,count> values{};
    std::array<uint64_t,22> topology{};
    std::array<std::array<uint64_t,9>,16> payload_details{};
    static constexpr unsigned timer_word_count=3*2*3*32;
    // source/next/live, then side, slot, little-endian uint32 word offset.
    bool timer_evidence_valid{};
    std::array<uint32_t,timer_word_count> timer_words{};
};
}
