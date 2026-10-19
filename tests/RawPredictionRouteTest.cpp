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
    Route r;bind(r);
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
    assert(published==2 && r.request_handoff(99) && r.handoff_ready());
    assert(r.submit(p)==Route::Submit::accepted && !r.handoff_ready());
    assert(r.drain(99) && r.handoff_ready() && r.counters().duplicates==1);
    assert(!r.append(99,0,{},{}));
    p.raw=3;assert(r.submit(p)==Route::Submit::accepted && !r.drain(99));
    assert(r.failure()==Route::Failure::conflict && !r.handoff_ready());
    Route neutral;bind(neutral);
    for(unsigned n=0;n<3;++n)assert(neutral.append(99,0,{},{}));
    assert(!neutral.append(99,0,{},{}));
    for(unsigned n=0;n<3;++n)assert(neutral.submit(packet(n,0))==Route::Submit::accepted);
    assert(neutral.reconcile(99,[](const auto& d){
        for(auto n=d.first();n<d.head();++n)assert(d.frame(n)->confirmed);return true;
    }));
    assert(neutral.publish_confirmed(99,[](auto,auto,auto,auto,const auto&){return true;}));
    assert(neutral.append(99,0,{},{}));
    Route partial;bind(partial);assert(partial.append(99,0,{},{}));
    assert(partial.submit(packet(0,1))==Route::Submit::accepted);
    assert(!partial.reconcile(99,[](const auto&){return false;}));
    assert(partial.failed() && !partial.handoff_ready());
    Route future;bind(future);assert(future.submit(packet(1,0))==Route::Submit::accepted);
    assert(future.drain(99) && future.request_handoff(99) && !future.handoff_ready());
    // Producer/consumer exercise across every ring reuse and archived neutral
    // duplicate; raw words never originate from filtered envelopes.
    Route stream;bind(stream);
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
    Route exception;bind(exception);assert(exception.append(99,0,{},{}));
    assert(!exception.reconcile(99,[](const auto&)->bool{throw 1;}));
    assert(exception.failure()==Route::Failure::apply);
    Route publish_failure;bind(publish_failure);
    assert(publish_failure.submit(packet(0,0))==Route::Submit::accepted);
    assert(publish_failure.append(99,0,{},{}));
    assert(!publish_failure.publish_confirmed(99,[](auto,auto,auto,auto,const auto&){return false;}));
    assert(publish_failure.failure()==Route::Failure::publish);
    Route wrong_owner;bind(wrong_owner);assert(!wrong_owner.drain(98));
    assert(wrong_owner.failure()==Route::Failure::owner);
    std::cout<<"Raw prediction route tests passed\n";
}
