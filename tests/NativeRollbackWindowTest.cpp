#include <functional>
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
#include "BoundedCorrectionTransactionFixture.hpp"
#include "CorrectionCycleScheduleFixture.hpp"
#include "CorrectionCycleBankFixture.hpp"
struct Backend {
    std::vector<unsigned> calls;unsigned rejectPrepare{99},rejectRestore{99},invalidateAfterLoads{99},loads{};
    bool validate_before_load(unsigned){return loads<invalidateAfterLoads;}
    bool prepare(unsigned i){calls.push_back(i);return i!=rejectPrepare;}
    bool restore(unsigned i){calls.push_back(10+i);++loads;return i!=rejectRestore;}
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
struct GeneralOwnedWindowFixture {
    HistoryFixture history;
    std::array<RestoreFixture,3> work;
    unsigned rejectLive{99};
    GeneralOwnedWindowFixture(){for(auto& s:history.states)s->mode=NativeBoundarySnapshot::Mode::general_combat;}
    bool prepare(unsigned i){return OwnedWindowRestorePreparation::preflight(work[i],*history.states[i],*history.states[i+1]);}
    bool validate_before_load(unsigned){return history.owned;}
    bool restore(unsigned i){
        RestoreFixture::reject=rejectLive;
        return OwnedWindowRestorePreparation::before_load(work[i],*history.states[i],*history.states[i+1]) &&
            work[i].restore_captured(*history.states[i],1,1);
    }
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
    inline static unsigned constructions{},resets{}; StorageRestore(){++constructions;}
    bool prepare_for_reuse(){++resets;return true;}
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
    ReplayDependencySource source(){assert(false && "guard must reject before resource access");return {};}
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
struct SuccessorResourcesFixture {
    struct Control {uintptr_t table{};LONG strong{1},weak{1};};
    inline static Control control{};
    inline static unsigned attempts{},last_source{},serial{};
    inline static bool reject{},stop_failure{};
    unsigned generation=++serial;
    GeneratedSnapshotDependency dependency;
    ~SuccessorResourcesFixture(){assert(dependency.reset());}
    bool start(uintptr_t){return dependency.acquire(1,2,reinterpret_cast<uintptr_t>(&control),40,[](uintptr_t){},[](uintptr_t){});}
    bool start_successor(SuccessorResourcesFixture& prior){++attempts;return !reject && dependency.retain_from(prior.dependency);}
    bool current_dependencies()const{return !reject && dependency.active();}
    bool stop(){return !stop_failure && dependency.reset();}
    ReplayDependencySource source(){last_source=generation;return {this,[](void*,uint32_t){return true;},&dependency};}
    template<class Stream> void wind_report(Stream& out)const{out<<"fixture_generation="<<generation<<'\n';}
};
struct MatchEndResourcesFixture : PreparationResourcesFixture {
    inline static MatchEndObservation observation{};
    inline static OwnershipCancellation end_cancellation;
    const OwnershipCancellation& cancellation()const{return end_cancellation;}
    inline static bool stop_ok=true;
    MatchEndObservation match_end_observation()const{return observation;}
    bool stop(){++releases;return stop_ok;}
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
struct RetainedReplayFixture {
    inline static std::function<void()> on_capture,on_release;
    void capture_failure_evidence(ReplayFailureEvidence& e){if(on_capture)on_capture();e.state.store(1,std::memory_order_release);}
    inline static bool archive_ok=true;
    bool capture_owned_recording(std::array<std::unique_ptr<ReplayFailureEvidence>,4>& out){
        if(!archive_ok)return false;
        for(auto& e:out){e=std::make_unique<ReplayFailureEvidence>();e->state=1;}return true;
    }
    enum class Stage {clean_rewind,reference_replay};Stage stage{};
    struct Window {
        inline static int removal=0;
        inline static bool rewind_ok=true,replay_ok=true,release_ok=true;
        inline static unsigned rewinds{},replays{},discards{},revivals{},round_finishes{};
        inline static bool finish_ok=true;
        bool finish_effect_round(){++round_finishes;return finish_ok;}
        bool defer_derived{};
        bool mutated{},quarantined{};
        bool capture(const RollbackBoundaryKey&,uintptr_t,uintptr_t,NativeBoundarySnapshot::Mode m,bool,ReplayDependencySource*){assert(m==NativeBoundarySnapshot::Mode::general_combat);return !quarantined;}
        int retained_removal(){return removal;}
        uint64_t retained_revivals(){return revivals;}
        bool discard_unmodified(){assert(!mutated);++discards;return true;}
        bool rewind(const RollbackBoundaryKey&){assert(defer_derived);mutated=true;++rewinds;if(rewind_ok && removal)++revivals;return rewind_ok;}
        bool replay_reference(SingleUpdateCall::Routine){++replays;return replay_ok;}
        bool recycle(){mutated=false;return true;}
        bool any_restore_invoked(){return mutated;}
        bool rewind_started(){return mutated;}
        bool release_owned_dependencies(){quarantined=true;if(RetainedReplayFixture::on_release)RetainedReplayFixture::on_release();return release_ok;}
    } clean;
};
struct CorrectionExchangeFixture {
    std::atomic<bool> failed{};
    bool publish(const RollbackBoundaryKey&,const OwnedPredictionCorrection::Input&){return true;}
    bool frames(OwnedPredictionCorrection::Frames&){return true;}
};
// Fault injection tests the production owner-thread wrapper, not SC6's loader.
struct CorrectionLifecycleFixture {
    inline static unsigned failStage{},releaseMode{},releases{},runs{};
    inline static std::function<void()> evidence_observer;
    void capture_failure_evidence(ReplayFailureEvidence& output){if(evidence_observer)evidence_observer();output.state=1;}
    struct Clean {
        unsigned capture_stage{};bool mutated{};
        bool capture(const RollbackBoundaryKey&,uintptr_t,uintptr_t,NativeBoundarySnapshot::Mode,bool,ReplayDependencySource*){return true;}
        bool any_restore_invoked()const{return mutated;}
        bool rewind_started()const{return mutated;}
    } clean;
    unsigned stage{},predicted_updates{},corrected_updates{},capture_detail{};
    enum class Stage {complete=8};
    bool injected{},state_changed{},restart_required{},retired{};
    inline static bool unchanged{};
    FullStateProbe::BlendPairEvidence evidence;
    const auto& blend_evidence(){return evidence;}
    OwnedPredictionCorrection::Diagnostics diagnostics(){return {};}
    bool run_confirmed(const OwnedPredictionCorrection::Frames&,const RollbackBoundaryKey&,uintptr_t,ReplayDependencySource&,SingleUpdateCall::Routine){
        ++runs;stage=failStage?failStage:8;
        if(failStage==6){evidence.valid=true;for(unsigned i=0;i<1372;++i){evidence.saved[i]=std::byte(i%256);evidence.next[i]=std::byte((i+1)%256);}}
        clean.mutated=failStage!=1;restart_required=failStage && clean.mutated;
        injected=stage>=3;state_changed=stage>=5;
        predicted_updates=stage>=6?3:0;corrected_updates=failStage?0:3;
        if(unchanged){state_changed=false;retired=true;restart_required=false;return false;}
        if(failStage==9)throw 1;
        return !failStage;
    }
    bool release(){++releases;if(releaseMode==2)throw 2;return releaseMode==0;}
};
void correction_lifecycle_tests(){
    using Runner=StartupPredictionCorrectionRunner<CorrectionLifecycleFixture,CorrectionExchangeFixture>;
    ReplayDependencySource source;RollbackBoundaryKey key{};RecordedInput input;
    std::array<std::byte,40> bytes{};const auto argument=input.assign(bytes);
    {
        CorrectionLifecycleFixture::failStage=6;CorrectionLifecycleFixture::releaseMode=0;
        CorrectionLifecycleFixture::releases=0;
        Runner runner;bool observed=false;
        CorrectionLifecycleFixture::evidence_observer=[&]{observed=true;assert(!runner.restart && !runner.failed && !runner.retired && CorrectionLifecycleFixture::releases==0);};
        for(unsigned i=0;i<4;++i)runner.step(key,1,argument,source,nullptr,true);
        assert(observed && runner.restart && runner.failed && runner.retired && runner.failure_evidence.state==1);
        CorrectionLifecycleFixture::evidence_observer={};
    }
    for(unsigned stage=0;stage<=9;++stage){
        CorrectionLifecycleFixture::failStage=stage;CorrectionLifecycleFixture::releaseMode=0;
        CorrectionLifecycleFixture::releases=CorrectionLifecycleFixture::runs=0;
        Runner runner;
        for(unsigned i=0;i<4;++i)runner.step(key,1,argument,source,nullptr,true);
        assert(runner.retired && runner.verified==(stage==0) && runner.failed==(stage!=0));
        assert(runner.restart==(stage>1));assert(runner.failure_reason==(stage==0?0u:stage==9?7u:6u));
        assert(runner.blend_evidence_valid==(stage==6));
        if(stage==6)for(unsigned i=0;i<1372;++i)assert(runner.blend_saved[i]==i%256 && runner.blend_next[i]==(i+1)%256);
        assert(CorrectionLifecycleFixture::releases==1 && CorrectionLifecycleFixture::runs==1);
        runner.step(key,1,argument,source,nullptr,true);assert(runner.release());
        assert(CorrectionLifecycleFixture::releases==1 && CorrectionLifecycleFixture::runs==1);
    }
    for(unsigned mode=0;mode<3;++mode){
        CorrectionLifecycleFixture::failStage=0;CorrectionLifecycleFixture::releaseMode=mode;
        CorrectionLifecycleFixture::unchanged=true;
        Runner runner;runner.combat_inputs=true;
        for(unsigned i=0;i<4;++i)runner.step(key,1,argument,source,nullptr,true);
        assert(runner.no_divergence==(mode==0) && !runner.verified);
        assert(runner.failed==(mode!=0) && runner.restart==(mode!=0));
        CorrectionLifecycleFixture::unchanged=false;
    }
    {
        CorrectionLifecycleFixture::failStage=0;CorrectionLifecycleFixture::releaseMode=0;
        Runner runner;runner.exchange.failed=true;
        runner.step(key,1,argument,source,nullptr,true);
        assert(runner.failure_reason==1 && runner.captured==0 && runner.retired);
        Runner hold;
        for(unsigned i=0;i<4;++i)hold.step(key,1,argument,source,nullptr,false);
        assert(hold.failure_reason==4 && hold.captured==4 && hold.retired && !hold.restored);
    }
    for(unsigned mode=1;mode<=2;++mode){
        CorrectionLifecycleFixture::failStage=0;CorrectionLifecycleFixture::releaseMode=mode;
        CorrectionLifecycleFixture::releases=CorrectionLifecycleFixture::runs=0;
        Runner runner;
        for(unsigned i=0;i<4;++i)runner.step(key,1,argument,source,nullptr,true);
        assert(runner.failed && runner.restart && !runner.verified && !runner.retired);
        assert(CorrectionLifecycleFixture::runs==1 && CorrectionLifecycleFixture::releases==1);
        runner.step(key,1,argument,source,nullptr,true);assert(CorrectionLifecycleFixture::runs==1);
        CorrectionLifecycleFixture::releaseMode=0;assert(runner.release());
        assert(runner.retired && runner.failed && runner.restart && !runner.verified);
    }
}
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
struct LivePreparationFixture : PreparationResourcesFixture {
    inline static unsigned wind_calls{},animation_calls{};
    inline static bool wind_ok=true,animation_ok=true;
    bool enable_wind(){++wind_calls;return wind_ok;}
    bool enable_animation_copy(){++animation_calls;return animation_ok;}
};
void reference_padding_tests(){
    constexpr uintptr_t image=0x140000000;
    auto expected=std::make_unique<FullStateProbe>(),actual=std::make_unique<FullStateProbe>();
    for(const auto& site:NativeReferencePadding::sites){
        for(auto* stream:{&expected->saved,&actual->saved,&actual->restored,&actual->after_save,&actual->replay}){
            stream->sealed=true;stream->writes=1;stream->lengths[0]=16;stream->stream.write_position=16;
            stream->traces[0].writer=image+site.writer;stream->traces[0].reader=image+site.reader;
            std::fill_n(stream->stream.payload.begin(),16,std::byte{});
        }
        actual->restore_completed=true;
        for(unsigned byte=0;byte<16;++byte){
            actual->restored.stream.payload[byte]=actual->replay.stream.payload[byte]=std::byte{0x91};
            NativeAdjacentRestore::PayloadDiagnostic d;
            const bool padding=byte>=4 && byte<8;
            assert(NativeAdjacentRestore::native_equal(*expected,*actual,image,&d)==padding);
            assert(TwoFrameIdleReplay::native_equal(*actual,image)==padding);
            if(!padding)assert(d.records==1 && d.bytes==1 && d.details[0][5]==byte && d.details[0][8]==0x91);
            actual->restored.stream.payload[byte]=actual->replay.stream.payload[byte]=std::byte{};
        }
        actual->restored.stream.payload[4]=actual->replay.stream.payload[4]=std::byte{7};
        ++actual->saved.traces[0].reader;
        assert(!NativeAdjacentRestore::native_equal(*expected,*actual,image));
        assert(!TwoFrameIdleReplay::native_equal(*actual,image));--actual->saved.traces[0].reader;
        actual->restore_completed=false;
        assert(!NativeAdjacentRestore::native_equal(*expected,*actual,image));
        assert(!TwoFrameIdleReplay::native_equal(*actual,image));actual->restore_completed=true;
        ++actual->restored.traces[0].writer;++actual->replay.traces[0].writer;
        assert(!NativeAdjacentRestore::native_equal(*expected,*actual,image));
        assert(!TwoFrameIdleReplay::native_equal(*actual,image));
        assert(expected->saved.stream.payload[4]==std::byte{} && actual->restored.stream.payload[4]==std::byte{7});
    }
    expected->saved.writes=actual->restored.writes=FullRestoreStream::transfer_limit+1;
    assert(!NativeAdjacentRestore::native_equal(*expected,*actual,image));
    actual->after_save.writes=actual->replay.writes=FullRestoreStream::transfer_limit+1;
    assert(!TwoFrameIdleReplay::native_equal(*actual,image));
    expected->saved.writes=actual->restored.writes=actual->after_save.writes=actual->replay.writes=1;
    actual->restored.stream.write_position=actual->replay.stream.write_position=SaveStream::limit+1;
    assert(!NativeAdjacentRestore::native_equal(*expected,*actual,image));
    assert(!TwoFrameIdleReplay::native_equal(*actual,image));
    NativeHistoryRestore<> restore;auto& pd=restore.diagnostic().payload_diagnostic;
    pd.structural=2;pd.records=19;pd.bytes=25;pd.retained=16;pd.overflow=3;pd.deferred_bytes=60;
    pd.details[15]={656,16,0x31DCF1,0x10000,0x20000,8,9,10,11};
    restore.diagnostic().probe->load_route_diagnostic={3,77,0x1234,0x5678};
      restore.diagnostic().probe->camera_readiness={20,7,3,0x68,1,0x3F000000,0x3ED8D4FE};
    const auto d=restore.details();assert(d.values[NativeRestoreDiagnostic::route_reason]==3 && d.values[NativeRestoreDiagnostic::route_record]==77);
    assert(d.values[NativeRestoreDiagnostic::payload_records]==19 && d.payload_details[15][0]==656);
      assert(d.values[NativeRestoreDiagnostic::camera_reason]==20 && d.values[NativeRestoreDiagnostic::camera_record]==7);
      assert(d.values[NativeRestoreDiagnostic::camera_saved]==0x3F000000 && d.values[NativeRestoreDiagnostic::camera_offset]==0x68);
    CombatRuntimeStatus status;
    for(unsigned i=0;i<NativeRestoreDiagnostic::count;++i)status.restore_details[i]=d.values[i];
    for(unsigned i=0;i<16;++i)for(unsigned j=0;j<9;++j)status.restore_payload_details[i][j]=d.payload_details[i][j];
    std::ostringstream events,report;status.write(events,report);
    assert(report.str().find("continuous_restore_payload_15_record=656")!=std::string::npos);
    assert(report.str().find("continuous_restore_payload_overflow=3")!=std::string::npos);
    std::cout<<"PASS: all audited descriptor padding pairs; all active bytes stay exact; raw payload retained; live mismatch evidence exported\n";
}
int main(){
    {
        CombatStartSendCadence cadence;CombatStartPacket::Bytes packet{};
        assert(cadence.due(packet,100,false));assert(!cadence.due(packet,109,false));
        assert(cadence.due(packet,110,false));packet[0]=1;
        assert(cadence.due(packet,111,false));assert(!cadence.due(packet,120,true));
        assert(cadence.due(packet,361,true));
    }

    reference_padding_tests();
    {
        NativeHistoryRestore<> restore;auto& p=*restore.diagnostic().probe;
        p.timer_diagnostic={1,2,1,2,0x44,0x3F800000,0x40000000};
        p.blend_timer_source.valid_mask=p.blend_timer_next.valid_mask=63;
        p.blend_timer_source.slots[1][2][0x44]=std::byte{0xAB};
        p.blend_timer_next.slots[1][2][0x44]=std::byte{0xCD};
        const auto d=restore.details();
        assert(d.values[NativeRestoreDiagnostic::timer_reason]==2 && d.values[NativeRestoreDiagnostic::timer_offset]==0x44);
        assert(d.timer_evidence_valid && d.timer_words[(1*3+2)*32+17]==0xAB);
        assert(d.timer_words[((1*2+1)*3+2)*32+17]==0xCD);
        CombatRuntimeStatus status;for(unsigned i=0;i<NativeRestoreDiagnostic::count;++i)status.restore_details[i]=d.values[i];
        for(unsigned i=0;i<d.timer_words.size();++i)status.restore_timer_words[i]=d.timer_words[i];
        status.restore_timer_evidence_valid=true;
        std::ostringstream events,report;status.write(events,report);
        assert(report.str().find("continuous_restore_timer_next_side1_slot2_offset68=205")!=std::string::npos);
        assert(report.str().find("continuous_restore_timer_reason=2")!=std::string::npos);
        p.timer_diagnostic={};assert(!restore.details().timer_evidence_valid);
        p.blend_pair_diagnostic={6,1,160,1,2,3,0x10000,0x20000};
        p.blend_pair_diagnostic.table_saved=0x123456789ABC0020ull;
        p.blend_pair_diagnostic.table_next=0;
        p.blend_pair_diagnostic.wrapper_saved=0x123456789ABC0020ull;
        p.blend_pair_diagnostic.resource_next=0x30000;
        const auto blend=restore.details();
        assert(blend.values[NativeRestoreDiagnostic::blend_reason]==6 && blend.values[NativeRestoreDiagnostic::blend_offset]==160);
        assert(blend.values[NativeRestoreDiagnostic::blend_saved_owner]==0x10000 && blend.values[NativeRestoreDiagnostic::blend_next]==3);
        assert(blend.values[NativeRestoreDiagnostic::blend_table_saved]==0x123456789ABC0020ull);
        assert(blend.values[NativeRestoreDiagnostic::blend_resource_next]==0x30000);
        for(unsigned i=0;i<NativeRestoreDiagnostic::count;++i)status.restore_details[i]=blend.values[i];
        std::ostringstream blend_events,blend_report;status.write(blend_events,blend_report);
        assert(blend_report.str().find("continuous_restore_blend_offset=160")!=std::string::npos);
        assert(blend_report.str().find("continuous_restore_blend_table_saved="+std::to_string(0x123456789ABC0020ull))!=std::string::npos);
        assert(blend_report.str().find("continuous_restore_blend_table_next=0")!=std::string::npos);

    }

    {
        using F=LivePreparationFixture;
        F::wind_calls=F::animation_calls=0;F::wind_ok=F::animation_ok=true;
        auto resources=std::make_unique<CombatResourcePreparation<F>>();
        assert(resources->enable_live_prediction());
        assert(resources->live_prediction_enabled && !resources->capture_enabled);
        assert(resources->enable_live_prediction() && F::wind_calls==1 && F::animation_calls==1);
        resources->request_stop();assert(!resources->enable_live_prediction());
        F::wind_ok=false;
        auto failure=std::make_unique<CombatResourcePreparation<F>>();
        assert(!failure->enable_live_prediction() && failure->failed && !failure->live_prediction_enabled);
        F::wind_ok=true;F::animation_ok=false;
        auto animation_failure=std::make_unique<CombatResourcePreparation<F>>();
        assert(!animation_failure->enable_live_prediction() && animation_failure->failed);
        F::animation_ok=true;
    }

    {
        using F=SuccessorResourcesFixture;
        F::serial=F::attempts=F::last_source=0;F::reject=false;
        {
            CombatResourcePreparation<F> resources;
            assert(resources.start(1));
            assert(!resources.successor_source_on_owner().available() && F::attempts==0);
            assert(!resources.successor_dependencies_current_on_owner());
            resources.owner_thread=GetCurrentThreadId();resources.ready=true;
            assert(resources.successor_dependencies_current_on_owner());
            std::thread foreign([&]{assert(!resources.successor_source_on_owner().available());assert(!resources.successor_dependencies_current_on_owner());});foreign.join();
            assert(F::attempts==0 && resources.source_on_owner().available() && F::last_source==1);
            F::reject=true;assert(!resources.successor_source_on_owner().available());
            assert(!resources.successor_dependencies_current_on_owner());
            assert(resources.source_on_owner().available() && F::last_source==1);
            F::reject=false;
            for(unsigned i=0;i<15;++i){assert(resources.successor_source_on_owner().available());assert(F::control.strong==2);}
            const auto attempts=F::attempts;
            assert(!resources.successor_source_on_owner().available() && F::attempts==attempts);
            std::ostringstream report;resources.wind_report(report);
            assert(report.str().find("continuous_resource_generations=16\n")!=std::string::npos);
            assert(report.str().find("retired_resources_0_fixture_generation=1\n")!=std::string::npos);
            resources.request_stop();assert(!resources.source_on_owner().available());
            assert(!resources.successor_dependencies_current_on_owner());
        }
        assert(F::control.strong==1);
        {
            CombatResourcePreparation<F> failed;
            assert(failed.start(1));failed.owner_thread=GetCurrentThreadId();failed.ready=true;
            F::stop_failure=true;
            assert(!failed.successor_source_on_owner().available() && failed.failed.load());
            assert(!failed.source_on_owner().available() && F::control.strong==3);
            F::stop_failure=false;
        }
        assert(F::control.strong==1);
    }
    {
        struct Control {uintptr_t table{};LONG strong{1},weak{1};} control;
        GeneratedSnapshotDependency prior,next,foreign;
        assert(!next.retain_from(prior));
        assert(prior.acquire(1,2,reinterpret_cast<uintptr_t>(&control),40,[](uintptr_t){},[](uintptr_t){}));
        assert(!prior.retain_from(prior));
        std::thread other([&]{assert(!foreign.retain_from(prior));});other.join();
        assert(control.strong==2 && next.retain_from(prior) && control.strong==3);
        assert(!next.retain_from(prior));
        assert(prior.reset() && control.strong==2 && next.active());
        assert(next.reset() && control.strong==1);
        ReplayOwnedResources unstarted,successor;
        assert(!successor.start_successor(unstarted));
        // Keep this large fixture off the unoptimized monolithic test stack.
        auto effect_storage=std::make_unique<NativeEffectSession>();auto& effect=*effect_storage;assert(effect.retired_on_owner());
        std::thread foreign_effect([&]{assert(!effect.retired_on_owner());});foreign_effect.join();
        effect.captures=1;assert(!effect.retired_on_owner());
        effect.retirements=1;assert(effect.retired_on_owner());
        effect.failure=2;assert(!effect.retired_on_owner());
    }
    {
        CombatRuntimeStatus old,current;unsigned drained=0;
        old.session=current.session=42;old.round_event=7;current.round_event=8;current.previous=&old;
        old.context=&drained;old.drain=[](void* p,std::ostream& events,std::ostream& summary){
            ++*static_cast<unsigned*>(p);events<<"old-event\n";summary<<"late_packets=3\n";
        };
        std::ostringstream events,summary;current.write_history(events,summary);
        assert(drained==1 && events.str()=="old-event\n");
        assert(summary.str().find("retired_event_7_late_packets=3")!=std::string::npos);
        assert(summary.str().find("continuous_diagnostic_chain_invalid=0")!=std::string::npos);
        // Repeated drains retain late-arriving evidence and do not mutate the chain.
        current.write_history(events,summary);assert(drained==2 && current.previous==&old);
        old.previous=&current;std::ostringstream invalid;
        current.write_history(events,invalid);
        assert(invalid.str().find("continuous_diagnostic_chain_invalid=1")!=std::string::npos);
        old.previous=nullptr;old.session=43;std::ostringstream foreign;
        current.write_history(events,foreign);
        assert(foreign.str().find("continuous_diagnostic_generations=1")!=std::string::npos);
        assert(foreign.str().find("old-event")==std::string::npos);
    }
    { // Timing preserves return values and records failed/throwing work without retrying it.
        ReplayTiming timing;auto& sample=timing.rows[0][ReplayTiming::capture];
        sample.add(10);sample.add(30);sample.add(20);
        assert(sample.count==3 && sample.ticks==60 && sample.maximum==30);
        unsigned called=0;
        assert(!timing.measure(0,ReplayTiming::restore,[&]{++called;return false;}));
        assert(called==1 && timing.rows[0][ReplayTiming::restore].count==1);
        bool threw=false;try{timing.measure(1,ReplayTiming::replay,[&]()->void{throw 7;});}catch(int){threw=true;}
        assert(threw && timing.rows[1][ReplayTiming::replay].count==1);
        timing.measure(99,ReplayTiming::release,[]{});
        assert(timing.rows[3][ReplayTiming::release].count==1 && ReplayTiming::frequency()>0);
    }

    {
        std::array<RollbackBoundaryKey,4> keys;
        for(unsigned i=0;i<4;++i)keys[i]={1,2,3,4,5,100+i,1,GetCurrentThreadId()};
        auto reset=[](){RestoreFixture::calls.clear();RestoreFixture::prepareCount=RestoreFixture::loadCount=RestoreFixture::structuralCount=0;RestoreFixture::reject=RestoreFixture::rejectLoad=99;};
        reset();GeneralOwnedWindowFixture success;
        assert(RollbackWindowPlan::execute(keys,4,keys[3],success)==RollbackWindowPlan::Result::restored);
        assert((RestoreFixture::calls==std::vector<unsigned>{100,101,102,52,12,51,11,50,10}));
        // No native load is admitted if any of the three structural pairs fails.
        for(unsigned i=0;i<3;++i){reset();RestoreFixture::reject=i;GeneralOwnedWindowFixture reject;
            assert(RollbackWindowPlan::execute(keys,4,keys[3],reject)==RollbackWindowPlan::Result::prepare_failed);
            assert(RestoreFixture::loadCount==0);
        }
        // Reject a changed live endpoint before loading that hop; never continue
        // into older states after a partial rewind has failed.
        for(unsigned i=0;i<3;++i){reset();GeneralOwnedWindowFixture reject;reject.rejectLive=i;
            assert(RollbackWindowPlan::execute(keys,4,keys[3],reject)==RollbackWindowPlan::Result::restore_failed);
            assert(RestoreFixture::loadCount==2-i);
        }
        reset();GeneralOwnedWindowFixture mixed; mixed.history.states[2]->mode=NativeBoundarySnapshot::Mode::idle;
        assert(RollbackWindowPlan::execute(keys,4,keys[3],mixed)==RollbackWindowPlan::Result::prepare_failed);
        assert(RestoreFixture::loadCount==0);
        reset();
    }
    test_correction_cycle_schedule();
    cycle_bank_test::run();
    correction_lifecycle_tests();
    bounded_transaction_test::run();
    for(unsigned scenario=0;scenario<4;++scenario){
        StartupCorrectionExchange a,b;a.require_combat_inputs=b.require_combat_inputs=true;
        assert(a.configure(600,1) && b.configure(600,2));
        RollbackBoundaryKey ka{1,2,3,1,4,600,1,GetCurrentThreadId()},kb{2,1,9,1,4,600,2,GetCurrentThreadId()};
        StartupCorrectionExchange::Words row{},ca{},cb{};
        for(unsigned i=0;i<3;++i){
            OwnedPredictionCorrection::Input x{},y{};unsigned p1=1,p2=scenario==2?0:0x400;
            for(unsigned off:{0u,4u,16u})std::memcpy(x.data()+off,&p1,4);
            for(unsigned off:{8u,12u,20u})std::memcpy(x.data()+off,&p2,4);
            y=x;if(scenario==1 && i==1)y[8]=std::byte{2};
            if(scenario==3){float half=.5f;std::memcpy(y.data()+24,&half,4);}
            ka.frame=kb.frame=600+i;assert(a.publish(ka,x) && b.publish(kb,y));
            assert(a.local(i,row) && b.receive(i,row));assert(b.local(i,row) && a.receive(i,row));
        }
        if(scenario==1 || scenario==2){
            assert(!a.certificate(ca) && !b.certificate(cb));a.update();b.update();
            assert(a.failed && b.failed && !a.ready && !b.ready && a.input_rejection==scenario && b.input_rejection==scenario);
        }else{
            assert(a.certificate(ca) && b.certificate(cb));assert(a.receive_certificate(cb) && b.receive_certificate(ca));
            a.update();b.update();assert(a.ready && b.ready);
        }
    }
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
            assert(C::inputs(frames,keys,reference,actual,wrong,true));
            for(unsigned i=0;i<3;++i)for(unsigned word=0;word<10;++word){
                unsigned value{};std::memcpy(&value,wrong[i].data()+4*word,4);
                const unsigned remote=3-side;
                const bool button=word==2*(remote-1) || word==2*(remote-1)+1 || word==4+remote-1;
                assert(value==((i==0 && button)?1u:0u));
                assert(actual[i]==reference[i]);
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
            assert(!C::inputs(frames,keys,reference,actual,wrong,false,true));
            auto combat=frames;auto combatReference=reference;
            for(unsigned i=0;i<3;++i){
                for(unsigned w=0;w<6;++w)combat[i].local[w]=combat[i].peer[w]=(i+1)*16+w+1;
                combat[i].local[6]=0x3F000000;
                std::memcpy(combatReference[i].data(),combat[i].local.data(),40);
            }
            assert(C::inputs(combat,keys,combatReference,actual,wrong,false,true));
            for(unsigned i=0;i<3;++i){
                assert(actual[i]==combatReference[i]);
                for(unsigned w=0;w<10;++w){unsigned value{};std::memcpy(&value,wrong[i].data()+w*4,4);
                    const unsigned remote=3-side;
                    const bool remoteWord=w==2*(remote-1) || w==2*(remote-1)+1 || w==4+remote-1;
                    assert(value==(remoteWord?0u:combat[i].local[w]));
                }
            }
            auto missingCombat=combat;missingCombat[1].confirmed=false;
            assert(!C::inputs(missingCombat,keys,combatReference,actual,wrong,false,true));
            auto wrongReference=combatReference;wrongReference[2][0]^=std::byte{1};
            assert(!C::inputs(combat,keys,wrongReference,actual,wrong,false,true));
            auto unsupported=reference;unsupported[0][0]=std::byte{1};
            auto active=frames;active[0].local[0]=1;active[0].peer[0]=1;
            assert(!C::inputs(active,keys,unsupported,actual,wrong));
        }
        auto correction=std::make_unique<C>();sc6::NetworkGameHandoff handoff;ReplayDependencySource source;
        assert(!correction->run(handoff,keys[3],1,source,nullptr));
        assert(correction->attempted && !correction->injected && !correction->restart_required && !correction->verified);
        correction->stage=C::Stage::clean_rewind;
        auto& detail=correction->clean.diagnostic();
        detail.probe->source_guard_reason=4;detail.probe->source_guard_record=17;
        detail.probe->source_guard_length=16;detail.probe->source_guard_saved=100;detail.probe->source_guard_next=200;detail.probe->source_guard_writer=300;
        detail.completion.failure_line=321;detail.completion.section=2;detail.completion.record=9;detail.completion.kind=24;detail.completion.difference_offset=12;
        detail.probe->combat_postload_pass_mask=31;detail.probe->load_entry_rng=42;detail.probe->load_entry_rng_valid=true;detail.probe->original_next.rng=43;detail.probe->combat_preflight_pass_mask=7;
        detail.probe->camera_readiness={9,7,3,104,1,0,1065353216};
        correction->clean.topology={6,12,20,21,{0x100000001ULL,1,2,3,4,24},{0x200000002ULL,2,3,4,5,32}};
        detail.replay_measurement_read=true;detail.replay_rng_equal=false;detail.replay_players_equal=true;
        detail.replay_rng_expected=123;detail.replay_rng_actual=456;
        detail.replay_scalar_differences={2,3};detail.replay_first_offset={8,128};detail.replay_expected_byte={10,20};detail.replay_live_byte={11,21};
        assert(correction->diagnostics()[291]==0); // Not compared yet.
        detail.replay_checked=true;
        detail.probe->rings[2].valid=detail.probe->replay_rings.valid=true;
        detail.probe->replay_rings.bytes[12]=std::byte{7};
        detail.replay_native_difference.reason=4;detail.replay_native_difference.record=19;
        detail.replay_native_difference.writer=0x100000001ULL;
        detail.probe->camera_diagnostic={101,2,10,72,20,22,0,0};
        const auto diagnosis=correction->diagnostics();
        assert(diagnosis[291]==2 && diagnosis[292]==3 && diagnosis[295]==12 && diagnosis[298]==7);
        assert(diagnosis[301]==4 && diagnosis[302]==19 && diagnosis[305]==0x100000001ULL);
        ReplayFailureEvidence evidence;evidence.capture(*detail.probe);
        assert(evidence.state==1 && evidence.bytes.size()>RingSnapshot::byte_count*2);
        assert(evidence.bytes.capacity()>=ReplayFailureEvidence::limit && evidence.bytes.size()<=ReplayFailureEvidence::limit);
        ReplayFailureEvidence deferred;
        auto scratch=std::make_shared<ReplayFailureEvidence::TraceScratch>();
        deferred.prepare_deferred(scratch);deferred.capture(*detail.probe);
        assert(deferred.state==1 && deferred.bytes.empty() && deferred.logical_size==evidence.bytes.size());
        // Source/scratch reuse cannot change a published worker-owned artifact.
        const auto old_rng=detail.probe->original_next.rng;detail.probe->original_next.rng^=123;
        scratch->rng.schema=999;
        auto path=std::filesystem::temp_directory_path()/"sc6-deferred-evidence-fixture.bin";
        bool worker_ok=false;std::thread worker([&]{worker_ok=deferred.write(path);});worker.join();assert(worker_ok);
        std::ifstream input(path,std::ios::binary);std::vector<char> emitted_bytes((std::istreambuf_iterator<char>(input)),{});input.close();
        assert(emitted_bytes.size()==evidence.bytes.size() && !std::memcmp(emitted_bytes.data(),evidence.bytes.data(),emitted_bytes.size()));
        std::filesystem::remove(path);detail.probe->original_next.rng=old_rng;
        const auto arena_address=deferred.arena.get();
        deferred.reset_after_write();assert(deferred.state==0 && deferred.used==0 && deferred.section_count==0);
        deferred.capture(*detail.probe);assert(deferred.state==1 && deferred.arena.get()==arena_address);
        assert(deferred.write(path));std::filesystem::remove(path);
        ReplayFailureEvidence overflow;overflow.prepare_deferred(scratch);bool overflow_rejected=false;
        try{overflow.section(std::string(96,'x'),nullptr,0);}catch(...){overflow_rejected=true;}
        assert(overflow_rejected && overflow.section_count==0 && overflow.used==0);
        const auto evidence_size=evidence.bytes.size();evidence.capture(*detail.probe);assert(evidence.bytes.size()==evidence_size);
        ReplayFailureEvidence bounded;bool rejected=false;try{bounded.append(nullptr,ReplayFailureEvidence::limit+1);}catch(...){rejected=true;}assert(rejected);
        assert(diagnosis[313]==101 && diagnosis[315]==10 && diagnosis[316]==72 && diagnosis[318]==22);
        assert(diagnosis.size()==350 && diagnosis[0]==0 && diagnosis[91]==31);
        const std::array<uint64_t,15> expectedDetail={4,17,16,100,200,300,321,2,9,24,12,42,1,43,7};
        for(unsigned i=0;i<expectedDetail.size();++i)assert(diagnosis[23+i]==expectedDetail[i]);
        const std::array<uint64_t,7> expectedCamera={9,7,3,104,1,0,1065353216};
        for(unsigned i=0;i<7;++i)assert(diagnosis[46+i]==expectedCamera[i]);
        const std::array<uint64_t,15> expectedTopology={12,20,21,0x100000001ULL,1,2,3,4,24,0x200000002ULL,2,3,4,5,32};
        for(unsigned i=0;i<15;++i)assert(diagnosis[53+i]==expectedTopology[i]);
        const std::array<uint64_t,13> expectedScalars={1,0,1,123,456,2,8,10,11,3,128,20,21};
        for(unsigned i=0;i<13;++i)assert(diagnosis[68+i]==expectedScalars[i]);
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
        StartupSuite combatSuite;assert(combatSuite.configure(9,60) && combatSuite.mask()==255);
        StartupSuite attackSuite;assert(attackSuite.configure(8,60) && attackSuite.mask()==255);
        StartupSuite a,b;assert(!a.configure(17,60) && !a.configure(3,181));
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
        TargetedRetentionCapture<StartupHistoryFixture> retention;ReplayDependencySource src;
        RollbackBoundaryKey key{1,2,3,1,1,600,1,GetCurrentThreadId()};
        const auto initial_releases=StartupHistoryFixture::releases;
        for(unsigned i=0;i<1200;++i){
            key.frame=600+i;retention.step(key,1,2,src);
            assert(retention.captured==i+1);
            assert(retention.windows==(i+1)/4);
            assert(StartupHistoryFixture::releases==initial_releases+(i+1)/4);
            assert(bool(retention.retired)==(i==1199));
        }
        assert(retention.retired && !retention.failed && retention.windows==300 && retention.captured==1200);
        const auto calls=StartupHistoryFixture::calls;
        key.round=2;retention.step(key,0,0,src);assert(StartupHistoryFixture::calls==calls);
        TargetedRetentionCapture<StartupHistoryFixture> transition;
        key.round=1;transition.step(key,1,2,src);key.round=2;transition.step(key,0,0,src);
        assert(transition.round_cancelled && transition.retired && !transition.failed && transition.captured==1);
        TargetedRetentionCapture<StartupHistoryFixture> rejected;StartupHistoryFixture::capture_ok=false;
        rejected.step(key,1,2,src);assert(rejected.failed && rejected.retired);
        StartupHistoryFixture::capture_ok=true;
        StartupSuite retention_suite;assert(retention_suite.configure(11,30) && retention_suite.mask()==127);
        assert(retention_suite.seal(63,0,64));
        using F=RetainedReplayFixture::Window;
        ReplayDependencySource replay_source;
        RollbackBoundaryKey rk{1,2,3,1,1,700,1,GetCurrentThreadId()};
        RetainedObjectReplay<RetainedReplayFixture> no_removal;
        for(unsigned i=0;i<128;++i){rk.frame=700+i;no_removal.step(rk,1,2,replay_source,nullptr);}
        assert(no_removal.retired && !no_removal.failed && no_removal.no_event==32 && !no_removal.verified);
        assert(F::discards==32 && F::rewinds==0 && F::replays==0);
        RetainedObjectReplay<RetainedReplayFixture> success;F::removal=1;
        for(unsigned i=0;i<12;++i){rk.frame=700+i;success.step(rk,1,2,replay_source,nullptr);if(i<11)assert(!success.verified && !success.terminal());}
        assert(success.verified && success.revived==3 && success.completed_cycles==3 && success.restored && success.retired && !success.restart);
        const auto replay_count=F::replays;rk.round=2;success.step(rk,0,0,replay_source,nullptr);assert(F::replays==replay_count);
        RetainedObjectReplay<RetainedReplayFixture> boundary;rk.round=1;boundary.step(rk,1,2,replay_source,nullptr);
        rk.round=2;boundary.step(rk,0,0,replay_source,nullptr);assert(boundary.round_cancelled && boundary.retired && !boundary.restored);
        RetainedObjectReplay<RetainedReplayFixture> replay_failure;F::replay_ok=false;
        unsigned publication_order=0;
        RetainedReplayFixture::on_capture=[&]{assert(!replay_failure.terminal() && !replay_failure.failed && replay_failure.restart);publication_order=1;};
        RetainedReplayFixture::on_release=[&]{assert(publication_order==1 && !replay_failure.terminal() && replay_failure.evidence.state.load()==1);publication_order=2;};
        for(unsigned i=0;i<4;++i){rk.frame=700+i;replay_failure.step(rk,1,2,replay_source,nullptr);}
        assert(replay_failure.failed && replay_failure.restart && replay_failure.retired && !replay_failure.verified);
        assert(publication_order==2 && replay_failure.terminal() && replay_failure.animation_ready && replay_failure.evidence.state.load()==1);
        RetainedReplayFixture::on_capture={};RetainedReplayFixture::on_release={};
        F::replay_ok=true;F::release_ok=false;
        RetainedObjectReplay<RetainedReplayFixture> cleanup_failure;
        for(unsigned i=0;i<12;++i){rk.frame=700+i;cleanup_failure.step(rk,1,2,replay_source,nullptr);}
        assert(cleanup_failure.failed && cleanup_failure.restart && !cleanup_failure.retired && !cleanup_failure.verified && cleanup_failure.terminal());
        F::release_ok=true;assert(cleanup_failure.release());
        RetainedObjectReplay<RetainedReplayFixture> evidence_failure;
        RetainedReplayFixture::on_capture=[] {throw 1;};
        evidence_failure.stop(6);
        assert(evidence_failure.terminal() && evidence_failure.failed && evidence_failure.retired && evidence_failure.evidence.state.load()==2);
        RetainedReplayFixture::on_capture={};

        // A verified first cycle must not hide a later failure or round change.
        RetainedObjectReplay<RetainedReplayFixture> later_failure;F::removal=1;
        for(unsigned i=0;i<4;++i){rk.frame=800+i;later_failure.step(rk,1,2,replay_source,nullptr);}
        assert(later_failure.completed_cycles==1 && !later_failure.terminal() && later_failure.cycles[0].ready);
        F::replay_ok=false;
        for(unsigned i=0;i<4;++i){rk.frame=804+i;later_failure.step(rk,1,2,replay_source,nullptr);}
        assert(later_failure.failed && later_failure.restart && !later_failure.verified && later_failure.completed_cycles==1);
        const auto stopped_replays=F::replays;later_failure.step(rk,1,2,replay_source,nullptr);assert(F::replays==stopped_replays);
        F::replay_ok=true;
        RetainedObjectReplay<RetainedReplayFixture> between_rounds;
        for(unsigned i=0;i<4;++i){rk.frame=900+i;between_rounds.step(rk,1,2,replay_source,nullptr);}
        ++rk.round;between_rounds.step(rk,1,2,replay_source,nullptr);
        assert(between_rounds.round_cancelled && between_rounds.terminal() && !between_rounds.verified && between_rounds.completed_cycles==1);
        RetainedObjectReplay<RetainedReplayFixture> exhausted;
        for(unsigned i=0;i<4;++i){rk.frame=1000+i;exhausted.step(rk,1,2,replay_source,nullptr);}
        F::removal=0;
        for(unsigned i=0;i<128;++i){rk.frame=1004+i;exhausted.step(rk,1,2,replay_source,nullptr);}
        assert(exhausted.terminal() && !exhausted.verified && !exhausted.failed && exhausted.completed_cycles==1 && exhausted.windows==33);
        F::removal=1;

        // Same-session recording requires one verified replay, not removal.
        F::removal=0;rk.round=1;
        RetainedObjectReplay<RetainedReplayFixture> recording(true);
        for(unsigned i=0;i<4;++i){rk.frame=1200+i;recording.step(rk,1,2,replay_source,nullptr);}
        assert(recording.verified && recording.retired && recording.completed_cycles==1 && recording.revived==0);
        assert(recording.recording_ready && recording.terminal() && recording.cycles[0].replay_evidence.state==1);
        const auto once=F::replays;rk.frame=1204;recording.step(rk,1,2,replay_source,nullptr);assert(F::replays==once);
        RetainedObjectReplay<RetainedReplayFixture> recording_round(true);
        recording_round.step(rk,1,2,replay_source,nullptr);++rk.round;recording_round.step(rk,1,2,replay_source,nullptr);
        assert(recording_round.round_cancelled && recording_round.retired && !recording_round.restored && !recording_round.verified);
        RetainedReplayFixture::archive_ok=false;
        RetainedObjectReplay<RetainedReplayFixture> recording_archive_failure(true);
        const auto before_archive=F::rewinds;
        for(unsigned i=0;i<4;++i){rk.frame=1300+i;recording_archive_failure.step(rk,1,2,replay_source,nullptr);}
        assert(recording_archive_failure.failed && recording_archive_failure.failure_reason==11 && F::rewinds==before_archive);
        RetainedReplayFixture::archive_ok=true;F::replay_ok=false;
        RetainedObjectReplay<RetainedReplayFixture> recording_replay_failure(true);
        for(unsigned i=0;i<4;++i){rk.frame=1400+i;recording_replay_failure.step(rk,1,2,replay_source,nullptr);}
        assert(recording_replay_failure.failed && recording_replay_failure.restart && recording_replay_failure.terminal());
        F::replay_ok=true;F::removal=1;
        // Repeated recording preserves each archive and never captures in the gap.
        F::removal=0;rk.round=1;
        RetainedObjectReplay<RetainedReplayFixture> spaced(true,true);
        const auto initial_replays=F::replays;
        for(unsigned cycle=0;cycle<3;++cycle){
            const unsigned start=1500+cycle*123;
            for(unsigned i=0;i<4;++i){rk.frame=start+i;spaced.step(rk,1,2,replay_source,nullptr);}
            assert(spaced.completed_cycles==cycle+1 && F::replays==initial_replays+cycle+1);
            if(cycle<2){
                assert(!spaced.terminal() && !spaced.verified);
                const auto captures=spaced.captured.load();
                for(unsigned i=4;i<123;++i){rk.frame=start+i;spaced.step(rk,1,2,replay_source,nullptr);}
                assert(spaced.captured==captures);
            }
        }
        assert(spaced.verified && spaced.retired && spaced.captured==12 && spaced.revived==0);
        assert(spaced.cycles[1].snapshots_ready && spaced.cycles[2].snapshots_ready);
        for(const auto& c:spaced.cycles)assert(c.ready && c.replay_evidence.state==1);
        assert(spaced.evidence.state==0); // Failure channel not occupied by successes.
        RetainedObjectReplay<RetainedReplayFixture> gap_round(true,true);
        for(unsigned i=0;i<4;++i){rk.frame=2000+i;gap_round.step(rk,1,2,replay_source,nullptr);}
        ++rk.round;rk.frame=1;gap_round.step(rk,1,2,replay_source,nullptr);
        assert(gap_round.round_cancelled && gap_round.retired && !gap_round.verified && gap_round.completed_cycles==1);
        RetainedObjectReplay<RetainedReplayFixture> second_fail(true,true);
        for(unsigned i=0;i<4;++i){rk.frame=2100+i;second_fail.step(rk,1,2,replay_source,nullptr);}
        F::replay_ok=false;
        for(unsigned i=0;i<4;++i){rk.frame=2223+i;second_fail.step(rk,1,2,replay_source,nullptr);}
        assert(second_fail.failed && second_fail.restart && !second_fail.verified && second_fail.completed_cycles==1);
        assert(second_fail.cycles[0].replay_evidence.state==1 && second_fail.evidence.state==1 && second_fail.cycles[1].snapshots_ready);
        F::replay_ok=true;F::removal=1;
        // Three distinct rounds, no captures in the gap, and immutable per-round evidence.
        F::removal=0;rk.round=1;rk.generation=100;
        sc6::RoundFightReadiness fight;
        auto activate=[&](unsigned frame){fight={rk.generation,rk.round,frame,rk.thread,0x3E873D0,0x384EE0,3};};
        RetainedObjectReplay<RetainedReplayFixture> boundary_policy(true,true,true);
        assert(!boundary_policy.clean_match_end_boundary());
        boundary_policy.completed_cycles=2;boundary_policy.waiting_round_=true;
        assert(boundary_policy.clean_match_end_boundary());
        boundary_policy.failed=true;assert(!boundary_policy.clean_match_end_boundary());
        boundary_policy.failed=false;boundary_policy.restart=true;assert(!boundary_policy.clean_match_end_boundary());
        boundary_policy.restart=false;boundary_policy.waiting_round_=false;assert(!boundary_policy.clean_match_end_boundary());
        RetainedObjectReplay<RetainedReplayFixture> multi(true,true,true);
        for(unsigned round=1;round<=3;++round){
            rk.round=round;rk.generation=99+round;
            rk.frame=50;multi.step(rk,1,2,replay_source,nullptr,fight);
            assert(multi.captured==(round-1)*4);activate(99);
            for(unsigned i=0;i<4;++i){rk.frame=100+i;activate(rk.frame);multi.step(rk,1,2,replay_source,nullptr,fight);}
            assert(multi.completed_cycles==round && multi.cycles[round-1].round==round);
            rk.frame=200;multi.step(rk,1,2,replay_source,nullptr,fight);assert(multi.captured==round*4);
        }
        assert(multi.verified && multi.round_transitions==2 && !multi.restart);
        RetainedObjectReplay<RetainedReplayFixture> partial(true,true,true);
        rk.round=1;rk.generation=200;rk.frame=10;activate(10);partial.step(rk,1,2,replay_source,nullptr,fight);
        rk.round=2;rk.generation=201;rk.frame=1;partial.step(rk,1,2,replay_source,nullptr,fight);
        assert(partial.partial_round_discards==1 && partial.completed_cycles==0 && !partial.failed);
        F::finish_ok=false;
        rk.round=3;rk.generation=202;partial.step(rk,1,2,replay_source,nullptr,fight);
        assert(partial.failed && partial.failure_reason==16 && !partial.restored);
        F::finish_ok=true;
        RetainedObjectReplay<RetainedReplayFixture> rematch(true,true,true);
        rk.round=3;rk.generation=300;rematch.step(rk,1,2,replay_source,nullptr);
        rk.round=1;rk.generation=301;rematch.step(rk,1,2,replay_source,nullptr);
        assert(rematch.round_cancelled && !rematch.verified);
        RetainedObjectReplay<RetainedReplayFixture> interrupted(true,true,true);
        rk.round=1;rk.generation=400;rk.frame=10;activate(10);
        interrupted.step(rk,1,2,replay_source,nullptr,fight);assert(interrupted.captured==1);
        fight.status=1;rk.frame=11;
        interrupted.step(rk,1,2,replay_source,nullptr,fight);
        assert(interrupted.fight_partial_discards==1 && !interrupted.restored && !interrupted.failed);
        rk.frame=1000;interrupted.step(rk,1,2,replay_source,nullptr,fight);assert(interrupted.captured==1);
        rk.round=2;rk.generation=401;rk.frame=1001;
        interrupted.step(rk,1,2,replay_source,nullptr,fight);assert(interrupted.round_transitions==1 && interrupted.captured==1);
        activate(1001);
        for(unsigned i=0;i<4;++i){rk.frame=1002+i;activate(rk.frame);interrupted.step(rk,1,2,replay_source,nullptr,fight);}
        assert(interrupted.completed_cycles==1 && interrupted.cycles[0].fight_frame==1002 && interrupted.cycles[0].first_frame==1002);
        for(unsigned marker=0;marker<2;++marker){
            RetainedObjectReplay<RetainedReplayFixture> playback_guard(true,true,true,true);
            rk.round=1;rk.generation=450;rk.frame=10;activate(10);
            playback_guard.step(rk,1,2,replay_source,nullptr,fight);
            for(unsigned f=11;f<20;++f){
                rk.frame=f;activate(f);
                if(marker==0)fight.native_playback=1;else fight.round_result=1;
                playback_guard.step(rk,1,2,replay_source,nullptr,fight);
            }
            assert(playback_guard.captured==1 && playback_guard.fight_partial_discards==1);
            assert(!playback_guard.restored && !playback_guard.failed && !playback_guard.restart);
            assert(marker==0?playback_guard.fight_playback_wait_frames==9:playback_guard.fight_result_wait_frames==9);
            for(unsigned f=20;f<24;++f){rk.frame=f;activate(f);playback_guard.step(rk,1,2,replay_source,nullptr,fight);}
            assert(playback_guard.completed_cycles==1 && !playback_guard.failed);
        }
        // Repeated combat keeps the same fight and round guards without waiting a round per cycle.
        RetainedObjectReplay<RetainedReplayFixture> combat(true,true,true,true);
        rk.round=1;rk.generation=500;
        for(unsigned cycle=0;cycle<3;++cycle){
            const unsigned start=100+cycle*123;
            if(cycle){rk.frame=start-1;activate(rk.frame);combat.step(rk,1,2,replay_source,nullptr,fight);assert(combat.captured==cycle*4);}
            for(unsigned i=0;i<4;++i){rk.frame=start+i;activate(rk.frame);combat.step(rk,1,2,replay_source,nullptr,fight);}
            assert(combat.completed_cycles==cycle+1 && combat.cycles[cycle].round==1);
            assert(combat.cycles[cycle].first_frame==start && !combat.waiting_round_);
            assert(combat.clean_match_end_boundary());
        }
        assert(combat.verified && combat.retired && !combat.failed && !combat.restart);
        RetainedObjectReplay<RetainedReplayFixture> combat_transition(true,true,true,true);
        rk.round=1;rk.generation=600;
        for(unsigned i=0;i<4;++i){rk.frame=100+i;activate(rk.frame);combat_transition.step(rk,1,2,replay_source,nullptr,fight);}
        rk.frame=223;activate(223);combat_transition.step(rk,1,2,replay_source,nullptr,fight);
        assert(!combat_transition.clean_match_end_boundary());
        rk.round=2;rk.generation=601;rk.frame=1;fight.status=1;
        combat_transition.step(rk,1,2,replay_source,nullptr,fight);
        assert(combat_transition.partial_round_discards==1 && combat_transition.completed_cycles==1 && !combat_transition.failed);
        for(unsigned i=0;i<4;++i){rk.frame=50+i;activate(rk.frame);combat_transition.step(rk,1,2,replay_source,nullptr,fight);}
        assert(combat_transition.completed_cycles==2 && combat_transition.cycles[1].round==2);
        F::replay_ok=false;
        for(unsigned i=0;i<4;++i){rk.frame=173+i;activate(rk.frame);combat_transition.step(rk,1,2,replay_source,nullptr,fight);}
        assert(combat_transition.failed && combat_transition.restart && combat_transition.completed_cycles==2);
        F::replay_ok=true;
        // Full-match mode wraps its bounded evidence queue; a slow worker never overwrites it.
        {
            auto full=std::make_unique<RetainedObjectReplay<RetainedReplayFixture>>(true,true,true,true,true);
            rk.round=1;rk.generation=700;
            auto play=[&](unsigned start){for(unsigned i=0;i<4;++i){rk.frame=start+i;activate(rk.frame);full->step(rk,1,2,replay_source,nullptr,fight);}};
            play(100);play(223);play(346);
            assert(full->completed_cycles==3 && !full->terminal() && !full->verified && full->target_cycles()==0);
            const auto count=full->captured.load();play(469);
            assert(full->captured==count && full->evidence_wait_frames==4);
            auto directory=std::filesystem::temp_directory_path()/("sc6-full-match-"+std::to_string(GetCurrentProcessId()));
            std::filesystem::create_directories(directory);
            std::ostringstream report;
            std::thread writer([&]{full->report(report,directory/"test.txt");});writer.join();
            assert(full->evidence_flushed==3 && !full->evidence_io_failures);
            play(500);assert(full->completed_cycles==4 && full->cycles[0].number==4 && !full->terminal());
            // Partial pre-fight window must not survive into the next round.
            rk.frame=623;activate(rk.frame);full->step(rk,1,2,replay_source,nullptr,fight);
            rk.round=2;rk.generation=701;rk.frame=1;fight.status=1;
            full->step(rk,1,2,replay_source,nullptr,fight);
            assert(full->partial_round_discards==1 && full->round_transitions==1 && !full->failed);
            play(100);assert(full->completed_cycles==5);
            rk.frame=223;activate(rk.frame);full->step(rk,1,2,replay_source,nullptr,fight);
            assert(full->clean_match_end_boundary());
            assert(full->finish_match() && full->terminal() && full->verified && full->match_completed && full->match_end_discards==1);
            full->report(report,directory/"test.txt");assert(full->evidence_flushed==5);
            assert(std::filesystem::exists(directory/"test-cycle-5-summary.txt"));
            assert(std::filesystem::exists(directory/"test-cycle-1-animation-copy.bin"));
            assert(!std::filesystem::exists(directory/"test-cycle-2-animation-copy.bin"));
            assert(std::filesystem::exists(directory/"test-cycle-2-summary.txt"));
            assert(std::filesystem::exists(directory/"test-cycle-3-animation-copy.bin"));
            {std::ofstream unverified(directory/"test-cycle-9-animation-copy.bin");unverified<<"keep";}
            assert(prune_verified_cycle(directory/"test.txt",12));
            assert(std::filesystem::exists(directory/"test-cycle-9-animation-copy.bin"));
            std::filesystem::remove_all(directory);
        }
        {
            auto io=std::make_unique<RetainedObjectReplay<RetainedReplayFixture>>(true,true,true,true,true);
            rk.round=1;rk.generation=800;
            for(unsigned i=0;i<4;++i){rk.frame=100+i;activate(rk.frame);io->step(rk,1,2,replay_source,nullptr,fight);}
            std::ostringstream report;io->report(report,"Z:/nonexistent-sc6-directory/test.txt");
            assert(io->evidence_io_failures && io->evidence_flushed==0 && !io->cycles[0].available);
            rk.frame=223;activate(rk.frame);io->step(rk,1,2,replay_source,nullptr,fight);
            assert(!io->failed && io->storage_blocked && io->captured==4 && !io->verified);
            auto directory=std::filesystem::temp_directory_path()/("sc6-storage-retry-"+std::to_string(GetCurrentProcessId()));
            std::filesystem::create_directories(directory);
            io->report(report,directory/"test.txt");
            assert(!io->storage_blocked && io->evidence_flushed==1 && io->cycles[0].available);
            for(unsigned i=0;i<4;++i){rk.frame=224+i;activate(rk.frame);io->step(rk,1,2,replay_source,nullptr,fight);}
            assert(io->completed_cycles==2 && !io->failed);
            std::filesystem::remove_all(directory);
        }
        {
            auto empty=std::make_unique<RetainedObjectReplay<RetainedReplayFixture>>(true,true,true,true,true);
            assert(empty->finish_match() && empty->match_completed && !empty->verified);
            auto bad_release=std::make_unique<RetainedObjectReplay<RetainedReplayFixture>>(true,true,true,true,true);
            F::release_ok=false;assert(!bad_release->finish_match() && bad_release->terminal() && bad_release->failed && !bad_release->verified);F::release_ok=true;
            auto abort=std::make_unique<RetainedObjectReplay<RetainedReplayFixture>>(true,true,true,true,true);
            rk.round=1;rk.generation=900;
            for(unsigned i=0;i<4;++i){rk.frame=100+i;activate(rk.frame);abort->step(rk,1,2,replay_source,nullptr,fight);}
            assert(abort->release() && !abort->verified && !abort->match_completed);
        }
        // Startup identity may change only after bilateral admission and release.
        {
            StartupBoundaryPairs pairs;pairs.count=120;pairs.rows[0]={};
            pairs.rows[0].identity=7;pairs.rows[0].clock=10;pairs.rows[0].battle=20;
            StartupBoundaryAgreement a,b;assert(a.begin(pairs,1) && b.begin(pairs,2));
            assert(a.runtime_rejection(15,3,false,false,8,7,900)==2);
            assert(a.receive(b.outgoing()) && b.receive(a.outgoing()) && a.receive(b.outgoing()));
            assert(a.ready());
            assert(a.runtime_rejection(15,3,false,false,8,7,900)==0);
            assert(a.runtime_rejection(14,3,false,false,8,7,900)==2);
            assert(a.runtime_rejection(15,2,false,false,8,7,900)==2);
            assert(a.runtime_rejection(15,0,false,false,7,7,900)==4);
            auto bad=b.outgoing();bad[1]++;assert(!a.receive(bad));
            assert(a.runtime_rejection(15,3,false,false,8,7,900)==3);
        }
        StartupSuite fullMatch;assert(fullMatch.configure(16,0) && fullMatch.first_match_start() && fullMatch.mask()==255);
        StartupSuite multiround;assert(multiround.configure(15,30) && multiround.mask()==255 && multiround.first_match_start());
        for(unsigned profile=1;profile<15;++profile){StartupSuite legacy;assert(legacy.configure(profile,30) && !legacy.first_match_start());}
        StartupSuite repeated_recording_suite;assert(repeated_recording_suite.configure(14,30) && repeated_recording_suite.mask()==255);
        StartupSuite recording_suite;assert(recording_suite.configure(13,30) && recording_suite.mask()==255 && recording_suite.seal(255,0,0));
        StartupSuite old_suite;assert(old_suite.configure(12,30) && !old_suite.receive(recording_suite.outgoing()));
        StartupSuite replay_suite;assert(replay_suite.configure(12,30) && replay_suite.mask()==255 && replay_suite.seal(63,0,192));
        StartupSuite baseline_suite,immediate_suite;
        assert(baseline_suite.configure(10,60) && immediate_suite.configure(9,60));
        assert(baseline_suite.seal(255,0,0) && immediate_suite.seal(255,0,0));
        assert(!baseline_suite.receive(immediate_suite.outgoing()));
        using H=NativeHoldRehearsal<HoldRehearsalFixture>;
        H immediate;assert(immediate.request(0,601,661,1000,0));
        assert(!immediate.run(1,2,3,0,601,660,110,true));
        assert(immediate.state==H::released && immediate.services==0 && immediate.held_ms==0);
        H gated;assert(gated.request(0,601,661,1000,0));
        assert(gated.run(1,2,3,0,601,660,110,true,false));
        assert(gated.state==H::holding);
        assert(!gated.run(1,2,3,0,601,660,111,true,true));
        assert(gated.state==H::released && gated.held_ms==1);
        H expired;assert(expired.request(0,601,661,1000,0));
        assert(expired.run(1,2,3,0,601,660,110,true,false));
        assert(expired.run(1,2,3,0,601,660,1110,true,false));
        assert(expired.state==H::restart_required);
        H invalid;assert(!invalid.request(0,601,661,1000,1000));
        H incomplete;assert(incomplete.request(0,604,722,2000));
        assert(incomplete.target_clock()==604);
        assert(!incomplete.run(1,2,3,0,604,721,110,false,false));
        assert(incomplete.state==H::rejected && incomplete.services==0 && incomplete.reason==H::invalid_context);
        H hold;assert(hold.request(0,601,661));assert(!hold.request(0,601,661));
        assert(!hold.run(1,2,3,0,600,659,100,true));
        assert(hold.run(1,2,3,0,601,660,110,true) && hold.state==H::holding);
        assert(!hold.run(1,9,3,0,601,660,120,true));
        assert(hold.run(1,2,3,0,601,660,209,true));
        assert(!hold.run(1,2,3,0,601,660,210,true) && hold.state==H::released && hold.services==2 && hold.held_ms==100 && hold.reason==H::none);
        H wrong;assert(wrong.request(0,601,661));
        assert(!wrong.run(1,2,3,0,601,659,110,true) && wrong.state==H::rejected && wrong.reason==H::battle_mismatch && wrong.observed_battle==659);
        H failure;assert(failure.request(0,601,661));HoldRehearsalFixture::service_ok=false;
        assert(failure.run(1,2,3,0,601,660,110,true) && failure.state==H::restart_required);
        assert(failure.run(1,2,3,0,601,660,120,true));HoldRehearsalFixture::service_ok=true;
        H waiting;assert(waiting.request(0,604,664,2000));
        assert(waiting.run(1,2,3,0,604,663,100,true,false));
        assert(waiting.run(1,2,3,0,604,663,201,true,false) && waiting.state==H::holding);
        assert(!waiting.run(1,2,3,0,604,663,202,true,true) && waiting.state==H::released);
        H cancelled;assert(cancelled.request(0,604,664,2000));
        assert(cancelled.run(1,2,3,0,604,663,100,true,false));
        assert(!cancelled.run(1,2,3,0,604,663,101,true,false,true));
        assert(cancelled.state==H::rejected && cancelled.services==1);
        assert(!cancelled.run(1,2,3,0,605,664,102,true,false));
        assert(H::clean_cancel(true,0,false,false,false));
        assert(!H::clean_cancel(false,0,false,false,false));
        assert(!H::clean_cancel(true,1,false,false,false));
        assert(!H::clean_cancel(true,0,true,false,false));
        assert(!H::clean_cancel(true,0,false,true,false));
        assert(!H::clean_cancel(true,0,false,false,true));
        H peer_stop;assert(peer_stop.request(0,604,664,2000));
        assert(peer_stop.run(1,2,3,0,604,663,100,true,false));
        assert(!peer_stop.run(1,2,3,0,604,663,110,true,false,H::clean_cancel(true,0,false,false,false)));
        assert(peer_stop.state==H::rejected && peer_stop.reason==H::neutral_cancel);
        H stale_stop;assert(stale_stop.request(0,604,664,2000));
        assert(stale_stop.run(1,2,3,0,604,663,100,true,false));
        assert(stale_stop.run(1,2,3,0,604,663,110,false,false,true));
        assert(stale_stop.state==H::restart_required); // cancellation never overrides invalid native context
        H invalid_cancel;assert(invalid_cancel.request(0,604,664,2000));
        assert(invalid_cancel.run(1,2,3,0,604,663,100,true,false));
        assert(invalid_cancel.run(1,2,3,1,604,663,101,true,false,true));
        assert(invalid_cancel.state==H::restart_required && invalid_cancel.reason==H::identity_changed);
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
        StartupBoundaryPairs repeated;
        for(unsigned i=0;i<120;++i){for(unsigned j=0;j<5;++j)repeated.clock(0,i+1);repeated.battle(i+546);}
        assert(repeated.duplicates==480 && repeated.overwritten==0 && repeated.discontinuities==0);
        StartupBoundaryAgreement accepted;assert(accepted.begin(repeated,1));
        for(bool change_identity:{false,true}){
            StartupBoundaryPairs changed_pair;
            changed_pair.clock(0,1);changed_pair.clock(change_identity?1:0,change_identity?1:2);changed_pair.battle(546);
            assert(changed_pair.overwritten==1 && changed_pair.duplicates==0);
            for(unsigned i=1;i<120;++i){changed_pair.clock(change_identity?1:0,i+(change_identity?1:2));changed_pair.battle(i+546);}
            StartupBoundaryAgreement denied;assert(!denied.begin(changed_pair,1));
        }
        StartupBoundaryPairs empty;StartupBoundaryAgreement invalid;assert(!invalid.begin(empty,1));
        FightStartupPairs gated;
        for(unsigned i=0;i<180;++i){gated.clock(1,i);gated.battle(i+221,false,1,0x3E87378,0x384040);}
        assert(gated.pairs.count==0 && gated.waiting==180);
        gated.clock(1,180);gated.battle(401,true,3,0x3E873D0,0x384EE0);
        gated.clock(1,181);gated.battle(402,true,3,0x3E873D0,0x384EE0);
        assert(gated.pairs.count==2 && gated.pairs.discontinuities==0);
        StartupBoundaryAgreement gatedAgreement;assert(gatedAgreement.begin(gated.pairs,1,true));
        assert(gatedAgreement.target_clock()==240 && gatedAgreement.target_battle()==461);
        gated.clock(2,0);gated.battle(0,false,4,0x3E873D0,0x384EE0);
        assert(gated.pairs.count==2 && gated.pairs.rows[0].clock==180);
        sc6::research::CombatRuntimeStatus watchdog;
        watchdog.active=true;watchdog.activated_at=100;
        assert(!watchdog.first_update_expired(99) && !watchdog.first_update_expired(10099));
        assert(watchdog.first_update_expired(10100));watchdog.updates=1;
        assert(!watchdog.first_update_expired(20000));watchdog.updates=0;watchdog.active=false;
        assert(!watchdog.first_update_expired(20000));

        StartupBoundaryPairs early;early.clock(1,10);early.battle(20);
        StartupBoundaryAgreement earlyA,earlyB;assert(!earlyA.begin(early,1,true));
        early.clock(1,11);early.battle(21);
        assert(earlyA.begin(early,1,true) && earlyB.begin(early,2,true));
        assert(earlyA.target_clock()==70 && earlyA.target_battle()==80);
        assert(earlyA.receive(earlyB.outgoing()));assert(earlyB.receive(earlyA.outgoing()));
        assert(earlyA.receive(earlyB.outgoing()) && earlyA.ready() && earlyB.ready());
        const auto staleAck=earlyB.outgoing();
        assert(earlyA.refresh(75) && earlyA.target_clock()>75 && !earlyA.ready());
        assert(earlyA.receive(staleAck) && !earlyA.ready());
        assert(earlyB.newer(earlyA.outgoing()));
        assert(earlyB.receive(earlyA.outgoing()) && !earlyB.ready());
        assert(earlyA.receive(earlyB.outgoing()) && earlyA.ready());
        assert(earlyB.receive(earlyA.outgoing()) && earlyB.ready());
        assert(earlyB.refresh(200));assert(earlyA.refresh(140));
        assert(earlyA.receive(earlyB.outgoing()));assert(earlyB.receive(earlyA.outgoing()));
        assert(earlyA.receive(earlyB.outgoing()) && earlyA.ready() && earlyB.ready());
        assert(earlyA.target_clock()==earlyB.target_clock());
        assert(earlyA.target_battle()-earlyA.target_clock()==10);
        assert(!earlyA.refresh(UINT32_MAX));
        auto badRevision=earlyB.outgoing();++badRevision[5];
        assert(!earlyA.receive(badRevision) && earlyA.failed());
        StartupBoundaryAgreement legacy;assert(!legacy.begin(early,1));
        StartupBoundaryPairs discontinuous;discontinuous.clock(1,10);discontinuous.battle(20);
        discontinuous.clock(1,12);discontinuous.battle(22);
        StartupBoundaryAgreement rejected;assert(!rejected.begin(discontinuous,1,true));

    }
    {
        StartupBoundaryPairs pairs;
        pairs.battle(61);assert(pairs.unpaired==1 && pairs.count==0);
        pairs.clock(0,1);pairs.clock(0,1);pairs.battle(62);
        assert(pairs.overwritten==0 && pairs.duplicates==1 && pairs.count==1 && pairs.rows[0].battle==62);
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
        {
            auto guard=std::make_unique<CombatResourcePreparation<PreparationResourcesFixture>>();
            guard->enable_cycles(false,true,100);
            assert(guard->cycles->arm(1,1,100,200,1));
            auto* cycle=guard->cycles->current();cycle->entry.state=3;
            RollbackBoundaryKey oldKey{1,2,3,4,5,200,1,GetCurrentThreadId()};
            auto nextKey=oldKey;nextKey.round=5;nextKey.frame=1;
            guard->owner_cycle_=1;guard->cycle_identity_=oldKey;
            cycle->runner.retired=true;cycle->runner.reference_verified=true;cycle->runner.no_divergence=true;
            cycle->runner.stop_outcome=1;
            guard->replay_boundary(nextKey,0,0,nullptr);
            assert(cycle->runner.retired && !cycle->runner.failed && cycle->runner.failure_reason==0);
            assert(cycle->runner.reference_verified && cycle->runner.no_divergence && cycle->runner.stop_outcome==1);
            assert(guard->cycle_identity_.same_round(oldKey));
            // Still-active old-round work must be rejected before any native call.
            cycle->runner.retired=false;cycle->runner.reference_verified=false;cycle->runner.no_divergence=false;
            guard->replay_boundary(nextKey,0,0,nullptr);
            assert(cycle->runner.failed && cycle->runner.failure_reason==8 && cycle->runner.retired);
        }
        CombatResourcePreparation<ColdPreparationFixture> cold;
        StartupSuite first_match_suite;assert(first_match_suite.configure(15,30));
        assert(cold.start(1,first_match_suite.first_match_start()));ColdPreparationFixture::available=true;
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
        assert(!preparation.stop_requested());
        preparation.request_stop();assert(preparation.stop_requested() && PreparationResourcesFixture::releases==0);
        observed.clock.frame=13;preparation.clock_entry(observed,125,100);assert(preparation.clock_frame==1);
        preparation.frame(13,true);assert(preparation.retired.load() && !preparation.failed.load());
        preparation.frame(14,true);assert(PreparationResourcesFixture::releases==1);
        // Exercise owner-thread cleanup and ensure cancellation details survive classification.
        for(unsigned variant=0;variant<8;++variant){
            auto end=std::make_unique<CombatResourcePreparation<MatchEndResourcesFixture>>();
            assert(end->start(1));MatchEndResourcesFixture::available=true;
            end->frame(10,true);
            end->retained_replay=std::make_unique<RetainedObjectReplay<>>(true,true,true);
            end->retained_replay_enabled=true;
            end->retained_replay->completed_cycles=2;end->retained_replay->waiting_round_=true;
            MatchEndResourcesFixture::observation={0x3E87428,0x2D2BC0,GetCurrentThreadId(),0x301590};
            MatchEndResourcesFixture::stop_ok=variant!=3;
            if(variant==1)MatchEndResourcesFixture::observation.vtable=0x3E873D0;
            if(variant==2)end->retained_replay->waiting_round_=false;
            if(variant==4)end->failed=true;
            if(variant==7)MatchEndResourcesFixture::observation={0x3E87530,0x387530,GetCurrentThreadId(),0x301470};
            if(variant==5 || variant==6){
                MatchEndResourcesFixture::end_cancellation.reason=variant==5?7:8;
                MatchEndResourcesFixture::cancel=true;
            }
            // Test both proactive match end and native-clear cancellation.
            end->frame(20,true);
            if(variant==0 || variant==5)assert(end->match_end_clean && end->retired && !end->failed && end->stop_requested());
            else assert(!end->match_end_clean);
            if(variant==3)assert(end->failed && !end->retired);
            if(variant==5)assert(end->cancellation==7);
            if(variant==6)assert(end->failed && end->cancellation==8);
            if(variant==7){
                assert(!end->stop_requested() && !end->retired && !end->failed);
                MatchEndResourcesFixture::observation={0x3E87428,0x2D2BC0,GetCurrentThreadId(),0x301590};
                end->frame(30,true);assert(end->match_end_clean && end->retired);
            }
        }
        MatchEndResourcesFixture::stop_ok=true;
        for(unsigned variant=0;variant<6;++variant){
            auto end=std::make_unique<CombatResourcePreparation<MatchEndResourcesFixture>>();
            assert(end->start(1));MatchEndResourcesFixture::available=true;end->frame(10,true);
            end->live_prediction_enabled=true;
            MatchEndResourcesFixture::observation={0x3E87428,0x2D2BC0,GetCurrentThreadId(),0x301590};
            if(variant==1)MatchEndResourcesFixture::observation={0x3E87530,0x387530,GetCurrentThreadId(),0x301470};
            if(variant==3)end->failed=true;
            MatchEndResourcesFixture::stop_ok=variant!=4;
            const bool requested=end->finish_live_match(variant==5?0:GetCurrentThreadId(),variant!=2);
            assert(requested==(variant==0 || variant==4));
            if(requested){end->frame(20,false);assert(end->match_end_clean==(variant==0));assert(end->failed==(variant==4));}
            else assert(!end->match_end_clean && !end->match_end_requested);
        }
        MatchEndResourcesFixture::stop_ok=true;
        CombatResourcePreparation<PreparationResourcesFixture> cancelled;
        assert(cancelled.start(1));PreparationResourcesFixture::cancel=true;
        cancelled.frame(15,true);assert(cancelled.failed.load() && cancelled.retired.load() && !cancelled.ready.load());
    }
    {
        StartupBoundaryAgreement earlyA,earlyB;
        assert(earlyA.begin_first_fight(7,1,42,1) && earlyB.begin_first_fight(7,1,42,2));
        assert(!earlyA.ready() && earlyA.first_fight());
        assert(earlyA.receive(earlyB.outgoing()) && earlyB.receive(earlyA.outgoing()));
        assert(earlyA.receive(earlyB.outgoing()) && earlyA.ready() && earlyB.ready());
        assert(!earlyA.runtime_rejection(0,0,false,false,7,7,1000));
        assert(earlyA.runtime_rejection(0,0,false,false,8,7,1000)==2);
        for(unsigned index:{1u,2u,3u,4u,5u,6u,7u,8u,9u}){
            StartupBoundaryAgreement guard;assert(guard.begin_first_fight(7,1,42,1));
            auto bad=earlyB.outgoing();bad[index]=index==7?2:bad[index]+1;
            assert(!guard.receive(bad) && guard.failed());
        }
        StartupBoundaryPairs fallbackPairs;
        fallbackPairs.rows[0]={7,100,200};fallbackPairs.count=2;
        StartupBoundaryAgreement late,early;
        assert(late.begin(fallbackPairs,2,true) && early.begin_first_fight(7,1,42,1));
        assert(late.receive(early.outgoing()) && !late.ready());
        assert(early.receive(late.outgoing()) && early.fallback_requested() && !early.ready());
        assert(early.fallback(fallbackPairs,1) && !early.first_fight());
        assert(late.receive(early.outgoing()) && early.receive(late.outgoing()) && late.receive(early.outgoing()));
        assert(late.ready() && early.ready() && early.target_clock()==160);
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
        auto next_round=config;
        ++next_round.identity.round;++next_round.identity.event;
        next_round.offer.round=next_round.identity.round;next_round.offer.event=next_round.identity.event;
        ++next_round.offer.ticket;
        assert(NativeCombatArming::valid_successor(config,next_round,address));
        auto stale_round=next_round;stale_round.offer.ticket=config.offer.ticket;
        assert(!NativeCombatArming::valid_successor(config,stale_round,address));
        stale_round=next_round;stale_round.identity.event=stale_round.offer.event=config.identity.event;
        assert(!NativeCombatArming::valid_successor(config,stale_round,address));
        stale_round=next_round;--stale_round.identity.generation;
        assert(!NativeCombatArming::valid_successor(config,stale_round,address));
        stale_round=next_round;stale_round.identity.slot=stale_round.offer.slot=2;
        assert(!NativeCombatArming::valid_successor(config,stale_round,address));
        assert(!NativeCombatArming::valid_successor(config,next_round,address+16));
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
        assert(waiting.rejection()==5 && waiting.observed_frame()==101);
        // Republish for withdrawal test; no resources or native hold were touched.
        // The callback is still attached until explicitly withdrawn.
        unsigned supplied=0;
        NativeArmingDispatch withdrawing(config,source,*shared,22,GetTickCount64()+1000);
        *reinterpret_cast<unsigned*>(address+0x3A4)=99;
        assert(withdrawing.queue());
        const auto* loadedCallback=native_clock_hook::preparation_poll.load();
        assert(withdrawing.withdraw_unstarted() && !native_clock_hook::preparation_poll.load());
        *reinterpret_cast<unsigned*>(address+0x3A4)=100;
        loadedCallback->poll(loadedCallback->context,address);
        assert(withdrawing.state()==NativeArmingDispatch::State::rejected && !combat_routes.load());
        NativeArmingDispatch missed(config,source,*shared,22,GetTickCount64()+1000);
        assert(missed.queue());*reinterpret_cast<unsigned*>(address+0x3A4)=101;
        native_clock_hook::bridge(address);assert(missed.rejection()==5);
        assert(missed.withdraw_unstarted() && !native_clock_hook::preparation_poll.load());
        NativeArmingDispatch wrongIdentity(config,source,*shared,22,GetTickCount64()+1000);
        assert(wrongIdentity.queue());*reinterpret_cast<unsigned*>(address+0x3A0)=config.clock_identity+1;
        native_clock_hook::bridge(address);assert(wrongIdentity.rejection()==4 && !wrongIdentity.withdraw_unstarted());
        native_clock_hook::preparation_poll=nullptr;*reinterpret_cast<unsigned*>(address+0x3A0)=config.clock_identity;
        NativeArmingDispatch wrongThread(config,source,*shared,22,GetTickCount64()+1000);
        assert(wrongThread.queue());
        std::thread foreign([&]{native_clock_hook::bridge(address);});foreign.join();
        assert(wrongThread.rejection()==2 && !wrongThread.withdraw_unstarted());
        native_clock_hook::preparation_poll=nullptr;
        NativeArmingDispatch deferred(config,source,*shared,22,GetTickCount64()+1000);
        unsigned rearm_calls=0;
        auto rearm_provider=[](void* p,uintptr_t,NativeCombatSession::Config&,ReplayDependencySource&){
            ++*static_cast<unsigned*>(p);return NativeCombatArming::RearmResult::waiting;
        };
        assert(!deferred.automatic_rearm(nullptr,rearm_provider));
        assert(!deferred.automatic_rearm(&rearm_calls,nullptr));
        assert(deferred.automatic_rearm(&rearm_calls,rearm_provider));
        assert(!deferred.automatic_rearm(&rearm_calls,rearm_provider));
        auto provider=[](void* p){++*static_cast<unsigned*>(p);return ReplayDependencySource{};};
        assert(deferred.source_on_owner(&supplied,provider));
        *reinterpret_cast<unsigned*>(address+0x3A4)=99;
        assert(deferred.queue() && supplied==0);
        assert(!deferred.automatic_rearm(&rearm_calls,rearm_provider));
        assert(!deferred.source_on_owner(&supplied,provider));
        native_clock_hook::bridge(address);assert(supplied==0);
        *reinterpret_cast<unsigned*>(address+0x3A4)=100;
        native_clock_hook::bridge(address);assert(supplied==1 && deferred.state()==NativeArmingDispatch::State::rejected);
        assert(rearm_calls==0); // Rejected preparation cannot acquire a successor.
        assert(deferred.rejection()==6 && !deferred.withdraw_unstarted());
        assert(!native_clock_hook::start_hold.load() && !combat_routes.load());
        native_clock_hook::preparation_poll=nullptr;
        NativeArmingDispatch cancelled(config,source,*shared,22,GetTickCount64()+1000);
        assert(cancelled.source_on_owner(&supplied,provider) && cancelled.queue());cancelled.cancel();
        assert(!cancelled.automatic_rearm(&rearm_calls,rearm_provider));
        native_clock_hook::bridge(address);assert(supplied==1 && cancelled.state()==NativeArmingDispatch::State::rejected);
        native_clock_hook::preparation_poll=nullptr;
        // Intro preparation runs native-owned until the first freshly verified
        // fighting entry. No wall-clock guess or cached readiness grants a hold.
        auto* memory=VirtualAlloc(nullptr,0x4900000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(memory);
        const auto base=reinterpret_cast<uintptr_t>(memory),clockObject=base+0x1000,mode=base+0x7000;
        auto word=[&](uintptr_t address,unsigned value){*reinterpret_cast<unsigned*>(address)=value;};
        *reinterpret_cast<uintptr_t*>(clockObject)=base+0x3283578;
        *reinterpret_cast<uintptr_t*>(base+0x3283578+0x620)=base+0x3FDB80;
        *reinterpret_cast<uintptr_t*>(base+0x3283578+0x628)=base+0x3E9EE0;
        *reinterpret_cast<uintptr_t*>(base+0x3283578+0x630)=base+0x3E2000;
        word(clockObject+0x398,2);word(clockObject+0x39C,1);word(clockObject+0x43FC,3);word(clockObject+0x4400,2);
        *reinterpret_cast<unsigned char*>(clockObject+0x4404)=1;*reinterpret_cast<unsigned char*>(clockObject+0x4424)=2;
        word(clockObject+0x3A0,7);word(clockObject+0x3A4,100);word(base+0x470D0C4,500);
        *reinterpret_cast<uintptr_t*>(base+0x4843ED0)=mode;
        *reinterpret_cast<uintptr_t*>(mode)=base+0x3E87378;
        *reinterpret_cast<uintptr_t*>(base+0x3E87378+0x10)=base+0x384040;
        *reinterpret_cast<uintptr_t*>(base+0x3E873D0+0x10)=base+0x384EE0;
        assert(sc6::RoundFightReadiness::read(base,4,3,500).intro(4,3,GetCurrentThreadId()));
        word(base+0x48463F8,1);
        assert(!sc6::RoundFightReadiness::read(base,4,3,500).intro(4,3,GetCurrentThreadId()));
        word(base+0x48463F8,0);*reinterpret_cast<unsigned short*>(base+0x4846408)=1;
        assert(!sc6::RoundFightReadiness::read(base,4,3,500).intro(4,3,GetCurrentThreadId()));
        *reinterpret_cast<unsigned short*>(base+0x4846408)=0;
        const auto oldIdentity=sc6::round_event::identity;const auto oldCounter=sc6::round_event::event_counter;
        sc6::round_event::identity={};sc6::round_event::identity.known=true;
        sc6::round_event::identity.round=4;sc6::round_event::identity.boundary=3;sc6::round_event::event_counter=5;
        auto earlyConfig=config;earlyConfig.base=base;earlyConfig.clock_object=clockObject;earlyConfig.caller=base+1;
        native_clock_hook::installed_base=base;
        for(unsigned variant=0;variant<3;++variant){
            *reinterpret_cast<uintptr_t*>(mode)=base+0x3E87378;
            sc6::round_event::event_counter=5;
            NativeArmingDispatch first(earlyConfig,source,*shared,22,GetTickCount64()+60000);
            unsigned requests=0;
            assert(first.first_fight() && first.source_on_owner(&requests,provider) && first.queue());
            const auto* callback=native_clock_hook::preparation_poll.load();
            assert(!first.first_fight());
            for(unsigned frame=101;frame<110;++frame){word(clockObject+0x3A4,frame);callback->poll(callback->context,clockObject);}
            assert(first.state()==NativeArmingDispatch::State::waiting && !requests);
            *reinterpret_cast<uintptr_t*>(mode)=base+0x3E873D0;
            word(base+0x48463F8,1);callback->poll(callback->context,clockObject);assert(!requests);
            word(base+0x48463F8,0);*reinterpret_cast<unsigned short*>(base+0x4846408)=1;
            callback->poll(callback->context,clockObject);assert(!requests);
            *reinterpret_cast<unsigned short*>(base+0x4846408)=0;
            if(variant==1)sc6::round_event::event_counter=6;
            if(variant==2)word(clockObject+0x39C,2);
            callback->poll(callback->context,clockObject);
            assert(first.state()==NativeArmingDispatch::State::rejected);
            assert(first.rejection()==(variant==0?6u:variant==1?10u:12u));
            assert(requests==(variant==0?1u:0u));
            assert(!native_clock_hook::start_hold.load() && !combat_routes.load());
            native_clock_hook::preparation_poll=nullptr;word(clockObject+0x39C,1);
        }
        // Exercise the production first-intro callback with real reservation
        // publication. Native memory is a fixture; no simulation runs here.
        {
            struct Control {uintptr_t table{};LONG strong{1},weak{1};bool ready{true};} control;
            GeneratedSnapshotDependency dependency;
            assert(dependency.acquire(1,2,reinterpret_cast<uintptr_t>(&control),40,[](uintptr_t){},[](uintptr_t){}));
            ReplayDependencySource preparedSource{&control,[](void*,uint32_t){return true;},&dependency,
                [](void* p){return static_cast<Control*>(p)->ready;}};
            for(unsigned fault=0;fault<4;++fault){
                sc6::round_event::event_counter=5;
                *reinterpret_cast<uintptr_t*>(mode)=base+0x3E87378;
                word(mode+8,10);word(mode+12,120);word(base+0x470D0C4,500);word(clockObject+0x3A4,100);
                NativeArmingDispatch first(earlyConfig,preparedSource,*shared,22,GetTickCount64()+60000);
                assert(first.first_fight() && first.queue());
                const auto* callback=native_clock_hook::preparation_poll.load();
                callback->poll(callback->context,clockObject);
                assert(!combat_round_preparation.load() && !native_clock_hook::start_hold.load());
                word(mode+8,11);word(base+0x470D0C4,501);word(clockObject+0x3A4,101);
                callback->poll(callback->context,clockObject);
                const auto* prep=combat_round_preparation.load();assert(prep && prep->reserved());
                auto offer=CombatStartPacket::decode(prep->outgoing(),112);assert(offer && offer->offer.first_frame==610);
                assert(first.state()==NativeArmingDispatch::State::waiting && !combat_routes.load() && !native_clock_hook::start_hold.load());
                const auto* barrier=round_transition_boundary_guard.load();assert(barrier && barrier->before_step(barrier->context));
                if(fault==0){word(mode+12,121);} // Published projection must not change.
                if(fault==1){word(base+0x470D0C4,611);}
                if(fault==2){++sc6::round_event::event_counter;}
                if(fault==3){word(base+0x470D0C4,610);} // Still Start at promised Fight frame.
                callback->poll(callback->context,clockObject);
                assert(first.state()==NativeArmingDispatch::State::rejected);
                assert(first.rejection()==(fault==2?10u:14u));
                word(base+0x470D0C4,610);assert(!barrier->before_step(barrier->context));
                native_clock_hook::preparation_poll=nullptr;combat_round_preparation=nullptr;round_transition_boundary_guard=nullptr;
            }
            assert(dependency.reset());
        }
        sc6::round_event::identity=oldIdentity;sc6::round_event::event_counter=oldCounter;
        VirtualFree(memory,0,MEM_RELEASE);
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
        using Ingress=CombatStartIngress;
        const auto early=CombatStartPacket{a,1,0,{1000000,0,0,0}}.encode();
        assert(Ingress::receive(nullptr,true,true,11,11,2,1,77,early,early.size())==Ingress::Result::deferred);
        assert(Ingress::receive(nullptr,true,true,99,11,2,1,77,early,early.size())==Ingress::Result::rejected);
        assert(Ingress::receive(nullptr,true,false,11,11,2,1,77,early,early.size())==Ingress::Result::rejected);
        assert(Ingress::receive(nullptr,false,true,11,11,2,1,77,early,early.size())==Ingress::Result::rejected);
        assert(Ingress::receive(nullptr,true,true,11,11,2,9,77,early,early.size())==Ingress::Result::rejected);
        assert(Ingress::receive(nullptr,true,true,11,11,2,1,78,early,early.size())==Ingress::Result::rejected);
        assert(Ingress::receive(nullptr,true,true,11,11,2,1,77,early,79)==Ingress::Result::rejected);
        CombatStartExchange delayed;
        assert(delayed.begin(11,b));
        assert(Ingress::receive(&delayed,true,true,11,11,2,1,77,early,early.size())==Ingress::Result::delivered);
        assert(!delayed.consume()); // retransmission is accepted, not permission to activate
        // Simulate independent clock epochs, preparation skew, and a 66ms RTT.
        // No activation until a reliable proposal/ack/commit and the local deadline.
        CombatStartExchange left,right;CombatStartMailbox mailbox(GetCurrentThreadId()),other(GetCurrentThreadId());
        assert(left.attach(mailbox) && right.attach(other));assert(left.begin(22,a) && right.begin(11,b));
        assert(left.report_local_hold(a.ticket));assert(!left.report_local_hold(a.ticket));
        struct Transit {uint64_t at;bool to_left;CombatStartPacket::Bytes bytes;};
        std::vector<Transit> transit;
        CombatStartSendCadence lc,rc;CombatStartAgreement transferred,rt;
        uint64_t left_release=0,right_release=0;constexpr uint64_t offset=9000000;
        for(uint64_t time=1000000;time<2000000;time+=1000){
            if(time==1030000)assert(right.report_local_hold(b.ticket));
            for(size_t n=0;n<transit.size();){
                if(transit[n].at>time){++n;continue;}
                const auto packet=transit[n];transit.erase(transit.begin()+n);
                assert(packet.to_left?left.receive(22,true,packet.bytes,packet.bytes.size(),time):
                    right.receive(11,true,packet.bytes,packet.bytes.size(),time+offset));
            }
            auto l=left.outgoing(time),r=right.outgoing(time+offset);
            if(lc.due(l,time/1000,left.completed())){left.sent(l,time);transit.push_back({time+33000,false,l});}
            if(rc.due(r,time/1000,right.completed())){right.sent(r,time+offset);transit.push_back({time+33000,true,r});}
            if(mailbox.take(transferred,time))left_release=time;
            if(other.take(rt,time+offset))right_release=time;
            if(left_release && right_release)break;
        }
        assert(left_release && left_release==right_release);
        assert(transferred.activation_ready() && transferred.consume(a) && rt.consume(b));
        assert(!mailbox.take(transferred));assert(left.consume() && right.consume());assert(!left.consume());
        auto first=CombatStartPacket{a,1,0,{1000000,0,0,0}}.encode();
        for(unsigned field:{0u,4u,8u,56u,64u}){auto corrupt=first;corrupt[field]=255;assert(!CombatStartPacket::decode(corrupt,corrupt.size()));}
        auto roundtrip=CombatStartPacket::decode(first,first.size());assert(roundtrip && roundtrip->offer==a);
        auto next_a=a;next_a.round=4;next_a.event=5;next_a.first_frame=0;++next_a.ticket;
        CombatStartExchange next_left;CombatStartMailbox next_mailbox(GetCurrentThreadId());assert(next_left.attach(next_mailbox));
        assert(next_left.begin(22,next_a,&left));
        auto old=CombatStartPacket{b,6,a.ticket,{2000000,0,0,2000000}}.encode();
        assert(next_left.receive(22,true,old,old.size()) && next_left.retired_packets()==1);
        auto wrong_old=CombatStartPacket{b,3,a.ticket+1}.encode();
        assert(!next_left.receive(22,true,wrong_old,wrong_old.size()));
        CombatStartExchange reused;assert(!reused.begin(22,a,&left));
        auto next_b=b;next_b.round=4;next_b.event=5;next_b.first_frame=0;++next_b.ticket;next_b.delay=2;
        CombatStartExchange next_right;CombatStartMailbox next_other(GetCurrentThreadId());
        assert(next_right.attach(next_other) && next_right.begin(11,next_b,&right));
        assert(next_left.report_local_hold(next_a.ticket) && next_right.report_local_hold(next_b.ticket));
        transit.clear();lc={};rc={};left_release=right_release=0;
        for(uint64_t time=2000000;time<3000000;time+=1000){
            for(size_t n=0;n<transit.size();){
                if(transit[n].at>time){++n;continue;}
                const auto packet=transit[n];transit.erase(transit.begin()+n);
                assert(packet.to_left?next_left.receive(22,true,packet.bytes,packet.bytes.size(),time):
                    next_right.receive(11,true,packet.bytes,packet.bytes.size(),time+offset));
            }
            auto l=next_left.outgoing(time),r=next_right.outgoing(time+offset);
            if(lc.due(l,time/1000,next_left.completed())){next_left.sent(l,time);transit.push_back({time+33000,false,l});}
            if(rc.due(r,time/1000,next_right.completed())){next_right.sent(r,time+offset);transit.push_back({time+33000,true,r});}
            if(next_mailbox.take(transferred,time)){left_release=time;assert(transferred.agreed_delay()==2);}
            if(next_other.take(rt,time+offset))right_release=time;
            if(left_release && right_release)break;
        }
        assert(left_release && left_release==right_release); // new round independently measures and agrees
    }
    {
        CombatStartAgreement::Offer local{1,2,100,77,3,4,500,1,0,3,7200};
        auto peer=local;peer.sender=2;peer.recipient=1;peer.ticket=200;peer.slot=2;
        CombatStartAgreement agreed;assert(agreed.begin(local));assert(!agreed.consume(local));
        assert(agreed.receive(peer) && agreed.receive(peer));assert(!agreed.consume(local));
        assert(agreed.acknowledge(2,100,200));assert(agreed.consume(local));assert(!agreed.consume(local));
        for(unsigned field=0;field<5;++field){CombatStartAgreement mismatch;assert(mismatch.begin(local));auto bad=peer;
            switch(field){case 0:++bad.build;break;case 1:++bad.first_frame;break;case 2:bad.delay=3;break;case 3:bad.slot=1;break;case 4:++bad.round;break;}
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
        PostBattleContinuation incomplete;
        assert(!session->retire_after_continuation(incomplete) && !session->retired());
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
        auto scratch=std::make_unique<NativeHistoryRestore<>>();
        assert(scratch->prepare_for_reuse());
        auto& w=scratch->diagnostic();auto* snapshot=w.current.data.get();auto* probe=w.probe.get();
        scratch->attempted=true;scratch->loaded=true;scratch->depth=1;
        scratch->stage=NativeHistoryRestore<>::Stage::restored;
        assert(!scratch->prepare_for_reuse()); // incomplete work cannot be recycled
        w.attempted=w.prepared=w.verified=w.native_payload_verified=w.completion.verified=true;
        probe->restore_invoked=probe->restore_completed=probe->delayed_loaded=probe->camera_restore_verified=true;
        w.current.effects=std::make_unique<NativeEffectSession::Binding>();
        assert(!scratch->prepare_for_reuse()); // no retained binding may be discarded
        w.current.effects.reset();probe->fault=true;assert(!scratch->prepare_for_reuse());probe->fault=false;
        assert(scratch->prepare_for_reuse());
        assert(w.current.data.get()==snapshot && w.probe.get()==probe);
        assert(!w.attempted && !w.prepared && !w.verified && !probe->restore_invoked);
        assert(!scratch->attempted && !scratch->loaded && !scratch->depth);
        assert(probe->saved.stream.failures==0 && probe->camera_readiness.reason==0);
        scratch->attempted=true;scratch->stage=NativeHistoryRestore<>::Stage::prepare;
        assert(!scratch->prepare_for_reuse()); // a failed attempt remains one-shot
        StorageHistory history;ReplayDependencySource source;
        RollbackBoundaryKey identity{1,2,3,4,5,100,1,GetCurrentThreadId()};
        const auto first_samples=native_startup_timing.buckets[NativeStartupTiming::first_capture].calls.load();
        const auto ring_samples=native_startup_timing.buckets[NativeStartupTiming::initial_ring_capture].calls.load();
        NativeSchedulerStorage<StorageHistory,StorageRestore> storage(history,source,1,identity);
        decltype(storage)::Snapshot first,current;std::array<std::byte,40> bytes{};
        for(unsigned i=100;i<103;++i){assert(storage.begin_boundary(i,{}));assert(storage.capture(current,i,bytes));if(i==100)first=current;}
        assert(storage.begin_boundary(103,{}));assert(storage.restore(first));assert(history.frames.empty());
        for(unsigned i=100;i<=103;++i)assert(storage.capture(current,i,bytes));
        assert((history.frames==std::vector<unsigned>{100,101,102,103}));
        auto replacement=first;
        // A second successful transaction uses the same allocated Restore.
        history.frames.clear(); const auto allocated=StorageRestore::constructions;
        for(unsigned i=100;i<103;++i){assert(storage.begin_boundary(i,{}));assert(storage.capture(current,i,bytes));if(i==100)replacement=current;}
        assert(storage.begin_boundary(103,{}));assert(storage.restore(replacement));
        assert(StorageRestore::constructions==allocated);
        assert(!storage.restore(first) && storage.failed()); // replaced handle cannot alias recaptured state
        assert(storage.timing.buckets[NativeRollbackTiming::capture].calls==12);
        assert(native_startup_timing.buckets[NativeStartupTiming::first_capture].calls==first_samples+1);
        assert(native_startup_timing.buckets[NativeStartupTiming::initial_ring_capture].calls==ring_samples+3);
        assert(storage.timing.buckets[NativeRollbackTiming::restore].calls==3);
        assert(storage.timing.buckets[NativeRollbackTiming::cleanup].calls==2);
        assert(storage.timing.buckets[NativeRollbackTiming::workspace].calls==2);
        assert(!NativeRollbackTiming::active);
        StorageHistory reuseHistory;auto reusable=std::make_unique<decltype(storage)>(reuseHistory,source,1,identity);
        assert(!reusable->take_retired_workspace(false));
        const auto before_rearm=StorageRestore::constructions;
        auto workspace=reusable->take_retired_workspace(true);assert(workspace);
        assert(!reusable->begin_boundary(100,{}));
        auto successor=std::make_unique<decltype(storage)>(reuseHistory,source,1,identity,false,std::move(workspace));
        assert(StorageRestore::constructions==before_rearm);assert(successor->begin_boundary(100,{}));
        StorageHistory failedHistory;decltype(storage) failedStorage(failedHistory,source,1,identity);
        for(unsigned i=100;i<103;++i){assert(failedStorage.begin_boundary(i,{}));assert(failedStorage.capture(current,i,bytes));if(i==100)first=current;}
        assert(failedStorage.begin_boundary(103,{}));StorageRestore::reject=true;
        assert(!failedStorage.restore(first) && failedStorage.failed());StorageRestore::reject=false;
        assert(failedStorage.restore_diagnostic.values[NativeRestoreDiagnostic::stage]==4);
        assert(failedStorage.restore_diagnostic.values[NativeRestoreDiagnostic::invoked]==1);
        assert(failedHistory.frames.size()==4); // failed restore never retires/replaces the predicted suffix
        assert(failedStorage.timing.buckets[NativeRollbackTiming::restore].calls==1);
        assert(failedStorage.timing.buckets[NativeRollbackTiming::cleanup].calls==0);
        auto actualHistory=std::make_unique<NativeRollingHistory<4>>();
        assert(!actualHistory->capture_general_owned(identity,97,0,0,source));
        assert(actualHistory->capture_diagnostic[0]==1);
        assert(actualHistory->capture_diagnostic[1]==identity.frame);
        assert(actualHistory->capture_diagnostic[2]==97);
        assert(actualHistory->capture_diagnostic[4]==0);
        assert(actualHistory->release());
        assert(actualHistory->capture_diagnostic[0]==1); // teardown preserves failure evidence
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
    {
        // Rolling restore must reject an unpaired or unowned effect binding
        // before any native load, rather than silently use the old path.
        HistoryFixture history;
        RollbackBoundaryKey first{1,2,3,4,5,100,1,GetCurrentThreadId()},last=first;last.frame=103;
        history.states[1]->effects=std::make_unique<NativeEffectSession::Binding>();
        RestoreFixture::calls.clear();RestoreFixture::prepareCount=RestoreFixture::loadCount=RestoreFixture::structuralCount=0;
        RestoreFixture::reject=RestoreFixture::rejectLoad=99;
        NativeHistoryRestore<RestoreFixture> missing;
        assert(!missing.run(history,first,last,1) && RestoreFixture::loadCount==0);
        for(auto& state:history.states)state->effects=std::make_unique<NativeEffectSession::Binding>();
        NativeHistoryRestore<RestoreFixture> unowned;
        assert(!unowned.run(history,first,last,1) && RestoreFixture::loadCount==0);
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
    // Retained dependency validity can change after preparation or a prior
    // native load. Never invoke the next load with invalid dependencies.
    for(unsigned completed=0;completed<3;++completed){
        Backend invalidated;invalidated.invalidateAfterLoads=completed;
        assert(RollbackWindowPlan::execute(keys,4,keys[3],invalidated)==R::restore_failed);
        assert(invalidated.loads==completed && invalidated.calls.size()==3+completed);
        for(unsigned i=0;i<completed;++i)assert(invalidated.calls[3+i]==12-i);
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





