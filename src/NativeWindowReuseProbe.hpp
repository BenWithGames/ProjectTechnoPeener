#pragma once
#include "NativeRollbackWindow.hpp"
#include "NativeNetworkCheckpointProbe.hpp"
#include "RngTransition.hpp"
namespace sc6::research {
// Explicit private reference replay; two windows reuse the same native memory.
// No predicted branch or continuous correction is enabled by this experiment.
struct NativeWindowReuseProbe {
    NativeRollbackWindow window;
    std::array<RollbackBoundaryKey,4> keys{};
    std::array<bool,3> confirmed{};
    unsigned captured{},stable{},completed{},retries{};bool failed{},done{};
    static bool retry_allowed(unsigned attempts,bool handoff_failed,const std::array<bool,3>& inputs){
        return attempts<2 && !handoff_failed && !(inputs[0] && inputs[1] && inputs[2]);
    }
    void fail(unsigned code){failed=true;network_checkpoint_status.failure=code;network_checkpoint_status.phase=3;}
    static void validated(void* context,const NetworkGameHandoff::Request& request,const NetworkGameHandoff::Boundary& saved){
        auto& self=*static_cast<NativeWindowReuseProbe*>(context);
        for(unsigned i=0;i<self.captured && i<3;++i){const auto& k=self.keys[i];
            if(!self.confirmed[i] && k.session==request.local_nonce && k.peer==request.peer_nonce && k.slot==request.slot &&
               k.generation==saved.generation && k.round==request.round && k.event==request.boundary && k.frame==request.frame &&
               k.thread==GetCurrentThreadId() && saved.input==request.input){self.confirmed[i]=true;++network_checkpoint_status.matched;}
        }
    }
    void process(uintptr_t base,uintptr_t argument,uintptr_t caller,unsigned thread,
        const NetworkGameHandoff::Boundary& boundary,SingleUpdateCall::Routine original,uint64_t session) noexcept {
        __try{body(base,argument,caller,thread,boundary,original,session);}
        __except(EXCEPTION_EXECUTE_HANDLER){network_checkpoint_status.restore_exception=GetExceptionCode();
            network_checkpoint_status.restore_invoked=network_checkpoint_status.restore_invoked.load() || window.any_restore_invoked();fail(13);}
    }
    void body(uintptr_t base,uintptr_t argument,uintptr_t caller,unsigned thread,
        const NetworkGameHandoff::Boundary& boundary,SingleUpdateCall::Routine original,uint64_t session){
        auto& h=network_game_handoff;auto& s=network_checkpoint_status;
        if(failed || done){h.boundary(session,thread,boundary);return;}
        if(caller!=base+0x3FE816 || !original || reinterpret_cast<uintptr_t>(original)==base+0x2DBC60){fail(1);h.boundary(session,thread,boundary);return;}
        RollbackBoundaryKey key{session,h.peer_nonce.load(),boundary.generation,boundary.round,boundary.event,boundary.frame,h.slot.load(),thread};
        const bool ready=session==h.local_nonce.load() && key.same_round(key) &&
            NativeNetworkCheckpointProbe::normal_battle(base) && NativeNetworkCheckpointProbe::idle(base);
        if(!ready){stable=0;if(captured)fail(23);h.boundary(session,thread,boundary);return;}
        if(!captured && ++stable<30){h.boundary(session,thread,boundary);return;}
        if(!window.capture(key,base,argument)){s.capture_stage=window.capture_stage;s.capture_exception=window.capture_exception;fail(4);h.boundary(session,thread,boundary);return;}
        s.window_rng[completed][captured]=window.captured_rng;
        s.window_rng_after_save[completed][captured]=window.captured_rng_after_save;
        if(captured)s.window_rng_steps[completed][captured-1]=RngTransition::distance(s.window_rng[completed][captured-1].load(),window.captured_rng);
        keys[captured]=key;s.depth_frames[captured]=key.frame;++captured;s.captured=captured;
        h.boundary(session,thread,boundary,&validated,this);
        if(captured!=4)return;
        s.replay_attempted=1;s.first_frame=keys[0].frame;s.second_frame=keys[3].frame;
        if(h.failed){fail(9);return;}
        if(!confirmed[0] || !confirmed[1] || !confirmed[2]){
            ++s.confirmation_misses;
            s.window_missing_mask=unsigned(!confirmed[0])|(unsigned(!confirmed[1])<<1)|(unsigned(!confirmed[2])<<2);
            s.window_retry_frame=key.frame;
            if(!retry_allowed(retries,h.failed,confirmed) || !window.discard_unmodified()){fail(15);return;}
            ++retries;++s.confirmation_retries;
            captured=stable=0;keys={};confirmed={};s.captured=0;
            return;
        }
        // Clear success from the prior cycle before a new mutation starts.
        s.replay_verified=0;s.restore_verified=0;
        const bool restored=window.rewind(key);
        s.window_plan_result=window.plan_result;s.window_prepare_stage=window.prepare_stage;s.window_hop=window.last_hop;
        s.window_source_frame=window.source_frame;s.window_next_frame=window.next_frame;s.window_workspace_flags=window.workspace_flags;
        s.readiness_attempted=1;s.readiness_prepared=window.diagnostic().prepared;
        s.readiness_rejection=static_cast<unsigned>(window.diagnostic().rejection);
        const auto& topology=window.topology;s.topology_mask=topology.mask;s.topology_index=topology.index;
        s.topology_before_count=topology.before_count;s.topology_after_count=topology.after_count;
        for(unsigned i=0;i<6;++i){s.topology_before[i]=topology.before[i];s.topology_after[i]=topology.after[i];}
        if(window.diagnostic().rejection==NativeBoundarySnapshot::Rejection::sources)
            NativeNetworkCheckpointProbe::publish_source_mismatch(window.diagnostic(),base);
        s.restore_invoked=s.restore_invoked.load() || window.any_restore_invoked();
        NativeNetworkCheckpointProbe::publish_restore_diagnostics(window.diagnostic());
        if(!restored){fail(20);return;}
        s.restore_verified=1;s.depth_rewound=3;
        s.replay_invoked=1;
        const bool replayed=window.replay_reference(original);
        s.window_hop=window.last_hop;s.window_source_frame=keys[window.last_hop].frame;s.window_next_frame=keys[window.last_hop+1].frame;
        if(!replayed){NativeNetworkCheckpointProbe::publish_restore_diagnostics(window.diagnostic());fail(12);return;}
        NativeNetworkCheckpointProbe::publish_restore_diagnostics(window.diagnostic());
        s.replay_verified=1;s.depth_compared=3;
        if(!window.recycle()){s.replay_verified=0;fail(25);return;}
        ++completed;s.window_reuse_completed=completed;
        captured=stable=retries=0;keys={};confirmed={};
        if(completed==2){done=true;s.phase=2;}
    }
};
}
