#include "../src/RawPredictionRoute.hpp"
#include <cassert>
#include <iostream>
#include <thread>
#include <sstream>
using Route=sc6::research::RawPredictionRoute<>;
using Packet=sc6::research::RawInputConfirmation;
static Packet packet(uint64_t frame,uint32_t raw){return {22,11,3,4,100+frame,2,raw};}
static void bind(Route& r){assert(r.bind(11,22,3,4,100,1,99,{0,0}));}
int main(){
    { // Dedicated receive and fallback polling can race; publication stays serialized.
        auto storage=std::make_unique<Route>();auto& q=*storage;bind(q);std::atomic<unsigned> done{};
        auto produce=[&]{for(unsigned n=0;n<64;++n)while(q.submit(packet(n,n))==Route::Submit::full)std::this_thread::yield();++done;};
        std::thread first(produce),second(produce);
        while(done.load()!=2){assert(q.drain(99));std::this_thread::yield();}
        first.join();second.join();assert(q.drain(99));
        for(unsigned n=0;n<64;++n){uint32_t raw{};assert(q.truth(99,n,raw)&&raw==n);}
        assert(q.coalesced()==64);
    }

    { // Retransmit bursts cannot crowd fresh truth out of the bounded queue.
        auto storage=std::make_unique<Route>();auto& q=*storage;bind(q);
        for(unsigned repeat=0;repeat<100;++repeat)
            for(unsigned n=0;n<16;++n)assert(q.submit(packet(n,n))==Route::Submit::accepted);
        assert(q.coalesced()==1584 && q.queue_full()==0);
        for(unsigned n=16;n<32;++n)assert(q.submit(packet(n,n))==Route::Submit::accepted);
        assert(q.submit(packet(32,32))==Route::Submit::full);
        assert(q.drain(99));
        // Full rejection never poisons the dedup archive; retry still delivers.
        assert(q.submit(packet(32,32))==Route::Submit::accepted && q.drain(99));
        uint32_t raw{};assert(q.truth(99,32,raw)&&raw==32);
        assert(q.submit(packet(0,999))==Route::Submit::accepted && !q.drain(99));
        assert(q.failure()==Route::Failure::conflict);
    }

    {
        auto baseline_storage=std::make_unique<Route>();auto& baseline=*baseline_storage;bind(baseline);
        assert(baseline.append(99,1,{},{},false,2));
        assert(baseline.delivery().frame(0)->confirmed);
        assert(baseline.delivery().frame(0)->raw[1]==2);
        assert(baseline.submit(packet(0,2))==Route::Submit::accepted && baseline.drain(99));
        assert(baseline.counters().corrections==0 && baseline.counters().duplicates==1);
        assert(baseline.submit(packet(0,3))==Route::Submit::accepted && !baseline.drain(99));
        assert(baseline.failure()==Route::Failure::conflict);
        auto conflict_storage=std::make_unique<Route>();auto& conflict=*conflict_storage;bind(conflict);
        assert(conflict.submit(packet(0,3))==Route::Submit::accepted);
        assert(!conflict.append(99,1,{},{},false,2) && conflict.delivery().head()==0);
    }

    auto r_storage=std::make_unique<Route>();auto& r=*r_storage;bind(r);
    auto p=packet(0,2);auto bad=p;bad.sender=23;
    assert(r.submit(bad)==Route::Submit::rejected);
    bad=p;bad.event=5;assert(r.submit(bad)==Route::Submit::rejected);
    bad=p;bad.slot=1;assert(r.submit(bad)==Route::Submit::rejected);
    std::array<uint32_t,10> aux{};aux[4]=17;
    assert(r.append(99,1,{},aux));
    assert(r.submit(packet(1,2))==Route::Submit::accepted);
    assert(r.append(99,1,{},aux));
    assert(r.delivery().frame(1)->envelope[3]==2);
    assert(r.submit(p)==Route::Submit::accepted);
    assert(r.reconcile(99,[](const auto& d){
        assert(d.frame(0)->envelope[3]==2);
        assert(d.frame(1)->envelope[3]==0);
        assert(d.frame(1)->envelope[4]==17);return true;
    }));
    unsigned published=0;
    assert(r.publish_confirmed(99,[&](auto session,auto round,auto event,auto frame,const auto& raw){
        assert(session==11 && round==3 && event==4 && frame==100+published);
        assert(raw[0]==1 && raw[1]==2);++published;return true;
    }));
    uint64_t enqueued{},drained{};assert(r.arrival_times(99,0,enqueued,drained) && enqueued && drained>=enqueued);
    const auto first_enqueued=enqueued,first_drained=drained;
    assert(!r.arrival_times(98,0,enqueued,drained) && !r.arrival_times(99,7200,enqueued,drained));
    assert(published==2 && r.request_handoff(99) && r.handoff_ready());
    assert(r.submit(p)==Route::Submit::accepted && r.handoff_ready());
    assert(r.drain(99) && r.handoff_ready() && r.coalesced()==1);
    assert(r.arrival_times(99,0,enqueued,drained) && enqueued==first_enqueued && drained==first_drained);
    assert(!r.append(99,0,{},{}));
    p.raw=3;assert(r.submit(p)==Route::Submit::accepted && !r.drain(99));
    assert(r.failure()==Route::Failure::conflict && !r.handoff_ready());
    auto neutral_storage=std::make_unique<Route>();auto& neutral=*neutral_storage;bind(neutral);
    for(unsigned n=0;n<3;++n)assert(neutral.append(99,0,{},{}));
    assert(!neutral.append(99,0,{},{}));
    for(unsigned n=0;n<3;++n)assert(neutral.submit(packet(n,0))==Route::Submit::accepted);
    assert(neutral.reconcile(99,[](const auto& d){
        for(auto n=d.first();n<d.head();++n)assert(d.frame(n)->confirmed);return true;
    }));
    assert(neutral.publish_confirmed(99,[](auto,auto,auto,auto,const auto&){return true;}));
    assert(neutral.append(99,0,{},{}));
    auto partial_storage=std::make_unique<Route>();auto& partial=*partial_storage;bind(partial);assert(partial.append(99,0,{},{}));
    assert(partial.submit(packet(0,1))==Route::Submit::accepted);
    assert(!partial.reconcile(99,[](const auto&){return false;}));
    assert(partial.failed() && !partial.handoff_ready());
    auto future_storage=std::make_unique<Route>();auto& future=*future_storage;bind(future);assert(future.submit(packet(1,0))==Route::Submit::accepted);
    assert(future.drain(99) && future.request_handoff(99) && !future.handoff_ready());
    // Producer/consumer exercise across every ring reuse and archived neutral
    // duplicate; raw words never originate from filtered envelopes.
    auto stream_storage=std::make_unique<Route>();auto& stream=*stream_storage;bind(stream);
    std::thread producer([&]{for(unsigned n=0;n<7200;++n){
        while(stream.submit(packet(n,n%7))==Route::Submit::full)std::this_thread::yield();
    }});
    for(unsigned n=0;n<7200;++n){
        while(!stream.append(99,n%11,{},{})){assert(!stream.failed());assert(stream.reconcile(99,[](const auto&){return true;}));
            assert(stream.publish_confirmed(99,[](auto,auto,auto,auto,const auto&){return true;}));}
        assert(stream.reconcile(99,[](const auto&){return true;}));
        assert(stream.publish_confirmed(99,[](auto,auto,auto,auto,const auto&){return true;}));
    }
    producer.join();assert(stream.reconcile(99,[](const auto&){return true;}));
    assert(stream.publish_confirmed(99,[](auto,auto,auto,auto,const auto&){return true;}));
    assert(stream.request_handoff(99) && stream.handoff_ready());
    assert(stream.counters().published==7200);
    assert(stream.submit(packet(0,0))==Route::Submit::accepted && stream.drain(99));
    std::ostringstream report;assert(stream.report(99,report));
    assert(report.str().find("raw_prediction_published=7200")!=std::string::npos);
    assert(!stream.report(98,report));
    auto exception_storage=std::make_unique<Route>();auto& exception=*exception_storage;bind(exception);assert(exception.append(99,0,{},{}));
    assert(!exception.reconcile(99,[](const auto&)->bool{throw 1;}));
    assert(exception.failure()==Route::Failure::apply);
    auto publish_failure_storage=std::make_unique<Route>();auto& publish_failure=*publish_failure_storage;bind(publish_failure);
    assert(publish_failure.submit(packet(0,0))==Route::Submit::accepted);
    assert(publish_failure.append(99,0,{},{}));
    assert(!publish_failure.publish_confirmed(99,[](auto,auto,auto,auto,const auto&){return false;}));
    assert(publish_failure.failure()==Route::Failure::publish);
    auto wrong_owner_storage=std::make_unique<Route>();auto& wrong_owner=*wrong_owner_storage;bind(wrong_owner);assert(!wrong_owner.drain(98));
    assert(wrong_owner.failure()==Route::Failure::owner);
    std::cout<<"Raw prediction route tests passed\n";
}
