#include "../src/AutomaticSteam.hpp"
#include <cassert>
#include <deque>
#include <iostream>
namespace a=sc6::automatic_steam;
static constexpr uint64_t friendID=76561198000000001ULL,otherID=76561198000000002ULL;
static std::deque<a::Packet> inbound;
static sc6::steam_handshake::Protocol remote;
static bool responsive=true,live=true;static unsigned sends{};
static bool tx(void*,uint64_t peer,const void* data,uint32_t size,int type,int channel){
    assert(peer==friendID && size==sizeof(a::Packet) && type==1 && channel==27862);++sends;
    if(!responsive)return true;
    a::Packet p{};std::memcpy(&p,data,sizeof(p));
    if(p.kind==0){assert(remote.receive(friendID,p.handshake,sizeof(p.handshake)));inbound.push_back({remote.outgoing(),0,0,0});}
    if(p.kind==1)inbound.push_back({remote.outgoing(),2,p.id,0});
    return true;
}
static bool available(void*,uint32_t* size,int channel){assert(channel==27862);if(inbound.empty())return false;*size=sizeof(a::Packet);return true;}
static bool read(void*,void* data,uint32_t capacity,uint32_t* size,uint64_t* sender,int channel){assert(channel==27862 && capacity>=sizeof(a::Packet));if(inbound.empty())return false;std::memcpy(data,&inbound.front(),sizeof(a::Packet));inbound.pop_front();*size=sizeof(a::Packet);*sender=friendID;return true;}
static bool state(void*,uint64_t,a::Session* s){s->active=live?1:0;return true;}
int main(){
    sc6::AutomaticPeer peers;
    peers.observe(friendID,20001,false,true,100);assert(!peers.candidate(100));
    peers.observe(friendID,20001,true,false,100);assert(!peers.candidate(100));
    peers.observe(friendID,27862,true,true,100);assert(!peers.candidate(100));
    peers.observe(friendID,20001,true,true,100);assert(peers.candidate(100)==friendID);
    peers.observe(otherID,20001,false,true,100);peers.observe(otherID,20001,true,true,100);assert(!peers.candidate(100));
    sc6::AutomaticPeer expiry;
    expiry.observe(friendID,20001,false,true,100);expiry.observe(friendID,20001,true,true,100);
    assert(expiry.candidate(2099)==friendID && expiry.candidate(2100)==friendID && !expiry.candidate(2101));
    expiry.observe(friendID,20001,false,true,2102);assert(!expiry.candidate(2102));
    expiry.observe(friendID,20001,true,true,2102);assert(expiry.candidate(2102)==friendID);
    assert(!peers.candidate(10101));
    peers.observe(friendID,20001,false,true,10200);peers.observe(friendID,20001,true,true,10200);assert(peers.candidate(10200)==friendID);
    a::initialized=true;a::next_report=UINT64_MAX;a::send=tx;a::read=read;a::available=available;a::state=state;
    remote.expected_peer=friendID;remote.local_nonce=123456;remote.expected_signature=a::version;
    const auto now=GetTickCount64();auto& observed=sc6::steam_observer::automatic_peers;
    observed.observe(friendID,20001,false,true,now);observed.observe(friendID,20001,true,true,now);
    for(unsigned i=0;i<8;++i){a::next_send=0;a::poll();}
    assert(a::protocol.mutual && remote.mutual && a::compatible(GetTickCount64()) && sends>0);
    auto bad=a::Packet{remote.outgoing(),0,0,0};bad.handshake.magic^=1;inbound.push_back(bad);
    const auto rejected=a::rejected;a::poll();assert(a::rejected==rejected+1);
    const auto nonce=a::protocol.local_nonce;live=false;a::poll();assert(!a::peer && !a::compatible(GetTickCount64()));
    live=true;responsive=false;a::poll();assert(a::peer==friendID && a::protocol.local_nonce!=nonce && !a::protocol.mutual);
    a::started=GetTickCount64()-10001;auto count=sends;a::poll();assert(sends==count && !a::compatible(GetTickCount64()));
    observed.observe(otherID,20001,false,true,GetTickCount64());observed.observe(otherID,20001,true,true,GetTickCount64());
    a::poll();assert(!a::peer && sends==count);
    std::cout<<"PASS: native-channel discovery, bidirectional/expiry/ambiguity guards, automatic mutual handshake, wrong-version rejection, disconnect reset, fresh reconnect nonce, unmodded fallback; no game input or restoration API\n";
}
