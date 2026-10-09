#include "../src/RecordedInput.hpp"
#include <cstdlib>
#include <iostream>
#include <intrin.h>
extern "C" void SC6EntryBridge();
extern "C" {void* SC6Original=nullptr;}
static sc6::research::RecordedInput input;
static bool enabled;
static uintptr_t original_arg;
static uintptr_t observed_return;
extern "C" void SC6RecordEntry(uintptr_t arg,uintptr_t* replacement,uintptr_t return_address) noexcept {
    original_arg=arg;
    observed_return=return_address;
    if(enabled)*replacement=reinterpret_cast<uintptr_t>(input.pointers.data());
}
static double target(uintptr_t arg,double b,uintptr_t c,double d,uintptr_t e,double f){
    if(!observed_return || observed_return!=reinterpret_cast<uintptr_t>(_ReturnAddress()))std::abort();
    if(arg!=(enabled?reinterpret_cast<uintptr_t>(input.pointers.data()):17))std::abort();
    if(enabled){auto ptrs=reinterpret_cast<uintptr_t*>(arg);
        if(std::memcmp(reinterpret_cast<void*>(ptrs[2]),input.bytes.data()+16,24))std::abort();}
    return double(c+e)+b+d+f;
}
int main(){
    std::array<std::byte,40> bytes{};bytes[37]=std::byte{1};input.assign(bytes);
    SC6Original=reinterpret_cast<void*>(&target);
    auto entry=reinterpret_cast<double(*)(uintptr_t,double,uintptr_t,double,uintptr_t,double)>(&SC6EntryBridge);
    for(unsigned n=0;n<4;++n){enabled=(n%2)==1;
        if(entry(17,1.25,23,2.5,31,4.75)!=62.5 || original_arg!=17)std::abort();}
    std::cout<<"PASS: original caller address, recorded argument handoff, original pass-through, remaining register/stack arguments and return.\n";
}
