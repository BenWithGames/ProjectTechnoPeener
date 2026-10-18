#include "../src/NativeInputWords.hpp"
#include "../src/PredictionInputDelivery.hpp"
#include <cassert>
#include <vector>
using W=sc6::research::NativeInputWords;
static void delivery_tests(){
 using D=sc6::research::PredictionInputDelivery<>;
 D d;std::array<D::Filter,2> filters{};std::array<uint32_t,10> auxiliary{};
 auxiliary[4]=0x12345678;auxiliary[9]=17;
 assert(d.begin(11,22,2,{0,0}));
 assert(d.append(1,std::nullopt,filters,auxiliary));
 assert(d.append(1,2,filters,auxiliary));
 assert(d.frame(1)->envelope[3]==2);
 const auto stale=d.revision();
 std::vector<std::array<uint32_t,2>> recorded;
 auto publish=[&](uint64_t session,uint64_t round,uint64_t frame,const auto& raw){
   assert(session==11 && round==22 && frame==recorded.size());recorded.push_back(raw);return true;
 };
 assert(d.publish_confirmed(publish) && recorded.empty());
 assert(d.confirm(11,22,0,2)==D::Arrival::accepted);
 // Late press on frame 0 removes the spurious press on frame 1, even
 // though frame 1's confirmed raw value never changed.
 assert(d.frame(0)->envelope[3]==2 && d.frame(1)->envelope[3]==0);
 assert(d.frame(1)->envelope[4]==auxiliary[4] && d.frame(1)->envelope[9]==17);
 assert(!d.reconciled(stale,2));
 assert(d.request_handoff() && !d.handoff_ready());
 assert(!d.append(0,0,filters,auxiliary));
 assert(d.publish_confirmed(publish) && recorded.size()==2 && !d.handoff_ready());
 assert(d.reconciled(d.revision(),2) && d.handoff_ready());
 assert(!d.begin(11,22,2,{0,0}));
 assert(d.begin(11,23,1,{0,0}));
 assert(d.confirm(11,22,0,2)==D::Arrival::wrong_identity);
 // Corrections converge to independent bit-transition calculation, across
 // both slot assignments, suppression, masks and repeated storage reuse.
 for(unsigned remote=1;remote<=2;++remote){
   D stream;assert(stream.begin(7,8,remote,{0,0}));
   std::array<uint32_t,2> previous{};uint64_t published=0;
   for(unsigned n=0;n<7200;++n){
     const uint32_t local=(n*17u)&0xFFFFu,truth=(n*31u)&0xFFFFu;
     for(unsigned p=0;p<2;++p)filters[p]={n%7==0?0x100u:0u,n%11==0,n%13==0,n%17==0};
     assert(stream.append(local,std::nullopt,filters,auxiliary));
     assert(stream.confirm(7,8,n,truth)==D::Arrival::accepted);
     const auto* frame=stream.frame(n);assert(frame);
     const std::array<uint32_t,2> raw=remote==1?std::array<uint32_t,2>{truth,local}:std::array<uint32_t,2>{local,truth};
     for(unsigned p=0;p<2;++p){const auto f=filters[p];
       const uint32_t mask=~f.disabled & (f.restrict_buttons?0xFFFFC3F0u:0xFFFFFFFFu);
       assert(frame->envelope[p*2]==(f.suppress_held?0u:raw[p]&mask));
       assert(frame->envelope[p*2+1]==(f.suppress_edges?0u:(~previous[p]&raw[p]&mask)));
     }
     assert(stream.reconciled(stream.revision(),n+1));
     assert(stream.publish_confirmed([&](uint64_t,uint64_t,uint64_t number,const auto& words){
       assert(number==published++ && words==raw);return true;
     }));previous=raw;
   }
   assert(published==7200 && stream.request_handoff() && stream.handoff_ready());
 }
 D limit;assert(limit.begin(1,2,2,{0,0}));
 for(unsigned n=0;n<3;++n)assert(limit.append(0,std::nullopt,{},{}));
 assert(!limit.append(0,std::nullopt,{},{}));
 assert(limit.confirm(1,2,2,0)==D::Arrival::accepted);
 unsigned calls=0;
 assert(limit.publish_confirmed([&](auto,auto,auto,const auto&){++calls;return true;}) && calls==0);
 assert(limit.confirm(1,2,0,0)==D::Arrival::accepted);
 assert(limit.publish_confirmed([&](auto,auto,auto,const auto&){++calls;return true;}) && calls==1);
 assert(limit.confirm(1,2,1,0)==D::Arrival::accepted);
 assert(!limit.publish_confirmed([](auto,auto,auto,const auto&){return false;}));
 assert(limit.failed() && !limit.handoff_ready());
}
int main(){
 delivery_tests();
 // Exhaust every combination of old/new state for the native 16-bit buttons.
 // Bit independence lets each of 16 bits exercise all four transitions.
 for(unsigned bit=0;bit<16;++bit)for(unsigned old=0;old<2;++old)for(unsigned next=0;next<2;++next)
 for(unsigned edges=0;edges<2;++edges)for(unsigned held=0;held<2;++held)
 for(unsigned restricted=0;restricted<2;++restricted)for(unsigned disabled=0;disabled<2;++disabled){
   auto b=1u<<bit;auto r=W::compose(old*b,next*b,disabled?b:0,edges!=0,held!=0,restricted!=0);
   bool allowed=!disabled && (!restricted || (0xFFFFC3F0u&b));
   assert(r.held==((allowed && next && !held)?b:0));
   assert(r.pressed==((allowed && next && !old && !edges)?b:0));
 }
 auto repeat=W::compose(0xFFFF,0xFFFF,0,false,false,false);
 assert(repeat.held==0xFFFF && repeat.pressed==0);
 auto release=W::compose(0xFFFF,0,0,false,false,false);
 assert(!release.held && !release.pressed);
}
