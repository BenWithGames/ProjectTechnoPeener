#include <array>
#include <cstdint>
#include <cassert>
#include <immintrin.h>
#include <iostream>
extern "C" void NativeFilterHarness(uintptr_t*);
extern "C" void NativeFilterHarnessContinuation();
extern "C" {void* SC6NativeFilterOriginal=reinterpret_cast<void*>(&NativeFilterHarnessContinuation);}
static std::array<uintptr_t,7> observed;
extern "C" uintptr_t SC6NativeFilterCapture(const uintptr_t* values)noexcept{
    for(unsigned n=0;n<7;++n)observed[n]=values[n];
    _mm_setcsr(_mm_getcsr()^0x6000u);
    return 0xDEADC0DE;
}
int main(){
    std::array<uintptr_t,14> values={0x12345678,0x10000,1,3,0xAB01,0xCD00,0xEF01};
    const auto control=_mm_getcsr();NativeFilterHarness(values.data());
    for(unsigned n=0;n<7;++n)assert(observed[n]==values[n]);
    assert(values[7]==0xDEADC0DE);
    for(unsigned n=0;n<6;++n)assert(values[n+8]==(n+1)*0x111u);
    assert(_mm_getcsr()==control);
    std::cout<<"Native filter bridge tests passed\n";
}
