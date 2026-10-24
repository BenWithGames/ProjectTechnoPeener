#include "../src/RawPredictionCoordinator.hpp"
#include "../src/RawPredictionTransport.hpp"
#include "../src/BoundedRollbackSession.hpp"
#include "../src/NativeConfirmedInputRecorder.hpp"
#include "../src/NativeBattleBoundaryReader.hpp"
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
static void integration(unsigned local_slot,unsigned input_delay=0){
    const auto owner=GetCurrentThreadId();RawPredictionRoute<> route;Session session;
    assert(route.bind(11,22,3,4,100,local_slot,owner,{},nullptr,input_delay));assert(session.begin(11,3));
    RawPredictionCoordinator<Session> coordinator(route,session);Adapter adapter{route.delivery()};
    assert(coordinator.bind(11,3,owner,3-local_slot));assert(coordinator.seed(owner,{3,2}));
    uint64_t expected{},publications{};std::array<uint32_t,2> previous{3,2};
    auto sampled=[](unsigned n,unsigned side){return (n/(side+2))%4;};
    auto raw=[&](unsigned n,unsigned side){return n<input_delay?0u:sampled(n-input_delay,side);};
    const unsigned frames=7200-input_delay;
    auto receive=[&](unsigned n){
        RawInputConfirmation p{22,11,3,4,100+n,3-local_slot,raw(n,2-local_slot)};
        Wire packet;assert(RawPredictionPacket::encode(p,packet));RawInputConfirmation decoded;
        assert(RawPredictionPacket::decode(packet,decoded));assert(route.submit(decoded)==RawPredictionRoute<>::Submit::accepted);
    };
    auto publish=[&](auto,auto,auto,uint64_t frame,const auto& pair){
        const auto n=static_cast<unsigned>(frame-100);assert(pair[0]==raw(n,0) && pair[1]==raw(n,1));++publications;return true;
    };
    const unsigned delay=local_slot==1?2:3;
    for(unsigned n=0;n<frames;++n){
        if(n>=delay)receive(n-delay);
        assert(coordinator.drain(owner));
        if(input_delay)assert(route.publish_local(owner,n,sampled(n,local_slot-1)));
        assert(route.append(owner,raw(n,local_slot-1),{},{}));
        assert(coordinator.before_step(owner));
        assert(session.advance(adapter,{}));assert(coordinator.after_step(owner,publish));
        RawInputConfirmation out;assert(route.outgoing(n,out));assert(out.raw==raw(n,local_slot-1) && out.slot==local_slot);
        std::array<uint32_t,2> actual{raw(n,0),raw(n,1)};
        expected+=actual[0]+3*((~previous[0])&actual[0])+5*actual[1]+7*((~previous[1])&actual[1]);previous=actual;
    }
    for(unsigned n=frames-delay;n<frames;++n)receive(n);
    assert(session.request_handoff(11,3));
    assert(coordinator.settle(owner,[&]{return session.reconcile(adapter);},publish));
    assert(session.handoff_ready() && route.handoff_ready());
    assert(adapter.frame==frames && adapter.value==expected && publications==frames && session.corrections>0);
    assert(route.retire(owner));
    auto old=RawInputConfirmation{22,11,3,4,100,3-local_slot,raw(0,2-local_slot)};
    assert(route.submit(old)==RawPredictionRoute<>::Submit::retired);
    RawPredictionRoute<> next;assert(next.bind(11,22,5,6,8000,local_slot,owner,{},&route));
    assert(next.submit(old)==RawPredictionRoute<>::Submit::retired);++old.raw;
    assert(next.submit(old)==RawPredictionRoute<>::Submit::rejected);
}
static void completed_receipts(unsigned slot,bool conflict=false){
    const auto owner=GetCurrentThreadId();RawPredictionRoute<> route;Session session;
    assert(route.bind(11,22,3,4,100,slot,owner,{}));assert(session.begin(11,3));
    RawPredictionCoordinator<Session> coordinator(route,session);Adapter adapter{route.delivery()};
    assert(coordinator.bind(11,3,owner,3-slot));assert(coordinator.seed(owner,{0,0}));
    unsigned publications=0;
    auto publish=[&](auto,auto,auto,auto,const auto&){++publications;return true;};
    assert(route.append(owner,0,{},{}));assert(coordinator.before_step(owner));
    assert(session.advance(adapter,{}));const auto old_value=adapter.value;
    RawInputConfirmation packet{22,11,3,4,100,3-slot,2};
    assert(route.submit(packet)==RawPredictionRoute<>::Submit::accepted);
    if(conflict){++packet.raw;assert(route.submit(packet)==RawPredictionRoute<>::Submit::accepted);}
    const bool ok=coordinator.after_step(owner,publish);
    assert(ok!=conflict);
    if(conflict){assert(coordinator.failed());return;}
    assert(!route.pending_receipts() && coordinator.completed_drained()==1 && coordinator.completed_submitted()==1);
    assert(session.admission_evidence(11,3).dirty==0 && session.corrections==0);
    assert(adapter.value==old_value && publications==0); // Receipt is not replay/publication.
    assert(session.request_handoff(11,3) && !session.handoff_ready());
    assert(coordinator.settle(owner,[&]{return session.reconcile(adapter);},publish));
    assert(session.corrections==1 && adapter.value!=old_value && publications==1);
    assert(session.handoff_ready() && route.handoff_ready());
}
static void completed_future_truth(){
    const auto owner=GetCurrentThreadId();RawPredictionRoute<> route;Session session;
    assert(route.bind(11,22,3,4,100,1,owner,{}));assert(session.begin(11,3));
    RawPredictionCoordinator<Session> coordinator(route,session);Adapter adapter{route.delivery()};
    assert(coordinator.bind(11,3,owner,2));
    assert(route.submit({22,11,3,4,101,2,7})==RawPredictionRoute<>::Submit::accepted);
    assert(coordinator.drain(owner)); // Frame one is archived, beyond the current scheduler head.
    assert(route.append(owner,0,{},{}));assert(coordinator.before_step(owner));
    assert(session.advance(adapter,{}));
    assert(coordinator.after_step(owner,[](auto,auto,auto,auto,const auto&){return true;}));
    assert(coordinator.completed_drained()==0 && coordinator.completed_submitted()==1);
    CombatFrameInput marker;marker.envelope[2]=7;
    assert(session.receive(11,3,1,marker)==Session::Arrival::duplicate);
}
static void pre_execute_receipts(unsigned slot,bool conflict=false){
    const auto owner=GetCurrentThreadId();RawPredictionRoute<> route;Session session;
    assert(route.bind(11,22,3,4,100,slot,owner,{}));assert(session.begin(11,3));
    RawPredictionCoordinator<Session> coordinator(route,session);Adapter adapter{route.delivery()};
    assert(coordinator.bind(11,3,owner,3-slot));
    auto publish=[](auto,auto,auto,auto,const auto&){return true;};
    assert(route.append(owner,0,{},{}));assert(coordinator.before_step(owner));
    assert(coordinator.before_execute(owner)); // Empty queue changes no revision.
    assert(session.advance(adapter,{}) && coordinator.after_step(owner,publish));
    assert(route.append(owner,0,{},{}));assert(coordinator.before_step(owner));
    // Two packets arrive after filter completion: one corrects the previous
    // frame and the other confirms the imminent update before it is simulated.
    RawInputConfirmation prior{22,11,3,4,100,3-slot,2},current{22,11,3,4,101,3-slot,3};
    assert(route.submit(prior)==RawPredictionRoute<>::Submit::accepted);
    assert(route.submit(current)==RawPredictionRoute<>::Submit::accepted);
    if(conflict){++prior.raw;assert(route.submit(prior)==RawPredictionRoute<>::Submit::accepted);}
    assert(coordinator.before_execute(owner)!=conflict);
    if(conflict){assert(coordinator.failed() && session.head()==1);return;}
    assert(coordinator.pre_execute_checks()==2 && coordinator.pre_execute_drained()==2);
    assert(session.advance(adapter,{}) && coordinator.after_step(owner,publish));
    assert(session.corrections==1 && session.replayed==1 && !route.failed());
    const auto value=adapter.value;
    assert(session.reconcile(adapter) && session.corrections==1 && adapter.value==value);
    assert(!coordinator.before_execute(owner)); // Cannot refresh an executed token.
}
// Use the production boundary reader and filter-completion route: preparation
// happens INSIDE read(), not in the test before invoking the execution callback.
struct StartupAccess {
    static inline unsigned frame=100;
    static inline std::array<uint32_t,10> envelope{};
    static sc6::round_event::Snapshot round(){return {3,4,5};}
    static bool counter(uintptr_t,unsigned& out){out=frame;return true;}
    static bool input(uintptr_t,std::array<std::byte,40>& out){std::memcpy(out.data(),envelope.data(),40);return true;}
    template<class C,class S> static bool sample(const C&,unsigned,unsigned,uint32_t,S& out,uint32_t& n){
        out.local_raw=0;out.previous=0;out.disabled=0;n=frame;return true;
    }
    template<class C,class S> static bool previous(const C&,unsigned,const S&,uint32_t){return true;}
    template<class C> static bool boundary(const C&,uint32_t){return true;}
};
static void startup_callback_order(unsigned slot,unsigned delay,unsigned failure=0){
    const auto owner=GetCurrentThreadId();RawPredictionRoute<> route;Session session;
    assert(route.bind(11,22,3,4,100,slot,owner,{},nullptr,delay));assert(session.begin(11,3));
    RawPredictionCoordinator<Session> coordinator(route,session);Adapter adapter{route.delivery()};
    assert(coordinator.bind(11,3,owner,3-slot));assert(coordinator.seed(owner,{0,0}));
    NativeFilterFramePreparation<StartupAccess> filters(route);
    assert(filters.bind({0x1000,0x2000,1,owner,7,100,100},slot,true,true));
    struct Context {decltype(filters)& filters;decltype(coordinator)& coordinator;uint64_t owner;unsigned completed{};};
    Context context{filters,coordinator,owner};
    NativeFilterPreparationRoute preparation{&context,nullptr,
        [](void* p,uint32_t frame,const std::array<uint32_t,10>& input){
            auto& c=*static_cast<Context*>(p);++c.completed;
            return c.filters.complete(frame,input) && c.coordinator.before_step(c.owner);
        }};
    assert(!native_filter_preparation_route.exchange(&preparation));
    NativeBattleBoundaryReader<StartupAccess> reader;
    assert(reader.bind(1,2,{11,22,5,3,4,100,slot,owner},true));
    struct Boundary {uint64_t session{},round{};unsigned frame{};CombatFrameInput local;};
    for(unsigned n=0;n<2;++n){
        StartupAccess::frame=100+n;StartupAccess::envelope={};
        assert(route.publish_local(owner,n,0));
        uint32_t raw{};
        for(unsigned side=0;side<2;++side)
            assert(filters.filter(0x1000,side,0,false,false,false,raw)==decltype(filters)::Result::prepared);
        if(failure==2)StartupAccess::envelope[1]=1; // Invalid pressed edge must still stop execution.
        Boundary boundary;
        const bool ok=reader.read_for_execution(10,failure==1?3:2,boundary,coordinator,owner);
        assert(ok==(failure==0));
        if(failure){assert(session.head()==0 && adapter.frame==0 && coordinator.pre_execute_checks()==0);break;}
        assert(context.completed==n+1 && filters.sealed==n+1 && coordinator.pre_execute_checks()==n+1);
        assert(boundary.frame==100+n && !coordinator.failed());
        assert(session.advance(adapter,boundary.local));
        assert(coordinator.after_step(owner,[](auto,auto,auto,auto,const auto&){return true;}));
        assert(session.head()==n+1 && adapter.frame==n+1);
    }
    assert(native_filter_preparation_route.exchange(nullptr)==&preparation);
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
    for(unsigned delay=0;delay<=2;++delay)for(unsigned slot=1;slot<=2;++slot)startup_callback_order(slot,delay);
    startup_callback_order(1,0,1);startup_callback_order(2,0,2);
    pre_execute_receipts(1);pre_execute_receipts(2);pre_execute_receipts(1,true);
    completed_receipts(1);completed_receipts(2);completed_receipts(1,true);completed_future_truth();
    integration(1);integration(2);integration(1,1);integration(2,1);integration(1,2);integration(2,2);recorder();
    Wire packet;assert(RawPredictionPacket::encode({22,11,3,4,100,2,0},packet));
    RawInputConfirmation out;packet.record.data[13]=1;assert(!RawPredictionPacket::decode(packet,out));
    packet.record.data[13]=0;packet.index=2;assert(!RawPredictionPacket::decode(packet,out));
    std::cout<<"Live raw integration: both slots, zero/two-frame input delay, 7198+ frames, delayed corrections, publication and round retirement passed\n";
}

