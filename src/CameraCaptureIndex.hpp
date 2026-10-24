#pragma once
#include "FullRestoreStream.hpp"
namespace sc6::research {
// Per-capture index of the six audited base-camera serializer call sites.
// Stores record indices only; no game pointers are cached across snapshots.
struct CameraCaptureIndex {
    inline static constexpr unsigned sites[]={0x33FC68,0x33FC7B,0x33FC8E,0x33FE69,0x33FE7F,0x33FE95};
    static constexpr uint16_t end=UINT16_MAX;
    static_assert(FullRestoreStream::transfer_limit<end);
    std::array<uint16_t,6> heads;
    std::array<uint16_t,FullRestoreStream::transfer_limit> next;
    bool build(const FullRestoreStream& stream,uintptr_t base) noexcept {
        heads.fill(end);
        if(!stream.sealed || stream.writes>FullRestoreStream::transfer_limit)return false;
        for(unsigned i=0;i<stream.writes;++i){
            for(unsigned k=0;k<6;++k)if(stream.traces[i].writer==base+sites[k]){
                next[i]=heads[k];heads[k]=static_cast<uint16_t>(i);break;
            }
        }
        return true;
    }
    unsigned matches(const FullRestoreStream& stream,unsigned site,uintptr_t source,uint64_t length)const noexcept {
        unsigned count=0;
        for(auto i=heads[site];i!=end;i=next[i])
            if(stream.traces[i].source==source && stream.lengths[i]==length)++count;
        return count;
    }
};
}
