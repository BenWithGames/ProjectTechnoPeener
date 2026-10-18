#include <Windows.h>
#include "../src/TransformSnapshot.hpp"
#include <cassert>
#include <memory>
#include <cstdio>
using namespace sc6::research;
int main(){
    const auto base=reinterpret_cast<uintptr_t>(VirtualAlloc(nullptr,0x4900000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(base);
    const auto manager=base+0x4100B60,clock=manager+8;
    *reinterpret_cast<uintptr_t*>(base+0x470E038)=manager;
    *reinterpret_cast<unsigned*>(clock)=0x3F800000;
    auto a=std::make_unique<TransformSnapshot>(),b=std::make_unique<TransformSnapshot>();
    auto capture=[&](TransformSnapshot& s){s.count=0;s.valid=true;
        assert(s.append(base+0x470E038,2,5,0,0,8));
        assert(s.append(clock,2,3,0,0,8));
        assert(s.append(base+0x485EB30,2,10,0,0,104));
        assert(s.append(base+0x470E2B0,2,11,0,0,24));};
    TransformSnapshot::WindDiagnostic d;
    // Bit-exact cached scalar restoration, not a hard-coded reset to 1.
    for(unsigned saved:{0u,0x80000000u,0x3ECCC000u,0x3F800000u,0x40000000u}){
        *reinterpret_cast<unsigned*>(clock+4)=saved;capture(*a);
        *reinterpret_cast<unsigned*>(clock+4)=0x3ECCC000u;capture(*b);
        assert(a->restore_wind(*b,*b,base,true,&d));assert(*reinterpret_cast<unsigned*>(clock+4)==0x3ECCC000u);
        assert(a->restore_wind(*b,*b,base,false,&d));assert(*reinterpret_cast<unsigned*>(clock+4)==saved);
        assert(*reinterpret_cast<unsigned*>(clock)==0x3F800000 && *reinterpret_cast<uintptr_t*>(base+0x470E038)==manager);
    }
    *reinterpret_cast<unsigned*>(clock+4)=0x3F800000;capture(*a);
    *reinterpret_cast<unsigned*>(clock+4)=0x3ECCC000;capture(*b);
    // Neighbor is not approved for restoration; no partial scalar/RNG writes.
    *reinterpret_cast<unsigned*>(clock)=0x40000000;capture(*b);
    assert(!a->restore_wind(*b,*b,base,false,&d) && d.shared.reason==4);
    assert(*reinterpret_cast<unsigned*>(clock+4)==0x3ECCC000);
    *reinterpret_cast<unsigned*>(clock)=0x3F800000;capture(*b);
    *reinterpret_cast<unsigned*>(clock+4)=0;assert(!a->restore_wind(*b,*b,base,false,&d) && d.shared.reason==3);
    *reinterpret_cast<unsigned*>(clock+4)=0x3ECCC000;
    a->records[1].parent=b->records[1].parent=3;
    assert(!a->restore_wind(*b,*b,base,false,&d) && d.shared.reason==4);
    a->records[1].parent=b->records[1].parent=0;
    a->records[1].size=b->records[1].size=16;
    assert(!a->restore_wind(*b,*b,base,false,&d) && d.shared.reason==4);
    a->records[1].size=b->records[1].size=8;
    std::memcpy(reinterpret_cast<void*>(clock+0x1000),reinterpret_cast<void*>(clock),8);
    a->records[1].address=b->records[1].address=clock+0x1000;
    assert(!a->restore_wind(*b,*b,base,false,&d) && d.shared.reason==4);
    a->records[1].address=b->records[1].address=clock;
    *reinterpret_cast<uintptr_t*>(base+0x470E038)=manager+16;
    assert(!a->restore_wind(*b,*b,base,false,&d));
    assert(*reinterpret_cast<unsigned*>(clock+4)==0x3ECCC000);
    VirtualFree(reinterpret_cast<void*>(base),0,MEM_RELEASE);
    std::puts("PASS: audited manager clock exact restoration, validate-only, neighboring field, ownership and live drift guards");
}
