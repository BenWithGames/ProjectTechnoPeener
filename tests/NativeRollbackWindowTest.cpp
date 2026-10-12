#include "../src/NativeRollbackWindow.hpp"
#include "../src/OwnedPredictionCorrection.hpp"
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
#include "../src/CombatResourcePreparation.hpp"
#include "../src/StartupBoundaryAgreement.hpp"
#include "../src/StartupSuite.hpp"
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
    inline static unsigned prepareCount{},loadCount{},reject{99},rejectLoad{99},structuralCount{}; unsigned index{};bool general_checked{};
    bool preflight_general(NativeBoundarySnapshot& a,NativeBoundarySnapshot& b){
        if(a.mode!=NativeBoundarySnapshot::Mode::general_combat || b.mode!=a.mode)return false;
        index=structuralCount++;general_checked=true;calls.push_back(100+index);return index!=reject;
    }
    bool prepare_captured(NativeBoundarySnapshot& a,NativeBoundarySnapshot& b,bool direction,bool attack,bool combat){
        if(!NativeAdjacentRestore::supported_pair(a.mode,b.mode,direction,attack,combat))return false;
        if(general_checked){calls.push_back(50+index);++prepareCount;return index!=reject;}
        index=prepareCount;calls.push_back(prepareCount);return prepareCount++!=reject;
    }
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
    inline static bool reject{};
    bool run(StorageHistory& h,const RollbackBoundaryKey& first,const RollbackBoundaryKey& head,uintptr_t){
        return !reject && first.frame==100 && head.frame==103 && h.frames==std::vector<unsigned>{100,101,102,103};
    }
    NativeRestoreDiagnostic details()const{
        NativeRestoreDiagnostic result;result.values[NativeRestoreDiagnostic::stage]=4;
        result.values[NativeRestoreDiagnostic::invoked]=1;return result;
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
struct PreparationResourcesFixture {
    std::atomic<unsigned> deferred_callbacks{},last_deferred_thread{};
    inline static bool available{},cancel{};
    inline static unsigned releases{};
    OwnershipCancellation reason;
    bool start(uintptr_t){available=cancel=false;releases=0;return true;}
    bool ready()const{return available;}
    bool cancelled()const{return cancel;}
    const OwnershipCancellation& cancellation()const{return reason;}
    bool stop(){++releases;return true;}
};
struct ColdPreparationFixture : PreparationResourcesFixture {
    inline static bool confirmed{},confirm_ok{};
    bool start(uintptr_t base,bool cold){PreparationResourcesFixture::start(base);confirmed=false;confirm_ok=true;return cold;}
    bool confirm_boundary(){if(!confirm_ok){cancel=true;return false;}confirmed=true;return true;}
    bool ready()const{return available && confirmed;}
};
struct ReferenceWindowFixture {
    unsigned capture_stage{},prepare_stage{},last_hop{},plan_result{};
    inline static bool capture_ok=true,rewind_ok=true,replay_ok=true,mutated=false;
    bool capture(const RollbackBoundaryKey&,uintptr_t,uintptr_t,NativeBoundarySnapshot::Mode,bool,ReplayDependencySource*){return capture_ok;}
    bool rewind(const RollbackBoundaryKey&){return rewind_ok;}
    bool replay_reference(SingleUpdateCall::Routine){return replay_ok;}
    bool any_restore_invoked()const{return mutated;}
    bool rewind_started()const{return mutated;}
    bool release_owned_dependencies(){return true;}
};
struct StartupHistoryFixture {
    inline static unsigned calls{},releases{};
    inline static bool capture_ok=true,validate_ok=true;
    bool capture_owned(const RollbackBoundaryKey&,unsigned,uintptr_t,uintptr_t,ReplayDependencySource&,bool){++calls;return capture_ok;}
    bool validate_owned(const RollbackBoundaryKey&){return validate_ok;}
    bool release(){++releases;return true;}
};
struct HoldRehearsalFixture {
    inline static bool valid=true,service_ok=true;
    uint64_t deadline{};
    bool bind(uintptr_t,uintptr_t,unsigned,unsigned,unsigned,uint64_t now,uint64_t timeout,bool,bool,bool){deadline=now+timeout;return valid;}
    bool ready(uintptr_t,uint64_t now){return valid && now<deadline;}
    bool run(uintptr_t,uint64_t){return service_ok;}
};
int main(){
    {
        StartupCorrectionExchange a,b;
        assert(!a.configure(UINT32_MAX,1) && !a.configure(600,0));
        assert(a.configure(600,1) && a.configure(600,1) && !a.configure(601,1));
        assert(b.configure(600,2));
        OwnedPredictionCorrection::Input zero{};
        RollbackBoundaryKey ka{1,2,3,1,4,600,1,GetCurrentThreadId()},kb{2,1,9,1,4,600,2,GetCurrentThreadId()};
        StartupCorrectionExchange::Words row{},ca{},cb{};
        assert(!a.certificate(ca));
        for(unsigned i=0;i<3;++i){ka.frame=kb.frame=600+i;
            assert(a.publish(ka,zero) && b.publish(kb,zero));
            assert(a.local(i,row) && b.receive(i,row));
            assert(b.local(i,row) && a.receive(i,row));
            a.update();b.update();assert(!a.ready && !b.ready);
        }
        // Three received records alone do not authorize release. Each peer
        // must acknowledge both immutable transcripts under the session nonce.
        assert(a.certificate(ca) && b.certificate(cb));
        assert(a.receive_certificate(cb));a.update();assert(a.ready && !b.ready);
        assert(b.receive_certificate(ca));b.update();assert(b.ready);
        OwnedPredictionCorrection::Frames frames{};assert(a.frames(frames));
        assert(frames[2].key.frame==602 && frames[2].key.session==1 && frames[2].confirmed);
        assert(a.receive_certificate(cb) && a.frames(frames));
        assert(b.local(0,row));++row[0];assert(!a.receive(0,row) && a.failed && !a.frames(frames));
        StartupCorrectionExchange bad;assert(bad.configure(600,1));
        assert(b.local(0,row));row[10]=599;assert(!bad.receive(0,row) && bad.failed);
        StartupCorrectionExchange badSlot;assert(badSlot.configure(600,1));
        assert(b.local(0,row));row[13]=1;assert(!badSlot.receive(0,row));
        StartupCorrectionExchange wrongRound;assert(wrongRound.configure(600,1));
        for(unsigned i=0;i<3;++i){ka.frame=600+i;assert(wrongRound.publish(ka,zero));
            assert(b.local(i,row));++row[11];assert(wrongRound.receive(i,row));}
        assert(!wrongRound.certificate(ca) && wrongRound.failed);
        StartupCorrectionExchange forged;assert(forged.configure(600,1));
        for(unsigned i=0;i<3;++i){ka.frame=600+i;assert(forged.publish(ka,zero));assert(b.local(i,row));assert(forged.receive(i,row));}
        ++cb[0];assert(forged.receive_certificate(cb));forged.update();assert(forged.failed && !forged.ready);
        StartupSuite correctionA,correctionB;assert(correctionA.configure(5,60) && correctionB.configure(5,60));
        assert(!correctionA.seal(127,0,0) && correctionA.seal(255,0,0) && correctionB.seal(127,128,0));
        assert(correctionA.receive(correctionB.outgoing()) && correctionB.receive(correctionA.outgoing()));
        assert(correctionA.receive(correctionB.outgoing()) && correctionA.complete() && correctionA.peer[4]==128);
    }
    {
        using C=OwnedPredictionCorrection;
        std::array<RollbackBoundaryKey,4> keys{};
        C::Frames frames{};std::array<C::Input,3> reference{},actual{},wrong{};
        for(unsigned side=1;side<=2;++side){
            for(unsigned i=0;i<4;++i)keys[i]={1,2,3,1,7,100+i,side,GetCurrentThreadId()};
            for(unsigned i=0;i<3;++i)frames[i]={keys[i],{},{},true};
            assert(C::inputs(frames,keys,reference,actual,wrong));
            for(unsigned i=0;i<3;++i){
                assert(actual[i]==reference[i] && wrong[i]!=actual[i]);
                for(unsigned word=0;word<10;++word){unsigned value{};std::memcpy(&value,wrong[i].data()+4*word,4);
                    const unsigned remote=3-side;
                    assert(value==((word==2*(remote-1) || word==2*(remote-1)+1 || word==4+remote-1)?0x400u:0u));
                }
            }
            for(unsigned i=0;i<3;++i){auto missing=frames;missing[i].confirmed=false;
                assert(!C::inputs(missing,keys,reference,actual,wrong));
                assert((actual==std::array<C::Input,3>{}) && (wrong==std::array<C::Input,3>{}));
                auto stale=frames;++stale[i].key.frame;assert(!C::inputs(stale,keys,reference,actual,wrong));
                auto foreign=frames;++foreign[i].key.peer;assert(!C::inputs(foreign,keys,reference,actual,wrong));
                auto mismatched=frames;mismatched[i].peer[2*(2-side)]=1;
                assert(!C::inputs(mismatched,keys,reference,actual,wrong));
                auto changedAux=frames;changedAux[i].local[6]=0x3F000000;
                assert(!C::inputs(changedAux,keys,reference,actual,wrong));
            }
            // Peer auxiliary data is not owned by the peer's player slot and
            // must not overwrite the local historical envelope.
            auto peerAux=frames;peerAux[0].peer[6]=0x7FC00000;
            assert(C::inputs(peerAux,keys,reference,actual,wrong));
            auto unsupported=reference;unsupported[0][0]=std::byte{1};
            auto active=frames;active[0].local[0]=1;active[0].peer[0]=1;
            assert(!C::inputs(active,keys,unsupported,actual,wrong));
        }
        auto correction=std::make_unique<C>();sc6::NetworkGameHandoff handoff;ReplayDependencySource source;
        assert(!correction->run(handoff,keys[3],1,source,nullptr));
        assert(correction->attempted && !correction->injected && !correction->restart_required && !correction->verified);
        assert(correction->release());
    }
    {
        ReplayDependencySource source;RollbackBoundaryKey key{};
        StartupReferenceReplay<ReferenceWindowFixture> success;
        for(unsigned i=0;i<4;++i)success.step(key,1,2,source,nullptr);
        assert(success.verified && success.retired && !success.restart && success.captured==4);
        ReferenceWindowFixture::rewind_ok=false;
        StartupReferenceReplay<ReferenceWindowFixture> rejected;
        for(unsigned i=0;i<4;++i)rejected.step(key,1,2,source,nullptr);
        assert(rejected.failed && rejected.retired && !rejected.restart);
        ReferenceWindowFixture::mutated=true;
        StartupReferenceReplay<ReferenceWindowFixture> partial;
        for(unsigned i=0;i<4;++i)partial.step(key,1,2,source,nullptr);
        assert(partial.failed && partial.restart && partial.restored);
        ReferenceWindowFixture::rewind_ok=true;ReferenceWindowFixture::replay_ok=false;
        StartupReferenceReplay<ReferenceWindowFixture> mismatch;
        for(unsigned i=0;i<4;++i)mismatch.step(key,1,2,source,nullptr);
        assert(mismatch.failed && mismatch.restart && !mismatch.verified);
        ReferenceWindowFixture::mutated=false;ReferenceWindowFixture::replay_ok=true;
    }
    {
        StartupSuite a,b;assert(!a.configure(6,60) && !a.configure(3,181));
        assert(a.configure(3,1) && b.configure(3,1));
        a.observe(100,1,1);a.observe(200,2,2);a.observe(300,1,3);
        assert(a.round_changes==2 && a.rematches==1 && !a.elapsed(1099) && a.elapsed(1100));
        assert(!a.seal(127,1,0) && a.seal(63,0,64) && b.seal(31,32,64));
        assert(a.receive(b.outgoing()) && !a.complete());assert(b.receive(a.outgoing()) && b.complete());
        assert(a.receive(b.outgoing()) && a.complete() && a.peer[4]==32);
        auto changed=b.outgoing();changed[7]++;assert(!a.receive(changed));
        a.observe(2000,3,4);assert(a.round_changes==2);
    }
    {
        StartupSnapshotCapture<StartupHistoryFixture> capture;ReplayDependencySource source;
        RollbackBoundaryKey key{1,2,3,1,1,600,1,GetCurrentThreadId()};
        for(unsigned i=0;i<4;++i){key.frame=600+i;assert(capture.capture(key,1,2,source));}
        assert(capture.captured==4 && capture.retired && !capture.failed && StartupHistoryFixture::releases==1);
        assert(!capture.capture(key,1,2,source));
        StartupSnapshotCapture<StartupHistoryFixture> gap;key.frame=600;assert(gap.capture(key,1,2,source));
        key.frame=602;assert(!gap.capture(key,1,2,source) && gap.failed && gap.retired);
        StartupSnapshotCapture<StartupHistoryFixture> fault;StartupHistoryFixture::capture_ok=false;
        assert(!fault.capture(key,1,2,source) && fault.captured==0 && fault.retired);
        assert(fault.release()); // Cleanup remains successful after capture failure.
        StartupHistoryFixture::capture_ok=true;
    }
    {
        using H=NativeHoldRehearsal<HoldRehearsalFixture>;
        H hold;assert(hold.request(0,601,661));assert(!hold.request(0,601,661));
        assert(!hold.run(1,2,3,0,600,659,100,true));
        assert(hold.run(1,2,3,0,601,660,110,true) && hold.state==H::holding);
        assert(!hold.run(1,9,3,0,601,660,120,true));
        assert(hold.run(1,2,3,0,601,660,209,true));
        assert(!hold.run(1,2,3,0,601,660,210,true) && hold.state==H::released && hold.services==2);
        H wrong;assert(wrong.request(0,601,661));
        assert(!wrong.run(1,2,3,0,601,659,110,true) && wrong.state==H::rejected);
        H failure;assert(failure.request(0,601,661));HoldRehearsalFixture::service_ok=false;
        assert(failure.run(1,2,3,0,601,660,110,true) && failure.state==H::restart_required);
        assert(failure.run(1,2,3,0,601,660,120,true));HoldRehearsalFixture::service_ok=true;
        H waiting;assert(waiting.request(0,604,664,2000));
        assert(waiting.run(1,2,3,0,604,663,100,true,false));
        assert(waiting.run(1,2,3,0,604,663,201,true,false) && waiting.state==H::holding);
        assert(!waiting.run(1,2,3,0,604,663,202,true,true) && waiting.state==H::released);
        H timeout;assert(timeout.request(0,604,664,2000));
        assert(timeout.run(1,2,3,0,604,663,100,true,false));
        assert(timeout.run(1,2,3,0,604,663,2100,true,false) && timeout.state==H::restart_required);
    }
    {
        StartupBoundaryPairs pairs;
        for(unsigned i=0;i<120;++i){pairs.clock(0,i+1);pairs.battle(i+546);}
        StartupBoundaryAgreement a,b;assert(a.begin(pairs,1) && b.begin(pairs,2));
        assert(a.target_clock()==601 && a.target_battle()==1146);
        assert(a.receive(b.outgoing()) && !a.ready());
        assert(b.receive(a.outgoing()) && b.ready());assert(a.receive(b.outgoing()) && a.ready());
        assert(a.receive(b.outgoing()));
        StartupBoundaryAgreement bad;assert(bad.begin(pairs,1));auto wrong=b.outgoing();++wrong[3];
        assert(!bad.receive(wrong) && bad.failed() && !bad.ready());
        StartupBoundaryPairs empty;StartupBoundaryAgreement invalid;assert(!invalid.begin(empty,1));
    }
    {
        StartupBoundaryPairs pairs;
        pairs.battle(61);assert(pairs.unpaired==1 && pairs.count==0);
        pairs.clock(0,1);pairs.clock(0,1);pairs.battle(62);
        assert(pairs.overwritten==1 && pairs.count==1 && pairs.rows[0].battle==62);
        pairs.clock(0,2);pairs.battle(63);assert(pairs.discontinuities==0);
        pairs.clock(1,1);pairs.battle(64);assert(pairs.discontinuities==1);
        for(unsigned i=0;i<200;++i){pairs.clock(1,i+2);pairs.battle(i+65);}
        assert(pairs.count==120 && pairs.discontinuities==1);
        const auto last=pairs.rows[119];pairs.clock(4,0);pairs.battle(0);
        assert(pairs.count==120 && pairs.rows[119].clock==last.clock);
    }
    {
        using R=ReplayOwnedResources;using D=R::ThreadDecision;
        R::FirstMatchOwner cold;
        assert(!cold.nominate(0,0) && !cold.matches(10));
        assert(cold.nominate(10,0) && cold.candidate()==10);
        assert(!cold.nominate(11,0) && !cold.matches(11));
        assert(cold.nominate(10,10) && cold.matches(10));
        assert(!cold.nominate(10,11));
        R::FirstMatchOwner known;
        assert(!known.nominate(10,11) && known.candidate()==0);
        assert(known.nominate(11,11));
        assert(R::thread_decision(10,0,false)==D::defer);
        assert(R::thread_decision(10,0,true)==D::reject);
        assert(R::thread_decision(10,10,false)==D::accept);
        assert(R::thread_decision(10,10,true)==D::accept);
        assert(R::thread_decision(10,11,false)==D::reject);
        assert(R::thread_decision(10,11,true)==D::reject);
    }
    {
        CombatResourcePreparation<ColdPreparationFixture> cold;
        assert(cold.start(1,true));ColdPreparationFixture::available=true;
        cold.frame(1,false);assert(!cold.ready && !ColdPreparationFixture::confirmed);
        cold.frame(2,true);assert(cold.ready && ColdPreparationFixture::confirmed);
        cold.request_stop();cold.frame(3,true);assert(cold.retired && ColdPreparationFixture::releases==1);
        CombatResourcePreparation<ColdPreparationFixture> wrong;
        assert(wrong.start(1,true));ColdPreparationFixture::available=true;ColdPreparationFixture::confirm_ok=false;
        wrong.frame(1,true);assert(wrong.failed && !wrong.ready && wrong.retired);
                CombatResourcePreparation<PreparationResourcesFixture> preparation;
        assert(preparation.start(1));preparation.frame(10,true);assert(!preparation.ready.load());
        PreparationResourcesFixture::available=true;
        preparation.frame(11,false);assert(!preparation.ready.load());
        preparation.frame(12,true);assert(preparation.ready.load() && preparation.boundary_frame==12);
        NativeClockDiscovery::Observation observed{};observed.valid=true;observed.object=123;
        observed.thread=GetCurrentThreadId();observed.tick=100;observed.clock.local_mask=1;
        observed.clock.identity=7;observed.clock.frame=12;
        preparation.clock_entry(observed,124,100);assert(!preparation.clock_ready.load());
        preparation.clock_entry(observed,123,351);assert(!preparation.clock_ready.load());
        PreparationResourcesFixture::available=false;
        preparation.clock_entry(observed,123,100);assert(!preparation.clock_ready.load());
        PreparationResourcesFixture::available=true;
        preparation.clock_entry(observed,123,100);assert(preparation.clock_ready.load());
        assert(preparation.clock_identity==7 && preparation.clock_frame==12 && preparation.clock_slot==1);
        preparation.clock_entry(observed,123,101);assert(preparation.clock_samples==2 && preparation.clock_changes==0);
        observed.clock.frame=13;observed.clock.local_cursor=14;
        preparation.clock_entry(observed,123,102);preparation.frame(14,true);
        assert(preparation.clock_changes==1 && preparation.clock_first==12 && preparation.clock_last==13);
        assert(preparation.cursor_last==14 && preparation.battle_samples==1 && preparation.battle_first==14);
        observed.clock.frame=1;observed.object=125;
        preparation.clock_entry(observed,125,103);assert(preparation.clock_resets==1 && preparation.clock_object_changes==1);
        for(unsigned i=0;i<600;++i){preparation.clock_entry(observed,125,104);preparation.frame(15,true);}
        assert(preparation.clock_samples==512 && preparation.battle_samples==512);
        preparation.request_stop();assert(PreparationResourcesFixture::releases==0);
        observed.clock.frame=13;preparation.clock_entry(observed,125,100);assert(preparation.clock_frame==1);
        preparation.frame(13,true);assert(preparation.retired.load() && !preparation.failed.load());
        preparation.frame(14,true);assert(PreparationResourcesFixture::releases==1);
        CombatResourcePreparation<PreparationResourcesFixture> cancelled;
        assert(cancelled.start(1));PreparationResourcesFixture::cancel=true;
        cancelled.frame(15,true);assert(cancelled.failed.load() && cancelled.retired.load() && !cancelled.ready.load());
    }
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
        // Regression: a valid offer must not acquire a hold for a backend
        // that can capture unrestricted combat but cannot rewind it.
        NativeCombatArming unsupported;
        assert(!unsupported.prepare(config,source,*shared,22,true));
        assert(unsupported.phase()==NativeCombatArming::Phase::idle && !unsupported.requires_process_lifetime());
        assert(!native_clock_hook::start_hold.load() && !combat_routes.load() && !combat_runtime_status.load());
        auto profile_config=config;profile_config.approved_profile=true;
        assert(!unsupported.prepare(profile_config,source,*shared,22,true));
        assert(!native_clock_hook::start_hold.load() && !combat_routes.load());
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
        native_clock_hook::preparation_poll=nullptr;
        unsigned supplied=0;
        NativeArmingDispatch deferred(config,source,*shared,22,GetTickCount64()+1000);
        auto provider=[](void* p){++*static_cast<unsigned*>(p);return ReplayDependencySource{};};
        assert(deferred.source_on_owner(&supplied,provider));
        *reinterpret_cast<unsigned*>(address+0x3A4)=99;
        assert(deferred.queue() && supplied==0);
        assert(!deferred.source_on_owner(&supplied,provider));
        native_clock_hook::bridge(address);assert(supplied==0);
        *reinterpret_cast<unsigned*>(address+0x3A4)=100;
        native_clock_hook::bridge(address);assert(supplied==1 && deferred.state()==NativeArmingDispatch::State::rejected);
        assert(!native_clock_hook::start_hold.load() && !combat_routes.load());
        native_clock_hook::preparation_poll=nullptr;
        NativeArmingDispatch cancelled(config,source,*shared,22,GetTickCount64()+1000);
        assert(cancelled.source_on_owner(&supplied,provider) && cancelled.queue());cancelled.cancel();
        native_clock_hook::bridge(address);assert(supplied==1 && cancelled.state()==NativeArmingDispatch::State::rejected);
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
        assert(arming->phase()==NativeCombatArming::Phase::idle && !arming->requires_process_lifetime());
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
        NativeCombatSession::Config config;config.identity.thread=GetCurrentThreadId();config.identity.session=7;
        ReplayDependencySource source;auto session=std::make_unique<NativeCombatSession>(config,source);
        session->status.request_stop();assert(session->status.stop_requested.load());
        session->terminate(CombatRuntimeStatus::requested);
        assert(!session->status.active.load() && session->status.restart_required.load());
        assert(session->status.history_retired.load() && session->status.reason.load()==CombatRuntimeStatus::requested);
        session->terminate(CombatRuntimeStatus::backend_failure);
        assert(session->status.reason.load()==CombatRuntimeStatus::requested); // retain first cause
        std::ostringstream events,summary;session->status.write(events,summary);
        assert(summary.str().find("continuous_restart_required=1\n")!=std::string::npos);
        assert(summary.str().find("continuous_history_retired=1\n")!=std::string::npos);
        assert(!combat_runtime_status.load() && !combat_routes.load()); // construction/termination never activates
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
        StorageHistory failedHistory;decltype(storage) failedStorage(failedHistory,source,1,identity);
        for(unsigned i=100;i<103;++i){assert(failedStorage.begin_boundary(i,{}));assert(failedStorage.capture(current,i,bytes));if(i==100)first=current;}
        assert(failedStorage.begin_boundary(103,{}));StorageRestore::reject=true;
        assert(!failedStorage.restore(first) && failedStorage.failed());StorageRestore::reject=false;
        assert(failedStorage.restore_diagnostic.values[NativeRestoreDiagnostic::stage]==4);
        assert(failedStorage.restore_diagnostic.values[NativeRestoreDiagnostic::invoked]==1);
        assert(failedHistory.frames.size()==4); // failed restore never retires/replaces the predicted suffix
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
        RestoreFixture::calls.clear();RestoreFixture::prepareCount=RestoreFixture::loadCount=RestoreFixture::structuralCount=0;RestoreFixture::reject=2;
        NativeHistoryRestore<RestoreFixture> rejected;assert(!rejected.run(history,first,last,1));
        assert(RestoreFixture::loadCount==0);
        RestoreFixture::calls.clear();RestoreFixture::prepareCount=RestoreFixture::loadCount=RestoreFixture::structuralCount=0;RestoreFixture::reject=99;RestoreFixture::rejectLoad=1;
        NativeHistoryRestore<RestoreFixture> partial;assert(!partial.run(history,first,last,1));
        assert((RestoreFixture::calls==std::vector<unsigned>{0,1,2,12,11}) && !partial.loaded);
        history.owned=false;NativeHistoryRestore<RestoreFixture> stale;
        assert(!stale.run(history,first,last,1) && stale.stage==decltype(stale)::Stage::ownership);
        NativeHistoryRestore<> nativeRestore;assert(!nativeRestore.run(history,first,last,1));
    }
    assert(NativeWindowReuseProbe::topology_retry_allowed(2,2,false));
    {
        using M=NativeBoundarySnapshot::Mode;
        for(auto mode:{M::throw_move,M::charge,M::attack,M::edge,M::reversal,M::general_combat}){
            HistoryFixture history;for(auto& state:history.states)state->mode=mode;
            RollbackBoundaryKey first{1,2,3,4,5,100,1,GetCurrentThreadId()},last=first;last.frame=103;
            RestoreFixture::calls.clear();RestoreFixture::prepareCount=RestoreFixture::loadCount=RestoreFixture::structuralCount=0;
            RestoreFixture::reject=RestoreFixture::rejectLoad=99;
            NativeHistoryRestore<RestoreFixture> restore;assert(restore.run(history,first,last,1));
            if(mode==M::general_combat)assert((RestoreFixture::calls==std::vector<unsigned>{100,101,102,52,12,51,11,50,10}));
            else assert((RestoreFixture::calls==std::vector<unsigned>{0,1,2,12,11,10}));
            history.states[2]->mode=M::combat_capture_only;
            RestoreFixture::calls.clear();RestoreFixture::prepareCount=RestoreFixture::loadCount=RestoreFixture::structuralCount=0;
            NativeHistoryRestore<RestoreFixture> unsupported;
            assert(!unsupported.run(history,first,last,1) && RestoreFixture::loadCount==0);
            assert(unsupported.source_mode==static_cast<unsigned>(mode) && unsupported.next_mode==static_cast<unsigned>(M::combat_capture_only));
            assert(!NativeAdjacentRestore::supported_pair(mode,mode,false,false,false));
            assert(!NativeAdjacentRestore::supported_pair(mode,mode,true,false,true));
            // Explicit routing does not bypass capture integrity or invoke a
            // loader merely because a mode name is recognized.
            auto native=std::make_unique<NativeAdjacentRestore>();
            history.states[0]->thread=GetCurrentThreadId();history.states[1]->thread=GetCurrentThreadId();
            assert(!native->prepare_captured(*history.states[0],*history.states[1],false,false,true));
            assert(native->rejection==NativeBoundarySnapshot::Rejection::invalid_capture && !native->probe->restore_invoked);
            NativeHistoryRestore<> invalid_native;
            assert(!invalid_native.run(history,first,last,1));
            const auto detail=invalid_native.details();
            assert(detail.values[NativeRestoreDiagnostic::stage]==static_cast<unsigned>(decltype(invalid_native)::Stage::prepare));
            assert(detail.values[NativeRestoreDiagnostic::source_mode]==static_cast<unsigned>(mode));
            assert(detail.values[NativeRestoreDiagnostic::rejection]==static_cast<unsigned>(NativeBoundarySnapshot::Rejection::invalid_capture));
            assert(!detail.values[NativeRestoreDiagnostic::invoked]);
        }
        assert(!NativeAdjacentRestore::supported_pair(M::combat_capture_only,M::combat_capture_only,false,false,true));
        assert(!NativeAdjacentRestore::supported_pair(M::throw_move,M::charge,false,false,true));
    }
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

