#pragma once
#include "NativeClockDispatch.hpp"
#include "ExclusiveBattleRoute.hpp"
#include "SchedulerInputSink.hpp"
namespace sc6::research {
// One release-store exposes all three prepared routes. Failed ownership is
// never cleared. A verified, committed round can retire without destroying its
// immutable input archive; only that exact retired generation can be replaced.
class NativeCombatSession;
struct CombatRouteBundle {
    NativeClockDispatch<>* clock{};
    ExclusiveBattleRoute* battle{};
    const SchedulerInputSink* inputs{};
    void* recovery_context{};
    bool (*recover)(void*,uintptr_t){};
private:
    std::atomic<bool> retired_{};
    friend class NativeCombatSession;
public:
    CombatRouteBundle()=default;
    CombatRouteBundle(NativeClockDispatch<>* c,ExclusiveBattleRoute* b,const SchedulerInputSink* i):clock(c),battle(b),inputs(i){}
    bool retired()const{return retired_.load(std::memory_order_acquire);}
};
inline std::atomic<const CombatRouteBundle*> combat_routes{};
inline bool publish_combat_routes(const CombatRouteBundle& routes,const CombatRouteBundle* previous=nullptr){
    if(!routes.clock || !routes.clock->active() || routes.clock->restart_required() ||
        !routes.battle || !routes.battle->ready() || !routes.inputs || !routes.inputs->context || !routes.inputs->send ||
        routes.retired() || (previous && (previous==&routes || !previous->retired())))return false;
    return combat_routes.compare_exchange_strong(previous,&routes,std::memory_order_release,std::memory_order_relaxed);
}
}
