#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "../src/SharedRestoreProbe.hpp"
#include <memory>
#include <cstdio>
#include <cstdlib>
using P=sc6::research::SharedRestoreProbe;
using S=sc6::research::SaveStream;
uintptr_t base; unsigned calls; int mode;
void check(bool ok){if(!ok){std::fprintf(stderr,"shared probe test failed, mode=%d\n",mode);std::exit(1);}}
void* writer(void*,S* s,P::Ranges* ranges){
    for(unsigned i=0;i<5;++i)s->slots->write(s,reinterpret_cast<void*>(base+P::offsets[i]),P::sizes[i]);
    for(unsigned i=0;i<4;++i){
        std::uint64_t record[2]={i%3,0};
        auto pointer=*reinterpret_cast<uintptr_t*>(base+0x4846470+i*8);
        record[1]=pointer-ranges->begin[i%3].base;
        if(mode==1 && i==0)record[0]=3;
        s->slots->write(s,record,16);
    }
    if(mode==2)s->slots->write(s,&base,1);
    return nullptr;
}
void* reader(void*,S* s,P::Ranges* ranges){
    ++calls;
    // Prove that the test actually restores changed destinations.
    std::memset(reinterpret_cast<void*>(base+P::offsets[0]),0xCC,P::sizes[0]);
    if(mode==3){s->slots->read(s,&base,UINT64_MAX);return nullptr;}
    for(unsigned i=0;i<5;++i)s->slots->read(s,reinterpret_cast<void*>(base+P::offsets[i]),P::sizes[i]);
    for(unsigned i=0;i<4;++i){
        std::uint64_t record[2]{};s->slots->read(s,record,16);
        *reinterpret_cast<uintptr_t*>(base+0x4846470+i*8)=ranges->begin[record[0]].base+record[1];
    }
    if(mode==4)*reinterpret_cast<unsigned char*>(base+P::offsets[0])^=1;
    return nullptr;
}
int main(){
    base=reinterpret_cast<uintptr_t>(VirtualAlloc(nullptr,0x4900000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));check(base!=0);
    for(mode=0;mode<5;++mode){
        *reinterpret_cast<uintptr_t*>(base+0x470DE90)=base+0x1000;
        *reinterpret_cast<uintptr_t*>(base+0x470DE98)=base+0x100000;
        *reinterpret_cast<uintptr_t*>(base+0x470F1A0)=base+0x1000;
        *reinterpret_cast<uintptr_t*>(base+0x470F1A8)=base+0x100000;
        for(unsigned i=0;i<5;++i)std::memset(reinterpret_cast<void*>(base+P::offsets[i]),0x5A,P::sizes[i]);
        uintptr_t pointers[]={0,base+0x1020,base+0x100040,0x12345678};
        std::memcpy(reinterpret_cast<void*>(base+0x4846470),pointers,sizeof(pointers));
        auto p=std::make_unique<P>();calls=0;p->run(base,writer,reader);
        if(mode==0)check(p->saved && p->restored && p->equal && !p->fault && p->reads==9 && p->stream.read_position==4384 && calls==1);
        else check(p->fault);
        if(mode==1 || mode==2)check(!p->restore_invoked && calls==0);
        if(mode==2 || mode==3)check(p->exception==P::rejection);
        if(mode==4)check(p->restored && !p->equal);
        auto old=calls;p->run(base,writer,reader);check(calls==old);
    }
    auto p=std::make_unique<P>();p->run(0,writer,reader);check(p->fault && !p->restore_invoked);
    VirtualFree(reinterpret_cast<void*>(base),0,MEM_RELEASE);
    std::puts("Shared restore probe tests passed: round-trip, relocation rejection, transfer bounds, trapped failure, mismatch, once-only, invalid base.");
}
