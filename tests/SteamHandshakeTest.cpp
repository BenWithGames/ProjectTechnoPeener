#include "../src/ContinuousStopPacket.hpp"
#include "../src/SteamHandshakeProtocol.hpp"
#include "../src/CombatPeerContext.hpp"
#include <cassert>
#include <iostream>
#include <thread>
#include <atomic>
using namespace sc6::steam_handshake;
int main(){
    {
        using Stop=sc6::research::ContinuousStopPacket;
        auto packet=Stop::encode(10,20);
        assert(Stop::valid(packet,20,10));
        assert(!Stop::valid(packet,20,11) && !Stop::valid(packet,21,10));
        packet.index=1;assert(!Stop::valid(packet,20,10));packet.index=0;
        for(auto& word:packet.record.data){word=1;assert(!Stop::valid(packet,20,10));word=0;}
        packet.own_hash=0;assert(!Stop::valid(packet,20,10));
    }

    Protocol a{22,101},b{11,202};
    assert(!a.receive(33,b.outgoing(),32));
    assert(!a.receive(22,b.outgoing(),31));
    auto bad=b.outgoing();bad.magic++;assert(!a.receive(22,bad,32));
    bad=b.outgoing();bad.echo=999;assert(!a.receive(22,bad,32));
    bad=b.outgoing();bad.confirmed=1;assert(!a.receive(22,bad,32));
    assert(a.receive(22,b.outgoing(),32));assert(!a.proof && !a.mutual);
    assert(b.receive(11,a.outgoing(),32));assert(b.proof && !b.mutual);
    assert(a.receive(22,b.outgoing(),32));assert(a.proof && a.mutual);
    assert(b.receive(11,a.outgoing(),32));assert(b.mutual);
    sc6::research::CombatPeerContext context;
    sc6::research::CombatPeerContext::Snapshot snapshot;
    assert(context.publish(a,1,1000,true));
    assert(context.read(1000,snapshot) && snapshot.peer==22 && snapshot.local_nonce==101 && snapshot.remote_nonce==202);
    assert(!context.read(1251,snapshot) && !snapshot.valid);
    assert(!context.read(999,snapshot));
    assert(!context.publish(a,1,1001,false) && !context.read(1001,snapshot));
    assert(!context.publish(a,3,1002,true));
    auto pending=a;pending.mutual=false;assert(!context.publish(pending,1,1002,true));
    assert(context.publish(b,2,1003,true) && context.read(1003,snapshot) && snapshot.local_slot==2);
    context.clear();assert(!context.read(1003,snapshot));
    // Reproduce the former poll gap: a battle callback between clear and
    // refresh would skip its capture despite a still-authenticated peer.
    assert(context.publish(a,1,1004,true));context.clear();
    assert(!context.read(1004,snapshot));
    assert(context.publish(a,1,1004,true));
    std::atomic<bool> go{};
    std::thread writer([&]{while(!go.load())std::this_thread::yield();
        for(unsigned i=0;i<10000;++i)assert(context.publish(a,1,1004,true));});
    go=true;
    for(unsigned i=0;i<10000;++i){assert(context.read(1004,snapshot));
        assert(snapshot.peer==22 && snapshot.local_nonce==101 && snapshot.remote_nonce==202);}
    writer.join();
    assert(!context.publish(a,1,1004,false));assert(!context.read(1004,snapshot));
    assert(a.receive(22,b.outgoing(),32)); // duplicate is harmless
    bad=b.outgoing();bad.nonce=303;assert(!a.receive(22,bad,32));
    Protocol restarted{22,404};assert(!restarted.receive(22,b.outgoing(),32));assert(!restarted.mutual);
    Protocol loss_a{22,501},loss_b{11,502};
    for(unsigned frame=0;frame<30;++frame){
        auto pa=loss_a.outgoing(),pb=loss_b.outgoing();
        if(frame%3)assert(loss_b.receive(11,pa,32));
        if(frame%4)assert(loss_a.receive(22,pb,32));
    }
    assert(loss_a.mutual && loss_b.mutual);
    Protocol early{22,601},late{11,602};
    const auto queued=early.outgoing();
    assert(late.receive(11,queued,32));
    assert(early.receive(22,late.outgoing(),32));
    assert(late.receive(11,early.outgoing(),32));
    assert(early.receive(22,late.outgoing(),32));
    assert(early.mutual && late.mutual);
    std::cout<<"PASS: mutual challenge, wrong peer/version/size, stale challenge, false confirmation, duplicate, changed nonce, restart rejection\n";
}
