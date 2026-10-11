#pragma once
#include "CorrectedHistoryReplay.hpp"
#include "NetworkGameHandoff.hpp"
namespace sc6::research {
// Called only at the owner-thread boundary after draining the handoff. Copies
// the entire window before any restoration; partial confirmation never replays.
inline bool confirmed_replay_window(const NetworkGameHandoff& handoff,const RollbackBoundaryKey& target,
    const RollbackBoundaryKey& head,std::array<CorrectedHistoryReplay::Frame,3>& out){
    out={};
    if(!target.same_round(head) || target.thread!=GetCurrentThreadId() || target.frame>=head.frame ||
        head.frame-target.frame>3 || target.slot!=handoff.slot.load())return false;
    std::array<CorrectedHistoryReplay::Frame,3> staged{};
    for(unsigned i=0;i<head.frame-target.frame;++i){auto key=target;key.frame+=i;
        NetworkGameHandoff::Confirmation c;
        NetworkGameHandoff::Boundary boundary{key.generation,key.frame,key.round,key.event,{}};
        if(!handoff.confirmed(key.session,key.peer,key.thread,boundary,c))return false;
        staged[i]={key,c.boundary.input,c.input,true};
    }
    out=staged;return true;
}
}
