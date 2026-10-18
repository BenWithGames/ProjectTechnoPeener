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
 return 0;
}
