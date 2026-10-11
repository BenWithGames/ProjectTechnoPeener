#include "../src/CombatUpdateAdapter.hpp"
#include "../src/BoundedRollbackSession.hpp"
#include <cassert>
#include "../src/CombatBoundaryReservation.hpp"
#include "../src/SimulationBoundaryDriver.hpp"
#include "../src/ReservedBattleDriver.hpp"
#include "../src/SchedulerConfirmationQueue.hpp"
#include "../src/OwnedInputPublication.hpp"
#include <memory>
#include <iostream>
using namespace sc6::research;
static uint32_t simulationCounter;
static uint64_t value;
static unsigned remoteSlot;
static bool excess;
static sc6::NetworkInputComposer::Envelope last;
static void update(uintptr_t argument){
    const auto pointers=reinterpret_cast<const uintptr_t*>(argument);
    std::memcpy(last.data(),reinterpret_cast<const void*>(pointers[0]),8);
    std::memcpy(last.data()+2,reinterpret_cast<const void*>(pointers[1]),8);
    std::memcpy(last.data()+4,reinterpret_cast<const void*>(pointers[2]),24);
    value=(value*3+last[0]+last[1]*5+last[2]*7+last[3]*11+last[4])%1000003;
    simulationCounter+=excess?2:1;
}
struct Backend {
    struct Snapshot {uint32_t frame{};uint64_t value{};};
    bool rejectLoad{};
    bool capture(Snapshot& s,uint32_t expected){s={simulationCounter,value};return simulationCounter==expected;}
    bool restore(const Snapshot& s){if(rejectLoad)return false;simulationCounter=s.frame;value=s.value;return true;}
    uint32_t frame(const Snapshot& s)const{return s.frame;}
};
struct ClockFixture {
    CombatBoundaryReservation reserve;
    bool stopped{};
    bool active()const{return !stopped;}
    ClockFixture& dispatch(){return *this;}
    CombatBoundaryReservation& reservation(){return reserve;}
    bool restart_required()const{return stopped || reserve.restart_required();}
    void stop(){stopped=true;reserve.stop();}
};
int main(){
    {
        using Session=BoundedRollbackSession<CombatFrameInput,Backend::Snapshot>;
        Session session;assert(session.begin(1,2));SchedulerConfirmationQueue<Session,2> queue(session);
        assert(queue.bind(1,9,2,8,1,500));
        sc6::SchedulerInputSink sink{&queue,decltype(queue)::send};
        sc6::NetworkGameHandoff::Request input{1,9,1,500,2,8,{}};
        bool a=false,b=false,c=false;
        assert(sc6::submit_scheduler_input(sink,input,a)==sc6::SchedulerPublish::accepted && a);
        assert(sc6::submit_scheduler_input(sink,input,a)==sc6::SchedulerPublish::accepted);
        assert(sc6::submit_scheduler_input(sink,input,b)==sc6::SchedulerPublish::accepted && b);
        assert(sc6::submit_scheduler_input(sink,input,c)==sc6::SchedulerPublish::full && !c);
        assert(queue.drain());
        assert(sc6::submit_scheduler_input(sink,input,c)==sc6::SchedulerPublish::accepted && c);
        assert(queue.drain());input.peer_nonce=10;bool wrong=false;
        assert(sc6::submit_scheduler_input(sink,input,wrong)==sc6::SchedulerPublish::rejected && !wrong);
    }
    {
        using Session=BoundedRollbackSession<CombatFrameInput,Backend::Snapshot>;
        Session session;Backend backend;ClockFixture clock;
        simulationCounter=500;excess=false;value=0;
        CombatUpdateAdapter adapter(backend,update,&simulationCounter,GetCurrentThreadId(),2,500);
        using Driver=ReservedBattleDriver<Session,decltype(adapter),ClockFixture>;
        Driver driver(session,adapter,clock);
        auto shared=std::make_unique<sc6::observation::Shared>();
        shared->signature=sc6::observation::magic;shared->schema=sc6::observation::version;
        shared->nonce=1;shared->frequency=10000000;shared->status=1;
        OwnedInputPublication publication;assert(publication.bind(*shared,1,2,8,500));
        assert(driver.attach_publication(&publication,[](void* p,const Driver::Boundary& b){
            return static_cast<OwnedInputPublication*>(p)->publish(b.session,b.round,b.frame,b.local);
        }));
        SchedulerConfirmationQueue<Session> confirmations(session);
        assert(confirmations.bind(1,9,2,8,1,500));
        assert(clock.reserve.bind(1,2,GetCurrentThreadId(),7,100,500));
        unsigned reads=0;
        assert(driver.bind(1,2,&reads,[](void* p,uintptr_t,uintptr_t,Driver::Boundary& b){
            ++*static_cast<unsigned*>(p);b={1,2,simulationCounter,{}};return true;
        },true,true,true));
        for(unsigned i=0;i<3;++i){
            assert(clock.reserve.reserve_from(session,1,2,GetCurrentThreadId(),7,100+i));
            assert(clock.reserve.clock_completed(true));driver.route().run(17,23);
            assert(!driver.restart_required() && simulationCounter==501+i && session.head()==i+1);
        }
        assert(!clock.reserve.reserve_from(session,1,2,GetCurrentThreadId(),7,103));
        assert(clock.reserve.clock_completed(false) && !driver.restart_required());
        CombatFrameInput truth;truth.envelope[2]=0x400;
        assert(!confirmations.publish({1,10,1,500,2,8,truth.envelope}));
        assert(confirmations.publish({1,9,1,500,2,8,truth.envelope}));
        assert(confirmations.drain()); // no battle callback required while stalled
        assert(clock.reserve.reserve_from(session,1,2,GetCurrentThreadId(),7,103));
        assert(clock.reserve.clock_completed(true));driver.route().run(17,23);
        assert(!driver.restart_required() && simulationCounter==504 && session.head()==4);
        assert(session.replayed==3 && adapter.calls==7 && reads==4);
        assert(shared->count==4); // replayed frames never duplicate network records
        for(unsigned i=0;i<4;++i){assert(shared->records[i].data[0]==500+i);assert(shared->rounds[i].number==2 && shared->rounds[i].event_counter==8);}
        // An unreserved extra entry must not advance, and stops both boundaries.
        driver.route().run(17,23);
        assert(driver.restart_required() && clock.stopped && simulationCounter==504 && adapter.calls==7);
        driver.route().run(17,23);assert(adapter.calls==7);
        assert(shared->count==4);
    }
    {
        std::array<std::byte,40> first{},second{};
        uint32_t attack=1,negative_zero=0x80000000;
        std::memcpy(second.data(),&attack,4);std::memcpy(second.data()+24,&negative_zero,4);
        const auto transition=RecordedInput::transition(first,second);
        assert(transition.valid && transition.changed_words==65);
        assert(!RecordedInput::compatible(first,second)); // Legacy same-input gate remains strict.
        for(uint32_t invalid : {0x7FC00000u,0x7F800000u,0x3F800001u,0xBF800001u}){
            std::memcpy(second.data()+28,&invalid,4);assert(!RecordedInput::transition(first,second).valid);
        }
        simulationCounter=10;Backend inputBackend;
        CombatUpdateAdapter inputAdapter(inputBackend,&update,&simulationCounter,GetCurrentThreadId(),2,10);
        CombatFrameInput local,remote;local.envelope[6]=0x7FC00000u;
        assert(!inputAdapter.step(local,remote) && inputAdapter.calls==0 && simulationCounter==10);
        assert(inputAdapter.failure==decltype(inputAdapter)::input);
    }
    {
        CombatBoundaryReservation reservation;
        assert(reservation.bind(1,2,3,4,50,100));
        assert(!reservation.reserve(1,2,3,4,50,false));
        assert(reservation.clock_completed(false));
        assert(!reservation.restart_required());
        for(unsigned i=0;i<10;++i){
            assert(reservation.reserve(1,2,3,4,50+i,true));
            assert(reservation.clock_completed(true));
            assert(reservation.consume(1,2,3,100+i));
        }
        assert(!reservation.consume(1,2,3,109) && reservation.restart_required());
        CombatBoundaryReservation missed;assert(missed.bind(1,2,3,4,50,100));
        assert(missed.reserve(1,2,3,4,50,true));assert(missed.clock_completed(true));
        assert(!missed.reserve(1,2,3,4,51,true) && missed.restart_required());
        CombatBoundaryReservation wrong;assert(wrong.bind(1,2,3,4,50,100));
        assert(wrong.reserve(1,2,3,4,50,true));assert(wrong.clock_completed(true));
        assert(!wrong.consume(1,2,3,50)); // Clock frame cannot substitute for battle frame.
        CombatBoundaryReservation failure;assert(failure.bind(1,2,3,4,50,100));
        assert(failure.reserve(1,2,3,4,50,true));assert(!failure.clock_completed(false));
    }
    for(remoteSlot=1;remoteSlot<=2;++remoteSlot){
        simulationCounter=100;value=0;Backend backend;
        CombatUpdateAdapter adapter(backend,&update,&simulationCounter,GetCurrentThreadId(),remoteSlot,100);
        BoundedRollbackSession<CombatFrameInput,Backend::Snapshot> session;assert(session.begin(1,2));
        CombatFrameInput local,remote;local.envelope={1,2,3,4,5,6,7,8,9,10};
        remote.envelope.fill(999);const auto off=(remoteSlot-1)*2;
        remote.envelope[off]=8;remote.envelope[off+1]=1;
        remote=CombatFrameInput::remote(remote.envelope,remoteSlot);
        assert(remote.envelope[4]==0);
        assert(session.advance(adapter,local));
        assert(last[off]==0 && last[off+1]==0 && last[4]==5);
        assert(session.receive(1,2,0,remote)==decltype(session)::Arrival::accepted);
        assert(session.reconcile(adapter));assert(simulationCounter==101 && adapter.calls==2);
        assert(last[off]==8 && last[off+1]==1);
        for(unsigned i=0;i<10;++i)if(i!=off && i!=off+1)assert(last[i]==local.envelope[i]);
        auto predicted=adapter.predict(remote);assert(predicted.envelope[off]==8 && predicted.envelope[off+1]==0);
        Backend::Snapshot s;assert(adapter.save(s));++simulationCounter;assert(!adapter.step(local,remote));
        assert(adapter.failure==decltype(adapter)::counter && adapter.calls==2);
        assert(!adapter.load(s));
    }
    simulationCounter=1;Backend backend;CombatUpdateAdapter bad(backend,&update,&simulationCounter,GetCurrentThreadId(),2,1);
    excess=true;assert(!bad.step({},{}));assert(bad.failure==decltype(bad)::update && bad.calls==1);
    assert(!bad.step({},{}));
    excess=false;simulationCounter=10;value=0;Backend clockBackend;
    CombatUpdateAdapter clockAdapter(clockBackend,&update,&simulationCounter,GetCurrentThreadId(),2,10);
    using CombatSession=BoundedRollbackSession<CombatFrameInput,Backend::Snapshot>;
    CombatSession clockSession;SimulationBoundaryDriver driver(clockSession);
    using D=decltype(driver)::Disposition;CombatFrameInput neutral;
    assert(driver.tick(1,1,7,clockAdapter,neutral)==D::pass_through);
    assert(!driver.arm(1,1,7,true,false,true,true)); // Ranked/unverified context.
    assert(!driver.arm(1,1,7,true,true,true,false)); // No clock ownership.
    assert(driver.arm(1,1,7,true,true,true,true));
    for(unsigned i=0;i<3;++i)assert(driver.tick(1,1,7,clockAdapter,neutral)==D::consumed);
    const auto before=clockAdapter.calls;
    assert(driver.tick(1,1,7,clockAdapter,neutral)==D::waiting);
    assert(clockAdapter.calls==before && simulationCounter==13);
    for(unsigned i=0;i<3;++i)assert(clockSession.receive(1,1,i,neutral)==CombatSession::Arrival::accepted);
    assert(driver.tick(1,1,7,clockAdapter,neutral)==D::consumed);
    assert(driver.tick(1,2,7,clockAdapter,neutral)==D::restart_required);
    const auto after=clockAdapter.calls;
    assert(driver.tick(1,1,7,clockAdapter,neutral)==D::restart_required && clockAdapter.calls==after);
    simulationCounter=20;value=0;Backend reservedBackend;
    CombatUpdateAdapter reservedAdapter(reservedBackend,&update,&simulationCounter,GetCurrentThreadId(),2,20);
    CombatSession reservedSession;SimulationBoundaryDriver reservedDriver(reservedSession);
    CombatBoundaryReservation reservation;
    assert(reservedDriver.arm(3,4,7,true,true,true,true));assert(reservation.bind(3,4,7,9,100,20));
    for(unsigned i=0;i<3;++i){
        assert(reservation.reserve(3,4,7,9,100+i,true));assert(reservation.clock_completed(true));
        assert(reservedDriver.tick_reserved(reservation,3,4,7,20+i,reservedAdapter,neutral)==D::consumed);
    }
    assert(reservation.reserve(3,4,7,9,103,true));assert(reservation.clock_completed(true));
    assert(reservedDriver.tick_reserved(reservation,3,4,7,23,reservedAdapter,neutral)==D::restart_required);
    assert(reservedAdapter.calls==3 && simulationCounter==23);
    simulationCounter=30;value=0;Backend admittedBackend;
    CombatUpdateAdapter admittedAdapter(admittedBackend,&update,&simulationCounter,GetCurrentThreadId(),2,30);
    CombatSession admittedSession;SimulationBoundaryDriver admittedDriver(admittedSession);
    CombatBoundaryReservation admitted;
    assert(admittedDriver.arm(5,6,7,true,true,true,true));assert(admitted.bind(5,6,7,9,100,30));
    for(unsigned i=0;i<3;++i){
        assert(admitted.reserve_from(admittedSession,5,6,7,9,100+i));assert(admitted.clock_completed(true));
        assert(admittedDriver.tick_reserved(admitted,5,6,7,30+i,admittedAdapter,neutral)==D::consumed);
    }
    assert(!admitted.reserve_from(admittedSession,5,6,7,9,103));
    assert(admitted.clock_completed(false) && !admitted.restart_required());
    assert(admittedAdapter.calls==3 && simulationCounter==33);
    CombatFrameInput changed;changed.envelope[2]=8;
    assert(admittedSession.receive(5,6,2,changed)==CombatSession::Arrival::accepted);
    assert(!admitted.reserve_from(admittedSession,5,6,7,9,103)); // Gap at oldest frame still blocks.
    assert(admitted.clock_completed(false));
    assert(admittedSession.receive(5,6,0,changed)==CombatSession::Arrival::accepted);
    assert(admitted.reserve_from(admittedSession,5,6,7,9,103));
    assert(admittedSession.corrections==0 && admittedAdapter.calls==3); // Admission never replays.
    assert(admitted.clock_completed(true));
    assert(admittedDriver.tick_reserved(admitted,5,6,7,33,admittedAdapter,neutral)==D::consumed);
    assert(admittedSession.corrections==1 && admittedSession.replayed==3 && simulationCounter==34);
    assert(!admitted.reserve_from(admittedSession,5,99,7,9,104) && admitted.restart_required());
    std::cout<<"PASS: owned update composition for both slots, delayed correction, local/aux preservation, edge clearing, counter and failure latching\n";
}
