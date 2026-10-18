#pragma once
#include "TimerLookupTrace.hpp"
#include "SharedRngTrace.hpp"
#include "InlineRngTrace.hpp"
#include "NativeSnapshotPool.hpp"
#include "NativeAdjacentRestore.hpp"
#include "OwnedWindowRestorePreparation.hpp"
#include "SingleUpdateCall.hpp"
#include "ResourceDependencyCapture.hpp"
#include "GeneratedSnapshotDependency.hpp"
#include "GeneratedBufferFingerprint.hpp"
namespace sc6::research {
// Seed must synchronously publish already-owned descriptor references into the
// active collector. No cached borrowed pointers may be used for acquisition.
struct ReplayDependencySource {
 void* context{};
 bool (*seed)(void*,uint32_t){};
 GeneratedSnapshotDependency* generated{};
 bool (*ready)(void*){};
 NativeEffectSession* effects{};
 EffectExternalSource effect_references{};
 bool available()const{return context && seed && generated && generated->active() && (!ready || ready(context));}
};
struct RollbackBoundaryKey {
    uint64_t session{},peer{},generation{};
    unsigned round{},event{},frame{},slot{},thread{};
    bool same_round(const RollbackBoundaryKey& b)const{
        return session && peer && generation && round && thread && slot>=1 && slot<=2 &&
            session==b.session && peer==b.peer && generation==b.generation && round==b.round &&
            event==b.event && slot==b.slot && thread==b.thread;
    }
};
// Preflight every hop before any loading; never cross identity or frame gaps.
// Separate policy permits failure-order tests without invoking SC6.
struct RollbackWindowPlan {
    enum class Result {invalid,prepare_failed,restore_failed,restored};
    template<class Backend> static Result execute(const std::array<RollbackBoundaryKey,4>& keys,
        unsigned count,const RollbackBoundaryKey& live,Backend& backend){
        if(count<2 || count>4 || !keys[count-1].same_round(live) || keys[count-1].frame!=live.frame)return Result::invalid;
        for(unsigned i=1;i<count;++i)
            if(!keys[i-1].same_round(keys[i]) || keys[i-1].frame==UINT32_MAX || keys[i].frame!=keys[i-1].frame+1)return Result::invalid;
        for(unsigned i=0;i<count-1;++i)if(!backend.prepare(i))return Result::prepare_failed;
        for(unsigned i=count-1;i-- >0;){
            if(!backend.validate_before_load(i) || !backend.restore(i))return Result::restore_failed;
        }
        return Result::restored;
    }
};
// Bounded native history bridge, not installed in the live game hook.
// Captures and reverse-loads existing supported native states only. A rewind
// leaves simulation in the past: the caller MUST finish replay before resuming.
class NativeRollbackWindow {
    template<class,class,class> friend class OwnedPredictionCorrectionBackend;
    using Pool=NativeSnapshotPool<4>;
    Pool pool_;
    std::array<Pool::Handle,4> handles_{};
    std::array<RollbackBoundaryKey,4> keys_{};
    std::array<std::unique_ptr<NativeAdjacentRestore>,3> hops_;
    RecordedInput guard_;
    bool effect_rewind_started_{};
    NativeEffectSession* effect_session_{};
    struct DependencyGuard {bool enabled{};uintptr_t root{},control{},module{};std::array<uintptr_t,2> descriptors{};GeneratedBufferFingerprint fingerprint;};
    std::array<DependencyGuard,4> dependency_guards_{};
    static bool dependency_valid(const DependencyGuard& guard){
        if(!guard.enabled)return true;
        __try {
            if(*reinterpret_cast<uintptr_t*>(guard.module+0x470D0D0)!=guard.descriptors[0] || *reinterpret_cast<uintptr_t*>(guard.module+0x470D0F8)!=guard.descriptors[1])return false;
            const auto descriptor=*reinterpret_cast<uintptr_t*>(guard.root);
            if(descriptor!=guard.descriptors[0] && descriptor!=guard.descriptors[1])return false;
            if(*reinterpret_cast<uintptr_t*>(guard.root+8)!=guard.fingerprint.buffer)return false;
            if(*reinterpret_cast<uintptr_t*>(guard.root+0x430)!=guard.fingerprint.buffer ||
               *reinterpret_cast<uintptr_t*>(guard.root+0x438)!=guard.control)return false;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
        return guard.fingerprint.matches(GeneratedBufferFingerprint::read(guard.fingerprint.buffer,guard.fingerprint.size));
    }
    bool dependencies_valid()const{
        for(unsigned i=0;i<count_;++i)if(!dependency_valid(dependency_guards_[i]))return false;
        return true;
    }
    unsigned count_{};uintptr_t base_{};
    bool fault_{},rewind_attempted_{},rewound_{},replayed_{};
    static bool descriptor_identity(uintptr_t base,std::array<uintptr_t,2>& values){
        __try{values={*reinterpret_cast<uintptr_t*>(base+0x470D0D0),*reinterpret_cast<uintptr_t*>(base+0x470D0F8)};return values[0]!=0;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
public:
    bool defer_derived{};
    uint64_t retained_revivals()const{return effect_session_?effect_session_->revival_count():0;}
    // Classify recorded list removal only. Restore still validates generations,
    // ownership, dependencies and every native hop independently.
    int retained_removal(){
        if(count_!=4 || fault_ || rewind_attempted_ || keys_[0].thread!=GetCurrentThreadId())return -1;
        bool removed=false;
        for(unsigned i=0;i<3;++i){
            const bool ok=pool_.visit(handles_[i],[&](NativeBoundarySnapshot& a){
                return pool_.visit(handles_[3],[&](NativeBoundarySnapshot& b){
                    if(!a.effects || !b.effects || !a.effects->owner || a.effects->owner!=b.effects->owner ||
                       !a.effects->owner->validate(*a.effects) || !b.effects->owner->validate(*b.effects))return false;
                    for(unsigned x=0;x<a.effects->image.count;++x){
                        const auto& saved=a.effects->image.objects[x];bool found=false;
                        for(unsigned y=0;y<b.effects->image.count;++y){const auto& live=b.effects->image.objects[y];
                            found|=saved.address==live.address && saved.generation==live.generation;
                        }
                        removed|=!found;
                    }
                    return true;
                });
            });
            if(!ok)return -1;
        }
        return removed?1:0;
    }
    unsigned last_hop{},capture_stage{},capture_exception{};
    unsigned captured_rng{},captured_rng_after_save{};
    TransformSnapshot::TopologyDifference topology;
    unsigned plan_result{},prepare_stage{},source_frame{},next_frame{},workspace_flags{};
    std::array<uint64_t,5> motion_zero_summary{}; // retained across workspace recycling
    NativeAdjacentRestore& diagnostic(){return *hops_[last_hop];}
    NativeRollbackWindow(){for(auto& hop:hops_)hop=std::make_unique<NativeAdjacentRestore>();}
    bool release_owned_dependencies(){
        if(count_ && keys_[0].thread!=GetCurrentThreadId())return false;
        for(unsigned i=0;i<count_;++i)if(dependency_guards_[i].enabled){if(!pool_.release_dependencies(handles_[i]))return false;dependency_guards_[i]={};}
        return true;
    }
    bool fault()const{return fault_;}
    bool rewind_started()const{return rewind_attempted_ || effect_rewind_started_;}
    bool rewound()const{return rewound_;}
    // Missing confirmation is retryable only before any preparation or load.
    bool discard_unmodified(){
        if(fault_ || rewind_attempted_ || rewound_ || replayed_ || !count_ || keys_[0].thread!=GetCurrentThreadId())return false;
        for(const auto& hop:hops_)if(hop->attempted || hop->fault || hop->probe->restore_invoked)return false;
        for(unsigned i=0;i<count_;++i)if(!pool_.retire(handles_[i])){fault_=true;return false;}
        handles_={};keys_={};dependency_guards_={};count_=0;base_=0;return true;
    }
    // Read-only classification before any workspace preparation or native loading.
    // 0 invalid, 1 stable, 2 changing topology. Never authorizes restoration.
    unsigned topology_readiness(){
        if(fault_ || rewind_attempted_ || rewound_ || replayed_ || count_!=4 || keys_[0].thread!=GetCurrentThreadId())return 0;
        for(unsigned i=0;i<3;++i){
            bool same=false;
            const bool visited=pool_.visit(handles_[i],[&](NativeBoundarySnapshot& a){
                return pool_.visit(handles_[i+1],[&](NativeBoundarySnapshot& b){
                    same=a.data->transforms[0].same_topology(b.data->transforms[0]);
                    if(!same){topology=a.data->transforms[0].topology_difference(b.data->transforms[0],base_);last_hop=i;source_frame=keys_[i].frame;next_frame=keys_[i+1].frame;}
                    return true;
                });
            });
            if(!visited)return 0;
            if(!same)return 2;
        }
        return 1;
    }
    bool any_restore_invoked()const{for(const auto& hop:hops_)if(hop->probe->restore_invoked)return true;return false;}
    static bool verify_effects(NativeBoundarySnapshot& snapshot){return !snapshot.effects ||
        (snapshot.effects->owner && snapshot.effects->owner->compare(*snapshot.effects));}
    // Reference replay only: no new authoritative input or prediction is
    // introduced. This closes the native lifecycle before live integration.
    bool replay_reference(SingleUpdateCall::Routine original){
        if(fault_ || !rewound_ || replayed_ || !original ||
           reinterpret_cast<uintptr_t>(original)==base_+0x2DBC60 ||
           keys_[0].thread!=GetCurrentThreadId())return false;
        if(!dependencies_valid()){fault_=true;return false;}
        for(unsigned i=0;i<count_-1;++i){
            last_hop=i;
            if(!dependencies_valid()){fault_=true;return false;}
            const bool ok=pool_.visit(handles_[i],[&](NativeBoundarySnapshot& source){
                return pool_.visit(handles_[i+1],[&](NativeBoundarySnapshot& reference){
                    auto& hop=*hops_[i];auto& q=*hop.probe;const auto& b=*reference.data;
                    q.after_save=b.saved;q.after_keys=b.before_keys;
                    if(b.blend_timer_count)q.blend_timer_expected=b.blend_timer_history[0];
                    std::memcpy(q.after_players,b.before_players,sizeof(q.after_players));
                    q.original_next=b.measurements[0];q.rings[2]=b.rings[0];
                    q.blends[1]=b.blends[0];q.transforms[1]=b.transforms[0];q.cameras[1]=b.cameras[0];
                    rng_trace::Scope rng_phase(2);
                    SingleUpdateCall call;RecordedInput delivery;
                    if(source.effects && !q.effect_rng_boundary.before(source.data->transforms[0],b.transforms[0],base_))return false;
                    if(!inline_rng::boundary(base_,source.data->measurements[0].rng,false))return false;
                    timer_lookup::boundary(base_,false);
                    shared_rng::record(0,0,keys_[i].frame);
                    if(source.effects && (!reference.effects ||
                        !source.effects->owner->begin_birth_replay(*source.effects,*reference.effects)))return false;
                    const bool ran=call.run(original,delivery.assign(source.data->delayed_inputs),
                        reinterpret_cast<volatile uint32_t*>(base_+0x470D0C4),keys_[i].frame,keys_[i].thread);
                    const bool births_ok=!source.effects || source.effects->owner->end_birth_replay();
                    if(!ran || !births_ok)return false;
                    shared_rng::record(0,3,keys_[i].frame);
                    timer_lookup::boundary(base_,true);
                    if(source.effects)q.effect_rng_boundary.finish(base_);
                    inline_rng::boundary(base_,b.measurements[0].rng,true);
                    hop.replay_delivered=true;
                    const bool matched=hop.replay_step(source,base_,guard_.assign(source.mode==NativeBoundarySnapshot::Mode::general_combat?source.data->delayed_inputs:reference.data->delayed_inputs),true);
                    if(q.motion_zero_evidence[0]){auto total=motion_zero_summary[0]+q.motion_zero_evidence[0];motion_zero_summary=q.motion_zero_evidence;motion_zero_summary[0]=total;}
                    return verify_replay_effects(matched,[&]{return verify_effects(reference);});
                });
            });
            if(!ok){fault_=true;return false;}
        }
        replayed_=true;return true;
    }
    // Keep the immutable oracle and its ownership after a successful reference
    // replay. Fresh workspaces are required before rewinding it a second time.
    bool prepare_prediction_after_reference(){
        if(fault_ || !replayed_ || keys_[0].thread!=GetCurrentThreadId() || !dependencies_valid())return false;
        for(unsigned i=0;i<count_-1;++i)if(!hops_[i]->recycle_after_replay()){fault_=true;return false;}
        rewind_attempted_=effect_rewind_started_=rewound_=replayed_=false;
        return true;
    }
    bool recycle(){
        if(fault_ || !replayed_ || keys_[0].thread!=GetCurrentThreadId())return false;
        for(unsigned i=0;i<count_-1;++i)if(!hops_[i]->recycle_after_replay()){fault_=true;return false;}
        for(unsigned i=0;i<count_;++i)if(!pool_.retire(handles_[i])){fault_=true;return false;}
        handles_={};keys_={};dependency_guards_={};count_=0;base_=0;rewind_attempted_=effect_rewind_started_=rewound_=replayed_=false;
        return true;
    }
    // Only a fully retired window may detach the round's allocation route.
    bool finish_effect_round(){
        if(fault_ || count_ || rewind_attempted_ || rewound_ || replayed_)return false;
        if(effect_session_ && !effect_session_->finish())return false;
        effect_session_=nullptr;return true;
    }
    bool capture(const RollbackBoundaryKey& key,uintptr_t base,uintptr_t argument,
        NativeBoundarySnapshot::Mode mode=NativeBoundarySnapshot::Mode::idle,bool retain_descriptors=false,ReplayDependencySource* ownership=nullptr){
        if(fault_ || rewind_attempted_ || count_==4 || !key.same_round(key) ||
           key.thread!=GetCurrentThreadId() || !base)return false;
        if(count_ && (base!=base_ || !keys_[count_-1].same_round(key) ||
           keys_[count_-1].frame==UINT32_MAX || key.frame!=keys_[count_-1].frame+1))return false;
        if(!pool_.bind_current_thread())return false;
        auto handle=pool_.acquire(key.session,key.round,key.frame,mode,3-key.slot);
        if(!handle){fault_=true;return false;}
        std::optional<ResourceDependencyCapture> dependencies;
        DependencyGuard owned_guard;
        retain_descriptors=retain_descriptors || ownership;
        std::array<uintptr_t,2> descriptor_before{},descriptor_after{};
        if(retain_descriptors){
            if(!descriptor_identity(base,descriptor_before)){pool_.retire(*handle);fault_=true;return false;}
            dependencies.emplace(ResourceDependencyCapture::native_object_dispose,ResourceDependencyCapture::native_control_dispose);
            if(ownership){
                if(!ownership->available() || !ownership->seed(ownership->context,key.frame)){pool_.retire(*handle);fault_=true;return false;}
                const auto& generated=*ownership->generated;
                owned_guard.enabled=true;owned_guard.module=base;owned_guard.descriptors=descriptor_before;owned_guard.root=generated.root;owned_guard.control=generated.control;
                owned_guard.fingerprint=GeneratedBufferFingerprint::read(generated.buffer,generated.size);
                if(!owned_guard.fingerprint.valid || !dependency_valid(owned_guard)){pool_.retire(*handle);fault_=true;return false;}
            }
        }
        const bool ok=pool_.visit(*handle,[&](NativeBoundarySnapshot& snapshot){
            if(mode==NativeBoundarySnapshot::Mode::general_combat && ownership && ownership->effects){
                effect_session_=ownership->effects;
                snapshot.effects=std::make_unique<NativeEffectSession::Binding>();
                if(!ownership->effects->begin(base,key.session,key.round,ownership->effect_references) ||
                   !ownership->effects->capture(*snapshot.effects))return false;
                snapshot.data->external_wind_lifecycle=true;
            }
            const bool captured=snapshot.capture(base,argument);
            capture_stage=snapshot.capture_stage;capture_exception=snapshot.exception;
            captured_rng=snapshot.data->measurements[0].rng;captured_rng_after_save=snapshot.data->measurements[1].rng;
            return captured && snapshot.data->before_keys[0]==key.frame && (!snapshot.effects || ownership->effects->compare(*snapshot.effects));
        });
        if(!ok){pool_.retire(*handle);fault_=true;return false;}
        if(dependencies && (!descriptor_identity(base,descriptor_after) || descriptor_before!=descriptor_after ||
            !dependencies->covers(descriptor_before[0],descriptor_before[1]) || !dependencies->bind(pool_,*handle))){
            pool_.retire(*handle);fault_=true;return false;
        }
        if(ownership && (!dependency_valid(owned_guard) || !ownership->generated->bind(pool_,*handle))){pool_.retire(*handle);fault_=true;return false;}
        dependency_guards_[count_]=owned_guard;
        base_=base;handles_[count_]=*handle;keys_[count_++]=key;return true;
    }
    bool rewind(const RollbackBoundaryKey& live){
        if(fault_ || rewind_attempted_ || live.thread!=GetCurrentThreadId() || !dependencies_valid())return false;
        auto result=RollbackWindowPlan::execute(keys_,count_,live,*this);
        plan_result=static_cast<unsigned>(result)+1;
        if(result==RollbackWindowPlan::Result::invalid)return false;
        if(result!=RollbackWindowPlan::Result::restored){fault_=true;return false;}
        rewound_=true;return true;
    }
private:
    friend struct RollbackWindowPlan;
    // Internal plan callbacks: still guarded by immutable captured handles.
    bool prepare(unsigned i){
        if(count_<2 || i>=count_-1 || fault_)return false;
        last_hop=i;
        prepare_stage=1;source_frame=keys_[i].frame;next_frame=keys_[i+1].frame;
        const auto& hop=*hops_[i];workspace_flags=unsigned(hop.attempted)|(unsigned(hop.fault)<<1)|
            (unsigned(hop.probe->saved.sealed)<<2)|(unsigned(hop.probe->attempted)<<3)|(unsigned(hop.probe->restore_invoked)<<4);
        return pool_.visit(handles_[i],[&](NativeBoundarySnapshot& source){
            prepare_stage=2;
            return pool_.visit(handles_[i+1],[&](NativeBoundarySnapshot& next){
                prepare_stage=3;
                if(source.effects && (!source.effects->owner->validate(*source.effects) || !next.effects ||
                    next.effects->owner!=source.effects->owner || !next.effects->owner->validate(*next.effects)))return false;
                if(!OwnedWindowRestorePreparation::preflight(*hops_[i],source,next)){
                    if(hops_[i]->rejection==NativeBoundarySnapshot::Rejection::transforms)
                        topology=source.data->transforms[0].topology_difference(next.data->transforms[0],base_);
                    return false;
                }
                prepare_stage=4;
                const auto& a=source.data->transforms[0];const auto& b=next.data->transforms[0];
                auto& diagnostic=hops_[i]->probe->wind_diagnostic;
                for(unsigned n=0;n<a.count;++n){const auto& record=a.records[n];
                    if(record.side!=2)continue;
                    diagnostic.record=n;diagnostic.kind=record.kind;diagnostic.size=record.size;
                    if(record.kind==4){const auto vt=TransformSnapshot::pointer(record.bytes.data());
                        diagnostic.vtable_rva=vt>=base_?vt-base_:0;
                        if(vt!=base_+0x3E88CB8 && vt!=base_+0x3E88C88 && vt!=base_+0x3E88D18 && vt!=base_+0x3E88CE8)return false;
                    }
                    if(record.kind==24 && (n>=b.count || !TransformSnapshot::stage_wind_finite(record,b.records[n],&diagnostic)))return false;
                }
                prepare_stage=5;
                return true;
            });
        });
    }
    bool validate_before_load(unsigned i){
        last_hop=i;
        if(count_<2 || i>=count_-1 || fault_ || keys_[0].thread!=GetCurrentThreadId() || !dependencies_valid()){
            prepare_stage=6;return false;
        }
        return true;
    }
    bool restore(unsigned i){
        if(count_<2 || i>=count_-1 || fault_)return false;
        last_hop=i;
        return pool_.visit(handles_[i],[&](NativeBoundarySnapshot& source){
            return pool_.visit(handles_[i+1],[&](NativeBoundarySnapshot& next){
                hops_[i]->probe->defer_inactive_animation_weights=defer_derived;
                if(!OwnedWindowRestorePreparation::before_load(*hops_[i],source,next))return false;
                if(source.data->selector_enabled && !source.data->selectors[0].dependencies(hops_[i]->probe->selector_diagnostic))return false;
                if(source.effects){
                    if(!source.effects->owner->restore(*source.effects)){
                        effect_rewind_started_=source.effects->owner->mutated;return false;
                    }
                    effect_rewind_started_=true;
                }
                rewind_attempted_=true;
                return hops_[i]->restore_captured(source,base_,guard_.assign(next.data->delayed_inputs)) && verify_effects(source);
            });
        });
    }
};
}
