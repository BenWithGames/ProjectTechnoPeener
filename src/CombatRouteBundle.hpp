#pragma once
#include "NativeClockDispatch.hpp"
#include "ExclusiveBattleRoute.hpp"
#include "SchedulerInputSink.hpp"
namespace sc6::research {
// One release-store exposes all three prepared routes. Never clear after
// native mutation: completion/failure retains ownership until game restart.
struct CombatRouteBundle {
    NativeClockDispatch<>* clock{};
    ExclusiveBattleRoute* battle{};
    const SchedulerInputSink* inputs{};
};
inline std::atomic<const CombatRouteBundle*> combat_routes{};
inline bool publish_combat_routes(const CombatRouteBundle& routes){
    if(!routes.clock || !routes.clock->active() || routes.clock->restart_required() ||
        !routes.battle || !routes.battle->ready() || !routes.inputs || !routes.inputs->context || !routes.inputs->send)return false;
    const CombatRouteBundle* empty=nullptr;
    return combat_routes.compare_exchange_strong(empty,&routes,std::memory_order_release,std::memory_order_relaxed);
}
}
