#include "../src/NativeRawInputLookup.hpp"
#include <array>
#include <cassert>
#include <iostream>
using Lookup=sc6::research::NativeRawInputLookup;
int main(){
    std::array<Lookup::Entry,1024> rings{};
    auto read=[&](uintptr_t offset,Lookup::Entry& out){
        assert(offset>=0x3C0 && offset<0x43C0 && (offset-0x3C0)%16==0);
        out=rings[(offset-0x3C0)/16];return true;
    };
    for(uint32_t slot=0;slot<2;++slot)for(uint32_t frame=0;frame<1536;++frame){
        auto& entry=rings[slot*512+(frame&511)];entry={7,frame,0,0};
        auto result=Lookup::inspect({slot,7,frame,0,false},read);
        assert(result.result==Lookup::Result::tagged && result.raw==0);
        entry.raw=0xA5000001u;result=Lookup::inspect({slot,7,frame,0,false},read);
        assert(result.result==Lookup::Result::tagged && result.raw==entry.raw);
        assert(Lookup::inspect({slot,8,frame,0,false},read).result==Lookup::Result::missing);
        assert(Lookup::inspect({slot,7,frame+512,0,false},read).result==Lookup::Result::missing);
        result=Lookup::inspect({slot,7,frame+3,3,true},read);
        assert(result.result==Lookup::Result::tagged && result.effective_frame==frame);
    }
    assert(Lookup::inspect({0,7,0,1,true},read).result==Lookup::Result::invalid);
    assert(Lookup::inspect({2,7,0,0,false},read).result==Lookup::Result::invalid);
    assert(Lookup::inspect({0,7,0x80000000u,0,false},read).result==Lookup::Result::invalid);
    auto failed=[](uintptr_t,Lookup::Entry&){return false;};
    assert(Lookup::inspect({0,7,0,0,false},failed).result==Lookup::Result::invalid);
    unsigned calls=0;
    auto changing=[&](uintptr_t,Lookup::Entry& e){e={7,0,++calls,0};return true;};
    assert(Lookup::inspect({0,7,0,0,false},changing).result==Lookup::Result::unstable);
    std::cout<<"Native raw input lookup tests passed\n";
}


