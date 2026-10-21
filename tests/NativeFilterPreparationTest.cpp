#include "../src/NativeFilterFramePreparation.hpp"
#include "../src/NativeFilterAccess.hpp"
#include "../src/RawInputHistoryView.hpp"
#include <cassert>
#include <iostream>
using namespace sc6::research;
struct Access {
    static inline uint32_t frame=100,local=1;
    static inline std::array<uint32_t,2> previous_words{};
    static inline bool valid=true,remote_available=true;
    template<class C> static bool confirmed_remote(const C&,unsigned,uint32_t,uint32_t& raw){raw=2;return remote_available;}
    template<class C,class S> static bool sample(const C&,unsigned,unsigned slot,uint32_t,S& out,uint32_t& number){
        out.local_raw=local;out.disabled=0;out.previous=previous_words[slot];number=frame;return valid;
    }
    template<class C,class S> static bool previous(const C&,unsigned slot,const S&,uint32_t raw){previous_words[slot]=raw;return valid;}
    template<class C> static bool boundary(const C&,uint32_t){return valid;}
};
int main(){
    NativeFilterFramePreparation<NativeFilterAccess>::Config invalid{};
    invalid.owner=GetCurrentThreadId();
    NativeFilterFramePreparation<NativeFilterAccess>::Sample sample{};uint32_t number{};
    assert(!NativeFilterAccess::sample(invalid,1,0,0,sample,number));
    RawPredictionRoute<> route;const auto thread=GetCurrentThreadId();
    assert(route.bind(1,2,3,4,500,1,thread,{0,0}));
    NativeFilterFramePreparation<Access> preparation(route);
    assert(preparation.bind({0x1000,0x2000,0,thread,7,100,500},1));
    RawInputHistoryView view;assert(view.bind(route.delivery(),1,3,4,500,thread));
    uint32_t raw=99;using R=decltype(preparation)::Result;
    assert(preparation.filter(0x1111,0,0,false,false,false,raw)==R::pass && raw==99);
    assert(route.submit({2,1,3,4,500,2,2})==RawPredictionRoute<>::Submit::accepted);
    // Either slot order is valid, but no full envelope is available halfway.
    assert(preparation.filter(0x1000,1,0,false,false,false,raw)==R::prepared && raw==2);
    std::array<uint32_t,10> envelope{};
    assert(!view.envelope(1,3,4,500,envelope));
    bool applied=false;
    assert(!route.reconcile(thread,[&](const auto&){applied=true;return true;}));
    assert(!applied && !route.failed());
    assert(route.submit({2,1,3,4,501,2,7})==RawPredictionRoute<>::Submit::accepted);
    assert(route.drain(thread) && route.counters().drained==1);
    // A separate clean route checks the full native capture/consumer cycle.
    RawPredictionRoute<> clean;assert(clean.bind(1,2,3,4,500,1,thread,{0,0}));
    NativeFilterFramePreparation<Access> p(clean);assert(p.bind({0x1000,0x2000,0,thread,7,100,500},1));
    RawInputHistoryView h;assert(h.bind(clean.delivery(),1,3,4,500,thread));
    assert(clean.submit({2,1,3,4,500,2,2})==RawPredictionRoute<>::Submit::accepted);
    assert(p.filter(0x1000,0,0,false,false,false,raw)==R::prepared && raw==1);
    assert(p.filter(0x1000,1,0,false,false,false,raw)==R::prepared && raw==2);
    envelope={1,1,2,2,17,0,0,0,0,0};assert(p.complete(500,envelope));
    std::array<uint32_t,10> read{};assert(h.envelope(1,3,4,500,read) && read==envelope);
    assert(clean.reconcile(thread,[](const auto&){return true;}));
    assert(clean.publish_confirmed(thread,[](auto,auto,auto,auto,const auto&){return true;}));
    ++Access::frame;Access::local=7;
    assert(p.filter(0x1000,0,0,true,true,false,raw)==R::prepared && raw==7);
    assert(Access::previous_words[0]==1);
    assert(p.filter(0x1000,1,0,false,false,true,raw)==R::prepared && raw==2);
    envelope={0,0,0,0,19,0,0,0,0,0};assert(p.complete(501,envelope));
    assert(h.envelope(1,3,4,501,read) && read==envelope);
    // Wrong native edge bit rejects the frame, rather than normalizing it.
    ++Access::frame;
    assert(p.filter(0x1000,0,0,false,false,false,raw)==R::prepared);
    assert(p.filter(0x1000,1,0,false,false,false,raw)==R::prepared);
    envelope={7,7,2,0,0,0,0,0,0,0};assert(!p.complete(502,envelope));
    assert(p.failed() && !h.envelope(1,3,4,502,read));
    RawPredictionRoute<> guarded;assert(guarded.bind(1,2,3,4,500,1,thread,{0,0}));
    NativeFilterFramePreparation<Access> guard(guarded);assert(guard.bind({0x1000,0x2000,0,thread,7,100,500},1));
    Access::frame=100;Access::valid=false;
    assert(guard.filter(0x1000,0,0,false,false,false,raw)==R::failed);
    assert(guarded.delivery().head()==0);
    Access::valid=true;Access::local=1;Access::frame=100;
    RawPredictionRoute<> baseline;assert(baseline.bind(1,2,3,4,500,1,thread,{0,0}));
    NativeFilterFramePreparation<Access> bp(baseline);assert(bp.bind({0x1000,0x2000,0,thread,7,100,500},1,false));
    assert(bp.filter(0x1000,0,0,false,false,false,raw)==R::prepared);
    assert(bp.filter(0x1000,1,0,false,false,false,raw)==R::prepared && raw==2);
    assert(baseline.delivery().frame(0)->confirmed);
    envelope={1,1,2,2,0,0,0,0,0,0};assert(bp.complete(500,envelope));
    Access::frame=101;Access::remote_available=false;
    assert(bp.filter(0x1000,0,0,false,false,false,raw)==R::failed);
    assert(baseline.delivery().head()==1);
    Access::frame=100;Access::local=1;Access::remote_available=true;Access::previous_words={};
    for(unsigned slot=1;slot<=2;++slot){
        RawPredictionRoute<> fresh;assert(fresh.bind(1,2,3,4,500,slot,thread,{0,0}));
        NativeFilterFramePreparation<Access> fp(fresh);assert(fp.bind({0x1000,0x2000,0,thread,7,100,500},slot,true,true));
        assert(fresh.publish_local(thread,0,8));
        RawInputConfirmation wire;assert(fresh.outgoing(0,wire) && wire.raw==8 && wire.frame==500 && wire.slot==slot);
        assert(fp.filter(0x1000,slot-1,0,false,false,false,raw)==R::prepared && raw==8);
        Access::local=16; // Native delayed input must not replace the committed fresh input.
        assert(fp.filter(0x1000,2-slot,0,false,false,false,raw)==R::prepared);
        assert(fresh.delivery().frame(0)->raw[slot-1]==8);
        std::array<uint32_t,10> e{};e[(slot-1)*2]=8;e[(slot-1)*2+1]=8;
        assert(fp.complete(500,e));RawInputHistoryView v;assert(v.bind(fresh.delivery(),1,3,4,500,thread));
        uint32_t replay{};assert(v.raw(1,3,4,500,slot-1,replay) && replay==8);
        assert(!v.raw(1,4,4,500,slot-1,replay));
    }
    RawPredictionRoute<> absent;assert(absent.bind(1,2,3,4,500,1,thread,{0,0}));
    NativeFilterFramePreparation<Access> ap(absent);assert(ap.bind({0x1000,0x2000,0,thread,7,100,500},1,true,true));
    assert(ap.filter(0x1000,0,0,false,false,false,raw)==R::failed && absent.delivery().head()==0);
    std::cout<<"Native filter preparation tests passed\n";
}
