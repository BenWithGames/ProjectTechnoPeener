#include "../src/NativeHistoryRestore.hpp"
#include <cassert>
#include <iostream>
#include <vector>
using namespace sc6::research;
struct History {
    std::array<std::unique_ptr<NativeBoundarySnapshot>,4> states;
    bool owned{true};
    History(){for(auto& s:states){s=std::make_unique<NativeBoundarySnapshot>(NativeBoundarySnapshot::Mode::general_combat);s->module=1;}}
    bool validate_owned(const RollbackBoundaryKey&){return owned;}
    template<class F> bool visit(const RollbackBoundaryKey& k,F f){return k.frame>=100 && k.frame<=103 && f(*states[k.frame-100]);}
};
struct Work {
    inline static unsigned preflights{},loads{},prepares{},discards{},recycles{},reject_preflight{99};
    inline static bool admit{true},load_ok{true};
    DirectLoadCompatibility::Diagnostic direct_diagnostic{};
    NativeBoundarySnapshot::Rejection rejection{};
    bool loaded{};
    bool preflight_general(const NativeBoundarySnapshot&,const NativeBoundarySnapshot&){return preflights++!=reject_preflight;}
    bool prepare_direct(const NativeBoundarySnapshot&,const NativeBoundarySnapshot&,unsigned span){assert(span>=2 && span<=3);++prepares;if(!admit)direct_diagnostic.reason=6;return admit;}
    bool discard_unloaded_candidate(){assert(!loaded);++discards;return true;}
    bool prepare_captured(const NativeBoundarySnapshot&,const NativeBoundarySnapshot&,bool,bool,bool){return true;}
    bool restore_captured(const NativeBoundarySnapshot&,uintptr_t,uintptr_t){++loads;loaded=load_ok;return load_ok;}
    bool detached_load_reusable(){return loaded;}
    bool recycle_detached_load(){assert(loaded);loaded=false;++recycles;return true;}
    static void reset(){preflights=loads=prepares=discards=recycles=0;reject_preflight=99;admit=load_ok=true;}
};
int main(){
    for(unsigned depth:{2u,3u}){
        History h;RollbackBoundaryKey a{1,2,3,4,5,100,1,GetCurrentThreadId()},b=a;b.frame+=depth;
        Work::reset();NativeHistoryRestore<Work> direct;
        assert(direct.run(h,a,b,1) && direct.direct_used && Work::loads==1 && Work::preflights==depth);
        assert(direct.native_load_attempts==1 && direct.prepare_for_reuse() && Work::recycles==1);
        assert(direct.run(h,a,b,1) && Work::loads==2);
        Work::reset();Work::admit=false;NativeHistoryRestore<Work> fallback;
        assert(fallback.run(h,a,b,1) && !fallback.direct_used && Work::discards==1 && Work::loads==depth);
        assert(fallback.direct_guard_reason==6 && fallback.prepare_for_reuse() && Work::recycles==depth);
        Work::reset();Work::load_ok=false;NativeHistoryRestore<Work> stopped;
        assert(!stopped.run(h,a,b,1) && stopped.direct_used && Work::loads==1 && Work::discards==0 && !stopped.prepare_for_reuse());
        for(unsigned hop=0;hop<depth;++hop){Work::reset();Work::reject_preflight=hop;NativeHistoryRestore<Work> rejected;
            assert(!rejected.run(h,a,b,1) && !Work::loads && !Work::prepares);}
        Work::reset();b.round++;NativeHistoryRestore<Work> crossed;assert(!crossed.run(h,a,b,1) && !Work::loads);
    }
    auto a=std::make_unique<FullRestoreStream>(),b=std::make_unique<FullRestoreStream>();
    constexpr uintptr_t base=0x10000000;uintptr_t players[]={0x20000000,0x30000000};
    for(auto* s:{a.get(),b.get()}){s->sealed=true;s->writes=2;s->stream.write_position=2*0x3510;
        for(unsigned side=0;side<2;++side){s->lengths[side]=0x3510;s->traces[side].writer=base+0x30A9CE;s->traces[side].source=players[side]+0x90;}}
    DirectLoadCompatibility::Diagnostic d;assert(DirectLoadCompatibility::ready(*a,*b,base,players,d));
    for(unsigned side=0;side<2;++side)for(unsigned offset:{0x244u,0x248u,0x324u}){
        const auto at=side*0x3510+offset-0x90;b->stream.payload[at]=std::byte{1};
        assert(!DirectLoadCompatibility::ready(*a,*b,base,players,d) && d.reason==6 && d.side==side && d.offset==offset);
        b->stream.payload[at]=std::byte{};
    }
    b->traces[1].writer++;assert(!DirectLoadCompatibility::ready(*a,*b,base,players,d));b->traces[1].writer--;
    b->stream.write_position--;assert(!DirectLoadCompatibility::ready(*a,*b,base,players,d));b->stream.write_position++;
    auto x=std::make_unique<RingSnapshot>(),y=std::make_unique<RingSnapshot>();x->valid=y->valid=true;
    for(unsigned index=0;index<3;++index)for(unsigned span=1;span<=3;++span){
        for(unsigned i=0;i<4;++i){auto& h=x->headers[i];h.slots[0]=100;h.slots[1]=200;h.slots[2]=300;h.index=index;h.current=h.slots[index];y->headers[i]=h;y->headers[i].index=(index+2*span)%3;y->headers[i].previous=h.slots[(index+2*(span-1))%3];}
        assert(y->rotated_from(*x,span));y->headers[3].previous++;assert(!y->rotated_from(*x,span));
    }
    assert(!y->rotated_from(*x,0) && !y->rotated_from(*x,4));
    auto p=std::make_unique<FullStateProbe>();auto q=std::make_unique<NativeAdjacentRestore>();q->probe->restore_invoked=true;
    assert(!q->discard_unloaded_candidate());q->probe->restore_invoked=false;assert(q->discard_unloaded_candidate());
    std::cout<<"PASS: direct 2/3-frame routing, all-hop rejection, pre-write fallback, no fallback after load failure, reuse, native-preserved fields and ring span contracts\n";
}
