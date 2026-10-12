#pragma once
#include "NativeCombatArming.hpp"
#include "NativeCombatConfiguration.hpp"
#include "ReplayOwnedResources.hpp"
#include "CombatPeerContext.hpp"
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
    std::atomic<bool> cancelled_{};
    bool queued_{};
    observation::Shared& shared_;
    uint64_t steam_peer_,deadline_;
    std::atomic<State> state_{State::waiting};
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
        unsigned identity{},frame{};
        if(GetTickCount64()>=deadline_ || GetCurrentThreadId()!=config_.identity.thread ||
            !clock_frame(object,identity,frame) || identity!=config_.clock_identity || frame>config_.clock_frame){state_=State::rejected;return;}
        if(frame<config_.clock_frame)return;
        state_=State::preparing;
        try {
            if(source_provider_)source_=source_provider_(source_context_);
            if(!source_.available()){state_=State::rejected;return;}
            arming_=std::make_unique<NativeCombatArming>();
            state_=arming_->prepare(config_,source_,shared_,steam_peer_,true)?State::prepared:State::rejected;
        }catch(...){state_=State::rejected;}
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
    // Configure before queue; resolve owner-thread resources only when the
    // selected native clock actually reaches the agreed boundary.
    bool source_on_owner(void* context,ReplayDependencySource (*provider)(void*)){
        if(queued_ || state()!=State::waiting || source_provider_ || !context || !provider)return false;
        source_context_=context;source_provider_=provider;return true;
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
