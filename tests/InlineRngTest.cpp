#include "../src/InlineRngHook.hpp"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace sc6::research;
extern "C" {void AnimationHarness(void*,void*,void*);void AnimationContinue();void AnimationClobber();
void (*SelectedAnimationBridge)()=AnimationContinue;uintptr_t AnimationStack{};}
static unsigned calls{},observed{};
static void callback(const uint64_t* registers,unsigned site){++calls;observed=site;inline_rng::observe(registers,site);AnimationClobber();}
int main(){using namespace inline_rng;
SC6InlineRngCallback=callback;
SC6InlineRngOriginal0=reinterpret_cast<void*>(&AnimationContinue);
SC6InlineRngOriginal1=reinterpret_cast<void*>(&AnimationContinue);
SC6InlineRngOriginal2=reinterpret_cast<void*>(&AnimationContinue);
SC6InlineRngOriginal3=reinterpret_cast<void*>(&AnimationContinue);
SC6InlineRngOriginal4=reinterpret_cast<void*>(&AnimationContinue);
SC6InlineRngOriginal5=reinterpret_cast<void*>(&AnimationContinue);
SC6InlineRngOriginal6=reinterpret_cast<void*>(&AnimationContinue);
SC6InlineRngOriginal7=reinterpret_cast<void*>(&AnimationContinue);
std::array<unsigned char,176> expected{},actual{};std::array<unsigned char,0x3800> stack{};AnimationStack=reinterpret_cast<uintptr_t>(stack.data());
AnimationHarness(expected.data(),stack.data(),stack.data());
void (*bridges[])()={SC6InlineRngBridge0,SC6InlineRngBridge1,SC6InlineRngBridge2,SC6InlineRngBridge3,SC6InlineRngBridge4,SC6InlineRngBridge5,SC6InlineRngBridge6,SC6InlineRngBridge7};
for(unsigned site=0;site<8;++site){SelectedAnimationBridge=bridges[site];for(unsigned i=0;i<100;++i){SetLastError(123);AnimationHarness(actual.data(),stack.data(),stack.data());assert(actual==expected && GetLastError()==123 && observed==site);}}
assert(calls==800);
auto memory=VirtualAlloc(nullptr,0x4860000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(memory);module=reinterpret_cast<uintptr_t>(memory);
*reinterpret_cast<unsigned*>(module+0x485EB28)=123;*reinterpret_cast<unsigned*>(module+0x470D0C4)=77;
std::array<uint64_t,16> regs{};regs[0]=17;regs[3]=18;regs[8]=19;
for(unsigned i=0;i<8;++i)observe(regs.data(),i);
assert(boundary(module,123,false));assert(!boundary(module,124,false));{rng_trace::Scope phase(2);assert(boundary(module,123,true));}
observe(nullptr,0);
auto snap=std::make_unique<Snapshot>();snapshot(*snap);
for(unsigned i=0;i<8;++i){const auto& e=snap->events[800+i];assert(e.valid && e.frame==77 && e.before==123 && e.after==(i==6?18u:i==7?19u:17u));}
assert(snap->events[808].site==8 && snap->events[810].phase==2 && !snap->events[811].valid);
assert(*reinterpret_cast<unsigned*>(module+0x485EB28)==123);
AcquireSRWLockExclusive(&lock);record(Event{});ReleaseSRWLockExclusive(&lock);
for(unsigned i=0;i<8300;++i)record(Event{});snapshot(*snap);assert(snap->count==8192 && snap->dropped==1);
for(uint64_t i=snap->total-snap->count;i<snap->total;++i)assert(snap->events[i%8192].sequence==i);
// Relocation test: real MinHook trampoline must execute the original RIP-relative store exactly once.
assert(MH_Initialize()==MH_OK);
auto code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(code);
// mov [rip+250],eax; jmp [rip+0]; dq AnimationContinue; seed at +256.
const unsigned char machine[]={0x89,0x05,0xFA,0,0,0,0xFF,0x25,0,0,0,0};std::memcpy(code,machine,sizeof(machine));
auto continuation=reinterpret_cast<uintptr_t>(&AnimationContinue);std::memcpy(code+12,&continuation,8);
SelectedAnimationBridge=reinterpret_cast<void(*)()>(code);
AnimationHarness(expected.data(),stack.data(),stack.data());const auto stored=*reinterpret_cast<unsigned*>(code+256);assert(stored==0x11110000);
assert(MH_CreateHook(code,reinterpret_cast<void*>(&SC6InlineRngBridge0),&SC6InlineRngOriginal0)==MH_OK);
assert(MH_EnableHook(code)==MH_OK);*reinterpret_cast<unsigned*>(code+256)=0;
AnimationHarness(actual.data(),stack.data(),stack.data());assert(actual==expected && *reinterpret_cast<unsigned*>(code+256)==stored);
assert(MH_DisableHook(code)==MH_OK);assert(MH_RemoveHook(code)==MH_OK);assert(MH_Uninitialize()==MH_OK);VirtualFree(code,0,MEM_RELEASE);
VirtualFree(memory,0,MEM_RELEASE);
puts("PASS: eight RNG bridges preserve machine state; seed source registers, boundary mismatch, phase, invalid reads, wrap and drops verified");
}
