#pragma once
#include "NativeCombatArming.hpp"
#include "NativeCombatConfiguration.hpp"
#include "ReplayOwnedResources.hpp"
#include "CombatPeerContext.hpp"
#include "RearmPeerAdmission.hpp"
namespace sc6::research {
// Immutable request after queue(). Request, shared mapping and resource source
// must remain alive for the process lifetime. Native preparation runs only from
// the selected object's clock hook, before service/advancement.
class NativeArmingDispatch {
public:
    enum class State {waiting,preparing,prepared,rejected};
private:
    NativeCombatSession::Config config_;
    ReplayDependencySource source_;
    void* source_context_{};
    ReplayDependencySource (*source_provider_)(void*){};
    void* rearm_context_{};
    NativeCombatArming::RearmProvider rearm_provider_{};
    CombatResourcePreparation<>* rearm_resources_{};
    RearmPeerAdmission peer_admission_;
    NativeCombatArming::RearmResult production_rearm(uintptr_t object,NativeCombatSession::Config& out,ReplayDependencySource& source){
        using R=NativeCombatArming::RearmResult;
        out={};source={};
        auto* runtime=combat_runtime_status.load(std::memory_order_acquire);
        if(runtime && runtime->session!=config_.identity.session)runtime=nullptr;
        if(runtime)++runtime->rearm_polls;
        auto finish=[&](R result,unsigned failure=0){
            if(runtime){runtime->rearm_result=static_cast<unsigned>(result)+1;runtime->rearm_failure=failure;}
            return result;
        };
        const auto* routes=combat_routes.load(std::memory_order_acquire);
        if(cancelled_.load() || !routes || !routes->retired() || !rearm_resources_ ||
           GetCurrentThreadId()!=config_.identity.thread || object!=config_.clock_object)return finish(R::failed,1);
        // Terminal End is distinct from GameSet/round replay. Inspect on the
        // owner thread only, after native continuation and history retirement.
        if(runtime && (rearm_resources_->match_end_requested.load() ||
            rearm_resources_->finish_live_match(config_.identity.thread,
                runtime->history_retired.load() && runtime->native_outer_committed.load() &&
                !runtime->active.load() && !runtime->restart_required.load() && runtime->updates.load()>0))){
            // Cleanup needs an owner-thread tick even when combat no longer
            // produces frames on the results screen. stop_ prevents capture.
            rearm_resources_->frame(runtime->settlement_frame.load(),false);
            if(rearm_resources_->failed.load())return finish(R::failed,10);
            if(rearm_resources_->match_end_clean.load())runtime->match_completed.store(true,std::memory_order_release);
            return finish(R::waiting);
        }
        uint64_t now{};CombatPeerContext::Snapshot peer;
        const auto peer_check=combat_peer_context.inspect_current(peer,now);
        const auto peer_result=peer_admission_.check(peer,peer_check,now,steam_peer_,config_.identity.session,
            config_.identity.peer,config_.identity.slot,shared_.nonce);
        if(runtime){
            runtime->rearm_peer_reason=peer_admission_.reason;runtime->rearm_peer_read=peer_admission_.read_check;
            runtime->rearm_peer_tick=peer.tick;runtime->rearm_peer_now=now;runtime->rearm_peer_waits=peer_admission_.waits;
        }
        if(peer_result==RearmPeerAdmission::Result::failed)return finish(R::failed,2);
        if(peer_result==RearmPeerAdmission::Result::waiting)return finish(R::waiting);
        const auto round=round_event::snapshot();
        if(round.counter<=config_.identity.event)return finish(R::waiting);
        if(!round.round || round.generation<config_.identity.generation || config_.offer.ticket==UINT64_MAX)return finish(R::failed,3);
        NativeClockDiscovery::Observation observed;
        observed.valid=true;observed.base=config_.base;observed.object=object;observed.thread=GetCurrentThreadId();observed.tick=now;
        using Clock=NativeClockAccess<>;
        Clock::Failure clock_failure{};unsigned clock_exception{};
        const auto inspection=Clock::inspect_rearm(config_.base,object,observed.clock,clock_failure,clock_exception);
        if(runtime){
            runtime->rearm_clock_phase=observed.clock.phase;
            runtime->rearm_clock_identity=observed.clock.identity;runtime->rearm_clock_frame=observed.clock.frame;
            runtime->rearm_clock_failure=static_cast<unsigned>(clock_failure);runtime->rearm_clock_exception=clock_exception;
        }
        if(inspection==Clock::Inspection::invalid)return finish(R::failed,4);
        if(inspection==Clock::Inspection::waiting){
            if(runtime){++runtime->rearm_clock_waits;runtime->rearm_clock_phase_mask.fetch_or(1u<<observed.clock.phase);}
            return finish(R::waiting);
        }
        unsigned frame{};if(!NativeBattleBoundaryAccess::counter(config_.base,frame))return finish(R::failed,5);
        if(runtime)runtime->rearm_frame=frame;
        const auto fight=RoundFightReadiness::read(config_.base,round.round,round.generation,frame);
        if(!fight.status)return finish(R::failed,6);
        if(!fight.permits(round.round,round.generation,frame,observed.thread))return finish(R::waiting);
        auto admission=config_;
        admission.identity.round=admission.offer.round=round.round;
        admission.identity.event=admission.offer.event=round.counter;
        admission.identity.generation=round.generation;
        admission.identity.frame=admission.offer.first_frame=frame;++admission.offer.ticket;
        const auto assembled=NativeCombatConfiguration<>::assemble(observed,now,admission,out);
        if(assembled!=NativeCombatConfiguration<>::Result::ready)return finish(R::failed,100+static_cast<unsigned>(assembled));
        try {source=rearm_resources_->successor_source_on_owner();}
        catch(...){return finish(R::failed,9);}
        if(!source.available())return finish(R::failed,8);
        // prepare() still performs bilateral hold/offer/ack agreement. Merely
        // observing this boundary does not authorize prediction or advancement.
        config_=out;return finish(R::ready);
    }
    std::atomic<bool> cancelled_{};
    bool queued_{};
    observation::Shared& shared_;
    uint64_t steam_peer_,deadline_;
    std::atomic<State> state_{State::waiting};
    std::atomic<unsigned> stage_{0};
    std::unique_ptr<NativeCombatArming> arming_;
    native_clock_hook::StartupPoll callback_{this,[](void* context,uintptr_t object)noexcept{
        static_cast<NativeArmingDispatch*>(context)->poll(object);
    }};
    static bool clock_frame(uintptr_t object,unsigned& identity,unsigned& frame)noexcept{
        __try {identity=*reinterpret_cast<unsigned*>(object+0x3A0);frame=*reinterpret_cast<unsigned*>(object+0x3A4);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    void poll(uintptr_t object)noexcept{
        if(object!=config_.clock_object)return;
        if(cancelled_.load(std::memory_order_acquire)){
            if(auto* status=combat_runtime_status.load(std::memory_order_acquire);status && status->session==config_.identity.session)status->request_stop();
            if(state_.load()==State::waiting)state_=State::rejected;
            return;
        }
        if(state_.load()!=State::waiting)return;
        stage_=1;
        unsigned identity{},frame{};
        if(GetTickCount64()>=deadline_ || GetCurrentThreadId()!=config_.identity.thread ||
            !clock_frame(object,identity,frame) || identity!=config_.clock_identity || frame>config_.clock_frame){state_=State::rejected;return;}
        if(frame<config_.clock_frame)return;
        state_=State::preparing;stage_=2;
        try {
            if(source_provider_)source_=source_provider_(source_context_);
            if(!source_.available()){state_=State::rejected;return;}
            stage_=3;arming_=std::make_unique<NativeCombatArming>();
            if(rearm_provider_ && !arming_->automatic_rearm(rearm_context_,rearm_provider_)){
                state_=State::rejected;return;
            }
            stage_=4;
            const bool prepared=arming_->prepare(config_,source_,shared_,steam_peer_,true);
            stage_=prepared?100:10+arming_->preparation_stage();
            state_=prepared?State::prepared:State::rejected;
        }catch(...){
            // A hold may already be published. Latch failure on that owner;
            // reporting rejection alone would leave native servicing enabled.
            if(arming_)arming_->preparation_failed();
            state_=State::rejected;
        }
    }
public:
    // Owner-thread assembly only. The caller must retain a successfully queued
    // request for process lifetime; construction does not publish or arm it.
    static std::unique_ptr<NativeArmingDispatch> from_observation(
        const NativeClockDiscovery::Observation& observed,const NativeCombatSession::Config& admission,
        ReplayDependencySource source,observation::Shared& shared,uint64_t peer,uint64_t now,
        NativeCombatConfiguration<>::Result& result){
        NativeCombatSession::Config config;
        result=NativeCombatConfiguration<>::assemble(observed,now,admission,config);
        if(result!=NativeCombatConfiguration<>::Result::ready)return {};
        if(!source.available()){result=NativeCombatConfiguration<>::Result::resources;return {};}
        if(now>UINT64_MAX-10000){result=NativeCombatConfiguration<>::Result::configuration;return {};}
        return std::make_unique<NativeArmingDispatch>(config,source,shared,peer,now+10000);
    }
    static std::unique_ptr<NativeArmingDispatch> from_owned_resources(
        const NativeCombatSession::Config& admission,ReplayOwnedResources& resources,
        observation::Shared& shared,uint64_t peer,NativeCombatConfiguration<>::Result& result){
        const auto now=GetTickCount64();NativeClockDiscovery::Observation observed;
        if(!native_clock_hook::discovery.read(now,observed)){result=NativeCombatConfiguration<>::Result::stale;return {};}
        return from_observation(observed,admission,resources.source(),shared,peer,now,result);
    }
    static std::unique_ptr<NativeArmingDispatch> from_live_context(
        NativeCombatSession::Config admission,ReplayOwnedResources& resources,
        observation::Shared& shared,NativeCombatConfiguration<>::Result& result){
        CombatPeerContext::Snapshot peer;
        if(!combat_peer_context.read(GetTickCount64(),peer) || shared.nonce!=peer.local_nonce ||
            admission.identity.slot!=peer.local_slot){result=NativeCombatConfiguration<>::Result::offer;return {};}
        admission.identity.session=admission.offer.sender=peer.local_nonce;
        admission.identity.peer=admission.offer.recipient=peer.remote_nonce;
        return from_owned_resources(admission,resources,shared,peer.peer,result);
    }
    NativeArmingDispatch(const NativeCombatSession::Config& config,ReplayDependencySource source,
        observation::Shared& shared,uint64_t peer,uint64_t deadline)
        :config_(config),source_(source),shared_(shared),steam_peer_(peer),deadline_(deadline){}
    State state()const{return state_.load(std::memory_order_acquire);}
    unsigned stage()const{return stage_.load(std::memory_order_acquire);}
    // Configure before queue; resolve owner-thread resources only when the
    // selected native clock actually reaches the agreed boundary.
    bool source_on_owner(void* context,ReplayDependencySource (*provider)(void*)){
        if(queued_ || state()!=State::waiting || source_provider_ || !context || !provider)return false;
        source_context_=context;source_provider_=provider;return true;
    }
    // Registration is immutable once the clock callback is published. The
    // provider and its retained resources must outlive every successor round.
    // Invocation remains on the simulation owner, after route retirement.
    bool automatic_rearm(void* context,NativeCombatArming::RearmProvider provider){
        if(queued_ || cancelled_.load() || state()!=State::waiting || rearm_provider_ || !context || !provider)return false;
        rearm_context_=context;rearm_provider_=provider;return true;
    }
    bool production_rearm(CombatResourcePreparation<>& resources){
        if(rearm_resources_ || !automatic_rearm(this,[](void* p,uintptr_t object,NativeCombatSession::Config& config,ReplayDependencySource& source){
            return static_cast<NativeArmingDispatch*>(p)->production_rearm(object,config,source);
        }))return false;
        rearm_resources_=&resources;return true;
    }
    void cancel(){
        cancelled_.store(true,std::memory_order_release);
        if(auto* status=combat_runtime_status.load(std::memory_order_acquire);status && status->session==config_.identity.session)status->request_stop();
    }
    bool queue(){
        if(queued_ || cancelled_.load() || !NativeCombatSession::valid_config(config_) || shared_.reserved!=observation::owned_mode || !steam_peer_ || !config_.clock_object || !config_.identity.same_round(config_.identity) ||
            !config_.image_verified || !config_.bilateral_private || !config_.boundaries_owned || !config_.verified_void_entry ||
            !native_clock_hook::installed || native_clock_hook::installed_base!=config_.base ||
            combat_routes.load() || native_clock_hook::start_hold.load() || native_clock_hook::startup_poll.load())return false;
        const auto now=GetTickCount64();if(deadline_<=now || deadline_-now>10000)return false;
        const native_clock_hook::StartupPoll* empty=nullptr;
        queued_=true;
        if(native_clock_hook::preparation_poll.compare_exchange_strong(empty,&callback_,std::memory_order_release))return true;
        queued_=false;return false;
    }
};
}
