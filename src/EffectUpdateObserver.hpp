#pragma once
#include <Windows.h>
#include "EffectAllocationObserver.hpp"
#include <MinHook.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
namespace sc6::research::effect_observer {
struct Node {uintptr_t address{},vtable{};};
struct List {std::array<Node,128> nodes{};unsigned count{};bool valid{};};
inline List sample(uintptr_t manager) noexcept {
    List list{};
    __try {
        if(!manager)return list;
        auto node=*reinterpret_cast<const uintptr_t*>(manager);
        while(node){
            if(list.count==list.nodes.size())return list;
            for(unsigned i=0;i<list.count;++i)if(list.nodes[i].address==node)return list;
            list.nodes[list.count++]={node,*reinterpret_cast<const uintptr_t*>(node)};
            node=*reinterpret_cast<const uintptr_t*>(node+0x10);
        }
        list.valid=true;
    }__except(EXCEPTION_EXECUTE_HANDLER){}
    return list;
}
struct Delta {unsigned added{},removed{},type_changed{};bool valid{};};
inline Delta compare(const List& before,const List& after) noexcept {
    Delta d{};if(!before.valid || !after.valid)return d;
    for(unsigned i=0;i<before.count;++i){
        bool found=false;
        for(unsigned j=0;j<after.count;++j)if(before.nodes[i].address==after.nodes[j].address){
            found=true;if(before.nodes[i].vtable!=after.nodes[j].vtable)++d.type_changed;break;
        }
        if(!found)++d.removed;
    }
    for(unsigned j=0;j<after.count;++j){
        bool found=false;
        for(unsigned i=0;i<before.count;++i)if(before.nodes[i].address==after.nodes[j].address){found=true;break;}
        if(!found)++d.added;
    }
    d.valid=true;return d;
}
using Original=uintptr_t(*)();
// Observed presence only, never allocation-generation evidence. Baselines reset
// on gaps/invalid reads/manager or thread changes so we do not invent continuity.
template<unsigned Capacity=8192> struct IdentityTrace {
    struct Event {unsigned sample{},phase{},kind{},epoch{};uintptr_t address{},old_type{},new_type{};};
    std::array<Event,Capacity> events{};
    unsigned count{},dropped{},epoch{},last_gap{},last_thread{};
    uintptr_t last_manager{};
    List previous{};
    void emit(unsigned sample_index,unsigned phase,unsigned kind,uintptr_t address,uintptr_t old_type,uintptr_t new_type) noexcept {
        if(count==events.size()){++dropped;return;}
        events[count++]={sample_index,phase,kind,epoch,address,old_type,new_type};
    }
    void reset() noexcept {count=0;dropped=0;epoch=0;last_gap=0;last_thread=0;last_manager=0;previous={};}
    void observe(const List& current,uintptr_t manager_address,unsigned thread,unsigned gap,unsigned sample_index,unsigned phase) noexcept {
        if(!current.valid || current.count>current.nodes.size() || !manager_address){previous={};return;}
        const bool continuous=previous.valid && last_manager==manager_address && last_thread==thread && last_gap==gap;
        if(!continuous){
            ++epoch;
            emit(sample_index,phase,4,0,0,0); // Baseline marker even for an empty list.
            for(unsigned i=0;i<current.count;++i)emit(sample_index,phase,0,current.nodes[i].address,0,current.nodes[i].vtable);
        }else{
            for(unsigned i=0;i<previous.count;++i){
                const auto& old=previous.nodes[i];bool found=false;
                for(unsigned j=0;j<current.count;++j)if(current.nodes[j].address==old.address){
                    found=true;if(current.nodes[j].vtable!=old.vtable)emit(sample_index,phase,3,old.address,old.vtable,current.nodes[j].vtable);break;
                }
                if(!found)emit(sample_index,phase,2,old.address,old.vtable,0);
            }
            for(unsigned j=0;j<current.count;++j){
                bool found=false;for(unsigned i=0;i<previous.count;++i)if(previous.nodes[i].address==current.nodes[j].address){found=true;break;}
                if(!found)emit(sample_index,phase,1,current.nodes[j].address,0,current.nodes[j].vtable);
            }
        }
        previous=current;last_manager=manager_address;last_thread=thread;last_gap=gap;
    }
};
inline IdentityTrace<> identities{};
inline Original original{};
inline uintptr_t base{};
inline bool installed{};
inline SRWLOCK lock=SRWLOCK_INIT;
inline std::atomic<bool> active{};
inline std::atomic<unsigned> dropped{};
struct Row {uint64_t qpc{};uintptr_t caller{},before_manager{},after_manager{};unsigned thread{},before{},after{};Delta delta{};};
inline std::array<Row,4096> rows{};
inline unsigned count{};
inline uint64_t deadline{};
inline uintptr_t manager() noexcept {
    __try {return *reinterpret_cast<const uintptr_t*>(base+0x470E038);}
    __except(EXCEPTION_EXECUTE_HANDLER){return 0;}
}
// Original called exactly once, including inactive, overflow and contention paths.
// Only list observations: same-address reuse and intra-update churn are invisible.
inline uintptr_t observe(uintptr_t caller,Original invoke) {
    if(!active.load(std::memory_order_acquire))return invoke();
    if(!TryAcquireSRWLockExclusive(&lock)){++dropped;return invoke();}
    if(!active.load() || count==rows.size()){
        if(active.load())++dropped;
        ReleaseSRWLockExclusive(&lock);return invoke();
    }
    const auto entry_error=GetLastError();
    Row row{};LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);
    row.qpc=stamp.QuadPart;row.caller=caller;row.thread=GetCurrentThreadId();
    row.before_manager=manager();const auto before=sample(row.before_manager);
    identities.observe(before,row.before_manager,row.thread,dropped.load(),count,0);
    SetLastError(entry_error);
    uintptr_t result{};bool completed=false;
    __try {
        result=invoke();const auto exit_error=GetLastError();
        row.after_manager=manager();const auto after=sample(row.after_manager);
        row.before=before.count;row.after=after.count;
        identities.observe(after,row.after_manager,row.thread,dropped.load(),count,1);
        if(row.before_manager==row.after_manager)row.delta=compare(before,after);
        rows[count++]=row;completed=true;SetLastError(exit_error);
    }__finally {if(!completed){identities.previous={};++dropped;}ReleaseSRWLockExclusive(&lock);}
    return result;
}
inline uintptr_t bridge(){return observe(reinterpret_cast<uintptr_t>(_ReturnAddress()),original);}
inline bool install(uintptr_t module){
    if(installed)return base==module;
    // Caller verifies the complete pinned executable hash first.
    constexpr unsigned char prefix[]={0x48,0x8b,0xc4,0x48,0x89,0x58,0x08};
    auto target=reinterpret_cast<void*>(module+0x333FD0);
    if(std::memcmp(target,prefix,sizeof(prefix)))return false;
    auto init=MH_Initialize();if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED)return false;
    if(MH_CreateHook(target,reinterpret_cast<void*>(&bridge),reinterpret_cast<void**>(&original))!=MH_OK)return false;
    base=module;
    if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);original=nullptr;return false;}
    installed=true;return true;
}
inline void poll(uintptr_t module,const std::filesystem::path& directory){
    const auto now=GetTickCount64();
    if(active.load() && now>=deadline && TryAcquireSRWLockExclusive(&lock)){
        active.store(false);
        const auto stem="effect-updates-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(now);
        std::ofstream csv(directory/(stem+".csv"));
        csv<<"qpc,caller_rva,thread,before_manager,after_manager,before_count,after_count,valid,added,removed,type_changed\n";
        unsigned invalid=0;
        for(unsigned i=0;i<count;++i){const auto& r=rows[i];if(!r.delta.valid)++invalid;
            csv<<r.qpc<<','<<(r.caller>=base?r.caller-base:0)<<','<<r.thread<<','<<r.before_manager<<','<<r.after_manager<<','<<r.before<<','<<r.after<<','<<r.delta.valid<<','<<r.delta.added<<','<<r.delta.removed<<','<<r.delta.type_changed<<'\n';}
        csv.flush();const bool csv_ok=csv.good();
        std::ofstream identity_csv(directory/(stem+"-identities.csv"));
        identity_csv<<"sample,phase,kind,epoch,address,old_vtable_rva,new_vtable_rva\n";
        for(unsigned i=0;i<identities.count;++i){const auto& e=identities.events[i];
            identity_csv<<e.sample<<','<<e.phase<<','<<e.kind<<','<<e.epoch<<','<<e.address<<','
                <<(e.old_type>=base?e.old_type-base:0)<<','<<(e.new_type>=base?e.new_type-base:0)<<'\n';}
        identity_csv.flush();const bool identity_ok=identity_csv.good();
        std::ofstream report(directory/(stem+".txt"));
        LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
        report<<"schema=3\nscope=effect_update_observation\nretention=0\nrestore=0\nallocation_events_complete=0\nsame_address_reuse_verified=0\nqpc_frequency="<<frequency.QuadPart<<"\nsamples="<<count<<"\ndropped="<<dropped.load()<<"\ninvalid="<<invalid<<"\ncsv_written="<<csv_ok
            <<"\nidentity_events="<<identities.count<<"\nidentity_dropped="<<identities.dropped<<"\nidentity_epochs="<<identities.epoch<<"\nidentity_csv_written="<<identity_ok<<'\n';
        effect_allocations::write(directory/(stem+"-allocations.csv"),report);
        ReleaseSRWLockExclusive(&lock);
    }
    if(active.load())return;
    const auto arm=directory/"effect-observer.arm";std::ifstream request(arm);if(!request)return;
    std::string token,extra;unsigned pid{};request>>token>>pid;
    const bool valid=bool(request) && !(request>>extra) && token=="EFFECT_OBSERVE_ONLY" && pid==GetCurrentProcessId();
    request.close();std::filesystem::remove(arm);
    const bool ready=valid && install(module) && effect_allocations::install(module);
    std::ofstream status(directory/("effect-observer-ready-"+std::to_string(GetCurrentProcessId())+".txt"));
    status<<"schema=1\nstatus="<<(ready?"recording":valid?"hook_failed":"invalid_arm")<<"\nduration_seconds=30\nretention=0\nrestore=0\n";
    if(!ready)return;
    AcquireSRWLockExclusive(&lock);count=0;dropped=0;identities.reset();effect_allocations::start();deadline=now+30000;active.store(true,std::memory_order_release);ReleaseSRWLockExclusive(&lock);
}
}
