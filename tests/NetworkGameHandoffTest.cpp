#include "../src/NetworkGameHandoff.hpp"
#include <cassert>
#include <iostream>
#include <memory>
#include <thread>
int main(){
    using H=sc6::NetworkGameHandoff;
    for(unsigned side:{1u,2u}){
        H correction;assert(correction.bind(10,20,side));
        H::Boundary original{1,100,1,80,{}};original.input[4]=23;
        correction.boundary(10,7,original);
        H::Request truth{10,20,side,100,1,80,original.input};truth.input[(2-side)*2]=1024;
        assert(correction.publish(truth));auto next=original;++next.frame;correction.boundary(10,7,next);
        H::Confirmation c;assert(correction.confirmed(10,20,7,original,c));
        assert(c.input==truth.input && c.boundary.input==original.input);
        assert(correction.validated==0 && correction.rejection_reasons[5]==1);
        assert(!correction.confirmed(10,21,7,original,c));assert(!correction.confirmed(10,20,8,original,c));
        assert(correction.publish(truth));++next.frame;correction.boundary(10,7,next);
        assert(!correction.confirmation_conflict);
        truth.input[(2-side)*2]=2048;assert(correction.publish(truth));++next.frame;correction.boundary(10,7,next);
        assert(correction.confirmation_conflict && !correction.confirmed(10,20,7,original,c));
        H invalid;assert(invalid.bind(10,20,side));invalid.boundary(10,7,original);
        truth.input=original.input;truth.input[4]=24;assert(invalid.publish(truth));next=original;++next.frame;
        invalid.boundary(10,7,next);assert(!invalid.confirmed(10,20,7,original,c));
    }
    auto h=std::make_unique<H>();assert(h->bind(10,20,1));assert(h->bind(10,20,1));assert(!h->bind(10,21,1));
    H::Boundary b{1,100,1,80,{}};b.input[4]=23;h->boundary(10,7,b);
    H::Request r{10,20,1,100,1,80,b.input};assert(h->publish(r));++b.frame;h->boundary(10,7,b);
    assert(h->validated==1 && h->rejected==0);
    assert(h->publish(r));++b.frame;++b.generation;h->boundary(10,7,b);assert(h->rejected==1);
    r.frame=b.frame;r.input=b.input;assert(h->publish(r));++b.frame;++b.round;h->boundary(10,7,b);assert(h->rejected==2);
    for(unsigned i=0;i<8;++i)assert(h->publish(r));assert(!h->publish(r));
    ++b.frame;h->boundary(10,7,b);assert(h->rejected==10 && h->full==1);
    r.round=b.round;r.frame=b.frame;r.input[0]=1;assert(h->publish(r));++b.frame;h->boundary(10,7,b);assert(h->rejected==11);
    r.frame=b.frame;r.input=b.input;assert(h->publish(r));++b.frame;h->boundary(10,8,b);assert(h->failed && h->rejected==12);
    assert(h->rejection_reasons[0]==1 && h->rejection_reasons[2]==9 && h->rejection_reasons[4]==1 && h->rejection_reasons[5]==1);
    r.peer_nonce=21;assert(!h->publish(r));
    h=std::make_unique<H>();assert(h->bind(10,20,1));b={1,100,1,80,{}};
    h->boundary(10,7,b);r={10,20,1,100,1,80,{}};
    for(unsigned i=0;i<3;++i){++b.frame;h->boundary(10,7,b);}
    assert(h->publish(r));++b.frame;h->boundary(10,7,b);assert(h->rejected==1 && h->rejection_reasons[3]==1); // older than three frames
    h=std::make_unique<H>();assert(h->bind(10,20,1));b={1,100,0,0,{}};
    h->boundary(10,7,b);r={10,20,1,100,0,0,{}};
    assert(h->publish(r));++b.frame;h->boundary(10,7,b);assert(h->rejected==1);
    h=std::make_unique<H>();assert(h->bind(10,20,1));b={1,100,1,80,{}};
    h->boundary(10,7,b);r={10,20,1,100,1,80,{}};
    assert(h->publish(r));b.frame+=2;h->boundary(10,7,b);assert(h->failed && h->rejected==1);
    // Live boundary choices use only confirmations already drained, not future truth.
    for(unsigned localSlot:{1u,2u}){
        auto live=std::make_unique<H>();assert(live->bind(10,20,localSlot));live->late_enabled=true;
        const unsigned word=(2-localSlot)*2;
        H::Boundary first{1,100,1,80,{}};first.input[word]=0x400;
        live->boundary(10,7,first);assert(live->late.unseeded==1);
        assert(live->publish({10,20,localSlot,100,1,80,first.input}));
        H::Boundary second{1,101,1,80,{}};live->boundary(10,7,second);
        assert(live->late.choices[101%64].held==0x400 && live->late.choices[101%64].edge==0);
        assert(live->publish({10,20,localSlot,101,1,80,second.input}));
        auto third=second;third.frame=102;live->boundary(10,7,third);
        assert(live->late.errors==1 && live->late.requests==1 && live->late.last_depth==1);
        assert(live->late.remote_slot==3-localSlot && live->late.choices[102%64].held==0);
        assert(live->publish({10,20,localSlot,101,1,80,second.input}));
        ++third.frame;live->boundary(10,7,third);assert(live->late.errors==1);
        ++third.frame;++third.round;++third.generation;live->boundary(10,7,third);
        assert(live->late.resets==1 && !live->late.choices[104%64].seeded);
    }
    {
        sc6::LateInputScheduler l;
        assert(l.enter(1,1,80,0,1));l.decide();
        assert(l.enter(1,1,80,1,1));l.receive(0,0x400,1);l.decide();
        for(unsigned frame=2;frame<6;++frame){assert(l.enter(1,1,80,frame,1));l.decide();}
        l.receive(1,0,0);l.decide();assert(l.over_limit==1 && l.max_depth==4 && l.requests==0);
        // Newer confirmations win even if older packets are later delivered.
        l.receive(4,0x800,0);l.receive(3,0,0);assert(l.seed_frame==4 && l.seed_held==0x800);
    }
    // Completed-match retirement excludes readers and rejects stale sessions.
    {
        H clean;assert(clean.retire_completed(100,100,100));
        assert(clean.retire_completed(100,100,100) && clean.retirements==1);
        assert(!clean.bind(100,200,1));
        for(uint64_t session=101;session<105;++session){
            const unsigned side=unsigned(session%2)+1;
            assert(clean.bind(session,session+100,side));
            H::Boundary first{1,0,1,0,{}};
            clean.boundary(session,7,first);
            H::Request request{session,session+100,side,0,1,0,{}};
            assert(clean.publish(request));
            assert(!clean.retire_completed(session,0,session));
            assert(!clean.retire_completed(session,session,0));
            assert(!clean.retire_completed(session+1,session+1,session+1));
            clean.late_enabled=true;
            assert(!clean.retire_completed(session,session,session));
            clean.late_enabled=false;
            std::atomic<bool> entered{false},release{false};
            std::thread reader([&]{H::ReadGuard guard(clean);assert(guard.entered);
                entered=true;while(!release.load())std::this_thread::yield();});
            while(!entered.load())std::this_thread::yield();
            assert(!clean.retire_completed(session,session,session));
            assert(clean.retirement_check==4 && !clean.publish(request));
            assert(!clean.bind(session+1,session+101,side));
            H::Confirmation confirmation;
            assert(!clean.confirmed(session,session+100,7,first,confirmation));
            release=true;reader.join();
            assert(clean.retire_completed(session,session,session));
            assert(!clean.started && !clean.failed && clean.owner==0 && clean.local_nonce==0);
            assert(clean.write==0 && clean.read==0 && clean.submitted==0);
            for(const auto& entry:clean.confirmations)assert(!entry.valid);
            assert(!clean.publish(request));
            clean.boundary(session,7,first);assert(!clean.started);
        }
        assert(clean.retirements==5 && clean.retirement_waits==4 && clean.discarded_requests==4);
    }
    // Exercise release/acquire publication under real concurrent producer/consumer use.
    h=std::make_unique<H>();assert(h->bind(10,20,1));
    std::thread producer([&]{for(unsigned i=0;i<10000;++i){H::Request q{10,20,1,i,1,0,{}};
        q.input[0]=i;while(!h->publish(q))std::this_thread::yield();}});
    for(unsigned i=0;i<10000;++i){
        unsigned pos=h->read.load(std::memory_order_relaxed);
        while(pos==h->write.load(std::memory_order_acquire))std::this_thread::yield();
        const auto q=h->queue[pos%8];assert(q.frame==i && q.input[0]==i);
        h->read.store(pos+1,std::memory_order_release);
    }
    producer.join();assert(h->submitted==10000);
    std::cout<<"PASS: game-thread handoff, session binding, generation/round/input rejection, bounded queue and concurrent publication\n";
}
