#include "../src/AnimationCopyHook.hpp"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace sc6::research;
extern "C" {
 void MatrixBeforeSite();void MatrixAfterSite();
 void AnimationBeforeSite();void AnimationAfterSite();
 void AnimationHarness(void*,void*,void*);void AnimationContinue();void AnimationClobber();
 void (*SelectedAnimationBridge)()=AnimationContinue;
 uintptr_t AnimationStack{};
}
static unsigned calls{},observed_site{};
static uintptr_t observed_cache{},observed_ring{},observed_stack{};
static void callback(uintptr_t c,uintptr_t r,uintptr_t s,unsigned site){
 ++calls;observed_cache=c;observed_ring=r;observed_stack=s;observed_site=site;
 animation_copy::observe(c,r,s,site);AnimationClobber();
}
int main(){
 using namespace animation_copy;
 std::array<unsigned char,0x1200> cache{},ring{};
 std::array<unsigned char,0x3800> stack{};AnimationStack=reinterpret_cast<uintptr_t>(stack.data());
 stack[0x3758]=1;stack[0x3728]=1;cache[0x7B0]=42;ring[0x7B0]=17;cache[0xB18]=1;
 const auto oldcache=cache,oldring=ring;const auto oldstack=stack;
 SC6AnimationEntryOriginal=SC6AnimationBeforeOriginal=SC6AnimationAfterOriginal=SC6MatrixEntryOriginal=SC6MatrixBeforeOriginal=SC6MatrixAfterOriginal=reinterpret_cast<void*>(&AnimationContinue);
 SC6AnimationCallback=callback;
 std::array<unsigned char,176> expected{},actual{};
 AnimationHarness(expected.data(),cache.data(),ring.data());
 void (*bridges[])()={SC6AnimationEntryBridge,SC6AnimationBeforeBridge,SC6AnimationAfterBridge,SC6MatrixEntryBridge,SC6MatrixBeforeBridge,SC6MatrixAfterBridge};
 for(unsigned site=0;site<6;++site){
  SelectedAnimationBridge=bridges[site];
  for(unsigned i=0;i<100;++i){SetLastError(123);AnimationHarness(actual.data(),cache.data(),ring.data());
   assert(actual==expected && GetLastError()==123 && observed_site==site);
   assert(observed_cache==reinterpret_cast<uintptr_t>(cache.data()) && observed_ring==reinterpret_cast<uintptr_t>(ring.data()) && observed_stack==(site==3?uintptr_t(0x11110003):AnimationStack));}
 }
 assert(calls==600 && cache==oldcache && ring==oldring && stack==oldstack);
 auto snap=std::make_unique<Snapshot>();snapshot(*snap);assert(snap->total==600 && snap->events[100].condition==1 && snap->events[0].condition==UINT32_MAX);
 assert(snap->events[100].cache_translation[0]==42 && snap->events[100].ring_translation[0]==17);
 {rng_trace::Scope scope(2);observe(reinterpret_cast<uintptr_t>(cache.data()),reinterpret_cast<uintptr_t>(ring.data()),AnimationStack,1);}
 observe(1,1,1,1);snapshot(*snap);assert(snap->events[600].phase==2 && snap->events[601].valid==0);
 AcquireSRWLockExclusive(&lock);record(Event{});ReleaseSRWLockExclusive(&lock);
 for(unsigned i=0;i<4200;++i)record(Event{});
 snapshot(*snap);assert(snap->count==4096 && snap->total==4802 && snap->dropped==1);
 for(uint64_t i=snap->total-snap->count;i<snap->total;++i)assert(snap->events[i%4096].sequence==i);
 // Exercise real MinHook trampolines on the same seven-byte interior cmp,
 // both copy directions and the following branch, not just direct bridge calls.
 assert(MH_Initialize()==MH_OK);
 assert(MH_CreateHook(reinterpret_cast<void*>(&AnimationBeforeSite),reinterpret_cast<void*>(&SC6AnimationBeforeBridge),&SC6AnimationBeforeOriginal)==MH_OK);
 assert(MH_CreateHook(reinterpret_cast<void*>(&AnimationAfterSite),reinterpret_cast<void*>(&SC6AnimationAfterBridge),&SC6AnimationAfterOriginal)==MH_OK);
 SelectedAnimationBridge=AnimationBeforeSite;
 for(unsigned direction=0;direction<2;++direction){
  cache=oldcache;ring=oldring;stack[0x3758]=static_cast<unsigned char>(direction);
  AnimationHarness(expected.data(),cache.data(),ring.data());const auto wantcache=cache,wantring=ring;
  cache=oldcache;ring=oldring;
  assert(MH_EnableHook(reinterpret_cast<void*>(&AnimationBeforeSite))==MH_OK);
  assert(MH_EnableHook(reinterpret_cast<void*>(&AnimationAfterSite))==MH_OK);
  AnimationHarness(actual.data(),cache.data(),ring.data());
  assert(actual==expected && cache==wantcache && ring==wantring);
  assert(MH_DisableHook(reinterpret_cast<void*>(&AnimationBeforeSite))==MH_OK);
  assert(MH_DisableHook(reinterpret_cast<void*>(&AnimationAfterSite))==MH_OK);
 }
 assert(MH_CreateHook(reinterpret_cast<void*>(&MatrixBeforeSite),reinterpret_cast<void*>(&SC6MatrixBeforeBridge),&SC6MatrixBeforeOriginal)==MH_OK);
 assert(MH_CreateHook(reinterpret_cast<void*>(&MatrixAfterSite),reinterpret_cast<void*>(&SC6MatrixAfterBridge),&SC6MatrixAfterOriginal)==MH_OK);
 SelectedAnimationBridge=MatrixBeforeSite;
 for(unsigned direction=0;direction<2;++direction){
  cache=oldcache;ring=oldring;stack[0x3728]=static_cast<unsigned char>(direction);
  AnimationHarness(expected.data(),cache.data(),ring.data());const auto wantcache=cache,wantring=ring;
  cache=oldcache;ring=oldring;
  assert(MH_EnableHook(reinterpret_cast<void*>(&MatrixBeforeSite))==MH_OK);
  assert(MH_EnableHook(reinterpret_cast<void*>(&MatrixAfterSite))==MH_OK);
  AnimationHarness(actual.data(),cache.data(),ring.data());
  assert(actual==expected && cache==wantcache && ring==wantring);
  assert(MH_DisableHook(reinterpret_cast<void*>(&MatrixBeforeSite))==MH_OK);
  assert(MH_DisableHook(reinterpret_cast<void*>(&MatrixAfterSite))==MH_OK);
 }
 assert(MH_Uninitialize()==MH_OK);
 puts("PASS: all six interior bridges preserve GPRs, flags, SSE/FP controls, LastError and game bytes; phase, conditions, invalid reads, overflow and dropped events recorded");
}

