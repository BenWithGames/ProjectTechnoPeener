#include <cstdint>
#include <cassert>
#include <iostream>
#include <xmmintrin.h>
extern "C" uintptr_t SC6BattleAfterHarness(uintptr_t,uintptr_t*);
extern "C" void SC6BattleAfterContinuation();
extern "C" {void* SC6BattleAfterOriginal=reinterpret_cast<void*>(&SC6BattleAfterContinuation);}
static uintptr_t expected_header;
static unsigned calls;
extern "C" void SC6TraceBattleAfter(uintptr_t outer,uintptr_t header)noexcept{
    assert(outer==0xABCDEF && header==expected_header);
    ++calls;
    _mm_setcsr(_mm_getcsr() ^ 0x6000); // Bridge must restore native FP controls.
}
int main(){
    const auto csr=_mm_getcsr();
    assert(SC6BattleAfterHarness(0xABCDEF,&expected_header)==0x12345678);
    assert(calls==1 && _mm_getcsr()==csr);
    std::cout<<"PASS: post-battle bridge preserves return register, FP controls and native input-header address\n";
}
