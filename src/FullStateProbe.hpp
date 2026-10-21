#pragma once
#include "NativeRollbackTiming.hpp"
#include "FullRestoreStream.hpp"
#include "NativeLoadRoute.hpp"
#include "RingSnapshot.hpp"
#include "TransformSnapshot.hpp"
#include "EffectRngBoundary.hpp"
#include "CameraSnapshot.hpp"
#include "CameraSlots.hpp"
#include "ThrowCameraFlag.hpp"
#include "CombatBlend.hpp"
#include "SelectorSnapshot.hpp"
#include "ActiveAnimationGroup.hpp"
#include "ExactBytes.hpp"
#include <memory>
#include <type_traits>
namespace sc6::research {
struct FullStateProbe {
    // Warm once when the pool is constructed, never on its first retirement.
    static const FullStateProbe& reset_defaults();
    // Only callers that have completed ownership drain and reuse guards may call.
    void reset_for_reuse() noexcept;
    bool copy_recorded_from(const FullStateProbe& source) noexcept;
    struct StorageSpan {size_t offset{},capacity{},published{};};
    // Sorted byte ranges whose unused capacity is never input to native loading.
    std::array<StorageSpan,22> storage_spans() const noexcept;
    unsigned restore_span{1}; // Explicit bounded target-to-live distance; fixture default remains adjacent.
    bool external_wind_lifecycle{};
    bool selector_enabled{};
    std::array<SelectorSnapshot,4> selectors; // source, expected, restored, replayed
    SelectorSnapshot::Diagnostic selector_diagnostic;
    bool selector_restored{},selector_endpoint_verified{};
    ActiveAnimationGroup::Diagnostic active_group_diagnostic;
    bool defer_inactive_animation_weights{};
    struct BlendPairDiagnostic {unsigned reason{},side{},offset{UINT32_MAX},changed{},saved{},next{};uintptr_t saved_owner{},next_owner{},table_saved{},table_next{},wrapper_saved{},wrapper_next{},resource_saved{},resource_next{};uint64_t mask_saved{},mask_next{};unsigned index_saved{},index_next{};} blend_pair_diagnostic;
    struct BlendPairEvidence {bool valid{};std::array<std::byte,1372> saved{},next{};} blend_pair_evidence;
    FullRestoreStream saved,after_save;
    std::array<CameraSlots,2> camera_slots{};
    std::array<unsigned,FullRestoreStream::transfer_limit> camera_source_map{};
    unsigned source_index(unsigned i)const{return camera_slots[0].enabled?camera_source_map[i]:i;}
    std::array<CameraSnapshot,6> cameras{};
    bool camera_restore_requested{},camera_restore_verified{};
    struct Measurement {
        std::uint32_t rng{}; uintptr_t players[2]{};
        std::array<std::array<std::byte,224>,2> fields{};
        bool valid{};
    };
    std::array<Measurement,7> measurements{};
    // Timer snapshots retain slot identity; only the validated cursor is restored.
    struct BlendTimerSample {
        std::uint32_t phase{},valid_mask{};
        std::array<std::array<std::array<std::byte,128>,3>,2> slots{};
    };
    std::array<BlendTimerSample,32> blend_timer_history{};
    unsigned blend_timer_count{},blend_timer_dropped{};
    BlendTimerSample blend_timer_expected{};
    BlendTimerSample blend_timer_source{},blend_timer_next{},blend_timer_live{};
    struct TimerDiagnostic {unsigned phase{},reason{},side{},slot{},offset{UINT32_MAX},expected{},actual{};} timer_diagnostic;
    // RVA349840 activates category zero using a bounded descriptor selector,
    // cursor 3 and state 2. RVA3499D0 then advances the cursor. The resource
    // initializer 38B740 snapshots the category-zero count at slot+1C.
    // 3498B8 tests state != 0, not state == 1: active timers can be
    // reselected as well. Restrict this policy to audited states 1 and 2.
    static bool timer_activation(const std::array<std::byte,128>& a,const std::array<std::byte,128>& b,unsigned phase) noexcept {
        const auto word=[](const auto& v,unsigned off){return CombatBlend::word(v.data()+off);};
        const auto header=CombatBlend::wide(a.data()),descriptors=CombatBlend::wide(a.data()+8);
        const auto count=word(a,0x1C),cursor=word(b,0x40),rate=word(a,0x44);
        const auto state=word(a,0x48),prior_cursor=word(a,0x40);
        const bool restartable=(state==1 && prior_cursor==0) ||
            (state==2 && prior_cursor>0 && prior_cursor<0x7F800000u);
        return phase!=2 && restartable && word(b,0x48)==2 &&
            word(a,0x18)==0 && word(a,0x38)==0 && word(b,0x38)==0 &&
            header>=0x10000 && header<=UINTPTR_MAX-0x40 && descriptors==header+0x40 &&
            CombatBlend::wide(a.data()+0x10)>=0x10000 && count>0 && count<0x80000000u &&
            word(a,0x3C)<count && word(b,0x3C)<count &&
            cursor>=0x40400000u && cursor<0x7F800000u && rate>0 && rate<0x7F800000u &&
            CombatBlend::wide(a.data()+0x58)==0;
    }
    static bool timer_compatible(const BlendTimerSample& saved,const BlendTimerSample& live,TimerDiagnostic& d,unsigned phase) noexcept {
        d={};d.phase=phase;d.offset=UINT32_MAX;
        if(saved.valid_mask!=63 || live.valid_mask!=63){d.reason=1;d.expected=saved.valid_mask;d.actual=live.valid_mask;return false;}
        for(unsigned side=0;side<2;++side)for(unsigned slot=0;slot<3;++slot){
            d.side=side;d.slot=slot;
            const auto& a=saved.slots[side][slot];const auto& b=live.slots[side][slot];
            // Native completion clears the cursor and changes active 2 to inactive 1.
            // Permit only the observed null-callback completion; all ownership bytes stay exact.
            const auto cursor=CombatBlend::word(a.data()+0x40),rate=CombatBlend::word(a.data()+0x44);
            const bool activated=timer_activation(a,b,phase);
            const bool completed=phase!=2 && CombatBlend::word(a.data()+0x48)==2 &&
                CombatBlend::word(b.data()+0x48)==1 && cursor>0 && cursor<0x7F800000u &&
                rate>0 && rate<0x7F800000u && CombatBlend::word(b.data()+0x40)==0 &&
                CombatBlend::word(a.data()+0x58)==0 && CombatBlend::word(a.data()+0x5C)==0;
            for(unsigned i=0;i<128;++i)if((i<0x40 || i>=0x44) &&
                !((completed || activated) && i>=0x48 && i<0x4C) &&
                !(activated && i>=0x3C && i<0x40) && a[i]!=b[i]){
                d.reason=2;d.offset=i;d.expected=std::to_integer<unsigned>(a[i]);d.actual=std::to_integer<unsigned>(b[i]);return false;
            }
            auto x=CombatBlend::word(a.data()+0x40),y=CombatBlend::word(b.data()+0x40);
            if(x>=0x7F800000u || y>=0x7F800000u || (x!=y && CombatBlend::word(a.data()+0x48)!=2 && !activated)){
                d.reason=3;d.offset=0x40;d.expected=x;d.actual=y;return false;
            }
        }
        return true;
    }
    bool timer_transition_ready() noexcept {
        return timer_compatible(blend_timer_source,blend_timer_next,timer_diagnostic,1);
    }
    bool timer_endpoint_ready() noexcept {
        read_blend_timers(blend_timer_live,measurements[2],7);
        // At load entry the live state belongs to the newer endpoint.
        return timer_compatible(blend_timer_next,blend_timer_live,timer_diagnostic,2);
    }
    bool blend_timer_restore_requested{},blend_timer_restored{};
    static bool timer_pair(const BlendTimerSample& saved,const BlendTimerSample& live) noexcept {
        TimerDiagnostic d{};return timer_compatible(saved,live,d,0);
    }
    bool timer_live_ready() noexcept {
        read_blend_timers(blend_timer_live,measurements[0],7);
        return timer_compatible(blend_timer_source,blend_timer_live,timer_diagnostic,3);
    }
    bool restore_blend_timers() noexcept {
        if(!timer_live_ready())return false;
        __try {
            for(unsigned side=0;side<2;++side)for(unsigned slot=0;slot<3;++slot){
                std::memcpy(reinterpret_cast<void*>(measurements[0].players[side]+0x972A4+slot*128),blend_timer_source.slots[side][slot].data()+0x3C,4);
                std::memcpy(reinterpret_cast<void*>(measurements[0].players[side]+0x972A8+slot*128),blend_timer_source.slots[side][slot].data()+0x40,4);
                std::memcpy(reinterpret_cast<void*>(measurements[0].players[side]+0x972B0+slot*128),blend_timer_source.slots[side][slot].data()+0x48,4);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
        BlendTimerSample live{};read_blend_timers(live,measurements[0],8);
        blend_timer_restored=live.valid_mask==63 && live.slots==blend_timer_source.slots;
        return blend_timer_restored;
    }
    static void read_blend_timers(BlendTimerSample& sample,const Measurement& m,unsigned phase) noexcept {
        sample={};sample.phase=phase;
        for(unsigned side=0;side<2;++side)for(unsigned slot=0;slot<3;++slot){
            const auto player=m.players[side];
            if(!m.valid || player<0x10000 || player>UINTPTR_MAX-0x973E8)continue;
            __try {
                std::memcpy(sample.slots[side][slot].data(),reinterpret_cast<const void*>(player+0x97268+slot*128),128);
                sample.valid_mask|=1u<<(side*3+slot);
            } __except(EXCEPTION_EXECUTE_HANDLER) { sample.slots[side][slot]={}; }
        }
    }
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
        if(blend_timer_count<blend_timer_history.size())read_blend_timers(blend_timer_history[blend_timer_count++],m,phase_index);
        else ++blend_timer_dropped;
    }
    bool general_combat_requested{};
    bool preserve_requested{},preserve_applied{},preserve_verified{},throw_requested{},charge_requested{},attack_requested{},edge_requested{},reversal_requested{},trigger_matched{};
    static constexpr unsigned scalar_offsets[]={0x20,0x24,0x30,0x34,0x40,0x50};
    static constexpr unsigned throw_scalar_offsets[]={0x1C,0x38};
    static bool combat_transition(const std::array<std::byte,224>& before,
                                 const std::array<std::byte,224>& after,unsigned side,bool,bool=false,bool=false){
        if(side>1)return false;
        uintptr_t object{},data{};
        std::memcpy(&object,before.data()+128,8);
        std::memcpy(&data,before.data()+136,8);
        unsigned old_mode{},new_mode{},old_flag{},new_flag{};
        std::memcpy(&old_mode,before.data()+128+0x1C,4);
        std::memcpy(&new_mode,after.data()+128+0x1C,4);
        std::memcpy(&old_flag,before.data()+128+0x38,4);
        std::memcpy(&new_flag,after.data()+128+0x38,4);
        // Reader30B080 and initializer2F8FA0/2FA080: non-null resources
        // rebuild flag0 and mode1, independent of the saved move or side.
        // Seven entries in pinned mode tables admit saved modes0..6.
        // Pointer identity is additionally checked by the unknown-byte guard.
        return object && data && old_mode<=6 && old_flag<=1 && new_mode==1 && new_flag==0;
    }
    static std::uint32_t advance_rng(std::uint32_t seed){
        for(unsigned i=0;i<4;++i)seed=static_cast<std::uint32_t>(static_cast<std::uint64_t>(seed)*16807%2147483647);
        return seed;
    }
    std::uint32_t load_entry_rng{};
    bool load_entry_rng_valid{};
    struct ScalarResetDiagnostic {unsigned side{},saved{},loaded{},live{},reason{},restored{};};
    std::array<ScalarResetDiagnostic,2> scalar_resets{};
    unsigned postload_failure_check{};
    static bool scalar_reset_compatible(unsigned saved,unsigned loaded){return loaded==saved || loaded==0x3F800000u;}
    bool preserve(uintptr_t b){
        auto& source=measurements[0];auto& written=measurements[1];auto& loaded=measurements[2];
        const auto entry_rng=load_entry_rng_valid?load_entry_rng:source.rng;
        if(!source.valid || !written.valid || !loaded.valid || source.rng==0 || source.rng>=2147483647 ||
           entry_rng==0 || entry_rng>=2147483647 || source.rng!=written.rng ||
           loaded.rng!=advance_rng(entry_rng) || source.fields!=written.fields)return false;
        for(unsigned side=0;side<2;++side){
            auto player=ptr(b+0x470DE90+side*8);
            if(player!=source.players[side] || player!=written.players[side] || player!=loaded.players[side])return false;
            uintptr_t rebuilt{};std::memcpy(&rebuilt,loaded.fields[side].data(),8);
            if(rebuilt!=player)return false;
            const bool general_controls_equal=general_combat_requested &&
                !std::memcmp(source.fields[side].data()+128+0x1C,loaded.fields[side].data()+128+0x1C,4) &&
                !std::memcmp(source.fields[side].data()+128+0x38,loaded.fields[side].data()+128+0x38,4);
            if(combat_requested() && !general_controls_equal && !combat_transition(source.fields[side],loaded.fields[side],side,charge_requested || attack_requested || edge_requested,reversal_requested,
                combat_later_window && combat_replay_requested && throw_requested && delayed_requested))return false;
            // Pinned initializer 2F90AD resets wrapper +54 to exact float 1.
            // Only that reset (or an unchanged value) is accepted; copy saved bits.
            auto& scalar_reset=scalar_resets[side];scalar_reset.side=side;
            std::memcpy(&scalar_reset.saved,source.fields[side].data()+128+0x54,4);
            std::memcpy(&scalar_reset.loaded,loaded.fields[side].data()+128+0x54,4);
            std::memcpy(&scalar_reset.live,reinterpret_cast<void*>(player+0x9723C),4);
            if(!scalar_reset_compatible(scalar_reset.saved,scalar_reset.loaded)){scalar_reset.reason=1;return false;}
            if(scalar_reset.live!=scalar_reset.loaded){scalar_reset.reason=2;return false;}
            // Reject unexpected changes before any preservation writes.
            for(unsigned k=8;k<224;++k){
                bool allowed=k<128 || (k>=128+0x54 && k<132+0x54);
                for(auto off:scalar_offsets)if(k>=128+off && k<132+off)allowed=true;
                if(combat_requested())for(auto off:throw_scalar_offsets)if(k>=128+off && k<132+off)allowed=true;
                if(!allowed && source.fields[side][k]!=loaded.fields[side][k])return false;
            }
        }
        if(blend_timer_restore_requested && !timer_live_ready())return false;
        preserve_applied=true;
        for(unsigned side=0;side<2;++side){
            auto player=source.players[side];
            std::memcpy(reinterpret_cast<void*>(player+0x97100),source.fields[side].data()+8,120);
            for(auto off:scalar_offsets)std::memcpy(reinterpret_cast<void*>(player+0x971E8+off),source.fields[side].data()+128+off,4);
            std::memcpy(reinterpret_cast<void*>(player+0x9723C),source.fields[side].data()+128+0x54,4);
            scalar_resets[side].restored=1;
        }
        if(combat_requested())for(unsigned side=0;side<2;++side)
            for(auto off:throw_scalar_offsets)
                std::memcpy(reinterpret_cast<void*>(source.players[side]+0x971E8+off),source.fields[side].data()+128+off,4);
        *reinterpret_cast<std::uint32_t*>(b+0x485EB28)=source.rng;
        if(blend_timer_restore_requested && !restore_blend_timers())return false;
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
    bool combat_requested()const{return general_combat_requested || throw_requested || charge_requested || attack_requested || edge_requested || reversal_requested;}
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
    // General loading is authorized by an exact captured live endpoint, not
    // a character-specific move number. Recheck immediately before mutation.
    bool general_live_endpoint(uintptr_t b)const{
        if(!general_combat_requested || !audit_done)return false;
        // Before loading, require the captured live endpoint. After replay,
        // require the independently recorded corrected endpoint.
        const auto& expected=delayed_loaded?original_next:measurements[2];
        if(!expected.valid || !expected.rng || expected.rng>=2147483647 ||
           *reinterpret_cast<const unsigned*>(b+0x485EB28)!=expected.rng)return false;
        std::array<unsigned,std::size(key_rvas)> live_keys{};uintptr_t live_players[2]{};
        keys(b,live_keys.data(),live_players);
        if(live_keys!=after_keys || std::memcmp(live_players,after_players,sizeof(live_players)))return false;
        for(unsigned side=0;side<2;++side){const auto player=live_players[side];
            if(!player || player!=expected.players[side] ||
               (std::memcmp(reinterpret_cast<void*>(player+0x43DF0),expected.fields[side].data(),8) && ptr(player+0x43DF0)!=player) ||
               std::memcmp(reinterpret_cast<void*>(player+0x97100),expected.fields[side].data()+8,120) ||
               std::memcmp(reinterpret_cast<void*>(player+0x971E8),expected.fields[side].data()+128,96))return false;
        }
        return true;
    }
    bool combat_state(uintptr_t b)const{
        if(general_combat_requested)return general_live_endpoint(b);
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
    unsigned combat_postload_pass_mask{};
    CombatBlend::LoadedDiagnostic combat_loaded_blend;
    bool combat_preflight_attempted{};
    // Report the exact rejected predicate without relaxing blend readiness.
    bool combat_blend_ready(){
        blend_pair_diagnostic={};blend_pair_evidence={};
        auto& d=blend_pair_diagnostic;
        if(!blends[0].valid || !blends[1].valid){d.reason=1;d.saved=blends[0].valid;d.next=blends[1].valid;return false;}
        for(unsigned side=0;side<2;++side)if(blends[0].owners[side]!=blends[1].owners[side]){
            d.reason=2;d.side=side;d.saved_owner=blends[0].owners[side];d.next_owner=blends[1].owners[side];return false;
        }
        for(unsigned side=0;side<2;++side){
            const auto& a=blends[0].bytes[side];const auto& b=blends[1].bytes[side];CombatBlend::Diagnostic detail;
            // Archived-only preflight must not consult a newer live wrapper.
            const uintptr_t table=side==1 && blends[0].tables[side]==blends[1].tables[side] &&
                blends[0].resources[side]>=0x10000 && blends[0].resources[side]==blends[1].resources[side]?
                blends[0].tables[side]:0;
            const bool accepted=side==0 && charge_requested?
                (measurements[0].valid && CombatBlend::charge_adjacent(a,b,measurements[0].fields[0].data()+128)):
                CombatBlend::adjacent(a,b,side==0 && throw_requested,&detail,general_combat_requested,table);
            if(accepted)continue;
            d.reason=side==0 && charge_requested?8:detail.reason;d.side=side;d.offset=detail.offset;d.saved=detail.saved;d.next=detail.next;
            d.saved_owner=blends[0].owners[side];d.next_owner=blends[1].owners[side];
            if(d.offset>=140 && d.offset<1372){const auto block=(d.offset-140)/176;const auto* x=a.data()+140+block*176;const auto* y=b.data()+140+block*176;
                d.table_saved=CombatBlend::wide(x+8);d.table_next=CombatBlend::wide(y+8);
                d.mask_saved=CombatBlend::wide(x+24);d.mask_next=CombatBlend::wide(y+24);
                d.index_saved=CombatBlend::word(x+16);d.index_next=CombatBlend::word(y+16);}
            d.wrapper_saved=blends[0].tables[side];d.wrapper_next=blends[1].tables[side];
            d.resource_saved=blends[0].resources[side];d.resource_next=blends[1].resources[side];
            std::copy_n(a.begin(),1372,blend_pair_evidence.saved.begin());std::copy_n(b.begin(),1372,blend_pair_evidence.next.begin());blend_pair_evidence.valid=true;
            for(unsigned offset=0;offset<1372;++offset)if(a[offset]!=b[offset])++d.changed;
            return false;
        }
        return true;
    }
    unsigned combat_preflight_pass_mask{};
    CameraSnapshot::ReadinessDiagnostic camera_readiness;
    bool general_idle_camera{};
    bool camera_rising_requested()const{return charge_requested || (general_combat_requested && !general_idle_camera);}
    static constexpr unsigned combat_preflight_required_mask=255;
    // Read-only rehearsal. Post-load acceptance still requires live validation.
    void combat_replay_preflight(uintptr_t b) noexcept {
        if(combat_preflight_attempted)return;
        combat_preflight_attempted=true;
        if(!throw_audit || !audit_done || fault || restore_invoked)return;
        __try {
            if(delayed_sources_ok(b))combat_preflight_pass_mask|=1;
            if(input_checkpoints[0].valid && input_checkpoints[1].valid &&
               input_checkpoints[0].bytes==input_checkpoints[1].bytes)combat_preflight_pass_mask|=2;
            if(combat_blend_ready())combat_preflight_pass_mask|=4;
            if(transforms[0].combat_groups_ready(transforms[1],transforms[1]))combat_preflight_pass_mask|=8;
            if(transforms[0].restore_wind(transforms[1],transforms[1],b,true,&wind_preflight_diagnostic))combat_preflight_pass_mask|=16;
            if(transforms[0].restore_sway(transforms[1],transforms[1],b,true))combat_preflight_pass_mask|=32;
            if(transforms[0].restore_constraints(transforms[1],transforms[1],b,true))combat_preflight_pass_mask|=64;
            general_idle_camera=general_combat_requested && cameras[0].adjacent_idle_ready(cameras[1],b,restore_span);
            if(general_idle_camera || cameras[0].adjacent_combat_ready(cameras[1],b,camera_rising_requested(),&camera_readiness,false,general_combat_requested,restore_span))combat_preflight_pass_mask|=128;
        }__except(EXCEPTION_EXECUTE_HANDLER){return;}
    }
    FullRestoreStream replay,restored;
    RingSnapshot restored_rings,restored_after_rings;
    struct BlendCheckpoint {
        uintptr_t owners[2]{};
        uintptr_t tables[2]{},resources[2]{};
        std::array<std::array<std::byte,1380>,2> bytes{};
        bool valid{};
    };
    std::array<BlendCheckpoint,10> blends{};
    std::array<std::array<std::array<unsigned,2>,2>,10> blend_branch_inputs{};
    std::array<TransformSnapshot,10> transforms{};
    static constexpr const char* blend_names[]={"original_a","original_b","restored","restored_after_save","replay","weights_corrected","blocks_corrected","wind_corrected","sway_corrected","constraints_corrected"};
    bool wind_restore_verified{},sway_restore_verified{},constraint_restore_verified{};
    TransformSnapshot::WindDiagnostic wind_diagnostic,wind_preflight_diagnostic;
    EffectRngBoundary effect_rng_boundary;
    mutable std::array<uint64_t,5> motion_zero_evidence{}; // count, record, offset, expected bits, actual bits
    CameraSnapshot::RestoreDiagnostic camera_diagnostic;
    bool blend_blocks_applied{},blend_blocks_verified{};
    struct BlendBlockDiagnostic {
        unsigned stage{},side{UINT32_MAX},block{UINT32_MAX},offset{UINT32_MAX};
        uint64_t expected{},actual{},index{},loaded_index{},mask{},loaded_mask{};
        bool compare(const std::byte* a,const std::byte* b,unsigned count){
            for(unsigned i=0;i<count;++i)if(a[i]!=b[i]){offset=i;
                expected=std::to_integer<unsigned>(a[i]);actual=std::to_integer<unsigned>(b[i]);return false;}
            return true;
        }
    } blend_block_diagnostic;
    std::array<uintptr_t,2> combat_reset_tables{},combat_reset_resources{};
    bool verify_combat_reset_tables(uintptr_t b) noexcept {
        combat_reset_tables={};combat_reset_resources={};
        __try {
            if(!general_combat_requested)return true;
            for(unsigned side=0;side<2;++side){
                auto player=ptr(b+0x470DE90+side*8),owner=blends[0].owners[side];
                if(player<0x10000 || player>UINTPTR_MAX-0x97200 || ptr(player+0x971F8)!=owner)return false;
                uintptr_t table{},resource{};
                for(unsigned m:{0u,1u,2u}){
                    if(!measurements[m].valid || measurements[m].players[side]!=player || blends[m].owners[side]!=owner)return false;
                    const auto* wrapper=measurements[m].fields[side].data()+128;
                    if(CombatBlend::wide(wrapper+16)!=owner)return false;
                    if(!m){table=CombatBlend::wide(wrapper);resource=CombatBlend::wide(wrapper+8);}
                    else if(CombatBlend::wide(wrapper)!=table || CombatBlend::wide(wrapper+8)!=resource)return false;
                }
                if(ptr(player+0x971E8)!=table || ptr(player+0x971F0)!=resource)return false;
                combat_reset_tables[side]=table;combat_reset_resources[side]=resource;
            }
            return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){combat_reset_tables={};combat_reset_resources={};return false;}
    }
    bool preserve_blend_blocks(uintptr_t b) noexcept {
        auto& d=blend_block_diagnostic;d={};d.stage=1;
        __try {
            if(!blend_weights_verified || !blends[0].valid || !blends[1].valid || !blends[5].valid)return false;
            for(unsigned side=0;side<2;++side){
                d.side=side;d.stage=2;
                auto owner=blends[0].owners[side];auto player=ptr(b+0x470DE90+side*8);
                if(player<0x10000 || ptr(player+0x971F8)!=owner || owner!=blends[1].owners[side] || owner!=blends[5].owners[side])return false;
                auto source=blends[0].bytes[side].data()+140;
                auto loaded=blends[5].bytes[side].data()+140;
                d.stage=3;
                auto next=blends[1].bytes[side].data()+140;
                d.stage=4;
                if(!d.compare(loaded,reinterpret_cast<const std::byte*>(owner+0x1DD8),1232))return false;
                for(unsigned j=0;j<7;++j){
                    d.block=j;d.stage=5;
                    auto s=source+j*176;auto l=loaded+j*176;auto n=next+j*176;
                    // Selection, mask and 35 float weights are mutable. Resource
                    // pointers, unknown fields and padding must retain identity.
                    for(unsigned k=0;k<176;++k)
                        if(!(k>=16 && k<20) && !(k>=24 && k<172) && s[k]!=n[k]){d.stage=3;d.offset=k;d.expected=std::to_integer<unsigned>(s[k]);d.actual=std::to_integer<unsigned>(n[k]);return false;}
                    for(unsigned k=0;k<176;++k)
                        if(!(k>=16 && k<20) && !(k>=24 && k<172) && s[k]!=l[k]){d.offset=k;d.expected=std::to_integer<unsigned>(s[k]);d.actual=std::to_integer<unsigned>(l[k]);return false;}
                    for(unsigned k=32;k<172;k+=4){
                        std::uint32_t saved_weight{},next_weight{},loaded_weight{};
                        std::memcpy(&saved_weight,s+k,4);std::memcpy(&next_weight,n+k,4);std::memcpy(&loaded_weight,l+k,4);
                        if((saved_weight&0x7F800000u)==0x7F800000u || (next_weight&0x7F800000u)==0x7F800000u ||
                           (loaded_weight!=saved_weight && loaded_weight!=next_weight)){d.offset=k;d.expected=saved_weight;d.actual=loaded_weight;return false;}
                    }
                    uintptr_t table{};std::uint32_t index{},new_index{};std::uint64_t mask{},new_mask{};
                    std::uint32_t next_index{};std::uint64_t next_mask{};
                    std::memcpy(&next_index,n+16,4);std::memcpy(&next_mask,n+24,8);
                    if(next_mask>>35)return false;
                    std::memcpy(&table,s+8,8);std::memcpy(&index,s+16,4);std::memcpy(&new_index,l+16,4);
                    std::memcpy(&mask,s+24,8);std::memcpy(&new_mask,l+24,8);
                    d.stage=6;d.index=index;d.loaded_index=new_index;d.mask=mask;d.loaded_mask=new_mask;
                    // Loader2FA080 reconstructs block2 selection. Narrow observed A+G case only.
                    const bool throw_selection=combat_load_diagnostic && throw_requested && !delayed_requested &&
                        side==1 && j==2 && index==12 && new_index==3 && mask==0 && new_mask==0;
                    // RVA2F8FA0 invokes 2FA080(mode=1) when both resources
                    // exist. Its selection lands at owner+1F48 (block2+16),
                    // from resource[3]&127, clamped to zero by table bounds.
                    // Verify captured/live identities before reading the resource.
                    bool reset_selection=false;
                    if(j==2 && new_index!=0 && new_index!=index && measurements[0].valid){
                        uintptr_t resource{},saved_table{},saved_owner{};
                        const auto* state=measurements[0].fields[side].data()+128;
                        std::memcpy(&saved_table,state,8);std::memcpy(&resource,state+8,8);
                        std::memcpy(&saved_owner,state+16,8);
                        if(measurements[0].players[side]==player && saved_owner==owner &&
                           saved_table==table && table>=0x10000 && resource>=0x10000 && resource<=UINTPTR_MAX-3 &&
                           ptr(player+0x971E8)==table && ptr(player+0x971F0)==resource){
                            const auto count=*reinterpret_cast<const std::int32_t*>(table);
                            const auto selected=*reinterpret_cast<const unsigned char*>(resource+3)&127u;
                            reset_selection=count>0 && selected<static_cast<unsigned>(count) && new_index==selected;
                        }
                    }
                    if((new_index!=0 && new_index!=index && !(a_restore_requested && side==0 && j==0 && index==12 && new_index==14) && !throw_selection && !reset_selection) || (new_mask!=0 && new_mask!=mask) || (mask>>35))return false;
                    d.stage=7;
                    if(table){
                        if(table<0x10000)return false;
                        auto count=*reinterpret_cast<const std::int32_t*>(table);
                        if(count<=0 || index>=static_cast<unsigned>(count) || new_index>=static_cast<unsigned>(count) || next_index>=static_cast<unsigned>(count))return false;
                    }else if(index || mask || next_index || next_mask)return false;
                }
            }
            d.stage=8;blend_blocks_applied=true;
            for(unsigned side=0;side<2;++side)for(unsigned j=0;j<7;++j){
                auto target=blends[0].owners[side]+0x1DD8+j*176;
                auto source=blends[0].bytes[side].data()+140+j*176;
                std::memcpy(reinterpret_cast<void*>(target+16),source+16,4);
                std::memcpy(reinterpret_cast<void*>(target+24),source+24,8);
                std::memcpy(reinterpret_cast<void*>(target+32),source+32,140);
            }
            if(!capture_blend(b,6))return false;
            d.stage=9;blend_blocks_verified=true;
            for(unsigned side=0;side<2;++side)
                blend_blocks_verified=blend_blocks_verified && !std::memcmp(blends[0].bytes[side].data(),blends[6].bytes[side].data(),1372) &&
                    !std::memcmp(blends[5].bytes[side].data()+1372,blends[6].bytes[side].data()+1372,8);
            if(blend_blocks_verified)d.stage=10;
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
                // Current-frame weights may differ during animation. The native
                // reader resets these words independently of either frame;
                // identity and the captured/live zero result are the guards.
                if(std::memcmp(reinterpret_cast<void*>(owner+0x4BC),blends[2].bytes[side].data(),140))return false;
                for(unsigned i=0;i<35;++i){
                    std::uint32_t source{},loaded{};
                    std::memcpy(&source,blends[0].bytes[side].data()+i*4,4);
                    std::memcpy(&loaded,blends[2].bytes[side].data()+i*4,4);
                    // Pinned initializer RVA2F9050 clears all 35 words,
                    // independent of their saved values. Restore finite captured
                    // weights, not only constants from the original idle test.
                    if(loaded || (source&0x7F800000u)==0x7F800000u)return false;
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
    unsigned intermediate_refreshes{},intermediate_fallbacks{},intermediate_last_rejection{};
    bool capture_blend(uintptr_t b,unsigned checkpoint,unsigned prior_checkpoint=UINT32_MAX) noexcept {
        auto& out=blends[checkpoint];out.valid=false;
        __try {
            for(unsigned side=0;side<2;++side){
                auto player=ptr(b+0x470DE90+side*8);
                if(player<0x10000)return false;
                auto owner=ptr(player+0x971F8);
                if(owner<0x10000 || owner>UINTPTR_MAX-0x22B0)return false;
                if(checkpoint && owner!=blends[0].owners[side])return false;
                out.owners[side]=owner;
                out.tables[side]=ptr(player+0x971E8);out.resources[side]=ptr(player+0x971F0);
                blend_branch_inputs[checkpoint][side]={*reinterpret_cast<unsigned*>(player+0x78),*reinterpret_cast<unsigned*>(player+0x210)};
                std::memcpy(out.bytes[side].data(),reinterpret_cast<void*>(owner+0x4BC),140);
                std::memcpy(out.bytes[side].data()+140,reinterpret_cast<void*>(owner+0x1DD8),1232);
                std::memcpy(out.bytes[side].data()+1372,reinterpret_cast<void*>(owner+0x22A8),8);
                if(ptr(player+0x971F8)!=owner || ptr(b+0x470DE90+side*8)!=player ||
                   ptr(player+0x971E8)!=out.tables[side] || ptr(player+0x971F0)!=out.resources[side])return false;
            }
            bool refreshed=false;
            if(general_combat_requested && external_wind_lifecycle && prior_checkpoint<transforms.size() && prior_checkpoint!=checkpoint){
                refreshed=transforms[checkpoint].refresh_intermediate(transforms[prior_checkpoint],out.owners,b);
                if(refreshed)++intermediate_refreshes;
                else {++intermediate_fallbacks;intermediate_last_rejection=transforms[checkpoint].failure;}
            }
            if(!refreshed){
                if(!transforms[checkpoint].capture(out.owners,b,true))return false;
                if(external_wind_lifecycle && !transforms[checkpoint].separate_effects())return false;
            }
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
    mutable unsigned source_guard_reason{},source_guard_record{};
    mutable uint64_t source_guard_length{};
    mutable uintptr_t source_guard_saved{},source_guard_next{},source_guard_writer{};
    // Ghidra: save RVA30EC90 copies global RVA470DEC4 to [rsp+30]
    // before writer return RVA30ED1F. Load RVA30EDC0 reads a fresh stack
    // local and assigns the global. This record owns bytes, not its source.
    bool audited_scalar_source(unsigned i,uintptr_t module)const{
        if(!module || module>UINTPTR_MAX-0x30ED1F || i<4 ||
           i>=saved.writes || source_index(i)>=after_save.writes)return false;
        constexpr uintptr_t writers[]={0x30ECBF,0x30ECD5,0x30ECEB,0x30ED01,0x30ED1F};
        constexpr uint64_t sizes[]={0x200,0x200,0x14,4,4};
        constexpr uintptr_t offsets[]={0,0x200,0x45C,0x470};
        const auto owner=saved.traces[i-4].source;
        if(owner<0x10000 || owner>UINTPTR_MAX-0x474)return false;
        for(unsigned j=0;j<5;++j){
            const auto k=i-4+j;
            if(source_index(k)>=after_save.writes || saved.lengths[k]!=sizes[j] || after_save.lengths[source_index(k)]!=sizes[j] ||
               saved.traces[k].writer!=module+writers[j] ||
               after_save.traces[source_index(k)].writer!=module+writers[j])return false;
            if(j<4 && (saved.traces[k].source!=owner+offsets[j] ||
                       after_save.traces[source_index(k)].source!=owner+offsets[j]))return false;
        }
        return true;
    }
    // RVA30A980 serializes nine reference-table descriptors through one
    // [rbp-20] local; RVA30AE80 reads fresh locals and reconstructs pointers.
    // Only the temporary source identity is irrelevant, never the payload.
    bool audited_reference_source(unsigned i,uintptr_t module)const{
        constexpr uintptr_t writers[]={0x30AB2F,0x30AB84,0x30ABDF,0x30AC34,0x30AC8F,0x30ACE4,0x30AD3F,0x30AD94,0x30ADEF};
        if(!module || module>UINTPTR_MAX-writers[8] || i>=saved.writes || source_index(i)>=after_save.writes)return false;
        unsigned position=9;
        for(unsigned j=0;j<9;++j)if(saved.traces[i].writer==module+writers[j])position=j;
        if(position==9 || i<position)return false;
        const unsigned first=i-position;
        if(source_index(first)>=after_save.writes || saved.writes-first<9 || after_save.writes-source_index(first)<9)return false;
        const auto a=saved.traces[first].source,b=after_save.traces[source_index(first)].source;
        if(a<0x10000 || b<0x10000 || a>UINTPTR_MAX-16 || b>UINTPTR_MAX-16)return false;
        for(unsigned j=0;j<9;++j){
            const auto k=first+j;
            if(source_index(k)>=after_save.writes || saved.lengths[k]!=16 || after_save.lengths[source_index(k)]!=16 ||
               saved.traces[k].writer!=module+writers[j] || after_save.traces[source_index(k)].writer!=module+writers[j] ||
               saved.traces[k].source!=a || after_save.traces[source_index(k)].source!=b)return false;
        }
        return true;
    }
    // Audited native temporary descriptors: each group reuses one stack local.
    // Writers/loaders: 31DC00/31DE50, 301820/301B10, 390690/390800.
    // Repeated loop callsites require trying both possible positions.
    template<size_t N> bool temporary_descriptor_group(unsigned i,uintptr_t module,
                                                       const uintptr_t (&writers)[N])const{
        if(!module || module>UINTPTR_MAX-0x400000 || i>=saved.writes || source_index(i)>=after_save.writes)return false;
        for(unsigned position=0;position<N;++position){
            if(i<position || saved.traces[i].writer!=module+writers[position])continue;
            const unsigned first=i-position;
            if(source_index(first)>=after_save.writes || saved.writes-first<N || after_save.writes-source_index(first)<N)continue;
            const auto archived_source=saved.traces[first].source,observed_source=after_save.traces[source_index(first)].source;
            if(archived_source<0x10000 || observed_source<0x10000 || archived_source>UINTPTR_MAX-16 || observed_source>UINTPTR_MAX-16)continue;
            bool match=true;
            for(unsigned j=0;j<N;++j){
                const auto k=first+j;
                if(source_index(k)>=after_save.writes || saved.lengths[k]!=16 || after_save.lengths[source_index(k)]!=16 ||
                   saved.traces[k].writer!=module+writers[j] || after_save.traces[source_index(k)].writer!=module+writers[j] ||
                   saved.traces[k].source!=archived_source || after_save.traces[source_index(k)].source!=observed_source){match=false;break;}
            }
            if(match)return true;
        }
        return false;
    }
    bool audited_additional_temporary_source(unsigned i,uintptr_t module)const{
        constexpr uintptr_t globals[]={0x31DCF1,0x31DD48,0x31DDA1,0x31DDF5};
        constexpr uintptr_t selector_writers[]={0x301A75,0x301AD2};
        constexpr uintptr_t pairs[]={0x390765,0x3907B6,0x390765,0x3907B6};
        if(temporary_descriptor_group(i,module,globals) || temporary_descriptor_group(i,module,selector_writers) ||
           temporary_descriptor_group(i,module,pairs))return true;
        // Camera sentinels/indices and polymorphic type tags are copied values.
        // Their native loaders read fresh locals (317C70,340F70,3411A0,321A30).
        // 32AC00 writes a reference descriptor and a separately copied 64-bit field;
        // 32ACE0 reads fresh locals. No source pointer is retained by the stream.
        if(!module || module>UINTPTR_MAX-0x400000 || i>=saved.writes || source_index(i)>=after_save.writes)return false;
        constexpr uintptr_t writers[]={0x317C17,0x317C60,0x340F59,0x341183,0x3217EC,0x321814,0x32AC76,0x32ACAC};
        constexpr uint64_t sizes[]={4,4,4,4,4,4,16,8};
        for(unsigned j=0;j<std::size(writers);++j){
            if(saved.traces[i].writer!=module+writers[j] || after_save.traces[source_index(i)].writer!=module+writers[j] ||
               saved.lengths[i]!=sizes[j] || after_save.lengths[source_index(i)]!=sizes[j])continue;
            const auto archived_source=saved.traces[i].source,observed_source=after_save.traces[source_index(i)].source;
            return archived_source>=0x10000 && observed_source>=0x10000 && archived_source<=UINTPTR_MAX-sizes[j] && observed_source<=UINTPTR_MAX-sizes[j];
        }
        return false;
    }
    bool delayed_sources_ok(uintptr_t module=0)const{
        source_guard_reason=source_guard_record=0;source_guard_length=0;
        source_guard_saved=source_guard_next=source_guard_writer=0;
        if(audit_shape_changes || !ring_save_unchanged || !ring_rotation_ok){source_guard_reason=audit_shape_changes?1:!ring_save_unchanged?2:3;return false;}
        for(unsigned i=0;i<saved.writes;++i){
            if(camera_slots[0].enabled && source_index(i)==CameraSlots::empty)continue;
            if(source_index(i)>=after_save.writes){source_guard_reason=5;source_guard_record=i;return false;}
            if(saved.traces[i].source==after_save.traces[source_index(i)].source)continue;
            if(audited_scalar_source(i,module) || audited_reference_source(i,module) || audited_additional_temporary_source(i,module))continue;
            bool ring=false;
            for(unsigned j=0;j<4;++j){
                auto expected= j%2?0x800u:0x1840u;
                if(saved.lengths[i]==expected && saved.traces[i].source==rings[0].headers[j].current &&
                   after_save.traces[source_index(i)].source==rings[2].headers[j].current)ring=true;
            }
            if(!ring){source_guard_reason=4;source_guard_record=i;source_guard_length=saved.lengths[i];
                source_guard_saved=saved.traces[i].source;source_guard_next=after_save.traces[source_index(i)].source;source_guard_writer=saved.traces[i].writer;return false;}
        }
        return true;
    }
    bool outer_correction_applied{},outer_correction_verified{};
    bool correct_delayed_outer(uintptr_t b){
        if(!delayed_keys_captured || !restore_completed || !postflight_ok ||
           std::memcmp(delayed_loaded_players,before_players,sizeof(before_players)))return false;
        for(unsigned i=1;i<before_keys.size();++i)if(delayed_loaded_keys[i]!=before_keys[i])return false;
        if(delayed_loaded_keys[0]!=before_keys[0]){
            if(!restore_span || restore_span>3 || before_keys[0]>UINT32_MAX-restore_span || after_keys[0]!=before_keys[0]+restore_span || delayed_loaded_keys[0]!=after_keys[0] ||
               *reinterpret_cast<std::uint32_t*>(b+key_rvas[0])!=delayed_loaded_keys[0])return false;
            *reinterpret_cast<std::uint32_t*>(b+key_rvas[0])=before_keys[0];
            outer_correction_applied=true;
        }
        std::array<std::uint32_t,std::size(key_rvas)> corrected{};uintptr_t players[2]{};
        keys(b,corrected.data(),players);
        outer_correction_verified=corrected==before_keys && !std::memcmp(players,before_players,sizeof(players));
        return outer_correction_verified;
    }
    NativeLoadRoute::Diagnostic load_route_diagnostic;
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
                if(!NativeRollbackTiming::measure(NativeRollbackTiming::restore_state_capture,[&]{return capture_blend(b,audit_started?1u:0u);})){delayed_failure_line=__LINE__;fault=true;__leave;}
                audit_step(b,writer,check);
                if(!fault){if(audit_done)cameras[1].capture(after_save,b);else if(save_valid)cameras[0].capture(saved,b);}
                if(fault || !audit_done)__leave;
            }
            bookkeeping=ptr(b+0x470E870);captured=true;
            if(!delayed_loaded){
                original_next=measurements[2];
                delayed_guard_ok=NativeRollbackTiming::measure(NativeRollbackTiming::restore_source_guard,[&]{return delayed_sources_ok(b);}) && original_next.rng>0 && original_next.rng<2147483647;
                for(unsigned side=0;side<2;++side)
                    delayed_guard_ok=delayed_guard_ok && !std::memcmp(original_next.fields[side].data()+128,measurements[0].fields[side].data()+128,16);
                if(combat_replay_requested){
                    NativeRollbackTiming::measure(NativeRollbackTiming::restore_combat_preflight,[&]{combat_replay_preflight(b);});
                    delayed_guard_ok=delayed_guard_ok &&
                        (combat_preflight_pass_mask | (recorded_input_replay && delayed_inputs_equal?2u:0u))==combat_preflight_required_mask;
                }
                if(blend_timer_restore_requested && (!timer_transition_ready() || !timer_endpoint_ready()))delayed_guard_ok=false;
                if(!delayed_guard_ok){delayed_failure_line=__LINE__;fault=true;__leave;}
                if(reader==reinterpret_cast<Routine>(b+0x384540) && !NativeLoadRoute::ready(saved,b,load_route_diagnostic)){
                    delayed_failure_line=__LINE__;fault=true;__leave;
                }
                // Loader side effects start from the live seed, not the older
                // snapshot's seed. Check drift before any restoration writes.
                load_entry_rng=*reinterpret_cast<const std::uint32_t*>(b+0x485EB28);
                if(load_entry_rng!=original_next.rng){delayed_failure_line=__LINE__;fault=true;__leave;}
                load_entry_rng_valid=true;
                if(camera_slots[0].enabled && (!camera_slots[0].compatible(camera_slots[1]) || !camera_slots[1].live())){delayed_failure_line=__LINE__;fault=true;__leave;}
                // Restore physical slot selection before native fixups resolve aliases.
                if(!NativeRollbackTiming::measure(NativeRollbackTiming::restore_rings,[&]{return rings[0].restore(b,delayed_scratch);})){delayed_failure_line=__LINE__;fault=true;__leave;}
                restore_invoked=true;
                if(camera_slots[0].enabled && !camera_slots[0].restore_empty(camera_slots[1])){delayed_failure_line=__LINE__;fault=true;__leave;}
                NativeRollbackTiming::measure(NativeRollbackTiming::restore_native_load,[&]{reader(&saved.stream);});restore_completed=saved.consumed();
                if(camera_slots[0].enabled && !camera_slots[0].live()){delayed_failure_line=__LINE__;fault=true;__leave;}
                measure(b,2);
                keys(b,delayed_loaded_keys.data(),delayed_loaded_players);delayed_keys_captured=true;
                postflight_ok=check(b);
                selected_equal=correct_delayed_outer(b);
                postload_failure_check=0;
                if(!restore_completed)postload_failure_check=1;
                else if(!selected_equal)postload_failure_check=2;
                else if(!postflight_ok)postload_failure_check=3;
                else if(!preserve(b))postload_failure_check=4;
                else if(!NativeRollbackTiming::measure(NativeRollbackTiming::restore_rings,[&]{return rings[0].restore(b,delayed_scratch);}))postload_failure_check=5;
                if(postload_failure_check){delayed_failure_line=__LINE__;fault=true;}
                else {
                    delayed_loaded=true;measure(b,5);
                    if(!NativeRollbackTiming::measure(NativeRollbackTiming::restore_state_capture,[&]{return capture_blend(b,2);})){delayed_failure_line=__LINE__;fault=true;__leave;}
                    if(combat_replay_requested){
                        combat_postload_pass_mask=0;
                        combat_postload_ready=verify_combat_reset_tables(b) && CombatBlend::ready(blends[0].bytes,blends[1].bytes,blends[2].bytes,blends[0].owners,throw_requested,general_combat_requested,&combat_loaded_blend,combat_reset_tables.data(),combat_reset_resources.data()) && ((combat_postload_pass_mask|=1),true) &&
                            transforms[0].combat_groups_ready(transforms[1],transforms[2]) && ((combat_postload_pass_mask|=2),true) &&
                            NativeRollbackTiming::measure(NativeRollbackTiming::restore_wind,[&]{return transforms[0].restore_root_wind(transforms[1],transforms[2],true);}) && ((combat_postload_pass_mask|=4),true) &&
                            NativeRollbackTiming::measure(NativeRollbackTiming::restore_wind,[&]{return transforms[0].restore_wind(transforms[1],transforms[2],b,true);}) && ((combat_postload_pass_mask|=8),true) &&
                            NativeRollbackTiming::measure(NativeRollbackTiming::restore_sway,[&]{return transforms[0].restore_sway(transforms[1],transforms[2],b,true);}) && ((combat_postload_pass_mask|=16),true) &&
                            NativeRollbackTiming::measure(NativeRollbackTiming::restore_constraints,[&]{return transforms[0].restore_constraints(transforms[1],transforms[2],b,true);}) && ((combat_postload_pass_mask|=32),true) &&
                            cameras[2].capture(saved,b) && ((combat_postload_pass_mask|=64),true) &&
                            NativeRollbackTiming::measure(NativeRollbackTiming::restore_camera,[&]{return cameras[0].restore_smoothing(cameras[1],cameras[2],b,false,!general_idle_camera,true,camera_rising_requested(),&camera_diagnostic,general_combat_requested,restore_span);}) && ((combat_postload_pass_mask|=128),true);
                        if(!combat_postload_ready){delayed_failure_line=__LINE__;fault=true;__leave;}
                        if(!NativeRollbackTiming::measure(NativeRollbackTiming::restore_blend,[&]{return CombatBlend::restore(blends[0].bytes,blends[1].bytes,blends[2].bytes,blends[0].owners,throw_requested,general_combat_requested,combat_reset_tables.data(),combat_reset_resources.data());})){delayed_failure_line=__LINE__;fault=true;__leave;}
                        blend_weights_applied=blend_weights_verified=blend_blocks_applied=blend_blocks_verified=true;
                        combat_groups_restored=transforms[0].restore_combat_groups(transforms[1],transforms[2]);
                        combat_root_wind_restored=combat_groups_restored && NativeRollbackTiming::measure(NativeRollbackTiming::restore_wind,[&]{return transforms[0].restore_root_wind(transforms[1],transforms[2]);});
                        if(!combat_root_wind_restored || !NativeRollbackTiming::measure(NativeRollbackTiming::restore_state_capture,[&]{return capture_blend(b,6,2);})){delayed_failure_line=__LINE__;fault=true;__leave;}
                    }else{
                        if(!preserve_blend_weights(b)){delayed_failure_line=__LINE__;fault=true;__leave;}
                        if(!preserve_blend_blocks(b)){delayed_failure_line=__LINE__;fault=true;__leave;}
                    }
                    wind_restore_verified=NativeRollbackTiming::measure(NativeRollbackTiming::restore_wind,[&]{return transforms[0].restore_wind(transforms[1],transforms[6],b,false,&wind_diagnostic);});
                    if(!wind_restore_verified || !NativeRollbackTiming::measure(NativeRollbackTiming::restore_state_capture,[&]{return capture_blend(b,7,6);})){delayed_failure_line=__LINE__;fault=true;__leave;}
                    sway_restore_verified=NativeRollbackTiming::measure(NativeRollbackTiming::restore_sway,[&]{return transforms[0].restore_sway(transforms[1],transforms[7],b);});
                    if(!sway_restore_verified || !NativeRollbackTiming::measure(NativeRollbackTiming::restore_state_capture,[&]{return capture_blend(b,8,7);})){delayed_failure_line=__LINE__;fault=true;__leave;}
                    constraint_restore_verified=NativeRollbackTiming::measure(NativeRollbackTiming::restore_constraints,[&]{return transforms[0].restore_constraints(transforms[1],transforms[8],b);});
                    if(!constraint_restore_verified || !NativeRollbackTiming::measure(NativeRollbackTiming::restore_state_capture,[&]{return capture_blend(b,9,8);})){delayed_failure_line=__LINE__;fault=true;__leave;}
                    if(general_combat_requested && !ActiveAnimationGroup::restore(saved,before_players,b,active_group_diagnostic)){delayed_failure_line=__LINE__;fault=true;__leave;}
                    if(!restored_rings.capture(b)){delayed_failure_line=__LINE__;fault=true;__leave;}
                    restored_ring_equal=restored_rings.same(rings[0]);
                    NativeRollbackTiming::measure(NativeRollbackTiming::restore_native_resave,[&]{writer(&restored.stream);});measure(b,6);restored_valid=restored.seal();
                    if(restored_valid)cameras[2].capture(restored,b);
                    if(!NativeRollbackTiming::measure(NativeRollbackTiming::restore_state_capture,[&]{return capture_blend(b,3);})){delayed_failure_line=__LINE__;fault=true;__leave;}
                    restored_after_rings.capture(b);
                    restored_save_unchanged=measurements[5].rng==measurements[6].rng && measurements[5].fields==measurements[6].fields &&
                        !std::memcmp(measurements[5].players,measurements[6].players,sizeof(measurements[5].players)) && restored_rings.same(restored_after_rings);
                    restored_save_unchanged=restored_save_unchanged && blends[6].bytes==blends[3].bytes;
                    if(!restored_valid || !restored_save_unchanged){delayed_failure_line=__LINE__;fault=true;__leave;}
                    if(camera_restore_requested){
                        camera_restore_verified=NativeRollbackTiming::measure(NativeRollbackTiming::restore_camera,[&]{return cameras[0].restore_smoothing(cameras[1],cameras[2],b,false,(combat_replay_requested && !general_idle_camera) || direction_restore_requested || a_restore_requested,false,camera_rising_requested() || direction_restore_requested || a_restore_requested,&camera_diagnostic,general_combat_requested,restore_span);});
                        if(!camera_restore_verified || !cameras[4].capture(restored,b)){delayed_failure_line=__LINE__;fault=true;__leave;}
                    }
                    if(combat_replay_requested && !(general_combat_requested?throw_camera_flag.restore_general(saved,restored,b):throw_camera_flag.restore(saved,restored,b,attack_requested,edge_requested,reversal_requested))){delayed_failure_line=__LINE__;fault=true;__leave;}
                    auto n=restored.stream.write_position,m=saved.stream.write_position;
                    restored_byte_differences=n>m?n-m:m-n;
                    restored_byte_differences+=exact_byte_differences(restored.stream.payload.data(),saved.stream.payload.data(),(std::min)(n,m));
                }
            }else{
                combat_comparison_attempted=combat_replay_requested;
                if(!NativeRollbackTiming::measure(NativeRollbackTiming::restore_state_capture,[&]{return capture_blend(b,4);})){delayed_failure_line=__LINE__;fault=true;__leave;}
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
                replay_byte_differences+=exact_byte_differences(replay.stream.payload.data(),after_save.stream.payload.data(),(std::min)(n,m));
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
                    if(reader==reinterpret_cast<Routine>(b+0x384540) && !NativeLoadRoute::ready(saved,b,load_route_diagnostic)){fault=true;__leave;}
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
                        byte_differences+=exact_byte_differences(saved.stream.payload.data(),after_save.stream.payload.data(),(std::min)(n,m));
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

namespace sc6::research {
inline const FullStateProbe& FullStateProbe::reset_defaults(){
    static const auto defaults=std::make_unique<const FullStateProbe>();
    return *defaults;
}
inline void FullStateProbe::reset_for_reuse() noexcept {
    static_assert(std::is_trivially_copyable_v<FullStateProbe>);
    // Copy every default field, including stream tables, copier callbacks and
    // diagnostic sentinels. Only length/count-gated backing storage is skipped.
    // No native references are owned by these byte buffers. Capture overwrites
    // each published prefix before its next use; controls and metadata are reset.
    const auto& defaults=reset_defaults();
    auto* destination=reinterpret_cast<std::byte*>(this);
    const auto* source=reinterpret_cast<const std::byte*>(&defaults);
    size_t cursor=0;
    for(const auto& span:defaults.storage_spans()){
        const auto begin=span.offset;
        std::memcpy(destination+cursor,source+cursor,begin-cursor);
        cursor=begin+span.capacity;
    }
    std::memcpy(destination+cursor,source+cursor,sizeof(*this)-cursor);
}
}

namespace sc6::research {
inline bool FullStateProbe::copy_recorded_from(const FullStateProbe& other) noexcept {
    static_assert(std::is_trivially_copyable_v<FullStateProbe>);
    for(const auto& t:other.transforms)if(t.count>t.records.size())return false;
    const auto spans=other.storage_spans();
    for(const auto& span:spans)if(span.published>span.capacity)return false;
    if(this==&other)return true;
    auto* destination=reinterpret_cast<std::byte*>(this);
    const auto* source=reinterpret_cast<const std::byte*>(&other);
    size_t cursor=0;
    // Preserve every non-record field and every published record byte. Unused
    // capacity is inaccessible through count and is initialized by append.
    for(const auto& span:spans){
        const auto begin=span.offset;
        std::memcpy(destination+cursor,source+cursor,begin-cursor);
        std::memcpy(destination+begin,source+begin,span.published);
        cursor=begin+span.capacity;
    }
    std::memcpy(destination+cursor,source+cursor,sizeof(*this)-cursor);
    return true;
}
inline std::array<FullStateProbe::StorageSpan,22> FullStateProbe::storage_spans() const noexcept {
    std::array<StorageSpan,22> result{};size_t n=0;
    const auto* base=reinterpret_cast<const std::byte*>(this);
    auto add=[&](const auto& buffer,size_t published){
        result[n++]={size_t(reinterpret_cast<const std::byte*>(buffer.data())-base),sizeof(buffer),published};
    };
    for(const auto& selector:selectors)for(const auto& e:selector.entries)add(e.table,e.bytes);
    for(const auto* s:{&saved,&after_save,&replay,&restored})add(s->stream.payload,size_t(s->stream.write_position));
    for(const auto& t:transforms)add(t.records,size_t(t.count)*sizeof(TransformSnapshot::Record));
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.offset<b.offset;});
    return result;
}
}

