#pragma once
#include "NativeCombatSession.hpp"
#include "CombatStartPacket.hpp"
#include "RoundFightReadiness.hpp"
#include "NativeStartupTiming.hpp"
#include "OverlayPingTransport.hpp"
namespace sc6::research {
// Process-lifetime owner of startup routes. Must be heap allocated and called
// at an independently verified quiescent boundary on the simulation thread.
// No UI/file command may call prepare directly from the network worker.
class NativeCombatArming {
public:
    enum class Phase {idle,preparing,holding,active,complete,restart_required,retired};
    enum class RearmResult {waiting,ready,failed};
    using RearmProvider=RearmResult(*)(void*,uintptr_t,NativeCombatSession::Config&,ReplayDependencySource&);
    static bool valid_successor(const NativeCombatSession::Config& prior,const NativeCombatSession::Config& config,uintptr_t object){
        const auto& old=prior.identity;const auto& next=config.identity;
        return NativeCombatSession::valid_config(config) && config.clock_object==object && config.base==prior.base &&
            config.original==prior.original && next.session==old.session && next.peer==old.peer && next.slot==old.slot &&
            next.thread==old.thread && next.generation>=old.generation && next.event>old.event &&
            config.offer.ticket!=prior.offer.ticket && config.offer.build==prior.offer.build;
    }
private:
    DWORD owner_;
    unsigned preparation_stage_{};
    CombatStartMailbox mailbox_;
    CombatStartExchange exchange_;
    NativeStartHold<> hold_;
    std::unique_ptr<NativeCombatSession> session_;
    NativeCombatSession::Config config_;
    bool attempted_{},published_{};
    uint64_t hold_begin_{};
    void finish_hold_timing(){if(hold_begin_){native_startup_timing.add(NativeStartupTiming::hold_wall,config_.identity.round,hold_begin_,NativeRollbackTiming::now(),owner_);hold_begin_=0;}}
    Phase phase_{Phase::idle};
    NativeCombatArming* previous_{};
    std::unique_ptr<NativeCombatArming> successor_;
    observation::Shared* shared_{};
    uint64_t steam_peer_{};
    void* rearm_context_{};RearmProvider rearm_provider_{};
    native_clock_hook::StartupPoll poll_route_{this,[](void* context,uintptr_t object)noexcept{
        auto& self=*static_cast<NativeCombatArming*>(context);
        if(GetCurrentThreadId()!=self.owner_)return;
        const bool completed_ack=automatic_play::enabled && self.session_ && self.session_->status.match_completed.load() &&
            automatic_play::report_saved.load()==self.config_.identity.session;
        if(object!=self.config_.clock_object && !completed_ack)return;
        if(self.session_ && self.session_->retired()){
            self.phase_=Phase::retired;
            if(!self.hold_.retire()){self.stop();return;}
            if(automatic_play::enabled && self.session_->status.match_completed.load() &&
               automatic_play::report_saved.load()==self.config_.identity.session){
                if(native_clock_hook::start_hold.load()!=&self.hold_ ||
                   native_clock_hook::startup_poll.load()!=&self.poll_route_ ||
                   combat_start_exchange.load()!=&self.exchange_ || !self.session_->detach_completed_match()){
                    self.stop();return;
                }
                native_clock_hook::start_hold.store(nullptr,std::memory_order_release);
                combat_start_exchange.store(nullptr,std::memory_order_release);
                native_clock_hook::startup_poll.store(nullptr,std::memory_order_release);
                return;
            }
            try {self.rearm(object);}catch(...){self.stop();}
            return;
        }
        // Native tail work may poll the clock before the wrapped call returns.
        // Keep the settled old clock owned/withheld until receipt verification;
        // do not interpret an intermediate round event as a completed handoff.
        if(self.session_ && self.session_->awaiting_native_return())return;
        const auto round=round_event::snapshot();
        if(self.phase_==Phase::active && (round.round!=self.config_.identity.round || round.counter!=self.config_.identity.event ||
            round.generation!=self.config_.identity.generation)){self.stop(CombatRuntimeStatus::round_boundary);return;}
        if(object!=self.config_.clock_object)return;
        try {self.poll();}catch(...){self.stop();}
    }};
    bool stop(CombatRuntimeStatus::Reason reason=CombatRuntimeStatus::backend_failure){
        finish_hold_timing();
        const bool released=!previous_ && published_ &&
            hold_.release_unactivated(combat_routes.load()!=nullptr || native_clock_hook::route.load()!=nullptr ||
                exclusive_battle_route.load()!=nullptr);
        if(!released)hold_.cancel();
        phase_=Phase::restart_required;
        if(session_){session_->status.startup_hold_released=released;session_->terminate(reason);}return false;
    }
    bool rearm(uintptr_t object){
        if(!rearm_provider_ || successor_)return true;
        NativeStartupScope measured(NativeStartupTiming::rearm_poll,config_.identity.round);
        if(!session_ || !session_->retired() || !shared_ || session_->status.stop_requested.load())return stop();
        NativeCombatSession::Config config;ReplayDependencySource source;
        const auto result=[&]{NativeStartupScope provider(NativeStartupTiming::rearm_provider,config_.identity.round);return rearm_provider_(rearm_context_,object,config,source);}();
        if(result==RearmResult::waiting)return true;
        if(result==RearmResult::failed)return stop();
        const auto& next=config.identity;
        if(!valid_successor(config_,config,object))return stop(CombatRuntimeStatus::activation_failure);
        const auto fight=sc6::RoundFightReadiness::read(config.base,next.round,next.generation,next.frame);
        if(!fight.permits(next.round,next.generation,next.frame,owner_)){
            const auto* preparation=combat_round_preparation.load(std::memory_order_acquire);
            if(preparation && preparation->reserved() && preparation->matches(config.offer))
                return stop(CombatRuntimeStatus::activation_failure);
            return true;
        }
        successor_=std::make_unique<NativeCombatArming>(this);
        if(!successor_->automatic_rearm(rearm_context_,rearm_provider_) ||
            !successor_->prepare(config,source,*shared_,steam_peer_,true))return stop(CombatRuntimeStatus::activation_failure);
        return true;
    }
public:
    explicit NativeCombatArming(NativeCombatArming* previous=nullptr):owner_(GetCurrentThreadId()),mailbox_(owner_),previous_(previous){}
    NativeCombatArming(const NativeCombatArming&)=delete;
    NativeCombatArming& operator=(const NativeCombatArming&)=delete;
    Phase phase()const{return phase_;}
    unsigned preparation_stage()const{return preparation_stage_;}
    void preparation_failed()noexcept{
        if(GetCurrentThreadId()==owner_)stop(CombatRuntimeStatus::activation_failure);
    }
    bool automatic_rearm(void* context,RearmProvider provider){
        if(attempted_ || rearm_provider_ || !context || !provider || GetCurrentThreadId()!=owner_)return false;
        rearm_context_=context;rearm_provider_=provider;return true;
    }
    bool prepare(const NativeCombatSession::Config& requested,ReplayDependencySource source,
        observation::Shared& shared,uint64_t steam_peer,bool quiescent_verified,NativeRollingHistory<>* prepared_history=nullptr){
        auto config=requested;
        const auto* transition=combat_round_preparation.load(std::memory_order_acquire);
        const bool prepared_delay=transition && (previous_ || transition->reserved()) && transition->fresh(CombatStartSync::now()) &&
            transition->context(steam_peer,config.offer.sender,config.offer.recipient,config.offer.build) && transition->matches(config.offer);
        if(!prepared_delay)config.offer.delay=sc6::InputDelayBuildPolicy::proposal(config.prediction_enabled,sc6::overlay_ping.delay_proposal(steam_peer));
        NativeStartupScope measured(previous_?NativeStartupTiming::prepare_rearm:NativeStartupTiming::prepare_first,config.identity.round);
        // Refuse before a native hold, resource access, or route publication.
        preparation_stage_=1;
        if(!config.general_restore || !NativeBoundarySnapshot::general_restore_implemented)return false;
        if(attempted_ || GetCurrentThreadId()!=owner_ || !quiescent_verified)return false;
        attempted_=true;phase_=Phase::preparing;config_=config;
        shared_=&shared;steam_peer_=steam_peer;
        if(previous_ && (previous_->owner_!=owner_ || !previous_->session_ || !previous_->session_->retired() ||
            previous_->shared_!=&shared || previous_->steam_peer_!=steam_peer ||
            !previous_->session_->owns_current_routes()))return stop();
        preparation_stage_=2;
        if(!NativeCombatSession::valid_config(config) || shared.reserved!=observation::owned_mode || config.identity.thread!=owner_ || !native_clock_hook::installed || native_clock_hook::installed_base!=config.base ||
            (!previous_ && combat_routes.load()) || native_clock_hook::route.load() ||
            native_clock_hook::start_hold.load()!=(previous_?&previous_->hold_:nullptr) ||
            exclusive_battle_route.load() || scheduler_input_sink.load() ||
            combat_start_exchange.load()!=(previous_?&previous_->exchange_:nullptr) ||
            native_clock_hook::startup_poll.load()!=(previous_?&previous_->poll_route_:nullptr))return stop();
        // Verify the actual supplied battle boundary before any publication.
        preparation_stage_=3;
        NativeBattleBoundaryReader<> reader;
        if(!reader.bind(config.base,config.caller,config.identity,config.image_verified) ||
            !reader.frontier(config.identity.frame))return stop();
        preparation_stage_=7;
        const auto fight=RoundFightReadiness::read(config.base,config.identity.round,config.identity.generation,config.identity.frame);
        if(!fight.permits(config.identity.round,config.identity.generation,config.identity.frame,owner_))return stop();
        preparation_stage_=4;
        if(!exchange_.attach(mailbox_) || !exchange_.begin(steam_peer,config.offer,previous_?&previous_->exchange_:nullptr))return stop();
        preparation_stage_=5;
        {NativeStartupScope allocation(previous_?NativeStartupTiming::session_rearm:NativeStartupTiming::session_first,config.identity.round);
        session_=std::make_unique<NativeCombatSession>(config,source,previous_?previous_->session_.get():nullptr,prepared_history);}
        if(!session_->prepare(shared) || !hold_.bind(config.base,config.clock_object,owner_,config.clock_identity,config.clock_frame,
            GetTickCount64(),10000,config.image_verified,config.bilateral_private,config.boundaries_owned))return stop();
        // There is one owner-thread publisher. Consumers only load these;
        // failure after the first publication retains ownership for restart.
        preparation_stage_=6;
        NativeStartHold<>* empty_hold=previous_?&previous_->hold_:nullptr;
        if(!native_clock_hook::start_hold.compare_exchange_strong(empty_hold,&hold_,std::memory_order_release))return stop();
        published_=true;
        CombatRuntimeStatus* empty_status=previous_?&previous_->session_->status:nullptr;
        if(!combat_runtime_status.compare_exchange_strong(empty_status,&session_->status,std::memory_order_release))return stop();
        if(!hold_.announce(exchange_,config.offer.ticket,config.clock_object,GetTickCount64()))return stop();
        const bool reserved_ready=exchange_.prime_reservation(transition,CombatStartSync::now());
        CombatStartExchange* empty_exchange=previous_?&previous_->exchange_:nullptr;
        if(!combat_start_exchange.compare_exchange_strong(empty_exchange,&exchange_,std::memory_order_release))return stop();
        phase_=Phase::holding;hold_begin_=NativeRollbackTiming::now();
        const native_clock_hook::StartupPoll* empty_poll=previous_?&previous_->poll_route_:nullptr;
        if(!native_clock_hook::startup_poll.compare_exchange_strong(empty_poll,&poll_route_,std::memory_order_release))return stop();
        // The prepared mailbox can activate at this same safe owner boundary;
        // otherwise a whole native tick would be lost waiting for worker polling.
        if(reserved_ready)return poll();
        return true;
    }
    bool poll(){
        if(GetCurrentThreadId()!=owner_)return false;
        if(exchange_.failed())return stop(CombatRuntimeStatus::activation_failure);
        if(session_ && session_->status.stop_requested.load(std::memory_order_acquire))return stop(CombatRuntimeStatus::requested);
        if(phase_==Phase::active){
            if(session_->complete()){session_->terminate(CombatRuntimeStatus::capture_limit);phase_=Phase::complete;return true;}
            return session_->ready()?true:stop();
        }
        if(phase_!=Phase::holding)return false;
        if(!hold_.ready(config_.clock_object,GetTickCount64()))return stop();
        CombatStartAgreement agreement;
        if(!mailbox_.take(agreement))return true;
        finish_hold_timing();
        NativeStartupScope activation(NativeStartupTiming::activation,config_.identity.round);
        if(!session_->activate(agreement))return stop(CombatRuntimeStatus::activation_failure);
        phase_=Phase::active;return true;
    }
    bool requires_process_lifetime()const{return published_;}
};
}
