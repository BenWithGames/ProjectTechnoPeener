#include "../src/BoundedRollbackSession.hpp"
#include "../src/ContinuousRollbackReport.hpp"
#include "../src/PostBattleSettlement.hpp"
#include "../src/PostBattleHookGate.hpp"
#include "../src/NativeOuterCompletion.hpp"
#include "PostBattleContinuationTest.hpp"
#include "NativeOuterContinuationFrameTest.hpp"
#include <sstream>
#include <cassert>
#include <iostream>
struct Input {int held{},edge{};bool operator==(const Input&)const=default;};
struct Adapter {
    long long value{};unsigned steps{},loads{};bool failSave{},failLoad{},failStep{};
    Input predict(Input prior){return {prior.held,0};}
    bool save(long long& snapshot){snapshot=value;return !failSave;}
    bool load(long long snapshot){++loads;value=snapshot;return !failLoad;}
    bool step(Input local,Input remote){++steps;value=(value*3+local.held+remote.held*7+remote.edge*11)%1000003;return !failStep;}
};
using Session=sc6::research::BoundedRollbackSession<Input,long long>;
struct BoundaryAdapter {
    struct Snapshot {int value{};unsigned frame{};};
    int value{};unsigned frame{},endpoint{},loads{};bool fault{},fail_boundary{};
    unsigned expected_frame()const{return frame;}
    bool failed()const{return fault;}
    bool begin_boundary(Input){endpoint=frame;return !fail_boundary;}
    Input predict(Input previous){return {previous.held,0};}
    bool save(Snapshot& out){out={value,frame};return true;}
    bool load(Snapshot saved){
        if(endpoint!=frame){fault=true;return false;}
        ++loads;value=saved.value;frame=saved.frame;return true;
    }
    bool step(Input local,Input remote){value+=local.held+remote.held+remote.edge;++frame;return true;}
};
static void post_battle_tests(){
    using S=sc6::research::BoundedRollbackSession<Input,BoundaryAdapter::Snapshot>;
    using Barrier=sc6::research::PostBattleSettlement;
    using R=Barrier::Result;
    {
        Barrier blocking;uint64_t tick=0;unsigned polls=0,pauses=0;bool caller_resumed=false;
        const auto result=blocking.wait([&]{assert(!caller_resumed);return ++polls==4?R::ready:R::waiting;},
            [&]{return tick;},[&]{assert(!caller_resumed);++pauses;++tick;},10);
        caller_resumed=true;
        assert(result==R::ready && polls==4 && pauses==3);
        Barrier timeout;tick=0;
        assert(timeout.wait([]{return R::waiting;},[&]{return tick;},[&]{++tick;},3)==R::restart_required);
        assert(timeout.failure()==Barrier::Failure::timeout);
        assert(timeout.wait([]{return R::ready;},[]{return 0u;},[]{},3)==R::restart_required);
        Barrier reentry;
        assert(reentry.wait([&]{
            assert(reentry.wait([]{return R::ready;},[]{return 0u;},[]{},3)==R::restart_required);
            return R::ready;
        },[]{return 0u;},[]{},3)==R::restart_required);
        assert(reentry.failure()==Barrier::Failure::boundary);
        Barrier exception;
        assert(exception.wait([]()->R{throw 1;},[]{return 0u;},[]{},3)==R::restart_required);
        assert(exception.failure()==Barrier::Failure::exception);
    }
    // Incomplete truth waits at exactly the same endpoint. The last correction
    // requires refreshing the native backend endpoint before its load.
    S s;BoundaryAdapter a,reference;Barrier barrier;
    assert(s.begin(11,12) && s.advance(a,{1,0}) && s.advance(a,{2,0}));
    assert(a.endpoint==0 && a.frame==2);
    auto poll=[&]{return barrier.poll(s,a,[]{return true;},Input{},11,12,7,2,true);};
    assert(poll()==R::waiting && a.endpoint==2);
    assert(s.receive(11,12,1,{4,1})==S::Arrival::accepted);
    assert(poll()==R::waiting && a.frame==2 && a.loads==1);
    assert(s.receive(11,12,0,{3,1})==S::Arrival::accepted);
    assert(poll()==R::ready && a.frame==2 && s.head()==2);
    reference.step({1,0},{3,1});reference.step({2,0},{4,1});
    assert(a.value==reference.value);
    const auto loads=a.loads;
    assert(poll()==R::ready && a.loads==loads); // Never repeats a clean replay.
    assert(!s.advance(a,{9,0})); // No extra frame while native commit is pending.
    // Every rejection below must stop before an unsafe load/step.
    for(unsigned fault=0;fault<6;++fault){
        S broken;BoundaryAdapter engine;Barrier guard;
        assert(broken.begin(11,12) && broken.advance(engine,{}));
        engine.fail_boundary=fault==3;
        auto drain=[&]{if(fault==4)throw 1;return fault!=2;};
        assert(guard.poll(broken,engine,drain,Input{},11,12,fault==0?0u:7u,
            fault==1?2u:1u,fault!=5)==R::restart_required);
        assert(engine.frame==1 && !engine.loads);
        assert(guard.poll(broken,engine,[]{return true;},Input{},11,12,7,1,true)==R::restart_required);
    }
    S changed;BoundaryAdapter engine;Barrier guard;
    assert(changed.begin(11,12) && changed.advance(engine,{}));
    assert(guard.poll(changed,engine,[]{return true;},Input{},11,12,7,1,true)==R::waiting);
    assert(guard.poll(changed,engine,[]{return true;},Input{},11,13,7,1,true)==R::restart_required);
    S recursive;BoundaryAdapter nested_engine;Barrier nested;
    assert(recursive.begin(11,12));
    auto reenter=[&]{
        assert(nested.poll(recursive,nested_engine,[]{return true;},Input{},11,12,7,0,true)==R::restart_required);
        return true;
    };
    assert(nested.poll(recursive,nested_engine,reenter,Input{},11,12,7,0,true)==R::restart_required);
}
int main(){
    {Session s;Adapter a;sc6::research::RollbackJournal<> j;assert(s.attach_journal(j));
     assert(s.begin(42,1));assert(s.admission_evidence(42,1).reason==0);
     for(int i=0;i<3;++i)assert(s.advance(a,{}));
     auto e=s.admission_evidence(42,1);assert(e.reason==5 && e.head==3 && e.confirmed==0);
     s.trace_admission(42,1);assert(s.head()==3);
     assert(s.receive(42,1,0,{})==Session::Arrival::accepted);
     e=s.admission_evidence(42,1);assert(e.reason==0 && e.confirmed==1 && e.settled==0);
     assert(s.request_handoff(42,1));assert(s.admission_evidence(42,1).reason==2);
     assert(s.admission_evidence(99,1).reason==1);
     s.trace_confirmation(0,100,200);sc6::research::RollbackEvent row;bool saw=false;
     while(j.pop(row))if(row.kind==sc6::research::RollbackEvent::confirmation_delivery){assert(row.aux0==100 && row.aux1==200 && row.qpc);saw=true;}
     assert(saw);
    }

    {
        using Scope=sc6::research::NativeOuterCompletion;
        struct State {uint64_t token{};unsigned completions{};} state;
        Scope scope;
        assert(scope.enter(7,11));
        state.token=scope.claim(7,11,&state,[](void* p,const Scope::Receipt& receipt){
            auto& s=*static_cast<State*>(p);assert(receipt.matches(11,s.token,7));++s.completions;return true;
        });
        assert(state.token && !state.completions);
        assert(scope.enter(7,22) && scope.returned(7,22) && !state.completions);
        assert(scope.returned(7,11) && state.completions==1);
        assert(!scope.returned(7,11) && state.completions==1);
        Scope bad;assert(bad.enter(7,11));assert(!bad.returned(8,11) && bad.failed());
        Scope order;assert(order.enter(7,11) && order.enter(7,22));assert(!order.returned(7,11));
        Scope throws;assert(throws.enter(7,11));
        assert(throws.claim(7,11,&state,[](void*,const Scope::Receipt&)->bool{throw 1;}));
        assert(!throws.returned(7,11) && throws.failed());
    }
    {
        using Gate=sc6::research::PostBattleHookGate;
        unsigned calls=0;
        Gate good(7,&calls,[](void* p,uintptr_t outer,uintptr_t input){
            assert(outer==11 && input==22);++*static_cast<unsigned*>(p);return Gate::Result::permit;
        });
        assert(good.invoke(7,11,22) && good.permitted==1 && calls==1);
        assert(!good.invoke(8,11,22) && calls==1);
        assert(!good.invoke(7,11,22) && calls==1);
        Gate failed(7,&calls,[](void* p,uintptr_t,uintptr_t){++*static_cast<unsigned*>(p);return Gate::Result::failed;});
        assert(!failed.invoke(7,11,22) && !failed.invoke(7,11,22) && calls==2);
        assert(failed.failure==static_cast<unsigned>(Gate::Failure::callback));
        unsigned pauses=0;bool continued=false;
        struct EndHarness {};
        try {
            failed.enter(7,11,22,[&]{if(++pauses==3)throw EndHarness{};});
            continued=true;
        }catch(const EndHarness&){}
        assert(!continued && pauses==3 && failed.contained==1 && calls==2);
        Gate throws(7,&calls,[](void*,uintptr_t,uintptr_t)->Gate::Result{throw 1;});
        assert(!throws.invoke(7,11,22) && throws.failure==static_cast<unsigned>(Gate::Failure::exception));
        Gate* recursive=nullptr;
        Gate reentry(7,&recursive,[](void* p,uintptr_t outer,uintptr_t input){
            assert(!(*static_cast<Gate**>(p))->invoke(7,outer,input));return Gate::Result::permit;
        });
        recursive=&reentry;
        assert(!reentry.invoke(7,11,22) && reentry.permitted==0 && reentry.failure==static_cast<unsigned>(Gate::Failure::reentry));
    }
    test_post_battle_continuation();
    test_native_outer_continuation_frame();
    post_battle_tests();
    {
        Session rearm;Adapter engine;
        assert(rearm.begin(10,20) && rearm.settled());
        assert(!rearm.begin(10,20)); // Never erase/reuse an active identity.
        assert(rearm.advance(engine,{1,0}) && !rearm.settled());
        assert(!rearm.begin(10,21) && rearm.head()==1);
        assert(rearm.receive(10,20,0,{2,1})==Session::Arrival::accepted);
        assert(!rearm.settled() && !rearm.begin(10,21)); // Truth still needs replay.
        assert(rearm.reconcile(engine) && rearm.settled());
        assert(rearm.begin(10,21) && rearm.head()==0);
        assert(rearm.receive(10,20,0,{2,1})==Session::Arrival::wrong_identity);
        assert(rearm.advance(engine,{}));
        assert(rearm.receive(10,21,0,{})==Session::Arrival::accepted);
        assert(!rearm.settled() && !rearm.begin(10,22)); // Equal prediction still needs retirement.
        assert(rearm.reconcile(engine) && rearm.settled() && rearm.begin(10,22));
    }
    sc6::research::RollbackJournal<2> tiny;
    tiny.push(1,2,0,0,1);tiny.push(1,2,1,1,1);tiny.push(1,2,2,2,1);
    assert(tiny.dropped()==1);sc6::research::RollbackEvent e;
    assert(tiny.pop(e) && e.sequence==0);assert(tiny.pop(e) && e.sequence==1);assert(!tiny.pop(e));
    tiny.push(1,2,3,3,1);assert(tiny.pop(e) && e.sequence==3);
    sc6::research::RollbackJournal<> journal;Session logged;Adapter engine;
    assert(logged.attach_journal(journal) && logged.begin(4,5));
    assert(!logged.attach_journal(journal));
    assert(logged.advance(engine,{}));assert(logged.receive(4,5,0,{1,1})==Session::Arrival::accepted);
    assert(logged.reconcile(engine));
    unsigned beginCount=0,endCount=0;
    while(journal.pop(e)){if(e.kind==sc6::research::RollbackEvent::correction_begin){++beginCount;assert(e.frame==0 && e.detail==1);}
        if(e.kind==sc6::research::RollbackEvent::correction_end)++endCount;}
    assert(beginCount==1 && endCount==1 && journal.dropped()==0);
    assert(logged.advance(engine,{}));assert(logged.receive(4,5,1,{2,1})==Session::Arrival::accepted);
    engine.failLoad=true;assert(!logged.reconcile(engine));bool failureLogged=false;
    while(journal.pop(e))if(e.kind==sc6::research::RollbackEvent::failure){failureLogged=true;assert(e.detail==4 && e.frame==1);}
    assert(failureLogged);
    // Late reordered truth must converge to an independently stepped reference.
    Session s;Adapter a,reference;assert(s.begin(1,2));
    Input truth[3]={{1,1},{2,2},{0,0}};
    for(unsigned n=0;n<3;++n){assert(s.advance(a,{int(n),0}));assert(reference.step({int(n),0},truth[n]));}
    assert(!s.advance(a,{}));assert(s.state==Session::State::waiting && a.steps==3);
    assert(s.receive(1,2,2,truth[2])==Session::Arrival::accepted);
    assert(s.receive(1,2,0,truth[0])==Session::Arrival::accepted);
    assert(s.receive(1,2,1,truth[1])==Session::Arrival::accepted);
    assert(s.reconcile(a));assert(a.value==reference.value && s.replayed==3 && s.corrections==1);
    assert(s.receive(1,2,1,truth[1])==Session::Arrival::duplicate);
    assert(s.reconcile(a) && a.loads==1);
    // Repeated ring reuse, with a fresh mismatch every frame; no capacity stop.
    for(unsigned n=3;n<100;++n){
        a.value=reference.value=0; // Keep arithmetic bounded; next save captures it.
        Input remote{int(n%3),1};
        assert(s.advance(a,{1,0}));assert(s.receive(1,2,n,remote)==Session::Arrival::accepted);
        assert(s.reconcile(a));assert(reference.step({1,0},remote));assert(a.value==reference.value);
    }
    assert(s.head()==100);
    assert(s.receive(9,2,100,{})==Session::Arrival::wrong_identity);
    assert(s.receive(1,3,100,{})==Session::Arrival::wrong_identity);
    assert(s.receive(1,2,200,{})==Session::Arrival::too_far);
    assert(s.begin(1,3));assert(s.head()==0);assert(s.receive(1,2,0,{})==Session::Arrival::wrong_identity);
    // Hold prediction clears edge bits; late correction propagates hold into
    // subsequent unconfirmed frames instead of preserving obsolete predictions.
    Session hold;Adapter h;assert(hold.begin(1,1));
    assert(hold.advance(h,{}));assert(hold.advance(h,{}));assert(hold.advance(h,{}));
    assert(hold.receive(1,1,0,{2,1})==Session::Arrival::accepted);assert(hold.reconcile(h));
    Adapter expected;expected.step({},{2,1});expected.step({},{2,0});expected.step({},{2,0});
    assert(h.value==expected.value);
    assert(hold.advance(h,{})); // oldest unresolved now at depth3
    const auto steps=h.steps;assert(!hold.advance(h,{}));assert(h.steps==steps);
    // An exact retained retransmission does not trigger a spurious restart.
    assert(hold.receive(1,1,0,{2,1})==Session::Arrival::duplicate);
    assert(hold.state!=Session::State::restart_required);
    // Four-frame-old conflicting input is terminal, never clamped or silently skipped.
    assert(hold.receive(1,1,0,{3,0})==Session::Arrival::expired);
    assert(!hold.advance(h,{}) && !hold.begin(2,2));
    for(unsigned failure=0;failure<3;++failure){
        Session broken;Adapter b;assert(broken.begin(1,1));assert(broken.advance(b,{}));
        assert(broken.receive(1,1,0,{1,0})==Session::Arrival::accepted);
        b.failLoad=failure==0;b.failSave=failure==1;b.failStep=failure==2;
        assert(!broken.reconcile(b));assert(broken.state==Session::State::restart_required);
        const auto count=b.steps;assert(!broken.advance(b,{}) && b.steps==count);
    }
    Session conflict;Adapter c;assert(conflict.begin(1,1));
    assert(conflict.receive(1,1,0,{1,0})==Session::Arrival::accepted);
    assert(conflict.receive(1,1,0,{2,0})==Session::Arrival::conflict);
    assert(!conflict.advance(c,{}));
    // Deliver inputs after 1..6 wall-clock ticks. The simulation must wait
    // through network gaps, then converge without ever replaying beyond3.
    Session stream;Adapter actual,baseline;assert(stream.begin(4,5));
    struct Pending {uint64_t frame{},due{};Input input{};bool active{};};
    Pending pending[8]{};unsigned delivered=0;
    for(uint64_t tick=0;delivered<7200 && tick<60000;++tick){
        for(auto& p:pending)if(p.active && p.due<=tick){
            assert(stream.receive(4,5,p.frame,p.input)==Session::Arrival::accepted);
            p.active=false;++delivered;
        }
        const auto forecast=stream.admission(4,5);
        assert(stream.reconcile(actual));
        assert(forecast==stream.admission(4,5));
        if(stream.head()<7200){const auto frame=stream.head();
            const bool advanced=stream.advance(actual,{1,0});
            assert(advanced==(forecast==Session::Admission::ready));
            if(advanced){
                baseline.step({1,0},{int(frame%3),int(frame%2)});
                auto& p=pending[frame%8];assert(!p.active);
                p={frame,tick+1+(frame*7)%6,{int(frame%3),int(frame%2)},true};
            }
        }

    }
    assert(delivered==7200 && stream.head()==7200 && stream.stalls>0);
    assert(actual.value==baseline.value);
    // A pre-handoff drain must apply the final correction without advancing
    // another frame. Packet receipt and equal predictions both need settlement.
    for(bool changed : {false,true}){
        Session drain;Adapter da,db;assert(drain.begin(31,32));
        assert(drain.advance(da,{1,0}));assert(drain.advance(da,{2,0}));
        assert(!drain.request_handoff(31,33));
        assert(drain.request_handoff(31,32));
        assert(drain.admission(31,32)==Session::Admission::waiting);
        assert(!drain.settle_handoff(da) && !drain.handoff_ready());
        assert(!drain.advance(da,{3,0}) && drain.head()==2);
        const Input remote{changed?2:0,0};
        assert(drain.receive(31,32,1,remote)==Session::Arrival::accepted);
        assert(!drain.settle_handoff(da));
        assert(drain.receive(31,32,0,remote)==Session::Arrival::accepted);
        assert(!drain.handoff_ready());
        assert(drain.settle_handoff(da) && drain.handoff_ready() && drain.head()==2);
        assert(db.step({1,0},remote) && db.step({2,0},remote));assert(da.value==db.value);
        assert(drain.begin(31,33) && drain.admission(31,33)==Session::Admission::ready);
    }
    // Reports must distinguish a started correction from a completed one,
    // retain the failing frame/reason, and expose dropped evidence.
    sc6::research::RollbackJournal<4> evidence;
    evidence.push(1,2,10,13,sc6::research::RollbackEvent::correction_begin,3);
    evidence.push(1,2,10,13,sc6::research::RollbackEvent::failure,4);
    sc6::research::ContinuousRollbackReport report;std::ostringstream rows,summary;
    report.drain(evidence,rows);report.write(summary);
    assert(summary.str().find("continuous_corrections_completed=0\n")!=std::string::npos);
    assert(summary.str().find("continuous_correction_pending=1\n")!=std::string::npos);
    assert(summary.str().find("continuous_failure_frame=10\n")!=std::string::npos);
    assert(summary.str().find("continuous_failure_reason=4\n")!=std::string::npos);
    sc6::research::RollbackJournal<2> overflow;
    overflow.push(1,2,0,0,sc6::research::RollbackEvent::advance,1);
    overflow.push(1,2,0,1,sc6::research::RollbackEvent::correction_end,1);
    overflow.push(1,2,1,1,sc6::research::RollbackEvent::advance);
    sc6::research::ContinuousRollbackReport dropped;summary.str("");
    dropped.drain(overflow,rows);dropped.write(summary);
    assert(summary.str().find("continuous_events_dropped=1\n")!=std::string::npos);
    assert(summary.str().find("continuous_replayed_frames=1\n")!=std::string::npos);
    std::cout<<"PASS: bounded correction convergence, ring reuse, reordered input, edge prediction, stalls, identity and terminal failures\n";
}

