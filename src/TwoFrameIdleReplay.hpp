#pragma once
#include "IdleTrajectoryAudit.hpp"
namespace sc6::research {
// Bounded two/three-update rewind through adjacent restores and natural updates.
// Updates run normally through the original hook; no recursive engine calls.
struct TwoFrameIdleReplay {
    std::array<std::unique_ptr<FullStateProbe>,3> hops;
    bool prepared{},restoration_started{},rewound{},done{},fault{},combat_mode{};
    unsigned phase{},exception{},completed_restores{},compared_frames{};
    bool recorded_inputs{},charge_mode{},attack_mode{},edge_mode{},reversal_mode{};
    RecordedInput delivery,restore_input;
    uintptr_t next_argument{};
    unsigned delivered_frames{};
    std::array<bool,3> matches{};
    std::array<bool,3> scalar_matches{};
    unsigned target_updates{2};
    explicit TwoFrameIdleReplay(bool combat=false,unsigned updates=2,bool charge=false,bool attack=false,bool edge=false,bool reversal=false):combat_mode(combat),charge_mode(charge),attack_mode(attack),edge_mode(edge),reversal_mode(reversal),target_updates(updates){
        if(reversal?(combat || charge || attack || edge || updates!=3):edge?(combat || charge || attack || updates!=3):attack?(combat || charge || updates!=3):charge?(combat || updates<1 || updates>3):(updates<2 || updates>3 || (updates==3 && !combat))){fault=true;return;}
        for(unsigned i=0;i<updates;++i)hops[i]=std::make_unique<FullStateProbe>();}
    static bool seed(FullStateProbe& q,const IdleTrajectoryAudit& audit,unsigned first,bool combat=false,unsigned updates=2,bool charge=false,bool attack=false,bool edge=false,bool reversal=false){
        if(audit.reversal_mode!=reversal || (reversal && (combat || charge || attack || edge || updates!=3)) || audit.edge_mode!=edge || (edge && (combat || charge || attack || updates!=3)) || audit.structure_only || audit.attack_mode!=attack || (attack && (combat || charge || updates!=3)) || audit.charge_mode!=charge || (charge?(combat || updates<1 || updates>3):(updates<2 || updates>3)) || first>=updates || (updates==3 && (!(combat || charge || attack || edge || reversal) || !audit.recorded_inputs)) || audit.throw_mode!=combat || !audit.done || audit.fault || audit.count!=updates+1)return false;
        if((combat || charge || attack || edge || reversal) && (!audit.combat_checks[first] || !audit.combat_checks[first]->combat_preflight_attempted ||
           (audit.combat_checks[first]->combat_preflight_pass_mask | (audit.recorded_inputs?2u:0u))!=FullStateProbe::combat_preflight_required_mask ||
           !audit.root_wind_ready[first] || audit.combat_checks[first]->restore_invoked))return false;
        const auto& a=*audit.frames[first];const auto& b=*audit.frames[first+1];
        if(!a.valid || !b.valid || !a.save_unchanged || !b.save_unchanged ||
           !IdleTrajectoryAudit::adjacent_identity(a,b) || (a.inputs!=b.inputs && !(audit.recorded_inputs && (charge?RecordedInput::charge_compatible(a.inputs,b.inputs):(combat || attack || edge || reversal) && RecordedInput::compatible(a.inputs,b.inputs)))) ||
           !IdleTrajectoryAudit::same_shape(a.saved,b.saved) || !b.rings.rotated_from(a.rings))return false;
        // Charge replay permits only the explicitly recorded release pattern;
        // strict blend progression remains required before any native load.
        if(charge && (!audit.charge_camera_ready[first] ||
           !CombatBlend::adjacent(audit.visuals->blends[first].bytes[0],audit.visuals->blends[first+1].bytes[0]) ||
           !CombatBlend::adjacent(audit.visuals->blends[first].bytes[1],audit.visuals->blends[first+1].bytes[1])))return false;
        if(!q.saved.prepare_reader_from(a.saved))return false;
        q.after_save=b.saved;
        q.measurements[0]=a.before;q.measurements[1]=a.after;
        q.measurements[2]=b.before;q.measurements[3]=b.after;
        q.before_keys=a.keys;q.after_keys=b.keys;
        for(unsigned side=0;side<2;++side){q.before_players[side]=a.before.players[side];q.after_players[side]=b.before.players[side];}
        q.rings[0]=a.rings;q.rings[1]=a.rings;q.rings[2]=b.rings;q.rings[3]=b.rings;
        for(unsigned i=0;i<2;++i){
            q.blends[i]=audit.visuals->blends[first+i];q.transforms[i]=audit.visuals->transforms[first+i];
            q.cameras[i]=audit.visuals->cameras[first+i];
            if(!q.blends[i].valid || !q.transforms[i].valid || !q.cameras[i].valid)return false;
        }
        if(!q.transforms[0].same_topology(q.transforms[1]) ||
           !IdleTrajectoryAudit::camera_identity(q.cameras[0],q.cameras[1]))return false;
        q.delayed_inputs=a.inputs;
        q.recorded_input_replay=(combat || charge || attack || edge || reversal) && audit.recorded_inputs;
        q.attempted=q.preflight_ok=q.save_valid=q.post_save_valid=true;
        q.audit_started=q.audit_done=q.ring_save_unchanged=q.ring_rotation_ok=true;
        q.delay_audit=q.delayed_requested=q.ring_capture_enabled=q.preserve_requested=q.camera_restore_requested=true;
        if(combat || charge || attack || edge || reversal){
            q.combat_replay_requested=q.throw_audit=q.combat_flag_requested=true;
            q.throw_requested=combat;q.charge_requested=charge;q.attack_requested=attack;q.edge_requested=edge;q.reversal_requested=reversal;
            q.combat_later_window=first>0;
            q.combat_preflight_attempted=true;
            q.combat_preflight_pass_mask=audit.combat_checks[first]->combat_preflight_pass_mask;
            for(unsigned i=0;i<2;++i){q.input_checkpoints[i].valid=audit.frames[first+i]->input_valid;
                q.input_checkpoints[i].bytes=audit.frames[first+i]->inputs;}
        }
        return q.delayed_sources_ok() && a.before.rng==b.before.rng;
    }
    bool prepare(const IdleTrajectoryAudit& audit){
        if(prepared || restoration_started || fault)return false;
        recorded_inputs=(combat_mode || charge_mode || attack_mode || edge_mode || reversal_mode) && audit.recorded_inputs;
        prepared=true;
        for(unsigned i=0;i<target_updates && prepared;++i)prepared=seed(*hops[i],audit,i,combat_mode,target_updates,charge_mode,attack_mode,edge_mode,reversal_mode);
        if(!prepared)fault=true;
        return prepared;
    }
    static bool transforms_equal(const TransformSnapshot& a,const TransformSnapshot& b){
        if(!a.valid || !b.valid || !a.same_topology(b))return false;
        for(unsigned i=0;i<a.count;++i)
            if(std::memcmp(a.records[i].bytes.data(),b.records[i].bytes.data(),a.records[i].size))return false;
        return true;
    }
    static bool cameras_equal(const CameraSnapshot& a,const CameraSnapshot& b){
        if(!IdleTrajectoryAudit::camera_identity(a,b))return false;
        for(unsigned i=0;i<a.count;++i){const auto& x=a.records[i];const auto& y=b.records[i];
            if(x.bytes!=y.bytes || (x.smoothing_valid &&
               (x.smoothing!=y.smoothing || x.targets!=y.targets || x.response!=y.response)))return false;
        }
        return true;
    }
    struct NativeDifference {
        unsigned reason{},record{UINT32_MAX};
        uint64_t offset{},length{},writer{},source{},expected{},actual{},word_offset{},expected_word{},actual_word{},changed_bytes{};
    };
    static bool native_equal(const FullStateProbe& q,uintptr_t base,NativeDifference* diagnostic=nullptr){
        if(diagnostic)*diagnostic={};
        q.motion_zero_evidence={};
        auto reject=[&](unsigned reason,unsigned record,uint64_t offset,uint64_t length,uint64_t expected,uint64_t actual){
            if(diagnostic){diagnostic->reason=reason;diagnostic->record=record;diagnostic->offset=offset;
                diagnostic->length=length;diagnostic->expected=expected;diagnostic->actual=actual;
                if(record<q.after_save.writes){diagnostic->writer=q.after_save.traces[record].writer-base;
                    diagnostic->source=q.after_save.traces[record].source;}}
            return false;
        };
        const auto& a=q.after_save;const auto& b=q.replay;
        if(!IdleTrajectoryAudit::same_shape(a,b) || a.stream.write_position!=b.stream.write_position)return reject(1,UINT32_MAX,0,0,a.stream.write_position,b.stream.write_position);
        std::uint64_t offset=0;
        for(unsigned i=0;i<a.writes;++i){auto length=a.lengths[i];
            if(offset>a.stream.write_position || length>a.stream.write_position-offset)return reject(2,i,offset,length,0,0);
            unsigned skip=0;
            for(unsigned side=0;side<2;++side){
                const auto player=q.after_players[side];
                if(length==48 && a.traces[i].writer==base+0x30AA54 &&
                   a.traces[i].source==player+0x43DF0 && b.traces[i].source==player+0x43DF0){
                    uintptr_t rebuilt{};std::memcpy(&rebuilt,b.stream.payload.data()+offset,8);
                    if(rebuilt!=player)return reject(3,i,0,length,player,rebuilt);
                    skip=8;
                }
            }
            // Pinned writer32AC76 leaves bytes4..7 uninitialized; reader32AD1B
            // consumes only DWORD0 and QWORD8. Preserve raw payloads and counts.
            const bool pointer_record_padding=length==16 && a.traces[i].writer==base+0x32AC76 &&
                q.restore_completed && q.saved.sealed && i<q.saved.writes && q.saved.lengths[i]==16 &&
                q.saved.traces[i].writer==base+0x32AC76 && q.saved.traces[i].reader==base+0x32AD1B;
            // Comparison-only signed-zero equivalence for observed motion W fields.
            // 306530 uses +6C as sample W and +7C as delta W; XYZ drives position.
            // Keep nonzero values and every other field byte-exact.
            // Pin serializer, both player-relative sources, record size and exact zeros.
            constexpr unsigned motion_words[]={0x11C,0x12C,0x1CC};
            bool motion_zero[3]{};
            if(q.general_combat_requested && q.restore_completed && length==0x370 &&
               a.traces[i].writer==base+0x30AA80 && b.traces[i].writer==base+0x30AA80){
                for(auto player:q.after_players)if(player && a.traces[i].source==player+0x45230 &&
                    b.traces[i].source==player+0x45230){
                    for(unsigned field=0;field<3;++field){
                    const auto word=motion_words[field];
                    uint32_t x{},y{};
                    std::memcpy(&x,a.stream.payload.data()+offset+word,4);
                    std::memcpy(&y,b.stream.payload.data()+offset+word,4);
                    if((x^y)==0x80000000u && !(x&0x7FFFFFFFu) && !(y&0x7FFFFFFFu)){
                        motion_zero[field]=true;
                        q.motion_zero_evidence={q.motion_zero_evidence[0]+1,i,word,x,y};
                    }
                    }
                    break; // Count each field once even if player identities alias.
                }
            }
            const bool differs=pointer_record_padding?
                (std::memcmp(a.stream.payload.data()+offset,b.stream.payload.data()+offset,4) ||
                 std::memcmp(a.stream.payload.data()+offset+8,b.stream.payload.data()+offset+8,8)):
                std::memcmp(a.stream.payload.data()+offset+skip,b.stream.payload.data()+offset+skip,static_cast<size_t>(length-skip))!=0;
            if(differs){
                for(uint64_t j=skip;j<length;++j){
                    if(pointer_record_padding && j>=4 && j<8)continue;
                    bool zero_sign=false;
                    for(unsigned field=0;field<3;++field)
                        zero_sign |= motion_zero[field] && j==motion_words[field]+3;
                    if(zero_sign)continue;
                    const auto expected=a.stream.payload[offset+j],actual=b.stream.payload[offset+j];
                    if(expected!=actual){
                        reject(4,i,j,length,std::to_integer<unsigned>(expected),std::to_integer<unsigned>(actual));
                        if(diagnostic){
                            diagnostic->word_offset=j & ~uint64_t{7};
                            const auto bytes=static_cast<size_t>((std::min)(uint64_t{8},length-diagnostic->word_offset));
                            std::memcpy(&diagnostic->expected_word,a.stream.payload.data()+offset+diagnostic->word_offset,bytes);
                            std::memcpy(&diagnostic->actual_word,b.stream.payload.data()+offset+diagnostic->word_offset,bytes);
                            for(uint64_t k=skip;k<length;++k){
                                if(pointer_record_padding && k>=4 && k<8)continue;
                                if(a.stream.payload[offset+k]!=b.stream.payload[offset+k])++diagnostic->changed_bytes;
                            }
                        }
                        return false;
                    }
                }
                // All differing bytes were explicitly accounted for above.
            }
            offset+=length;
        }
        return offset==a.stream.write_position || reject(5,UINT32_MAX,offset,0,a.stream.write_position,offset);
    }
    // Diagnostic pass bits, evaluated independently so one failure cannot hide another.
    static unsigned comparison_mask(const FullStateProbe& q,uintptr_t base){
        unsigned mask=0;
        if(!q.fault)mask|=1;
        if(q.replay_done)mask|=2;
        if(q.replay_keys_equal)mask|=4;
        if(q.replay_ring_equal)mask|=8;
        if(q.blends[1].valid && q.blends[4].valid)mask|=16;
        if(q.blends[1].bytes==q.blends[4].bytes)mask|=32;
        if(!std::memcmp(q.blends[1].owners,q.blends[4].owners,sizeof(q.blends[1].owners)))mask|=64;
        if(transforms_equal(q.transforms[1],q.transforms[4]))mask|=128;
        if(cameras_equal(q.cameras[1],q.cameras[3]))mask|=256;
        if(native_equal(q,base))mask|=512;
        return mask;
    }
    static bool comparison_ok(const FullStateProbe& q,uintptr_t base){
        return !q.fault && q.replay_done && q.replay_keys_equal && q.replay_ring_equal &&
            q.blends[1].valid && q.blends[4].valid &&
            q.blends[1].bytes==q.blends[4].bytes &&
            !std::memcmp(q.blends[1].owners,q.blends[4].owners,sizeof(q.blends[1].owners)) &&
            transforms_equal(q.transforms[1],q.transforms[4]) && cameras_equal(q.cameras[1],q.cameras[3]) && native_equal(q,base);
    }
    void step(uintptr_t base,uintptr_t argument,FullStateProbe::Routine writer=nullptr,
              FullStateProbe::Routine reader=nullptr,FullStateProbe::Preflight check=nullptr) noexcept {
        next_argument=0;
        if(fault || done)return;
        __try {
            if(!prepared){fault=true;__leave;}
            if(recorded_inputs){std::array<std::byte,40> live{};
                if(!FullStateProbe::input_bytes(argument,live) || !(charge_mode?RecordedInput::charge_reachable(hops[0]->delayed_inputs,live):RecordedInput::compatible(hops[0]->delayed_inputs,live))){fault=true;__leave;}}
            if(!rewound){
                restoration_started=true;
                for(unsigned n=0;n<target_updates;++n){auto& q=*hops[target_updates-1-n];phase=1+n;
                    std::array<std::uint32_t,std::size(FullStateProbe::key_rvas)> current_keys{};uintptr_t players[2]{};
                    FullStateProbe::keys(base,current_keys.data(),players);
                    if(current_keys!=q.after_keys || std::memcmp(players,q.after_players,sizeof(players))){fault=true;__leave;}
                    q.delayed_step(base,recorded_inputs?restore_input.assign(q.delayed_inputs):argument,writer,reader,check);
                    if(q.fault || !q.delayed_loaded || !q.camera_restore_verified){fault=true;__leave;}
                    ++completed_restores;
                }
                rewound=true;phase=target_updates+1;
                if(recorded_inputs){next_argument=delivery.assign(hops[0]->delayed_inputs);++delivered_frames;}
            }else{
                auto& q=*hops[compared_frames];phase=target_updates+1+compared_frames;
                FullStateProbe::Measurement current{};IdleTrajectoryAudit::measure(base,current);
                scalar_matches[compared_frames]=current.rng==q.original_next.rng &&
                    !std::memcmp(current.players,q.original_next.players,sizeof(current.players));
                for(unsigned side=0;side<2;++side)
                    scalar_matches[compared_frames]=scalar_matches[compared_frames] &&
                        !std::memcmp(current.fields[side].data()+8,q.original_next.fields[side].data()+8,216);
                // This envelope will drive the second replay update after this hook.
                if((combat_mode || charge_mode || attack_mode || edge_mode || reversal_mode) && !recorded_inputs && compared_frames+1<target_updates){std::array<std::byte,40> inputs{};
                    if(!FullStateProbe::input_bytes(argument,inputs) || inputs!=hops[1]->delayed_inputs){fault=true;__leave;}}
                q.delayed_step(base,argument,writer,reader,check);
                matches[compared_frames]=scalar_matches[compared_frames] && comparison_ok(q,base);
                ++compared_frames;
                if(!matches[compared_frames-1]){fault=true;__leave;}
                done=compared_frames==target_updates;
                if(done)phase=2*target_updates+1;
                else if(recorded_inputs){next_argument=delivery.assign(hops[compared_frames]->delayed_inputs);++delivered_frames;}
            }
        }__except(EXCEPTION_EXECUTE_HANDLER){exception=GetExceptionCode();fault=true;}
    }
};
}
