#include "../src/CameraCaptureIndex.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
using namespace sc6::research;
unsigned naive(const FullRestoreStream& s,uintptr_t base,unsigned k,uintptr_t p,uint64_t n){unsigned c=0;for(unsigned i=0;i<s.writes;++i)if(s.traces[i].writer==base+CameraCaptureIndex::sites[k] && s.traces[i].source==p && s.lengths[i]==n)++c;return c;}
int main(){
 auto s=std::make_unique<FullRestoreStream>();CameraCaptureIndex index;constexpr uintptr_t base=0x140000000;
 assert(!index.build(*s,base));s->sealed=true;s->writes=FullRestoreStream::transfer_limit+1;assert(!index.build(*s,base));
 unsigned random=123;auto rng=[&]{random^=random<<13;random^=random>>17;random^=random<<5;return random;};
 for(unsigned trial=0;trial<120;++trial){
  s->writes=trial==0?4096:rng()%4097;
  for(unsigned i=0;i<s->writes;++i){auto k=rng()%9;s->traces[i].writer=k<6?base+CameraCaptureIndex::sites[k]:base+100+k;s->traces[i].source=0x10000+(rng()%8)*16;s->lengths[i]=rng()%20;}
  assert(index.build(*s,base));
  for(unsigned k=0;k<6;++k)for(unsigned p=0;p<8;++p)for(unsigned n:{4u,16u})assert(index.matches(*s,k,0x10000+p*16,n)==naive(*s,base,k,0x10000+p*16,n));
 }
 // Duplicate, wrong-size, missing and stale entries remain distinguishable.
 s->writes=2;for(unsigned i=0;i<2;++i){s->traces[i].writer=base+CameraCaptureIndex::sites[0];s->traces[i].source=0x10050;s->lengths[i]=16;}
 assert(index.build(*s,base)&&index.matches(*s,0,0x10050,16)==2);
 s->lengths[1]=4;assert(index.build(*s,base)&&index.matches(*s,0,0x10050,16)==1);
 s->writes=0;assert(index.build(*s,base)&&index.matches(*s,0,0x10050,16)==0);
 // Synthetic lookup benchmark, including index construction every capture.
 s->writes=2048;for(unsigned i=0;i<s->writes;++i){s->traces[i]={};s->lengths[i]=4;}
 for(unsigned p=0;p<16;++p)for(unsigned k=0;k<6;++k){auto i=p*6+k;s->traces[i].writer=base+CameraCaptureIndex::sites[k];s->traces[i].source=0x20000+p*0x400+k*16;s->lengths[i]=16;}
 uint64_t sum=0;auto bench=[&](bool indexed){auto start=std::chrono::steady_clock::now();for(unsigned trial=0;trial<500;++trial){s->traces[2047].source=trial;if(indexed)assert(index.build(*s,base));for(unsigned p=0;p<16;++p)for(unsigned k=0;k<6;++k)sum+=indexed?index.matches(*s,k,0x20000+p*0x400+k*16,16):naive(*s,base,k,0x20000+p*0x400+k*16,16);}return std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/500;};
 auto old=bench(false),now=bench(true);std::cout<<"PASS: 120 differential record sets, full capacity, malformed bounds, duplicate/missing/wrong-size and reuse checks\nsynthetic_scan_us="<<old<<"\nsynthetic_index_us="<<now<<"\nchecksum="<<sum<<"\n";
}
