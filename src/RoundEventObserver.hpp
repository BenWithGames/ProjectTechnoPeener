#pragma once
#include <Windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <cstring>
#include <array>
#include <fstream>
#include <filesystem>
#include "RoundBoundaryIdentity.hpp"
namespace sc6::round_event {
struct Snapshot {uint32_t round{},counter{};uint64_t generation{};};
inline SRWLOCK lock=SRWLOCK_INIT;
inline RoundBoundaryIdentity identity;
inline uint32_t event_counter{};
inline uintptr_t module{};
inline bool installed=false,upstream_installed=false;
inline bool direct_identity=false;
struct UpstreamEvent {uint64_t qpc{},caller_rva{};uint32_t counter{},thread{};unsigned code{};bool readable{},in_module{};};
inline std::array<UpstreamEvent,256> upstream_events{};
inline unsigned upstream_count{},upstream_dropped{};
using UpstreamRoutine=void(*)(void*,void*);
inline UpstreamRoutine upstream_original=nullptr;
inline void observe_upstream(void* event,uintptr_t caller) noexcept {
    const auto error=GetLastError();LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);
    UpstreamEvent row{};row.qpc=static_cast<uint64_t>(stamp.QuadPart);row.thread=GetCurrentThreadId();
    row.in_module=caller>=module && caller-module<0x4900000;row.caller_rva=row.in_module?caller-module:0;
    __try {row.code=*static_cast<const unsigned char*>(event);row.counter=*reinterpret_cast<const uint32_t*>(module+0x470D0C4);row.readable=true;}
    __except(EXCEPTION_EXECUTE_HANDLER){}
    AcquireSRWLockExclusive(&lock);
    if(upstream_count<upstream_events.size())upstream_events[upstream_count++]=row;else ++upstream_dropped;
    ReleaseSRWLockExclusive(&lock);SetLastError(error);
}
// Scoped traces observe existing calls only; never invoke game getters twice.
struct GateTrace {uintptr_t context{},controller{},state{},state_vtable{},round_getter{};unsigned path_status{};uint16_t raw_round_before{},raw_round_after{};bool raw_before_readable{},raw_after_readable{};uint64_t qpc{};unsigned code{};bool readable{};unsigned lookups{},nonnull{};bool round_called{},completed{};int round{};};
inline std::array<GateTrace,256> gates{};
inline unsigned gate_count{},gate_dropped{};
inline void sample_round_path(void* context,GateTrace& trace) noexcept {
    // Mirrors only pointer reads established in 3BDF40. No virtual calls,
    // reference-count changes, or assumptions about object class/lifetime.
    trace.context=reinterpret_cast<uintptr_t>(context);
    __try {
        if(!context)return;
        trace.controller=*reinterpret_cast<const uintptr_t*>(trace.context+0x98);
        trace.path_status=1;if(!trace.controller)return;
        trace.state=*reinterpret_cast<const uintptr_t*>(trace.controller+0x1450);
        trace.path_status=2;if(!trace.state)return;
        trace.state_vtable=*reinterpret_cast<const uintptr_t*>(trace.state);
        trace.path_status=3;if(!trace.state_vtable)return;
        const auto getter=*reinterpret_cast<const uintptr_t*>(trace.state_vtable+0x100);
        if(getter>=module && getter-module<0x4900000){trace.round_getter=getter-module;trace.path_status=4;}
    } __except(EXCEPTION_EXECUTE_HANDLER){trace.path_status=5;}
}

// Pinned 2D7390: movzx eax, word ptr [RIP+456F009], next RIP 2D7397.
// This is the raw value before the optional adjustment in 3BDF40.
inline void sample_raw_round(uintptr_t base,uint16_t& value,bool& readable) noexcept {
    value=0;readable=false;
    __try {value=*reinterpret_cast<const uint16_t*>(base+0x48463A0);readable=true;}
    __except(EXCEPTION_EXECUTE_HANDLER){}
}
inline bool lookup_installed{},accessor_installed{};
inline thread_local GateTrace* active_gate=nullptr;
using LookupRoutine=void*(*)(void*,void*);
using AccessorRoutine=int(*)(void*);
inline LookupRoutine lookup_original=nullptr;
inline AccessorRoutine accessor_original=nullptr;
inline void record_lookup(uintptr_t caller,void* result){
    if(active_gate && (caller==module+0x3C45DB || caller==module+0x3C4602)){
        ++active_gate->lookups;if(result)++active_gate->nonnull;
    }
}
inline void record_accessor(uintptr_t caller,int result){
    if(active_gate && caller==module+0x3C466A){active_gate->round_called=true;active_gate->round=result;}
}
__declspec(noinline) inline void* lookup_bridge(void* context,void* world){
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    auto result=lookup_original(context,world);const auto error=GetLastError();
    record_lookup(caller,result);SetLastError(error);return result;
}
__declspec(noinline) inline int accessor_bridge(void* context){
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    const auto result=accessor_original(context);const auto error=GetLastError();
    record_accessor(caller,result);SetLastError(error);return result;
}
__declspec(noinline) inline void upstream_bridge(void* context,void* event){
    observe_upstream(event,reinterpret_cast<uintptr_t>(_ReturnAddress()));
    const auto entry_caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    GateTrace trace{};const auto entry_error=GetLastError();LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);trace.qpc=stamp.QuadPart;
    __try {trace.code=*static_cast<unsigned char*>(event);trace.readable=true;} __except(EXCEPTION_EXECUTE_HANDLER){}
    sample_round_path(context,trace);
    sample_raw_round(module,trace.raw_round_before,trace.raw_before_readable);
    auto previous=active_gate;active_gate=&trace;SetLastError(entry_error);
    // Restore TLS even if the original listener raises an exception.
    __try {upstream_original(context,event);trace.completed=true;}
    __finally {
        const auto error=GetLastError();active_gate=previous;
        sample_raw_round(module,trace.raw_round_after,trace.raw_after_readable);
        uint32_t counter{};bool counter_readable=false;
        __try {counter=*reinterpret_cast<const uint32_t*>(module+0x470D0C4);counter_readable=true;} __except(EXCEPTION_EXECUTE_HANDLER){}
        AcquireSRWLockExclusive(&lock);
        if(direct_identity && entry_caller==module+0x3EF099 && trace.readable){
            if(trace.code==6 || trace.code==9)identity.known=false;
            if(trace.code==1){
                const bool trusted=trace.completed && counter_readable && trace.path_status==4 && trace.round_getter==0x3BDDD0 &&
                    trace.raw_before_readable && trace.raw_after_readable && trace.raw_round_before==trace.raw_round_after;
                if(identity.on_upstream_start(trace.raw_round_before,counter,trusted))event_counter=counter;
            }
        }
        if(gate_count<gates.size())gates[gate_count++]=trace;else ++gate_dropped;
        ReleaseSRWLockExclusive(&lock);SetLastError(error);
    }
}
struct Event {uint64_t qpc{},caller_rva{};int round{};uint32_t counter{};bool readable{},accepted{},in_module{};};
struct Diagnostics {std::array<Event,256> events{};unsigned count{},dropped{},accepted{},foreign{};bool installed{},failed{};unsigned upstream_count{},upstream_dropped{};bool upstream_installed{};};
inline Diagnostics diagnostic;
inline Diagnostics diagnostics(){
    AcquireSRWLockShared(&lock);auto result=diagnostic;
    result.installed=installed;result.failed=identity.failed;
    result.upstream_count=upstream_count;result.upstream_dropped=upstream_dropped;result.upstream_installed=upstream_installed;
    ReleaseSRWLockShared(&lock);return result;
}
inline void export_events(const std::filesystem::path& path){
    const auto d=diagnostics();std::ofstream out(path);
    out<<"event,qpc,caller_rva,in_module,round,outer,readable,accepted\n";
    for(unsigned i=0;i<d.count;++i){const auto& e=d.events[i];
        out<<i<<','<<e.qpc<<','<<e.caller_rva<<','<<e.in_module<<','<<e.round<<','<<e.counter<<','<<e.readable<<','<<e.accepted<<'\n';
    }
    out.flush();if(!out)throw std::runtime_error("round_event_export_failed");
    std::array<UpstreamEvent,256> events{};unsigned count{};
    AcquireSRWLockShared(&lock);events=upstream_events;count=upstream_count;ReleaseSRWLockShared(&lock);
    std::ofstream upstream(path.parent_path()/(path.stem().string()+"-upstream.csv"));
    upstream<<"event,qpc,caller_rva,in_module,thread,event_code,outer,readable\n";
    for(unsigned i=0;i<count;++i){const auto& e=events[i];upstream<<i<<','<<e.qpc<<','<<e.caller_rva<<','<<e.in_module<<','<<e.thread<<','<<e.code<<','<<e.counter<<','<<e.readable<<'\n';}
    upstream.flush();if(!upstream)throw std::runtime_error("upstream_event_export_failed");
    std::array<GateTrace,256> traces{};unsigned traces_count{},dropped{};
    AcquireSRWLockShared(&lock);traces=gates;traces_count=gate_count;dropped=gate_dropped;ReleaseSRWLockShared(&lock);
    std::ofstream gate_out(path.parent_path()/(path.stem().string()+"-gates.csv"));
    gate_out<<"completion,qpc,event_code,readable,lookup_hook,accessor_hook,lookups,nonnull,round_called,round,completed,dropped,context,controller,state,state_vtable,round_getter_rva,path_status,raw_round_before,raw_before_readable,raw_round_after,raw_after_readable\n";
    for(unsigned i=0;i<traces_count;++i){const auto& t=traces[i];gate_out<<i<<','<<t.qpc<<','<<t.code<<','<<t.readable<<','<<lookup_installed<<','<<accessor_installed<<','<<t.lookups<<','<<t.nonnull<<','<<t.round_called<<','<<t.round<<','<<t.completed<<','<<dropped<<','<<t.context<<','<<t.controller<<','<<t.state<<','<<t.state_vtable<<','<<t.round_getter<<','<<t.path_status<<','<<t.raw_round_before<<','<<t.raw_before_readable<<','<<t.raw_round_after<<','<<t.raw_after_readable<<'\n';}
    gate_out.flush();if(!gate_out)throw std::runtime_error("round_gate_export_failed");
}
using Routine=void(*)(void*,int);
inline Routine original=nullptr;
inline Snapshot snapshot(){
    AcquireSRWLockShared(&lock);Snapshot result{};
    if(identity.known && !identity.failed)result={identity.round,event_counter,identity.boundary};
    ReleaseSRWLockShared(&lock);return result;
}
inline void observe(int round,uintptr_t caller) noexcept {
    const auto error=GetLastError();uint32_t counter{};bool valid=false;
    LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);
    __try{counter=*reinterpret_cast<const uint32_t*>(module+0x470D0C4);valid=true;}
    __except(EXCEPTION_EXECUTE_HANDLER){}
    AcquireSRWLockExclusive(&lock);
    bool accepted=false;
    if(caller==module+0x3C467E){
        if(valid && round>0 && (direct_identity || identity.on_round_started(round))){if(!direct_identity)event_counter=counter;accepted=true;++diagnostic.accepted;}
        else {identity.known=false;identity.failed=true;}
    }else ++diagnostic.foreign;
    const bool in_module=caller>=module && caller-module<0x4900000;
    if(diagnostic.count<diagnostic.events.size())
        diagnostic.events[diagnostic.count++]={static_cast<uint64_t>(stamp.QuadPart),in_module?caller-module:0,round,counter,valid,accepted,in_module};
    else ++diagnostic.dropped;
    ReleaseSRWLockExclusive(&lock);
    SetLastError(error);
}
__declspec(noinline) inline void bridge(void* object,int round){
    observe(round,reinterpret_cast<uintptr_t>(_ReturnAddress()));
    original(object,round);
}
inline bool install(uintptr_t base){
    if(installed)return true;
    // Full executable hash is checked by recorder_init before reaching here.
    constexpr unsigned char expected[]={0x89,0x54,0x24,0x10,0x48,0x89,0x4C,0x24,0x08,0x48,0x83,0xEC,0x38};
    auto target=reinterpret_cast<void*>(base+0x94A160);
    if(std::memcmp(target,expected,sizeof(expected)))return false;
    auto init=MH_Initialize();if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED)return false;
    if(MH_CreateHook(target,reinterpret_cast<void*>(&bridge),reinterpret_cast<void**>(&original))!=MH_OK)return false;
    module=base;identity.begin(1); // local observer generation, not a peer session
    if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);return false;}
    installed=true;
    // Observe the delegate listener before its optional level-script checks.
    // This does NOT establish round identity or allow recording/restoration.
    constexpr unsigned char upstream_expected[]={0x40,0x56,0x57,0x48,0x81,0xEC,0x18,0x01,0x00,0x00};
    auto upstream_target=reinterpret_cast<void*>(base+0x3C4580);
    if(!std::memcmp(upstream_target,upstream_expected,sizeof(upstream_expected)) &&
       MH_CreateHook(upstream_target,reinterpret_cast<void*>(&upstream_bridge),reinterpret_cast<void**>(&upstream_original))==MH_OK){
        if(MH_EnableHook(upstream_target)==MH_OK)upstream_installed=true;
        else MH_RemoveHook(upstream_target);
    }
    constexpr unsigned char lookup_expected[]={0x48,0x85,0xD2,0x75,0x07,0x48,0x8B,0x91,0x38,0x01,0x00,0x00};
    constexpr unsigned char accessor_expected[]={0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,0xEC,0x40};
    auto add=[&](uintptr_t rva,const unsigned char* bytes,size_t size,void* detour,void** trampoline){
        auto address=reinterpret_cast<void*>(base+rva);
        if(std::memcmp(address,bytes,size) || MH_CreateHook(address,detour,trampoline)!=MH_OK)return false;
        if(MH_EnableHook(address)==MH_OK)return true;
        MH_RemoveHook(address);return false;
    };
    lookup_installed=add(0x21BB8F0,lookup_expected,sizeof(lookup_expected),reinterpret_cast<void*>(&lookup_bridge),reinterpret_cast<void**>(&lookup_original));
    accessor_installed=add(0x3BDF40,accessor_expected,sizeof(accessor_expected),reinterpret_cast<void*>(&accessor_bridge),reinterpret_cast<void**>(&accessor_original));
    constexpr unsigned char scalar_expected[]={0x0F,0xB7,0x05,0x09,0xF0,0x56,0x04,0xC3};
    direct_identity=upstream_installed && !std::memcmp(reinterpret_cast<void*>(base+0x2D7390),scalar_expected,sizeof(scalar_expected));
    return true;
}
}

