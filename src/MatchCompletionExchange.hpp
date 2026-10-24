#pragma once
#include "SteamInputExchange.hpp"
namespace sc6::research {
// Control-worker only. A peer completion never grants native cleanup permission.
struct MatchCompletionExchange {
    static constexpr unsigned kind=13;
    static constexpr int channel=27865;
    bool local_clean{},peer_clean{},peer_ack{},sent_ack{};
    uint64_t began{},last_send{},sent{},received{};
    void local_complete(uint64_t now){local_clean=true;if(!began)began=now;}
    steam_inputs::Packet outgoing(uint64_t local,uint64_t remote)const{
        steam_inputs::Packet p;p.kind=kind;p.nonce=local;p.echo=remote;
        p.own_hash=1;p.received_hash=peer_clean?1u:0u;return p;
    }
    static bool valid(const steam_inputs::Packet& p,uint64_t local,uint64_t remote){
        if(!local || !remote || p.magic!=steam_inputs::signature || p.kind!=kind ||
           p.nonce!=remote || p.echo!=local || p.index || p.own_hash!=1 || p.received_hash>1)return false;
        for(auto word:p.record.data)if(word)return false;
        return true;
    }
    bool receive(const steam_inputs::Packet& p,uint64_t local,uint64_t remote,uint64_t now){
        if(!valid(p,local,remote))return false;
        peer_clean=true;peer_ack=peer_ack || (local_clean && p.received_hash==1);
        ++received;if(!began)began=now;return true;
    }
    bool due(uint64_t now)const{return local_clean && (!last_send || now-last_send>=250);}
    void dispatched(uint64_t now,bool success){last_send=now;if(success){++sent;if(peer_clean)sent_ack=true;}}
    bool complete()const{return local_clean && peer_clean && peer_ack && sent_ack;}
    bool expired(uint64_t now)const{return began && now>=began && now-began>=30000 && !complete();}
};
// Control-thread-only, bounded old-session responder. It owns no native resources
// and can never mark a new match complete. Keep servicing it across lobby/reset.
class RetiredMatchCompletion {
    struct Entry {uint64_t peer{},local{},remote{},expires{},last_reply{};};
    std::array<Entry,4> entries_{};
public:
    uint64_t accepted{},replies{},send_failures{},capacity_failures{};
    bool retain(uint64_t peer,uint64_t local,uint64_t remote,const MatchCompletionExchange& end,uint64_t now){
        if(!peer || !local || !remote || !end.complete())return false;
        for(auto& e:entries_)if(e.peer==peer && e.local==local && e.remote==remote && now<e.expires)return true;
        for(auto& e:entries_)if(!e.local || now>=e.expires){e={peer,local,remote,now+60000,0};return true;}
        ++capacity_failures;return false;
    }
    template<class Send> bool receive(uint64_t sender,const steam_inputs::Packet& p,uint64_t now,Send send){
        for(auto& e:entries_)if(e.local && now<e.expires && sender==e.peer && MatchCompletionExchange::valid(p,e.local,e.remote)){
            ++accepted;
            if(!e.last_reply || now-e.last_reply>=100){
                MatchCompletionExchange end;end.local_clean=end.peer_clean=true;
                e.last_reply=now;
                if(send(e.peer,end.outgoing(e.local,e.remote)))++replies;else ++send_failures;
            }
            return true;
        }
        return false;
    }
};
}
