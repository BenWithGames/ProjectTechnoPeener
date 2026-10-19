#pragma once
#include "RawPredictionRoute.hpp"
namespace sc6::research {
// Uses the already-authenticated SteamInputExchange envelope. Authentication,
// peer identity and nonce checks MUST precede decode at the transport boundary.
struct RawPredictionPacket {
    static constexpr unsigned kind=11,version=1;
    template<class Packet> static bool encode(const RawInputConfirmation& input,Packet& packet){
        if(!input.sender || !input.recipient || input.sender==input.recipient || !input.round || !input.event ||
           input.round>UINT32_MAX || input.event>UINT32_MAX || input.frame>UINT32_MAX || input.slot<1 || input.slot>2)return false;
        packet={};packet.nonce=input.sender;packet.echo=input.recipient;packet.kind=kind;packet.index=version;
        packet.record.data[0]=static_cast<uint32_t>(input.round);
        packet.record.data[1]=static_cast<uint32_t>(input.event);
        packet.record.data[2]=static_cast<uint32_t>(input.frame);
        packet.record.data[3]=input.slot;packet.record.data[4]=input.raw;return true;
    }
    template<class Packet> static bool decode(const Packet& packet,RawInputConfirmation& out){
        out={};
        if(packet.kind!=kind || packet.index!=version || packet.own_hash || packet.received_hash ||
           !packet.nonce || !packet.echo || packet.nonce==packet.echo)return false;
        for(unsigned i=5;i<packet.record.data.size();++i)if(packet.record.data[i])return false;
        const auto& d=packet.record.data;
        if(!d[0] || !d[1] || d[3]<1 || d[3]>2)return false;
        out={packet.nonce,packet.echo,d[0],d[1],d[2],d[3],d[4]};return true;
    }
};
// Published only after bilateral activation; routes have process lifetime.
inline std::atomic<RawPredictionRoute<>*> raw_prediction_transport{};
}
