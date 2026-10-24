#pragma once
#include <cstdint>
namespace sc6::research {
// Only authenticated transport may deliver these values. Not a wire struct.
class CombatStartAgreement {
public:
 struct Offer {
  uint64_t sender{},recipient{},ticket{},build{};
  uint32_t round{},event{},first_frame{},slot{},delay{},rollback{},records{};
  bool operator==(const Offer&)const=default;
 };
private:
 Offer local_{},peer_{};bool bound_{},received_{},ack_{},failed_{},consumed_{};
 bool local_held_{},peer_held_{};
 bool peer_reserved_{};
public:
 bool ready()const{return bound_ && received_ && ack_ && !failed_ && !consumed_;}
 bool activation_ready()const{return ready() && local_held_ && (peer_held_ || peer_reserved_);}
 // Only the authenticated transition protocol may supply this promise. Its
 // owner has installed a boundary reservation before advertising the offer.
 // Local activation still requires a physical hold and exact Fight validation.
 bool peer_reserved(uint64_t ticket){if(!ready() || ticket!=peer_.ticket)return false;peer_reserved_=true;return true;}
 unsigned agreed_delay()const{return local_.delay>peer_.delay?local_.delay:peer_.delay;}
 unsigned peer_delay()const{return peer_.delay;}
 bool local_held(uint64_t ticket){if(!bound_ || failed_ || consumed_ || ticket!=local_.ticket)return false;local_held_=true;return true;}
 bool peer_held(uint64_t ticket){if(!ready() || ticket!=peer_.ticket)return false;peer_held_=true;return true;}
 bool begin(const Offer& x){
  if(bound_ || !x.sender || !x.recipient || x.sender==x.recipient || !x.ticket || !x.build || !x.round ||
    x.first_frame==UINT32_MAX || x.slot<1 || x.slot>2 || x.rollback!=3 || x.delay>2 || !x.records || x.records>7200)return false;
  local_=x;bound_=true;return true;
 }
 bool receive(const Offer& x){
  if(!bound_ || failed_ || consumed_)return false;
  if(x.sender!=local_.recipient || x.recipient!=local_.sender || !x.ticket || x.build!=local_.build ||
    x.round!=local_.round || x.event!=local_.event || x.first_frame!=local_.first_frame || x.slot!=3-local_.slot ||
    x.delay>2 || x.rollback!=local_.rollback || x.records!=local_.records || (received_ && x!=peer_)){failed_=true;return false;}
  peer_=x;received_=true;return true;
 }
 bool acknowledge(uint64_t sender,uint64_t local_ticket,uint64_t peer_ticket){
  if(!bound_ || !received_ || failed_ || consumed_)return false;
  if(sender!=peer_.sender || local_ticket!=local_.ticket || peer_ticket!=peer_.ticket){failed_=true;return false;}
  ack_=true;return true;
 }
 bool consume(const Offer& expected){
  if(!bound_ || !received_ || !ack_ || failed_ || consumed_ || expected!=local_)return false;
  consumed_=true;return true;
 }
};
}
