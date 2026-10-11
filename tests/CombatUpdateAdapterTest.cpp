#include "../src/CombatUpdateAdapter.hpp"
#include "../src/BoundedRollbackSession.hpp"
#include <cassert>
#include "../src/SimulationBoundaryDriver.hpp"
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
int main(){
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
    std::cout<<"PASS: owned update composition for both slots, delayed correction, local/aux preservation, edge clearing, counter and failure latching\n";
}
