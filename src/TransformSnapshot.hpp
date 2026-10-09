#pragma once
#include <array>
#include <cstdint>
#include <cstring>
namespace sc6::research {
struct TransformSnapshot {
    struct Record {
        uintptr_t address{};
        unsigned side{},kind{},list{},parent{},size{};
        std::array<std::byte,512> bytes{};
    };
    static constexpr unsigned group_limit=64,node_limit=512,global_node_limit=128;
    // A bounded diagnostic budget; excessive constraint records fail capture.
    std::array<Record,2*(1+group_limit+3*node_limit)+5+4*global_node_limit+2048> records{};
    unsigned count{},failure{},trapped_exception{}; uintptr_t failure_address{};
    bool reject(unsigned reason,uintptr_t address){failure=reason;failure_address=address;return false;}
    bool valid{};
    static uintptr_t pointer(const std::byte* p){uintptr_t v{};std::memcpy(&v,p,8);return v;}
    bool append(uintptr_t address,unsigned side,unsigned kind,unsigned list,unsigned parent,unsigned size){
        failure_address=address;
        if(address<0x10000 || address>UINTPTR_MAX-size)return reject(1,address);
        if(count>=records.size())return reject(2,address);
        auto& r=records[count];r.address=address;r.side=side;r.kind=kind;r.list=list;r.parent=parent;r.size=size;
        std::memcpy(r.bytes.data(),reinterpret_cast<void*>(address),size);++count;return true;
    }
    bool capture(const uintptr_t* owners,uintptr_t module_base=0) noexcept {
        valid=false;count=0;failure=0;trapped_exception=0;failure_address=0;
        __try {
            for(unsigned side=0;side<2;++side){
                auto root=count;
                if(!append(owners[side],side,0,0,0,512))return false;
                unsigned groups=0,nodes=0;
                for(unsigned list=0;list<2;++list){
                    auto group=pointer(records[root].bytes.data()+0x10+list*0x10);
                    auto first_group=count;
                    while(group){
                        for(unsigned n=first_group;n<count;++n)if(records[n].kind==1 && records[n].address==group)return reject(3,group);
                        if(++groups>group_limit)return reject(4,group);
                        auto g=count;
                        if(!append(group,side,1,list,root,0x58))return false;
                        auto node=pointer(records[g].bytes.data()+0x48);auto first_node=count;
                        while(node){
                            for(unsigned n=first_node;n<count;++n)if(records[n].kind==2 && records[n].address==node)return reject(5,node);
                            if(++nodes>node_limit)return reject(6,node);
                            auto n=count;
                            if(!append(node,side,2,list,g,0x80))return false;
                            auto vtable=pointer(records[n].bytes.data());
                            // Exact known node types; only proven fields of bounded constraints.
                            if(module_base && (vtable==module_base+0x3E88DB8 || vtable==module_base+0x3E88DF8)){
                                if(node>UINTPTR_MAX-0x2FC)return reject(1,node);
                                if(!append(node+0x80,side,12,list,n,0x60))return false;
                                auto payload=count-1;
                                if(vtable==module_base+0x3E88DF8 && !append(node+0x2F0,side,13,list,n,12))return false;
                                std::int16_t links{},constraints{};
                                std::memcpy(&links,records[payload].bytes.data()+0x58,2);
                                std::memcpy(&constraints,records[payload].bytes.data()+0x5A,2);
                                if(links<0 || links>4 || constraints<0 || constraints>4)return reject(12,node+0xD8);
                                for(unsigned j=0;j<static_cast<unsigned>(links);++j)
                                    if(!append(node+0xE0+j*0x10,side,14,list,n,12))return false;
                                for(unsigned j=0;j<static_cast<unsigned>(constraints);++j){
                                    auto entry=node+0x120+j*0x60;
                                    if(!append(entry,side,15,list,n,16))return false;
                                    auto link=count;
                                    if(!append(entry+0x50,side,16,list,n,8))return false;
                                    auto target=pointer(records[link].bytes.data());
                                    if(target<0x10000 || target>UINTPTR_MAX-0xC0)return reject(1,target);
                                    if(!append(target,side,17,list,link,8) ||
                                       !append(target+0x80,side,18,list,link,64))return false;
                                    // KSwayHitNode builds both transforms from these inputs.
                                    if(pointer(records[link+1].bytes.data())==module_base+0x3E88E38){
                                        if(target>UINTPTR_MAX-0xD0)return reject(1,target);
                                        if(!append(target+0x40,side,19,list,link,64) ||
                                           !append(target+0x10,side,20,list,link,32) ||
                                           !append(target+0xC0,side,21,list,link,16))return false;
                                    }
                                    // The group solver also reads/writes one vector per D8 link.
                                    // At most four vectors fit before the reference at entry+50.
                                    if(links && !append(entry+0x10,side,22,list,n,static_cast<unsigned>(links)*16))return false;
                                }
                            }
                            node=pointer(records[n].bytes.data()+0x28);
                        }
                        group=pointer(records[g].bytes.data()+0x40);
                    }
                }
            }
            if(module_base){
                auto global_record=count;
                if(!append(module_base+0x470E038,2,5,0,0,8) || !append(module_base+0x4846450,2,6,0,0,4))return false;
                auto manager=pointer(records[global_record].bytes.data());
                if(!append(module_base+0x485EB30,2,10,0,0,104) || !append(module_base+0x470E2B0,2,11,0,0,24))return false;
                if(manager){
                    auto m=count;
                    if(!append(manager,2,3,0,global_record,16))return false;
                    auto node=pointer(records[m].bytes.data());unsigned nodes=0;
                    auto first=count;
                    while(node){
                        for(unsigned i=first;i<count;++i)if(records[i].address==node)return reject(10,node);
                        if(++nodes>global_node_limit)return reject(11,node);
                        auto n=count;
                        // Only the proven header: vtable, intervening field, next link.
                        if(!append(node,2,4,0,m,24))return false;
                        auto vtable=pointer(records[n].bytes.data());
                        // Observation only: ShockWave methods3322C0/332B60 use
                        // fields through17F. Do not treat it as a validated wind restorer.
                        if(vtable==module_base+0x3E88D18){
                            if(!append(node+0x18,2,23,0,n,0x168))return false;
                        }
                        if(vtable==module_base+0x3E88CB8 || vtable==module_base+0x3E88C88){
                            if(!append(node+0x40,2,7,0,n,32) || !append(node+0x70,2,8,0,n,112) ||
                               !append(node+0x120,2,9,0,n,12))return false;
                        }
                        node=pointer(records[n].bytes.data()+16);
                    }
                }
            }
            for(unsigned n=0;n<count;++n){auto& r=records[n];
                failure_address=r.address;
                if(std::memcmp(r.bytes.data(),reinterpret_cast<void*>(r.address),r.size))return reject(7,r.address);
            }
            valid=true;failure_address=0;return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){trapped_exception=GetExceptionCode();return reject(8,failure_address);}
    }
    bool same_topology(const TransformSnapshot& other)const{
        if(!valid || !other.valid || count!=other.count)return false;
        for(unsigned i=0;i<count;++i){auto& a=records[i];auto& b=other.records[i];
            if(a.address!=b.address || a.side!=b.side || a.kind!=b.kind || a.list!=b.list || a.parent!=b.parent || a.size!=b.size)return false;
        }
        return true;
    }
    // Combat group matrices retain the previous update. Validate every group
    // before touching any matrix; linked-list pointers are never restored here.
    bool combat_groups_ready(const TransformSnapshot& next,const TransformSnapshot& current)const noexcept {
        __try {
            if(!count || count>records.size() || !same_topology(next) || !same_topology(current))return false;
            unsigned groups=0;
            for(unsigned i=0;i<count;++i){auto& a=records[i];auto& b=next.records[i];auto& c=current.records[i];
                if(a.kind!=1)continue;
                if(a.size!=88 || a.side>1 || a.list>1 || a.parent>=count ||
                   records[a.parent].kind!=0 || records[a.parent].side!=a.side ||
                   a.address<0x10000 || a.address>UINTPTR_MAX-88)return false;
                for(unsigned j=0;j<i;++j)if(records[j].kind==1 &&
                    a.address<records[j].address+88 && records[j].address<a.address+88)return false;
                if(std::memcmp(a.bytes.data()+64,b.bytes.data()+64,24) ||
                   std::memcmp(a.bytes.data()+64,c.bytes.data()+64,24) ||
                   std::memcmp(c.bytes.data(),reinterpret_cast<void*>(a.address),88))return false;
                for(unsigned k=0;k<64;k+=4){
                    std::uint32_t x{},y{},z{};
                    std::memcpy(&x,a.bytes.data()+k,4);std::memcpy(&y,b.bytes.data()+k,4);std::memcpy(&z,c.bytes.data()+k,4);
                    if((x&0x7F800000u)==0x7F800000u || (y&0x7F800000u)==0x7F800000u ||
                       (z&0x7F800000u)==0x7F800000u)return false;
                }
                ++groups;
            }
            return groups!=0;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool restore_combat_groups(const TransformSnapshot& next,const TransformSnapshot& current)const noexcept {
        __try {
            if(!combat_groups_ready(next,current))return false;
            for(unsigned i=0;i<count;++i){auto& a=records[i];
                if(a.kind==1)std::memcpy(reinterpret_cast<void*>(a.address),a.bytes.data(),64);
            }
            for(unsigned i=0;i<count;++i){auto& a=records[i];
                if(a.kind==1 && std::memcmp(reinterpret_cast<void*>(a.address),a.bytes.data(),88))return false;
            }
            return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    // Narrow observed fields, confirmed written by332400/332B60. Unknown
    // changing fields reject; headers, pointers and configuration are retained.
    static bool shockwave_byte(unsigned o){
        return (o>=0x30 && o<0x34) || (o>=0x40 && o<0x68) ||
            // 332400 advances five phase floats (90 via 331180) and writes
            // direction outputs120/124; neighboring configuration stays exact.
            (o>=0x70 && o<0xB4 && (o&0xF)<4) ||
            (o>=0xD8 && o<0xDC) || (o>=0xF0 && o<0xFC) ||
            (o>=0x120 && o<0x12C) || (o>=0x160 && o<0x16C) || (o>=0x170 && o<0x17C);
    }
    bool restore_root_wind(const TransformSnapshot& next,const TransformSnapshot& current,bool validate_only=false)const noexcept {
        __try {
            if(!count || count>records.size() || !same_topology(next) || !same_topology(current))return false;
            unsigned sides=0;
            for(unsigned i=0;i<count;++i){auto& a=records[i];auto& b=next.records[i];auto& c=current.records[i];
                if(a.kind!=0)continue;
                if(a.side>1 || a.size!=512 || (sides&(1u<<a.side)) || a.address<0x10000 || a.address>UINTPTR_MAX-512)return false;
                sides|=1u<<a.side;
                if(std::memcmp(c.bytes.data(),reinterpret_cast<void*>(a.address),512) ||
                   std::memcmp(b.bytes.data(),c.bytes.data(),512) ||
                   std::memcmp(a.bytes.data(),b.bytes.data(),0x1F0))return false;
                for(unsigned k=0x1F0;k<0x200;k+=4){
                    unsigned x{},y{};std::memcpy(&x,a.bytes.data()+k,4);std::memcpy(&y,b.bytes.data()+k,4);
                    if((x&0x7F800000u)==0x7F800000u || (y&0x7F800000u)==0x7F800000u)return false;
                }
            }
            if(sides!=3)return false;
            if(validate_only)return true;
            for(unsigned i=0;i<count;++i){auto& a=records[i];if(a.kind==0)
                std::memcpy(reinterpret_cast<void*>(a.address+0x1F0),a.bytes.data()+0x1F0,16);
            }
            for(unsigned i=0;i<count;++i){auto& a=records[i];if(a.kind==0 &&
                std::memcmp(reinterpret_cast<void*>(a.address),a.bytes.data(),512))return false;
            }
            return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool wind_field(unsigned kind){return (kind>=7 && kind<=11) || kind==23;}
    bool restore_constraints(const TransformSnapshot& next,const TransformSnapshot& current,uintptr_t module_base,bool validate_only=false)const noexcept {
        __try {
            if(!same_topology(next) || !same_topology(current))return false;
            for(unsigned i=0;i<count;++i){auto& a=records[i];auto& b=next.records[i];auto& c=current.records[i];
                if(a.kind!=2 && a.kind!=12 && a.kind!=22 && (a.kind<14 || a.kind>17))continue;
                if(a.side>1 || std::memcmp(c.bytes.data(),reinterpret_cast<void*>(a.address),a.size))return false;
                if(a.kind==12){
                    if(a.size!=0x60 || std::memcmp(a.bytes.data()+0x58,b.bytes.data()+0x58,4) ||
                       std::memcmp(a.bytes.data()+0x58,c.bytes.data()+0x58,4))return false;
                    continue;
                }
                if(std::memcmp(b.bytes.data(),c.bytes.data(),a.size))return false;
                if(a.kind!=15 && a.kind!=22){if(std::memcmp(a.bytes.data(),b.bytes.data(),a.size))return false;continue;}
                if(a.parent>=count || i+1>=count)return false;
                auto& node=records[a.parent];auto vt=pointer(node.bytes.data());
                if(node.kind!=2 || node.side!=a.side || (vt!=module_base+0x3E88DB8 && vt!=module_base+0x3E88DF8))return false;
                std::int16_t entries{};std::memcpy(&entries,reinterpret_cast<void*>(node.address+0xDA),2);
                auto field_offset=a.kind==15?0x120u:0x130u;
                std::int16_t links{};std::memcpy(&links,reinterpret_cast<void*>(node.address+0xD8),2);
                if(entries<0 || entries>4 || links<0 || links>4 || a.address<node.address+field_offset)return false;
                if(a.kind==15?a.size!=16:(links==0 || a.size!=static_cast<unsigned>(links)*16))return false;
                auto offset=a.address-node.address-field_offset;
                if(offset%0x60 || offset/0x60>=static_cast<unsigned>(entries))return false;
                unsigned vector_index=i;
                if(a.kind==22){
                    vector_index=count;
                    for(unsigned j=0;j<i;++j)if(records[j].kind==15 && records[j].parent==a.parent && records[j].address==a.address-0x10){vector_index=j;break;}
                    if(vector_index>=count-1)return false;
                }
                auto& link=records[vector_index+1];
                if(link.kind!=16 || link.parent!=a.parent || link.address!=node.address+0x170+offset || link.size!=8)return false;
            }
            if(validate_only)return true;
            for(unsigned i=0;i<count;++i){auto& a=records[i];
                if(a.kind==15 || a.kind==22)std::memcpy(reinterpret_cast<void*>(a.address),a.bytes.data(),a.size);
            }
            for(unsigned i=0;i<count;++i){auto& a=records[i];
                if((a.kind==15 || a.kind==22) && std::memcmp(reinterpret_cast<void*>(a.address),a.bytes.data(),a.size))return false;
            }
            return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool restore_sway(const TransformSnapshot& next,const TransformSnapshot& current,uintptr_t module_base,bool validate_only=false)const noexcept {
        __try {
            if(!same_topology(next) || !same_topology(current))return false;
            // Validate every node before the first write; retain all unmeasured fields.
            for(unsigned i=0;i<count;++i){auto& a=records[i];auto& b=next.records[i];auto& c=current.records[i];
                if(a.kind!=2 && a.kind!=12 && a.kind!=13)continue;
                if(a.side>1 || std::memcmp(b.bytes.data(),c.bytes.data(),a.size) ||
                   std::memcmp(c.bytes.data(),reinterpret_cast<void*>(a.address),a.size))return false;
                if(a.kind==12 || a.kind==13){
                    if(a.parent>=count)return false;
                    auto& parent=records[a.parent];auto vt=pointer(parent.bytes.data());
                    if(parent.kind!=2 || parent.side!=a.side ||
                       (vt!=module_base+0x3E88DB8 && vt!=module_base+0x3E88DF8))return false;
                    if(a.kind==12){
                        if(a.address!=parent.address+0x80 || a.size!=0x60)return false;
                        for(unsigned j=0x40;j<a.size;++j)
                            if((j<0x4C || j>=0x50) && a.bytes[j]!=b.bytes[j])return false;
                    }else if(vt!=module_base+0x3E88DF8 || a.address!=parent.address+0x2F0 || a.size!=12)return false;
                }
                if(a.kind!=12 && std::memcmp(a.bytes.data(),b.bytes.data(),a.size))return false;
            }
            if(validate_only)return true;
            for(unsigned i=0;i<count;++i){auto& a=records[i];if(a.kind!=12)continue;
                std::memcpy(reinterpret_cast<void*>(a.address),a.bytes.data(),0x40);
                std::memcpy(reinterpret_cast<void*>(a.address+0x4C),a.bytes.data()+0x4C,4);
            }
            for(unsigned i=0;i<count;++i){auto& a=records[i];
                if(a.kind==12 && std::memcmp(reinterpret_cast<void*>(a.address),a.bytes.data(),a.size))return false;
            }
            return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool restore_wind(const TransformSnapshot& next,const TransformSnapshot& current,uintptr_t module_base,bool validate_only=false)const noexcept {
        __try {
            if(!same_topology(next) || !same_topology(current))return false;
            unsigned rng_records=0;
            // Validate all shared records before writing any wind or RNG data.
            for(unsigned i=0;i<count;++i){auto& a=records[i];auto& b=next.records[i];auto& c=current.records[i];
                if(a.side!=2)continue;
                if(std::memcmp(b.bytes.data(),c.bytes.data(),a.size) ||
                   std::memcmp(c.bytes.data(),reinterpret_cast<void*>(a.address),a.size))return false;
                if(!wind_field(a.kind) && std::memcmp(a.bytes.data(),b.bytes.data(),a.size))return false;
                if(a.kind==4){auto vt=pointer(a.bytes.data());
                    if(vt!=module_base+0x3E88CB8 && vt!=module_base+0x3E88C88 && vt!=module_base+0x3E88D18)return false;
                    if(vt==module_base+0x3E88D18){
                        unsigned children=0;
                        for(unsigned j=0;j<count;++j)if(records[j].kind==23 && records[j].parent==i)++children;
                        if(children!=1)return false;
                    }
                }
                if(a.kind>=7 && a.kind<=9){
                    if(a.parent>=count || records[a.parent].kind!=4)return false;
                    auto offset=a.kind==7?0x40u:a.kind==8?0x70u:0x120u;
                    auto length=a.kind==7?32u:a.kind==8?112u:12u;
                    if(a.address!=records[a.parent].address+offset || a.size!=length)return false;
                }
                if(a.kind==23){
                    if(a.size!=0x168 || a.parent>=count)return false;
                    auto& parent=records[a.parent];
                    if(parent.kind!=4 || parent.side!=2 || pointer(parent.bytes.data())!=module_base+0x3E88D18 ||
                       a.address!=parent.address+0x18)return false;
                    for(unsigned j=0;j<a.size;++j)
                        if(!shockwave_byte(j+0x18) && a.bytes[j]!=b.bytes[j])return false;
                    for(unsigned j=0;j<a.size;j+=4)if(shockwave_byte(j+0x18) && j+0x18!=0x60){
                        std::uint32_t x{},y{};std::memcpy(&x,a.bytes.data()+j,4);std::memcpy(&y,b.bytes.data()+j,4);
                        if((x&0x7F800000u)==0x7F800000u || (y&0x7F800000u)==0x7F800000u)return false;
                    }
                    unsigned old{},next_index{};std::memcpy(&old,a.bytes.data()+0x48,4);std::memcpy(&next_index,b.bytes.data()+0x48,4);
                    if(old==UINT32_MAX || next_index!=old+1)return false;
                }
                if(a.kind==10){
                    if(a.address!=module_base+0x485EB30 || a.size!=104)return false;
                    unsigned index{};std::memcpy(&index,a.bytes.data()+100,4);if(index>25)return false;
                    std::memcpy(&index,b.bytes.data()+100,4);if(index>25)return false;++rng_records;
                }
                if(a.kind==11){if(a.address!=module_base+0x470E2B0 || a.size!=24)return false;++rng_records;}
            }
            if(rng_records!=2)return false;
            if(validate_only)return true;
            for(unsigned i=0;i<count;++i){auto& a=records[i];
                if(a.side==2 && a.kind==23){
                    for(unsigned j=0;j<a.size;j+=4)if(shockwave_byte(j+0x18))
                        std::memcpy(reinterpret_cast<void*>(a.address+j),a.bytes.data()+j,4);
                }else if(a.side==2 && wind_field(a.kind))std::memcpy(reinterpret_cast<void*>(a.address),a.bytes.data(),a.size);
            }
            for(unsigned i=0;i<count;++i){auto& a=records[i];
                if(a.side==2 && std::memcmp(reinterpret_cast<void*>(a.address),a.bytes.data(),a.size))return false;
            }
            return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
};
}
