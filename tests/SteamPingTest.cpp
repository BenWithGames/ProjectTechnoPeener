#include "../src/SteamPing.hpp"
#include "../src/CombatStartAgreement.hpp"
#include "../src/InputDelayBuildPolicy.hpp"
#include <cassert>
#include <iostream>
int main(){
    for(unsigned measured:{0u,1u,2u,99u}){
        assert(sc6::InputDelayBuildPolicy::proposal(true,measured)==(measured<=2?measured:2));
        assert(sc6::InputDelayBuildPolicy::proposal(false,measured)==0);
    }
    assert(sc6::InputDelayBuildPolicy::accepts(true,1));
    assert(sc6::InputDelayBuildPolicy::accepts(true,0) && sc6::InputDelayBuildPolicy::accepts(true,2));
    assert(!sc6::InputDelayBuildPolicy::accepts(true,3));
    assert(sc6::InputDelayBuildPolicy::accepts(false,0) && !sc6::InputDelayBuildPolicy::accepts(false,1));
    {
        sc6::AdaptiveInputDelay d;assert(d.proposal(1)==2);
        d.observe(1000000,5,20000);assert(d.proposal(1000000)==2);
        d.observe(4000000,5,24000);assert(d.proposal(4000000)==0);
        d.observe(7000000,5,29000);assert(d.proposal(7000000)==1);
        d.observe(10000000,5,31000);assert(d.proposal(10000000)==1);
        d.observe(13000000,5,71000);assert(d.proposal(13000000)==2);
        d.observe(16000000,5,20000);d.observe(19000000,5,20000);assert(d.proposal(19000000)==0);
        d.observe(22000000,5,20000);assert(d.proposal(22000000)==0);
        assert(d.proposal(31000001)==2 && d.proposal(1)==2);
        d.observe(25000000,4,10000);assert(d.proposal(25000000)==2);
    }
    for(unsigned previous=0;previous<=2;++previous)for(uint64_t rtt:{24999ull,25000ull,55000ull,60000ull,69999ull,70000ull,70001ull}){
        sc6::AdaptiveInputDelay d;d.level=previous;
        d.observe(1000000,5,rtt);d.observe(4000000,5,rtt);
        assert(d.proposal(4000000)==(rtt<25000?0u:(rtt<=70000?1u:2u)));
    }
    {
        sc6::SteamPing measured;
        auto burst=[&](uint64_t start,std::array<uint64_t,5> times){
            for(unsigned i=0;i<5;++i){const auto sent=start+i*20000;const auto probe=measured.begin(sent);assert(probe);
                assert(measured.pong(probe,sent+times[i],0));}
            measured.update(start+400000);
            return measured.adaptive.proposal(start+400000);
        };
        assert(burst(1000000,{80000,81000,82000,83000,84000})==2);
        assert(burst(4000000,{63000,64000,65000,66000,120000})==2);
        assert(burst(7000000,{62000,63000,64000,65000,110000})==1);
        assert(measured.adaptive.worst_us==120000 && measured.adaptive.selection_us==66000);
        assert(burst(10000000,{20000,21000,22000,23000,90000})==1);
        assert(burst(13000000,{18000,19000,20000,21000,80000})==0);
        assert(burst(16000000,{60000,61000,62000,71000,100000})==2); // repeated spikes
        assert(measured.adaptive.proposal(22000001)==2); // old supporting burst expired
        measured.adaptive.observe(19000000,4,60000,60000);assert(measured.adaptive.proposal(19000000)==2);
        measured.adaptive.observe(22000000,5,60000,60000);assert(measured.adaptive.proposal(22000000)==2);
        measured.adaptive.observe(25000000,5,60000,60000);assert(measured.adaptive.proposal(25000000)==1);
        assert(measured.adaptive.proposal(28000001)==1); // next burst in flight: no expiry flicker
        assert(measured.adaptive.proposal(31000001)==2);
        measured.adaptive.observe(25000000,5,10000,10000);assert(measured.adaptive.proposal(25000000)==2);
    }
    for(unsigned a=0;a<=2;++a)for(unsigned b=0;b<=2;++b){
        using Agreement=sc6::research::CombatStartAgreement;
        Agreement::Offer x{1,2,10,99,3,4,100,1,a,3,7200},y=x;
        y.sender=2;y.recipient=1;y.ticket=20;y.slot=2;y.delay=b;
        Agreement left,right;assert(left.begin(x)&&right.begin(y));
        assert(left.receive(y)&&right.receive(x));assert(!left.activation_ready());
        assert(left.acknowledge(2,10,20)&&right.acknowledge(1,20,10));
        assert(left.local_held(10)&&right.local_held(20)&&left.peer_held(20)&&right.peer_held(10));
        assert(left.activation_ready()&&right.activation_ready());
        assert(left.agreed_delay()==(a>b?a:b)&&right.agreed_delay()==left.agreed_delay());
        assert(left.consume(x)&&right.consume(y));
        Agreement mutation;assert(mutation.begin(x)&&mutation.receive(y));y.delay=(b+1)%3;
        assert(!mutation.receive(y)&&!mutation.ready());
    }
    sc6::SteamPing p;assert(!p.value(0));assert(p.due(1000000));
    uint32_t ids[5]{};for(unsigned i=0;i<5;++i){ids[i]=p.begin(1000000+i*20000);assert(ids[i]);}
    assert(!p.due(1100000));assert(!p.pong(ids[0],999999,0));
    assert(!p.pong(ids[0],1100000,100001));
    assert(!p.pong(999,1100000,0));
    assert(p.pong(ids[0],1110000,10000)); // 100ms adjusted
    assert(p.pong(ids[1],1105000,5000));  // 80ms adjusted
    assert(p.pong(ids[2],1140000,0));
    assert(p.pong(ids[3],1160000,0));
    assert(p.pong(ids[4],1180000,0));
    assert(!p.pong(ids[1],1190000,0));p.update(1180000);
    assert(p.value(1180000)==80u && p.latest_samples==5);
    assert(p.latest_raw_us==85000 && p.latest_handling_us==5000);
    assert(!p.due(3999999));assert(p.due(4000000));
    auto id=p.begin(4000000);p.send_failed(id);assert(!p.pong(id,4080000,0));
    p.update(4500000);assert(p.value(4500000)==80u);
    assert(!p.value(10180001));
    id=p.begin(11000000);assert(p.pong(id,11120000,20000));p.update(11500000);
    assert(p.value(11500000)==100u && p.latest_samples==1);
    id=p.begin(14000000);assert(!p.pong(id,14500000,0));p.update(14500000);
    assert(p.value(14500000)==100u); // lost burst does not invent a reading
    std::cout<<"PASS: minimum burst RTT, measured handling subtraction, 3-second cadence, partial/lost bursts, stale expiry, invalid/duplicate/late replies and failed sends\n";
}
