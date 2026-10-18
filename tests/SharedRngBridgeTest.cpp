#include "../src/SharedRngTrace.hpp"
#include <memory>
#include <array>
#include <MinHook.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
extern "C" {
unsigned TestSeed{},TestCalls{},TestCallbackCalls{};
std::array<uint64_t,4> TestArgs{};
void FakeRng();void ClobberObserver();void SC6SharedRngBridge1();void SC6SharedRngBridge2();
void (*SC6SharedRngOriginal1)()=FakeRng;
void (*SC6SharedRngOriginal2)()=FakeRng;
void (*SC6SharedRngCallback)()=ClobberObserver;
void RngHarness(void(*)(),void*);
}
int main(){
 std::array<unsigned char,176> expected{},actual{};RngHarness(FakeRng,expected.data());
 const auto init=MH_Initialize();if(init!=MH_OK)std::abort();
 for(unsigned stream=1;stream<=2;++stream){
  auto bridge=stream==1?SC6SharedRngBridge1:SC6SharedRngBridge2;
  auto original=stream==1?&SC6SharedRngOriginal1:&SC6SharedRngOriginal2;
  if(MH_CreateHook(reinterpret_cast<void*>(&FakeRng),reinterpret_cast<void*>(bridge),reinterpret_cast<void**>(original))!=MH_OK || MH_EnableHook(reinterpret_cast<void*>(&FakeRng))!=MH_OK)std::abort();
  for(unsigned i=0;i<1000;++i){RngHarness(FakeRng,actual.data());if(actual!=expected || TestArgs[1]!=stream || TestArgs[2]!=0x3F200000 || !TestArgs[0])std::abort();}
  if(MH_DisableHook(reinterpret_cast<void*>(&FakeRng))!=MH_OK || MH_RemoveHook(reinterpret_cast<void*>(&FakeRng))!=MH_OK)std::abort();
 }
 if(TestCalls!=2001 || TestCallbackCalls!=2000 || MH_Uninitialize()!=MH_OK)std::abort();
 using namespace sc6::research;
 auto mem=VirtualAlloc(nullptr,0x5000000,MEM_RESERVE,PAGE_READWRITE);if(!mem)std::abort();
 shared_rng::module=reinterpret_cast<uintptr_t>(mem);
 for(auto offset:{0x470D000u,0x485E000u,0x470E000u})if(!VirtualAlloc(reinterpret_cast<void*>(shared_rng::module+offset),4096,MEM_COMMIT,PAGE_READWRITE))std::abort();
 *reinterpret_cast<unsigned*>(shared_rng::module+0x470D0C4)=55;
 *reinterpret_cast<unsigned*>(shared_rng::module+0x485EB94)=12;
 *reinterpret_cast<unsigned*>(shared_rng::module+0x470E2B0)=99;
 {rng_trace::Scope scope(2);for(unsigned i=0;i<8200;++i){SetLastError(1234);shared_rng::record(shared_rng::module+0x332739,2,42);if(GetLastError()!=1234)std::abort();}}
 auto snap=std::make_unique<shared_rng::Snapshot>();shared_rng::snapshot(*snap);
 auto e=snap->events[8199%8192];
 if(snap->total!=8200 || snap->count!=8192 || e.caller!=0x332739 || e.phase!=2 || e.frame!=55 || e.state[25]!=12 || e.state[26]!=99)std::abort();
 AcquireSRWLockExclusive(&shared_rng::lock);shared_rng::record(0,0,0);ReleaseSRWLockExclusive(&shared_rng::lock);
 VirtualFree(mem,0,MEM_RELEASE);shared_rng::record(0,0,0);shared_rng::snapshot(*snap);
 if(snap->dropped!=2 || snap->total!=8200)std::abort();
 std::puts("PASS: both shared RNG wrappers preserve native inputs/outputs, flags and floating controls under detouring; original exactly once");
}
