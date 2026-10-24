#include "../src/MatchLoadingGate.hpp"
#include <cassert>
#include <iostream>
using sc6::research::MatchLoadingGate;
int main(){
    using G=MatchLoadingGate;G::Key k{1,2,3};G::Observation o{k,true,true,true,false,false,false};
    G ready;assert(ready.begin(k,100,true,false));assert(ready.hold(o,101));
    o.committed=true;assert(!ready.hold(o,102) && ready.state()==G::State::released);
    G missing;assert(!missing.begin(k,100,false,false));assert(!missing.hold(o,101));
    assert(missing.reason()==G::Reason::no_compatible_peer);
    G timeout;assert(timeout.begin(k,100,true,false));o.committed=false;
    assert(timeout.hold(o,5099));o.committed=true;
    assert(!timeout.hold(o,5100) && timeout.reason()==G::Reason::timeout);
    assert(!timeout.hold(o,5101) && timeout.state()==G::State::native_fallback);
    assert(!timeout.begin(k,5200,true,false));
    G cancelled;assert(cancelled.begin(k,0,true,false));o.peer_cancelled=true;
    assert(!cancelled.hold(o,1) && cancelled.reason()==G::Reason::peer_cancelled);o.peer_cancelled=false;
    G changed;assert(changed.begin(k,0,true,false));o.key.ticket++;
    assert(!changed.hold(o,1) && changed.reason()==G::Reason::identity_changed);o.key=k;
    G reversed;assert(reversed.begin(k,100,true,false));assert(!reversed.hold(o,99));
    G unsafe;assert(unsafe.begin(k,0,true,false));o.safe=false;assert(!unsafe.hold(o,1));
    G modified;assert(!modified.begin(k,0,true,true));
    G unexpected;assert(unexpected.begin(k,0,true,false));o.modified=true;
    assert(unexpected.hold(o,1) && unexpected.state()==G::State::invalid_modified);
    assert(unexpected.hold(o,6000));
    std::cout<<"Loading gate policy: bounded wait, bilateral readiness, expiry, native bypass, identity and terminal fallback passed\n";
}
