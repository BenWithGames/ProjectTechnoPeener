#pragma once
#include <Windows.h>
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include "ResourceLeaseRuntime.hpp"
#include "GeneratedBufferHold.hpp"
namespace sc6::research::generated_observer {
using Setup=int(*)(uintptr_t,uintptr_t);
using Allocate=bool(*)(uintptr_t,uintptr_t,uintptr_t);
inline Setup setup_original{};inline Allocate allocate_original{};
inline uintptr_t base{};inline bool installed{},recording{};
inline std::atomic<bool> armed{};
inline SRWLOCK lock=SRWLOCK_INIT;
inline thread_local uintptr_t current_root{};
struct Context {uintptr_t previous;explicit Context(uintptr_t value):previous(current_root){current_root=value;}~Context(){current_root=previous;}};
struct Row {uintptr_t root{},buffer{},control{},size{};DWORD thread{},frame_thread{};int32_t strong{},weak{};unsigned valid{};};
inline std::atomic<void(*)(const Row&)> owned_listener{};
inline std::atomic<void(*)(uintptr_t)> setup_listener{};
inline std::array<Row,256> rows{};inline unsigned count{};inline std::atomic<unsigned> dropped{};inline uint64_t deadline{};
struct ThreadCorrelation {
 DWORD first{};unsigned samples{},changes{};
 void observe(DWORD thread){if(!first)first=thread;else if(first!=thread)++changes;++samples;}
 unsigned classify(DWORD thread)const {return !first || !samples || changes ? 0u : thread==first ? 1u : 2u;}
};
inline ThreadCorrelation correlation{};
inline GeneratedBufferHold hold;
inline bool hold_requested{},used{};
inline uintptr_t held_root{},cancel_root{};
inline std::atomic<bool> stop_hold{};
inline void dispose_buffer(uintptr_t control){reinterpret_cast<void(*)(uintptr_t)>(base+0x37C7B0)(control);}
inline void dispose_buffer_control(uintptr_t control){reinterpret_cast<void(*)(uintptr_t)>(base+0x2DE1D0)(control);}
inline void observe_frame(){
 if(!armed.load())return;
 const auto error=GetLastError();
 if(TryAcquireSRWLockExclusive(&lock)){
  if(armed.load()){correlation.observe(GetCurrentThreadId());hold.step(stop_hold.load(),2);}
  ReleaseSRWLockExclusive(&lock);
 }else ++dropped;
 SetLastError(error);
}
inline Row read(uintptr_t root,uintptr_t pair,uintptr_t size){
 Row row{};row.root=root;row.size=size;row.thread=GetCurrentThreadId();row.frame_thread=lease_service::owner.load();
 __try {row.buffer=*reinterpret_cast<uintptr_t*>(pair);row.control=*reinterpret_cast<uintptr_t*>(pair+8);
  if(!row.control || *reinterpret_cast<uintptr_t*>(row.control)!=base+0x3E89308)return row;
  row.strong=*reinterpret_cast<volatile int32_t*>(row.control+8);row.weak=*reinterpret_cast<volatile int32_t*>(row.control+12);
  row.valid=row.buffer && row.strong>0 && row.weak>0 && *reinterpret_cast<uintptr_t*>(row.control+0x18)==row.buffer && *reinterpret_cast<uintptr_t*>(pair+16)==size;
 }__except(EXCEPTION_EXECUTE_HANDLER){row.valid=0;}return row;
}
inline bool observe_allocate(uintptr_t manager,uintptr_t pair,uintptr_t size,Allocate invoke){
 const auto result=invoke(manager,pair,size);const auto error=GetLastError();
 if(result && (armed.load() || owned_listener.load()) && current_root && pair==current_root+0x430){
  const auto row=read(current_root,pair,size);
  if(row.valid)if(const auto notify=owned_listener.load())notify(row);
  if(TryAcquireSRWLockExclusive(&lock)){if(armed.load()){if(count<rows.size()){rows[count++]=row;if(hold_requested && row.valid && !stop_hold.load()){if(hold.capture(row.control,row.frame_thread,dispose_buffer,dispose_buffer_control))held_root=row.root;}}else ++dropped;}ReleaseSRWLockExclusive(&lock);}else ++dropped;
 }SetLastError(error);return result;
}
inline bool allocate_bridge(uintptr_t manager,uintptr_t pair,uintptr_t size){return observe_allocate(manager,pair,size,allocate_original);}
inline int setup_bridge(uintptr_t root,uintptr_t descriptor){const auto error=GetLastError();AcquireSRWLockExclusive(&lock);if(hold.pending()){const auto before=hold.cancelled;hold.before_setup(held_root,root);if(hold.cancelled!=before)cancel_root=root;}ReleaseSRWLockExclusive(&lock);SetLastError(error);if(const auto notify=setup_listener.load())notify(root);SetLastError(error);Context context(root);return setup_original(root,descriptor);}
inline bool install(uintptr_t module){
 if(installed)return base==module;
 const auto setup=reinterpret_cast<void*>(module+0x392080),allocation=reinterpret_cast<void*>(module+0x37C630);
 const unsigned char sp[]={0x40,0x53,0x57,0x41,0x54,0x48,0x83,0xEC,0x30},ap[]={0x40,0x57,0x48,0x83,0xEC,0x50};
 if(std::memcmp(setup,sp,sizeof(sp)) || std::memcmp(allocation,ap,sizeof(ap)))return false;
 const auto init=MH_Initialize();if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED)return false;
 if(MH_CreateHook(setup,reinterpret_cast<void*>(&setup_bridge),reinterpret_cast<void**>(&setup_original))!=MH_OK)return false;
 if(MH_CreateHook(allocation,reinterpret_cast<void*>(&allocate_bridge),reinterpret_cast<void**>(&allocate_original))!=MH_OK){MH_RemoveHook(setup);return false;}
 base=module;
 if(MH_EnableHook(allocation)!=MH_OK){MH_RemoveHook(allocation);MH_RemoveHook(setup);return false;}
 if(MH_EnableHook(setup)!=MH_OK){MH_DisableHook(allocation);MH_RemoveHook(allocation);MH_RemoveHook(setup);return false;}installed=true;return true;
}
inline void poll(uintptr_t module,const std::filesystem::path& directory){
 const auto now=GetTickCount64();
 if(recording && now>=deadline){
  AcquireSRWLockExclusive(&lock);stop_hold=true;if(hold.pending()){ReleaseSRWLockExclusive(&lock);return;}armed=false;recording=false;
  std::ofstream report(directory/("generated-owner-"+std::to_string(GetCurrentProcessId())+".txt"));
  report<<"schema=1\nscope=generated_owner_observation\nrestore=0\ninjection=0\nrollback_active=0\nrecords="<<count<<"\ndropped="<<dropped.load()<<'\n';
  report<<"hold_requested="<<hold_requested<<"\nreferences_acquired="<<hold.acquired<<"\nreferences_released="<<hold.released<<"\nhold_cancelled="<<hold.cancelled<<"\nhold_rejected="<<hold.rejected<<"\nhold_pending="<<hold.pending()<<'\n';
  report<<"hold_diagnostic_schema=1\nhold_depth_callbacks="<<hold.depth()<<"\nhold_cancel_reason="<<hold.cancel_reason<<"\nhold_cancel_reason_names=0:none,1:query_setup,2:deadline\nheld_root="<<held_root<<"\ncancel_setup_root="<<cancel_root<<'\n';
  report<<"thread_correlation_schema=1\nobserved_frame_thread="<<correlation.first<<"\nframe_samples="<<correlation.samples<<"\nframe_thread_changes="<<correlation.changes<<'\n';
  std::ofstream csv(directory/("generated-owner-"+std::to_string(GetCurrentProcessId())+".csv"));csv<<"root,buffer,control,size,thread,frame_thread,strong,weak,valid,thread_relation\n";
  for(unsigned i=0;i<count;++i){const auto& row=rows[i];csv<<row.root<<','<<row.buffer<<','<<row.control<<','<<row.size<<','<<row.thread<<','<<row.frame_thread<<','<<row.strong<<','<<row.weak<<','<<row.valid<<','<<correlation.classify(row.thread)<<'\n';}
  csv.flush();report<<"csv_written="<<csv.good()<<'\n';ReleaseSRWLockExclusive(&lock);
 }
 if(recording)return;
 const auto path=directory/"generated-owner.arm";std::ifstream request(path);if(!request)return;
 std::string token,extra;unsigned pid{};request>>token>>pid;const bool valid=bool(request) && !(request>>extra) && (token=="GENERATED_OWNER_ONLY" || token=="GENERATED_HOLD_ONLY") && pid==GetCurrentProcessId();request.close();std::filesystem::remove(path);
 const bool ready=valid && !used && install(module);
 if(ready){AcquireSRWLockExclusive(&lock);used=true;hold_requested=token=="GENERATED_HOLD_ONLY";stop_hold=false;count=0;dropped=0;correlation={};deadline=now+120000;recording=true;armed=true;ReleaseSRWLockExclusive(&lock);}
 std::ofstream status(directory/("generated-owner-ready-"+std::to_string(GetCurrentProcessId())+".txt"));status<<"schema=1\nstatus="<<(ready?"armed":"rejected")<<"\nduration_seconds=120\n";
}
}
