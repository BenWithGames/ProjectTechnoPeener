#pragma once
#include "NativeRollbackWindow.hpp"
#include "ResourceDescriptorObserver.hpp"
#include "GeneratedOwnerObserver.hpp"
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
 static void clear(){if(const auto self=active_.load())self->on_resource_clear();}
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
 void on_resource_clear(){if(has_owned_.load())cancel(7);}
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
 ReplayDependencySource source(){return {this,seed,&generated,[](void* context){return static_cast<ReplayOwnedResources*>(context)->ready();}};}
 bool start(uintptr_t module,bool first_match=false){
  if(active_.load() || dependency_events::owned.load() || dependency_events::clear.load() || generated_observer::owned_listener.load() || generated_observer::recording || resource_observer::recording)return false;
  if(!resource_observer::install(module) || !resource_clear::install(module) || !generated_observer::install(module))return false;
  module_=module;first_match_=first_match;active_=this;dependency_events::owned=descriptor;dependency_events::clear=clear;
  generated_observer::owned_listener=buffer;generated_observer::setup_listener=setup;return true;
 }
 bool stop(){
  if(owner_ && owner_!=GetCurrentThreadId())return false;
  cancelled_=true;
  for(auto& entry:descriptors_)if(entry.lease.active())entry.lease.release(0,true);
  count_=0;generated.reset();
  if(active_.load()==this){dependency_events::owned=nullptr;dependency_events::clear=nullptr;generated_observer::owned_listener=nullptr;generated_observer::setup_listener=nullptr;active_=nullptr;}
  return true;
 }
};
}
