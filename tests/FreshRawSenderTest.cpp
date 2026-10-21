#include "../src/FreshRawSender.hpp"
#include <cassert>
#include <sstream>
#include <iostream>
using namespace sc6::research;
std::atomic<unsigned> sends{};
bool send_packet(void*,uint64_t peer,const void* data,uint32_t bytes,int mode,int channel){
 assert(peer==123 && bytes==sizeof(sc6::steam_inputs::Packet) && mode==1 && (channel==27861 || channel==RawPredictionPacket::fast_channel));
 RawInputConfirmation raw;assert(RawPredictionPacket::decode(*static_cast<const sc6::steam_inputs::Packet*>(data),raw));
 assert(raw.sender==11 && raw.recipient==22 && raw.raw==7);++sends;return true;
}
std::atomic<bool> incoming{true};
bool available_packet(void*,uint32_t* size,int channel){assert(channel==RawPredictionPacket::fast_channel);*size=sizeof(sc6::steam_inputs::Packet);return incoming.load();}
bool read_packet(void*,void* out,uint32_t capacity,uint32_t* size,uint64_t* sender,int channel){
 assert(channel==RawPredictionPacket::fast_channel&&capacity>=sizeof(sc6::steam_inputs::Packet));
 if(!incoming.exchange(false))return false;
 sc6::steam_inputs::Packet p;assert(RawPredictionPacket::encode({22,11,3,4,100,2,0},p));
 std::memcpy(out,&p,sizeof(p));*size=sizeof(p);*sender=123;return true;
}
int main(){
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
 // The receive worker queues authenticated raw input, leaving application to owner.
 FreshRawSender duplex;assert(duplex.start(reinterpret_cast<void*>(1),send_packet,123,11,22,27861,available_packet,read_packet));
 for(unsigned n=0;n<200 && incoming.load();++n)Sleep(1);
 duplex.stop();assert(!incoming);assert(a->drain(99));
 std::ostringstream receive_report;duplex.report(receive_report);
 assert(receive_report.str().find("fresh_raw_receiver_received=1")!=std::string::npos);
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
 std::cout<<"Fresh send retry, identity, replacement, retirement, wire envelope and worker stop passed\n";
}
