#include "../src/SelectorSnapshot.hpp"
#include "../src/SelectorLifecycleHook.hpp"
#include <cassert>
#include <memory>
#include <cstdio>
#include <thread>
#include <atomic>
using namespace sc6::research;
template<class T> void put(uintptr_t p,T v){std::memcpy(reinterpret_cast<void*>(p),&v,sizeof(v));}
struct Fixture {
    std::array<std::unique_ptr<std::byte[]>,2> players;
    std::array<std::array<std::byte,24>,2> headers{};
    std::array<std::array<std::byte,8+3*0x84>,2> tables{};
    uintptr_t ids[2]{};
    Fixture(){
        for(unsigned i=0;i<2;++i){players[i]=std::make_unique<std::byte[]>(0x97400);ids[i]=reinterpret_cast<uintptr_t>(players[i].get());}
        for(unsigned i=0;i<2;++i){auto o=ids[i]+0x95788;auto h=reinterpret_cast<uintptr_t>(headers[i].data());auto t=reinterpret_cast<uintptr_t>(tables[i].data());
            put<unsigned>(h,0x00627476);put<unsigned>(h+8,0x1002);put<unsigned>(h+12,3);put<unsigned>(h+16,32);put<unsigned>(h+20,1);
            put<uintptr_t>(o+0x240,h);put<uintptr_t>(o+0x248,t);put<uintptr_t>(o+0x250,t+8);put<uintptr_t>(o+0x260,t);
            put<unsigned>(o+0x258,1);put<unsigned>(o+0x25C,2);put<int16_t>(t+4,3);
            for(unsigned j=0;j<24;++j)put<int16_t>(o+j*24,-1);
            put<int16_t>(o,5);put<uintptr_t>(o+8,ids[i]);
        }
    }
};
static unsigned init_calls{},free_calls{};
static uintptr_t init_fake(uintptr_t a,uintptr_t b){++init_calls;assert(a==12 && b==34);SetLastError(123);return 0x123456789abcdef0ULL;}
static void free_fake(uintptr_t a){++free_calls;assert(a==12);SetLastError(456);}
int main(){
    Fixture f;auto source=std::make_unique<SelectorSnapshot>();auto next=std::make_unique<SelectorSnapshot>();
    auto actual=std::make_unique<SelectorSnapshot>();SelectorSnapshot::Diagnostic d;
    assert(source->capture(f.ids,d));
    const auto o=f.ids[0]+0x95788,t=reinterpret_cast<uintptr_t>(f.tables[0].data());
    // Reproduce the concrete mechanism: same seed, stale previous choice, extra draw.
    auto draw=[&](){unsigned n=0;const unsigned values[]={15811038,3140156686};unsigned value;
        do{value=values[n++]%3;}while(value==SelectorSnapshot::load<unsigned>(o+0x25C) && n<2);
        put<unsigned>(o+0x25C,value);return n;};
    assert(draw()==1);assert(draw()==2);
    put<int16_t>(o,1);put<int16_t>(t+2,2);assert(next->capture(f.ids,d));
    assert(source->compatible(*next,d));assert(!source->same(*next,d));
    const auto pointer_before=SelectorSnapshot::load<uintptr_t>(o+0x260);
    assert(source->restore(d));assert(d.written && d.verified);assert(draw()==1);
    assert(source->restore(d));assert(actual->capture(f.ids,d));assert(source->same(*actual,d));
    assert(SelectorSnapshot::load<uintptr_t>(o+0x260)==pointer_before);
    // Immutable dependency drift must reject before writes on either side.
    put<unsigned>(o+0x25C,0);f.tables[1][12]=std::byte{1};d={};assert(!source->restore(d));assert(!d.written);
    assert(SelectorSnapshot::load<unsigned>(o+0x25C)==0);f.tables[1][12]=std::byte{};
    // Header identity, freed allocation, malformed size, queue owner and epoch reuse.
    put<uintptr_t>(o+0x260,0);d={};assert(!source->restore(d) && !d.written);put<uintptr_t>(o+0x260,pointer_before);
    put<unsigned>(reinterpret_cast<uintptr_t>(f.headers[0].data())+12,UINT32_MAX);assert(!actual->capture(f.ids,d));
    put<unsigned>(reinterpret_cast<uintptr_t>(f.headers[0].data())+12,3);
    put<uintptr_t>(o+8,0x123456);assert(!actual->capture(f.ids,d));put<uintptr_t>(o+8,f.ids[0]);
    ++selector_lifetime::epoch;d={};assert(!source->restore(d) && !d.written);--selector_lifetime::epoch;
    assert(source->restore(d));
    selector_lifetime::initialize_original=init_fake;selector_lifetime::dispose_original=free_fake;
    auto epoch=selector_lifetime::epoch;
    assert(selector_lifetime::initialize(12,34)==0x123456789abcdef0ULL && GetLastError()==123);
    selector_lifetime::dispose(12);assert(GetLastError()==456 && init_calls==1 && free_calls==1 && selector_lifetime::epoch==epoch+4);
    d={};assert(!source->restore(d) && !d.written);
    // Lifecycle writer cannot run concurrently with a snapshot read lease.
    std::atomic<bool> entered{},done{};AcquireSRWLockShared(&selector_lifetime::lock);
    std::thread worker([&]{entered=true;selector_lifetime::dispose(12);done=true;});
    while(!entered.load())SwitchToThread();assert(!done.load());ReleaseSRWLockShared(&selector_lifetime::lock);worker.join();assert(done);
    std::puts("Selector snapshot, dependency, RNG retry and lifecycle tests passed");
}
