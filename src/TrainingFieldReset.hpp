#pragma once
#include <Windows.h>
#include <array>
#include <cstdint>
namespace sc6::research {
// Offline diagnostic only: observations of the pinned Training setter.
struct TrainingFieldReset {
 struct Sample {uintptr_t player{};unsigned counter{},flag{};int value{};bool valid{};};
 using Samples=std::array<Sample,2>;
 Samples latest{};
 void observe(uintptr_t base,uintptr_t object,unsigned value) noexcept {
  __try {
   const auto index=*reinterpret_cast<int*>(object+16);
   if(index<0 || index>1 || (value!=0 && value!=240))return;
   const auto player=reinterpret_cast<uintptr_t*>(base+0x470DE90)[index];
   latest[index]={player,*reinterpret_cast<unsigned*>(base+0x470D0C4),*reinterpret_cast<unsigned*>(player+0x43E2C),static_cast<int>(value),true};
  }__except(EXCEPTION_EXECUTE_HANDLER){}
 }
 static bool validate(const Samples& samples,uintptr_t base,unsigned counter) noexcept {
  __try {
   if(!samples[1].valid)return false;
   for(unsigned i=0;i<2;++i){const auto& s=samples[i];if(!s.valid)continue;
    if(s.counter!=counter || (s.value!=0 && s.value!=240) || s.player!=reinterpret_cast<uintptr_t*>(base+0x470DE90)[i] ||
       s.flag!=*reinterpret_cast<unsigned*>(s.player+0x43E2C))return false;
   }
   return true;
  }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
 }
 static bool apply(const Samples& samples,uintptr_t base,unsigned counter) noexcept {
  if(!validate(samples,base,counter))return false;
  __try {
   if(*reinterpret_cast<unsigned*>(base+0x470D0C4)!=counter)return false;
   for(const auto& s:samples)if(s.valid && !s.flag)*reinterpret_cast<short*>(s.player+0x1B3C)=static_cast<short>(s.value);
   return true;
  }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
 }
};
inline TrainingFieldReset training_field_reset;
}
