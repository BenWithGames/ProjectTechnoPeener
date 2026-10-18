#pragma once
#include "TransformSnapshot.hpp"
namespace sc6::research {
// Ghidra 34F130: 25-word generator plus cursor. 34F3C0: six-word
// generator. Neither is the scalar RNG at 485EB28. Their callers include player decisions. Observe, never repair
// an unexplained mismatch by substituting a later endpoint.
struct EffectRngBoundary {
    struct State {std::array<unsigned,26> first{};std::array<unsigned,6> second{};
        bool operator==(const State&)const=default;};
    unsigned schema{1},check{},source_valid{},expected_valid{},entry_valid{},after_valid{};
    unsigned entry_match{},after_match{},exception{};
    State source{},expected{},entry{},after{};
    static bool archived(const TransformSnapshot& snapshot,uintptr_t base,State& out){
        if(!snapshot.valid || snapshot.count>snapshot.records.size())return false;
        unsigned seen=0;State value{};
        for(unsigned i=0;i<snapshot.count;++i){const auto& r=snapshot.records[i];
            if(r.side!=2 || (r.kind!=10 && r.kind!=11))continue;
            const unsigned bit=r.kind==10?1:2;
            const auto size=r.kind==10?104u:24u;
            if((seen&bit) || r.address!=base+(r.kind==10?0x485EB30:0x470E2B0) || r.size!=size)return false;
            std::memcpy(r.kind==10?value.first.data():value.second.data(),r.bytes.data(),size);seen|=bit;
        }
        if(seen!=3 || value.first[25]>25)return false;
        out=value;return true;
    }
    bool live(uintptr_t base,State& out) noexcept {
        if(!base || base>UINTPTR_MAX-0x485EB98)return false;
        __try {
            State value{};
            std::memcpy(value.first.data(),reinterpret_cast<const void*>(base+0x485EB30),104);
            std::memcpy(value.second.data(),reinterpret_cast<const void*>(base+0x470E2B0),24);
            if(value.first[25]>25)return false;
            out=value;return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){exception=GetExceptionCode();return false;}
    }
    bool before(const TransformSnapshot& a,const TransformSnapshot& b,uintptr_t base){
        check=1;source_valid=archived(a,base,source);expected_valid=archived(b,base,expected);
        if(!source_valid || !expected_valid)return false;
        check=2;entry_valid=live(base,entry);if(!entry_valid)return false;
        check=3;entry_match=source==entry;if(!entry_match)return false;
        check=0;return true;
    }
    void finish(uintptr_t base){after_valid=live(base,after);after_match=after_valid && expected_valid && expected==after;}
};
// Evaluate independent evidence even when the first comparison fails.
template<class Verify> bool verify_replay_effects(bool transforms_matched,Verify&& verify){
    const bool effects_matched=verify();return transforms_matched && effects_matched;
}
}
