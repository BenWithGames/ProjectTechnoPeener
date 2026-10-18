#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
namespace sc6::research {
inline uint64_t exact_byte_differences(const std::byte* a,const std::byte* b,uint64_t n){
    uint64_t count=0;
    for(uint64_t at=0;at<n;){const auto size=(std::min)(uint64_t{256},n-at);
        if(std::memcmp(a+at,b+at,static_cast<size_t>(size)))
            for(uint64_t j=0;j<size;++j)count+=a[at+j]!=b[at+j];
        at+=size;
    }return count;
}
}
