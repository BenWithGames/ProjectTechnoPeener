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
#include <cstdio>
#include <iostream>
#include <cstdlib>
using T=sc6::research::FullRestoreStream;
using P=sc6::research::FullStateProbe;
using S=sc6::research::SaveStream;
uintptr_t base;int mode;unsigned saves,loads,checks;
void require(bool v){if(!v){std::fprintf(stderr,"Full restore test failed mode=%d\n",mode);std::exit(1);}}
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
int main(){
    base=reinterpret_cast<uintptr_t>(VirtualAlloc(nullptr,0x4900000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));require(base!=0);
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
        cb->records[0].targets[0]^=std::byte{1};
        require(!ca->restore_smoothing(*cb,camera,base) && *reinterpret_cast<unsigned*>(owner+0x154)==0);
        cb->records[0].targets[0]^=std::byte{1};
        *reinterpret_cast<float*>(owner+0x1D8)=0.5f;
        require(!ca->restore_smoothing(*cb,camera,base) && *reinterpret_cast<unsigned*>(owner+0x154)==0);
        *reinterpret_cast<float*>(owner+0x1D8)=0.375f;
        cb->records[0].response[0]^=std::byte{1};
        require(!ca->restore_smoothing(*cb,camera,base) && *reinterpret_cast<unsigned*>(owner+0x154)==0);
        cb->records[0].response[0]^=std::byte{1};
        camera.records[0].response[4]^=std::byte{1};
        require(!ca->restore_smoothing(*cb,camera,base) && *reinterpret_cast<unsigned*>(owner+0x154)==0);
        camera.records[0].response[4]^=std::byte{1};
        require(ca->restore_smoothing(*cb,camera,base));
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
            auto original_a=ca->records[0].targets,original_b=cb->records[0].targets;
            float a=0.6f,b=a+0.1f;
            for(unsigned off:{0x68u,0x6Cu}){
                std::memcpy(ca->records[0].targets.data()+off,&a,4);
                std::memcpy(cb->records[0].targets.data()+off,&b,4);}
            require(ca->adjacent_combat_ready(*cb,base,true));
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
        require(!ca->restore_smoothing(*cb,camera,base,false,true));
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
        if(mode==9 || mode==13 || mode==17 || mode==26 || mode==32 || mode==38){
            require(!p->fault && p->preserve_applied && p->preserve_verified && saves==2);
            require(p->measurements[2].rng==P::advance_rng(123) && p->measurements[4].rng==123 && p->measurements[3].rng==123);
            for(unsigned side=0;side<2;++side){auto player=P::ptr(base+0x470DE90+side*8);
                require(P::ptr(player+0x43DF0)==player);
                if(mode==17 || mode==26 || mode==32){
                    require(*reinterpret_cast<unsigned*>(player+0x97220)==(side==0?0u:1u));
                    require(*reinterpret_cast<unsigned*>(player+0x97204)==1);
                }
                if(mode==38){
                    require(*reinterpret_cast<unsigned*>(player+0x97204)==(side==0?1u:0u));
                    require(*reinterpret_cast<unsigned*>(player+0x97220)==0);
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
        require(!wind_a->restore_wind(*wind_b,*t,base));
        require(*reinterpret_cast<unsigned*>(global_nodes+0x70)==0x3f000000);
        *reinterpret_cast<unsigned*>(base+0x470E2B0)=456;
        require(wind_a->restore_wind(*wind_b,*t,base));
        require(*reinterpret_cast<unsigned*>(global_nodes+0x70)==0 && *reinterpret_cast<unsigned*>(base+0x485EB30)==0 && *reinterpret_cast<unsigned*>(base+0x470E2B0)==0);
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
                auto restore=std::make_unique<P>();
                require(source->prepare_adjacent(*current,*restore) && !restore->fault && loads==0);
                require(source->data->saved.reads==0 && restore->saved.reads==0);
                require(!source->prepare_adjacent(*current,*restore));
                for(unsigned reject=0;reject<5;++reject){
                    auto rejected=std::make_unique<P>();
                    if(reject==0)++current->data->measurements[0].rng;
                    if(reject==1)++current->data->before_keys[0];
                    if(reject==2)current->data->delayed_inputs[0]=std::byte{1};
                    if(reject==3)current->module+=8;
                    if(reject==4)current->mode=Boundary::Mode::reversal;
                    Boundary::Rejection reason{};
                    require(!source->prepare_adjacent(*current,*rejected,&reason) && !rejected->restore_invoked && loads==0);
                    constexpr Boundary::Rejection expected[]={Boundary::Rejection::rng,Boundary::Rejection::frame,Boundary::Rejection::inputs,Boundary::Rejection::module,Boundary::Rejection::mode};
                    require(reason==expected[reject]);
                    if(reject==0)--current->data->measurements[0].rng;
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
                    b.data->blends[0].bytes[0][60]^=std::byte{1};
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
        if(test==1)*reinterpret_cast<unsigned*>(argument+0x100)=1;
        q->delayed_step(base,argument,delayed_save,delayed_load,check);
        if(test==1){require(q->fault && loads==0);require(q->input_check_phase==1 && q->input_checkpoints[1].valid && q->input_checkpoints[1].changed_mask==1 && q->input_checkpoints[1].bytes[0]==std::byte{1} && q->input_checkpoints[1].sources[0]==argument+0x100);*reinterpret_cast<unsigned*>(argument+0x100)=0;continue;}
        if(test>=3){require(q->fault && !q->outer_correction_applied && !q->preserve_applied && loads==1);continue;}
        require(q->outer_correction_verified && q->outer_correction_applied==(test==2));
        require(q->delayed_loaded && !q->fault && loads==1 && q->preserve_verified);
        require(q->restored_valid && q->restored_save_unchanged && q->restored_ring_equal && q->restored_byte_differences==0 && q->restored.writes==q->saved.writes);
        require(q->delayed_keys_captured && q->delayed_loaded_keys[0]==(test==2?101u:100u) && q->delayed_loaded_players[0]==P::ptr(base+0x470DE90));
        require(*reinterpret_cast<unsigned*>(base+0x470D0C4)==100);
        require(q->rings[2].restore(base,*scratch));
        *reinterpret_cast<unsigned*>(base+0x470D0C4)=101;
        q->delayed_step(base,argument,delayed_save,delayed_load,check);
        require(q->replay_done && q->replay_keys_equal && q->replay_ring_equal && !q->replay_byte_differences && loads==1);
        require(q->blends[0].valid && q->blends[4].valid && q->blends[2].bytes==q->blends[3].bytes);
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
            require(P::combat_transition(before,after,side,false)==(side==1));
            require(P::combat_transition(before,after,side,false,false,true));
            require(!P::combat_transition(before,after,side,true,false,true));
            require(!P::combat_transition(before,after,side,false,true,true));
            put(before,0x38,2);
            require(!P::combat_transition(before,after,side,false,false,true));
            put(before,0x38,1);put(after,0x38,1);
            require(!P::combat_transition(before,after,side,false,false,true));
            put(after,0x38,0);put(before,0x1C,2);
            require(!P::combat_transition(before,after,side,false,false,true));
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
    for(unsigned failure=0;failure<17;++failure){
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
        if(failure>=10){ // Idle drift admits only the three captured float fields.
            for(unsigned off:{0x24u,0x64u,0x114u})
                std::memcpy(b->cameras[0].records[0].bytes.data()+off,&newer,4);
            if(failure==11)b->cameras[0].records[0].bytes[0x154]^=std::byte{1};
            if(failure==12)b->cameras[0].records[0].bytes[0x30]^=std::byte{1};
            if(failure==15)b->cameras[0].records[0].bytes[0x60]^=std::byte{1};
            if(failure==16)b->cameras[0].records[0].bytes[0x110]^=std::byte{1};
            if(failure==14)b->cameras[0].records[0].bytes[0x20]^=std::byte{1};
            if(failure==13){unsigned nan=0x7FC00000;std::memcpy(b->cameras[0].records[0].bytes.data()+0x64,&nan,4);}
        }
        auto correction=std::make_unique<sc6::research::ImmediateIdleCorrection>();
        bool ok=correction->apply(*a,*b,*c,base);
        require(ok==(failure==0 || failure==6 || failure==10) && correction->verified==ok);
        require(*reinterpret_cast<float*>(root+0x1F0)==(ok?0.0f:newer));
        require(*reinterpret_cast<float*>(group)==(ok?0.0f:newer));
        require(*reinterpret_cast<float*>(camera+0x154)==(ok?newer:0.0f));
        if(ok)require(*reinterpret_cast<float*>(target+0x80)==0.0f && correction->count==6);
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






