#pragma once
#include <cstdint>
namespace sc6::research {
// Pinned Ghidra save/load audit: DWORD index at 0, QWORD offset at 8.
// Bytes 4..7 are unwritten stack padding, never consumed by these loaders.
// Comparison only: never alter archived payloads or relax active fields.
struct NativeReferencePadding {
    struct Site {uintptr_t writer,reader;};
    inline static constexpr Site sites[]={
        {0x30AB2F,0x30B1AE},
        {0x30AB84,0x30B1E2},
        {0x30ABDF,0x30B216},
        {0x30AC34,0x30B24A},
        {0x30AC8F,0x30B27E},
        {0x30ACE4,0x30B2B2},
        {0x30AD3F,0x30B2E6},
        {0x30AD94,0x30B31A},
        {0x30ADEF,0x30B34E},
        {0x31DCF1,0x31DEF1},
        {0x31DD48,0x31DF27},
        {0x31DDA1,0x31DF5D},
        {0x31DDF5,0x31DF94},
        {0x301A75,0x301D1C},
        {0x301AD2,0x301D53},
        {0x390765,0x390884},
        {0x3907B6,0x3908B7},
        {0x32AC76,0x32AD1B},
    };
    static bool audited(uintptr_t writer,uintptr_t reader,uintptr_t base){
        if(base>UINTPTR_MAX-0x400000)return false;
        for(const auto& site:sites)if(writer==base+site.writer && reader==base+site.reader)return true;
        return false;
    }
};
}
