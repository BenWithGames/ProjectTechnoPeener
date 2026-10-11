#pragma once
#include "NativeCombatSession.hpp"
namespace sc6::research {
struct NativeConfigurationAccess {
    static bool clock(uintptr_t base,uintptr_t object,NativeClockAccess<>::State& out){
        return NativeClockAccess<>::inspect(base,object,out);
    }
    static auto round(){return round_event::snapshot();}
    static bool counter(uintptr_t base,unsigned& out){return NativeBattleBoundaryAccess::counter(base,out);}
};
// Assemble on the observed owner thread at the selected clock entry. Discovery
// supplies an address, not admission or proof of battle-boundary ownership.
template<class Access=NativeConfigurationAccess> struct NativeCombatConfiguration {
    enum class Result {ready,stale,thread,clock,round,battle,offer,configuration,resources};
    static Result assemble(const NativeClockDiscovery::Observation& observed,uint64_t now,
        const NativeCombatSession::Config& admission,NativeCombatSession::Config& out){
        out={};
        if(!observed.valid || now<observed.tick || now-observed.tick>250)return Result::stale;
        if(observed.thread!=GetCurrentThreadId() || admission.identity.thread!=observed.thread)return Result::thread;
        if(observed.base!=admission.base)return Result::clock;
        NativeClockAccess<>::State clock{};
        if(!Access::clock(observed.base,observed.object,clock) || clock.identity!=observed.clock.identity ||
            clock.frame!=observed.clock.frame || clock.local_mask!=observed.clock.local_mask ||
            (clock.local_mask!=1 && clock.local_mask!=2))return Result::clock;
        const auto before=Access::round();unsigned frame{};
        if(!Access::counter(observed.base,frame))return Result::battle;
        const auto after=Access::round();
        if(!before.round || !before.generation || before.round!=after.round || before.counter!=after.counter ||
            before.generation!=after.generation || before.round!=admission.identity.round ||
            before.counter!=admission.identity.event || before.generation!=admission.identity.generation)return Result::round;
        if(frame!=admission.identity.frame)return Result::battle;
        const auto slot=clock.local_mask==1?1u:2u;
        if(admission.identity.slot!=slot || admission.offer.slot!=slot)return Result::offer;
        auto result=admission;
        result.clock_object=observed.object;result.clock_identity=clock.identity;result.clock_frame=clock.frame;
        if(!NativeCombatSession::valid_config(result))return Result::configuration;
        out=result;return Result::ready;
    }
};
}
