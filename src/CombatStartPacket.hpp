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
    using Bytes=std::array<unsigned char,112>;
    CombatStartAgreement::Offer offer;
    uint32_t kind{1};uint64_t echo{};
    CombatStartSync::Wire timing{};
    static void put(Bytes& out,unsigned offset,uint64_t value,unsigned length){
        for(unsigned i=0;i<length;++i)out[offset+i]=static_cast<unsigned char>(value>>(i*8));
    }
    static uint64_t get(const Bytes& in,unsigned offset,unsigned length){
        uint64_t value=0;for(unsigned i=0;i<length;++i)value|=uint64_t(in[offset+i])<<(i*8);return value;
    }
    Bytes encode()const{
        Bytes out{};put(out,0,0x53433643,4);put(out,4,2,4);put(out,8,kind,4);
        put(out,12,offer.sender,8);put(out,20,offer.recipient,8);put(out,28,offer.ticket,8);put(out,36,offer.build,8);
        const uint32_t fields[]={offer.round,offer.event,offer.first_frame,offer.slot,offer.delay,offer.rollback,offer.records};
        for(unsigned i=0;i<7;++i)put(out,44+i*4,fields[i],4);put(out,72,echo,8);
        put(out,80,timing.sent,8);put(out,88,timing.echo,8);put(out,96,timing.received,8);put(out,104,timing.target,8);return out;
    }
    static std::optional<CombatStartPacket> decode(const Bytes& in,size_t size){
        if(size!=in.size() || get(in,0,4)!=0x53433643 || get(in,4,4)!=2)return {};
        CombatStartPacket p;p.kind=static_cast<uint32_t>(get(in,8,4));p.echo=get(in,72,8);
        if((p.kind<1 || p.kind>10) || ((p.kind==1 || p.kind==7 || p.kind==9) && p.echo) ||
           (p.kind!=1 && p.kind!=7 && p.kind!=9 && !p.echo))return {};
        p.timing={get(in,80,8),get(in,88,8),get(in,96,8),get(in,104,8)};
        p.offer={get(in,12,8),get(in,20,8),get(in,28,8),get(in,36,8),
            static_cast<uint32_t>(get(in,44,4)),static_cast<uint32_t>(get(in,48,4)),static_cast<uint32_t>(get(in,52,4)),
            static_cast<uint32_t>(get(in,56,4)),static_cast<uint32_t>(get(in,60,4)),static_cast<uint32_t>(get(in,64,4)),static_cast<uint32_t>(get(in,68,4))};
        CombatStartAgreement validator;if(!validator.begin(p.offer))return {};return p;
    }
};
// Kinds 7/8 carry intent only. Kinds 9/10 carry an irrevocable owner-thread
// reservation of an exact future Fight boundary; they do not claim a held clock.
// Owner initializes once before publication; the network worker owns peer state.
class CombatRoundPreparation {
    CombatStartAgreement::Offer local_{},peer_{};
    uint64_t steam_peer_{},created_{},sent_at_{},used_at_{};unsigned last_kind_{};
    bool received_{};
    const bool reserved_;
    std::atomic<bool> agreed_{},failed_{};
    std::atomic<uint64_t> received_count_{},rejected_{},seeded_{};
    std::atomic<unsigned> rejection_stage_{};
public:
    CombatRoundPreparation* previous{};
    CombatRoundPreparation(uint64_t steam_peer,CombatStartAgreement::Offer offer,uint64_t now,bool reserved=false):
        local_(offer),steam_peer_(steam_peer),created_(now),reserved_(reserved){if(!reserved_)local_.first_frame=0;}
    bool fresh(uint64_t now)const{return now>=created_ && now-created_<=60000000 && !failed_.load();}
    bool matches(CombatStartAgreement::Offer offer)const{if(!reserved_)offer.first_frame=0;return offer==local_;}
    bool reserved()const{return reserved_;}
    const CombatStartAgreement::Offer& peer_offer()const{return peer_;} // network worker only
    unsigned delay()const{return local_.delay;}
    bool context(uint64_t peer,uint64_t local,uint64_t remote,uint64_t build)const{
        return peer==steam_peer_ && local==local_.sender && remote==local_.recipient && build==local_.build;
    }
    bool receive(uint64_t peer,const CombatStartPacket& packet,uint64_t now){
        auto reject=[&](unsigned stage){++rejected_;rejection_stage_=stage;return false;};
        const unsigned first=reserved_?9u:7u;
        if(peer!=steam_peer_)return reject(1);
        if(!fresh(now))return reject(2);
        if(packet.kind<first || packet.kind>first+1)return reject(3);
        if((packet.kind==first && packet.echo) || (!reserved_ && packet.offer.first_frame) || packet.timing.sent ||
           packet.timing.echo || packet.timing.received || packet.timing.target)return reject(4);
        CombatStartAgreement check;
        if(!check.begin(local_) || !check.receive(packet.offer))return reject(5);
        if(received_ && packet.offer!=peer_){failed_=true;return reject(6);}
        if(packet.kind==first+1 && packet.echo!=local_.ticket)return reject(7);
        if(!received_){peer_=packet.offer;received_=true;}
        if(packet.kind==first+1)agreed_.store(true,std::memory_order_release);
        ++received_count_;return true;
    }
    CombatStartPacket::Bytes outgoing()const{return CombatStartPacket{local_,(reserved_?9u:7u)+(received_?1u:0u),received_?peer_.ticket:0,{}}.encode();}
    bool due(uint64_t now){
        if(!fresh(now))return false;
        const auto kind=(reserved_?9u:7u)+(received_?1u:0u);
        const uint64_t interval=used_at_ && now>=used_at_ && now-used_at_>=1000000?250000:20000;
        if(sent_at_ && kind==last_kind_ && now>=sent_at_ && now-sent_at_<interval)return false;
        sent_at_=now;last_kind_=kind;return true;
    }
    uint64_t seed(const CombatStartAgreement::Offer& actual,uint64_t now){
        if(!fresh(now) || !matches(actual) || !agreed_.load(std::memory_order_acquire))return 0;
        if(!used_at_)used_at_=now;++seeded_;return peer_.ticket;
    }
    // Network-thread only; readiness publication above makes no native calls.
    bool import_reservation(const CombatStartAgreement::Offer& actual,uint64_t now,CombatStartAgreement& agreement){
        if(!reserved_ || !seed(actual,now))return false;
        return agreement.receive(peer_) && agreement.acknowledge(peer_.sender,local_.ticket,peer_.ticket) &&
            agreement.peer_reserved(peer_.ticket);
    }
    // A held-frame acknowledgment is stronger than an early reservation ACK.
    // It may repair a lost transition ACK, but only for the exact reservation
    // installed by the native owner. Intent-only, expired and conflicting offers
    // cannot use this path. The caller validates authentication and packet timing.
    bool held_reservation(const CombatStartAgreement::Offer& actual,
                          const CombatStartAgreement::Offer& peer,uint64_t echo,uint64_t now)const{
        if(!reserved_ || !fresh(now) || !matches(actual) || (received_ && peer!=peer_))return false;
        CombatStartAgreement check;
        return check.begin(local_) && check.receive(peer) && check.acknowledge(peer.sender,echo,peer.ticket);
    }
    // peer_ is written exactly once, before the release-store to agreed_. Later
    // conflicting offers only latch failed_; they never overwrite peer_. This
    // immutable copy lets the owner finish activation without waiting for the
    // worker to discover the newly published exchange on its next poll.
    bool copy_reservation(const CombatStartAgreement::Offer& actual,uint64_t now,
                          CombatStartAgreement::Offer& peer)const{
        if(!reserved_ || !agreed_.load(std::memory_order_acquire) || !fresh(now) || !matches(actual))return false;
        peer=peer_;return true;
    }
    template<class Stream>void report(Stream& out)const{
        for(auto* p=this;p;p=p->previous)
            out<<"round_preparation_"<<p->local_.event<<"_agreed="<<p->agreed_.load()
               <<"\nround_preparation_"<<p->local_.event<<"_received="<<p->received_count_.load()
               <<"\nround_preparation_"<<p->local_.event<<"_rejected="<<p->rejected_.load()
               <<"\nround_preparation_"<<p->local_.event<<"_seeded="<<p->seeded_.load()
               <<"\nround_preparation_"<<p->local_.event<<"_last_rejection_stage="<<p->rejection_stage_.load()
               <<"\nround_preparation_"<<p->local_.event<<"_failed="<<p->failed_.load()
               <<"\nround_preparation_"<<p->local_.event<<"_reserved="<<p->reserved_
               <<"\nround_preparation_"<<p->local_.event<<"_first_frame="<<p->local_.first_frame<<'\n';
    }
};
inline std::atomic<CombatRoundPreparation*> combat_round_preparation{};
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
    CombatStartSync sync_;
    struct Delivery {uint64_t sent{},received{},accepted{},first_sent{},last_sent{},first_received{},last_received{},immediate_ok{},reliable_ok{},send_failed{};};
    std::array<Delivery,7> delivery_{};
    uint64_t rejection_stage_{},rejection_time_{};
    bool timing_initialized_{};
    bool transition_seeded_{};
    bool transition_reserved_{};
    uint64_t held_reservation_recovered_{},held_reservation_rejected_{};
    void import_transition(uint64_t now){
        if(transition_reserved_ || published_ || sync_failed_.load(std::memory_order_acquire))return;
        auto* prep=combat_round_preparation.load(std::memory_order_acquire);
        if(!prep || !prep->context(expected_steam_peer_,local_.sender,local_.recipient,local_.build))return;
        if(prep->import_reservation(local_,now,agreement_)){
            transition_reserved_=true;transition_seeded_=true;
            peer_ticket_=prep->seed(local_,now);
            completed_peer_=prep->peer_offer();
        }
    }
    std::atomic<bool> sync_failed_{};
    void initialize_timing(uint64_t now){
        // Called only by the network worker, after publication. The game thread
        // must not read the previous worker-owned estimator during begin().
        if(timing_initialized_)return;
        timing_initialized_=true;
        if(previous_)sync_.inherit_timing(previous_->sync_,now);
        if(previous_)if(auto* preparation=combat_round_preparation.load(std::memory_order_acquire)){
            if(preparation->context(expected_steam_peer_,local_.sender,local_.recipient,local_.build)){
                const auto ticket=preparation->seed(local_,now);
                if(!transition_reserved_){peer_ticket_=ticket;transition_seeded_=peer_ticket_!=0;}
            }
        }
    }
    void publish_ready(){
        if(held_ticket_.load(std::memory_order_acquire)==local_.ticket)agreement_.local_held(local_.ticket);
        if(sync_.failed && !transition_reserved_)sync_failed_.store(true,std::memory_order_release);
        if(mailbox_ && !published_ && agreement_.activation_ready() && (transition_reserved_ || sync_.can_publish()))published_=mailbox_->publish(agreement_,transition_reserved_?0:sync_.local_target);
        if(published_)completed_.store(true,std::memory_order_release);
    }
public:
    // Owner-thread only, before publishing this exchange. Only immutable
    // reservation data is read here, never the previous worker's clock samples.
    bool prime_reservation(const CombatRoundPreparation* prep,uint64_t now){
        CombatStartAgreement::Offer peer;
        if(!bound_ || published_ || !prep ||
           !prep->context(expected_steam_peer_,local_.sender,local_.recipient,local_.build) ||
           !prep->copy_reservation(local_,now,peer))return false;
        if(!agreement_.receive(peer) || !agreement_.acknowledge(peer.sender,local_.ticket,peer.ticket) ||
           !agreement_.peer_reserved(peer.ticket))return false;
        completed_peer_=peer;peer_ticket_=peer.ticket;transition_seeded_=transition_reserved_=true;
        publish_ready();return published_;
    }
    // Keep the reliable copy, but do not make a short release deadline depend
    // solely on ordered reliable retransmission. Both copies use identical,
    // authenticated tickets and targets; receive remains idempotent.
    template<class Send> bool transmit(const CombatStartPacket::Bytes& bytes,Send&& send,uint64_t now=0){
        const auto p=CombatStartPacket::decode(bytes,bytes.size());
        if(!p || p->kind>6)return false;
        auto& d=delivery_[p->kind];
        const bool immediate=send(1);if(immediate)++d.immediate_ok;else ++d.send_failed;
        bool reliable=false;
        if(p->kind>=4){reliable=send(2);if(reliable)++d.reliable_ok;else ++d.send_failed;}
        if(immediate || reliable){sent(bytes,now?now:CombatStartSync::now());return true;}
        return false;
    }
    // Game thread calls only after validating its bound native hold. Ticket is
    // immutable and prevents another start attempt's notification from matching.
    bool report_local_hold(uint64_t ticket){uint64_t empty=0;return ticket && held_ticket_.compare_exchange_strong(empty,ticket,std::memory_order_release);}
    bool attach(CombatStartMailbox& mailbox){if(bound_ || mailbox_)return false;mailbox_=&mailbox;return true;}
    bool begin(uint64_t steam_peer,const CombatStartAgreement::Offer& local,const CombatStartExchange* previous=nullptr){
        if(previous && (!previous->completed_.load(std::memory_order_acquire) || previous->expected_steam_peer_!=steam_peer ||
            previous->local_.sender!=local.sender || previous->local_.recipient!=local.recipient ||
            previous->local_.build!=local.build || previous->depth_>=15))return false;
        for(auto* old=previous;old;old=old->previous_)
            if(old->local_.round==local.round && old->local_.event==local.event)return false;
        if(bound_ || !steam_peer || !agreement_.begin(local))return false;
        previous_=previous;depth_=previous?previous->depth_+1:0;
        local_=local;expected_steam_peer_=steam_peer;sync_.leader=local.sender<local.recipient;bound_=true;return true;
    }
    CombatStartPacket::Bytes outgoing(uint64_t now=CombatStartSync::now()){
        initialize_timing(now);
        import_transition(now);
        publish_ready();const bool held=held_ticket_.load(std::memory_order_acquire)==local_.ticket;
        const auto fallback=peer_ticket_?(held?3u:2u):1u;
        const auto kind=transition_reserved_?fallback:sync_.kind(now,agreement_.activation_ready(),fallback);
        publish_ready();return CombatStartPacket{local_,kind,peer_ticket_,sync_.wire(now,kind)}.encode();
    }
    void sent(const CombatStartPacket::Bytes& bytes,uint64_t now=CombatStartSync::now()){
        const auto p=CombatStartPacket::decode(bytes,bytes.size());
        if(p && p->kind<=6){auto& d=delivery_[p->kind];++d.sent;if(!d.first_sent)d.first_sent=now;d.last_sent=now;
            sync_.sent(p->timing,p->kind,now);publish_ready();}
    }
    bool failed()const{return sync_failed_.load(std::memory_order_acquire);}
    template<class Stream> void report(Stream& out)const{
        out<<"start_sync_protocol=2\n";
        for(auto* p=this;p;p=p->previous_){
            out<<"start_sync_round_"<<p->local_.event<<"_samples="<<p->sync_.samples
               <<"\nstart_sync_round_"<<p->local_.event<<"_rtt_us="<<p->sync_.best_rtt
               <<"\nstart_sync_round_"<<p->local_.event<<"_worst_rtt_us="<<p->sync_.worst_rtt
               <<"\nstart_sync_round_"<<p->local_.event<<"_release_margin_us="<<p->sync_.release_margin
               <<"\nstart_sync_round_"<<p->local_.event<<"_carried_samples="<<p->sync_.carried_samples
               <<"\nstart_sync_round_"<<p->local_.event<<"_carried_age_us="<<p->sync_.carried_age_us
               <<"\nstart_sync_round_"<<p->local_.event<<"_transition_seeded="<<p->transition_seeded_
               <<"\nstart_sync_round_"<<p->local_.event<<"_transition_reserved="<<p->transition_reserved_
               <<"\nstart_sync_round_"<<p->local_.event<<"_held_reservation_recovered="<<p->held_reservation_recovered_
               <<"\nstart_sync_round_"<<p->local_.event<<"_held_reservation_rejected="<<p->held_reservation_rejected_
               <<"\nstart_sync_round_"<<p->local_.event<<"_offset_us="<<p->sync_.peer_offset
               <<"\nstart_sync_round_"<<p->local_.event<<"_leader="<<p->sync_.leader
               <<"\nstart_sync_round_"<<p->local_.event<<"_target_us="<<p->sync_.local_target
               <<"\nstart_sync_round_"<<p->local_.event<<"_released_us="<<(p->mailbox_?p->mailbox_->released_at():0)
               <<"\nstart_sync_round_"<<p->local_.event<<"_committed="<<p->sync_.committed
               <<"\nstart_sync_round_"<<p->local_.event<<"_failed="<<p->sync_.failed
               <<"\nstart_sync_round_"<<p->local_.event<<"_failure_reason="<<p->sync_.failure<<'\n';
            out<<"start_sync_round_"<<p->local_.event<<"_last_rejection_stage="<<p->rejection_stage_
               <<"\nstart_sync_round_"<<p->local_.event<<"_last_rejection_us="<<p->rejection_time_<<'\n';
            for(unsigned k=1;k<=6;++k){const auto& d=p->delivery_[k];
                out<<"start_sync_round_"<<p->local_.event<<"_delivery_"<<k<<"="
                   <<d.sent<<','<<d.received<<','<<d.accepted<<','<<d.first_sent<<','<<d.last_sent<<','
                   <<d.first_received<<','<<d.last_received<<','<<d.immediate_ok<<','<<d.reliable_ok<<','<<d.send_failed<<'\n';}
        }
    }
    bool receive(uint64_t steam_peer,bool mutual,const CombatStartPacket::Bytes& bytes,size_t size,uint64_t now=CombatStartSync::now()){
        if(!bound_ || !mutual || steam_peer!=expected_steam_peer_)return false;
        const auto p=CombatStartPacket::decode(bytes,size);
        if(!p || p->kind>6 || p->offer.sender!=local_.recipient || p->offer.recipient!=local_.sender)return false;
        auto reject=[&](uint64_t stage){rejection_stage_=stage;rejection_time_=now;return false;};
        // An old, authenticated start/ack must not poison the new agreement.
        // Exact completed offers only; no numeric "older round" shortcut.
        for(auto* old=previous_;old;old=old->previous_){
            if(p->offer==old->completed_peer_ && (p->kind==1 || p->echo==old->local_.ticket)){
                ++retired_packets_;return true;
            }
        }
        if(p->offer.round!=local_.round || p->offer.event!=local_.event)return reject(1);
        auto& delivery=delivery_[p->kind];++delivery.received;
        if(!delivery.first_received)delivery.first_received=now;delivery.last_received=now;
        initialize_timing(now);
        import_transition(now);
        if(!agreement_.receive(p->offer))return reject(2);peer_ticket_=p->offer.ticket;
        const bool accepted=p->kind==1 || agreement_.acknowledge(p->offer.sender,p->echo,p->offer.ticket);
        if(!accepted)return reject(3);
        if(p->kind>=3 && !agreement_.peer_held(p->offer.ticket))return reject(4);
        if(held_ticket_.load(std::memory_order_acquire)==local_.ticket)agreement_.local_held(local_.ticket);
        // A delayed legacy proposal cannot replace the reserved frame contract.
        // Continue collecting probes, but do not add a second release timer.
        if((!transition_reserved_ || p->kind<4) &&
           !sync_.receive(p->timing,p->kind,now,agreement_.activation_ready())){publish_ready();return reject(5);}
        if(p->kind==3 && !transition_reserved_ && !published_ && !sync_.failed){
            auto* prep=combat_round_preparation.load(std::memory_order_acquire);
            if(prep && prep->context(expected_steam_peer_,local_.sender,local_.recipient,local_.build) &&
               prep->held_reservation(local_,p->offer,p->echo,now)){
                transition_reserved_=true;++held_reservation_recovered_;
            }else ++held_reservation_rejected_;
        }
        ++delivery.accepted;
        if(!completed_.load(std::memory_order_relaxed))completed_peer_=p->offer;
        publish_ready();return true;
    }
    bool completed()const{return completed_.load(std::memory_order_acquire);}
    bool consume(){return agreement_.consume(local_);}
    uint64_t retired_packets()const{return retired_packets_.load(std::memory_order_relaxed);}
};
// Worker-only cadence. New offer/ack/held transitions send immediately;
// unchanged incomplete agreements retry at most 100 times/second.
struct CombatStartSendCadence {
    CombatStartPacket::Bytes last{};uint64_t sent_at{},completed_at{};bool sent{},saw_completion{};
    bool due(const CombatStartPacket::Bytes& packet,uint64_t now,bool completed){
        // A successor round must get its own fast commit-retry window even
        // though the connection's cadence object survives across exchanges.
        if(sent && !std::equal(packet.begin()+12,packet.begin()+72,last.begin()+12)){
            saw_completion=false;completed_at=0;
        }
        // Keep commit retransmissions fast through the release window. Publishing
        // locally must not throttle the first commit retry to 250 ms.
        if(completed && !saw_completion){saw_completion=true;completed_at=now;}
        const bool settled=completed && saw_completion && now>=completed_at && now-completed_at>=1000;
        // Probe timestamps change every poll; only semantic transitions bypass cadence.
        auto semantic=packet,prior=last;
        for(unsigned i=80;i<104;++i)semantic[i]=prior[i]=0;
        if(sent && semantic==prior && now>=sent_at && now-sent_at<(settled?250u:10u))return false;
        last=packet;sent_at=now;sent=true;return true;
    }
};
// Early packets are consumed, authenticated, and deferred without publishing an
// agreement. The peer retries on a bounded cadence; no unbounded pre-start queue exists.
struct CombatStartIngress {
    enum class Result {rejected,deferred,delivered};
    static Result receive(CombatStartExchange* exchange,bool continuous,bool mutual,
        uint64_t sender,uint64_t expected_peer,uint64_t local,uint64_t remote,uint64_t build,
        const CombatStartPacket::Bytes& bytes,size_t size){
        const auto packet=CombatStartPacket::decode(bytes,size);
        if(!continuous || !mutual || sender!=expected_peer || !local || !remote || !packet ||
           packet->offer.sender!=remote || packet->offer.recipient!=local || packet->offer.build!=build)
            return Result::rejected;
        if(packet->kind>=7){
            const auto prep=combat_round_preparation.load(std::memory_order_acquire);
            if(!prep || !prep->context(expected_peer,local,remote,build))return Result::deferred;
            return prep->receive(sender,*packet,CombatStartSync::now())?Result::delivered:Result::rejected;
        }
        if(!exchange)return Result::deferred;
        return exchange->receive(sender,mutual,bytes,size)?Result::delivered:Result::rejected;
    }
};
// Publish before polling; exchange is then owned exclusively by the network
// worker and must outlive it. This pointer alone never arms native execution.
inline std::atomic<CombatStartExchange*> combat_start_exchange{};
}
