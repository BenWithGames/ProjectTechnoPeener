#pragma once
#include <ostream>
#include "NativeCombatArming.hpp"
#include "NativeCombatConfiguration.hpp"
#include "ReplayOwnedResources.hpp"
#include "CombatPeerContext.hpp"
#include "RearmPeerAdmission.hpp"
#include "RoundTransitionReservation.hpp"
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
    std::array<std::unique_ptr<CombatRoundPreparation>,16> transition_preparations_;
    unsigned transition_count_{},prepared_event_{},prepared_round_{};
    uint64_t prepared_generation_{};
    ReplayDependencySource prepared_source_;
    RoundTransitionReservation transition_reservation_;
    uint64_t transition_reserved_at_{};
    const unsigned transition_owner_{config_.identity.thread};
    const uintptr_t transition_base_{config_.base};
    const uint64_t transition_session_{config_.identity.session};
    std::atomic<bool> transition_barrier_active_{};
    RoundTransitionBoundaryGuard transition_barrier_{this,[](void* context)noexcept{
        auto& self=*static_cast<NativeArmingDispatch*>(context);
        if(!self.transition_barrier_active_.load(std::memory_order_acquire))return true;
        unsigned battle{};
        if(GetCurrentThreadId()==self.transition_owner_ &&
           NativeBattleBoundaryAccess::counter(self.transition_base_,battle) &&
           self.transition_reservation_.native_step_allowed(battle))return true;
        self.transition_seen_battle_=battle;self.transition_failure_=11;
        if(auto* runtime=combat_runtime_status.load(std::memory_order_acquire);
           runtime && runtime->session==self.transition_session_){
            runtime->rearm_failure=19;runtime->restart_required=true;runtime->request_stop();
        }
        return false;
    }};
    std::atomic<unsigned> transition_reserved_{},transition_arrived_{},transition_projection_waits_{};
    std::atomic<unsigned> transition_target_battle_{},transition_target_clock_{},transition_seen_battle_{},transition_seen_clock_{};
    std::atomic<unsigned> transition_elapsed_{},transition_duration_{},transition_readiness_{};
    std::atomic<unsigned> transition_prepared_{},transition_used_{},transition_waits_{},transition_failure_{};
    std::atomic<unsigned> transition_wait_mask_{},transition_wait_battle_{},transition_wait_clock_{},transition_wait_identity_{};
    std::atomic<unsigned> transition_wait_event_{},transition_wait_round_{};
    std::atomic<uint64_t> transition_wait_generation_{},transition_wait_now_{},transition_wait_reserved_at_{};
    std::atomic<uint64_t> rearm_peer_control_tick_{},rearm_peer_heartbeat_tick_{};
    std::atomic<unsigned> terminal_polls_{},terminal_other_clock_polls_{},terminal_completed_{};
    NativeCombatArming::RearmResult production_rearm(uintptr_t object,NativeCombatSession::Config& out,ReplayDependencySource& source){
        using R=NativeCombatArming::RearmResult;
        out={};source={};
        auto* runtime=combat_runtime_status.load(std::memory_order_acquire);
        if(runtime && runtime->session!=config_.identity.session)runtime=nullptr;
        if(runtime)++runtime->rearm_polls;
        auto finish=[&](R result,unsigned failure=0){
            // Every waiting exit (including peer/context or phase waits) must
            // honor an advertised reservation. It may not let stock dispatch
            // cross the promised frontier while preparation is incomplete.
            if(result==R::waiting && transition_reservation_.reserved()){
                unsigned battle{},identity{},clock{};
                const auto boundary=round_event::snapshot();
                const bool battle_read=NativeBattleBoundaryAccess::counter(config_.base,battle);
                const bool clock_read=clock_frame(object,identity,clock);
                const auto wait_now=GetTickCount64();
                const auto mask=(!battle_read?512u:0u) | (!clock_read?1024u:0u) |
                    transition_reservation_.wait_rejections(boundary.counter,boundary.round,boundary.generation,
                        battle,clock,identity,wait_now,transition_reserved_at_);
                if(mask){
                    transition_wait_mask_=mask;transition_wait_battle_=battle;transition_wait_clock_=clock;
                    transition_wait_identity_=identity;transition_wait_event_=boundary.counter;transition_wait_round_=boundary.round;
                    transition_wait_generation_=boundary.generation;transition_wait_now_=wait_now;
                    transition_wait_reserved_at_=transition_reserved_at_;
                    transition_failure_=10;result=R::failed;failure=18;
                }
            }
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
        rearm_peer_control_tick_=peer.control_tick;rearm_peer_heartbeat_tick_=peer.heartbeat_tick;
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
        unsigned frame{};if(!NativeBattleBoundaryAccess::counter(config_.base,frame)){
            if(inspection==Clock::Inspection::waiting){
                if(runtime){++runtime->rearm_clock_waits;runtime->rearm_clock_phase_mask.fetch_or(1u<<observed.clock.phase);}
                return finish(R::waiting);
            }
            return finish(R::failed,5);
        }
        if(runtime)runtime->rearm_frame=frame;
        const auto fight=RoundFightReadiness::read(config_.base,round.round,round.generation,frame);
        transition_seen_battle_=frame;transition_seen_clock_=observed.clock.frame;
        transition_elapsed_=fight.start_elapsed;transition_duration_=fight.start_duration;transition_readiness_=fight.status;
        // Non-running clock phases may legitimately have no readable battle
        // mode yet. Preserve the prior wait behavior while observing early.
        if(!fight.status && inspection!=Clock::Inspection::waiting)return finish(R::failed,6);
        // Start updates must keep running (RVA384040 and 3F5D20). Only acquire
        // owned resources and negotiate immutable round intent here: no native
        // clock hold, snapshot, prediction, or activation in the transition.
        if(prepared_event_ && (prepared_event_!=round.counter || prepared_round_!=round.round || prepared_generation_!=round.generation)){
            transition_failure_=1;return finish(R::failed,11);
        }
        if(transition_reservation_.reserved() &&
           (now<transition_reserved_at_ || now-transition_reserved_at_>=10000 ||
            frame>transition_reservation_.battle() ||
            (frame==transition_reservation_.battle() &&
             !fight.permits(round.round,round.generation,frame,observed.thread)))){
            transition_failure_=9;return finish(R::failed,17);
        }
        if(fight.intro(round.round,round.generation,observed.thread)){
            ++transition_waits_;
            // The clock hook executes before native service/advance, on the
            // owner. Do not infer a boundary from cross-thread memory sampling.
            const bool countdown=inspection==Clock::Inspection::ready && !fight.pending_state &&
                transition_reservation_.observe({round.counter,round.round,frame,observed.clock.frame,
                    observed.clock.identity,fight.start_elapsed,fight.start_duration,round.generation});
            if(transition_reservation_.failed()){
                transition_failure_=6;return finish(R::failed,14);
            }
            if(!prepared_event_ && rearm_resources_->successor_dependencies_current_on_owner()){
                const bool reserve=countdown && transition_reservation_.can_reserve();
                // Let at least two distinct updates establish the projection.
                // A short Start uses the unchanged held-boundary protocol.
                if(!reserve && fight.start_duration>=fight.start_elapsed &&
                   fight.start_duration-fight.start_elapsed>=30){++transition_projection_waits_;return finish(R::waiting);}
                if(transition_count_==transition_preparations_.size()){transition_failure_=2;return finish(R::failed,12);}
                try {prepared_source_=rearm_resources_->successor_source_on_owner();}
                catch(...){transition_failure_=3;return finish(R::failed,9);}
                if(!prepared_source_.available()){transition_failure_=4;return finish(R::failed,8);}
                auto intent=config_.offer;intent.round=round.round;intent.event=round.counter;
                intent.first_frame=reserve?transition_reservation_.battle():0;++intent.ticket;
                intent.delay=InputDelayBuildPolicy::proposal(config_.prediction_enabled,overlay_ping.delay_proposal(steam_peer_));
                auto preparation=std::make_unique<CombatRoundPreparation>(steam_peer_,intent,CombatStartSync::now(),reserve);
                if(reserve){
                    if(!transition_reservation_.reserve()){transition_failure_=7;return finish(R::failed,15);}
                    transition_reserved_at_=now;
                    transition_target_battle_=transition_reservation_.battle();transition_target_clock_=transition_reservation_.projected_clock();
                    transition_barrier_active_.store(true,std::memory_order_release);
                    round_transition_boundary_guard.store(&transition_barrier_,std::memory_order_release);
                    ++transition_reserved_;
                }
                preparation->previous=transition_count_?transition_preparations_[transition_count_-1].get():nullptr;
                auto* published=preparation.get();transition_preparations_[transition_count_++]=std::move(preparation);
                prepared_event_=round.counter;prepared_round_=round.round;prepared_generation_=round.generation;
                ++transition_prepared_;combat_round_preparation.store(published,std::memory_order_release);
            }
            return finish(R::waiting);
        }
        if(inspection==Clock::Inspection::waiting){
            if(runtime){++runtime->rearm_clock_waits;runtime->rearm_clock_phase_mask.fetch_or(1u<<observed.clock.phase);}
            return finish(R::waiting);
        }
        if(!fight.permits(round.round,round.generation,frame,observed.thread))return finish(R::waiting);
        if(transition_reservation_.reserved()){
            if(!transition_reservation_.arrives(round.counter,round.round,round.generation,frame,
                    observed.clock.frame,observed.clock.identity)){
                transition_failure_=8;return finish(R::failed,16);
            }
            ++transition_arrived_;
        }
        auto admission=config_;
        admission.identity.round=admission.offer.round=round.round;
        admission.identity.event=admission.offer.event=round.counter;
        admission.identity.generation=round.generation;
        admission.identity.frame=admission.offer.first_frame=frame;++admission.offer.ticket;
        if(prepared_event_)admission.offer.delay=transition_preparations_[transition_count_-1]->delay();
        const auto assembled=NativeCombatConfiguration<>::assemble(observed,now,admission,out);
        if(assembled!=NativeCombatConfiguration<>::Result::ready)return finish(R::failed,100+static_cast<unsigned>(assembled));
        if(prepared_event_){
            if(!rearm_resources_->successor_dependencies_current_on_owner()){
                transition_failure_=5;return finish(R::failed,13);
            }
            source=prepared_source_;++transition_used_;
        }else{
            try {source=rearm_resources_->successor_source_on_owner();}
            catch(...){return finish(R::failed,9);}
        }
        if(!source.available())return finish(R::failed,8);
        // prepare() still binds a physical local hold and validates this exact
        // Fight boundary. An authenticated future reservation can supply the
        // peer promise; otherwise the existing bilateral timed hold is used.
        prepared_event_=prepared_round_=0;prepared_generation_=0;prepared_source_={};
        transition_reservation_={};
        transition_barrier_active_.store(false,std::memory_order_release);
        const RoundTransitionBoundaryGuard* barrier=&transition_barrier_;
        round_transition_boundary_guard.compare_exchange_strong(barrier,nullptr,std::memory_order_acq_rel);
        transition_reserved_at_=0;
        config_=out;return finish(R::ready);
    }
    std::atomic<bool> cancelled_{};
    bool queued_{};
    bool first_fight_{};
    bool first_workspace_ready_{};
    std::atomic<unsigned> first_intro_reserved_{},first_intro_used_{},first_intro_failure_{};
    bool prepare_first_intro(const RoundFightReadiness& fight,unsigned clock,unsigned identity){
        const auto& key=config_.identity;
        if(!fight.intro(key.round,key.generation,key.thread))return true;
        if(fight.pending_state)return true;
        NativeClockAccess<>::State state;NativeClockAccess<>::Failure failure{};unsigned exception{};
        const auto inspection=NativeClockAccess<>::inspect_rearm(config_.base,config_.clock_object,state,failure,exception);
        if(inspection!=NativeClockAccess<>::Inspection::ready)return true;
        if(!transition_reservation_.observe({key.event,key.round,fight.frame,clock,identity,
                fight.start_elapsed,fight.start_duration,key.generation})){
            if(transition_reservation_.failed()){first_intro_failure_=1;return false;}
            return true;
        }
        if(transition_reservation_.reserved() || !transition_reservation_.can_reserve())return true;
        NativeStartupScope measured(NativeStartupTiming::first_intro_prepare,key.round);
        // Resolve native dependencies on their owner, while Start still runs.
        // No snapshot, input replacement or native load is authorized here.
        if(rearm_resources_ && !rearm_resources_->successor_dependencies_current_on_owner())return true;
        const auto source=source_provider_?source_provider_(source_context_):source_;
        if(!source.available())return true;
        if(!first_workspace_ready_){
            if(!NativeCombatSession::prepare_workspace(config_.base)){first_intro_failure_=2;return false;}
            first_workspace_ready_=true;
        }
        auto offer=config_.offer;offer.first_frame=transition_reservation_.battle();
        offer.delay=InputDelayBuildPolicy::proposal(config_.prediction_enabled,overlay_ping.delay_proposal(steam_peer_));
        auto preparation=std::make_unique<CombatRoundPreparation>(steam_peer_,offer,CombatStartSync::now(),true);
        if(!transition_reservation_.reserve()){first_intro_failure_=3;return false;}
        source_=source;transition_reserved_at_=GetTickCount64();
        transition_target_battle_=offer.first_frame;transition_target_clock_=transition_reservation_.projected_clock();
        // Install the actual battle-entry barrier before advertising the promise.
        transition_barrier_active_.store(true,std::memory_order_release);
        round_transition_boundary_guard.store(&transition_barrier_,std::memory_order_release);
        auto* published=preparation.get();transition_preparations_[transition_count_++]=std::move(preparation);
        config_.offer.delay=offer.delay;
        combat_round_preparation.store(published,std::memory_order_release);++first_intro_reserved_;
        return true;
    }
    observation::Shared& shared_;
    uint64_t steam_peer_,deadline_;
    std::atomic<State> state_{State::waiting};
    std::atomic<unsigned> stage_{0};
    // 1 deadline, 2 thread, 3 clock read, 4 identity, 5 missed target;
    // 6 resources, 7 rearm wiring, 8 native prepare, 9 exception;
    // 10 intro round changed, 11 battle counter, 12 fresh assembly, 13 unreadable readiness.
    std::atomic<unsigned> rejection_{0},observed_frame_{0};
    std::atomic<unsigned> intro_waits_{},fight_clock_{},fight_battle_{},assembly_rejection_{};
    std::unique_ptr<NativeCombatArming> arming_;
    native_clock_hook::StartupPoll callback_{this,[](void* context,uintptr_t object)noexcept{
        static_cast<NativeArmingDispatch*>(context)->poll(object);
    }};
    static bool clock_frame(uintptr_t object,unsigned& identity,unsigned& frame)noexcept{
        __try {identity=*reinterpret_cast<unsigned*>(object+0x3A0);frame=*reinterpret_cast<unsigned*>(object+0x3A4);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    void poll(uintptr_t object)noexcept{
        // Results can service a different clock. Only inspect terminal state
        // on the verified owner thread after all old combat work has retired.
        if(!cancelled_.load() && state_.load()==State::prepared && rearm_resources_ &&
           GetCurrentThreadId()==config_.identity.thread){
            auto* runtime=combat_runtime_status.load(std::memory_order_acquire);
            const auto* routes=combat_routes.load(std::memory_order_acquire);
            if(runtime && runtime->session==config_.identity.session && routes && routes->retired() &&
               runtime->history_retired.load() && runtime->native_outer_committed.load() &&
               !runtime->active.load() && !runtime->restart_required.load() && !runtime->stop_requested.load() &&
               runtime->updates.load()>0 && !runtime->match_completed.load()){
                ++terminal_polls_;if(object!=config_.clock_object)++terminal_other_clock_polls_;
                try {
                    if(rearm_resources_->match_end_requested.load() ||
                       rearm_resources_->finish_live_match(config_.identity.thread,true)){
                        rearm_resources_->frame(runtime->settlement_frame.load(),false);
                        if(rearm_resources_->failed.load())runtime->request_stop();
                        else if(rearm_resources_->match_end_clean.load()){
                            ++terminal_completed_;runtime->match_completed.store(true,std::memory_order_release);
                        }
                    }
                }catch(...){runtime->request_stop();}
            }
        }
        if(automatic_play::enabled && state_.load()==State::prepared &&
           automatic_play::report_saved.load()==config_.identity.session &&
           !combat_routes.load() && !native_clock_hook::startup_poll.load() && !native_clock_hook::start_hold.load()){
            auto* runtime=combat_runtime_status.load();
            if(runtime && runtime->session==config_.identity.session && runtime->match_completed.load() &&
               !runtime->restart_required.load() && rearm_resources_ && rearm_resources_->match_end_clean.load() &&
               rearm_resources_->retired.load() && !rearm_resources_->failed.load() && GetCurrentThreadId()==config_.identity.thread){
                if(!rearm_resources_->snapshots.discard_released_storage()){automatic_play::cleanup_failed=true;return;}
                const native_clock_hook::StartupPoll* expected=&callback_;
                if(native_clock_hook::preparation_poll.compare_exchange_strong(expected,nullptr)){
                    combat_resource_preparation.store(nullptr,std::memory_order_release);
                    combat_runtime_status.store(nullptr,std::memory_order_release);
                    automatic_play::released.store(config_.identity.session,std::memory_order_release);
                }
            }
            return;
        }
        if(object!=config_.clock_object)return;
        if(cancelled_.load(std::memory_order_acquire)){
            if(auto* status=combat_runtime_status.load(std::memory_order_acquire);status && status->session==config_.identity.session)status->request_stop();
            if(state_.load()==State::waiting)state_=State::rejected;
            return;
        }
        if(state_.load()!=State::waiting)return;
        stage_=1;
        unsigned identity{},frame{};
        const bool readable=clock_frame(object,identity,frame);
        observed_frame_=frame;
        const unsigned reason=GetTickCount64()>=deadline_?1u:GetCurrentThreadId()!=config_.identity.thread?2u:
            !readable?3u:identity!=config_.clock_identity?4u:(!first_fight_ && frame>config_.clock_frame)?5u:0u;
        if(reason){rejection_=reason;State expected=State::waiting;state_.compare_exchange_strong(expected,State::rejected);return;}
        if(first_fight_){
            const auto round=round_event::snapshot();unsigned battle{};
            if(round.round!=config_.identity.round || round.counter!=config_.identity.event || round.generation!=config_.identity.generation){
                rejection_=10;state_=State::rejected;return;
            }
            if(!NativeBattleBoundaryAccess::counter(config_.base,battle)){rejection_=11;state_=State::rejected;return;}
            const auto fight=RoundFightReadiness::read(config_.base,round.round,round.generation,battle);
            if(!fight.status){rejection_=13;state_=State::rejected;return;}
            if(transition_reservation_.reserved() && (GetTickCount64()<transition_reserved_at_ ||
               GetTickCount64()-transition_reserved_at_>=10000 || battle>transition_reservation_.battle())){
                first_intro_failure_=4;rejection_=14;state_=State::rejected;return;
            }
            if(!fight.permits(round.round,round.generation,battle,config_.identity.thread)){
                ++intro_waits_;
                if(transition_reservation_.reserved() && battle==transition_reservation_.battle()){
                    first_intro_failure_=5;rejection_=14;state_=State::rejected;return;
                }
                try{if(!prepare_first_intro(fight,frame,identity)){rejection_=14;state_=State::rejected;}}
                catch(...){first_intro_failure_=6;rejection_=9;state_=State::rejected;}
                return;
            }
            if(transition_reservation_.reserved() &&
               (!transition_reservation_.arrives(round.counter,round.round,round.generation,battle,frame,identity) ||
                (rearm_resources_ && !rearm_resources_->successor_dependencies_current_on_owner()))){
                first_intro_failure_=7;rejection_=14;state_=State::rejected;return;
            }
            fight_clock_=frame;fight_battle_=battle;
            NativeClockDiscovery::Observation observed;observed.base=config_.base;observed.object=object;
            observed.thread=GetCurrentThreadId();observed.tick=GetTickCount64();
            observed.valid=NativeClockAccess<>::inspect(config_.base,object,observed.clock);
            auto admission=config_;admission.identity.frame=admission.offer.first_frame=battle;
            NativeCombatSession::Config assembled;
            const auto result=NativeCombatConfiguration<>::assemble(observed,observed.tick,admission,assembled);
            assembly_rejection_=static_cast<unsigned>(result);
            if(result!=NativeCombatConfiguration<>::Result::ready){rejection_=12;state_=State::rejected;return;}
            config_=assembled;
        }else if(frame<config_.clock_frame)return;
        State expected=State::waiting;
        if(!state_.compare_exchange_strong(expected,State::preparing))return;
        stage_=2;
        try {
            if(source_provider_)source_=source_provider_(source_context_);
            if(!source_.available()){rejection_=6;state_=State::rejected;return;}
            stage_=3;arming_=std::make_unique<NativeCombatArming>();
            if(rearm_provider_ && !arming_->automatic_rearm(rearm_context_,rearm_provider_)){
                rejection_=7;state_=State::rejected;return;
            }
            stage_=4;
            auto* prepared_history=rearm_resources_ && rearm_resources_->live_prediction_enabled.load() &&
                !rearm_resources_->capture_enabled.load()?rearm_resources_->snapshots.release_unused_history():nullptr;
            const bool prepared=arming_->prepare(config_,source_,shared_,steam_peer_,true,prepared_history);
            if(prepared && transition_reservation_.reserved()){
                // A physical hold now owns this exact boundary, even if packets
                // were late and the existing held-start protocol is still needed.
                ++first_intro_used_;transition_reservation_={};
                transition_barrier_active_.store(false,std::memory_order_release);
                const RoundTransitionBoundaryGuard* barrier=&transition_barrier_;
                round_transition_boundary_guard.compare_exchange_strong(barrier,nullptr,std::memory_order_acq_rel);
                transition_reserved_at_=0;
            }
            stage_=prepared?100:10+arming_->preparation_stage();
            rejection_=prepared?0:8;state_=prepared?State::prepared:State::rejected;
        }catch(...){
            // A hold may already be published. Latch failure on that owner;
            // reporting rejection alone would leave native servicing enabled.
            rejection_=9;
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
    unsigned rejection()const{return rejection_.load();}
    unsigned observed_frame()const{return observed_frame_.load();}
    bool withdraw_unstarted(){
        State expected=State::waiting;
        if(!state_.compare_exchange_strong(expected,State::rejected) &&
           !(expected==State::rejected && stage_.load()==1 && rejection_.load()==5))return false;
        cancelled_.store(true,std::memory_order_release);
        const native_clock_hook::StartupPoll* callback=&callback_;
        return native_clock_hook::preparation_poll.compare_exchange_strong(callback,nullptr);
    }
    unsigned stage()const{return stage_.load(std::memory_order_acquire);}
    void report(std::ostream& out)const{
        out<<"continuous_first_intro_schema=1\ncontinuous_first_intro_reserved="<<first_intro_reserved_.load()
           <<"\ncontinuous_first_intro_used="<<first_intro_used_.load()
           <<"\ncontinuous_first_intro_failure="<<first_intro_failure_.load()<<'\n';
        out<<"continuous_terminal_owner_polls="<<terminal_polls_.load()
           <<"\ncontinuous_terminal_other_clock_polls="<<terminal_other_clock_polls_.load()
           <<"\ncontinuous_terminal_owner_completed="<<terminal_completed_.load()<<'\n';
        out<<"continuous_intro_waits="<<intro_waits_.load()<<"\ncontinuous_first_safe_clock="<<fight_clock_.load()
           <<"\ncontinuous_first_safe_battle="<<fight_battle_.load()<<"\ncontinuous_fresh_assembly_rejection="<<assembly_rejection_.load()<<'\n';
        out<<"continuous_transition_prepared="<<transition_prepared_.load()<<"\ncontinuous_transition_used="<<transition_used_.load()
           <<"\ncontinuous_transition_waits="<<transition_waits_.load()<<"\ncontinuous_transition_failure="<<transition_failure_.load()<<'\n'
           <<"\ncontinuous_transition_wait_schema=1\ncontinuous_transition_wait_mask="<<transition_wait_mask_.load()
           <<"\ncontinuous_transition_wait_battle="<<transition_wait_battle_.load()
           <<"\ncontinuous_transition_wait_clock="<<transition_wait_clock_.load()
           <<"\ncontinuous_transition_wait_identity="<<transition_wait_identity_.load()
           <<"\ncontinuous_transition_wait_event="<<transition_wait_event_.load()
           <<"\ncontinuous_transition_wait_round="<<transition_wait_round_.load()
           <<"\ncontinuous_transition_wait_generation="<<transition_wait_generation_.load()
           <<"\ncontinuous_transition_wait_now="<<transition_wait_now_.load()
           <<"\ncontinuous_transition_wait_reserved_at="<<transition_wait_reserved_at_.load()
           <<"\ncontinuous_rearm_peer_control_tick="<<rearm_peer_control_tick_.load()
           <<"\ncontinuous_rearm_peer_heartbeat_tick="<<rearm_peer_heartbeat_tick_.load()<<'\n';
        out<<"continuous_transition_reserved="<<transition_reserved_.load()
           <<"\ncontinuous_transition_arrived="<<transition_arrived_.load()
           <<"\ncontinuous_transition_projection_waits="<<transition_projection_waits_.load()<<'\n';
        out<<"continuous_transition_target_battle="<<transition_target_battle_.load()
           <<"\ncontinuous_transition_projected_clock="<<transition_target_clock_.load()
           <<"\ncontinuous_transition_seen_battle="<<transition_seen_battle_.load()
           <<"\ncontinuous_transition_seen_clock="<<transition_seen_clock_.load()
           <<"\ncontinuous_transition_elapsed="<<transition_elapsed_.load()
           <<"\ncontinuous_transition_duration="<<transition_duration_.load()
           <<"\ncontinuous_transition_readiness="<<transition_readiness_.load()<<'\n';
    }
    bool first_fight(){if(queued_ || state()!=State::waiting)return false;first_fight_=true;return true;}
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
        const auto now=GetTickCount64();if(deadline_<=now || deadline_-now>(first_fight_?60000u:10000u))return false;
        const native_clock_hook::StartupPoll* empty=nullptr;
        queued_=true;
        if(native_clock_hook::preparation_poll.compare_exchange_strong(empty,&callback_,std::memory_order_release))return true;
        queued_=false;return false;
    }
};
}
