#pragma once
#include <cstdint>
#include <span>
#include <limits>
namespace sc6::research {
// Pure preflight only. Callers must supply actual native range lists and prove
// backing allocations remain owned; equal addresses do not establish lifetime.
struct NativeRangeCompatibility {
    struct Range { uint64_t base{},size{},generation{}; };
    struct Reference { uint32_t index{}; uint64_t offset{}; };
    template<class Retained,class Absolute> static bool check(
        std::span<const Range> saved,std::span<const Range> current,
        std::span<const Reference> references,Retained&& retained,Absolute&& absolute){
        if(saved.empty() || saved.size()!=current.size() ||
           saved[0].base || saved[0].size || current[0].base || current[0].size)return false;
        for(std::size_t i=1;i<saved.size();++i){
            const auto& prior=saved[i];const auto& live=current[i];
            if(!prior.base || !prior.size || !prior.generation || prior.base!=live.base || prior.size!=live.size ||
               prior.generation!=live.generation || prior.size>(std::numeric_limits<uint64_t>::max)()-prior.base ||
               !retained(prior))return false;
        }
        for(const auto& ref:references){
            if(ref.index>=saved.size())return false;
            if(!ref.index){if(!absolute(ref.offset))return false;}
            // Native encoding admits an endpoint pointer. Dereference validity
            // is a separate caller obligation; this only checks reconstruction.
            else if(ref.offset>saved[ref.index].size)return false;
        }
        return true;
    }
};
}

