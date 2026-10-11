#pragma once
#include <Windows.h>
#include <array>
#include <cstring>
#include "NativeRangeCompatibility.hpp"
namespace sc6::research {
// Copy only the native {begin,end,capacity} vector and its 16-byte entries.
// Must run while its owner guarantees stability. Does not read range payloads,
// infer ownership, hook the serializer, or authorize native loading.
template<unsigned Capacity=512> struct NativeRangeImage {
    struct Header { uintptr_t begin{},end{},capacity{}; };
    struct Entry { uint64_t base{},size{}; };
    std::array<Entry,Capacity> entries{};
    unsigned count{},exception{};bool valid{};
    bool capture(uintptr_t address) noexcept {
        valid=false;count=exception=0;
        if(!address || address>UINTPTR_MAX-sizeof(Header))return false;
        __try {
            Header first{},last{};std::memcpy(&first,reinterpret_cast<void*>(address),sizeof(first));
            if(!first.begin || first.begin%8 || first.begin>first.end || first.end>first.capacity ||
               (first.end-first.begin)%sizeof(Entry) || (first.capacity-first.begin)%sizeof(Entry))return false;
            const auto length=(first.end-first.begin)/sizeof(Entry);
            if(!length || length>Capacity)return false;
            std::memcpy(entries.data(),reinterpret_cast<void*>(first.begin),static_cast<size_t>(length)*sizeof(Entry));
            if(entries[0].base || entries[0].size)return false;
            for(size_t i=1;i<length;++i){const auto& entry=entries[i];
                if(!entry.base || !entry.size || entry.size>UINT64_MAX-entry.base)return false;
            }
            if(std::memcmp(entries.data(),reinterpret_cast<void*>(first.begin),static_cast<size_t>(length)*sizeof(Entry)))return false;
            std::memcpy(&last,reinterpret_cast<void*>(address),sizeof(last));
            if(std::memcmp(&first,&last,sizeof(first)))return false;
            count=static_cast<unsigned>(length);valid=true;return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){exception=GetExceptionCode();return false;}
    }
    // Caller resolves real ownership generations; never manufacture them from
    // addresses or assume capture stability proves allocation lifetime.
    template<class Ownership> bool bind(std::array<NativeRangeCompatibility::Range,Capacity>& output,Ownership&& ownership)const {
        output={};if(!valid || !count || count>Capacity)return false;
        for(unsigned i=1;i<count;++i){const auto& entry=entries[i];
            const auto generation=ownership(entry.base,entry.size);
            if(!generation){output={};return false;}
            output[i]={entry.base,entry.size,generation};
        }
        return true;
    }
};
}
