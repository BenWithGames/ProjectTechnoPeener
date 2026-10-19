#pragma once
#include "SteamInputExchange.hpp"
namespace sc6::research {
struct ContinuousStopPacket {
    static constexpr unsigned kind=12;
    static sc6::steam_inputs::Packet encode(uint64_t local,uint64_t remote){
        sc6::steam_inputs::Packet p;p.kind=kind;p.nonce=local;p.echo=remote;p.own_hash=1;return p;
    }
    static bool valid(const sc6::steam_inputs::Packet& p,uint64_t local,uint64_t remote){
        if(!local || !remote || p.magic!=sc6::steam_inputs::signature || p.kind!=kind ||
           p.nonce!=remote || p.echo!=local || p.index || p.own_hash!=1 || p.received_hash)return false;
        for(auto word:p.record.data)if(word)return false;
        return true;
    }
};
}
