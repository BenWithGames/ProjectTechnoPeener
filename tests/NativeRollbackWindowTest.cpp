#include "../src/NativeRollbackWindow.hpp"
#include <cassert>
#include "../src/RngTransition.hpp"
#include "../src/NativeWindowReuseProbe.hpp"
#include <vector>
#include <iostream>
using namespace sc6::research;
struct Backend {
    std::vector<unsigned> calls;unsigned rejectPrepare{99},rejectRestore{99};
    bool prepare(unsigned i){calls.push_back(i);return i!=rejectPrepare;}
    bool restore(unsigned i){calls.push_back(10+i);return i!=rejectRestore;}
};
int main(){
    FullStateProbe::BlendBlockDiagnostic blendDiagnostic;
    std::array<std::byte,1232> expectedBlend{},actualBlend{};
    assert(blendDiagnostic.compare(expectedBlend.data(),actualBlend.data(),1232));
    actualBlend[1231]=std::byte{19};
    assert(!blendDiagnostic.compare(expectedBlend.data(),actualBlend.data(),1232));
    assert(blendDiagnostic.offset==1231 && blendDiagnostic.expected==0 && blendDiagnostic.actual==19);

    auto comparison=std::make_unique<FullStateProbe>();
    comparison->blends[1].valid=comparison->blends[4].valid=true;
    comparison->transforms[1].valid=comparison->transforms[4].valid=true;
    comparison->transforms[1].count=comparison->transforms[4].count=1;
    comparison->transforms[1].records[0].size=comparison->transforms[4].records[0].size=4;
    comparison->replay_done=comparison->replay_keys_equal=comparison->replay_ring_equal=true;
    auto pass=TwoFrameIdleReplay::comparison_mask(*comparison,0);
    assert((pass&255)==255);
    comparison->blends[4].bytes[0][0]=std::byte{1};
    comparison->transforms[4].records[0].bytes[0]=std::byte{1};
    auto comparisonChanged=TwoFrameIdleReplay::comparison_mask(*comparison,0);
    assert((pass^comparisonChanged)==(32|128)); // Report both failures independently.
    comparison->replay_ring_equal=false;
    assert((comparisonChanged^TwoFrameIdleReplay::comparison_mask(*comparison,0))==8);
    assert(!TwoFrameIdleReplay::comparison_ok(*comparison,0));

    auto& savedNative=comparison->after_save;auto& replayNative=comparison->replay;
    savedNative.sealed=replayNative.sealed=true;savedNative.writes=replayNative.writes=1;
    savedNative.lengths[0]=replayNative.lengths[0]=16;
    savedNative.stream.write_position=replayNative.stream.write_position=16;
    savedNative.traces[0].writer=replayNative.traces[0].writer=0x32AC76;
    comparison->restore_completed=true;comparison->saved.sealed=true;comparison->saved.writes=1;
    comparison->saved.lengths[0]=16;comparison->saved.traces[0].writer=0x32AC76;comparison->saved.traces[0].reader=0x32AD1B;
    replayNative.stream.payload[4]=std::byte{17};
    TwoFrameIdleReplay::NativeDifference nd;
    assert(TwoFrameIdleReplay::native_equal(*comparison,0,&nd) && nd.reason==0);
    replayNative.stream.payload[9]=std::byte{23};
    assert(!TwoFrameIdleReplay::native_equal(*comparison,0,&nd));
    assert(nd.reason==4 && nd.record==0 && nd.offset==9 && nd.actual==23 && nd.writer==0x32AC76);
    replayNative.stream.payload[9]=std::byte{};comparison->restore_completed=false;
    assert(!TwoFrameIdleReplay::native_equal(*comparison,0,&nd) && nd.offset==4);

    auto expectedRing=std::make_unique<RingSnapshot>(),actualRing=std::make_unique<RingSnapshot>();
    assert(expectedRing->first_difference(*actualRing).status==0);
    expectedRing->valid=true;assert(expectedRing->first_difference(*actualRing).validity==1);
    actualRing->valid=true;assert(expectedRing->first_difference(*actualRing).status==1);
    unsigned start=0;
    for(unsigned group=0;group<4;++group){const unsigned stride=group%2?0x800:0xC000;
        for(unsigned slot=0;slot<3;++slot){const auto at=start+(slot+1)*stride-1;
            actualRing->bytes[at]=std::byte{42};auto d=expectedRing->first_difference(*actualRing);
            assert(d.status==2 && d.group==group && d.slot==slot && d.offset==stride-1 && d.expected==0 && d.actual==42);
            actualRing->bytes[at]=std::byte{};
        }start+=3*stride;
    }
    actualRing->headers[3].index=2;auto ringDiff=expectedRing->first_difference(*actualRing);
    assert(ringDiff.status==3 && ringDiff.group==3 && ringDiff.field==8 && ringDiff.actual==2);
    actualRing->headers[3].index=0;assert(expectedRing->same(*actualRing));

    auto left=std::make_unique<TransformSnapshot>(),right=std::make_unique<TransformSnapshot>();
    left->valid=right->valid=true;left->count=right->count=2;
    left->records[0].address=right->records[0].address=0x10000;
    assert(left->topology_difference(*right).mask==0);
    for(unsigned field=0;field<6;++field){
        right->records[1]=left->records[1];auto& record=right->records[1];
        switch(field){case 0:++record.address;break;case 1:++record.side;break;case 2:++record.kind;break;
            case 3:++record.list;break;case 4:++record.parent;break;case 5:++record.size;break;}
        const auto diff=left->topology_difference(*right);assert(diff.mask==(4u<<field) && diff.index==1);
        assert(diff.after[field]==diff.before[field]+1);
    }
    right->records[1]=left->records[1];right->count=1;
    auto diff=left->topology_difference(*right);assert(diff.mask==2 && diff.index==1 && diff.before_count==2 && diff.after_count==1);
    right->count=2;right->valid=false;assert(left->topology_difference(*right).mask==1);
    right->count=UINT32_MAX;assert(left->topology_difference(*right).mask&256);
    assert(NativeWindowReuseProbe::retry_allowed(0,false,{true,false,true}));
    assert(NativeWindowReuseProbe::retry_allowed(1,false,{false,false,false}));
    assert(!NativeWindowReuseProbe::retry_allowed(2,false,{true,false,true}));
    assert(!NativeWindowReuseProbe::retry_allowed(0,true,{true,false,true}));
    assert(!NativeWindowReuseProbe::retry_allowed(0,false,{true,true,true}));
    assert(RngTransition::step(1)==16807 && RngTransition::step(16807)==282475249);
    assert(RngTransition::distance(1,1)==0 && RngTransition::distance(1,282475249)==2);
    assert(RngTransition::distance(0,0)==RngTransition::unknown && RngTransition::distance(1,2147483647)==RngTransition::unknown);
    unsigned seed=1;for(unsigned n=0;n<65;++n)seed=RngTransition::step(seed);assert(RngTransition::distance(1,seed)==RngTransition::unknown);
    std::array<RollbackBoundaryKey,4> keys{};
    for(unsigned i=0;i<4;++i)keys[i]={1,2,3,4,100,100+i,1,7};
    using R=RollbackWindowPlan::Result;
    for(unsigned depth=1;depth<=3;++depth){Backend b;
        assert(RollbackWindowPlan::execute(keys,depth+1,keys[depth],b)==R::restored);
        assert(b.calls.size()==depth*2);
        for(unsigned i=0;i<depth;++i){assert(b.calls[i]==i);assert(b.calls[depth+i]==10+depth-1-i);}
    }
    for(unsigned i=0;i<3;++i){Backend b;b.rejectPrepare=i;
        assert(RollbackWindowPlan::execute(keys,4,keys[3],b)==R::prepare_failed);
        for(auto call:b.calls)assert(call<10);
        Backend c;c.rejectRestore=i;
        assert(RollbackWindowPlan::execute(keys,4,keys[3],c)==R::restore_failed);
        assert(c.calls.back()==10+i);
    }
    for(unsigned field=0;field<8;++field){auto changed=keys;
        auto& k=changed[1];switch(field){
            case 0:++k.session;break;case 1:++k.peer;break;case 2:++k.generation;break;
            case 3:++k.round;break;case 4:++k.event;break;case 5:++k.frame;break;
            case 6:++k.slot;break;case 7:++k.thread;break;
        }
        Backend b;assert(RollbackWindowPlan::execute(changed,4,keys[3],b)==R::invalid && b.calls.empty());
    }
    Backend b;auto stale=keys[3];++stale.frame;
    assert(RollbackWindowPlan::execute(keys,4,stale,b)==R::invalid && b.calls.empty());
    assert(RollbackWindowPlan::execute(keys,5,keys[3],b)==R::invalid);
    assert(RollbackWindowPlan::execute(keys,1,keys[0],b)==R::invalid);
    // Construct the real bridge and reject invalid identity without game reads.
    auto native=std::make_unique<NativeRollbackWindow>();
    assert(!native->capture({},0,0));assert(!native->rewind(keys[3]));
    assert(!native->rewind_started());
    assert(!native->replay_reference(nullptr) && !native->recycle());
    assert(!native->discard_unmodified());
    // A late confirmation for a discarded window cannot seed its replacement.
    auto probe=std::make_unique<NativeWindowReuseProbe>();probe->captured=1;
    probe->keys[0]={10,20,1,1,80,200,1,GetCurrentThreadId()};
    sc6::NetworkGameHandoff::Boundary saved{1,100,1,80,{}};
    sc6::NetworkGameHandoff::Request request{10,20,1,100,1,80,{}};
    NativeWindowReuseProbe::validated(probe.get(),request,saved);assert(!probe->confirmed[0]);
    request.frame=saved.frame=200;NativeWindowReuseProbe::validated(probe.get(),request,saved);assert(probe->confirmed[0]);
    std::cout<<"PASS: native window identity, all-hop preflight, reverse load order, depth limits and failure stop\n";
}

