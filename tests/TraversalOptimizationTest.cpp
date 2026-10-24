#include <Windows.h>
#include <memory>
#include <cassert>
#include <cstdio>
#include "../src/TransformSnapshot.hpp"
using T=sc6::research::TransformSnapshot;
struct Reference:T {
    bool legacy_d78(uintptr_t base)const noexcept {
        if(count>records.size())return false;
        for(unsigned i=0;i<count;++i){const auto& n=records[i];
            if(n.kind!=2 || pointer(n.bytes.data())!=base+0x3E88D78)continue;
            if(n.size!=0x80 || n.side>1 || n.parent>=count)return false;
            const auto& g=records[n.parent];
            if(g.kind!=1 || g.side!=n.side || g.parent>=count)return false;
            const auto& root=records[g.parent];
            if(root.kind!=0 || root.size!=512 || root.side!=n.side)return false;
            unsigned suffixes=0;
            for(unsigned j=i+1;j<count && records[j].kind!=2 && records[j].kind!=1 && records[j].kind!=0;++j){
                const auto& r=records[j];if(r.kind!=28 || r.parent!=i)continue;
                if(r.side!=n.side || r.size!=0x20 || r.address!=n.address+0x2A0 ||
                   pointer(r.bytes.data()+0x10)!=root.address+0x2C ||
                   pointer(r.bytes.data()+0x18)!=root.address+0x38)return false;
                ++suffixes;
            }
            if(suffixes!=1)return false;
            const auto parent=pointer(n.bytes.data()+0x50);
            if(parent){bool found=false;
                for(unsigned j=0;j<count;++j){const auto& p=records[j];
                    if(p.kind==2 && p.address==parent && p.side==n.side && sway_state_header(p,base)){found=true;break;}}
                if(!found)return false;
            }
        }
        return true;
    }
};
int main(){
 auto x=std::make_unique<Reference>();constexpr uintptr_t base=0x140000000;
 x->count=2;x->records[0].kind=0;x->records[0].size=512;x->records[0].address=0x100000;
 x->records[1].kind=1;x->records[1].parent=0;
 auto put=[](T::Record& r,unsigned offset,uintptr_t value){std::memcpy(r.bytes.data()+offset,&value,8);};
 for(unsigned i=0;i<512;++i){auto n=x->count++;auto& r=x->records[n];r.kind=2;r.size=0x80;r.parent=1;r.address=0x200000+i*0x400;
  put(r,0,base+0x3E88D78);put(r,0x50,0x200000+511*0x400);
  auto& suffix=x->records[x->count++];suffix.kind=28;suffix.parent=n;suffix.size=0x20;suffix.address=r.address+0x2A0;
  put(suffix,0x10,0x10002C);put(suffix,0x18,0x100038);
 }
 assert(x->legacy_d78(base)&&x->d78_dependencies(base));
 // Every parent identity, type and side change must match the original predicate.
 for(unsigned i=2;i<x->count;i+=2){auto saved=x->records[i];
  for(unsigned mode=0;mode<4;++mode){x->records[i]=saved;
   if(mode==0)put(x->records[i],0x50,0xDEADBEEF);
   if(mode==1)x->records[i].side=1;
   if(mode==2)x->records[i].size=0x78;
   if(mode==3)x->records[i].parent=x->count;
   assert(x->legacy_d78(base)==x->d78_dependencies(base));
  }x->records[i]=saved;
 }
 auto memory=VirtualAlloc(nullptr,0x4860000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(memory);
 const auto livebase=reinterpret_cast<uintptr_t>(memory);uintptr_t owners[]={livebase+0x1000,livebase+0x2000};
 auto liveput=[](uintptr_t a,uintptr_t v){*reinterpret_cast<uintptr_t*>(a)=v;};
 auto capture=std::make_unique<T>();
 for(unsigned n:{0u,8u,16u,17u,64u,256u,512u}){
  auto first=livebase+0x10000;liveput(owners[0]+8,n?first:0);
  for(unsigned i=0;i<n;++i){auto target=first+i*0x100;liveput(target,livebase+0x3E88E38);liveput(target+0x28,i+1<n?target+0x100:0);}
  assert(capture->capture(owners,livebase,true));
  if(n){liveput(first+(n-1)*0x100+0x28,first);assert(!capture->capture(owners,livebase,true)&&capture->failure==13);
   liveput(first+(n-1)*0x100+0x28,0);liveput(owners[0]+0x18,first);
   assert(!capture->capture(owners,livebase,true)&&capture->failure==13);liveput(owners[0]+0x18,0);}
 }
 liveput(owners[0]+8,0);
 for(unsigned n:{8u,9u,64u,65u}){
  auto first=livebase+0x40000;liveput(owners[0]+0x10,first);
  for(unsigned i=0;i<n;++i)liveput(first+i*0x100+0x40,i+1<n?first+(i+1)*0x100:0);
  if(n>64){assert(!capture->capture(owners,livebase,true)&&capture->failure==4);continue;}
  assert(capture->capture(owners,livebase,true));
  liveput(first+(n-1)*0x100+0x40,first);assert(!capture->capture(owners,livebase,true)&&capture->failure==3);
  liveput(first+(n-1)*0x100+0x40,0);
 }
 VirtualFree(memory,0,MEM_RELEASE);
 LARGE_INTEGER f{},a{},b{};QueryPerformanceFrequency(&f);volatile unsigned sink=0;
 constexpr unsigned runs=200;
 QueryPerformanceCounter(&a);for(unsigned i=0;i<runs;++i)sink=x->legacy_d78(base);QueryPerformanceCounter(&b);
 auto before=(b.QuadPart-a.QuadPart)*1e6/f.QuadPart/runs;
 QueryPerformanceCounter(&a);for(unsigned i=0;i<runs;++i)sink=x->d78_dependencies(base);QueryPerformanceCounter(&b);
 auto after=(b.QuadPart-a.QuadPart)*1e6/f.QuadPart/runs;
 std::printf("PASS: 2048 dependency mutations match original; 512-parent synthetic validation %.3f -> %.3f us (%u)\n",before,after,unsigned(sink));
}
