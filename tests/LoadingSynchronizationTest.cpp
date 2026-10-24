#include "../src/LoadingReadinessObserver.hpp"
#include <cassert>
#include <iostream>
using namespace sc6::research::loading_sync;
void setup(Session& a,Session& b){a.local=11;a.remote=22;a.peer=2;a.build=99;b.local=22;b.remote=11;b.peer=1;b.build=99;}
int main(){
    for(unsigned loss=0;loss<9;++loss){
        Session a,b;setup(a,b);unsigned sends=0;bool releasedA=false,releasedB=false;
        for(uint64_t t=100;t<4000;t+=10){
            if(!releasedA)releasedA=!a.hold(1000,10,t);
            if(t>=400 && !releasedB)releasedB=!b.hold(2000,20,t);
            a.advance(t);b.advance(t);
            auto x=a.outgoing(),y=b.outgoing();
            // Drop a different one of the first protocol steps in each run.
            if(sends++!=loss)assert(b.receive(1,x,x.size(),t));
            if(sends++!=loss)assert(a.receive(2,y,y.size(),t));
            if(a.admitted() && b.admitted())break;
        }
        assert(releasedA && releasedB && a.admitted() && b.admitted());
        assert(a.withheld.load()>0 && b.withheld.load()>0);
        auto stale=a.outgoing();Session::put(stale,8,999);assert(!b.receive(1,stale,stale.size(),4100));
        stale=a.outgoing();assert(!b.receive(9,stale,stale.size(),4100));
        Session::put(stale,40,1);assert(!b.receive(1,stale,stale.size(),4100));
    }
    {Session a,b;setup(a,b);assert(a.hold(1,1,100));assert(!a.hold(1,1,5100));assert(a.reason==Session::deadline);
     auto x=a.outgoing();assert(b.receive(1,x,x.size(),5100));assert(b.cancelled());assert(!b.hold(2,2,5200));
     Session::put(x,32,4);assert(b.receive(1,x,x.size(),5300));assert(!b.admitted());}
    {Session a,b;setup(a,b);assert(a.hold(1,1,100));auto p=a.outgoing();assert(b.receive(1,p,p.size(),120));
     b.advance(5120);assert(b.cancelled());assert(!b.hold(2,2,5200));}
    {Session a,b;setup(a,b);assert(a.hold(1,1,100));assert(!a.hold(2,1,110));assert(a.reason==Session::identity);}
    {Session a,b;setup(a,b);assert(a.hold(1,1,100));assert(!a.hold(1,2,110));assert(a.reason==Session::wrong_thread);}
    {Session a,b;setup(a,b);assert(a.hold(1,1,100));assert(!a.hold(1,1,90));assert(a.reason==Session::clock_reversed);}
    {Session a,b;setup(a,b);assert(a.hold(1,1,100));auto x=b.outgoing();Session::put(x,32,7);assert(!a.receive(2,x,x.size(),120));
     Session::put(x,32,4);Session::put(x,24,98);assert(!a.receive(2,x,x.size(),120));assert(!a.admitted());}
    {Session a,b;setup(a,b);a.stage=4;a.peer_stage=4;a.sealed=true;
     assert(!a.hold(999,88,9000));b.cancel(Session::deadline);auto x=b.outgoing();assert(a.receive(2,x,x.size(),9000));assert(a.cancelled());}
    auto* first=bind(101,102,2,99);assert(first && bind(101,102,2,99)==first);assert(!bind(101,103,2,99));
    clear(999);assert(current==first);clear(101);assert(current==nullptr);
    auto* second=bind(201,202,2,99);assert(second && second!=first);assert(first->local==101);

    { // Exercise the actual native adapter's caller/state/object restrictions.
        namespace obs=sc6::research::loading_readiness_observer;
        auto* h=bind(301,302,2,99);assert(h);obs::base=0x140000000ULL;
        assert(obs::gate_result(100,true,obs::base+0x999,1));assert(!h->entered.load());
        assert(obs::gate_result(100,true,obs::base+0x3FBE50,3));assert(!h->entered.load());
        assert(!obs::gate_result(100,false,obs::base+0x3FBE50,1));assert(!h->entered.load());assert(!obs::last_loading_tick.load());
        assert(!obs::gate_result(100,true,obs::base+0x3FBE50,1));assert(h->entered.load());assert(!obs::last_loading_tick.load());
        // An unrelated object/UE wrapper cannot inherit the hold.
        assert(obs::alternate_result(101,true,obs::base+0x1E67216,0x1E67216));
        assert(obs::alternate_result(100,true,obs::base+1,0x1E67216));
        h->cancel(Session::deadline);assert(obs::gate_result(100,true,obs::base+0x3FBE50,1));
        clear(301);
    }
    std::cout<<"PASS: loading readiness agreement, loss retries, timeout/native fallback, stale peer/build rejection, owner binding and stable match lifetime\n";
}
