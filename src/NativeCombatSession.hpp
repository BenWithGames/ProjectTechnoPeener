#pragma once
#include "NativeSchedulerStorage.hpp"
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
    NativeBattleBoundaryReader<> reader_;
    OwnedInputPublication publication_;
    Driver driver_;
    SchedulerInputSink sink_;
    CombatRouteBundle routes_;
    bool completed_{},published_{};
    bool attempted_{},prepared_{};
    bool cleanup_attempted_{};
public:
    CombatRuntimeStatus status;
    NativeCombatSession(const Config& config,ReplayDependencySource source)
        :config_(config),source_(source),storage_(history_,source_,config.base,config.identity,config.approved_profile),
         adapter_(storage_,config.original,reinterpret_cast<volatile uint32_t*>(config.base+0x470D0C4),config.identity.thread,
            3-config.identity.slot,config.identity.frame),clock_(session_),confirmations_(session_),driver_(session_,adapter_,clock_),
         sink_{&confirmations_,decltype(confirmations_)::send},routes_{&clock_.dispatch(),&driver_.route(),&sink_}{
        status.session=config.identity.session;status.context=this;
        status.drain=[](void* p,std::ostream& events,std::ostream& summary){static_cast<NativeCombatSession*>(p)->report(events,summary);};
    }
    NativeCombatSession(const NativeCombatSession&)=delete;
    NativeCombatSession& operator=(const NativeCombatSession&)=delete;
    bool prepare(observation::Shared& shared){
        if(!config_.general_restore || !NativeBoundarySnapshot::general_restore_implemented)return false;
        if(attempted_)return false;attempted_=true;
        const auto& c=config_;const auto& k=c.identity;
        if(!owned_transport_attachment.ready(k.session) || shared.reserved!=observation::owned_mode || !valid_config(c) || !c.base || !c.original || reinterpret_cast<uintptr_t>(c.original)==c.base+0x2DBC60 ||
            !k.same_round(k) || k.thread!=GetCurrentThreadId() || !c.image_verified || !c.bilateral_private ||
            !c.boundaries_owned || !c.verified_void_entry || !source_.available())return false;
        if(!session_.attach_journal(journal_) || !reader_.bind(c.base,c.caller,k,c.image_verified) ||
            !publication_.bind(shared,k.session,k.round,k.event,k.frame) ||
            !confirmations_.bind(k.session,k.peer,k.round,k.event,k.slot,k.frame) ||
            !clock_.attach_confirmations(&confirmations_,decltype(confirmations_)::service) ||
            !clock_.bind(c.base,c.clock_object,k.thread,c.image_verified,c.bilateral_private,c.boundaries_owned,
                k.session,k.round,c.clock_identity,c.clock_frame,k.frame))return false;
        if(!driver_.attach_publication(this,[](void* p,const Driver::Boundary& b){
                auto& self=*static_cast<NativeCombatSession*>(p);
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
    // Call at a quiescent owner-thread boundary BEFORE the first reserved clock
    // call, with both peers agreeing to this exact test. Caller retains *this.
    bool activate(CombatStartAgreement& agreement){
        if(!config_.general_restore || !NativeBoundarySnapshot::general_restore_implemented)return false;
        const auto& offer=config_.offer;const auto& key=config_.identity;
        if(!owned_transport_attachment.ready(key.session) || published_ || !ready() || GetCurrentThreadId()!=key.thread || offer.sender!=key.session || offer.recipient!=key.peer ||
            offer.round!=key.round || offer.event!=key.event || offer.first_frame!=key.frame || offer.slot!=key.slot ||
            offer.delay!=0 || offer.rollback!=3 || offer.records!=observation::capacity ||
            !native_clock_hook::installed || native_clock_hook::installed_base!=config_.base ||
            native_clock_hook::route.load() || exclusive_battle_route.load() || scheduler_input_sink.load())return false;
        if(const auto hold=native_clock_hook::start_hold.load(std::memory_order_acquire);
            hold && !hold->ready(config_.clock_object,GetTickCount64()))return false;
        if(!reader_.frontier(key.frame) ||
            !clock_.dispatch().at_boundary(config_.clock_identity,config_.clock_frame))return false;
        if(!agreement.activation_ready() || !agreement.consume(offer))return false;
        published_=publish_combat_routes(routes_);status.active.store(published_,std::memory_order_release);return published_;
    }
    auto& clock_dispatch(){return clock_.dispatch();}
    auto& battle_route(){return driver_.route();}
    const SchedulerInputSink& input_sink()const{return sink_;}
    // Exactly one logging worker may call this after session publication.
    void report(std::ostream& events,std::ostream& summary){report_.drain(journal_,events);report_.write(summary);}
    void stop(){clock_.stop();}
    // Only at the selected clock's owner-thread poll, before another update.
    // Keep routes installed: a partially restored session cannot resume stock.
    void terminate(CombatRuntimeStatus::Reason reason){
        if(GetCurrentThreadId()!=config_.identity.thread)return;
        stop();status.active=false;status.restart_required=true;
        unsigned empty=CombatRuntimeStatus::none;status.reason.compare_exchange_strong(empty,reason);
        status.adapter_failure=adapter_.failure;status.adapter_exception=adapter_.exception;
        status.updates=adapter_.calls;
        if(storage_.restore_diagnostic.values[NativeRestoreDiagnostic::invoked])status.restored=true;
        static_assert(NativeRestoreDiagnostic::count==22);
        for(unsigned i=0;i<NativeRestoreDiagnostic::count;++i)status.restore_details[i]=storage_.restore_diagnostic.values[i];
        if(!cleanup_attempted_){cleanup_attempted_=true;status.history_retired=release_history();}
    }
    // Only after hook execution has stopped, on the owner thread. The caller
    // also owns the original dependency source and releases it separately.
    bool release_history(){return history_.release();}
};
}
