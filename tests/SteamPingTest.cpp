#include "../src/SteamPing.hpp"
#include <cassert>
#include <iostream>
int main(){
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
