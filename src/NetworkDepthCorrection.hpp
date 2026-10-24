#pragma once
#include "RepeatedNetworkCorrection.hpp"
namespace sc6::research {
// Explicit private experiment. Four clean boundaries, three predicted updates,
// three reverse restores, then three independently checked corrected updates.
struct NetworkDepthCorrection {
    using P=NativeNetworkCheckpointProbe;using B=NativeBoundarySnapshot;using Key=P::Key;
    using Input=std::array<std::byte,40>;
    static constexpr unsigned depth=3;
    static_assert(27ull*sizeof(FullStateProbe)<1536ull*1024*1024,"Depth snapshot budget exceeds 1.5 GiB");
    std::unique_ptr<P> save_only;
    std::array<std::unique_ptr<B>,4> clean;
    std::array<std::unique_ptr<B>,8> spare;
    std::array<std::unique_ptr<B>,3> predicted;
    std::array<std::unique_ptr<NativeAdjacentRestore>,3> clean_hops,correction_hops;
    std::array<SingleUpdateCall,3> prediction_calls,replay_calls;
    std::array<Key,4> keys{};
    std::array<Input,3> confirmed{};
    std::array<bool,3> linked{};
    RecordedInput delivery,guard;
    unsigned captured{},stable{},retries{},latest_confirmed{};
    bool failed{},done{},replay{};
    explicit NetworkDepthCorrection(bool enabled):replay(enabled){
        if(!enabled){save_only=std::make_unique<P>(false);return;}
        for(auto& p:clean)p=std::make_unique<B>(B::Mode::idle);
        for(auto& p:spare)p=std::make_unique<B>(B::Mode::idle);
        for(auto& p:predicted)p=std::make_unique<B>(B::Mode::direction_observation);
        for(auto& p:clean_hops)p=std::make_unique<NativeAdjacentRestore>();
        for(auto& p:correction_hops)p=std::make_unique<NativeAdjacentRestore>();
        network_checkpoint_status.depth_target=depth;
    }
    void fail(unsigned code){failed=true;network_checkpoint_status.failure=code;network_checkpoint_status.phase=3;}
    static bool window_valid(const std::array<Key,4>& window,const Key& now){
        for(unsigned i=1;i<4;++i)
            if(!RepeatedNetworkCorrection::next_boundary(window[i-1],window[i]))return false;
        return RepeatedNetworkCorrection::same_session(window[3],now) && window[3].frame==now.frame;
    }
    static bool all_confirmed(const std::array<bool,3>& values){return values[0] && values[1] && values[2];}
    static bool changed(const B& a,const B& b){
        const auto& x=*a.data;const auto& y=*b.data;
        if(!TwoFrameIdleReplay::transforms_equal(x.transforms[0],y.transforms[0]) ||
           !TwoFrameIdleReplay::cameras_equal(x.cameras[0],y.cameras[0]) ||
           x.blends[0].bytes!=y.blends[0].bytes || x.blends[0].controller!=y.blends[0].controller || x.measurements[0].rng!=y.measurements[0].rng)return true;
        for(unsigned side=0;side<2;++side)
            if(std::memcmp(x.measurements[0].fields[side].data()+8,y.measurements[0].fields[side].data()+8,216))return true;
        return false;
    }
    bool wind_pair(const B& a,const B& b,NativeAdjacentRestore& hop){
        const auto& x=a.data->transforms[0];const auto& y=b.data->transforms[0];
        auto& d=hop.probe->wind_diagnostic;
        for(unsigned i=0;i<x.count;++i){const auto& r=x.records[i];
            if(r.side!=2)continue;
            d.record=i;d.kind=r.kind;d.size=r.size;
            if(r.kind==4){const auto vt=TransformSnapshot::pointer(r.bytes.data());d.vtable_rva=vt>=a.module?vt-a.module:0;
                if(vt!=a.module+0x3E88CB8 && vt!=a.module+0x3E88C88 && vt!=a.module+0x3E88D18 && vt!=a.module+0x3E88CE8){
                    P::publish_restore_diagnostics(hop);fail(14);return false;}}
            if(r.kind==24 && (i>=y.count || !TransformSnapshot::stage_wind_finite(r,y.records[i],&d))){
                P::publish_restore_diagnostics(hop);fail(22);return false;}
        }
        return true;
    }
    bool prepare(NativeAdjacentRestore& hop,const B& a,const B& b,bool direction){
        auto& s=network_checkpoint_status;
        s.readiness_attempted=1;
        const bool ok=hop.prepare_captured(a,b,direction);
        s.readiness_rejection=static_cast<unsigned>(hop.rejection);
        if(!ok){P::publish_restore_diagnostics(hop);fail(direction?19:7);return false;}
        return wind_pair(a,b,hop);
    }
    bool restore(NativeAdjacentRestore& hop,const B& source,const B& current,uintptr_t base){
        const bool ok=hop.restore_captured(source,base,guard.assign(current.data->delayed_inputs));
        auto& s=network_checkpoint_status;
        s.restore_invoked=s.restore_invoked.load() || hop.probe->restore_invoked;
        P::publish_restore_diagnostics(hop);
        if(!ok){fail(20);return false;}
        return true;
    }
    static void validated(void* context,const NetworkGameHandoff::Request& request,const NetworkGameHandoff::Boundary& saved){
        auto& self=*static_cast<NetworkDepthCorrection*>(context);
        if(self.failed || self.done)return;
        self.latest_confirmed=request.frame;
        for(unsigned i=0;i<self.captured && i<depth;++i){
            if(self.linked[i] || !P::matches(self.keys[i],request,saved,GetCurrentThreadId()))continue;
            const auto& snapshot=*self.clean[i];
            if(!snapshot.valid || snapshot.fault || std::memcmp(snapshot.data->delayed_inputs.data(),request.input.data(),40)){
                self.fail(6);return;}
            std::memcpy(self.confirmed[i].data(),request.input.data(),40);
            self.linked[i]=true;++network_checkpoint_status.matched;
        }
    }
    void process(uintptr_t base,uintptr_t argument,uintptr_t caller,unsigned thread,
        const NetworkGameHandoff::Boundary& boundary,SingleUpdateCall::Routine original,uint64_t session) noexcept {
        if(!replay){save_only->process(base,argument,caller,thread,boundary,original,session);return;}
        __try{process_body(base,argument,caller,thread,boundary,original,session);}
        __except(EXCEPTION_EXECUTE_HANDLER){network_checkpoint_status.restore_exception=GetExceptionCode();fail(13);}
    }
    __declspec(noinline) void process_body(uintptr_t base,uintptr_t argument,uintptr_t caller,unsigned thread,
        const NetworkGameHandoff::Boundary& boundary,SingleUpdateCall::Routine original,uint64_t session){
        auto& h=network_game_handoff;auto& s=network_checkpoint_status;
        if(failed || done){h.boundary(session,thread,boundary);return;}
        const Key now{h.local_nonce.load(),h.peer_nonce.load(),boundary.generation,h.slot.load(),thread,boundary.frame,boundary.round,boundary.event};
        bool capture=false;
        if(caller!=base+0x3FE816 || !original || reinterpret_cast<uintptr_t>(original)==base+0x2DBC60){fail(1);}
        else if(!now.local || session!=now.local || !now.peer || !now.generation || !now.round || !thread || now.slot<1 || now.slot>2){stable=0;if(captured)fail(3);}
        else if(!P::normal_battle(base) || !P::idle(base)){stable=0;if(captured)fail(23);}
        else if(captured && !RepeatedNetworkCorrection::next_boundary(keys[captured-1],now)){fail(3);}
        else if(captured || ++stable>=30)capture=true;
        // Every save shares this call site and frame, including predicted saves.
        for(unsigned phase=0;phase<=depth;++phase){
            B* target=phase?predicted[phase-1].get():(capture?clean[captured].get():nullptr);
            if(target){
                const bool ok=target->capture(base,argument);
                s.capture_phase=phase;s.capture_stage=target->capture_stage;s.capture_exception=target->exception;
                s.capture_mode_values_valid=target->mode_values_valid;
                for(unsigned i=0;i<6;++i)s.capture_mode_values[i]=target->mode_values[i];
                if(!ok){fail(phase?18:4);if(!phase)h.boundary(session,thread,boundary,&validated,this);return;}
                if(!phase){
                    if(target->data->before_keys[0]!=now.frame || std::memcmp(target->data->delayed_inputs.data(),boundary.input.data(),40)){
                        fail(5);h.boundary(session,thread,boundary,&validated,this);return;}
                    keys[captured]=now;s.depth_frames[captured]=now.frame;
                    s.captured=++captured;s.first_frame=keys[0].frame;s.second_frame=now.frame;
                }else{
                    if(target->data->before_keys[0]!=keys[phase].frame){fail(18);return;}
                    s.depth_predicted=phase;
                }
            }
            if(!phase){
                h.boundary(session,thread,boundary,&validated,this);
                if(failed || captured!=4)return;
                if(h.failed || !window_valid(keys,now)){fail(9);return;}
                if(!all_confirmed(linked)){
                    auto& timing=s.confirmation_attempts[retries];timing[0]=keys[0].frame;timing[1]=now.frame;
                    timing[2]=latest_confirmed;timing[3]=h.write.load()-h.read.load();++s.confirmation_misses;
                    if(retries==2){fail(15);return;}
                    for(unsigned i=0;i<4;++i)clean[i].swap(spare[4*retries+i]);
                    s.confirmation_retries=++retries;captured=stable=0;keys={};linked={};confirmed={};
                    s.captured=0;s.matched=0;return;
                }
                s.replay_attempted=1;s.prediction_fixture_slot=3-now.slot;
                for(auto& p:clean)p->direction_slot=3-now.slot;
                for(auto& p:predicted)p->direction_slot=3-now.slot;
                for(unsigned i=0;i<depth;++i){Input unused{};
                    if(confirmed[i]!=clean[i]->data->delayed_inputs || !P::make_movement_prediction(confirmed[i],unused,3-now.slot)){fail(16);return;}
                    s.depth_phase=1;s.depth_hop=i;
                    if(!prepare(*clean_hops[i],*clean[i],*clean[i+1],false))return;
                }
                s.readiness_prepared=1;
                for(unsigned i=depth;i-- >0;){s.depth_phase=2;s.depth_hop=i;
                    if(!restore(*clean_hops[i],*clean[i],*clean[i+1],base))return;
                    ++s.depth_clean_restored;
                }
            }
            if(phase==depth)break;
            s.depth_phase=3;s.depth_hop=phase;
            if(!P::normal_battle(base)){fail(23);return;}
            Input input{};
            if(!P::make_movement_prediction(confirmed[phase],input,3-now.slot)){fail(16);return;}
            const bool advanced=prediction_calls[phase].run(original,delivery.assign(input),
                reinterpret_cast<volatile uint32_t*>(base+0x470D0C4),keys[phase].frame,thread);
            s.prediction_invoked=s.prediction_invoked.load() || prediction_calls[phase].invoked;
            if(!advanced){fail(17);return;}
        }
        s.prediction_captured=1;s.prediction_state_changed=changed(*clean[depth],*predicted[depth-1]);
        for(unsigned i=0;i<depth;++i){s.depth_phase=4;s.depth_hop=i;
            if(!prepare(*correction_hops[i],i?*predicted[i-1]:*clean[0],*predicted[i],true))return;
        }
        for(unsigned i=depth;i-- >0;){s.depth_phase=5;s.depth_hop=i;
            if(!restore(*correction_hops[i],i?*predicted[i-1]:*clean[0],*predicted[i],base))return;
            ++s.depth_rewound;
        }
        s.restore_verified=1;s.prediction_rewound=1;
        for(unsigned i=0;i<depth;++i){s.depth_phase=6;s.depth_hop=i;
            if(!P::normal_battle(base)){fail(23);return;}
            auto& hop=*correction_hops[i];P::set_reference(hop,*clean[i+1]);
            const bool advanced=replay_calls[i].run(original,delivery.assign(confirmed[i]),
                reinterpret_cast<volatile uint32_t*>(base+0x470D0C4),keys[i].frame,thread);
            s.replay_invoked=s.replay_invoked.load() || replay_calls[i].invoked;
            if(!advanced){fail(11);return;}
            ++s.depth_delivered;hop.replay_delivered=true;
            const bool equal=hop.replay_step(*clean[i],base,guard.assign(clean[i+1]->data->delayed_inputs),true);
            P::publish_restore_diagnostics(hop);
            if(!equal){fail(12);return;}
            s.depth_verified[i]=1;++s.depth_compared;
        }
        if(!s.prediction_state_changed.load()){fail(21);return;}
        s.network_input_delivered=1;s.replay_verified=1;s.prediction_stage=6;s.depth_phase=7;s.phase=2;done=true;
    }
};
}
