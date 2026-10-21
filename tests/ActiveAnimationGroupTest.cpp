#include "../src/ActiveAnimationGroup.hpp"
#include <memory>
#include <vector>
#include <cassert>
using namespace sc6::research;
int main(){
 auto s=std::make_unique<FullRestoreStream>();std::vector<std::byte> a(0x98000),b(0x98000);
 uintptr_t players[]={reinterpret_cast<uintptr_t>(a.data()),reinterpret_cast<uintptr_t>(b.data())};constexpr uintptr_t base=0x140000000;
 s->sealed=true;s->writes=4;uint64_t at=0;
 for(unsigned k=0;k<2;++k){s->lengths[k*2]=0x3510;s->traces[k*2]={players[k]+0x90,0,base+0x30A9CE,0};
 int16_t slot=2;uint32_t w=0x3e99999a;std::memcpy(s->stream.payload.data()+at+0x2020-0x90,&slot,2);std::memcpy(s->stream.payload.data()+at+0x2030-0x90,&w,4);
 std::memcpy(reinterpret_cast<void*>(players[k]+0x2020),&slot,2);std::memcpy(reinterpret_cast<void*>(players[k]+0x2030),&w,4);at+=0x3510;
 s->lengths[k*2+1]=0x11f0;s->traces[k*2+1]={players[k]+0x95fa0,0,base+0x30aa96,base+0x30afd8};uint64_t mask=0x7fff;std::memcpy(s->stream.payload.data()+at+0x1118,&mask,8);
 for(unsigned j=0;j<15;++j)std::memcpy(s->stream.payload.data()+at+0x1120+j*4,&w,4);at+=0x11f0;}
 s->stream.write_position=at;ActiveAnimationGroup::Diagnostic d;
 assert(ActiveAnimationGroup::restore(*s,players,base,d));assert(d.restored==2 && d.saved==d.verified);
 assert(ActiveAnimationGroup::restore(*s,players,base,d));
 std::memset(a.data()+0x970b8,0,68);std::memset(b.data()+0x970b8,0,68);
 b[0x2030]=std::byte{0};assert(!ActiveAnimationGroup::restore(*s,players,base,d) && d.reason==6);assert(a[0x970b8]==std::byte{});
 b[0x2030]=std::byte{0x9a};b[0x970b8]=std::byte{1};assert(!ActiveAnimationGroup::restore(*s,players,base,d) && d.reason==7);b[0x970b8]=std::byte{};
 s->traces[1].reader=0;assert(!ActiveAnimationGroup::restore(*s,players,base,d) && d.reason==2);s->traces[1].reader=base+0x30afd8;
 auto pos=0x3510+0x1120;s->stream.payload[pos]=std::byte{};assert(!ActiveAnimationGroup::restore(*s,players,base,d) && d.reason==5);
 // All combinations of the two audited secondary groups on both players.
 auto fixture=[&](unsigned enabled){
  std::fill(a.begin(),a.end(),std::byte{});std::fill(b.begin(),b.end(),std::byte{});
  s->stream.payload.fill(std::byte{});
  for(unsigned side=0;side<2;++side)for(unsigned group=0;group<2;++group){
   const unsigned off=group?0x1058:0x1118,slotoff=group?0x2064:0x2020,weightoff=group?0x2074:0x2030;
   const size_t prefix=side*(0x3510+0x11f0),record=prefix+0x3510;
   int16_t slot=short(2+group);uint32_t weight=group?0x3e4ccccd:0x3e99999a;
   std::memcpy(s->stream.payload.data()+prefix+slotoff-0x90,&slot,2);
   std::memcpy(s->stream.payload.data()+prefix+weightoff-0x90,&weight,4);
   std::memcpy(reinterpret_cast<void*>(players[side]+slotoff),&slot,2);
   std::memcpy(reinterpret_cast<void*>(players[side]+weightoff),&weight,4);
   if(enabled&(1u<<(side*2+group))){uint64_t mask=0x7fff;
    std::memcpy(s->stream.payload.data()+record+off,&mask,8);
    for(unsigned j=0;j<15;++j)std::memcpy(s->stream.payload.data()+record+off+8+j*4,&weight,4);
   }
  }
 };
 for(unsigned enabled=0;enabled<16;++enabled){
  fixture(enabled);assert(ActiveAnimationGroup::restore(*s,players,base,d));
  unsigned count=0;for(unsigned bit=0;bit<4;++bit)count+=(enabled>>bit)&1;
  assert(d.restored==count && d.saved==d.verified && d.secondary_saved==d.secondary_verified);
  if(enabled==15){unsigned differences=0;for(unsigned i=0;i<68;++i)differences+=d.secondary_saved[0][i]!=d.secondary_loaded[0][i];assert(differences==62);}

  for(unsigned side=0;side<2;++side)for(unsigned off:{0x1118u,0x1058u})
   assert(!std::memcmp(reinterpret_cast<void*>(players[side]+0x95fa0+off),s->stream.payload.data()+side*(0x3510+0x11f0)+0x3510+off,68));
  assert(ActiveAnimationGroup::restore(*s,players,base,d));
 }
 // Any second-group rejection leaves the first group and both players untouched.
 for(unsigned bad=0;bad<5;++bad){fixture(15);
  auto* saved=s->stream.payload.data()+(0x3510+0x11f0)+0x3510+0x1058;
  if(bad==0)saved[0]=std::byte{1};
  if(bad==1)saved[8]=std::byte{1};
  if(bad==2)b[0x2074]=std::byte{1};
  if(bad==3)b[0x96ff8]=std::byte{1};
  if(bad==4){int16_t slot=-1;std::memcpy(s->stream.payload.data()+(0x3510+0x11f0)+0x2064-0x90,&slot,2);}
  assert(!ActiveAnimationGroup::restore(*s,players,base,d));
  assert(d.group==1 && d.side==1 && !d.restored && a[0x970b8]==std::byte{} && a[0x96ff8]==std::byte{});
 }
 return 0;
}
