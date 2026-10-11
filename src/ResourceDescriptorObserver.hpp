#pragma once
#include <Windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include "ResourceReferenceProbe.hpp"
#include "ResourceClearObserver.hpp"
#include "ResourcePayloadFingerprint.hpp"
#include "ResourceDependencyCapture.hpp"
namespace sc6::research::resource_observer {
using Original=uintptr_t(*)(uintptr_t);
struct Row { uintptr_t object{},vtable{},payload{},result{},descriptor0{},descriptor1{}; int32_t size{}; unsigned ready{},valid{}; uintptr_t control{}; int32_t strong{},weak{}; unsigned owner_valid{}; };
using Factory=uintptr_t(*)(uintptr_t,uintptr_t,uintptr_t);
inline Factory factory_original{};
struct OwnerRow {uintptr_t object{},control{},vtable{},controlled{}; int32_t strong{},weak{}; unsigned valid{},eligibility{};uintptr_t payload{},descriptor0{},descriptor1{};unsigned cross_frame_acquired{},resource_valid{},ready{},probe_attempted{},probe_passed{};LONG probe_before{},probe_held{},probe_released{};};
inline std::array<OwnerRow,4096> owners{};
inline unsigned owner_count{};
inline std::atomic<bool> retention_requested{};
inline std::atomic<bool> descriptor_hold_requested{},descriptor_hold_selected{};
inline std::atomic<unsigned> descriptor_hold_attempts{},descriptor_hold_acquired{};
inline std::atomic<uintptr_t> descriptor_hold_object{},descriptor_hold_control{},descriptor_hold_payload{};
inline ResourcePayloadFingerprint descriptor_baseline{};
inline std::atomic<unsigned> descriptor_hash_samples{},descriptor_hash_changes{},descriptor_hash_invalid{};
inline std::atomic<uint64_t> descriptor_initial_hash{},descriptor_last_hash{};
inline void sample_descriptor_payload(uint32_t){
    const auto sample=ResourcePayloadFingerprint::read(descriptor_hold_object.load(),lease_runtime::module);
    ++descriptor_hash_samples;descriptor_last_hash=sample.hash;
    if(!sample.valid)++descriptor_hash_invalid;
    else if(!descriptor_baseline.matches(sample))++descriptor_hash_changes;
}
inline bool retention_attempted{},retention_passed{};
inline LONG retention_before{},retention_held{},retention_released{};
inline OwnerRow read_owner(uintptr_t output,uintptr_t module){
    OwnerRow row{};
    __try {
        row.object=*reinterpret_cast<uintptr_t*>(output);
        row.control=*reinterpret_cast<uintptr_t*>(output+8);
        row.vtable=*reinterpret_cast<uintptr_t*>(row.control);
        if(row.vtable!=module+0x36E9D40)return row;
        row.controlled=*reinterpret_cast<uintptr_t*>(row.control+0x10);
        row.strong=*reinterpret_cast<volatile int32_t*>(row.control+8);
        row.weak=*reinterpret_cast<volatile int32_t*>(row.control+12);
        row.valid=row.object && row.controlled==row.object && row.strong>0 && row.weak>0 &&
            *reinterpret_cast<uintptr_t*>(row.object)==module+0x3CEA0F8;
    } __except(EXCEPTION_EXECUTE_HANDLER) {row.valid=0;}
    return row;
}
inline Original original{};
inline uintptr_t base{};
inline bool installed{},recording{};
inline std::atomic<uint64_t> epoch{};
inline SRWLOCK lock=SRWLOCK_INIT;
inline std::array<Row,4096> rows{};
inline unsigned count{};
inline std::atomic<unsigned> dropped{};
inline uint64_t serial{},deadline{};
inline unsigned clear_start{},clear_same_start{},clear_other_start{},clear_unknown_start{};
inline uint64_t boundary_start{};
inline Row read(uintptr_t object,uintptr_t result,uintptr_t module){
    Row row{};row.object=object;row.result=result;
    __try {
        row.vtable=*reinterpret_cast<uintptr_t*>(object);
        if(row.vtable!=module+0x3CEA0F8)return row;
        row.payload=*reinterpret_cast<uintptr_t*>(object+0x38);
        row.size=*reinterpret_cast<int32_t*>(object+0x48);
        row.ready=*reinterpret_cast<unsigned char*>(object+0x50);
        row.descriptor0=*reinterpret_cast<uintptr_t*>(module+0x470D0D0);
        row.descriptor1=*reinterpret_cast<uintptr_t*>(module+0x470D0F8);
        row.valid=1;
    } __except(EXCEPTION_EXECUTE_HANDLER) {row.valid=0;}
    return row;
}
inline bool descriptor_hold_eligible(const Row& row){
    return row.valid && row.owner_valid && row.ready && row.size>0 && row.payload &&
        row.result==row.payload && (row.payload==row.descriptor0 || row.payload==row.descriptor1);
}
inline uintptr_t observe(uintptr_t object,uintptr_t caller,Original invoke,uintptr_t return_slot=0){
    const auto session=epoch.load();
    if(caller!=base+0x89D79C || (!session && !ResourceDependencyCapture::active() && !dependency_events::owned.load()))return invoke(object);
    const auto result=invoke(object); // Native exceptions propagate unchanged.
    const auto error=GetLastError();
    auto row=read(object,result,base); // Caller still owns its temporary reference.
    if(return_slot){
        // Pinned enumerator stack pair, not an inferred field of the object.
        // Read only; no ownership is acquired from this diagnostic.
        const auto owner=read_owner(return_slot+0x80,base);
        row.control=owner.control;row.strong=owner.strong;row.weak=owner.weak;
        row.owner_valid=owner.valid && owner.object==object;
    }
    if(descriptor_hold_eligible(row)){
        ResourceDependencyCapture::observe_owned(row.control,row.payload);
        if(const auto notify=dependency_events::owned.load())notify(row.object,row.control,row.payload);
    }
    if(!session){SetLastError(error);return result;}
    if(descriptor_hold_requested.load() && epoch.load()==session && descriptor_hold_eligible(row) &&
       GetCurrentThreadId()==lease_service::owner.load()){
        bool unselected=false;
        if(descriptor_hold_selected.compare_exchange_strong(unselected,true)){
            ++descriptor_hold_attempts;
            descriptor_baseline=ResourcePayloadFingerprint::read(row.object,base);
            descriptor_initial_hash=descriptor_baseline.hash;
            // Start lifetime tracking at the loaded-resource boundary, not in
            // the lobby where a loading reset would consume the experiment.
            if(descriptor_baseline.valid && descriptor_baseline.payload==row.payload &&
               descriptor_baseline.size==row.size && lease_runtime::prepare(base) && lease_runtime::capture(row.control)){
                descriptor_hold_object=row.object;descriptor_hold_control=row.control;
                descriptor_hold_payload=row.payload;++descriptor_hold_acquired;
                lease_runtime::observe_held=sample_descriptor_payload;
            }
        }
    }
    if(TryAcquireSRWLockExclusive(&lock)){
        if(epoch.load()==session){if(count<rows.size())rows[count++]=row;else ++dropped;}
        ReleaseSRWLockExclusive(&lock);
    }else ++dropped;
    SetLastError(error);return result;
}
__declspec(noinline) inline uintptr_t bridge(uintptr_t object){
    return observe(object,reinterpret_cast<uintptr_t>(_ReturnAddress()),original,
        reinterpret_cast<uintptr_t>(_AddressOfReturnAddress()));
}
// First failing predicate; evaluated even in read-only captures.
inline unsigned eligibility(const OwnerRow& owner,const Row& resource){
    if(!owner.valid)return 1;
    if(!resource.valid)return 2;
    if(!resource.ready)return 3;
    if(!resource.payload)return 4;
    if(!resource.descriptor0 && !resource.descriptor1)return 5;
    if(resource.payload!=resource.descriptor0 && resource.payload!=resource.descriptor1)return 6;
    return 0;
}
inline uintptr_t observe_factory(uintptr_t manager,uintptr_t output,uintptr_t key,Factory invoke){
    const auto session=epoch.load();
    const auto result=invoke(manager,output,key);
    if(!session)return result;
    const auto error=GetLastError();
    auto row=read_owner(output,base);
    const auto resource=row.valid?read(row.object,0,base):Row{};
    row.eligibility=eligibility(row,resource);
    row.payload=resource.payload;row.descriptor0=resource.descriptor0;row.descriptor1=resource.descriptor1;
    row.resource_valid=resource.valid;row.ready=resource.ready;
    if(row.valid && epoch.load()==session && !descriptor_hold_requested.load())row.cross_frame_acquired=lease_runtime::capture(row.control);
    if(TryAcquireSRWLockExclusive(&lock)){
        if(epoch.load()==session){
            // Native caller owns the returned pair throughout this callback.
            // Probe only bounded, publishable rows; no pointer is retained.
            if(row.valid && retention_requested.load() && owner_count<512){
                row.probe_attempted=1;
                row.probe_passed=probe_resource_reference(reinterpret_cast<volatile LONG*>(row.control+8),row.probe_before,row.probe_held,row.probe_released);
                if(!retention_attempted)retention_passed=true;
                retention_attempted=true;retention_passed=retention_passed && row.probe_passed;
                retention_before=row.probe_before;retention_held=row.probe_held;retention_released=row.probe_released;
            }
            if(owner_count<owners.size())owners[owner_count++]=row;else ++dropped;}
        ReleaseSRWLockExclusive(&lock);
    }else ++dropped;
    SetLastError(error);return result;
}
inline uintptr_t factory_bridge(uintptr_t manager,uintptr_t output,uintptr_t key){return observe_factory(manager,output,key,factory_original);}
inline bool install(uintptr_t module){
    if(installed)return base==module;
    constexpr unsigned char prefix[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0x01,0x48,0x8B,0xD9};
    const auto target=reinterpret_cast<void*>(module+0x2DDDA30);
    if(std::memcmp(target,prefix,sizeof(prefix)) || *reinterpret_cast<uint16_t*>(module+0x89D79A)!=0x12FF)return false;
    constexpr unsigned char factory_prefix[]={0x48,0x89,0x74,0x24,0x18,0x55,0x41,0x56,0x41,0x57};
    const auto factory_target=reinterpret_cast<void*>(module+0x2DDB8C0);
    if(std::memcmp(factory_target,factory_prefix,sizeof(factory_prefix)))return false;
    const auto init=MH_Initialize();if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED)return false;
    if(MH_CreateHook(target,reinterpret_cast<void*>(&bridge),reinterpret_cast<void**>(&original))!=MH_OK)return false;
    if(MH_CreateHook(factory_target,reinterpret_cast<void*>(&factory_bridge),reinterpret_cast<void**>(&factory_original))!=MH_OK){MH_RemoveHook(target);return false;}
    base=module;
    if(MH_EnableHook(factory_target)!=MH_OK){MH_RemoveHook(factory_target);MH_RemoveHook(target);return false;}
    if(MH_EnableHook(target)!=MH_OK){MH_DisableHook(factory_target);MH_RemoveHook(factory_target);MH_RemoveHook(target);original=nullptr;return false;}
    installed=true;return true;
}
inline void poll(uintptr_t module,const std::filesystem::path& directory){
    const auto now=GetTickCount64();
    if(recording && now>=deadline){
        epoch=0;
        if(lease_runtime::enabled.load())lease_runtime::stop();
        AcquireSRWLockExclusive(&lock);const auto captured=count;const auto captured_owners=owner_count;ReleaseSRWLockExclusive(&lock);
        // Epoch is disabled; rows remain immutable until the next arm below.
        const auto stem="resource-descriptors-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(now);
        std::ofstream csv(directory/(stem+".csv"));
        csv<<"object,vtable,payload,result,size,ready,valid,descriptor0,descriptor1,match0,match1,control,strong,weak,owner_valid\n";
        unsigned accessor_owners=0,descriptor_owners=0;
        for(unsigned i=0;i<captured;++i){const auto& v=rows[i];csv<<v.object<<','<<v.vtable<<','<<v.payload<<','<<v.result<<','<<v.size<<','<<v.ready<<','<<v.valid<<','<<v.descriptor0<<','<<v.descriptor1<<','<<(v.valid && v.payload && v.payload==v.descriptor0)<<','<<(v.valid && v.payload && v.payload==v.descriptor1)<<','<<v.control<<','<<v.strong<<','<<v.weak<<','<<v.owner_valid<<'\n';
            accessor_owners+=v.owner_valid;
            descriptor_owners+=v.owner_valid && v.valid && v.ready && v.payload && v.result==v.payload && (v.payload==v.descriptor0 || v.payload==v.descriptor1);
        }
        csv.flush();
        std::ofstream ownership(directory/(stem+"-owners.csv"));
        ownership<<"object,control,vtable,controlled,strong,weak,valid,eligibility,payload,descriptor0,descriptor1,resource_valid,ready,probe_attempted,probe_passed,probe_before,probe_held,probe_released,cross_frame_acquired\n";
        for(unsigned i=0;i<captured_owners;++i){const auto& v=owners[i];ownership<<v.object<<','<<v.control<<','<<v.vtable<<','<<v.controlled<<','<<v.strong<<','<<v.weak<<','<<v.valid<<','<<v.eligibility<<','<<v.payload<<','<<v.descriptor0<<','<<v.descriptor1<<','<<v.resource_valid<<','<<v.ready<<','<<v.probe_attempted<<','<<v.probe_passed<<','<<v.probe_before<<','<<v.probe_held<<','<<v.probe_released<<','<<v.cross_frame_acquired<<'\n';}
        ownership.flush();
        std::array<unsigned,7> reasons{};
        for(unsigned i=0;i<captured_owners;++i)++reasons[owners[i].eligibility];
        std::ofstream report(directory/(stem+".txt"));
        report<<"schema=1\nscope=resource_descriptor_observation\nrestore=0\ninjection=0\nrollback_active=0\nownership_verified=0\ncontrol_pointer_source=factory_output\nstatus="<<(captured?"captured":"no_accessor_events")<<"\nowner_records="<<captured_owners<<"\nowner_csv_written="<<ownership.good()<<"\nrecords="<<captured<<"\ndropped="<<dropped.load()<<"\ncsv_written="<<csv.good()<<'\n';
        report<<"accessor_owner_schema=1\naccessor_owner_source=pinned_enumerator_stack\naccessor_owner_valid="<<accessor_owners<<"\naccessor_descriptor_owner_valid="<<descriptor_owners<<"\naccessor_reference_acquired="<<descriptor_hold_acquired.load()<<'\n';
        report<<"descriptor_hold_requested="<<descriptor_hold_requested.load()<<"\ndescriptor_hold_attempts="<<descriptor_hold_attempts.load()
              <<"\ndescriptor_hold_acquired="<<descriptor_hold_acquired.load()<<"\ndescriptor_hold_object="<<descriptor_hold_object.load()
              <<"\ndescriptor_hold_control="<<descriptor_hold_control.load()<<"\ndescriptor_hold_payload="<<descriptor_hold_payload.load()
              <<"\ndescriptor_hold_frames_after_adoption=3\n";
        report<<"descriptor_fingerprint_schema=1\ndescriptor_initial_hash="<<descriptor_initial_hash.load()
              <<"\ndescriptor_last_hash="<<descriptor_last_hash.load()<<"\ndescriptor_hash_samples="<<descriptor_hash_samples.load()
              <<"\ndescriptor_hash_changes="<<descriptor_hash_changes.load()<<"\ndescriptor_hash_invalid="<<descriptor_hash_invalid.load()
              <<"\ndescriptor_hash_limit_bytes=1048576\ndescriptor_immutability_proven=0\n";
        report<<"retention_probe_requested="<<retention_requested.load()<<"\nretention_probe_attempted="<<retention_attempted
              <<"\nretention_probe_passed="<<retention_passed<<"\nretention_before="<<retention_before<<"\nretention_held="<<retention_held
              <<"\nretention_released="<<retention_released<<"\nretention_cross_frame=0\nretention_probe_schema=2\nretention_probe_limit=512\n";
        report<<"eligibility_schema=1\neligibility_legend=eligible,owner_invalid,resource_invalid,not_ready,no_payload,no_descriptor,different_payload\n";
        for(unsigned i=0;i<reasons.size();++i)report<<"eligibility_"<<i<<'='<<reasons[i]<<'\n';
        report<<"clear_hook_installed="<<resource_clear::installed<<"\nclear_events_process_total="<<resource_clear::events.load()
              <<"\nclear_same_thread="<<resource_clear::same_thread.load()<<"\nclear_other_thread="<<resource_clear::other_thread.load()
              <<"\nclear_unknown_thread="<<resource_clear::unknown_thread.load()<<"\nclear_last_thread="<<resource_clear::last_thread.load()
              <<"\nboundary_owner_thread="<<lease_service::owner.load()<<"\nboundary_events_process_total="<<lease_service::events.load()<<'\n';
        report<<"cross_frame_requested="<<lease_runtime::used.load()<<"\ncross_frame_acquired="<<lease_runtime::queue.total()
              <<"\ncross_frame_released="<<lease_runtime::released.load()<<"\ncross_frame_cancelled="<<lease_runtime::cancelled.load()
              <<"\ncross_frame_pending="<<(lease_runtime::queue.total()-lease_runtime::released.load())<<"\ncross_frame_factory_off_owner="<<lease_runtime::wrong_factory_thread.load()<<"\ncross_frame_transferred="<<lease_runtime::transferred.load()<<"\ncross_frame_queue_pending="<<lease_runtime::queue.pending()<<'\n';
        report<<"handoff_diagnostic_schema=1\nhandoff_stop_legend=none,external_stop,resource_clear,frame_reset,service_cancel\n"
              <<"handoff_first_stop="<<lease_runtime::first_stop.load()<<"\nhandoff_gate_disabled="<<lease_runtime::gate_disabled.load()
              <<"\nhandoff_gate_no_owner="<<lease_runtime::gate_no_owner.load()<<"\nhandoff_gate_queue_rejected="<<lease_runtime::gate_queue_rejected.load()
              <<"\nhandoff_reset_from="<<lease_runtime::reset_from.load()<<"\nhandoff_reset_to="<<lease_runtime::reset_to.load()<<'\n';
        report<<"lifecycle_window_schema=1\nclear_events_in_window="<<(resource_clear::events.load()-clear_start)
              <<"\nclear_same_thread_in_window="<<(resource_clear::same_thread.load()-clear_same_start)
              <<"\nclear_other_thread_in_window="<<(resource_clear::other_thread.load()-clear_other_start)
              <<"\nclear_unknown_thread_in_window="<<(resource_clear::unknown_thread.load()-clear_unknown_start)
              <<"\nboundary_events_in_window="<<(lease_service::events.load()-boundary_start)<<'\n';
        recording=false;
    }
    if(recording)return;
    const auto path=directory/"resource-descriptors.arm";std::ifstream request(path);if(!request)return;
    std::string token,extra;unsigned pid{};request>>token>>pid;
    const bool valid=bool(request) && !(request>>extra) && (token=="RESOURCE_DESCRIPTORS_ONLY" || token=="RESOURCE_RETAIN_ONLY" || token=="RESOURCE_HOLD_ONLY" || token=="RESOURCE_DESCRIPTOR_HOLD_ONLY" || token=="RESOURCE_REMATCH_ONLY") && pid==GetCurrentProcessId();
    request.close();std::filesystem::remove(path);
    const bool ready=valid && install(module) && resource_clear::install(module) &&
        (token!="RESOURCE_HOLD_ONLY" || lease_runtime::prepare(module)) &&
        (token!="RESOURCE_DESCRIPTOR_HOLD_ONLY" || !lease_runtime::used.load());
    const unsigned duration=token=="RESOURCE_REMATCH_ONLY"?180:120;
    std::ofstream status(directory/("resource-descriptors-ready-"+std::to_string(GetCurrentProcessId())+".txt"));
    status<<"schema=1\nstatus="<<(ready?"waiting_for_resource_enumeration":valid?"hook_failed":"invalid_arm")<<"\nrestore=0\nduration_seconds="<<duration<<'\n';
    if(!ready)return;
    clear_start=resource_clear::events.load();clear_same_start=resource_clear::same_thread.load();
    clear_other_start=resource_clear::other_thread.load();clear_unknown_start=resource_clear::unknown_thread.load();
    boundary_start=lease_service::events.load();
    AcquireSRWLockExclusive(&lock);count=0;owner_count=0;dropped=0;
    descriptor_hold_requested=token=="RESOURCE_DESCRIPTOR_HOLD_ONLY";descriptor_hold_selected=false;
    descriptor_hold_attempts=0;descriptor_hold_acquired=0;
    descriptor_hold_object=0;descriptor_hold_control=0;descriptor_hold_payload=0;
    descriptor_hash_samples=0;descriptor_hash_changes=0;descriptor_hash_invalid=0;
    descriptor_initial_hash=0;descriptor_last_hash=0;
    retention_requested=token=="RESOURCE_RETAIN_ONLY";retention_attempted=retention_passed=false;
    retention_before=retention_held=retention_released=0;recording=true;deadline=now+uint64_t(duration)*1000;epoch=++serial;ReleaseSRWLockExclusive(&lock);
}
}
