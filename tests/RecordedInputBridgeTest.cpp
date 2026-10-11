#include "../src/RecordedInput.hpp"
#include "../src/ExclusiveBattleRoute.hpp"
#include <cstdlib>
#include <iostream>
#include <intrin.h>
extern "C" void SC6EntryBridge();
extern "C" {void* SC6Original=nullptr;}
static sc6::research::RecordedInput input;
static bool enabled;
static uintptr_t original_arg;
static uintptr_t observed_return;
static sc6::research::ExclusiveBattleRoute* route;
static unsigned updates;
static void void_target(uintptr_t){++updates;}
extern "C" void SC6RecordEntry(uintptr_t arg,uintptr_t* replacement,uintptr_t return_address,uintptr_t* continuation) noexcept {
    if(route){*continuation=0;route->run(arg,return_address);return;}
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
    SC6Original=reinterpret_cast<void*>(&void_target);
    auto void_entry=reinterpret_cast<void(*)(uintptr_t)>(&SC6EntryBridge);
    sc6::research::ExclusiveBattleRoute owned;
    if(!owned.bind(GetCurrentThreadId(),&updates,[](void*,uintptr_t arg,uintptr_t){void_target(arg);return true;},true,true))std::abort();
    route=&owned;
    for(unsigned i=0;i<10;++i)void_entry(17);
    if(updates!=10 || owned.failed())std::abort(); // no duplicate original call
    sc6::research::ExclusiveBattleRoute failed;
    if(!failed.bind(GetCurrentThreadId(),&updates,[](void*,uintptr_t,uintptr_t){return false;},true,true))std::abort();
    route=&failed;void_entry(17);void_entry(17);
    if(updates!=10 || !failed.failed())std::abort();
    sc6::research::ExclusiveBattleRoute reentrant;
    if(!reentrant.bind(GetCurrentThreadId(),&updates,[](void*,uintptr_t arg,uintptr_t){reinterpret_cast<void(*)(uintptr_t)>(&SC6EntryBridge)(arg);return true;},true,true))std::abort();
    route=&reentrant;void_entry(17);
    if(updates!=10 || !reentrant.failed())std::abort();
    std::cout<<"PASS: original caller address, recorded argument handoff, original pass-through, remaining register/stack arguments and return.\n";
}
