#include "../src/LiveInputComposition.hpp"
#include <cassert>
#include <iostream>
#include <memory>
#include <fstream>
#include <sstream>
#include <vector>
struct Fixture {
    std::array<sc6::observation::Record,sc6::observation::capacity> local{},peer{};
    std::array<bool,sc6::observation::capacity> seen{};
    Fixture(){for(unsigned i=0;i<4;++i){
        local[i].data[0]=peer[i].data[0]=100+i;
        local[i].data[1]=peer[i].data[1]=i;
        local[i].data[3]=peer[i].data[3]=i+1;
        local[i].data[5]=17;peer[i].data[5]=23;
    }}
};
int main(int argc,char** argv){
    using C=sc6::LiveInputComposition;
    // Recorded 79392 boundary: outer 1228 -> 1607, round 1/97 -> 2/1487.
    // Reordered peer delivery must wait for its preceding record and then
    // validate the boundary once; eligibility must never span that boundary.
    for(unsigned slot=1;slot<=2;++slot){
        auto data=std::make_unique<Fixture>();auto composition=std::make_unique<C>();
        std::array<sc6::observation::Round,sc6::observation::capacity> localRounds{},peerRounds{};
        const uint32_t frames[]={1228,1607,1608,2000};
        const sc6::observation::Round rounds[]={{1,97},{2,1487},{2,1487},{3,1900}};
        for(unsigned i=0;i<4;++i){data->local[i].data[0]=data->peer[i].data[0]=frames[i];localRounds[i]=peerRounds[i]=rounds[i];}
        assert(composition->configure(slot,C::Purpose::authoritative_peer));
        assert(composition->announce(3-slot) && composition->bind_replay_session(10,20));
        data->seen[1]=data->seen[3]=true;
        assert(composition->update(data->local,4,data->peer,data->seen,&localRounds,&peerRounds));
        assert(composition->matched==0 && composition->local_round_gaps==2 && composition->remote_round_gaps==0);
        data->seen[0]=data->seen[2]=true;
        assert(composition->update(data->local,4,data->peer,data->seen,&localRounds,&peerRounds));
        assert(composition->matched==4 && composition->local_round_gaps==2 && composition->remote_round_gaps==2);
        assert(composition->replay_eligible==1 && composition->replay_round_rejected==5);
        assert(composition->update(data->local,4,data->peer,data->seen,&localRounds,&peerRounds));
        assert(composition->remote_round_gaps==2 && composition->local_round_gaps==2);
    }
    // Missing/stale/skipped boundaries, backwards counters and same-round gaps
    // stay fatal on either stream; comparison mode retains its strict policy.
    for(unsigned side=0;side<2;++side)for(unsigned fault=0;fault<8;++fault){
        auto data=std::make_unique<Fixture>();auto composition=std::make_unique<C>();
        std::array<sc6::observation::Round,sc6::observation::capacity> lr{},rr{};
        data->local[0].data[0]=data->peer[0].data[0]=1228;
        data->local[1].data[0]=data->peer[1].data[0]=1607;
        lr[0]=rr[0]={1,97};lr[1]=rr[1]={2,1487};
        auto& rounds=side?rr:lr;auto& records=side?data->peer:data->local;
        switch(fault){
        case 0:rounds[1]=rounds[0];break;
        case 1:rounds[1]={};break;
        case 2:rounds[1].number=3;break;
        case 3:rounds[1].event_counter=97;break;
        case 4:rounds[1].event_counter=1200;break;
        case 5:rounds[1].event_counter=1608;break;
        case 6:records[1].data[0]=0;break;
        case 7:break; // same valid boundary in the old comparison mode
        }
        data->seen[0]=data->seen[1]=true;
        assert(composition->configure(1,fault==7?C::Purpose::comparison:C::Purpose::authoritative_peer));
        assert(composition->announce(2) && composition->bind_replay_session(10,20));
        assert(!composition->update(data->local,2,data->peer,data->seen,&lr,&rr));
    }
    // Independently valid but conflicting peer boundaries still fail staging.
    {
        auto data=std::make_unique<Fixture>();auto composition=std::make_unique<C>();
        std::array<sc6::observation::Round,sc6::observation::capacity> lr{},rr{};
        data->local[0].data[0]=data->peer[0].data[0]=1228;
        data->local[1].data[0]=data->peer[1].data[0]=1607;
        lr[0]=rr[0]={1,97};lr[1]={2,1487};rr[1]={2,1488};
        data->seen[0]=data->seen[1]=true;
        assert(composition->configure(1,C::Purpose::authoritative_peer));
        assert(composition->announce(2) && composition->bind_replay_session(10,20));
        assert(!composition->update(data->local,2,data->peer,data->seen,&lr,&rr));
        assert(std::string_view(composition->failure)=="composition_round_boundary_mismatch");
    }
    auto f=std::make_unique<Fixture>();auto c=std::make_unique<C>();
    assert(c->configure(1));f->seen[1]=true;
    assert(c->update(f->local,4,f->peer,f->seen) && c->matched==0);
    assert(c->announce(2));assert(c->announce(2));
    assert(c->update(f->local,4,f->peer,f->seen) && c->matched==0);
    f->seen[0]=true;
    assert(c->update(f->local,4,f->peer,f->seen) && c->matched==2);
    assert(c->envelopes[0][4]==17 && c->auxiliary_differences==2);
    assert(c->update(f->local,4,f->peer,f->seen) && c->matched==2);
    assert(!c->announce(1));assert(!c->update(f->local,4,f->peer,f->seen));
    c=std::make_unique<C>();assert(c->configure(2));assert(c->announce(1));
    f->peer[0].data[1]^=1;
    assert(!c->update(f->local,4,f->peer,f->seen));
    f=std::make_unique<Fixture>();f->seen[0]=f->seen[1]=true;
    f->peer[1].data[0]=100;
    c=std::make_unique<C>();assert(c->configure(1));assert(c->announce(2));
    assert(!c->update(f->local,4,f->peer,f->seen));
    f=std::make_unique<Fixture>();f->seen[0]=f->seen[1]=true;f->peer[1].data[0]=0;
    c=std::make_unique<C>();assert(c->configure(1));assert(c->announce(2));
    assert(!c->update(f->local,4,f->peer,f->seen));
    f=std::make_unique<Fixture>();f->local[1].data[0]=0;
    c=std::make_unique<C>();assert(c->configure(1));
    assert(!c->update(f->local,4,f->peer,f->seen));
    c=std::make_unique<C>();assert(!c->configure(0));
    std::array<sc6::observation::Round,sc6::observation::capacity> lr{},rr{};
    f=std::make_unique<Fixture>();f->seen[0]=f->seen[1]=true;
    c=std::make_unique<C>();assert(c->configure(1));assert(c->announce(2));
    assert(c->update(f->local,2,f->peer,f->seen,&lr,&rr));
    assert(c->round_missing==2 && c->matched==0);
    assert(c->update(f->local,2,f->peer,f->seen,&lr,&rr) && c->round_missing==2);
    lr[0]=rr[0]={1,80};lr[1]=rr[1]={2,101};
    c=std::make_unique<C>();assert(c->configure(1));assert(c->announce(2));
    assert(c->update(f->local,2,f->peer,f->seen,&lr,&rr));
    assert(c->round_matches==2 && c->matched==2);
    for(unsigned mismatch=0;mismatch<2;++mismatch){
        rr[1]=lr[1];if(mismatch)++rr[1].event_counter;else ++rr[1].number;
        c=std::make_unique<C>();assert(c->configure(1));assert(c->announce(2));
        assert(!c->update(f->local,2,f->peer,f->seen,&lr,&rr));
        assert(c->round_mismatches==1 && c->matched==1);
    }
    using G=sc6::NetworkReplayEligibility;using R=G::Result;
    G::Key saved{10,20,1,1,80,100};std::array<G::Key,3> path{};
    for(unsigned i=0;i<3;++i){path[i]=saved;path[i].frame=101+i;}
    assert(G::check(saved,path)==R::eligible);
    sc6::RoundBoundaryIdentity identity;identity.begin(10);assert(identity.on_round_started(1));
    const auto checkpoint=*identity.key(100);
    assert(G::check_checkpoint(identity,checkpoint,saved,path,103)==R::eligible);
    assert(G::check_checkpoint(identity,checkpoint,saved,path,104)==R::sequence);
    assert(identity.on_round_started(1)); // same numbered round/event must invalidate old local checkpoints
    assert(G::check_checkpoint(identity,checkpoint,saved,path,103)==R::checkpoint);
    identity.begin(11);assert(identity.on_round_started(1));
    assert(G::check_checkpoint(identity,*identity.key(100),saved,path,103)==R::checkpoint);
    auto changed=path;changed[1].round=2;assert(G::check(saved,changed)==R::round);
    changed=path;changed[1].boundary=100;assert(G::check(saved,changed)==R::round);
    changed=path;changed[2].peer_nonce=21;assert(G::check(saved,changed)==R::session);
    changed=path;changed[0].local_nonce=11;assert(G::check(saved,changed)==R::session);
    changed=path;changed[0].local_slot=2;assert(G::check(saved,changed)==R::slot);
    changed=path;changed[0].round=0;assert(G::check(saved,changed)==R::missing);
    changed=path;changed[1].frame=101;assert(G::check(saved,changed)==R::sequence);
    assert(G::check(saved,{})==R::depth);
    saved.frame=UINT32_MAX;changed=path;changed[0].frame=0;
    assert(G::check(saved,changed)==R::sequence);
    f=std::make_unique<Fixture>();f->seen[0]=f->seen[1]=true;
    lr[0]=rr[0]={1,80};lr[1]=rr[1]={2,101};
    c=std::make_unique<C>();assert(c->configure(1));assert(c->announce(2));assert(c->bind_replay_session(10,20));
    assert(c->update(f->local,2,f->peer,f->seen,&lr,&rr));
    assert(c->replay_round_rejected==1 && c->replay_eligible==0);
    assert(c->update(f->local,2,f->peer,f->seen,&lr,&rr) && c->replay_round_rejected==1);
    assert(!c->bind_replay_session(10,21));
    // Continuous prediction uses the peer's own pair as truth, not equality
    // against our predicted remote pair. Never copy their view of our player
    // or their auxiliary block. Exercise both player assignments.
    for(unsigned slot=1;slot<=2;++slot){
        f=std::make_unique<Fixture>();f->seen[0]=true;
        for(unsigned w=1;w<=10;++w){f->local[0].data[w]=10+w;f->peer[0].data[w]=100+w;}
        lr[0]=rr[0]={1,80};
        c=std::make_unique<C>();assert(c->configure(slot,C::Purpose::authoritative_peer));
        assert(c->announce(3-slot) && c->bind_replay_session(10,20));
        assert(c->update(f->local,1,f->peer,f->seen,&lr,&rr) && c->matched==1);
        const unsigned off=(2-slot)*2;
        for(unsigned w=0;w<10;++w)
            assert(c->envelopes[0][w]==((w==off || w==off+1)?f->peer[0].data[w+1]:f->local[0].data[w+1]));
        assert(c->update(f->local,1,f->peer,f->seen,&lr,&rr) && c->matched==1);
        c=std::make_unique<C>();assert(c->configure(slot,C::Purpose::authoritative_peer));
        assert(c->announce(3-slot));
        assert(!c->update(f->local,1,f->peer,f->seen,&lr,&rr)); // no authenticated session binding
        c=std::make_unique<C>();assert(c->configure(slot,C::Purpose::authoritative_peer));
        assert(c->announce(3-slot) && c->bind_replay_session(10,20));
        rr[0].event_counter++;
        assert(!c->update(f->local,1,f->peer,f->seen,&lr,&rr)); // wrong round still rejects
    }
    if(argc==2){
        std::ifstream input(argv[1]);assert(input);std::string line;std::getline(input,line);
        std::map<uint32_t,G::Key> left,right;
        while(std::getline(input,line)){
            std::stringstream row(line);std::string field;std::vector<uint32_t> values;
            while(std::getline(row,field,','))values.push_back(static_cast<uint32_t>(std::stoul(field)));
            assert(values.size()==7);
            left.emplace(values[1],G::Key{10,20,1,values[2],values[3],values[1]});
            right.emplace(values[4],G::Key{10,20,1,values[5],values[6],values[4]});
        }
        unsigned overlap=0,eligible=0,cross=0;
        for(const auto& [frame,k]:left){
            auto remote=right.find(frame);if(remote==right.end())continue;
            assert(remote->second.round==k.round && remote->second.boundary==k.boundary);++overlap;
            for(unsigned depth=1;depth<=3;++depth){
                std::array<G::Key,3> window{};bool found=true;
                for(unsigned j=0;j<depth;++j){auto next=left.find(frame+j+1),peer=right.find(frame+j+1);
                    if(next==left.end() || peer==right.end()){found=false;break;}
                    assert(next->second.round==peer->second.round && next->second.boundary==peer->second.boundary);
                    window[j]=next->second;
                }
                if(!found)continue;
                const auto result=G::check(k,std::span(window.data(),depth));
                assert(result==R::eligible || result==R::round);
                if(result==R::eligible)++eligible;else ++cross;
            }
        }
        assert(overlap==1799 && eligible==5385 && cross==6);
        std::cout<<"PASS captured PID84776: "<<overlap<<" aligned frames, "<<eligible<<" same-round windows eligible, "<<cross<<" cross-round windows rejected; no game writes\n";
    }
    std::cout<<"PASS: slot agreement, reordered arrivals, duplicate idempotence, local auxiliary preservation, slot conflicts, mismatched buttons, duplicate/reset counters, failure latch\n";
}
