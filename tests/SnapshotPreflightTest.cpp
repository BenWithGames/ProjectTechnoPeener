#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <source_location>
#include "../src/FullStateProbe.hpp"
#include "../src/NativeBoundarySnapshot.hpp"
using namespace sc6::research;
void need(bool v,const std::source_location w=std::source_location::current()){if(!v){std::printf("preflight test failed at %u\n",w.line());std::exit(1);}}
int main(){
 auto b=reinterpret_cast<uintptr_t>(VirtualAlloc(nullptr,0x4900000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));need(b!=0);
 auto put=[](uintptr_t a,uintptr_t v){*reinterpret_cast<uintptr_t*>(a)=v;};
 auto u32=[](uintptr_t a,unsigned v){*reinterpret_cast<unsigned*>(a)=v;};
 auto method=[&](uintptr_t o,unsigned slot,unsigned rva){put(*reinterpret_cast<uintptr_t*>(o)+slot,b+rva);};
 for(unsigned side=0;side<2;++side){
  auto p=b+0x100000+side*0x100000;
  put(b+0x470DE90+8*side,p);put(b+0x470F1A0+8*side,p);
  put(p+0x35A0,b+0x300000+side*0x1000);put(p+0x27760,b+0x304000+side*0x1000);
  method(p+0x35A0,0x28,0x30B7B0);method(p+0x27760,0x28,0x30B680);
 }
 auto manager=b+0x310000,table=b+0x311000,camera=b+0x312000;
 put(b+0x470DF28,manager);put(manager,b+0x313000);method(manager,0,0x31F700);method(manager,8,0x31F480);
 put(manager+8,1);put(manager+0x90,table);put(table+0x270,camera);put(manager+0x10,camera);put(camera,b+0x314000);
 method(camera,0xE0,0x301450);method(camera,0x100,0x318860);method(camera,0x108,0x3188A0);
 method(camera,0xD0,0x318640);method(camera,0xD8,0x2D2BC0);method(camera,0xC8,0x33E640);
 auto interface=b+0x320000;put(b+0x470D188,interface);put(interface,b+0x322000);method(interface,0xC0,0x400760);
 auto delegate=interface+0xA18,list=b+0x323000;u32(delegate+0x50,4);put(delegate+0x40,list);
 put(b+0x42A1150,b+0x324000);u32(b+0x42A115C,4);
 constexpr unsigned targets[]={0x3C6050,0x3C6050,0x5112A0,0x3C6020};
 for(unsigned i=0;i<4;++i){
  auto entry=list+i*0x40,o=b+0x325000+i*0x1000,target=b+0x330000+i*0x1000;
  put(entry+0x20,o);u32(entry+0x30,1);put(o,b+0x32A000+i*0x1000);method(o,0x68,0x3EF060);
  u32(o+8,i);u32(o+12,1);put(o+0x10,b+targets[i]);
  put(b+0x324000+i*24,target);u32(b+0x324000+i*24+16,1);
  put(target,b+(i<2?0x3268078:i==2?0x32C3210:0x326A6C8));
  if(i<2){
   u32(target+0x3A0,i);u32(target+0x3B8,1);put(target+0x3B0,b+0x335000+i*0x1000);
   constexpr unsigned slots[]={0x5F8,0x600,0x680,0x698},rvas[]={0x3BF540,0x3BF8A0,0x3BE3A0,0x3BE670};
   for(unsigned j=0;j<4;++j)method(target,slots[j],rvas[j]);
   auto component=b+0x337000+i*0x1000;put(b+0x335000+i*0x1000,component);put(component,b+0x339000+i*0x1000);
   method(component,0x308,0x1D5DBE0);method(component,0x3A8,0x1DACA60);
  }
 }
 SnapshotPreflightDiagnostic d;need(FullStateProbe::preflight(b));need(FullStateProbe::preflight_detailed(b,&d));need(!d.words[2]);
 auto rejects=[&](unsigned phase){d.words={};need(!FullStateProbe::preflight_detailed(b,&d));need(d.words[0]==phase && d.words[1] && d.words[2]==1 && d.words[5]);need(!FullStateProbe::preflight(b));};
 // Player identity; callback count, weak serial and component method; camera
 // save pair versus restore pair; ownership mismatch; auxiliary method pair.
 put(b+0x470F1A0,b+0x100001);rejects(1);put(b+0x470F1A0,b+0x100000);
 u32(delegate+0x50,3);rejects(2);u32(delegate+0x50,4);
 u32(b+0x324000+16,2);rejects(2);u32(b+0x324000+16,1);
 method(b+0x337000,0x3A8,0x123);rejects(2);need(d.words[7]==b+0x337000);method(b+0x337000,0x3A8,0x1DACA60);
 method(camera,0x100,0x123);rejects(1);method(camera,0x100,0x318860);
 method(camera,0x108,0x456);rejects(3);
 bool found=false;for(unsigned i=0;i<SnapshotPreflightDiagnostic::capacity;++i){auto k=8+i*5;if(d.words[k]==2 && d.words[k+2]==b+0x456 && d.words[k+3]==b+0x3188A0)found=true;}need(found);
 method(camera,0x108,0x3188A0);
 put(manager+0x10,camera+8);rejects(3);put(manager+0x10,camera);
 auto aux=b+0x340000,obj=b+0x341000;put(manager+0xA0,aux);put(aux+0x10,obj);put(obj,b+0x342000);method(obj,0x20,0x32AC00);method(obj,0x28,0x123);rejects(3);method(obj,0x28,0x32ACE0);
 need(FullStateProbe::preflight(b));d.words={};need(FullStateProbe::preflight_detailed(b,&d));need(!d.words[2]);
 // The native callback list can gain particle attachments on a later match.
 // Exercise the real full preflight, without invoking callbacks or changing
 // any game memory, on both owners. A nonzero count is not a readiness failure.
 const auto particles=b+0x400000,attachments=b+0x380000,registry=b+0x324000;
 u32(b+0x42A115C,6);
 for(unsigned i=0;i<2;++i){
  auto object=particles+i*0x1000;
  put(object,b+CallbackAttachmentGuard::particle_vtable);method(object,0x3A8,0x1DACA60);
  put(registry+(i+4)*24,object);u32(registry+(i+4)*24+16,17+i);
  u32(attachments+i*16,4+i);u32(attachments+i*16+4,17+i);put(attachments+i*16+8,0x1234567800000123ull+i);
 }
 using D=SnapshotPreflightDiagnostic;
 for(unsigned side=0;side<2;++side){
  const auto owner=b+0x330000+side*0x1000;
  const auto evidence=D::attachment_begin+side*D::attachment_fields;
  put(owner+0x520,attachments);u32(owner+0x528,2);u32(owner+0x52C,4);
  auto accepts=[&](unsigned live,unsigned expired){
   d.words={};need(FullStateProbe::preflight_detailed(b,&d));need(FullStateProbe::preflight(b));
   need(d.words[evidence+D::attachment_count]==2 && d.words[evidence+D::attachment_capacity]==4);
   need(d.words[evidence+D::attachment_live]==live && d.words[evidence+D::attachment_expired]==expired && !d.words[evidence+D::attachment_reason]);
  };
  std::array<std::byte,0x540> before_owner;std::array<std::byte,32> before_entries;
  std::memcpy(before_owner.data(),reinterpret_cast<void*>(owner),before_owner.size());
  std::memcpy(before_entries.data(),reinterpret_cast<void*>(attachments),before_entries.size());
  accepts(2,0);
  need(!std::memcmp(before_owner.data(),reinterpret_cast<void*>(owner),before_owner.size()));
  need(!std::memcmp(before_entries.data(),reinterpret_cast<void*>(attachments),before_entries.size()));
  auto rejects_attachment=[&](unsigned reason){rejects(2);need(d.words[evidence+D::attachment_reason]==reason);};
  // Native weak resolution skips expired handles, including stale addresses.
  u32(attachments+4,0);accepts(1,1);u32(attachments+4,17);
  u32(attachments,UINT32_MAX);accepts(1,1);u32(attachments,6);accepts(1,1);u32(attachments,4);
  u32(registry+4*24+16,18);put(registry+4*24,0x10000);accepts(1,1);u32(registry+4*24+16,17);
  for(unsigned flags:{0x10000000u,0x20000000u,0x30000000u}){u32(registry+4*24+8,flags);accepts(1,1);}
  u32(registry+4*24+8,0);put(registry+4*24,0);accepts(1,1);put(registry+4*24,particles);
  // A live entry still requires the exact audited type and scale method.
  put(particles,b+0x33FFFF0);rejects_attachment(CallbackAttachmentGuard::type);
  need(d.words[evidence+D::attachment_entry]==attachments && d.words[evidence+D::attachment_object]==particles);
  put(particles,b+CallbackAttachmentGuard::particle_vtable);
  method(particles,0x3A8,0x123);rejects_attachment(CallbackAttachmentGuard::scale_method);method(particles,0x3A8,0x1DACA60);
  put(registry+4*24,8);rejects_attachment(CallbackAttachmentGuard::address);put(registry+4*24,particles);
  // The full preflight rejects this corrupt global before reaching attachments;
  // exercise the attachment helper's independent registry guard directly.
  u32(b+0x42A115C,UINT32_MAX);d.words={};
  {SnapshotPreflightTrace trace(&d,2);need(!CallbackAttachmentGuard::validate(b,owner,side,trace));}
  need(d.words[evidence+D::attachment_reason]==CallbackAttachmentGuard::registry);u32(b+0x42A115C,6);
  u32(owner+0x528,UINT32_MAX);rejects_attachment(CallbackAttachmentGuard::shape);u32(owner+0x528,2);
  u32(owner+0x52C,1);rejects_attachment(CallbackAttachmentGuard::shape);u32(owner+0x52C,4);
  put(owner+0x520,0);rejects_attachment(CallbackAttachmentGuard::storage);
  put(owner+0x520,UINTPTR_MAX-8);rejects_attachment(CallbackAttachmentGuard::storage);put(owner+0x520,attachments);
  u32(owner+0x528,CallbackAttachmentGuard::limit+1);u32(owner+0x52C,CallbackAttachmentGuard::limit+1);rejects_attachment(CallbackAttachmentGuard::shape);
  // All-null native entries are legal up to our explicit traversal budget.
  auto null_entries=b+0x500000;put(owner+0x520,null_entries);
  for(unsigned n:{1u,2u,65u,128u,CallbackAttachmentGuard::limit}){
   u32(owner+0x528,n);u32(owner+0x52C,n);d.words={};need(FullStateProbe::preflight_detailed(b,&d));
   need(d.words[evidence+D::attachment_expired]==n);
  }
  u32(owner+0x528,0);u32(owner+0x52C,4);put(owner+0x520,0x10000);need(FullStateProbe::preflight(b));
  put(owner+0x520,0);u32(owner+0x52C,0);
 }
 u32(b+0x42A115C,4);
 // Native null-terminated lists may exceed the former 64-node cap on either player.
 auto nodes=b+0x350000,nodevt=b+0x360000;put(nodes,nodevt);method(nodes,0x30,0x30EB50);method(nodes,0x38,0x30EBA0);
 for(unsigned i=0;i<128;++i){put(nodes+i*0xA0,nodevt);put(nodes+i*0xA0+0x18,i==127?0:nodes+(i+1)*0xA0);}
 for(unsigned side=0;side<2;++side)for(unsigned off:{0x400u,0x420u,0x440u}){
  auto head=b+0x100000+side*0x100000+0x44078+off;put(head,nodes);
  need(FullStateProbe::preflight(b));
  method(nodes,0x38,0x123);rejects(3);method(nodes,0x38,0x30EBA0);
  method(nodes,0x30,0x123);rejects(1);method(nodes,0x30,0x30EB50);
  put(nodes+127*0xA0+0x18,nodes+64*0xA0);rejects(1);
  bool cycle=false;for(unsigned j=0;j<SnapshotPreflightDiagnostic::capacity;++j)if(d.words[8+j*5]==5)cycle=true;need(cycle);
  put(nodes+127*0xA0+0x18,0);put(head,0);
 }
 // Snapshot path retains exception address and never invokes save/load.
 auto snap=std::make_unique<NativeBoundarySnapshot>(NativeBoundaryTypes::Mode::general_combat);
 put(b+0x470DE90,0x10000);put(b+0x470F1A0,0x10000);
 need(!snap->capture_general(b,0));need(snap->capture_stage==1 && snap->preflight_diagnostic.words[2] && snap->preflight_diagnostic.words[3]==EXCEPTION_ACCESS_VIOLATION);
 need(snap->preflight_diagnostic.words[4]==0x135A0 && snap->preflight_diagnostic.words[6]==b);
 VirtualFree(reinterpret_cast<void*>(b),0,MEM_RELEASE);
 std::puts("PASS: preflight parity; nonempty particle attachments on both owners, native expired-weak handling, bounded shape/type/method guards, read-only validation, exact rejection evidence and exception provenance; capture stops before save");
}
