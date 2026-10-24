#define NOMINMAX
#include <Windows.h>
#undef interface
#include "../src/CameraSlotEvidence.hpp"
#include "../src/CameraSnapshot.hpp"
#include "../src/FullStateProbe.hpp"
#include <cassert>
#include <iostream>
#include <fstream>
using namespace sc6::research;
struct Fixture {
    alignas(16) std::array<std::byte,0xA0> globals{};
    uintptr_t global{};uintptr_t base{};
    alignas(16) std::array<std::byte,0x400> manager{},table{},arena{};
    alignas(16) std::array<std::array<std::byte,0x400>,16> pools{};
    Fixture(){global=reinterpret_cast<uintptr_t>(manager.data());base=reinterpret_cast<uintptr_t>(globals.data())-0x470DE90;put(globals.data()+0x98,global);
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
int main(int argc,char** argv){
    auto f=std::make_unique<Fixture>();auto a=std::make_unique<FullRestoreStream>(),b=std::make_unique<FullRestoreStream>();
    CameraSlots sa,sb;std::array<unsigned,FullRestoreStream::transfer_limit> map;
    if(argc==3){
        auto read=[](const char* path,FullRestoreStream& stream){std::ifstream in(path,std::ios::binary);
            assert(in);in.read(reinterpret_cast<char*>(&stream),sizeof(stream));assert(in.gcount()==sizeof(stream));};
        read(argv[1],*a);read(argv[2],*b);
        constexpr uintptr_t module=140702265507840ULL;
        assert(sa.parse(*a,module) && sb.parse(*b,module));
        sa.valid=sb.valid=true;sa.manager=sb.manager=0x100000;sa.arena=sb.arena=0x200000;sa.table=sb.table=0x300000;
        const auto pool=sa.slots[0].pool;
        for(unsigned k=0;k<16;++k){sa.slots[k].pool=sb.slots[k].pool=pool+800*k;}
        CameraSlots::MapDiagnostic d;assert(sa.map(*a,sb,*b,map,&d));
        assert(map[1125]==CameraSlots::empty && map[1126]==CameraSlots::empty && map[1127]==1122);
        assert(sb.map(*b,sa,*a,map,&d) && map[1122]==1127);
        std::cout<<"Recovered Critical Edge streams: both mapping directions passed with exact non-slot alignment\n";return 0;
    }

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
    auto references=[&](FullRestoreStream& stream,bool present){
        --stream.writes;stream.stream.write_position-=4;stream.sealed=false;
        std::array<std::byte,0x2f0> table{};std::array<std::byte,0x41e0> arena{};
        const uintptr_t address=0x100000;Fixture::put(table.data()+8,address);
        for(unsigned k=0;k<17;++k){Fixture::put(table.data()+0x10+k*8,address+k*0x3e0);
            Fixture::put(arena.data()+k*0x3e0,f->base+(present && k==4?0x3e88858:0x3e88ba8));}
        f->append(stream,f->base+0x32492f,0x200000,static_cast<unsigned>(table.size()),table.data());
        f->append(stream,f->base+0x324942,address,static_cast<unsigned>(arena.size()),arena.data());
        uint64_t words[2]{};
        if(present){f->append(stream,f->base+0x32ac76,0x300000,16,words);f->append(stream,f->base+0x32acac,0x300010,8,words);}
        f->append(stream,f->base+0x324982,0x400000,4,words);
        f->append(stream,f->base+0x321861,0x400010,4,words);assert(stream.seal());
    };
    f->save(*a,0);references(*a,true);assert(sa.capture(*a,f->base));
    f->save(*b,8);references(*b,false);assert(sb.capture(*b,f->base));
    CameraSlots::MapDiagnostic diagnostic;
    assert(sa.map(*a,sb,*b,map,&diagnostic) && sb.map(*b,sa,*a,map,&diagnostic));
    const auto arenaOffset=sa.slots[15].offset+sa.slots[15].bytes+0x2f0;
    auto badVtable=f->base+0x3e80000;std::memcpy(a->stream.payload.data()+arenaOffset+4*0x3e0,&badVtable,8);
    assert(!sa.map(*a,sb,*b,map,&diagnostic) && diagnostic.reason==3);
    auto goodVtable=f->base+0x3e88858;std::memcpy(a->stream.payload.data()+arenaOffset+4*0x3e0,&goodVtable,8);
    uint64_t external=0x123456;std::memcpy(a->stream.payload.data()+arenaOffset+4*0x3e0+0x3b8,&external,8);
    assert(!sa.map(*a,sb,*b,map,&diagnostic) && diagnostic.reason==5);
    external=0;std::memcpy(a->stream.payload.data()+arenaOffset+4*0x3e0+0x3b8,&external,8);
    b->traces[b->writes-1].writer++;assert(!sa.map(*a,sb,*b,map,&diagnostic));b->traces[b->writes-1].writer--;
    assert(sa.map(*a,sb,*b,map,&diagnostic));
    // Native 322610/324320: auxiliary slot 4 borrows one battle player.
    // 3841E0/384540 relocate exact player roots through entries 1 and 2.
    auto aux=std::make_unique<std::array<std::byte,0x2F0>>();
    auto auxArena=std::make_unique<std::array<std::byte,0x41E0>>();
    auto playersMemory=std::make_unique<std::array<std::array<std::byte,0x973F0>,2>>();
    const uintptr_t players[]={reinterpret_cast<uintptr_t>((*playersMemory)[0].data()),reinterpret_cast<uintptr_t>((*playersMemory)[1].data())};
    const auto auxAddress=reinterpret_cast<uintptr_t>(aux->data()),arenaAddress=reinterpret_cast<uintptr_t>(auxArena->data());
    Fixture::put(f->manager.data()+0xA0,auxAddress);Fixture::put(aux->data()+8,arenaAddress);
    for(unsigned k=0;k<17;++k)Fixture::put(aux->data()+0x10+k*8,arenaAddress+k*0x3E0);
    for(unsigned side=0;side<2;++side)Fixture::put(f->globals.data()+side*8,players[side]);
    auto own=[&](FullRestoreStream& s,unsigned side,bool present){
        references(s,present);const auto first=s.writes-(present?6u:4u);
        unsigned offset=0;for(unsigned k=0;k<first;++k)offset+=static_cast<unsigned>(s.lengths[k]);
        auto* header=s.stream.payload.data()+offset;auto* area=header+0x2F0;
        std::memcpy(header,aux->data(),aux->size());s.traces[first].source=auxAddress;s.traces[first+1].source=arenaAddress;
        if(present){Fixture::put(area+4*0x3E0+0x3B8,players[side]);
            unsigned relocation=side+1;std::memcpy(area+0x41E0,&relocation,4);
            Fixture::put(area+0x41E0+16,0x12345678);}
        return offset;
    };
    for(unsigned side=0;side<2;++side){
        f->save(*a,8);const auto aoffset=own(*a,side,true);assert(sa.capture(*a,f->base,players));
        f->save(*b,0);own(*b,side,false);assert(sb.capture(*b,f->base,players));
        assert(sa.map(*a,sb,*b,map,&diagnostic) && sb.map(*b,sa,*a,map,&diagnostic));
        assert(map[b->writes-1]==a->writes-1);assert(sa.reference_identity_live());
        CameraSlotEvidence refEvidence;refEvidence.capture(sa,*a,sb,*b);
        assert(refEvidence.reference_header[0][0]==1 && refEvidence.references[0][4][1]==players[side] &&
               refEvidence.references[0][4][2]==side+1 && refEvidence.references[0][4][5]==1);
        auto* payload=a->stream.payload.data()+aoffset+0x2F0;auto* descriptor=payload+0x41E0;
        for(unsigned badIndex:{0u,3u,UINT32_MAX}){std::memcpy(descriptor,&badIndex,4);
            assert(!sa.map(*a,sb,*b,map,&diagnostic) && diagnostic.reason==5);}
        unsigned indexValue=side+1;std::memcpy(descriptor,&indexValue,4);
        unsigned wrongSide=2-side;std::memcpy(descriptor,&wrongSide,4);assert(!sa.map(*a,sb,*b,map,&diagnostic));
        std::memcpy(descriptor,&indexValue,4);
        Fixture::put(descriptor+8,1);assert(!sa.map(*a,sb,*b,map,&diagnostic) && diagnostic.reason==5);Fixture::put(descriptor+8,0);
        Fixture::put(payload+4*0x3E0+0x3B8,players[side]+8);assert(!sa.map(*a,sb,*b,map,&diagnostic));
        Fixture::put(payload+4*0x3E0+0x3B8,players[side]);
        if(side==1){
            // Recorded Azwel failure: native inclusive range selects P1's
            // endpoint for P2. Exercise both disappearance and appearance.
            assert(players[1]-players[0]==0x973F0);
            unsigned firstPlayer=1;std::memcpy(descriptor,&firstPlayer,4);
            Fixture::put(descriptor+8,0x973F0);
            assert(sa.map(*a,sb,*b,map,&diagnostic) && sb.map(*b,sa,*a,map,&diagnostic));
            refEvidence.capture(sa,*a,sb,*b);
            assert(refEvidence.references[0][4][5]==1);
            std::memcpy(auxArena->data(),payload,auxArena->size());
            Fixture::put(auxArena->data()+4*0x3E0+0x3B8,players[0]+0x973F0);
            std::array<uint64_t,5> relocated{};assert(sa.references_loaded(*a,relocated));
            for(uint64_t badOffset:{uint64_t{0x973EF},uint64_t{0x973F1},UINT64_MAX}){
                Fixture::put(descriptor+8,badOffset);
                assert(!sa.map(*a,sb,*b,map,&diagnostic) && diagnostic.reason==5);
            }
            // Even a self-consistent descriptor cannot authorize interior data.
            Fixture::put(descriptor+8,8);Fixture::put(payload+4*0x3E0+0x3B8,players[0]+8);
            assert(!sa.map(*a,sb,*b,map,&diagnostic) && diagnostic.reason==5);
            Fixture::put(payload+4*0x3E0+0x3B8,players[side]);
            std::memcpy(descriptor,&indexValue,4);Fixture::put(descriptor+8,0);
        }
        auto stale=sb;stale.reference_players[side]+=8;assert(!sa.map(*a,stale,*b,map,&diagnostic));
        stale=sb;stale.reference_arena+=8;assert(!sa.map(*a,stale,*b,map,&diagnostic));
        auto unownedA=sa,unownedB=sb;unownedA.reference_players=unownedB.reference_players={};
        unownedA.reference_owner=unownedB.reference_owner=unownedA.reference_arena=unownedB.reference_arena=0;
        assert(!unownedA.map(*a,unownedB,*b,map,&diagnostic) && diagnostic.reason==5);
        const auto first=sa.tail;a->traces[first+1].source+=8;
        assert(!sa.map(*a,sb,*b,map,&diagnostic));a->traces[first+1].source-=8;
        // Emulate the audited native arena copy and 32ACE0 relocation; the
        // post-load verifier must see exact saved references, not current ones.
        std::memcpy(auxArena->data(),payload,auxArena->size());
        Fixture::put(auxArena->data()+4*0x3E0+0x3B8,players[indexValue-1]);
        std::array<uint64_t,5> result{};assert(sa.references_loaded(*a,result) && result[0]==2);
        Fixture::put(auxArena->data()+4*0x3E0+0x3B8,players[1-side]);
        assert(!sa.references_loaded(*a,result) && result[1]==4 && result[2]==4);
        Fixture::put(auxArena->data()+4*0x3E0+0x3B8,players[side]);
        Fixture::put(f->globals.data()+side*8,players[side]+8);
        assert(!sa.live() && !sa.references_loaded(*a,result) && result[1]==1);
        Fixture::put(f->globals.data()+side*8,players[side]);
        Fixture::put(aux->data()+0x10,arenaAddress+8);assert(!sa.reference_identity_live());Fixture::put(aux->data()+0x10,arenaAddress);
        f->set(8);assert(sa.live());
        // A native load of the absent type removes the reference records again.
        const auto boffset=sb.slots[15].offset+sb.slots[15].bytes+0x2F0;
        std::memcpy(auxArena->data(),b->stream.payload.data()+boffset,auxArena->size());
        assert(sb.references_loaded(*b,result));
        Fixture::put(auxArena->data()+4*0x3E0,f->base+0x3E88858);
        assert(!sb.references_loaded(*b,result) && result[1]==3);
    }
    Fixture::put(f->manager.data()+0xA0,0);f->save(*a,0);assert(sa.capture(*a,f->base,players));
    assert(!sa.reference_owner && (sa.reference_players==std::array<uintptr_t,2>{}));
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
    std::cout<<"Camera slots: 100 type pairs, both player references and mapping directions, relocation bounds, stale roots/arenas, native-load reference verification, raw evidence and exact source guards passed\n";
}
