#pragma once
#include <Windows.h>
#include <cstdint>
namespace sc6::research {
// Read only while the caller owns a strong resource reference. Hashes are
// diagnostics, not a proof of immutability or a substitute for snapshot data.
struct ResourcePayloadFingerprint {
    uintptr_t payload{};
    int32_t size{};
    uint64_t hash{};
    bool valid{};
    static ResourcePayloadFingerprint read(uintptr_t object,uintptr_t module){
        ResourcePayloadFingerprint value{};
        __try {
            if(*reinterpret_cast<uintptr_t*>(object)!=module+0x3CEA0F8 ||
               *reinterpret_cast<unsigned char*>(object+0x50)!=1)return value;
            value.payload=*reinterpret_cast<uintptr_t*>(object+0x38);
            value.size=*reinterpret_cast<int32_t*>(object+0x48);
            if(!value.payload || value.size<=0 || value.size>1024*1024)return value;
            value.hash=14695981039346656037ull;
            const auto bytes=reinterpret_cast<const volatile unsigned char*>(value.payload);
            for(int32_t i=0;i<value.size;++i){value.hash^=bytes[i];value.hash*=1099511628211ull;}
            value.valid=true;
        } __except(EXCEPTION_EXECUTE_HANDLER){value.valid=false;}
        return value;
    }
    bool matches(const ResourcePayloadFingerprint& other)const{
        return valid && other.valid && payload==other.payload && size==other.size && hash==other.hash;
    }
};
}
