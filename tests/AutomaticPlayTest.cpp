#include "../src/AutomaticPlayControl.hpp"
#include <cassert>
int main(){
 using sc6::automatic_play::may_release;
 assert(may_release(1,1,true,false,false,true,true,true));
 assert(!may_release(0,0,true,false,false,true,true,true));
 assert(!may_release(2,1,true,false,false,true,true,true)); // stale prior match report
 assert(!may_release(1,1,false,false,false,true,true,true)); // ordinary round handoff
 assert(!may_release(1,1,true,true,false,true,true,true)); // still executing
 assert(!may_release(1,1,true,false,true,true,true,true)); // failed restoration
 assert(!may_release(1,1,true,false,false,false,true,true)); // ownership remains
 assert(!may_release(1,1,true,false,false,true,false,true)); // native tail not committed
 assert(!may_release(1,1,true,false,false,true,true,false)); // route still accepts inputs
 assert(may_release(2,2,true,false,false,true,true,true)); // independent next match
}
