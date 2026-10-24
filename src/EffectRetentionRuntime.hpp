#pragma once
#include "EffectObjectRegistry.hpp"
#include "EffectReleaseBridge.hpp"
#include "EffectReleaseNotice.hpp"
#include "NativeEffectReferences.hpp"
#include "NativeEffectImage.hpp"
#include "EffectRestorePlan.hpp"
#include "EffectImageWriter.hpp"
#include "EffectBirthJournal.hpp"
namespace sc6::research {
// Construct on simulation owner thread; keep alive until hooks are quiescent.
// This does not publish itself or install hooks. Native save/load is not protected
// by this lifecycle; only explicit direct-memory accesses can use the gate.
template<unsigned Capacity=64,unsigned Slots=16> class EffectRetentionRuntime {
    static_assert(Capacity<=64);
public:
    EffectBirthJournal births;
    // Rolling-history edits share the allocation hook's direct-memory gate.
    // Never wait for worker allocations on the simulation thread.
    std::atomic<unsigned> branch_gate_outcome{},branch_gate_attempts{},branch_gate_accepted{};
    std::atomic<uint64_t> branch_gate_recovered{},branch_gate_exhausted{};
    bool branch_births(unsigned at){
        branch_gate_outcome=branch_gate_attempts=branch_gate_accepted=0;
        if(!owner() || !active_)return false;
        unsigned attempts{};EffectMemoryGate::AccessOutcome outcome{};
        LARGE_INTEGER start{},frequency{};
        const bool accepted=births.resume_bounded(gate_,notices_.stopped,at,[&]{
            LARGE_INTEGER now{};if(!QueryPerformanceCounter(&now))return false;
            if(!start.QuadPart){start=now;if(!QueryPerformanceFrequency(&frequency) || frequency.QuadPart<=0)return false;}
            if(now.QuadPart-start.QuadPart>=frequency.QuadPart/1000)return false;
            SwitchToThread();return true;
        },64,attempts,outcome);
        branch_gate_outcome=static_cast<unsigned>(outcome);branch_gate_attempts=attempts;branch_gate_accepted=accepted;
        if(accepted && attempts>1)++branch_gate_recovered;
        if(outcome==EffectMemoryGate::AccessOutcome::contended)++branch_gate_exhausted;
        return accepted;
    }
    EffectBirthJournal::Compaction compact_births(unsigned first){
        if(!owner() || !active_)return EffectBirthJournal::Compaction::rejected;
        return births.try_discard_prefix(gate_,notices_.stopped,first);
    }
    // Lifetime evidence only. Atomics allow report reads without traversing live objects.
    std::atomic<uint64_t> native_deferred{},snapshot_protected{},allocator_releases{},revivals{};
    template<class Stream> void retention_report(Stream& out)const{
        births.report(out);
        out<<"suite_effect_allocation_schema=1\nsuite_effect_allocation_reason="<<allocation_reason.load()
           <<"\nsuite_effect_allocation_address="<<allocation_address.load()<<"\nsuite_effect_allocation_bytes="<<allocation_bytes.load()
           <<"\nsuite_effect_allocation_caller="<<allocation_caller.load()<<"\nsuite_effect_allocation_thread="<<allocation_thread.load()
           <<"\nsuite_effect_allocation_attempts="<<allocation_attempts.load()<<"\nsuite_effect_allocation_gate="<<allocation_gate.load()
           <<"\nsuite_effect_allocation_recovered="<<allocation_recovered.load()<<"\nsuite_effect_unexpected_release="<<unexpected_release.load()<<'\n';
        out<<"suite_birth_branch_gate_outcome="<<branch_gate_outcome.load()<<"\nsuite_birth_branch_gate_attempts="<<branch_gate_attempts.load()
           <<"\nsuite_birth_branch_gate_accepted="<<branch_gate_accepted.load()<<"\nsuite_birth_branch_gate_recovered="<<branch_gate_recovered.load()
           <<"\nsuite_birth_branch_gate_exhausted="<<branch_gate_exhausted.load()<<'\n';
        out<<"suite_wind_restore_schema=1\nsuite_wind_restore_check="<<restore_check.load()
           <<"\nsuite_wind_restore_object="<<restore_object.load()<<"\nsuite_wind_restore_gate="<<restore_gate_outcome.load()
           <<"\nsuite_wind_restore_attempts="<<restore_attempts.load()<<"\nsuite_wind_restore_image_check="<<restore_image_check.load()
           <<"\nsuite_wind_restore_image_exception="<<restore_image_exception.load()<<"\nsuite_wind_restore_mutation_started="<<restore_mutated.load()
           <<"\nsuite_wind_restore_retry_acquired="<<restore_retry_acquired.load()<<"\nsuite_wind_restore_retry_exhausted="<<restore_retry_exhausted.load()<<'\n';
        out<<"suite_wind_capture_runtime_check="<<capture_check.load()<<"\nsuite_wind_capture_object="<<capture_object.load()
           <<"\nsuite_wind_capture_address="<<capture_address.load()<<"\nsuite_wind_capture_vtable="<<capture_vtable.load()
           <<"\nsuite_wind_capture_image_check="<<image_check.load()<<"\nsuite_wind_capture_image_exception="<<image_exception.load()
           <<"\nsuite_wind_capture_stopped="<<capture_stopped.load()<<"\nsuite_wind_capture_window_failed="<<capture_window_failed.load()<<'\n';
        out<<"suite_wind_gate_schema=1\nsuite_wind_gate_outcome="<<capture_gate_outcome.load()
           <<"\nsuite_wind_gate_thread="<<capture_gate_thread.load()<<"\nsuite_wind_gate_owner="<<capture_gate_owner.load()
           <<"\nsuite_wind_gate_frame="<<capture_gate_frame.load()<<"\nsuite_wind_gate_attempts="<<capture_gate_attempts.load()
           <<"\nsuite_wind_gate_contentions="<<capture_gate_contentions.load()<<"\nsuite_wind_gate_stops="<<capture_gate_stops.load()
           <<"\nsuite_wind_gate_retry_recovered="<<capture_gate_retry_recovered.load()
           <<"\nsuite_wind_gate_retry_exhausted="<<capture_gate_retry_exhausted.load()
           <<"\nsuite_wind_gate_last_attempts="<<capture_gate_last_attempts.load()
           <<"\nsuite_wind_gate_last_rejection="<<capture_gate_last_rejection.load()<<'\n';
        out<<"suite_retention_schema=1\nsuite_retention_native_deferred="<<native_deferred.load()
           <<"\nsuite_retention_snapshot_protected="<<snapshot_protected.load()
           <<"\nsuite_retention_allocator_releases="<<allocator_releases.load()
           <<"\nsuite_retention_revivals="<<revivals.load()<<'\n';
    }
    std::atomic<unsigned> restore_check{},restore_object{},restore_gate_outcome{},restore_attempts{},restore_image_check{},restore_image_exception{},restore_mutated{};
    std::atomic<uint64_t> restore_retry_acquired{},restore_retry_exhausted{};
    std::atomic<uint64_t> capture_gate_retry_recovered{},capture_gate_retry_exhausted{},capture_gate_last_attempts{};
    std::atomic<unsigned> capture_gate_outcome{},capture_gate_thread{},capture_gate_owner{},capture_gate_last_rejection{};
    std::atomic<uint64_t> capture_gate_frame{},capture_gate_attempts{},capture_gate_contentions{},capture_gate_stops{};
    std::atomic<unsigned> capture_check{},capture_object{},image_check{},image_exception{},capture_stopped{},capture_window_failed{};
    std::atomic<uintptr_t> capture_address{},capture_vtable{};
    std::atomic<unsigned> allocation_reason{},allocation_thread{},allocation_attempts{},allocation_gate{};
    std::atomic<uint64_t> allocation_recovered{};
    std::atomic<uintptr_t> allocation_address{},allocation_bytes{},allocation_caller{},unexpected_release{};
    using Registry=EffectObjectRegistry<Capacity,Slots>;
    using Window=typename Registry::Window;
    using Token=typename Window::Token;
    using Snapshot=typename Window::Snapshot;
private:
    using EnvironmentReader=bool(*)(uintptr_t,EffectEnvironment&) noexcept;
    EnvironmentReader environment_reader_=EffectEnvironment::capture;
    DWORD owner_=GetCurrentThreadId();
    uintptr_t module_{};
    bool active_{},restart_required_{};unsigned write_exception_{};
    Window window_;
    Registry registry_;
    EffectReleaseNotice<Capacity> notices_;
    EffectMemoryGate gate_;
    effect_release::NativeRelease native_{};
    std::array<uintptr_t,Capacity> pending_frees_{};
    unsigned free_count_{};
    effect_release::Route route_{};
    bool owner()const noexcept{return owner_==GetCurrentThreadId();}
    void forget_address(uintptr_t address) noexcept {
        for(auto& value:notices_.addresses)if(value==address)value=0;
    }
    void drain() noexcept {
        notices_.drain([&](uintptr_t address) noexcept {
            registry_.unexpected_release(address,window_);forget_address(address);
        });
    }
    void queue_free(typename Window::Allocation allocation) noexcept {
        // At most Capacity tracked allocations can be collected in one operation.
        pending_frees_[free_count_++]=allocation.address;
        registry_.released(allocation.address);forget_address(allocation.address);
    }
    void flush(){
        const auto count=free_count_;free_count_=0;
        for(unsigned i=0;i<count;++i){native_(pending_frees_[i],0x70);++allocator_releases;}
    }
    static bool defer(void* context,uintptr_t address,uintptr_t argument) noexcept {
        auto& self=*static_cast<EffectRetentionRuntime*>(context);
        if(!self.active_ || argument!=0x70)return false;
        const auto token=self.registry_.lookup(address);
        const bool pinned=self.window_.pinned(token);
        if(!self.window_.retire(token))return false;
        ++self.native_deferred;if(pinned)++self.snapshot_protected;return true;
    }
    static void before_native(void* context,uintptr_t address) noexcept {
        auto& self=*static_cast<EffectRetentionRuntime*>(context);
        // Created and destroyed between captures: no snapshot ever pinned it.
        if(self.owner() && self.registry_.contains(address) && !self.registry_.lookup(address).generation){
            self.registry_.released(address);self.forget_address(address);return;
        }
        self.notices_.before_native(address);
        if(self.notices_.stopped.load()){uintptr_t empty=0;self.unexpected_release.compare_exchange_strong(empty,address);}
    }
    static void allocation_return(void* context,uintptr_t address,uintptr_t bytes,uintptr_t caller) {
        auto& self=*static_cast<EffectRetentionRuntime*>(context);
        if(caller<self.module_ || !self.allocation(address,bytes,caller-self.module_))self.notices_.stopped=true;
    }
public:
    bool begin(uintptr_t module,uint64_t session,uint64_t round,effect_release::NativeRelease native,EnvironmentReader environment_reader=EffectEnvironment::capture){
        if(!owner() || active_ || !module || module>UINTPTR_MAX-0x3E88D18 || !native || !environment_reader)return false;
        if(!window_.begin(session,round))return false;
        environment_reader_=environment_reader;module_=module;native_=native;active_=true;
        notices_.stopped=false;notices_.pending=0;notices_.addresses.fill(0);
        route_={owner_,module+0x3340F9,this,&defer,&before_native,&notices_.stopped,&gate_,&allocation_return};
        return true;
    }
    // External hook owner may publish only after all lifetime coverage checks.
    const effect_release::Route* route()const noexcept{return active_?&route_:nullptr;}
    // Seed only a freshly validated complete native list before publishing the
    // route. This adopts existing live allocations, never retired/raw addresses.
    bool adopt_live(uintptr_t manager){
        if(!owner() || !active_ || effect_release::route.load())return false;
        bool ok=false;
        gate_.try_access(notices_.stopped,[&]{
            NativeEffectImage<Capacity> image;if(!image.capture(manager,module_))return;
            for(unsigned i=0;i<image.count;++i){const auto& object=image.objects[i];
                const EffectAllocationSite* site=nullptr;
                for(const auto& candidate:effect_allocation_sites)if(module_+candidate.vtable==object.vtable && candidate.bytes==object.size)site=&candidate;
                if(!site)return;
                if(!registry_.contains(object.address)){
                    uintptr_t* empty=nullptr;for(auto& slot:notices_.addresses)if(!slot){empty=&slot;break;}
                    if(!empty || !registry_.allocation(object.address,object.size,site->caller,module_))return;
                    *empty=object.address;
                }
                if(!registry_.constructed(object.address,object.vtable,true,window_).generation)return;
            }
            ok=true;
        });return ok;
    }
    bool allocation(uintptr_t address,std::size_t bytes,uintptr_t caller_rva){
        LARGE_INTEGER start{},frequency{};
        return allocation_with_retry(address,bytes,caller_rva,[&]{
            LARGE_INTEGER now{};if(!QueryPerformanceCounter(&now))return false;
            if(!start.QuadPart){start=now;if(!QueryPerformanceFrequency(&frequency) || frequency.QuadPart<=0)return false;}
            if(now.QuadPart-start.QuadPart>=frequency.QuadPart/1000)return false;
            SwitchToThread();return true;
        });
    }
    template<class Retry> bool allocation_with_retry(uintptr_t address,std::size_t bytes,uintptr_t caller_rva,Retry&& retry){
        // Allocation has already returned but construction has not started.
        // Retry only brief bookkeeping contention; never an entered rejection,
        // unexpected release, or stale identity. No native call occurs under gate.
        if(notices_.stopped.load())return false;
        allocation_reason=0;allocation_address=address;allocation_bytes=bytes;
        allocation_caller=caller_rva;allocation_thread=GetCurrentThreadId();
        allocation_attempts=0;allocation_gate=0;
        if(!owner() || !active_ || restart_required_){allocation_reason=1;return false;}
        bool accepted=false;unsigned attempts{};EffectMemoryGate::AccessOutcome outcome{};
        gate_.try_access_bounded(notices_.stopped,[&]{
            uintptr_t* empty=nullptr;for(auto& slot:notices_.addresses)if(!slot){empty=&slot;break;}
            if(!empty){allocation_reason=2;notices_.stopped=true;return;}
            if(!registry_.allocation(address,bytes,caller_rva,module_)){allocation_reason=3;notices_.stopped=true;return;}
            *empty=address;accepted=births.allocation(address,bytes,caller_rva);
            if(!accepted){allocation_reason=4;notices_.stopped=true;}
        },retry,64,attempts,outcome);
        allocation_attempts=attempts;allocation_gate=static_cast<unsigned>(outcome);
        if(accepted && attempts>1)++allocation_recovered;
        if(!accepted && !allocation_reason.load())allocation_reason=outcome==EffectMemoryGate::AccessOutcome::contended?5:6;
        return accepted;
    }
    Token constructed(uintptr_t address,uintptr_t vtable,bool verified_list_member){
        Token token{};if(!owner() || !active_)return token;
        gate_.try_access(notices_.stopped,[&]{token=registry_.constructed(address,vtable,verified_list_member,window_);});return token;
    }
    Snapshot capture(uint64_t frame,std::span<const Token> tokens){
        Snapshot result{};if(!owner() || !active_)return result;
        gate_.try_access(notices_.stopped,[&]{result=window_.capture(frame,tokens);});return result;
    }
    // Capture actual transform bytes and bind their complete supported effect
    // list in one memory critical section. No native serializer/load calls here.
    // Reader is injectable for tests; it must only read memory and must not wait.
    template<class Reader> Snapshot capture_effects_with_reader(uint64_t frame,TransformSnapshot& snapshot,Reader&& reader){
        Snapshot result{};if(!owner() || !active_)return result;
        gate_.try_access(notices_.stopped,[&]{
            if(!reader())return;
            NativeEffectReferences<Capacity> manifest;
            if(!manifest.read(snapshot,module_))return;
            std::array<Token,Capacity> tokens{};
            for(unsigned i=0;i<manifest.count;++i){const auto& ref=manifest.references[i];
                tokens[i]=registry_.constructed(ref.address,ref.vtable,true,window_);
                if(!tokens[i].generation)return;
            }
            result=window_.capture(frame,std::span<const Token>(tokens.data(),manifest.count));
        });return result;
    }
    Snapshot capture_effects(uint64_t frame,TransformSnapshot& snapshot,const uintptr_t* owners){
        return capture_effects_with_reader(frame,snapshot,[&]{return snapshot.capture(owners,module_);});
    }
    Snapshot capture_image(uint64_t frame,uintptr_t manager,NativeEffectImage<Capacity>& image){
        Snapshot result{};image.valid=false;
        capture_gate_outcome=0;capture_gate_thread=GetCurrentThreadId();capture_gate_owner=owner_;capture_gate_frame=frame;
        capture_check=1;capture_object=0;capture_address=0;capture_vtable=0;image_check=0;image_exception=0;
        capture_stopped=notices_.stopped.load();capture_window_failed=window_.failed();
        if(!owner() || !active_)return result;
        capture_check=2;
        EffectMemoryGate::AccessOutcome gate_outcome{};unsigned attempts{};
        LARGE_INTEGER retry_start{},retry_frequency{};
        gate_.try_access_bounded(notices_.stopped,[&]{
            EffectEnvironment before,after;
            capture_check=3;if(!environment_reader_(module_,before))return;
            typename NativeEffectImage<Capacity>::CaptureDiagnostic diagnostic;
            capture_check=4;const bool read=image.capture(manager,module_,&diagnostic);
            image_check=diagnostic.check;image_exception=diagnostic.exception;capture_object=diagnostic.index;
            capture_address=diagnostic.address;capture_vtable=diagnostic.vtable;
            if(!read)return;
            capture_check=5;if(!environment_reader_(module_,after))return;
            capture_check=6;if(!before.same(after))return;
            image.environment=before;image.valid=false;std::array<Token,Capacity> tokens{};
            for(unsigned i=0;i<image.count;++i){auto& object=image.objects[i];
                capture_check=7;capture_object=i;capture_address=object.address;capture_vtable=object.vtable;
                tokens[i]=registry_.constructed(object.address,object.vtable,true,window_);
                if(!tokens[i].generation)return;
                object.generation=tokens[i].generation;
            }
            capture_check=8;result=window_.capture(frame,std::span<const Token>(tokens.data(),image.count));
            image.valid=result.generation!=0;
            if(image.valid){image.snapshot_serial=result.generation;image.snapshot_slot=result.slot;
                image.session=result.session;image.round=result.round;image.frame=result.frame;capture_check=0;}
        },[&]{
            // Native allocators/releases are outside this gate. Yield only after
            // contention; cap retries at 64 attempts and a 1 ms QPC budget.
            LARGE_INTEGER now{};if(!QueryPerformanceCounter(&now))return false;
            if(!retry_start.QuadPart){retry_start=now;if(!QueryPerformanceFrequency(&retry_frequency) || retry_frequency.QuadPart<=0)return false;}
            if(now.QuadPart-retry_start.QuadPart>=retry_frequency.QuadPart/1000)return false;
            SwitchToThread();return true;
        },64,attempts,gate_outcome);
        capture_gate_attempts+=attempts;capture_gate_last_attempts=attempts;
        capture_gate_outcome=static_cast<unsigned>(gate_outcome);
        const auto contentions=attempts-(gate_outcome==EffectMemoryGate::AccessOutcome::contended?0u:1u);
        if(contentions){capture_gate_contentions+=contentions;capture_gate_last_rejection=2;}
        if(gate_outcome==EffectMemoryGate::AccessOutcome::entered && attempts>1)++capture_gate_retry_recovered;
        if(gate_outcome==EffectMemoryGate::AccessOutcome::contended)++capture_gate_retry_exhausted;
        if(gate_outcome==EffectMemoryGate::AccessOutcome::stopped){++capture_gate_stops;capture_gate_last_rejection=3;}
        capture_stopped=notices_.stopped.load();capture_window_failed=window_.failed();
        if(!result.generation)image.valid=false;
        return result;
    }

    bool prepare_effect_restore(Snapshot snapshot,const NativeEffectImage<Capacity>& saved,
        NativeEffectImage<Capacity>& current,EffectRestorePlan<Capacity>& plan){
        plan.valid=false;if(!owner() || !active_ || saved.snapshot_serial!=snapshot.generation ||
            saved.snapshot_slot!=snapshot.slot || saved.session!=snapshot.session || saved.round!=snapshot.round || saved.frame!=snapshot.frame)return false;
        bool prepared=false;
        gate_.try_access(notices_.stopped,[&]{
            EffectEnvironment environment;
            if(!window_.snapshot_valid(snapshot) || !environment_reader_(module_,environment) ||
               !saved.environment.same(environment) || !current.capture(saved.manager,module_))return;
            for(unsigned i=0;i<current.count;++i){auto& object=current.objects[i];
                const auto token=registry_.constructed(object.address,object.vtable,true,window_);
                if(!token.generation){current.valid=false;return;}
                object.generation=token.generation;
            }
            prepared=plan.prepare(saved,current,module_,[&](uintptr_t address,uint64_t generation){
                return window_.retained(snapshot,Token{address,generation});
            });
        });return prepared;
    }
    // References must independently prove non-list pointer targets are still
    // valid. There is no production implementation of that verifier yet.
    template<class References> bool apply_effect_image(Snapshot snapshot,const NativeEffectImage<Capacity>& saved,
        NativeEffectImage<Capacity>& current,References&& references){
        restore_check=1;restore_object=UINT32_MAX;restore_gate_outcome=0;restore_attempts=0;
        restore_image_check=0;restore_image_exception=0;restore_mutated=0;
        if(!owner() || !active_ || restart_required_ || saved.snapshot_serial!=snapshot.generation ||
           saved.snapshot_slot!=snapshot.slot || saved.session!=snapshot.session || saved.round!=snapshot.round || saved.frame!=snapshot.frame)return false;
        bool applied=false;restore_check=2;
        EffectMemoryGate::AccessOutcome outcome{};unsigned attempts{};
        LARGE_INTEGER retry_start{},frequency{};
        gate_.try_access_bounded(notices_.stopped,[&]{
            EffectEnvironment environment;
            restore_check=3;if(!window_.snapshot_valid(snapshot))return;
            restore_check=4;if(!environment_reader_(module_,environment))return;
            restore_check=5;if(!saved.environment.same(environment))return;
            typename NativeEffectImage<Capacity>::CaptureDiagnostic diagnostic;
            restore_check=6;const bool captured=current.capture(saved.manager,module_,&diagnostic);
            restore_image_check=diagnostic.check;restore_image_exception=diagnostic.exception;restore_object=diagnostic.index;
            if(!captured)return;
            for(unsigned i=0;i<current.count;++i){auto& object=current.objects[i];
                restore_check=7;restore_object=i;
                auto token=registry_.constructed(object.address,object.vtable,true,window_);
                if(!token.generation)return;object.generation=token.generation;
            }
            EffectRestorePlan<Capacity> plan;
            restore_check=8;
            if(!plan.prepare(saved,current,module_,[&](uintptr_t address,uint64_t generation){return window_.retained(snapshot,Token{address,generation});}))return;
            restore_check=9;if(!references(saved,current))return;
            EffectEnvironment before_write;
            restore_check=10;if(!environment_reader_(module_,before_write))return;
            restore_check=11;if(!saved.environment.same(before_write))return;
            restore_check=12;restore_mutated=1;
            if(!EffectImageWriter<Capacity>::write(saved,write_exception_)){restart_required_=true;notices_.stopped=true;return;}
            for(unsigned i=0;i<saved.count;++i){const auto& object=saved.objects[i];
                restore_check=13;restore_object=i;
                const Token token{object.address,object.generation};const bool was_retired=!window_.live(token);
                if(!window_.restored(snapshot,token)){restart_required_=true;notices_.stopped=true;return;}
                if(was_retired)++revivals;
            }
            for(unsigned i=0;i<plan.remove_count;++i){const auto& object=current.objects[plan.remove[i]];
                restore_check=14;restore_object=plan.remove[i];
                if(!window_.retire({object.address,object.generation})){restart_required_=true;notices_.stopped=true;return;}
            }
            applied=true;restore_check=0;
        },[&]{
            // Only lock contention is retried. An entered transaction, including
            // validation rejection or partial writes, is never executed twice.
            LARGE_INTEGER now{};if(!QueryPerformanceCounter(&now))return false;
            if(!retry_start.QuadPart){retry_start=now;if(!QueryPerformanceFrequency(&frequency) || frequency.QuadPart<=0)return false;}
            if(now.QuadPart-retry_start.QuadPart>=frequency.QuadPart/1000)return false;
            SwitchToThread();return true;
        },64,attempts,outcome);
        restore_attempts=attempts;restore_gate_outcome=static_cast<unsigned>(outcome);
        if(outcome==EffectMemoryGate::AccessOutcome::entered && attempts>1)++restore_retry_acquired;
        if(outcome==EffectMemoryGate::AccessOutcome::contended)++restore_retry_exhausted;
        return applied;
    }
    bool restart_required()const noexcept{return restart_required_;}
    unsigned write_exception()const noexcept{return write_exception_;}
    bool expire(Snapshot snapshot){
        if(!owner() || !active_ || restart_required_)return false;
        bool result=false;
        gate_.release_decision([&]{drain();result=window_.expire(snapshot,[&](typename Window::Allocation allocation) noexcept {queue_free(allocation);});return false;});
        flush();return result;
    }
    bool collect(){
        if(!owner() || !active_ || restart_required_)return false;
        gate_.release_decision([&]{drain();window_.collect([&](typename Window::Allocation allocation) noexcept {queue_free(allocation);});return false;});
        flush();return true;
    }
    bool stopped()const noexcept{return notices_.stopped.load();}
    // Caller has removed the route and waited for all release-hook users to exit.
    bool finish(bool hooks_quiescent){
        if(!owner() || !active_ || restart_required_ || !hooks_quiescent || effect_release::route.load()==&route_)return false;
        bool result=false;
        gate_.release_decision([&]{drain();result=window_.finish([&](typename Window::Allocation allocation) noexcept {queue_free(allocation);});return false;});
        flush();if(result){active_=false;registry_=Registry{};}return result;
    }
};
}
