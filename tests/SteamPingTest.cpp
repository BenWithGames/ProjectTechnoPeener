#include "../src/SteamPing.hpp"
#include <cassert>
#include <iostream>
int main(){
    sc6::SteamPing p;assert(!p.value(0));assert(p.due(0));
    auto seq=p.begin(1000000);assert(!p.due(3999999));assert(p.due(4000000));
    assert(!p.pong(seq+1,1080000));assert(p.pong(seq,1080000));assert(p.value(1080000)==80u);
    assert(!p.pong(seq,1090000));assert(!p.value(10080001));
    auto next=p.begin(4000000);assert(!p.pong(seq,4080000));assert(p.pong(next,4100000));assert(p.value(4100000)==100u);
    next=p.begin(7000000);assert(!p.pong(next,6999999));assert(!p.pong(next,10000001));
    std::cout<<"PASS: measured RTT, 3-second cadence, stale expiry, wrong/duplicate/old/late response rejection\n";
}

