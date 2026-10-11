#include "../src/NativeClockAccess.hpp"
#include "../src/NativeClockDispatch.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
static unsigned services{},waits{},advances{},order{};
static bool waiting{},bad_service{},bad_advance{};
struct Calls {
    static void service(uintptr_t,uintptr_t object){
        assert(order==0);order=1;++services;
        if(bad_service)++*reinterpret_cast<uint32_t*>(object+0x3A4);
    }
    static bool waiting(uintptr_t,uintptr_t){assert(order==1);order=2;++waits;return ::waiting;}
    static void advance(uintptr_t,uintptr_t object){
        assert(order==2);++advances;
        *reinterpret_cast<uint32_t*>(object+0x3A4)+=bad_advance?2:1;
    }
};
using Clock=sc6::research::NativeClockAccess<Calls>;
using R=Clock::Result;
using Dispatch=sc6::research::NativeClockDispatch<Clock>;
static unsigned originals{},reservations{},notifications{};
static bool allow{};
static R notified{};
static void original(uintptr_t){++originals;}
static bool admit(void*,const Clock::State&) noexcept {++reservations;return allow;}
static void notify(void*,R result) noexcept {++notifications;notified=result;}
struct Reentry {Dispatch* dispatch;uintptr_t object;};
static bool reenter(void* context,const Clock::State&) noexcept {
    const auto& nested=*static_cast<Reentry*>(context);
    nested.dispatch->run(nested.object,original);return true;
}
struct Fixture {
    alignas(8) std::array<unsigned char,0x4500> object{};
    alignas(8) std::array<uintptr_t,0x700/8> table{};
    uintptr_t base=reinterpret_cast<uintptr_t>(table.data())-0x3283578;
    Fixture(){
        auto t=reinterpret_cast<uintptr_t>(table.data());std::memcpy(object.data(),&t,8);
        table[0x620/8]=base+0x3FDB80;table[0x628/8]=base+0x3E9EE0;table[0x630/8]=base+0x3E2000;
        put(0x398,2);put(0x39C,1);put(0x3A0,7);put(0x3A4,100);
        put(0x43FC,3);put(0x4400,2);object[0x4404]=1;object[0x4424]=2;
        services=waits=advances=order=0;waiting=bad_service=bad_advance=false;
    }
    void put(unsigned offset,uint32_t value){std::memcpy(object.data()+offset,&value,4);}
    bool bind(Clock& clock){return clock.bind(base,reinterpret_cast<uintptr_t>(object.data()),GetCurrentThreadId(),true,true,true);}
};
int main(){
    {
        Fixture f;Clock c;
        assert(c.service()==R::unbound);
        assert(!c.bind(f.base,reinterpret_cast<uintptr_t>(f.object.data()),GetCurrentThreadId(),true,false,true));
        assert(f.bind(c));
        for(unsigned i=0;i<10;++i){
            order=0;assert(c.service()==R::ready);assert(c.finish(false)==R::withheld);
        }
        assert(services==10 && waits==10 && advances==0); // Service continues during mod stalls.
        order=0;waiting=true;assert(c.service()==R::native_wait);
        assert(c.finish(true)==R::native_wait && advances==0); // Never override native wait.
        order=0;waiting=false;assert(c.service()==R::ready);
        assert(c.finish(true)==R::advanced && advances==1);
        assert(c.finish(true)==R::restart_required); // No duplicate native advance.
    }
    {
        Fixture f;Clock c;assert(f.bind(c));bad_service=true;
        assert(c.service()==R::restart_required && c.failure()==Clock::Failure::service_advanced);
        assert(c.finish(true)==R::restart_required && advances==0);
    }
    {
        Fixture f;Clock c;assert(f.bind(c));assert(c.service()==R::ready);bad_advance=true;
        assert(c.finish(true)==R::restart_required && c.failure()==Clock::Failure::advance_count);
        assert(c.service()==R::restart_required && services==1);
    }
    {
        Fixture f;Clock c;assert(f.bind(c));assert(c.service()==R::ready);f.put(0x3A0,8);
        assert(c.finish(true)==R::restart_required && advances==0);
    }
    {
        Fixture f;Clock c;assert(f.bind(c));f.table[0x630/8]=0;
        assert(c.service()==R::restart_required && services==0);
    }
    {
        Fixture f;Dispatch d;const auto object=reinterpret_cast<uintptr_t>(f.object.data());
        d.run(object,original);assert(originals==1 && services==0);
        assert(d.bind(f.base,object,GetCurrentThreadId(),true,true,true,nullptr,admit,notify));
        d.run(object+8,original);assert(originals==2 && services==0);
        allow=false;d.run(object,original);
        assert(services==1 && reservations==1 && advances==0 && notified==R::withheld);
        order=0;waiting=true;d.run(object,original);
        assert(services==2 && reservations==1 && advances==0 && notified==R::native_wait);
        order=0;waiting=false;allow=true;d.run(object,original);
        assert(services==3 && reservations==2 && advances==1 && notified==R::advanced);
        assert(originals==2); // No second dispatcher call for a managed object.
        order=0;bad_advance=true;d.run(object,original);
        assert(d.restart_required() && notified==R::restart_required);
        const auto count=services;d.run(object,original);
        assert(services==count && originals==2 && notifications==4);
    }
    {
        Fixture f;Dispatch d;const auto object=reinterpret_cast<uintptr_t>(f.object.data());
        Reentry nested{&d,object};
        assert(d.bind(f.base,object,GetCurrentThreadId(),true,true,true,&nested,reenter,notify));
        const auto original_count=originals;d.run(object,original);
        assert(d.restart_required() && services==1 && advances==0 && originals==original_count);
        assert(notified==R::restart_required);
    }
    std::cout<<"PASS: dispatcher pass-through, no duplicate advance, servicing during waits, and terminal identity/counter/reentry failures\n";
}
