#pragma once
#include <cstdint>
namespace sc6::research {
// Pure model of RVA 3FCD10, after native suppression callbacks have resolved
// their booleans. Does not poll, publish, confirm, or mutate the native ring.
struct NativeInputWords {
    uint32_t held{},pressed{};
    static NativeInputWords compose(uint32_t previous_raw,uint32_t current_raw,
        uint32_t disabled_mask,bool suppress_edges,bool suppress_held,bool restrict_buttons){
        const uint32_t mask=~disabled_mask & (restrict_buttons?0xFFFFC3F0u:0xFFFFFFFFu);
        return {(suppress_held?0u:current_raw)&mask,
                (suppress_edges?0u:((previous_raw^current_raw)&current_raw))&mask};
    }
};
}
