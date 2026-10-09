#include "../src/FieldUpdateTrace.hpp"
#include <intrin.h>
#include <cstdlib>
#include <iostream>
extern "C" void SC6FieldBridge();
extern "C" {void* SC6FieldOriginal=nullptr;}
extern "C" uintptr_t SC6AfterHarness(uintptr_t);
extern "C" void SC6AfterContinuation();
extern "C" {void* SC6AfterOriginal=reinterpret_cast<void*>(&SC6AfterContinuation);}
static uintptr_t after_player{};
extern "C" void SC6TraceFieldAfter(uintptr_t player) noexcept {after_player=player;}
static uintptr_t seen[4]{},return_address{};static unsigned calls{};
extern "C" void SC6TraceFieldUpdate(const uintptr_t* regs,uintptr_t caller) noexcept {
    for(unsigned i=0;i<4;++i)seen[i]=regs[i];return_address=caller;
}
static void require(bool ok){if(!ok)std::abort();}
static double target(uintptr_t a,double b,uintptr_t c,double d,uintptr_t e,double f){
    ++calls;require(a==17 && c==23 && e==31 && b==1.25 && d==2.5 && f==4.75);
    require(seen[0]==17 && seen[2]==23 && return_address==reinterpret_cast<uintptr_t>(_ReturnAddress()));
    return 62.5;
}
int main(){
    require(SC6AfterHarness(0xABCDEF)==0x12345678 && after_player==0xABCDEF);
    SC6FieldOriginal=reinterpret_cast<void*>(&target);
    auto bridge=reinterpret_cast<double(*)(uintptr_t,double,uintptr_t,double,uintptr_t,double)>(&SC6FieldBridge);
    require(bridge(17,1.25,23,2.5,31,4.75)==62.5 && calls==1);
    auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x4800000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));require(memory!=nullptr);
    auto base=reinterpret_cast<uintptr_t>(memory);auto player=base+0x10000;
    *reinterpret_cast<short*>(player+0x1B3C)=7;*reinterpret_cast<unsigned*>(base+0x470D0C4)=42;
    sc6::research::FieldUpdateTrace trace;uintptr_t regs[]={player,2,3,1};
    trace.record(regs,19);require(trace.count==0);trace.start(base);trace.phase=3;
    SetLastError(123);trace.record(regs,19);require(GetLastError()==123);
    require(trace.count==1 && trace.events[0].before==7 && trace.events[0].delta==3 && trace.events[0].counter==42 && trace.events[0].phase==3);
    regs[1]=0;trace.record(regs,19);require(trace.count==1);regs[1]=2;
    for(unsigned i=0;i<300;++i)trace.record(regs,19);
    require(trace.count==256 && trace.dropped==45);trace.stop();trace.record(regs,19);require(trace.dropped==45);
    trace.start(base);uintptr_t reset[]={player,base+0x20000,0,0};
    *reinterpret_cast<short*>(reset[1]+0xC)=0;
    trace.record_reset(reset,29);require(trace.count==1 && trace.events[0].assignment==1 && trace.events[0].delta==0 && trace.events[0].before==7);
    *reinterpret_cast<short*>(reset[1]+0xC)=-1;trace.record_reset(reset,29);require(trace.count==1);
    reset[1]=1;trace.record_reset(reset,29);require(trace.count==1);trace.stop();
    trace.start(base);
    const auto outer=base+0x30000,list=outer+0xA30,table=base+0x40000;
    *reinterpret_cast<int*>(list+0x50)=1;
    *reinterpret_cast<unsigned*>(list+0x30)=1;
    *reinterpret_cast<uintptr_t*>(list)=table;
    *reinterpret_cast<uintptr_t*>(table+0x68)=1234;
    trace.inspect_callbacks(outer);
    require(trace.callbacks_valid && trace.callback_count==1 && trace.callbacks[0].object==list && trace.callbacks[0].target==1234);
    trace.start(base);
    *reinterpret_cast<uintptr_t*>(list+8)=(uintptr_t(7)<<32)|1;
    *reinterpret_cast<uintptr_t*>(list+16)=base+0x8954A4;
    *reinterpret_cast<int*>(base+0x42A115C)=2;
    const auto items=base+0x50000,resolved=base+0x60000,virtual_table=base+0x70000;
    *reinterpret_cast<uintptr_t*>(base+0x42A1150)=items;
    *reinterpret_cast<uintptr_t*>(items+24)=resolved;
    *reinterpret_cast<unsigned*>(items+24+16)=7;
    *reinterpret_cast<uintptr_t*>(resolved)=virtual_table;
    *reinterpret_cast<uintptr_t*>(virtual_table+0x5F8)=5678;
    trace.inspect_callbacks(outer);
    require(trace.callbacks_valid && trace.callbacks[0].resolved_object==resolved && trace.callbacks[0].resolved_target==5678);
    trace.start(base);*reinterpret_cast<unsigned*>(items+24+16)=8;trace.inspect_callbacks(outer);
    require(trace.callbacks_valid && !trace.callbacks[0].resolved_object);
    trace.start(base);uintptr_t setter_regs[]={resolved,240,0,0};
    *reinterpret_cast<uintptr_t*>(virtual_table+0x110)=9012;
    SetLastError(42);trace.record_setter(setter_regs,0x4283B9);trace.record_setter(setter_regs,0x4283B9);
    require(GetLastError()==42 && trace.setter_count==1 && trace.setters[0].target==9012 && trace.setters[0].value==240 && !trace.setter_fault);
    setter_regs[0]=1;trace.record_setter(setter_regs,0x428334);require(trace.setter_fault);
    trace.start(base);require(trace.setter_count==0 && !trace.setter_fault);
    trace.start(base);*reinterpret_cast<int*>(list+0x50)=65;trace.inspect_callbacks(outer);
    require(!trace.callbacks_valid && trace.callback_count==0);
    trace.start(base);trace.inspect_callbacks(1);require(!trace.callbacks_valid);trace.stop();
    *reinterpret_cast<uintptr_t*>(base+0x470DE90+8)=player;
    *reinterpret_cast<int*>(resolved+16)=1;
    sc6::research::TrainingFieldReset reset_probe;
    reset_probe.observe(base,resolved,0);
    require(sc6::research::TrainingFieldReset::apply(reset_probe.latest,base,42));
    require(*reinterpret_cast<short*>(player+0x1B3C)==0);
    *reinterpret_cast<short*>(player+0x1B3C)=7;
    require(!sc6::research::TrainingFieldReset::apply(reset_probe.latest,base,43));
    *reinterpret_cast<unsigned*>(player+0x43E2C)=1;
    require(!sc6::research::TrainingFieldReset::apply(reset_probe.latest,base,42));
    reset_probe.observe(base,resolved,240);
    require(sc6::research::TrainingFieldReset::apply(reset_probe.latest,base,42));
    require(*reinterpret_cast<short*>(player+0x1B3C)==7);
    *reinterpret_cast<unsigned*>(player+0x43E2C)=0;
    reset_probe.observe(base,resolved,240);
    require(sc6::research::TrainingFieldReset::apply(reset_probe.latest,base,42));
    require(*reinterpret_cast<short*>(player+0x1B3C)==240);
    *reinterpret_cast<short*>(player+0x1B3C)=7;
    require(*reinterpret_cast<short*>(player+0x1B3C)==7);VirtualFree(memory,0,MEM_RELEASE);
    std::cout<<"PASS: trace bridge preserves arguments, return address and result; filtering, bounded events, stop, LastError and no field writes\n";
}
