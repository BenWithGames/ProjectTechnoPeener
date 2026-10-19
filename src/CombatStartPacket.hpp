#pragma once
#include "CombatStartAgreement.hpp"
#include "CombatStartMailbox.hpp"
#include <array>
#include <cstddef>
#include <optional>
#include <atomic>
namespace sc6::research {
// Fixed little-endian encoding, independent of compiler packing and padding.
struct CombatStartPacket {
    using Bytes=std::array<unsigned char,80>;
    CombatStartAgreement::Offer offer;
    uint32_t kind{1};uint64_t echo{};
    static void put(Bytes& out,unsigned offset,uint64_t value,unsigned length){
        for(unsigned i=0;i<length;++i)out[offset+i]=static_cast<unsigned char>(value>>(i*8));
    }
    static uint64_t get(const Bytes& in,unsigned offset,unsigned length){
        uint64_t value=0;for(unsigned i=0;i<length;++i)value|=uint64_t(in[offset+i])<<(i*8);return value;
    }
    Bytes encode()const{
        Bytes out{};put(out,0,0x53433643,4);put(out,4,1,4);put(out,8,kind,4);
        put(out,12,offer.sender,8);put(out,20,offer.recipient,8);put(out,28,offer.ticket,8);put(out,36,offer.build,8);
        const uint32_t fields[]={offer.round,offer.event,offer.first_frame,offer.slot,offer.delay,offer.rollback,offer.records};
        for(unsigned i=0;i<7;++i)put(out,44+i*4,fields[i],4);put(out,72,echo,8);return out;
    }
    static std::optional<CombatStartPacket> decode(const Bytes& in,size_t size){
        if(size!=in.size() || get(in,0,4)!=0x53433643 || get(in,4,4)!=1)return {};
        CombatStartPacket p;p.kind=static_cast<uint32_t>(get(in,8,4));p.echo=get(in,72,8);
        if((p.kind<1 || p.kind>3) || (p.kind==1 && p.echo) || (p.kind>=2 && !p.echo))return {};
        p.offer={get(in,12,8),get(in,20,8),get(in,28,8),get(in,36,8),
            static_cast<uint32_t>(get(in,44,4)),static_cast<uint32_t>(get(in,48,4)),static_cast<uint32_t>(get(in,52,4)),
            static_cast<uint32_t>(get(in,56,4)),static_cast<uint32_t>(get(in,60,4)),static_cast<uint32_t>(get(in,64,4)),static_cast<uint32_t>(get(in,68,4))};
        CombatStartAgreement validator;if(!validator.begin(p.offer))return {};return p;
    }
};
// Network-thread state only. A completed agreement still needs an owner-thread
// transfer and live start-boundary validation before it can authorize activation.
class CombatStartExchange {
    CombatStartAgreement agreement_;
    CombatStartAgreement::Offer local_{};
    uint64_t expected_steam_peer_{},peer_ticket_{};
    bool bound_{};
    CombatStartMailbox* mailbox_{};bool published_{};
    std::atomic<uint64_t> held_ticket_{0};
    CombatStartAgreement::Offer completed_peer_{};
    std::atomic<bool> completed_{};
    std::atomic<uint64_t> retired_packets_{};
    const CombatStartExchange* previous_{};unsigned depth_{};
    void publish_ready(){
        if(held_ticket_.load(std::memory_order_acquire)==local_.ticket)agreement_.local_held(local_.ticket);
        if(mailbox_ && !published_ && agreement_.activation_ready())published_=mailbox_->publish(agreement_);
        if(published_)completed_.store(true,std::memory_order_release);
    }
public:
    // Game thread calls only after validating its bound native hold. Ticket is
    // immutable and prevents another start attempt's notification from matching.
    bool report_local_hold(uint64_t ticket){uint64_t empty=0;return ticket && held_ticket_.compare_exchange_strong(empty,ticket,std::memory_order_release);}
    bool attach(CombatStartMailbox& mailbox){if(bound_ || mailbox_)return false;mailbox_=&mailbox;return true;}
    bool begin(uint64_t steam_peer,const CombatStartAgreement::Offer& local,const CombatStartExchange* previous=nullptr){
        if(previous && (!previous->completed_.load(std::memory_order_acquire) || previous->expected_steam_peer_!=steam_peer ||
            previous->local_.sender!=local.sender || previous->local_.recipient!=local.recipient || previous->depth_>=15))return false;
        for(auto* old=previous;old;old=old->previous_)
            if(old->local_.round==local.round && old->local_.event==local.event)return false;
        if(bound_ || !steam_peer || !agreement_.begin(local))return false;
        previous_=previous;depth_=previous?previous->depth_+1:0;
        local_=local;expected_steam_peer_=steam_peer;bound_=true;return true;
    }
    CombatStartPacket::Bytes outgoing(){
        publish_ready();const bool held=held_ticket_.load(std::memory_order_acquire)==local_.ticket;
        return CombatStartPacket{local_,peer_ticket_?(held?3u:2u):1u,peer_ticket_}.encode();
    }
    bool receive(uint64_t steam_peer,bool mutual,const CombatStartPacket::Bytes& bytes,size_t size){
        if(!bound_ || !mutual || steam_peer!=expected_steam_peer_)return false;
        const auto p=CombatStartPacket::decode(bytes,size);
        if(!p || p->offer.sender!=local_.recipient || p->offer.recipient!=local_.sender)return false;
        // An old, authenticated start/ack must not poison the new agreement.
        // Exact completed offers only; no numeric "older round" shortcut.
        for(auto* old=previous_;old;old=old->previous_){
            if(p->offer==old->completed_peer_ && (p->kind==1 || p->echo==old->local_.ticket)){
                ++retired_packets_;return true;
            }
        }
        if(p->offer.round!=local_.round || p->offer.event!=local_.event)return false;
        if(!agreement_.receive(p->offer))return false;peer_ticket_=p->offer.ticket;
        const bool accepted=p->kind==1 || agreement_.acknowledge(p->offer.sender,p->echo,p->offer.ticket);
        if(!accepted)return false;
        if(p->kind==3 && !agreement_.peer_held(p->offer.ticket))return false;
        if(!completed_.load(std::memory_order_relaxed))completed_peer_=p->offer;
        publish_ready();return true;
    }
    bool consume(){return agreement_.consume(local_);}
    uint64_t retired_packets()const{return retired_packets_.load(std::memory_order_relaxed);}
};
// Early packets are consumed, authenticated, and deferred without publishing an
// agreement. The peer retries every 250 ms; no unbounded pre-start queue exists.
struct CombatStartIngress {
    enum class Result {rejected,deferred,delivered};
    static Result receive(CombatStartExchange* exchange,bool continuous,bool mutual,
        uint64_t sender,uint64_t expected_peer,uint64_t local,uint64_t remote,uint64_t build,
        const CombatStartPacket::Bytes& bytes,size_t size){
        const auto packet=CombatStartPacket::decode(bytes,size);
        if(!continuous || !mutual || sender!=expected_peer || !local || !remote || !packet ||
           packet->offer.sender!=remote || packet->offer.recipient!=local || packet->offer.build!=build)
            return Result::rejected;
        if(!exchange)return Result::deferred;
        return exchange->receive(sender,mutual,bytes,size)?Result::delivered:Result::rejected;
    }
};
// Publish before polling; exchange is then owned exclusively by the network
// worker and must outlive it. This pointer alone never arms native execution.
inline std::atomic<CombatStartExchange*> combat_start_exchange{};
}
