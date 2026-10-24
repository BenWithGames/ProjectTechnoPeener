#pragma once
#include "EffectExternalReferences.hpp"
#include "EffectLifecycleWindow.hpp"
#include "WindEmitterSnapshot.hpp"
namespace sc6::research {
// Owned by ReplayOwnedResources on the game thread; bindings die before the
// source's strong stage references. No worker is allowed to destroy this object.
class NativeEffectSession {
public:
    uint64_t revival_count()const{return runtime_.revivals.load();}
    bool retired_on_owner()const{
        return GetCurrentThreadId()==owner_thread_ && !active_ && !route_published_ && !comparison_pending_.generation &&
            captures.load()==retirements.load() && !failure.load();
    }
    using Runtime=EffectRetentionRuntime<64,32>;
    using Image=NativeEffectImage<64>;
    struct Binding {
        Runtime::Snapshot snapshot{};Image image;WindEmitterSnapshot emitters;EffectExternalReferences references;
        NativeEffectSession* owner{};bool valid{};
        uint64_t birth_window{};unsigned birth_cursor{};
    };
private:
    DWORD owner_thread_=GetCurrentThreadId();
    Runtime runtime_;
    uintptr_t module_{},manager_{};uint64_t session_{},round_{},serial_{};
    EffectExternalSource external_{};
    bool active_{},route_published_{};
    Runtime::Snapshot comparison_pending_{};
    effect_release::NativeRelease native_release_{};
public:
    const EffectBirthJournal& birth_evidence()const{return runtime_.births;}
    // Only the sole rolling-history owner may trim/rebase retained cursors.
    std::atomic<uint64_t> birth_compaction_attempts{},birth_compaction_deferred{};
    std::atomic<unsigned> birth_compaction_result{},birth_compaction_first{},birth_compaction_retained{};
    bool compact_birth_history(unsigned first,unsigned retained,bool& compacted){
        compacted=false;++birth_compaction_attempts;birth_compaction_first=first;birth_compaction_retained=retained;
        if(!active_ || runtime_.stopped() || captures.load()-retirements.load()!=retained){birth_compaction_result=5;return false;}
        const auto result=runtime_.compact_births(first);birth_compaction_result=static_cast<unsigned>(result);
        if(result==EffectBirthJournal::Compaction::deferred){++birth_compaction_deferred;return true;}
        compacted=result==EffectBirthJournal::Compaction::applied;return compacted;
    }
    std::atomic<unsigned> branch_check{},branch_at{},branch_retained{},branch_owned{},branch_mode{},branch_count{};
    bool branch_birth_history(unsigned at,unsigned retained){
        branch_at=at;branch_retained=retained;branch_owned=captures.load()-retirements.load();
        branch_mode=static_cast<unsigned>(runtime_.births.mode);branch_count=runtime_.births.count;
        branch_check=1;if(!active_)return false;
        branch_check=2;if(runtime_.stopped())return false;
        branch_check=3;if(branch_owned.load()!=retained)return false;
        branch_check=4;if(!retained){if(runtime_.births.mode!=EffectBirthJournal::Mode::off)return false;}
        else {branch_check=5;if(!runtime_.branch_births(at))return false;}
        branch_check=0;return true;
    }
    bool begin_birth_replay(const Binding& source,const Binding& next){
        const bool ok=source.owner==this && next.owner==this && source.valid && next.valid &&
            source.birth_window==next.birth_window && !runtime_.stopped() &&
            runtime_.births.begin(source.birth_cursor,next.birth_cursor,session_,round_,source.birth_window);
        if(!ok)failure=8;return ok;
    }
    bool end_birth_replay(){const bool ok=runtime_.births.finish() && !runtime_.stopped();if(!ok)failure=8;return ok;}
    template<class Stream> void retention_report(Stream& out)const{out<<"suite_wind_capture_session_check="<<capture_check.load()<<'\n';runtime_.retention_report(out);
        out<<"suite_effect_begin_reason="<<begin_reason.load()<<"\nsuite_effect_begin_stopped="<<begin_stopped.load()<<'\n';
        out<<"suite_birth_branch_check="<<branch_check.load()<<"\nsuite_birth_branch_at="<<branch_at.load()<<"\nsuite_birth_branch_retained="<<branch_retained.load()
           <<"\nsuite_birth_branch_owned="<<branch_owned.load()<<"\nsuite_birth_branch_mode="<<branch_mode.load()<<"\nsuite_birth_branch_count="<<branch_count.load()<<'\n';
        out<<"suite_birth_compaction_schema=1\nsuite_birth_compaction_attempts="<<birth_compaction_attempts.load()
           <<"\nsuite_birth_compaction_deferred="<<birth_compaction_deferred.load()
           <<"\nsuite_birth_compaction_result="<<birth_compaction_result.load()
           <<"\nsuite_birth_compaction_first="<<birth_compaction_first.load()
           <<"\nsuite_birth_compaction_retained="<<birth_compaction_retained.load()<<'\n';out<<"suite_emitter_schema=1\nsuite_emitter_check="<<emitter_check.load()<<"\nsuite_emitter_count="<<emitter_count.load()<<"\nsuite_emitter_restores="<<emitter_restores.load()<<"\nsuite_emitter_compares="<<emitter_compares.load()<<'\n';}
    // Read after owner retirement; no live object traversal from report worker.
    unsigned retention_result()const{
        if(failure.load() || runtime_.births.failure || captures.load()!=retirements.load() ||
           runtime_.native_deferred.load()!=runtime_.allocator_releases.load())return 2;
        return runtime_.snapshot_protected.load()?1:0;
    }
    struct Evidence {Image expected,actual;unsigned external_failure{};} evidence;
    struct EmitterEvidence {WindEmitterSnapshot expected,actual;} emitter_evidence;
    std::atomic<unsigned> emitter_check{},emitter_restores{},emitter_compares{},emitter_count{};
    void configure(effect_release::NativeRelease release){if(!active_)native_release_=release;}
    std::atomic<unsigned> restore_phase{},failure{},captures{},loads{},comparisons{},retirements{},added{},removed{},mismatch_object{UINT32_MAX},mismatch_offset{UINT32_MAX};
    std::atomic<bool> mutated{};
    std::atomic<unsigned> capture_check{};
    std::atomic<uint64_t> external_check{},external_visits{},external_column{},external_interval{},external_candidate{},external_address{},external_extent{};
    void external_diagnostic(const Binding& b){const auto& d=b.references.diagnostic;external_check=d.check;external_visits=d.visits;external_column=d.column;external_interval=d.interval;external_candidate=d.candidate;external_address=d.address;external_extent=d.extent;}
    std::atomic<unsigned> begin_reason{},begin_stopped{};
    bool begin(uintptr_t module,uint64_t session,uint64_t round,EffectExternalSource external){
        begin_reason=0;begin_stopped=runtime_.stopped();
        if(active_){
            if(module_!=module){begin_reason=1;return false;}
            if(session_!=session){begin_reason=2;return false;}
            if(round_!=round){begin_reason=3;return false;}
            if(runtime_.stopped()){begin_reason=4;return false;}
            return true;
        }
        failure=1;
        if(effect_release::route.load() || !native_release_ || !external.available()){begin_reason=5;return false;}
        EffectEnvironment env;if(!EffectEnvironment::capture(module,env)){begin_reason=6;return false;}
        if(!runtime_.begin(module,session,round,native_release_)){begin_reason=7;return false;}
        if(!runtime_.adopt_live(env.values[2])){begin_reason=8;return false;}
        module_=module;manager_=env.values[2];session_=session;round_=round;external_=external;
        if(!effect_release::publish(runtime_.route())){begin_reason=9;runtime_.finish(true);return false;}
        active_=route_published_=true;failure=0;return true;
    }
    bool capture(Binding& binding){
        failure=2;
        capture_check=1;if(!active_)return false;
        capture_check=2;if(binding.snapshot.generation)return false;
        capture_check=3;if(serial_==UINT64_MAX)return false;
        capture_check=4;if(runtime_.stopped())return false;
        if(captures.load()==retirements.load() &&
           !runtime_.births.start(session_,round_,serial_+1))return false;
        capture_check=5;
        if(!binding.references.capture(module_,external_)){external_diagnostic(binding);failure=100+static_cast<unsigned>(binding.references.failure);return false;}
        external_diagnostic(binding);
        emitter_check=1;
        if(!binding.emitters.capture(module_)){failure=7;emitter_evidence.actual=binding.emitters;return false;}
        emitter_count=binding.emitters.count;emitter_check=0;
        binding.owner=this;
        binding.birth_window=runtime_.births.window;binding.birth_cursor=runtime_.births.count;
        capture_check=6;binding.snapshot=runtime_.capture_image(++serial_,manager_,binding.image);
        if(binding.snapshot.generation)++captures;
        if(!binding.snapshot.generation)return false;
        capture_check=7;binding.valid=binding.references.validate();external_diagnostic(binding);
        if(!binding.valid)return false;
        capture_check=8;if(!runtime_.collect())return false;
        capture_check=0;failure=0;return true;
    }
    bool validate(Binding& binding){
        const bool ok=active_ && binding.owner==this && binding.valid && !runtime_.stopped() && binding.references.validate();
        external_diagnostic(binding);evidence.external_failure=static_cast<unsigned>(binding.references.failure);return ok;
    }
    bool restore(Binding& binding){
        runtime_.births.seal();
        restore_phase=1;failure=3;evidence.expected=binding.image;evidence.actual={};if(!validate(binding))return false;
        restore_phase=2;emitter_evidence.expected=binding.emitters;emitter_check=2;
        if(!emitter_evidence.actual.capture(module_) || !binding.emitters.compatible(emitter_evidence.actual)){failure=7;return false;}
        Image current;
        unsigned add=0,remove=0;
        restore_phase=3;const bool ok=runtime_.apply_effect_image(binding.snapshot,binding.image,current,[&](const Image& saved,const Image& live){
            for(unsigned i=0;i<saved.count;++i){bool found=false;for(unsigned j=0;j<live.count;++j)found|=saved.objects[i].address==live.objects[j].address;if(!found)++add;}
            for(unsigned i=0;i<live.count;++i){bool found=false;for(unsigned j=0;j<saved.count;++j)found|=live.objects[i].address==saved.objects[j].address;if(!found)++remove;}
            return binding.references.validate();
        });
        evidence.actual=current;
        mutated=mutated || ok || runtime_.restart_required();
        if(!ok)return false;
        restore_phase=4;emitter_check=3;
        if(!binding.emitters.restore(module_,emitter_evidence.actual)){failure=7;return false;}
        ++emitter_restores;emitter_check=0;
        added+=add;removed+=remove;++loads;failure=0;restore_phase=0;return true;
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
        emitter_evidence.expected=reference.emitters;emitter_check=4;
        if(!emitter_evidence.actual.capture(module_) || !reference.emitters.compatible(emitter_evidence.actual,true)){failure=7;return false;}
        ++emitter_compares;emitter_check=0;
        ++comparisons;failure=0;return true;
    }
    bool release(Binding& binding){
        if(!binding.snapshot.generation){binding.valid=false;return true;}
        if(binding.owner!=this || !runtime_.expire(binding.snapshot)){failure=5;return false;}
        binding.snapshot={};binding.valid=false;++retirements;
        if(captures.load()==retirements.load())runtime_.births.stop();
        return true;
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
