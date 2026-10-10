#pragma once
#include "NativeNetworkCheckpointProbe.hpp"
namespace sc6::research {
// Two fresh, preallocated attempts; no allocation or destruction in callbacks.
struct RepeatedNetworkCorrection {
    using Probe=NativeNetworkCheckpointProbe;using Key=Probe::Key;
    static constexpr unsigned target=2;
    static_assert(target*11ull*sizeof(FullStateProbe)<1024ull*1024*1024,"Repeated snapshot budget exceeds 1 GiB");
    std::array<std::unique_ptr<Probe>,target> probes;
    unsigned current{},completed{};bool replay{},stopped{},advance_pending{};
    explicit RepeatedNetworkCorrection(bool enabled):replay(enabled){
        probes[0]=std::make_unique<Probe>(enabled);
        if(enabled)probes[1]=std::make_unique<Probe>(true);
        network_checkpoint_status.repeated_target=enabled?target:0;
        network_checkpoint_status.repeated_cycle=enabled?1:0;
    }
    static bool same_session(const Key& previous,const Key& next){
        return previous.local && previous.peer && previous.generation && previous.thread && previous.round &&
            previous.slot>=1 && previous.slot<=2 && previous.local==next.local && previous.peer==next.peer &&
            previous.generation==next.generation && previous.thread==next.thread && previous.slot==next.slot &&
            previous.round==next.round && previous.event==next.event;
    }
    static bool next_boundary(const Key& previous,const Key& next){
        return same_session(previous,next) && previous.frame!=UINT32_MAX && next.frame==previous.frame+1;
    }
    void process(uintptr_t base,uintptr_t argument,uintptr_t caller,unsigned thread,
        const NetworkGameHandoff::Boundary& boundary,SingleUpdateCall::Routine original,uint64_t session) noexcept {
        auto& status=network_checkpoint_status;
        if(stopped){network_game_handoff.boundary(session,thread,boundary);return;}
        if(completed){
            auto& h=network_game_handoff;
            const Key now{h.local_nonce.load(),h.peer_nonce.load(),boundary.generation,h.slot.load(),thread,boundary.frame,boundary.round,boundary.event};
            if(!same_session(probes[0]->keys[1],now)){
                status.failure=24;status.repeated_failed=current+1;stopped=true;
                network_game_handoff.boundary(session,thread,boundary);return;
            }
        }
        if(advance_pending){
            auto& h=network_game_handoff;
            const Key now{h.local_nonce.load(),h.peer_nonce.load(),boundary.generation,h.slot.load(),thread,boundary.frame,boundary.round,boundary.event};
            if(!next_boundary(probes[current]->keys[1],now)){
                status.failure=24;status.repeated_failed=1;stopped=true;
                network_game_handoff.boundary(session,thread,boundary);return;
            }
            ++current;advance_pending=false;
            status.reset_attempt();status.enabled=1;status.replay_enabled=1;status.repeated_cycle=current+1;
        }
        probes[current]->process(base,argument,caller,thread,boundary,original,session);
        if(!replay)return;
        if(probes[current]->failed){status.repeated_failed=current+1;stopped=true;return;}
        if(status.replay_verified.load()){
            auto& result=status.repeated_results[current];
            result[0]=probes[current]->keys[0].frame;result[1]=probes[current]->keys[1].frame;
            result[2]=status.prediction_fixture_slot.load();result[3]=1;
            status.repeated_completed=++completed;
            if(completed==target)stopped=true;else advance_pending=true;
        }
    }
};
}
