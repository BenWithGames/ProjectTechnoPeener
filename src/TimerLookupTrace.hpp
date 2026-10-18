#pragma once
#include "RngCallerTrace.hpp"
#include <cstring>
namespace sc6::research::timer_lookup {
// Read-only observations. Validity bits distinguish unavailable fields from zero.
struct Event {
 uint64_t sequence{},qpc{},player{},caller{},slot{},descriptor{},sample{},category_output{},weight_output{};
 std::array<uint64_t,16> registers{};
 unsigned site{},thread{},phase{},frame{},valid{},side{UINT32_MAX},rng{},character{},category{},weight{},mxcsr{},reserved{};
 std::array<std::byte,384> timers{};
 std::array<std::byte,96> controller{};
 std::array<std::byte,128> controls{};
 std::array<std::byte,84> descriptor_bytes{};
 std::array<std::byte,16> sample_bytes{};
 std::array<std::byte,96> xmm{};
};
static_assert(sizeof(Event)==1056);
struct Snapshot {unsigned schema{1},installed{},count{},dropped{};uint64_t total{};std::array<Event,4096> events{};};
inline SRWLOCK lock=SRWLOCK_INIT;
inline Snapshot trace;
inline std::atomic<unsigned> drops{};
inline uintptr_t module{};
inline unsigned word(const std::byte* p){unsigned v{};std::memcpy(&v,p,4);return v;}
inline uint64_t wide(const std::byte* p){uint64_t v{};std::memcpy(&v,p,8);return v;}
inline void record(Event e) noexcept {
 if(!TryAcquireSRWLockExclusive(&lock)){++drops;return;}
 e.sequence=trace.total++;trace.events[e.sequence%trace.events.size()]=e;
 if(trace.count<trace.events.size())++trace.count;
 ReleaseSRWLockExclusive(&lock);
}
inline void observe(const uint64_t* registers,unsigned site,const void* fx) noexcept {
 const auto error=GetLastError();Event e{};e.site=site;e.thread=GetCurrentThreadId();e.phase=rng_trace::phase;
 LARGE_INTEGER now{};QueryPerformanceCounter(&now);e.qpc=static_cast<uint64_t>(now.QuadPart);
 __try {
  if(!module || site>9 || !registers)__leave;
  std::memcpy(e.registers.data(),registers,128);
  if(fx){const auto* f=static_cast<const std::byte*>(fx);std::memcpy(e.xmm.data(),f+160,96);e.mxcsr=word(f+24);e.valid|=1;}
  e.frame=*reinterpret_cast<const unsigned*>(module+0x470D0C4);e.rng=*reinterpret_cast<const unsigned*>(module+0x485EB28);
  // Register order: rax rcx rdx rbx rsp rbp rsi rdi r8 r9 r10 r11 r12 r13 r14 r15.
  e.player=(site==0 || site==2 || site>=8)?registers[1]:(site==1?registers[1]:(site==6?registers[13]:(site==7?registers[2]:registers[11])));
  for(unsigned side=0;side<2;++side)if(e.player>=0x10000 && e.player==*reinterpret_cast<const uintptr_t*>(module+0x470DE90+side*8))e.side=side;
  if(e.side>1)__leave;
  e.valid|=2;
  std::memcpy(e.timers.data(),reinterpret_cast<const void*>(e.player+0x97268),384);
  std::memcpy(e.controller.data(),reinterpret_cast<const void*>(e.player+0x971E8),96);
  std::memcpy(e.controls.data(),reinterpret_cast<const void*>(e.player+0x97100),120);
  std::memcpy(e.controls.data()+120,reinterpret_cast<const void*>(e.player+0x43DF0),8);
  e.character=*reinterpret_cast<const unsigned short*>(e.player+0x25E);e.valid|=4;
  if(site==0 || site==2 || site==7){e.caller=*reinterpret_cast<const uintptr_t*>(registers[4]);e.valid|=8;}
  unsigned selected=3;
  if(site==0){if(registers[2]<3)selected=static_cast<unsigned>(registers[2]);}
  else if(site==1){if(registers[2]<=256 && !(registers[2]%128))selected=static_cast<unsigned>(registers[2]/128);}
  else if(site==3 || site==4){for(unsigned i=0;i<3;++i)if(registers[10]==e.player+0x97268+i*128)selected=i;}
  else for(unsigned i=0;i<3;++i)if(word(e.timers.data()+i*128+0x48)==2){selected=i;break;}
  if(selected<3){
   e.slot=e.player+0x97268+selected*128;const auto* t=e.timers.data()+selected*128;
   const auto category=word(t+0x38);const auto count=word(t+0x1C);
   const auto index=(site==0?registers[8]:site==1?registers[11]:word(t+0x3C));
   if(category<6 && index<count && count<65536){
    const uint64_t n=word(t+0x18+category*4)+index;const auto base=wide(t+8);
    if(n<65536 && base>=0x10000 && base<UINTPTR_MAX-n*84-84){e.descriptor=base+n*84;std::memcpy(e.descriptor_bytes.data(),reinterpret_cast<const void*>(e.descriptor),84);e.valid|=16;}
   }
   // Actual address used by the native lookup, not a reconstructed float-to-int conversion.
   if(site==4 && registers[8]==wide(t+0x10) && registers[9]<16*1024*1024 && registers[8]>=0x10000 && registers[8]<UINTPTR_MAX-registers[9]-16){
    e.sample=registers[8]+registers[9];std::memcpy(e.sample_bytes.data(),reinterpret_cast<const void*>(e.sample),16);e.valid|=32;
   }
  }
  if(site==2){e.category_output=registers[2];e.weight_output=registers[8];}
  if(site==4 || site==5){e.category_output=registers[7];e.weight_output=registers[3];}
  if(site==6){e.category_output=registers[4]+0x78;e.weight_output=registers[4]+0x70;}
  if((site==5 || site==6) && e.category_output>=0x10000 && e.weight_output>=0x10000){e.category=*reinterpret_cast<const unsigned*>(e.category_output);e.weight=*reinterpret_cast<const unsigned*>(e.weight_output);e.valid|=64;}
 }__except(EXCEPTION_EXECUTE_HANDLER){e.valid|=0x80000000u;}
 record(e);SetLastError(error);
}
inline void boundary(uintptr_t base,bool after) noexcept {
 __try {for(unsigned i=0;i<2;++i){std::array<uint64_t,16> r{};r[1]=*reinterpret_cast<const uintptr_t*>(base+0x470DE90+i*8);observe(r.data(),after?9u:8u,nullptr);}}
 __except(EXCEPTION_EXECUTE_HANDLER){}
}
inline void snapshot(Snapshot& out) noexcept {AcquireSRWLockShared(&lock);out=trace;out.dropped=drops.load();ReleaseSRWLockShared(&lock);}
}
