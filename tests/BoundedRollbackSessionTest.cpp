#include "../src/BoundedRollbackSession.hpp"
#include "../src/ContinuousRollbackReport.hpp"
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
int main(){
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
    // Four-frame-old input is terminal, never clamped or silently skipped.
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

