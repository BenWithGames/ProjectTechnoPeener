#pragma once
#include "EffectExternalReferences.hpp"
#include "EffectLifecycleWindow.hpp"
namespace sc6::research {
// Owned by ReplayOwnedResources on the game thread; bindings die before the
// source's strong stage references. No worker is allowed to destroy this object.
class NativeEffectSession {
public:
    using Runtime=EffectRetentionRuntime<64,32>;
    using Image=NativeEffectImage<64>;
    struct Binding {
        Runtime::Snapshot snapshot{};Image image;EffectExternalReferences references;
        NativeEffectSession* owner{};bool valid{};
    };
private:
    Runtime runtime_;
    uintptr_t module_{},manager_{};uint64_t session_{},round_{},serial_{};
    EffectExternalSource external_{};
    bool active_{},route_published_{};
    Runtime::Snapshot comparison_pending_{};
    effect_release::NativeRelease native_release_{};
public:
    struct Evidence {Image expected,actual;unsigned external_failure{};} evidence;
    void configure(effect_release::NativeRelease release){if(!active_)native_release_=release;}
    std::atomic<unsigned> failure{},captures{},loads{},comparisons{},retirements{},added{},removed{},mismatch_object{UINT32_MAX},mismatch_offset{UINT32_MAX};
    std::atomic<bool> mutated{};
    std::atomic<uint64_t> external_check{},external_visits{},external_column{},external_interval{},external_candidate{},external_address{},external_extent{};
    void external_diagnostic(const Binding& b){const auto& d=b.references.diagnostic;external_check=d.check;external_visits=d.visits;external_column=d.column;external_interval=d.interval;external_candidate=d.candidate;external_address=d.address;external_extent=d.extent;}
    bool begin(uintptr_t module,uint64_t session,uint64_t round,EffectExternalSource external){
        if(active_)return module_==module && session_==session && round_==round && !runtime_.stopped();
        failure=1;
        if(effect_release::route.load() || !native_release_ || !external.available())return false;
        EffectEnvironment env;if(!EffectEnvironment::capture(module,env))return false;
        if(!runtime_.begin(module,session,round,native_release_) || !runtime_.adopt_live(env.values[2]))return false;
        module_=module;manager_=env.values[2];session_=session;round_=round;external_=external;
        if(!effect_release::publish(runtime_.route())){runtime_.finish(true);return false;}
        active_=route_published_=true;failure=0;return true;
    }
    bool capture(Binding& binding){
        failure=2;
        if(!active_ || binding.snapshot.generation || serial_==UINT64_MAX || runtime_.stopped())return false;
        if(!binding.references.capture(module_,external_)){external_diagnostic(binding);failure=100+static_cast<unsigned>(binding.references.failure);return false;}
        external_diagnostic(binding);
        binding.owner=this;
        binding.snapshot=runtime_.capture_image(++serial_,manager_,binding.image);
        if(binding.snapshot.generation)++captures;
        binding.valid=binding.snapshot.generation && binding.references.validate();
        if(!binding.valid)return false;
        if(!runtime_.collect())return false;
        failure=0;return true;
    }
    bool validate(Binding& binding){
        const bool ok=active_ && binding.owner==this && binding.valid && !runtime_.stopped() && binding.references.validate();
        external_diagnostic(binding);evidence.external_failure=static_cast<unsigned>(binding.references.failure);return ok;
    }
    bool restore(Binding& binding){
        failure=3;evidence.expected=binding.image;evidence.actual={};if(!validate(binding))return false;
        Image current;
        unsigned add=0,remove=0;
        const bool ok=runtime_.apply_effect_image(binding.snapshot,binding.image,current,[&](const Image& saved,const Image& live){
            for(unsigned i=0;i<saved.count;++i){bool found=false;for(unsigned j=0;j<live.count;++j)found|=saved.objects[i].address==live.objects[j].address;if(!found)++add;}
            for(unsigned i=0;i<live.count;++i){bool found=false;for(unsigned j=0;j<saved.count;++j)found|=live.objects[i].address==saved.objects[j].address;if(!found)++remove;}
            return binding.references.validate();
        });
        evidence.actual=current;
        mutated=mutated || ok || runtime_.restart_required();
        if(!ok)return false;
        added+=add;removed+=remove;++loads;failure=0;return true;
    }
    bool compare(Binding& reference){
        failure=4;evidence.expected=reference.image;evidence.actual={};if(!validate(reference) || serial_==UINT64_MAX)return false;
        if(comparison_pending_.generation)return false;
        Image current;const auto token=runtime_.capture_image(++serial_,manager_,current);
        evidence.actual=current;
        if(!token.generation)return false;
        unsigned object{},offset{};
        const bool same=EffectLifecycleWindow<64,16>::equivalent(reference.image,current,object,offset);
        mismatch_object=object;mismatch_offset=offset;
        comparison_pending_=token;
        if(!runtime_.expire(token))return false;
        comparison_pending_={};
        if(!same)return false;
        ++comparisons;failure=0;return true;
    }
    bool release(Binding& binding){
        if(!binding.snapshot.generation){binding.valid=false;return true;}
        if(binding.owner!=this || !runtime_.expire(binding.snapshot)){failure=5;return false;}
        binding.snapshot={};binding.valid=false;++retirements;return true;
    }
    bool finish(){
        if(!active_)return true;
        // Called only on the same owner after all snapshots retire. The hook
        // route lives with this session; failed cleanup must preserve it.
        if(comparison_pending_.generation){if(!runtime_.expire(comparison_pending_))return false;comparison_pending_={};}
        if(captures.load()!=retirements.load())return false;
        if(route_published_){if(!effect_release::withdraw(runtime_.route()))return false;route_published_=false;}
        if(!runtime_.finish(true)){failure=6;return false;}
        active_=false;return true;
    }
};
}
