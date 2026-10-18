#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "../../src/FullStateProbe.hpp"
#include "../../src/IdleTrajectoryAudit.hpp"
#include "../../src/NativeAdjacentRestore.hpp"
#include <cstdio>
using namespace sc6::research;
int main(){auto a=std::make_unique<FullStateProbe>();auto b=std::make_unique<FullStateProbe>();
 auto& x=a->saved;auto& y=b->restored;x.sealed=y.sealed=true;x.writes=y.writes=256;x.stream.write_position=y.stream.write_position=131072;
 for(unsigned i=0;i<256;++i){x.lengths[i]=y.lengths[i]=512;x.traces[i].source=y.traces[i].source=0x100000+i*512;}
 LARGE_INTEGER f,t,u;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&t);unsigned ok=0;
 for(unsigned i=0;i<1000;++i){NativeAdjacentRestore::PayloadDiagnostic d;ok+=NativeAdjacentRestore::native_equal(*a,*b,0x140000000,&d);}
 QueryPerformanceCounter(&u);std::printf("exact comparator: %.6f ms/call; verified=%u; probe_bytes=%zu\n",1000.0*(u.QuadPart-t.QuadPart)/f.QuadPart/1000,ok,sizeof(FullStateProbe));return ok==1000?0:1;}

