#pragma once
#include <array>
#include <cstdint>
namespace sc6 {
// Native game channel metadata only. Ambiguous lobbies never select an opponent.
struct AutomaticPeer {
    struct Entry {uint64_t peer{},sent{},received{};};
    std::array<Entry,16> entries{};
    static constexpr uint64_t lifetime=2000;
    static bool individual(uint64_t id){return id>76561197960265728ULL && id<=76561202255233023ULL;}
    void observe(uint64_t peer,int channel,bool receive,bool ok,uint64_t now){
        if(channel!=20001 || !ok || !individual(peer) || !now)return;
        Entry* slot=nullptr;
        for(auto& e:entries){if(e.peer==peer){slot=&e;break;}
            if(!slot && (!e.peer || (now>=e.sent && now-e.sent>lifetime && now>=e.received && now-e.received>lifetime)))slot=&e;}
        if(!slot)return;
        if(slot->peer!=peer)*slot={peer,0,0};
        (receive?slot->received:slot->sent)=now;
    }
    uint64_t candidate(uint64_t now)const{
        uint64_t found=0;
        for(const auto& e:entries)if(e.peer && e.sent && e.received && now>=e.sent && now>=e.received && now-e.sent<=lifetime && now-e.received<=lifetime){
            if(found && found!=e.peer)return 0;found=e.peer;}
        return found;
    }
};
}
