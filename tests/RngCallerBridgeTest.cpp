#include <array>
#include <MinHook.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
extern "C" {
unsigned TestSeed{},TestCalls{},TestCallbackCalls{};
std::array<uint64_t,4> TestArgs{};
unsigned Frame=42;
void FakeRng();void ClobberObserver();void SC6RngBridge();
void (*SC6RngOriginal)()=FakeRng;
void (*SC6RngCallback)()=ClobberObserver;
const unsigned* SC6RngSeed=&TestSeed;
const unsigned* SC6RngFrame=&Frame;
void RngHarness(void(*)(),void*);
}
int main(){
 std::array<unsigned char,176> expected{},actual{};
 TestSeed=10;RngHarness(FakeRng,expected.data());
 TestSeed=10;RngHarness(SC6RngBridge,actual.data());
 if(expected!=actual || TestCalls!=2 || TestCallbackCalls!=1 || TestSeed!=20 ||
    !TestArgs[0] || TestArgs[1]!=42 || TestArgs[2]!=10 || TestArgs[3]!=20)std::abort();
 if(MH_Initialize()!=MH_OK || MH_CreateHook(reinterpret_cast<void*>(&FakeRng),reinterpret_cast<void*>(&SC6RngBridge),reinterpret_cast<void**>(&SC6RngOriginal))!=MH_OK || MH_EnableHook(reinterpret_cast<void*>(&FakeRng))!=MH_OK)std::abort();
 for(unsigned i=0;i<1000;++i){TestSeed=10;RngHarness(FakeRng,actual.data());if(actual!=expected || TestSeed!=20)std::abort();}
 if(TestCalls!=1002 || TestCallbackCalls!=1001)std::abort();
 if(MH_DisableHook(reinterpret_cast<void*>(&FakeRng))!=MH_OK || MH_RemoveHook(reinterpret_cast<void*>(&FakeRng))!=MH_OK || MH_Uninitialize()!=MH_OK)std::abort();
 std::puts("PASS: native volatile GPR/flags/XMM outputs and controls preserved despite clobbering observer; original once, seed/frame/caller recorded");
}
