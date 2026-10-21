#include <source_location>
#include <xmmintrin.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "../src/FullStateProbe.hpp"
#include "../src/CombatBlend.hpp"
#include "../src/IdleTrajectoryAudit.hpp"
#include "../src/TwoFrameIdleReplay.hpp"
#include "../src/NativeBoundarySnapshot.hpp"
#include "../src/NativeAdjacentRestore.hpp"
#include "../src/NativeTwoFrameReplay.hpp"
#include "../src/NativePredictionProbe.hpp"
#include "../src/NativeTwoMovement.hpp"
#include "../src/RepeatedMovement.hpp"
#include <memory>
#include <cassert>
#include <cstdio>
#include <iostream>
#include <cstdlib>
using T=sc6::research::FullRestoreStream;
using P=sc6::research::FullStateProbe;
using S=sc6::research::SaveStream;
uintptr_t base;int mode;unsigned saves,loads,checks;
void require(bool v,const std::source_location where=std::source_location::current()){if(!v){std::fprintf(stderr,"Full restore test failed mode=%d line=%u\n",mode,where.line());std::exit(1);}}
bool check(uintptr_t){++checks;return mode!=1 && !(mode==7 && checks==3);}
void* save(S* s){++saves;if(mode==12 && saves==2)++*reinterpret_cast<unsigned*>(base+0x485EB28);s->slots->write(s,reinterpret_cast<void*>(base+P::key_rvas[1]),4);return s;}
void* load(S* s){
    ++loads;
    auto& rng=*reinterpret_cast<unsigned*>(base+0x485EB28);
    if(mode>=9){
        rng=P::advance_rng(rng);
        for(unsigned side=0;side<2;++side){
            auto player=P::ptr(base+0x470DE90+side*8);
            *reinterpret_cast<uintptr_t*>(player+0x43DF0)=player;
            std::memset(reinterpret_cast<void*>(player+0x97100),0x11,120);
            for(auto off:P::scalar_offsets)*reinterpret_cast<unsigned*>(player+0x971E8+off)=0x12345678;
            if(mode>=13 && mode<=16){
                *reinterpret_cast<unsigned*>(player+0x97204)=mode==15?2:1;
                *reinterpret_cast<unsigned*>(player+0x97220)=mode==16?1:0;
            }
            if(mode>=17){
                *reinterpret_cast<unsigned*>(player+0x97220)=(mode==19 || mode==27 || mode==33)?2:0;
                if(mode==22 || mode==30 || mode==36)*reinterpret_cast<unsigned*>(player+0x97204)=2;
            }
            if(mode>=38){
                *reinterpret_cast<unsigned*>(player+0x97204)=mode==39?2:1;
                if(mode==40)*reinterpret_cast<unsigned*>(player+0x97220)=1;
            }
            if(mode==10)*reinterpret_cast<unsigned*>(player+0x971F0)=1;
        }
        if(mode==11)rng=1;
    }else ++rng;
    if(mode==2){s->slots->read(s,reinterpret_cast<void*>(base+P::key_rvas[1]),UINT64_MAX);return s;}
    if(mode==3){s->slots->reset(s);return s;}
    if(mode==4)return s;
    if(mode==5){s->slots->read(s,nullptr,4);return s;}
    *reinterpret_cast<unsigned*>(base+P::key_rvas[1])=99;
    s->slots->read(s,reinterpret_cast<void*>(base+P::key_rvas[1]),4);
    if(mode==6)*reinterpret_cast<unsigned*>(base+P::key_rvas[1])=98;
    return s;
}
bool track_boundary_stack=false;
unsigned boundary_stack_count=0;
uintptr_t boundary_stack_addresses[8]{};
__declspec(noinline) void* trajectory_save(S* s){
    volatile unsigned stack_marker=123;
    if(track_boundary_stack && boundary_stack_count<8)boundary_stack_addresses[boundary_stack_count++]=reinterpret_cast<uintptr_t>(&stack_marker);
    save(s);
    constexpr unsigned sites[]={0x33FC68,0x33FC7B,0x33FC8E,0x33FE69,0x33FE7F,0x33FE95};
    constexpr unsigned offsets[]={0x50,0x60,0x70,0x110,0x120,0x130};
    constexpr unsigned sizes[]={16,16,4,16,4,16};
    auto* stream=T::owner(s);
    for(unsigned i=0;i<6;++i){
        s->slots->write(s,reinterpret_cast<void*>(base+0x600000+offsets[i]),sizes[i]);
        stream->traces[stream->writes-1].writer=base+sites[i];
    }
    return s;
}
void* delayed_save(S* s){++saves;for(auto off:P::key_rvas)s->slots->write(s,reinterpret_cast<void*>(base+off),4);return s;}
void* delayed_load(S* s){
    auto outer=*reinterpret_cast<unsigned*>(base+0x470D0C4);
    ++loads;for(auto off:P::key_rvas)s->slots->read(s,reinterpret_cast<void*>(base+off),4);
    if(mode>=100)*reinterpret_cast<unsigned*>(base+0x470D0C4)=outer+(mode==102?1:0);
    if(mode==101)++*reinterpret_cast<unsigned*>(base+P::key_rvas[1]);
    *reinterpret_cast<unsigned*>(base+0x485EB28)=P::advance_rng(*reinterpret_cast<unsigned*>(base+0x485EB28));
    for(unsigned side=0;side<2;++side){auto player=P::ptr(base+0x470DE90+side*8);
        *reinterpret_cast<uintptr_t*>(player+0x43DF0)=player;
        std::memset(reinterpret_cast<void*>(player+0x97100),0x11,120);
        for(auto off:P::scalar_offsets)*reinterpret_cast<unsigned*>(player+0x971E8+off)=0x12345678;
    }
    return s;
}
unsigned boundary_failure=0;
void* boundary_load(S* s){
    ++loads;auto* reader=T::owner(s);
    for(unsigned i=0;i<reader->writes-(boundary_failure==3?1u:0u);++i)s->slots->read(s,reinterpret_cast<void*>(reader->traces[i].source),reader->lengths[i]);
    auto& rng=*reinterpret_cast<unsigned*>(base+0x485EB28);rng=P::advance_rng(rng);if(boundary_failure==1)rng=1;
    for(unsigned side=0;side<2;++side){auto player=P::ptr(base+0x470DE90+side*8);
        *reinterpret_cast<uintptr_t*>(player+0x43DF0)=player;
        std::memset(reinterpret_cast<void*>(player+0x97100),0x11,120);
        for(auto off:P::scalar_offsets)*reinterpret_cast<unsigned*>(player+0x971E8+off)=0x12345678;
        auto owner=P::ptr(player+0x971F8);std::memset(reinterpret_cast<void*>(owner+0x4BC),0,140);
    }
    auto camera=base+0x600000;
    *reinterpret_cast<unsigned*>(camera+0x154)=0;
    *reinterpret_cast<unsigned*>(camera+0x1E0)=1;*reinterpret_cast<unsigned*>(camera+0x1E4)=1;
    std::memset(reinterpret_cast<void*>(camera+0x25C),0,72);
    for(unsigned off:{0x1D0u,0x1D4u,0x1D8u})*reinterpret_cast<float*>(camera+off)=0.375f;
    for(unsigned off:{0x250u,0x254u})*reinterpret_cast<float*>(camera+off)=1.0f;
    if(boundary_failure==2)*reinterpret_cast<unsigned*>(camera+0x1E0)=2;
    return s;
}
DWORD reject_case(S* s,int test){
    __try {
        unsigned x=123;
        if(test==0)s->slots->read(s,&x,4);
        else if(test==1)s->slots->write(s,&x,4);
        else if(test==2)s->slots->set_read(s,0);
        else if(test==3)s->slots->destroy(s,0);
        else if(test==4)s->slots->read(s,&x,2);
    }__except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}
    return 0;
}
// Source identity is distinct from archived bytes and ownership. Equal bytes
// from different stack slots must not silently authorize a native restore.
void source_identity_audit(){
    auto q=std::make_unique<P>();
    unsigned values[2]={123,123};
    T* captures[2]={&q->saved,&q->after_save};
    for(unsigned i=0;i<2;++i){
        auto& stream=captures[i]->stream;
        stream.slots->write(&stream,&values[i],sizeof(values[i]));
        require(captures[i]->seal());
    }
    require(q->saved.traces[0].writer==q->after_save.traces[0].writer);
    require(q->saved.traces[0].source!=q->after_save.traces[0].source);
    require(!std::memcmp(q->saved.stream.payload.data(),q->after_save.stream.payload.data(),sizeof(unsigned)));
    values[0]=999; // The capture owns the bytes, not this stack location.
    unsigned archived{};std::memcpy(&archived,q->saved.stream.payload.data(),sizeof(archived));require(archived==123);
    q->ring_save_unchanged=q->ring_rotation_ok=true;
    require(!q->delayed_sources_ok() && q->source_guard_reason==4 && q->source_guard_length==sizeof(unsigned));
    require(!q->restore_invoked && loads==0);
    std::cout<<"PASS: equal archived bytes from distinct stack sources remain rejected before native loading\n";
}
void audited_scalar_source_test(){
    auto q=std::make_unique<P>();
    constexpr uintptr_t module=0x140000000,owner=0x200000;
    constexpr uintptr_t writers[]={0x30ECBF,0x30ECD5,0x30ECEB,0x30ED01,0x30ED1F};
    constexpr uint64_t sizes[]={0x200,0x200,0x14,4,4};
    constexpr uintptr_t offsets[]={0,0x200,0x45C,0x470};
    q->saved.writes=q->after_save.writes=5;
    q->ring_save_unchanged=q->ring_rotation_ok=true;
    for(unsigned i=0;i<5;++i){
        q->saved.lengths[i]=q->after_save.lengths[i]=sizes[i];
        q->saved.traces[i].writer=q->after_save.traces[i].writer=module+writers[i];
        q->saved.traces[i].source=q->after_save.traces[i].source=i<4?owner+offsets[i]:0x300000;
    }
    q->after_save.traces[4].source=0x300210;
    require(q->delayed_sources_ok(module));
    require(!q->delayed_sources_ok() && !q->delayed_sources_ok(module+1));
    for(unsigned i=0;i<5;++i){
        ++q->after_save.traces[i].writer;require(!q->delayed_sources_ok(module));--q->after_save.traces[i].writer;
        ++q->after_save.lengths[i];require(!q->delayed_sources_ok(module));--q->after_save.lengths[i];
    }
    ++q->after_save.traces[2].source;require(!q->delayed_sources_ok(module));--q->after_save.traces[2].source;
    q->audit_shape_changes=1;require(!q->delayed_sources_ok(module));q->audit_shape_changes=0;
    require(q->delayed_sources_ok(module));
    require(q->saved.traces[4].source==0x300000 && q->after_save.traces[4].source==0x300210);
    require(!q->restore_invoked);
    std::cout<<"PASS: audited by-value scalar accepts relocated temporary only with exact writer and owner layout\n";
}
void audited_reference_source_test(){
    auto q=std::make_unique<P>();constexpr uintptr_t image_base=0x140000000;
    constexpr uintptr_t writers[]={0x30AB2F,0x30AB84,0x30ABDF,0x30AC34,0x30AC8F,0x30ACE4,0x30AD3F,0x30AD94,0x30ADEF};
    std::array<unsigned char,16> source{},next{},loaded{};source[0]=2;source[8]=17;next[0]=3;next[8]=29;
    for(unsigned i=0;i<9;++i){
        auto& a=q->saved.stream;auto& b=q->after_save.stream;
        a.slots->write(&a,source.data(),16);b.slots->write(&b,next.data(),16);
        q->saved.traces[i].writer=q->after_save.traces[i].writer=image_base+writers[i];
    }
    require(q->saved.seal() && q->after_save.seal());
    q->ring_save_unchanged=q->ring_rotation_ok=true;
    const auto archived=q->saved.stream.payload;
    require(q->delayed_sources_ok(image_base));
    require(!q->delayed_sources_ok() && !q->delayed_sources_ok(image_base+1));
    for(unsigned i=0;i<9;++i){
        require(q->audited_reference_source(i,image_base));
        ++q->after_save.traces[i].writer;require(!q->delayed_sources_ok(image_base));--q->after_save.traces[i].writer;
        ++q->after_save.lengths[i];require(!q->delayed_sources_ok(image_base));--q->after_save.lengths[i];
        ++q->after_save.traces[i].source;require(!q->delayed_sources_ok(image_base));--q->after_save.traces[i].source;
    }
    --q->after_save.writes;require(!q->delayed_sources_ok(image_base));++q->after_save.writes;
    q->audit_shape_changes=1;require(!q->delayed_sources_ok(image_base));q->audit_shape_changes=0;
    require(q->delayed_sources_ok(image_base));
    require(q->saved.stream.payload==archived);
    auto reader=std::make_unique<T>();require(reader->prepare_reader_from(q->saved));
    for(unsigned i=0;i<9;++i){reader->stream.slots->read(&reader->stream,loaded.data(),16);require(loaded==source);}
    require(q->saved.traces[0].source==reinterpret_cast<uintptr_t>(source.data()));
    require(q->after_save.traces[0].source==reinterpret_cast<uintptr_t>(next.data()));
    std::cout<<"PASS: all nine audited reference descriptors permit relocated temporary storage, retain exact payload and reject malformed groups\n";
}
void additional_temporary_sources_test(){
    constexpr uintptr_t image_base=0x140000000;
    const std::vector<std::vector<uintptr_t>> groups={
        {0x31DCF1,0x31DD48,0x31DDA1,0x31DDF5}, {0x301A75,0x301AD2},
        {0x390765,0x3907B6,0x390765,0x3907B6},
        {0x317C17},{0x317C60},{0x340F59},{0x341183},{0x3217EC},{0x321814},{0x32AC76},{0x32ACAC}};
    for(unsigned g=0;g<groups.size();++g){
        auto q=std::make_unique<P>();const auto& writers=groups[g];
        const unsigned size=g<3 || g==9?16:g==10?8:4;
        std::array<unsigned char,16> a{},b{},loaded{};a[0]=7;b[0]=9;a[size-1]=21;b[size-1]=37;
        for(unsigned i=0;i<writers.size();++i){
            q->saved.stream.slots->write(&q->saved.stream,a.data(),size);
            q->after_save.stream.slots->write(&q->after_save.stream,b.data(),size);
            q->saved.traces[i].writer=q->after_save.traces[i].writer=image_base+writers[i];
        }
        require(q->saved.seal() && q->after_save.seal());
        q->ring_save_unchanged=q->ring_rotation_ok=true;
        const auto bytes=q->saved.stream.payload;
        require(q->delayed_sources_ok(image_base));
        require(!q->delayed_sources_ok() && !q->delayed_sources_ok(image_base+1));
        require(!q->delayed_sources_ok(UINTPTR_MAX-1));
        for(unsigned i=0;i<writers.size();++i){
            require(q->audited_additional_temporary_source(i,image_base));
            ++q->after_save.traces[i].writer;require(!q->delayed_sources_ok(image_base));--q->after_save.traces[i].writer;
            ++q->after_save.lengths[i];require(!q->delayed_sources_ok(image_base));--q->after_save.lengths[i];
            const auto source=q->after_save.traces[i].source;
            q->after_save.traces[i].source=UINTPTR_MAX;require(!q->delayed_sources_ok(image_base));
            q->after_save.traces[i].source=1;require(!q->delayed_sources_ok(image_base));
            q->after_save.traces[i].source=source;
            if(g<3){++q->after_save.traces[i].source;require(!q->delayed_sources_ok(image_base));--q->after_save.traces[i].source;}
        }
        if(g<3){--q->after_save.writes;require(!q->delayed_sources_ok(image_base));++q->after_save.writes;}
        q->audit_shape_changes=1;require(!q->delayed_sources_ok(image_base));q->audit_shape_changes=0;
        q->ring_rotation_ok=false;require(!q->delayed_sources_ok(image_base));q->ring_rotation_ok=true;
        require(q->delayed_sources_ok(image_base) && q->saved.stream.payload==bytes && !q->restore_invoked);
        auto reader=std::make_unique<T>();require(reader->prepare_reader_from(q->saved));
        for(unsigned i=0;i<writers.size();++i){reader->stream.slots->read(&reader->stream,loaded.data(),size);require(!std::memcmp(loaded.data(),a.data(),size));}
    }
    std::cout<<"PASS: audited global/selector/pair descriptors and camera/type temporaries retain exact bytes and reject unknown writers, sizes, groups and invalid addresses\n";
}
void native_load_route_test(){
    using sc6::research::NativeLoadRoute;
    constexpr uintptr_t image=0x140000000,manager=0x500000,optional=0x600000;
    auto stream=std::make_unique<T>();NativeLoadRoute::Diagnostic d;
    constexpr uintptr_t writers[]={0x391DEA,0x3906CC,0x38448D,0x321861,0x384403};
    constexpr uint64_t sizes[]={2,0xC60,4,4,0x360};
    for(bool alternate:{false,true})for(bool present:{false,true}){
        stream->sealed=true;stream->writes=present?5:4;
        const uintptr_t sources[]={image+(alternate?0x4845E80:0x4844DD0)+0x414,
            image+(alternate?0x4845220:0x4844170),image+(alternate?0x48462C8:0x4845218),manager+0xA8,optional};
        for(unsigned i=0;i<5;++i){stream->traces[i].writer=image+writers[i];stream->traces[i].source=sources[i];stream->lengths[i]=sizes[i];}
        auto test=[&](){return NativeLoadRoute::validate(*stream,image,alternate,present?optional:0,manager,d);};
        require(test());
        require(!NativeLoadRoute::validate(*stream,image,!alternate,present?optional:0,manager,d) && d.reason==3);
        require(!NativeLoadRoute::validate(*stream,image,alternate,present?optional:0,manager+8,d) && d.reason==3);
        require(!NativeLoadRoute::validate(*stream,image,alternate,present?0:optional,manager,d));
        for(unsigned i=0;i<stream->writes;++i){
            ++stream->traces[i].source;require(!test() && d.reason==3 && d.record==i);--stream->traces[i].source;
            ++stream->lengths[i];require(!test() && d.reason==2);--stream->lengths[i];
            ++stream->traces[i].writer;require(!test() && d.reason==4);--stream->traces[i].writer;
        }
        const auto count=stream->writes;stream->traces[count]=stream->traces[0];stream->lengths[count]=sizes[0];
        ++stream->writes;require(!test() && d.reason==2);stream->writes=count;
        require(test());stream->sealed=false;require(!test() && d.reason==1);
    }
    require(!NativeLoadRoute::ready(*stream,0,d) && d.reason==1);
    require(!NativeLoadRoute::validate(*stream,UINTPTR_MAX,true,optional,manager,d));
    std::cout<<"PASS: audited native destination routing rejects mode/manager/optional-buffer changes before load\n";
}
int main(){
    native_load_route_test();
    additional_temporary_sources_test();
    audited_reference_source_test();
    audited_scalar_source_test();
    {
        namespace rt=sc6::research::rng_trace;
        unsigned seed=10,counter=42,calls=0;
        {rt::Scope phase(2);
            const auto value=rt::observe([&](){++calls;seed=20;SetLastError(123);return 0.625f;},&seed,&counter,0xABC);
            require(value==0.625f && calls==1 && seed==20 && counter==42 && GetLastError()==123);
            {rt::Scope inner(1);require(rt::phase==1);}require(rt::phase==2);
        }
        require(rt::phase==0);
        auto snapshot=std::make_unique<rt::Snapshot>();rt::snapshot(*snapshot);
        require(snapshot->count==1 && snapshot->events[0].phase==2 && snapshot->events[0].before==10 && snapshot->events[0].after==20 && snapshot->events[0].caller==0xABC && snapshot->events[0].frame==42);
        AcquireSRWLockExclusive(&rt::trace_lock);rt::record(0,0,0,0);ReleaseSRWLockExclusive(&rt::trace_lock);
        for(unsigned i=0;i<8200;++i)rt::record(i,1,2,3);
        rt::snapshot(*snapshot);require(snapshot->count==8192 && snapshot->total==8201 && snapshot->dropped==1);
    }

    {
        auto player=std::make_unique<std::array<std::byte,0x973E8>>();
        (*player)[0x97268+0x40]=std::byte{123};
        P::Measurement m{};m.valid=true;
        m.players[0]=reinterpret_cast<uintptr_t>(player->data());m.players[1]=1;
        P::BlendTimerSample sample{};
        P::read_blend_timers(sample,m,5);
        assert(sample.phase==5 && sample.valid_mask==7);
        assert(sample.slots[0][0][0x40]==std::byte{123});
        assert((*player)[0x97268+0x40]==std::byte{123});
        m.players[1]=m.players[0];
        P::read_blend_timers(sample,m,0);
        auto probe=std::make_unique<P>();probe->measurements[0]=m;
        probe->blend_timer_source=sample;
        // Both players refer to the same test allocation only in this unit fixture.
        for(unsigned side=0;side<2;++side){
            unsigned active=2;std::memcpy(probe->blend_timer_source.slots[side][0].data()+0x48,&active,4);
        }
        unsigned active=2,cursor=0x40A00000;std::memcpy(player->data()+0x972B0,&active,4);
        std::memcpy(player->data()+0x972A8,&cursor,4);
        probe->measurements[2]=m;
        P::read_blend_timers(probe->blend_timer_next,m,0);
        assert(probe->timer_transition_ready() && probe->timer_endpoint_ready());
        auto transition=probe->blend_timer_next;
        transition.slots[0][0][8]=std::byte{9};
        probe->blend_timer_next=transition;
        assert(!probe->timer_transition_ready() && probe->timer_diagnostic.reason==2 && probe->timer_diagnostic.offset==8);
        assert(!probe->timer_endpoint_ready() && probe->timer_diagnostic.phase==2);
        P::read_blend_timers(probe->blend_timer_next,m,0);
        assert(probe->restore_blend_timers() && probe->blend_timer_restored);
        assert((*player)[0x972A8]==std::byte{123});
        (*player)[0x97268+8]=std::byte{1};
        std::memcpy(player->data()+0x972A8,&cursor,4);
        assert(!probe->restore_blend_timers());
        unsigned unchanged{};std::memcpy(&unchanged,player->data()+0x972A8,4);assert(unchanged==cursor);
        auto invalid=probe->blend_timer_source;invalid.valid_mask=0;
        assert(!P::timer_pair(invalid,probe->blend_timer_source));
        invalid=probe->blend_timer_source;unsigned nan=0x7FC00000;
        std::memcpy(invalid.slots[0][0].data()+0x40,&nan,4);
        assert(!P::timer_pair(invalid,probe->blend_timer_source));
        m.valid=false;P::read_blend_timers(sample,m,6);
        assert(sample.valid_mask==0 && sample.phase==6);
    }
    {
        auto p=std::make_unique<P>();
        auto players=std::make_unique<std::array<std::array<std::byte,0x973E8>,2>>();
        auto put=[](auto& v,unsigned off,unsigned value){std::memcpy(v.data()+off,&value,4);};
        p->blend_timer_source.valid_mask=63;
        auto& a=p->blend_timer_source.slots[1][0];
        uintptr_t header=0x100000,descriptors=header+0x40,data=0x200000;
        std::memcpy(a.data(),&header,8);std::memcpy(a.data()+8,&descriptors,8);std::memcpy(a.data()+16,&data,8);
        put(a,0x1C,524);put(a,0x3C,207);put(a,0x44,0x3F800000);put(a,0x48,1);
        auto next=p->blend_timer_source;put(next.slots[1][0],0x3C,204);put(next.slots[1][0],0x40,0x40800000);put(next.slots[1][0],0x48,2);
        p->blend_timer_next=next;assert(p->timer_transition_ready());
        P::TimerDiagnostic d{};assert(!P::timer_compatible(p->blend_timer_source,next,d,2));
        for(unsigned off:{0u,8u,16u,0x1Cu,0x38u,0x44u,0x4Cu,0x58u,0x68u}){auto bad=next;bad.slots[1][0][off]^=std::byte{1};assert(!P::timer_pair(p->blend_timer_source,bad));}
        for(unsigned v:{524u,0xFFFFFFFFu}){auto bad=next;put(bad.slots[1][0],0x3C,v);assert(!P::timer_pair(p->blend_timer_source,bad));}
        for(unsigned v:{0u,0x40000000u,0x80000000u,0x7F800000u,0x7FC00000u}){auto bad=next;put(bad.slots[1][0],0x40,v);assert(!P::timer_pair(p->blend_timer_source,bad));}
        auto bad_source=p->blend_timer_source;put(bad_source.slots[1][0],0x3C,524);assert(!P::timer_pair(bad_source,next));
        put(bad_source.slots[1][0],0x3C,207);put(bad_source.slots[1][0],0x58,1);auto bad_next=next;put(bad_next.slots[1][0],0x58,1);assert(!P::timer_pair(bad_source,bad_next));
        // Captured 45352 transition: active selector 211 -> 200, cursor 11 -> 4.
        put(a,0x3C,211);put(a,0x40,1093664768u);put(a,0x48,2);
        next=p->blend_timer_source;put(next.slots[1][0],0x3C,200);put(next.slots[1][0],0x40,1082130432u);
        p->blend_timer_next=next;assert(p->timer_transition_ready());
        assert(!P::timer_compatible(p->blend_timer_source,next,d,2));
        for(unsigned state:{0u,3u,0xFFFFFFFFu}){auto bad=p->blend_timer_source;put(bad.slots[1][0],0x48,state);assert(!P::timer_pair(bad,next));}
        for(unsigned cursor:{0u,0x80000000u,0x7F800000u,0x7FC00000u}){auto bad=p->blend_timer_source;put(bad.slots[1][0],0x40,cursor);assert(!P::timer_pair(bad,next));}
        for(unsigned off:{0u,8u,16u,0x1Cu,0x38u,0x44u,0x4Cu,0x58u,0x68u}){auto bad=next;bad.slots[1][0][off]^=std::byte{1};assert(!P::timer_pair(p->blend_timer_source,bad));}
        for(unsigned selector:{524u,0xFFFFFFFFu}){auto bad=next;put(bad.slots[1][0],0x3C,selector);assert(!P::timer_pair(p->blend_timer_source,bad));}
        p->measurements[0].valid=p->measurements[2].valid=true;
        for(unsigned side=0;side<2;++side){p->measurements[0].players[side]=p->measurements[2].players[side]=reinterpret_cast<uintptr_t>((*players)[side].data());
            for(unsigned i=0;i<3;++i)std::memcpy((*players)[side].data()+0x97268+i*128,next.slots[side][i].data(),128);}
        assert(p->timer_endpoint_ready() && p->restore_blend_timers());
        P::BlendTimerSample restored{};P::read_blend_timers(restored,p->measurements[0],0);assert(restored.slots==p->blend_timer_source.slots);
        // Reject stale resource identity before touching any of the six slots.
        for(unsigned side=0;side<2;++side)for(unsigned i=0;i<3;++i)std::memcpy((*players)[side].data()+0x97268+i*128,next.slots[side][i].data(),128);
        (*players)[1][0x97268+8]^=std::byte{1};const auto before=std::make_unique<std::array<std::array<std::byte,0x973E8>,2>>(*players);assert(!p->restore_blend_timers() && *players==*before);
    }
    {
        auto p=std::make_unique<P>();
        auto players=std::make_unique<std::array<std::array<std::byte,0x973E8>,2>>();
        p->measurements[0].valid=p->measurements[2].valid=true;
        for(unsigned side=0;side<2;++side){
            p->measurements[0].players[side]=p->measurements[2].players[side]=reinterpret_cast<uintptr_t>((*players)[side].data());
        }
        auto put=[](auto& slot,unsigned offset,unsigned value){std::memcpy(slot.data()+offset,&value,4);};
        p->blend_timer_source.valid_mask=63;
        auto& slot=p->blend_timer_source.slots[1][0];
        put(slot,0x40,0x42600000);put(slot,0x44,0x3F800000);put(slot,0x48,2);
        auto next=p->blend_timer_source;put(next.slots[1][0],0x40,0);put(next.slots[1][0],0x48,1);
        p->blend_timer_next=next;
        assert(p->timer_transition_ready());
        P::TimerDiagnostic d{};
        assert(!P::timer_compatible(p->blend_timer_source,next,d,2)); // endpoint may not complete again
        assert(!P::timer_pair(next,p->blend_timer_source)); // no arbitrary activation
        for(unsigned offset:{8u,0x10u,0x38u,0x44u,0x4Cu,0x58u,0x68u}){
            auto bad=next;bad.slots[1][0][offset]^=std::byte{1};
            assert(!P::timer_pair(p->blend_timer_source,bad));
        }
        for(unsigned value:{1u,0x80000000u,0x7F800000u,0x7FC00000u}){
            auto bad=next;put(bad.slots[1][0],0x40,value);assert(!P::timer_pair(p->blend_timer_source,bad));
        }
        for(unsigned value:{0u,0x80000000u,0x7F800000u,0x7FC00000u}){
            auto bad=p->blend_timer_source;put(bad.slots[1][0],0x40,value);assert(!P::timer_pair(bad,next));
        }
        for(unsigned offset:{0x58u,0x5Cu}){
            auto a=p->blend_timer_source,b=next;put(a.slots[1][0],offset,1);put(b.slots[1][0],offset,1);
            assert(!P::timer_pair(a,b));
        }
        for(unsigned side=0;side<2;++side)for(unsigned i=0;i<3;++i)
            std::memcpy((*players)[side].data()+0x97268+i*128,next.slots[side][i].data(),128);
        assert(p->timer_endpoint_ready());
        (*players)[1][0x97268+2*128+8]=std::byte{1}; // validate every slot before any write
        assert(!p->restore_blend_timers());
        assert(sc6::research::CombatBlend::word((*players)[1].data()+0x972B0)==1);
        (*players)[1][0x97268+2*128+8]=std::byte{};
        assert(p->restore_blend_timers());
        assert(sc6::research::CombatBlend::word((*players)[1].data()+0x972A8)==0x42600000);
        assert(sc6::research::CombatBlend::word((*players)[1].data()+0x972B0)==2);
    }
    {
        using R=sc6::research::NativeAdjacentRestore;
        auto a=std::make_unique<P>();auto b=std::make_unique<P>();R::PayloadDiagnostic d;
        auto& x=a->saved;auto& y=b->restored;x.sealed=y.sealed=true;x.writes=y.writes=20;
        x.stream.write_position=y.stream.write_position=80;
        for(unsigned i=0;i<20;++i){x.lengths[i]=y.lengths[i]=4;x.traces[i].writer=y.traces[i].writer=0x1000+i;
            x.traces[i].source=0x2000+i;y.traces[i].source=0x3000+i;
            y.stream.payload[i*4+1]=std::byte{7};y.stream.payload[i*4+3]=std::byte{9};}
        require(!R::native_equal(*a,*b,0,&d));
        require(d.records==20 && d.bytes==40 && d.retained==16 && d.overflow==4 && !d.structural);
        require(d.details[15][0]==15 && d.details[15][2]==0x100f && d.details[15][3]==0x200f && d.details[15][4]==0x300f);
        require(d.details[15][5]==1 && d.details[15][6]==3 && d.details[15][7]==0 && d.details[15][8]==7);
        for(unsigned i=0;i<80;++i)y.stream.payload[i]=std::byte{};
        require(R::native_equal(*a,*b,0,&d) && !d.records && !d.bytes && !d.retained && !d.overflow);
        y.lengths[19]=3;require(!R::native_equal(*a,*b,0,&d) && d.structural==1);
        y.lengths[19]=4;x.stream.write_position=y.stream.write_position=79;
        require(!R::native_equal(*a,*b,0,&d) && d.structural==3);
    }

    {
        using R=sc6::research::NativeAdjacentRestore;
        auto a=std::make_unique<P>(),b=std::make_unique<P>();R::PayloadDiagnostic d;
        constexpr uintptr_t image_base=0x10000000,player=0x20000000;
        auto& x=a->saved;auto& y=b->restored;
        x.writes=y.writes=2;x.sealed=y.sealed=true;
        x.lengths[0]=y.lengths[0]=0x3510;x.lengths[1]=y.lengths[1]=0x11F0;
        x.stream.write_position=y.stream.write_position=0x4700;
        a->before_players[0]=player;
        x.traces[0].source=y.traces[0].source=player+0x90;
        x.traces[0].writer=y.traces[0].writer=image_base+0x30A9CE;
        x.traces[1].source=y.traces[1].source=player+0x95FA0;
        x.traces[1].writer=y.traces[1].writer=image_base+0x30AA96;
        b->saved.writes=2;b->saved.traces[1].reader=image_base+0x30AFD8;
        b->restore_completed=b->general_combat_requested=true;
        unsigned tiny=0x34AF0A3D;int16_t slot=20; // Bilateral MatrixUpdate record357.
        for(auto* stream:{&x,&y}){
            std::memcpy(stream->stream.payload.data()+0x2020-0x90,&slot,2);
            std::memcpy(stream->stream.payload.data()+0x2030-0x90,&tiny,4);
        }
        for(unsigned j=0;j<15;++j)std::memcpy(x.stream.payload.data()+0x3510+0x1120+j*4,&tiny,4);
        require(!R::native_equal(*a,*b,image_base,&d) && d.bytes==60 && !d.deferred_bytes);
        b->defer_inactive_animation_weights=true;
        require(R::native_equal(*a,*b,image_base,&d) && d.deferred_bytes==60 && !d.records);
        // Deferral must never accept these same 60 bytes at a replay endpoint.
        b->after_save=x;b->replay=y;
        sc6::research::TwoFrameIdleReplay::NativeDifference endpoint;
        require(!sc6::research::TwoFrameIdleReplay::native_equal(*b,image_base,&endpoint));
        require(endpoint.record==1 && endpoint.offset==0x1120);
        b->replay=x;
        require(sc6::research::TwoFrameIdleReplay::native_equal(*b,image_base,&endpoint));
        // Active masks, unknown writes, changed control, and unrelated bytes remain failures.
        y.stream.payload[0x3510+0x1118]=std::byte{1};require(!R::native_equal(*a,*b,image_base,&d));
        y.stream.payload[0x3510+0x1118]=std::byte{};
        y.stream.payload[0x2030-0x90]=std::byte{};require(!R::native_equal(*a,*b,image_base,&d));
        std::memcpy(y.stream.payload.data()+0x2030-0x90,&tiny,4);
        y.stream.payload[0x3510+0x115c]=std::byte{1};require(!R::native_equal(*a,*b,image_base,&d) && d.bytes==1 && d.deferred_bytes==60);
        y.stream.payload[0x3510+0x115c]=std::byte{};
        b->saved.traces[1].reader++;require(!R::native_equal(*a,*b,image_base,&d));
    }
    {
        using R=sc6::research::NativeAdjacentRestore;
        auto a=std::make_unique<sc6::research::FullStateProbe>();
        auto b=std::make_unique<sc6::research::FullStateProbe>();
        std::array<std::byte,0x41E0> live{};
        const uintptr_t flag_base=0x140000000,source=reinterpret_cast<uintptr_t>(live.data());
        auto& x=a->saved;auto& y=b->restored;
        x.sealed=y.sealed=true;x.writes=y.writes=1;
        x.lengths[0]=y.lengths[0]=live.size();
        x.stream.write_position=y.stream.write_position=live.size();
        x.traces[0].writer=y.traces[0].writer=flag_base+0x324942;
        x.traces[0].source=y.traces[0].source=source;
        uintptr_t vt=flag_base+0x3E88858;
        std::memcpy(x.stream.payload.data()+0xF80,&vt,8);
        std::memcpy(y.stream.payload.data(),x.stream.payload.data(),live.size());
        y.stream.payload[0x1350]=std::byte{1};
        std::memcpy(live.data(),y.stream.payload.data(),live.size());
        b->restore_completed=true;
        R::PayloadDiagnostic d;
        require(!R::native_equal(*a,*b,flag_base,&d));
        require(b->throw_camera_flag.restore_general(x,y,flag_base));
        require(R::native_equal(*a,*b,flag_base,&d));
        require(y.stream.payload[0x1350]==std::byte{1}); // raw evidence preserved
        live[0x1350]=std::byte{1};require(!R::native_equal(*a,*b,flag_base,&d));
        live[0x1350]=std::byte{0};
        live[12]=std::byte{1};require(!R::native_equal(*a,*b,flag_base,&d));
        live[12]=std::byte{0};
        y.stream.payload[12]=std::byte{1};require(!R::native_equal(*a,*b,flag_base,&d));
        y.stream.payload[12]=std::byte{0};
        b->throw_camera_flag.verified=false;require(!R::native_equal(*a,*b,flag_base,&d));
    }

    {
        using TS=sc6::research::TransformSnapshot;


        auto a=std::make_unique<TS>();auto b=std::make_unique<TS>();a->valid=b->valid=true;a->count=3;b->count=2;
        a->records[0].address=b->records[0].address=0x100000001ULL;
        for(unsigned i=1;i<3;++i){auto& r=a->records[i];r.address=0x200000000ULL+i*32;r.side=2;r.kind=4;r.size=24;r.parent=0;
            uintptr_t vt=0x100003000ULL;std::memcpy(r.bytes.data(),&vt,8);}
        b->records[1]=a->records[2];
        auto d=a->topology_difference(*b,0x100000000ULL);
        require(d.removed_nodes==1 && d.added_nodes==0 && d.removed_node[0]==a->records[1].address && d.removed_node[1]==0x3000 && d.removed_node[3]==0x100000001ULL);
        d=b->topology_difference(*a,0x100000000ULL);require(d.added_nodes==1 && d.removed_nodes==0);
        b->count=3;b->records[2]=a->records[1];d=a->topology_difference(*b);require(d.mask!=0 && d.added_nodes==0 && d.removed_nodes==0);
        b->count=1;a->count=5;
        for(unsigned i=3;i<5;++i){auto& r=a->records[i];r.side=2;r.kind=24;r.size=456;r.parent=i-2;r.address=a->records[i-2].address+24;}
        d=b->topology_difference(*a,0x100000000ULL);
        require(d.added_nodes==2 && d.removed_nodes==0);
        for(unsigned i=0;i<2;++i){const auto& node=d.added_details[i];
            require(node[0]==a->records[i+1].address && node[1]==0x3000 && node[4]==1);
            require(node[5]==a->records[i+3].address && node[6]==24 && node[7]==456 && node[8]==456);
        }
        d=a->topology_difference(*b,0x100000000ULL);require(d.removed_details[1][8]==456);
        a->count=static_cast<unsigned>(a->records.size())+1;d=a->topology_difference(*b);require((d.mask&256)!=0 && d.removed_nodes==0);
    }
    {
        auto p=std::make_unique<P>();
        require(!p->combat_blend_ready() && p->blend_pair_diagnostic.reason==1);
        p->blends[0].valid=p->blends[1].valid=true;
        require(p->combat_blend_ready());
        p->blends[1].owners[1]=123;
        require(!p->combat_blend_ready() && p->blend_pair_diagnostic.reason==2 && p->blend_pair_diagnostic.side==1);
        p->blends[1].owners[1]=0;
        // Invalid but unchanged values must still be reported (not only diffs).
        p->blends[0].bytes[0][0]=p->blends[1].bytes[0][0]=std::byte{1};
        require(!p->combat_blend_ready() && p->blend_pair_diagnostic.reason==5 && p->blend_pair_diagnostic.offset==0 && p->blend_pair_diagnostic.saved==1 && p->blend_pair_diagnostic.next==1 && p->blend_pair_diagnostic.changed==0 && p->blend_pair_evidence.valid);
        p->blends[0].bytes[0][0]=p->blends[1].bytes[0][0]=std::byte{};
        p->blends[1].bytes[1][140+60]=std::byte{2};
        require(!p->combat_blend_ready() && p->blend_pair_diagnostic.reason==6 && p->blend_pair_diagnostic.side==1 && p->blend_pair_diagnostic.offset==200 && p->blend_pair_diagnostic.next==2 && p->blend_pair_evidence.next[200]==std::byte{2});
        p->blends[1].bytes[1][200]=std::byte{};
        p->blends[0].bytes[0][168]=p->blends[1].bytes[0][168]=std::byte{8};
        require(!p->combat_blend_ready() && p->blend_pair_diagnostic.reason==7 && p->blend_pair_diagnostic.offset==168);
        p->blends[0].bytes[0][168]=p->blends[1].bytes[0][168]=std::byte{};
        p->charge_requested=true;
        require(!p->combat_blend_ready() && p->blend_pair_diagnostic.reason==8);
        p->measurements[0].valid=true;
        require(p->combat_blend_ready() && p->blend_pair_diagnostic.reason==0 && !p->blend_pair_evidence.valid);
    }
    source_identity_audit();
    base=reinterpret_cast<uintptr_t>(VirtualAlloc(nullptr,0x4900000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));require(base!=0);
    {
        using Boundary=sc6::research::EffectRngBoundary;
        static_assert(sizeof(Boundary)==548);
        auto a=std::make_unique<sc6::research::TransformSnapshot>();a->valid=true;a->count=2;
        for(unsigned i=0;i<2;++i){auto& r=a->records[i];r.side=2;r.kind=10+i;r.address=base+(i?0x470E2B0:0x485EB30);r.size=i?24:104;}
        auto b=std::make_unique<sc6::research::TransformSnapshot>(*a);
        unsigned next=1;std::memcpy(b->records[0].bytes.data()+100,&next,4);
        for(unsigned changed=0;changed<33;++changed){
            std::memset(reinterpret_cast<void*>(base+0x485EB30),0,104);
            std::memset(reinterpret_cast<void*>(base+0x470E2B0),0,24);
            auto address=base+(changed<26?0x485EB30+changed*4:0x470E2B0+(changed-26)*4);
            if(changed<32)*reinterpret_cast<unsigned*>(address)=1;
            Boundary d;require(d.before(*a,*b,base)==(changed==32));
            if(changed<32)require(*reinterpret_cast<unsigned*>(address)==1 && d.check==3);
            else {require(d.entry_match);d.finish(base);require(d.after_valid && !d.after_match);
                *reinterpret_cast<unsigned*>(base+0x485EB94)=1;d.finish(base);require(d.after_match);}
        }
        Boundary::State state;
        a->records[1].address+=4;require(!Boundary::archived(*a,base,state));a->records[1].address-=4;
        a->count=1;require(!Boundary::archived(*a,base,state));a->count=2;
        unsigned invalid=26;std::memcpy(a->records[0].bytes.data()+100,&invalid,4);require(!Boundary::archived(*a,base,state));
        bool called=false;require(!sc6::research::verify_replay_effects(false,[&]{called=true;return true;}));require(called);
        require(!sc6::research::verify_replay_effects(true,[]{return false;}));
        require(sc6::research::verify_replay_effects(true,[]{return true;}));
        std::memset(reinterpret_cast<void*>(base+0x485EB30),0,104);std::memset(reinterpret_cast<void*>(base+0x470E2B0),0,24);
        std::puts("PASS: both shared RNG states checked before replay; all 32 word mismatches reject without writes; independent effect comparison retained");
    }

    {
        auto stream=std::make_unique<T>();sc6::research::CameraSnapshot camera;
        constexpr unsigned sites[]={0x33FC68,0x33FC7B,0x33FC8E,0x33FE69,0x33FE7F,0x33FE95};
        constexpr unsigned offsets[]={0x50,0x60,0x70,0x110,0x120,0x130};
        constexpr unsigned sizes[]={16,16,4,16,4,16};
        auto owner=base+0x600000;*reinterpret_cast<unsigned*>(owner+0x60)=123;
        stream->sealed=true;stream->writes=6;
        for(unsigned i=0;i<6;++i){stream->traces[i].source=owner+offsets[i];stream->traces[i].writer=base+sites[i];stream->lengths[i]=sizes[i];}
        require(camera.capture(*stream,base) && camera.count==1 && camera.records[0].owner==owner);
        unsigned value{};std::memcpy(&value,camera.records[0].bytes.data()+0x60,4);require(value==123);
        require(!camera.records[0].smoothing_valid);
        *reinterpret_cast<uintptr_t*>(owner)=base+0x3E87EB0;
        *reinterpret_cast<unsigned*>(owner+0x1E0)=7;
        *reinterpret_cast<unsigned*>(owner+0x25C)=1234;
        *reinterpret_cast<unsigned*>(owner+0x2A0)=15;
        for(unsigned j=0;j<116;++j)*reinterpret_cast<unsigned char*>(owner+0x1E8+j)=static_cast<unsigned char>(j+1);
        for(unsigned j=0;j<12;++j)*reinterpret_cast<unsigned char*>(owner+0x2A4+j)=static_cast<unsigned char>(j+117);
        for(unsigned j=0;j<12;++j)*reinterpret_cast<unsigned char*>(owner+0x1D0+j)=static_cast<unsigned char>(j+31);
        require(camera.capture(*stream,base) && camera.records[0].smoothing_valid);
        for(unsigned j=0;j<12;++j)require(camera.records[0].response[j]==static_cast<std::byte>(j+31));
        for(unsigned j=0;j<128;++j)require(camera.records[0].targets[j]==static_cast<std::byte>(j+1));
        std::memcpy(&value,camera.records[0].smoothing.data(),4);require(value==7);
        std::memcpy(&value,camera.records[0].smoothing.data()+8,4);require(value==1234);
        std::memcpy(&value,camera.records[0].smoothing.data()+76,4);require(value==15);
        auto ca=std::make_unique<sc6::research::CameraSnapshot>(),cb=std::make_unique<sc6::research::CameraSnapshot>();
        *reinterpret_cast<unsigned*>(owner+0x1E0)=0;
        *reinterpret_cast<unsigned*>(owner+0x154)=0x40600000;
        *reinterpret_cast<unsigned*>(owner+0x2AC)=0; // valid two-player index after raw capture-layout test
        *reinterpret_cast<unsigned*>(owner+0x29C)=20;
        std::memset(reinterpret_cast<void*>(owner+0x1E8),0,116);
        for(unsigned off:{0x230u,0x234u,0x238u,0x240u,0x244u,0x248u})*reinterpret_cast<float*>(owner+off)=2.5f;
        *reinterpret_cast<float*>(owner+0x1D0)=0.0f;
        *reinterpret_cast<float*>(owner+0x1D4)=0.375f;
        *reinterpret_cast<float*>(owner+0x1D8)=0.0f;
        require(ca->capture(*stream,base));
        *reinterpret_cast<unsigned*>(owner+0x29C)=21;
        *reinterpret_cast<unsigned*>(owner+0x2A0)=0;
        require(cb->capture(*stream,base));
        std::memset(reinterpret_cast<void*>(owner+0x25C),0,72);
        *reinterpret_cast<unsigned*>(owner+0x154)=0;
        *reinterpret_cast<unsigned*>(owner+0x1E0)=1;
        *reinterpret_cast<unsigned*>(owner+0x1E4)=1;
        std::memset(reinterpret_cast<void*>(owner+0x230),0,12);
        std::memset(reinterpret_cast<void*>(owner+0x240),0,12);
        *reinterpret_cast<float*>(owner+0x250)=1.0f;
        *reinterpret_cast<float*>(owner+0x254)=1.0f;
        *reinterpret_cast<float*>(owner+0x1D0)=0.375f;
        *reinterpret_cast<float*>(owner+0x1D8)=0.375f;
        require(camera.capture(*stream,base));
        *reinterpret_cast<unsigned*>(owner+0x2A0)=2;
        require(!ca->restore_smoothing(*cb,camera,base) && *reinterpret_cast<unsigned*>(owner+0x154)==0);
        *reinterpret_cast<unsigned*>(owner+0x2A0)=0;
        cb->records[0].smoothing[76]=std::byte{16};
        require(!ca->restore_smoothing(*cb,camera,base) && *reinterpret_cast<unsigned*>(owner+0x154)==0);
        cb->records[0].smoothing[76]=std::byte{0};
        *reinterpret_cast<float*>(owner+0x250)=0.5f;
        require(!ca->restore_smoothing(*cb,camera,base) && *reinterpret_cast<unsigned*>(owner+0x154)==0);
        *reinterpret_cast<float*>(owner+0x250)=1.0f;
        sc6::research::CameraSnapshot::RestoreDiagnostic targetDiagnostic;
        cb->records[0].targets[0]^=std::byte{1};
        require(!ca->restore_smoothing(*cb,camera,base,false,false,false,false,&targetDiagnostic) && *reinterpret_cast<unsigned*>(owner+0x154)==0);
        require(targetDiagnostic.section==4 && targetDiagnostic.offset==0 && targetDiagnostic.a!=targetDiagnostic.b);
        cb->records[0].targets[0]^=std::byte{1};
        *reinterpret_cast<unsigned*>(owner+0x2A4)^=1;
        require(!ca->restore_smoothing(*cb,camera,base,false,false,false,false,&targetDiagnostic));
        require(targetDiagnostic.section==5 && targetDiagnostic.offset==116 && targetDiagnostic.c!=targetDiagnostic.live);
        require(*reinterpret_cast<unsigned*>(owner+0x154)==0);
        *reinterpret_cast<unsigned*>(owner+0x2A4)^=1;
        *reinterpret_cast<float*>(owner+0x1D8)=0.5f;
        require(!ca->restore_smoothing(*cb,camera,base) && *reinterpret_cast<unsigned*>(owner+0x154)==0);
        *reinterpret_cast<float*>(owner+0x1D8)=0.375f;
        cb->records[0].response[0]^=std::byte{1};
        require(!ca->restore_smoothing(*cb,camera,base) && *reinterpret_cast<unsigned*>(owner+0x154)==0);
        cb->records[0].response[0]^=std::byte{1};
        camera.records[0].response[4]^=std::byte{1};
        require(!ca->restore_smoothing(*cb,camera,base) && *reinterpret_cast<unsigned*>(owner+0x154)==0);
        camera.records[0].response[4]^=std::byte{1};
        // Observed idle selector -1 -> native-load zero, not arbitrary selector changes.
        unsigned sentinel=0xFFFFFFFFu,invalid_selector=2,zero_selector=0;
        std::memcpy(ca->records[0].targets.data(),&sentinel,4);
        std::memcpy(cb->records[0].targets.data(),&sentinel,4);
        std::memcpy(camera.records[0].targets.data(),&zero_selector,4);
        *reinterpret_cast<unsigned*>(owner+0x1E8)=0;
        require(ca->restore_smoothing(*cb,camera,base,false,false,true));
        require(*reinterpret_cast<unsigned*>(owner+0x1E8)==0);
        std::memcpy(camera.records[0].targets.data(),&invalid_selector,4);
        *reinterpret_cast<unsigned*>(owner+0x1E8)=invalid_selector;
        require(!ca->restore_smoothing(*cb,camera,base));
        require(*reinterpret_cast<unsigned*>(owner+0x154)==0);
        std::memcpy(camera.records[0].targets.data(),&zero_selector,4);
        *reinterpret_cast<unsigned*>(owner+0x1E8)=0;
        for(unsigned j:{116u,120u}){
            unsigned old=26,next=27,bad=28;
            std::memcpy(ca->records[0].targets.data()+j,&old,4);
            std::memcpy(cb->records[0].targets.data()+j,&next,4);
            std::memcpy(camera.records[0].targets.data()+j,&bad,4);
            *reinterpret_cast<unsigned*>(owner+0x2A4+j-116)=bad;
            require(!ca->restore_smoothing(*cb,camera,base,false,false,false,false,nullptr,true));
            require(*reinterpret_cast<unsigned*>(owner+0x154)==0);
            std::memcpy(camera.records[0].targets.data()+j,&next,4);
            *reinterpret_cast<unsigned*>(owner+0x2A4+j-116)=next;
        }
        // Exercise actual validation/restoration for every camera mode transition.
        {
            std::array<std::byte,0x2B0> live_before{};
            std::memcpy(live_before.data(),reinterpret_cast<void*>(owner),live_before.size());
            for(unsigned saved=0;saved<=14;++saved)for(unsigned next=0;next<=14;++next){
                using Camera=sc6::research::CameraSnapshot;
                require(Camera::camera_mode_loaded(saved,next,0));
                require(Camera::camera_mode_loaded(saved,next,saved));
                for(unsigned loaded=0;loaded<=15;++loaded)
                    require(Camera::camera_mode_loaded(saved,next,loaded)==(loaded==0 || loaded==saved));
                std::memcpy(reinterpret_cast<void*>(owner),live_before.data(),live_before.size());
                std::memcpy(ca->records[0].targets.data()+112,&saved,4);
                std::memcpy(cb->records[0].targets.data()+112,&next,4);
                require(ca->restore_smoothing(*cb,camera,base,false,false,true,false,nullptr,true));
                require(!std::memcmp(live_before.data(),reinterpret_cast<void*>(owner),live_before.size()));
                require(ca->restore_smoothing(*cb,camera,base,false,false,false,false,nullptr,true));
                require(*reinterpret_cast<unsigned*>(owner+0x258)==saved);
            }
            for(unsigned invalid:{15u,16u,0x7FFFFFFFu,0x80000000u,0xFFFFFFFFu}){
                std::memcpy(reinterpret_cast<void*>(owner),live_before.data(),live_before.size());
                unsigned good=2;
                for(bool bad_saved:{false,true}){
                    std::memcpy(ca->records[0].targets.data()+112,bad_saved?&invalid:&good,4);
                    std::memcpy(cb->records[0].targets.data()+112,bad_saved?&good:&invalid,4);
                    sc6::research::CameraSnapshot::RestoreDiagnostic detail{};
                    require(!ca->restore_smoothing(*cb,camera,base,false,false,false,false,&detail,true));
                    require(detail.failure_line && detail.offset==112);
                    require(!std::memcmp(live_before.data(),reinterpret_cast<void*>(owner),live_before.size()));
                }
            }
            std::memcpy(reinterpret_cast<void*>(owner),live_before.data(),live_before.size());
            std::memset(ca->records[0].targets.data()+112,0,4);std::memset(cb->records[0].targets.data()+112,0,4);
            std::puts("PASS: all 225 camera mode pairs restore saved value; invalid domains reject without writes");
        }
        // Both observed post-load decision-counter sentinels. Exercise full
        // guarded restoration, validate-only, and stale-live rejection.
        {
            using C=sc6::research::CameraSnapshot;
            const auto savedA=*ca,savedB=*cb,savedC=camera;
            std::array<std::byte,0x2B0> originalLive{};
            std::memcpy(originalLive.data(),reinterpret_cast<void*>(owner),originalLive.size());
            for(unsigned counterOffset:{116u,120u})for(unsigned source:{0xFFFFFFFFu,0u,1u,26u})for(bool resetNext:{false,true}){
                *ca=savedA;*cb=savedB;camera=savedC;
                std::memcpy(reinterpret_cast<void*>(owner),originalLive.data(),originalLive.size());
                const unsigned next=resetNext?0xFFFFFFFFu:source+1,reset=0xFFFFFFFFu;
                std::memcpy(ca->records[0].targets.data()+counterOffset,&source,4);
                std::memcpy(cb->records[0].targets.data()+counterOffset,&next,4);
                std::memcpy(camera.records[0].targets.data()+counterOffset,&reset,4);
                const auto address=owner+0x2A4+counterOffset-116;
                *reinterpret_cast<unsigned*>(address)=reset;
                require(ca->restore_smoothing(*cb,camera,base,false,false,true,false,nullptr,true));
                require(*reinterpret_cast<unsigned*>(address)==reset);
                // Live memory must still agree with the post-load observation.
                *reinterpret_cast<unsigned*>(address)=7;
                C::RestoreDiagnostic detail{};
                require(!ca->restore_smoothing(*cb,camera,base,false,false,false,false,&detail,true));
                require(detail.section==5 && detail.offset==counterOffset);
                require(*reinterpret_cast<unsigned*>(address)==7);
                *reinterpret_cast<unsigned*>(address)=reset;
                require(ca->restore_smoothing(*cb,camera,base,false,false,false,false,nullptr,true));
                require(*reinterpret_cast<unsigned*>(address)==source);
            }
            *ca=savedA;*cb=savedB;camera=savedC;
            std::memcpy(reinterpret_cast<void*>(owner),originalLive.data(),originalLive.size());
            require(C::decision_counter_loaded(26,27,0xFFFFFFFFu));
            require(!C::decision_counter_loaded(26,28,0xFFFFFFFFu));
            require(!C::decision_counter_loaded(26,27,0xFFFFFFFEu));
            require(!C::decision_counter_loaded(26,27,0));
            require(!C::decision_counter_loaded(0x7FFFFFFFu,0x80000000u,0xFFFFFFFFu));
            std::puts("PASS: camera counter reset sentinel restores saved values; adjacency and live checks remain strict");
        }
        // Native +2AC reset loses the previous selected player. Exercise all
        // source/next side pairs, exact restore, validate-only and no-write rejects.
        {
            using C=sc6::research::CameraSnapshot;
            const auto savedA=*ca,savedB=*cb,savedC=camera;
            std::array<std::byte,0x2B0> originalLive{};
            std::memcpy(originalLive.data(),reinterpret_cast<void*>(owner),originalLive.size());
            for(unsigned a=0;a<2;++a)for(unsigned b=0;b<2;++b){
                *ca=savedA;*cb=savedB;camera=savedC;
                std::memcpy(reinterpret_cast<void*>(owner),originalLive.data(),originalLive.size());
                std::memcpy(ca->records[0].targets.data()+124,&a,4);
                std::memcpy(cb->records[0].targets.data()+124,&b,4);
                require(ca->restore_smoothing(*cb,camera,base,false,false,true,false,nullptr,true));
                require(!std::memcmp(originalLive.data(),reinterpret_cast<void*>(owner),originalLive.size()));
                *reinterpret_cast<unsigned*>(owner+0x2AC)=1;
                C::RestoreDiagnostic detail{};
                require(!ca->restore_smoothing(*cb,camera,base,false,false,false,false,&detail,true));
                require(detail.section==5 && detail.offset==124);
                *reinterpret_cast<unsigned*>(owner+0x2AC)=0;
                require(ca->restore_smoothing(*cb,camera,base,false,false,false,false,nullptr,true));
                require(*reinterpret_cast<unsigned*>(owner+0x2AC)==a);
            }
            for(unsigned field=0;field<3;++field)for(unsigned bad:{2u,15u,0xFFFFFFFFu}){
                *ca=savedA;*cb=savedB;camera=savedC;
                std::memcpy(reinterpret_cast<void*>(owner),originalLive.data(),originalLive.size());
                auto* r=field==0?&ca->records[0]:field==1?&cb->records[0]:&camera.records[0];
                std::memcpy(r->targets.data()+124,&bad,4);
                if(field==2)*reinterpret_cast<unsigned*>(owner+0x2AC)=bad;
                std::array<std::byte,0x2B0> before{};std::memcpy(before.data(),reinterpret_cast<void*>(owner),before.size());
                require(!ca->restore_smoothing(*cb,camera,base,false,false,false,false,nullptr,true));
                require(!std::memcmp(before.data(),reinterpret_cast<void*>(owner),before.size()));
            }
            *ca=savedA;*cb=savedB;camera=savedC;
            std::memcpy(reinterpret_cast<void*>(owner),originalLive.data(),originalLive.size());
            unsigned one=1;std::memcpy(ca->records[0].targets.data()+124,&one,4);std::memcpy(cb->records[0].targets.data()+124,&one,4);
            require(!ca->restore_smoothing(*cb,camera,base)); // opt-in required
            require(!std::memcmp(originalLive.data(),reinterpret_cast<void*>(owner),originalLive.size()));
            *ca=savedA;*cb=savedB;camera=savedC;
            require(!C::player_selection_loaded(0,1,1)); // not reset or saved provenance
            std::puts("PASS: all four camera player-selection pairs restore exactly; invalid indices, stale live state and opt-out reject without writes");
        }
        // Both forced camera directions have the same native reset as auto.
        {
            using C=sc6::research::CameraSnapshot;
            const auto savedA=*ca,savedB=*cb,savedC=camera;
            std::array<std::byte,0x2B0> originalLive{};std::memcpy(originalLive.data(),reinterpret_cast<void*>(owner),originalLive.size());
            for(unsigned a:{0u,1u,0xFFFFFFFFu})for(unsigned b:{0u,1u,0xFFFFFFFFu}){
                *ca=savedA;*cb=savedB;camera=savedC;std::memcpy(reinterpret_cast<void*>(owner),originalLive.data(),originalLive.size());
                std::memcpy(ca->records[0].targets.data(),&a,4);std::memcpy(cb->records[0].targets.data(),&b,4);
                require(ca->restore_smoothing(*cb,camera,base,false,false,true,false,nullptr,true));
                require(!std::memcmp(originalLive.data(),reinterpret_cast<void*>(owner),originalLive.size()));
                require(ca->restore_smoothing(*cb,camera,base,false,false,false,false,nullptr,true));
                require(*reinterpret_cast<unsigned*>(owner+0x1E8)==a);
            }
            *ca=savedA;*cb=savedB;camera=savedC;std::memcpy(reinterpret_cast<void*>(owner),originalLive.data(),originalLive.size());
            require(!C::forced_selection_loaded(1,0,0xFFFFFFFFu));
            for(unsigned bad:{2u,0xFFFFFFFEu,0x7FFFFFFFu}){
                std::memcpy(ca->records[0].targets.data(),&bad,4);
                require(!ca->restore_smoothing(*cb,camera,base,false,false,false,false,nullptr,true));
                require(!std::memcmp(originalLive.data(),reinterpret_cast<void*>(owner),originalLive.size()));
            }
            *ca=savedA;*cb=savedB;camera=savedC;
            std::puts("PASS: nine forced camera selection pairs restore exact signed values; invalid selection and wrong loaded provenance rejected");
        }
        // Camera +258: known stable saved value reset by native load.
        ca->records[0].targets[112]=cb->records[0].targets[112]=std::byte{1};
        require(!ca->restore_smoothing(*cb,camera,base));
        require(ca->restore_smoothing(*cb,camera,base,false,false,true,false,nullptr,true));
        require(*reinterpret_cast<unsigned*>(owner+0x258)==0); // validate-only
        cb->records[0].targets[112]=std::byte{15};
        require(!ca->restore_smoothing(*cb,camera,base,false,false,true,false,nullptr,true));
        cb->records[0].targets[112]=std::byte{1};
        camera.records[0].targets[112]=std::byte{2};
        *reinterpret_cast<unsigned*>(owner+0x258)=2;
        require(!ca->restore_smoothing(*cb,camera,base,false,false,true,false,nullptr,true));
        camera.records[0].targets[112]=std::byte{0};
        *reinterpret_cast<unsigned*>(owner+0x258)=0;
        require(!ca->restore_smoothing(*cb,camera,base)); // opt-in required
        require(ca->restore_smoothing(*cb,camera,base,false,false,true,false,nullptr,true));
        require(*reinterpret_cast<unsigned*>(owner+0x2A8)==27); // validation never writes
        require(ca->restore_smoothing(*cb,camera,base,false,false,false,false,nullptr,true));
        require(*reinterpret_cast<unsigned*>(owner+0x2A4)==26 && *reinterpret_cast<unsigned*>(owner+0x2A8)==26);
        require(*reinterpret_cast<unsigned*>(owner+0x258)==1);
        for(auto* r:{&ca->records[0],&cb->records[0],&camera.records[0]})r->targets[112]=std::byte{0};
        *reinterpret_cast<unsigned*>(owner+0x258)=0;
        for(auto* r:{&ca->records[0],&cb->records[0],&camera.records[0]})
            std::memset(r->targets.data()+116,0,8);
        std::memset(reinterpret_cast<void*>(owner+0x2A4),0,8);
        require(sc6::research::CameraSnapshot::decision_counter_pair(26,0xFFFFFFFFu));
        require(sc6::research::CameraSnapshot::decision_counter_pair(0xFFFFFFFFu,0));
        require(!sc6::research::CameraSnapshot::decision_counter_pair(0xFFFFFFFFu,1));
        require(!sc6::research::CameraSnapshot::decision_counter_pair(0xFFFFFFFEu,0xFFFFFFFEu));
        require(!sc6::research::CameraSnapshot::decision_counter_pair(0x7FFFFFFFu,0x80000000u));
        require(!sc6::research::CameraSnapshot::decision_counter_pair(26,28));
        require(*reinterpret_cast<unsigned*>(owner+0x1E8)==0xFFFFFFFFu);
        require(*reinterpret_cast<float*>(owner+0x1D0)==0 && *reinterpret_cast<float*>(owner+0x1D8)==0);
        require(*reinterpret_cast<float*>(owner+0x1D4)==0.375f);
        for(unsigned off:{0x230u,0x234u,0x238u,0x240u,0x244u,0x248u})require(*reinterpret_cast<float*>(owner+off)==2.5f);
        require(*reinterpret_cast<unsigned*>(owner+0x250)==0 && *reinterpret_cast<unsigned*>(owner+0x254)==0);
        require(*reinterpret_cast<unsigned*>(owner+0x154)==0x40600000 && *reinterpret_cast<unsigned*>(owner+0x1E4)==0);
        require(*reinterpret_cast<unsigned*>(owner+0x25C)==1234 && *reinterpret_cast<unsigned*>(owner+0x29C)==20 && *reinterpret_cast<unsigned*>(owner+0x2A0)==15);
        // Same-boundary throw: nonzero adaptive response, unchanged history, factors1.
        *reinterpret_cast<float*>(owner+0x1D0)=0.24f;
        *reinterpret_cast<float*>(owner+0x1D8)=0.14f;
        *reinterpret_cast<float*>(owner+0x250)=1.0f;
        *reinterpret_cast<float*>(owner+0x254)=1.0f;
        require(ca->capture(*stream,base));*cb=*ca;
        *reinterpret_cast<unsigned*>(owner+0x154)=0;
        *reinterpret_cast<unsigned*>(owner+0x1E0)=1;
        *reinterpret_cast<unsigned*>(owner+0x1E4)=1;
        std::memset(reinterpret_cast<void*>(owner+0x25C),0,72);
        std::memset(reinterpret_cast<void*>(owner+0x230),0,12);
        std::memset(reinterpret_cast<void*>(owner+0x240),0,12);
        *reinterpret_cast<float*>(owner+0x1D0)=0.375f;
        *reinterpret_cast<float*>(owner+0x1D8)=0.375f;
        require(camera.capture(*stream,base));
        require(!ca->restore_smoothing(*cb,camera,base));
        cb->records[0].smoothing[8]^=std::byte{1};
        require(!ca->restore_smoothing(*cb,camera,base,true) && *reinterpret_cast<unsigned*>(owner+0x154)==0);
        cb->records[0].smoothing[8]^=std::byte{1};
        require(ca->restore_smoothing(*cb,camera,base,true));
        require(*reinterpret_cast<float*>(owner+0x1D0)==0.24f && *reinterpret_cast<float*>(owner+0x1D8)==0.14f);
        // Adjacent combat permits measured target/response progression, with
        // a read-only preflight and exact one-frame history advancement.
        std::memcpy(reinterpret_cast<void*>(owner),camera.records[0].bytes.data(),0x1C8);
        std::memcpy(reinterpret_cast<void*>(owner+0x1E0),camera.records[0].smoothing.data(),8);
        std::memcpy(reinterpret_cast<void*>(owner+0x25C),camera.records[0].smoothing.data()+8,72);
        std::memcpy(reinterpret_cast<void*>(owner+0x1E8),camera.records[0].targets.data(),116);
        std::memcpy(reinterpret_cast<void*>(owner+0x2A4),camera.records[0].targets.data()+116,12);
        std::memcpy(reinterpret_cast<void*>(owner+0x1D0),camera.records[0].response.data(),12);
        unsigned n=21,index=0;float response=0.26f,position=3.0f;
        std::memcpy(cb->records[0].smoothing.data()+72,&n,4);
        std::memcpy(cb->records[0].smoothing.data()+76,&index,4);
        std::memcpy(cb->records[0].response.data(),&response,4);
        std::memcpy(cb->records[0].targets.data()+0x48,&position,4);
        require(ca->adjacent_combat_ready(*cb,base));
        {
            const auto a=ca->records[0],b=cb->records[0];
            for(unsigned span:{2u,3u})for(unsigned count:{20u,0x7FFFFFFEu,0x7FFFFFFFu}){
                unsigned oldIndex=15,expected=count>0x7FFFFFFFu-span?0x7FFFFFFFu:count+span,nextIndex=(oldIndex+span)%16;
                std::memcpy(ca->records[0].smoothing.data()+72,&count,4);
                std::memcpy(ca->records[0].smoothing.data()+76,&oldIndex,4);
                std::memcpy(cb->records[0].smoothing.data()+72,&expected,4);
                std::memcpy(cb->records[0].smoothing.data()+76,&nextIndex,4);
                require(ca->adjacent_combat_ready(*cb,base,false,nullptr,false,false,span));
                require(!ca->adjacent_combat_ready(*cb,base,false,nullptr,false,false,1));
            }
            require(!ca->adjacent_combat_ready(*cb,base,false,nullptr,false,false,0));
            require(!ca->adjacent_combat_ready(*cb,base,false,nullptr,false,false,4));
            ca->records[0]=a;cb->records[0]=b;
        }

        {
            const auto originalA=ca->records[0],originalB=cb->records[0];
            for(auto* r:{&ca->records[0],&cb->records[0]}){
                for(unsigned off:{0x68u,0x6Cu})std::memset(r->targets.data()+off,0,4);
                std::memset(r->response.data(),0,4);std::memset(r->response.data()+8,0,4);
            }
            cb->records[0].targets=ca->records[0].targets;
            require(ca->adjacent_idle_ready(*cb,base));
            require(!ca->adjacent_combat_ready(*cb,base));
            cb->records[0].targets[0x48]^=std::byte{1};require(!ca->adjacent_idle_ready(*cb,base));
            cb->records[0].targets=ca->records[0].targets;
            unsigned half=0x3F000000u;
            for(auto* r:{&ca->records[0],&cb->records[0]})std::memcpy(r->targets.data()+0x68,&half,4);
            require(!ca->adjacent_idle_ready(*cb,base));
            ca->records[0]=originalA;cb->records[0]=originalB;
            require(!ca->adjacent_idle_ready(*cb,base));
        }

        {
            sc6::research::CameraSnapshot::ReadinessDiagnostic d;
            require(ca->adjacent_combat_ready(*cb,base,false,&d) && d.reason==0);
            const auto saved=cb->records[0];
            cb->records[0].owner++;require(!ca->adjacent_combat_ready(*cb,base,false,&d) && d.reason==2 && d.record==0);
            cb->records[0]=saved;
            cb->records[0].smoothing[76]=std::byte{16};
            require(!ca->adjacent_combat_ready(*cb,base,false,&d) && d.reason==6 && d.section==1 && d.offset==76 && d.side==1 && d.saved==16);
            cb->records[0]=saved;
            cb->records[0].targets[0]^=std::byte{1};
            require(!ca->adjacent_combat_ready(*cb,base,false,&d) && d.reason==14 && d.section==3 && d.offset==0 && d.saved!=d.next);
            cb->records[0]=saved;
            cb->records[0].response[4]=std::byte{1};
            require(!ca->adjacent_combat_ready(*cb,base,false,&d) && d.reason==8 && d.section==2 && d.offset==4 && d.side==1);
            cb->records[0]=saved;
            require(ca->adjacent_combat_ready(*cb,base,false,&d) && d.reason==0);
        }

        {
            const auto saved_a=ca->records[0],saved_b=cb->records[0];
            for(auto* r:{&ca->records[0],&cb->records[0]})
                for(unsigned off:{0x68u,0x6Cu})std::memset(r->targets.data()+off,0,4);
            // Moving targets/response prevent the old idle-only shortcut.
            require(!ca->adjacent_idle_ready(*cb,base));
            require(ca->adjacent_combat_ready(*cb,base,true));
            require(ca->restore_smoothing(*cb,camera,base,false,true,true,true));
            auto bad_loaded=camera;
            std::memset(bad_loaded.records[0].targets.data()+0x68,0,4);
            require(!ca->restore_smoothing(*cb,bad_loaded,base,false,true,true,true));
            require(ca->restore_smoothing(*cb,camera,base,false,true,false,true));
            require(*reinterpret_cast<unsigned*>(owner+0x250)==0 && *reinterpret_cast<unsigned*>(owner+0x254)==0);
            require(!ca->restore_smoothing(*cb,camera,base,false,true,false,true));
            // Restore the mock native post-load state for subsequent tests.
            std::memcpy(reinterpret_cast<void*>(owner),camera.records[0].bytes.data(),0x1C8);
            std::memcpy(reinterpret_cast<void*>(owner+0x1E0),camera.records[0].smoothing.data(),8);
            std::memcpy(reinterpret_cast<void*>(owner+0x25C),camera.records[0].smoothing.data()+8,72);
            std::memcpy(reinterpret_cast<void*>(owner+0x1E8),camera.records[0].targets.data(),116);
            std::memcpy(reinterpret_cast<void*>(owner+0x2A4),camera.records[0].targets.data()+116,12);
            std::memcpy(reinterpret_cast<void*>(owner+0x1D0),camera.records[0].response.data(),12);
            ca->records[0]=saved_a;cb->records[0]=saved_b;
            using C=sc6::research::CameraSnapshot;
            require(C::replay_factor(0,0) && !C::rising_factor(0,0));
            require(C::replay_factor(0,0x3DCCCCCDu));
            for(unsigned bad:{0x80000000u,0x7FC00000u,0x7F800000u,0xBF000000u,0x3F000000u,0x3F800001u}){
                require(!C::replay_factor(bad,bad));require(!C::replay_factor(0,bad));
            }
        }

        {
            using C=sc6::research::CameraSnapshot;
            const auto original_a=ca->records[0].targets,original_b=cb->records[0].targets;
            auto bits=[](float f){unsigned v{};std::memcpy(&v,&f,4);return v;};
            auto native_fall=[&](unsigned a){float value{},delta{};unsigned d=0x3D9CAC08u;
                std::memcpy(&value,&a,4);std::memcpy(&delta,&d,4);
                return bits(_mm_cvtss_f32(_mm_max_ss(_mm_setzero_ps(),_mm_sub_ss(_mm_set_ss(value),_mm_set_ss(delta)))));};
            for(unsigned a:{0u,1u,0x3D9CAC07u,0x3D9CAC08u,0x3D9CAC09u,0x3E4CCCCDu,0x3F000000u,0x3F800000u}){
                const auto b=native_fall(a);require(C::falling_factor(a,b));
                require(C::general_factors(a,a,b,b));require(!C::falling_factor(a,b+1));
                require(C::general_factors(a,a,0,0));
            }
            for(unsigned bad:{0x80000000u,0xBF000000u,0x3F800001u,0x7F800000u,0x7FC00000u}){
                require(!C::falling_factor(bad,0));require(!C::general_factors(bad,0,0,0));
                require(!C::general_factors(0,0,bad,0));
            }
            const unsigned a0=bits(0.5f),a1=bits(0.2f);
            require(!C::general_factors(a0,a1,bits(0.6f),native_fall(a1)));
            for(bool reset:{false,true}){
                const unsigned b0=reset?0:native_fall(a0),b1=reset?0:native_fall(a1);
                std::memcpy(ca->records[0].targets.data()+0x68,&a0,4);std::memcpy(ca->records[0].targets.data()+0x6C,&a1,4);
                std::memcpy(cb->records[0].targets.data()+0x68,&b0,4);std::memcpy(cb->records[0].targets.data()+0x6C,&b1,4);
                require(!ca->adjacent_combat_ready(*cb,base,true)); // fixture policy unchanged
                C::ReadinessDiagnostic d;
                require(ca->adjacent_combat_ready(*cb,base,true,&d,false,true));
                require(ca->restore_smoothing(*cb,camera,base,false,true,true,true,nullptr,true));
                require(ca->restore_smoothing(*cb,camera,base,false,true,false,true,nullptr,true));
                require(*reinterpret_cast<unsigned*>(owner+0x250)==a0 && *reinterpret_cast<unsigned*>(owner+0x254)==a1);
                require(!ca->restore_smoothing(*cb,camera,base,false,true,false,true,nullptr,true)); // stale live image
                std::memcpy(reinterpret_cast<void*>(owner),camera.records[0].bytes.data(),0x1C8);
                std::memcpy(reinterpret_cast<void*>(owner+0x1E0),camera.records[0].smoothing.data(),8);
                std::memcpy(reinterpret_cast<void*>(owner+0x25C),camera.records[0].smoothing.data()+8,72);
                std::memcpy(reinterpret_cast<void*>(owner+0x1E8),camera.records[0].targets.data(),116);
                std::memcpy(reinterpret_cast<void*>(owner+0x2A4),camera.records[0].targets.data()+116,12);
                std::memcpy(reinterpret_cast<void*>(owner+0x1D0),camera.records[0].response.data(),12);
            }
            ca->records[0].targets=original_a;cb->records[0].targets=original_b;
        }
        {
            auto original_a=ca->records[0].targets,original_b=cb->records[0].targets;
            float a=0.2f,b=a+0.1f;
            for(unsigned off:{0x68u,0x6Cu}){
                std::memcpy(ca->records[0].targets.data()+off,&a,4);
                std::memcpy(cb->records[0].targets.data()+off,&b,4);}
            require(ca->adjacent_combat_ready(*cb,base,true));
            {
                auto policy=std::make_unique<sc6::research::FullStateProbe>();
                require(!policy->camera_rising_requested());
                policy->general_combat_requested=true;
                require(policy->camera_rising_requested());
                sc6::research::CameraSnapshot::ReadinessDiagnostic d;
                require(ca->adjacent_combat_ready(*cb,base,policy->camera_rising_requested(),&d));
                unsigned wrong=0x3F000000u;
                const auto original=cb->records[0].targets;
                std::memcpy(cb->records[0].targets.data()+0x68,&wrong,4);
                require(!ca->adjacent_combat_ready(*cb,base,policy->camera_rising_requested(),&d));
                require(d.reason==12 && d.offset==0x68 && d.next==wrong);
                cb->records[0].targets=original;
                policy->general_idle_camera=true;require(!policy->camera_rising_requested());
                policy->charge_requested=true;require(policy->camera_rising_requested());
            }

            require(!ca->adjacent_combat_ready(*cb,base));
            require(!ca->restore_smoothing(*cb,camera,base,false,true,true));
            require(ca->restore_smoothing(*cb,camera,base,false,true,true,true));
            require(*reinterpret_cast<unsigned*>(owner+0x154)==0);
            require(!ca->restore_smoothing(*cb,camera,base,true,false,true,true));
            require(ca->restore_smoothing(*cb,camera,base,false,true,false,true));
            require(*reinterpret_cast<float*>(owner+0x250)==a && *reinterpret_cast<float*>(owner+0x254)==a);
            // Reusing a stale post-load snapshot must reject without another write.
            require(!ca->restore_smoothing(*cb,camera,base,false,true,false,true));
            std::memcpy(reinterpret_cast<void*>(owner),camera.records[0].bytes.data(),0x1C8);
            std::memcpy(reinterpret_cast<void*>(owner+0x1E0),camera.records[0].smoothing.data(),8);
            std::memcpy(reinterpret_cast<void*>(owner+0x25C),camera.records[0].smoothing.data()+8,72);
            std::memcpy(reinterpret_cast<void*>(owner+0x1E8),camera.records[0].targets.data(),116);
            std::memcpy(reinterpret_cast<void*>(owner+0x2A4),camera.records[0].targets.data()+116,12);
            std::memcpy(reinterpret_cast<void*>(owner+0x1D0),camera.records[0].response.data(),12);
            cb->records[0].targets[0]^=std::byte{1};require(!ca->adjacent_combat_ready(*cb,base,true));
            cb->records[0].targets[0]^=std::byte{1};
            cb->records[0].targets[0x68]^=std::byte{1};require(!ca->adjacent_combat_ready(*cb,base,true));
            ca->records[0].targets=original_a;cb->records[0].targets=original_b;
            using C=sc6::research::CameraSnapshot;
            require(C::rising_factor(0x3F800000u,0x3F800000u));
            require(C::rising_factor(0x3F733333u,0x3F800000u));
            require(!C::rising_factor(0x7FC00000u,0x3F800000u));
            require(!C::rising_factor(0xBF000000u,0x3F800000u));
            require(!C::rising_factor(0x3F800001u,0x3F800000u));
        }

        require(!ca->restore_smoothing(*cb,camera,base,true));
        require(ca->restore_smoothing(*cb,camera,base,false,true,true));
        require(*reinterpret_cast<unsigned*>(owner+0x154)==0);
        cb->records[0].targets[0]^=std::byte{1};
        require(!ca->adjacent_combat_ready(*cb,base));
        require(!ca->restore_smoothing(*cb,camera,base,false,true));
        require(*reinterpret_cast<unsigned*>(owner+0x154)==0);
        cb->records[0].targets[0]^=std::byte{1};
        n=22;std::memcpy(cb->records[0].smoothing.data()+72,&n,4);
        sc6::research::CameraSnapshot::RestoreDiagnostic camera_detail{};
        require(!ca->restore_smoothing(*cb,camera,base,false,true,true,false,&camera_detail));
        require(camera_detail.failure_line && camera_detail.section==10 && camera_detail.offset==72);
        require(camera_detail.a==20 && camera_detail.b==22 && camera_detail.c==0);
        require(*reinterpret_cast<unsigned*>(owner+0x154)==0);
        n=21;std::memcpy(cb->records[0].smoothing.data()+72,&n,4);
        require(ca->restore_smoothing(*cb,camera,base,false,true));
        require(*reinterpret_cast<float*>(owner+0x1D0)==0.24f);
        require(*reinterpret_cast<float*>(owner+0x230)==2.5f);
        *reinterpret_cast<uintptr_t*>(owner)=0;
        require(camera.capture(*stream,base) && !camera.records[0].smoothing_valid);
        ++stream->traces[5].source;require(!camera.capture(*stream,base) && camera.failure==3 && camera.count==0);
        --stream->traces[5].source;stream->sealed=false;require(!camera.capture(*stream,base) && camera.failure==1);
    }
    *reinterpret_cast<uintptr_t*>(base+0x470DE90)=base+0x1000;
    *reinterpret_cast<uintptr_t*>(base+0x470DE98)=base+0x100000;
    for(mode=0;mode<=8;++mode){
        *reinterpret_cast<unsigned*>(base+0x485EB28)=123;

        *reinterpret_cast<unsigned*>(base+P::key_rvas[1])=55;
        *reinterpret_cast<float*>(base+0x48462F4)=1;
        *reinterpret_cast<std::uint16_t*>(base+0x475A04A)=65535;
        *reinterpret_cast<std::uint16_t*>(base+0x47F143A)=65535;
        *reinterpret_cast<std::uint16_t*>(base+0x4717840)=mode==8?1:0;
        *reinterpret_cast<std::uint64_t*>(base+0x470E870)=1234;
        auto p=std::make_unique<P>();saves=loads=checks=0;p->run(base,save,load,check);
        if(mode==0)require(!p->fault && p->restore_completed && p->selected_equal && p->post_save_valid && p->byte_differences==0 && loads==1 && saves==2);
        else require(p->fault);
        if(mode==0){
            require(p->saved.writes==1 && p->saved.reads==1);
            require(p->saved.traces[0].source==base+P::key_rvas[1]);
            require(p->saved.traces[0].destination==base+P::key_rvas[1]);
            require(p->saved.traces[0].writer && p->saved.traces[0].reader);
            require(p->after_save.traces[0].source==base+P::key_rvas[1]);
            require(p->after_save.traces[0].destination==0 && p->after_save.traces[0].reader==0);
            for(unsigned i=0;i<4;++i)require(p->measurements[i].valid);
            require(!p->measurements[4].valid);
            require(p->measurements[0].rng==123 && p->measurements[1].rng==123 && p->measurements[2].rng==124 && p->measurements[3].rng==124);
            require(p->measurements[0].players[0]==base+0x1000);
            require(*reinterpret_cast<unsigned*>(base+0x485EB28)==124);
        }
        if(mode==1 || mode==8)require(!p->measurements[0].valid);

        if(mode==1 || mode==8)require(!p->restore_invoked && !saves && !loads);
        if(mode==2 || mode==3 || mode==5)require(p->exception==T::rejection && saves==1);
        if(mode==4)require(!p->restore_completed && !p->exception);
        if(mode==6)require(!p->selected_equal && saves==1);
        if(mode==7)require(!p->postflight_ok && saves==1);
        require(*reinterpret_cast<std::uint64_t*>(base+0x470E870)==1234);
        auto old=saves;p->run(base,save,load,check);require(saves==old);
    }
    // Exact native +54 reset, both sides, with no guessed saved-value range.
    for(unsigned value:{0x3F333333u,0u,0x80000000u,0xBF800000u,0x7F7FFFFFu,0x7FC12345u}){
        auto q=std::make_unique<P>();
        for(unsigned side=0;side<2;++side){
            auto player=P::ptr(base+0x470DE90+side*8);
            std::memset(reinterpret_cast<void*>(player+0x971E8),0,96);
            std::memset(reinterpret_cast<void*>(player+0x97100),0,120);
            *reinterpret_cast<uintptr_t*>(player+0x43DF0)=player;
            *reinterpret_cast<unsigned*>(player+0x9723C)=value;
        }
        *reinterpret_cast<unsigned*>(base+0x485EB28)=123;q->measure(base,0);q->measure(base,1);
        *reinterpret_cast<unsigned*>(base+0x485EB28)=P::advance_rng(123);
        for(unsigned side=0;side<2;++side)*reinterpret_cast<unsigned*>(P::ptr(base+0x470DE90+side*8)+0x9723C)=0x3F800000;
        q->measure(base,2);require(q->preserve(base) && q->preserve_verified);
        for(unsigned side=0;side<2;++side){require(q->scalar_resets[side].restored==1);require(*reinterpret_cast<unsigned*>(P::ptr(base+0x470DE90+side*8)+0x9723C)==value);}
    }
    for(unsigned rejection=0;rejection<3;++rejection){
        auto q=std::make_unique<P>();
        for(unsigned side=0;side<2;++side){auto player=P::ptr(base+0x470DE90+side*8);std::memset(reinterpret_cast<void*>(player+0x971E8),0,96);*reinterpret_cast<unsigned*>(player+0x9723C)=0x3F333333;}
        *reinterpret_cast<unsigned*>(base+0x485EB28)=123;q->measure(base,0);q->measure(base,1);
        *reinterpret_cast<unsigned*>(base+0x485EB28)=P::advance_rng(123);
        auto player=P::ptr(base+0x470DE90);
        *reinterpret_cast<unsigned*>(player+0x9723C)=rejection==0?0x40000000:0x3F800000;
        if(rejection==2)*reinterpret_cast<unsigned*>(player+0x97240)=1; // adjacent unknown bytes
        q->measure(base,2);
        if(rejection==1)*reinterpret_cast<unsigned*>(player+0x9723C)=0x40000000; // changed since measurement
        require(!q->preserve(base) && !q->preserve_applied);
    }
    for(mode=9;mode<=43;++mode){
        *reinterpret_cast<unsigned*>(base+0x485EB28)=123;
        *reinterpret_cast<std::uint16_t*>(base+0x4717840)=0;
        for(unsigned side=0;side<2;++side){auto player=P::ptr(base+0x470DE90+side*8);
            std::memset(reinterpret_cast<void*>(player+0x43DF0),0,8);
            std::memset(reinterpret_cast<void*>(player+0x97100),0,120);
            std::memset(reinterpret_cast<void*>(player+0x971E8),0,96);
            if(mode>=13 && mode<=16){
                *reinterpret_cast<uintptr_t*>(player+0x971E8)=player+0x200;
                *reinterpret_cast<uintptr_t*>(player+0x971F0)=player+0x300;
                *reinterpret_cast<unsigned*>(player+0x97204)=side==0?1:0;
                *reinterpret_cast<unsigned*>(player+0x97220)=side==0?0:1;
            }
        }
        if(mode>=17)for(unsigned side=0;side<2;++side){auto player=P::ptr(base+0x470DE90+side*8);
            *reinterpret_cast<uintptr_t*>(player+0x971E8)=(mode==21 || mode==29 || mode==35)?0:player+0x200;
            *reinterpret_cast<uintptr_t*>(player+0x971F0)=player+0x300;
            *reinterpret_cast<unsigned*>(player+0x97204)=1;
            *reinterpret_cast<unsigned*>(player+0x97220)=(mode==20 || mode==28 || mode==34)?2:(side==0?0:1);
        }
        if(mode>=38)for(unsigned side=0;side<2;++side){auto player=P::ptr(base+0x470DE90+side*8);
            *reinterpret_cast<unsigned*>(player+0x97204)=side==0?1:0;
            *reinterpret_cast<unsigned*>(player+0x97220)=mode==41?1:0;
            if(mode==42)*reinterpret_cast<uintptr_t*>(player+0x971E8)=0;
        }
        *reinterpret_cast<float*>(base+0x48462F4)=mode>=38?819.0f/16384.0f:mode>=13?0.0f:1.0f;
        *reinterpret_cast<std::uint16_t*>(base+0x475A04A)=mode>=38?(mode==43?656:187):mode>=32?(mode==37?679:656):mode>=26?(mode==31?219:679):mode>=17?219:mode>=13?487:65535;
        *reinterpret_cast<std::uint16_t*>(base+0x47F143A)=mode>=38?193:mode>=17?65535:mode==14?10040:mode>=13?10041:65535;
        *reinterpret_cast<int*>(base+0x4846458)=mode>=17 && mode<38 && mode!=18?0:-1;
        if(mode>=17){
            auto manager=base+0x300000,owner=base+0x301000,object=base+0x302000,vt=base+0x303000;
            *reinterpret_cast<uintptr_t*>(base+0x470DF28)=manager;
            *reinterpret_cast<uintptr_t*>(manager+0xA0)=owner;
            *reinterpret_cast<uintptr_t*>(owner+0x30)=object;
            *reinterpret_cast<uintptr_t*>(object)=vt;
            *reinterpret_cast<uintptr_t*>(vt+0x38)=base+0x31F4E0;
            *reinterpret_cast<uintptr_t*>(vt+0x28)=base+0x32ACE0;
            *reinterpret_cast<uintptr_t*>(object+0x3B8)=P::ptr(base+(mode==24?0x470DE98:0x470DE90));
            *reinterpret_cast<uintptr_t*>(object+0x3C8)=mode==25?0:base+0x304000;
            *reinterpret_cast<uintptr_t*>(object+0x38)=base+0x305000;
            *reinterpret_cast<unsigned*>(object+0xA4)=mode==23?0:3;
            *reinterpret_cast<unsigned*>(object+0x3D0)=0;
        }
        auto p=std::make_unique<P>();p->throw_requested=mode>=13 && mode<=16;p->charge_requested=mode>=17 && mode<=25;p->attack_requested=mode>=26 && mode<=31;p->edge_requested=mode>=32 && mode<=37;p->reversal_requested=mode>=38;p->preserve_requested=true;saves=loads=checks=0;p->run(base,save,load,check);
        if(mode==9 || mode==13 || mode==17 || mode==26 || mode==32 || mode==38 || mode==41){
            require(!p->fault && p->preserve_applied && p->preserve_verified && saves==2);
            require(p->measurements[2].rng==P::advance_rng(123) && p->measurements[4].rng==123 && p->measurements[3].rng==123);
            for(unsigned side=0;side<2;++side){auto player=P::ptr(base+0x470DE90+side*8);
                require(P::ptr(player+0x43DF0)==player);
                if(mode==17 || mode==26 || mode==32){
                    require(*reinterpret_cast<unsigned*>(player+0x97220)==(side==0?0u:1u));
                    require(*reinterpret_cast<unsigned*>(player+0x97204)==1);
                }
                if(mode==38 || mode==41){
                    require(*reinterpret_cast<unsigned*>(player+0x97204)==(side==0?1u:0u));
                    require(*reinterpret_cast<unsigned*>(player+0x97220)==(mode==41?1u:0u));
                }
                if(mode==13){
                    require(*reinterpret_cast<unsigned*>(player+0x97204)==(side==0?1u:0u));
                    require(*reinterpret_cast<unsigned*>(player+0x97220)==(side==0?0u:1u));
                }
                require(*reinterpret_cast<unsigned*>(player+0x97100)==0);
                for(auto off:P::scalar_offsets)require(*reinterpret_cast<unsigned*>(player+0x971E8+off)==0);
            }
        }else if(mode==14 || mode==18 || (mode>=23 && mode<=25) || mode==31 || mode==37 || mode==43){require(p->fault && !p->restore_invoked && !p->preserve_applied && saves==0);
        }else if(mode==12){require(p->fault && p->preserve_applied && !p->preserve_verified && saves==2);
        }else {
            require(p->fault && !p->preserve_applied && !p->measurements[4].valid && saves==1);
            require(*reinterpret_cast<unsigned*>(P::ptr(base+0x470DE90)+0x97100)==0x11111111);
        }
    }
    // Physical slot capture and rotation use the pinned layout, never game writes.
    using Ring=sc6::research::RingSnapshot;
    for(unsigned side=0;side<2;++side)for(unsigned kind=0;kind<2;++kind){
        auto player=P::ptr(base+0x470DE90+side*8),object=player+(kind?0x27760:0x35A0);
        auto first=(object+0xBF)&~uintptr_t(0x7F);unsigned stride=kind?0x800:0xC000;
        *reinterpret_cast<uintptr_t*>(object)=base+(kind?0x3E87700:0x3E87638);
        for(unsigned slot=0;slot<3;++slot){
            *reinterpret_cast<uintptr_t*>(object+8+slot*8)=first+slot*stride;
            std::memset(reinterpret_cast<void*>(first+slot*stride),int(1+side*6+kind*3+slot),stride);
        }
        *reinterpret_cast<unsigned*>(object+0x20)=0;
        *reinterpret_cast<uintptr_t*>(object+0x28)=first;
        *reinterpret_cast<uintptr_t*>(object+0x30)=first+stride;
    }
    auto ring0=std::make_unique<Ring>(),ring1=std::make_unique<Ring>();
    require(ring0->capture(base) && ring1->capture(base) && ring0->same(*ring1));
    require(ring0->bytes.front()==std::byte{1} && ring0->bytes.back()==std::byte{12});
    ring1->bytes.fill(std::byte{0xA5});
    require(ring0->validate_owners(base,*ring1) && !ring1->valid);
    require(std::all_of(ring1->bytes.begin(),ring1->bytes.end(),[](auto b){return b==std::byte{0xA5};}));
    require(!ring0->validate_owners(base,*ring0) && ring0->valid);
    for(unsigned group=0;group<4;++group){
        const auto& h=ring0->headers[group];
        auto* index=reinterpret_cast<unsigned*>(h.object+0x20);
        const auto original=*index;*index=3;
        require(!ring0->validate_owners(base,*ring1) && !ring1->valid);
        *index=original;
    }
    require(ring0->restore(base,*ring1) && ring0->same(*ring1));
    // Diagnostic benchmark only: no timing assertion tied to host hardware.
    LARGE_INTEGER fq{},start{},middle{},end{};QueryPerformanceFrequency(&fq);
    constexpr unsigned repetitions=1000;
    QueryPerformanceCounter(&start);
    for(unsigned i=0;i<repetitions;++i)require(ring1->capture(base) && ring0->same_owners(*ring1));
    QueryPerformanceCounter(&middle);
    for(unsigned i=0;i<repetitions;++i)require(ring0->validate_owners(base,*ring1));
    QueryPerformanceCounter(&end);
    std::cout<<"Ring ownership benchmark us/check: full="<<double(middle.QuadPart-start.QuadPart)*1e6/double(fq.QuadPart)/repetitions
             <<" headers="<<double(end.QuadPart-middle.QuadPart)*1e6/double(fq.QuadPart)/repetitions<<"; exact post-restore comparison retained\n";

    for(auto& h:ring0->headers){
        *reinterpret_cast<unsigned*>(h.object+0x20)=2;
        *reinterpret_cast<uintptr_t*>(h.object+0x30)=h.current;
        *reinterpret_cast<uintptr_t*>(h.object+0x28)=h.slots[2];
    }
    require(ring1->capture(base) && ring1->rotated_from(*ring0) && !ring0->same(*ring1));
    auto object=ring0->headers[0].object;
    *reinterpret_cast<unsigned*>(object+0x20)=3;require(!ring1->capture(base));
    *reinterpret_cast<unsigned*>(object+0x20)=2;
    *reinterpret_cast<uintptr_t*>(object+0x30)=1;require(!ring1->capture(base));
    *reinterpret_cast<uintptr_t*>(object+0x30)=ring0->headers[0].current;
    *reinterpret_cast<uintptr_t*>(object+8)=1;require(!ring1->capture(base));
    *reinterpret_cast<uintptr_t*>(object+8)=ring0->headers[0].slots[0];
    *reinterpret_cast<uintptr_t*>(object)=0;require(!ring1->capture(base));
    *reinterpret_cast<uintptr_t*>(object)=ring0->headers[0].vtable;
    require(!ring1->capture(0));
    mode=0;
    *reinterpret_cast<std::uint16_t*>(base+0x475A04A)=65535;
    *reinterpret_cast<std::uint16_t*>(base+0x47F143A)=65535;
    *reinterpret_cast<float*>(base+0x48462F4)=1;
    for(unsigned test=0;test<4;++test){
        auto q=std::make_unique<P>();q->delay_audit=true;saves=loads=0;
        *reinterpret_cast<unsigned*>(base+0x470D0C4)=100;
        *reinterpret_cast<std::uint16_t*>(base+0x4717840)=0;
        q->audit_step(base,save,check);
        require(q->audit_started && !q->audit_done && saves==1 && loads==0);
        *reinterpret_cast<unsigned*>(base+0x470D0C4)=test==1?102:101;
        if(test==2)*reinterpret_cast<std::uint16_t*>(base+0x4717840)=1;
        if(test==3)q->saved.traces[0].source+=4;
        q->audit_step(base,save,check);
        require(q->audit_done && loads==0 && !q->restore_invoked && !q->preserve_applied);
        if(test==0)require(q->audit_stable && !q->fault && saves==2);
        if(test==1 || test==2)require(q->fault && saves==1);
        if(test==3)require(!q->audit_stable && q->audit_source_changes==1 && !q->fault);
        auto n=saves;q->audit_step(base,save,check);require(saves==n);
    }
    for(unsigned test=0;test<3;++test){
        auto q=std::make_unique<P>();q->delay_audit=true;q->throw_audit=true;q->throw_requested=true;saves=loads=0;
        *reinterpret_cast<unsigned*>(base+0x470D0C4)=100;
        *reinterpret_cast<std::uint16_t*>(base+0x475A04A)=487;
        *reinterpret_cast<std::uint16_t*>(base+0x47F143A)=10041;
        *reinterpret_cast<float*>(base+0x48462F4)=0;
        *reinterpret_cast<int*>(base+0x4846458)=-1;
        if(test==1)*reinterpret_cast<std::uint16_t*>(base+0x475A04A)=65535;
        q->audit_step(base,save,check);
        if(test==1)require(q->fault && saves==0);
        else{
            require(q->audit_started && saves==1);
            *reinterpret_cast<unsigned*>(base+0x470D0C4)=101;
            if(test==2)*reinterpret_cast<float*>(base+0x48462F4)=1;
            q->audit_step(base,save,check);
            require(q->audit_done && (test==2?q->fault:!q->fault) && saves==(test==2?1u:2u));
        }
        require(loads==0 && !q->restore_invoked && !q->preserve_applied);
    }
    *reinterpret_cast<std::uint16_t*>(base+0x475A04A)=65535;
    *reinterpret_cast<std::uint16_t*>(base+0x47F143A)=65535;
    *reinterpret_cast<float*>(base+0x48462F4)=1;
    {
        auto q=std::make_unique<P>();
        auto argument=base+0x600000;
        *reinterpret_cast<uintptr_t*>(argument)=argument+0x100;
        *reinterpret_cast<uintptr_t*>(argument+8)=argument+0x200;
        *reinterpret_cast<uintptr_t*>(argument+16)=argument+0x300;
        std::memset(reinterpret_cast<void*>(argument+0x100),0,8);
        std::memset(reinterpret_cast<void*>(argument+0x200),0,8);
        std::memset(reinterpret_cast<void*>(argument+0x300),0,24);
        require(!q->capture_audit_inputs(argument,1));
        require(q->capture_audit_inputs(argument,0));
        *reinterpret_cast<unsigned char*>(argument+0x200)=1;
        *reinterpret_cast<unsigned char*>(argument+0x317)=2;
        require(q->capture_audit_inputs(argument,1));
        require(q->input_checkpoints[1].changed_mask==((1ull<<8)|(1ull<<39)));
        require(q->input_checkpoints[1].sources[2]==argument+0x300);
        require(!q->capture_audit_inputs(argument,2));
        *reinterpret_cast<uintptr_t*>(argument)=0;
        require(!q->capture_audit_inputs(argument,0) && !q->input_checkpoints[0].valid);
        require(!q->restore_invoked && !q->preserve_applied);
    }
    // Full adjacent-save/older-load/replayed-save orchestration, plus changed-input rejection.
    {
        auto t=std::make_unique<sc6::research::TransformSnapshot>();
        uintptr_t owners[]={base+0x700000,base+0x701000};
        auto group=base+0x702000,node=base+0x703000;
        *reinterpret_cast<uintptr_t*>(owners[0]+0x10)=group;
        *reinterpret_cast<uintptr_t*>(group+0x48)=node;
        require(t->capture(owners) && t->count==4 && t->records[2].address==node);
        *reinterpret_cast<uintptr_t*>(node)=base+0x3E88DB8;
        *reinterpret_cast<unsigned*>(node+0x90)=0x12345678;
        require(t->capture(owners,base) && t->records[3].kind==12 && t->records[3].parent==2 && t->records[3].size==0x60);
        unsigned solver_value{};std::memcpy(&solver_value,t->records[3].bytes.data()+0x10,4);
        require(solver_value==0x12345678);
        auto solver_count=t->count;
        *reinterpret_cast<uintptr_t*>(node)=base+0x3E88DF8;
        *reinterpret_cast<unsigned*>(node+0x2F0)=0x3f800000;
        require(t->capture(owners,base) && t->count==solver_count+1 && t->records[4].kind==13 && t->records[4].parent==2);
        std::memcpy(&solver_value,t->records[4].bytes.data(),4);require(solver_value==0x3f800000);
        auto sway_a=std::make_unique<sc6::research::TransformSnapshot>();
        auto sway_b=std::make_unique<sc6::research::TransformSnapshot>();
        require(sway_a->capture(owners,base));
        *reinterpret_cast<unsigned*>(node+0x90)=42;
        *reinterpret_cast<unsigned*>(node+0xCC)=99;
        require(sway_b->capture(owners,base) && t->capture(owners,base));
        // Late auxiliary-scale mismatch must reject before writing the earlier vector.
        *reinterpret_cast<unsigned*>(node+0x2F0)=0;
        require(!sway_a->restore_sway(*sway_b,*t,base));
        require(*reinterpret_cast<unsigned*>(node+0x90)==42);
        *reinterpret_cast<unsigned*>(node+0x2F0)=0x3f800000;
        require(sway_a->restore_sway(*sway_b,*t,base));
        require(*reinterpret_cast<unsigned*>(node+0x90)==0x12345678 && *reinterpret_cast<unsigned*>(node+0xCC)==0);
        // A changed unapproved payload field rejects the entire correction.
        *reinterpret_cast<unsigned*>(node+0x90)=42;
        *reinterpret_cast<unsigned*>(node+0xD0)=1;
        require(sway_b->capture(owners,base) && t->capture(owners,base));
        require(!sway_a->restore_sway(*sway_b,*t,base));
        require(*reinterpret_cast<unsigned*>(node+0x90)==42);
        *reinterpret_cast<unsigned*>(node+0xD0)=0;
        *reinterpret_cast<short*>(node+0xD8)=4;
        *reinterpret_cast<short*>(node+0xDA)=4;
        for(unsigned j=0;j<4;++j)*reinterpret_cast<uintptr_t*>(node+0x170+j*0x60)=node;
        require(t->capture(owners,base) && t->count==solver_count+25);
        require(t->records[5].kind==14 && t->records[9].kind==15 && t->records[12].kind==18);
        require(t->records[12].address==node+0x80 && t->records[12].parent==10);
        require(t->records[13].kind==22 && t->records[13].address==node+0x130 && t->records[13].size==64 && t->records[13].parent==2);
        require(sway_a->capture(owners,base));
        *reinterpret_cast<unsigned*>(node+0x120)=123;
        *reinterpret_cast<unsigned*>(node+0x130)=456;
        require(sway_b->capture(owners,base) && t->capture(owners,base));
        *reinterpret_cast<unsigned*>(node+0x280)=789;
        require(!sway_a->restore_constraints(*sway_b,*t,base));
        require(*reinterpret_cast<unsigned*>(node+0x120)==123 && *reinterpret_cast<unsigned*>(node+0x130)==456);
        *reinterpret_cast<unsigned*>(node+0x280)=0;
        *reinterpret_cast<uintptr_t*>(node+0x290)=node+0x10;
        require(!sway_a->restore_constraints(*sway_b,*t,base));
        require(*reinterpret_cast<unsigned*>(node+0x120)==123);
        *reinterpret_cast<uintptr_t*>(node+0x290)=node;
        require(sway_a->restore_constraints(*sway_b,*t,base));
        require(*reinterpret_cast<unsigned*>(node+0x120)==0 && *reinterpret_cast<unsigned*>(node+0x130)==0);
        *reinterpret_cast<unsigned*>(node+0x120)=123;
        *reinterpret_cast<short*>(node+0xDA)=3;
        require(!sway_a->restore_constraints(*sway_b,*t,base));
        require(*reinterpret_cast<unsigned*>(node+0x120)==123);
        *reinterpret_cast<short*>(node+0xDA)=5;
        require(!t->capture(owners,base) && t->failure==12);
        *reinterpret_cast<short*>(node+0xDA)=-1;
        require(!t->capture(owners,base) && t->failure==12);
        *reinterpret_cast<short*>(node+0xDA)=1;
        auto hit_node=base+0x704000;
        *reinterpret_cast<uintptr_t*>(node+0x170)=hit_node;
        *reinterpret_cast<uintptr_t*>(hit_node)=base+0x3E88E38;
        *reinterpret_cast<unsigned*>(hit_node+0x40)=1234;
        *reinterpret_cast<unsigned*>(hit_node+0x10)=5678;
        *reinterpret_cast<unsigned*>(hit_node+0xC0)=9012;
        require(t->capture(owners,base));
        auto hit_count=t->count;
        require(t->records[13].kind==19 && t->records[13].parent==10 && t->records[13].size==64);
        require(t->records[14].kind==20 && t->records[15].kind==21);
        std::memcpy(&solver_value,t->records[13].bytes.data(),4);require(solver_value==1234);
        std::memcpy(&solver_value,t->records[14].bytes.data(),4);require(solver_value==5678);
        std::memcpy(&solver_value,t->records[15].bytes.data(),4);require(solver_value==9012);
        *reinterpret_cast<uintptr_t*>(hit_node)=0;
        require(t->capture(owners,base) && t->count==hit_count-3);
        *reinterpret_cast<uintptr_t*>(node+0x170)=1;
        require(!t->capture(owners,base) && t->failure==1 && t->failure_address==1);
        *reinterpret_cast<short*>(node+0xD8)=0;
        *reinterpret_cast<short*>(node+0xDA)=0;
        *reinterpret_cast<uintptr_t*>(node)=0;
        require(t->capture(owners,base) && t->count==solver_count-1);
        *reinterpret_cast<uintptr_t*>(node+0x28)=node;
        require(!t->capture(owners) && t->failure==5 && t->count==3 && t->failure_address==node);
        *reinterpret_cast<uintptr_t*>(node+0x28)=0;
        *reinterpret_cast<uintptr_t*>(group+0x40)=group;
        require(!t->capture(owners) && t->failure==3);
        *reinterpret_cast<uintptr_t*>(group+0x40)=0;
        *reinterpret_cast<uintptr_t*>(group+0x48)=1;
        require(!t->capture(owners) && t->failure==1 && t->count==2 && t->failure_address==1);
        *reinterpret_cast<uintptr_t*>(group+0x48)=node;
        auto original=std::make_unique<sc6::research::TransformSnapshot>();
        require(original->capture(owners));
        *reinterpret_cast<uintptr_t*>(group+0x48)=node+0x100;
        require(t->capture(owners) && !t->same_topology(*original));
        using TS=sc6::research::TransformSnapshot;
        TS::TopologyDiagnostic topology{};
        require(!t->same_topology(*original,&topology) && topology[0]==4 && topology[2]==0);
        require(topology[9]!=topology[15]);
        require(t->copy_recorded_from(*original) && t->same_topology(*original,&topology) && !topology[0]);
        for(unsigned field=0;field<6;++field){
            require(t->copy_recorded_from(*original));
            auto& r=t->records[0];
            switch(field){case 0:++r.address;break;case 1:++r.side;break;case 2:++r.kind;break;
                case 3:++r.list;break;case 4:++r.parent;break;case 5:++r.size;break;}
            require(!t->same_topology(*original,&topology) && topology[0]==4 && topology[1]==0 && topology[2]==field);
            require(topology[3]!=topology[4]);
        }
        require(t->copy_recorded_from(*original));++t->count;
        require(!t->same_topology(*original,&topology) && topology[0]==2);
        t->count=static_cast<unsigned>(t->records.size()+1);
        require(!t->same_topology(*original,&topology) && topology[0]==3);
        t->valid=false;require(!t->same_topology(*original,&topology) && topology[0]==1);
        auto groups=base+0x710000,nodes=base+0x720000;
        *reinterpret_cast<uintptr_t*>(owners[0]+0x10)=groups;
        for(unsigned i=0;i<TS::group_limit;++i)
            *reinterpret_cast<uintptr_t*>(groups+i*0x100+0x40)=i+1<TS::group_limit?groups+(i+1)*0x100:0;
        require(t->capture(owners) && t->count==TS::group_limit+2);
        *reinterpret_cast<uintptr_t*>(groups+(TS::group_limit-1)*0x100+0x40)=groups+TS::group_limit*0x100;
        require(!t->capture(owners) && t->failure==4 && t->count==TS::group_limit+1);
        *reinterpret_cast<uintptr_t*>(groups+0x40)=0;
        *reinterpret_cast<uintptr_t*>(groups+0x48)=nodes;
        for(unsigned i=0;i<TS::node_limit;++i)
            *reinterpret_cast<uintptr_t*>(nodes+i*0x100+0x28)=i+1<TS::node_limit?nodes+(i+1)*0x100:0;
        require(t->capture(owners) && t->count==TS::node_limit+3);
        *reinterpret_cast<uintptr_t*>(nodes+(TS::node_limit-1)*0x100+0x28)=nodes+TS::node_limit*0x100;
        require(!t->capture(owners) && t->failure==6 && t->count==TS::node_limit+2);
        *reinterpret_cast<uintptr_t*>(owners[0]+0x10)=0;
        auto manager=base+0x790000,global_nodes=base+0x791000;
        *reinterpret_cast<uintptr_t*>(base+0x470E038)=manager;
        *reinterpret_cast<uintptr_t*>(manager)=global_nodes;
        for(unsigned i=0;i<TS::global_node_limit;++i)
            *reinterpret_cast<uintptr_t*>(global_nodes+i*32+16)=i+1<TS::global_node_limit?global_nodes+(i+1)*32:0;
        require(t->capture(owners,base) && t->count==TS::global_node_limit+7);
        *reinterpret_cast<uintptr_t*>(global_nodes+(TS::global_node_limit-1)*32+16)=global_nodes+TS::global_node_limit*32;
        require(!t->capture(owners,base) && t->failure==11);
        *reinterpret_cast<uintptr_t*>(global_nodes+16)=global_nodes;
        require(!t->capture(owners,base) && t->failure==10);
        *reinterpret_cast<uintptr_t*>(manager)=1;
        require(!t->capture(owners,base) && t->failure==1);
        *reinterpret_cast<uintptr_t*>(manager)=global_nodes;
        *reinterpret_cast<uintptr_t*>(global_nodes+16)=0;
        *reinterpret_cast<uintptr_t*>(global_nodes)=base+0x3E88CB8;
        *reinterpret_cast<unsigned*>(global_nodes+0x120)=0x12345678;
        require(t->capture(owners,base) && t->count==11);
        require(t->records[10].kind==9 && t->records[10].size==12);
        unsigned value{};std::memcpy(&value,t->records[10].bytes.data(),4);require(value==0x12345678);
        *reinterpret_cast<uintptr_t*>(global_nodes)=base+0x3E88C88;
        require(t->capture(owners,base) && t->count==11);
        *reinterpret_cast<uintptr_t*>(global_nodes)=0;
        require(t->capture(owners,base) && t->count==8);
        *reinterpret_cast<uintptr_t*>(global_nodes)=base+0x3E88C88;
        auto wind_a=std::make_unique<TS>(),wind_b=std::make_unique<TS>();
        *reinterpret_cast<unsigned*>(global_nodes+0x70)=0;
        std::memset(reinterpret_cast<void*>(base+0x485EB30),0,104);
        std::memset(reinterpret_cast<void*>(base+0x470E2B0),0,24);
        require(wind_a->capture(owners,base));
        *reinterpret_cast<unsigned*>(global_nodes+0x70)=0x3f000000;
        *reinterpret_cast<unsigned*>(base+0x485EB30)=123;
        *reinterpret_cast<unsigned*>(base+0x470E2B0)=456;
        require(wind_b->capture(owners,base) && t->capture(owners,base));
        *reinterpret_cast<unsigned*>(base+0x470E2B0)=789;
        TS::WindDiagnostic shared_diag{};
        static_assert(sizeof(TS::SharedStateDetail)==2080);
        require(!wind_a->restore_wind(*wind_b,*t,base,true,&shared_diag));
        require(shared_diag.shared.reason==3 && shared_diag.shared.kind==11 && shared_diag.shared.live_valid==1);
        require(shared_diag.shared.address==base+0x470E2B0 && shared_diag.shared.offset==0);
        unsigned shared_live{},shared_current{};
        std::memcpy(&shared_live,shared_diag.shared.live.data(),4);
        std::memcpy(&shared_current,shared_diag.shared.current.data(),4);
        require(shared_live==789 && shared_current==456 && shared_diag.failure_line!=0);
        auto shared_record=shared_diag.shared.record;
        t->records[shared_record].bytes[0]^=std::byte{1};
        require(!wind_a->restore_wind(*wind_b,*t,base,true,&shared_diag));
        require(shared_diag.shared.reason==2 && shared_diag.shared.record==shared_record && !shared_diag.shared.live_valid);
        t->records[shared_record].bytes[0]^=std::byte{1};
        require(*reinterpret_cast<unsigned*>(global_nodes+0x70)==0x3f000000);
        *reinterpret_cast<unsigned*>(base+0x470E2B0)=456;
        require(wind_a->restore_wind(*wind_b,*t,base));
        require(*reinterpret_cast<unsigned*>(global_nodes+0x70)==0 && *reinterpret_cast<unsigned*>(base+0x485EB30)==0 && *reinterpret_cast<unsigned*>(base+0x470E2B0)==0);
        // Stage wind has its own bounded payload and narrow mutation mask.
        std::memset(reinterpret_cast<void*>(global_nodes+0x18),0,0x1C8);
        *reinterpret_cast<uintptr_t*>(global_nodes)=base+0x3E88CE8;
        require(wind_a->capture(owners,base));
        for(unsigned off:{0x30u,0x40u,0x70u,0x80u,0x90u,0xA0u,0xB0u,0xD0u,0x130u,0x138u,0x160u,0x190u,0x1D0u})
            *reinterpret_cast<float*>(global_nodes+off)=0.25f;
        *reinterpret_cast<unsigned*>(global_nodes+0x60)=1;
        require(wind_b->capture(owners,base));
        require(wind_a->restore_wind(*wind_b,*wind_b,base,true));
        require(*reinterpret_cast<float*>(global_nodes+0x40)==0.25f);
        *reinterpret_cast<unsigned*>(global_nodes+0x38)=1;
        require(t->capture(owners,base));
        require(!wind_a->restore_wind(*t,*t,base));
        require(*reinterpret_cast<float*>(global_nodes+0x40)==0.25f);
        *reinterpret_cast<unsigned*>(global_nodes+0x38)=0;
        *reinterpret_cast<unsigned*>(global_nodes+0x40)=0x7F800000;
        require(t->capture(owners,base));require(!wind_a->restore_wind(*t,*t,base));
        *reinterpret_cast<float*>(global_nodes+0x40)=0.25f;
        require(wind_b->capture(owners,base));
        require(wind_a->restore_wind(*wind_b,*wind_b,base));
        require(*reinterpret_cast<float*>(global_nodes+0x40)==0 && *reinterpret_cast<float*>(global_nodes+0x1D0)==0);
        *reinterpret_cast<uintptr_t*>(global_nodes)=base+0x3E88D18;
        *reinterpret_cast<unsigned*>(global_nodes+0x17C)=0x12345678;
        require(t->capture(owners,base));
        unsigned shock_records=0;
        for(unsigned i=0;i<t->count;++i)if(t->records[i].kind==23){
            auto& r=t->records[i];++shock_records;
            require(r.address==global_nodes+0x18 && r.size==0x168 && r.side==2);
            unsigned tail{};std::memcpy(&tail,r.bytes.data()+0x164,4);require(tail==0x12345678);
        }
        require(shock_records==1 && !t->restore_wind(*t,*t,base,true));
        require(*reinterpret_cast<unsigned*>(global_nodes+0x17C)==0x12345678);
        *wind_a=*t;
        ++*reinterpret_cast<unsigned*>(global_nodes+0x60);
        *reinterpret_cast<float*>(global_nodes+0x40)=0.5f;
        require(wind_b->capture(owners,base));
        require(wind_a->restore_wind(*wind_b,*wind_b,base,true));
        require(*reinterpret_cast<float*>(global_nodes+0x40)==0.5f);
        for(unsigned i=0;i<wind_b->count;++i)if(wind_b->records[i].kind==23){
            wind_b->records[i].bytes[0x20]^=std::byte{1};
            *reinterpret_cast<unsigned char*>(global_nodes+0x38)^=1;
        }
        require(!wind_a->restore_wind(*wind_b,*wind_b,base));
        require(*reinterpret_cast<float*>(global_nodes+0x40)==0.5f);
        *reinterpret_cast<unsigned char*>(global_nodes+0x38)^=1;
        require(wind_b->capture(owners,base));
        require(wind_a->restore_wind(*wind_b,*wind_b,base));
        require(*reinterpret_cast<unsigned*>(global_nodes+0x40)==0);
        require(*reinterpret_cast<unsigned*>(global_nodes+0x17C)==0x12345678);
        // Each newly identified phase/output is restored, but nonfinite data
        // and changes to adjacent configuration reject before any write.
        for(unsigned off:{0x70u,0x80u,0x90u,0xA0u,0xB0u,0x120u,0x124u}){
            require(wind_a->capture(owners,base));
            ++*reinterpret_cast<unsigned*>(global_nodes+0x60);
            *reinterpret_cast<float*>(global_nodes+off)=0.25f;
            require(wind_b->capture(owners,base));
            require(wind_a->restore_wind(*wind_b,*wind_b,base,true));
            require(*reinterpret_cast<float*>(global_nodes+off)==0.25f);
            *reinterpret_cast<unsigned*>(global_nodes+off)=0x7F800000u;
            require(t->capture(owners,base));
            require(!wind_a->restore_wind(*t,*t,base));
            *reinterpret_cast<float*>(global_nodes+off)=0.25f;
            *reinterpret_cast<unsigned char*>(global_nodes+0x74)^=1;
            require(t->capture(owners,base));
            require(!wind_a->restore_wind(*t,*t,base));
            require(*reinterpret_cast<float*>(global_nodes+off)==0.25f);
            *reinterpret_cast<unsigned char*>(global_nodes+0x74)^=1;
            require(wind_a->restore_wind(*wind_b,*wind_b,base));
        }
        *reinterpret_cast<uintptr_t*>(base+0x470E038)=0;
    }
    {
        using A=sc6::research::IdleTrajectoryAudit;
        auto audit=std::make_unique<A>(true,4);
        auto& f=*audit->frames[1];f.input_valid=true;
        unsigned positive=0x3B800100u,negative=0xBB810000u;
        std::memcpy(audit->frames[0]->inputs.data()+36,&negative,4);
        std::memcpy(f.inputs.data()+36,&positive,4);
        require(audit->inputs_supported(f) && !f.inputs_equal);
        require(audit->pair_observable(253,true));
        require(!audit->pair_observable(249,true) && !audit->pair_observable(253,false));
        for(unsigned off:{0u,16u,20u}){
            f.inputs[off]=std::byte{1};require(!audit->inputs_supported(f));f.inputs[off]=std::byte{0};
        }
        for(unsigned bits:{0x7FC00000u,0x7F800000u,0x3F800001u,0xBF800001u}){
            std::memcpy(f.inputs.data()+36,&bits,4);require(!audit->inputs_supported(f));
        }
        std::memcpy(f.inputs.data()+36,&positive,4);
        f.input_valid=false;require(!audit->inputs_supported(f));f.input_valid=true;
        audit->target_frames=3;
        require(!audit->inputs_supported(f) && !audit->pair_observable(253,true));
        audit->target_frames=4;audit->throw_mode=false;
        require(!audit->inputs_supported(f) && !audit->pair_observable(253,true));
    }
    {
        auto charge=std::make_unique<sc6::research::IdleTrajectoryAudit>(false,4);
        charge->charge_mode=true;
        auto saved_count=saves,loaded_count=loads;
        charge->step(base,0,trajectory_save,check);
        require(charge->fault && charge->count==0 && saves==saved_count && loads==loaded_count);
        require(!charge->combat_checks[0] && !charge->throw_mode);
        auto charge_checks=std::make_unique<sc6::research::IdleTrajectoryAudit>(false,4,true);
        require(charge_checks->charge_mode && !charge_checks->throw_mode);
        for(unsigned i=0;i<3;++i)require(bool(charge_checks->combat_checks[i]));
        require(!charge_checks->check_combat_pair(base,0)); // Missing references: no native calls.
        require(!charge_checks->check_combat_pair(base,3));
        require(charge_checks->pair_observable(253,true));
        for(unsigned bit:{1u,4u,8u,16u,32u,64u,128u})require(!charge_checks->pair_observable(255u&~bit,true));
        require(!charge_checks->pair_observable(255,false));
        auto conflicting=std::make_unique<sc6::research::IdleTrajectoryAudit>(true,4,true);require(conflicting->fault);
        auto short_charge=std::make_unique<sc6::research::IdleTrajectoryAudit>(false,3,true);require(!short_charge->fault && short_charge->combat_checks[0] && short_charge->combat_checks[1]);
        require(saves==saved_count && loads==loaded_count);

        auto& reference=*charge->frames[0];auto& next=*charge->frames[1];
        unsigned before=0x807u,after=0x7u;
        std::memcpy(reference.inputs.data()+16,&before,4);
        std::memcpy(next.inputs.data()+16,&after,4);next.input_valid=true;
        require(charge->inputs_supported(next) && !next.inputs_equal);
        require(!sc6::research::RecordedInput::compatible(reference.inputs,next.inputs));
        charge->charge_mode=false;charge->throw_mode=true;
        require(!charge->inputs_supported(next));charge->throw_mode=false;charge->charge_mode=true;
        for(unsigned invalid:{0u,0x407u,0x1007u}){
            std::memcpy(next.inputs.data()+16,&invalid,4);require(!charge->inputs_supported(next));}
        std::memcpy(next.inputs.data()+16,&after,4);
        for(unsigned off:{0u,8u,20u}){next.inputs[off]=std::byte{1};
            require(!charge->inputs_supported(next));next.inputs[off]=std::byte{0};}
        std::memcpy(reference.inputs.data(),&before,4);
        std::memcpy(next.inputs.data(),&after,4);
        unsigned released=0;std::memcpy(next.inputs.data()+16,&released,4);
        require(charge->inputs_supported(next));
        require(!sc6::research::RecordedInput::compatible(reference.inputs,next.inputs));
        for(unsigned off:{0u,4u,8u,12u,20u}){next.inputs[off]^=std::byte{1};
            require(!charge->inputs_supported(next));next.inputs[off]^=std::byte{1};}
        charge->charge_mode=false;charge->throw_mode=true;require(!charge->inputs_supported(next));
        charge->throw_mode=false;charge->charge_mode=true;
        // Auxiliary flags can clear one boundary before the player word does.
        std::memcpy(next.inputs.data(),&before,4);
        require(charge->inputs_supported(next));
        require(!sc6::research::RecordedInput::compatible(reference.inputs,next.inputs));
        for(unsigned off:{0u,4u,8u,12u,20u}){next.inputs[off]^=std::byte{1};
            require(!charge->inputs_supported(next));next.inputs[off]^=std::byte{1};}
        charge->charge_mode=false;charge->throw_mode=true;require(!charge->inputs_supported(next));
        charge->throw_mode=false;charge->charge_mode=true;
        unsigned nan=0x7FC00000u;std::memcpy(next.inputs.data()+36,&nan,4);
        require(!charge->inputs_supported(next));

        require(P::charge_values(219,65535,0.0f,0));
        require(!P::charge_values(487,10041,0.0f,-1));
        require(!P::charge_values(219,65535,1.0f,0));
    }
    for(unsigned test=0;test<9;++test){
        auto audit=std::make_unique<sc6::research::IdleTrajectoryAudit>(test==6,test==8?4u:3u);
        auto scratch=std::make_unique<Ring>();require(ring0->restore(base,*scratch));
        for(unsigned side=0;side<2;++side){auto player=P::ptr(base+0x470DE90+side*8);
            *reinterpret_cast<uintptr_t*>(player+0x971F8)=base+0x500000+side*0x10000;}
        audit->throw_mode=(test==6 || test==7);
        *reinterpret_cast<std::uint16_t*>(base+0x475A04A)=(test==6 || test==7)?487:65535;
        *reinterpret_cast<std::uint16_t*>(base+0x47F143A)=(test==6 || test==7)?10041:65535;
        *reinterpret_cast<float*>(base+0x48462F4)=(test==6 || test==7)?0.0f:1.0f;
        *reinterpret_cast<int*>(base+0x4846458)=-1;
        mode=test==4?12:0;saves=loads=checks=0;
        auto argument=base+0x400000;
        std::memset(reinterpret_cast<void*>(argument),0,0x200);
        for(unsigned j=0;j<3;++j)*reinterpret_cast<uintptr_t*>(argument+j*8)=argument+0x100+j*0x40;
        *reinterpret_cast<std::uint16_t*>(base+0x4717840)=0;
        *reinterpret_cast<unsigned*>(base+0x485EB28)=123;
        for(unsigned frame=0;frame<audit->target_frames;++frame){
            *reinterpret_cast<unsigned*>(base+0x470D0C4)=100+frame+(test==2 && frame?1u:0u);
            if(frame){
                require(scratch->capture(base));
                for(auto& h:scratch->headers){auto index=(h.index+2u)%3u;
                    *reinterpret_cast<unsigned*>(h.object+0x20)=index;
                    *reinterpret_cast<uintptr_t*>(h.object+0x28)=h.slots[index];
                    *reinterpret_cast<uintptr_t*>(h.object+0x30)=h.current;
                }
                if(test==7)*reinterpret_cast<std::uint16_t*>(base+0x475A04A)=488;
                if(test==1)*reinterpret_cast<unsigned*>(argument+0x100)=1;
                if(test==3)++audit->frames[0]->saved.traces[0].writer;
            }
            audit->step(base,test==5?0:argument,trajectory_save,check);
            if(audit->fault)break;
        }
        require(loads==0);
        if(test==1)require(audit->frames[1]->input_valid && audit->frames[1]->input_changed_mask==1 && audit->frames[1]->inputs[0]==std::byte{1});
        if(test==5)require(audit->frames[0]->input_attempted && !audit->frames[0]->input_valid);
        if(test==8){require(audit->done && !audit->fault && audit->count==4 && saves==4 && loads==0);
            require(audit->visuals->transforms[3].valid && audit->visuals->cameras[3].valid);
            audit->step(base,argument,trajectory_save,check);require(saves==4);
            {
                using Boundary=sc6::research::NativeBoundarySnapshot;
                auto source=std::make_unique<Boundary>(Boundary::Mode::idle);
                auto current=std::make_unique<Boundary>(Boundary::Mode::idle);
                auto original_outer=*reinterpret_cast<unsigned*>(base+P::key_rvas[0]);
                std::memset(reinterpret_cast<void*>(base+0x600000),0,0x2B0);
                *reinterpret_cast<uintptr_t*>(base+0x600000)=base+0x3E87EB0;
                *reinterpret_cast<float*>(base+0x600000+0x1D4)=0.375f;
                auto aligned=std::make_unique<sc6::research::NativeAdjacentRestore>();
                track_boundary_stack=true;boundary_stack_count=0;
                const auto auxiliary=P::ptr(argument+16)+8;
                *reinterpret_cast<unsigned*>(auxiliary)=0xBB810000u;
                auto missing_bridge=std::make_unique<sc6::research::NativeAdjacentRestore>();
                require(!missing_bridge->replay_step(*source,base,argument,false,trajectory_save,boundary_load,check));
                require(missing_bridge->fault && !source->attempted && loads==0);
                require(aligned->replay_step(*source,base,argument,true,trajectory_save,boundary_load,check));
                require(!aligned->next_argument && !aligned->replay_delivered);
                require(source->valid && !aligned->attempted && loads==0);
                require(!source->capture(base,argument,trajectory_save,check));
                ++*reinterpret_cast<unsigned*>(base+P::key_rvas[0]);
                for(auto& h:source->data->rings[0].headers){auto index=(h.index+2u)%3u;
                    *reinterpret_cast<unsigned*>(h.object+0x20)=index;
                    *reinterpret_cast<uintptr_t*>(h.object+0x28)=h.slots[index];
                    *reinterpret_cast<uintptr_t*>(h.object+0x30)=h.current;
                }
                *reinterpret_cast<unsigned*>(base+0x600000+0x29C)=1;
                *reinterpret_cast<unsigned*>(base+0x600000+0x2A0)=1;
                *reinterpret_cast<unsigned*>(auxiliary)=0x3B800100u;
                require(current->capture(base,argument,trajectory_save,check));
                require(source->structural_readiness(*current)==Boundary::Rejection::none && loads==0);
                source->mode=current->mode=Boundary::Mode::combat_capture_only;
                require(source->structural_readiness(*current)==Boundary::Rejection::none);
                auto capture_only_restore=std::make_unique<P>();Boundary::Rejection capture_only_reason{};
                require(!source->prepare_adjacent(*current,*capture_only_restore,&capture_only_reason) && capture_only_reason==Boundary::Rejection::mode && !capture_only_restore->saved.sealed && loads==0);
                {
                    auto gs=std::make_unique<Boundary>(Boundary::Mode::general_combat);
                    auto gn=std::make_unique<Boundary>(Boundary::Mode::general_combat);
                    gs->module=gn->module=base;gs->thread=gn->thread=GetCurrentThreadId();
                    gs->attempted=gn->attempted=gs->valid=gn->valid=true;
                    *gs->data=*source->data;*gn->data=*current->data;
                    std::array<std::byte,88> finite_group{};
                    const auto root=gs->data->transforms[0].records[0].address;
                    const auto previous_group=P::ptr(root+0x10);
                    *reinterpret_cast<uintptr_t*>(root+0x10)=reinterpret_cast<uintptr_t>(finite_group.data());
                    require(gs->data->transforms[0].capture(gs->data->blends[0].owners,base,true));
                    require(gn->data->transforms[0].capture(gn->data->blends[0].owners,base,true));
                    for(auto* state:{gs.get(),gn.get()})for(auto& cam:state->data->cameras[0].records)if(cam.smoothing_valid){
                        unsigned one=0x3F800000u;std::memcpy(cam.targets.data()+0x68,&one,4);std::memcpy(cam.targets.data()+0x6C,&one,4);
                        *reinterpret_cast<unsigned*>(cam.owner+0x250)=one;*reinterpret_cast<unsigned*>(cam.owner+0x254)=one;
                    }
                    gn->data->delayed_inputs[0]=std::byte{1};
                    auto general=std::make_unique<P>();Boundary::Rejection general_reason{};
                    require(!gs->prepare_adjacent(*gn,*general,&general_reason) && general_reason==Boundary::Rejection::mode && loads==0);
                    const auto saved_trace=gn->data->saved.traces[0];
                    gn->data->saved.traces[0].source+=1;
                    auto moved_source=std::make_unique<P>();
                    require(gs->structural_readiness(*gn)==Boundary::Rejection::none);
                    require(!gs->prepare_adjacent(*gn,*moved_source,&general_reason,false,false,true));
                    require(general_reason==Boundary::Rejection::sources && moved_source->source_guard_reason==4 && !moved_source->restore_invoked && loads==0);
                    require(moved_source->audit_shape_changes==0 && moved_source->ring_save_unchanged && moved_source->ring_rotation_ok);
                    gn->data->saved.traces[0]=saved_trace;
                    general=std::make_unique<P>();
                    const bool gp=gs->prepare_adjacent(*gn,*general,&general_reason,false,false,true);
                    if(!gp)std::cerr<<"general prepare rejection="<<unsigned(general_reason)<<" mask="<<general->combat_preflight_pass_mask<<"\n";
                    require(gp && loads==0 && general->general_live_endpoint(base));
                    // Exercise real preparation across bounded distances, not only the routing mock.
                    {
                        const auto oldFrame=gn->data->before_keys[0];
                        const auto oldRings=std::make_unique<sc6::research::RingSnapshot>(gn->data->rings[0]);
                        const auto oldCamera=std::make_unique<sc6::research::CameraSnapshot>(gn->data->cameras[0]);
                        for(unsigned span:{2u,3u}){
                            gn->data->before_keys[0]=gs->data->before_keys[0]+span;
                            for(unsigned i=0;i<oldRings->headers.size();++i){
                                const auto& a=gs->data->rings[0].headers[i];auto& b=gn->data->rings[0].headers[i];
                                b.index=(a.index+2*span)%3;b.current=a.slots[b.index];b.previous=a.slots[(a.index+2*(span-1))%3];
                            }
                            for(unsigned i=0;i<gn->data->cameras[0].count;++i){
                                auto& b=gn->data->cameras[0].records[i];const auto& a=gs->data->cameras[0].records[i];
                                if(!a.smoothing_valid)continue;
                                unsigned count{},index{};std::memcpy(&count,a.smoothing.data()+72,4);std::memcpy(&index,a.smoothing.data()+76,4);
                                count=count>0x7FFFFFFFu-span?0x7FFFFFFFu:count+span;index=(index+span)%16;
                                std::memcpy(b.smoothing.data()+72,&count,4);std::memcpy(b.smoothing.data()+76,&index,4);
                            }
                            auto bounded=std::make_unique<P>();
                            require(gs->prepare_adjacent(*gn,*bounded,&general_reason,false,false,true,span));
                            require(bounded->restore_span==span && !bounded->restore_invoked && loads==0);
                            require(!gs->prepare_adjacent(*gn,*bounded,&general_reason,false,false,true,1));
                        }
                        gn->data->before_keys[0]=oldFrame;gn->data->rings[0]=*oldRings;gn->data->cameras[0]=*oldCamera;
                    }

                    general->original_next=general->measurements[2];general->delayed_loaded=true;
                    require(general->general_live_endpoint(base));
                    ++general->original_next.rng;require(!general->general_live_endpoint(base));--general->original_next.rng;
                    general->original_next.fields[0][8]^=std::byte{1};require(!general->general_live_endpoint(base));general->original_next.fields[0][8]^=std::byte{1};
                    general->original_next.players[0]++;require(!general->general_live_endpoint(base));general->original_next.players[0]--;
                    general->delayed_loaded=false;
                    const auto source_before=general->after_save.traces[0].source;
                    general->after_save.traces[0].source=source_before+1;
                    require(!general->delayed_sources_ok() && general->source_guard_reason==4 && general->source_guard_record==0 && general->source_guard_next==source_before+1 && loads==0);
                    general->after_save.traces[0].source=source_before;
                    require(general->delayed_sources_ok() && general->source_guard_reason==0);
                    ++*reinterpret_cast<unsigned*>(base+P::key_rvas[1]);require(!general->general_live_endpoint(base));--*reinterpret_cast<unsigned*>(base+P::key_rvas[1]);
                    const auto general_player=P::ptr(base+0x470DE90);
                    *reinterpret_cast<unsigned char*>(general_player+0x97100)^=1;require(!general->general_live_endpoint(base));*reinterpret_cast<unsigned char*>(general_player+0x97100)^=1;
                    auto stale=std::make_unique<sc6::research::NativeAdjacentRestore>();
                    require(stale->prepare_captured(*gs,*gn,false,false,true));
                    ++*reinterpret_cast<unsigned char*>(general_player+0x97100);
                    sc6::research::RecordedInput stale_input;
                    require(!stale->restore_captured(*gs,base,stale_input.assign(gn->data->delayed_inputs),trajectory_save,boundary_load,check) && !stale->probe->restore_invoked && loads==0);
                    --*reinterpret_cast<unsigned char*>(general_player+0x97100);
                    const auto old_move1=*reinterpret_cast<uint16_t*>(base+0x475A04A),old_move2=*reinterpret_cast<uint16_t*>(base+0x47F143A);
                    *reinterpret_cast<uint16_t*>(base+0x475A04A)=777;*reinterpret_cast<uint16_t*>(base+0x47F143A)=888;
                    auto unrestricted_capture=std::make_unique<Boundary>(Boundary::Mode::combat_capture_only);
                    track_boundary_stack=false;
                    require(unrestricted_capture->capture_general(base,argument,trajectory_save,check) && unrestricted_capture->mode==Boundary::Mode::general_combat && unrestricted_capture->mode_values[2]==777 && unrestricted_capture->mode_values[3]==888);
                                        auto bad_general=std::make_unique<sc6::research::NativeAdjacentRestore>();
                    auto saved_general_inputs=gn->data->delayed_inputs;
                    unsigned bad_nan=0x7FC00000u;std::memcpy(gn->data->delayed_inputs.data()+24,&bad_nan,4);
                    require(!bad_general->preflight_general(*gs,*gn) && bad_general->rejection==Boundary::Rejection::inputs && !bad_general->probe->restore_invoked && loads==0);
                    gn->data->delayed_inputs=saved_general_inputs;
                    // A rejected archived blend pair must be caught before native load.
                    for(unsigned side=0;side<2;++side){
                        auto saved_blend=gn->data->blends[0];
                        const unsigned offset=140+20;
                        gn->data->blends[0].bytes[side][offset]^=std::byte{1};
                        auto early=std::make_unique<sc6::research::NativeAdjacentRestore>();
                        require(!early->preflight_general(*gs,*gn));
                        require(early->rejection==Boundary::Rejection::combat && !early->probe->restore_invoked && loads==0);
                        require(early->probe->blend_pair_diagnostic.reason==6 && early->probe->blend_pair_diagnostic.side==side && early->probe->blend_pair_diagnostic.offset==offset);
                        gn->data->blends[0]=saved_blend;
                    }
                    auto early_ok=std::make_unique<sc6::research::NativeAdjacentRestore>();
                    require(early_ok->preflight_general(*gs,*gn) && !early_ok->probe->restore_invoked && loads==0);

                    auto gr=std::make_unique<sc6::research::NativeAdjacentRestore>();
                    require(gr->prepare_captured(*gs,*gn,false,false,true));
                    sc6::research::RecordedInput general_input;
                    track_boundary_stack=false;
                    const bool loaded=gr->restore_captured(*gs,base,general_input.assign(gn->data->delayed_inputs),trajectory_save,boundary_load,check);
                    if(!loaded)std::cerr<<"general load stage="<<gr->diagnostic_stage<<" line="<<gr->probe->delayed_failure_line<<" mask="<<gr->probe->combat_preflight_pass_mask<<" completion="<<gr->completion.failure_line<<"\n";
                    require(loaded && gr->verified && gr->probe->restore_invoked && loads==1);
                    *reinterpret_cast<uint16_t*>(base+0x475A04A)=old_move1;*reinterpret_cast<uint16_t*>(base+0x47F143A)=old_move2;
                    // Restore the independently captured fixture endpoint so
                    // the pre-existing idle tests still run from identical state.
                    *reinterpret_cast<uintptr_t*>(root+0x10)=previous_group;
                    for(unsigned i=0;i<std::size(P::key_rvas);++i)*reinterpret_cast<unsigned*>(base+P::key_rvas[i])=current->data->before_keys[i];
                    *reinterpret_cast<unsigned*>(base+0x485EB28)=current->data->measurements[0].rng;
                    for(unsigned side=0;side<2;++side){const auto gp_player=current->data->before_players[side];const auto& fields=current->data->measurements[0].fields[side];
                        std::memcpy(reinterpret_cast<void*>(gp_player+0x43DF0),fields.data(),8);std::memcpy(reinterpret_cast<void*>(gp_player+0x97100),fields.data()+8,120);std::memcpy(reinterpret_cast<void*>(gp_player+0x971E8),fields.data()+128,96);
                    }
                    const auto& cam=current->data->cameras[0].records[0];
                    std::memcpy(reinterpret_cast<void*>(cam.owner),cam.bytes.data(),cam.bytes.size());
                    std::memcpy(reinterpret_cast<void*>(cam.owner+0x1D0),cam.response.data(),12);
                    std::memcpy(reinterpret_cast<void*>(cam.owner+0x1E0),cam.smoothing.data(),8);std::memcpy(reinterpret_cast<void*>(cam.owner+0x25C),cam.smoothing.data()+8,72);
                    std::memcpy(reinterpret_cast<void*>(cam.owner+0x1E8),cam.targets.data(),116);std::memcpy(reinterpret_cast<void*>(cam.owner+0x2A4),cam.targets.data()+116,12);
                    require(current->data->rings[0].restore(base,*scratch));loads=0;track_boundary_stack=true;
                }
                source->mode=current->mode=Boundary::Mode::idle;
                const auto original_frame=source->data->before_keys[0],next_frame=current->data->before_keys[0];
                source->data->before_keys[0]=UINT32_MAX;current->data->before_keys[0]=0;
                require(source->structural_readiness(*current)==Boundary::Rejection::frame);
                source->data->before_keys[0]=original_frame;current->data->before_keys[0]=next_frame;
                auto restore=std::make_unique<P>();
                require(source->prepare_adjacent(*current,*restore) && !restore->fault && loads==0);
                require(source->data->saved.reads==0 && restore->saved.reads==0);
                require(!source->prepare_adjacent(*current,*restore));
                const auto valid_current_rng=current->data->measurements[0].rng;
                ++current->data->measurements[0].rng;
                auto changed_rng=std::make_unique<P>();
                require(source->prepare_adjacent(*current,*changed_rng) && !changed_rng->restore_invoked);
                current->data->measurements[0].rng=valid_current_rng;
                for(unsigned reject=0;reject<5;++reject){
                    auto rejected=std::make_unique<P>();
                    if(reject==0)current->data->measurements[0].rng=0;
                    if(reject==1)++current->data->before_keys[0];
                    if(reject==2)current->data->delayed_inputs[0]=std::byte{1};
                    if(reject==3)current->module+=8;
                    if(reject==4)current->mode=Boundary::Mode::reversal;
                    Boundary::Rejection reason{};
                    require(!source->prepare_adjacent(*current,*rejected,&reason) && !rejected->restore_invoked && loads==0);
                    constexpr Boundary::Rejection expected[]={Boundary::Rejection::rng,Boundary::Rejection::frame,Boundary::Rejection::inputs,Boundary::Rejection::module,Boundary::Rejection::mode};
                    require(reason==expected[reject]);
                    if(reject==0)current->data->measurements[0].rng=valid_current_rng;
                    if(reject==1)--current->data->before_keys[0];
                    if(reject==2)current->data->delayed_inputs[0]=std::byte{0};
                    if(reject==3)current->module-=8;
                    if(reject==4)current->mode=Boundary::Mode::idle;
                }
                for(unsigned bits:{0x7FC00000u,0x7F800000u,0x3F800001u,0xBF800001u}){
                    auto original_source=source->data->delayed_inputs,original_current=current->data->delayed_inputs;
                    std::memcpy(source->data->delayed_inputs.data()+24,&bits,4);
                    std::memcpy(current->data->delayed_inputs.data()+24,&bits,4);
                    auto rejected=std::make_unique<P>();Boundary::Rejection reason{};
                    require(!source->prepare_adjacent(*current,*rejected,&reason) && reason==Boundary::Rejection::inputs && loads==0);
                    source->data->delayed_inputs=original_source;current->data->delayed_inputs=original_current;
                }
                // Prepared snapshots do not authorize loading after the live
                // boundary changes. Exercise the real adjacent loader guard.
                for(unsigned changed=0;changed<3;++changed){
                    auto guarded=std::make_unique<sc6::research::NativeAdjacentRestore>();
                    require(guarded->prepare_captured(*source,*current));
                    const auto field=base+P::key_rvas[changed];
                    const auto old=*reinterpret_cast<unsigned*>(field);
                    *reinterpret_cast<unsigned*>(field)=old^1u;
                    sc6::research::RecordedInput guard_input;
                    require(!guarded->restore_captured(*source,base,guard_input.assign(current->data->delayed_inputs),trajectory_save,boundary_load,check));
                    require(guarded->fault && guarded->diagnostic_stage==2 && !guarded->probe->restore_invoked && loads==0);
                    *reinterpret_cast<unsigned*>(field)=old;
                    // The failed transaction is latched, even after fixing the boundary.
                    require(!guarded->restore_captured(*source,base,guard_input.assign(current->data->delayed_inputs),trajectory_save,boundary_load,check) && loads==0);
                }
                {
                    auto guarded=std::make_unique<sc6::research::NativeAdjacentRestore>();
                    require(guarded->prepare_captured(*source,*current));
                    auto wrong=current->data->delayed_inputs;wrong[0]^=std::byte{1};
                    sc6::research::RecordedInput guard_input;
                    require(!guarded->restore_captured(*source,base,guard_input.assign(wrong),trajectory_save,boundary_load,check));
                    require(guarded->diagnostic_stage==4 && guarded->fault && !guarded->probe->restore_invoked && loads==0);
                }
                auto attempt=std::move(aligned);
                bool restored=attempt->replay_step(*source,base,argument,true,trajectory_save,boundary_load,check);
                track_boundary_stack=false;
                require(boundary_stack_count>=3 && boundary_stack_addresses[0]==boundary_stack_addresses[2]);
                if(!restored)std::cerr<<"boundary restore fault: prepared="<<attempt->prepared<<" loaded="<<attempt->probe->delayed_loaded<<" preserve="<<attempt->probe->preserve_verified<<" weights="<<attempt->probe->blend_weights_verified<<" blocks="<<attempt->probe->blend_blocks_verified<<" wind="<<attempt->probe->wind_restore_verified<<" camera="<<attempt->probe->camera_restore_verified<<" bytes="<<attempt->probe->restored_byte_differences<<" verified="<<attempt->verified<<"\n";
                require(restored && attempt->verified && !attempt->fault && loads==1);
                require(attempt->restore_input_used && attempt->restore_input.bytes==source->data->delayed_inputs);
                require(attempt->probe->input_checkpoints[1].bytes==source->data->delayed_inputs);
                require(*reinterpret_cast<unsigned*>(auxiliary)==0x3B800100u);
                require(attempt->replay_delivered && attempt->next_argument && !attempt->replay_checked);
                std::array<std::byte,40> delivered{};
                require(P::input_bytes(attempt->next_argument,delivered) && delivered==source->data->delayed_inputs);
                // No game update occurred in this fixture: comparison must fail,
                // never count an immediate restore as a successful replay.
                require(!attempt->replay_step(*source,base,argument,true,trajectory_save,boundary_load,check));
                require(attempt->replay_checked && !attempt->replay_verified && attempt->fault && !attempt->next_argument && loads==1);
                require(!attempt->replay_step(*source,base,argument,true,trajectory_save,boundary_load,check) && loads==1);
                require(!attempt->run(*source,base,argument,trajectory_save,boundary_load,check) && loads==1);
                require(source->data->saved.reads==0);
                for(boundary_failure=1;boundary_failure<=3;++boundary_failure){
                    require(current->data->rings[0].restore(base,*scratch));
                    *reinterpret_cast<unsigned*>(base+P::key_rvas[0])=original_outer+1;
                    *reinterpret_cast<unsigned*>(base+0x485EB28)=source->data->measurements[0].rng;
                    for(unsigned side=0;side<2;++side){auto player=source->data->before_players[side];auto& fields=source->data->measurements[0].fields[side];
                        std::memcpy(reinterpret_cast<void*>(player+0x43DF0),fields.data(),8);
                        std::memcpy(reinterpret_cast<void*>(player+0x97100),fields.data()+8,120);
                        std::memcpy(reinterpret_cast<void*>(player+0x971E8),fields.data()+128,96);
                    }
                    auto& camera=current->data->cameras[0].records[0];
                    std::memcpy(reinterpret_cast<void*>(camera.owner),camera.bytes.data(),camera.bytes.size());
                    std::memcpy(reinterpret_cast<void*>(camera.owner+0x1D0),camera.response.data(),12);
                    std::memcpy(reinterpret_cast<void*>(camera.owner+0x1E0),camera.smoothing.data(),8);
                    std::memcpy(reinterpret_cast<void*>(camera.owner+0x25C),camera.smoothing.data()+8,72);
                    std::memcpy(reinterpret_cast<void*>(camera.owner+0x1E8),camera.targets.data(),116);
                    std::memcpy(reinterpret_cast<void*>(camera.owner+0x2A4),camera.targets.data()+116,12);
                    auto failed=std::make_unique<sc6::research::NativeAdjacentRestore>();auto prior_loads=loads;
                    require(!failed->run(*source,base,argument,trajectory_save,boundary_load,check));
                    require(failed->fault && !failed->verified && failed->probe->restore_invoked && loads==prior_loads+1);
                    require(!failed->run(*source,base,argument,trajectory_save,boundary_load,check) && loads==prior_loads+1);
                    require(source->data->saved.reads==0);
                }
                boundary_failure=0;
                *reinterpret_cast<unsigned*>(auxiliary)=0;
                loads=0;
                require(source->data->rings[0].restore(base,*scratch));
                *reinterpret_cast<unsigned*>(base+P::key_rvas[0])=original_outer;
                std::memset(reinterpret_cast<void*>(base+0x600000),0,0x2B0);
                *reinterpret_cast<uintptr_t*>(base+0x600000)=base+0x3E87EB0;
                *reinterpret_cast<float*>(base+0x600000+0x1D4)=0.375f;
                auto multi=std::make_unique<sc6::research::NativeTwoFrameReplay>();
                for(unsigned frame=0;frame<3;++frame){
                    if(frame){
                        ++*reinterpret_cast<unsigned*>(base+P::key_rvas[0]);
                        require(scratch->capture(base));
                        for(auto& h:scratch->headers){auto index=(h.index+2u)%3u;
                            *reinterpret_cast<unsigned*>(h.object+0x20)=index;
                            *reinterpret_cast<uintptr_t*>(h.object+0x28)=h.slots[index];
                            *reinterpret_cast<uintptr_t*>(h.object+0x30)=h.current;
                        }
                        ++*reinterpret_cast<unsigned*>(base+0x600000+0x29C);
                        auto& ci=*reinterpret_cast<unsigned*>(base+0x600000+0x2A0);ci=(ci+1)%16;
                    }
                    bool ok=multi->step(base,argument,true,trajectory_save,boundary_load,check);
                    if(!ok)std::cerr<<"multi failed captured="<<multi->captured<<" restored="<<multi->restored<<" hop0="<<multi->hops[0]->fault<<" hop1="<<multi->hops[1]->fault<<" rejection="<<int(multi->hops[1]->rejection)<<" invoked="<<multi->hops[1]->probe->restore_invoked<<" loaded="<<multi->hops[1]->probe->delayed_loaded<<" camera="<<multi->hops[1]->probe->camera_restore_verified<<" completion="<<multi->hops[1]->completion.verified<<" native="<<multi->hops[1]->native_payload_verified<<"\n";
                    require(ok);
                    if(frame<2)require(loads==0 && !multi->next_argument);
                }
                require(multi->prepared && multi->restored==2 && loads==2 && multi->delivered==1 && multi->next_argument);
                std::array<std::byte,40> multi_input{};
                require(P::input_bytes(multi->next_argument,multi_input) && multi_input==multi->frames[0]->data->delayed_inputs);
                // With no intervening update, first replay must reject and latch.
                require(!multi->step(base,argument,true,trajectory_save,boundary_load,check));
                require(multi->fault && multi->compared==0 && !multi->next_argument && loads==2);
                require(!multi->step(base,argument,true,trajectory_save,boundary_load,check) && loads==2);
                loads=0;
                auto prediction=std::make_unique<sc6::research::NativePredictionProbe>();
                for(unsigned frame=0;frame<4;++frame){
                    if(frame){
                        ++*reinterpret_cast<unsigned*>(base+P::key_rvas[0]);
                        require(scratch->capture(base));
                        for(auto& h:scratch->headers){auto index=(h.index+2u)%3u;
                            *reinterpret_cast<unsigned*>(h.object+0x20)=index;
                            *reinterpret_cast<uintptr_t*>(h.object+0x28)=h.slots[index];
                            *reinterpret_cast<uintptr_t*>(h.object+0x30)=h.current;
                        }
                        ++*reinterpret_cast<unsigned*>(base+0x600000+0x29C);
                        auto& ci=*reinterpret_cast<unsigned*>(base+0x600000+0x2A0);ci=(ci+1)%16;
                    }
                    bool ok=prediction->step(base,argument,true,trajectory_save,boundary_load,check);
                    if(!ok)std::cerr<<"prediction failed frame="<<frame<<" captures="<<prediction->captured<<" loads="<<prediction->loads<<"\n";
                    require(ok);
                    if(frame==0)require(loads==0 && !prediction->next_argument);
                    if(frame==1 || frame==2){std::array<std::byte,40> input{};
                        require(P::input_bytes(prediction->next_argument,input));
                        require(input==(frame==1?prediction->predicted:prediction->actual));
                    }
                }
                require(prediction->done && prediction->corrected && prediction->dirty_detected && !prediction->state_changed);
                require(prediction->loads==2 && loads==2 && prediction->deliveries==2 && !prediction->next_argument);
                require(!prediction->history->correction());
                require(prediction->step(base,argument,true,trajectory_save,boundary_load,check) && loads==2 && !prediction->next_argument);
                sc6::research::NativePredictionProbe::Input invalid{},output{};
                unsigned nan=0x7FC00000;std::memcpy(invalid.data()+24,&nan,4);
                require(!sc6::research::NativePredictionProbe::make_prediction(invalid,output));
                invalid={};float quarter=0.25f;std::memcpy(invalid.data()+24,&quarter,4);
                require(sc6::research::NativePredictionProbe::make_prediction(invalid,output) && output!=invalid);
                require(!std::memcmp(invalid.data(),output.data(),24) && !std::memcmp(invalid.data()+28,output.data()+28,12));
                sc6::research::NativePredictionProbe::Input neutral{},direction{};
                require(sc6::research::NativePredictionProbe::make_prediction(neutral,direction,true));
                for(unsigned off=0;off<40;off+=4){unsigned bits{};std::memcpy(&bits,direction.data()+off,4);
                    require(bits==((off==0 || off==4 || off==16)?0x400u:0u));}
                for(unsigned off=0;off<24;++off){neutral[off]=std::byte{1};
                    require(!sc6::research::NativePredictionProbe::make_prediction(neutral,direction,true));neutral[off]=std::byte{};}
                unsigned bad_float=0x7F800000u;std::memcpy(neutral.data()+28,&bad_float,4);
                require(!sc6::research::NativePredictionProbe::make_prediction(neutral,direction,true));
                { // Explicit movement opt-in must not weaken the default mode or neutral-input guards.
                    using B=sc6::research::NativeBoundarySnapshot;
                    auto& a=*prediction->frames[0];auto& b=*prediction->frames[1];
                    b.mode=B::Mode::direction_observation;
                    for(auto& r:b.data->cameras[0].records)if(r.smoothing_valid){float factor=0.1f;
                        std::memcpy(r.targets.data()+104,&factor,4);std::memcpy(r.targets.data()+108,&factor,4);}
                    auto q=std::make_unique<P>();B::Rejection why{};
                    require(!a.prepare_adjacent(b,*q,&why) && why==B::Rejection::mode);
                    auto ok_direction=a.prepare_adjacent(b,*q,&why,true); if(!ok_direction)std::cerr<<"direction reason="<<int(why)<<"\n"; require(ok_direction && q->direction_restore_requested && !q->combat_replay_requested);
                    auto bad=std::make_unique<P>();
                    a.data->delayed_inputs[0]=b.data->delayed_inputs[0]=std::byte{1};
                    require(!a.prepare_adjacent(b,*bad,&why,true) && why==B::Rejection::inputs && !bad->restore_invoked);
                    a.data->delayed_inputs[0]=b.data->delayed_inputs[0]=std::byte{};
                    b.data->blends[0].bytes[0][60]^=std::byte{1};
                    require(!a.prepare_adjacent(b,*bad,&why,true) && why==B::Rejection::combat && !bad->restore_invoked);
                    require(bad->blend_pair_evidence.valid && bad->blend_pair_evidence.saved[60]==a.data->blends[0].bytes[0][60] && bad->blend_pair_evidence.next[60]==b.data->blends[0].bytes[0][60]);
                    require(bad->blend_pair_diagnostic.reason==3 && bad->blend_pair_diagnostic.side==0 && bad->blend_pair_diagnostic.offset==60 && bad->blend_pair_diagnostic.changed==1);
                    b.data->blends[0].bytes[0][60]^=std::byte{1};
                    b.data->blends[0].valid=false;
                    require(!a.prepare_adjacent(b,*bad,&why,true) && why==B::Rejection::combat && bad->blend_pair_diagnostic.reason==1 && !bad->restore_invoked);
                    require(!bad->blend_pair_evidence.valid);
                    b.data->blends[0].valid=true;
                    const auto blend_owner=b.data->blends[0].owners[1];
                    b.data->blends[0].owners[1]=blend_owner+8;
                    require(!a.prepare_adjacent(b,*bad,&why,true) && why==B::Rejection::combat && bad->blend_pair_diagnostic.reason==2 && bad->blend_pair_diagnostic.side==1 && bad->blend_pair_diagnostic.next_owner==blend_owner+8 && !bad->restore_invoked);
                    b.data->blends[0].owners[1]=blend_owner;
                    b.mode=B::Mode::idle;
                    require(!a.prepare_adjacent(b,*bad,&why,true) && why==B::Rejection::mode);
                    a.mode=b.mode=B::Mode::direction_observation;
                    for(auto& r:a.data->cameras[0].records)if(r.smoothing_valid){float factor=0.1f;std::memcpy(r.targets.data()+104,&factor,4);std::memcpy(r.targets.data()+108,&factor,4);}
                    for(auto& r:b.data->cameras[0].records)if(r.smoothing_valid){float factor=0.2f;std::memcpy(r.targets.data()+104,&factor,4);std::memcpy(r.targets.data()+108,&factor,4);}
                    auto consecutive=std::make_unique<P>();
                    require(!a.prepare_adjacent(b,*consecutive,&why) && why==B::Rejection::mode);
                    require(a.prepare_adjacent(b,*consecutive,&why,true) && consecutive->direction_restore_requested && !consecutive->combat_replay_requested);
                    b.direction_bits=0x800;
                    auto mixed_direction=std::make_unique<P>();
                    require(!a.prepare_adjacent(b,*mixed_direction,&why,true) && why==B::Rejection::mode);
                    a.direction_bits=0x800;
                    require(a.prepare_adjacent(b,*mixed_direction,&why,true) && mixed_direction->direction_bits==0x800);
                    a.direction_bits=b.direction_bits=0x400;
                    a.mode=b.mode=B::Mode::idle;

                }
                using MovementBoundary=sc6::research::NativeBoundarySnapshot;
                *reinterpret_cast<std::uint16_t*>(base+0x4717840)=0x400;
                require(MovementBoundary::direction_state(base) && !P::idle(base));
                for(unsigned off:{0x4717840u,0x47AEC30u,0x475A04Au,0x47F143Au,0x48462F4u,0x48462D0u}){
                    auto* byte=reinterpret_cast<unsigned char*>(base+off);*byte^=1;
                    require(!MovementBoundary::direction_state(base));*byte^=1;
                }
                { // Two-frame observation must never restore the divergent state.
                    using N=sc6::research::NativePredictionProbe;
                    auto two=std::make_unique<N>(true,false,true);
                    two->captured=3;two->history=std::make_unique<N::History>(N::Input{});
                    two->actual[24]=std::byte{1};N::Input used{};
                    require(two->history->advance(used) && two->history->advance(used));
                    auto before_loads=loads;
                    require(two->step(base,argument,true,trajectory_save,boundary_load,check));
                    require(two->captured==4 && two->done && two->dirty_detected && !two->corrected && !two->next_argument && loads==before_loads);
                    auto before_saves=saves;
                    require(two->step(base,argument,true,trajectory_save,boundary_load,check) && saves==before_saves && loads==before_loads);
                    auto invalid_mode=std::make_unique<N>(true,true,true);require(invalid_mode->fault);
                    auto camera_gate=std::make_unique<N>(true,false,true);
                    camera_gate->waiting_camera=base+0x610000;
                    std::memset(reinterpret_cast<void*>(camera_gate->waiting_camera),0,0x2B0);
                    *reinterpret_cast<float*>(camera_gate->waiting_camera+0x250)=0.1f;
                    require(camera_gate->wait_camera() && camera_gate->camera_stable==0);
                    *reinterpret_cast<float*>(camera_gate->waiting_camera+0x250)=0;
                    for(unsigned i=0;i<10;++i)require(camera_gate->wait_camera());
                    require(camera_gate->camera_stable==10);
                    *reinterpret_cast<float*>(camera_gate->waiting_camera+0x230)=1;
                    require(camera_gate->wait_camera() && camera_gate->camera_stable==1);
                    camera_gate->camera_wait_samples=600;require(!camera_gate->wait_camera());

                }
                {
                    sc6::research::CameraSnapshot first_camera,second_camera;
                    first_camera.valid=true;first_camera.count=1;first_camera.records[0].owner=base+0x610000;
                    first_camera.records[0].smoothing_valid=true;second_camera=first_camera;
                    require(sc6::research::NativeTwoMovement::clean_camera_pair(first_camera,second_camera));
                    second_camera.records[0].targets[104]=std::byte{1};
                    require(!sc6::research::NativeTwoMovement::clean_camera_pair(first_camera,second_camera));
                    second_camera=first_camera;second_camera.records[0].targets[72]=std::byte{1};
                    require(!sc6::research::NativeTwoMovement::clean_camera_pair(first_camera,second_camera));
                    second_camera=first_camera;second_camera.records[0].owner+=8;
                    require(!sc6::research::NativeTwoMovement::clean_camera_pair(first_camera,second_camera));
                    auto blend_a=std::make_unique<P>(),blend_b=std::make_unique<P>();
                    blend_a->blends[0].valid=blend_b->blends[0].valid=true;
                    require(sc6::research::NativeTwoMovement::clean_blend_pair(*blend_a,*blend_b));
                    blend_b->blends[0].bytes[0][1372]=std::byte{1};
                    require(sc6::research::NativeTwoMovement::clean_blend_pair(*blend_a,*blend_b));
                    blend_b->blends[0].bytes[0][0xC8]=std::byte{1};
                    require(!sc6::research::NativeTwoMovement::clean_blend_pair(*blend_a,*blend_b));
                    blend_b->blends[0].bytes[0][0xC8]=std::byte{};blend_b->blends[0].owners[1]=1;
                    require(!sc6::research::NativeTwoMovement::clean_blend_pair(*blend_a,*blend_b));
                    auto three_movement=std::make_unique<sc6::research::NativeTwoMovement>(3);
                    require(!three_movement->fault && three_movement->window==3);
                    require(three_movement->movement_source(0)==0 && three_movement->movement_source(1)==4 && three_movement->movement_source(2)==5);
                    for(unsigned i=0;i<7;++i)require(three_movement->frames[i] && three_movement->frames[i]->mode==(i<4?MovementBoundary::Mode::idle:MovementBoundary::Mode::direction_observation));
                    for(unsigned i=0;i<6;++i)require(three_movement->restores[i]!=nullptr);
                    auto too_long=std::make_unique<sc6::research::NativeTwoMovement>(4);require(too_long->fault);
                    sc6::research::NativePredictionProbe::Input neutral_input{},wrong_input{};wrong_input[0]=std::byte{1};
                    sc6::research::NativePredictionProbe::History three_history(wrong_input);
                    for(unsigned i=0;i<3;++i){sc6::research::NativePredictionProbe::Input used{};require(three_history.advance(used) && used==wrong_input);}
                    for(unsigned i=0;i<3;++i)require(three_history.receive(i,neutral_input)==sc6::research::NativePredictionProbe::History::Arrival::accepted);
                    require(three_history.correction()==std::optional<std::size_t>{0});
                    require(three_history.input_for(2)==neutral_input && !three_history.finish_correction(1));
                    require(three_history.finish_correction(0) && !three_history.correction());
                    auto guarded=std::make_unique<sc6::research::NativeTwoMovement>();
                    auto old_loads=loads,old_saves=saves;
                    require(!guarded->step(base,argument,false,trajectory_save,boundary_load,check));
                    require(guarded->fault && !guarded->next_argument && loads==old_loads && saves==old_saves);
                    require(!guarded->step(base,argument,true,trajectory_save,boundary_load,check) && loads==old_loads);
                }
                {
                    sc6::research::NativePredictionProbe::Input zero{},left{};
                    require(sc6::research::NativePredictionProbe::make_prediction(zero,left,true,0x800));
                    for(unsigned off=0;off<40;off+=4){unsigned word{};std::memcpy(&word,left.data()+off,4);
                        require(word==((off==0 || off==4 || off==16)?0x800u:0u));}
                    require(!sc6::research::NativePredictionProbe::make_prediction(zero,left,true,0xC00));
                    *reinterpret_cast<std::uint16_t*>(base+0x4717840)=0x800;
                    require(MovementBoundary::direction_state(base,0x800) && P::direction_state(base,0x800));
                    require(!MovementBoundary::direction_state(base) && !P::direction_state(base));
                    *reinterpret_cast<std::uint16_t*>(base+0x4717840)=0x400;
                    auto left_controller=std::make_unique<sc6::research::NativeTwoMovement>(3,0x800);
                    for(unsigned i=0;i<7;++i)require(left_controller->frames[i]->direction_bits==0x800);
                    auto bad_direction=std::make_unique<sc6::research::NativeTwoMovement>(3,0xC00);require(bad_direction->fault);
                }
                {
                    std::array<std::byte,1372> source_blend{},attack_blend{};
                    unsigned idle_index=12,attack_index=14;
                    std::memcpy(source_blend.data()+156,&idle_index,4);std::memcpy(attack_blend.data()+156,&attack_index,4);
                    require(P::a_blend_pair(source_blend.data(),attack_blend.data(),0));
                    require(!P::a_blend_pair(source_blend.data(),attack_blend.data(),1));
                    attack_blend[160]=std::byte{1};require(!P::a_blend_pair(source_blend.data(),attack_blend.data(),0));
                    attack_blend[160]=std::byte{};attack_index=15;std::memcpy(attack_blend.data()+156,&attack_index,4);
                    require(!P::a_blend_pair(source_blend.data(),attack_blend.data(),0));
                    sc6::research::NativePredictionProbe::Input zero{},attack{};
                    require(sc6::research::NativePredictionProbe::make_a_prediction(zero,attack));
                    for(unsigned off=0;off<40;off+=4){unsigned word{};std::memcpy(&word,attack.data()+off,4);
                        require(word==((off==0 || off==4 || off==16)?1u:0u));}
                    zero[8]=std::byte{1};require(!sc6::research::NativePredictionProbe::make_a_prediction(zero,attack));
                    *reinterpret_cast<std::uint16_t*>(base+0x4717840)=1;
                    *reinterpret_cast<std::uint16_t*>(base+0x475A04A)=264;
                    require(MovementBoundary::a_state(base));
                    auto observation=std::make_unique<MovementBoundary>(MovementBoundary::Mode::a_observation);
                    auto previous_loads=loads;
                    require(observation->capture(base,argument,trajectory_save,check) && loads==previous_loads);
                    auto blocked=std::make_unique<P>();MovementBoundary::Rejection reason_a{};
                    require(!observation->prepare_adjacent(*observation,*blocked,&reason_a,true) && reason_a==MovementBoundary::Rejection::mode);
                    *reinterpret_cast<std::uint16_t*>(base+0x475A04A)=265;require(!MovementBoundary::a_state(base));
                    *reinterpret_cast<std::uint16_t*>(base+0x475A04A)=65535;
                    *reinterpret_cast<std::uint16_t*>(base+0x4717840)=0x400;
                }
                auto movement=std::make_unique<MovementBoundary>(MovementBoundary::Mode::direction_observation);
                auto prior_loads=loads,prior_saves=saves;
                require(movement->capture(base,argument,trajectory_save,check));
                require(movement->valid && saves==prior_saves+1 && loads==prior_loads);
                auto forbidden=std::make_unique<P>();MovementBoundary::Rejection reason{};
                require(!movement->prepare_adjacent(*movement,*forbidden,&reason));
                require(reason==MovementBoundary::Rejection::mode && !forbidden->restore_invoked);
                require(!movement->capture(base,argument,trajectory_save,check) && saves==prior_saves+1);
                *reinterpret_cast<std::uint16_t*>(base+0x4717840)=0;
                loads=0;
            }
            auto bounded=std::make_unique<sc6::research::IdleTrajectoryAudit>(false,5);require(bounded->fault);
            auto reject_long=std::make_unique<sc6::research::TwoFrameIdleReplay>();require(!reject_long->prepare(*audit));
            audit->charge_mode=true;
            auto no_charge_replay=std::make_unique<sc6::research::TwoFrameIdleReplay>(true,3);
            require(!no_charge_replay->prepare(*audit) && !no_charge_replay->restoration_started);
            audit->charge_mode=false;
            using Three=sc6::research::TwoFrameIdleReplay;
            audit->throw_mode=true;audit->recorded_inputs=true;
            for(unsigned i=0;i<3;++i){audit->combat_checks[i]=std::make_unique<P>();
                audit->combat_checks[i]->combat_preflight_attempted=true;
                audit->combat_checks[i]->combat_preflight_pass_mask=255;audit->root_wind_ready[i]=true;}
            auto three=std::make_unique<Three>(true,3);require(three->prepare(*audit));
            require(three->hops[2]->before_keys[0]==102 && three->hops[2]->after_keys[0]==103);
            require(three->hops[2]->combat_later_window && three->hops[2]->recorded_input_replay);
            audit->combat_checks[2]->combat_preflight_pass_mask=251;
            auto reject_third=std::make_unique<Three>(true,3);require(!reject_third->prepare(*audit));
            audit->combat_checks[2]->combat_preflight_pass_mask=255;
            audit->root_wind_ready[2]=false;
            auto reject_wind=std::make_unique<Three>(true,3);require(!reject_wind->prepare(*audit));
            { // Explicit one-update charge wiring; rejects observation-only exceptions.
                audit->throw_mode=false;audit->charge_mode=true;audit->count=2;audit->recorded_inputs=false;
                audit->charge_camera_ready[0]=true;
                auto charge=std::make_unique<Three>(false,1,true);
                require(charge->prepare(*audit) && charge->hops[0]->charge_requested && !charge->hops[0]->throw_requested);
                require(!charge->recorded_inputs && !charge->hops[1] && !charge->restoration_started);
                audit->frames[1]->inputs[16]^=std::byte{1};
                auto reject_release=std::make_unique<Three>(false,1,true);
                require(!reject_release->prepare(*audit) && !reject_release->restoration_started);
                audit->frames[1]->inputs[16]^=std::byte{1};
                audit->charge_camera_ready[0]=false;
                auto reject_camera=std::make_unique<Three>(false,1,true);require(!reject_camera->prepare(*audit));
                audit->charge_camera_ready[0]=true;
                audit->visuals->blends[1].bytes[0][140+0x3C]^=std::byte{1};
                auto reject_blend=std::make_unique<Three>(false,1,true);require(!reject_blend->prepare(*audit));
                audit->visuals->blends[1].bytes[0][140+0x3C]^=std::byte{1};
                audit->count=3;audit->charge_camera_ready[1]=true;
                auto two_charge=std::make_unique<Three>(false,2,true);
                require(two_charge->prepare(*audit) && two_charge->hops[1]->charge_requested && !two_charge->hops[2]);
                audit->frames[2]->inputs[16]^=std::byte{1};
                auto reject_second_input=std::make_unique<Three>(false,2,true);
                require(!reject_second_input->prepare(*audit) && !reject_second_input->restoration_started);
                audit->frames[2]->inputs[16]^=std::byte{1};
                { // Only explicit recorded mode admits the captured 807 -> 0 flag release.
                    using I=sc6::research::RecordedInput;
                    unsigned bits=0x807;
                    for(unsigned i=0;i<3;++i){std::memcpy(audit->frames[i]->inputs.data(),&bits,4);
                        if(i<2)std::memcpy(audit->frames[i]->inputs.data()+16,&bits,4);}
                    auto a=audit->frames[1]->inputs,b=audit->frames[2]->inputs;
                    require(I::charge_compatible(a,b) && !I::charge_compatible(b,a) && !I::compatible(a,b));
                    for(unsigned off=0;off<24;++off){auto bad=b;bad[off]^=std::byte{1};require(!I::charge_compatible(a,bad));}
                    auto strict=std::make_unique<Three>(false,2,true);require(!strict->prepare(*audit));
                    audit->recorded_inputs=true;audit->combat_checks[1]->combat_preflight_pass_mask=253;
                    auto recorded=std::make_unique<Three>(false,2,true);
                    require(recorded->prepare(*audit) && recorded->recorded_inputs && recorded->hops[1]->recorded_input_replay);
                    I packet;std::array<std::byte,40> copied{};
                    require(P::input_bytes(packet.assign(a),copied) && copied==a);
                    require(P::input_bytes(packet.assign(b),copied) && copied==b);
                    unsigned seven=7;
                    for(unsigned i=0;i<2;++i)std::memcpy(audit->frames[i]->inputs.data()+16,&seven,4);
                    std::memcpy(audit->frames[2]->inputs.data(),&seven,4);
                    a=audit->frames[1]->inputs;b=audit->frames[2]->inputs;
                    require(I::charge_compatible(a,b) && !I::charge_compatible(b,a) && !I::compatible(a,b));
                    for(unsigned off=0;off<24;++off){auto bad=b;bad[off]^=std::byte{1};require(!I::charge_compatible(a,bad));}
                    require(audit->inputs_supported(*audit->frames[2]));
                    auto final_release=std::make_unique<Three>(false,2,true);
                    require(final_release->prepare(*audit) && final_release->recorded_inputs);
                    audit->recorded_inputs=false;
                    auto strict_release=std::make_unique<Three>(false,2,true);require(!strict_release->prepare(*audit));
                    audit->recorded_inputs=true;
                    for(unsigned i=0;i<3;++i)audit->frames[i]->inputs={};
                    audit->recorded_inputs=false;audit->combat_checks[1]->combat_preflight_pass_mask=255;
                }
                audit->charge_camera_ready[1]=false;
                auto reject_second_camera=std::make_unique<Three>(false,2,true);require(!reject_second_camera->prepare(*audit));
                audit->count=4;audit->recorded_inputs=true;
                audit->charge_camera_ready[1]=audit->charge_camera_ready[2]=true;audit->root_wind_ready[2]=true;
                auto three_charge=std::make_unique<Three>(false,3,true);
                require(three_charge->prepare(*audit) && three_charge->hops[2]->charge_requested && three_charge->recorded_inputs);
                { // Full captured release chain, strict adjacent edges and baseline reachability.
                    using I=sc6::research::RecordedInput;
                    unsigned players[]={0x807,0x807,7,0},aux[]={0x807,7,0,0};
                    for(unsigned i=0;i<4;++i){auto& f=*audit->frames[i];
                        std::memcpy(f.inputs.data(),&players[i],4);std::memcpy(f.inputs.data()+16,&aux[i],4);
                        f.input_valid=true;f.inputs_equal=false;
                        require(audit->inputs_supported(f));
                        require(I::charge_reachable(audit->frames[0]->inputs,f.inputs));
                        if(i){auto a=audit->frames[i-1]->inputs,b=f.inputs;
                            require(I::charge_compatible(a,b) && !I::charge_compatible(b,a));
                            require(!I::charge_reachable(b,a));
                            for(unsigned off=0;off<24;++off){auto bad=b;bad[off]^=std::byte{1};require(!I::charge_compatible(a,bad));}}
                    }
                    require(!I::charge_compatible(audit->frames[0]->inputs,audit->frames[3]->inputs));
                    auto chain=std::make_unique<Three>(false,3,true);require(chain->prepare(*audit));
                    // Reproduce 65804: same flags with changing float, asynchronous
                    // auxiliary release, then player807->0 with auxiliary already0.
                    unsigned ps[]={0x807,0x807,0x807,0},fs[]={0x807,0x807,0,0};
                    float values[]={-0.01177978515625f,-0.019622802734375f,-0.019622802734375f,-0.01177978515625f};
                    for(unsigned i=0;i<4;++i){auto& input=audit->frames[i]->inputs;
                        std::memcpy(input.data(),&ps[i],4);std::memcpy(input.data()+16,&fs[i],4);
                        std::memcpy(input.data()+32,&values[i],4);
                        require(I::charge_reachable(audit->frames[0]->inputs,input));
                        if(i)require(I::charge_compatible(audit->frames[i-1]->inputs,input));}
                    auto variable=std::make_unique<Three>(false,3,true);require(variable->prepare(*audit));
                    auto a=audit->frames[2]->inputs,b=audit->frames[3]->inputs;
                    require(!I::charge_compatible(b,a));
                    for(unsigned off=24;off<40;off+=4){
                        for(unsigned bits:{0x7FC00000u,0x7F800000u,0xFF800000u,0x3F800001u,0xBF800001u}){
                            auto bad=b;std::memcpy(bad.data()+off,&bits,4);
                            require(!I::charge_compatible(a,bad) && !I::charge_reachable(a,bad) && !I::charge_compatible(bad,bad));
                            bad=a;std::memcpy(bad.data()+off,&bits,4);require(!I::charge_compatible(bad,b));}
                        for(float value:{-1.0f,0.0f,1.0f}){auto good=b;std::memcpy(good.data()+off,&value,4);require(I::charge_compatible(a,good));}}
                    I packet;std::array<std::byte,40> copied{};
                    for(unsigned i=0;i<3;++i){require(P::input_bytes(packet.assign(variable->hops[i]->delayed_inputs),copied));
                        require(copied==audit->frames[i]->inputs);}

                    for(unsigned i=0;i<4;++i)audit->frames[i]->inputs={};
                }
                audit->frames[3]->inputs[4]^=std::byte{1};
                auto reject_last_input=std::make_unique<Three>(false,3,true);require(!reject_last_input->prepare(*audit) && !reject_last_input->restoration_started);
                audit->frames[3]->inputs[4]^=std::byte{1};
                audit->combat_checks[2]->combat_preflight_pass_mask=251;
                auto reject_last_blend=std::make_unique<Three>(false,3,true);require(!reject_last_blend->prepare(*audit));
                audit->combat_checks[2]->combat_preflight_pass_mask=255;
                audit->recorded_inputs=false;
                auto reject_unrecorded=std::make_unique<Three>(false,3,true);require(!reject_unrecorded->prepare(*audit));
                { // Attack mode is explicit and cannot inherit charge release rules.
                    audit->charge_mode=false;audit->attack_mode=true;audit->recorded_inputs=true;
                    auto attack=std::make_unique<Three>(false,3,false,true);
                    require(attack->prepare(*audit) && attack->recorded_inputs);
                    for(unsigned i=0;i<3;++i)require(attack->hops[i]->attack_requested && !attack->hops[i]->charge_requested && !attack->hops[i]->throw_requested);
                    audit->structure_only=true;
                    auto forbidden=std::make_unique<Three>(false,3,false,true);
                    require(!forbidden->prepare(*audit) && !forbidden->restoration_started);
                    audit->structure_only=false;
                    auto observation=std::make_unique<sc6::research::IdleTrajectoryAudit>(false,4,false,true,true);
                    require(!observation->fault && observation->structure_only);
                    uintptr_t players[]={0x100000,0x200000};
                    auto window=std::make_unique<sc6::research::IdleTrajectoryAudit>(false,4,false,true);
                    require(!window->attack_window_ready(100,players) && !window->fault && window->count==0);
                    require(window->attack_window_ready(101,players) && !window->fault);
                    window->count=1;require(window->attack_window_ready(102,players));
                    auto skipped=std::make_unique<sc6::research::IdleTrajectoryAudit>(false,4,false,true);
                    require(!skipped->attack_window_ready(100,players));require(!skipped->attack_window_ready(102,players) && skipped->fault);
                    auto replaced=std::make_unique<sc6::research::IdleTrajectoryAudit>(false,4,false,true);
                    require(!replaced->attack_window_ready(100,players));players[1]+=8;
                    require(!replaced->attack_window_ready(101,players) && replaced->fault);
                    require(observation->attack_window_ready(100,players) && !observation->attack_start_seen);
                    auto wrong_observation=std::make_unique<sc6::research::IdleTrajectoryAudit>(false,4,true,false,true);
                    require(wrong_observation->fault);
                    auto wrong_mode=std::make_unique<Three>(false,3,true);require(!wrong_mode->prepare(*audit));
                    audit->frames[3]->inputs[16]=std::byte{7};
                    auto changed_buttons=std::make_unique<Three>(false,3,false,true);require(!changed_buttons->prepare(*audit) && !changed_buttons->restoration_started);
                    audit->frames[3]->inputs[16]=std::byte{0};
                    float axis=0.25f;std::memcpy(audit->frames[3]->inputs.data()+24,&axis,4);
                    audit->combat_checks[2]->combat_preflight_pass_mask=253;
                    auto axis_change=std::make_unique<Three>(false,3,false,true);require(axis_change->prepare(*audit));
                    audit->combat_checks[2]->combat_preflight_pass_mask=125;
                    auto camera_failure=std::make_unique<Three>(false,3,false,true);require(!camera_failure->prepare(*audit));
                    audit->combat_checks[2]->combat_preflight_pass_mask=255;audit->frames[3]->inputs={};
                    auto invalid=std::make_unique<Three>(false,2,false,true);require(invalid->fault);
                    auto conflict=std::make_unique<Three>(false,3,true,true);require(conflict->fault);
                    auto refs=std::make_unique<sc6::research::IdleTrajectoryAudit>(false,4,false,true);
                    require(!refs->fault && refs->attack_mode && refs->combat_checks[2]);
                    auto bad_refs=std::make_unique<sc6::research::IdleTrajectoryAudit>(false,3,false,true);require(bad_refs->fault);
                    audit->attack_mode=false;audit->edge_mode=true;
                    auto edge=std::make_unique<Three>(false,3,false,false,true);require(edge->prepare(*audit));
                    for(unsigned i=0;i<3;++i)require(edge->hops[i]->edge_requested && !edge->hops[i]->attack_requested && !edge->hops[i]->charge_requested);
                    auto wrong_edge=std::make_unique<Three>(false,3,false,true);require(!wrong_edge->prepare(*audit));
                    audit->frames[3]->inputs[0]=std::byte{1};
                    auto edge_input=std::make_unique<Three>(false,3,false,false,true);require(!edge_input->prepare(*audit) && !edge_input->restoration_started);
                    audit->frames[3]->inputs={};audit->combat_checks[2]->combat_preflight_pass_mask=127;
                    auto edge_camera=std::make_unique<Three>(false,3,false,false,true);require(!edge_camera->prepare(*audit));
                    audit->combat_checks[2]->combat_preflight_pass_mask=255;
                    auto edge_refs=std::make_unique<sc6::research::IdleTrajectoryAudit>(false,4,false,false,false,true);
                    require(!edge_refs->fault && edge_refs->combat_checks[2] && edge_refs->attack_window_ready(10,players));
                    auto edge_conflict=std::make_unique<Three>(false,3,false,true,true);require(edge_conflict->fault);
                    audit->edge_mode=false;audit->reversal_mode=true;
                    auto reversal=std::make_unique<Three>(false,3,false,false,false,true);require(reversal->prepare(*audit));
                    for(unsigned i=0;i<3;++i)require(reversal->hops[i]->reversal_requested && !reversal->hops[i]->edge_requested && !reversal->hops[i]->attack_requested);
                    audit->structure_only=true;
                    auto no_diagnostic_replay=std::make_unique<Three>(false,3,false,false,false,true);
                    require(!no_diagnostic_replay->prepare(*audit) && !no_diagnostic_replay->restoration_started);
                    audit->structure_only=false;
                    auto reversal_observation=std::make_unique<sc6::research::IdleTrajectoryAudit>(false,4,false,false,true,false,true);
                    require(!reversal_observation->fault && reversal_observation->structure_only && reversal_observation->reversal_mode);
                    auto wrong_reversal=std::make_unique<Three>(false,3,false,false,true);require(!wrong_reversal->prepare(*audit));
                    audit->frames[3]->inputs[16]=std::byte{1};
                    auto reversal_input=std::make_unique<Three>(false,3,false,false,false,true);require(!reversal_input->prepare(*audit) && !reversal_input->restoration_started);
                    audit->frames[3]->inputs={};audit->combat_checks[2]->combat_preflight_pass_mask=127;
                    auto reversal_camera=std::make_unique<Three>(false,3,false,false,false,true);require(!reversal_camera->prepare(*audit));
                    audit->combat_checks[2]->combat_preflight_pass_mask=255;
                    auto reversal_refs=std::make_unique<sc6::research::IdleTrajectoryAudit>(false,4,false,false,false,false,true);
                    require(!reversal_refs->fault && reversal_refs->combat_checks[2] && !reversal_refs->attack_window_ready(10,players));
                    require(reversal_refs->attack_start_seen && reversal_refs->reference_start_delay()==4);
                    for(unsigned outer=11;outer<14;++outer)require(!reversal_refs->attack_window_ready(outer,players) && !reversal_refs->fault && reversal_refs->count==0);
                    require(reversal_refs->attack_window_ready(14,players) && !reversal_refs->fault);
                    require(!reversal_refs->attack_window_ready(15,players) && reversal_refs->fault);
                    auto reversal_skip=std::make_unique<sc6::research::IdleTrajectoryAudit>(false,4,false,false,false,false,true);
                    require(!reversal_skip->attack_window_ready(10,players));
                    require(!reversal_skip->attack_window_ready(11,players));
                    require(!reversal_skip->attack_window_ready(13,players) && reversal_skip->fault);
                    auto reversal_identity=std::make_unique<sc6::research::IdleTrajectoryAudit>(false,4,false,false,false,false,true);
                    require(!reversal_identity->attack_window_ready(10,players));
                    uintptr_t changed_players[]={players[0]+8,players[1]};
                    require(!reversal_identity->attack_window_ready(11,changed_players) && reversal_identity->fault);
                    require(reversal_observation->attack_window_ready(10,players) && !reversal_observation->attack_start_seen);
                    auto rng_before=audit->frames[1]->before.rng;
                    audit->frames[1]->before.rng^=1u;
                    auto reversal_rng=std::make_unique<Three>(false,3,false,false,false,true);
                    require(!reversal_rng->prepare(*audit) && !reversal_rng->restoration_started);
                    audit->frames[1]->before.rng=rng_before;
                    auto reversal_conflict=std::make_unique<Three>(false,3,false,false,true,true);require(reversal_conflict->fault);
                    audit->reversal_mode=false;
                    audit->edge_mode=false;
                    audit->attack_mode=false;audit->charge_mode=true;
                }
                auto invalid_charge=std::make_unique<Three>(false,4,true);require(invalid_charge->fault);
                audit->throw_mode=true;audit->charge_mode=false;audit->count=4;audit->recorded_inputs=true;
            }
            audit->root_wind_ready[2]=true;audit->recorded_inputs=false;
            auto reject_inputs=std::make_unique<Three>(true,3);require(!reject_inputs->prepare(*audit));
            auto bad_count=std::make_unique<Three>(true,4);require(bad_count->fault);
            auto bad_mode=std::make_unique<Three>(false,3);require(bad_mode->fault);
            *reinterpret_cast<unsigned*>(base+0x470D0C4)=104;
            three->step(base,argument,trajectory_save,load,check);
            require(three->fault && !three->completed_restores && !three->next_argument && loads==0);
            three->step(base,argument,trajectory_save,load,check);require(loads==0);
        }
        else if(test==6){require(audit->fault && !audit->done && audit->count==2 && saves==2);
            require(audit->combat_checks[0]->combat_preflight_attempted && !audit->combat_checks[0]->restore_invoked);
            require(!(audit->combat_checks[0]->combat_preflight_pass_mask&128u));
            require(!audit->combat_checks[1]->combat_preflight_attempted);
            auto idle_replay=std::make_unique<sc6::research::TwoFrameIdleReplay>();require(!idle_replay->prepare(*audit));}
        else if(test==0){require(audit->done && !audit->fault && audit->count==3 && saves==3);
            require(audit->visuals->transforms[2].valid && audit->visuals->cameras[2].valid);
            using Two=sc6::research::TwoFrameIdleReplay;
            auto two=std::make_unique<Two>();
            require(two->prepare(*audit) && !two->prepare(*audit));
            { // Synthetic passed preflight records test mode wiring, not native combat restoration.
                audit->throw_mode=true;
                for(unsigned pair=0;pair<2;++pair){audit->combat_checks[pair]=std::make_unique<P>();
                    audit->combat_checks[pair]->combat_preflight_attempted=true;
                    audit->combat_checks[pair]->combat_preflight_pass_mask=255;audit->root_wind_ready[pair]=true;}
                auto combat=std::make_unique<Two>(true);require(combat->prepare(*audit));
                require(combat->hops[0]->combat_replay_requested && combat->hops[1]->combat_replay_requested);
                require(!combat->hops[0]->combat_later_window && combat->hops[1]->combat_later_window);
                require(combat->hops[0]->throw_requested && combat->hops[1]->combat_flag_requested);
                audit->recorded_inputs=true;
                audit->frames[1]->inputs[37]=std::byte{1};
                audit->combat_checks[0]->combat_preflight_pass_mask=253;
                audit->combat_checks[1]->combat_preflight_pass_mask=253;
                auto recorded=std::make_unique<Two>(true);require(recorded->prepare(*audit));
                require(recorded->recorded_inputs && recorded->hops[0]->recorded_input_replay);
                sc6::research::RecordedInput packet;
                std::array<std::byte,40> roundtrip{};
                require(P::input_bytes(packet.assign(audit->frames[1]->inputs),roundtrip));
                require(roundtrip==audit->frames[1]->inputs);
                auto original_byte=*reinterpret_cast<std::byte*>(argument+0x100);
                *reinterpret_cast<std::byte*>(argument+0x100)=std::byte{1};
                auto loads_before=loads;
                recorded->step(base,argument,trajectory_save,load,check);
                require(recorded->fault && !recorded->restoration_started && !recorded->next_argument && loads==loads_before);
                *reinterpret_cast<std::byte*>(argument+0x100)=original_byte;

                audit->frames[1]->inputs[0]=std::byte{1};
                auto reject_buttons=std::make_unique<Two>(true);require(!reject_buttons->prepare(*audit));
                audit->frames[1]->inputs={};audit->recorded_inputs=false;
                audit->combat_checks[0]->combat_preflight_pass_mask=255;
                audit->combat_checks[1]->combat_preflight_pass_mask=127;
                auto blocked=std::make_unique<Two>(true);require(!blocked->prepare(*audit) && !blocked->restoration_started);
                audit->combat_checks[1]->combat_preflight_pass_mask=255;audit->root_wind_ready[0]=false;
                auto blocked_root=std::make_unique<Two>(true);require(!blocked_root->prepare(*audit));
                audit->throw_mode=false;
                auto wrong_mode=std::make_unique<Two>(true);require(!wrong_mode->prepare(*audit));
            }

            require(two->hops[0]->before_keys[0]==100 && two->hops[0]->after_keys[0]==101);
            require(two->hops[1]->before_keys[0]==101 && two->hops[1]->after_keys[0]==102);
            require(two->hops[0]->saved.stream.read_position==0 && two->hops[1]->saved.stream.read_position==0);
            auto& q=*two->hops[0];q.replay=q.after_save;
            require(Two::native_equal(q,base));
            q.replay.stream.payload[0]^=std::byte{1};require(!Two::native_equal(q,base));
            q.replay=q.after_save;
            require(Two::transforms_equal(q.transforms[0],q.transforms[0]));
            q.transforms[4]=q.transforms[0];q.transforms[4].records[0].bytes[16]^=std::byte{1};
            require(!Two::transforms_equal(q.transforms[0],q.transforms[4]));
            q.cameras[3]=q.cameras[0];require(Two::cameras_equal(q.cameras[0],q.cameras[3]));
            q.cameras[3].records[0].bytes[16]^=std::byte{1};require(!Two::cameras_equal(q.cameras[0],q.cameras[3]));
            // A stale live endpoint must stop before the first native load.
            *reinterpret_cast<unsigned*>(base+0x470D0C4)=103;
            two->step(base,argument,trajectory_save,load,check);
            require(two->fault && loads==0 && two->completed_restores==0);
            two->step(base,argument,trajectory_save,load,check);require(loads==0);
            *reinterpret_cast<unsigned*>(base+0x470D0C4)=102;
            auto partial=std::make_unique<Two>();require(partial->prepare(*audit));
            partial->step(base,argument,trajectory_save,load,check);
            require(partial->fault && loads==1 && partial->completed_restores==0 && !partial->hops[0]->restore_invoked);
            partial->step(base,argument,trajectory_save,load,check);require(loads==1);
            loads=0;
            // Signed zero is allowed only in the observed motion W field.
            {
                auto z=std::make_unique<sc6::research::FullStateProbe>();auto& a=z->after_save;auto& b=z->replay;
                a.writes=b.writes=1;a.sealed=b.sealed=true;
                a.stream.write_position=b.stream.write_position=0x370;a.lengths[0]=b.lengths[0]=0x370;
                z->after_players[0]=0x100000;z->general_combat_requested=true;z->restore_completed=true;
                a.traces[0].writer=b.traces[0].writer=base+0x30AA80;
                a.traces[0].source=b.traces[0].source=0x145230;
                std::memset(a.stream.payload.data(),0,0x370);std::memset(b.stream.payload.data(),0,0x370);
                a.stream.payload[0x1CF]=std::byte{0x80};
                require(Two::native_equal(*z,base) && z->motion_zero_evidence[0]==1);
                require(a.stream.payload[0x1CF]==std::byte{0x80} && b.stream.payload[0x1CF]==std::byte{0});
                b.stream.payload[0x1CC]=std::byte{1};require(!Two::native_equal(*z,base));b.stream.payload[0x1CC]=std::byte{};
                b.stream.payload[0x1D0]=std::byte{1};require(!Two::native_equal(*z,base));b.stream.payload[0x1D0]=std::byte{};
                ++b.traces[0].source;require(!Two::native_equal(*z,base));--b.traces[0].source;
                ++a.traces[0].writer;++b.traces[0].writer;require(!Two::native_equal(*z,base));--a.traces[0].writer;--b.traces[0].writer;
                z->general_combat_requested=false;require(!Two::native_equal(*z,base));z->general_combat_requested=true;
                z->restore_completed=false;require(!Two::native_equal(*z,base));z->restore_completed=true;
                a.stream.payload[0x1CF]=std::byte{};b.stream.payload[0x1CF]=std::byte{0x80};require(Two::native_equal(*z,base));
                // Each newly observed W field accepts either zero sign only.
                for(unsigned word:{0x11Cu,0x12Cu,0x1CCu}){
                    std::memset(a.stream.payload.data(),0,0x370);std::memset(b.stream.payload.data(),0,0x370);
                    a.stream.payload[word+3]=std::byte{0x80};
                    require(Two::native_equal(*z,base) && z->motion_zero_evidence[2]==word);
                    b.stream.payload[word]=std::byte{1};require(!Two::native_equal(*z,base));
                    b.stream.payload[word]=std::byte{};
                    a.stream.payload[word+3]=std::byte{};b.stream.payload[word+3]=std::byte{0x80};
                    require(Two::native_equal(*z,base));
                    b.stream.payload[word+3]=std::byte{0x3f};require(!Two::native_equal(*z,base));
                    b.stream.payload[word+3]=std::byte{0x7f};require(!Two::native_equal(*z,base));
                }
                std::memset(a.stream.payload.data(),0,0x370);std::memset(b.stream.payload.data(),0,0x370);
                a.stream.payload[0x11F]=a.stream.payload[0x12F]=a.stream.payload[0x1CF]=std::byte{0x80};
                require(Two::native_equal(*z,base) && z->motion_zero_evidence[0]==3);
                b.stream.payload[0x11B]=std::byte{0x80};require(!Two::native_equal(*z,base));
                b.stream.payload[0x11B]=std::byte{};
                b.stream.payload[0x1CF]=std::byte{0x80};
                // A later nonzero divergence is still rejected after an accepted zero.
                b.stream.payload[0x1CF]=std::byte{0x3f};require(!Two::native_equal(*z,base));
            }
            // The native-byte exception is restricted to a verified rebuilt self pointer.
            auto& native_probe=*two->hops[0];
            auto& original=native_probe.after_save;auto& replay=native_probe.replay;
            original.writes=replay.writes=1;original.sealed=replay.sealed=true;
            original.stream.write_position=replay.stream.write_position=48;
            original.lengths[0]=replay.lengths[0]=48;
            original.traces[0].writer=replay.traces[0].writer=base+0x30AA54;
            auto player=native_probe.after_players[0];
            original.traces[0].source=replay.traces[0].source=player+0x43DF0;
            std::memset(original.stream.payload.data(),0,48);std::memset(replay.stream.payload.data(),0,48);
            std::memcpy(replay.stream.payload.data(),&player,8);
            require(Two::native_equal(native_probe,base));
            replay.stream.payload[8]=std::byte{1};require(!Two::native_equal(native_probe,base));
            replay.stream.payload[8]=std::byte{0};replay.stream.payload[0]^=std::byte{1};
            require(!Two::native_equal(native_probe,base));
            original.stream.write_position=replay.stream.write_position=16;
            original.lengths[0]=replay.lengths[0]=16;
            original.traces[0].writer=replay.traces[0].writer=base+0x32AC76;
            native_probe.restore_completed=true;native_probe.saved.sealed=true;native_probe.saved.writes=1;
            native_probe.saved.lengths[0]=16;native_probe.saved.traces[0].writer=base+0x32AC76;
            native_probe.saved.traces[0].reader=base+0x32AD1B;
            std::memset(original.stream.payload.data(),0,16);std::memset(replay.stream.payload.data(),0,16);
            for(unsigned byte=0;byte<16;++byte){
                replay.stream.payload[byte]=std::byte{1};
                require(Two::native_equal(native_probe,base)==(byte>=4 && byte<8));
                replay.stream.payload[byte]=std::byte{0};
            }
            replay.stream.payload[4]=std::byte{1};
            ++native_probe.saved.traces[0].reader;require(!Two::native_equal(native_probe,base));
            --native_probe.saved.traces[0].reader;
            ++original.traces[0].writer;++replay.traces[0].writer;require(!Two::native_equal(native_probe,base));
            --original.traces[0].writer;--replay.traces[0].writer;
            native_probe.restore_completed=false;require(!Two::native_equal(native_probe,base));
            auto immediate_source=std::make_unique<P>(),immediate_result=std::make_unique<P>();
            immediate_source->saved=original;immediate_result->restored=replay;
            immediate_result->saved=native_probe.saved;immediate_result->restore_completed=true;
            auto& result_bytes=immediate_result->restored.stream.payload;
            std::memset(result_bytes.data(),0,16);
            using Immediate=sc6::research::NativeAdjacentRestore;
            for(unsigned byte=0;byte<16;++byte){result_bytes[byte]=std::byte{1};
                require(Immediate::native_equal(*immediate_source,*immediate_result,base)==(byte>=4 && byte<8));
                result_bytes[byte]=std::byte{};}
            result_bytes[4]=std::byte{1};
            ++immediate_result->saved.traces[0].reader;
            require(!Immediate::native_equal(*immediate_source,*immediate_result,base));
            --immediate_result->saved.traces[0].reader;
            ++immediate_source->saved.traces[0].writer;++immediate_result->restored.traces[0].writer;
            require(!Immediate::native_equal(*immediate_source,*immediate_result,base));
            --immediate_source->saved.traces[0].writer;--immediate_result->restored.traces[0].writer;
            immediate_result->restore_completed=false;
            require(!Immediate::native_equal(*immediate_source,*immediate_result,base));
            immediate_result->restore_completed=true;immediate_result->saved.sealed=false;
            require(!Immediate::native_equal(*immediate_source,*immediate_result,base));

            auto rejected=std::make_unique<Two>();audit->frames[1]->inputs[0]^=std::byte{1};
            require(!rejected->prepare(*audit) && rejected->fault);
            audit->frames[1]->inputs[0]^=std::byte{1};
            auto& ca=audit->visuals->cameras[0];auto& cb=audit->visuals->cameras[2];
            require(sc6::research::IdleTrajectoryAudit::camera_identity(ca,cb));
            ++cb.records[0].owner;require(!sc6::research::IdleTrajectoryAudit::camera_identity(ca,cb));
            --cb.records[0].owner;cb.records[0].smoothing_valid=!cb.records[0].smoothing_valid;
            require(!sc6::research::IdleTrajectoryAudit::camera_identity(ca,cb));
            audit->step(base,argument,trajectory_save,check);require(saves==3);}
        else {require(audit->fault && !audit->done && audit->count<3);
            auto saved_count=saves;audit->step(base,argument,trajectory_save,check);require(saves==saved_count);}
        std::memset(reinterpret_cast<void*>(argument+0x100),0,0x100);
    }
    *reinterpret_cast<std::uint16_t*>(base+0x475A04A)=65535;
    *reinterpret_cast<std::uint16_t*>(base+0x47F143A)=65535;
    *reinterpret_cast<float*>(base+0x48462F4)=1.0f;
    for(unsigned test=0;test<5;++test){
        auto q=std::make_unique<P>();q->delay_audit=true;q->delayed_requested=true;q->ring_capture_enabled=true;q->preserve_requested=true;
        auto scratch=std::make_unique<Ring>();require(ring0->restore(base,*scratch));
        mode=test>=2?int(98+test):0;saves=loads=0;
        *reinterpret_cast<unsigned*>(base+0x485EB28)=123;
        *reinterpret_cast<unsigned*>(base+0x470D0C4)=100;
        *reinterpret_cast<std::uint16_t*>(base+0x4717840)=0;
        auto argument=base+0x400000;
        for(unsigned side=0;side<2;++side){
            auto player=P::ptr(base+0x470DE90+side*8);
            *reinterpret_cast<uintptr_t*>(player+0x971F8)=base+0x500000+side*0x10000;
        }
        for(unsigned j=0;j<3;++j)*reinterpret_cast<uintptr_t*>(argument+j*8)=argument+0x100+j*0x40;
        q->delayed_step(base,argument,delayed_save,delayed_load,check);
        require(!q->fault && q->audit_started && !q->audit_done && loads==0);
        for(auto& h:ring0->headers){
            *reinterpret_cast<unsigned*>(h.object+0x20)=2;
            *reinterpret_cast<uintptr_t*>(h.object+0x28)=h.slots[2];
            *reinterpret_cast<uintptr_t*>(h.object+0x30)=h.current;
        }
        *reinterpret_cast<unsigned*>(base+0x470D0C4)=101;
        if(test==0)*reinterpret_cast<unsigned*>(base+0x485EB28)=456;
        if(test==1)*reinterpret_cast<unsigned*>(argument+0x100)=1;
        q->delayed_step(base,argument,delayed_save,delayed_load,check);
        if(test==1){require(q->fault && loads==0);require(q->input_check_phase==1 && q->input_checkpoints[1].valid && q->input_checkpoints[1].changed_mask==1 && q->input_checkpoints[1].bytes[0]==std::byte{1} && q->input_checkpoints[1].sources[0]==argument+0x100);*reinterpret_cast<unsigned*>(argument+0x100)=0;continue;}
        if(test>=3){require(q->fault && !q->outer_correction_applied && !q->preserve_applied && loads==1);continue;}
        require(q->outer_correction_verified && q->outer_correction_applied==(test==2));
        require(q->delayed_loaded && !q->fault && loads==1 && q->preserve_verified);
        if(test==0){require(q->load_entry_rng_valid && q->load_entry_rng==456);
            require(q->measurements[2].rng==P::advance_rng(456));
            require(*reinterpret_cast<unsigned*>(base+0x485EB28)==123);}
        require(q->restored_valid && q->restored_save_unchanged && q->restored_ring_equal && q->restored_byte_differences==0 && q->restored.writes==q->saved.writes);
        require(q->delayed_keys_captured && q->delayed_loaded_keys[0]==(test==2?101u:100u) && q->delayed_loaded_players[0]==P::ptr(base+0x470DE90));
        require(*reinterpret_cast<unsigned*>(base+0x470D0C4)==100);
        require(q->rings[2].restore(base,*scratch));
        *reinterpret_cast<unsigned*>(base+0x470D0C4)=101;
        q->delayed_step(base,argument,delayed_save,delayed_load,check);
        require(q->replay_done && q->replay_keys_equal && q->replay_ring_equal && !q->replay_byte_differences && loads==1);
        require(q->blends[0].valid && q->blends[4].valid && q->blends[2].bytes==q->blends[3].bytes);
        {
            auto refreshed=std::make_unique<P>();
            refreshed->general_combat_requested=true;refreshed->external_wind_lifecycle=true;
            require(refreshed->capture_blend(base,0) && refreshed->capture_blend(base,2));
            require(refreshed->capture_blend(base,6,2) && refreshed->intermediate_refreshes==1);
            require(refreshed->blends[6].bytes==refreshed->blends[2].bytes);
            refreshed->transforms[6].valid=false;
            require(refreshed->capture_blend(base,7,6) && refreshed->intermediate_fallbacks==1);
            require(refreshed->transforms[7].valid && refreshed->transforms[7].same_topology(refreshed->transforms[2]));
        }
        auto owner=q->blends[0].owners[0];
        *reinterpret_cast<unsigned char*>(owner+0x4BC)=0x5A;
        require(q->capture_blend(base,4) && q->blends[4].bytes[0][0]==std::byte{0x5A});
        require(q->blends[0].bytes!=q->blends[4].bytes);
        *reinterpret_cast<unsigned char*>(owner+0x4BC)=0;
        *reinterpret_cast<uintptr_t*>(P::ptr(base+0x470DE90)+0x971F8)=0;
        require(!q->capture_blend(base,4) && !q->blends[4].valid);
        *reinterpret_cast<uintptr_t*>(P::ptr(base+0x470DE90)+0x971F8)=owner;
        for(unsigned side=0;side<2;++side){
            auto o=q->blends[0].owners[side];
            for(unsigned i=0;i<35;++i)*reinterpret_cast<unsigned*>(o+0x4BC+i*4)=i<24?0x3F59999A:0x3F400000;
        }
        require(q->capture_blend(base,0) && q->capture_blend(base,1));
        for(auto o:q->blends[0].owners)std::memset(reinterpret_cast<void*>(o+0x4BC),0,140);
        require(q->capture_blend(base,2) && q->preserve_blend_weights(base) && q->blend_weights_verified);
        for(auto o:q->blends[0].owners)std::memset(reinterpret_cast<void*>(o+0x4BC),0,140);
        // An unexpected second-player value must reject before touching player one.
        *reinterpret_cast<unsigned*>(q->blends[0].owners[1]+0x4BC)=1;
        require(q->capture_blend(base,2) && !q->preserve_blend_weights(base));
        require(*reinterpret_cast<unsigned*>(owner+0x4BC)==0);
        *reinterpret_cast<unsigned*>(q->blends[0].owners[1]+0x4BC)=0;
        // Arbitrary captured finite weights survive the native zero reset.
        // Include signed zero, negative, subnormal and >1 values: preservation
        // copies bits rather than imposing an unproven normalization rule.
        const unsigned weights[]={0x3E800000u,0x80000000u,0xBE000000u,1u,0x40000000u};
        for(auto o:q->blends[0].owners)
            for(unsigned i=0;i<35;++i)*reinterpret_cast<unsigned*>(o+0x4BC+i*4)=weights[i%5];
        require(q->capture_blend(base,0));
        for(auto o:q->blends[0].owners)
            for(unsigned i=0;i<35;++i)*reinterpret_cast<unsigned*>(o+0x4BC+i*4)=0x3F000000u;
        require(q->capture_blend(base,1));
        require(q->blends[0].bytes!=q->blends[1].bytes);
        for(auto o:q->blends[0].owners)std::memset(reinterpret_cast<void*>(o+0x4BC),0,140);
        require(q->capture_blend(base,2) && q->preserve_blend_weights(base) && q->blend_weights_verified);
        for(auto o:q->blends[0].owners)
            for(unsigned i=0;i<35;++i)require(*reinterpret_cast<unsigned*>(o+0x4BC+i*4)==weights[i%5]);
        for(unsigned invalid:{0x7F800000u,0xFF800000u,0x7FC00001u}){
            auto o=q->blends[0].owners[1];
            *reinterpret_cast<unsigned*>(o+0x4BC+34*4)=invalid;
            require(q->capture_blend(base,0) && q->capture_blend(base,1));
            for(auto p:q->blends[0].owners)std::memset(reinterpret_cast<void*>(p+0x4BC),0,140);
            require(q->capture_blend(base,2) && !q->preserve_blend_weights(base));
            for(auto p:q->blends[0].owners)
                for(unsigned i=0;i<35;++i)require(*reinterpret_cast<unsigned*>(p+0x4BC+i*4)==0);
        }
        auto table=base+0x600000;*reinterpret_cast<int*>(table)=20;
        for(auto o:q->blends[0].owners){
            *reinterpret_cast<uintptr_t*>(o+0x1DD8+8)=table;
            *reinterpret_cast<unsigned*>(o+0x1DD8+16)=12;
            *reinterpret_cast<std::uint64_t*>(o+0x1DD8+24)=0x7FFFDFFDFull;
        }
        require(q->capture_blend(base,0) && q->capture_blend(base,1));
        for(auto o:q->blends[0].owners){
            *reinterpret_cast<unsigned*>(o+0x1DD8+16)=0;
            *reinterpret_cast<std::uint64_t*>(o+0x1DD8+24)=0;
        }
        require(q->capture_blend(base,5));q->blend_weights_verified=true;
        *reinterpret_cast<int*>(table)=12;
        require(!q->preserve_blend_blocks(base) && *reinterpret_cast<unsigned*>(owner+0x1DD8+16)==0);
        *reinterpret_cast<int*>(table)=20;
        // Unknown second-player changes reject before any first-player write.
        auto second=q->blends[0].owners[1];
        *reinterpret_cast<unsigned*>(second+0x1DD8+36)=1;
        require(q->capture_blend(base,5) && !q->preserve_blend_blocks(base));
        require(*reinterpret_cast<unsigned*>(owner+0x1DD8+16)==0);
        *reinterpret_cast<unsigned*>(second+0x1DD8+36)=0;
        require(q->capture_blend(base,5) && q->preserve_blend_blocks(base) && q->blend_blocks_verified);
        require(*reinterpret_cast<unsigned*>(owner+0x1DD8+16)==12);
        auto special=second+0x1DD8+2*176;
        *reinterpret_cast<uintptr_t*>(special+8)=table;
        *reinterpret_cast<unsigned*>(special+16)=12;
        require(q->capture_blend(base,0) && q->capture_blend(base,1));
        *reinterpret_cast<unsigned*>(special+16)=3;
        require(q->capture_blend(base,5) && !q->preserve_blend_blocks(base));
        q->combat_load_diagnostic=true;q->throw_requested=true;q->delayed_requested=false;
        *reinterpret_cast<unsigned*>(special+16)=4;
        require(q->capture_blend(base,5) && !q->preserve_blend_blocks(base));
        *reinterpret_cast<unsigned*>(special+16)=3;
        require(q->capture_blend(base,5));
        *reinterpret_cast<int*>(table)=12;
        require(!q->preserve_blend_blocks(base) && *reinterpret_cast<unsigned*>(special+16)==3);
        *reinterpret_cast<int*>(table)=20;
        require(q->preserve_blend_blocks(base) && *reinterpret_cast<unsigned*>(special+16)==12);
        // Resource-derived block2 selection works without a move-specific flag.
        q->combat_load_diagnostic=false;q->throw_requested=false;
        auto player=P::ptr(base+0x470DE90+8),resource=base+0x600100;
        const auto old_table=P::ptr(player+0x971E8),old_resource=P::ptr(player+0x971F0);
        *reinterpret_cast<uintptr_t*>(player+0x971E8)=table;
        *reinterpret_cast<uintptr_t*>(player+0x971F0)=resource;
        *reinterpret_cast<unsigned char*>(resource+3)=0x85; // masked selection5
        q->measure(base,0);
        *reinterpret_cast<unsigned*>(special+16)=6;
        require(q->capture_blend(base,5) && !q->preserve_blend_blocks(base));
        require(*reinterpret_cast<unsigned*>(special+16)==6);
        *reinterpret_cast<unsigned*>(special+16)=5;
        *reinterpret_cast<uintptr_t*>(player+0x971F0)=resource+16;
        require(q->capture_blend(base,5) && !q->preserve_blend_blocks(base));
        *reinterpret_cast<uintptr_t*>(player+0x971F0)=resource;
        require(q->preserve_blend_blocks(base) && *reinterpret_cast<unsigned*>(special+16)==12);
        *reinterpret_cast<uintptr_t*>(player+0x971E8)=old_table;
        *reinterpret_cast<uintptr_t*>(player+0x971F0)=old_resource;
        // Animation advances within one resource: selection/mask/weights change.
        *reinterpret_cast<unsigned*>(special+32)=0x3E800000u;
        require(q->capture_blend(base,0));
        *reinterpret_cast<unsigned*>(special+16)=14;
        *reinterpret_cast<std::uint64_t*>(special+24)=2;
        *reinterpret_cast<unsigned*>(special+32)=0x3F000000u;
        require(q->capture_blend(base,1));
        *reinterpret_cast<unsigned*>(special+16)=0;
        *reinterpret_cast<std::uint64_t*>(special+24)=0;
        // A value belonging to neither captured frame must reject before writes.
        *reinterpret_cast<unsigned*>(special+32)=0x3F400000u;
        require(q->capture_blend(base,5) && !q->preserve_blend_blocks(base));
        require(*reinterpret_cast<unsigned*>(special+16)==0);
        *reinterpret_cast<unsigned*>(special+32)=0x3F000000u;
        require(q->capture_blend(base,5) && q->preserve_blend_blocks(base));
        require(*reinterpret_cast<unsigned*>(special+16)==12 &&
                *reinterpret_cast<unsigned*>(special+32)==0x3E800000u);
        for(auto o:q->blends[0].owners)std::memset(reinterpret_cast<void*>(o+0x1DD8),0,1232);
        auto count=saves;q->delayed_step(base,argument,delayed_save,delayed_load,check);require(saves==count);
    }
    require(!P::throw_values(487,10041,1.0f,-1));
    require(!P::throw_values(487,10041,0.0f,0));
    require(!P::throw_values(488,10041,0.0f,-1));
    require(P::reversal_values(187,193,819.0f/16384.0f,-1));
    require(!P::reversal_values(187,193,0.05f,-1));
    require(!P::reversal_values(187,65535,819.0f/16384.0f,-1));
    require(!P::reversal_values(188,193,819.0f/16384.0f,-1));
    require(!P::reversal_values(187,193,819.0f/16384.0f,0));
    require(P::edge_values(656,65535,0.0f,0));
    require(!P::edge_values(679,65535,0.0f,0));
    require(!P::edge_values(656,10041,0.0f,0));
    require(!P::edge_values(656,65535,1.0f,0));
    require(!P::edge_values(656,65535,0.0f,-1));
    { auto q=std::make_unique<P>();q->edge_requested=true;q->attack_requested=true;
      require(!q->trigger_values(656,65535,0.0f,0)); }
    require(P::attack_values(679,65535,0.0f,0));
    require(!P::attack_values(219,65535,0.0f,0));
    require(!P::attack_values(679,65535,1.0f,0));
    require(!P::attack_values(679,10041,0.0f,0));
    require(!P::attack_values(679,65535,0.0f,-1));
    require(P::charge_values(219,65535,0.0f,0));
    require(!P::charge_values(219,65535,1.0f,0));
    require(!P::charge_values(219,10041,0.0f,0));
    require(!P::charge_values(487,65535,0.0f,0));
    for(int test=0;test<5;++test){auto t=std::make_unique<T>();unsigned x=42;
        if(test!=0){t->stream.slots->write(&t->stream,&x,4);require(t->seal());}
        require(reject_case(&t->stream,test)==T::rejection);
    }
    auto p=std::make_unique<P>();p->run(0);require(p->fault && !p->restore_invoked);
    {
        mode=0;saves=loads=checks=0;
        auto q=std::make_unique<P>();q->combat_load_diagnostic=true;q->throw_requested=true;q->preserve_requested=true;
        *reinterpret_cast<std::uint16_t*>(base+0x475A04A)=487;
        *reinterpret_cast<std::uint16_t*>(base+0x47F143A)=10041;
        *reinterpret_cast<float*>(base+0x48462F4)=0;
        *reinterpret_cast<int*>(base+0x4846458)=-1;
        *reinterpret_cast<uintptr_t*>(base+0x470DE90)=0;
        q->run(base,save,load,check);
        require(q->preflight_ok && q->fault && saves==0 && loads==0 && !q->restore_invoked);
    }
    {
        mode=1;saves=loads=0;
        auto q=std::make_unique<P>();q->combat_load_diagnostic=true;q->attempted=true;
        q->capture_combat_next(base,save,check);
        require(q->combat_next_attempted && !q->combat_next_valid && q->fault && saves==0 && loads==0);
        q->capture_combat_next(base,save,check);require(saves==0 && loads==0);
    }
    for(unsigned test=0;test<5;++test){
        auto a=std::make_unique<T>(),b=std::make_unique<T>();auto pool=base+0x600000;
        std::memset(reinterpret_cast<void*>(pool),0,0x41E0);
        *reinterpret_cast<uintptr_t*>(pool+0xF80)=base+0x3E88858;
        a->stream.slots->write(&a->stream,reinterpret_cast<void*>(pool),0x41E0);require(a->seal());
        *reinterpret_cast<unsigned*>(pool+0x1350)=1;
        b->stream.slots->write(&b->stream,reinterpret_cast<void*>(pool),0x41E0);require(b->seal());
        a->traces[0].writer=b->traces[0].writer=base+0x324942;
        if(test==1)*reinterpret_cast<unsigned*>(pool+0x1350)=2;
        if(test==2)b->stream.payload[0]^=std::byte{1};
        if(test==3)a->stream.payload[0xF80]^=std::byte{1};
        if(test==4)b->traces[0].source+=8;
        sc6::research::ThrowCameraFlag flag;
        if(test==0){require(flag.restore(*a,*b,base) && flag.verified && *reinterpret_cast<unsigned*>(pool+0x1350)==0);require(!flag.restore(*a,*b,base));}
        else require(!flag.restore(*a,*b,base) && *reinterpret_cast<unsigned*>(pool+0x1350)==(test==1?2u:1u));
    }
    for(unsigned test=0;test<6;++test){
        auto a=std::make_unique<T>(),b=std::make_unique<T>();auto pool=base+0x600000;
        std::memset(reinterpret_cast<void*>(pool),0,0x41E0);
        *reinterpret_cast<uintptr_t*>(pool+0xF80)=base+0x3E88858;
        a->stream.slots->write(&a->stream,reinterpret_cast<void*>(pool),0x41E0);require(a->seal());
        b->stream.slots->write(&b->stream,reinterpret_cast<void*>(pool),0x41E0);require(b->seal());
        a->traces[0].writer=b->traces[0].writer=base+0x324942;
        if(test==2)b->stream.payload[0]^=std::byte{1};
        if(test==3)*reinterpret_cast<unsigned*>(pool)=1;
        if(test==4)a->stream.payload[0xF80]^=std::byte{1};
        if(test==5)b->traces[0].source+=8;
        sc6::research::ThrowCameraFlag flag;
        bool result=flag.restore(*a,*b,base,test!=1);
        if(test==0){require(result && flag.unchanged_verified && flag.verified);require(!flag.restore(*a,*b,base,true));}
        else require(!result && !flag.verified);
        require(*reinterpret_cast<unsigned*>(pool+0x1350)==0);
    }
    for(unsigned test=0;test<8;++test){
        auto a=std::make_unique<T>(),b=std::make_unique<T>();auto pool=base+0x600000;
        std::memset(reinterpret_cast<void*>(pool),0,0x41E0);
        *reinterpret_cast<uintptr_t*>(pool+0xF80)=base+0x3E88B28;
        a->stream.slots->write(&a->stream,reinterpret_cast<void*>(pool),0x41E0);require(a->seal());
        b->stream.slots->write(&b->stream,reinterpret_cast<void*>(pool),0x41E0);require(b->seal());
        a->traces[0].writer=b->traces[0].writer=base+0x324942;
        if(test==2)b->stream.payload[0]^=std::byte{1};
        if(test==3)*reinterpret_cast<unsigned*>(pool)=1;
        if(test==4)a->stream.payload[0xF80]^=std::byte{1};
        if(test==5)b->traces[0].source+=8;
        if(test==6){b->stream.payload[0x1350]=std::byte{1};*reinterpret_cast<unsigned*>(pool+0x1350)=1;}
        sc6::research::ThrowCameraFlag flag;
        bool result=flag.restore(*a,*b,base,test==7,test!=1);
        if(test==0){require(result && flag.unchanged_verified && flag.verified);require(!flag.restore(*a,*b,base,false,true));}
        else require(!result && !flag.verified);
        require(*reinterpret_cast<unsigned*>(pool+0x1350)==(test==6?1u:0u));
    }
    for(unsigned test=0;test<10;++test){
        auto a=std::make_unique<T>(),b=std::make_unique<T>();auto pool=base+0x600000;
        std::memset(reinterpret_cast<void*>(pool),0,0x41E0);
        *reinterpret_cast<uintptr_t*>(pool+0xF80)=base+0x3E88858;
        a->stream.slots->write(&a->stream,reinterpret_cast<void*>(pool),0x41E0);require(a->seal());
        b->stream.slots->write(&b->stream,reinterpret_cast<void*>(pool),0x41E0);require(b->seal());
        a->traces[0].writer=b->traces[0].writer=base+0x324942;
        if(test==2)b->stream.payload[0]^=std::byte{1};
        if(test==3)*reinterpret_cast<unsigned*>(pool)=1;
        if(test==4)a->stream.payload[0xF80]^=std::byte{1};
        if(test==5)b->traces[0].source+=8;
        if(test==6){b->stream.payload[0x1350]=std::byte{1};*reinterpret_cast<unsigned*>(pool+0x1350)=1;}
        if(test==9)a->traces[0].writer+=8;
        sc6::research::ThrowCameraFlag flag;
        bool result=flag.restore(*a,*b,base,test==7,test==8,test!=1);
        if(test==0){require(result && flag.unchanged_verified && flag.verified);require(!flag.restore(*a,*b,base,false,false,true));}
        else require(!result && !flag.verified);
        require(*reinterpret_cast<unsigned*>(pool+0x1350)==(test==6?1u:0u));
    }
    // A rejection in a later group must leave the earlier group untouched.
    for(unsigned test=0;test<5;++test){
        using X=sc6::research::TransformSnapshot;
        auto a=std::make_unique<X>();auto b=std::make_unique<X>();auto c=std::make_unique<X>();
        a->valid=true;a->count=3;
        a->records[0].kind=0;a->records[0].size=512;a->records[0].address=base+0x600000;
        for(unsigned i=1;i<3;++i){auto& r=a->records[i];
            r.kind=1;r.size=88;r.address=base+0x610000+i*128;
            float f=1.0f;std::memcpy(r.bytes.data(),&f,4);
        }
        *b=*a;*c=*a;
        for(unsigned i=1;i<3;++i){float f=2.0f;std::memcpy(b->records[i].bytes.data(),&f,4);
            c->records[i].bytes=b->records[i].bytes;
            std::memcpy(reinterpret_cast<void*>(c->records[i].address),c->records[i].bytes.data(),88);
        }
        if(test==1)b->records[2].bytes[64]=std::byte{1};
        if(test==2)*reinterpret_cast<unsigned*>(c->records[2].address+4)=1;
        if(test==3){unsigned nan=0x7FC00000;std::memcpy(a->records[2].bytes.data(),&nan,4);}
        if(test==4)b->records[2].address+=8;
        require(a->restore_combat_groups(*b,*c)==(test==0));
        float value{};std::memcpy(&value,reinterpret_cast<void*>(a->records[1].address),4);
        require(value==(test==0?1.0f:2.0f));
    }
    for(unsigned test=0;test<6;++test){
        using C=sc6::research::CombatBlend;
        std::array<C::Bytes,2> a{},b{},c{};
        uintptr_t owners[]={base+0x620000,base+0x630000};
        auto table=base+0x640000;*reinterpret_cast<int*>(table)=20;
        for(unsigned side=0;side<2;++side){
            unsigned weight=0x3F59999A,index=12;std::uint64_t mask=1;
            std::memcpy(a[side].data(),&weight,4);
            std::memcpy(a[side].data()+148,&table,8);
            std::memcpy(a[side].data()+156,&index,4);
            std::memcpy(a[side].data()+164,&mask,8);
            b[side]=a[side];std::memset(b[side].data(),0,140);std::memset(b[side].data()+164,0,8);
            c[side]=b[side];std::memset(c[side].data()+156,0,4);
            c[side][1372]=std::byte{42};
            std::memcpy(reinterpret_cast<void*>(owners[side]+0x4BC),c[side].data(),140);
            std::memcpy(reinterpret_cast<void*>(owners[side]+0x1DD8),c[side].data()+140,1232);
            std::memcpy(reinterpret_cast<void*>(owners[side]+0x22A8),c[side].data()+1372,8);
        }
        if(test==1)b[1][180]=std::byte{1}; // Unknown adjacent-state change.
        if(test==2)*reinterpret_cast<int*>(table)=12; // Out-of-range saved index.
        if(test==3)*reinterpret_cast<unsigned*>(owners[1]+0x4BC)=1; // Stale live snapshot.
        if(test==4)b[1][0]=std::byte{1}; // Unknown weight.
        if(test==5){c[1][180]=std::byte{1};*reinterpret_cast<unsigned char*>(owners[1]+0x1DD8+40)=1;}
        require(C::restore(a,b,c,owners)==(test==0));
        require(*reinterpret_cast<unsigned*>(owners[0]+0x4BC)==(test==0?0x3F59999Au:0u));
        require(*reinterpret_cast<unsigned char*>(owners[0]+0x22A8)==42);
    }
    {
        using C=sc6::research::CombatBlend;
        std::array<std::byte,176> a{},b{},c{};uint64_t pointer=0x1234567810ull;
        std::memcpy(c.data()+8,&pointer,8);C::LoadedDiagnostic d;
        require(!C::fail_block(&d,1,5,8,a.data(),b.data(),c.data()));
        require(d.reason==5 && d.side==1 && d.block==5 && d.offset==8 && d.pointer_valid==1);
        require(d.pointer_saved==0 && d.pointer_next==0 && d.pointer_loaded==pointer);
        require(!C::fail_block(&d,0,0,20,a.data(),b.data(),c.data()) && !d.pointer_valid);
    }
    for(unsigned fault=0;fault<5;++fault){
        using C=sc6::research::CombatBlend;
        std::array<C::Bytes,2> a{},b{},loaded{};
        uintptr_t owners[]={base+0x620000,base+0x630000};
        for(unsigned side=0;side<2;++side){
            unsigned old=0x3F6B2C6B,next=0x3F6AB8B8;
            std::memcpy(a[side].data()+172,&old,4);b[side]=a[side];
            std::memcpy(b[side].data()+172,&next,4);loaded[side]=b[side];
        }
        if(fault==1)loaded[1][172]=std::byte{0}; // unexplained third loaded value
        if(fault==2)b[1][140]=std::byte{1}; // pointer identity
        if(fault==3){unsigned nan=0x7FC00000u;std::memcpy(b[1].data()+172,&nan,4);}
        if(fault==4)b[1][312]=std::byte{1}; // trailing block padding
        for(unsigned side=0;side<2;++side){
            std::memcpy(reinterpret_cast<void*>(owners[side]+0x4BC),loaded[side].data(),140);
            std::memcpy(reinterpret_cast<void*>(owners[side]+0x1DD8),loaded[side].data()+140,1232);
            std::memcpy(reinterpret_cast<void*>(owners[side]+0x22A8),loaded[side].data()+1372,8);
        }
        require(!C::adjacent(a[0],b[0]));
        C::LoadedDiagnostic detail;
        require(C::ready(a,b,loaded,owners,false,true,&detail)==(fault==0));
        if(fault==1){require(detail.reason==6 && detail.side==1 && detail.block==0 && detail.offset==32);
            require(detail.saved==0x3F6B2C6Bu && detail.next==0x3F6AB8B8u && detail.loaded==0x3F6AB800u);}
        require(C::restore(a,b,loaded,owners,false,true)==(fault==0));
        require(!std::memcmp(reinterpret_cast<void*>(owners[0]+0x1DD8),(fault?loaded:a)[0].data()+140,1232));
    }
    {
        using C=sc6::research::CombatBlend;
        C::Bytes a{},b{};std::array<std::byte,96> wrapper{};
        unsigned parameter=0x3EE54EF2u,one=0x3F800000u;
        std::memcpy(wrapper.data()+0x20,&parameter,4);std::memcpy(wrapper.data()+0x54,&one,4);
        std::uint64_t mask=(1ull<<7)|(1ull<<8)|(1ull<<19)|(1ull<<20);
        for(unsigned block:{0u,2u}){
            unsigned old=block?0x3F25DAACu:0x3EB44AA8u,next=block?parameter:0x3F0D5887u;
            for(unsigned off:{0x3Cu,0x40u,0x6Cu,0x70u}){
                std::memcpy(a.data()+140+block*176+off,&old,4);
                std::memcpy(b.data()+140+block*176+off,&next,4);}
            std::memcpy(a.data()+140+block*176+24,&mask,8);
            std::memcpy(b.data()+140+block*176+24,&mask,8);
        }
        require(C::charge_adjacent(a,b,wrapper.data()) && !C::adjacent(a,b));
        auto original=b;
        for(unsigned off:{140u+0x3Cu,140u+0x44u,140u+176u+0x3Cu,140u+352u+0x3Cu}){
            b[off]^=std::byte{1};require(!C::charge_adjacent(a,b,wrapper.data()));b=original;}
        wrapper[0x38]=std::byte{1};require(!C::charge_adjacent(a,b,wrapper.data()));wrapper[0x38]=std::byte{0};
        unsigned nan=0x7FC00000u;std::memcpy(wrapper.data()+0x20,&nan,4);
        require(!C::charge_adjacent(a,b,wrapper.data()));
        require(!C::charge_adjacent(a,b,nullptr));
    }
    for(unsigned fault=0;fault<10;++fault){
        using C=sc6::research::CombatBlend;
        std::array<C::Bytes,2> a{},b{},loaded{};
        uintptr_t owners[]={base+0x620000,base+0x630000},tables[]={base+0x640000,base+0x640100};
        uintptr_t resources[]={base+0x641000,base+0x641100};
        for(unsigned side=0;side<2;++side){
            *reinterpret_cast<int*>(tables[side])=20;
            *reinterpret_cast<unsigned char*>(resources[side]+3)=0x83; // native 7-bit selection
            const unsigned offset=140+2*176;
            std::memcpy(a[side].data()+offset+8,&tables[side],8);
            unsigned old=12;std::memcpy(a[side].data()+offset+16,&old,4);
            b[side]=a[side];loaded[side]=a[side];
            unsigned selected=3;std::memcpy(loaded[side].data()+offset+16,&selected,4);
        }
        const unsigned offset=140+2*176;
        if(fault==1)*reinterpret_cast<unsigned char*>(resources[1]+3)=4;
        if(fault==2)tables[1]+=8;
        if(fault==3)*reinterpret_cast<int*>(tables[1])=12; // saved selection out of bounds
        if(fault==4)loaded[1][offset+24]=std::byte{1};
        if(fault==5)resources[1]=0;
        if(fault==6){ // correct value at the wrong block is still rejected
            for(auto* state:{&a,&b,&loaded}){
                std::memcpy((*state)[1].data()+140+176,(*state)[1].data()+offset,176);
                std::memset((*state)[1].data()+offset,0,176);
            }
        }
        if(fault==9){ // saved next selection also must be in bounds
            unsigned bad=20;std::memcpy(b[1].data()+offset+16,&bad,4);
        }
        for(unsigned side=0;side<2;++side){
            std::memcpy(reinterpret_cast<void*>(owners[side]+0x4BC),loaded[side].data(),140);
            std::memcpy(reinterpret_cast<void*>(owners[side]+0x1DD8),loaded[side].data()+140,1232);
            std::memcpy(reinterpret_cast<void*>(owners[side]+0x22A8),loaded[side].data()+1372,8);
        }
        require(C::restore(a,b,loaded,owners,false,fault!=7,tables,fault==8?nullptr:resources)==(fault==0));
        for(unsigned side=0;side<2;++side)
            require(!std::memcmp(reinterpret_cast<void*>(owners[side]+0x1DD8),(fault?loaded:a)[side].data()+140,1232));
    }
    // Inactive block5 may alternate between null and the verified wrapper table.
    // Validate both directions, reject foreign/active states, and never partially write.
    for(unsigned direction=0;direction<2;++direction)for(unsigned fault=0;fault<10;++fault){
        using C=sc6::research::CombatBlend;
        std::array<C::Bytes,2> a{},b{},loaded{};
        uintptr_t owners[]={base+0x620000,base+0x630000},tables[]={base+0x640000,base+0x640100};
        *reinterpret_cast<unsigned*>(tables[1])=20;
        const unsigned offset=140+5*176;
        std::memcpy((direction?b:a)[1].data()+offset+8,&tables[1],8);
        std::memcpy(loaded[1].data()+offset+8,&tables[1],8);
        unsigned weight=0x3F000000u;std::memcpy(a[0].data()+172,&weight,4);
        if(fault==1)tables[1]+=8;
        if(fault==2)a[1][offset+24]=std::byte{1};
        if(fault==3)b[1][offset+16]=std::byte{1};
        if(fault==4)loaded[1][offset+20]=std::byte{1};
        if(fault==5)loaded[1][offset]=std::byte{1};
        if(fault==6){a[0]=a[1];b[0]=b[1];loaded[0]=loaded[1];a[1]={};b[1]={};loaded[1]={};}
        if(fault==7){for(auto* state:{&a,&b,&loaded}){std::memcpy((*state)[1].data()+140+4*176,(*state)[1].data()+offset,176);std::memset((*state)[1].data()+offset,0,176);}}
        for(unsigned side=0;side<2;++side){
            std::memcpy(reinterpret_cast<void*>(owners[side]+0x4BC),loaded[side].data(),140);
            std::memcpy(reinterpret_cast<void*>(owners[side]+0x1DD8),loaded[side].data()+140,1232);
            std::memcpy(reinterpret_cast<void*>(owners[side]+0x22A8),loaded[side].data()+1372,8);
        }
        require(!C::adjacent(a[fault==6?0:1],b[fault==6?0:1],false,nullptr,true));
        require(C::restore(a,b,loaded,owners,false,fault!=8,fault==9?nullptr:tables)==(fault==0));
        for(unsigned side=0;side<2;++side){
            require(!std::memcmp(reinterpret_cast<void*>(owners[side]+0x4BC),(fault?loaded:a)[side].data(),140));
            require(!std::memcmp(reinterpret_cast<void*>(owners[side]+0x1DD8),(fault?loaded:a)[side].data()+140,1232));
        }
    }
    {
        auto table_probe=std::make_unique<sc6::research::FullStateProbe>();
        const unsigned offset=140+5*176;
        table_probe->general_combat_requested=true;
        for(auto i:{0u,1u}){table_probe->blends[i].valid=true;table_probe->blends[i].owners[1]=base+0x630000;
            table_probe->blends[i].tables[1]=base+0x640100;table_probe->blends[i].resources[1]=base+0x641100;}
        std::memcpy(table_probe->blends[0].bytes[1].data()+offset+8,&table_probe->blends[0].tables[1],8);
        require(table_probe->combat_blend_ready());
        ++table_probe->blends[1].resources[1];require(!table_probe->combat_blend_ready());
        require(table_probe->blend_pair_diagnostic.table_saved==base+0x640100 && table_probe->blend_pair_diagnostic.table_next==0);
        require(table_probe->blend_pair_diagnostic.resource_saved!=table_probe->blend_pair_diagnostic.resource_next);
        --table_probe->blends[1].resources[1];++table_probe->blends[1].tables[1];require(!table_probe->combat_blend_ready());
    }
    for(unsigned fault=0;fault<12;++fault){
        using C=sc6::research::CombatBlend;
        std::array<C::Bytes,2> a{},b{},loaded{};
        uintptr_t owners[]={base+0x620000,base+0x630000},tables[]={base+0x640000,base+0x640100};
        const unsigned offset=140+5*176;
        std::memcpy(loaded[1].data()+offset+8,&tables[1],8);
        // A first-side change must not be written if the second side is rejected.
        unsigned weight=0x3F000000u;std::memcpy(a[0].data()+172,&weight,4);b[0]=a[0];loaded[0]=a[0];
        weight=0x3F400000u;std::memcpy(b[0].data()+172,&weight,4);loaded[0]=b[0];
        if(fault==1)tables[1]+=8; // unrelated pointer
        if(fault==2)a[1][offset+16]=b[1][offset+16]=std::byte{1};
        if(fault==3)loaded[1][offset+24]=std::byte{1};
        if(fault==4)a[1][offset+24]=b[1][offset+24]=std::byte{1};
        if(fault==5)loaded[1][offset]=std::byte{1}; // other pointer remains protected
        if(fault==6)loaded[1][offset+20]=std::byte{1};
        if(fault==7){loaded[0]=loaded[1];loaded[1]={};} // wrong side
        if(fault==8){std::memcpy(loaded[1].data()+140+4*176+8,&tables[1],8);}
        if(fault==9)a[1][offset+8]=b[1][offset+8]=std::byte{1}; // non-null saved pointer
        for(unsigned side=0;side<2;++side){
            std::memcpy(reinterpret_cast<void*>(owners[side]+0x4BC),loaded[side].data(),140);
            std::memcpy(reinterpret_cast<void*>(owners[side]+0x1DD8),loaded[side].data()+140,1232);
            std::memcpy(reinterpret_cast<void*>(owners[side]+0x22A8),loaded[side].data()+1372,8);
        }
        require(C::restore(a,b,loaded,owners,false,fault!=10,fault==11?nullptr:tables)==(fault==0));
        for(unsigned side=0;side<2;++side)
            require(!std::memcmp(reinterpret_cast<void*>(owners[side]+0x1DD8),(fault?loaded:a)[side].data()+140,1232));
    }
    for(unsigned test=0;test<10;++test){
        using C=sc6::research::CombatBlend;
        std::array<C::Bytes,2> a{},b{},c{};
        uintptr_t owners[]={base+0x620000,base+0x630000};
        unsigned side=test==4?1u:0u,block=test==5?1u:0u;
        auto offset=140+block*176;
        unsigned old=0x3E910AD0u,one=0x3F800000u;
        if(test==1)old=0x7FC00000u;
        if(test==2)old=0xBF000000u;
        if(test==3)old=0x3F800001u;
        for(unsigned off:{0x3Cu,0x40u,0x6Cu,0x70u}){
            std::memcpy(a[side].data()+offset+off,&old,4);
            std::memcpy(b[side].data()+offset+off,&one,4);
        }
        if(test==6)a[0][140+0x40]=std::byte{0}; // Unequal quartet.
        if(test==7)b[0][140+0x44]=std::byte{1}; // Neighbor remains protected.
        c=b;
        if(test==8)c[0][140+0x3C]=std::byte{1}; // Unexpected loaded weight.
        for(unsigned i=0;i<2;++i){
            std::memcpy(reinterpret_cast<void*>(owners[i]+0x4BC),c[i].data(),140);
            std::memcpy(reinterpret_cast<void*>(owners[i]+0x1DD8),c[i].data()+140,1232);
            std::memcpy(reinterpret_cast<void*>(owners[i]+0x22A8),c[i].data()+1372,8);
        }
        if(test==9)*reinterpret_cast<unsigned*>(owners[1]+0x4BC)=1;
        require(!C::adjacent(a[side],b[side])); // Explicit throw opt-in required.
        require(C::restore(a,b,c,owners,true)==(test==0));
        require(C::word(reinterpret_cast<std::byte*>(owners[side]+0x1DD8+block*176+0x3C))==
                (test==0?old:C::word(c[side].data()+offset+0x3C)));
    }
    {
        auto rehearsal=std::make_unique<P>();auto old_saves=saves,old_loads=loads;
        rehearsal->throw_audit=true;rehearsal->audit_done=true;
        rehearsal->combat_replay_preflight(base);
        require(rehearsal->combat_preflight_attempted && rehearsal->combat_preflight_pass_mask==0);
        require(saves==old_saves && loads==old_loads && !rehearsal->restore_invoked);
        rehearsal->combat_preflight_pass_mask=1;rehearsal->combat_replay_preflight(base);
        require(rehearsal->combat_preflight_pass_mask==1); // once-only
    }
    {
        auto combat_probe=std::make_unique<P>();
        combat_probe->combat_replay_requested=true;combat_probe->throw_requested=true;
        combat_probe->throw_audit=true;combat_probe->delayed_requested=true;
        auto old_loads=loads,old_saves=saves;
        // A malformed input envelope must never get as far as native save/load.
        combat_probe->delayed_step(base,0,delayed_save,delayed_load,check);
        require(combat_probe->fault && !combat_probe->restore_invoked && loads==old_loads && saves==old_saves);
        combat_probe->delayed_step(base,0,delayed_save,delayed_load,check);
        require(loads==old_loads && saves==old_saves);
        combat_probe=std::make_unique<P>();
        combat_probe->combat_replay_requested=true;combat_probe->throw_requested=true;
        combat_probe->throw_audit=true;combat_probe->audit_started=true;combat_probe->audit_done=true;
        std::array<std::byte,40> neutral{};
        uintptr_t arguments[]={reinterpret_cast<uintptr_t>(neutral.data()),reinterpret_cast<uintptr_t>(neutral.data()+8),reinterpret_cast<uintptr_t>(neutral.data()+16)};
        *reinterpret_cast<std::uint16_t*>(base+0x475A04A)=487;
        *reinterpret_cast<std::uint16_t*>(base+0x47F143A)=10041;
        *reinterpret_cast<float*>(base+0x48462F4)=0;
        *reinterpret_cast<int*>(base+0x4846458)=-1;
        mode=0;
        combat_probe->delayed_step(base,reinterpret_cast<uintptr_t>(arguments),delayed_save,delayed_load,check);
        require(combat_probe->fault && combat_probe->combat_preflight_attempted &&
                combat_probe->combat_preflight_pass_mask!=255 && !combat_probe->restore_invoked);
        require(loads==old_loads && saves==old_saves);
        for(unsigned boundary=0;boundary<2;++boundary){
            combat_probe=std::make_unique<P>();
            combat_probe->combat_replay_requested=true;combat_probe->throw_requested=true;
            combat_probe->throw_audit=true;combat_probe->audit_started=true;combat_probe->audit_done=true;
            combat_probe->delayed_loaded=boundary!=0;
            neutral[30]=std::byte{1};
            combat_probe->delayed_step(base,reinterpret_cast<uintptr_t>(arguments),delayed_save,delayed_load,check);
            require(!combat_probe->delayed_inputs_equal);
            require(combat_probe->combat_comparison_attempted==(boundary!=0));
            // Incomplete snapshots still reject; the future input must not be
            // what prevents reaching phase2 comparison. No native load occurs.
            require(combat_probe->fault && loads==old_loads && saves==old_saves);
        }
    }
    for(unsigned test=0;test<4;++test){
        using X=sc6::research::TransformSnapshot;
        auto original=std::make_unique<X>(),next=std::make_unique<X>();
        original->valid=true;original->count=2;
        for(unsigned side=0;side<2;++side){auto& r=original->records[side];
            r.kind=0;r.side=side;r.size=512;r.address=base+0x650000+side*0x1000;
        }
        *next=*original;
        for(unsigned side=0;side<2;++side){float value=1.0f;
            std::memcpy(next->records[side].bytes.data()+0x1F0,&value,4);
            std::memcpy(reinterpret_cast<void*>(next->records[side].address),next->records[side].bytes.data(),512);
        }
        if(test==1)original->records[1].bytes[0x20]=std::byte{1};
        if(test==2)*reinterpret_cast<unsigned*>(next->records[1].address+0x1F0)=0;
        if(test==3){unsigned nan=0x7FC00000;std::memcpy(original->records[1].bytes.data()+0x1F0,&nan,4);}
        require(original->restore_root_wind(*next,*next,true)==(test==0));
        require(*reinterpret_cast<float*>(next->records[0].address+0x1F0)==1.0f);
        require(original->restore_root_wind(*next,*next)==(test==0));
        require(*reinterpret_cast<float*>(next->records[0].address+0x1F0)==(test==0?0.0f:1.0f));
    }
    {
        std::array<std::byte,224> before{},after{};
        auto put=[](auto& fields,unsigned offset,unsigned value){std::memcpy(fields.data()+128+offset,&value,4);};
        put(before,0,1);put(before,8,1);
        put(after,0x1C,1);
        for(unsigned side=0;side<2;++side){
            put(before,0x1C,side==0?1u:0u);
            put(before,0x38,side==0?0u:1u);
            require(P::combat_transition(before,after,side,false));
            put(before,0x38,1);
            require(P::combat_transition(before,after,side,false));
            require(P::combat_transition(before,after,side,false,false,true));
            require(P::combat_transition(before,after,side,true,false,true));
            require(P::combat_transition(before,after,side,false,true,true));
            put(before,0x38,2);
            require(!P::combat_transition(before,after,side,false,false,true));
            put(before,0x38,1);put(after,0x38,1);
            require(!P::combat_transition(before,after,side,false,false,true));
            put(after,0x38,0);put(before,0x1C,7);
            require(!P::combat_transition(before,after,side,false,false,true));
            for(unsigned saved_mode=0;saved_mode<7;++saved_mode){
                put(before,0x1C,saved_mode);
                require(P::combat_transition(before,after,side,false));
            }
            put(after,0x1C,2);
            require(!P::combat_transition(before,after,side,false));
            put(after,0x1C,1);
        }
        auto trigger_probe=std::make_unique<P>();
        require(trigger_probe->accept_replay_trigger(true,100));
        trigger_probe->combat_later_window=true;
        require(!trigger_probe->accept_replay_trigger(true,100));
        require(trigger_probe->accept_replay_trigger(true,101));
        require(!trigger_probe->accept_replay_trigger(false,102));
        require(!trigger_probe->accept_replay_trigger(true,103));
        require(!trigger_probe->accept_replay_trigger(true,105));
        require(trigger_probe->accept_replay_trigger(true,106));
    }
    // Immediate completion: retained roots/groups/matrices, ring aliases and
    // the separate camera reset. Late validation failure must write nothing.
    for(unsigned failure=0;failure<39;++failure){
        auto a=std::make_unique<P>(),b=std::make_unique<P>(),c=std::make_unique<P>();
        auto& x=a->transforms[0];x.valid=true;x.count=4;
        auto root=base+0x700000,group=base+0x701000,link=base+0x702000,target=base+0x703000;
        auto camera=base+0x704000;
        x.records[0].address=root;x.records[0].kind=0;x.records[0].size=512;
        x.records[1].address=group;x.records[1].kind=1;x.records[1].size=88;x.records[1].parent=0;
        x.records[2].address=link;x.records[2].kind=16;x.records[2].size=8;
        std::memcpy(x.records[2].bytes.data(),&target,8);
        x.records[3].address=target+0x80;x.records[3].kind=18;x.records[3].size=64;x.records[3].parent=2;
        *reinterpret_cast<uintptr_t*>(target)=base+0x3E88E38;
        auto& y=b->transforms[0];y=x;
        float newer=2.0f;
        std::memcpy(y.records[0].bytes.data()+0x1F0,&newer,4);
        std::memcpy(y.records[1].bytes.data(),&newer,4);
        std::memcpy(y.records[3].bytes.data(),&newer,4);
        c->transforms[3]=y;
        for(unsigned i=0;i<y.count;++i)std::memcpy(reinterpret_cast<void*>(y.records[i].address),y.records[i].bytes.data(),y.records[i].size);
        a->rings[0].valid=true;
        for(unsigned i=0;i<4;++i){auto& h=a->rings[0].headers[i];h.current=root+0x10000+i*0x1000;h.previous=h.current+0x100;}
        b->rings[0]=a->rings[0];c->restored_rings=a->rings[0];
        for(unsigned i=0;i<4;++i){b->rings[0].headers[i].index=2;b->rings[0].headers[i].previous=a->rings[0].headers[i].current;}
        a->blends[0].valid=b->blends[0].valid=c->blends[3].valid=true;
        for(unsigned side=0;side<2;++side){auto owner=base+0x710000+side*0x10000;
            a->blends[0].owners[side]=b->blends[0].owners[side]=c->blends[3].owners[side]=owner;
            auto prior=a->rings[0].headers[side*2].previous,next=b->rings[0].headers[side*2].previous;
            std::memcpy(a->blends[0].bytes[side].data()+1372,&prior,8);
            std::memcpy(b->blends[0].bytes[side].data()+1372,&next,8);
            std::memcpy(c->blends[3].bytes[side].data()+1372,&next,8);
            std::memcpy(reinterpret_cast<void*>(owner+0x22A8),&next,8);
        }
        auto& ca=a->cameras[0];ca.valid=true;ca.count=1;ca.records[0].owner=camera;
        auto vt=base+0x3E881C8;std::memcpy(ca.records[0].bytes.data(),&vt,8);
        std::memcpy(ca.records[0].bytes.data()+0x154,&newer,4);
        b->cameras[0]=ca;c->cameras[4]=ca;
        std::memset(c->cameras[4].records[0].bytes.data()+0x154,0,4);
        std::memcpy(reinterpret_cast<void*>(camera),c->cameras[4].records[0].bytes.data(),456);
        if(failure==1)ca.records[0].bytes[0]=std::byte{0}; // late identity rejection
        if(failure==2){unsigned nan=0x7FC00000;std::memcpy(x.records[3].bytes.data(),&nan,4);}
        if(failure==3)*reinterpret_cast<unsigned*>(target+0x80)=1; // stale live matrix
        if(failure==4)x.records[3].kind=19; // topology mismatch
        if(failure==5)a->blends[0].bytes[0][1372]^=std::byte{1}; // unsupported alias
        if(failure>=6 && failure<10){
            c->direction_restore_requested=failure!=7;
            for(unsigned off:{0x60u,0x110u,0x20u,0x24u,0x28u,0x50u,0x64u,0x114u,0x130u,0x154u})
                std::memcpy(b->cameras[0].records[0].bytes.data()+off,&newer,4);
            if(failure==8)b->cameras[0].records[0].bytes[0x30]^=std::byte{1};
            if(failure==9){unsigned nan=0x7FC00000;std::memcpy(b->cameras[0].records[0].bytes.data()+0x24,&nan,4);}
        }
        if(failure>=10){ // Later-frame values are not copied by this correction.
            for(unsigned off:{0x24u,0x64u,0x114u})
                std::memcpy(b->cameras[0].records[0].bytes.data()+off,&newer,4);
            if(failure==11)b->cameras[0].records[0].bytes[0x154]^=std::byte{1};
            if(failure==12)b->cameras[0].records[0].bytes[0x30]^=std::byte{1};
            if(failure==15)b->cameras[0].records[0].bytes[0x60]^=std::byte{1};
            if(failure==16)b->cameras[0].records[0].bytes[0x110]^=std::byte{1};
            if(failure==14)b->cameras[0].records[0].bytes[0x20]^=std::byte{1};
            if(failure==13){unsigned nan=0x7FC00000;std::memcpy(b->cameras[0].records[0].bytes.data()+0x64,&nan,4);}
        }
        if(failure==17)b->cameras[0].records[0].bytes[0]^=std::byte{1};
        if(failure==18){ // Unrelated loaded change still rejects before any patch.
            c->cameras[4].records[0].bytes[0x30]^=std::byte{1};
            std::memcpy(reinterpret_cast<void*>(camera),c->cameras[4].records[0].bytes.data(),456);
        }
        if(failure==19){unsigned nan=0x7FC00000;std::memcpy(ca.records[0].bytes.data()+0x154,&nan,4);}
        if(failure>=20){
            // First matrix word retained, second already restored by the loader.
            std::memcpy(y.records[3].bytes.data()+4,&newer,4);
            if(failure==21){float unknown=3.0f;
                std::memcpy(c->transforms[3].records[3].bytes.data()+4,&unknown,4);
                std::memcpy(reinterpret_cast<void*>(target+0x84),&unknown,4);}
            if(failure==22){ // A changed non-float header must still fail.
                c->transforms[3].records[1].bytes[64]^=std::byte{1};
                *reinterpret_cast<unsigned char*>(group+64)^=1;}
        }
        const unsigned cameraValues[]={0x3DF439C0u,0xBF800000u,0x80000000u,1u,0x007FFFFFu,0x00800000u,0x7F7FFFFFu,0xFF7FFFFFu};
        unsigned archived144=0;
        if(failure>=23){
            archived144=cameraValues[(failure-23)%8];
            if(failure==31)archived144=0x7F800000;
            if(failure==32)archived144=0xFF800000;
            if(failure==33)archived144=0x7FC00000;
            if(failure==34)archived144=0x7F800001;
            std::memcpy(ca.records[0].bytes.data()+0x144,&archived144,4);
            if(failure==35){unsigned other=0x3F000000;std::memcpy(c->cameras[4].records[0].bytes.data()+0x144,&other,4);}
            if(failure==36)c->cameras[4].records[0].bytes[0x148]^=std::byte{1};
            if(failure==37)ca.records[0].owner+=8;
            if(failure==38)c->cameras[4].records[0].smoothing_valid=true;
            std::memcpy(reinterpret_cast<void*>(camera),c->cameras[4].records[0].bytes.data(),456);
        }
        auto correction=std::make_unique<sc6::research::ImmediateIdleCorrection>();
        bool ok=correction->apply(*a,*b,*c,base);
        require(ok==(failure==0 || (failure>=6 && failure<=16) || failure==20 || (failure>=23 && failure<=30)) && correction->verified==ok);
        require(ok ? correction->failure_line==0 : correction->failure_line!=0);
        require(*reinterpret_cast<float*>(root+0x1F0)==(ok?0.0f:newer));
        require(*reinterpret_cast<float*>(group)==(ok?0.0f:newer));
        require(*reinterpret_cast<float*>(camera+0x154)==(ok?newer:0.0f));
        if(ok){
            require(*reinterpret_cast<float*>(target+0x80)==0.0f && correction->count==(failure>=23?7u:6u));
            require(*reinterpret_cast<unsigned*>(camera+0x144)==archived144);
        } else if(failure>=23 && failure!=35)require(*reinterpret_cast<unsigned*>(camera+0x144)==0);
    }
    // Owned collision-target matrices: exact completion and transactional failures.
    for(unsigned scenario=0;scenario<12;++scenario){
        auto a=std::make_unique<P>(),b=std::make_unique<P>(),c=std::make_unique<P>();
        auto& x=a->transforms[0];x.valid=true;x.fixed_constraint_slots=true;x.count=7;
        const auto root=base+0x700000,target=base+0x703000,vt=base+0x3E88E38;
        x.records[0].address=root;x.records[0].kind=0;x.records[0].size=512;
        std::memcpy(x.records[0].bytes.data()+8,&target,8);
        x.records[1].address=target;x.records[1].kind=26;x.records[1].size=8;
        std::memcpy(x.records[1].bytes.data(),&vt,8);
        const unsigned kinds[]={27,18,19,20,21},sizes[]={8,64,64,32,16},offsets[]={0x28,0x80,0x40,0x10,0xC0};
        for(unsigned j=0;j<5;++j){auto& r=x.records[2+j];r.address=target+offsets[j];r.kind=kinds[j];r.size=sizes[j];r.parent=1;}
        auto& y=b->transforms[0];y=x;float newer=2.0f;
        std::memcpy(y.records[3].bytes.data(),&newer,4);std::memcpy(y.records[4].bytes.data(),&newer,4);
        c->transforms[3]=y;
        a->rings[0].valid=b->rings[0].valid=c->restored_rings.valid=true;
        for(auto& h:b->rings[0].headers)h.index=2;
        a->blends[0].valid=b->blends[0].valid=c->blends[3].valid=true;
        a->cameras[0].valid=b->cameras[0].valid=c->cameras[4].valid=true;
        for(unsigned side=0;side<2;++side){auto owner=base+0x710000+side*0x10000;
            a->blends[0].owners[side]=b->blends[0].owners[side]=c->blends[3].owners[side]=owner;
            std::memset(reinterpret_cast<void*>(owner+0x22A8),0,8);}
        // Corrupt all layouts together so topology equality alone cannot hide bad ownership.
        for(auto* t:{&x,&y,&c->transforms[3]}){
            if(scenario==1)t->records[1].side=1;
            if(scenario==2)t->records[1].parent=3;
            if(scenario==3)t->records[5].parent=0;
            if(scenario==4)t->records[6].address+=8;
            if(scenario==5)t->records[1].bytes[0]^=std::byte{1};
            if(scenario==6)t->fixed_constraint_slots=false;
        }
        for(unsigned i=0;i<c->transforms[3].count;++i){const auto& r=c->transforms[3].records[i];std::memcpy(reinterpret_cast<void*>(r.address),r.bytes.data(),r.size);}
        if(scenario==7)*reinterpret_cast<unsigned char*>(target)^=1;
        if(scenario==8){float third=3.0f;std::memcpy(c->transforms[3].records[4].bytes.data(),&third,4);std::memcpy(reinterpret_cast<void*>(target+0x40),&third,4);}
        if(scenario==9)*reinterpret_cast<unsigned char*>(target+0x28)^=1;
        if(scenario==10){unsigned nan=0x7FC00000;std::memcpy(x.records[3].bytes.data(),&nan,4);}
        if(scenario==11){ // Mixed saved/next words are valid; exact saved endpoint required.
            float other=4.0f;std::memcpy(y.records[3].bytes.data()+4,&other,4);
        }
        std::array<std::byte,0xD0> before{};std::memcpy(before.data(),reinterpret_cast<void*>(target),before.size());
        auto correction=std::make_unique<sc6::research::ImmediateIdleCorrection>();
        const bool ok=correction->apply(*a,*b,*c,base);
        if(ok!=(scenario==0 || scenario==11))std::fprintf(stderr,"owned completion scenario=%u section=%u line=%u\n",scenario,correction->section,correction->failure_line);
        require(ok==(scenario==0 || scenario==11));
        if(ok){require(correction->verified);for(unsigned i:{3u,4u})require(!std::memcmp(reinterpret_cast<void*>(x.records[i].address),x.records[i].bytes.data(),64));}
        else require(!std::memcmp(before.data(),reinterpret_cast<void*>(target),before.size()));
    }
    // Audited 3E885E0 camera limits: exact restoration, transactional rejection.
    for(unsigned scenario=0;scenario<13;++scenario){
        auto a=std::make_unique<P>(),b=std::make_unique<P>(),c=std::make_unique<P>();
        a->transforms[0].valid=b->transforms[0].valid=c->transforms[3].valid=true;
        a->rings[0].valid=b->rings[0].valid=c->restored_rings.valid=true;
        for(auto& h:b->rings[0].headers)h.index=2;
        a->blends[0].valid=b->blends[0].valid=c->blends[3].valid=true;
        for(unsigned side=0;side<2;++side){auto owner=base+0x710000+side*0x10000;
            a->blends[0].owners[side]=b->blends[0].owners[side]=c->blends[3].owners[side]=owner;
            std::memset(reinterpret_cast<void*>(owner+0x22A8),0,8);
        }
        auto& ca=a->cameras[0];ca.valid=true;ca.count=1;
        auto& r=ca.records[0];r.owner=base+0x704000;
        auto vt=base+0x3E885E0;std::memcpy(r.bytes.data(),&vt,8);
        constexpr unsigned offsets[]={0x188,0x18C,0x1B8,0x1BC};
        constexpr unsigned defaults[]={0xBE7FFFBC,0x3E7FFD60,0x3F000000,0x437A0000};
        for(unsigned j=0;j<4;++j){float value=float(j+1);std::memcpy(r.bytes.data()+offsets[j],&value,4);}
        b->cameras[0]=ca;c->cameras[4]=ca;
        auto& live=c->cameras[4].records[0];
        for(unsigned j=0;j<4;++j)std::memcpy(live.bytes.data()+offsets[j],&defaults[j],4);
        if(scenario>=1 && scenario<=4)live.bytes[offsets[scenario-1]]^=std::byte{1};
        if(scenario==5)live.bytes[0x190]^=std::byte{1};
        if(scenario==6)b->cameras[0].records[0].owner+=8;
        if(scenario==7)live.bytes[0]^=std::byte{1};
        if(scenario==8){unsigned nan=0x7FC00000;std::memcpy(r.bytes.data()+0x188,&nan,4);}
        if(scenario==9)r.smoothing_valid=true;
        if(scenario==10)b->cameras[0].records[0].bytes[0]^=std::byte{1};
        if(scenario==12){unsigned negativeZero=0x80000000;std::memcpy(r.bytes.data()+0x188,&negativeZero,4);}
        std::memcpy(reinterpret_cast<void*>(r.owner),live.bytes.data(),456);
        if(scenario==11)*reinterpret_cast<unsigned char*>(r.owner+0x18C)^=1;
        std::array<std::byte,456> before{};std::memcpy(before.data(),reinterpret_cast<void*>(r.owner),456);
        auto correction=std::make_unique<sc6::research::ImmediateIdleCorrection>();
        bool ok=correction->apply(*a,*b,*c,base);
        if(ok!=(scenario==0 || scenario==12))std::fprintf(stderr,"camera limits scenario=%u section=%u line=%u\n",scenario,correction->section,correction->failure_line);
        require(ok==(scenario==0 || scenario==12));
        require(!std::memcmp(reinterpret_cast<void*>(r.owner),ok?r.bytes.data():before.data(),456));
        require(ok?correction->count==4:!correction->verified);
    }
    {
        sc6::research::IdleBlendSettle settle;
        sc6::research::IdleBlendSettle::Bytes bytes{};std::array<uintptr_t,2> owners{0x10000,0x20000};
        for(unsigned i=0;i<9;++i)require(settle.observe(true,owners,bytes) && !settle.ready);
        bytes[0][200]=std::byte{1};require(settle.observe(true,owners,bytes) && settle.stable==1);
        require(settle.observe(false,owners,bytes) && settle.stable==0);
        for(unsigned i=0;i<9;++i)require(settle.observe(true,owners,bytes) && !settle.ready);
        owners[1]+=8;require(settle.observe(true,owners,bytes) && settle.stable==1);
        for(unsigned i=0;i<9;++i)require(settle.observe(true,owners,bytes));
        require(settle.ready && settle.stable==10);
        sc6::research::IdleBlendSettle timeout;
        for(unsigned i=0;i<180;++i)require(timeout.observe(false,owners,bytes));
        require(!timeout.observe(false,owners,bytes) && timeout.fault && !timeout.ready);
        require(!timeout.observe(true,owners,bytes));
        sc6::research::IdleBlendSettle unreadable;
        require(!unreadable.step(0) && unreadable.fault);
    }
    {
        auto batch=std::make_unique<sc6::research::RepeatedMovement>();
        for(unsigned cycle=0;cycle<3;++cycle){
            auto* current=batch->cycles[cycle].get();require(current && current->window==3);
            current->done=current->corrected=true;current->compared=3;
            require(batch->step(0,0,true) && batch->completed==cycle+1 && !batch->next_argument);
            if(cycle<2){require(batch->cooldown==60 && !batch->done);
                for(unsigned tick=0;tick<60;++tick)require(batch->step(0,0,true));
                require(batch->cycles[cycle+1]->captured==0 && batch->completed==cycle+1);
            }
        }
        require(batch->done && batch->step(0,0,true) && batch->completed==3);
        auto failed_batch=std::make_unique<sc6::research::RepeatedMovement>();
        failed_batch->cycles[0]->fault=true;
        require(!failed_batch->step(0,0,true) && failed_batch->fault && !failed_batch->cycles[1]);
        require(!failed_batch->step(0,0,true) && failed_batch->completed==0);
        auto incomplete_batch=std::make_unique<sc6::research::RepeatedMovement>();
        incomplete_batch->cycles[0]->done=true;
        require(!incomplete_batch->step(0,0,true) && incomplete_batch->completed==0);
    }
    VirtualFree(reinterpret_cast<void*>(base),0,MEM_RELEASE);
    std::puts("PASS: full probe mock round-trip, no load on preflight/idle rejection, read bounds/shape, forbidden operations, incomplete read, state mismatch, postflight rejection, bookkeeping, once-only, invalid base; preservation restores scalars/RNG, retains rebuilt pointers, rejects unexpected fields/RNG before writes.");
}








