#include <Windows.h>
#include <cstddef>
#include <memory>
#include <cassert>
#include <iostream>
#include "../src/TransformSnapshot.hpp"
using T=sc6::research::TransformSnapshot;
int main(){
 // Collisions, full-table termination, duplicates, and same address on opposite sides.
 T::AddressIndex<8> index;
 uintptr_t keys[8]{};unsigned found=0;
 for(uintptr_t x=0x10000;found<8;x+=8)if(T::AddressIndex<8>::bucket(x,0)==0)keys[found++]=x;
 for(unsigned i=0;i<8;++i)assert(index.insert(keys[i],0,i));
 for(unsigned i=0;i<8;++i)assert(index.find(keys[i])==i);
 assert(!index.insert(keys[0],0,99));assert(!index.insert(0xDEADBEEF,0,99));
 assert(index.find(0xDEADBEEF)==UINT32_MAX && index.find(0)==UINT32_MAX);
 T::AddressIndex<8> sides;assert(sides.insert(keys[0],0,4)&&sides.insert(keys[0],1,5));
 assert(sides.find(keys[0],0)==4 && sides.find(keys[0],1)==5);

 auto mem=VirtualAlloc(nullptr,0x4860000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(mem);
 auto base=reinterpret_cast<uintptr_t>(mem);
 uintptr_t owners[]={base+0x1000,base+0x2000};auto group=base+0x3000,node=base+0x4000,hit=base+0x5000;
 *reinterpret_cast<uintptr_t*>(owners[0]+0x10)=group;*reinterpret_cast<uintptr_t*>(group+0x48)=node;
 *reinterpret_cast<uintptr_t*>(node)=base+0x3E88DB8;
 *reinterpret_cast<uintptr_t*>(owners[0]+8)=hit;*reinterpret_cast<uintptr_t*>(hit)=base+0x3E88E38;
 auto a=std::make_unique<T>(),b=std::make_unique<T>(),c=std::make_unique<T>();
 auto state=[&](unsigned n,unsigned stamp){
  *reinterpret_cast<short*>(node+0xDA)=static_cast<short>(n);
  for(unsigned j=0;j<4;++j){std::memset(reinterpret_cast<void*>(node+0x120+j*0x60),0x20+stamp,0x50);
   *reinterpret_cast<uintptr_t*>(node+0x170+j*0x60)=j<n?hit:uintptr_t(1);}
 };
 for(unsigned from=0;from<=4;++from)for(unsigned to=0;to<=4;++to){
  state(from,from);assert(a->capture(owners,base,true));state(to,to+10);assert(b->capture(owners,base,true));assert(c->capture(owners,base,true));
  assert(a->same_topology(*b));assert(a->restore_sway(*b,*c,base));assert(c->capture(owners,base,true));
  assert(a->restore_constraints(*b,*c,base,true));assert(*reinterpret_cast<short*>(node+0xDA)==static_cast<short>(to));
  assert(a->restore_constraints(*b,*c,base));assert(*reinterpret_cast<short*>(node+0xDA)==static_cast<short>(from));
  assert(c->capture(owners,base,true));assert(a->same_topology(*c));
  for(unsigned i=0;i<a->count;++i)assert(!std::memcmp(a->records[i].bytes.data(),c->records[i].bytes.data(),a->records[i].size));
 }
 // D78 shares the base solver, but also retains owner control references.
 // Exercise history/position changes, both sides, nonzero parent and contact counts.
 for(unsigned side=0;side<2;++side){
  *reinterpret_cast<uintptr_t*>(owners[0]+0x10)=0;
  *reinterpret_cast<uintptr_t*>(owners[side]+0x10)=group;
  *reinterpret_cast<uintptr_t*>(owners[0]+8)=0;
  *reinterpret_cast<uintptr_t*>(owners[side]+8)=hit;
  *reinterpret_cast<uintptr_t*>(node)=base+0x3E88D78;
  *reinterpret_cast<uintptr_t*>(node+0x2B0)=owners[side]+0x2C;
  *reinterpret_cast<uintptr_t*>(node+0x2B8)=owners[side]+0x38;
  *reinterpret_cast<unsigned*>(owners[side]+0x2C)=1;
  *reinterpret_cast<float*>(owners[side]+0x38)=0.5f;
  *reinterpret_cast<float*>(node+0x2A0)=-0.25f;
  const auto parent=base+0x6000;
  *reinterpret_cast<uintptr_t*>(node+0x28)=parent;
  *reinterpret_cast<uintptr_t*>(node+0x50)=parent;
  *reinterpret_cast<uintptr_t*>(parent)=base+0x3E88DB8;
  for(unsigned from=0;from<=4;++from)for(unsigned to=0;to<=4;++to){
   state(from,from);
   for(unsigned o=0x80;o<0xC0;o+=4)*reinterpret_cast<float*>(node+o)=float(o+from);
   *reinterpret_cast<float*>(node+0x10)=0.011394858f;
   assert(a->capture(owners,base,true));
   state(to,to+10);
   for(unsigned o=0x80;o<0xC0;o+=4)*reinterpret_cast<float*>(node+o)=float(o+to+10);
   *reinterpret_cast<float*>(node+0x10)=0.005053222f;
   assert(b->capture(owners,base,true)&&c->capture(owners,base,true));
   assert(a->restore_sway(*b,*c,base,true)&&a->restore_constraints(*b,*c,base,true));
   assert(*reinterpret_cast<float*>(node+0x10)==0.005053222f);
   assert(a->restore_sway(*b,*c,base));assert(c->capture(owners,base,true));
   assert(a->restore_constraints(*b,*c,base));assert(c->capture(owners,base,true));
   for(unsigned i=0;i<a->count;++i)assert(!std::memcmp(a->records[i].bytes.data(),c->records[i].bytes.data(),a->records[i].size));
   assert(c->refresh_intermediate(*a,owners,base));assert(c->unchanged_live(owners,base));
  }
  assert(a->capture(owners,base,true));
  unsigned parent_record=UINT32_MAX;
  for(unsigned i=0;i<a->count;++i)if(a->records[i].address==parent && a->records[i].kind==2)parent_record=i;
  assert(parent_record!=UINT32_MAX);
  a->records[parent_record].side=1-side;assert(!a->d78_dependencies(base));
  a->records[parent_record].side=side;assert(a->d78_dependencies(base));
  a->records[parent_record].size=0x78;assert(!a->d78_dependencies(base));
  a->records[parent_record].size=0x80;assert(a->d78_dependencies(base));
  // Configuration stays immutable; invalid control encodings still reject.
  assert(a->capture(owners,base,true));
  *reinterpret_cast<float*>(node+0x2A0)=-0.5f;
  *reinterpret_cast<float*>(node+0x10)=10.0f;
  assert(b->capture(owners,base,true)&&c->capture(owners,base,true));
  assert(!a->restore_sway(*b,*c,base));assert(*reinterpret_cast<float*>(node+0x10)==10.0f);
  *reinterpret_cast<float*>(node+0x2A0)=-0.25f;
  for(unsigned offset:{0x2Cu,0x38u}){
   assert(a->capture(owners,base,true));
   auto& value=*reinterpret_cast<unsigned*>(owners[side]+offset);auto saved=value;value=offset==0x2C?3u:0x7FC00000u;
   assert(b->capture(owners,base,true)&&c->capture(owners,base,true));
   assert(!a->restore_sway(*b,*c,base));value=saved;
  }
  // Native 308310 modes and changing blend values, using production restore order.
  for(unsigned from=0;from<3;++from)for(unsigned to=0;to<3;++to)for(float blend:{0.0f,0.125f,0.5f,0.99f,1.0f}){
   *reinterpret_cast<unsigned*>(owners[side]+0x2C)=from;
   *reinterpret_cast<float*>(owners[side]+0x38)=blend;
   *reinterpret_cast<float*>(node+0x10)=1.0f;state(1,1);
   assert(a->capture(owners,base,true));
   *reinterpret_cast<unsigned*>(owners[side]+0x2C)=to;
   *reinterpret_cast<float*>(owners[side]+0x38)=blend*0.9f;
   *reinterpret_cast<float*>(node+0x10)=2.0f;state(2,2);
   assert(b->capture(owners,base,true)&&c->capture(owners,base,true));
   assert(a->restore_root_wind(*b,*c,true)&&a->restore_sway(*b,*c,base,true));
   assert(*reinterpret_cast<float*>(owners[side]+0x38)==blend*0.9f);
   assert(a->restore_root_wind(*b,*c));assert(c->capture(owners,base,true));
   assert(a->restore_sway(*b,*c,base));assert(c->capture(owners,base,true));
   assert(a->restore_constraints(*b,*c,base));assert(c->capture(owners,base,true));
   for(unsigned i=0;i<a->count;++i)assert(!std::memcmp(a->records[i].bytes.data(),c->records[i].bytes.data(),a->records[i].size));
  }
  // A third post-load control value is not silently accepted; all writes stay blocked.
  *reinterpret_cast<float*>(owners[side]+0x38)=0.25f;assert(a->capture(owners,base,true));
  *reinterpret_cast<float*>(owners[side]+0x38)=0.5f;assert(b->capture(owners,base,true));
  *reinterpret_cast<float*>(owners[side]+0x38)=0.75f;assert(c->capture(owners,base,true));
  assert(!a->restore_root_wind(*b,*c)&&!a->restore_sway(*b,*c,base));
  assert(a->sway_check.control_offset==0x38 && a->sway_check.control_saved==0x3E800000 &&
         a->sway_check.control_next==0x3F000000 && a->sway_check.control_current==0x3F400000);
  // A live write after capture, including at the audited control, also rejects.
  assert(b->capture(owners,base,true)&&c->capture(owners,base,true));
  *reinterpret_cast<float*>(owners[side]+0x38)=0.9f;
  assert(!a->restore_root_wind(*b,*c)&&!a->restore_sway(*b,*c,base));
  *reinterpret_cast<float*>(owners[side]+0x38)=0.5f;assert(a->capture(owners,base,true));
  *reinterpret_cast<unsigned*>(owners[side]+0x34)=1;
  assert(b->capture(owners,base,true)&&c->capture(owners,base,true));
  assert(!a->restore_root_wind(*b,*c));*reinterpret_cast<unsigned*>(owners[side]+0x34)=0;
  // Reference rebinding and missing parent coverage fail capture, before writes.
  *reinterpret_cast<uintptr_t*>(node+0x2B0)=owners[side]+0x30;
  assert(!c->capture(owners,base,true));
  *reinterpret_cast<uintptr_t*>(node+0x2B0)=owners[side]+0x2C;
  *reinterpret_cast<uintptr_t*>(node+0x50)=base+0x7000;
  assert(!c->capture(owners,base,true));
  *reinterpret_cast<uintptr_t*>(node+0x50)=0;
  *reinterpret_cast<uintptr_t*>(node+0x28)=0;
  *reinterpret_cast<uintptr_t*>(owners[side]+0x10)=0;
  *reinterpret_cast<uintptr_t*>(owners[side]+8)=0;
 }
 *reinterpret_cast<uintptr_t*>(owners[0]+0x10)=group;
 *reinterpret_cast<uintptr_t*>(owners[0]+8)=hit;
 // Audited native branch bytes change independently of object identity.
 for(auto type:{uintptr_t(0x3E88DB8),uintptr_t(0x3E88DF8)})for(unsigned from=0;from<4;++from)for(unsigned to=0;to<4;++to){
  *reinterpret_cast<uintptr_t*>(node)=base+type;
  state(1,1);*reinterpret_cast<unsigned char*>(node+0x7C)=static_cast<unsigned char>(from);*reinterpret_cast<unsigned char*>(node+0x7D)=static_cast<unsigned char>(3-from);
  assert(a->capture(owners,base,true));
  state(2,2);*reinterpret_cast<unsigned char*>(node+0x7C)=static_cast<unsigned char>(to);*reinterpret_cast<unsigned char*>(node+0x7D)=static_cast<unsigned char>(3-to);
  assert(b->capture(owners,base,true)&&c->capture(owners,base,true));
  assert(a->restore_sway(*b,*c,base,true)&&a->restore_constraints(*b,*c,base,true));
  assert(*reinterpret_cast<unsigned char*>(node+0x7C)==to);
  assert(a->restore_sway(*b,*c,base));assert(c->capture(owners,base,true));
  assert(a->restore_constraints(*b,*c,base));assert(c->capture(owners,base,true));
  for(unsigned i=0;i<a->count;++i)assert(!std::memcmp(a->records[i].bytes.data(),c->records[i].bytes.data(),a->records[i].size));
 }
 *reinterpret_cast<uintptr_t*>(node)=base+0x3E88DB8;
 state(1,1);assert(a->capture(owners,base,true));
 // XYZ belongs to captured position state on the two audited sway types.
 for(auto type:{uintptr_t(0x3E88DB8),uintptr_t(0x3E88DF8)}){
  *reinterpret_cast<uintptr_t*>(node)=base+type;
  float saved[3]={-10.25f,2.5f,99.0f},later[3]={12.0f,-3.0f,0.0f};
  std::memcpy(reinterpret_cast<void*>(node+0x10),saved,12);assert(a->capture(owners,base,true));
  std::memcpy(reinterpret_cast<void*>(node+0x10),later,12);assert(b->capture(owners,base,true)&&c->capture(owners,base,true));
  assert(a->restore_sway(*b,*c,base,true)&&a->restore_constraints(*b,*c,base,true));
  assert(!std::memcmp(reinterpret_cast<void*>(node+0x10),later,12));
  *reinterpret_cast<float*>(node+0x10)=88.0f;assert(!a->restore_sway(*b,*c,base));
  assert(*reinterpret_cast<float*>(node+0x10)==88.0f);
  std::memcpy(reinterpret_cast<void*>(node+0x10),later,12);
  assert(a->restore_sway(*b,*c,base));assert(!std::memcmp(reinterpret_cast<void*>(node+0x10),saved,12));
  assert(c->capture(owners,base,true));assert(a->restore_constraints(*b,*c,base));
  assert(c->capture(owners,base,true));
  for(unsigned i=0;i<a->count;++i)assert(!std::memcmp(a->records[i].bytes.data(),c->records[i].bytes.data(),a->records[i].size));
 }
 state(1,1);assert(a->capture(owners,base,true));
 // Neighboring unknown fields remain immutable; report the exact byte, no writes.
 *reinterpret_cast<unsigned char*>(node+0x7E)=1;
 assert(b->capture(owners,base,true)&&c->capture(owners,base,true));
 assert(!a->restore_sway(*b,*c,base));assert(a->sway_check.offset==0x7E && a->sway_check.actual==1);
 assert(!a->restore_constraints(*b,*c,base));assert(a->constraint_check.offset==0x7E);
 *reinterpret_cast<unsigned char*>(node+0x7E)=0;
 assert(b->capture(owners,base,true)&&c->capture(owners,base,true));
 // Live mutation is not ignored even for a restorable byte.
 *reinterpret_cast<unsigned char*>(node+0x7C)=255;
 assert(!a->restore_sway(*b,*c,base));assert(a->sway_check.offset==0x7C);
 *reinterpret_cast<unsigned char*>(node+0x7C)=3;
 // Unrecognized node types do not get the exception.
 auto x=a->records[0],y=x; x.kind=y.kind=2;x.size=y.size=0x80;x.side=y.side=0;
 *reinterpret_cast<uintptr_t*>(x.bytes.data())=*reinterpret_cast<uintptr_t*>(y.bytes.data())=base+0x1234;
 y.bytes[0x7C]=std::byte{7};T::RestoreCheckDiagnostic diagnostic;
 assert(T::compare_sway_header(x,y,base,diagnostic) && diagnostic.offset==0x7C);
 y=x;y.bytes[0x10]=std::byte{7};diagnostic={};assert(T::compare_sway_header(x,y,base,diagnostic)&&diagnostic.offset==0x10);
 // Native finalize updates each link distance independently of stable topology.
 for(unsigned links=1;links<=4;++links){
  *reinterpret_cast<short*>(node+0xD8)=static_cast<short>(links);
  for(unsigned j=0;j<links;++j){*reinterpret_cast<uintptr_t*>(node+0xE0+j*16)=node;
   *reinterpret_cast<float*>(node+0xE8+j*16)=float(j+1);}
  state(1,1);assert(a->capture(owners,base,true));
  for(unsigned j=0;j<links;++j)*reinterpret_cast<float*>(node+0xE8+j*16)=float(j+10);
  state(2,2);assert(b->capture(owners,base,true)&&c->capture(owners,base,true));
  assert(a->restore_constraints(*b,*c,base,true));
  assert(*reinterpret_cast<float*>(node+0xE8)==10.0f);
  // A later live change rejects before any saved distance is published.
  *reinterpret_cast<float*>(node+0xE8+(links-1)*16)=99.0f;
  assert(!a->restore_constraints(*b,*c,base));
  assert(*reinterpret_cast<float*>(node+0xE8+(links-1)*16)==99.0f);
  *reinterpret_cast<float*>(node+0xE8+(links-1)*16)=float(links+9);
  assert(a->restore_constraints(*b,*c,base));
  for(unsigned j=0;j<links;++j)assert(*reinterpret_cast<float*>(node+0xE8+j*16)==float(j+1));
  // A changed target is never accepted as a scalar change.
  *reinterpret_cast<uintptr_t*>(node+0xE0)=node+16;
  assert(b->capture(owners,base,true)&&c->capture(owners,base,true));
  assert(!a->restore_constraints(*b,*c,base));
 }
 *reinterpret_cast<short*>(node+0xD8)=0;
 state(1,1);assert(a->capture(owners,base,true));state(2,2);assert(b->capture(owners,base,true));assert(c->capture(owners,base,true));
 // A late table mutation must reject without overwriting the first slot/count.
 *reinterpret_cast<unsigned*>(node+0x240)=123;assert(!a->restore_constraints(*b,*c,base));
 assert(*reinterpret_cast<unsigned*>(node+0x120)==0x22222222 && *reinterpret_cast<short*>(node+0xDA)==2);
 state(2,2);assert(c->capture(owners,base,true));
 // Removed owner list / changed type / corrupt count are never reclassified as activation.
 *reinterpret_cast<uintptr_t*>(owners[0]+8)=0;assert(!c->capture(owners,base,true) && c->failure==14);
 *reinterpret_cast<uintptr_t*>(owners[0]+8)=hit;*reinterpret_cast<uintptr_t*>(hit)=base+0x123;
 assert(!c->capture(owners,base,true) && c->failure==14);
 *reinterpret_cast<uintptr_t*>(hit)=base+0x3E88E38;*reinterpret_cast<short*>(node+0xDA)=5;
 assert(!c->capture(owners,base,true) && c->failure==12);
 *reinterpret_cast<short*>(node+0xDA)=-1;assert(!c->capture(owners,base,true) && c->failure==12);
 state(2,2);*reinterpret_cast<uintptr_t*>(hit+0x28)=hit;assert(!c->capture(owners,base,true) && c->failure==13);
 *reinterpret_cast<uintptr_t*>(hit+0x28)=0;assert(c->capture(owners,base,true));
 // Rebuild the node index every capture: a new cycle must reject, then recover.
 *reinterpret_cast<uintptr_t*>(node+0x28)=node;assert(!c->capture(owners,base,true)&&c->failure==5);
 *reinterpret_cast<uintptr_t*>(node+0x28)=0;assert(c->capture(owners,base,true));
 // Capture indexes retain cycle detection and cross-list duplicate rejection.
 *reinterpret_cast<uintptr_t*>(group+0x40)=group;
 assert(!c->capture(owners,base,true)&&c->failure==3);
 *reinterpret_cast<uintptr_t*>(group+0x40)=0;
 *reinterpret_cast<uintptr_t*>(hit+0x28)=hit;
 assert(!c->capture(owners,base,true)&&c->failure==13);
 *reinterpret_cast<uintptr_t*>(hit+0x28)=0;
 *reinterpret_cast<uintptr_t*>(owners[0]+0x18)=hit;
 assert(!c->capture(owners,base,true)&&c->failure==13);
 *reinterpret_cast<uintptr_t*>(owners[0]+0x18)=0;
 assert(c->capture(owners,base,true));
 // Dependency index must retain side ownership and child metadata validation.
 auto owner_record=0u;
 for(unsigned i=0;i<c->count;++i)if(c->records[i].kind==26){owner_record=i;break;}
 c->records[owner_record].side=1;assert(!c->constraint_dependencies(base));
 c->records[owner_record].side=0;c->records[owner_record+1].parent=0;assert(!c->constraint_dependencies(base));
 assert(c->capture(owners,base,true)&&c->constraint_dependencies(base));
 auto saved=c->records[0];c->fixed_constraint_slots=false;assert(!a->same_topology(*c));c->fixed_constraint_slots=true;
 // Validate all active dependencies before any write, even when metadata looks stable.
 for(unsigned i=0;i<a->count;++i)if(a->records[i].kind==25){*reinterpret_cast<uintptr_t*>(a->records[i].bytes.data()+0x50)=hit+0x100;break;}
 assert(!a->restore_constraints(*b,*c,base));assert(*reinterpret_cast<short*>(node+0xDA)==2);
 assert(c->records[0].address==saved.address);
 // Refresh every published value and compare against independently enumerated captures.
 for(unsigned contacts=0;contacts<=4;++contacts){
  state(0,0);assert(a->capture(owners,base,true)&&a->separate_effects());
  state(contacts,contacts+4);*reinterpret_cast<float*>(group)=float(contacts);
  assert(b->refresh_intermediate(*a,owners,base));
  assert(c->capture(owners,base,true)&&c->separate_effects()&&b->same_topology(*c));
  for(unsigned i=0;i<c->count;++i)assert(!std::memcmp(b->records[i].bytes.data(),c->records[i].bytes.data(),c->records[i].size));
 }
 // Every traversal input is checked before stale descendants are followed.
 const uintptr_t topology_addresses[]={owners[0]+8,owners[0]+0x10,group+0x40,group+0x48,node,node+0x28,node+0xD8,hit,hit+0x28,base+0x470E038};
 for(auto address:topology_addresses){
  assert(a->capture(owners,base,true)&&a->separate_effects());
  uint64_t original{};std::memcpy(&original,reinterpret_cast<void*>(address),8);
  uint64_t changed=1;std::memcpy(reinterpret_cast<void*>(address),&changed,8);
  assert(!b->refresh_intermediate(*a,owners,base)&&!b->valid);
  std::memcpy(reinterpret_cast<void*>(address),&original,8);
 }
 assert(a->capture(owners,base,true)&&a->separate_effects());
 *reinterpret_cast<uintptr_t*>(node+0x170)=hit+0x100;
 assert(!b->refresh_intermediate(*a,owners,base)&&b->failure==17);
 *reinterpret_cast<uintptr_t*>(node+0x170)=hit;
 auto wrong=owners[0];owners[0]=owners[1];assert(!b->refresh_intermediate(*a,owners,base));owners[0]=wrong;
 a->valid=false;assert(!b->refresh_intermediate(*a,owners,base));
 VirtualFree(mem,0,MEM_RELEASE);std::cout<<"Constraint slot transitions (25), dependencies, no-partial-write and bounds passed\n";
}
