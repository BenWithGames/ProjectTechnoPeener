#pragma once
#include <MinHook.h>
#include <intrin.h>
#include <filesystem>
#include <fstream>
#include "NativeRangeCaptureBridge.hpp"
#include "StageRangeDescriptor.hpp"
namespace sc6::research::range_observer {
using Original=uintptr_t(*)(uintptr_t,uintptr_t,uintptr_t);
inline Original original{};
inline uintptr_t base{};
inline bool installed{},recording{};
inline std::atomic<bool> armed{};
inline SRWLOCK lock=SRWLOCK_INIT;
inline uint64_t serial{},deadline{};
inline bool completed{},captured{};
inline NativeRangeImage<> image;
inline StageRangeDescriptor descriptor;
inline thread_local NativeRangeImage<>* scoped_image{};
inline thread_local bool scoped_seen{};
struct ScopedCapture {
    bool active{};
    explicit ScopedCapture(NativeRangeImage<>& output){
        output={};if(!scoped_image){scoped_image=&output;scoped_seen=false;active=true;}
    }
    ~ScopedCapture(){if(active)scoped_image=nullptr;}
    ScopedCapture(const ScopedCapture&)=delete;
    ScopedCapture& operator=(const ScopedCapture&)=delete;
};
inline uintptr_t observe(uintptr_t context,uintptr_t stream,uintptr_t vector,uintptr_t caller,Original invoke){
    if(caller==base+0x3844AC && scoped_image){
        if(scoped_seen){scoped_image->valid=false;return invoke(context,stream,vector);}
        scoped_seen=true;
        NativeRangeCaptureBridge<> capture;capture.arm(stream,caller);
        const auto result=capture.observe(context,stream,vector,caller,invoke);
        const auto error=GetLastError();capture.finish(*scoped_image);SetLastError(error);return result;
    }
    if(caller!=base+0x3844AC || !armed.load())return invoke(context,stream,vector);
    const auto entry_error=GetLastError();uint64_t session{};
    if(!TryAcquireSRWLockExclusive(&lock)){SetLastError(entry_error);return invoke(context,stream,vector);}
    if(recording && armed.exchange(false))session=serial;
    ReleaseSRWLockExclusive(&lock);
    if(!session){SetLastError(entry_error);return invoke(context,stream,vector);}
    NativeRangeCaptureBridge<> capture;
    capture.arm(stream,caller);
    SetLastError(entry_error);
    const auto result=capture.observe(context,stream,vector,caller,invoke);
    const auto error=GetLastError();NativeRangeImage<> copy;
    const bool ok=capture.finish(copy);
    StageRangeDescriptor detail;
    if(ok && copy.count>3)detail.capture(copy.entries[3].base);
    AcquireSRWLockExclusive(&lock);
    if(recording && session==serial){image=copy;descriptor=detail;captured=ok;completed=true;}
    ReleaseSRWLockExclusive(&lock);
    SetLastError(error);return result;
}
inline uintptr_t bridge(uintptr_t context,uintptr_t stream,uintptr_t vector){
    return observe(context,stream,vector,reinterpret_cast<uintptr_t>(_ReturnAddress()),original);
}
inline bool install(uintptr_t module){
    if(installed)return base==module;
    // Caller has verified the full pinned executable hash.
    constexpr unsigned char prefix[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x40};
    const auto target=reinterpret_cast<void*>(module+0x301820);
    int32_t displacement{};std::memcpy(&displacement,reinterpret_cast<void*>(module+0x3844A8),4);
    if(std::memcmp(target,prefix,sizeof(prefix)) || *reinterpret_cast<unsigned char*>(module+0x3844A7)!=0xE8 ||
       int64_t{0x3844AC}+displacement!=0x301820)return false;
    const auto init=MH_Initialize();if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED)return false;
    if(MH_CreateHook(target,reinterpret_cast<void*>(&bridge),reinterpret_cast<void**>(&original))!=MH_OK)return false;
    base=module;
    if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);original=nullptr;return false;}
    installed=true;return true;
}
inline void poll(uintptr_t module,const std::filesystem::path& directory){
    const auto now=GetTickCount64();
    AcquireSRWLockExclusive(&lock);
    if(recording && (completed || now>=deadline)){
        armed=false;recording=false;
        const auto stem="native-ranges-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(now);
        std::ofstream csv(directory/(stem+".csv"));csv<<"index,base,size\n";
        if(captured)for(unsigned i=0;i<image.count;++i)csv<<i<<','<<image.entries[i].base<<','<<image.entries[i].size<<'\n';
        csv.flush();
        std::ofstream report(directory/(stem+".txt"));
        report<<"schema=1\nscope=native_range_observation\nrestore=0\ninjection=0\nrollback_active=0\nownership_verified=0\nstatus="
              <<(captured?"captured":completed?"capture_rejected":"no_native_save_observed")
              <<"\nranges="<<image.count<<"\ncsv_written="<<csv.good()<<'\n';
        report<<"descriptor_schema=1\ndescriptor_valid="<<descriptor.valid<<"\ndescriptor_columns="<<descriptor.columns
              <<"\ndescriptor_records="<<descriptor.records<<"\ndescriptor_candidates="<<descriptor.candidates
              <<"\ndescriptor_read_count="<<descriptor.read_count<<"\ndescriptor_computed_size="<<descriptor.computed
              <<"\ndescriptor_bounded_out="<<descriptor.bounded_out<<"\ndescriptor_exception="<<descriptor.exception
              <<"\ndescriptor_size_match="<<(descriptor.valid && image.count>3 && descriptor.computed==image.entries[3].size)<<'\n';
        std::ofstream counts(directory/(stem+"-descriptor.csv"));counts<<"record,count\n";
        for(unsigned i=0;i<descriptor.read_count;++i)counts<<i<<','<<descriptor.counts[i]<<'\n';
        counts.flush();report<<"descriptor_csv_written="<<counts.good()<<'\n';
    }
    const bool busy=recording;ReleaseSRWLockExclusive(&lock);if(busy)return;
    const auto path=directory/"native-ranges.arm";std::ifstream request(path);if(!request)return;
    std::string token,extra;unsigned pid{};request>>token>>pid;
    const bool valid=bool(request) && !(request>>extra) && token=="NATIVE_RANGES_ONLY" && pid==GetCurrentProcessId();
    request.close();std::filesystem::remove(path);
    const bool ready=valid && install(module);
    std::ofstream status(directory/("native-ranges-ready-"+std::to_string(GetCurrentProcessId())+".txt"));
    status<<"schema=1\nstatus="<<(ready?"waiting_for_native_save":valid?"hook_failed":"invalid_arm")<<"\nrestore=0\nduration_seconds=30\n";
    if(!ready)return;
    AcquireSRWLockExclusive(&lock);++serial;image={};descriptor={};completed=captured=false;recording=true;deadline=now+30000;armed=true;ReleaseSRWLockExclusive(&lock);
}
}
