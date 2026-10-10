#pragma once
#include "FullRestoreStream.hpp"
#include "RingSnapshot.hpp"
#include "TransformSnapshot.hpp"
#include "CameraSnapshot.hpp"
#include "ThrowCameraFlag.hpp"
#include "CombatBlend.hpp"
namespace sc6::research {
struct FullStateProbe {
    FullRestoreStream saved,after_save;
    std::array<CameraSnapshot,6> cameras{};
    bool camera_restore_requested{},camera_restore_verified{};
    struct Measurement {
        std::uint32_t rng{}; uintptr_t players[2]{};
        std::array<std::array<std::byte,224>,2> fields{};
        bool valid{};
    };
    std::array<Measurement,7> measurements{};
    static constexpr const char* measurement_names[]={"before_save","after_save","after_load","after_post_save","after_preservation","restored_before_save","restored_after_save"};
    void measure(uintptr_t b,unsigned phase_index){
        auto& m=measurements[phase_index];
        m.rng=*reinterpret_cast<std::uint32_t*>(b+0x485EB28);
        for(unsigned i=0;i<2;++i){
            auto p=ptr(b+0x470DE90+i*8);m.players[i]=p;
            std::memcpy(m.fields[i].data(),reinterpret_cast<void*>(p+0x43DF0),8);
            std::memcpy(m.fields[i].data()+8,reinterpret_cast<void*>(p+0x97100),120);
            std::memcpy(m.fields[i].data()+128,reinterpret_cast<void*>(p+0x971E8),96);
        }
        m.valid=true;
    }
    bool preserve_requested{},preserve_applied{},preserve_verified{},throw_requested{},charge_requested{},attack_requested{},edge_requested{},reversal_requested{},trigger_matched{};
    static constexpr unsigned scalar_offsets[]={0x20,0x24,0x30,0x34,0x40,0x50};
    static constexpr unsigned throw_scalar_offsets[]={0x1C,0x38};
    static bool combat_transition(const std::array<std::byte,224>& before,
                                 const std::array<std::byte,224>& after,unsigned side,bool charge,bool reversal=false,bool later_throw=false){
        if(side>1 || (later_throw && (charge || reversal)))return false;
        uintptr_t object{},data{};
        std::memcpy(&object,before.data()+128,8);
        std::memcpy(&data,before.data()+136,8);
        unsigned old_mode{},new_mode{},old_flag{},new_flag{};
        std::memcpy(&old_mode,before.data()+128+0x1C,4);
        std::memcpy(&new_mode,after.data()+128+0x1C,4);
        std::memcpy(&old_flag,before.data()+128+0x38,4);
        std::memcpy(&new_flag,after.data()+128+0x38,4);
        // Exact captured A+G/Soul Charge/Soul Attack/Critical Edge/Reversal Edge transition; non-null pointers enable mode 1.
        // Pointer identity is additionally checked by the unknown-byte guard.
        return object && data && old_mode==((charge || side==0)?1u:0u) &&
            old_flag==(later_throw?1u:((reversal || side==0)?0u:1u)) && new_mode==1 && new_flag==0;
    }
    static std::uint32_t advance_rng(std::uint32_t seed){
        for(unsigned i=0;i<4;++i)seed=static_cast<std::uint32_t>(static_cast<std::uint64_t>(seed)*16807%2147483647);
        return seed;
    }
    bool preserve(uintptr_t b){
        auto& source=measurements[0];auto& written=measurements[1];auto& loaded=measurements[2];
        if(!source.valid || !written.valid || !loaded.valid || source.rng==0 || source.rng>=2147483647 ||
           source.rng!=written.rng || loaded.rng!=advance_rng(source.rng) || source.fields!=written.fields)return false;
        for(unsigned side=0;side<2;++side){
            auto player=ptr(b+0x470DE90+side*8);
            if(player!=source.players[side] || player!=written.players[side] || player!=loaded.players[side])return false;
            uintptr_t rebuilt{};std::memcpy(&rebuilt,loaded.fields[side].data(),8);
            if(rebuilt!=player)return false;
            if(combat_requested() && !combat_transition(source.fields[side],loaded.fields[side],side,charge_requested || attack_requested || edge_requested,reversal_requested,
                combat_later_window && combat_replay_requested && throw_requested && delayed_requested))return false;
            // Reject unexpected changes before any preservation writes.
            for(unsigned k=8;k<224;++k){
                bool allowed=k<128;
                for(auto off:scalar_offsets)if(k>=128+off && k<132+off)allowed=true;
                if(throw_requested)for(auto off:throw_scalar_offsets)if(k>=128+off && k<132+off)allowed=true;
                if((charge_requested || attack_requested || edge_requested) && k>=128+0x38 && k<132+0x38)allowed=true;
                if(reversal_requested && side==1 && k>=128+0x1C && k<132+0x1C)allowed=true;
                if(!allowed && source.fields[side][k]!=loaded.fields[side][k])return false;
            }
        }
        preserve_applied=true;
        for(unsigned side=0;side<2;++side){
            auto player=source.players[side];
            std::memcpy(reinterpret_cast<void*>(player+0x97100),source.fields[side].data()+8,120);
            for(auto off:scalar_offsets)std::memcpy(reinterpret_cast<void*>(player+0x971E8+off),source.fields[side].data()+128+off,4);
        }
        if(throw_requested)for(unsigned side=0;side<2;++side)
            for(auto off:throw_scalar_offsets)
                std::memcpy(reinterpret_cast<void*>(source.players[side]+0x971E8+off),source.fields[side].data()+128+off,4);
        if(charge_requested || attack_requested || edge_requested)for(unsigned side=0;side<2;++side)
            std::memcpy(reinterpret_cast<void*>(source.players[side]+0x97220),source.fields[side].data()+128+0x38,4);
        if(reversal_requested)std::memcpy(reinterpret_cast<void*>(source.players[1]+0x97204),source.fields[1].data()+128+0x1C,4);
        *reinterpret_cast<std::uint32_t*>(b+0x485EB28)=source.rng;
        measure(b,4);auto& result=measurements[4];
        preserve_verified=result.rng==source.rng;
        for(unsigned side=0;side<2;++side){
            preserve_verified=preserve_verified && result.players[side]==source.players[side] &&
                !std::memcmp(result.fields[side].data()+8,source.fields[side].data()+8,216) &&
                !std::memcmp(result.fields[side].data(),loaded.fields[side].data(),8);
        }
        return preserve_verified;
    }
    using Routine=void* (*)(SaveStream*);
    using Preflight=bool (*)(uintptr_t);
    static constexpr unsigned key_rvas[]={0x470D0C4,0x475A038,0x47F1428,0x48462D0,0x48462D4,0x48462F4,0x4846458};
    std::array<std::uint32_t,std::size(key_rvas)> before_keys{},after_keys{};
    uintptr_t before_players[2]{},after_players[2]{};
    bool attempted{},preflight_ok{},save_valid{},restore_invoked{},restore_completed{},postflight_ok{},selected_equal{},post_save_valid{},fault{};
    DWORD exception{}; LONGLONG elapsed_qpc{};
    std::uint64_t byte_differences{};
    unsigned phase{};
    static uintptr_t ptr(uintptr_t a){return SaveProbe::ptr(a);}
    static bool method(uintptr_t o,unsigned s,uintptr_t b,unsigned r){return SaveProbe::method(o,s,b,r);}
    static uintptr_t weak(uintptr_t b,uintptr_t a){
        auto index=*reinterpret_cast<int*>(a),serial=*reinterpret_cast<int*>(a+4);
        auto count=*reinterpret_cast<int*>(b+0x42A115C);
        if(!serial || index<0 || index>=count)return 0;
        auto item=ptr(b+0x42A1150)+static_cast<uintptr_t>(index)*24;
        if(*reinterpret_cast<int*>(item+16)!=serial || (*reinterpret_cast<unsigned*>(item+8)&0x30000000))return 0;
        return ptr(item);
    }
    static bool callbacks(uintptr_t b){
        auto interface=ptr(b+0x470D188);
        if(!method(interface,0xC0,b,0x400760))return false;
        auto delegate=interface+0xA18;
        if(*reinterpret_cast<int*>(delegate+0x50)!=4)return false;
        auto list=ptr(delegate+0x40);if(!list)list=delegate;
        constexpr unsigned targets[]={0x3C6050,0x3C6050,0x5112A0,0x3C6020};
        for(unsigned i=0;i<4;++i){
            auto entry=list+i*0x40,object=ptr(entry+0x20);if(!object)object=entry;
            if(*reinterpret_cast<int*>(entry+0x30)<=0 || !method(object,0x68,b,0x3EF060) || ptr(object+0x10)!=b+targets[i])return false;
            auto target=weak(b,object+8);if(!target)return false;
            if(i<2){
                if(ptr(target)!=b+0x3268078 || *reinterpret_cast<unsigned*>(target+0x3A0)!=i ||
                   *reinterpret_cast<int*>(target+0x528)!=0)return false;
                constexpr unsigned slots[]={0x5F8,0x600,0x680,0x698};
                constexpr unsigned methods[]={0x3BF540,0x3BF8A0,0x3BE3A0,0x3BE670};
                for(unsigned j=0;j<4;++j)if(!method(target,slots[j],b,methods[j]))return false;
                auto n=*reinterpret_cast<int*>(target+0x3B8);if(n<1 || n>64)return false;
                auto components=ptr(target+0x3B0);if(!components)return false;
                for(int j=0;j<n;++j){auto c=ptr(components+j*8);
                    if(!method(c,0x308,b,0x1D5DBE0) || !method(c,0x3A8,b,0x1DACA60))return false;
                }
            }else if(ptr(target)!=b+(i==2?0x32C3210:0x326A6C8))return false;
        }
        return true;
    }
    static bool preflight(uintptr_t b){
        if(!SaveProbe::preflight(b) || !callbacks(b))return false;
        for(unsigned p=0;p<2;++p){
            auto player=ptr(b+0x470DE90+p*8);
            for(unsigned off:{0x400u,0x420u,0x440u}){
                auto node=ptr(player+0x44078+off);unsigned n=0;
                while(node){
                    if(++n>64)return false;
                    bool ok=(method(node,0x30,b,0x30EA70) && method(node,0x38,b,0x30EAE0)) ||
                            (method(node,0x30,b,0x30EB50) && method(node,0x38,b,0x30EBA0));
                    if(!ok)return false;node=ptr(node+0x18);
                }
            }
        }
        auto manager=ptr(b+0x470DF28);
        if(!method(manager,0,b,0x31F700))return false;
        if(ptr(manager+8)){
            auto table=ptr(manager+0x90);
            constexpr unsigned pairs[][4]={{0x318860,0x3188A0,0x318640,0x2D2BC0},{0x33FBC0,0x33FF40,0x31A2B0,0x2D2BC0},
                {0x31C190,0x31C1E0,0x31BC50,0x2D2BC0},{0x340ED0,0x340F70,0x3402C0,0x340C50},
                {0x317BD0,0x317C70,0x3179E0,0x317BC0},{0x341110,0x3411A0,0x341010,0x340C50}};
            for(unsigned i=0;i<16;++i){auto object=ptr(table+0x270+i*8);if(!object)continue;
                if(object!=ptr(manager+0x10+i*8) || !method(object,0xC8,b,0x33E640))return false;
                bool ok=false;
                for(auto& x:pairs)if(method(object,0x100,b,x[0]) && method(object,0x108,b,x[1]) && method(object,0xD0,b,x[2]) && method(object,0xD8,b,x[3]))ok=true;
                if(!ok)return false;
            }
        }
        auto aux=ptr(manager+0xA0);
        if(aux)for(unsigned i=0;i<17;++i){auto object=ptr(aux+0x10+i*8);if(!object)continue;
            if(!((method(object,0x20,b,0x2D2BB0) && method(object,0x28,b,0x2D2BB0)) ||
                 (method(object,0x20,b,0x32AC00) && method(object,0x28,b,0x32ACE0))))return false;
        }
        return true;
    }
    bool combat_requested()const{return throw_requested || charge_requested || attack_requested || edge_requested || reversal_requested;}
    static bool charge_aux_active(uintptr_t b)noexcept{
        __try {
            auto manager=ptr(b+0x470DF28);if(manager<0x10000)return false;
            auto owner=ptr(manager+0xA0);if(owner<0x10000)return false;
            auto object=ptr(owner+0x30); // observed auxiliary slot 4
            if(!method(object,0x38,b,0x31F4E0) || !method(object,0x28,b,0x32ACE0))return false;
            return ptr(object+0x3B8)==ptr(b+0x470DE90) && ptr(object+0x3B8)>=0x10000 &&
                ptr(object+0x3C8)>=0x10000 && ptr(object+0x38)>=0x10000 &&
                *reinterpret_cast<unsigned*>(object+0xA4)==3 &&
                *reinterpret_cast<unsigned*>(object+0x3D0)==0;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool charge_values(unsigned p1,unsigned p2,float multiplier,int selector){
        return p1==219 && p2==65535 && multiplier==0.0f && selector==0;
    }
    static bool attack_values(unsigned p1,unsigned p2,float multiplier,int selector){
        return p1==679 && p2==65535 && multiplier==0.0f && selector==0;
    }
    static bool edge_values(unsigned p1,unsigned p2,float multiplier,int selector){
        return p1==656 && p2==65535 && multiplier==0.0f && selector==0;
    }
    static bool reversal_values(unsigned p1,unsigned p2,float multiplier,int selector){
        // Captured Yoshimitsu clash slowdown, exactly 819/16384.
        return p1==187 && p2==193 && multiplier==819.0f/16384.0f && selector==-1;
    }
    bool trigger_values(unsigned p1,unsigned p2,float multiplier,int selector)const{
        if(unsigned(throw_requested)+unsigned(charge_requested)+unsigned(attack_requested)+unsigned(edge_requested)+unsigned(reversal_requested)!=1)return false;
        if(reversal_requested)return reversal_values(p1,p2,multiplier,selector);
        if(edge_requested)return edge_values(p1,p2,multiplier,selector);
        if(attack_requested)return attack_values(p1,p2,multiplier,selector);
        return charge_requested?charge_values(p1,p2,multiplier,selector):throw_values(p1,p2,multiplier,selector);
    }
    static bool throw_values(unsigned p1,unsigned p2,float multiplier,int selector){
        // Observed connected Yoshimitsu A+G freeze; not a generic combat detector.
        return p1==487 && p2==10041 && multiplier==0.0f && selector==-1;
    }
    bool combat_state(uintptr_t b)const{
        return trigger_values(*reinterpret_cast<std::uint16_t*>(b+0x475A04A),
            *reinterpret_cast<std::uint16_t*>(b+0x47F143A),*reinterpret_cast<float*>(b+0x48462F4),
            *reinterpret_cast<int*>(b+0x4846458)) && (!charge_requested || charge_aux_active(b));
    }
    bool a_restore_requested{};
    bool direction_restore_requested{};unsigned direction_bits{0x400},direction_slot{1};
    static bool a_state(uintptr_t b){
        return *reinterpret_cast<std::uint16_t*>(b+0x4717840)==1 && *reinterpret_cast<std::uint16_t*>(b+0x47AEC30)==0 &&
            *reinterpret_cast<std::uint16_t*>(b+0x475A04A)==264 && *reinterpret_cast<std::uint16_t*>(b+0x47F143A)==65535 &&
            *reinterpret_cast<float*>(b+0x48462F4)==1.0f && *reinterpret_cast<unsigned char*>(b+0x48462D0)==0;
    }
    static bool a_blend_pair(const std::byte* a,const std::byte* b,unsigned side){
        if(side)return !std::memcmp(a,b,1372);
        unsigned x{},y{};std::memcpy(&x,a+156,4);std::memcpy(&y,b+156,4);
        return x==12 && y==14 && !std::memcmp(a,b,156) && !std::memcmp(a+160,b+160,1212);
    }
    static bool direction_state(uintptr_t b,unsigned bits=0x400,unsigned slot=1){
        if((bits!=0x400 && bits!=0x800) || slot<1 || slot>2)return false;
        return *reinterpret_cast<std::uint16_t*>(b+0x4717840)==(slot==1?bits:0) && *reinterpret_cast<std::uint16_t*>(b+0x47AEC30)==(slot==2?bits:0) &&
            *reinterpret_cast<std::uint16_t*>(b+0x475A04A)==65535 && *reinterpret_cast<std::uint16_t*>(b+0x47F143A)==65535 &&
            *reinterpret_cast<float*>(b+0x48462F4)==1.0f && *reinterpret_cast<unsigned char*>(b+0x48462D0)==0;
    }
    static bool idle(uintptr_t b){
        return *reinterpret_cast<std::uint16_t*>(b+0x4717840)==0 && *reinterpret_cast<std::uint16_t*>(b+0x47AEC30)==0 &&
            *reinterpret_cast<std::uint16_t*>(b+0x475A04A)==65535 && *reinterpret_cast<std::uint16_t*>(b+0x47F143A)==65535 &&
            *reinterpret_cast<float*>(b+0x48462F4)==1.0f && *reinterpret_cast<unsigned char*>(b+0x48462D0)==0;
    }
    static void keys(uintptr_t b,std::uint32_t* k,uintptr_t* p){
        for(unsigned i=0;i<std::size(key_rvas);++i)k[i]=*reinterpret_cast<std::uint32_t*>(b+key_rvas[i]);
        p[0]=ptr(b+0x470DE90);p[1]=ptr(b+0x470DE98);
    }
    // Two adjacent idle saves only; never invokes a reader or preservation writes.
    std::array<RingSnapshot,4> rings{};
    bool ring_capture_enabled{},ring_save_unchanged{},ring_rotation_ok{};
    bool throw_audit{},delay_audit{},audit_started{},audit_done{},audit_stable{};
    unsigned audit_source_changes{},audit_shape_changes{};
    void audit_step(uintptr_t b,Routine writer=nullptr,Preflight check=nullptr) noexcept {
        if(audit_done)return;
        std::uint64_t bookkeeping{};bool captured=false;
        __try {
            if(!check)check=preflight;
            if(!check(b) || !(throw_audit?(throw_requested && combat_state(b)):idle(b))){fault=true;audit_done=true;return;}
            if(!writer)writer=reinterpret_cast<Routine>(b+0x3841E0);
            bookkeeping=ptr(b+0x470E870);captured=true;
            if(!audit_started){
                attempted=true;preflight_ok=true;keys(b,before_keys.data(),before_players);
                measure(b,0);
                if(ring_capture_enabled && !rings[0].capture(b)){fault=true;audit_done=true;return;}
                writer(&saved.stream);measure(b,1);save_valid=saved.seal();
                if(ring_capture_enabled){rings[1].capture(b);ring_save_unchanged=rings[0].same(rings[1]);}
                audit_started=true;
                fault=(ring_capture_enabled && !ring_save_unchanged) || !save_valid || measurements[0].rng!=measurements[1].rng || measurements[0].fields!=measurements[1].fields;
                if(fault)audit_done=true;
            }else{
                keys(b,after_keys.data(),after_players);
                if(after_keys[0]!=before_keys[0]+1u || std::memcmp(before_players,after_players,sizeof(before_players))){fault=true;audit_done=true;}
                else{
                    measure(b,2);
                    if(ring_capture_enabled && !rings[2].capture(b)){fault=true;audit_done=true;return;}
                    writer(&after_save.stream);measure(b,3);post_save_valid=after_save.seal();
                    if(ring_capture_enabled){rings[3].capture(b);ring_save_unchanged=ring_save_unchanged && rings[2].same(rings[3]);ring_rotation_ok=rings[2].rotated_from(rings[0]);}
                    fault=(ring_capture_enabled && !ring_save_unchanged) || !post_save_valid || measurements[2].rng!=measurements[3].rng || measurements[2].fields!=measurements[3].fields;
                    audit_shape_changes=saved.writes>after_save.writes?saved.writes-after_save.writes:after_save.writes-saved.writes;
                    for(unsigned i=0;i<saved.writes && i<after_save.writes;++i){
                        if(saved.lengths[i]!=after_save.lengths[i] || saved.traces[i].writer!=after_save.traces[i].writer)++audit_shape_changes;
                        if(saved.traces[i].source!=after_save.traces[i].source)++audit_source_changes;
                    }
                    audit_stable=!fault && !audit_shape_changes && !audit_source_changes;
                    audit_done=true;
                }
            }
        }__except(EXCEPTION_EXECUTE_HANDLER){exception=GetExceptionCode();fault=true;audit_done=true;}
        if(captured){__try{*reinterpret_cast<std::uint64_t*>(b+0x470E870)=bookkeeping;}
            __except(EXCEPTION_EXECUTE_HANDLER){exception=GetExceptionCode();fault=true;audit_done=true;}}
    }
    std::array<std::uint32_t,std::size(key_rvas)> delayed_loaded_keys{};
    uintptr_t delayed_loaded_players[2]{};
    bool delayed_keys_captured{};
    bool delayed_requested{},delayed_loaded{},replay_done{},replay_ring_equal{},replay_keys_equal{};
    bool delayed_guard_ok{},delayed_inputs_equal{};
    bool recorded_input_replay{};
    bool combat_later_window{},combat_trigger_seen{};
    unsigned combat_first_trigger_outer{};
    bool accept_replay_trigger(bool matches,unsigned outer){
        if(!matches){combat_trigger_seen=false;return false;}
        if(!combat_later_window)return true;
        if(combat_trigger_seen && outer==combat_first_trigger_outer+1u)return true;
        combat_trigger_seen=true;combat_first_trigger_outer=outer;return false;
    }
    bool combat_replay_requested{},combat_postload_ready{},combat_groups_restored{},combat_root_wind_restored{},combat_comparison_attempted{};
    bool combat_preflight_attempted{};
    unsigned combat_preflight_pass_mask{};
    static constexpr unsigned combat_preflight_required_mask=255;
    // Read-only rehearsal. Post-load acceptance still requires live validation.
    void combat_replay_preflight(uintptr_t b) noexcept {
        if(combat_preflight_attempted)return;
        combat_preflight_attempted=true;
        if(!throw_audit || !audit_done || fault || restore_invoked)return;
        __try {
            if(delayed_sources_ok())combat_preflight_pass_mask|=1;
            if(input_checkpoints[0].valid && input_checkpoints[1].valid &&
               input_checkpoints[0].bytes==input_checkpoints[1].bytes)combat_preflight_pass_mask|=2;
            if(blends[0].valid && blends[1].valid &&
               !std::memcmp(blends[0].owners,blends[1].owners,sizeof(blends[0].owners)) &&
               (charge_requested?(measurements[0].valid && CombatBlend::charge_adjacent(blends[0].bytes[0],blends[1].bytes[0],measurements[0].fields[0].data()+128)):
                CombatBlend::adjacent(blends[0].bytes[0],blends[1].bytes[0],throw_requested)) &&
               CombatBlend::adjacent(blends[0].bytes[1],blends[1].bytes[1]))combat_preflight_pass_mask|=4;
            if(transforms[0].combat_groups_ready(transforms[1],transforms[1]))combat_preflight_pass_mask|=8;
            if(transforms[0].restore_wind(transforms[1],transforms[1],b,true))combat_preflight_pass_mask|=16;
            if(transforms[0].restore_sway(transforms[1],transforms[1],b,true))combat_preflight_pass_mask|=32;
            if(transforms[0].restore_constraints(transforms[1],transforms[1],b,true))combat_preflight_pass_mask|=64;
            if(cameras[0].adjacent_combat_ready(cameras[1],b,charge_requested))combat_preflight_pass_mask|=128;
        }__except(EXCEPTION_EXECUTE_HANDLER){return;}
    }
    FullRestoreStream replay,restored;
    RingSnapshot restored_rings,restored_after_rings;
    struct BlendCheckpoint {
        uintptr_t owners[2]{};
        std::array<std::array<std::byte,1380>,2> bytes{};
        bool valid{};
    };
    std::array<BlendCheckpoint,10> blends{};
    std::array<std::array<std::array<unsigned,2>,2>,10> blend_branch_inputs{};
    std::array<TransformSnapshot,10> transforms{};
    static constexpr const char* blend_names[]={"original_a","original_b","restored","restored_after_save","replay","weights_corrected","blocks_corrected","wind_corrected","sway_corrected","constraints_corrected"};
    bool wind_restore_verified{},sway_restore_verified{},constraint_restore_verified{};
    TransformSnapshot::WindDiagnostic wind_diagnostic;
    CameraSnapshot::RestoreDiagnostic camera_diagnostic;
    bool blend_blocks_applied{},blend_blocks_verified{};
    bool preserve_blend_blocks(uintptr_t b) noexcept {
        __try {
            if(!blend_weights_verified || !blends[0].valid || !blends[1].valid || !blends[5].valid)return false;
            for(unsigned side=0;side<2;++side){
                auto owner=blends[0].owners[side];auto player=ptr(b+0x470DE90+side*8);
                if(player<0x10000 || ptr(player+0x971F8)!=owner || owner!=blends[1].owners[side] || owner!=blends[5].owners[side])return false;
                auto source=blends[0].bytes[side].data()+140;
                auto loaded=blends[5].bytes[side].data()+140;
                if((a_restore_requested?!a_blend_pair(blends[0].bytes[side].data(),blends[1].bytes[side].data(),side):std::memcmp(source,blends[1].bytes[side].data()+140,1232)!=0) ||
                   std::memcmp(loaded,reinterpret_cast<void*>(owner+0x1DD8),1232))return false;
                for(unsigned j=0;j<7;++j){
                    auto s=source+j*176;auto l=loaded+j*176;
                    for(unsigned k=0;k<176;++k)
                        if(!(k>=16 && k<20) && !(k>=24 && k<32) && s[k]!=l[k])return false;
                    uintptr_t table{};std::uint32_t index{},new_index{};std::uint64_t mask{},new_mask{};
                    std::memcpy(&table,s+8,8);std::memcpy(&index,s+16,4);std::memcpy(&new_index,l+16,4);
                    std::memcpy(&mask,s+24,8);std::memcpy(&new_mask,l+24,8);
                    // Loader2FA080 reconstructs block2 selection. Narrow observed A+G case only.
                    const bool throw_selection=combat_load_diagnostic && throw_requested && !delayed_requested &&
                        side==1 && j==2 && index==12 && new_index==3 && mask==0 && new_mask==0;
                    if((new_index!=0 && new_index!=index && !(a_restore_requested && side==0 && j==0 && index==12 && new_index==14) && !throw_selection) || (new_mask!=0 && new_mask!=mask) || (mask>>35))return false;
                    if(table){
                        if(table<0x10000)return false;
                        auto count=*reinterpret_cast<const std::int32_t*>(table);
                        if(count<=0 || index>=static_cast<unsigned>(count) || new_index>=static_cast<unsigned>(count))return false;
                    }else if(index || mask)return false;
                }
            }
            blend_blocks_applied=true;
            for(unsigned side=0;side<2;++side)for(unsigned j=0;j<7;++j){
                auto target=blends[0].owners[side]+0x1DD8+j*176;
                auto source=blends[0].bytes[side].data()+140+j*176;
                std::memcpy(reinterpret_cast<void*>(target+16),source+16,4);
                std::memcpy(reinterpret_cast<void*>(target+24),source+24,8);
            }
            if(!capture_blend(b,6))return false;
            blend_blocks_verified=true;
            for(unsigned side=0;side<2;++side)
                blend_blocks_verified=blend_blocks_verified && !std::memcmp(blends[0].bytes[side].data(),blends[6].bytes[side].data(),1372) &&
                    !std::memcmp(blends[5].bytes[side].data()+1372,blends[6].bytes[side].data()+1372,8);
            return blend_blocks_verified;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool blend_weights_applied{},blend_weights_verified{};
    bool preserve_blend_weights(uintptr_t b) noexcept {
        __try {
            if(!blends[0].valid || !blends[1].valid || !blends[2].valid)return false;
            // Validate both owners and every weight before the first write.
            for(unsigned side=0;side<2;++side){
                auto owner=blends[0].owners[side];
                auto player=ptr(b+0x470DE90+side*8);
                if(player<0x10000 || ptr(player+0x971F8)!=owner || owner!=blends[1].owners[side] || owner!=blends[2].owners[side])return false;
                if(std::memcmp(blends[0].bytes[side].data(),blends[1].bytes[side].data(),140) ||
                   std::memcmp(reinterpret_cast<void*>(owner+0x4BC),blends[2].bytes[side].data(),140))return false;
                for(unsigned i=0;i<35;++i){
                    std::uint32_t source{},loaded{};
                    std::memcpy(&source,blends[0].bytes[side].data()+i*4,4);
                    std::memcpy(&loaded,blends[2].bytes[side].data()+i*4,4);
                    if(loaded || (source!=0 && source!=(i<24?0x3F59999Au:0x3F400000u)))return false;
                }
            }
            blend_weights_applied=true;
            for(unsigned side=0;side<2;++side)
                std::memcpy(reinterpret_cast<void*>(blends[0].owners[side]+0x4BC),blends[0].bytes[side].data(),140);
            if(!capture_blend(b,5))return false;
            blend_weights_verified=true;
            for(unsigned side=0;side<2;++side)
                blend_weights_verified=blend_weights_verified && !std::memcmp(blends[0].bytes[side].data(),blends[5].bytes[side].data(),140) &&
                    !std::memcmp(blends[2].bytes[side].data()+140,blends[5].bytes[side].data()+140,1240);
            return blend_weights_verified;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool capture_blend(uintptr_t b,unsigned checkpoint) noexcept {
        auto& out=blends[checkpoint];out.valid=false;
        __try {
            for(unsigned side=0;side<2;++side){
                auto player=ptr(b+0x470DE90+side*8);
                if(player<0x10000)return false;
                auto owner=ptr(player+0x971F8);
                if(owner<0x10000 || owner>UINTPTR_MAX-0x22B0)return false;
                if(checkpoint && owner!=blends[0].owners[side])return false;
                out.owners[side]=owner;
                blend_branch_inputs[checkpoint][side]={*reinterpret_cast<unsigned*>(player+0x78),*reinterpret_cast<unsigned*>(player+0x210)};
                std::memcpy(out.bytes[side].data(),reinterpret_cast<void*>(owner+0x4BC),140);
                std::memcpy(out.bytes[side].data()+140,reinterpret_cast<void*>(owner+0x1DD8),1232);
                std::memcpy(out.bytes[side].data()+1372,reinterpret_cast<void*>(owner+0x22A8),8);
                if(ptr(player+0x971F8)!=owner || ptr(b+0x470DE90+side*8)!=player)return false;
            }
            if(!transforms[checkpoint].capture(out.owners,b))return false;
            if(checkpoint && !transforms[checkpoint].same_topology(transforms[0]))
                return transforms[checkpoint].reject(9,out.owners[0]);
            out.valid=true;return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool restored_valid{},restored_save_unchanged{},restored_ring_equal{};
    std::uint64_t restored_byte_differences{};
    RingSnapshot delayed_scratch,replay_rings;
    Measurement original_next;
    std::array<std::byte,40> delayed_inputs{};
    struct InputCheckpoint {
        std::array<std::byte,40> bytes{};
        std::array<uintptr_t,3> sources{};
        std::uint64_t changed_mask{};
        bool attempted{},valid{};
    };
    std::array<InputCheckpoint,3> input_checkpoints{};
    unsigned input_check_phase{};
    std::uint64_t replay_byte_differences{};
    static bool input_bytes(uintptr_t argument,std::array<std::byte,40>& out,std::array<uintptr_t,3>* sources=nullptr) noexcept {
        __try {
            auto p0=ptr(argument),p1=ptr(argument+8),timing=ptr(argument+16);
            if(sources)*sources={p0,p1,timing};
            if(p0<0x10000 || p1<0x10000 || timing<0x10000)return false;
            std::memcpy(out.data(),reinterpret_cast<void*>(p0),8);
            std::memcpy(out.data()+8,reinterpret_cast<void*>(p1),8);
            std::memcpy(out.data()+16,reinterpret_cast<void*>(timing),24);
            return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool capture_audit_inputs(uintptr_t argument,unsigned checkpoint) noexcept {
        if(checkpoint>1)return false;
        input_check_phase=checkpoint;
        auto& out=input_checkpoints[checkpoint];out={};out.attempted=true;
        out.valid=input_bytes(argument,out.bytes,&out.sources);
        if(!out.valid)return false;
        if(checkpoint==0)delayed_inputs=out.bytes;
        else if(!input_checkpoints[0].valid){out.valid=false;return false;}
        for(unsigned i=0;i<out.bytes.size();++i)
            if(out.bytes[i]!=delayed_inputs[i])out.changed_mask|=std::uint64_t{1}<<i;
        // Observation only: changing inputs do not authorize or trigger restoration.
        return true;
    }
    bool delayed_sources_ok()const{
        if(audit_shape_changes || !ring_save_unchanged || !ring_rotation_ok)return false;
        for(unsigned i=0;i<saved.writes;++i){
            if(saved.traces[i].source==after_save.traces[i].source)continue;
            bool ring=false;
            for(unsigned j=0;j<4;++j){
                auto expected= j%2?0x800u:0x1840u;
                if(saved.lengths[i]==expected && saved.traces[i].source==rings[0].headers[j].current &&
                   after_save.traces[i].source==rings[2].headers[j].current)ring=true;
            }
            if(!ring)return false;
        }
        return true;
    }
    bool outer_correction_applied{},outer_correction_verified{};
    bool correct_delayed_outer(uintptr_t b){
        if(!delayed_keys_captured || !restore_completed || !postflight_ok ||
           std::memcmp(delayed_loaded_players,before_players,sizeof(before_players)))return false;
        for(unsigned i=1;i<before_keys.size();++i)if(delayed_loaded_keys[i]!=before_keys[i])return false;
        if(delayed_loaded_keys[0]!=before_keys[0]){
            if(after_keys[0]!=before_keys[0]+1u || delayed_loaded_keys[0]!=after_keys[0] ||
               *reinterpret_cast<std::uint32_t*>(b+key_rvas[0])!=delayed_loaded_keys[0])return false;
            *reinterpret_cast<std::uint32_t*>(b+key_rvas[0])=before_keys[0];
            outer_correction_applied=true;
        }
        std::array<std::uint32_t,std::size(key_rvas)> corrected{};uintptr_t players[2]{};
        keys(b,corrected.data(),players);
        outer_correction_verified=corrected==before_keys && !std::memcmp(players,before_players,sizeof(players));
        return outer_correction_verified;
    }
    unsigned delayed_failure_line{};
    void delayed_step(uintptr_t b,uintptr_t argument,Routine writer=nullptr,Routine reader=nullptr,Preflight check=nullptr) noexcept {
        if(fault || replay_done)return;
        if(!writer)writer=reinterpret_cast<Routine>(b+0x3841E0);
        if(!reader)reader=reinterpret_cast<Routine>(b+0x384540);
        if(!check)check=preflight;
        std::uint64_t bookkeeping{};bool captured=false;
        __try {
            std::array<std::byte,40> inputs{};
            input_check_phase=delayed_loaded?2u:audit_started?1u:0u;
            auto& input_checkpoint=input_checkpoints[input_check_phase];
            input_checkpoint.attempted=true;
            input_checkpoint.valid=input_bytes(argument,inputs,&input_checkpoint.sources);
            if(!input_checkpoint.valid){delayed_failure_line=__LINE__;fault=true;__leave;}
            input_checkpoint.bytes=inputs;
            if(!audit_started)delayed_inputs=inputs;
            for(unsigned i=0;i<inputs.size();++i)
                if(inputs[i]!=delayed_inputs[i])input_checkpoint.changed_mask|=std::uint64_t{1}<<i;
            delayed_inputs_equal=inputs==delayed_inputs;
            // At phase2 the replayed update is already complete; this envelope
            // belongs to the upcoming update. A/B equality remains mandatory.
            const bool require_input_equality=!combat_replay_requested || !delayed_loaded;
            if((require_input_equality && !delayed_inputs_equal) || !check(b) || !(combat_replay_requested?combat_state(b):(a_restore_requested && !delayed_loaded?a_state(b):(direction_restore_requested && !delayed_loaded?direction_state(b,direction_bits,direction_slot):idle(b))))){delayed_failure_line=__LINE__;fault=true;__leave;}
            if(!audit_done){
                if(!capture_blend(b,audit_started?1u:0u)){delayed_failure_line=__LINE__;fault=true;__leave;}
                audit_step(b,writer,check);
                if(!fault){if(audit_done)cameras[1].capture(after_save,b);else if(save_valid)cameras[0].capture(saved,b);}
                if(fault || !audit_done)__leave;
            }
            bookkeeping=ptr(b+0x470E870);captured=true;
            if(!delayed_loaded){
                original_next=measurements[2];
                delayed_guard_ok=delayed_sources_ok() && original_next.rng==measurements[0].rng;
                for(unsigned side=0;side<2;++side)
                    delayed_guard_ok=delayed_guard_ok && !std::memcmp(original_next.fields[side].data()+128,measurements[0].fields[side].data()+128,16);
                if(combat_replay_requested){
                    combat_replay_preflight(b);
                    delayed_guard_ok=delayed_guard_ok &&
                        (combat_preflight_pass_mask | (recorded_input_replay && delayed_inputs_equal?2u:0u))==combat_preflight_required_mask;
                }
                if(!delayed_guard_ok){delayed_failure_line=__LINE__;fault=true;__leave;}
                // Restore physical slot selection before native fixups resolve aliases.
                if(!rings[0].restore(b,delayed_scratch)){delayed_failure_line=__LINE__;fault=true;__leave;}
                restore_invoked=true;reader(&saved.stream);restore_completed=saved.consumed();
                measure(b,2);
                keys(b,delayed_loaded_keys.data(),delayed_loaded_players);delayed_keys_captured=true;
                postflight_ok=check(b);
                selected_equal=correct_delayed_outer(b);
                if(!restore_completed || !selected_equal || !postflight_ok || !preserve(b) || !rings[0].restore(b,delayed_scratch)){delayed_failure_line=__LINE__;fault=true;}
                else {
                    delayed_loaded=true;measure(b,5);
                    if(!capture_blend(b,2)){delayed_failure_line=__LINE__;fault=true;__leave;}
                    if(combat_replay_requested){
                        combat_postload_ready=CombatBlend::ready(blends[0].bytes,blends[1].bytes,blends[2].bytes,blends[0].owners,throw_requested) &&
                            transforms[0].combat_groups_ready(transforms[1],transforms[2]) &&
                            transforms[0].restore_root_wind(transforms[1],transforms[2],true) &&
                            transforms[0].restore_wind(transforms[1],transforms[2],b,true) &&
                            transforms[0].restore_sway(transforms[1],transforms[2],b,true) &&
                            transforms[0].restore_constraints(transforms[1],transforms[2],b,true) &&
                            cameras[2].capture(saved,b) && cameras[0].restore_smoothing(cameras[1],cameras[2],b,false,true,true,charge_requested);
                        if(!combat_postload_ready){delayed_failure_line=__LINE__;fault=true;__leave;}
                        if(!CombatBlend::restore(blends[0].bytes,blends[1].bytes,blends[2].bytes,blends[0].owners,throw_requested)){delayed_failure_line=__LINE__;fault=true;__leave;}
                        blend_weights_applied=blend_weights_verified=blend_blocks_applied=blend_blocks_verified=true;
                        combat_groups_restored=transforms[0].restore_combat_groups(transforms[1],transforms[2]);
                        combat_root_wind_restored=combat_groups_restored && transforms[0].restore_root_wind(transforms[1],transforms[2]);
                        if(!combat_root_wind_restored || !capture_blend(b,6)){delayed_failure_line=__LINE__;fault=true;__leave;}
                    }else{
                        if(!preserve_blend_weights(b)){delayed_failure_line=__LINE__;fault=true;__leave;}
                        if(!preserve_blend_blocks(b)){delayed_failure_line=__LINE__;fault=true;__leave;}
                    }
                    wind_restore_verified=transforms[0].restore_wind(transforms[1],transforms[6],b,false,&wind_diagnostic);
                    if(!wind_restore_verified || !capture_blend(b,7)){delayed_failure_line=__LINE__;fault=true;__leave;}
                    sway_restore_verified=transforms[0].restore_sway(transforms[1],transforms[7],b);
                    if(!sway_restore_verified || !capture_blend(b,8)){delayed_failure_line=__LINE__;fault=true;__leave;}
                    constraint_restore_verified=transforms[0].restore_constraints(transforms[1],transforms[8],b);
                    if(!constraint_restore_verified || !capture_blend(b,9)){delayed_failure_line=__LINE__;fault=true;__leave;}
                    if(!restored_rings.capture(b)){delayed_failure_line=__LINE__;fault=true;__leave;}
                    restored_ring_equal=restored_rings.same(rings[0]);
                    writer(&restored.stream);measure(b,6);restored_valid=restored.seal();
                    if(restored_valid)cameras[2].capture(restored,b);
                    if(!capture_blend(b,3)){delayed_failure_line=__LINE__;fault=true;__leave;}
                    restored_after_rings.capture(b);
                    restored_save_unchanged=measurements[5].rng==measurements[6].rng && measurements[5].fields==measurements[6].fields &&
                        !std::memcmp(measurements[5].players,measurements[6].players,sizeof(measurements[5].players)) && restored_rings.same(restored_after_rings);
                    restored_save_unchanged=restored_save_unchanged && blends[6].bytes==blends[3].bytes;
                    if(!restored_valid || !restored_save_unchanged){delayed_failure_line=__LINE__;fault=true;__leave;}
                    if(camera_restore_requested){
                        camera_restore_verified=cameras[0].restore_smoothing(cameras[1],cameras[2],b,false,combat_replay_requested || direction_restore_requested || a_restore_requested,false,charge_requested || direction_restore_requested || a_restore_requested,&camera_diagnostic);
                        if(!camera_restore_verified || !cameras[4].capture(restored,b)){delayed_failure_line=__LINE__;fault=true;__leave;}
                    }
                    if(combat_replay_requested && !throw_camera_flag.restore(saved,restored,b,attack_requested,edge_requested,reversal_requested)){delayed_failure_line=__LINE__;fault=true;__leave;}
                    auto n=restored.stream.write_position,m=saved.stream.write_position;
                    restored_byte_differences=n>m?n-m:m-n;
                    for(std::uint64_t i=0;i<(n<m?n:m);++i)if(restored.stream.payload[i]!=saved.stream.payload[i])++restored_byte_differences;
                }
            }else{
                combat_comparison_attempted=combat_replay_requested;
                if(!capture_blend(b,4)){delayed_failure_line=__LINE__;fault=true;__leave;}
                std::array<std::uint32_t,std::size(key_rvas)> replay_keys{};uintptr_t replay_players[2]{};
                keys(b,replay_keys.data(),replay_players);
                replay_keys_equal=replay_keys==after_keys && !std::memcmp(replay_players,after_players,sizeof(after_players));
                if(!replay_rings.capture(b)){delayed_failure_line=__LINE__;fault=true;__leave;}
                replay_ring_equal=replay_rings.same(rings[2]);
                writer(&replay.stream);
                if(!replay.seal()){delayed_failure_line=__LINE__;fault=true;__leave;}
                cameras[3].capture(replay,b);
                auto n=replay.stream.write_position,m=after_save.stream.write_position;
                replay_byte_differences=n>m?n-m:m-n;
                for(std::uint64_t i=0;i<(n<m?n:m);++i)if(replay.stream.payload[i]!=after_save.stream.payload[i])++replay_byte_differences;
                replay_done=true;
                // Differences are reported as an experiment result, never silently normalized.
            }
        }__except(EXCEPTION_EXECUTE_HANDLER){exception=GetExceptionCode();delayed_failure_line=__LINE__;fault=true;}
        if(captured){__try{*reinterpret_cast<std::uint64_t*>(b+0x470E870)=bookkeeping;}
            __except(EXCEPTION_EXECUTE_HANDLER){exception=GetExceptionCode();delayed_failure_line=__LINE__;fault=true;}}
    }
    bool combat_next_attempted{},combat_next_valid{};
    void capture_combat_next(uintptr_t b,Routine writer=nullptr,Preflight check=nullptr) noexcept {
        if(combat_next_attempted || !combat_load_diagnostic || !attempted || fault)return;
        combat_next_attempted=true;
        std::uint64_t bookkeeping{};bool captured=false;
        __try {
            if(!check)check=preflight;
            if(!check(b)){fault=true;__leave;}
            std::array<std::uint32_t,std::size(key_rvas)> next_keys{};uintptr_t players[2]{};
            keys(b,next_keys.data(),players);
            if(next_keys[0]!=before_keys[0]+1u || std::memcmp(players,before_players,sizeof(players))){fault=true;__leave;}
            if(!capture_blend(b,4) || !replay_rings.capture(b)){fault=true;__leave;}
            bookkeeping=ptr(b+0x470E870);captured=true;
            measure(b,5);
            if(!writer)writer=reinterpret_cast<Routine>(b+0x3841E0);
            writer(&replay.stream);measure(b,6);
            if(!replay.seal() || !delayed_scratch.capture(b) || !replay_rings.same(delayed_scratch) ||
               measurements[5].fields!=measurements[6].fields || measurements[5].rng!=measurements[6].rng ||
               !cameras[5].capture(replay,b)){fault=true;__leave;}
            combat_next_valid=true;
        }__except(EXCEPTION_EXECUTE_HANDLER){exception=GetExceptionCode();fault=true;}
        if(captured){__try{*reinterpret_cast<std::uint64_t*>(b+0x470E870)=bookkeeping;}
            __except(EXCEPTION_EXECUTE_HANDLER){exception=GetExceptionCode();fault=true;combat_next_valid=false;}}
    }
    ThrowCameraFlag throw_camera_flag;
    bool combat_flag_requested{},combat_load_diagnostic{};
    void run(uintptr_t b,Routine writer=nullptr,Routine reader=nullptr,Preflight check=nullptr) noexcept {
        if(attempted)return;attempted=true;
        LARGE_INTEGER begin{},end{};QueryPerformanceCounter(&begin);
        std::uint64_t bookkeeping{};bool captured=false;
        __try {
            phase=1;
            if(!check)check=preflight;
            preflight_ok=check(b) && (combat_requested()?(preserve_requested && combat_state(b)):idle(b));
            if(preflight_ok){
                keys(b,before_keys.data(),before_players);
                bookkeeping=ptr(b+0x470E870);captured=true;
                if(!writer)writer=reinterpret_cast<Routine>(b+0x3841E0);
                if(!reader)reader=reinterpret_cast<Routine>(b+0x384540);
                if(combat_load_diagnostic && (!throw_requested || !capture_blend(b,0) || !rings[0].capture(b))){fault=true;__leave;}
                phase=2;measure(b,0);writer(&saved.stream);measure(b,1);
                *reinterpret_cast<std::uint64_t*>(b+0x470E870)=bookkeeping;
                save_valid=saved.seal();
                if(combat_load_diagnostic && (!save_valid || !capture_blend(b,1) || !rings[1].capture(b) ||
                   !rings[0].same(rings[1]) || blends[0].bytes!=blends[1].bytes || !cameras[0].capture(saved,b) || !cameras[1].capture(saved,b))){fault=true;__leave;}
                if(save_valid && check(b)){
                    phase=3;restore_invoked=true;reader(&saved.stream);measure(b,2);restore_completed=saved.consumed();
                    if(combat_load_diagnostic){
                        // Raw post-load diagnostics precede the existing scalar preservation.
                        capture_blend(b,2);rings[2].capture(b);cameras[2].capture(saved,b);
                    }
                    phase=4;keys(b,after_keys.data(),after_players);
                    selected_equal=before_keys==after_keys && !std::memcmp(before_players,after_players,sizeof(before_players));
                    postflight_ok=check(b);
                    bool preservation_ok=!preserve_requested;
                    if(preserve_requested && restore_completed && selected_equal && postflight_ok)preservation_ok=preserve(b);
                    if(restore_completed && selected_equal && postflight_ok && preservation_ok){
                        if(combat_load_diagnostic){
                            camera_restore_requested=true;
                            if(!preserve_blend_weights(b) || !preserve_blend_blocks(b)){fault=true;__leave;}
                            camera_restore_verified=cameras[0].restore_smoothing(cameras[1],cameras[2],b,true);
                            if(!camera_restore_verified || !cameras[4].capture(saved,b)){fault=true;__leave;}
                        }
                        phase=5;writer(&after_save.stream);measure(b,3);post_save_valid=after_save.seal();
                        if(combat_load_diagnostic){capture_blend(b,3);rings[3].capture(b);cameras[3].capture(after_save,b);}
                        if(preserve_requested)preserve_verified=preserve_verified &&
                            measurements[3].rng==measurements[4].rng && measurements[3].fields==measurements[4].fields &&
                            !std::memcmp(measurements[3].players,measurements[4].players,sizeof(measurements[3].players));
                        auto n=saved.stream.write_position,m=after_save.stream.write_position;
                        byte_differences=n>m?n-m:m-n;
                        for(std::uint64_t i=0;i<(n<m?n:m);++i)if(saved.stream.payload[i]!=after_save.stream.payload[i])++byte_differences;
                    }
                }
            }
            fault=!preflight_ok || !save_valid || !restore_completed || !selected_equal || !postflight_ok || !post_save_valid || (preserve_requested && !preserve_verified);
            if(!fault && combat_flag_requested){
                if(!combat_load_diagnostic || !throw_requested || delayed_requested ||
                   !throw_camera_flag.restore(saved,after_save,b))fault=true;
            }
            if(!fault)phase=6;
        } __except(EXCEPTION_EXECUTE_HANDLER){exception=GetExceptionCode();fault=true;}
        if(captured){__try {*reinterpret_cast<std::uint64_t*>(b+0x470E870)=bookkeeping;}
            __except(EXCEPTION_EXECUTE_HANDLER){exception=GetExceptionCode();fault=true;}}
        QueryPerformanceCounter(&end);elapsed_qpc=end.QuadPart-begin.QuadPart;
    }
};
}
