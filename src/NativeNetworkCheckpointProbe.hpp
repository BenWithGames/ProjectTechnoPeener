#pragma once
#include "NativeBoundarySnapshot.hpp"
#include "NativeAdjacentRestore.hpp"
#include "SingleUpdateCall.hpp"
#include "NetworkGameHandoff.hpp"
#include "NetworkCheckpointStatus.hpp"
namespace sc6::research {
// Explicit private-session checkpoint experiment; replay requires a separate opt-in mode.
// Buffers allocated when armed, before entering the game callback.
struct NativeNetworkCheckpointProbe {
    struct Key {uint64_t local{},peer{},generation{};unsigned slot{},thread{},frame{},round{},event{};};
    std::array<std::unique_ptr<NativeBoundarySnapshot>,2> snapshots;
    std::array<std::unique_ptr<NativeBoundarySnapshot>,4> spare_snapshots;
    unsigned confirmation_retries{},latest_confirmed_frame{};
    std::array<Key,2> keys{};std::array<bool,2> linked{};
    std::array<std::array<std::byte,40>,2> authenticated_inputs{};
    unsigned captured{},stable{};bool failed{},replay_enabled{},capture_requested{},prediction_capture_pending{};
    Key capture_key{};
    SingleUpdateCall invocation,prediction_invocation;RecordedInput delivery,prediction_delivery;
    std::unique_ptr<NativeBoundarySnapshot> predicted_snapshot;
    std::unique_ptr<NativeAdjacentRestore> correction;
    std::array<std::byte,40> predicted_input{};
    // Controlled screen-right prediction for the assigned remote slot.
    // Corrected input comes from the authenticated historical envelope.
    static bool make_movement_prediction(const std::array<std::byte,40>& actual,std::array<std::byte,40>& predicted,unsigned slot=1){
        if(slot<1 || slot>2 || !RecordedInput::compatible(actual,actual))return false;
        for(unsigned i=0;i<24;++i)if(actual[i]!=std::byte{})return false;
        predicted=actual;const unsigned right=0x400;
        for(unsigned offset:{8u*(slot-1),8u*(slot-1)+4u,16u+4u*(slot-1)})std::memcpy(predicted.data()+offset,&right,4);
        return predicted!=actual;
    }
    // Keep comparison independent of the deliberately predicted branch.
    static void set_reference(NativeAdjacentRestore& restore,const NativeBoundarySnapshot& reference){
        auto& q=*restore.probe;const auto& b=*reference.data;
        q.after_save=b.saved;q.after_keys=b.before_keys;
        std::memcpy(q.after_players,b.before_players,sizeof(q.after_players));
        q.original_next=b.measurements[0];q.rings[2]=b.rings[0];
        q.blends[1]=b.blends[0];q.transforms[1]=b.transforms[0];q.cameras[1]=b.cameras[0];
    }
    std::unique_ptr<NativeAdjacentRestore> readiness=std::make_unique<NativeAdjacentRestore>();
    // Captured-pair compatibility only: never authorizes loading stale live state.
    void prepare_linked() noexcept {
        if(replay_enabled || failed || captured!=2 || !linked[0] || !linked[1] || readiness->attempted)return;
        network_checkpoint_status.readiness_attempted=1;
        __try {
            const bool ok=readiness->prepare_captured(*snapshots[0],*snapshots[1]);
            network_checkpoint_status.readiness_rejection=static_cast<unsigned>(readiness->rejection);
            network_checkpoint_status.readiness_prepared=ok?1:0;
            if(!ok)fail(7);
            else preflight_wind();
        } __except(EXCEPTION_EXECUTE_HANDLER) {fail(8);}
    }
    explicit NativeNetworkCheckpointProbe(bool replay=false):replay_enabled(replay){for(auto& s:snapshots)s=std::make_unique<NativeBoundarySnapshot>(NativeBoundarySnapshot::Mode::idle);
        if(replay){
            for(auto& s:spare_snapshots)s=std::make_unique<NativeBoundarySnapshot>(NativeBoundarySnapshot::Mode::idle);
            predicted_snapshot=std::make_unique<NativeBoundarySnapshot>(NativeBoundarySnapshot::Mode::direction_observation);
            correction=std::make_unique<NativeAdjacentRestore>();
        }}
    void fail(unsigned reason){failed=true;network_checkpoint_status.failure=reason;network_checkpoint_status.phase=3;}
    static bool idle(uintptr_t base) noexcept {
        __try{return FullStateProbe::idle(base);}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool normal_battle(uintptr_t base) noexcept {
        network_checkpoint_status.battle_gate=0;
        network_checkpoint_status.battle_vtable=0;network_checkpoint_status.battle_handler=0;
        __try {
            const auto state=*reinterpret_cast<volatile uintptr_t*>(base+0x4843ED0);
            if(!state)return false;
            const auto vt=*reinterpret_cast<volatile uintptr_t*>(state);
            if(vt>=base && vt-base<0x5000000)network_checkpoint_status.battle_vtable=static_cast<unsigned>(vt-base);
            if(vt!=base+0x3E873D0){network_checkpoint_status.battle_gate=1;return false;}
            const auto handler=*reinterpret_cast<volatile uintptr_t*>(vt+0x10);
            if(handler>=base && handler-base<0x5000000)network_checkpoint_status.battle_handler=static_cast<unsigned>(handler-base);
            if(handler!=base+0x384EE0){network_checkpoint_status.battle_gate=2;return false;}
            network_checkpoint_status.battle_gate=3;return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    bool combat_gate(uintptr_t base) noexcept {
        if(!replay_enabled || normal_battle(base))return true;
        stable=0;capture_requested=false;
        if(captured)fail(23);
        return false;
    }
    void step(uintptr_t base,uintptr_t caller,unsigned thread,const NetworkGameHandoff::Boundary& boundary){
        if(failed || captured==2)return;
        auto& h=network_game_handoff;
        Key now{h.local_nonce.load(),h.peer_nonce.load(),boundary.generation,h.slot.load(),thread,boundary.frame,boundary.round,boundary.event};
        if(!now.local || !now.peer || !now.generation || !now.round || !thread || now.slot<1 || now.slot>2)return;
        if(caller!=base+0x3FE816){fail(1);return;}
        if(!combat_gate(base))return;
        if(!idle(base)){stable=0;if(captured)fail(2);return;}
        if(!captured && ++stable<30)return;
        if(captured){const auto& old=keys[0];
            if(now.local!=old.local || now.peer!=old.peer || now.slot!=old.slot || now.thread!=old.thread || now.generation!=old.generation || now.round!=old.round || now.event!=old.event || old.frame==UINT32_MAX || now.frame!=old.frame+1){fail(3);return;}
        }
        network_checkpoint_status.phase=1;
        capture_key=now;capture_requested=true;
    }
    void accept_reference(const NetworkGameHandoff::Boundary& boundary){
        auto& snapshot=*snapshots[captured];
        if(snapshot.data->before_keys[0]!=capture_key.frame || std::memcmp(snapshot.data->delayed_inputs.data(),boundary.input.data(),40)!=0){fail(5);return;}
        keys[captured]=capture_key;
        if(!captured)network_checkpoint_status.first_frame=capture_key.frame;else network_checkpoint_status.second_frame=capture_key.frame;
        network_checkpoint_status.captured=++captured;
        if(captured==2)network_checkpoint_status.phase=2;
    }
    // All three native saves use this exact call site and stack frame. The
    // serializer writes temporary stack values whose source addresses are checked.
    __declspec(noinline) void process(uintptr_t base,uintptr_t argument,uintptr_t caller,unsigned thread,
        const NetworkGameHandoff::Boundary& boundary,SingleUpdateCall::Routine original,uint64_t session) noexcept {
        if(failed){network_game_handoff.boundary(session,thread,boundary,&validated,this);return;}
        capture_requested=false;
        step(base,caller,thread,boundary);
        for(unsigned phase=0;phase<2;++phase){
            NativeBoundarySnapshot* target=phase?predicted_snapshot.get():(capture_requested?snapshots[captured].get():nullptr);
            // Snapshot the simulated movement state while retaining the actual
            // neutral boundary envelope used for restoration guards.
            const auto capture_argument=argument;
            if(target){
                const bool ok=target->capture(base,capture_argument);
                network_checkpoint_status.capture_phase=phase;
                network_checkpoint_status.capture_stage=target->capture_stage;
                network_checkpoint_status.capture_exception=target->exception;
                network_checkpoint_status.capture_mode_values_valid=target->mode_values_valid;
                for(unsigned i=0;i<6;++i)network_checkpoint_status.capture_mode_values[i]=target->mode_values[i];
                if(!ok){prediction_capture_pending=false;fail(phase?18:4);return;}
                if(!phase)accept_reference(boundary);
            }
            if(!phase){
                network_game_handoff.boundary(session,thread,boundary,&validated,this);
                replay_current(base,argument,caller,thread,boundary,original);
                if(!prediction_capture_pending)return;
            }else{
                prediction_capture_pending=false;
                finish_prediction(base,argument,thread,original);
            }
        }
    }
    // Called after handoff draining, before the normal game update. Never wait
    // for a packet here: a stale pair is rejected rather than loaded later.
    static bool live_pair(const Key& a,const Key& b,const Key& now,bool authenticated){
        return authenticated && a.local && a.peer && a.generation && a.thread && a.round &&
            a.slot>=1 && a.slot<=2 && a.frame!=UINT32_MAX && b.frame==a.frame+1 &&
            a.local==b.local && a.peer==b.peer && a.slot==b.slot && a.thread==b.thread &&
            a.generation==b.generation && a.round==b.round && a.event==b.event &&
            b.local==now.local && b.peer==now.peer && b.slot==now.slot && b.thread==now.thread &&
            b.generation==now.generation && b.round==now.round && b.event==now.event && b.frame==now.frame;
    }
    // Only an otherwise-current, unconfirmed pair may be discarded. Buffers
    // were allocated at arming; no allocation or destruction in the callback.
    bool retry_unconfirmed(const Key& now) noexcept {
        if(!replay_enabled || failed || captured!=2 || linked[0] ||
           !live_pair(keys[0],keys[1],now,true) || readiness->attempted ||
           readiness->probe->restore_invoked || invocation.invoked)return false;
        auto& timing=network_checkpoint_status.confirmation_attempts[confirmation_retries];
        timing[0]=keys[0].frame;timing[1]=now.frame;timing[2]=latest_confirmed_frame;
        timing[3]=network_game_handoff.write.load()-network_game_handoff.read.load();
        ++network_checkpoint_status.confirmation_misses;
        if(confirmation_retries==2){fail(15);return true;}
        for(unsigned i=0;i<2;++i)snapshots[i].swap(spare_snapshots[2*confirmation_retries+i]);
        ++confirmation_retries;network_checkpoint_status.confirmation_retries=confirmation_retries;
        captured=stable=0;keys={};linked={};authenticated_inputs={};
        network_checkpoint_status.captured=0;network_checkpoint_status.matched=0;
        network_checkpoint_status.first_frame=0;network_checkpoint_status.second_frame=0;
        network_checkpoint_status.phase=0;
        return true;
    }
    static void sample_input_routing(uintptr_t base,unsigned phase) noexcept {
        if(phase>1)return;
        unsigned values[18]{};
        network_checkpoint_status.routing_valid[phase]=0;network_checkpoint_status.dispatch_stage[phase]=0;
        for(auto& value:network_checkpoint_status.routing_values[phase])value=0;
        __try {
            for(unsigned i=0;i<4;++i)values[i]=*reinterpret_cast<volatile uint32_t*>(base+0x4855700+i*4);
            for(unsigned side=0;side<2;++side){
                const auto player=*reinterpret_cast<volatile uintptr_t*>(base+0x470DE90+side*8);
                if(!player)return;
                const unsigned selector=*reinterpret_cast<volatile uint8_t*>(player+0x23C);
                const unsigned at=4+side*5;
                values[at]=selector;values[at+1]=*reinterpret_cast<volatile uint32_t*>(player+0x324);
                if(selector>1)return;
                values[at+2]=*reinterpret_cast<volatile uint32_t*>(base+0x4715400+selector*0x60+8);
                values[at+3]=*reinterpret_cast<volatile uint32_t*>(base+0x470F390+selector*0x3038);
                values[at+4]=*reinterpret_cast<volatile uint16_t*>(base+(side?0x47AEC30:0x4717840));
            }
            for(unsigned i=0;i<14;++i)network_checkpoint_status.routing_values[phase][i]=values[i];
            network_checkpoint_status.routing_valid[phase]=1;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return;}
        __try {
            network_checkpoint_status.dispatch_stage[phase]=1;
            const auto state=*reinterpret_cast<volatile uintptr_t*>(base+0x4843ED0);
            if(!state)return;
            network_checkpoint_status.dispatch_stage[phase]=2;
            const auto vt=*reinterpret_cast<volatile uintptr_t*>(state);
            if(vt<base || vt-base>0x5000000)return;
            network_checkpoint_status.dispatch_stage[phase]=3;
            const auto handler=*reinterpret_cast<volatile uintptr_t*>(vt+0x10);
            if(handler<base || handler-base>0x5000000)return;
            network_checkpoint_status.dispatch_stage[phase]=4;
            values[14]=static_cast<unsigned>(vt-base);values[15]=static_cast<unsigned>(handler-base);
            values[16]=*reinterpret_cast<volatile uint32_t*>(state+8);
            values[17]=*reinterpret_cast<volatile uint32_t*>(state+0x10);
            for(unsigned i=14;i<18;++i)network_checkpoint_status.routing_values[phase][i]=values[i];
            network_checkpoint_status.dispatch_stage[phase]=5;
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    bool preflight_wind() noexcept {
        auto& diagnostic=readiness->probe->wind_diagnostic;
        const auto& capturedTransforms=snapshots[0]->data->transforms[0];
        // Captured type inspection only; never compare old bytes to live memory.
        for(unsigned i=0;i<capturedTransforms.count;++i){const auto& record=capturedTransforms.records[i];
            if(record.side!=2 || record.kind!=4)continue;
            const auto vt=TransformSnapshot::pointer(record.bytes.data());
            const auto base=snapshots[0]->module;
            if(vt==base+0x3E88CB8 || vt==base+0x3E88C88 || vt==base+0x3E88D18 || vt==base+0x3E88CE8)continue;
            diagnostic.record=i;diagnostic.kind=record.kind;diagnostic.size=record.size;
            diagnostic.vtable_rva=vt>=base?vt-base:0;
            publish_restore_diagnostics();fail(14);return false;
        }
        const auto& next=snapshots[1]->data->transforms[0];
        // Reuse the exact finite-value guard on captured bytes before native load.
        for(unsigned i=0;i<capturedTransforms.count;++i){const auto& a=capturedTransforms.records[i];
            if(a.side!=2 || a.kind!=24)continue;
            diagnostic.record=i;diagnostic.kind=a.kind;diagnostic.size=a.size;diagnostic.vtable_rva=0x3E88CE8;
            if(i>=next.count || !TransformSnapshot::stage_wind_finite(a,next.records[i],&diagnostic)){
                publish_restore_diagnostics();fail(22);return false;
            }
        }
        return true;
    }
    void publish_source_mismatch() noexcept {
        const auto& q=*readiness->probe;
        ULONG_PTR low{},high{};GetCurrentThreadStackLimits(&low,&high);
        for(unsigned i=0;i<q.saved.writes;++i){
            const auto a=q.saved.traces[i].source,b=q.after_save.traces[i].source;
            if(a==b)continue;
            bool ring=false;
            for(unsigned j=0;j<4;++j)if(q.saved.lengths[i]==(j%2?0x800u:0x1840u) &&
                a==q.rings[0].headers[j].current && b==q.rings[2].headers[j].current)ring=true;
            if(ring)continue;
            network_checkpoint_status.source_record=i;
            network_checkpoint_status.source_size=q.saved.lengths[i];
            network_checkpoint_status.source_saved=a;network_checkpoint_status.source_next=b;
            const auto writer=q.saved.traces[i].writer,base=snapshots[0]->module;
            network_checkpoint_status.source_writer_rva=writer>=base?writer-base:0;
            network_checkpoint_status.source_stack_mask=(a>=low && a<high?1u:0u)|(b>=low && b<high?2u:0u);
            network_checkpoint_status.source_mismatch_found=1;return;
        }
    }
    void publish_restore_diagnostics() noexcept {publish_restore_diagnostics(*readiness);}
    static void publish_restore_diagnostics(NativeAdjacentRestore& restore) noexcept {
        network_checkpoint_status.comparison_stage=restore.replay_stage;
        const auto& rp=*restore.probe;
        network_checkpoint_status.comparison_flags=unsigned(restore.replay_measurement_read) |
            (unsigned(restore.replay_rng_equal)<<1)|(unsigned(restore.replay_players_equal)<<2)|
            (unsigned(rp.replay_done)<<3)|(unsigned(rp.replay_keys_equal)<<4)|(unsigned(rp.replay_ring_equal)<<5)|(unsigned(rp.fault)<<6);
        network_checkpoint_status.comparison_bytes=rp.replay_byte_differences;
        for(unsigned side=0;side<2;++side){auto& a=network_checkpoint_status.comparison_scalars;
            a[side*4]=restore.replay_scalar_differences[side];a[side*4+1]=restore.replay_first_offset[side];
            a[side*4+2]=restore.replay_expected_byte[side];a[side*4+3]=restore.replay_live_byte[side];}
        const auto& d=restore.completion;
        network_checkpoint_status.completion_line=d.failure_line;network_checkpoint_status.completion_section=d.section;
        network_checkpoint_status.completion_record=d.record;network_checkpoint_status.completion_kind=d.kind;
        network_checkpoint_status.completion_offset=d.difference_offset;network_checkpoint_status.completion_saved=d.saved_word;
        network_checkpoint_status.completion_live=d.live_word;
        const auto& p=*restore.probe;
        network_checkpoint_status.camera_line=p.camera_diagnostic.failure_line;
        network_checkpoint_status.camera_record=p.camera_diagnostic.record;
        network_checkpoint_status.camera_section=p.camera_diagnostic.section;
        network_checkpoint_status.camera_offset=p.camera_diagnostic.offset;
        network_checkpoint_status.camera_a=p.camera_diagnostic.a;
        network_checkpoint_status.camera_b=p.camera_diagnostic.b;
        network_checkpoint_status.camera_c=p.camera_diagnostic.c;
        network_checkpoint_status.camera_live=p.camera_diagnostic.live;
        network_checkpoint_status.wind_verified=p.wind_restore_verified;
        network_checkpoint_status.wind_capture_valid=p.blends[7].valid;
        network_checkpoint_status.wind_failure_line=p.wind_diagnostic.failure_line;
        network_checkpoint_status.wind_offset=p.wind_diagnostic.offset;
        network_checkpoint_status.wind_saved_word=p.wind_diagnostic.saved_word;
        network_checkpoint_status.wind_next_word=p.wind_diagnostic.next_word;
        network_checkpoint_status.wind_control_saved=p.wind_diagnostic.control_saved;
        network_checkpoint_status.wind_control_next=p.wind_diagnostic.control_next;
        network_checkpoint_status.wind_record=p.wind_diagnostic.record;
        network_checkpoint_status.wind_kind=p.wind_diagnostic.kind;
        network_checkpoint_status.wind_size=p.wind_diagnostic.size;
        network_checkpoint_status.wind_vtable_rva=p.wind_diagnostic.vtable_rva;
        network_checkpoint_status.restore_stage=restore.diagnostic_stage;
        network_checkpoint_status.restore_failure_line=p.delayed_failure_line;
        network_checkpoint_status.restore_exception=p.exception;
        // Bits describe completed checks, not permissions to continue.
        network_checkpoint_status.restore_flags=
            (unsigned(p.fault)<<0)|(unsigned(p.restore_completed)<<1)|(unsigned(p.postflight_ok)<<2)|
            (unsigned(p.selected_equal)<<3)|(unsigned(p.preserve_verified)<<4)|(unsigned(p.delayed_loaded)<<5)|
            (unsigned(p.camera_restore_verified)<<6)|(unsigned(p.restored_valid)<<7)|
            (unsigned(p.restored_save_unchanged)<<8)|(unsigned(p.outer_correction_verified)<<9)|
            (unsigned(restore.native_payload_verified)<<10)|(unsigned(p.restored_ring_equal)<<11);
    }
    void replay_current(uintptr_t base,uintptr_t argument,uintptr_t caller,unsigned thread,
                        const NetworkGameHandoff::Boundary& boundary,SingleUpdateCall::Routine original) noexcept {
        if(!replay_enabled || failed || captured!=2 || network_checkpoint_status.replay_attempted.load())return;
        auto& h=network_game_handoff;
        const Key now{h.local_nonce.load(),h.peer_nonce.load(),boundary.generation,h.slot.load(),thread,boundary.frame,boundary.round,boundary.event};
        if(h.failed || !live_pair(keys[0],keys[1],now,true) || caller!=base+0x3FE816 ||
           !original || reinterpret_cast<uintptr_t>(original)==base+0x2DBC60 || !idle(base) ){network_checkpoint_status.replay_attempted=1;fail(9);return;}
        if(!combat_gate(base))return;
        if(retry_unconfirmed(now))return;
        network_checkpoint_status.replay_attempted=1;
        if(!linked[0] || authenticated_inputs[0]!=snapshots[0]->data->delayed_inputs){fail(9);return;}
        const unsigned remote_slot=3-now.slot;
        network_checkpoint_status.prediction_fixture_slot=remote_slot;
        snapshots[0]->direction_slot=snapshots[1]->direction_slot=predicted_snapshot->direction_slot=remote_slot;
        if(!make_movement_prediction(authenticated_inputs[0],predicted_input,remote_slot)){fail(16);return;}
        __try {
            network_checkpoint_status.readiness_attempted=1;
            const bool prepared=readiness->prepare_captured(*snapshots[0],*snapshots[1]);
            network_checkpoint_status.readiness_rejection=static_cast<unsigned>(readiness->rejection);
            network_checkpoint_status.readiness_prepared=prepared?1:0;
            if(!prepared){fail(7);return;}
            if(!preflight_wind())return;
            if(!combat_gate(base))return;
            const bool restored=readiness->restore_captured(*snapshots[0],base,argument);
            publish_restore_diagnostics();
            network_checkpoint_status.restore_invoked=network_checkpoint_status.restore_invoked.load() || readiness->probe->restore_invoked;
            if(!restored){fail(10);return;}
            network_checkpoint_status.restore_verified=1;
            if(!combat_gate(base))return;
            network_checkpoint_status.prediction_stage=1;
            sample_input_routing(base,0);
            const bool predicted_advanced=prediction_invocation.run(original,prediction_delivery.assign(predicted_input),
                reinterpret_cast<volatile uint32_t*>(base+0x470D0C4),keys[0].frame,thread);
            sample_input_routing(base,1);
            network_checkpoint_status.prediction_invoked=prediction_invocation.invoked?1:0;
            if(!predicted_advanced){fail(17);return;}
            network_checkpoint_status.prediction_stage=2;
            prediction_capture_pending=true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            publish_restore_diagnostics();
            network_checkpoint_status.restore_exception=GetExceptionCode();
            network_checkpoint_status.restore_invoked=network_checkpoint_status.restore_invoked.load() || readiness->probe->restore_invoked;
            fail(13);
        }
    }
    void finish_prediction(uintptr_t base,uintptr_t argument,unsigned thread,SingleUpdateCall::Routine original) noexcept {
        if(failed || !prediction_invocation.completed || !predicted_snapshot->valid || predicted_snapshot->fault){fail(18);return;}
        __try {
            network_checkpoint_status.prediction_stage=2;
            if(predicted_snapshot->data->before_keys[0]!=keys[1].frame){fail(18);return;}
            network_checkpoint_status.prediction_captured=1;
            const auto& expected=*snapshots[1]->data;const auto& observed=*predicted_snapshot->data;
            network_checkpoint_status.prediction_state_changed=
                !TwoFrameIdleReplay::transforms_equal(expected.transforms[0],observed.transforms[0]) ||
                !TwoFrameIdleReplay::cameras_equal(expected.cameras[0],observed.cameras[0]) ||
                expected.blends[0].bytes!=observed.blends[0].bytes ||
                expected.measurements[0].rng!=observed.measurements[0].rng;
            for(unsigned side=0;side<2;++side)if(std::memcmp(expected.measurements[0].fields[side].data()+8,
                observed.measurements[0].fields[side].data()+8,216))network_checkpoint_status.prediction_state_changed=1;
            network_checkpoint_status.prediction_stage=3;
            readiness.swap(correction);
            if(!readiness->prepare_captured(*snapshots[0],*predicted_snapshot,true)){
                network_checkpoint_status.readiness_rejection=static_cast<unsigned>(readiness->rejection);
                if(readiness->rejection==NativeBoundarySnapshot::Rejection::sources)publish_source_mismatch();
                fail(19);return;
            }
            network_checkpoint_status.prediction_stage=4;
            if(!readiness->restore_captured(*snapshots[0],base,argument)){
                publish_restore_diagnostics();fail(20);return;
            }
            publish_restore_diagnostics();
            network_checkpoint_status.prediction_rewound=1;
            set_reference(*readiness,*snapshots[1]);
            network_checkpoint_status.prediction_stage=5;
            const bool advanced=invocation.run(original,delivery.assign(authenticated_inputs[0]),
                reinterpret_cast<volatile uint32_t*>(base+0x470D0C4),keys[0].frame,thread);
            network_checkpoint_status.replay_invoked=invocation.invoked?1:0;
            network_checkpoint_status.network_input_delivered=invocation.invoked?1:0;
            if(!advanced){fail(11);return;}
            readiness->replay_delivered=true;
            if(!readiness->replay_step(*snapshots[0],base,argument,true)){publish_restore_diagnostics();fail(12);return;}
            publish_restore_diagnostics();
            // Even after a successful recovery, no divergence is not evidence
            // of gameplay prediction correction.
            if(!network_checkpoint_status.prediction_state_changed.load()){fail(21);return;}
            network_checkpoint_status.replay_verified=1;
            network_checkpoint_status.prediction_stage=6;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            publish_restore_diagnostics();network_checkpoint_status.restore_exception=GetExceptionCode();fail(13);
        }
    }
    static bool matches(const Key& key,const NetworkGameHandoff::Request& request,const NetworkGameHandoff::Boundary& saved,unsigned thread){
        return key.local==request.local_nonce && key.peer==request.peer_nonce && key.slot==request.slot && key.thread==thread &&
            key.generation==saved.generation && key.frame==request.frame && key.round==request.round && key.event==request.boundary;
    }
    static void validated(void* context,const NetworkGameHandoff::Request& request,const NetworkGameHandoff::Boundary& saved){
        auto& self=*static_cast<NativeNetworkCheckpointProbe*>(context);if(self.failed)return;
        self.latest_confirmed_frame=request.frame;
        for(unsigned i=0;i<self.captured;++i){
            if(self.linked[i] || !matches(self.keys[i],request,saved,GetCurrentThreadId()))continue;
            const auto& snapshot=*self.snapshots[i];
            if(!snapshot.valid || snapshot.fault || std::memcmp(snapshot.data->delayed_inputs.data(),request.input.data(),40)!=0){self.fail(6);return;}
            std::memcpy(self.authenticated_inputs[i].data(),request.input.data(),40);
            self.linked[i]=true;++network_checkpoint_status.matched;
        }
        self.prepare_linked();
    }
};
}
