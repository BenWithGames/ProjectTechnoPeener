#include "../src/NativeClockAccess.hpp"
#include "../src/NativeClockDispatch.hpp"
#include <array>
#include "../src/CombatClockBridge.hpp"
#include "../src/BoundedRollbackSession.hpp"
#include "../src/CombatRouteBundle.hpp"
#include "../src/NativeStartHold.hpp"
#include "../src/NativeClockDiscovery.hpp"
#include "../src/StartupBoundaryPairs.hpp"
#include "../src/NativeMissedUpdateBoundary.hpp"
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
static bool publish_input{},model_pause{},native_paused{};
static unsigned other_pause_bits{},service_reason{},resume_call_count{};
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
        if(model_pause)*reinterpret_cast<uint8_t*>(object+0x4425)=static_cast<uint8_t>(service_reason);
        if(bad_service)++*reinterpret_cast<uint32_t*>(object+0x3A4);
    }
    static bool waiting(uintptr_t,uintptr_t object){
        assert(order==1 || (model_pause && order==2));
        if(model_pause){
            if(order==2)++resume_call_count;
            const auto reason=*reinterpret_cast<uint8_t*>(object+0x4425);
            const auto old=*reinterpret_cast<uint32_t*>(object+0x3A8);
            native_paused=reason!=0;
            *reinterpret_cast<uint32_t*>(object+0x3A8)=reason?old+1:0;
            *reinterpret_cast<uint8_t*>(object+0x4425)=0;
            order=2;++waits;return reason==1 || old!=0;
        }
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
        model_pause=native_paused=false;other_pause_bits=service_reason=resume_call_count=0;
        services=waits=advances=order=0;waiting=bad_service=bad_advance=stateful_wait=false;
    }
    void put(unsigned offset,uint32_t value){std::memcpy(object.data()+offset,&value,4);}
    bool bind(Clock& clock){return clock.bind(base,reinterpret_cast<uintptr_t>(object.data()),GetCurrentThreadId(),true,true,true);}
};
static void missed_boundary_tests(){
    for(unsigned mode=0;mode<8;++mode){
        Fixture f;std::array<unsigned char,0x1500> outer{};
        auto object=reinterpret_cast<uintptr_t>(f.object.data());
        std::memcpy(outer.data()+0x478,&object,sizeof(object));outer[0x1461]=2;
        uint32_t identity=7,frame=100;
        std::memcpy(outer.data()+0x1488,&identity,4);std::memcpy(outer.data()+0x148C,&frame,4);
        if(mode==1){uintptr_t other=object+8;std::memcpy(outer.data()+0x478,&other,8);}
        if(mode==2)outer[0x1461]=3;
        if(mode==3)outer[0x12F3]=1;
        if(mode==4)outer[0x1463]=1;
        if(mode==5)outer[0x148C]=99;
        if(mode==7)outer[0x1488]=8;
        using B=sc6::research::NativeMissedUpdateBoundary;
        const auto expected=mode==7?B::pending_delta:static_cast<B::Reason>(mode);
        assert(B::inspect(mode==6?0:reinterpret_cast<uintptr_t>(outer.data()),object)==expected);
    }
}
static void admission_receipt_integration_tests(){
    using S=sc6::research::BoundedRollbackSession<int,int>;
    for(bool truth_arrives:{false,true}){
        auto f=std::make_unique<Fixture>();S session;assert(session.begin(1,2));
        struct Adapter{int predict(int v){return v;}bool save(int& v){v=0;return true;}bool load(int){return true;}bool step(int,int){return true;}} adapter;
        for(unsigned i=0;i<3;++i)assert(session.advance(adapter,0));
        struct Context{S* session;unsigned late{};bool arrives;} c{&session,0,truth_arrives};
        auto bridge=std::make_unique<sc6::research::CombatClockBridge<S,Clock>>(session);
        assert(bridge->attach_confirmations(&c,[](void*){return true;},[](void* p){auto& c=*static_cast<Context*>(p);
            if(++c.late==2 && c.arrives)return c.session->receive(1,2,0,0)==S::Arrival::accepted;return true;}));
        assert(bridge->attach_prediction(&c,[](void*,const Clock::State&,bool){return true;}));
        const auto object=reinterpret_cast<uintptr_t>(f->object.data());
        assert(bridge->bind(f->base,object,GetCurrentThreadId(),true,true,true,1,2,7,100,500));
        bridge->dispatch().run(object,original);
        assert(services==1 && advances==(truth_arrives?1u:0u) && c.late==2);
        assert(!bridge->restart_required() && bridge->grace_attempts()==0);
        if(truth_arrives){assert(bridge->reservation().consume(1,2,GetCurrentThreadId(),500));
            assert(!bridge->dispatch().recovery_pending());}
        assert(bridge->reservation().quiescent());
    }
}
static void missed_recovery_tests(){
    // Late truth reaches the owner after clock admission but before the outer
    // combat caller. Recover exactly once, without another native service/wait.
    for(unsigned mode=0;mode<10;++mode){
        Fixture f;using S=sc6::research::BoundedRollbackSession<int,int>;S session;
        auto journal=std::make_unique<sc6::research::RollbackJournal<>>();assert(session.attach_journal(*journal));assert(session.begin(1,2));
        struct Adapter{int predict(int v){return v;}bool save(int& v){v=0;return true;}bool load(int){return true;}bool step(int,int){return true;}} adapter;
        for(unsigned i=0;i<3;++i)assert(session.advance(adapter,0));
        struct Context{S* session;bool late{},drained{};unsigned mode;unsigned services{};} ctx{&session,false,false,mode};
        sc6::research::CombatClockBridge<S,Clock> bridge(session);
        assert(bridge.attach_confirmations(&ctx,[](void* p){auto& c=*static_cast<Context*>(p);++c.services;
            if(!c.late)return true;
            if(c.mode==5)return false;
            if(c.mode!=1 && !c.drained){c.drained=true;return c.session->receive(1,2,0,0)==S::Arrival::accepted;}
            return true;}));
        assert(bridge.attach_prediction(&ctx,[](void* p,const Clock::State&,bool){auto& c=*static_cast<Context*>(p);return !(c.late && c.mode==8);}));
        const auto object=reinterpret_cast<uintptr_t>(f.object.data());
        assert(bridge.bind(f.base,object,GetCurrentThreadId(),true,true,true,1,2,7,100,500));
        if(mode==9){model_pause=true;service_reason=2;other_pause_bits=4;}
        bridge.dispatch().run(object,original);
        assert(advances==0 && services==1 && waits==(mode==9?2u:1u) && bridge.reservation().quiescent());
        ctx.late=true;
        if(mode==2)f.object[0x4424]=0; // normal transition invalidates the retry
        if(mode==3)assert(session.request_handoff(1,2));
        if(mode==4)bridge.stop();
        if(mode==6)assert(bridge.dispatch().recover(object+8)); // other clock cannot consume retry
        if(mode==7)f.put(0x4410,1); // native state changed since the missed admission
        const bool ok=bridge.dispatch().recover(object);
        assert(ok==(mode!=4 && mode!=5));
        const bool recovered=mode==0 || mode==6 || mode==9;
        assert(advances==(recovered?1u:0u));
        assert(services==1 && waits==(mode==9?3u:1u)); // only the existing native pause-clear path may repeat
        if(mode==9)assert(!native_paused && other_pause_bits==4 && resume_call_count==2);
        assert(bridge.grace_attempts()==0);
        if(recovered){
            assert(bridge.reservation().consume(1,2,GetCurrentThreadId(),500));
            assert(bridge.reservation().quiescent());
        }
        const auto drained=ctx.services;
        (void)bridge.dispatch().recover(object);
        assert(ctx.services==drained && advances==(recovered?1u:0u));
        unsigned begins=0,ends=0;sc6::research::RollbackEvent e;
        while(journal->pop(e))if(e.kind==sc6::research::RollbackEvent::missed_update_recovery){if(e.detail==0)++begins;else ++ends;}
        assert(begins==(mode==4?0u:1u) && begins==ends);
    }
}
static void budget_pause_tests(){
    // Exhaustive native reasons: only a validated reason-5 rollback budget wait
    // may release native 0/2 pause side effects, never grant a combat update.
    for(unsigned reason=0;reason<6;++reason)for(unsigned mode=0;mode<6;++mode){
        auto fixture=std::make_unique<Fixture>();auto dispatch=std::make_unique<Dispatch>();auto& f=*fixture;auto& d=*dispatch;model_pause=true;service_reason=reason;other_pause_bits=13;f.put(0x3A8,3);
        struct Context{bool candidate,eligible,available;unsigned admissions{};} c{mode!=1,mode!=2,mode!=3};
        assert(d.attach_gate([](void* p,const Clock::State&)noexcept{return static_cast<Context*>(p)->available;}));
        assert(d.attach_prediction([](void* p,const Clock::State&,bool)noexcept{return static_cast<Context*>(p)->eligible;},mode!=4));
        assert(d.attach_recovery([](void* p)noexcept{return static_cast<Context*>(p)->candidate;},admit));
        const auto object=reinterpret_cast<uintptr_t>(f.object.data());
        assert(d.bind(f.base,object,GetCurrentThreadId(),true,true,true,&c,
            [](void* p,const Clock::State&)noexcept{++static_cast<Context*>(p)->admissions;return false;},notify));
        if(mode==5)d.stop();
        d.run(object,original);
        const bool released=mode==0 && (reason==0 || reason==2);
        assert(resume_call_count==unsigned(released));assert(advances==0 && other_pause_bits==13);
        assert(c.admissions==unsigned(mode!=2 && mode!=3 && mode!=5));
        if(mode!=5){
            assert(native_paused==(reason!=0 && !released));
            assert(*reinterpret_cast<uint32_t*>(f.object.data()+0x3A8)==(reason?4u:0u));
            assert(*reinterpret_cast<uint32_t*>(f.object.data()+0x3A4)==100);
        }
    }
    // Real bridge/session budget, not just a synthetic admission callback.
    auto fixture=std::make_unique<Fixture>();auto& f=*fixture;using S=sc6::research::BoundedRollbackSession<int,int>;auto owned=std::make_unique<S>();auto& session=*owned;assert(session.begin(1,2));
    struct Adapter{int predict(int v){return v;}bool save(int& v){v=0;return true;}bool load(int){return true;}bool step(int,int){return true;}} adapter;
    for(unsigned i=0;i<3;++i)assert(session.advance(adapter,0));
    auto holder=std::make_unique<sc6::research::CombatClockBridge<S,Clock>>(session);auto& bridge=*holder;int context=0;
    assert(bridge.attach_prediction(&context,[](void*,const Clock::State&,bool){return true;}));
    const auto object=reinterpret_cast<uintptr_t>(f.object.data());
    assert(bridge.bind(f.base,object,GetCurrentThreadId(),true,true,true,1,2,7,100,500));
    model_pause=true;service_reason=2;other_pause_bits=4;
    for(unsigned i=0;i<20;++i){
        order=0;bridge.dispatch().run(object,original);
        assert(!native_paused && advances==0 && bridge.reservation().quiescent());
        assert(session.head()==3 && other_pause_bits==4);
    }
    assert(session.receive(1,2,0,0)==S::Arrival::accepted);
    order=0;bridge.dispatch().run(object,original);
    assert(advances==1 && !native_paused && services==21);
    assert(bridge.reservation().consume(1,2,GetCurrentThreadId(),500));
    std::ostringstream report;bridge.dispatch().access().write_prediction_resume(report);
    assert(report.str().find("prediction_native_pause_budget_releases=20\n")!=std::string::npos);
}
int main(){
    budget_pause_tests();
    missed_boundary_tests();missed_recovery_tests();admission_receipt_integration_tests();
    for(unsigned mode=0;mode<5;++mode){
        Fixture f;using S=sc6::research::BoundedRollbackSession<int,int>;S session;
        auto journal=std::make_unique<sc6::research::RollbackJournal<>>();assert(session.attach_journal(*journal));assert(session.begin(1,2));
        struct Adapter{int predict(int v){return v;}bool save(int& v){v=0;return true;}bool load(int){return true;}bool step(int,int){return true;}} adapter;
        for(unsigned i=0;i<3;++i)assert(session.advance(adapter,0));
        assert(session.admission_evidence(1,2).reason==5);
        if(mode==3)assert(session.request_handoff(1,2));
        if(mode==4)waiting=true;
        struct Context{S* session;unsigned mode,calls{};} context{&session,mode};
        sc6::research::CombatClockBridge<S,Clock> bridge(session);
        assert(bridge.attach_confirmations(&context,[](void*){return true;},nullptr,[](void* p){
            auto& c=*static_cast<Context*>(p);++c.calls;
            if(c.mode==2)return false;
            if(c.mode==0)return c.session->receive(1,2,0,0)==S::Arrival::accepted;
            return true;
        }));
        const auto object=reinterpret_cast<uintptr_t>(f.object.data());
        assert(bridge.bind(f.base,object,GetCurrentThreadId(),true,true,true,1,2,7,100,500));
        bridge.dispatch().run(object,original);
        assert(context.calls==(mode<3?1u:0u));
        assert(advances==(mode==0?1u:0u));assert(bridge.grace_recovered()==(mode==0?1u:0u));
        assert(bridge.restart_required()==(mode==2));
        if(mode==1){order=0;bridge.dispatch().run(object,original);assert(context.calls==1 && advances==0);}
        unsigned starts=0,ends=0;sc6::research::RollbackEvent event;
        while(journal->pop(event)){
            if(event.kind==sc6::research::RollbackEvent::receipt_grace_begin){++starts;assert(event.detail==5 && event.head==3);}
            if(event.kind==sc6::research::RollbackEvent::receipt_grace_end){++ends;
                assert(event.aux0==(mode==2?0u:1u));assert(event.detail==(mode==0?0u:5u));}
        }
        assert(starts==(mode<3?1u:0u) && ends==starts);
    }
    // Native waiting changes engine pause state even when its return is false.
    // Approved prediction releases only that state, retaining native wait history.
    for(unsigned reason=0;reason<5;++reason)for(unsigned old:{0u,3u})for(bool admit_step:{false,true}){
        auto fixture=std::make_unique<Fixture>();auto clock=std::make_unique<Clock>();auto& f=*fixture;auto& c=*clock;assert(f.bind(c));model_pause=true;service_reason=reason;other_pause_bits=13;
        f.put(0x3A8,old);c.service();const auto expected_count=reason?old+1:0;
        const auto result=c.finish(admit_step,admit_step);
        const bool released=admit_step && (reason==0 || reason==2) && (reason || old);
        assert(resume_call_count==unsigned(released));
        assert(other_pause_bits==13);
        assert(*reinterpret_cast<uint32_t*>(f.object.data()+0x3A8)==expected_count);
        assert(native_paused==(reason!=0 && !released));
        assert(result!=R::restart_required);
    }
    {
        // Five clock services before battle consumes its single reservation:
        // each must leave the already-authorized update runnable, never advance twice.
        auto fixture=std::make_unique<Fixture>();auto dispatch=std::make_unique<Dispatch>();auto& f=*fixture;auto& d=*dispatch;model_pause=true;service_reason=2;
        bool pending=false;
        assert(d.attach_gate([](void* p,const Clock::State&)noexcept{return !*static_cast<bool*>(p);}));
        assert(d.attach_prediction([](void*,const Clock::State&,bool)noexcept{return true;}));
        auto object=reinterpret_cast<uintptr_t>(f.object.data());
        assert(d.bind(f.base,object,GetCurrentThreadId(),true,true,true,&pending,
            [](void*,const Clock::State&)noexcept{return true;},notify));
        d.run(object,original);assert(advances==1 && !native_paused);
        pending=true;
        for(unsigned i=0;i<4;++i){order=0;d.run(object,original);assert(advances==1 && !native_paused);}
        assert(resume_call_count==5 && !d.restart_required());
        pending=false;order=0;d.run(object,original);assert(advances==2 && !native_paused);
        std::ostringstream out;d.access().write_prediction_resume(out);
        assert(out.str().find("prediction_native_pause_pending_releases=4\n")!=std::string::npos);
        // New transition reasons never acquire permission from an older reservation.
        pending=true;service_reason=1;order=0;d.run(object,original);
        assert(native_paused && advances==2 && resume_call_count==6);
    }
    {
        // Native-only comparison runs must never gain pause-release permission.
        auto fixture=std::make_unique<Fixture>();auto dispatch=std::make_unique<Dispatch>();auto& f=*fixture;auto& d=*dispatch;model_pause=true;service_reason=2;
        assert(d.attach_prediction([](void*,const Clock::State&,bool)noexcept{return true;},false));
        auto object=reinterpret_cast<uintptr_t>(f.object.data());
        assert(d.bind(f.base,object,GetCurrentThreadId(),true,true,true,nullptr,
            [](void*,const Clock::State&)noexcept{return true;},notify));
        d.run(object,original);assert(resume_call_count==0 && native_paused);
    }
    // A receipt arriving inside admission gets one nonblocking retry, never
    // another native service or a bypass of fresh gate/prediction eligibility.
    for(unsigned mode=0;mode<7;++mode){
        auto fixture=std::make_unique<Fixture>();auto dispatch=std::make_unique<Dispatch>();auto& f=*fixture;auto& d=*dispatch;
        struct Context {unsigned mode,late{},gates{},predictions{},retries{};bool queued{},truth{};} c{mode};
        assert(d.attach_gate([](void* p,const Clock::State&)noexcept{auto& c=*static_cast<Context*>(p);return !(++c.gates==2 && c.mode==3);}));
        assert(d.attach_prediction([](void* p,const Clock::State&,bool)noexcept{auto& c=*static_cast<Context*>(p);return !(++c.predictions==2 && c.mode==4);},mode!=6));
        assert(d.attach_late_service([](void* p)noexcept{auto& c=*static_cast<Context*>(p);++c.late;
            if(c.late==2 && c.mode==2)return false;
            c.truth=c.queued && c.mode!=1;return true;}));
        assert(d.attach_recovery([](void* p)noexcept{return static_cast<Context*>(p)->mode!=5;},
            [](void* p,const Clock::State&)noexcept{auto& c=*static_cast<Context*>(p);++c.retries;return c.truth;}));
        const auto object=reinterpret_cast<uintptr_t>(f.object.data());
        assert(d.bind(f.base,object,GetCurrentThreadId(),true,true,true,&c,
            [](void* p,const Clock::State&)noexcept{static_cast<Context*>(p)->queued=true;return false;},notify));
        d.run(object,original);
        assert(services==1 && advances==(mode==0?1u:0u));
        assert(c.late==((mode==5 || mode==6)?1u:2u));
        assert(c.retries==((mode==0 || mode==1)?1u:0u));
        assert(d.restart_required()==(mode==2));
        std::ostringstream report;d.write_recovery(report);
        assert(report.str().find("admission_receipt_recovered="+std::to_string(mode==0?1:0)+"\n")!=std::string::npos);
    }
    // A confirmation arrives during prediction evaluation, after the early drain.
    for(unsigned fault=0;fault<3;++fault){
        Fixture f;Dispatch d;struct Context {bool queued{},drained{};unsigned late{},admissions{},fault{};} c;
        c.fault=fault;
        assert(d.attach_prediction([](void* p,const Clock::State&,bool)noexcept{auto& c=*static_cast<Context*>(p);c.queued=true;return c.fault!=2;}));
        assert(d.attach_late_service([](void* p)noexcept{auto& c=*static_cast<Context*>(p);++c.late;c.drained=c.queued;return c.fault!=1;}));
        const auto object=reinterpret_cast<uintptr_t>(f.object.data());
        assert(d.bind(f.base,object,GetCurrentThreadId(),true,true,true,&c,
            [](void* p,const Clock::State&)noexcept{auto& c=*static_cast<Context*>(p);assert(c.drained);++c.admissions;return true;},notify));
        d.run(object,original);
        assert(c.late==(fault==2?0u:1u));assert(c.admissions==(fault==0?1u:0u));
        assert(advances==(fault==0?1u:0u));assert(d.restart_required()==(fault==1));
    }

    for(unsigned slot=1;slot<=2;++slot){
        Fixture f;f.put(0x39C,1u<<(slot-1));f.put(0x4400,(1u<<(slot-1))^3u);
        f.put(0x4410,105);f.put(0x3B8+(slot-1)*4,37);Clock c;assert(f.bind(c));
        uint32_t raw{};assert(!c.fresh_local(100,raw));assert(c.service()==R::ready);
        assert(c.fresh_local(100,raw) && raw==37);assert(!c.fresh_local(101,raw));
        assert(c.finish(true)==R::advanced);assert(!c.fresh_local(100,raw));
        c.input_consumed(100,37,true);std::ostringstream out;c.write_input_latency(out,true);
        assert(out.str().find("input_latency_matched=1\n")!=std::string::npos);
        assert(out.str().find("input_latency_invalid=0\n")!=std::string::npos);
        assert(*reinterpret_cast<uint32_t*>(f.object.data()+0x4410)==105);
    }

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
        Fixture f;Dispatch d;model_pause=true;service_reason=2;f.put(0x3A8,1);allow=true;
        assert(d.attach_prediction([](void*,const Clock::State& wait,bool native_wait)noexcept{return native_wait && wait.wait_reason==2;}));
        assert(d.bind(f.base,reinterpret_cast<uintptr_t>(f.object.data()),GetCurrentThreadId(),true,true,true,nullptr,admit,notify));
        d.run(reinterpret_cast<uintptr_t>(f.object.data()),original);
        assert(services==1 && waits==2 && advances==1 && notified==R::advanced);
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
        assert(!out.automatic_side(1,1));
        sc6::RoundFightReadiness fight{};fight.status=3;fight.round=1;fight.generation=9;fight.frame=100;
        fight.thread=GetCurrentThreadId();fight.vtable=0x3E873D0;fight.handler=0x384EE0;
        discovery.observe(f.base,object,1000,fight);
        bool selected_on_other_thread=false;
        std::thread control([&]{sc6::research::NativeClockDiscovery::Observation cached;
            selected_on_other_thread=discovery.read(1000,cached) && cached.thread!=GetCurrentThreadId() && cached.automatic_side(1,9)==1;
        });control.join();assert(selected_on_other_thread);
        assert(discovery.read(1000,out));
        assert(!out.automatic_side(2,9) && !out.automatic_side(1,10));
        for(unsigned status:{0u,1u,2u,4u,5u}){auto bad=out;bad.fight.status=status;assert(!bad.automatic_side(1,9));}
        auto bad=out;bad.fight.native_playback=1;assert(!bad.automatic_side(1,9));
        bad=out;bad.fight.round_result=1;assert(!bad.automatic_side(1,9));
        bad=out;bad.clock.local_mask=3;assert(!bad.automatic_side(1,9));
        auto intro=out;intro.fight.status=1;intro.fight.vtable=0x3E87378;intro.fight.handler=0x384040;
        assert(intro.preparation_side(1,9)==1 && !intro.automatic_side(1,9));
        assert(!intro.preparation_side(2,9) && !intro.preparation_side(1,10));
        for(unsigned variant=0;variant<5;++variant){auto invalid=intro;
            if(variant==0)invalid.fight.native_playback=1;
            if(variant==1)invalid.fight.round_result=1;
            if(variant==2)++invalid.fight.handler;
            if(variant==3)++invalid.fight.thread;
            if(variant==4)++invalid.fight.vtable;
            assert(!invalid.preparation_side(1,9));
        }
        ++fight.thread;discovery.observe(f.base,object,1000,fight);assert(discovery.read(1000,out)&&!out.automatic_side(1,9));
        --fight.thread;
        assert(services==0 && waits==0 && advances==0);
        assert(!discovery.read(999,out) && !out.valid);
        assert(!discovery.read(1251,out));
        f.object[0x4424]=1;discovery.observe(f.base,object,1001);
        assert(!discovery.read(1001,out));
        f.object[0x4424]=2;f.put(0x39C,2);f.put(0x4400,1);
        discovery.observe(f.base,object,1002,fight);assert(discovery.read(1002,out) && out.clock.local_mask==2 && out.automatic_side(1,9)==2);
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

