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
#include <sstream>
static unsigned services{},waits{},advances{},order{};
static bool publish_input{};
static bool waiting{},bad_service{},bad_advance{},stateful_wait{};
struct Calls {
    static void service(uintptr_t,uintptr_t object){
        assert(order==0);order=1;++services;
        if(publish_input){
            const auto frame=*reinterpret_cast<const uint32_t*>(object+0x4410);
            const auto entry=object+0x3C0+(frame&511u)*16;
            *reinterpret_cast<uint32_t*>(entry)=*reinterpret_cast<const uint32_t*>(object+0x3A0);
            *reinterpret_cast<uint32_t*>(entry+4)=frame;*reinterpret_cast<uint32_t*>(entry+8)=5;
            *reinterpret_cast<uint8_t*>(entry+12)=1;
        }
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
struct TestTime {inline static uint64_t tick=0;static uint64_t now(){return tick;}};
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
        Fixture f;f.put(0x4410,100);Clock clock;assert(f.bind(clock));
        publish_input=true;assert(clock.service()==R::ready);assert(clock.finish(true)==R::advanced);
        clock.input_consumed(100,5);publish_input=false;
        std::ostringstream out;clock.write_input_latency(out);
        assert(out.str().find("input_latency_matched=1\n")!=std::string::npos);
        assert(out.str().find("input_latency_invalid=0\n")!=std::string::npos);
        clock.input_consumed(101,5);out.str("");clock.write_input_latency(out);
        assert(out.str().find("input_latency_missing=1\n")!=std::string::npos);
    }

    {
        Fixture f;const auto object=reinterpret_cast<uintptr_t>(f.object.data());
        Clock::State state{};Clock::Failure failure{};unsigned exception{};
        for(unsigned phase=0;phase<256;++phase){
            f.object[0x4424]=static_cast<unsigned char>(phase);
            const auto result=Clock::inspect_rearm(f.base,object,state,failure,exception);
            assert(state.phase==phase && exception==0);
            assert(result==(phase==2?Clock::Inspection::ready:phase<6?Clock::Inspection::waiting:Clock::Inspection::invalid));
            assert(failure==(phase<6?Clock::Failure::none:Clock::Failure::phase));
            Clock::State strict{};assert(Clock::inspect(f.base,object,strict)==(phase==2));
        }
        for(unsigned phase: {5u,0u,1u,2u}){
            f.object[0x4424]=static_cast<unsigned char>(phase);
            assert(Clock::inspect_rearm(f.base,object,state,failure,exception)==(phase==2?Clock::Inspection::ready:Clock::Inspection::waiting));
        }
        f.object[0x4424]=1;f.put(0x398,3);
        assert(Clock::inspect_rearm(f.base,object,state,failure,exception)==Clock::Inspection::invalid && failure==Clock::Failure::layout);
        f.put(0x398,2);f.table[0x620/8]=0;
        assert(Clock::inspect_rearm(f.base,object,state,failure,exception)==Clock::Inspection::invalid);
        assert(Clock::inspect_rearm(0,object,state,failure,exception)==Clock::Inspection::invalid);
        assert(Clock::inspect_rearm(f.base,1,state,failure,exception)==Clock::Inspection::invalid && exception!=0);
        assert(services==0 && waits==0 && advances==0);
        std::cout<<"PASS: rearm waits for audited native phases, rejects invalid clocks, strict combat inspection unchanged\n";
    }

    {
        // Reproduce a backend rejection after five completed updates. Keep
        // the first native cause and observed counters through repeated stop.
        Fixture f;Dispatch d;allow=true;
        const auto object=reinterpret_cast<uintptr_t>(f.object.data());
        assert(d.bind(f.base,object,GetCurrentThreadId(),true,true,true,nullptr,admit,notify));
        for(unsigned i=0;i<5;++i){order=0;d.run(object,original);assert(!d.restart_required());}
        assert(advances==5);
        order=0;bad_service=true;d.run(object,original);
        assert(d.failure()==Dispatch::Failure::native_clock);
        assert(d.access().failure()==Clock::Failure::service_advanced);
        assert(d.access().stage()==3 && d.access().last_observed_state().frame==106);
        assert(d.access().last_serviced_state().frame==104);
        d.stop();d.stop();d.run(object,original);
        assert(d.failure()==Dispatch::Failure::native_clock && advances==5);
        assert(d.access().last_observed_state().frame==106);
        reservations=notifications=0;
    }
    {
        Fixture f;Clock c;assert(f.bind(c));f.object[0x4424]=5;
        assert(c.service()==R::restart_required && c.failure()==Clock::Failure::phase);
        assert(c.stage()==1 && c.last_observed_state().phase==5 && services==0);
    }
    {
        Fixture f;Dispatch d;waiting=true;allow=true;f.object[0x4425]=2;
        assert(d.attach_prediction([](void*,const Clock::State& wait,bool native_wait)noexcept{return native_wait && wait.wait_reason==2;}));
        assert(d.bind(f.base,reinterpret_cast<uintptr_t>(f.object.data()),GetCurrentThreadId(),true,true,true,nullptr,admit,notify));
        d.run(reinterpret_cast<uintptr_t>(f.object.data()),original);
        assert(services==1 && waits==1 && advances==1 && notified==R::advanced);
        order=0;allow=false;d.run(reinterpret_cast<uintptr_t>(f.object.data()),original);
        assert(advances==1 && notified==R::native_wait);
        reservations=notifications=0;
    }
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
        assert(hold.release_unactivated(false) && !hold.matches(object)); // expired startup cannot hold forever
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
        assert(!old_hold.release_unactivated(true) && old_hold.matches(object));
        std::thread foreign([&]{assert(!old_hold.release_unactivated(false));});foreign.join();
        assert(old_hold.matches(object) && old_hold.release_unactivated(false) && !old_hold.matches(object));
        assert(!old_hold.ready(object,1001));
        assert(new_hold.bind(f.base,object,GetCurrentThreadId(),7,100,1001,100,true,true,true));
        assert(new_hold.matches(object) && new_hold.ready(object,1001));
        order=0;waiting=false;assert(new_hold.run(object,1001));
        assert(services==1 && advances==0);
    }
    {
        Fixture f;sc6::research::NativeStartHold<Clock> hold;
        const auto object=reinterpret_cast<uintptr_t>(f.object.data());
        assert(hold.bind(f.base,object,GetCurrentThreadId(),7,100,1000,100,true,true,true));
        std::thread foreign([&]{hold.cancel();});foreign.join();
        assert(!hold.failed());
        hold.cancel();hold.cancel();
        // Keep ownership of the hook, but do not service or advance a failed
        // startup. A failure must not enable the original-clock fallback.
        assert(hold.failed() && hold.matches(object));
        assert(!hold.ready(object,1001) && !hold.run(object,1001));
        assert(services==0 && advances==0 && hold.serviced==0);
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
        // SC6 may service the clock again before consuming its battle delta.
        // Preserve the pending reservation and do not advance a second time.
        for(unsigned i=0;i<4;++i){order=0;waiting=(i%2)!=0;d.run(object,original);
            assert(advances==2 && !bridge.restart_required());}
        assert(drained==7 && bridge.pending_services()==4);
        const auto diagnostic=bridge.reservation().diagnostic();
        assert(diagnostic.clock==101 && diagnostic.reserved && diagnostic.advanced);
        assert(bridge.reservation().consume(1,2,GetCurrentThreadId(),501));
        order=0;waiting=false;d.run(object,original);assert(advances==3 && !bridge.restart_required());
        assert(bridge.reservation().consume(1,2,GetCurrentThreadId(),502));
    }
    {
        Fixture f;sc6::research::BoundedRollbackSession<int,int> session;assert(session.begin(1,2));
        sc6::research::CombatClockBridge<decltype(session),Clock,TestTime> bridge(session);
        const auto object=reinterpret_cast<uintptr_t>(f.object.data());
        assert(bridge.bind(f.base,object,GetCurrentThreadId(),true,true,true,1,2,7,100,500));
        auto& d=bridge.dispatch();d.run(object,original);assert(advances==1);
        TestTime::tick=100;order=0;d.run(object,original);assert(!bridge.restart_required());
        TestTime::tick=10099;order=0;d.run(object,original);assert(!bridge.restart_required() && advances==1);
        TestTime::tick=10100;order=0;d.run(object,original);
        assert(bridge.restart_required() && bridge.failure()==decltype(bridge)::Failure::pending_timeout);
        assert(bridge.pending_services()==3 && bridge.pending_elapsed_ms()==10000 && advances==1);
        bridge.stop();assert(bridge.failure()==decltype(bridge)::Failure::pending_timeout);
    }
    {
        // Evidence counters 605 -> 606 may wait, but a second advance, a
        // rollback of the clock, wrong owner/round, or half reservation may not.
        using B=sc6::research::CombatBoundaryReservation;using G=B::ClockGate;
        for(unsigned bad=0;bad<7;++bad){
            B b;assert(b.bind(1,2,3,0,605,697));
            assert(b.reserve(1,2,3,0,605,true));
            if(bad!=6)assert(b.clock_completed(true));
            if(bad!=6)assert(b.clock_gate(1,2,3,0,606)==G::pending);
            const auto result=b.clock_gate(bad==0?9:1,bad==1?9:2,bad==2?9:3,bad==3?9:0,bad==4?607:bad==5?605:606);
            assert(result==G::restart_required && b.restart_required());
        }
        B b;assert(b.bind(1,2,3,0,605,697));assert(b.reserve(1,2,3,0,605,true));assert(b.clock_completed(true));
        assert(b.clock_gate(1,2,3,0,606)==G::pending);
        assert(b.consume(1,2,3,697));assert(b.clock_gate(1,2,3,0,606)==G::ready);
        assert(!b.consume(1,2,3,697)); // Still reject duplicate combat consumption.
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
        assert(bridge.failure()==decltype(bridge)::Failure::confirmation_drain);
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
