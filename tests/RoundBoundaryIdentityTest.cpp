#include "../src/RoundBoundaryIdentity.hpp"
#include <cassert>
#include <iostream>
int main(){
    sc6::RoundBoundaryIdentity guard;
    guard.begin(10);assert(!guard.key(100));
    assert(guard.on_round_started(1));auto old=*guard.key(100);
    assert(guard.permits(old,100));assert(!guard.permits(old,99));
    assert(guard.on_round_started(2));assert(!guard.permits(old,150));
    auto second=*guard.key(150);assert(guard.permits(second,151));
    assert(guard.on_round_started(2));assert(!guard.permits(second,151));
    assert(guard.on_round_started(1));assert(!guard.permits(old,100));
    auto rematch=*guard.key(100);assert(rematch.boundary!=old.boundary);
    guard.begin(11);assert(!guard.permits(rematch,100));
    assert(guard.on_round_started(1));assert(!guard.permits(old,100));
    assert(!guard.on_round_started(0));assert(!guard.key(100));
    assert(!guard.on_round_started(1));
    guard.begin(0);assert(!guard.on_round_started(1));
    guard.begin(12);
    assert(guard.on_upstream_start(1,101,true));auto first=*guard.key(102);
    assert(guard.on_upstream_start(1,101,true));assert(guard.permits(first,102));
    assert(guard.on_upstream_start(2,2224,true));assert(!guard.permits(first,2224));
    auto later=*guard.key(2225);guard.known=false;
    assert(guard.on_upstream_start(1,101,true));assert(!guard.permits(later,2225));
    assert(!guard.on_upstream_start(2,2224,false));assert(!guard.key(2224));
    guard.begin(13);assert(!guard.on_upstream_start(65535,1,true));
    guard.begin(14);assert(!guard.on_upstream_start(0,1,true));
    std::cout<<"PASS: unknown boundary, same-round rewind, future frame rejection, round advance, repeated event, rematch counter reuse, session change and failure latch\n";
}
