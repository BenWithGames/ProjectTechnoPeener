#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "../src/FullStateProbe.hpp"
#include "../src/IdleTrajectoryAudit.hpp"
#include "../src/NativeAdjacentRestore.hpp"
#include <cassert>
#include <cstdio>
using namespace sc6::research;
__declspec(noinline) unsigned old_scan(const std::byte* a,const std::byte* b,unsigned n){
 for(unsigned i=0;i<n;++i)if(a[i]!=b[i])return i;return UINT32_MAX;
}
int main(){
 auto s=std::make_unique<FullRestoreStream>(),d=std::make_unique<FullRestoreStream>();
 s->writes=2;s->lengths[0]=3;s->lengths[1]=5;s->stream.write_position=8;s->sealed=true;s->stream.transfers=2;
 for(unsigned i=0;i<8;++i)s->stream.payload[i]=std::byte(i+1);
 s->traces[0]={1,2,3,4};s->traces[1]={5,6,7,8};
 d->stream.payload.fill(std::byte{0xCC});assert(d->prepare_reader_from(*s));
 assert(d->stream.payload[8]==std::byte{0xCC});assert(!std::memcmp(d->stream.payload.data(),s->stream.payload.data(),8));
 assert(d->stream.slots==s->stream.slots && d->stream.copier==s->stream.copier && d->stream.transfers==2);
 assert(d->traces[0].source==1 && d->traces[0].writer==3 && !d->traces[0].reader && !d->traces[0].destination);
 std::array<std::byte,8> out{};FullRestoreStream::read(&d->stream,out.data(),3);FullRestoreStream::read(&d->stream,out.data()+3,5);assert(d->consumed());
 assert(!std::memcmp(out.data(),s->stream.payload.data(),8));assert(s->reads==0);
 s->writes=1;s->lengths[0]=3;s->stream.write_position=3;assert(d->copy_recorded_from(*s));assert(d->lengths[1]==0 && d->traces[1].source==0);
 auto old=d->stream.write_position;s->lengths[0]=4;assert(!d->copy_recorded_from(*s));assert(d->stream.write_position==old);s->lengths[0]=3;
 s->stream.failures=2;s->stream.first_error=SaveStream::copy_fault;assert(d->copy_recorded_from(*s));assert(d->stream.failures==2 && d->stream.first_error==SaveStream::copy_fault);
 std::array<std::byte,1025> a{},b{};
 for(unsigned n=0;n<=1025;++n){if(n)b[n-1]=std::byte{1};uint64_t ref=0;for(unsigned j=0;j<n;++j)ref+=a[j]!=b[j];assert(exact_byte_differences(a.data(),b.data(),n)==ref);}
 auto x=std::make_unique<FullStateProbe>(),y=std::make_unique<FullStateProbe>();
 x->saved.sealed=y->restored.sealed=true;x->saved.writes=y->restored.writes=1;x->saved.lengths[0]=y->restored.lengths[0]=512;x->saved.stream.write_position=y->restored.stream.write_position=512;
 NativeAdjacentRestore::PayloadDiagnostic diag;assert(NativeAdjacentRestore::native_equal(*x,*y,0,&diag));
 for(unsigned offset:{0u,255u,256u,511u}){y->restored.stream.payload[offset]=std::byte{1};assert(!NativeAdjacentRestore::native_equal(*x,*y,0,&diag));assert(diag.bytes==1 && diag.details[0][5]==offset && diag.details[0][6]==offset);y->restored.stream.payload[offset]=std::byte{};}
 x->saved.lengths[0]=y->restored.lengths[0]=48;x->saved.stream.write_position=y->restored.stream.write_position=48;x->before_players[0]=0x100000;
 x->saved.traces[0].writer=y->restored.traces[0].writer=0x30AA54;x->saved.traces[0].source=y->restored.traces[0].source=0x143DF0;
 assert(!NativeAdjacentRestore::native_equal(*x,*y,0,&diag));assert(diag.structural & 4);

 auto completion=std::make_unique<ImmediateIdleCorrection>();
 std::array<std::byte,512> left{},right{};
 for(unsigned n=0;n<=512;++n){
  assert(!completion->difference(left.data(),right.data(),n));
  assert(completion->difference_offset==UINT32_MAX && !completion->saved_word && !completion->live_word);
  for(unsigned byte=0;byte<n;++byte){
   right[byte]=std::byte{0xA5};assert(completion->difference(left.data(),right.data(),n));
   assert(completion->difference_offset==byte && completion->saved_word==0 && completion->live_word==0xA5);
   right[byte]=std::byte{};
  }
 }
 LARGE_INTEGER frequency{},start{},end{};QueryPerformanceFrequency(&frequency);
 volatile unsigned consumed=0;constexpr unsigned scans=100000;
 QueryPerformanceCounter(&start);
 for(unsigned i=0;i<scans;++i)consumed=old_scan(left.data(),right.data(),512);
 QueryPerformanceCounter(&end);const double old_us=(end.QuadPart-start.QuadPart)*1e6/frequency.QuadPart/scans;
 QueryPerformanceCounter(&start);
 for(unsigned i=0;i<scans;++i){completion->difference(left.data(),right.data(),512);consumed=completion->difference_offset;}
 QueryPerformanceCounter(&end);const double new_us=(end.QuadPart-start.QuadPart)*1e6/frequency.QuadPart/scans;
 x->transforms[0].count=2000;x->transforms[0].valid=true;
 for(unsigned i=0;i<2000;++i)x->transforms[0].records[i].size=512;
 constexpr unsigned copies=1000;
 QueryPerformanceCounter(&start);
 for(unsigned i=0;i<copies;++i){assert(y->copy_restore_endpoint_from(*x));consumed=y->transforms[0].count;}
 QueryPerformanceCounter(&end);const double copy_us=(end.QuadPart-start.QuadPart)*1e6/frequency.QuadPart/copies;
 QueryPerformanceCounter(&start);
 for(unsigned i=0;i<copies;++i){assert(y->copy_restore_endpoint_from(*x,false));consumed=y->transforms[0].count;}
 QueryPerformanceCounter(&end);const double controls_us=(end.QuadPart-start.QuadPart)*1e6/frequency.QuadPart/copies;
 std::printf("Mock microseconds: equal512 scan %.4f -> %.4f; endpoint with2000 records %.3f -> controls %.3f (sink %u)\n",old_us,new_us,copy_us,controls_us,unsigned(consumed));
 std::puts("PASS: first-difference evidence exact for every offset at lengths 0..512; endpoint controls omit duplicate transform storage");
 std::puts("PASS: bounded copies preserve controls/errors, clear obsolete metadata, isolate source, reject invalid sizes; exact scans retain mismatch counts and self-pointer rejection");
}
