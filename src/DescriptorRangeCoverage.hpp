#pragma once
#include <cstdint>
namespace sc6::research {
// Address classification only, not lifetime or mutability proof. In particular,
// a native registered extent may be a relocation layout, not allocated bytes.
inline unsigned descriptor_range_coverage(uint64_t base,uint64_t size,uint64_t payload,uint64_t bytes){
    if(!base || !size || !payload || !bytes || size>UINT64_MAX-base || bytes>UINT64_MAX-payload)return 0;
    if(base<payload || base-payload>=bytes)return 0;
    return size<=bytes-(base-payload)?1u:2u;
}
}
