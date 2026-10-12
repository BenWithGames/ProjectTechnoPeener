#pragma once
#include <array>
#include <cstdint>
namespace sc6::research {
struct NativeRestoreDiagnostic {
    enum Field : unsigned {stage,hop,depth,source_mode,next_mode,rejection,load_stage,exception,failure_line,invoked,preflight_mask,completion_line,completion_section,completion_record,completion_kind,completion_offset,source_guard_reason,source_guard_record,source_guard_length,source_guard_saved,source_guard_next,source_guard_writer,count};
    inline static constexpr const char* names[]={"stage","hop","depth","source_mode","next_mode","rejection","load_stage","exception","failure_line","invoked","preflight_mask","completion_line","completion_section","completion_record","completion_kind","completion_offset","source_guard_reason","source_guard_record","source_guard_length","source_guard_saved","source_guard_next","source_guard_writer"};
    std::array<uint64_t,count> values{};
};
}
