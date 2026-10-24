#include "../src/FreshRawSender.hpp"
#include <cassert>
#include <sstream>
#include <iostream>
using namespace sc6::research;
std::atomic<unsigned> sends{},heartbeats{};
std::atomic<bool> incoming{true},require_receive_first{},inject_during_send{};
bool send_packet(void*,uint64_t peer,const void* data,uint32_t bytes,int mode,int channel){
 if(require_receive_first)assert(!incoming.load());
 assert(peer==123 && bytes==sizeof(sc6::steam_inputs::Packet) && mode==1 && (channel==27861 || channel==RawPredictionPacket::fast_channel));
 const auto& packet=*static_cast<const sc6::steam_inputs::Packet*>(data);
 if(packet.kind==PeerLivenessPacket::kind){assert(packet.nonce==11 && packet.echo==22 && packet.index);++heartbeats;return true;}
 RawInputConfirmation raw;assert(RawPredictionPacket::decode(*static_cast<const sc6::steam_inputs::Packet*>(data),raw));
 assert(raw.sender==11 && raw.recipient==22 && raw.raw==7);++sends;
 if(inject_during_send.exchange(false))incoming=true;return true;
}

bool available_packet(void*,uint32_t* size,int channel){assert(channel==RawPredictionPacket::fast_channel);*size=sizeof(sc6::steam_inputs::Packet);return incoming.load();}
bool read_packet(void*,void* out,uint32_t capacity,uint32_t* size,uint64_t* sender,int channel){
 assert(channel==RawPredictionPacket::fast_channel&&capacity>=sizeof(sc6::steam_inputs::Packet));
 if(!incoming.exchange(false))return false;
 sc6::steam_inputs::Packet p;assert(RawPredictionPacket::encode({22,11,3,4,100,2,0},p));
 std::memcpy(out,&p,sizeof(p));*size=sizeof(p);*sender=123;return true;
}
int main(){
 { // Missing fresh packet can be recovered by a later fast-channel repeat.
  auto route=std::make_unique<RawPredictionRoute<>>();
  assert(route->bind(11,22,3,4,100,1,99,{},nullptr,2));
  assert(route->publish_local(99,0,7));
  auto receiver=std::make_unique<RawPredictionRoute<>>();
  assert(receiver->bind(22,11,3,4,100,2,99,{}));
  FreshRawCursor fresh;FreshRawRedundancy repeat;
  fresh.pump(route.get(),11,22,[&](const auto& raw){
   if(raw.frame!=100)assert(receiver->submit(raw)==RawPredictionRoute<>::Submit::accepted);
   return true; // Simulate network loss after successful enqueue of frame 100.
  });
  assert(receiver->drain(99));uint32_t recovered{};assert(!receiver->truth(99,0,recovered));
  unsigned copies=0;
  repeat.pump(fresh,[&](const auto& raw){assert(raw.frame==100+copies);++copies;
   assert(receiver->submit(raw)==RawPredictionRoute<>::Submit::accepted);
  });
  assert(receiver->drain(99) && receiver->truth(99,0,recovered) && recovered==0);
  assert(copies==2 && fresh.next==3);
  repeat.pump(fresh,[&](const auto&){assert(false);}); // No timer-driven flood.
  auto other=std::make_unique<RawPredictionRoute<>>();
  assert(other->bind(11,22,5,6,200,1,99,{}));
  assert(other->publish_local(99,0,9));
  fresh.pump(other.get(),11,22,[](const auto&){return true;});
  repeat.pump(fresh,[&](const auto&){assert(false);}); // New round has no predecessor.
 }
 {
  ConfirmationReceiptGrace grace;assert(grace.enable());
  grace.wait([]{return true;}); // Existing receipt must not sleep.
  unsigned checks=0;
  grace.wait([&]{return ++checks==2;}); // Receipt between initial check and arming.
  checks=0;grace.wait([&]{if(++checks==2)grace.notify();return false;}); // Wake cannot be lost before kernel wait.
  grace.wait([]{return false;}); // Bounded timer expiration, no polling loop.
  std::ostringstream timing;grace.write(timing);
  assert(timing.str().find("receipt_grace_waits=2\n")!=std::string::npos);
  assert(timing.str().find("receipt_grace_wakes=1\n")!=std::string::npos);
  assert(timing.str().find("receipt_grace_timeouts=1\n")!=std::string::npos);
 }
 { // The sender transmits the N+2 sample while simulation is still at N.
  auto delayed=std::make_unique<RawPredictionRoute<>>();assert(delayed->bind(11,22,3,4,100,1,99,{},nullptr,2));
  assert(delayed->publish_local(99,0,42));FreshRawCursor cursor;unsigned sent_now=0;
  cursor.pump(delayed.get(),11,22,[&](const auto& raw){
   assert(raw.frame==100+sent_now && raw.raw==(sent_now==2?42u:0u));++sent_now;return true;
  });
  assert(sent_now==3 && delayed->delivery().head()==0);
 }

 // Early publication does not advance delivery, is immutable, and is reused by filtering.
 auto early=std::make_unique<RawPredictionRoute<>>();assert(early->bind(11,22,3,4,100,1,99,{}));
 assert(!early->publish_local(99,1,7));assert(early->publish_local(99,0,7));
 assert(early->delivery().head()==0 && early->outgoing_count()==1);
 assert(early->publish_local(99,0,7) && early->outgoing_count()==1);
 assert(early->append(99,7,{},{},false,0) && early->delivery().head()==1 && early->outgoing_count()==1);
 assert(early->publish_local(99,1,9));
 assert(!early->append(99,8,{},{},false,0) && early->failed() && early->delivery().head()==1);
 for(unsigned slot:{1u,2u}){
  auto q=std::make_unique<RawPredictionRoute<>>();assert(q->bind(11,22,8,9,400,slot,99,{}));
  assert(q->publish_local(99,0,42));RawInputConfirmation out;
  assert(q->outgoing(0,out) && out.slot==slot && out.frame==400 && out.raw==42);
  assert(q->delivery().head()==0 && q->request_handoff(99));
  assert(!q->publish_local(99,0,42));assert(q->retire(99));
 }
 auto conflict=std::make_unique<RawPredictionRoute<>>();assert(conflict->bind(11,22,3,4,100,1,99,{}));
 assert(conflict->publish_local(99,0,1));assert(!conflict->publish_local(99,0,2) && conflict->failed());
 // Publications before waiting persist, repeated signals coalesce, consumed signals reset.
 RawPublicationSignal signal;assert(signal.event);signal.notify();signal.notify();
 assert(WaitForSingleObject(signal.event,0)==WAIT_OBJECT_0);
 assert(WaitForSingleObject(signal.event,0)==WAIT_TIMEOUT);
 auto a=std::make_unique<RawPredictionRoute<>>();assert(a->bind(11,22,3,4,100,1,99,{}));
 assert(a->append(99,7,{},{},false,0));
 FreshRawCursor c;unsigned attempts=0;
 c.pump(a.get(),11,22,[&](auto){++attempts;return false;});assert(c.next==0&&attempts==1);
 c.pump(a.get(),11,23,[&](auto){assert(false);return true;});assert(c.next==0);
 c.pump(a.get(),11,22,[&](auto raw){assert(raw.frame==100&&raw.raw==7);return true;});assert(c.next==1);
 c.pump(a.get(),11,22,[&](auto){assert(false);return true;});
 auto b=std::make_unique<RawPredictionRoute<>>();assert(b->bind(11,22,4,5,200,1,99,{}));
 assert(b->append(99,7,{},{},false,0));c.pump(b.get(),11,22,[&](auto raw){assert(raw.frame==200);return true;});assert(c.next==1);
 assert(b->reconcile(99,[](auto&){return true;}));assert(b->publish_confirmed(99,[](auto...){return true;}));
 assert(b->request_handoff(99)&&b->retire(99));c.next=0;c.pump(b.get(),11,22,[&](auto){assert(false);return true;});
 raw_prediction_transport.store(a.get());FreshRawSender worker;
 assert(worker.start(reinterpret_cast<void*>(1),send_packet,123,11,22,27861));
 for(unsigned n=0;n<200 && !sends.load();++n)Sleep(1);
 worker.stop();assert(sends==1);Sleep(3);assert(sends==1);worker.stop();
 std::ostringstream report;worker.report(report);assert(report.str().find("fresh_raw_sender_faults=0")!=std::string::npos);
 // Reuse the same worker across three matches, including a fresh route.
 for(unsigned match=0;match<3;++match){
  auto next=std::make_unique<RawPredictionRoute<>>();assert(next->bind(11,22,10+match,20+match,500,1,99,{}));
  assert(next->publish_local(99,0,7));raw_prediction_transport=next.get();
  const auto prior=sends.load();assert(worker.start(reinterpret_cast<void*>(1),send_packet,123,11,22,27861));
  assert(worker.start(reinterpret_cast<void*>(1),send_packet,123,11,22,27861)); // idempotent
  for(unsigned n=0;n<200 && sends.load()==prior;++n)Sleep(1);
  worker.stop();worker.stop();assert(sends.load()==prior+1);
  std::ostringstream cycle;worker.report(cycle);
  assert(cycle.str().find("fresh_raw_sender_sent=1\n")!=std::string::npos);
  assert(cycle.str().find("fresh_raw_sender_lifetime_starts="+std::to_string(match+2)+"\n")!=std::string::npos);
 }
 raw_prediction_transport=a.get();
 // The receive worker queues authenticated raw input, leaving application to owner.
 require_receive_first=true;
 FreshRawSender duplex;assert(duplex.start(reinterpret_cast<void*>(1),send_packet,123,11,22,27861,available_packet,read_packet));
 for(unsigned n=0;n<200 && incoming.load();++n)Sleep(1);
 duplex.stop();require_receive_first=false;assert(!incoming);assert(a->drain(99));
 std::ostringstream receive_report;duplex.report(receive_report);
 assert(receive_report.str().find("fresh_raw_receiver_received=1")!=std::string::npos);
 // Receipt becomes available during send; it is queued before the timer wait.
 incoming=false;inject_during_send=true;FreshRawSender post_send;
 assert(post_send.start(reinterpret_cast<void*>(1),send_packet,123,11,22,27861,available_packet,read_packet));
 for(unsigned n=0;n<200 && (inject_during_send.load() || incoming.load());++n)Sleep(1);
 post_send.stop();assert(!inject_during_send && !incoming);
 std::ostringstream post_report;post_send.report(post_report);
 assert(post_report.str().find("fresh_raw_receiver_post_send_passes=1\n")!=std::string::npos);
 assert(post_report.str().find("fresh_raw_receiver_post_send_received=1\n")!=std::string::npos);
 assert(a->drain(99));
 sc6::steam_inputs::Packet packet;assert(RawPredictionPacket::encode({22,11,3,4,100,2,0},packet));
 using S=RawPredictionRoute<>::Submit;
 assert(FreshRawIngress::receive(packet,999,123,11,22,a.get())==S::rejected);
 assert(FreshRawIngress::receive(packet,123,123,12,22,a.get())==S::rejected);
 packet.magic=0;assert(FreshRawIngress::receive(packet,123,123,11,22,a.get())==S::rejected);
 assert(RawPredictionPacket::encode({22,11,3,4,100,2,0},packet));packet.record.data[5]=1;
 assert(FreshRawIngress::receive(packet,123,123,11,22,a.get())==S::rejected);
 // Wake a sleeping worker with newly published input; no game-thread network calls.
 auto live=std::make_unique<RawPredictionRoute<>>();assert(live->bind(11,22,5,6,300,1,99,{}));
 raw_prediction_transport=live.get();FreshRawSender awakened;const auto before=sends.load();
 assert(awakened.start(reinterpret_cast<void*>(1),send_packet,123,11,22,27861));
 Sleep(3);assert(live->append(99,7,{},{},false,0));
 for(unsigned n=0;n<200 && sends.load()==before;++n)Sleep(1);
 awakened.stop();assert(sends.load()==before+1);
 std::ostringstream wake_report;awakened.report(wake_report);
 assert(wake_report.str().find("fresh_raw_sender_publication_wake_enabled=1")!=std::string::npos);
 assert(wake_report.str().find("fresh_raw_sender_faults=0")!=std::string::npos);
 raw_prediction_transport=nullptr;
 { // No raw route exists between rounds: liveness still runs, without control polling.
  incoming=false;FreshRawSender heartbeat_worker;
  assert(heartbeat_worker.start(reinterpret_cast<void*>(1),send_packet,123,11,22,27861,available_packet,read_packet,true));
  for(unsigned n=0;n<500 && heartbeats.load()<2;++n)Sleep(1);
  heartbeat_worker.stop();assert(heartbeats.load()>=2);auto count=heartbeats.load();Sleep(60);assert(heartbeats.load()==count);
  std::ostringstream heartbeat_report;heartbeat_worker.report(heartbeat_report);assert(heartbeat_report.str().find("peer_liveness_sent=0\n")==std::string::npos);
 }
 { // Receipt extends only an already-established, exact match identity.
  CombatPeerContext context;sc6::steam_handshake::Protocol protocol;
  protocol.expected_peer=123;protocol.local_nonce=11;protocol.remote_nonce=22;protocol.proof=protocol.mutual=true;
  auto heartbeat_packet=PeerLivenessPacket::make(22,11,1);
  assert(!PeerLivenessPacket::receive(heartbeat_packet,123,123,11,22,1000,context));
  assert(context.publish(protocol,1,1000,true));
  assert(!PeerLivenessPacket::receive(heartbeat_packet,999,123,11,22,1050,context));
  assert(!PeerLivenessPacket::receive(heartbeat_packet,123,123,12,22,1050,context));
  heartbeat_packet.record.data[0]=1;assert(!PeerLivenessPacket::receive(heartbeat_packet,123,123,11,22,1050,context));heartbeat_packet.record.data[0]=0;
  assert(PeerLivenessPacket::receive(heartbeat_packet,123,123,11,22,1050,context));
  assert(!PeerLivenessPacket::receive(heartbeat_packet,123,123,11,22,1100,context)); // duplicate cannot renew lease
  for(unsigned n=2;n<=12;++n){heartbeat_packet.index=n;assert(PeerLivenessPacket::receive(heartbeat_packet,123,123,11,22,1000+n*50,context));}
  CombatPeerContext::Snapshot snapshot;uint64_t now{};
  assert(context.inspect_current(snapshot,now,[]{return uint64_t(1600);})==CombatPeerContext::Check::ready);
  assert(snapshot.control_tick==1000 && snapshot.heartbeat_tick==1600);
  context.publish(protocol,1,1200,true); // delayed control publication must not regress evidence
  assert(context.inspect_current(snapshot,now,[]{return uint64_t(1601);})==CombatPeerContext::Check::ready);
  assert(context.inspect_current(snapshot,now,[]{return uint64_t(1851);})==CombatPeerContext::Check::stale);
  context.clear();heartbeat_packet.index=13;assert(!PeerLivenessPacket::receive(heartbeat_packet,123,123,11,22,1900,context));
 }
 std::cout<<"Fresh send retry, identity, replacement, retirement, wire envelope and worker stop passed\n";
}
