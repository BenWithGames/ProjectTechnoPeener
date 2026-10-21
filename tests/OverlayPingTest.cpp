#include "../src/OverlayPingTransport.hpp"
#include <cassert>
#include <deque>
using Transport=sc6::OverlayPingTransport;
struct Loopback {std::mutex mutex;std::deque<Transport::Packet> replies;};
bool send_packet(void* self,uint64_t peer,const void* bytes,uint32_t size,int type,int channel){
 assert(peer==9&&size==sizeof(Transport::Packet)&&type==1&&channel==Transport::channel);
 Transport::Packet p;std::memcpy(&p,bytes,size);assert(p.kind==1);
 std::swap(p.nonce,p.echo);p.kind=2;auto& loop=*static_cast<Loopback*>(self);
 std::lock_guard lock(loop.mutex);loop.replies.push_back(p);return true;
}
bool available(void* self,uint32_t* size,int channel){
 assert(channel==Transport::channel);auto& loop=*static_cast<Loopback*>(self);
 std::lock_guard lock(loop.mutex);*size=sizeof(Transport::Packet);return !loop.replies.empty();
}
bool read_packet(void* self,void* bytes,uint32_t capacity,uint32_t* size,uint64_t* peer,int channel){
 assert(channel==Transport::channel&&capacity>=sizeof(Transport::Packet));auto& loop=*static_cast<Loopback*>(self);
 std::lock_guard lock(loop.mutex);if(loop.replies.empty())return false;
 std::memcpy(bytes,&loop.replies.front(),sizeof(Transport::Packet));loop.replies.pop_front();*size=sizeof(Transport::Packet);*peer=9;return true;
}
int main(){
 using T=sc6::OverlayPingTransport;
 T::Packet p{T::magic,22,11,0,1,1};
 assert(T::authenticated(p,sizeof(p),9,9,11,22));
 assert(!T::authenticated(p,sizeof(p)-1,9,9,11,22));
 assert(!T::authenticated(p,sizeof(p),8,9,11,22));
 assert(!T::authenticated(p,sizeof(p),9,9,12,22));
 assert(!T::authenticated(p,sizeof(p),9,9,11,23));
 p.handling=1;assert(!T::authenticated(p,sizeof(p),9,9,11,22));
 p.kind=2;assert(T::authenticated(p,sizeof(p),9,9,11,22));
 p.kind=3;assert(!T::authenticated(p,sizeof(p),9,9,11,22));
 p.kind=2;p.id=0;assert(!T::authenticated(p,sizeof(p),9,9,11,22));
 static_assert(T::channel!=27861&&T::channel!=27862&&T::channel!=27863);
 T transport;transport.configure(nullptr,0,0,0);assert(!transport.value());transport.stop();
 Loopback loop;T worker({send_packet,available,read_packet});worker.configure(&loop,9,11,22);
 // No game update/configuration calls: the worker must collect the burst itself.
 for(unsigned i=0;i<100&&!worker.value();++i)std::this_thread::sleep_for(std::chrono::milliseconds(10));
 assert(worker.value());worker.configure(nullptr,0,0,0);assert(!worker.value());worker.stop();
}
