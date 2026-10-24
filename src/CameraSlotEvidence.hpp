#pragma once
#include "CameraSlots.hpp"
namespace sc6::research {
struct CameraSlotEvidence {
    static constexpr unsigned words=256; // 1024 bytes, beyond largest audited slot payload (648 bytes).
    unsigned selected{UINT32_MAX};
    std::array<std::array<uint64_t,6>,2> header{}; // enabled,valid,failure,count,manager,table
    std::array<std::array<std::array<uint64_t,7>,16>,2> slots{}; // type,first,end,offset,bytes,pool,active
    std::array<std::array<uint32_t,words>,2> payload{};
    std::array<unsigned,2> retained{};
    std::array<std::array<uint64_t,5>,2> reference_header{}; // valid schema, owner, arena, P1, P2
    std::array<std::array<std::array<uint64_t,6>,17>,2> references{}; // record,target,index,offset,resource,owned
    void capture(const CameraSlots& a,const FullRestoreStream& sa,const CameraSlots& b,const FullRestoreStream& sb){
        *this={};selected=UINT32_MAX;
        for(unsigned i=0;i<(std::min)(16u,(std::max)(a.count,b.count));++i)
            if(i>=a.count || i>=b.count || a.slots[i].type!=b.slots[i].type || a.slots[i].bytes!=b.slots[i].bytes){selected=i;break;}
        if(selected==UINT32_MAX && (!a.valid || !b.valid)){
            for(unsigned i=0;i<(std::min)(16u,(std::max)(a.count,b.count));++i)
                if((i<a.count && a.slots[i].type!=UINT32_MAX && a.slots[i].type>8) || (i<b.count && b.slots[i].type!=UINT32_MAX && b.slots[i].type>8)){selected=i;break;}
            if(selected==UINT32_MAX && (a.count || b.count))selected=0;
        }
        const CameraSlots* samples[]={&a,&b};const FullRestoreStream* streams[]={&sa,&sb};
        for(unsigned sample=0;sample<2;++sample){const auto& c=*samples[sample];const auto& s=*streams[sample];
            header[sample]={c.enabled,c.valid,c.failure,c.count,c.manager,c.table};
            std::array<unsigned,17> records;std::array<uint64_t,17> refs;std::array<CameraSlots::Reference,17> evidence{};unsigned end{};
            const bool parsed=c.enabled && CameraSlots::reference_tail(s,c.module,c.tail,records,refs,end,&evidence);
            reference_header[sample]={parsed,c.reference_owner,c.reference_arena,c.reference_players[0],c.reference_players[1]};
            for(unsigned k=0;k<17;++k){const auto& r=evidence[k];references[sample][k]={r.record,r.target,r.index,r.offset,r.resource,c.owns_reference(k,r)};}
            for(unsigned i=0;i<c.count && i<16;++i){const auto& x=c.slots[i];slots[sample][i]={x.type,x.first,x.end,x.offset,x.bytes,x.pool,x.active};}
            if(selected<c.count && selected<16){const auto& x=c.slots[selected];
                if(x.offset<=s.stream.write_position && x.bytes<=s.stream.write_position-x.offset && s.stream.write_position<=s.stream.payload.size()){
                    retained[sample]=(std::min)(x.bytes,words*4u);std::memcpy(payload[sample].data(),s.stream.payload.data()+x.offset,retained[sample]);}}
        }
    }
};
}
