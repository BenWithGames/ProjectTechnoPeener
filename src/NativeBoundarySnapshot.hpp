#pragma once
#include "IdleTrajectoryAudit.hpp"
namespace sc6::research {
// A single boundary, independent of a pre-recorded reference trajectory.
// Capture on the owning simulation thread. Capture itself never invokes native loading.
struct NativeBoundarySnapshot {
    // General-state loading is implemented, but individual windows must pass
    // structural/ownership/preflight checks; this is not universal coverage.
    static constexpr bool general_restore_implemented=true;
    enum class Mode {idle,throw_move,charge,attack,edge,reversal,direction_observation,a_observation,combat_capture_only,general_combat};
    // This is fixture routing, not a generic move detector. The caller must
    // identify an approved character/profile before enabling non-idle routes.
    static Mode supported_mode(unsigned p1_input,unsigned p2_input,unsigned p1_move,unsigned p2_move,
        float scale,int selector,unsigned gate,bool charge_aux,bool approved_profile){
        if(!p1_input && !p2_input && p1_move==65535 && p2_move==65535 && scale==1.0f && !gate)return Mode::idle;
        if(!approved_profile)return Mode::combat_capture_only;
        if(FullStateProbe::throw_values(p1_move,p2_move,scale,selector))return Mode::throw_move;
        if(FullStateProbe::charge_values(p1_move,p2_move,scale,selector) && charge_aux)return Mode::charge;
        if(FullStateProbe::attack_values(p1_move,p2_move,scale,selector))return Mode::attack;
        if(FullStateProbe::edge_values(p1_move,p2_move,scale,selector))return Mode::edge;
        if(FullStateProbe::reversal_values(p1_move,p2_move,scale,selector))return Mode::reversal;
        return Mode::combat_capture_only;
    }
    void select_mode(Mode value){
        mode=value;data->throw_requested=value==Mode::throw_move;data->charge_requested=value==Mode::charge;
        data->attack_requested=value==Mode::attack;data->edge_requested=value==Mode::edge;data->reversal_requested=value==Mode::reversal;
    }
    bool capture_supported(uintptr_t base,uintptr_t argument,bool approved_profile=false) noexcept {
        if(attempted)return false;
        __try {
            select_mode(supported_mode(*reinterpret_cast<uint16_t*>(base+0x4717840),*reinterpret_cast<uint16_t*>(base+0x47AEC30),
                *reinterpret_cast<uint16_t*>(base+0x475A04A),*reinterpret_cast<uint16_t*>(base+0x47F143A),
                *reinterpret_cast<float*>(base+0x48462F4),*reinterpret_cast<int*>(base+0x4846458),
                *reinterpret_cast<unsigned char*>(base+0x48462D0),approved_profile && FullStateProbe::charge_aux_active(base),approved_profile));
        }__except(EXCEPTION_EXECUTE_HANDLER){fault=true;exception=GetExceptionCode();return false;}
        return capture(base,argument);
    }
    bool capture_general(uintptr_t base,uintptr_t argument,FullStateProbe::Routine writer=nullptr,FullStateProbe::Preflight check=nullptr) noexcept {
        if(attempted)return false;
        select_mode(Mode::general_combat);return capture(base,argument,writer,check);
    }
    std::unique_ptr<FullStateProbe> data=std::make_unique<FullStateProbe>();
    unsigned direction_bits{0x400},direction_slot{1};
    unsigned capture_stage{};bool mode_values_valid{};std::array<unsigned,6> mode_values{};
    Mode mode;uintptr_t module{};DWORD thread{};bool attempted{},valid{},fault{};unsigned exception{};
    explicit NativeBoundarySnapshot(Mode value):mode(value){
        data->throw_requested=value==Mode::throw_move;data->charge_requested=value==Mode::charge;
        data->attack_requested=value==Mode::attack;data->edge_requested=value==Mode::edge;
        data->reversal_requested=value==Mode::reversal;
    }
    // Internal storage reuse: allocation is retained by the owning workspace.
    NativeBoundarySnapshot(Mode value,std::unique_ptr<FullStateProbe> storage) noexcept
        :data(std::move(storage)),mode(value){
        data->throw_requested=value==Mode::throw_move;data->charge_requested=value==Mode::charge;
        data->attack_requested=value==Mode::attack;data->edge_requested=value==Mode::edge;
        data->reversal_requested=value==Mode::reversal;
    }
    static bool direction_state(uintptr_t base,unsigned bits=0x400,unsigned slot=1){
        return FullStateProbe::direction_state(base,bits,slot);
    }
    static bool a_state(uintptr_t base){
        return *reinterpret_cast<std::uint16_t*>(base+0x4717840)==1 &&
            *reinterpret_cast<std::uint16_t*>(base+0x47AEC30)==0 &&
            *reinterpret_cast<std::uint16_t*>(base+0x475A04A)==264 &&
            *reinterpret_cast<std::uint16_t*>(base+0x47F143A)==65535 &&
            *reinterpret_cast<float*>(base+0x48462F4)==1.0f &&
            *reinterpret_cast<unsigned char*>(base+0x48462D0)==0;
    }
    __declspec(noinline) bool capture(uintptr_t base,uintptr_t argument,FullStateProbe::Routine writer=nullptr,FullStateProbe::Preflight check=nullptr) noexcept {
        if(attempted)return false;attempted=true;module=base;thread=GetCurrentThreadId();
        if(!writer)writer=reinterpret_cast<FullStateProbe::Routine>(base+0x3841E0);
        if(!check)check=FullStateProbe::preflight;
        std::uint64_t bookkeeping{};bool saved_bookkeeping=false;
        __try {
            auto& q=*data;
            capture_stage=1;if(!check(base)){fault=true;__leave;}
            capture_stage=2;
            // General combat capture retains all serializer, identity and
            // before/after integrity checks. It does not claim that the old
            // move-specific loader can restore the resulting snapshot.
            const bool state_ready=mode==Mode::combat_capture_only || mode==Mode::general_combat ||
                (mode==Mode::a_observation?a_state(base):mode==Mode::direction_observation?direction_state(base,direction_bits,direction_slot):mode==Mode::idle?FullStateProbe::idle(base):q.combat_state(base));
            if(!state_ready || mode==Mode::combat_capture_only || mode==Mode::general_combat){
                mode_values[0]=*reinterpret_cast<std::uint16_t*>(base+0x4717840);
                mode_values[1]=*reinterpret_cast<std::uint16_t*>(base+0x47AEC30);
                mode_values[2]=*reinterpret_cast<std::uint16_t*>(base+0x475A04A);
                mode_values[3]=*reinterpret_cast<std::uint16_t*>(base+0x47F143A);
                std::memcpy(&mode_values[4],reinterpret_cast<void*>(base+0x48462F4),4);
                mode_values[5]=*reinterpret_cast<unsigned char*>(base+0x48462D0);
                mode_values_valid=true;
                if(!state_ready){fault=true;__leave;}
            }
            capture_stage=3;if(!FullStateProbe::input_bytes(argument,q.delayed_inputs)){fault=true;__leave;}
            capture_stage=4;if(!q.rings[0].capture(base)){fault=true;__leave;}
            capture_stage=5;q.measure(base,0);FullStateProbe::keys(base,q.before_keys.data(),q.before_players);
            bookkeeping=FullStateProbe::ptr(base+0x470E870);saved_bookkeeping=true;
            capture_stage=6;writer(&q.saved.stream);q.measure(base,1);
            capture_stage=7;if(!q.saved.seal()){fault=true;__leave;}
            capture_stage=8;if(!IdleTrajectoryAudit::same_measurement(q.measurements[0],q.measurements[1])){fault=true;__leave;}
            capture_stage=9;if(!q.rings[1].capture(base)){fault=true;__leave;}
            capture_stage=10;if(!q.rings[0].same(q.rings[1])){fault=true;__leave;}
            capture_stage=11;if(!q.capture_blend(base,0)){fault=true;__leave;}
            capture_stage=12;if(!q.cameras[0].capture(q.saved,base)){fault=true;__leave;}
            capture_stage=13;
            std::array<std::uint32_t,std::size(FullStateProbe::key_rvas)> keys{};uintptr_t players[2]{};
            FullStateProbe::keys(base,keys.data(),players);
            if(keys!=q.before_keys || std::memcmp(players,q.before_players,sizeof(players))){fault=true;__leave;}
            valid=true;
        }__except(EXCEPTION_EXECUTE_HANDLER){exception=GetExceptionCode();fault=true;}
        if(saved_bookkeeping){__try{if(!fault)capture_stage=14;*reinterpret_cast<std::uint64_t*>(base+0x470E870)=bookkeeping;}
            __except(EXCEPTION_EXECUTE_HANDLER){exception=GetExceptionCode();fault=true;}}
        valid=valid && !fault;if(valid)capture_stage=15;return valid;
    }
    // Current is captured at restore time, not supplied as a correct predicted
    // outcome. The original bounded topology/input/RNG guards remain mandatory.
    enum class Rejection {none,invalid_capture,module,mode,thread,used_probe,frame,players,shape,rings,rng,transforms,camera,inputs,reader,sources,combat,root_wind,charge};
    static const char* rejection_name(Rejection r){
        constexpr const char* names[]={"none","invalid_capture","module","mode","thread","used_probe","frame_adjacency","player_identity","stream_shape","ring_rotation","rng_changed","transform_topology","camera_identity","input_changed","reader_copy","serializer_source","combat_readiness","root_wind","charge_readiness"};
        return names[static_cast<unsigned>(r)];
    }
    // Pure structural assessment is available for combat captures too. Passing
    // it does not authorize loading or relax move/input-specific readiness.
    Rejection structural_readiness(const NativeBoundarySnapshot& current)const{
        if(!valid || fault || !current.valid || current.fault)return Rejection::invalid_capture;
        if(module!=current.module)return Rejection::module;
        if(thread!=current.thread || thread!=GetCurrentThreadId())return Rejection::thread;
        const auto& a=*data;const auto& b=*current.data;
        if(a.before_keys[0]==UINT32_MAX || b.before_keys[0]!=a.before_keys[0]+1u)return Rejection::frame;
        if(std::memcmp(a.before_players,b.before_players,sizeof(a.before_players)))return Rejection::players;
        if(!IdleTrajectoryAudit::same_shape(a.saved,b.saved))return Rejection::shape;
        if(!b.rings[0].rotated_from(a.rings[0]))return Rejection::rings;
        if(!a.measurements[0].rng || a.measurements[0].rng>=2147483647 ||
           !b.measurements[0].rng || b.measurements[0].rng>=2147483647)return Rejection::rng;
        if(!a.transforms[0].same_topology(b.transforms[0]))return Rejection::transforms;
        if(!IdleTrajectoryAudit::camera_identity(a.cameras[0],b.cameras[0]))return Rejection::camera;
        return Rejection::none;
    }
    bool prepare_adjacent(const NativeBoundarySnapshot& current,FullStateProbe& q,Rejection* reason=nullptr,bool direction_restore=false,bool a_restore=false,bool general_restore=false)const {
        auto reject=[&](Rejection r){if(reason)*reason=r;return false;};
        if(reason)*reason=Rejection::none;
        // Capture support must never implicitly authorize restoration. Reject
        // either side before copying streams or preparing native reader state.
        if(mode==Mode::combat_capture_only || current.mode==Mode::combat_capture_only)return reject(Rejection::mode);
        const bool general_pair=general_restore && !direction_restore && !a_restore && mode==Mode::general_combat && current.mode==Mode::general_combat;
        if(general_restore!=general_pair || ((mode==Mode::general_combat || current.mode==Mode::general_combat) && !general_pair))return reject(Rejection::mode);
        if(!valid || fault || !current.valid || current.fault)return reject(Rejection::invalid_capture);
        if(module!=current.module)return reject(Rejection::module);
        const bool a_pair=a_restore && !direction_restore && mode==Mode::idle && current.mode==Mode::a_observation;
        if((a_restore && !a_pair) || ((mode==Mode::a_observation || current.mode==Mode::a_observation) && !a_pair))return reject(Rejection::mode);
        const bool direction_pair=direction_restore && (mode==Mode::idle || mode==Mode::direction_observation) && current.mode==Mode::direction_observation;
        if(!a_pair && (direction_restore?!direction_pair:(mode==Mode::direction_observation || current.mode==Mode::direction_observation || mode!=current.mode)))return reject(Rejection::mode);
        if(thread!=current.thread || thread!=GetCurrentThreadId())return reject(Rejection::thread);
        if(q.saved.sealed || q.attempted || q.restore_invoked)return reject(Rejection::used_probe);
        const auto& a=*data;const auto& b=*current.data;
        const bool combat=mode!=Mode::idle && !direction_pair;
        const auto structural=structural_readiness(current);
        if(structural!=Rejection::none)return reject(structural);
        // Idle permits only finite normalized auxiliary variation. Buttons and
        // flags (first24bytes) remain exact, including when both inputs match.
        if(general_pair){if(!RecordedInput::valid_envelope(a.delayed_inputs) || !RecordedInput::valid_envelope(b.delayed_inputs))return reject(Rejection::inputs);}
        else if(!combat){if(!RecordedInput::compatible(a.delayed_inputs,b.delayed_inputs))return reject(Rejection::inputs);}
        else if(a.delayed_inputs!=b.delayed_inputs && !(mode==Mode::charge?
           RecordedInput::charge_compatible(a.delayed_inputs,b.delayed_inputs):RecordedInput::compatible(a.delayed_inputs,b.delayed_inputs)))return reject(Rejection::inputs);
        if(direction_pair || a_pair){
            if(direction_pair && (direction_slot!=current.direction_slot || current.direction_slot<1 || current.direction_slot>2 || direction_bits!=current.direction_bits || (current.direction_bits!=0x400 && current.direction_bits!=0x800)))return reject(Rejection::mode);
            for(unsigned i=0;i<24;++i)if(a.delayed_inputs[i]!=std::byte{} || b.delayed_inputs[i]!=std::byte{})return reject(Rejection::inputs);
            q.blend_pair_diagnostic={};q.blend_pair_evidence={};
            for(unsigned side=0;side<2;++side){
                auto& d=q.blend_pair_diagnostic;
                if(!a.blends[0].valid || !b.blends[0].valid){
                    d.reason=1;d.side=side;d.saved=a.blends[0].valid;d.next=b.blends[0].valid;return reject(Rejection::combat);
                }
                if(a.blends[0].owners[side]!=b.blends[0].owners[side]){
                    d.reason=2;d.side=side;d.saved_owner=a.blends[0].owners[side];d.next_owner=b.blends[0].owners[side];return reject(Rejection::combat);
                }
                const auto& before=a.blends[0].bytes[side];const auto& after=b.blends[0].bytes[side];
                const bool accepted=a_pair?FullStateProbe::a_blend_pair(before.data(),after.data(),side):std::memcmp(before.data(),after.data(),1372)==0;
                if(!accepted){
                    std::copy_n(before.begin(),1372,q.blend_pair_evidence.saved.begin());
                    std::copy_n(after.begin(),1372,q.blend_pair_evidence.next.begin());q.blend_pair_evidence.valid=true;
                    d.reason=a_pair?4u:3u;d.side=side;d.saved_owner=a.blends[0].owners[side];d.next_owner=b.blends[0].owners[side];
                    for(unsigned offset=0;offset<1372;++offset)if(before[offset]!=after[offset]){
                        if(!d.changed){d.offset=offset;d.saved=std::to_integer<unsigned>(before[offset]);d.next=std::to_integer<unsigned>(after[offset]);}++d.changed;
                    }
                    return reject(Rejection::combat);
                }
            }
            if(!a.cameras[0].adjacent_combat_ready(b.cameras[0],module,true))return reject(Rejection::camera);
        }
        if(!q.saved.prepare_reader_from(a.saved))return reject(Rejection::reader);
        q.fault=true; // Remains latched if any preparation guard rejects.
        q.after_save=b.saved;q.measurements[0]=a.measurements[0];q.measurements[1]=a.measurements[1];
        q.measurements[2]=b.measurements[0];q.measurements[3]=b.measurements[1];
        q.before_keys=a.before_keys;q.after_keys=b.before_keys;
        std::memcpy(q.before_players,a.before_players,sizeof(q.before_players));std::memcpy(q.after_players,b.before_players,sizeof(q.after_players));
        q.rings[0]=a.rings[0];q.rings[1]=a.rings[0];q.rings[2]=b.rings[0];q.rings[3]=b.rings[0];
        q.blends[0]=a.blends[0];q.blends[1]=b.blends[0];q.transforms[0]=a.transforms[0];q.transforms[1]=b.transforms[0];
        q.cameras[0]=a.cameras[0];q.cameras[1]=b.cameras[0];q.delayed_inputs=a.delayed_inputs;
        q.general_combat_requested=general_pair;
        q.blend_timer_restore_requested=general_pair;
        if(a.blend_timer_count)q.blend_timer_source=a.blend_timer_history[0];
        q.throw_requested=a.throw_requested;q.charge_requested=a.charge_requested;q.attack_requested=a.attack_requested;
        q.edge_requested=a.edge_requested;q.reversal_requested=a.reversal_requested;
        q.attempted=q.preflight_ok=q.save_valid=q.post_save_valid=q.audit_started=q.audit_done=true;
        q.ring_save_unchanged=q.ring_rotation_ok=q.delay_audit=q.delayed_requested=q.ring_capture_enabled=q.preserve_requested=q.camera_restore_requested=true;
        if(!q.delayed_sources_ok())return reject(Rejection::sources);
        if(combat){
            q.combat_replay_requested=q.throw_audit=q.combat_flag_requested=q.recorded_input_replay=true;
            for(unsigned i=0;i<2;++i){q.input_checkpoints[i].valid=true;q.input_checkpoints[i].bytes=i?b.delayed_inputs:a.delayed_inputs;}
            q.fault=false;q.combat_replay_preflight(module);q.fault=true;
            if((q.combat_preflight_pass_mask|2u)!=FullStateProbe::combat_preflight_required_mask)return reject(Rejection::combat);
            if(!q.transforms[0].restore_root_wind(q.transforms[1],q.transforms[1],true))return reject(Rejection::root_wind);
            if(mode==Mode::charge && (!q.cameras[0].adjacent_combat_ready(q.cameras[1],module,true) ||
               !CombatBlend::adjacent(q.blends[0].bytes[0],q.blends[1].bytes[0]) ||
               !CombatBlend::adjacent(q.blends[0].bytes[1],q.blends[1].bytes[1])))return reject(Rejection::charge);
        }
        q.a_restore_requested=a_pair;q.direction_restore_requested=direction_pair;q.direction_bits=current.direction_bits;q.direction_slot=current.direction_slot;
        q.fault=false;return true;
    }
};
}
