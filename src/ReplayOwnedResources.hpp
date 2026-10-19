#pragma once
#include "NativeRollbackWindow.hpp"
#include "ResourceDescriptorObserver.hpp"
#include "GeneratedOwnerObserver.hpp"
#include "EffectAllocationObserver.hpp"
#include "RngCallerHook.hpp"
#include "SharedRngHook.hpp"
#include "CameraCalcHook.hpp"
#include "SelectorLifecycleHook.hpp"
#include "AnimationCopyHook.hpp"
#include "TimerLookupHook.hpp"
#include "InlineRngHook.hpp"
#include "ScopedResourceClear.hpp"
#include "MatchEndObservation.hpp"
namespace sc6::research {
// References originate only in synchronous native ownership callbacks.
struct OwnershipCancellation {
 std::atomic<unsigned> reason{},thread{},expected{},had_owned{};
 void note(unsigned code,unsigned actual,unsigned owner,bool held){
  unsigned empty=0;if(!reason.compare_exchange_strong(empty,UINT_MAX))return;
  thread=actual;expected=owner;had_owned=held;reason.store(code,std::memory_order_release);
 }
};
class ReplayOwnedResources {
public:
 enum class ThreadDecision {accept,defer,reject};
 static ThreadDecision thread_decision(unsigned actual,unsigned expected,bool held){
  if(!expected && !held)return ThreadDecision::defer;
  return actual && actual==expected?ThreadDecision::accept:ThreadDecision::reject;
 }
 // A loading callback may nominate a provisional thread, but only a verified
 // battle boundary can confirm it. References never migrate between threads.
 class FirstMatchOwner {
  std::atomic<unsigned> candidate_{};
 public:
  bool nominate(unsigned actual,unsigned known){
   if(!actual || (known && actual!=known))return false;
   unsigned empty=0;candidate_.compare_exchange_strong(empty,actual);
   return candidate_.load()==actual;
  }
  bool matches(unsigned actual)const{return actual && candidate_.load()==actual;}
  unsigned candidate()const{return candidate_.load();}
 };
 std::atomic<unsigned> deferred_callbacks{},last_deferred_thread{};
 std::atomic<bool> first_boundary_verified{};
 unsigned provisional_thread()const{return first_owner_.candidate();}
private:
 OwnershipCancellation cancellation_;
 FirstMatchOwner first_owner_;bool first_match_{};
 void cancel(unsigned code){cancellation_.note(code,GetCurrentThreadId(),lease_service::owner.load(),has_owned_.load());cancelled_=true;}
 struct Descriptor {ResourceReferenceLease lease;uintptr_t object{},control{},payload{};};
 std::array<Descriptor,2> descriptors_;unsigned count_{};
 DWORD owner_{};uintptr_t module_{};
 std::atomic<bool> wind_requested_{};
 std::unique_ptr<NativeEffectSession> wind_;
 std::atomic<NativeEffectSession*> wind_report_{};
 inline static std::atomic<ReplayOwnedResources*> active_{};
 std::atomic<bool> cancelled_{},has_owned_{};
 bool accept_thread(unsigned actual,unsigned code){
  if(first_match_){
   if(first_owner_.nominate(actual,lease_service::owner.load()))return true;
   cancel(code);return false;
  }
  const auto decision=thread_decision(actual,lease_service::owner.load(),has_owned_.load());
  if(decision==ThreadDecision::defer){++deferred_callbacks;last_deferred_thread=actual;return false;}
  if(decision==ThreadDecision::reject){cancel(code);return false;}
  return true;
 }
 static void descriptor(uintptr_t object,uintptr_t control,uintptr_t payload){
  const auto self=active_.load();if(!self || self->cancelled_)return;
  if(!self->accept_thread(GetCurrentThreadId(),1))return;
  self->owner_=GetCurrentThreadId();
  for(unsigned i=0;i<self->count_;++i)if(self->descriptors_[i].control==control){if(self->descriptors_[i].payload!=payload)self->cancel(2);return;}
  if(self->count_==2){self->cancel(3);return;}
  auto& entry=self->descriptors_[self->count_];
  if(!entry.lease.acquire(control,self->owner_,0,ResourceDependencyCapture::native_object_dispose,ResourceDependencyCapture::native_control_dispose)){self->cancel(4);return;}
  entry.object=object;entry.control=control;entry.payload=payload;++self->count_;self->has_owned_=true;
 }
 static void buffer(const generated_observer::Row& row){
  const auto self=active_.load();if(!self || self->cancelled_)return;
  if(row.root!=self->module_+0x4844DD0 && row.root!=self->module_+0x4845E80)return;
  if(!self->accept_thread(row.thread,5))return;
  self->owner_=row.thread;
  if(self->generated.active() && self->generated.root!=row.root)return;
  if(!self->generated.acquire(row.root,row.buffer,row.control,row.size,generated_observer::dispose_buffer,generated_observer::dispose_buffer_control))self->cancel(6);else self->has_owned_=true;
 }
 static void clear(uintptr_t object,uintptr_t caller,bool locked){if(const auto self=active_.load())self->on_scoped_clear(object,caller,locked);}
 static void setup(uintptr_t root){if(const auto self=active_.load()){
  if(root!=self->module_+0x4844DD0 && root!=self->module_+0x4845E80)return;
  if(!self->has_owned_.load())return; // Setup alone does not nominate an owner.
  if(!self->accept_thread(GetCurrentThreadId(),8))return;
  if(self->generated.active() && self->generated.root==root)self->cancel(9);
 }}
 static bool seed(void* context,uint32_t){
  auto& self=*static_cast<ReplayOwnedResources*>(context);
  if(!self.ready())return false;
  for(unsigned i=0;i<self.count_;++i){const auto& entry=self.descriptors_[i];
   const auto row=resource_observer::read(entry.object,entry.payload,self.module_);
   if(!row.valid || !row.ready || row.payload!=entry.payload)return false;
  }
  for(unsigned i=0;i<self.count_;++i)ResourceDependencyCapture::observe_owned(self.descriptors_[i].control,self.descriptors_[i].payload);
  return true;
 }
public:
 unsigned retention_result()const{
  const auto* wind=wind_report_.load(std::memory_order_acquire);
  return wind?wind->retention_result():0;
 }
 bool enable_animation_copy(){return animation_copy::install(module_) && inline_rng::install(module_) && timer_lookup::install(module_);}
 bool enable_wind(bool trace_rng=true){if(!selector_lifetime::install(module_) || (trace_rng && (!rng_trace::install(module_) || !shared_rng::install(module_) || !camera_calc::install(module_))) || !effect_allocations::install(module_))return false;wind_requested_=true;return true;}
 template<class Stream> void wind_report(Stream& out)const{
  out<<"suite_scoped_clear_schema=2\nsuite_scoped_clear_seen="<<clear_seen.load()<<"\nsuite_scoped_clear_ignored="<<clear_ignored.load()
     <<"\nsuite_scoped_clear_related="<<clear_related.load()<<"\nsuite_scoped_clear_unknown="<<clear_unknown.load()
     <<"\nsuite_scoped_clear_container="<<clear_container.load()<<"\nsuite_scoped_clear_caller="<<clear_caller.load()
     <<"\nsuite_scoped_clear_result="<<clear_result.load()<<"\nsuite_scoped_clear_reason="<<clear_reason.load()
     <<"\nsuite_scoped_clear_visited="<<clear_visited.load()<<"\nsuite_scoped_clear_secondary="<<clear_secondary.load()
     <<"\nsuite_scoped_clear_secondary_validated="<<clear_secondary_validated.load()
     <<"\nsuite_scoped_clear_secondary_buffer="<<clear_secondary_buffer.load()<<"\nsuite_scoped_clear_secondary_bytes="<<clear_secondary_bytes.load()
     <<"\nsuite_scoped_clear_secondary_length="<<clear_secondary_length.load()<<"\nsuite_scoped_clear_secondary_capacity="<<clear_secondary_capacity.load()
     <<"\nsuite_scoped_clear_secondary_check="<<clear_secondary_check.load()
     <<"\nsuite_scoped_clear_index="<<clear_index.load()<<"\nsuite_scoped_clear_matched="<<clear_matched.load()<<'\n';
  const auto* wind=wind_report_.load(std::memory_order_acquire);
  out<<"suite_wind_native_integrated=1\nsuite_wind_requested="<<wind_requested_.load()<<'\n';
  if(!wind){out<<"suite_wind_captures=0\nsuite_wind_loads=0\nsuite_wind_comparisons=0\n";return;}
  wind->retention_report(out);
  out<<"suite_wind_failure="<<wind->failure.load()<<"\nsuite_wind_captures="<<wind->captures.load()
     <<"\nsuite_wind_loads="<<wind->loads.load()<<"\nsuite_wind_comparisons="<<wind->comparisons.load()
     <<"\nsuite_wind_retirements="<<wind->retirements.load()<<"\nsuite_wind_added="<<wind->added.load()
     <<"\nsuite_wind_removed="<<wind->removed.load()<<"\nsuite_wind_mismatch_object="<<wind->mismatch_object.load()
     <<"\nsuite_wind_external_check="<<wind->external_check.load()
     <<"\nsuite_wind_external_visits="<<wind->external_visits.load()
     <<"\nsuite_wind_external_column="<<wind->external_column.load()
     <<"\nsuite_wind_external_interval="<<wind->external_interval.load()
     <<"\nsuite_wind_external_candidate="<<wind->external_candidate.load()
     <<"\nsuite_wind_external_address="<<wind->external_address.load()
     <<"\nsuite_wind_external_extent="<<wind->external_extent.load()
     <<"\nsuite_wind_mismatch_offset="<<wind->mismatch_offset.load()<<'\n';
 }
 static bool effect_ranges(void* context,std::array<EffectOwnedRange,3>& out,unsigned& count){
  auto& self=*static_cast<ReplayOwnedResources*>(context);count=0;
  if(!self.ready())return false;
  for(unsigned i=0;i<self.count_;++i){const auto& entry=self.descriptors_[i];
   const auto row=resource_observer::read(entry.object,entry.payload,self.module_);
   if(!row.valid || !row.ready || row.size<=0 || row.payload!=entry.payload || !entry.lease.active())return false;
   out[count++]={entry.payload,static_cast<uintptr_t>(row.size),entry.control};
  }
  out[count++]={self.generated.buffer,self.generated.size,self.generated.control};return true;
 }
 std::atomic<unsigned> clear_end_vtable{},clear_end_handler{},clear_end_thread{},clear_end_query{};
 MatchEndObservation match_end_observation()const{
  return cancelled_.load()?MatchEndObservation{clear_end_vtable.load(),clear_end_handler.load(),clear_end_thread.load(),clear_end_query.load()}:MatchEndObservation::read(module_);
 }
 void on_resource_clear(){if(has_owned_.load())cancel(7);}
 std::atomic<unsigned> clear_seen{},clear_ignored{},clear_related{},clear_unknown{},clear_result{},clear_reason{},clear_visited{},clear_secondary{},clear_index{UINT32_MAX};
 std::atomic<uintptr_t> clear_container{},clear_caller{},clear_matched{},clear_secondary_buffer{},clear_secondary_bytes{};
 std::atomic<unsigned> clear_secondary_validated{},clear_secondary_length{},clear_secondary_capacity{},clear_secondary_check{};
 void on_scoped_clear(uintptr_t object,uintptr_t caller,bool locked){
  if(!has_owned_.load() || cancelled_.load())return;
  ++clear_seen;ScopedResourceClear::Evidence e{};e.container=object;e.caller=caller;e.reason=ScopedResourceClear::context;
  // Do not inspect owner-managed lease state on a foreign thread.
  if(GetCurrentThreadId()==owner_ && owner_==lease_service::owner.load() && locked){
   std::array<ScopedResourceClear::Dependency,3> deps{};unsigned n=0;bool valid=count_>0 && count_<=2 && generated.active();
   for(unsigned i=0;i<count_ && i<2;++i){const auto& d=descriptors_[i];const auto row=resource_observer::read(d.object,d.payload,module_);
    if(!d.lease.active() || !row.valid || !row.ready || row.size<=0 || row.payload!=d.payload){valid=false;break;}
    deps[n++]={d.object,d.control,d.payload,static_cast<uintptr_t>(row.size)};
   }
   if(valid){deps[n++]={0,generated.control,generated.buffer,generated.size};e=ScopedResourceClear::inspect(object,caller,module_,deps.data(),n,locked);}
  }
  clear_container=e.container;clear_caller=e.caller;clear_result=e.result;clear_reason=e.reason;
  clear_secondary_buffer=e.secondary_buffer;clear_secondary_bytes=e.secondary_bytes;
  clear_secondary_validated=e.secondary_validated;clear_secondary_length=e.secondary_length;
  clear_secondary_capacity=e.secondary_capacity;clear_secondary_check=e.secondary_check;
  clear_visited=e.visited;clear_secondary=e.secondary;clear_index=e.index;clear_matched=e.matched;
  if(e.result==ScopedResourceClear::unrelated){++clear_ignored;return;}
  if(e.result==ScopedResourceClear::related)++clear_related;else ++clear_unknown;
  const auto end=MatchEndObservation::read(module_);
  clear_end_vtable=end.vtable;clear_end_handler=end.handler;clear_end_thread=end.thread;clear_end_query=end.state_query;
  on_resource_clear();
 }
 GeneratedSnapshotDependency generated;
 const OwnershipCancellation& cancellation()const{return cancellation_;}
 bool cancelled()const{return cancelled_.load();}
 bool confirm_boundary(){
  if(!first_match_)return true;
  if(!first_owner_.candidate())return false;
  if(!first_owner_.matches(GetCurrentThreadId())){cancel(10);return false;}
  first_boundary_verified=true;return true;
 }
 bool ready()const{return (!first_match_ || first_boundary_verified.load()) && !cancelled_ && owner_==GetCurrentThreadId() && count_ && generated.active();}
 bool current_dependencies()const noexcept{
  if(!ready())return false;
  __try {
   const uintptr_t first=*reinterpret_cast<uintptr_t*>(module_+0x470D0D0);
   const uintptr_t second=*reinterpret_cast<uintptr_t*>(module_+0x470D0F8);
   bool found_first=false,found_second=second==0;
   for(unsigned i=0;i<count_;++i){
    const auto& entry=descriptors_[i];
    const auto row=resource_observer::read(entry.object,entry.payload,module_);
    if(!entry.lease.active() || !row.valid || !row.ready || row.size<=0 || row.payload!=entry.payload ||
       (entry.payload!=first && entry.payload!=second))return false;
    found_first|=entry.payload==first;found_second|=entry.payload==second;
   }
   const auto descriptor=*reinterpret_cast<uintptr_t*>(generated.root);
   return first && found_first && found_second && (descriptor==first || descriptor==second) &&
    *reinterpret_cast<uintptr_t*>(generated.root+8)==generated.buffer &&
    *reinterpret_cast<uintptr_t*>(generated.root+0x430)==generated.buffer &&
    *reinterpret_cast<uintptr_t*>(generated.root+0x438)==generated.control;
  }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
 }
 // Owner-thread successor acquisition after the prior native history retires.
 // No guessed control blocks: acquire from strong references still held by prior.
 // If native dependencies changed, refuse; their fresh ownership callbacks must
 // be collected instead. Keep both objects alive for diagnostics and old packets.
 bool start_successor(ReplayOwnedResources& prior){
  if(this==&prior || module_ || count_ || generated.active() || active_.load()!=&prior ||
     !prior.current_dependencies() || (prior.wind_ && (!prior.wind_->finish() || !prior.wind_->retired_on_owner())))return false;
  module_=prior.module_;owner_=prior.owner_;
  wind_requested_=prior.wind_requested_.load();
  for(unsigned i=0;i<prior.count_;++i){
   const auto& old=prior.descriptors_[i];auto& fresh=descriptors_[i];
   if(!fresh.lease.acquire(old.control,owner_,0,ResourceDependencyCapture::native_object_dispose,
      ResourceDependencyCapture::native_control_dispose)){stop();return false;}
   fresh.object=old.object;fresh.control=old.control;fresh.payload=old.payload;++count_;has_owned_=true;
  }
  if(!generated.retain_from(prior.generated) || !prior.current_dependencies() || !current_dependencies()){
   stop();return false;
  }
  auto* expected=&prior;
  if(!active_.compare_exchange_strong(expected,this,std::memory_order_release)){stop();return false;}
  // The existing callbacks resolve active_ on each call. The prior source keeps
  // its own references; its eventual stop cannot detach this successor's hooks.
  return true;
 }
 ReplayDependencySource source(){
  if(wind_requested_.load() && ready() && !wind_){wind_=std::make_unique<NativeEffectSession>();wind_->configure(effect_allocations::release_original);wind_report_.store(wind_.get(),std::memory_order_release);}
  return {this,seed,&generated,[](void* context){return static_cast<ReplayOwnedResources*>(context)->ready();},wind_.get(),{this,effect_ranges}};
 }
 bool start(uintptr_t module,bool first_match=false){
  if(active_.load() || dependency_events::owned.load() || dependency_events::clear.load() || dependency_events::scoped_clear.load() || generated_observer::owned_listener.load() || generated_observer::recording || resource_observer::recording)return false;
  if(!resource_observer::install(module) || !resource_clear::install(module) || !generated_observer::install(module))return false;
  module_=module;first_match_=first_match;active_=this;dependency_events::owned=descriptor;dependency_events::scoped_clear=clear;
  generated_observer::owned_listener=buffer;generated_observer::setup_listener=setup;return true;
 }
 bool stop(){
  if(owner_ && owner_!=GetCurrentThreadId())return false;
  if(wind_ && !wind_->finish())return false;
  cancelled_=true;
  for(auto& entry:descriptors_)if(entry.lease.active())entry.lease.release(0,true);
  count_=0;generated.reset();
  if(active_.load()==this){dependency_events::owned=nullptr;dependency_events::scoped_clear=nullptr;generated_observer::owned_listener=nullptr;generated_observer::setup_listener=nullptr;active_=nullptr;}
  return true;
 }
};
}
