#pragma once
#include "NativeSchedulerStorage.hpp"
#include "NativeStartupTiming.hpp"
#include "BoundedRollbackSession.hpp"
#include "CombatClockBridge.hpp"
#include "ReservedBattleDriver.hpp"
#include "NativeBattleBoundaryReader.hpp"
#include "SchedulerConfirmationQueue.hpp"
#include "OwnedInputPublication.hpp"
#include "NativeClockHook.hpp"
#include "CombatStartAgreement.hpp"
#include "OwnedTransportAttachment.hpp"
#include "ContinuousRollbackReport.hpp"
#include "CombatRuntimeStatus.hpp"
#include "PostBattleSettlement.hpp"
#include "PostBattleContinuation.hpp"
#include "PostBattleHookGate.hpp"
#include "NativeOuterCompletion.hpp"
#include "NativeFilterAccess.hpp"
#include "RawPredictionCoordinator.hpp"
#include "RawPredictionTransport.hpp"
#include "NativeConfirmedInputRecorder.hpp"
#include "NativePredictionFrontier.hpp"
namespace sc6::research {
// Heap-allocate and retain for the entire lifetime of any published routes.
// Preparation is deliberately separate from publishing the two native hooks.
class NativeCombatSession {
public:
    struct Config {
        uintptr_t base{},caller{},clock_object{};
        SingleUpdateCall::Routine original{};
        RollbackBoundaryKey identity;
        unsigned clock_identity{},clock_frame{};
        CombatStartAgreement::Offer offer;
        bool image_verified{},bilateral_private{},boundaries_owned{},verified_void_entry{},approved_profile{},general_restore{};
        bool prediction_enabled{true};
    };
    // Pure validation: reject inconsistent startup data before publishing a hold.
    static bool valid_config(const Config& c){
        const auto& k=c.identity;const auto& o=c.offer;
        CombatStartAgreement validator;
        return c.base && c.original && reinterpret_cast<uintptr_t>(c.original)!=c.base+0x2DBC60 &&
            c.caller>=c.base && c.caller-c.base<0x4900000 && c.clock_object && c.clock_frame!=UINT32_MAX &&
            k.same_round(k) && c.image_verified && c.bilateral_private && c.boundaries_owned && c.verified_void_entry &&
            validator.begin(o) && o.sender==k.session && o.recipient==k.peer && o.round==k.round &&
            o.event==k.event && o.first_frame==k.frame && o.slot==k.slot && o.delay==0 &&
            o.rollback==3 && o.records==observation::capacity;
    }
private:
    using History=NativeRollingHistory<4>;
    using Storage=NativeSchedulerStorage<>;
    using Session=BoundedRollbackSession<CombatFrameInput,Storage::Snapshot>;
    using Adapter=CombatUpdateAdapter<Storage>;
    using Clock=CombatClockBridge<Session>;
    using Driver=ReservedBattleDriver<Session,Adapter,Clock>;
    Config config_;
    ReplayDependencySource source_;
    History history_;
    Storage storage_;
    RollbackJournal<> journal_;
    ContinuousRollbackReport report_;
    Session session_;
    Adapter adapter_;
    Clock clock_;
    SchedulerConfirmationQueue<Session> confirmations_;
    RawPredictionRoute<> raw_;
    RawInputHistoryView raw_view_;
    RawPredictionCoordinator<Session> raw_coordinator_;
    NativeFilterFramePreparation<NativeFilterAccess> filters_;
    NativeConfirmedInputRecorder<> recorder_;
    bool filters_bound_{},input_routes_retired_{};
    NativeFilterPreparationRoute filter_route_;
    NativeConfirmedRecorderRoute recorder_route_;
    std::atomic<uint64_t> raw_frames_{},raw_confirmed_{},prediction_admissions_{},prediction_waits_{};
    std::array<std::atomic<uint64_t>,17> raw_diagnostics_{};
    // Written once on the game thread BEFORE stop/cleanup changes the state.
    // The report worker reads only atomics, never live clock/game memory.
    std::array<std::atomic<uint64_t>,44> backend_diagnostics_{};
    std::atomic<bool> backend_diagnostic_ready_{};
    std::array<std::atomic<uint64_t>,10> capture_diagnostics_{};
    unsigned backend_site_{};
    void backend_diagnostics(){
        if(backend_diagnostic_ready_.load())return;
        const auto& access=clock_.dispatch().access();
        const auto& state=access.last_observed_state();
        const auto& serviced=access.last_serviced_state();
        const auto& wait=access.last_wait_state();
        const auto reservation=clock_.reservation().diagnostic();
        const uint64_t values[]={backend_site_,prepared_,published_,owned_transport_attachment.ready(config_.identity.session),
            driver_.restart_required(),storage_.failed(),static_cast<unsigned>(driver_.failure()),
            static_cast<unsigned>(clock_.failure()),static_cast<unsigned>(clock_.dispatch().failure()),
            static_cast<unsigned>(access.failure()),access.exception(),access.stage(),
            state.identity,state.frame,state.local_cursor,state.all_cursor,state.lead,state.phase,state.wait_reason,state.wait_count,state.published,
            serviced.identity,serviced.frame,wait.frame,wait.phase,wait.wait_reason,wait.wait_count,
            reservation.identity,reservation.clock,reservation.battle,reservation.reserved,reservation.advanced,reservation.failed,
            session_.head(),static_cast<unsigned>(session_.state),static_cast<unsigned>(session_.admission(config_.identity.session,config_.identity.round)),
            adapter_.expected_frame(),state.slots,state.local_mask,state.remote_mask,state.all_mask,state.mode,config_.clock_frame,config_.identity.frame};
        static_assert(std::size(values)==44);
        for(unsigned i=0;i<backend_diagnostics_.size();++i)backend_diagnostics_[i]=values[i];
        if constexpr(requires {history_.capture_diagnostic;})
            for(unsigned i=0;i<capture_diagnostics_.size();++i)capture_diagnostics_[i]=history_.capture_diagnostic[i];
        backend_diagnostic_ready_.store(true,std::memory_order_release);
    }
    NativeBattleBoundaryReader<> reader_;
    OwnedInputPublication publication_;
    Driver driver_;
    SchedulerInputSink sink_;
    CombatRouteBundle routes_;
    bool completed_{},published_{};
    bool attempted_{},prepared_{};
    bool cleanup_attempted_{};
    PostBattleSettlement post_battle_;
    NativeCombatSession* previous_{};
    PostBattleHookGate post_gate_;
    uintptr_t pending_outer_{};
    uint64_t pending_token_{};
    void raw_diagnostics(){
        const auto& c=raw_.counters();
        const uint64_t values[]={static_cast<uint64_t>(raw_.failure()),raw_coordinator_.failed(),filters_.failures,
            filters_.sealed,recorder_.failed(),recorder_.appended,recorder_.already_published,recorder_.flushes,
            c.drained,c.duplicates,c.corrections,c.applied,
            clock_.pending_services(),clock_.pending_elapsed_ms(),clock_.pending_max_ms(),raw_.early_publications,raw_.early_rechecks};
        for(unsigned i=0;i<raw_diagnostics_.size();++i)raw_diagnostics_[i]=values[i];
    }
    bool publish_raw(){
        const auto& k=config_.identity;
        return raw_.publish_confirmed(k.thread,[&](auto,auto,auto,uint64_t frame,const auto& words){
            if(frame<k.frame || frame-k.frame>UINT32_MAX-config_.clock_frame)return false;
            const bool ok=recorder_.publish(config_.clock_frame+static_cast<uint32_t>(frame-k.frame),words);
            if(ok)++raw_confirmed_;return ok;
        }) && recorder_.service();
    }
    bool prepare_filter(uintptr_t outer,unsigned slot,uint32_t reverse,bool edges,bool held,bool restrict_buttons,uint32_t& raw){
        if(!outer_clock(outer,config_.clock_object))return true;
        const auto& k=config_.identity;
        if(!published_ || !ready() || retired())return false;
        if(!filters_bound_){
            NativeFilterFramePreparation<NativeFilterAccess>::Config c{outer,config_.clock_object,config_.base,k.thread,
                config_.clock_identity,config_.clock_frame,k.frame,k.round,k.event,k.generation};
            std::array<uint32_t,2> seed{};
            if(!NativeFilterAccess::seed(c,seed) || !raw_coordinator_.seed(k.thread,seed) || !filters_.bind(c,k.slot,config_.prediction_enabled,config_.prediction_enabled))return false;
            filters_bound_=true;
        }
        return filters_.filter(outer,slot,reverse,edges,held,restrict_buttons,raw)==decltype(filters_)::Result::prepared;
    }
    std::array<std::atomic<uint64_t>,10> prediction_gate_rejections_{};
    std::array<std::atomic<uint64_t>,12> prediction_gate_last_{};
    bool prediction_gate(const NativeClockAccess<>::State& wait,bool native_wait){
        const auto& k=config_.identity;
        auto reject=[&](unsigned reason){++prediction_gate_rejections_[reason];prediction_gate_last_[0]=reason;return false;};
        prediction_gate_last_[1]=wait.identity;prediction_gate_last_[2]=wait.frame;
        prediction_gate_last_[3]=wait.wait_reason;prediction_gate_last_[4]=native_wait;
        prediction_gate_last_[5]=adapter_.expected_frame();
        if(!published_)return reject(1);
        if(!ready())return reject(2);
        if(pending_token_)return reject(3);
        if(GetCurrentThreadId()!=k.thread)return reject(4);
        NativePredictionFrontier::Sample sample;
        if(!NativePredictionFrontier::read(config_.base,config_.clock_object,sample))return reject(5);
        prediction_gate_last_[6]=sample.identity;prediction_gate_last_[7]=sample.frame;prediction_gate_last_[8]=sample.battle;
        if(sample.identity!=wait.identity || sample.frame!=wait.frame)return reject(6);
        if(sample.battle!=adapter_.expected_frame())return reject(7);
        const auto fight=RoundFightReadiness::read(config_.base,k.round,k.generation,sample.battle);
        prediction_gate_last_[9]=fight.status;prediction_gate_last_[10]=fight.vtable;prediction_gate_last_[11]=fight.handler;
        if(!fight.permits(k.round,k.generation,sample.battle,k.thread))return reject(8);
        // Both peers assign freshly sampled local words to the same mod frame.
        // Freeze the first publication across stalls; filter/replay reuse it.
        if(config_.prediction_enabled && sample.frame>=config_.clock_frame){
            const auto index=sample.frame-config_.clock_frame;
            if(index==raw_.delivery().head() && raw_.outgoing_count()==index){
                uint32_t fresh{};
                if(!clock_.dispatch().access().fresh_local(sample.frame,fresh) || !raw_.publish_local(k.thread,index,fresh))return reject(9);
            }
        }
        // 3FDFA0 reason 2 is missing confirmed horizon; reason 3 is pacing.
        // Preserve transition/timeout waits, including the prior stateful wait.
        const bool eligible=config_.prediction_enabled?NativePredictionFrontier::eligible(sample,wait.wait_reason,native_wait,true):
            (!native_wait && NativePredictionFrontier::classify(sample)==NativePredictionFrontier::confirmed);
        if(eligible && native_wait)++prediction_admissions_;else if(!eligible)++prediction_waits_;
        if(!eligible)return reject(9);
        prediction_gate_last_[0]=0;return true;
    }
    static bool outer_clock(uintptr_t outer,uintptr_t clock)noexcept{
        __try {return outer && *reinterpret_cast<uintptr_t*>(outer+0x478)==clock;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    PostBattleHookGate::Result post_battle(uintptr_t outer,uintptr_t input_header){
        using R=PostBattleHookGate::Result;
        if(!outer_clock(outer,config_.clock_object))return R::permit;
        if(retired())return R::permit;
        if(!published_ || !ready() || pending_token_)return R::failed;
        const auto& k=config_.identity;
        const auto fight=RoundFightReadiness::read(config_.base,k.round,k.generation,adapter_.expected_frame());
        if(fight.permits(k.round,k.generation,adapter_.expected_frame(),k.thread))return R::permit;
        if(!fight.status)return R::failed;
        CombatFrameInput endpoint;std::array<std::byte,40> bytes{};
        if(!FullStateProbe::input_bytes(input_header,bytes) || !RecordedInput::valid_envelope(bytes))return R::failed;
        std::memcpy(endpoint.envelope.data(),bytes.data(),bytes.size());
        pending_outer_=outer;
        pending_token_=native_outer_completion.claim(k.thread,outer,this,[](void* p,const NativeOuterCompletion::Receipt& receipt){
            auto& self=*static_cast<NativeCombatSession*>(p);
            try {
                const bool retired=self.retire_after_native_return(receipt,self.pending_outer_,self.pending_token_);
                if(!retired)self.terminate(CombatRuntimeStatus::round_boundary);return retired;
            }catch(...){self.terminate(CombatRuntimeStatus::exception);throw;}
        });
        status.native_outer_token=pending_token_;
        if(!pending_token_ || wait_before_post_battle(endpoint)!=PostBattleSettlement::Result::ready || !retire_confirmations()){
            terminate(CombatRuntimeStatus::round_boundary);return R::failed;
        }
        return R::permit;
    }
public:
    CombatRuntimeStatus status;
    NativeCombatSession(const Config& config,ReplayDependencySource source,NativeCombatSession* previous=nullptr)
        :config_(config),source_(source),storage_(history_,source_,config.base,config.identity,config.approved_profile,
             previous?previous->storage_.take_retired_workspace(previous->retired() && previous->status.history_retired.load() &&
                 previous->status.native_outer_committed.load() && !previous->status.restart_required.load() &&
                 previous->config_.identity.session==config.identity.session && previous->config_.identity.thread==config.identity.thread):nullptr),
         adapter_(storage_,config.original,reinterpret_cast<volatile uint32_t*>(config.base+0x470D0C4),config.identity.thread,
            3-config.identity.slot,config.identity.frame),clock_(session_),confirmations_(session_),raw_coordinator_(raw_,session_),filters_(raw_),
         filter_route_{this,[](void* p,uintptr_t outer,unsigned slot,uint32_t reverse,bool edges,bool held,bool restrict_buttons,uint32_t& raw){
             auto& self=*static_cast<NativeCombatSession*>(p);
             const bool ok=self.prepare_filter(outer,slot,reverse,edges,held,restrict_buttons,raw);
             if(!ok){self.backend_site_=1;self.terminate(CombatRuntimeStatus::backend_failure);}return ok;
         },[](void* p,uint32_t frame,const std::array<uint32_t,10>& native){
             auto& self=*static_cast<NativeCombatSession*>(p);
             const bool ok=self.filters_.complete(frame,native) && self.raw_coordinator_.before_step(self.config_.identity.thread);
             if(ok && frame>=self.config_.identity.frame){
                 const auto index=frame-self.config_.identity.frame;
                 if(const auto* input=self.raw_.delivery().frame(index))
                     self.clock_.dispatch().access().input_consumed(self.config_.clock_frame+index,input->raw[self.config_.identity.slot-1],self.config_.prediction_enabled);
             }
             return ok;
         }},recorder_route_{config.clock_object,this,[](void* p,uintptr_t){
             auto& self=*static_cast<NativeCombatSession*>(p);
             const bool ok=self.publish_raw();if(!ok){self.backend_site_=2;self.terminate(CombatRuntimeStatus::backend_failure);}return ok;
         }},driver_(session_,adapter_,clock_),
         sink_{&confirmations_,decltype(confirmations_)::send},routes_{&clock_.dispatch(),&driver_.route(),&sink_},
         post_gate_(config.identity.thread,this,[](void* p,uintptr_t outer,uintptr_t input){
             auto& self=*static_cast<NativeCombatSession*>(p);
             try {
                 const auto result=self.post_battle(outer,input);
                 if(result==PostBattleHookGate::Result::failed)self.terminate(CombatRuntimeStatus::round_boundary);
                 return result;
             }catch(...){self.terminate(CombatRuntimeStatus::exception);throw;}
         }){
        previous_=previous;
        status.session=config.identity.session;status.context=this;
        status.previous=previous?&previous->status:nullptr;status.round_event=config.identity.event;
        status.drain=[](void* p,std::ostream& events,std::ostream& summary){static_cast<NativeCombatSession*>(p)->report(events,summary);};
    }
    NativeCombatSession(const NativeCombatSession&)=delete;
    NativeCombatSession& operator=(const NativeCombatSession&)=delete;
    bool prepare(observation::Shared& shared){
        NativeStartupScope startup_measure(NativeStartupTiming::session_bind,config_.identity.round);
        if(!config_.general_restore || !NativeBoundarySnapshot::general_restore_implemented)return false;
        if(attempted_)return false;attempted_=true;
        if(previous_ && !previous_->routes_.retired())return false;
        const auto& c=config_;const auto& k=c.identity;
        if(!owned_transport_attachment.ready(k.session) || shared.reserved!=observation::owned_mode || !valid_config(c) || !c.base || !c.original || reinterpret_cast<uintptr_t>(c.original)==c.base+0x2DBC60 ||
            !k.same_round(k) || k.thread!=GetCurrentThreadId() || !c.image_verified || !c.bilateral_private ||
            !c.boundaries_owned || !c.verified_void_entry || !source_.available() || !source_.effects)return false;
        if(!session_.attach_journal(journal_) || !reader_.bind(c.base,c.caller,k,c.image_verified) ||
            !(previous_?publication_.bind_after(previous_->publication_,shared,k.session,k.round,k.event,k.frame):
                publication_.bind(shared,k.session,k.round,k.event,k.frame,true)) ||
            !raw_.bind(k.session,k.peer,k.round,k.event,k.frame,k.slot,k.thread,{},previous_?&previous_->raw_:nullptr) ||
            !raw_view_.bind(raw_.delivery(),k.session,k.round,k.event,k.frame,k.thread) ||
            !adapter_.attach_raw_history(raw_view_,k.session,k.round,k.event) ||
            !raw_coordinator_.bind(k.session,k.round,k.thread,3-k.slot) ||
            !clock_.attach_confirmations(this,[](void* p){auto& s=*static_cast<NativeCombatSession*>(p);return s.raw_coordinator_.drain(s.config_.identity.thread);}) ||
            !clock_.attach_prediction(this,[](void* p,const NativeClockAccess<>::State& state,bool waiting){return static_cast<NativeCombatSession*>(p)->prediction_gate(state,waiting);}) ||
            !clock_.bind(c.base,c.clock_object,k.thread,c.image_verified,c.bilateral_private,c.boundaries_owned,
                k.session,k.round,c.clock_identity,c.clock_frame,k.frame))return false;
        if(!driver_.attach_publication(this,[](void* p,const Driver::Boundary& b){
                auto& self=*static_cast<NativeCombatSession*>(p);
                if(!self.raw_coordinator_.after_step(self.config_.identity.thread,[&](auto,auto,auto,uint64_t frame,const auto& words){
                    const auto& c=self.config_;const auto& k=c.identity;
                    if(frame<k.frame || frame-k.frame>UINT32_MAX-c.clock_frame)return false;
                    const bool ok=self.recorder_.publish(c.clock_frame+static_cast<uint32_t>(frame-k.frame),words);
                    if(ok)++self.raw_confirmed_;return ok;
                }))return false;
                self.raw_frames_=self.raw_.delivery().head();
                self.raw_diagnostics();
                if(!self.publication_.publish(b.session,b.round,b.frame,b.local))return false;
                self.status.updates=self.adapter_.calls;
                if(self.storage_.restore_diagnostic.values[NativeRestoreDiagnostic::invoked])self.status.restored=true;
                if(self.publication_.complete()){self.completed_=true;self.clock_.stop();}
                return true;
            }) || !driver_.bind(k.session,k.round,&reader_,NativeBattleBoundaryReader<>::callback<Driver::Boundary>,
                c.bilateral_private,true,c.verified_void_entry)){clock_.stop();return false;}
        prepared_=true;return true;
    }
    bool ready()const{return prepared_ && owned_transport_attachment.ready(config_.identity.session) && !driver_.restart_required() && !storage_.failed();}
    bool complete()const{return completed_;}
    // Only from an owned outer continuation BEFORE delegates or queued state
    // changes. A waiting result must retain that continuation; returning from
    // a delegate hook alone would allow SC6 to commit the state queue anyway.
    // Deliberately not called by the existing next-clock startup poll.
    PostBattleSettlement::Result settle_before_post_battle(const CombatFrameInput& endpoint){
        using Result=PostBattleSettlement::Result;
        const auto frame=adapter_.expected_frame();
        ++status.settlement_polls;status.settlement_frame=frame;
        if(!published_ || !ready() || GetCurrentThreadId()!=config_.identity.thread || !reader_.frontier(frame)){
            status.settlement_result=3;status.settlement_failure=static_cast<unsigned>(PostBattleSettlement::Failure::identity);
            terminate(CombatRuntimeStatus::round_boundary);return Result::restart_required;
        }
        const auto result=post_battle_.poll(session_,adapter_,[this]{return raw_coordinator_.drain(config_.identity.thread);},endpoint,
            config_.identity.session,config_.identity.round,GetCurrentThreadId(),frame,clock_.reservation().quiescent());
        if(result!=Result::restart_required &&
           (!raw_.request_handoff(config_.identity.thread) ||
            !raw_.acknowledge(config_.identity.thread,raw_.delivery().revision(),raw_.delivery().head()) || !publish_raw())){
            terminate(CombatRuntimeStatus::backend_failure);return Result::restart_required;
        }
        // 0 means never attempted; 1 waiting, 2 ready, 3 restart required.
        status.settlement_result=static_cast<unsigned>(result)+1;
        status.settlement_failure=static_cast<unsigned>(post_battle_.failure());
        if(result==Result::waiting)++status.settlement_waits;
        status.updates=adapter_.calls;
        if(storage_.restore_diagnostic.values[NativeRestoreDiagnostic::invoked])status.restored=true;
        if(result==Result::restart_required)terminate(CombatRuntimeStatus::backend_failure);
        return result;
    }
    PostBattleSettlement::Result wait_before_post_battle(const CombatFrameInput& endpoint){
        NativeStartupScope startup_measure(NativeStartupTiming::settlement_wait,config_.identity.round);
        if(GetCurrentThreadId()!=config_.identity.thread)return PostBattleSettlement::Result::restart_required;
        // This method blocks the entire native caller chain, not just one
        // delegate. No engine pump or native update runs inside the wait.
        const auto result=post_battle_.wait([&]{return settle_before_post_battle(endpoint);},
            []{return GetTickCount64();},[]{Sleep(1);},10000);
        if(result==PostBattleSettlement::Result::restart_required){
            status.settlement_result=3;
            status.settlement_failure=static_cast<unsigned>(post_battle_.failure());
            terminate(CombatRuntimeStatus::backend_failure);
        }
        return result;
    }
    // The old session must remain alive after a new route is published: the
    // transport can still hold its sink and its immutable retired-input archive.
    bool retire_confirmations(){
        NativeStartupScope startup_measure(NativeStartupTiming::route_retire,config_.identity.round);
        if(!published_ || !ready() || GetCurrentThreadId()!=config_.identity.thread ||
            !reader_.frontier(adapter_.expected_frame()) || !clock_.reservation().quiescent() || !session_.handoff_ready())return false;
        if(input_routes_retired_)return true;
        // A producer holding the tiny queue gate is backpressure, not a native
        // restore failure. No native service or update runs during this retry.
        const auto start=GetTickCount64();
        while(!raw_.retire(config_.identity.thread)){
            if(raw_.failed() || GetTickCount64()-start>=50)return false;Sleep(1);
        }
        if(!raw_view_.retire() || !publication_.retire())return false;
        const NativeFilterPreparationRoute* expected_filter=&filter_route_;
        const NativeConfirmedRecorderRoute* expected_recorder=&recorder_route_;
        if(!native_filter_preparation_route.compare_exchange_strong(expected_filter,nullptr) ||
           !native_confirmed_recorder_route.compare_exchange_strong(expected_recorder,nullptr))return false;
        // Native commit may reset the clock/recording identity. Its original
        // non-speculative input routes resume only after settlement is complete.
        input_routes_retired_=true;return true;
    }
    // Only after the entire native outer continuation has committed. Do not
    // use a delegate return as evidence of completion. Failure leaves the old
    // routes blocking, exactly like any other partial-restoration failure.
    bool retire_after_continuation(const PostBattleContinuation& continuation){
        const auto& k=config_.identity;
        if(GetCurrentThreadId()!=k.thread || !published_ || routes_.retired() || !ready() ||
            combat_routes.load(std::memory_order_acquire)!=&routes_ || !raw_.retired() ||
            !session_.handoff_ready() || !clock_.reservation().quiescent() ||
            !continuation.committed({k.session,k.peer,k.generation,k.event,k.round,k.thread}))return false;
        if(cleanup_attempted_)return false;
        cleanup_attempted_=true;
        status.history_retired=release_history();
        if(!status.history_retired.load()){terminate(CombatRuntimeStatus::backend_failure);return false;}
        status.active=false;
        routes_.retired_.store(true,std::memory_order_release);
        return true;
    }
    bool retired()const{return routes_.retired();}
    bool awaiting_native_return()const{return pending_token_ && !routes_.retired() && !status.restart_required.load();}
    bool retire_after_native_return(const NativeOuterCompletion::Receipt& receipt,uintptr_t outer,uint64_t token){
        const auto& k=config_.identity;
        if(!receipt.matches(outer,token,k.thread) || outer!=pending_outer_ || token!=pending_token_ ||
           GetCurrentThreadId()!=k.thread || !published_ || routes_.retired() || !ready() ||
           combat_routes.load(std::memory_order_acquire)!=&routes_ || !raw_.retired() ||
           !session_.handoff_ready() || !clock_.reservation().quiescent() || cleanup_attempted_)return false;
        cleanup_attempted_=true;status.history_retired=release_history();
        if(!status.history_retired.load()){terminate(CombatRuntimeStatus::backend_failure);return false;}
        if(!input_routes_retired_ || native_filter_preparation_route.load() || native_confirmed_recorder_route.load())return false;
        status.native_outer_committed=true;status.active=false;routes_.retired_.store(true,std::memory_order_release);return true;
    }
    bool owns_current_routes()const{return combat_routes.load(std::memory_order_acquire)==&routes_;}
    // Call at a quiescent owner-thread boundary BEFORE the first reserved clock
    // call, with both peers agreeing to this exact test. Caller retains *this.
    bool activate(CombatStartAgreement& agreement){
        if(!config_.general_restore || !NativeBoundarySnapshot::general_restore_implemented)return false;
        const auto& offer=config_.offer;const auto& key=config_.identity;
        if(native_outer_hook_base.load()!=config_.base || native_post_battle_hook_base.load()!=config_.base || native_outer_completion.failed())return false;
        if(native_filter_hook_base.load()!=config_.base || native_confirmed_recorder_hook_base.load()!=config_.base ||
           native_filter_preparation_route.load() || native_confirmed_recorder_route.load() ||
           !recorder_.bind(config_.base,config_.clock_object,key.thread,config_.clock_identity,config_.clock_frame))return false;
        if(!owned_transport_attachment.ready(key.session) || published_ || !ready() || GetCurrentThreadId()!=key.thread || offer.sender!=key.session || offer.recipient!=key.peer ||
            offer.round!=key.round || offer.event!=key.event || offer.first_frame!=key.frame || offer.slot!=key.slot ||
            offer.delay!=0 || offer.rollback!=3 || offer.records!=observation::capacity ||
            !native_clock_hook::installed || native_clock_hook::installed_base!=config_.base ||
            native_clock_hook::route.load() || exclusive_battle_route.load() || scheduler_input_sink.load())return false;
        if(const auto hold=native_clock_hook::start_hold.load(std::memory_order_acquire);
            hold && !hold->ready(config_.clock_object,GetTickCount64()))return false;
        if(!reader_.frontier(key.frame) ||
            !clock_.dispatch().at_boundary(config_.clock_identity,config_.clock_frame))return false;
        const auto fight=RoundFightReadiness::read(config_.base,key.round,key.generation,key.frame);
        if(!fight.permits(key.round,key.generation,key.frame,key.thread))return false;
        if(!agreement.activation_ready() || !agreement.consume(offer))return false;
        published_=publish_combat_routes(routes_,previous_?&previous_->routes_:nullptr);
        if(published_){
            const NativeFilterPreparationRoute* empty_filter=nullptr;
            const NativeConfirmedRecorderRoute* empty_recorder=nullptr;
            auto* old_raw=previous_?&previous_->raw_:nullptr;
            if(!native_filter_preparation_route.compare_exchange_strong(empty_filter,&filter_route_) ||
               !native_confirmed_recorder_route.compare_exchange_strong(empty_recorder,&recorder_route_) ||
               !raw_prediction_transport.compare_exchange_strong(old_raw,&raw_)){
                terminate(CombatRuntimeStatus::activation_failure);return false;
            }
            auto* expected=previous_?&previous_->post_gate_:nullptr;
            if(!post_battle_hook_gate.compare_exchange_strong(expected,&post_gate_,std::memory_order_release)){
                terminate(CombatRuntimeStatus::activation_failure);return false;
            }
        }
        if(published_)status.activated_at=GetTickCount64();
        status.active.store(published_,std::memory_order_release);return published_;
    }
    auto& clock_dispatch(){return clock_.dispatch();}
    auto& battle_route(){return driver_.route();}
    const SchedulerInputSink& input_sink()const{return sink_;}
    // Exactly one logging worker may call this after session publication.
    void report(std::ostream& events,std::ostream& summary){
        summary<<"continuous_archive_full="<<publication_.archive_full()<<"\ncontinuous_archive_omitted="<<publication_.archive_omitted()<<'\n';
        storage_.timing.write(summary);
        summary<<"continuous_workspace_reused="<<storage_.workspace_reused()<<'\n';
        summary<<"continuous_prediction_gate_schema=1\n";
        for(unsigned i=1;i<prediction_gate_rejections_.size();++i)summary<<"continuous_prediction_gate_reject_"<<i<<'='<<prediction_gate_rejections_[i].load()<<'\n';
        for(unsigned i=0;i<prediction_gate_last_.size();++i)summary<<"continuous_prediction_gate_last_"<<i<<'='<<prediction_gate_last_[i].load()<<'\n';

        summary<<"input_latency_prediction_enabled="<<config_.prediction_enabled<<"\ninput_latency_baseline_is_unmodified_game=0\n";
        clock_.dispatch().access().write_input_latency(summary,config_.prediction_enabled);
        native_startup_timing.write(summary);
        summary<<"continuous_workspace_reuse=1\ncontinuous_workspace_timing_is_reset=1\n";
        report_.drain(journal_,events);report_.write(summary);
        raw_.confirmation_age.write(summary);
        summary<<"continuous_raw_duplicate_coalescing=1\ncontinuous_raw_coalesced="<<raw_.coalesced()
            <<"\ncontinuous_raw_queue_full="<<raw_.queue_full()<<'\n';
        summary<<"continuous_confirmation_round_retired="<<raw_.retired()
            <<"\ncontinuous_route_round_retired="<<routes_.retired()
            <<"\ncontinuous_retired_input_packets="<<confirmations_.retired_packets()<<'\n';
        summary<<"continuous_raw_frames="<<raw_frames_.load()<<"\ncontinuous_raw_confirmed="<<raw_confirmed_.load()
            <<"\ncontinuous_prediction_admissions="<<prediction_admissions_.load()
            <<"\ncontinuous_prediction_waits="<<prediction_waits_.load()<<"\ncontinuous_raw_round_retired="<<raw_.retired()<<'\n';
        constexpr const char* names[]={"route_failure","coordinator_failed","filter_failures","filter_sealed","recorder_failed",
            "recorder_appended","recorder_already_published","recorder_flushes","drained","duplicates","late_confirmations","applied",
            "pending_clock_services","pending_clock_elapsed_ms","pending_clock_max_ms","early_publications","early_rechecks"};
        for(unsigned i=0;i<raw_diagnostics_.size();++i)summary<<"continuous_raw_"<<names[i]<<'='<<raw_diagnostics_[i].load()<<'\n';
        summary<<"continuous_capture_schema=1\n";
        constexpr const char* capture_names[]={"stage","frame","oldest","count","native_stage","exception","fault","valid","observed_frame","saved"};
        if(backend_diagnostic_ready_.load(std::memory_order_acquire))
            for(unsigned i=0;i<capture_diagnostics_.size();++i)summary<<"continuous_capture_"<<capture_names[i]<<'='<<capture_diagnostics_[i].load()<<'\n';
        summary<<"continuous_backend_diagnostic_schema=1\ncontinuous_backend_diagnostic_ready="<<backend_diagnostic_ready_.load(std::memory_order_acquire)<<'\n';
        constexpr const char* backend_names[]={"site","prepared","published","transport_ready","driver_stopped","storage_failed","driver_failure",
            "bridge_failure","dispatch_failure","clock_failure","clock_exception","clock_stage",
            "identity","frame","local_cursor","all_cursor","lead","phase","wait_reason","wait_count","published_frame",
            "serviced_identity","serviced_frame","wait_frame","wait_phase","serviced_wait_reason","serviced_wait_count",
            "reservation_identity","reservation_clock","reservation_battle","reserved","advanced","reservation_failed",
            "scheduler_head","scheduler_state","scheduler_admission","expected_battle","slots","local_mask","remote_mask","all_mask","mode","initial_clock","initial_battle"};
        static_assert(std::size(backend_names)==44);
        if(backend_diagnostic_ready_.load(std::memory_order_acquire))for(unsigned i=0;i<backend_diagnostics_.size();++i)
            summary<<"continuous_backend_"<<backend_names[i]<<'='<<backend_diagnostics_[i].load()<<'\n';
    }
    void stop(){clock_.stop();}
    // Only at the selected clock's owner-thread poll, before another update.
    // Keep routes installed: a partially restored session cannot resume stock.
    void terminate(CombatRuntimeStatus::Reason reason)noexcept{
        if(GetCurrentThreadId()!=config_.identity.thread)return;
        backend_diagnostics();
        raw_diagnostics();
        routes_.retired_.store(false,std::memory_order_release);
        stop();status.active=false;status.restart_required=true;
        unsigned empty=CombatRuntimeStatus::none;status.reason.compare_exchange_strong(empty,reason);
        status.adapter_failure=adapter_.failure;status.adapter_exception=adapter_.exception;
        status.updates=adapter_.calls;
        if(storage_.restore_diagnostic.values[NativeRestoreDiagnostic::invoked])status.restored=true;
        for(unsigned i=0;i<status.topology_details.size();++i)status.topology_details[i]=storage_.restore_diagnostic.topology[i];
        static_assert(NativeRestoreDiagnostic::count==std::size(NativeRestoreDiagnostic::names));
        for(unsigned i=0;i<NativeRestoreDiagnostic::count;++i)status.restore_details[i]=storage_.restore_diagnostic.values[i];
        for(unsigned record=0;record<16;++record)for(unsigned field=0;field<9;++field)
            status.restore_payload_details[record][field]=storage_.restore_diagnostic.payload_details[record][field];
        const CameraSlotEvidence* slot_source=&storage_.restore_diagnostic.camera_slots;
        if constexpr(requires {history_.capture_slot_evidence;})
            if(history_.capture_diagnostic[4]==17 && history_.capture_slot_evidence.header[0][0])slot_source=&history_.capture_slot_evidence;
        const auto& slots=*slot_source;
        status.camera_slot_selected=slots.selected;
        for(unsigned sample=0;sample<2;++sample){
            for(unsigned field=0;field<6;++field)status.camera_slot_header[sample][field]=slots.header[sample][field];
            for(unsigned slot=0;slot<16;++slot)for(unsigned field=0;field<7;++field)status.camera_slot_meta[sample][slot][field]=slots.slots[sample][slot][field];
            for(unsigned word=0;word<CameraSlotEvidence::words;++word)status.camera_slot_payload[sample][word]=slots.payload[sample][word];
            status.camera_slot_retained[sample]=slots.retained[sample];
        }
        if(storage_.restore_diagnostic.timer_evidence_valid){
            for(unsigned i=0;i<NativeRestoreDiagnostic::timer_word_count;++i)status.restore_timer_words[i]=storage_.restore_diagnostic.timer_words[i];
            status.restore_timer_evidence_valid=true;
        }
        if(!cleanup_attempted_){
            cleanup_attempted_=true;
            try {status.history_retired=release_history();}
            catch(...){status.history_retired=false;}
        }
    }
    // Only after hook execution has stopped, on the owner thread. The caller
    // also owns the original dependency source and releases it separately.
    bool release_history(){return history_.release();}
};
}
