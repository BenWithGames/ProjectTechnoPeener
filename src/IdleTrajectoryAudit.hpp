#pragma once
#include "FullStateProbe.hpp"
#include <memory>
#include "RecordedInput.hpp"
namespace sc6::research {
// Three reference boundaries spanning two natural updates. No native reader.
struct IdleTrajectoryAudit {
    struct Frame {
        FullRestoreStream saved;
        RingSnapshot rings;
        FullStateProbe::Measurement before{},after{};
        std::array<std::uint32_t,std::size(FullStateProbe::key_rvas)> keys{};
        std::array<std::byte,40> inputs{};
        std::array<uintptr_t,3> input_sources{};
        bool valid{},save_unchanged{},adjacent{},inputs_equal{},shape_equal{},input_attempted{},input_valid{};
        std::uint64_t input_changed_mask{};
    };
    std::array<std::unique_ptr<Frame>,4> frames;
    RingSnapshot scratch;
    std::unique_ptr<FullStateProbe> visuals=std::make_unique<FullStateProbe>();
    std::array<std::unique_ptr<FullStateProbe>,3> combat_checks;
    std::array<bool,3> root_wind_ready{},charge_camera_attempted{},charge_camera_ready{};
    unsigned count{},exception{};bool fault{},done{},throw_mode{};
    unsigned target_frames{3};
    bool recorded_inputs{},charge_mode{},attack_mode{},structure_only{},edge_mode{},reversal_mode{};
    bool attack_start_seen{};
    unsigned attack_start_outer{},start_waited_updates{};
    unsigned reference_start_delay()const noexcept {return structure_only?0u:reversal_mode?4u:attack_mode?1u:0u;}
    std::array<uintptr_t,2> attack_start_players{};
    bool attack_window_ready(unsigned outer,const uintptr_t* players) noexcept {
        // Later Reversal window: retain exact consecutive boundaries and identities.
        // Startup RNG transitions remain outside this bounded experiment.
        if(!(attack_mode || reversal_mode) || structure_only || count)return true;
        if(!attack_start_seen){attack_start_seen=true;attack_start_outer=outer;
            attack_start_players={players[0],players[1]};return false;}
        if(outer!=attack_start_outer+start_waited_updates+1u || players[0]!=attack_start_players[0] || players[1]!=attack_start_players[1]){fault=true;return false;}
        if(start_waited_updates+1u<reference_start_delay()){++start_waited_updates;return false;}
        return true;
    }
    bool auxiliary_observation() const noexcept {return reversal_mode || edge_mode || attack_mode || charge_mode || (throw_mode && (target_frames==4 || recorded_inputs));}
    bool inputs_supported(const Frame& f) const noexcept {
        if(f.inputs_equal)return true;
        if(!auxiliary_observation() || !f.input_valid)return false;
        const auto& original=frames[0]->inputs;
        if(charge_mode && RecordedInput::charge_reachable(original,f.inputs))return true;
        if(std::memcmp(f.inputs.data(),original.data(),24)){
            // Observed charge-only camera flag release. Raw inputs stay intact;
            // these changed envelopes are rejected by charge replay seeding.
            unsigned before{},after{};
            std::memcpy(&before,original.data()+16,4);
            std::memcpy(&after,f.inputs.data()+16,4);
            const bool flag_only=before==0x807u && after==0x7u &&
                !std::memcmp(f.inputs.data(),original.data(),16);
            unsigned player_before{},player_after{};
            std::memcpy(&player_before,original.data(),4);
            std::memcpy(&player_after,f.inputs.data(),4);
            const bool activation_release=before==0x807u && after==0 &&
                player_before==0x807u && (player_after==0x7u || player_after==0x807u) &&
                !std::memcmp(f.inputs.data()+4,original.data()+4,12);
            if(!charge_mode || (!flag_only && !activation_release) ||
               std::memcmp(f.inputs.data()+20,original.data()+20,4))return false;
        }
        // Save-only observation: retain actual finite normalized axis values.
        // Only the exact charge-only release patterns above may change buttons/flags.
        for(unsigned off=24;off<40;off+=4){
            unsigned a{},b{};
            std::memcpy(&a,f.inputs.data()+off,4);
            std::memcpy(&b,frames[0]->inputs.data()+off,4);
            if((a&0x7FFFFFFFu)>0x3F800000u || (b&0x7FFFFFFFu)>0x3F800000u)return false;
        }
        return true;
    }
    bool pair_observable(unsigned mask,bool root_ready) const noexcept {
        auto required=FullStateProbe::combat_preflight_required_mask;
        if(auxiliary_observation())required&=~2u;
        return (mask&required)==required && root_ready;
    }
    explicit IdleTrajectoryAudit(bool combat=false,unsigned target=3,bool charge=false,bool attack=false,bool structure=false,bool edge=false,bool reversal=false):throw_mode(combat),target_frames(target),charge_mode(charge),attack_mode(attack),structure_only(structure),edge_mode(edge),reversal_mode(reversal){
        if((reversal && (combat || charge || attack || edge || target!=4)) || (edge && (combat || charge || attack || structure || target!=4)) || (structure && !attack && !reversal) || (attack && (combat || charge || target!=4)) || target<2 || target>4 || (target==2 && !charge) || (charge && combat)){fault=true;return;}
        for(auto& f:frames)f=std::make_unique<Frame>();
        if(combat || charge || attack || edge || reversal)for(unsigned i=0;i<target_frames-1;++i)combat_checks[i]=std::make_unique<FullStateProbe>();
    }
    bool check_combat_pair(uintptr_t b,unsigned first){
        if(first>=target_frames-1 || first>=combat_checks.size() || !combat_checks[first])return false;
        auto& q=*combat_checks[first];auto& a=*frames[first];auto& next=*frames[first+1];
        if(!a.valid || !next.valid)return false;
        q.saved=a.saved;q.after_save=next.saved;
        q.measurements[0]=a.after;
        q.rings[0]=a.rings;q.rings[2]=next.rings;
        q.ring_save_unchanged=a.save_unchanged && next.save_unchanged;
        q.ring_rotation_ok=next.rings.rotated_from(a.rings);
        q.audit_shape_changes=same_shape(a.saved,next.saved)?0u:1u;
        q.throw_audit=q.audit_done=true;
        q.throw_requested=throw_mode;q.charge_requested=charge_mode;q.attack_requested=attack_mode;q.edge_requested=edge_mode;q.reversal_requested=reversal_mode;
        for(unsigned i=0;i<2;++i){
            q.input_checkpoints[i].valid=frames[first+i]->input_valid;
            q.input_checkpoints[i].bytes=frames[first+i]->inputs;
            q.blends[i]=visuals->blends[first+i];q.transforms[i]=visuals->transforms[first+i];
            q.cameras[i]=visuals->cameras[first+i];
        }
        q.combat_replay_preflight(b);
        root_wind_ready[first]=q.transforms[0].restore_root_wind(q.transforms[1],q.transforms[1],true);
        return pair_observable(q.combat_preflight_pass_mask,root_wind_ready[first]);
    }
    static void measure(uintptr_t b,FullStateProbe::Measurement& m){
        m.rng=*reinterpret_cast<std::uint32_t*>(b+0x485EB28);
        for(unsigned i=0;i<2;++i){
            auto p=FullStateProbe::ptr(b+0x470DE90+i*8);m.players[i]=p;
            std::memcpy(m.fields[i].data(),reinterpret_cast<void*>(p+0x43DF0),8);
            std::memcpy(m.fields[i].data()+8,reinterpret_cast<void*>(p+0x97100),120);
            std::memcpy(m.fields[i].data()+128,reinterpret_cast<void*>(p+0x971E8),96);
        }
        m.valid=true;
    }
    static bool same_measurement(const FullStateProbe::Measurement& a,const FullStateProbe::Measurement& b){
        return a.valid && b.valid && a.rng==b.rng && a.fields==b.fields &&
            a.players[0]==b.players[0] && a.players[1]==b.players[1];
    }
    static bool same_shape(const FullRestoreStream& a,const FullRestoreStream& b){
        if(!a.sealed || !b.sealed || a.writes!=b.writes)return false;
        for(unsigned i=0;i<a.writes;++i)
            if(a.lengths[i]!=b.lengths[i] || a.traces[i].writer!=b.traces[i].writer)return false;
        return true;
    }
    static bool adjacent_identity(const Frame& a,const Frame& b){
        return a.valid && b.keys[0]==a.keys[0]+1u &&
            a.before.players[0]==b.before.players[0] && a.before.players[1]==b.before.players[1];
    }
    static bool camera_identity(const CameraSnapshot& a,const CameraSnapshot& b){
        if(!a.valid || !b.valid || !a.count || a.count!=b.count)return false;
        for(unsigned i=0;i<a.count;++i)
            if(a.records[i].owner!=b.records[i].owner || a.records[i].smoothing_valid!=b.records[i].smoothing_valid ||
               std::memcmp(a.records[i].bytes.data(),b.records[i].bytes.data(),8))return false;
        return true;
    }
    void step(uintptr_t b,uintptr_t argument,FullStateProbe::Routine writer=nullptr,
              FullStateProbe::Preflight check=nullptr) noexcept {
        if(done || fault)return;
        if(!writer)writer=reinterpret_cast<FullStateProbe::Routine>(b+0x3841E0);
        if(!check)check=FullStateProbe::preflight;
        std::uint64_t bookkeeping{};bool captured=false;
        __try {
            auto& f=*frames[count];
            f.input_attempted=true;
            f.input_valid=FullStateProbe::input_bytes(argument,f.inputs,&f.input_sources);
            if(f.input_valid && count)for(unsigned byte=0;byte<f.inputs.size();++byte)
                if(f.inputs[byte]!=frames[0]->inputs[byte])f.input_changed_mask|=std::uint64_t{1}<<byte;
            if(!check(b) || !(reversal_mode?FullStateProbe::reversal_values(
                *reinterpret_cast<std::uint16_t*>(b+0x475A04A),*reinterpret_cast<std::uint16_t*>(b+0x47F143A),
                *reinterpret_cast<float*>(b+0x48462F4),*reinterpret_cast<int*>(b+0x4846458)):edge_mode?FullStateProbe::edge_values(
                *reinterpret_cast<std::uint16_t*>(b+0x475A04A),*reinterpret_cast<std::uint16_t*>(b+0x47F143A),
                *reinterpret_cast<float*>(b+0x48462F4),*reinterpret_cast<int*>(b+0x4846458)):attack_mode?FullStateProbe::attack_values(
                *reinterpret_cast<std::uint16_t*>(b+0x475A04A),*reinterpret_cast<std::uint16_t*>(b+0x47F143A),
                *reinterpret_cast<float*>(b+0x48462F4),*reinterpret_cast<int*>(b+0x4846458)):charge_mode?(FullStateProbe::charge_values(
                *reinterpret_cast<std::uint16_t*>(b+0x475A04A),*reinterpret_cast<std::uint16_t*>(b+0x47F143A),
                *reinterpret_cast<float*>(b+0x48462F4),*reinterpret_cast<int*>(b+0x4846458)) && FullStateProbe::charge_aux_active(b)):throw_mode?FullStateProbe::throw_values(
                *reinterpret_cast<std::uint16_t*>(b+0x475A04A),*reinterpret_cast<std::uint16_t*>(b+0x47F143A),
                *reinterpret_cast<float*>(b+0x48462F4),*reinterpret_cast<int*>(b+0x4846458)):FullStateProbe::idle(b)) ||
               !f.input_valid){fault=true;__leave;}
            FullStateProbe::keys(b,f.keys.data(),f.before.players);
            measure(b,f.before);
            if(!attack_window_ready(f.keys[0],f.before.players))__leave;
            f.inputs_equal=!count || f.inputs==frames[0]->inputs;
            f.adjacent=!count || adjacent_identity(*frames[count-1],f);
            if((!structure_only && !inputs_supported(f)) || !f.adjacent || !f.rings.capture(b)){fault=true;__leave;}
            if(count && !f.rings.rotated_from(frames[count-1]->rings)){fault=true;__leave;}
            bookkeeping=FullStateProbe::ptr(b+0x470E870);captured=true;
            writer(&f.saved.stream);measure(b,f.after);
            f.save_unchanged=same_measurement(f.before,f.after) && scratch.capture(b) && f.rings.same(scratch);
            f.valid=f.saved.seal() && f.save_unchanged;
            f.shape_equal=!count || same_shape(frames[0]->saved,f.saved);
            if(!f.valid || (!structure_only && !f.shape_equal)){fault=true;__leave;}
            // Reference snapshots are taken after the native save, before the natural update.
            if(structure_only){
                // Observe changing topology without authorizing any native load.
                visuals->capture_blend(b,count);visuals->cameras[count].capture(f.saved,b);
            }else if(!visuals->capture_blend(b,count) || !visuals->cameras[count].capture(f.saved,b) ||
               (count && !camera_identity(visuals->cameras[0],visuals->cameras[count]))){fault=true;__leave;}
            ++count;
            if(charge_mode && count>1){auto pair=count-2;charge_camera_attempted[pair]=true;
                charge_camera_ready[pair]=visuals->cameras[pair].adjacent_combat_ready(visuals->cameras[pair+1],b,true);
                if(!charge_camera_ready[pair]){fault=true;__leave;}}
            if(!structure_only && (throw_mode || charge_mode || attack_mode || edge_mode || reversal_mode) && count>1 && !check_combat_pair(b,count-2)){fault=true;__leave;}
            done=count==target_frames;
        }__except(EXCEPTION_EXECUTE_HANDLER){exception=GetExceptionCode();fault=true;}
        if(captured){__try{*reinterpret_cast<std::uint64_t*>(b+0x470E870)=bookkeeping;}
            __except(EXCEPTION_EXECUTE_HANDLER){exception=GetExceptionCode();fault=true;}}
    }
};
}
