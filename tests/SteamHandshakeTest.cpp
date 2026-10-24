#include "../src/ContinuousStopPacket.hpp"
#include "../src/MatchCompletionExchange.hpp"
#include "../src/SteamHandshakeProtocol.hpp"
#include "../src/CombatPeerContext.hpp"
#include <cassert>
#include <iostream>
#include <thread>
#include <atomic>
using namespace sc6::steam_handshake;
int main(){
    {
        using End=sc6::research::MatchCompletionExchange;
        End a,b;a.local_complete(100);
        auto offer=a.outgoing(10,20);
        assert(!b.receive(offer,20,99,101)); // Stale/wrong session.
        auto malformed=offer;malformed.received_hash=2;assert(!b.receive(malformed,20,10,101));
        malformed=offer;malformed.record.data[3]=1;assert(!b.receive(malformed,20,10,101));
        malformed=offer;malformed.index=1;assert(!b.receive(malformed,20,10,101));
        assert(b.receive(offer,20,10,101));
        assert(!b.local_clean && !b.complete() && !b.due(102)); // Peer cannot authorize cleanup.
        a.dispatched(100,false);assert(!a.complete() && !a.due(349) && a.due(350));
        assert(!a.expired(30099) && a.expired(30100));
        b.local_complete(400);
        assert(a.receive(b.outgoing(20,10),10,20,401));
        assert(!a.complete()); // Must send the acknowledgement too.
        a.dispatched(401,false);assert(!a.complete());
        a.dispatched(651,true);assert(a.complete());
        assert(b.receive(a.outgoing(10,20),20,10,652));
        b.dispatched(652,true);assert(b.complete());
        assert(b.receive(a.outgoing(10,20),20,10,653) && b.complete()); // Duplicate is idempotent.
        assert(!a.expired(999999));
        End fresh;assert(!fresh.receive(a.outgoing(10,20),21,11,700)); // Rematch rejects old nonces.
        End missing;missing.local_complete(10);assert(!missing.complete() && missing.expired(30010));
    }
    {
        using End=sc6::research::MatchCompletionExchange;
        using Retired=sc6::research::RetiredMatchCompletion;
        static_assert(End::channel!=channel && End::channel!=27863 && End::channel!=27864);
        End a,b;Retired retired;
        a.local_complete(100);b.local_complete(100);
        assert(b.receive(a.outgoing(10,20),20,10,110));
        assert(a.receive(b.outgoing(20,10),10,20,120));
        a.dispatched(120,true);assert(a.complete()); // Last acknowledgement is lost after enqueue.
        assert(!b.complete());assert(retired.retain(123,10,20,a,121));
        a={};End next;next.local_complete(200); // Local returned to lobby and started a new match.
        assert(!next.receive(b.outgoing(20,10),30,40,250));
        unsigned replies=0;
        auto reply=[&](uint64_t peer,const auto& packet){assert(peer==123);++replies;return b.receive(packet,20,10,260);};
        assert(!retired.receive(999,b.outgoing(20,10),250,reply));
        auto bad=b.outgoing(20,10);bad.record.data[0]=1;assert(!retired.receive(123,bad,250,reply));
        assert(retired.receive(123,b.outgoing(20,10),250,reply));
        b.dispatched(260,true);assert(b.complete() && replies==1);
        assert(retired.receive(123,b.outgoing(20,10),251,reply) && replies==1); // Bounded duplicate replies.
        assert(!next.complete() && !next.peer_clean); // Old ACK cannot finish new match.
        assert(!retired.receive(123,b.outgoing(20,10),60121,reply));
        End unsafe;assert(!retired.retain(123,50,60,unsafe,300));
        // Lost response is retried; send failure cannot remove the responder.
        End clean;clean.local_clean=clean.peer_clean=clean.peer_ack=clean.sent_ack=true;
        Retired retry;assert(retry.retain(123,10,20,clean,1));
        assert(retry.receive(123,b.outgoing(20,10),500,[](auto,const auto&){return false;}));
        assert(retry.send_failures==1);
        assert(retry.receive(123,b.outgoing(20,10),600,reply) && retry.replies==1);
        // Capacity never evicts a live old-session responder.
        for(unsigned i=1;i<4;++i)assert(retry.retain(123,10+i,20+i,clean,700));
        assert(!retry.retain(123,99,100,clean,701));
        assert(retry.retain(123,99,100,clean,61000));
        // Delayed completion present at the deadline must be consumed before expiry.
        End deadline;deadline.local_complete(100);deadline.peer_clean=true;deadline.sent_ack=true;
        auto ack=clean.outgoing(20,10);
        assert(deadline.expired(30100));assert(deadline.receive(ack,10,20,30100));assert(!deadline.expired(30100));
    }
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
