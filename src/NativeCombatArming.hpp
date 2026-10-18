#pragma once
#include "NativeCombatSession.hpp"
#include "CombatStartPacket.hpp"
#include "RoundFightReadiness.hpp"
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
    CombatStartMailbox mailbox_;
    CombatStartExchange exchange_;
    NativeStartHold<> hold_;
    std::unique_ptr<NativeCombatSession> session_;
    NativeCombatSession::Config config_;
    bool attempted_{},published_{};
    Phase phase_{Phase::idle};
    NativeCombatArming* previous_{};
    std::unique_ptr<NativeCombatArming> successor_;
    observation::Shared* shared_{};
    uint64_t steam_peer_{};
    void* rearm_context_{};RearmProvider rearm_provider_{};
    native_clock_hook::StartupPoll poll_route_{this,[](void* context,uintptr_t object)noexcept{
        auto& self=*static_cast<NativeCombatArming*>(context);
        if(GetCurrentThreadId()!=self.owner_)return;
        if(self.session_ && self.session_->retired()){
            self.phase_=Phase::retired;
            if(!self.hold_.retire()){self.stop();return;}
            try {self.rearm(object);}catch(...){self.stop();}
            return;
        }
        const auto round=round_event::snapshot();
        if(self.phase_==Phase::active && (round.round!=self.config_.identity.round || round.counter!=self.config_.identity.event ||
            round.generation!=self.config_.identity.generation)){self.stop(CombatRuntimeStatus::round_boundary);return;}
        if(object!=self.config_.clock_object)return;
        try {self.poll();}catch(...){self.stop();}
    }};
    bool stop(CombatRuntimeStatus::Reason reason=CombatRuntimeStatus::backend_failure){
        if(session_)session_->terminate(reason);phase_=Phase::restart_required;return false;
    }
    bool rearm(uintptr_t object){
        if(!rearm_provider_ || successor_)return true;
        if(!session_ || !session_->retired() || !shared_ || session_->status.stop_requested.load())return stop();
        NativeCombatSession::Config config;ReplayDependencySource source;
        const auto result=rearm_provider_(rearm_context_,object,config,source);
        if(result==RearmResult::waiting)return true;
        if(result==RearmResult::failed)return stop();
        const auto& next=config.identity;
        if(!valid_successor(config_,config,object))return stop(CombatRuntimeStatus::activation_failure);
        const auto fight=sc6::RoundFightReadiness::read(config.base,next.round,next.generation,next.frame);
        if(!fight.permits(next.round,next.generation,next.frame,owner_))return true;
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
    bool automatic_rearm(void* context,RearmProvider provider){
        if(attempted_ || rearm_provider_ || !context || !provider || GetCurrentThreadId()!=owner_)return false;
        rearm_context_=context;rearm_provider_=provider;return true;
    }
    bool prepare(const NativeCombatSession::Config& config,ReplayDependencySource source,
        observation::Shared& shared,uint64_t steam_peer,bool quiescent_verified){
        // Refuse before a native hold, resource access, or route publication.
        if(!config.general_restore || !NativeBoundarySnapshot::general_restore_implemented)return false;
        if(attempted_ || GetCurrentThreadId()!=owner_ || !quiescent_verified)return false;
        attempted_=true;phase_=Phase::preparing;config_=config;
        shared_=&shared;steam_peer_=steam_peer;
        if(previous_ && (previous_->owner_!=owner_ || !previous_->session_ || !previous_->session_->retired() ||
            previous_->shared_!=&shared || previous_->steam_peer_!=steam_peer ||
            !previous_->session_->owns_current_routes()))return stop();
        if(!NativeCombatSession::valid_config(config) || shared.reserved!=observation::owned_mode || config.identity.thread!=owner_ || !native_clock_hook::installed || native_clock_hook::installed_base!=config.base ||
            (!previous_ && combat_routes.load()) || native_clock_hook::route.load() ||
            native_clock_hook::start_hold.load()!=(previous_?&previous_->hold_:nullptr) ||
            exclusive_battle_route.load() || scheduler_input_sink.load() ||
            combat_start_exchange.load()!=(previous_?&previous_->exchange_:nullptr) ||
            native_clock_hook::startup_poll.load()!=(previous_?&previous_->poll_route_:nullptr))return stop();
        // Verify the actual supplied battle boundary before any publication.
        NativeBattleBoundaryReader<> reader;
        if(!reader.bind(config.base,config.caller,config.identity,config.image_verified) ||
            !reader.frontier(config.identity.frame))return stop();
        if(!exchange_.attach(mailbox_) || !exchange_.begin(steam_peer,config.offer,previous_?&previous_->exchange_:nullptr))return stop();
        session_=std::make_unique<NativeCombatSession>(config,source,previous_?previous_->session_.get():nullptr);
        if(!session_->prepare(shared) || !hold_.bind(config.base,config.clock_object,owner_,config.clock_identity,config.clock_frame,
            GetTickCount64(),10000,config.image_verified,config.bilateral_private,config.boundaries_owned))return stop();
        // There is one owner-thread publisher. Consumers only load these;
        // failure after the first publication retains ownership for restart.
        NativeStartHold<>* empty_hold=previous_?&previous_->hold_:nullptr;
        if(!native_clock_hook::start_hold.compare_exchange_strong(empty_hold,&hold_,std::memory_order_release))return stop();
        published_=true;
        CombatRuntimeStatus* empty_status=previous_?&previous_->session_->status:nullptr;
        if(!combat_runtime_status.compare_exchange_strong(empty_status,&session_->status,std::memory_order_release))return stop();
        CombatStartExchange* empty_exchange=previous_?&previous_->exchange_:nullptr;
        if(!combat_start_exchange.compare_exchange_strong(empty_exchange,&exchange_,std::memory_order_release))return stop();
        if(!hold_.announce(exchange_,config.offer.ticket,config.clock_object,GetTickCount64()))return stop();
        phase_=Phase::holding;
        const native_clock_hook::StartupPoll* empty_poll=previous_?&previous_->poll_route_:nullptr;
        if(!native_clock_hook::startup_poll.compare_exchange_strong(empty_poll,&poll_route_,std::memory_order_release))return stop();
        return true;
    }
    bool poll(){
        if(GetCurrentThreadId()!=owner_)return false;
        if(session_ && session_->status.stop_requested.load(std::memory_order_acquire))return stop(CombatRuntimeStatus::requested);
        if(phase_==Phase::active){
            if(session_->complete()){session_->terminate(CombatRuntimeStatus::capture_limit);phase_=Phase::complete;return true;}
            return session_->ready()?true:stop();
        }
        if(phase_!=Phase::holding)return false;
        if(!hold_.ready(config_.clock_object,GetTickCount64()))return stop();
        CombatStartAgreement agreement;
        if(!mailbox_.take(agreement))return true;
        if(!session_->activate(agreement))return stop(CombatRuntimeStatus::activation_failure);
        phase_=Phase::active;return true;
    }
    bool requires_process_lifetime()const{return published_;}
};
}
