#define NOMINMAX
#include <Windows.h>
#undef interface
#include "../src/CameraSlotEvidence.hpp"
#include "../src/CameraSnapshot.hpp"
#include "../src/FullStateProbe.hpp"
#include <cassert>
#include <iostream>
using namespace sc6::research;
struct Fixture {
    uintptr_t global{};uintptr_t base{};
    alignas(16) std::array<std::byte,0x400> manager{},table{},arena{};
    alignas(16) std::array<std::array<std::byte,0x400>,16> pools{};
    Fixture(){global=reinterpret_cast<uintptr_t>(manager.data());base=reinterpret_cast<uintptr_t>(&global)-0x470DF28;
        put(manager.data(),base+0x3E885A0);put(manager.data()+8,reinterpret_cast<uintptr_t>(arena.data()));
        put(manager.data()+0x90,reinterpret_cast<uintptr_t>(table.data()));
        for(unsigned i=0;i<16;++i)put(manager.data()+0x10+i*8,reinterpret_cast<uintptr_t>(pools[i].data()));}
    static void put(std::byte* p,uintptr_t v){std::memcpy(p,&v,8);}
    void set(unsigned type){for(unsigned i=0;i<16;++i){auto t=i==1?3u:i?UINT32_MAX:type;auto p=reinterpret_cast<uintptr_t>(pools[i].data());
        put(table.data()+0x270+i*8,t==CameraSlots::empty?0:p);
        if(t!=CameraSlots::empty){put(pools[i].data(),base+CameraSlots::vtables[t]);std::memcpy(pools[i].data()+0x140,&i,4);}}}
    void append(FullRestoreStream& s,uintptr_t writer,uintptr_t source,unsigned n,const void* bytes){auto i=s.writes++;s.lengths[i]=n;s.traces[i].writer=writer;s.traces[i].source=source;
        std::memcpy(s.stream.payload.data()+s.stream.write_position,bytes,n);s.stream.write_position+=n;}
    void save(FullRestoreStream& s,unsigned type){s.writes=s.reads=0;s.sealed=false;s.stream.write_position=s.stream.read_position=0;s.stream.failures=0;set(type);unsigned zero=0;
        append(s,base+0x100,global+0x200,4,&zero);
        for(unsigned k=0;k<16;++k){auto t=k==1?3u:k?UINT32_MAX:type;append(s,base+(t==CameraSlots::empty?0x321814:0x3217EC),0x123400,4,&t);
            if(t==CameraSlots::empty)continue;
            auto fields=[&](std::span<const CameraSlotField> fs){for(auto f:fs){unsigned local=UINT32_MAX;
                auto source=f.offset<0?uintptr_t{0x123408}:reinterpret_cast<uintptr_t>(pools[k].data()+f.offset);
                append(s,base+f.writer,source,f.length,f.offset<0?static_cast<const void*>(&local):pools[k].data()+f.offset);}};
            fields(camera_fields_33fbc0);fields(CameraSlots::extra(t));}
        append(s,base+0x321861,global+0xA8,4,&zero);assert(s.seal());}
};
int main(){
    auto f=std::make_unique<Fixture>();auto a=std::make_unique<FullRestoreStream>(),b=std::make_unique<FullRestoreStream>();
    CameraSlots sa,sb;std::array<unsigned,FullRestoreStream::transfer_limit> map;
    for(unsigned ta=0;ta<10;++ta)for(unsigned tb=0;tb<10;++tb){const auto x=ta==9?UINT32_MAX:ta,y=tb==9?UINT32_MAX:tb;
        f->save(*a,x);assert(sa.capture(*a,f->base));f->save(*b,y);assert(sb.capture(*b,f->base));
        const bool allowed=ta==tb || (ta!=3 && tb!=3);
        assert(sa.map(*a,sb,*b,map)==allowed);
        if(!allowed)continue;
        assert(map[0]==0 && map[a->writes-1]==b->writes-1);
        for(unsigned i=sa.slots[0].first;i<sa.slots[0].end;++i)assert(map[i]==UINT32_MAX);
        assert(sb.live());assert(sa.restore_empty(sb));
        // Emulate native type factories for the occupancy postcheck.
        f->set(x);assert(sa.live());
    }
    f->save(*a,UINT32_MAX);assert(sa.capture(*a,f->base));f->save(*b,0);assert(sb.capture(*b,f->base));
    assert(b->writes-a->writes==46);CameraSlotEvidence e;e.capture(sa,*a,sb,*b);assert(e.selected==0 && e.retained[0]==4 && e.retained[1]==sb.slots[0].bytes);
    assert(e.payload[0][0]==UINT32_MAX && e.payload[1][0]==0);
    auto altered=sb;altered.arena++;assert(!sa.compatible(altered));
    Fixture::put(f->table.data()+0x270,0);assert(!sa.restore_empty(sb)); // stale live evidence, no mutation
    f->set(0);auto index=sb.slots[0].first+1;b->lengths[index]++;assert(!altered.parse(*b,f->base));b->lengths[index]--;
    b->traces[index].source++;assert(!altered.parse(*b,f->base));b->traces[index].source--;
    b->traces[b->writes-1].writer++;assert(!sa.map(*a,sb,*b,map));b->traces[b->writes-1].writer--;
    auto q=std::make_unique<FullStateProbe>();assert(q->saved.copy_recorded_from(*a) && q->after_save.copy_recorded_from(*b));
    q->camera_slots={sa,sb};assert(sa.map(*a,sb,*b,q->camera_source_map));q->ring_save_unchanged=q->ring_rotation_ok=true;
    assert(q->delayed_sources_ok(f->base));q->after_save.traces[b->writes-1].source++;assert(!q->delayed_sources_ok(f->base));
    // Guard helpers must not index an omitted slot record while probing a group.
    const auto tail=a->writes-1;q->saved.lengths[tail-4]=0x200;
    assert(!q->audited_scalar_source(tail,f->base));
    q->saved.traces[tail].writer=f->base+0x30AB84;
    assert(!q->audited_reference_source(tail,f->base));
    // Unknown type stays rejected, but its raw payload survives for diagnosis.
    unsigned unknown=99;std::memcpy(b->stream.payload.data()+sb.slots[0].offset,&unknown,4);
    assert(!altered.parse(*b,f->base) && altered.failure==4);
    e.capture(altered,*b,altered,*b);assert(e.selected==0 && e.payload[0][0]==99 && e.retained[0]==sb.slots[0].bytes);
    CameraSnapshot ca,cb,out;ca.valid=cb.valid=true;ca.count=1;cb.count=2;ca.records[0].owner=sa.slots[1].pool;ca.records[0].smoothing_valid=true;
    Fixture::put(ca.records[0].bytes.data(),f->base+CameraSlots::vtables[3]);cb.records[0]=ca.records[0];cb.records[1].owner=sb.slots[0].pool;
    assert(ca.align_next(cb,sa,sb,out) && out.count==1);cb.records[1].owner++;assert(!ca.align_next(cb,sa,sb,out));
    std::cout<<"Camera slots: 100 type pairs, fixed schemas, 46-record birth, mapping, stale ownership, raw evidence and exact non-slot source guards passed\n";
}
