#include "../src/SteamInputExchange.hpp"
#include "../src/SteamHandshakeProtocol.hpp"
#include <cassert>
#include <iostream>
using namespace sc6::steam_inputs;
int main(){
    Exchange a,b;
    a.published=b.published=limit;
    for(unsigned i=0;i<limit;++i)for(unsigned w=0;w<13;++w){a.local[i].data[w]=i*100+w;b.local[i].data[w]=50000+i*100+w;}
    for(unsigned retry=0;retry<5;++retry)for(unsigned i=0;i<limit;++i){
        Packet p;p.kind=1;p.index=(limit-1)-i;p.record=a.local[p.index];
        if((i+retry)%3)assert(b.accept(p));
        p.record=b.local[p.index];if((i+retry)%4)assert(a.accept(p));
    }
    assert(a.received==limit && b.received==limit);
    assert(a.remote[0].data==b.local[0].data && b.remote[(limit-1)].data==a.local[(limit-1)].data);
    Packet summary;summary.kind=2;summary.own_hash=sc6::observation::checksum(b.local);summary.received_hash=sc6::observation::checksum(b.remote);
    assert(a.accept(summary) && a.peer_match);
    std::swap(summary.own_hash,summary.received_hash);assert(b.accept(summary) && b.peer_match);
    Packet bad;bad.kind=1;bad.index=limit;assert(!a.accept(bad));bad.index=0;bad.record=a.remote[0];bad.record.data[0]++;
    bool caught=false;try{a.accept(bad);}catch(const std::runtime_error&){caught=true;}assert(caught);
    bad.record=a.remote[0];bad.record.data[13]=1;assert(!a.accept(bad));
    summary.own_hash++;caught=false;try{b.accept(summary);}catch(const std::runtime_error&){caught=true;}assert(caught);
    sc6::steam_handshake::Protocol old_mode{2,10},new_mode{1,20};new_mode.expected_signature=0x000353433648534bULL;
    assert(!new_mode.receive(1,old_mode.outgoing(),32));
    std::cout<<"PASS: limit distinct records each direction, loss/reorder/duplicates, crossed checksums, conflict/bounds/reserved rejection, mode isolation\n";
}
