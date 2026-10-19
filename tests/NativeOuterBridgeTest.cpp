#include "../src/NativeOuterCompletion.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <xmmintrin.h>
using sc6::research::native_outer_completion;
using Receipt=sc6::research::NativeOuterCompletion::Receipt;
extern "C" uint64_t SC6NativeOuterBridge(uintptr_t,float);
static unsigned stage{},csr;static uint64_t token;
static uint64_t original(uintptr_t outer,float delta){
    assert(stage==1 && outer==0xABCD && delta==0.125f && _mm_getcsr()==csr);
    token=native_outer_completion.claim(7,outer,&stage,[](void*,const Receipt& receipt){
        assert(stage==3 && receipt.matches(0xABCD,token,7));stage=4;return true;
    });
    assert(token);stage=2;
    // Represents the final native tail update, still inside the original call.
    assert(stage==2);stage=3;return 0x1234567812345678ULL;
}
extern "C" {void* SC6NativeOuterOriginal=reinterpret_cast<void*>(&original);}
extern "C" void SC6NativeOuterEnter(uintptr_t outer)noexcept{
    assert(stage==0 && native_outer_completion.enter(7,outer));stage=1;
    _mm_setcsr(_mm_getcsr() ^ 0x6000);
}
extern "C" void SC6NativeOuterReturned(uintptr_t outer)noexcept{
    assert(stage==3 && native_outer_completion.returned(7,outer) && stage==4);
    _mm_setcsr(_mm_getcsr() ^ 0x6000);
}
int main(){
    csr=_mm_getcsr();
    assert(SC6NativeOuterBridge(0xABCD,0.125f)==0x1234567812345678ULL);
    assert(stage==4 && _mm_getcsr()==csr);
    std::cout<<"PASS: outer completion follows full native call; integer/float arguments, return and FP state preserved\n";
}
