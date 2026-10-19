#pragma once
#include <cstdint>
#include <limits>
namespace sc6::research {
// RVA 3F0720: two 512-entry rings, 16-byte entries at clock+3C0.
// Unlike the native return value, Result distinguishes tagged neutral from
// absent input. Tags alone do not prove network confirmation; the admission
// path must also check the native validity flag. Neither case grants permission
// to advance the native clock.
struct NativeRawInputLookup {
    struct Entry { uint32_t identity{},frame{},raw{},reserved{}; };
    static_assert(sizeof(Entry)==16);
    enum class Result {tagged,missing,invalid,unstable};
    struct Query { uint32_t slot{},identity{},frame{},delay{}; bool delayed{}; };
    struct Observation { Result result{Result::invalid}; uint32_t raw{},effective_frame{}; uintptr_t offset{}; };
    template<class Read> static Observation inspect(const Query& q,Read read){
        Observation out;
        if(q.slot>=2 || q.identity>INT32_MAX)return out;
        // Native subtraction is uint32 followed by a signed-negative check.
        const uint32_t frame=q.frame-(q.delayed?q.delay:0u);
        if(frame>INT32_MAX)return out;
        out.effective_frame=frame;
        out.offset=0x3C0u+(static_cast<uintptr_t>(q.slot)*512u+(frame&511u))*sizeof(Entry);
        Entry a{},b{};
        if(!read(out.offset,a) || !read(out.offset,b))return out;
        if(a.identity!=b.identity || a.frame!=b.frame || a.raw!=b.raw || a.reserved!=b.reserved){
            out.result=Result::unstable;return out;
        }
        if(a.identity!=q.identity || a.frame!=frame){out.result=Result::missing;return out;}
        out.raw=a.raw;out.result=Result::tagged;return out;
    }
};
}


