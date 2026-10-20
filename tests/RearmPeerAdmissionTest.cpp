#include "../src/RearmPeerAdmission.hpp"
#include <cassert>
#include <iostream>
using namespace sc6::research;
int main(){
 using C=CombatPeerContext;using A=RearmPeerAdmission;using R=A::Result;
 C context;sc6::steam_handshake::Protocol protocol;protocol.expected_peer=10;protocol.local_nonce=11;protocol.remote_nonce=22;protocol.mutual=protocol.proof=true;
 assert(context.publish(protocol,1,101,true));C::Snapshot s;uint64_t now{};
 auto read=context.inspect_current(s,now,[]{return uint64_t(102);});assert(read==C::Check::ready&&now==102);
 // Old caller time would have incorrectly rejected a concurrently published snapshot.
 assert(C::classify(100,s)==C::Check::future);
 A a;auto check=[&](uint64_t time){return a.check(s,C::classify(time,s),time,10,11,22,1,11);};
 assert(check(351)==R::ready);assert(check(352)==R::waiting&&a.reason==3);
 assert(check(9351)==R::waiting);assert(check(9352)==R::failed&&a.reason==9);
 a={};s.tick=100;assert(check(500)==R::waiting);s.tick=501;assert(check(501)==R::ready&&!a.waiting);
 for(unsigned which=0;which<5;++which){a={};s={10,11,22,100,1,true};uint64_t shared=11;
  if(which==0)s.peer=99;if(which==1)s.local_nonce=99;if(which==2)s.remote_nonce=99;if(which==3)s.local_slot=2;if(which==4)shared=99;
  assert(a.check(s,C::Check::stale,1000,10,11,22,1,shared)==R::failed&&a.reason==which+4);
 }
 a={};s={};assert(a.check(s,C::Check::missing,1000,10,11,22,1,11)==R::waiting);
 assert(a.check(s,C::Check::missing,999,10,11,22,1,11)==R::failed);
 context.clear();assert(context.inspect_current(s,now,[]{return uint64_t(103);})==C::Check::missing);
 std::cout<<"Peer rearm freshness, post-copy clock, bounded recovery, identity rejection and clear passed\n";
}
