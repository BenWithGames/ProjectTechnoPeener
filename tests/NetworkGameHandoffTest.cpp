#include "../src/NetworkGameHandoff.hpp"
#include <cassert>
#include <iostream>
#include <memory>
#include <thread>
int main(){
    using H=sc6::NetworkGameHandoff;
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
