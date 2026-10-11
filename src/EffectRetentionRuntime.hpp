#pragma once
#include "EffectObjectRegistry.hpp"
#include "EffectReleaseBridge.hpp"
#include "EffectReleaseNotice.hpp"
#include "NativeEffectReferences.hpp"
#include "NativeEffectImage.hpp"
#include "EffectRestorePlan.hpp"
#include "EffectImageWriter.hpp"
namespace sc6::research {
// Construct on simulation owner thread; keep alive until hooks are quiescent.
// This does not publish itself or install hooks. Native save/load is not protected
// by this lifecycle; only explicit direct-memory accesses can use the gate.
template<unsigned Capacity=64,unsigned Slots=16> class EffectRetentionRuntime {
    static_assert(Capacity<=64);
public:
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
        for(unsigned i=0;i<count;++i)native_(pending_frees_[i],0x70);
    }
    static bool defer(void* context,uintptr_t address,uintptr_t argument) noexcept {
        auto& self=*static_cast<EffectRetentionRuntime*>(context);
        return self.active_ && argument==0x70 && self.window_.retire(self.registry_.lookup(address));
    }
    static void before_native(void* context,uintptr_t address) noexcept {
        auto& self=*static_cast<EffectRetentionRuntime*>(context);
        self.notices_.before_native(address);
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
    bool allocation(uintptr_t address,std::size_t bytes,uintptr_t caller_rva){
        if(!owner() || !active_ || restart_required_)return false;
        bool accepted=false;
        gate_.try_access(notices_.stopped,[&]{
            uintptr_t* empty=nullptr;for(auto& slot:notices_.addresses)if(!slot){empty=&slot;break;}
            if(empty && registry_.allocation(address,bytes,caller_rva,module_)){*empty=address;accepted=true;}
            else notices_.stopped=true;
        });return accepted;
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
        Snapshot result{};image.valid=false;if(!owner() || !active_)return result;
        gate_.try_access(notices_.stopped,[&]{
            EffectEnvironment before,after;
            if(!environment_reader_(module_,before) || !image.capture(manager,module_) ||
               !environment_reader_(module_,after) || !before.same(after)){image.valid=false;return;}
            image.environment=before;
            image.valid=false;std::array<Token,Capacity> tokens{};
            for(unsigned i=0;i<image.count;++i){auto& object=image.objects[i];
                tokens[i]=registry_.constructed(object.address,object.vtable,true,window_);
                if(!tokens[i].generation)return;
                object.generation=tokens[i].generation;
            }
            result=window_.capture(frame,std::span<const Token>(tokens.data(),image.count));
            image.valid=result.generation!=0;
            if(image.valid){image.snapshot_serial=result.generation;image.snapshot_slot=result.slot;
                image.session=result.session;image.round=result.round;image.frame=result.frame;}
        });return result;
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
        if(!owner() || !active_ || restart_required_ || saved.snapshot_serial!=snapshot.generation ||
           saved.snapshot_slot!=snapshot.slot || saved.session!=snapshot.session || saved.round!=snapshot.round || saved.frame!=snapshot.frame)return false;
        bool applied=false;
        gate_.try_access(notices_.stopped,[&]{
            EffectEnvironment environment;
            if(!window_.snapshot_valid(snapshot) || !environment_reader_(module_,environment) ||
               !saved.environment.same(environment) || !current.capture(saved.manager,module_))return;
            for(unsigned i=0;i<current.count;++i){auto& object=current.objects[i];
                auto token=registry_.constructed(object.address,object.vtable,true,window_);
                if(!token.generation)return;object.generation=token.generation;
            }
            EffectRestorePlan<Capacity> plan;
            if(!plan.prepare(saved,current,module_,[&](uintptr_t address,uint64_t generation){return window_.retained(snapshot,Token{address,generation});}) ||
               !references(saved,current))return;
            // The verifier may observe a transition. Reject before the first write.
            // This recheck is not a substitute for stage-worker synchronization.
            EffectEnvironment before_write;
            if(!environment_reader_(module_,before_write) || !saved.environment.same(before_write))return;
            if(!EffectImageWriter<Capacity>::write(saved,write_exception_)){restart_required_=true;notices_.stopped=true;return;}
            for(unsigned i=0;i<saved.count;++i){const auto& object=saved.objects[i];
                if(!window_.restored(snapshot,{object.address,object.generation})){restart_required_=true;notices_.stopped=true;return;}
            }
            for(unsigned i=0;i<plan.remove_count;++i){const auto& object=current.objects[plan.remove[i]];
                if(!window_.retire({object.address,object.generation})){restart_required_=true;notices_.stopped=true;return;}
            }
            applied=true;
        });return applied;
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
