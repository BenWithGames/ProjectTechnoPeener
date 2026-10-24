#pragma once
#include "FullRestoreStream.hpp"
#include "CameraSlotSchema.hpp"
#include <span>
namespace sc6::research {
// Native 321750/321A30 tags and manager-owned slot storage. No allocation or
// destructor is invented: the native loader reconstructs occupied pool slots.
struct CameraSlots {
    static constexpr unsigned empty=UINT32_MAX;
    inline static constexpr uintptr_t vtables[]={0x3E885E0,0x3E88328,0x3E881C8,0x3E87EB0,0x3E87D90,0x3E87C08,0x3E88030,0x3E88708,0x3E88488};
    struct Slot {unsigned type{empty},first{},end{},offset{},bytes{};uintptr_t pool{},active{};};
    std::array<Slot,16> slots{};
    uintptr_t module{},manager{},manager_vtable{},arena{},table{};
    // Borrowed round-owned roots, not reference-counted camera allocations.
    // 3841E0/384540 supply relocation slots 1 and 2 from these exact players.
    std::array<uintptr_t,2> reference_players{};
    uintptr_t reference_owner{},reference_arena{};
    unsigned count{},tail{},failure{};bool enabled{},valid{};
    static std::span<const CameraSlotField> extra(unsigned type){
        switch(type){case 0:return camera_fields_340ed0;case 1:return camera_fields_341110;
        case 2:case 7:return camera_fields_318860;case 4:return camera_fields_317bd0;
        case 8:return camera_fields_31c190;default:return {};}
    }
    static bool address(uintptr_t p,uintptr_t size=0x400){return p>=0x10000 && p<=UINTPTR_MAX-size;}
    bool parse(const FullRestoreStream& s,uintptr_t base){
        *this={};enabled=true;module=base;
        auto reject=[&](unsigned r){failure=r;return false;};
        if(!base || base>UINTPTR_MAX-0x4850000 || !s.sealed || s.stream.failures || !s.writes || s.writes>FullRestoreStream::transfer_limit || s.stream.write_position>s.stream.payload.size())return reject(1);
        uint64_t at=0;bool ended=false,invalid_tag=false;
        for(unsigned i=0;i<s.writes;++i){auto n=s.lengths[i];auto w=s.traces[i].writer;
            if(!n || at>s.stream.write_position || n>s.stream.write_position-at)return reject(2);
            const bool tag=w==base+0x3217EC || w==base+0x321814;
            if(tag){
                if(ended || count==16 || n!=4)return reject(3);
                if(count){slots[count-1].end=i;slots[count-1].bytes=static_cast<unsigned>(at)-slots[count-1].offset;}
                auto& x=slots[count++];x.first=i;x.offset=static_cast<unsigned>(at);
                std::memcpy(&x.type,s.stream.payload.data()+at,4);
                if((w==base+0x321814 && x.type!=empty) || (w==base+0x3217EC && x.type>8))invalid_tag=true;
            }
            if(count && !ended && (w==base+0x32492F || w==base+0x321861)){
                tail=i;slots[count-1].end=i;slots[count-1].bytes=static_cast<unsigned>(at)-slots[count-1].offset;ended=true;
            }
            at+=n;
        }
        if(!count || !ended || at!=s.stream.write_position)return reject(5);
        if(invalid_tag)return reject(4);
        // All known slot serializers have fixed, audited transfer sequences.
        for(unsigned k=0;k<count;++k){auto& x=slots[k];unsigned i=x.first+1;unsigned payload_at=x.offset+4;
            if(x.type==empty){if(i!=x.end)return reject(6);continue;}
            auto fields=[&](std::span<const CameraSlotField> schema){for(const auto& f:schema){
                if(i>=x.end || s.lengths[i]!=f.length || s.traces[i].writer!=base+f.writer)return false;
                if(f.offset>=0){auto p=s.traces[i].source;if(p<static_cast<uintptr_t>(f.offset))return false;
                    const auto owner=p-f.offset;if(!address(owner))return false;if(!x.pool)x.pool=owner;else if(x.pool!=owner)return false;}
                else if(!address(s.traces[i].source,f.length))return false;
                if(f.offset==0x140){unsigned slot{};std::memcpy(&slot,s.stream.payload.data()+payload_at,4);if(slot!=k)return false;}
                if(f.offset<0){unsigned value{};std::memcpy(&value,s.stream.payload.data()+payload_at,4);
                    if(f.writer==0x317C60?(value!=empty && value>1):value!=empty)return false;}
                payload_at+=f.length;++i;}return true;};
            if(!fields(camera_fields_33fbc0) || !fields(extra(x.type)) || i!=x.end)return reject(7);
        }
        return true;
    }
    bool capture(const FullRestoreStream& s,uintptr_t base,const uintptr_t* players=nullptr) noexcept {
        if(!parse(s,base))return false;
        __try {
            manager=*reinterpret_cast<const uintptr_t*>(base+0x470DF28);
            if(!address(manager)) {failure=8;return false;}
            manager_vtable=*reinterpret_cast<const uintptr_t*>(manager);
            arena=*reinterpret_cast<const uintptr_t*>(manager+8);table=*reinterpret_cast<const uintptr_t*>(manager+0x90);
            if(!address(arena) || !address(table) || manager_vtable!=base+0x3E885A0 || count!=16){failure=8;return false;}
            for(unsigned i=0;i<count;++i){auto& x=slots[i];const auto pool=*reinterpret_cast<const uintptr_t*>(manager+0x10+i*8);
                const auto active=*reinterpret_cast<const uintptr_t*>(table+0x270+i*8);
                if(!address(pool) || (x.pool && pool!=x.pool) || (x.type==empty?active!=0:active!=pool)){failure=9;return false;}
                for(unsigned j=0;j<i;++j)if(slots[j].pool==pool){failure=9;return false;}
                x.pool=pool;x.active=active;
                if(active && (*reinterpret_cast<const uintptr_t*>(active)!=base+vtables[x.type] || *reinterpret_cast<const unsigned*>(active+0x140)!=i)){failure=10;return false;}
            }
            if(players){
                reference_players={players[0],players[1]};
                reference_owner=*reinterpret_cast<const uintptr_t*>(manager+0xA0);
                // 321750 omits the entire auxiliary section when no owner is
                // present. This grants no permission for non-null references.
                if(!reference_owner && tail<s.writes && s.traces[tail].writer==base+0x321861 && s.lengths[tail]==4){
                    reference_players={};valid=true;return true;}
                const auto offset=slots[count-1].offset+slots[count-1].bytes;
                if(!address(reference_owner,0x2F0) || tail+2>s.writes ||
                   s.traces[tail].writer!=base+0x32492F || s.lengths[tail]!=0x2F0 || s.traces[tail].source!=reference_owner ||
                   s.traces[tail+1].writer!=base+0x324942 || s.lengths[tail+1]!=0x41E0 ||
                   offset>s.stream.write_position || 0x44D0>s.stream.write_position-offset){failure=12;return false;}
                std::memcpy(&reference_arena,s.stream.payload.data()+offset+8,8);
                if(!reference_identity_live() || s.traces[tail+1].source!=reference_arena){failure=13;return false;}
                for(unsigned k=0;k<17;++k){uintptr_t saved{};std::memcpy(&saved,s.stream.payload.data()+offset+0x10+k*8,8);
                    if(saved!=reference_arena+k*0x3E0){failure=14;return false;}}
            }
            valid=true;return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){failure=11;return false;}
    }
    bool compatible(const CameraSlots& b)const{
        if(!enabled || !valid || !b.enabled || !b.valid || module!=b.module || manager!=b.manager || manager_vtable!=b.manager_vtable || arena!=b.arena || table!=b.table || count!=b.count || count>16)return false;
        if(reference_players!=b.reference_players || reference_owner!=b.reference_owner || reference_arena!=b.reference_arena)return false;
        for(unsigned i=0;i<count;++i){if(slots[i].pool!=b.slots[i].pool)return false;
            // Smoothing-history identity is governed by the existing camera checks.
            if((slots[i].type==3 || b.slots[i].type==3) && slots[i].type!=b.slots[i].type)return false;}
        return true;
    }
    bool pool_owner(uintptr_t owner)const {if(!valid)return false;for(unsigned i=0;i<count;++i)if(slots[i].pool==owner)return true;return false;}
    // Corresponding non-slot records remain positional and exact. Only audited
    // per-slot payloads are independently shaped. Their source ranges were checked.
    struct MapDiagnostic {unsigned reason{},saved{empty},next{empty};};
    struct Reference {unsigned record{empty},index{};uint64_t target{},offset{},resource{};};
    // The secondary camera arena is restored before its virtual reference loads
    // (3249E0). 32AC00 emits two extra records; audited no-op types emit none.
    static bool reference_tail(const FullRestoreStream& s,uintptr_t base,unsigned first,
        std::array<unsigned,17>& records,std::array<uint64_t,17>& refs,unsigned& end,std::array<Reference,17>* evidence=nullptr){
        if(evidence)*evidence={};
        records.fill(empty);refs.fill(0);end=first;
        if(!s.sealed || s.stream.failures || s.writes>FullRestoreStream::transfer_limit || s.stream.write_position>s.stream.payload.size() ||
           first>s.writes || s.writes-first<2 || s.traces[first].writer!=base+0x32492F || s.lengths[first]!=0x2F0 ||
           s.traces[first+1].writer!=base+0x324942 || s.lengths[first+1]!=0x41E0)return false;
        uint64_t at=0;for(unsigned i=0;i<first;++i){if(s.lengths[i]>s.stream.write_position-at)return false;at+=s.lengths[i];}
        if(at>s.stream.write_position || 0x44D0>s.stream.write_position-at)return false;
        auto word=[&](uint64_t off){uint64_t v{};std::memcpy(&v,s.stream.payload.data()+off,8);return v;};
        const auto arena=word(at+8);if(!address(arena,0x41E0) || s.traces[first+1].source!=arena)return false;
        const auto payload=at+0x2F0;unsigned record=first+2;uint64_t extra=payload+0x41E0;
        for(unsigned k=0;k<17;++k){
            if(word(at+0x10+k*8)!=arena+k*0x3E0)return false;
            const auto vt=word(payload+k*0x3E0);
            if(vt==base+0x3E88858){
                if(record+2>s.writes || s.traces[record].writer!=base+0x32AC76 || s.lengths[record]!=16 ||
                   s.traces[record+1].writer!=base+0x32ACAC || s.lengths[record+1]!=8 ||
                   extra>s.stream.write_position || 24>s.stream.write_position-extra)return false;
                refs[k]=word(payload+k*0x3E0+0x3B8);records[k]=record;
                unsigned index{};std::memcpy(&index,s.stream.payload.data()+extra,4);
                if(evidence)(*evidence)[k]={record,index,refs[k],word(extra+8),word(extra+16)};
                if(!refs[k] && (index || word(extra+8) || word(extra+16)))return false;
                record+=2;extra+=24;
            }else if(vt!=base+0x3E88BA8 && vt!=base+0x3E88B70 && vt!=base+0x3E88B28 &&
                     vt!=base+0x3E88AF0 && vt!=base+0x3E88C50)return false;
        }
        if(record>=s.writes || s.traces[record].writer!=base+0x324982 || s.lengths[record]!=4)return false;
        end=record;return true;
    }
    bool owns_reference(unsigned slot,const Reference& r)const {
        // 322610 constructs this controller only in auxiliary slot 4; 324320
        // assigns its player. 32AC00 uses an inclusive relocation range, so
        // adjacent P2 can encode as P1 + 0x973F0. Accept only exact player roots,
        // never an arbitrary interior pointer or overflowing root+offset.
        if(slot!=4 || !reference_owner || !address(reference_arena,0x41E0) ||
           r.index<1 || r.index>2 || r.offset>0x973F0)return false;
        const auto root=reference_players[r.index-1];
        return address(root,0x973F0) &&
            (r.target==reference_players[0] || r.target==reference_players[1]) &&
            address(r.target,0x973F0) && r.target>=root && r.target-root==r.offset;
    }
    bool reference_identity_live()const noexcept {
        __try {
            if(!reference_owner)return reference_arena==0 && reference_players==std::array<uintptr_t,2>{};
            if(!address(reference_owner,0x2F0) || !address(reference_arena,0x41E0) ||
               !address(reference_players[0],0x973F0) || !address(reference_players[1],0x973F0) ||
               reference_players[0]==reference_players[1] ||
               *reinterpret_cast<const uintptr_t*>(module+0x470DE90)!=reference_players[0] ||
               *reinterpret_cast<const uintptr_t*>(module+0x470DE98)!=reference_players[1] ||
               *reinterpret_cast<const uintptr_t*>(manager+0xA0)!=reference_owner ||
               *reinterpret_cast<const uintptr_t*>(reference_owner+8)!=reference_arena)return false;
            for(unsigned k=0;k<17;++k)if(*reinterpret_cast<const uintptr_t*>(reference_owner+0x10+k*8)!=reference_arena+k*0x3E0)return false;
            return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool map(const FullRestoreStream& a,const CameraSlots& b,const FullRestoreStream& next,std::array<unsigned,FullRestoreStream::transfer_limit>& out,MapDiagnostic* diagnostic=nullptr)const{
        if(diagnostic)*diagnostic={};
        auto reject=[&](unsigned reason,unsigned i=empty,unsigned j=empty){if(diagnostic)*diagnostic={reason,i,j};return false;};
        out.fill(empty);if(!compatible(b))return reject(1);
        CameraSlots pa,pb;if(!pa.parse(a,module) || !pb.parse(next,module))return reject(2);
        const CameraSlots* originals[]={this,&b};const CameraSlots* parsed[]={&pa,&pb};
        for(unsigned sample=0;sample<2;++sample){const auto& x=*originals[sample];const auto& y=*parsed[sample];if(x.count!=y.count || x.tail!=y.tail)return false;
            for(unsigned k=0;k<x.count;++k){const auto& u=x.slots[k];const auto& v=y.slots[k];
                if(u.type!=v.type || u.first!=v.first || u.end!=v.end || u.offset!=v.offset || u.bytes!=v.bytes || (v.pool && u.pool!=v.pool))return false;}}
        unsigned i=0,j=0;
        auto equal_until=[&](unsigned ae,unsigned be){if(ae-i!=be-j)return false;while(i<ae){if(a.lengths[i]!=next.lengths[j] || a.traces[i].writer!=next.traces[j].writer)return false;out[i++]=j++;}return true;};
        for(unsigned k=0;k<count;++k){const auto& x=slots[k];const auto& y=b.slots[k];
            if(x.first<i || y.first<j || !equal_until(x.first,y.first))return false;
            // Slot tag and block sources are validated by parse/capture rather
            // than compared against a different object's serialized records.
            i=x.end;j=y.end;
        }
        const auto ai=i,bj=j;
        if(equal_until(a.writes,next.writes))return true;
        i=ai;j=bj;for(unsigned n=i;n<a.writes;++n)out[n]=empty;
        std::array<unsigned,17> ar,br;std::array<uint64_t,17> av,bv;std::array<Reference,17> ax,bx;unsigned ae{},be{};
        if(!reference_tail(a,module,i,ar,av,ae,&ax) || !reference_tail(next,module,j,br,bv,be,&bx))return reject(3,i,j);
        // Both fixed blocks retain their exact source addresses and lengths.
        for(unsigned n=0;n<2;++n){if(a.traces[i+n].source!=next.traces[j+n].source)return reject(4,i+n,j+n);out[i+n]=j+n;}
        for(unsigned k=0;k<17;++k){
            if(ar[k]!=empty && br[k]!=empty){out[ar[k]]=br[k];out[ar[k]+1]=br[k]+1;}
            else if((ar[k]!=empty && av[k] && !owns_reference(k,ax[k])) ||
                    (br[k]!=empty && bv[k] && !b.owns_reference(k,bx[k])))return reject(5,ar[k],br[k]);
            else if((ar[k]!=empty && av[k]) || (br[k]!=empty && bv[k])){
                if(a.traces[i].source!=reference_owner || next.traces[j].source!=reference_owner ||
                   a.traces[i+1].source!=reference_arena || next.traces[j+1].source!=reference_arena)return reject(7,ar[k],br[k]);
            }
        }
        i=ae;j=be;if(!equal_until(a.writes,next.writes))return reject(6,i,j);
        return true;
    }
    // After the native arena and virtual readers run, require the archived
    // controller types and reference targets exactly. Do not patch a pointer
    // over a failed native load. Other payload verification remains unchanged.
    bool references_loaded(const FullRestoreStream& s,std::array<uint64_t,5>& d)const noexcept {
        d={};if(!reference_owner)return true;
        __try {
            d[0]=1;if(!reference_identity_live()){d[1]=1;return false;}
            std::array<unsigned,17> records;std::array<uint64_t,17> refs;unsigned end{};
            if(!reference_tail(s,module,tail,records,refs,end)){d[1]=2;return false;}
            const auto offset=slots[count-1].offset+slots[count-1].bytes+0x2F0;
            for(unsigned k=0;k<17;++k){
                d[2]=k;std::memcpy(&d[3],s.stream.payload.data()+offset+k*0x3E0,8);
                d[4]=*reinterpret_cast<const uintptr_t*>(reference_arena+k*0x3E0);
                if(d[3]!=d[4]){d[1]=3;return false;}
                if(records[k]!=empty){d[3]=refs[k];d[4]=*reinterpret_cast<const uintptr_t*>(reference_arena+k*0x3E0+0x3B8);
                    if(d[3]!=d[4]){d[1]=4;return false;}}
            }
            d[0]=2;return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){d[1]=5;d[4]=GetExceptionCode();return false;}
    }
    bool live()const noexcept {
        __try {
            if(!valid || *reinterpret_cast<const uintptr_t*>(module+0x470DF28)!=manager || *reinterpret_cast<const uintptr_t*>(manager)!=manager_vtable ||
               *reinterpret_cast<const uintptr_t*>(manager+8)!=arena || *reinterpret_cast<const uintptr_t*>(manager+0x90)!=table || !reference_identity_live())return false;
            for(unsigned i=0;i<count;++i){const auto& x=slots[i];
                if(*reinterpret_cast<const uintptr_t*>(manager+0x10+i*8)!=x.pool || *reinterpret_cast<const uintptr_t*>(table+0x270+i*8)!=x.active)return false;
                if(x.active && (*reinterpret_cast<const uintptr_t*>(x.active)!=module+vtables[x.type] || *reinterpret_cast<const unsigned*>(x.active+0x140)!=i))return false;}
            return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool restore_empty(const CameraSlots& current)const noexcept {
        if(!compatible(current) || !current.live())return false;
        __try {for(unsigned i=0;i<count;++i)if(slots[i].type==empty)*reinterpret_cast<uintptr_t*>(table+0x270+i*8)=0;return true;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
};
}
