#pragma once
#include <array>
#include <cstdint>
namespace sc6 {
// Worker-owned policy; only round startup consumes a proposal. Two complete
// recent bursts must support a decrease. One isolated sample per burst may be
// above the band, but repeated spikes (two of five) keep the higher delay.
struct AdaptiveInputDelay {
 struct Burst {uint64_t time{},worst{},selection{};unsigned samples{};};
 std::array<Burst,3> history{};unsigned count{},next{},level{2};
 uint64_t worst_us{},selection_us{};unsigned complete_recent{},reason{1};
 static unsigned band(uint64_t rtt){return rtt<25000?0u:(rtt<=70000?1u:2u);}
 void observe(uint64_t now,unsigned samples,uint64_t worst,uint64_t selection){
  history[next]={now,worst,selection,samples};next=(next+1)%3;if(count<3)++count;
  worst_us=0;for(unsigned i=0;i<count;++i)if(history[i].worst>worst_us)worst_us=history[i].worst;
  selection_us=selection;complete_recent=0;reason=0;
  for(unsigned age=0;age<2 && age<count;++age){const auto& b=history[(next+2-age)%3];
   if(b.samples!=5 || b.selection>b.worst){reason=2;break;}
   if(now<b.time || now-b.time>6000000){reason=3;break;}
   if(age && b.time>=history[(next+3-age)%3].time){reason=3;break;}
   ++complete_recent;
  }
  if(complete_recent<2){level=2;if(!reason)reason=1;return;}
  const auto& previous=history[(next+1)%3];
  if(previous.selection>selection_us)selection_us=previous.selection;
  level=band(selection_us);
 }
 void observe(uint64_t now,unsigned samples,uint64_t worst){observe(now,samples,worst,worst);}
 unsigned proposal(uint64_t now)const{
  if(count<2 || complete_recent<2)return 2;
  const auto& last=history[(next+2)%3];const auto& previous=history[(next+1)%3];
  return now>=last.time && now>=previous.time && now-last.time<=6000000 && now-previous.time<=9000000?level:2;
 }
};
}
