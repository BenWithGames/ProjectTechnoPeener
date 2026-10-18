#pragma once
#include <Windows.h>
#include <cstdint>
namespace sc6::research::selector_lifetime {
// The native initializer/free own this allocation (38EFA0 / 38F110).
// Readers never retain or overwrite its pointers. Epochs also reject ABA reuse.
inline SRWLOCK lock=SRWLOCK_INIT;
inline uint64_t epoch{};
inline bool installed{};
inline uintptr_t module{};
}
