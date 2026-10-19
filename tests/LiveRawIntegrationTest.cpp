#include "../src/RawPredictionCoordinator.hpp"
#include "../src/RawPredictionTransport.hpp"
#include "../src/BoundedRollbackSession.hpp"
#include "../src/NativeConfirmedInputRecorder.hpp"
#include <cassert>
#include <iostream>
using namespace sc6::research;
struct Wire {
    uint64_t magic{},nonce{},echo{};unsigned kind{},index{};
    struct Record {std::array<uint32_t,14> data{};} record;
    unsigned own_hash{},received_hash{};
};
struct Snapshot {uint64_t frame{},value{};};
using Session=BoundedRollbackSession<CombatFrameInput,Snapshot>;
struct Adapter {
    const PredictionInputDelivery<>& history;uint64_t frame{},value{};
    CombatFrameInput predict(CombatFrameInput p)const{return p;}
    bool save(Snapshot& s,const CombatFrameInput&,const CombatFrameInput&){s={frame,value};return true;}
    bool load(const Snapshot& s){frame=s.frame;value=s.value;return true;}
    bool step(const CombatFrameInput&,const CombatFrameInput&){
        const auto* f=history.frame(frame);if(!f || !f->complete)return false;
        value+=f->envelope[0]+3*f->envelope[1]+5*f->envelope[2]+7*f->envelope[3];++frame;return true;
    }
};
static void integration(unsigned local_slot){
    const auto owner=GetCurrentThreadId();RawPredictionRoute<> route;Session session;
    assert(route.bind(11,22,3,4,100,local_slot,owner,{}));assert(session.begin(11,3));
    RawPredictionCoordinator<Session> coordinator(route,session);Adapter adapter{route.delivery()};
    assert(coordinator.bind(11,3,owner,3-local_slot));assert(coordinator.seed(owner,{3,2}));
    uint64_t expected{},publications{};std::array<uint32_t,2> previous{3,2};
    auto raw=[](unsigned n,unsigned side){return (n/(side+2))%4;};
    auto receive=[&](unsigned n){
        RawInputConfirmation p{22,11,3,4,100+n,3-local_slot,raw(n,2-local_slot)};
        Wire packet;assert(RawPredictionPacket::encode(p,packet));RawInputConfirmation decoded;
        assert(RawPredictionPacket::decode(packet,decoded));assert(route.submit(decoded)==RawPredictionRoute<>::Submit::accepted);
    };
    auto publish=[&](auto,auto,auto,uint64_t frame,const auto& pair){
        const auto n=static_cast<unsigned>(frame-100);assert(pair[0]==raw(n,0) && pair[1]==raw(n,1));++publications;return true;
    };
    const unsigned delay=local_slot==1?2:3;
    for(unsigned n=0;n<7200;++n){
        if(n>=delay)receive(n-delay);
        assert(coordinator.drain(owner));
        assert(route.append(owner,raw(n,local_slot-1),{},{}));
        assert(coordinator.before_step(owner));
        assert(session.advance(adapter,{}));assert(coordinator.after_step(owner,publish));
        RawInputConfirmation out;assert(route.outgoing(n,out));assert(out.raw==raw(n,local_slot-1) && out.slot==local_slot);
        std::array<uint32_t,2> actual{raw(n,0),raw(n,1)};
        expected+=actual[0]+3*((~previous[0])&actual[0])+5*actual[1]+7*((~previous[1])&actual[1]);previous=actual;
    }
    for(unsigned n=7200-delay;n<7200;++n)receive(n);
    assert(session.request_handoff(11,3));
    assert(coordinator.settle(owner,[&]{return session.reconcile(adapter);},publish));
    assert(session.handoff_ready() && route.handoff_ready());
    assert(adapter.frame==7200 && adapter.value==expected && publications==7200 && session.corrections>0);
    assert(route.retire(owner));
    auto old=RawInputConfirmation{22,11,3,4,100,3-local_slot,raw(0,2-local_slot)};
    assert(route.submit(old)==RawPredictionRoute<>::Submit::retired);
    RawPredictionRoute<> next;assert(next.bind(11,22,5,6,8000,local_slot,owner,{},&route));
    assert(next.submit(old)==RawPredictionRoute<>::Submit::retired);++old.raw;
    assert(next.submit(old)==RawPredictionRoute<>::Submit::rejected);
}
struct RecorderCalls {
    static inline unsigned calls{},flushes{};static inline std::array<uint32_t,2> last{};
    static void append(uintptr_t,uintptr_t,const uint32_t* raw){++calls;last={raw[0],raw[1]};}
    static void flush(uintptr_t,uintptr_t){++flushes;}
};
static void recorder(){
    alignas(8) std::array<unsigned char,0x4500> memory{};
    const uintptr_t base=0x140000000,clock=reinterpret_cast<uintptr_t>(memory.data());
    *reinterpret_cast<uintptr_t*>(clock)=base+0x3283578;
    auto word=[&](unsigned offset)->uint32_t&{return *reinterpret_cast<uint32_t*>(clock+offset);};
    word(0x3A0)=7;word(0x398)=2;memory[0x4404]=1;word(0x3B0)=100;word(0x3B4)=102;
    NativeConfirmedInputRecorder<RecorderCalls> recorder;assert(recorder.bind(base,clock,GetCurrentThreadId(),7,100));
    assert(recorder.publish(100,{1,2}) && word(0x3B0)==101 && RecorderCalls::calls==1);
    assert(recorder.publish(101,{3,4}) && word(0x3B0)==102 && RecorderCalls::flushes==1 && word(0x3B4)==UINT32_MAX);
    assert(!recorder.publish(104,{0,0}) && RecorderCalls::calls==2);
    // A stop installed after the final publication still flushes on service.
    word(0x3B0)=100;word(0x3B4)=UINT32_MAX;
    NativeConfirmedInputRecorder<RecorderCalls> ending;assert(ending.bind(base,clock,GetCurrentThreadId(),7,100));
    assert(ending.publish(100,{1,2}));word(0x3B4)=101;
    assert(ending.service() && RecorderCalls::flushes==2 && word(0x3B4)==UINT32_MAX);
    // Never bless a pair the original publisher emitted without valid tags.
    word(0x3B0)=101;
    NativeConfirmedInputRecorder<RecorderCalls> unsafe;assert(!unsafe.bind(base,clock,GetCurrentThreadId(),7,100));
}
int main(){
    integration(1);integration(2);recorder();
    Wire packet;assert(RawPredictionPacket::encode({22,11,3,4,100,2,0},packet));
    RawInputConfirmation out;packet.record.data[13]=1;assert(!RawPredictionPacket::decode(packet,out));
    packet.record.data[13]=0;packet.index=2;assert(!RawPredictionPacket::decode(packet,out));
    std::cout<<"Live raw integration: both slots, 7200 frames, delayed corrections, publication and round retirement passed\n";
}
