#pragma once
#include "TransformSnapshot.hpp"
namespace sc6::research {
// Reads captured bytes only. This manifest is NOT a lifetime token and must be
// resolved while the same capture is protected by the runtime memory gate.
template<unsigned Capacity=64> struct NativeEffectReferences {
    struct Reference {uintptr_t address{},vtable{};};
    std::array<Reference,Capacity> references{};
    unsigned count{};
    bool read(const TransformSnapshot& snapshot,uintptr_t module) noexcept {
        count=0;
        if(!module || module>UINTPTR_MAX-0x3E88D18 || !snapshot.valid || snapshot.count>snapshot.records.size())return false;
        unsigned manager_index=UINT32_MAX;
        for(unsigned i=0;i<snapshot.count;++i)if(snapshot.records[i].kind==3){
            if(manager_index!=UINT32_MAX)return false;manager_index=i;
        }
        if(manager_index==UINT32_MAX)return false;
        const auto& manager=snapshot.records[manager_index];
        if(manager.side!=2 || manager.list || manager.size!=16)return false;
        auto next=TransformSnapshot::pointer(manager.bytes.data());
        for(unsigned i=0;i<snapshot.count;++i){const auto& node=snapshot.records[i];
            if(node.kind!=4)continue;
            if(count==Capacity || node.parent!=manager_index || node.side!=2 || node.list || node.size!=24 || node.address!=next || !next)return false;
            for(unsigned j=0;j<count;++j)if(references[j].address==node.address)return false;
            const auto type=TransformSnapshot::pointer(node.bytes.data());
            unsigned kind=0,size=0;
            if(type==module+0x3E88CE8){kind=24;size=0x1C8;}
            if(type==module+0x3E88D18){kind=23;size=0x168;}
            const bool baseline=type==module+0x3E88C88 || type==module+0x3E88CB8;
            if(baseline){
                if(node.address>UINTPTR_MAX-0x130)return false;
                unsigned matches[3]{};
                constexpr unsigned offsets[]{0x40,0x70,0x120},sizes[]{32,112,12};
                for(unsigned j=0;j<snapshot.count;++j){const auto& payload=snapshot.records[j];
                    if(payload.parent==i && (payload.kind==23 || payload.kind==24))return false;
                    if(payload.parent!=i || payload.kind<7 || payload.kind>9)continue;
                    const auto field=payload.kind-7;
                    if(payload.address!=node.address+offsets[field] || payload.size!=sizes[field] || payload.side!=2 || payload.list)return false;
                    ++matches[field];
                }
                if(matches[0]!=1 || matches[1]!=1 || matches[2]!=1)return false;
                references[count++]={node.address,type};next=TransformSnapshot::pointer(node.bytes.data()+16);continue;
            }
            if(!kind || node.address>UINTPTR_MAX-(size+24))return false;
            unsigned children=0;
            for(unsigned j=0;j<snapshot.count;++j){const auto& payload=snapshot.records[j];
                if(payload.parent==i && payload.kind>=7 && payload.kind<=9)return false;
                if((payload.kind==23 || payload.kind==24) && payload.parent==i){
                    if(payload.kind!=kind || payload.size!=size || payload.side!=2 || payload.list || payload.address!=node.address+24)return false;
                    ++children;
                }
            }
            if(children!=1)return false;
            references[count++]={node.address,type};next=TransformSnapshot::pointer(node.bytes.data()+16);
        }
        if(next)return false;
        // Reject detached payloads as well as missing ones.
        for(unsigned i=0;i<snapshot.count;++i){const auto& payload=snapshot.records[i];
            if((payload.kind==23 || payload.kind==24) &&
               (payload.parent>=snapshot.count || snapshot.records[payload.parent].kind!=4))return false;
        }
        return true;
    }
};
}
