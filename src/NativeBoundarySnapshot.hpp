#pragma once
#include "IdleTrajectoryAudit.hpp"
namespace sc6::research {
// A single boundary, independent of a pre-recorded reference trajectory.
// Capture only on the offline update thread. Does not invoke native loading.
struct NativeBoundarySnapshot {
    enum class Mode {idle,throw_move,charge,attack,edge,reversal,direction_observation,a_observation};
    std::unique_ptr<FullStateProbe> data=std::make_unique<FullStateProbe>();
    unsigned direction_bits{0x400};
    Mode mode;uintptr_t module{};DWORD thread{};bool attempted{},valid{},fault{};unsigned exception{};
    explicit NativeBoundarySnapshot(Mode value):mode(value){
        data->throw_requested=value==Mode::throw_move;data->charge_requested=value==Mode::charge;
        data->attack_requested=value==Mode::attack;data->edge_requested=value==Mode::edge;
        data->reversal_requested=value==Mode::reversal;
    }
    static bool direction_state(uintptr_t base,unsigned bits=0x400){
        if(bits!=0x400 && bits!=0x800)return false;
        return *reinterpret_cast<std::uint16_t*>(base+0x4717840)==bits &&
            *reinterpret_cast<std::uint16_t*>(base+0x47AEC30)==0 &&
            *reinterpret_cast<std::uint16_t*>(base+0x475A04A)==65535 &&
            *reinterpret_cast<std::uint16_t*>(base+0x47F143A)==65535 &&
            *reinterpret_cast<float*>(base+0x48462F4)==1.0f &&
            *reinterpret_cast<unsigned char*>(base+0x48462D0)==0;
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
            if(!check(base) || !(mode==Mode::a_observation?a_state(base):mode==Mode::direction_observation?direction_state(base,direction_bits):mode==Mode::idle?FullStateProbe::idle(base):q.combat_state(base)) ||
               !FullStateProbe::input_bytes(argument,q.delayed_inputs) || !q.rings[0].capture(base)){fault=true;__leave;}
            q.measure(base,0);FullStateProbe::keys(base,q.before_keys.data(),q.before_players);
            bookkeeping=FullStateProbe::ptr(base+0x470E870);saved_bookkeeping=true;
            writer(&q.saved.stream);q.measure(base,1);
            if(!q.saved.seal() || !IdleTrajectoryAudit::same_measurement(q.measurements[0],q.measurements[1]) ||
               !q.rings[1].capture(base) || !q.rings[0].same(q.rings[1]) ||
               !q.capture_blend(base,0) || !q.cameras[0].capture(q.saved,base)){fault=true;__leave;}
            std::array<std::uint32_t,std::size(FullStateProbe::key_rvas)> keys{};uintptr_t players[2]{};
            FullStateProbe::keys(base,keys.data(),players);
            if(keys!=q.before_keys || std::memcmp(players,q.before_players,sizeof(players))){fault=true;__leave;}
            valid=true;
        }__except(EXCEPTION_EXECUTE_HANDLER){exception=GetExceptionCode();fault=true;}
        if(saved_bookkeeping){__try{*reinterpret_cast<std::uint64_t*>(base+0x470E870)=bookkeeping;}
            __except(EXCEPTION_EXECUTE_HANDLER){exception=GetExceptionCode();fault=true;}}
        valid=valid && !fault;return valid;
    }
    // Current is captured at restore time, not supplied as a correct predicted
    // outcome. The original bounded topology/input/RNG guards remain mandatory.
    enum class Rejection {none,invalid_capture,module,mode,thread,used_probe,frame,players,shape,rings,rng,transforms,camera,inputs,reader,sources,combat,root_wind,charge};
    static const char* rejection_name(Rejection r){
        constexpr const char* names[]={"none","invalid_capture","module","mode","thread","used_probe","frame_adjacency","player_identity","stream_shape","ring_rotation","rng_changed","transform_topology","camera_identity","input_changed","reader_copy","serializer_source","combat_readiness","root_wind","charge_readiness"};
        return names[static_cast<unsigned>(r)];
    }
    bool prepare_adjacent(const NativeBoundarySnapshot& current,FullStateProbe& q,Rejection* reason=nullptr,bool direction_restore=false,bool a_restore=false)const {
        auto reject=[&](Rejection r){if(reason)*reason=r;return false;};
        if(reason)*reason=Rejection::none;
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
        if(b.before_keys[0]!=a.before_keys[0]+1u)return reject(Rejection::frame);
        if(std::memcmp(a.before_players,b.before_players,sizeof(a.before_players)))return reject(Rejection::players);
        if(!IdleTrajectoryAudit::same_shape(a.saved,b.saved))return reject(Rejection::shape);
        if(!b.rings[0].rotated_from(a.rings[0]))return reject(Rejection::rings);
        if(a.measurements[0].rng!=b.measurements[0].rng)return reject(Rejection::rng);
        if(!a.transforms[0].same_topology(b.transforms[0]))return reject(Rejection::transforms);
        if(!IdleTrajectoryAudit::camera_identity(a.cameras[0],b.cameras[0]))return reject(Rejection::camera);
        // Idle permits only finite normalized auxiliary variation. Buttons and
        // flags (first24bytes) remain exact, including when both inputs match.
        if(!combat){if(!RecordedInput::compatible(a.delayed_inputs,b.delayed_inputs))return reject(Rejection::inputs);}
        else if(a.delayed_inputs!=b.delayed_inputs && !(mode==Mode::charge?
           RecordedInput::charge_compatible(a.delayed_inputs,b.delayed_inputs):RecordedInput::compatible(a.delayed_inputs,b.delayed_inputs)))return reject(Rejection::inputs);
        if(direction_pair || a_pair){
            if(direction_pair && (direction_bits!=current.direction_bits || (current.direction_bits!=0x400 && current.direction_bits!=0x800)))return reject(Rejection::mode);
            for(unsigned i=0;i<24;++i)if(a.delayed_inputs[i]!=std::byte{} || b.delayed_inputs[i]!=std::byte{})return reject(Rejection::inputs);
            for(unsigned side=0;side<2;++side)if(!a.blends[0].valid || !b.blends[0].valid ||
                a.blends[0].owners[side]!=b.blends[0].owners[side] ||
                (a_pair?!FullStateProbe::a_blend_pair(a.blends[0].bytes[side].data(),b.blends[0].bytes[side].data(),side):std::memcmp(a.blends[0].bytes[side].data(),b.blends[0].bytes[side].data(),1372)!=0))return reject(Rejection::combat);
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
        q.a_restore_requested=a_pair;q.direction_restore_requested=direction_pair;q.direction_bits=current.direction_bits;
        q.fault=false;return true;
    }
};
}
