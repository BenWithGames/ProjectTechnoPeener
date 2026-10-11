#include "../src/NativeRollbackWindow.hpp"
#include <cassert>
#include "../src/NativeHistoryRestore.hpp"
#include "../src/CorrectedHistoryReplay.hpp"
#include "../src/NativeCorrectedReplayBackend.hpp"
#include "../src/ConfirmedReplayWindow.hpp"
#include "../src/NativeSchedulerStorage.hpp"
#include "../src/NativeBattleBoundaryReader.hpp"
#include "../src/NativeCombatSession.hpp"
#include "../src/CombatStartPacket.hpp"
#include "../src/NativeCombatArming.hpp"
#include "../src/NativeArmingDispatch.hpp"
#include "../src/RngTransition.hpp"
#include "../src/NativeWindowReuseProbe.hpp"
#include <vector>
#include <iostream>
#include <thread>
using namespace sc6::research;
struct Backend {
    std::vector<unsigned> calls;unsigned rejectPrepare{99},rejectRestore{99};
    bool prepare(unsigned i){calls.push_back(i);return i!=rejectPrepare;}
    bool restore(unsigned i){calls.push_back(10+i);return i!=rejectRestore;}
};
struct HistoryFixture {
    std::array<std::unique_ptr<NativeBoundarySnapshot>,4> states;
    bool owned{true};
    HistoryFixture(){for(auto& s:states){s=std::make_unique<NativeBoundarySnapshot>(NativeBoundarySnapshot::Mode::idle);s->module=1;}}
    bool validate_owned(const RollbackBoundaryKey&){return owned;}
    template<class F> bool visit(const RollbackBoundaryKey& k,F f){return k.frame>=100 && k.frame<=103 && f(*states[k.frame-100]);}
};
struct RestoreFixture {
    inline static std::vector<unsigned> calls;
    inline static unsigned prepareCount{},loadCount{},reject{99},rejectLoad{99}; unsigned index{};
    bool prepare_captured(NativeBoundarySnapshot&,NativeBoundarySnapshot&,bool,bool){index=prepareCount;calls.push_back(prepareCount);return prepareCount++!=reject;}
    bool restore_captured(NativeBoundarySnapshot&,uintptr_t,uintptr_t){++loadCount;calls.push_back(10+index);return index!=rejectLoad;}
};
struct CorrectionFixture {
    std::vector<unsigned> calls;unsigned frame{103},value{99},failFrame{999};
    bool preflight(const RollbackBoundaryKey&,const RollbackBoundaryKey&){return true;}
    bool run(CorrectionFixture&,const RollbackBoundaryKey& k,const RollbackBoundaryKey&,uintptr_t){calls.push_back(1);frame=k.frame;value=0;return true;}
    template<class F> bool rewind_for_replay(const RollbackBoundaryKey&,F f){calls.push_back(2);return f(*this);}
    bool capture(CorrectionFixture&,const RollbackBoundaryKey& k,const std::array<std::byte,40>&){calls.push_back(3);return frame==k.frame;}
    bool update(const RollbackBoundaryKey& k,const std::array<std::byte,40>& bytes){
        calls.push_back(4);if(k.frame==failFrame)return false;
        sc6::NetworkInputComposer::Envelope words{};std::memcpy(words.data(),bytes.data(),40);
        assert(words[0]==17 && words[1]==18 && words[4]==19 && words[5]==20);
        assert(words[3]==0 && words[6]==0x80000000u);
        value+=words[2];++frame;return true;
    }
    bool capture_endpoint(CorrectionFixture&,const RollbackBoundaryKey& k){calls.push_back(5);return frame==k.frame;}
};
static volatile uint32_t* nativeCounter;
static unsigned nativeIncrement=1;
static void nativeUpdate(uintptr_t argument){
    const auto p=reinterpret_cast<uintptr_t*>(argument);
    assert(*reinterpret_cast<unsigned*>(p[0])==17);
    *nativeCounter+=nativeIncrement;
}
struct NativeCaptureFixture {
    unsigned captures{};bool reject{};
    bool capture_owned(const RollbackBoundaryKey& key,unsigned oldest,uintptr_t,uintptr_t argument,ReplayDependencySource&,bool){
        assert(key.frame==*nativeCounter && oldest==100 && argument);++captures;return !reject;
    }
    bool validate_owned(const RollbackBoundaryKey&){return true;}
};
struct StorageHistory {
    std::vector<unsigned> frames;
    bool capture_owned(const RollbackBoundaryKey& k,unsigned,uintptr_t,uintptr_t,ReplayDependencySource&,bool){frames.push_back(k.frame);return true;}
    template<class F> bool rewind_for_replay(const RollbackBoundaryKey& k,F f){
        if(!f(*this))return false;while(!frames.empty() && frames.back()>=k.frame)frames.pop_back();return true;
    }
};
struct StorageRestore {
    bool run(StorageHistory& h,const RollbackBoundaryKey& first,const RollbackBoundaryKey& head,uintptr_t){
        return first.frame==100 && head.frame==103 && h.frames==std::vector<unsigned>{100,101,102,103};
    }
};
struct BoundaryAccessFixture {
    inline static unsigned reads{};inline static bool changed{},invalid{};
    static sc6::round_event::Snapshot round(){return {4,5,3};}
    static bool counter(uintptr_t,unsigned& value){value=100+(changed?reads++:0);return true;}
    static bool input(uintptr_t,std::array<std::byte,40>& bytes){bytes[0]=std::byte{17};return !invalid;}
};
struct ConfigurationAccessFixture {
    inline static unsigned frame=100,mask=1,battle=500;
    inline static uint64_t generation=3;
    static bool clock(uintptr_t,uintptr_t,NativeClockAccess<>::State& out){out.identity=7;out.frame=frame;out.local_mask=mask;return true;}
    static auto round(){return sc6::round_event::Snapshot{4,5,generation};}
    static bool counter(uintptr_t,unsigned& out){out=battle;return true;}
};
int main(){
    {
        NativeCombatSession::Config config;ReplayDependencySource source;
        auto shared=std::make_unique<sc6::observation::Shared>();
        NativeArmingDispatch dispatch(config,source,*shared,22,GetTickCount64()+1000);
        assert(!dispatch.queue() && !native_clock_hook::preparation_poll.load());
        alignas(8) std::array<std::byte,0x400> object{};
        const auto address=reinterpret_cast<uintptr_t>(object.data());
        *reinterpret_cast<unsigned*>(address+0x3A0)=7;*reinterpret_cast<unsigned*>(address+0x3A4)=99;
        config.base=1;config.clock_object=address;config.clock_identity=7;config.clock_frame=100;
        config.identity={1,2,3,4,5,500,1,GetCurrentThreadId()};
        config.image_verified=config.bilateral_private=config.boundaries_owned=config.verified_void_entry=true;
        config.caller=2;config.original=nativeUpdate;shared->reserved=sc6::observation::owned_mode;
        config.offer={1,2,9,10,4,5,500,1,0,3,sc6::observation::capacity};
        assert(NativeCombatSession::valid_config(config));
        auto invalid=config;invalid.offer.first_frame++;assert(!NativeCombatSession::valid_config(invalid));
        invalid=config;invalid.offer.slot=2;assert(!NativeCombatSession::valid_config(invalid));
        invalid=config;invalid.offer.delay=4;assert(!NativeCombatSession::valid_config(invalid));
        invalid=config;invalid.offer.records=120;assert(!NativeCombatSession::valid_config(invalid));
        invalid=config;invalid.offer.recipient=3;assert(!NativeCombatSession::valid_config(invalid));
        using Configuration=NativeCombatConfiguration<ConfigurationAccessFixture>;
        NativeClockDiscovery::Observation observed;
        observed.valid=true;observed.base=1;observed.object=address;observed.thread=GetCurrentThreadId();observed.tick=1000;
        observed.clock.identity=7;observed.clock.frame=100;observed.clock.local_mask=1;
        NativeCombatSession::Config assembled;
        assert(Configuration::assemble(observed,1000,config,assembled)==Configuration::Result::ready);
        assert(assembled.clock_object==address && assembled.clock_frame==100 && assembled.offer==config.offer);
        assert(Configuration::assemble(observed,1251,config,assembled)==Configuration::Result::stale && !assembled.base);
        ++observed.thread;assert(Configuration::assemble(observed,1000,config,assembled)==Configuration::Result::thread);--observed.thread;
        ConfigurationAccessFixture::frame=101;
        assert(Configuration::assemble(observed,1000,config,assembled)==Configuration::Result::clock);
        ConfigurationAccessFixture::frame=100;ConfigurationAccessFixture::generation=4;
        assert(Configuration::assemble(observed,1000,config,assembled)==Configuration::Result::round);
        ConfigurationAccessFixture::generation=3;ConfigurationAccessFixture::battle=501;
        assert(Configuration::assemble(observed,1000,config,assembled)==Configuration::Result::battle);
        ConfigurationAccessFixture::battle=500;observed.clock.local_mask=ConfigurationAccessFixture::mask=2;
        assert(Configuration::assemble(observed,1000,config,assembled)==Configuration::Result::offer);
        auto p2=config;p2.identity.slot=p2.offer.slot=2;
        assert(Configuration::assemble(observed,1000,p2,assembled)==Configuration::Result::ready);
        p2.bilateral_private=false;
        assert(Configuration::assemble(observed,1000,p2,assembled)==Configuration::Result::configuration && !assembled.base);
        native_clock_hook::installed=true;native_clock_hook::installed_base=1;native_clock_hook::original=[](uintptr_t){};
        NativeArmingDispatch waiting(config,source,*shared,22,GetTickCount64()+1000);
        assert(waiting.queue());assert(!waiting.queue());
        native_clock_hook::bridge(address);assert(waiting.state()==NativeArmingDispatch::State::waiting);
        *reinterpret_cast<unsigned*>(address+0x3A4)=101;
        native_clock_hook::bridge(address);assert(waiting.state()==NativeArmingDispatch::State::rejected);
        assert(!native_clock_hook::start_hold.load() && !combat_routes.load());
        native_clock_hook::preparation_poll=nullptr;native_clock_hook::installed=false;
        native_clock_hook::installed_base=0;native_clock_hook::original=nullptr;
    }
    {
        static unsigned order=0;
        native_clock_hook::StartupPoll startup{&order,[](void* p,uintptr_t object)noexcept{assert(object==77);assert(*static_cast<unsigned*>(p)==0);*static_cast<unsigned*>(p)=1;}};
        native_clock_hook::original=[](uintptr_t object){assert(object==77 && order==1);order=2;};
        native_clock_hook::startup_poll=&startup;native_clock_hook::bridge(77);
        assert(order==2);native_clock_hook::startup_poll=nullptr;native_clock_hook::original=nullptr;
    }
    {
        auto arming=std::make_unique<NativeCombatArming>();NativeCombatSession::Config config;ReplayDependencySource source;
        auto shared=std::make_unique<sc6::observation::Shared>();
        assert(!arming->prepare(config,source,*shared,22,false));
        assert(arming->phase()==NativeCombatArming::Phase::idle && !arming->requires_process_lifetime());
        assert(!arming->prepare(config,source,*shared,22,true));
        assert(arming->phase()==NativeCombatArming::Phase::restart_required && !arming->requires_process_lifetime());
        assert(!arming->poll());assert(!native_clock_hook::start_hold.load() && !combat_start_exchange.load());
    }
    {
        CombatStartAgreement::Offer a{1,2,100,77,3,4,500,1,0,3,7200};auto b=a;
        b.sender=2;b.recipient=1;b.ticket=200;b.slot=2;
        CombatStartExchange left,right;CombatStartMailbox mailbox(GetCurrentThreadId());
        assert(left.attach(mailbox));assert(left.begin(22,a) && right.begin(11,b));
        CombatStartAgreement transferred;assert(!mailbox.take(transferred));
        auto first=left.outgoing();assert(first[0]==0x43 && first[1]==0x36);
        assert(!right.receive(99,true,first,first.size()));assert(!right.receive(11,false,first,first.size()));
        assert(!right.receive(11,true,first,79));assert(!right.consume());
        assert(right.receive(11,true,first,80));auto ack=right.outgoing();
        std::thread network([&]{assert(left.receive(22,true,ack,80));assert(!mailbox.take(transferred));});network.join();
        assert(!mailbox.take(transferred)); // acknowledgement alone cannot activate
        auto reply=left.outgoing();
        assert(right.receive(11,true,reply,80));
        assert(left.report_local_hold(a.ticket));assert(!left.report_local_hold(a.ticket));
        auto held=left.outgoing();assert(right.receive(11,true,held,80));
        assert(!mailbox.take(transferred)); // remote side has not reached its hold
        assert(right.report_local_hold(b.ticket));held=right.outgoing();
        std::thread held_network([&]{assert(left.receive(22,true,held,80));});held_network.join();
        assert(mailbox.take(transferred) && transferred.activation_ready() && transferred.consume(a));
        assert(!mailbox.take(transferred));assert(left.consume() && right.consume());
        assert(!left.consume());
        for(unsigned offset:{0u,4u,8u,56u,64u}){auto corrupt=first;corrupt[offset]=255;assert(!CombatStartPacket::decode(corrupt,80));}
        auto roundtrip=CombatStartPacket::decode(first,80);assert(roundtrip && roundtrip->offer==a);
    }
    {
        CombatStartAgreement::Offer local{1,2,100,77,3,4,500,1,0,3,7200};
        auto peer=local;peer.sender=2;peer.recipient=1;peer.ticket=200;peer.slot=2;
        CombatStartAgreement agreed;assert(agreed.begin(local));assert(!agreed.consume(local));
        assert(agreed.receive(peer) && agreed.receive(peer));assert(!agreed.consume(local));
        assert(agreed.acknowledge(2,100,200));assert(agreed.consume(local));assert(!agreed.consume(local));
        for(unsigned field=0;field<5;++field){CombatStartAgreement mismatch;assert(mismatch.begin(local));auto bad=peer;
            switch(field){case 0:++bad.build;break;case 1:++bad.first_frame;break;case 2:++bad.delay;break;case 3:bad.slot=1;break;case 4:++bad.round;break;}
            assert(!mismatch.receive(bad));assert(!mismatch.receive(peer));}
        CombatStartAgreement stale;assert(stale.begin(local) && stale.receive(peer));
        assert(!stale.acknowledge(2,99,200) && !stale.consume(local));
    }
    {
        NativeCombatSession::Config config;ReplayDependencySource source;
        auto session=std::make_unique<NativeCombatSession>(config,source);
        auto shared=std::make_unique<sc6::observation::Shared>();
        assert(!session->prepare(*shared) && !session->ready());
        CombatStartAgreement agreement;assert(!session->activate(agreement) && !combat_routes.load());
        assert(!session->prepare(*shared));
        assert(!exclusive_battle_route.load() && !sc6::scheduler_input_sink.load());
    }
    {
        CombatRouteBundle empty;assert(!publish_combat_routes(empty) && !combat_routes.load());
        auto shared=std::make_unique<sc6::observation::Shared>();
        shared->signature=sc6::observation::magic;shared->schema=sc6::observation::version;
        shared->nonce=1;shared->frequency=10000000;shared->status=1;
        OwnedInputPublication publication;assert(publication.bind(*shared,1,2,3,100));
        for(unsigned i=0;i<sc6::observation::limit;++i)assert(publication.publish(1,2,100+i,{}));
        assert(publication.complete() && shared->status==2 && shared->count==sc6::observation::limit);
        assert(!publication.publish(1,2,100+sc6::observation::limit,{}) && shared->count==sc6::observation::limit);
    }
    {
        struct Boundary {uint64_t session{},round{};unsigned frame{};CombatFrameInput local;};
        RollbackBoundaryKey identity{1,2,3,4,5,100,1,GetCurrentThreadId()};Boundary result;
        NativeBattleBoundaryReader<BoundaryAccessFixture> reader;
        assert(reader.bind(1,2,identity,true));assert(reader.read(10,2,result));
        assert(reader.frontier(100) && !reader.frontier(101));
        assert(result.session==1 && result.round==4 && result.frame==100 && result.local.envelope[0]==17);
        assert(!reader.read(10,3,result) && result.session==0 && reader.failed());
        assert(!reader.read(10,2,result));
        NativeBattleBoundaryReader<BoundaryAccessFixture> torn;assert(torn.bind(1,2,identity,true));
        BoundaryAccessFixture::changed=true;assert(!torn.read(10,2,result));BoundaryAccessFixture::changed=false;
        auto wrong=identity;++wrong.generation;NativeBattleBoundaryReader<BoundaryAccessFixture> stale;
        assert(stale.bind(1,2,wrong,true));assert(!stale.read(10,2,result));
        NativeBattleBoundaryReader<> native;assert(!native.read(0,0,result));
    }
    {
        StorageHistory history;ReplayDependencySource source;
        RollbackBoundaryKey identity{1,2,3,4,5,100,1,GetCurrentThreadId()};
        NativeSchedulerStorage<StorageHistory,StorageRestore> storage(history,source,1,identity);
        decltype(storage)::Snapshot first,current;std::array<std::byte,40> bytes{};
        for(unsigned i=100;i<103;++i){assert(storage.begin_boundary(i,{}));assert(storage.capture(current,i,bytes));if(i==100)first=current;}
        assert(storage.begin_boundary(103,{}));assert(storage.restore(first));assert(history.frames.empty());
        for(unsigned i=100;i<=103;++i)assert(storage.capture(current,i,bytes));
        assert((history.frames==std::vector<unsigned>{100,101,102,103}));
        assert(!storage.restore(first) && storage.failed()); // replaced handle cannot alias recaptured state
        auto actualHistory=std::make_unique<NativeRollingHistory<4>>();
        NativeSchedulerStorage<> native(*actualHistory,source,0,identity);
        assert(!native.begin_boundary(100,{}));
        decltype(native)::Snapshot invalid;assert(!native.restore(invalid));
        assert(!native.capture(invalid,100,bytes));
    }
    {
        sc6::NetworkGameHandoff handoff;assert(handoff.bind(1,2,1));
        RollbackBoundaryKey target{1,2,3,4,5,100,1,GetCurrentThreadId()},head=target;head.frame=103;
        for(unsigned i=0;i<4;++i){sc6::NetworkGameHandoff::Boundary b{3,100+i,4,5,{}};
            b.input[0]=17;b.input[4]=19;
            if(i){auto input=b.input;input[2]=i;
                assert(handoff.publish({1,2,1,99+i,4,5,input}));}
            handoff.boundary(1,target.thread,b);
        }
        std::array<CorrectedHistoryReplay::Frame,3> window{};
        assert(confirmed_replay_window(handoff,target,head,window));
        for(unsigned i=0;i<3;++i)assert(window[i].confirmed && window[i].peer[2]==i+1 && window[i].local[2]==0);
        ++head.frame;assert(!confirmed_replay_window(handoff,target,head,window) && !window[0].confirmed);
    }
    {
        const auto base=reinterpret_cast<uintptr_t>(VirtualAlloc(nullptr,0x470E000,MEM_RESERVE,PAGE_READWRITE));assert(base);
        assert(VirtualAlloc(reinterpret_cast<void*>(base+0x470D000),4096,MEM_COMMIT,PAGE_READWRITE));
        nativeCounter=reinterpret_cast<volatile uint32_t*>(base+0x470D0C4);
        struct Control {uintptr_t table{};LONG strong{1},weak{1};bool ready{true};} control;
        GeneratedSnapshotDependency dependency;
        assert(dependency.acquire(1,2,reinterpret_cast<uintptr_t>(&control),40,[](uintptr_t){},[](uintptr_t){}));
        ReplayDependencySource source{&control,[](void*,uint32_t){return true;},&dependency,
            [](void* p){return static_cast<Control*>(p)->ready;}};
        RollbackBoundaryKey first{1,2,3,4,5,100,1,GetCurrentThreadId()},head=first;head.frame=103;
        std::array<std::byte,40> bytes{};bytes[0]=std::byte{17};
        NativeCaptureFixture history;
        *nativeCounter=103;
        assert(source.available());control.ready=false;assert(!source.available());
        NativeCorrectedReplayBackend cancelled(base,nativeUpdate,source,bytes);
        assert(!cancelled.preflight(first,head) && history.captures==0 && *nativeCounter==103);
        control.ready=true;
        NativeCorrectedReplayBackend backend(base,nativeUpdate,source,bytes);
        assert(backend.preflight(first,head));*nativeCounter=100;
        for(unsigned i=0;i<3;++i){auto key=first;key.frame+=i;
            assert(backend.capture(history,key,bytes));assert(backend.update(key,bytes));}
        assert(backend.capture_endpoint(history,head) && history.captures==4 && backend.updates==3);
        assert(!backend.capture_endpoint(history,head));
        NativeCorrectedReplayBackend recursive(base,reinterpret_cast<SingleUpdateCall::Routine>(base+0x2DBC60),source,bytes);
        assert(!recursive.preflight(first,head));
        NativeCorrectedReplayBackend wrongCounter(base,nativeUpdate,source,bytes);*nativeCounter=102;
        assert(!wrongCounter.preflight(first,head));
        for(unsigned increment : {0u,2u}){
            *nativeCounter=103;nativeIncrement=increment;
            NativeCorrectedReplayBackend broken(base,nativeUpdate,source,bytes);assert(broken.preflight(first,head));
            *nativeCounter=100;assert(broken.capture(history,first,bytes));
            assert(!broken.update(first,bytes) && broken.failed() && broken.updates==1);
            assert(!broken.update(first,bytes) && broken.updates==1);
        }
        nativeIncrement=1;
        *nativeCounter=103;NativeCorrectedReplayBackend captureFailure(base,nativeUpdate,source,bytes);
        assert(captureFailure.preflight(first,head));*nativeCounter=100;history.reject=true;
        assert(!captureFailure.capture(history,first,bytes));
        assert(!captureFailure.update(first,bytes) && captureFailure.updates==0 && *nativeCounter==100);
        history.reject=false;
        *nativeCounter=103;NativeCorrectedReplayBackend changedInput(base,nativeUpdate,source,bytes);
        assert(changedInput.preflight(first,head));*nativeCounter=100;
        assert(changedInput.capture(history,first,bytes));auto altered=bytes;altered[8]=std::byte{1};
        assert(!changedInput.update(first,altered) && changedInput.updates==0 && *nativeCounter==100);
        // Instantiate the entire production bridge without authorizing native reads.
        auto realHistory=std::make_unique<NativeRollingHistory<4>>();
        auto realRestore=std::make_unique<NativeHistoryRestore<>>();CorrectedHistoryReplay transaction;
        std::array<CorrectedHistoryReplay::Frame,3> empty{};
        assert(!replay_corrected_native(transaction,*realHistory,*realRestore,backend,empty,0,head,base));
        assert(dependency.reset() && control.strong==1);
        assert(VirtualFree(reinterpret_cast<void*>(base),0,MEM_RELEASE));
    }
    {
        std::array<CorrectedHistoryReplay::Frame,3> frames{};
        for(unsigned i=0;i<3;++i){auto& f=frames[i];f.key={1,2,3,4,5,100+i,1,GetCurrentThreadId()};f.confirmed=true;
            f.local={17,18,99,99,19,20,0x80000000u,0,0,0};f.peer[2]=i+1;}
        auto live=frames[0].key;live.frame=103;
        CorrectionFixture backend;CorrectedHistoryReplay replay;
        assert(replay.run(backend,backend,backend,frames,3,live,1));
        assert(backend.value==6 && backend.frame==103 && replay.completed==3 && !replay.restart_required);
        assert((backend.calls==std::vector<unsigned>{1,2,3,4,3,4,3,4,5}));
        assert(!replay.run(backend,backend,backend,frames,3,live,1));
        for(unsigned bad=0;bad<3;++bad){auto invalid=frames;
            if(bad==0)invalid[2].confirmed=false;
            if(bad==1)++invalid[2].key.generation;
            if(bad==2)invalid[2].local[9]=0x7FC00000u;
            CorrectionFixture untouched;CorrectedHistoryReplay rejected;
            assert(!rejected.run(untouched,untouched,untouched,invalid,3,live,1));
            assert(untouched.calls.empty() && !rejected.restart_required);
        }
        CorrectionFixture partial;partial.failFrame=101;CorrectedHistoryReplay stopped;
        assert(!stopped.run(partial,partial,partial,frames,3,live,1));
        assert(stopped.restart_required && stopped.completed==1 && partial.frame==101);
        assert((partial.calls==std::vector<unsigned>{1,2,3,4,3,4}));
    }
    {
        HistoryFixture history;
        RollbackBoundaryKey first{1,2,3,4,5,100,1,GetCurrentThreadId()},last=first;last.frame=103;
        NativeHistoryRestore<RestoreFixture> restore;
        assert(restore.run(history,first,last,1) && restore.loaded);
        assert((RestoreFixture::calls==std::vector<unsigned>{0,1,2,12,11,10}));
        assert(!restore.run(history,first,last,1));
        RestoreFixture::calls.clear();RestoreFixture::prepareCount=RestoreFixture::loadCount=0;RestoreFixture::reject=2;
        NativeHistoryRestore<RestoreFixture> rejected;assert(!rejected.run(history,first,last,1));
        assert(RestoreFixture::loadCount==0);
        RestoreFixture::calls.clear();RestoreFixture::prepareCount=RestoreFixture::loadCount=0;RestoreFixture::reject=99;RestoreFixture::rejectLoad=1;
        NativeHistoryRestore<RestoreFixture> partial;assert(!partial.run(history,first,last,1));
        assert((RestoreFixture::calls==std::vector<unsigned>{0,1,2,12,11}) && !partial.loaded);
        history.owned=false;NativeHistoryRestore<RestoreFixture> stale;
        assert(!stale.run(history,first,last,1) && stale.stage==decltype(stale)::Stage::ownership);
        NativeHistoryRestore<> nativeRestore;assert(!nativeRestore.run(history,first,last,1));
    }
    assert(NativeWindowReuseProbe::topology_retry_allowed(2,2,false));
    assert(!NativeWindowReuseProbe::topology_retry_allowed(3,2,false));
    assert(!NativeWindowReuseProbe::topology_retry_allowed(0,0,false));
    assert(!NativeWindowReuseProbe::topology_retry_allowed(0,2,true));
    ReplayOwnedResources emptyResources;emptyResources.on_resource_clear();emptyResources.on_resource_clear();assert(!emptyResources.cancelled() && emptyResources.cancellation().reason==0);
    OwnershipCancellation cancellation;cancellation.note(7,123,456,false);cancellation.note(9,9,9,true);assert(cancellation.reason==7 && cancellation.thread==123 && cancellation.expected==456 && cancellation.had_owned==0);
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
    assert(nd.word_offset==8 && nd.expected_word==0 && nd.actual_word==5888 && nd.changed_bytes==1);
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

