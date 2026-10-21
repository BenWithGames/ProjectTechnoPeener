#define NOMINMAX
#include <Windows.h>
#undef interface
#include "../src/IdleTrajectoryAudit.hpp"
#include <cassert>
#include <memory>
#include <iostream>
using namespace sc6::research;
static bool old_check(const FullRestoreStream& a,const FullRestoreStream& b){
 if(!a.sealed || !b.sealed || a.writes!=b.writes || a.writes>FullRestoreStream::transfer_limit || a.stream.failures || b.stream.failures || a.stream.write_position>a.stream.payload.size() || b.stream.write_position>b.stream.payload.size())return false;
 for(unsigned i=0;i<a.writes;++i)if(a.lengths[i]!=b.lengths[i] || a.traces[i].writer!=b.traces[i].writer)return false;
 return true;
}
int main(){
 auto ap=std::make_unique<FullRestoreStream>(),bp=std::make_unique<FullRestoreStream>();auto& a=*ap;auto& b=*bp;
 a.sealed=b.sealed=true;a.writes=b.writes=3;a.stream.write_position=b.stream.write_position=24;
 for(unsigned i=0;i<3;++i){a.lengths[i]=b.lengths[i]=8;a.traces[i].writer=b.traces[i].writer=100+i;a.traces[i].source=1000+i;b.traces[i].source=2000+i;}
 IdleTrajectoryAudit::ShapeDiagnostic d;
 assert(IdleTrajectoryAudit::same_shape(a,b,&d) && d.values[0]==0);
 b.writes=4;b.lengths[3]=16;b.traces[3].writer=500;
 assert(!IdleTrajectoryAudit::same_shape(a,b,&d) && d.values[0]==5 && d.values[1]==3 && d.values[5]==16);
 b.lengths[1]=16;
 assert(!IdleTrajectoryAudit::same_shape(a,b,&d) && d.values[0]==6 && d.values[1]==1 && d.values[2]==3 && d.values[3]==4);
 b.lengths[1]=8;b.traces[1].writer=999;
 assert(!IdleTrajectoryAudit::same_shape(a,b,&d) && d.values[0]==7 && d.values[6]==101 && d.values[7]==999);
 b.traces[1].writer=101;b.writes=3;
 for(unsigned mask=0;mask<256;++mask){
  b.sealed=!(mask&1);b.writes=(mask&2)?4097:3;b.stream.failures=(mask&4)?1:0;
  b.stream.write_position=(mask&8)?b.stream.payload.size()+1:24;
  b.lengths[1]=(mask&16)?16:8;b.traces[1].writer=(mask&32)?999:101;
  a.sealed=!(mask&64);a.stream.failures=(mask&128)?1:0;
  assert(IdleTrajectoryAudit::same_shape(a,b,&d)==old_check(a,b));
 }
 std::cout<<"Stream shape: exact rejection evidence and 256 guard-equivalence combinations passed\n";
}


