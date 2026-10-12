#pragma once
#include "NativeCombatSession.hpp"
#include "CombatStartPacket.hpp"
namespace sc6::research {
// Process-lifetime owner of startup routes. Must be heap allocated and called
// at an independently verified quiescent boundary on the simulation thread.
// No UI/file command may call prepare directly from the network worker.
class NativeCombatArming {
public:
    enum class Phase {idle,preparing,holding,active,complete,restart_required};
private:
    DWORD owner_;
    CombatStartMailbox mailbox_;
    CombatStartExchange exchange_;
    NativeStartHold<> hold_;
    std::unique_ptr<NativeCombatSession> session_;
    NativeCombatSession::Config config_;
    bool attempted_{},published_{};
    Phase phase_{Phase::idle};
    native_clock_hook::StartupPoll poll_route_{this,[](void* context,uintptr_t object)noexcept{
        auto& self=*static_cast<NativeCombatArming*>(context);
        if(GetCurrentThreadId()!=self.owner_)return;
        const auto round=round_event::snapshot();
        if(self.phase_==Phase::active && (round.round!=self.config_.identity.round || round.counter!=self.config_.identity.event ||
            round.generation!=self.config_.identity.generation)){self.stop(CombatRuntimeStatus::round_boundary);return;}
        if(object!=self.config_.clock_object)return;
        try {self.poll();}catch(...){self.stop();}
    }};
    bool stop(CombatRuntimeStatus::Reason reason=CombatRuntimeStatus::backend_failure){
        if(session_)session_->terminate(reason);phase_=Phase::restart_required;return false;
    }
public:
    NativeCombatArming():owner_(GetCurrentThreadId()),mailbox_(owner_){}
    NativeCombatArming(const NativeCombatArming&)=delete;
    NativeCombatArming& operator=(const NativeCombatArming&)=delete;
    Phase phase()const{return phase_;}
    bool prepare(const NativeCombatSession::Config& config,ReplayDependencySource source,
        observation::Shared& shared,uint64_t steam_peer,bool quiescent_verified){
        // Refuse before a native hold, resource access, or route publication.
        if(!config.general_restore || !NativeBoundarySnapshot::general_restore_implemented)return false;
        if(attempted_ || GetCurrentThreadId()!=owner_ || !quiescent_verified)return false;
        attempted_=true;phase_=Phase::preparing;config_=config;
        if(!NativeCombatSession::valid_config(config) || shared.reserved!=observation::owned_mode || config.identity.thread!=owner_ || !native_clock_hook::installed || native_clock_hook::installed_base!=config.base ||
            combat_routes.load() || native_clock_hook::route.load() || native_clock_hook::start_hold.load() ||
            exclusive_battle_route.load() || scheduler_input_sink.load() || combat_start_exchange.load() || native_clock_hook::startup_poll.load())return stop();
        // Verify the actual supplied battle boundary before any publication.
        NativeBattleBoundaryReader<> reader;
        if(!reader.bind(config.base,config.caller,config.identity,config.image_verified) ||
            !reader.frontier(config.identity.frame))return stop();
        if(!exchange_.attach(mailbox_) || !exchange_.begin(steam_peer,config.offer))return stop();
        session_=std::make_unique<NativeCombatSession>(config,source);
        if(!session_->prepare(shared) || !hold_.bind(config.base,config.clock_object,owner_,config.clock_identity,config.clock_frame,
            GetTickCount64(),10000,config.image_verified,config.bilateral_private,config.boundaries_owned))return stop();
        // There is one owner-thread publisher. Consumers only load these;
        // failure after the first publication retains ownership for restart.
        NativeStartHold<>* empty_hold=nullptr;
        if(!native_clock_hook::start_hold.compare_exchange_strong(empty_hold,&hold_,std::memory_order_release))return stop();
        published_=true;
        CombatRuntimeStatus* empty_status=nullptr;
        if(!combat_runtime_status.compare_exchange_strong(empty_status,&session_->status,std::memory_order_release))return stop();
        CombatStartExchange* empty_exchange=nullptr;
        if(!combat_start_exchange.compare_exchange_strong(empty_exchange,&exchange_,std::memory_order_release))return stop();
        if(!hold_.announce(exchange_,config.offer.ticket,config.clock_object,GetTickCount64()))return stop();
        phase_=Phase::holding;
        const native_clock_hook::StartupPoll* empty_poll=nullptr;
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
