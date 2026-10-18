#pragma once
#include "../src/CorrectionCycleBank.hpp"
#include "../src/RepeatedCorrectionSession.hpp"
namespace cycle_bank_test {
struct Runner {
    sc6::research::StartupCorrectionExchange exchange;
    std::atomic<bool> verified{},retired{},failed{},restart{};
    std::atomic<bool> restored{},injected{},no_divergence{},safe_rejection{};std::atomic<unsigned> stage{};
    void configure_combat(){exchange.require_combat_inputs=true;}
    bool attack_fixture{};void configure_attack(){attack_fixture=true;}
    unsigned releases{};bool reject_release{};
    bool release(){++releases;if(reject_release)return false;retired=true;return true;}
};
inline void run(){
    using Bank=sc6::research::CorrectionCycleBank<Runner>;
    Bank baseline(false,true,100),immediate(false,true,0);
    assert(baseline.minimum_hold_ms==100 && immediate.minimum_hold_ms==0);
    Bank attack(true);for(unsigned i=0;i<3;++i)assert(attack.at(i).runner.attack_fixture);
    Bank b;for(unsigned i=0;i<3;++i)assert(!b.at(i).runner.attack_fixture);assert(!b.current() && !b.all_verified());
    assert(b.arm(1,1,100,200,1));auto* first=b.current();
    Bank::Words row{};row[10]=200;row[11]=1;row[13]=2;
    assert(b.receive(false,Bank::packet_index(1,0),row));
    assert(!b.arm(2,1,220,320,1));
    first->runner.verified=true;first->runner.retired=true;
    assert(b.arm(2,1,220,320,1));auto* second=b.current();assert(first!=second);
    assert(!b.receive(false,Bank::packet_index(1,0),row));
    assert(!b.receive(false,Bank::packet_index(3,0),row));
    assert(!second->runner.exchange.failed && second->runner.exchange.remote_count==0);
    assert(first->runner.exchange.remote_count==1);
    row[10]=320;assert(b.receive(false,Bank::packet_index(2,0),row));
    second->runner.verified=true;second->runner.retired=true;second->runner.restart=true;
    assert(!b.arm(3,1,340,440,1));second->runner.restart=false;
    assert(b.arm(3,1,340,440,1));assert(!b.all_verified());
    b.current()->runner.verified=true;b.current()->runner.retired=true;assert(b.all_verified());
    first->runner.reject_release=true;assert(!b.release());
    for(unsigned i=0;i<3;++i)assert(b.at(i).runner.releases==1);
    Bank next;sc6::research::RepeatedCorrectionSession session;
    assert(session.begin(next,1,100,200,1));
    for(unsigned i=1;i<=3;++i){
        auto* c=next.current();c->runner.verified=true;c->runner.retired=true;
        session.update(next,1,100+(i-1)*180+3);assert(next.active()==i);
        Bank::Words result{};result[0]=1;result[1]=200+(i-1)*180;result[2]=2;
        assert(!session.receive(i+1,result));assert(session.receive(i,result));
        session.update(next,1,100+(i-1)*180+3);
    }
    assert(session.complete && !session.failed);
    Bank stopped;sc6::research::RepeatedCorrectionSession timeout;
    assert(timeout.begin(stopped,1,100,200,1));timeout.update(stopped,1,280);assert(timeout.failed);
    for(bool exhaust:{false,true}){
        Bank retry(false,true);sc6::research::RepeatedCorrectionSession rs;
        assert(rs.begin(retry,1,100,200,1));
        for(unsigned i=1;i<=retry.limit;++i){
            auto* c=retry.current();const auto tick=100+(i-1)*180+4;
            if(!exhaust && i>=3){c->runner.verified=true;c->runner.retired=true;}
            else {
                c->runner.exchange.failed=true;c->runner.exchange.input_rejection=2;
                c->confirmation.state=4;
                rs.update(retry,1,tick);assert(retry.active()==i && rs.local[i-1]==0);
                c->runner.failed=true;c->runner.retired=true;
            }
            rs.update(retry,1,tick);assert(retry.active()==i && !rs.failed);
            Bank::Words result{};result[0]=(!exhaust && i>=3)?1:3;result[1]=200+(i-1)*180;result[2]=2;
            assert(rs.receive(i,result));rs.update(retry,1,tick);
            if(!exhaust && i==5)break;
        }
        assert(rs.complete && !rs.failed && rs.exhausted==exhaust);
        assert(retry.all_verified()!=exhaust);
        assert(retry.skipped_count()==(exhaust?12u:2u));
    }
    for(unsigned peer_result:{1u,3u}){
        Bank retry(false,true);sc6::research::RepeatedCorrectionSession rs;
        assert(rs.begin(retry,1,100,200,1));auto& r=retry.current()->runner;
        r.no_divergence=true;
        rs.update(retry,1,104);assert(rs.local[0]==0 && retry.active()==1);
        r.retired=true;rs.update(retry,1,104);assert(rs.local[0]==3);
        Bank::Words result{};result[0]=peer_result;result[1]=200;result[2]=2;
        assert(rs.receive(1,result));rs.update(retry,1,104);
        assert(!rs.failed && !rs.complete && retry.active()==2 && !retry.all_verified());
    }
    {
        Bank retry(false,true);sc6::research::RepeatedCorrectionSession rs;
        assert(rs.begin(retry,1,100,200,1));auto& r=retry.current()->runner;
        r.verified=true;r.retired=true;
        Bank::Words result{};result[0]=3;result[1]=200;result[2]=2;
        assert(rs.receive(1,result));rs.update(retry,1,104);
        assert(!rs.failed && !rs.complete && retry.active()==2);
    }
    for(bool restart:{false,true}){
        Bank bad(false,true);sc6::research::RepeatedCorrectionSession rs;
        assert(rs.begin(bad,1,100,200,1));auto& r=bad.current()->runner;
        r.no_divergence=true;r.retired=true;r.restart=restart;r.failed=!restart;
        rs.update(bad,1,104);assert(rs.failed && bad.active()==1 && !bad.skipped(0));
    }
    Bank dirty(false,true);sc6::research::RepeatedCorrectionSession ds;
    assert(ds.begin(dirty,1,100,200,1));auto* dc=dirty.current();
    dc->runner.exchange.failed=true;dc->runner.exchange.input_rejection=2;
    dc->runner.retired=true;dc->runner.restart=true;dc->confirmation.state=4;
    ds.update(dirty,1,104);assert(ds.failed && dirty.active()==1 && !dirty.skipped(0));
    {
        using Session=sc6::research::RepeatedCorrectionSession;
        Bank left(false,true),right(false,true);Session l,r;
        assert(l.begin(left,1,100,200,1,true) && r.begin(right,1,100,200,2,true));
        for(auto* bank:{&left,&right}){bank->current()->runner.no_divergence=true;bank->current()->runner.retired=true;}
        sc6::research::RetryBoundaryObservation::Sample sample{2,10,1000,4,100};
        l.update(left,2,10,sample);r.update(right,2,10,sample);
        Bank::Words lw{},rw{};assert(l.outgoing(1,lw) && r.outgoing(1,rw));
        assert(l.receive(1,rw) && r.receive(1,lw));
        l.update(left,2,10,sample);assert(l.outgoing(2,lw) && lw[0]==4);
        assert(r.receive(2,lw));r.update(right,2,10,{});assert(!r.outgoing(2,rw)); // no stale-frame activation
        r.update(right,2,10,sample);assert(r.outgoing(2,rw) && rw[0]==5);
        assert(l.receive(2,rw));l.update(left,2,10,sample);
        assert(left.active()==2 && l.outgoing(2,lw) && lw[0]==6);
        assert(right.active()==1 && r.receive(2,lw));r.update(right,2,10,sample);
        assert(right.active()==2 && !l.failed && !r.failed);
        assert(left.current()->entry.target_clock()==610 && left.current()->entry.target_battle()==1600);
        assert(l.begin(left,1,100,200,1,true)); // startup polling remains idempotent after a new round
        assert(!l.receive(3,rw)); // future-cycle/stale proposal rejection
        // Unstarted boundary cancellation waits for owner retirement, then retries.
        auto* c=left.current();c->entry.state=4;c->confirmation.state=4;
        l.update(left,3,0,{});assert(!l.failed && l.local[1]==0);
        c->runner.retired=true;c->runner.safe_rejection=true;
        l.update(left,3,0,{});assert(!l.failed && l.local[1]==3);
        c->runner.restart=true;l.update(left,3,0,{});assert(l.failed);
    }
    {
        sc6::research::RetryBoundaryObservation observation;
        for(unsigned i=1;i<=4;++i){observation.clock(2,i);observation.battle(i+50,100+i);}
        auto sample=observation.read(104);assert(sample.stable==4 && sample.clock==4 && sample.battle==54);
        assert(!observation.read(400).stable);
        observation.clock(3,1);observation.battle(100,401);assert(!observation.read(401).stable);
    }
    std::cout<<"PASS: isolated correction cycles, stale packet rejection and independent cleanup\n";
}
}

