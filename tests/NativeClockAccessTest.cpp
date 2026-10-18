#include "../src/NativeClockAccess.hpp"
#include "../src/NativeClockDispatch.hpp"
#include <array>
#include "../src/CombatClockBridge.hpp"
#include "../src/BoundedRollbackSession.hpp"
#include "../src/CombatRouteBundle.hpp"
#include "../src/NativeStartHold.hpp"
#include "../src/NativeClockDiscovery.hpp"
#include "../src/StartupBoundaryPairs.hpp"
#include <thread>
#include <atomic>
namespace sc6::research {
struct NativeClockDiscoveryTestAccess {
    static void lock(NativeClockDiscovery& d){AcquireSRWLockShared(&d.lock_);}
    static void unlock(NativeClockDiscovery& d){ReleaseSRWLockShared(&d.lock_);}
};
}
#include <cassert>
#include <cstring>
#include <iostream>
static unsigned services{},waits{},advances{},order{};
static bool waiting{},bad_service{},bad_advance{},stateful_wait{};
struct Calls {
    static void service(uintptr_t,uintptr_t object){
        assert(order==0);order=1;++services;
        if(bad_service)++*reinterpret_cast<uint32_t*>(object+0x3A4);
    }
    static bool waiting(uintptr_t,uintptr_t object){
        assert(order==1);order=2;++waits;
        if(stateful_wait){*reinterpret_cast<unsigned char*>(object+0x4425)=0;++*reinterpret_cast<uint32_t*>(object+0x3A8);}
        return ::waiting;
    }
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
        services=waits=advances=order=0;waiting=bad_service=bad_advance=stateful_wait=false;
    }
    void put(unsigned offset,uint32_t value){std::memcpy(object.data()+offset,&value,4);}
    bool bind(Clock& clock){return clock.bind(base,reinterpret_cast<uintptr_t>(object.data()),GetCurrentThreadId(),true,true,true);}
};
int main(){
    {
        Fixture f;Clock c;assert(f.bind(c));
        f.object[0x4425]=2;f.put(0x3A8,6);f.put(0x3B0,100);stateful_wait=waiting=true;
        assert(c.service()==R::native_wait);
        assert(c.last_wait_state().wait_reason==2 && c.last_wait_state().wait_count==6);
        assert(c.last_wait_state().published==100);
        assert(c.last_serviced_state().wait_reason==0 && c.last_serviced_state().wait_count==7);
        assert(c.finish(true)==R::native_wait && advances==0);
    }
    {
        sc6::research::CombatBoundaryReservation r;
        assert(!r.quiescent());assert(r.bind(1,2,3,4,5,6) && r.quiescent());
        assert(r.reserve(1,2,3,4,5,true) && !r.quiescent());
        assert(r.clock_completed(true) && !r.quiescent());
        assert(r.consume(1,2,3,6) && r.quiescent());r.stop();assert(!r.quiescent());
    }
    {
        Fixture f;sc6::research::NativeClockDiscovery discovery;
        sc6::research::StartupBoundaryPairs pairs;
        const auto object=reinterpret_cast<uintptr_t>(f.object.data());
        f.put(0x3A4,117);auto first=discovery.observe(f.base,object,1000);
        pairs.clock(first.clock.identity,first.clock.frame);pairs.battle(174);
        std::atomic<bool> held{},release{};
        std::thread reader([&]{sc6::research::NativeClockDiscoveryTestAccess::lock(discovery);held=true;
            while(!release.load())std::this_thread::yield();
            sc6::research::NativeClockDiscoveryTestAccess::unlock(discovery);});
        while(!held.load())std::this_thread::yield();
        f.put(0x3A4,118);const auto fresh=discovery.observe(f.base,object,1001);
        release=true;reader.join();
        sc6::research::NativeClockDiscovery::Observation cached;
        assert(discovery.read(1001,cached) && cached.clock.frame==117);
        assert(fresh.valid && fresh.clock.frame==118 && fresh.tick==1001 && fresh.thread==GetCurrentThreadId());
        pairs.clock(fresh.clock.identity,fresh.clock.frame);pairs.battle(175);
        f.put(0x3A4,119);const auto next=discovery.observe(f.base,object,1002);
        pairs.clock(next.clock.identity,next.clock.frame);pairs.battle(176);
        assert(pairs.count==3 && pairs.discontinuities==0);
        pairs.clock(next.clock.identity,119);pairs.battle(177);
        assert(pairs.discontinuities==1); // Real repeated clock is still rejected.
        assert(!discovery.observe(f.base,1,1003).valid);
        assert(services==0 && waits==0 && advances==0);
        std::cout<<"PASS: fresh startup clock survives contended publication; genuine discontinuity retained\n";
    }
    {
        Fixture f;sc6::research::NativeClockDiscovery discovery;
        sc6::research::NativeClockDiscovery::Observation out;
        const auto object=reinterpret_cast<uintptr_t>(f.object.data());
        assert(!discovery.read(1000,out));
        discovery.observe(f.base,object,1000);
        assert(discovery.read(1000,out) && out.object==object && out.thread==GetCurrentThreadId());
        assert(out.clock.frame==100 && out.clock.local_mask==1 && out.clock.identity==7);
        assert(services==0 && waits==0 && advances==0);
        assert(!discovery.read(999,out) && !out.valid);
        assert(!discovery.read(1251,out));
        f.object[0x4424]=1;discovery.observe(f.base,object,1001);
        assert(!discovery.read(1001,out));
        f.object[0x4424]=2;f.put(0x39C,2);f.put(0x4400,1);
        discovery.observe(f.base,object,1002);assert(discovery.read(1002,out) && out.clock.local_mask==2);
        discovery.observe(f.base,1,1003);assert(!discovery.read(1003,out));
    }
    {
        Fixture f;sc6::research::NativeStartHold<Clock> hold;
        auto object=reinterpret_cast<uintptr_t>(f.object.data());
        assert(hold.bind(f.base,object,GetCurrentThreadId(),7,100,1000,100,true,true,true));
        for(unsigned i=0;i<10;++i){order=0;waiting=i%2!=0;assert(hold.run(object,1000+i));}
        assert(services==10 && advances==0 && hold.serviced==10);
        struct Exchange {unsigned reports{};bool report_local_hold(uint64_t ticket){++reports;return ticket==55;}} exchange;
        assert(hold.announce(exchange,55,object,1050) && exchange.reports==1);
        assert(!hold.run(object,1100) && hold.failed());
        assert(!hold.announce(exchange,55,object,1100) && exchange.reports==1);
        assert(!hold.run(object,1101) && services==10);
    }
    {
        Fixture f;sc6::research::NativeStartHold<Clock> hold;auto object=reinterpret_cast<uintptr_t>(f.object.data());
        assert(hold.bind(f.base,object,GetCurrentThreadId(),7,100,1000,100,true,true,true));
        f.put(0x3A4,101);assert(!hold.run(object,1001) && services==0 && advances==0);
    }
    {
        Fixture f;sc6::research::NativeStartHold<Clock> old_hold,new_hold;
        auto object=reinterpret_cast<uintptr_t>(f.object.data());
        assert(old_hold.bind(f.base,object,GetCurrentThreadId(),7,100,1000,100,true,true,true));
        assert(old_hold.matches(object) && old_hold.retire() && !old_hold.matches(object));
        assert(!old_hold.ready(object,1001));
        assert(new_hold.bind(f.base,object,GetCurrentThreadId(),7,100,1001,100,true,true,true));
        assert(new_hold.matches(object) && new_hold.ready(object,1001));
        order=0;waiting=false;assert(new_hold.run(object,1001));
        assert(services==1 && advances==0);
    }
    {
        using namespace sc6::research;
        Fixture f;NativeClockDispatch<> dispatch;
        assert(dispatch.bind(f.base,reinterpret_cast<uintptr_t>(f.object.data()),GetCurrentThreadId(),true,true,true,nullptr,
            [](void*,const NativeClockAccess<>::State&)noexcept{return true;},
            [](void*,NativeClockAccess<>::Result)noexcept{}));
        unsigned context=0;ExclusiveBattleRoute battle;
        assert(battle.bind(GetCurrentThreadId(),&context,[](void*,uintptr_t,uintptr_t){return true;},true,true));
        sc6::SchedulerInputSink sink{&context,[](void*,const sc6::NetworkGameHandoff::Request&){return sc6::SchedulerPublish::accepted;}};
        CombatRouteBundle routes{&dispatch,&battle,&sink};assert(publish_combat_routes(routes));
        assert(combat_routes.load()==&routes && combat_routes.load()->inputs==&sink);
        assert(!publish_combat_routes(routes));dispatch.stop();
        assert(combat_routes.load()==&routes); // stop retains ownership
        combat_routes.store(nullptr); // fixture-only teardown, no native hooks installed
    }
    {
        Fixture f;Clock c;
        assert(c.service()==R::unbound);
        assert(!c.bind(f.base,reinterpret_cast<uintptr_t>(f.object.data()),GetCurrentThreadId(),true,false,true));
        assert(f.bind(c));
        assert(c.at_boundary(7,100));assert(!c.at_boundary(7,101));assert(!c.at_boundary(8,100));
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
    {
        Fixture f;
        sc6::research::BoundedRollbackSession<int,int> session;assert(session.begin(1,2));
        sc6::research::CombatClockBridge<decltype(session),Clock> bridge(session);
        unsigned drained=0;
        assert(bridge.attach_confirmations(&drained,[](void* p){++*static_cast<unsigned*>(p);return true;}));
        const auto object=reinterpret_cast<uintptr_t>(f.object.data());
        assert(bridge.bind(f.base,object,GetCurrentThreadId(),true,true,true,1,2,7,100,500));
        auto& d=bridge.dispatch();waiting=true;d.run(object,original);
        assert(services==1 && advances==0 && drained==1 && !bridge.restart_required());
        order=0;waiting=false;d.run(object,original);
        assert(advances==1 && bridge.reservation().consume(1,2,GetCurrentThreadId(),500));
        assert(drained==2);
        order=0;d.run(object,original);assert(advances==2);
        // Deliberately omit the battle call. Next admission must stop BEFORE advance.
        order=0;d.run(object,original);assert(advances==2 && bridge.restart_required());
        const auto stopped=services;d.run(object,original);assert(services==stopped);
    }
    {
        Fixture f;sc6::research::BoundedRollbackSession<int,int> session;assert(session.begin(1,2));
        sc6::research::CombatClockBridge<decltype(session),Clock> bridge(session);
        unsigned drained=0;
        assert(bridge.attach_confirmations(&drained,[](void* p){++*static_cast<unsigned*>(p);return false;}));
        const auto object=reinterpret_cast<uintptr_t>(f.object.data());
        assert(bridge.bind(f.base,object,GetCurrentThreadId(),true,true,true,1,2,7,100,500));
        bridge.dispatch().run(object,original);
        assert(drained==1 && advances==0 && bridge.restart_required());
    }
    {
        Fixture f;sc6::research::BoundedRollbackSession<int,int> session;assert(session.begin(1,2));
        sc6::research::CombatClockBridge<decltype(session),Clock> bridge(session);
        const auto object=reinterpret_cast<uintptr_t>(f.object.data());
        assert(bridge.bind(f.base,object,GetCurrentThreadId(),true,true,true,1,2,7,100,500));
        bad_advance=true;bridge.dispatch().run(object,original);
        assert(bridge.restart_required());const auto stopped=services;
        bridge.dispatch().run(object,original);assert(services==stopped);
    }
    std::cout<<"PASS: dispatcher pass-through, no duplicate advance, servicing during waits, and terminal identity/counter/reentry failures\n";
}
