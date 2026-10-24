#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
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
    bool valid{},fixed_constraint_slots{};
    // Same owned restoration call only, after separate_effects(). Every payload
    // is read again; only linked-list discovery/deduplication is reused. Any
    // changed traversal input falls back to capture(), never trusts new pointers.
    bool refresh_intermediate(const TransformSnapshot& prior,const uintptr_t* owners,uintptr_t base) noexcept {
        valid=false;count=0;failure=0;trapped_exception=0;failure_address=0;
        if(this==&prior || !base || !prior.valid || !prior.fixed_constraint_slots ||
           !prior.count || prior.count>records.size())return reject(15,0);
        fixed_constraint_slots=true;unsigned roots=0;
        __try {
            for(unsigned i=0;i<prior.count;++i){const auto& old=prior.records[i];
                failure_address=old.address;
                if(!old.size || old.size>old.bytes.size() || old.address<0x10000 ||
                   old.address>UINTPTR_MAX-old.size)return reject(15,old.address);
                auto& now=records[i];now=old;
                std::memcpy(now.bytes.data(),reinterpret_cast<const void*>(old.address),old.size);++count;
                const auto equal=[&](unsigned offset,unsigned size){return offset<=old.size && size<=old.size-offset &&
                    !std::memcmp(old.bytes.data()+offset,now.bytes.data()+offset,size);};
                bool topology=false;
                switch(old.kind){
                case 0: topology=old.side<2 && old.address==owners[old.side] && old.size==512 &&
                            !(roots&(1u<<old.side)) && equal(0,40);if(topology)roots|=1u<<old.side;break;
                case 1: topology=old.size==0x58 && equal(0x40,16);break;
                case 2: topology=old.size==0x80 && equal(0,8) && equal(0x28,8);break;
                case 12: topology=old.size==0x60 && equal(0x58,2);break;
                case 28: topology=old.size==0x20 && equal(0x10,16);break;
                case 26: case 27: topology=old.size==8 && equal(0,8);break;
                case 5: topology=old.side==2 && old.size==8 && old.address==base+0x470E038 && equal(0,8);break;
                case 3: topology=old.side==2 && old.size==8;break; // separated manager scalar, not list head
                case 6: topology=old.side==2 && old.size==4 && old.address==base+0x4846450;break;
                case 10: topology=old.side==2 && old.size==104 && old.address==base+0x485EB30;break;
                case 11: topology=old.side==2 && old.size==24 && old.address==base+0x470E2B0;break;
                case 13: case 14: case 18: case 19: case 20: case 21: case 25:
                    topology=old.side<2;break;
                default: break; // dynamic effects/legacy variable contacts require full enumeration
                }
                if(!topology)return reject(16,old.address);
            }
            if(roots!=3 || !d78_dependencies(base) || !constraint_dependencies(base))return reject(17,failure_address);
            // Preserve the full capture's second-read stability check.
            for(unsigned i=0;i<count;++i){const auto& r=records[i];failure_address=r.address;
                if(std::memcmp(r.bytes.data(),reinterpret_cast<const void*>(r.address),r.size))return reject(7,r.address);}
            valid=true;failure_address=0;return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){trapped_exception=GetExceptionCode();return reject(8,failure_address);}
    }
    // Synchronous intermediate checkpoint only. Compare every captured byte,
    // including traversal links, before reusing the preceding owned capture.
    // This is never a cross-frame cache and never suppresses final verification.
    bool unchanged_live(const uintptr_t* owners,uintptr_t base)const noexcept {
        if(!base || !valid || !fixed_constraint_slots || !count || count>records.size())return false;
        unsigned roots=0;
        __try {
            for(unsigned i=0;i<count;++i){const auto& r=records[i];
                if(!r.size || r.size>r.bytes.size() || r.address<0x10000 || r.address>UINTPTR_MAX-r.size)return false;
                if(r.kind==0){if(r.side>=2 || r.address!=owners[r.side] || r.size!=512 || (roots&(1u<<r.side)))return false;roots|=1u<<r.side;}
                // Dynamic effect lists must use their separate ownership path.
                if(r.side==2 && (r.kind==4 || (r.kind>=7 && r.kind<=9) || r.kind==23 || r.kind==24))return false;
                if(std::memcmp(r.bytes.data(),reinterpret_cast<const void*>(r.address),r.size))return false;
            }
            return roots==3 && d78_dependencies(base) && constraint_dependencies(base);
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool stable_live_bytes()const noexcept {
        if(!valid || !count || count>records.size())return false;
        __try {
            for(unsigned i=0;i<count;++i){const auto& r=records[i];
                if(!r.size || r.size>r.bytes.size() || r.address<0x10000 || r.address>UINTPTR_MAX-r.size ||
                   std::memcmp(r.bytes.data(),reinterpret_cast<const void*>(r.address),r.size))return false;
            }
            return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool copy_recorded_from(const TransformSnapshot& source) noexcept {
        if(source.count>source.records.size())return false;
        if(this==&source)return true;
        std::memcpy(records.data(),source.records.data(),source.count*sizeof(Record));
        count=source.count;failure=source.failure;trapped_exception=source.trapped_exception;
        failure_address=source.failure_address;valid=source.valid;fixed_constraint_slots=source.fixed_constraint_slots;
        return true;
    }
    // Scratch only: rebuilt for each capture/validation, never cached across native updates.
    template<unsigned Capacity> struct AddressIndex {
        static_assert(Capacity && !(Capacity&(Capacity-1)));
        struct Entry {uintptr_t address;unsigned side,record;};
        std::array<Entry,Capacity> entries;
        explicit AddressIndex(bool initialize=true) noexcept {if(initialize)entries.fill({});}
        static unsigned bucket(uintptr_t address,unsigned side) noexcept {
            auto x=(address>>3)^(static_cast<uintptr_t>(side)*0x9E3779B97F4A7C15ull);
            x^=x>>33;x*=0xFF51AFD7ED558CCDull;x^=x>>33;
            return static_cast<unsigned>(x)&(Capacity-1);
        }
        unsigned find(uintptr_t address,unsigned side=0)const noexcept {
            if(!address)return UINT32_MAX;
            auto slot=bucket(address,side);
            for(unsigned n=0;n<Capacity;++n){const auto& e=entries[slot];
                if(!e.address)return UINT32_MAX;
                if(e.address==address && e.side==side)return e.record;
                slot=(slot+1)&(Capacity-1);
            }
            return UINT32_MAX;
        }
        bool insert(uintptr_t address,unsigned side,unsigned record) noexcept {
            if(!address)return false;
            auto slot=bucket(address,side);
            for(unsigned n=0;n<Capacity;++n){auto& e=entries[slot];
                if(!e.address){e={address,side,record};return true;}
                if(e.address==address && e.side==side)return false;
                slot=(slot+1)&(Capacity-1);
            }
            return false;
        }
    };
    static uintptr_t pointer(const std::byte* p){uintptr_t v{};std::memcpy(&v,p,8);return v;}
    bool append(uintptr_t address,unsigned side,unsigned kind,unsigned list,unsigned parent,unsigned size){
        failure_address=address;
        if(address<0x10000 || address>UINTPTR_MAX-size)return reject(1,address);
        if(count>=records.size())return reject(2,address);
        auto& r=records[count];r={};r.address=address;r.side=side;r.kind=kind;r.list=list;r.parent=parent;r.size=size;
        std::memcpy(r.bytes.data(),reinterpret_cast<void*>(address),size);++count;return true;
    }
    bool capture(const uintptr_t* owners,uintptr_t module_base=0,bool fixed_slots=false) noexcept {
        fixed_constraint_slots=fixed_slots;
        valid=false;count=0;failure=0;trapped_exception=0;failure_address=0;
        __try {
            for(unsigned side=0;side<2;++side){
                auto root=count;
                if(!append(owners[side],side,0,0,0,512))return false;
                unsigned groups=0,nodes=0;
                for(unsigned list=0;list<2;++list){
                    auto group=pointer(records[root].bytes.data()+0x10+list*0x10);
                    AddressIndex<128> seen_groups(false);const auto first_group=count;unsigned list_groups=0;
                    while(group){
                        if(list_groups<8){
                            for(unsigned n=first_group;n<count;++n)if(records[n].kind==1 && records[n].address==group)return reject(3,group);
                        }else{
                            if(list_groups==8){seen_groups.entries.fill({});
                                for(unsigned n=first_group;n<count;++n)if(records[n].kind==1)seen_groups.insert(records[n].address,0,n);}
                            if(!seen_groups.insert(group,0,count))return reject(3,group);
                        }
                        ++list_groups;
                        if(++groups>group_limit)return reject(4,group);
                        auto g=count;
                        if(!append(group,side,1,list,root,0x58))return false;
                        auto node=pointer(records[g].bytes.data()+0x48);AddressIndex<1024> seen_nodes;
                        while(node){
                            if(!seen_nodes.insert(node,0,count))return reject(5,node);
                            if(++nodes>node_limit)return reject(6,node);
                            auto n=count;
                            if(!append(node,side,2,list,g,0x80))return false;
                            auto vtable=pointer(records[n].bytes.data());
                            // Exact known node types; only proven fields of bounded constraints.
                            if(module_base && (vtable==module_base+0x3E88D78 || vtable==module_base+0x3E88DB8 || vtable==module_base+0x3E88DF8)){
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
                                if(fixed_constraint_slots){
                                    // Ghidra336F80 adds collision contacts to four embedded slots.
                                    // Capture the slots, not a variable-length graph of active contacts.
                                    for(unsigned j=0;j<4;++j)
                                        if(!append(node+0x120+j*0x60,side,25,list,n,0x58))return false;
                                }else for(unsigned j=0;j<static_cast<unsigned>(constraints);++j){
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
                            if(module_base && vtable==module_base+0x3E88D78 &&
                               !append(node+0x2A0,side,28,list,n,0x20))return false;
                            node=pointer(records[n].bytes.data()+0x28);
                        }
                        group=pointer(records[g].bytes.data()+0x40);
                    }
                }
                if(fixed_constraint_slots && module_base){
                    // 34BE26..34BEC7 walks these owner lists before the group solver.
                    // Capture target dependencies independently of current contact count.
                    AddressIndex<1024> seen_owned(false);const auto first_owned=count;unsigned owned=0;
                    for(unsigned list=0;list<2;++list){
                        auto target=pointer(records[root].bytes.data()+8+list*0x10);
                        while(target){
                            if(owned<16){
                                for(unsigned j=first_owned;j<count;++j)if(records[j].kind==26 && records[j].address==target)return reject(13,target);
                            }else{
                                if(owned==16){seen_owned.entries.fill({});
                                    for(unsigned j=first_owned;j<count;++j)if(records[j].kind==26)seen_owned.insert(records[j].address,0,j);}
                                if(!seen_owned.insert(target,0,count))return reject(13,target);
                            }
                            if(++owned>node_limit || target>UINTPTR_MAX-0xD0)return reject(13,target);
                            const auto t=count;
                            if(!append(target,side,26,list,root,8) || !append(target+0x28,side,27,list,t,8))return false;
                            if(pointer(records[t].bytes.data())==module_base+0x3E88E38){
                                if(!append(target+0x80,side,18,list,t,64) ||
                                   !append(target+0x40,side,19,list,t,64) ||
                                   !append(target+0x10,side,20,list,t,32) ||
                                   !append(target+0xC0,side,21,list,t,16))return false;
                            }
                            target=pointer(records[t+1].bytes.data());
                        }
                    }
                }
            }
            if(!d78_dependencies(module_base) || (fixed_constraint_slots && !constraint_dependencies(module_base)))return reject(14,failure_address);
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
                        if(vtable==module_base+0x3E88CE8 && !append(node+0x18,2,24,0,n,0x1C8))return false;
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
    // Pure snapshot comparison: never dereference potentially retired game objects.
    // reason, record, field, saved, next, counts, validity, differing record metadata.
    using TopologyDiagnostic=std::array<uint64_t,22>;
    bool same_topology(const TransformSnapshot& other,TopologyDiagnostic* diagnostic=nullptr)const{
        if(diagnostic){*diagnostic={};(*diagnostic)[5]=count;(*diagnostic)[6]=other.count;
            (*diagnostic)[7]=valid;(*diagnostic)[8]=other.valid;}
        auto reject=[&](unsigned reason,unsigned record,unsigned field,uint64_t a,uint64_t b){
            if(diagnostic){auto& d=*diagnostic;d[0]=reason;d[1]=record;d[2]=field;d[3]=a;d[4]=b;
                if(record<count && record<records.size()){
                    const auto& r=records[record];d[9]=r.address;d[10]=r.side;d[11]=r.kind;d[12]=r.list;d[13]=r.parent;d[14]=r.size;}
                if(record<other.count && record<other.records.size()){
                    const auto& r=other.records[record];d[15]=r.address;d[16]=r.side;d[17]=r.kind;d[18]=r.list;d[19]=r.parent;d[20]=r.size;}}
            return false;
        };
        if(!valid || !other.valid)return reject(1,UINT32_MAX,0,valid,other.valid);
        if(fixed_constraint_slots!=other.fixed_constraint_slots)return reject(6,UINT32_MAX,0,fixed_constraint_slots,other.fixed_constraint_slots);
        if(count>records.size() || other.count>other.records.size())return reject(3,UINT32_MAX,0,count,other.count);
        const unsigned common=count<other.count?count:other.count;
        for(unsigned i=0;i<common;++i){const auto& a=records[i];const auto& b=other.records[i];
            const uint64_t av[]={a.address,a.side,a.kind,a.list,a.parent,a.size};
            const uint64_t bv[]={b.address,b.side,b.kind,b.list,b.parent,b.size};
            for(unsigned f=0;f<6;++f)if(av[f]!=bv[f])return reject(4,i,f,av[f],bv[f]);
        }
        if(count!=other.count)return reject(2,common,0,count,other.count);
        return true;
    }
    // Dynamic global wind lists are owned/compared separately only when a
    // NativeEffectSession binding exists. Keep all player transforms and global
    // RNG/settings records; their existing comparisons and writers still run.
    bool separate_effects(){
        if(!valid || count>records.size())return false;
        std::array<unsigned,std::tuple_size_v<decltype(records)>> map{};
        unsigned kept=0;
        for(unsigned i=0;i<count;++i){const auto& r=records[i];
            const bool dynamic=r.side==2 && (r.kind==4 || (r.kind>=7 && r.kind<=9) || r.kind==23 || r.kind==24);
            map[i]=dynamic?UINT32_MAX:kept++;
        }
        for(unsigned i=0;i<count;++i)if(map[i]!=UINT32_MAX){
            auto value=records[i];
            if(value.side==2 && value.kind==3){
                if(value.size!=16 || value.address>UINTPTR_MAX-16)return false;
                value.address+=8;value.size=8;std::memmove(value.bytes.data(),value.bytes.data()+8,8);
            }
            if(value.parent<count){if(map[value.parent]==UINT32_MAX)return false;value.parent=map[value.parent];}
            records[map[i]]=value;
        }
        count=kept;return true;
    }
    struct TopologyDifference {
        unsigned mask{},index{UINT32_MAX},before_count{},after_count{};
        // address, side, kind, list, parent index, size; no live dereference.
        std::array<uint64_t,6> before{},after{};
        unsigned removed_nodes{},added_nodes{};
        std::array<uint64_t,4> removed_node{},added_node{}; // address, saved vtable, next link, parent address
        // First two changed nodes: address, vtable RVA, next, parent, child count,
        // first child address, kind, size, and total captured child bytes.
        std::array<std::array<uint64_t,9>,2> removed_details{},added_details{};
    };
    TopologyDifference topology_difference(const TransformSnapshot& other,uintptr_t module_base=0)const{
        TopologyDifference d;d.before_count=count;d.after_count=other.count;
        if(!valid || !other.valid)d.mask|=1;
        if(count!=other.count)d.mask|=2;
        if(count>records.size() || other.count>other.records.size()){d.mask|=256;return d;}
        const auto common=count<other.count?count:other.count;
        for(unsigned i=0;i<common;++i){const auto& a=records[i];const auto& b=other.records[i];
            const unsigned fields=(a.address!=b.address?4u:0u)|(a.side!=b.side?8u:0u)|
                (a.kind!=b.kind?16u:0u)|(a.list!=b.list?32u:0u)|(a.parent!=b.parent?64u:0u)|(a.size!=b.size?128u:0u);
            if(fields){d.mask|=fields;d.index=i;break;}
        }
        if(d.index==UINT32_MAX && count!=other.count)d.index=common;
        if(d.index<count){const auto& a=records[d.index];d.before={a.address,a.side,a.kind,a.list,a.parent,a.size};}
        if(d.index<other.count){const auto& b=other.records[d.index];d.after={b.address,b.side,b.kind,b.list,b.parent,b.size};}
        // Match global list nodes by address, not shifting record indices.
        // Only captured bytes are inspected; removed objects are never read live.
        const auto scan=[module_base](const TransformSnapshot& from,const TransformSnapshot& to,unsigned& total,std::array<uint64_t,4>& first,std::array<std::array<uint64_t,9>,2>& details){
            for(unsigned i=0;i<from.count;++i){const auto& r=from.records[i];
                if(r.side!=2 || r.kind!=4 || r.size!=24)continue;
                bool found=false;
                for(unsigned j=0;j<to.count;++j){const auto& s=to.records[j];
                    if(s.side==2 && s.kind==4 && s.address==r.address){found=true;break;}
                }
                if(found)continue;
                if(!total)first={r.address,(module_base && pointer(r.bytes.data())>=module_base?pointer(r.bytes.data())-module_base:pointer(r.bytes.data())),pointer(r.bytes.data()+16),r.parent<from.count?from.records[r.parent].address:0};
                if(total<details.size()){
                    auto& detail=details[total];
                    detail={r.address,(module_base && pointer(r.bytes.data())>=module_base?pointer(r.bytes.data())-module_base:pointer(r.bytes.data())),pointer(r.bytes.data()+16),r.parent<from.count?from.records[r.parent].address:0};
                    for(unsigned k=0;k<from.count;++k){const auto& child=from.records[k];
                        if(k==i || child.side!=2 || child.parent!=i)continue;
                        if(!detail[4]){detail[5]=child.address;detail[6]=child.kind;detail[7]=child.size;}
                        ++detail[4];detail[8]+=child.size;
                    }
                }
                ++total;
            }
        };
        scan(*this,other,d.removed_nodes,d.removed_node,d.removed_details);scan(other,*this,d.added_nodes,d.added_node,d.added_details);
        return d;
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
    static uint32_t scalar_word(const Record& r,unsigned offset) noexcept {
        uint32_t word{};std::memcpy(&word,r.bytes.data()+offset,4);return word;
    }
    // 308310: owner=player+29120; +2C is mode 0/1/2, +38 is a
    // per-update blend copied from player+1B84. Native load covers 1B84,
    // but not these owner outputs. Do not call the advancing writer to restore.
    static bool sway_control_word(unsigned offset,uint32_t word) noexcept {
        return offset==0x2C?word<=2:(offset==0x38 && (word&0x7F800000u)!=0x7F800000u);
    }
    static bool sway_controls_compatible(const Record& a,const Record& b,const Record& c) noexcept {
        for(unsigned off:{0x2Cu,0x38u}){
            const auto x=scalar_word(a,off),y=scalar_word(b,off),z=scalar_word(c,off);
            if(!sway_control_word(off,x) || !sway_control_word(off,y) || (z!=x && z!=y))return false;
        }
        return true;
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
                   !sway_controls_compatible(a,b,c))return false;
                // Unknown fields remain exact; only the two audited controls may
                // already contain the saved value after an earlier restore stage.
                for(const auto span: {std::array<unsigned,2>{0,0x2C}, {0x30,8}, {0x3C,0x1B4}}){
                    if(std::memcmp(a.bytes.data()+span[0],b.bytes.data()+span[0],span[1]) ||
                       std::memcmp(b.bytes.data()+span[0],c.bytes.data()+span[0],span[1]))return false;
                }
                if(std::memcmp(b.bytes.data()+0x1F0,c.bytes.data()+0x1F0,16))return false;
                for(unsigned k=0x1F0;k<0x200;k+=4){
                    unsigned x{},y{};std::memcpy(&x,a.bytes.data()+k,4);std::memcpy(&y,b.bytes.data()+k,4);
                    if((x&0x7F800000u)==0x7F800000u || (y&0x7F800000u)==0x7F800000u)return false;
                }
            }
            if(sides!=3)return false;
            if(validate_only)return true;
            for(unsigned i=0;i<count;++i){auto& a=records[i];if(a.kind==0){
                for(unsigned off:{0x2Cu,0x38u})std::memcpy(reinterpret_cast<void*>(a.address+off),a.bytes.data()+off,4);
                std::memcpy(reinterpret_cast<void*>(a.address+0x1F0),a.bytes.data()+0x1F0,16);
            }}
            for(unsigned i=0;i<count;++i){auto& a=records[i];if(a.kind==0 &&
                std::memcmp(reinterpret_cast<void*>(a.address),a.bytes.data(),512))return false;
            }
            return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool wind_field(unsigned kind){return (kind>=7 && kind<=11) || kind==23 || kind==24;}
    // Pure captured dependency validation. Never dereference inactive slot pointers.
    bool constraint_dependencies(uintptr_t base)const noexcept {
        if(!base || count>records.size())return false;
        AddressIndex<2048> owned_targets;
        for(unsigned k=0;k<count;++k){const auto& t=records[k];
            if(t.kind!=26 || t.size!=8 || pointer(t.bytes.data())!=base+0x3E88E38)continue;
            // Preserve the first matching record used by the previous linear search.
            if(owned_targets.find(t.address,t.side)!=UINT32_MAX)continue;
            if(!owned_targets.insert(t.address,t.side,k))return false;
        }
        for(unsigned i=0;i<count;++i){const auto& node=records[i];
            if(node.kind!=2)continue;
            const auto vt=pointer(node.bytes.data());
            if(vt!=base+0x3E88D78 && vt!=base+0x3E88DB8 && vt!=base+0x3E88DF8)continue;
            if(node.size!=0x80 || node.side>1 || i+1>=count)return false;
            const auto& payload=records[i+1];
            if(payload.kind!=12 || payload.parent!=i || payload.address!=node.address+0x80 || payload.size!=0x60)return false;
            std::int16_t links{},contacts{};
            std::memcpy(&links,payload.bytes.data()+0x58,2);std::memcpy(&contacts,payload.bytes.data()+0x5A,2);
            if(links<0 || links>4 || contacts<0 || contacts>4)return false;
            unsigned slots=0;
            for(unsigned j=i+2;j<count && records[j].kind!=2 && records[j].kind!=0 && records[j].kind!=1;++j){
                const auto& r=records[j];if(r.kind!=25 || r.parent!=i)continue;
                if(slots>=4 || r.side!=node.side || r.address!=node.address+0x120+slots*0x60 || r.size!=0x58)return false;
                if(slots<static_cast<unsigned>(contacts)){
                    const auto target=pointer(r.bytes.data()+0x50);
                    const auto k=owned_targets.find(target,node.side);
                    if(k==UINT32_MAX)return false;
                    {const auto& t=records[k];
                        if(t.parent>=count || records[t.parent].kind!=0 || records[t.parent].side!=t.side || k+5>=count)return false;
                        const unsigned kinds[]={27,18,19,20,21},sizes[]={8,64,64,32,16},offsets[]={0x28,0x80,0x40,0x10,0xC0};
                        for(unsigned d=0;d<5;++d){const auto& x=records[k+1+d];
                            if(x.parent!=k || x.kind!=kinds[d] || x.side!=t.side || x.size!=sizes[d] || x.address!=target+offsets[d])return false;
                        }
                    }
                }
                ++slots;
            }
            if(slots!=4)return false;
        }
        return true;
    }
    // 34A49C passes owner+2C/+38 to 33AB60. 33AC80/33AF60 read
    // those scalar controls; the suffix also contains the solver clamp limits.
    // Bind to the already captured owner, never follow an arbitrary reference.
    bool d78_dependencies(uintptr_t base)const noexcept {
        if(count>records.size())return false;
        std::optional<AddressIndex<2048>> parents;
        bool indexed=false,overflow=false;
        for(unsigned i=0;i<count;++i){const auto& n=records[i];
            if(n.kind!=2 || pointer(n.bytes.data())!=base+0x3E88D78)continue;
            if(n.size!=0x80 || n.side>1 || n.parent>=count)return false;
            const auto& g=records[n.parent];
            if(g.kind!=1 || g.side!=n.side || g.parent>=count)return false;
            const auto& root=records[g.parent];
            if(root.kind!=0 || root.size!=512 || root.side!=n.side)return false;
            unsigned suffixes=0;
            for(unsigned j=i+1;j<count && records[j].kind!=2 && records[j].kind!=1 && records[j].kind!=0;++j){
                const auto& r=records[j];if(r.kind!=28 || r.parent!=i)continue;
                if(r.side!=n.side || r.size!=0x20 || r.address!=n.address+0x2A0 ||
                   pointer(r.bytes.data()+0x10)!=root.address+0x2C ||
                   pointer(r.bytes.data()+0x18)!=root.address+0x38)return false;
                ++suffixes;
            }
            if(suffixes!=1)return false;
            const auto parent=pointer(n.bytes.data()+0x50);
            if(parent){
                if(!indexed){
                    parents.emplace();
                    for(unsigned j=0;j<count;++j){const auto& p=records[j];
                        if(!sway_state_header(p,base) || parents->find(p.address,p.side)!=UINT32_MAX)continue;
                        if(!parents->insert(p.address,p.side,j)){overflow=true;break;}
                    }
                    indexed=true;
                }
                bool found=parents->find(parent,n.side)!=UINT32_MAX;
                if(!found && overflow)for(unsigned j=0;j<count;++j){const auto& p=records[j];
                    if(p.kind==2 && p.address==parent && p.side==n.side && sway_state_header(p,base)){found=true;break;}}
                if(!found)return false;
            }
        }
        return true;
    }
    struct RestoreCheckDiagnostic {
        unsigned line{},record{UINT32_MAX},kind{},side{},offset{UINT32_MAX},expected{},actual{},exception{};
        uintptr_t node_vtable{};
        uint64_t position_saved{},position_next{};
        unsigned control_offset{},control_saved{},control_next{},control_current{},control_live{};
        bool reject(unsigned where) noexcept {line=where;return false;}
        int compare(const void* x,const void* y,unsigned length,unsigned start=0) noexcept {
            if(!std::memcmp(x,y,length))return 0;
            const auto* a=static_cast<const unsigned char*>(x);const auto* b=static_cast<const unsigned char*>(y);
            for(unsigned i=0;i<length;++i)if(a[i]!=b[i]){offset=start+i;expected=a[i];actual=b[i];return 1;}
            return 0;
        }
    };
    mutable RestoreCheckDiagnostic sway_check{},constraint_check{};
    static bool sway_state_header(const Record& r,uintptr_t base) noexcept {
        const auto vt=pointer(r.bytes.data());
        return r.kind==2 && r.size==0x80 && r.side<2 && (vt==base+0x3E88D78 || vt==base+0x3E88DB8 || vt==base+0x3E88DF8);
    }
    static int compare_sway_header(const Record& a,const Record& b,uintptr_t base,RestoreCheckDiagnostic& d) noexcept {
        d.node_vtable=pointer(a.bytes.data());
        std::memcpy(&d.position_saved,a.bytes.data()+0x10,8);
        std::memcpy(&d.position_next,b.bytes.data()+0x10,8);
        if(!sway_state_header(a,base))return d.compare(a.bytes.data(),b.bytes.data(),a.size);
        // 33A3A0 initializes XYZ as floats; 33A120/339E20 consume this
        // local position. It is captured scalar state, not node identity.
        // Only these three audited vtables qualify; W and all links remain exact.
        return d.compare(a.bytes.data(),b.bytes.data(),0x10) ||
            d.compare(a.bytes.data()+0x1C,b.bytes.data()+0x1C,0x60,0x1C) ||
            d.compare(a.bytes.data()+0x7E,b.bytes.data()+0x7E,2,0x7E);
    }
    bool restore_constraint_slots(const TransformSnapshot& next,const TransformSnapshot& current,uintptr_t base,bool validate_only)const noexcept {
        auto& d=constraint_check;d={};
        __try {
            if(!fixed_constraint_slots || !same_topology(next) || !same_topology(current) ||
               !constraint_dependencies(base) || !next.constraint_dependencies(base) || !current.constraint_dependencies(base) ||
               !d78_dependencies(base) || !next.d78_dependencies(base) || !current.d78_dependencies(base))return d.reject(__LINE__);
            for(unsigned i=0;i<count;++i){const auto& a=records[i];const auto& b=next.records[i];const auto& c=current.records[i];d.record=i;d.kind=a.kind;d.side=a.side;d.offset=UINT32_MAX;
                if(a.kind!=2 && a.kind!=12 && a.kind!=14 && a.kind!=25 && a.kind!=26 && a.kind!=27)continue;
                if(d.compare(c.bytes.data(),reinterpret_cast<void*>(a.address),a.size))return d.reject(__LINE__);
                if(a.kind==12){
                    // Sway vectors may already be rewound; only contact count changes here.
                    if(a.size!=0x60 || d.compare(a.bytes.data()+0x58,b.bytes.data()+0x58,2,0x58) ||
                       d.compare(b.bytes.data()+0x58,c.bytes.data()+0x58,4,0x58))return d.reject(__LINE__);
                }else{
                    if(a.kind==2?compare_sway_header(b,c,base,d):d.compare(b.bytes.data(),c.bytes.data(),a.size))return d.reject(__LINE__);
                    if(a.kind==14){
                        // 33A1E0 writes the distance at +8; 338870 consumes it.
                        // The target pointer is topology, never a restorable scalar.
                        if(a.size!=12 || a.parent>=count)return d.reject(__LINE__);
                        const auto& node=records[a.parent];
                        const auto vt=pointer(node.bytes.data());
                        if(node.kind!=2 || node.side!=a.side ||
                           (vt!=base+0x3E88D78 && vt!=base+0x3E88DB8 && vt!=base+0x3E88DF8) ||
                           a.address<node.address+0xE0 || (a.address-node.address-0xE0)%0x10 ||
                           (a.address-node.address-0xE0)/0x10>=4 ||
                           d.compare(a.bytes.data(),b.bytes.data(),8))return d.reject(__LINE__);
                    }else if(a.kind!=25 && (a.kind==2?compare_sway_header(a,b,base,d):d.compare(a.bytes.data(),b.bytes.data(),a.size)))return d.reject(__LINE__);
                }
            }
            if(validate_only)return true;
            // Restore audited link distances without rewriting their stable target pointers.
            for(unsigned i=0;i<count;++i){const auto& a=records[i];if(a.kind==14)std::memcpy(reinterpret_cast<void*>(a.address+8),a.bytes.data()+8,4);}
            // Publish pointers/vectors first and the saved active count last.
            for(unsigned i=0;i<count;++i){const auto& a=records[i];if(a.kind==25)std::memcpy(reinterpret_cast<void*>(a.address),a.bytes.data(),a.size);}
            for(unsigned i=0;i<count;++i){const auto& a=records[i];if(a.kind==12)std::memcpy(reinterpret_cast<void*>(a.address+0x5A),a.bytes.data()+0x5A,2);}
            for(unsigned i=0;i<count;++i){const auto& a=records[i];d.record=i;d.kind=a.kind;d.side=a.side;
                if((a.kind==25 || a.kind==14) && d.compare(reinterpret_cast<void*>(a.address),a.bytes.data(),a.size))return d.reject(__LINE__);
                if(a.kind==12 && d.compare(reinterpret_cast<void*>(a.address+0x5A),a.bytes.data()+0x5A,2,0x5A))return d.reject(__LINE__);
            }
            return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){d.exception=GetExceptionCode();return d.reject(__LINE__);}
    }
    bool restore_constraints(const TransformSnapshot& next,const TransformSnapshot& current,uintptr_t module_base,bool validate_only=false)const noexcept {
        if(fixed_constraint_slots)return restore_constraint_slots(next,current,module_base,validate_only);
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
                if(node.kind!=2 || node.side!=a.side || (vt!=module_base+0x3E88D78 && vt!=module_base+0x3E88DB8 && vt!=module_base+0x3E88DF8))return false;
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
        auto& d=sway_check;d={};
        __try {
            if(!same_topology(next) || !same_topology(current) || !d78_dependencies(module_base) ||
               !next.d78_dependencies(module_base) || !current.d78_dependencies(module_base))return d.reject(__LINE__);
            // Validate every node before the first write; retain all unmeasured fields.
            for(unsigned i=0;i<count;++i){auto& a=records[i];auto& b=next.records[i];auto& c=current.records[i];d.record=i;d.kind=a.kind;d.side=a.side;d.offset=UINT32_MAX;
                if(a.kind!=2 && a.kind!=12 && a.kind!=13 && a.kind!=28)continue;
                if(a.side>1 || (a.kind==2?compare_sway_header(b,c,module_base,d):d.compare(b.bytes.data(),c.bytes.data(),a.size)) ||
                   d.compare(c.bytes.data(),reinterpret_cast<void*>(a.address),a.size))return d.reject(__LINE__);
                if(a.kind==2 && pointer(a.bytes.data())==module_base+0x3E88D78){
                    const auto root=records[a.parent].parent;
                    for(unsigned offset:{0x2Cu,0x38u}){
                        const auto& x=records[root];const auto& y=next.records[root];const auto& z=current.records[root];
                        d.control_offset=offset;d.control_saved=scalar_word(x,offset);
                        d.control_next=scalar_word(y,offset);d.control_current=scalar_word(z,offset);
                        std::memcpy(&d.control_live,reinterpret_cast<const void*>(x.address+offset),4);
                        if(!sway_control_word(offset,d.control_saved) || !sway_control_word(offset,d.control_next) ||
                           (d.control_current!=d.control_saved && d.control_current!=d.control_next) ||
                           d.compare(z.bytes.data()+offset,reinterpret_cast<const void*>(x.address+offset),4,offset))return d.reject(__LINE__);
                    }
                }
                if(a.kind==12 || a.kind==13){
                    if(a.parent>=count)return d.reject(__LINE__);
                    auto& parent=records[a.parent];auto vt=pointer(parent.bytes.data());
                    if(parent.kind!=2 || parent.side!=a.side ||
                       (vt!=module_base+0x3E88D78 && vt!=module_base+0x3E88DB8 && vt!=module_base+0x3E88DF8))return d.reject(__LINE__);
                    if(a.kind==12){
                        if(a.address!=parent.address+0x80 || a.size!=0x60)return d.reject(__LINE__);
                        for(unsigned j=0x40;j<a.size;++j)
                            if((j<0x4C || j>=0x50) && !(fixed_constraint_slots && (j==0x5A || j==0x5B)) && a.bytes[j]!=b.bytes[j]){d.compare(a.bytes.data()+j,b.bytes.data()+j,1,j);return d.reject(__LINE__);}
                    }else if(vt!=module_base+0x3E88DF8 || a.address!=parent.address+0x2F0 || a.size!=12)return d.reject(__LINE__);
                }
                if(a.kind!=12 && (a.kind==2?compare_sway_header(a,b,module_base,d):d.compare(a.bytes.data(),b.bytes.data(),a.size)))return d.reject(__LINE__);
            }
            if(validate_only)return true;
            for(unsigned i=0;i<count;++i){const auto& a=records[i];if(sway_state_header(a,module_base)){
                std::memcpy(reinterpret_cast<void*>(a.address+0x10),a.bytes.data()+0x10,12);
                std::memcpy(reinterpret_cast<void*>(a.address+0x7C),a.bytes.data()+0x7C,2);}}

            for(unsigned i=0;i<count;++i){auto& a=records[i];if(a.kind!=12)continue;
                std::memcpy(reinterpret_cast<void*>(a.address),a.bytes.data(),0x40);
                std::memcpy(reinterpret_cast<void*>(a.address+0x4C),a.bytes.data()+0x4C,4);
            }
            for(unsigned i=0;i<count;++i){auto& a=records[i];d.record=i;d.kind=a.kind;d.side=a.side;
                if(sway_state_header(a,module_base) && d.compare(reinterpret_cast<void*>(a.address),a.bytes.data(),a.size))return d.reject(__LINE__);
                if(a.kind==12){
                    const auto* live=reinterpret_cast<const std::byte*>(a.address);
                    for(unsigned j=0;j<a.size;++j){
                        const auto expected=fixed_constraint_slots && (j==0x5A || j==0x5B)?next.records[i].bytes[j]:a.bytes[j];
                        if(live[j]!=expected){d.compare(&expected,live+j,1,j);return d.reject(__LINE__);}
                    }
                }
            }
            return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){d.exception=GetExceptionCode();return d.reject(__LINE__);}
    }
    // Pinned Iw_WindForStage methods333130/333550/333C20: scalar writes,
    // phase loops at70/80/A0/B0, and matrix output160..19F/1D0..1DF.
    static bool stage_wind_byte(unsigned offset){
        return (offset>=0x30 && offset<0x34) || (offset>=0x40 && offset<0x68) ||
            (offset>=0x6C && offset<0x74) || (offset>=0x80 && offset<0x84) ||
            (offset>=0x90 && offset<0x94) || (offset>=0xA0 && offset<0xA4) ||
            (offset>=0xB0 && offset<0xB4) || (offset>=0xD0 && offset<0xE0) ||
            (offset>=0x120 && offset<0x144) || (offset>=0x148 && offset<0x14C) ||
            (offset>=0x160 && offset<0x1A0) || (offset>=0x1D0 && offset<0x1E0);
    }
    struct SharedStateDetail {
        unsigned reason{},record{UINT32_MAX},kind{},size{},offset{UINT32_MAX},live_valid{};
        uint64_t address{};
        std::array<std::byte,512> saved{},next{},current{},live{};
    };
    struct WindDiagnostic { SharedStateDetail shared{};unsigned failure_line{},record{UINT32_MAX},kind{},size{},offset{},saved_word{},next_word{},control_saved{},control_next{};uintptr_t vtable_rva{};};
    static bool wind_reject(WindDiagnostic* diagnostic,unsigned line) noexcept {
        if(diagnostic)diagnostic->failure_line=line;
        return false;
    }
    static bool stage_wind_finite(const Record& a,const Record& b,WindDiagnostic* diagnostic=nullptr){
        if(a.kind!=24 || b.kind!=24 || a.size!=0x1C8 || b.size!=a.size)return wind_reject(diagnostic,__LINE__);
        for(unsigned j=0;j<a.size;j+=4){const auto offset=j+0x18;
            if(!stage_wind_byte(offset) || offset==0x60 || offset==0x6C || offset==0x148)continue;
            uint32_t x{},y{};std::memcpy(&x,a.bytes.data()+j,4);std::memcpy(&y,b.bytes.data()+j,4);
            // The update skips phase accumulation at +138 when control +EC is zero.
            // Permit only the observed unchanged inactive bit pattern, never arbitrary NaNs.
            if(offset==0x138 && x==0xFFFFFFFFu && y==x){
                uint32_t ac{},bc{};std::memcpy(&ac,a.bytes.data()+0xEC-0x18,4);std::memcpy(&bc,b.bytes.data()+0xEC-0x18,4);
                if(diagnostic){diagnostic->control_saved=ac;diagnostic->control_next=bc;}
                if((ac&0x7FFFFFFFu)==0 && (bc&0x7FFFFFFFu)==0)continue;
                // Pinned 3336C7..333702: ADDSS keeps this quiet NaN, then
                // MINSS selects its second operand (+118), yielding finite
                // +13C and reciprocal +140. Accept only that exact captured
                // configuration/output relationship, on both boundaries.
                auto clamped=[](const Record& r){
                    auto word=[&](unsigned off){uint32_t v{};std::memcpy(&v,r.bytes.data()+off-0x18,4);return v;};
                    const auto cap=word(0x118),step=word(0x134);
                    if(cap<0x00800000u || cap>=0x7F800000u ||
                       (step&0x7F800000u)==0x7F800000u || word(0x13C)!=cap)return false;
                    float limit{};std::memcpy(&limit,&cap,4);const float reciprocal=1.0f/limit;
                    uint32_t reciprocal_bits{};std::memcpy(&reciprocal_bits,&reciprocal,4);
                    return reciprocal_bits>=0x00800000u && reciprocal_bits<0x7F800000u && word(0x140)==reciprocal_bits;
                };
                uint32_t cap_a{},cap_b{};
                std::memcpy(&cap_a,a.bytes.data()+0x118-0x18,4);std::memcpy(&cap_b,b.bytes.data()+0x118-0x18,4);
                if(ac==0x3F800000u && bc==ac && cap_a==cap_b && clamped(a) && clamped(b))continue;
            }
            if((x&0x7F800000u)==0x7F800000u || (y&0x7F800000u)==0x7F800000u){
                if(diagnostic){diagnostic->offset=offset;diagnostic->saved_word=x;diagnostic->next_word=y;}
                return wind_reject(diagnostic,__LINE__);
            }
        }
        return true;
    }
    // Pinned 2DA1B0 assigns the static manager; 333FD0 copies battle time
    // scale 48462F0 into manager+0C before effect updates (331180 etc.).
    // Only the separated +08/+0C scalar record qualifies, never the list head.
    bool manager_clock_field(const Record& r,uintptr_t base)const {
        if(!base || r.side!=2 || r.kind!=3 || r.size!=8 || r.address!=base+0x4100B68 || r.parent>=count)return false;
        const auto& owner=records[r.parent];
        return owner.side==2 && owner.kind==5 && owner.size==8 && owner.address==base+0x470E038 &&
            pointer(owner.bytes.data())==base+0x4100B60 &&
            pointer(reinterpret_cast<const std::byte*>(owner.address))==base+0x4100B60;
    }
    bool restore_wind(const TransformSnapshot& next,const TransformSnapshot& current,uintptr_t module_base,bool validate_only=false,WindDiagnostic* diagnostic=nullptr)const noexcept {
        if(diagnostic)*diagnostic={};
        __try {
            if(!same_topology(next) || !same_topology(current)){if(diagnostic)diagnostic->shared.reason=1;return wind_reject(diagnostic,__LINE__);}
            unsigned rng_records=0;
            std::array<uint16_t,std::tuple_size_v<decltype(records)>> children23,children24;
            static_assert(std::tuple_size_v<decltype(records)><UINT16_MAX);
            for(unsigned j=0;j<count;++j)if(records[j].kind==4){children23[j]=0;children24[j]=0;}
            for(unsigned j=0;j<count;++j){const auto& r=records[j];
                if(r.parent<count && records[r.parent].kind==4){
                    if(r.kind==23)++children23[r.parent];else if(r.kind==24)++children24[r.parent];
                }
            }
            // Validate all shared records before writing any wind or RNG data.
            for(unsigned i=0;i<count;++i){auto& a=records[i];auto& b=next.records[i];auto& c=current.records[i];
                if(a.side!=2)continue;
                if(diagnostic){
                    diagnostic->record=i;diagnostic->kind=a.kind;diagnostic->size=a.size;
                    auto& d=diagnostic->shared;d={};d.reason=8;d.record=i;d.kind=a.kind;d.size=a.size;d.address=a.address;
                    d.saved=a.bytes;d.next=b.bytes;d.current=c.bytes;
                }
                const auto mismatch=[&](unsigned reason,const std::byte* x,const std::byte* y){
                    if(diagnostic){auto& d=diagnostic->shared;d.reason=reason;
                        for(unsigned j=0;j<a.size;++j)if(x[j]!=y[j]){d.offset=j;break;}}
                    return wind_reject(diagnostic,__LINE__);
                };
                if(std::memcmp(b.bytes.data(),c.bytes.data(),a.size))return mismatch(2,b.bytes.data(),c.bytes.data());
                if(diagnostic){
                    std::memcpy(diagnostic->shared.live.data(),reinterpret_cast<void*>(a.address),a.size);
                    diagnostic->shared.live_valid=1;
                    if(std::memcmp(c.bytes.data(),diagnostic->shared.live.data(),a.size))return mismatch(3,c.bytes.data(),diagnostic->shared.live.data());
                }else if(std::memcmp(c.bytes.data(),reinterpret_cast<void*>(a.address),a.size))return wind_reject(diagnostic,__LINE__);
                if(!wind_field(a.kind) && std::memcmp(a.bytes.data(),b.bytes.data(),a.size)){
                    // Preserve the neighboring +08 scalar exactly. Permit only
                    // the audited cached time-step to differ between frames.
                    if(!manager_clock_field(a,module_base) || std::memcmp(a.bytes.data(),b.bytes.data(),4))
                        return mismatch(4,a.bytes.data(),b.bytes.data());
                }
                if(a.kind==4){auto vt=pointer(a.bytes.data());
                    if(diagnostic){diagnostic->size=a.size;diagnostic->vtable_rva=vt>=module_base?vt-module_base:0;}
                    if(vt!=module_base+0x3E88CB8 && vt!=module_base+0x3E88C88 && vt!=module_base+0x3E88D18 && vt!=module_base+0x3E88CE8)return wind_reject(diagnostic,__LINE__);
                    if(vt==module_base+0x3E88CE8){
                        if(children24[i]!=1)return wind_reject(diagnostic,__LINE__);
                    }
                    if(vt==module_base+0x3E88D18){
                        if(children23[i]!=1)return wind_reject(diagnostic,__LINE__);
                    }
                }
                if(a.kind>=7 && a.kind<=9){
                    if(a.parent>=count || records[a.parent].kind!=4)return wind_reject(diagnostic,__LINE__);
                    auto offset=a.kind==7?0x40u:a.kind==8?0x70u:0x120u;
                    auto length=a.kind==7?32u:a.kind==8?112u:12u;
                    if(a.address!=records[a.parent].address+offset || a.size!=length)return wind_reject(diagnostic,__LINE__);
                }
                if(a.kind==24){
                    if(a.size!=0x1C8 || a.parent>=count)return wind_reject(diagnostic,__LINE__);
                    const auto& parent=records[a.parent];
                    if(parent.kind!=4 || parent.side!=2 || pointer(parent.bytes.data())!=module_base+0x3E88CE8 ||
                       a.address!=parent.address+0x18)return wind_reject(diagnostic,__LINE__);
                    for(unsigned j=0;j<a.size;++j)
                        if(!stage_wind_byte(j+0x18) && a.bytes[j]!=b.bytes[j])return wind_reject(diagnostic,__LINE__);
                    if(!stage_wind_finite(a,b,diagnostic))return false;
                }
                if(a.kind==23){
                    if(a.size!=0x168 || a.parent>=count)return wind_reject(diagnostic,__LINE__);
                    auto& parent=records[a.parent];
                    if(parent.kind!=4 || parent.side!=2 || pointer(parent.bytes.data())!=module_base+0x3E88D18 ||
                       a.address!=parent.address+0x18)return wind_reject(diagnostic,__LINE__);
                    for(unsigned j=0;j<a.size;++j)
                        if(!shockwave_byte(j+0x18) && a.bytes[j]!=b.bytes[j])return wind_reject(diagnostic,__LINE__);
                    for(unsigned j=0;j<a.size;j+=4)if(shockwave_byte(j+0x18) && j+0x18!=0x60){
                        std::uint32_t x{},y{};std::memcpy(&x,a.bytes.data()+j,4);std::memcpy(&y,b.bytes.data()+j,4);
                        if((x&0x7F800000u)==0x7F800000u || (y&0x7F800000u)==0x7F800000u)return wind_reject(diagnostic,__LINE__);
                    }
                    unsigned old{},next_index{};std::memcpy(&old,a.bytes.data()+0x48,4);std::memcpy(&next_index,b.bytes.data()+0x48,4);
                    if(old==UINT32_MAX || next_index!=old+1)return wind_reject(diagnostic,__LINE__);
                }
                if(a.kind==10){
                    if(a.address!=module_base+0x485EB30 || a.size!=104)return wind_reject(diagnostic,__LINE__);
                    unsigned index{};std::memcpy(&index,a.bytes.data()+100,4);if(index>25)return wind_reject(diagnostic,__LINE__);
                    std::memcpy(&index,b.bytes.data()+100,4);if(index>25)return wind_reject(diagnostic,__LINE__);++rng_records;
                }
                if(a.kind==11){if(a.address!=module_base+0x470E2B0 || a.size!=24)return wind_reject(diagnostic,__LINE__);++rng_records;}
            }
            if(rng_records!=2)return wind_reject(diagnostic,__LINE__);
            if(validate_only){if(diagnostic)diagnostic->shared.reason=0;return true;}
            for(unsigned i=0;i<count;++i){auto& a=records[i];
                if(manager_clock_field(a,module_base)){
                    std::memcpy(reinterpret_cast<void*>(a.address+4),a.bytes.data()+4,4);
                }else if(a.side==2 && a.kind==24){
                    for(unsigned j=0;j<a.size;j+=4)if(stage_wind_byte(j+0x18))
                        std::memcpy(reinterpret_cast<void*>(a.address+j),a.bytes.data()+j,4);
                }else if(a.side==2 && a.kind==23){
                    for(unsigned j=0;j<a.size;j+=4)if(shockwave_byte(j+0x18))
                        std::memcpy(reinterpret_cast<void*>(a.address+j),a.bytes.data()+j,4);
                }else if(a.side==2 && wind_field(a.kind))std::memcpy(reinterpret_cast<void*>(a.address),a.bytes.data(),a.size);
            }
            for(unsigned i=0;i<count;++i){auto& a=records[i];
                if(diagnostic){diagnostic->record=i;diagnostic->kind=a.kind;}
                if(a.side==2 && std::memcmp(reinterpret_cast<void*>(a.address),a.bytes.data(),a.size))return wind_reject(diagnostic,__LINE__);
            }
            if(diagnostic)diagnostic->shared.reason=0;
            return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){if(diagnostic)diagnostic->shared.reason=7;return wind_reject(diagnostic,__LINE__);}
    }
};
}
