#pragma once
#include <Windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include "EffectReleaseBridge.hpp"
#include "EffectAllocationSites.hpp"
namespace sc6::research::effect_allocations {
// Passive, bounded event log. Never dereferences allocation payloads or retains memory.
using Allocate=uintptr_t(*)(uintptr_t);
using Release=uintptr_t(*)(uintptr_t,uintptr_t);
inline Allocate allocate_original{};
inline Release release_original{};
inline uintptr_t base{};
inline bool attempted{},installed{};
inline SRWLOCK lock=SRWLOCK_INIT;
inline std::atomic<unsigned> epoch{},dropped{};
inline unsigned serial{},count{};
struct Event {uint64_t qpc{};uintptr_t address{},bytes{},caller{};unsigned kind{},thread{},type{};};
inline std::array<Event,8192> events{};
inline unsigned allocation_type(uintptr_t caller,uintptr_t bytes) noexcept {
    const auto* site=caller>=base?effect_allocation_site(caller-base):nullptr;
    return site && site->bytes==bytes?site->type:0;
}
inline void record(unsigned capture,uintptr_t caller,uintptr_t address,uintptr_t bytes,unsigned kind,unsigned type) noexcept {
    if(!capture || epoch.load()!=capture)return;
    if(!TryAcquireSRWLockExclusive(&lock)){++dropped;return;}
    if(epoch.load()==capture){
        if(count==events.size())++dropped;
        else {LARGE_INTEGER qpc{};QueryPerformanceCounter(&qpc);
            events[count++]={static_cast<uint64_t>(qpc.QuadPart),address,bytes,caller,kind,GetCurrentThreadId(),type};}
    }
    ReleaseSRWLockExclusive(&lock);
}
inline uintptr_t observe_allocate(uintptr_t bytes,uintptr_t caller,Allocate invoke){
    const auto capture=epoch.load();
    const auto result=invoke(bytes);const auto error=GetLastError();
    if(capture && (caller>=base && effect_allocation_site(caller-base)))
        record(capture,caller,result,bytes,1,allocation_type(caller,bytes));
    const auto* selected=effect_release::route.load(std::memory_order_acquire);
    if(result && selected && selected->allocation_return &&
       (caller>=base && effect_allocation_site(caller-base)))
        selected->allocation_return(selected->context,result,bytes,caller);
    SetLastError(error);return result;
}
inline uintptr_t observe_release(uintptr_t address,uintptr_t size_argument,uintptr_t caller,Release invoke){
    const auto error=GetLastError();const auto capture=epoch.load();
    if(capture && caller==base+0x3340F9)record(capture,caller,address,0,2,0);
    SetLastError(error);return effect_release::dispatch(address,size_argument,caller,invoke);
}
inline uintptr_t allocate_bridge(uintptr_t bytes){return observe_allocate(bytes,reinterpret_cast<uintptr_t>(_ReturnAddress()),allocate_original);}
inline uintptr_t release_bridge(uintptr_t address,uintptr_t size_argument){return observe_release(address,size_argument,reinterpret_cast<uintptr_t>(_ReturnAddress()),release_original);}
inline bool install(uintptr_t module){
    if(attempted)return installed && base==module;
    attempted=true;base=module;
    // Full image hash is already checked by recorder_init. Verify relevant bytes too.
    const auto call_matches=[&](uintptr_t at,uintptr_t target){
        int32_t displacement{};std::memcpy(&displacement,reinterpret_cast<void*>(module+at+1),4);
        return *reinterpret_cast<unsigned char*>(module+at)==0xe8 && static_cast<int64_t>(at+5)+displacement==static_cast<int64_t>(target);};
    if(!call_matches(0x334A19,0x4A61C0) || !call_matches(0x2FC71B,0x4A61C0) || !call_matches(0x3340F4,0x1F90000))return false;
    for(const auto& site:effect_allocation_sites)if(!call_matches(site.caller-5,0x4A61C0))return false;
    const auto init=MH_Initialize();if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED)return false;
    const auto allocation=reinterpret_cast<void*>(module+0x4A61C0),release=reinterpret_cast<void*>(module+0x1F90000);
    if(MH_CreateHook(allocation,reinterpret_cast<void*>(&allocate_bridge),reinterpret_cast<void**>(&allocate_original))!=MH_OK)return false;
    if(MH_CreateHook(release,reinterpret_cast<void*>(&release_bridge),reinterpret_cast<void**>(&release_original))!=MH_OK){MH_RemoveHook(allocation);return false;}
    // On partial enable failure keep valid trampolines and inactive pass-through.
    if(MH_EnableHook(allocation)!=MH_OK || MH_EnableHook(release)!=MH_OK)return false;
    installed=true;return true;
}
inline void start(){
    AcquireSRWLockExclusive(&lock);count=0;dropped=0;
    ++serial;if(!serial)++serial;epoch.store(serial);ReleaseSRWLockExclusive(&lock);
}
inline bool write(const std::filesystem::path& path,std::ostream& report){
    epoch.store(0);AcquireSRWLockExclusive(&lock);
    std::ofstream csv(path);csv<<"qpc,kind,thread,address,requested_bytes,caller_rva,allocation_type\n";
    unsigned allocated=0,released=0,unmatched=0,failed=0,unexpected=0;
    // Correlation is offline and bounded. Baseline/pre-arm objects are unmatched.
    std::array<bool,8192> consumed{};
    for(unsigned i=0;i<count;++i){const auto& e=events[i];
        csv<<e.qpc<<','<<e.kind<<','<<e.thread<<','<<e.address<<','<<e.bytes<<','<<e.caller-base<<','<<e.type<<'\n';
        if(e.kind==1){++allocated;if(!e.address)++failed;if(!e.type)++unexpected;}
        else {++released;bool matched=false;
            for(unsigned j=i;j>0;--j){const auto& a=events[j-1];
                if(a.kind==1 && a.address && a.address==e.address && !consumed[j-1]){consumed[j-1]=true;matched=true;break;}}
            if(!matched)++unmatched;
        }
    }
    csv.flush();const bool ok=csv.good();
    report<<"allocation_observer_schema=1\nallocation_events="<<count<<"\nallocation_dropped="<<dropped.load()
          <<"\nallocation_returns="<<allocated<<"\nallocation_failed="<<failed<<"\nallocation_unexpected_size="<<unexpected
          <<"\nrelease_calls="<<released<<"\nrelease_unmatched="<<unmatched<<"\nallocation_csv_written="<<ok
          <<"\nrelease_completion_verified=0\n";
    ReleaseSRWLockExclusive(&lock);return ok;
}
}
