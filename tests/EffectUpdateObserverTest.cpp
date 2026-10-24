#include "../src/GeneratedOwnerObserver.hpp"
#include "../src/SnapshotDependencyProbe.hpp"
#include "../src/EffectUpdateObserver.hpp"
#include <cassert>
#include <cstdio>
#include <sstream>
#include <latch>
#include <memory>
#include "../src/EffectRetentionRuntime.hpp"
#include "../src/EffectLifecycleWindow.hpp"
#include "../src/NativeEffectSession.hpp"
#include "../src/NativeSnapshotPool.hpp"
#include "../src/NativeRangeCompatibility.hpp"
#include "../src/NativeRangeImage.hpp"
#include "../src/NativeRangeCaptureBridge.hpp"
#include "../src/NativeRangeObserver.hpp"
#include <stdexcept>
#include "../src/EffectReleaseNotice.hpp"
#include "../src/EffectRetentionWindow.hpp"
namespace o=sc6::research::effect_observer;
struct Node {uintptr_t vtable{},unused{},next{};};
static Node a{123,0,0},b{456,0,0};
static uintptr_t head=reinterpret_cast<uintptr_t>(&a);
static uintptr_t range_native(uintptr_t,uintptr_t,uintptr_t){assert(GetLastError()==123);SetLastError(456);return 789;}
static unsigned calls{};
static uintptr_t invoke(){++calls;assert(GetLastError()==1234);head=reinterpret_cast<uintptr_t>(&b);SetLastError(5678);return 9876;}
namespace alloc=sc6::research::effect_allocations;
static unsigned allocations{},releases{};
static uintptr_t allocation_result=4096;
static uintptr_t allocate_mock(uintptr_t bytes){assert(bytes);++allocations;SetLastError(4321);return allocation_result;}
static uintptr_t release_mock(uintptr_t address,uintptr_t size){assert(address==4096 && size==0x70);assert(GetLastError()==1234);++releases;SetLastError(4321);return 99;}
static void allocation_tests(){
    alloc::base=0x10000000;alloc::start();
    assert(alloc::observe_allocate(0x1E0,alloc::base+0x334A1E,&allocate_mock)==4096);
    assert(GetLastError()==4321 && allocations==1 && alloc::count==1 && alloc::events[0].type==1);
    alloc::observe_allocate(0x180,alloc::base+0x2FC720,&allocate_mock);
    assert(alloc::events[1].type==2);
    alloc::observe_allocate(1,0,&allocate_mock);assert(alloc::count==2);
    allocation_result=0;alloc::observe_allocate(1,alloc::base+0x334A1E,&allocate_mock);
    assert(alloc::count==3 && alloc::events[2].type==0);
    for(unsigned i=0;i<3;++i){SetLastError(1234);assert(alloc::observe_release(4096,0x70,alloc::base+0x3340F9,&release_mock)==99);assert(GetLastError()==4321);}
    assert(releases==3 && alloc::count==6);
    const auto path=std::filesystem::temp_directory_path()/"sc6-allocation-observer-test.csv";
    std::ostringstream report;assert(alloc::write(path,report));
    assert(report.str().find("release_unmatched=1\n")!=std::string::npos);
    assert(report.str().find("allocation_failed=1\n")!=std::string::npos);
    std::filesystem::remove(path);
    alloc::observe_allocate(1,alloc::base+0x334A1E,&allocate_mock);assert(alloc::count==6);
    alloc::start();AcquireSRWLockExclusive(&alloc::lock);
    alloc::observe_allocate(1,alloc::base+0x334A1E,&allocate_mock);
    ReleaseSRWLockExclusive(&alloc::lock);assert(alloc::dropped==1);
    alloc::count=static_cast<unsigned>(alloc::events.size());
    alloc::observe_allocate(1,alloc::base+0x334A1E,&allocate_mock);assert(alloc::dropped==2);
    alloc::epoch=0;
}
namespace release_bridge=sc6::research::effect_release;
using Retention=sc6::research::EffectRetentionWindow<2,2>;
struct RetainedFixture {Retention window;Retention::Token token;};
static bool defer_fixture(void* context,uintptr_t address,uintptr_t argument) noexcept {
    auto& f=*static_cast<RetainedFixture*>(context);
    SetLastError(999);
    return address==f.token.address && argument==0x70 && f.window.retire(f.token);
}
static void release_bridge_tests(){
    RetainedFixture fixture;assert(fixture.window.begin(1,1));
    fixture.token=fixture.window.track({4096,123,0x180});
    const std::array tokens{fixture.token};
    const auto snapshot=fixture.window.capture(1,tokens);assert(snapshot.generation);
    const auto caller=alloc::base+0x3340F9;
    std::atomic<bool> stopped{};
    sc6::research::EffectMemoryGate gate;
    const release_bridge::Route route{GetCurrentThreadId(),caller,&fixture,&defer_fixture,[](void*,uintptr_t) noexcept {},&stopped,&gate};
    release_bridge::route.store(&route);
    auto before=releases;
    SetLastError(1234);
    assert(alloc::observe_release(4096,0x70,caller+1,&release_mock)==99 && releases==before+1);
    // Wrong thread must preserve the native release contract and bypass the callback.
    std::thread other([&]{SetLastError(1234);assert(alloc::observe_release(4096,0x70,caller,&release_mock)==99);});other.join();
    assert(releases==before+2);
    SetLastError(1234);
    assert(alloc::observe_release(4096,0x70,caller,&release_mock)==0 && GetLastError()==1234);
    assert(releases==before+2); // Real hook path now owns, but hasn't freed, this fixture.
    auto native_free=[](Retention::Allocation allocation) noexcept {SetLastError(1234);release_mock(allocation.address,0x70);};
    assert(fixture.window.collect(native_free)==0);
    assert(fixture.window.expire(snapshot,native_free) && releases==before+3);
    assert(!fixture.window.expire(snapshot,native_free) && releases==before+3);
    assert(fixture.window.finish(native_free));
    release_bridge::route.store(nullptr);
    SetLastError(1234);assert(alloc::observe_release(4096,0x70,caller,&release_mock)==99 && releases==before+4);
}
using Notices=sc6::research::EffectReleaseNotice<>;
static Notices notices;
static unsigned defer_calls{};
static bool should_not_defer(void*,uintptr_t,uintptr_t) noexcept {++defer_calls;return false;}
static uintptr_t release_after_notice(uintptr_t address,uintptr_t argument){
    assert(address==4096 && argument==0x70 && notices.stopped.load());
    assert(GetLastError()==1234);SetLastError(4321);return 99;
}
static void release_notice_tests(){
    notices.addresses[63]=4096;
    const auto caller=alloc::base+0x3340F9;
    sc6::research::EffectMemoryGate gate;
    const release_bridge::Route selected{GetCurrentThreadId(),caller,nullptr,&should_not_defer,
        [](void*,uintptr_t address) noexcept {notices.before_native(address);SetLastError(888);},&notices.stopped,&gate};
    release_bridge::route.store(&selected);
    notices.before_native(123);assert(!notices.stopped && notices.pending==0);
    std::thread foreign([&]{SetLastError(1234);assert(alloc::observe_release(4096,0x70,caller,&release_after_notice)==99);assert(GetLastError()==4321);});foreign.join();
    assert(defer_calls==0 && notices.pending==(uint64_t{1}<<63));
    SetLastError(1234);assert(alloc::observe_release(4096,0x70,caller,&release_after_notice)==99);
    assert(defer_calls==0); // A stop suppresses future deferral, even on the owner.
    unsigned invalidations=0;
    notices.drain([&](uintptr_t address) noexcept {assert(address==4096);++invalidations;});
    assert(invalidations==1 && notices.pending==0 && notices.stopped);
    notices.drain([&](uintptr_t) noexcept {++invalidations;});assert(invalidations==1);
    release_bridge::route.store(nullptr);
}
static void memory_gate_tests(){
    sc6::research::EffectMemoryGate gate;
    std::atomic<bool> stopped{},release_decided{};
    std::latch entered(1),finish_access(1),release_started(1);
    std::thread restore([&]{assert(gate.try_access(stopped,[&]{entered.count_down();finish_access.wait();assert(!release_decided.load());}));});
    entered.wait();
    sc6::research::EffectMemoryGate::AccessOutcome outcome{};
    assert(!gate.try_access(stopped,[]{assert(false);},&outcome));
    assert(outcome==sc6::research::EffectMemoryGate::AccessOutcome::contended); // Never waits.
    std::thread releaser([&]{release_started.count_down();
        gate.release_decision([&]{stopped.store(true);release_decided=true;return false;});
        // Native free would run here, only after memory access finished.
    });
    release_started.wait();assert(!release_decided.load());finish_access.count_down();
    restore.join();releaser.join();
    assert(release_decided && !gate.try_access(stopped,[]{assert(false);},&outcome));
    assert(outcome==sc6::research::EffectMemoryGate::AccessOutcome::stopped);
    stopped=false;
    try {gate.try_access(stopped,[]{throw std::runtime_error("test");});assert(false);}catch(const std::runtime_error&){}
    assert(gate.try_access(stopped,[]{},&outcome));
    assert(outcome==sc6::research::EffectMemoryGate::AccessOutcome::entered); // Exception unwinds lock.
    // Contention retry is bounded and never repeats an entered callback.
    std::latch held(1),unlock(1);
    std::thread holder([&]{gate.release_decision([&]{held.count_down();unlock.wait();return false;});});
    held.wait();unsigned attempts{},reads{},retries{};
    assert(!gate.try_access_bounded(stopped,[&]{++reads;},[&]{++retries;return true;},4,attempts,outcome));
    assert(attempts==4 && retries==3 && reads==0 && outcome==sc6::research::EffectMemoryGate::AccessOutcome::contended);
    assert(gate.try_access_bounded(stopped,[&]{++reads;},[&]{unlock.count_down();holder.join();return true;},4,attempts,outcome));
    assert(attempts==2 && reads==1 && outcome==sc6::research::EffectMemoryGate::AccessOutcome::entered);
    stopped=true;retries=0;
    assert(!gate.try_access_bounded(stopped,[&]{++reads;},[&]{++retries;return true;},4,attempts,outcome));
    assert(attempts==1 && reads==1 && retries==0 && outcome==sc6::research::EffectMemoryGate::AccessOutcome::stopped);
    stopped=false;bool read_valid=false;
    assert(gate.try_access_bounded(stopped,[&]{++reads;read_valid=false;},[&]{++retries;return true;},4,attempts,outcome));
    assert(attempts==1 && !read_valid && reads==2 && retries==0);
    assert(!gate.release_decision([]{return false;}));
    assert(gate.try_access(stopped,[]{})); // No lock left for native free to run under.
}
static unsigned environment_epoch=1;
static bool fixture_environment(uintptr_t module,sc6::research::EffectEnvironment& environment) noexcept {
    environment.valid=true;environment.values={module,environment_epoch,0,0,0,0};return true;
}
static unsigned runtime_frees{};
static uintptr_t runtime_native(uintptr_t address,uintptr_t argument){assert(address==4096 && argument==0x70);++runtime_frees;return 88;}
static void runtime_tests(){
    using Runtime=sc6::research::EffectRetentionRuntime<2,2>;
    {
        Runtime contended;assert(contended.begin(alloc::base,11,12,&runtime_native,&fixture_environment));
        std::atomic<bool> locked{},release{};
        std::thread worker([&]{contended.route()->gate->release_decision([&]{
            locked=true;while(!release.load())std::this_thread::yield();return false;});});
        while(!locked.load())std::this_thread::yield();
        unsigned retries{};
        assert(contended.allocation_with_retry(4096,0x180,0x2FC720,[&]{++retries;release=true;worker.join();return true;}));
        assert(retries==1 && contended.allocation_recovered==1 && contended.allocation_attempts==2 && !contended.stopped());
        assert(!contended.allocation_with_retry(4096,0x180,0x2FC720,[&]{assert(false);return true;}));
        assert(contended.stopped() && contended.allocation_reason==3); // overlapping allocation is never retried
    }
    Runtime runtime;assert(runtime.begin(alloc::base,1,1,&runtime_native,&fixture_environment));
    release_bridge::route.store(runtime.route());
    allocation_result=4096;
    alloc::observe_allocate(0x180,alloc::base+0x2FC720,&allocate_mock);
    assert(!runtime.constructed(4096,alloc::base+0x3E88D18,false).generation);
    auto token=runtime.constructed(4096,alloc::base+0x3E88D18,true);assert(token.generation);
    std::array tokens{token};auto snapshot=runtime.capture(1,tokens);assert(snapshot.generation);
    assert(alloc::observe_release(4096,0x70,alloc::base+0x3340F9,&runtime_native)==0);
    assert(runtime_frees==0 && runtime.collect() && runtime_frees==0);
    assert(runtime.native_deferred==1 && runtime.snapshot_protected==1 && runtime.allocator_releases==0);
    assert(!runtime.finish(true)); // Cannot retire a published route.
    assert(runtime.expire(snapshot) && runtime_frees==1);
    assert(runtime.allocator_releases==1 && runtime.revivals==0);
    assert(!runtime.expire(snapshot) && runtime_frees==1 && runtime.allocator_releases==1);
    release_bridge::route.store(nullptr);assert(!runtime.finish(false));assert(runtime.finish(true));
    assert(runtime.begin(alloc::base,2,1,&runtime_native,&fixture_environment));release_bridge::route.store(runtime.route());
    alloc::observe_allocate(0x180,alloc::base+0x2FC720,&allocate_mock);
    auto reused=runtime.constructed(4096,alloc::base+0x3E88D18,true);assert(reused.generation!=token.generation);
    std::array next_tokens{reused};auto next=runtime.capture(1,next_tokens);assert(next.generation);
    std::thread foreign([&]{assert(alloc::observe_release(4096,0x70,alloc::base+0x3340F9,&runtime_native)==88);});foreign.join();
    assert(runtime.stopped() && runtime_frees==2 && !runtime.capture(2,next_tokens).generation);
    assert(runtime.collect()); // Drains owner invalidation, without freeing it twice.
    assert(!runtime.expire(next) && runtime_frees==2);
    release_bridge::route.store(nullptr);assert(runtime.finish(true) && runtime_frees==2);
}
static void birth_hook_tests(){
    using Runtime=sc6::research::EffectRetentionRuntime<4,2>;
    Runtime runtime;assert(runtime.begin(alloc::base,1,1,&runtime_native,&fixture_environment));
    assert(runtime.births.start(1,1,1));release_bridge::route.store(runtime.route());
    std::array<std::byte,0x1E0> original{},replay{};
    const uint32_t seed=0xc412fdba;std::memcpy(original.data()+0x138,&seed,4);
    allocation_result=reinterpret_cast<uintptr_t>(original.data());
    alloc::observe_allocate(0x1E0,alloc::base+0x334A1E,&allocate_mock);
    assert(GetLastError()==4321 && runtime.births.count==1 && !runtime.stopped());
    runtime.births.seal();assert(runtime.births.begin(0,1,1,1,1));
    allocation_result=reinterpret_cast<uintptr_t>(replay.data());
    alloc::observe_allocate(0x1E0,alloc::base+0x334A1E,&allocate_mock);
    assert(GetLastError()==4321 && runtime.births.finish());
    assert(std::memcmp(original.data()+0x138,replay.data()+0x138,4)==0);
    // A foreign thread cannot apply a recorded seed to arbitrary memory.
    std::array<std::byte,0x1E0> foreign{};assert(runtime.births.begin(0,1,1,1,1));
    allocation_result=reinterpret_cast<uintptr_t>(foreign.data());
    std::thread other([&]{alloc::observe_allocate(0x1E0,alloc::base+0x334A1E,&allocate_mock);});other.join();
    uint32_t word{};std::memcpy(&word,foreign.data()+0x138,4);assert(word==0 && runtime.stopped());
    assert(!runtime.births.finish());release_bridge::route.store(nullptr);
    runtime.births.stop();assert(runtime.finish(true));allocation_result=4096;
}
static void effect_manifest_tests(){
    auto snapshot=std::make_unique<sc6::research::TransformSnapshot>();
    auto& records=snapshot->records;snapshot->valid=true;snapshot->count=3;
    records[0].kind=3;records[0].side=2;records[0].size=16;records[0].address=0x8000;
    records[1].kind=4;records[1].side=2;records[1].size=24;records[1].address=4096;
    records[2].kind=23;records[2].side=2;records[2].size=0x168;records[2].address=4096+24;records[2].parent=1;
    uintptr_t address=4096,type=alloc::base+0x3E88D18;
    std::memcpy(records[0].bytes.data(),&address,8);std::memcpy(records[1].bytes.data(),&type,8);
    sc6::research::NativeEffectReferences<> manifest;
    assert(manifest.read(*snapshot,alloc::base) && manifest.count==1);
    records[2].size--;assert(!manifest.read(*snapshot,alloc::base));records[2].size++;
    records[2].parent=0;assert(!manifest.read(*snapshot,alloc::base));records[2].parent=1;
    std::memcpy(records[1].bytes.data()+16,&address,8);assert(!manifest.read(*snapshot,alloc::base));
    uintptr_t zero=0;std::memcpy(records[1].bytes.data()+16,&zero,8);
    type=alloc::base+0x3E88CB8;std::memcpy(records[1].bytes.data(),&type,8);assert(!manifest.read(*snapshot,alloc::base));
    type=alloc::base+0x3E88D18;std::memcpy(records[1].bytes.data(),&type,8);
    using Runtime=sc6::research::EffectRetentionRuntime<2,2>;
    Runtime runtime;assert(runtime.begin(alloc::base,1,1,&runtime_native,&fixture_environment));
    assert(!runtime.capture_effects_with_reader(1,*snapshot,[]{return true;}).generation); // No allocation evidence.
    assert(runtime.allocation(4096,0x180,0x2FC720));
    assert(!runtime.capture_effects_with_reader(1,*snapshot,[]{return false;}).generation);
    auto retained=runtime.capture_effects_with_reader(1,*snapshot,[]{return true;});assert(retained.generation);
    release_bridge::route.store(runtime.route());const auto before=runtime_frees;
    assert(alloc::observe_release(4096,0x70,alloc::base+0x3340F9,&runtime_native)==0 && runtime_frees==before);
    assert(runtime.expire(retained) && runtime_frees==before+1);
    release_bridge::route.store(nullptr);assert(runtime.finish(true));
}
static void effect_image_tests(){
    std::array<std::byte,0x1E0> object{};
    uintptr_t image_head=reinterpret_cast<uintptr_t>(object.data()),manager=reinterpret_cast<uintptr_t>(&image_head);
    auto store=[&](unsigned offset,uintptr_t value){std::memcpy(object.data()+offset,&value,8);};
    sc6::research::NativeEffectImage<2> image;
    for(const auto& site:sc6::research::effect_allocation_sites){
        object.fill(std::byte{});store(0,alloc::base+site.vtable);store(0x28,manager);
        object[site.bytes-1]=std::byte{0x5A};
        assert(image.capture(manager,alloc::base) && image.count==1 && image.objects[0].size==site.bytes);
        assert(image.objects[0].bytes[site.bytes-1]==std::byte{0x5A});
    }
    store(0x18,image_head);assert(!image.capture(manager,alloc::base));store(0x18,0);
    store(0x10,image_head);assert(!image.capture(manager,alloc::base));store(0x10,0);
    store(0x28,manager+8);assert(!image.capture(manager,alloc::base));store(0x28,manager);
    assert(!image.capture(1,alloc::base) && image.exception);
    using Runtime=sc6::research::EffectRetentionRuntime<2,2>;
    Runtime runtime;assert(runtime.begin(alloc::base,1,1,&runtime_native,&fixture_environment));
    assert(!runtime.capture_image(1,manager,image).generation && !image.valid);
    assert(runtime.capture_check==7 && runtime.capture_address==image_head);
    assert(runtime.allocation(image_head,0x130,0x3345B4));
    auto snapshot=runtime.capture_image(1,manager,image);
    assert(snapshot.generation && image.valid && image.objects[0].generation && runtime.capture_check==0);
    assert(!runtime.capture_image(1,manager,image).generation && runtime.capture_check==8);
    assert(!runtime.capture_image(2,1,image).generation && runtime.capture_check==4 && runtime.image_exception!=0);
    // Restore the valid diagnostic image after intentionally rejected captures.
    assert(runtime.expire(snapshot));snapshot=runtime.capture_image(3,manager,image);
    assert(snapshot.generation && runtime.capture_check==0 && runtime.image_check==0);
    sc6::research::NativeEffectImage<2> current;
    sc6::research::EffectRestorePlan<2> plan;
    assert(runtime.prepare_effect_restore(snapshot,image,current,plan) && plan.preserve_count==1);
    image.frame++;assert(!runtime.prepare_effect_restore(snapshot,image,current,plan));image.frame--;
    assert(runtime.expire(snapshot));assert(!runtime.prepare_effect_restore(snapshot,image,current,plan));
    assert(runtime.finish(true)); // Still live, no native free.
}
static void restore_plan_tests(){
    using Image=sc6::research::NativeEffectImage<2>;
    Image saved,current;
    auto make=[](Image& image,uintptr_t address,uint64_t generation){
        image.valid=true;image.manager=0x9000;image.head=address;image.count=1;
        auto& object=image.objects[0];object.address=address;object.generation=generation;
        object.vtable=alloc::base+0x3E88CB8;object.size=0x130;
        std::memcpy(object.bytes.data(),&object.vtable,8);std::memcpy(object.bytes.data()+0x28,&image.manager,8);
    };
    make(saved,0x1000,1);make(current,0x2000,2);
    sc6::research::EffectRestorePlan<2> plan;
    assert(plan.prepare(saved,current,alloc::base,[](uintptr_t address,uint64_t generation){return address==0x1000 && generation==1;}));
    assert(plan.revive_count==1 && plan.remove_count==1 && plan.preserve_count==0);
    assert(!plan.prepare(saved,current,alloc::base,[](uintptr_t,uint64_t){return false;}));
    current=saved;assert(plan.prepare(saved,current,alloc::base,[](uintptr_t,uint64_t){return true;}));
    assert(plan.preserve_count==1 && !plan.revive_count && !plan.remove_count);
    current.objects[0].generation++;assert(!plan.prepare(saved,current,alloc::base,[](uintptr_t,uint64_t){return true;}));
    make(current,0x1008,2);assert(!plan.prepare(saved,current,alloc::base,[](uintptr_t,uint64_t){return true;}));
    current=saved;current.manager++;assert(!plan.prepare(saved,current,alloc::base,[](uintptr_t,uint64_t){return true;}));
    current=saved;current.objects[0].size--;assert(!plan.prepare(saved,current,alloc::base,[](uintptr_t,uint64_t){return true;}));
    current=saved;uintptr_t bad=0x3333;std::memcpy(current.objects[0].bytes.data()+0x10,&bad,8);
    assert(!plan.prepare(saved,current,alloc::base,[](uintptr_t,uint64_t){return true;}));
}
static unsigned image_frees{};
static uintptr_t last_image_free{};
static uintptr_t image_native_free(uintptr_t address,uintptr_t argument){assert(argument==0x70);++image_frees;last_image_free=address;return 0;}
static void apply_image_tests(){
    using Runtime=sc6::research::EffectRetentionRuntime<2,2>;
    using Image=sc6::research::NativeEffectImage<2>;
    std::array<std::byte,0x130> older{},newer{};
    const auto old_address=reinterpret_cast<uintptr_t>(older.data()),new_address=reinterpret_cast<uintptr_t>(newer.data());
    uintptr_t image_head=old_address;const auto manager=reinterpret_cast<uintptr_t>(&image_head);
    const uintptr_t type=alloc::base+0x3E88CB8;
    for(auto* bytes:{older.data(),newer.data()}){std::memcpy(bytes,&type,8);std::memcpy(bytes+0x28,&manager,8);}
    older[0x12F]=std::byte{42};Runtime runtime;assert(runtime.begin(alloc::base,1,1,&image_native_free,&fixture_environment));
    assert(runtime.allocation(old_address,0x130,0x3345B4));Image saved,current;
    auto snapshot=runtime.capture_image(1,manager,saved);assert(snapshot.generation);
    release_bridge::route.store(runtime.route());
    image_head=0;assert(alloc::observe_release(old_address,0x70,alloc::base+0x3340F9,&image_native_free)==0 && image_frees==0);
    older[0x12F]=std::byte{99};
    assert(runtime.allocation(new_address,0x130,0x3345B4));image_head=new_address;
    std::latch restore_held(1),restore_unlock(1);
    std::thread restore_holder([&]{runtime.route()->gate->release_decision([&]{restore_held.count_down();restore_unlock.wait();return false;});});
    restore_held.wait();unsigned restore_callbacks=0;
    assert(!runtime.apply_effect_image(snapshot,saved,current,[&](const Image&,const Image&){++restore_callbacks;return true;}));
    assert(runtime.restore_check==2 && runtime.restore_gate_outcome==2 && runtime.restore_attempts>0 && runtime.restore_attempts<=64);
    assert(runtime.restore_retry_exhausted==1 && !runtime.restore_mutated && !restore_callbacks);
    assert(image_head==new_address && older[0x12F]==std::byte{99});
    restore_unlock.count_down();restore_holder.join();
    assert(!runtime.apply_effect_image(snapshot,saved,current,[](const Image&,const Image&){return false;}));
    assert(runtime.restore_check==9 && !runtime.restore_mutated && runtime.restore_attempts==1);
    assert(image_head==new_address && older[0x12F]==std::byte{99});
    ++environment_epoch;
    assert(!runtime.apply_effect_image(snapshot,saved,current,[](const Image&,const Image&){return true;}));
    assert(runtime.restore_check==5 && !runtime.restore_mutated);
    assert(image_head==new_address && older[0x12F]==std::byte{99});--environment_epoch;
    assert(!runtime.apply_effect_image(snapshot,saved,current,[](const Image&,const Image&){++environment_epoch;return true;}));
    assert(runtime.restore_check==11 && !runtime.restore_mutated);
    assert(image_head==new_address && older[0x12F]==std::byte{99} && image_frees==0);--environment_epoch;
    assert(runtime.apply_effect_image(snapshot,saved,current,[](const Image&,const Image&){return true;}));
    assert(runtime.restore_check==0 && runtime.restore_mutated);
    assert(image_head==old_address && older[0x12F]==std::byte{42} && image_frees==0);
    assert(runtime.native_deferred==1 && runtime.snapshot_protected==1 && runtime.revivals==1 && runtime.allocator_releases==0);
    assert(runtime.collect() && image_frees==1 && last_image_free==new_address && runtime.allocator_releases==1);
    assert(runtime.expire(snapshot) && image_frees==1); // Revived old object stays native-owned.
    assert(!runtime.apply_effect_image(snapshot,saved,current,[](const Image&,const Image&){return true;}));
    release_bridge::route.store(nullptr);assert(runtime.finish(true));
}
static void failed_image_write_test(){
    using Runtime=sc6::research::EffectRetentionRuntime<2,2>;
    using Image=sc6::research::NativeEffectImage<2>;
    std::array<std::byte,0x130> bytes{};
    auto* manager=static_cast<uintptr_t*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(manager);
    *manager=reinterpret_cast<uintptr_t>(bytes.data());
    const uintptr_t type=alloc::base+0x3E88CB8,manager_address=reinterpret_cast<uintptr_t>(manager);
    std::memcpy(bytes.data(),&type,8);std::memcpy(bytes.data()+0x28,&manager_address,8);
    Runtime runtime;assert(runtime.begin(alloc::base,1,1,&image_native_free,&fixture_environment));
    assert(runtime.allocation(*manager,0x130,0x3345B4));Image saved,current;
    auto snapshot=runtime.capture_image(1,manager_address,saved);assert(snapshot.generation);
    DWORD previous{};assert(VirtualProtect(manager,4096,PAGE_READONLY,&previous));
    assert(!runtime.apply_effect_image(snapshot,saved,current,[](const Image&,const Image&){return true;}));
    assert(runtime.restore_check==12 && runtime.restore_mutated && runtime.restore_attempts==1);
    assert(runtime.restart_required() && runtime.stopped() && runtime.write_exception());
    assert(!runtime.collect() && !runtime.expire(snapshot) && !runtime.finish(true));
    DWORD ignored{};assert(VirtualProtect(manager,4096,previous,&ignored));assert(VirtualFree(manager,0,MEM_RELEASE));
}
static void wind_lifecycle_window_tests(){
    using Lifecycle=sc6::research::EffectLifecycleWindow<4,4>;
    using Image=Lifecycle::Image;
    Lifecycle::Runtime runtime;
    std::array<std::byte,0x1E0> original{},created{},recreated{};
    const auto old_address=reinterpret_cast<uintptr_t>(original.data()),new_address=reinterpret_cast<uintptr_t>(created.data()),replay_address=reinterpret_cast<uintptr_t>(recreated.data());
    uintptr_t wind_head=old_address;const auto manager=reinterpret_cast<uintptr_t>(&wind_head);
    const uintptr_t type=alloc::base+0x3E88CE8;
    auto construct=[&](auto& bytes,uintptr_t next,uintptr_t previous){
        bytes.fill(std::byte{});std::memcpy(bytes.data(),&type,8);
        std::memcpy(bytes.data()+0x10,&next,8);std::memcpy(bytes.data()+0x18,&previous,8);
        std::memcpy(bytes.data()+0x28,&manager,8);
    };
    construct(original,0,0);image_frees=0;
    assert(runtime.begin(alloc::base,7,9,&image_native_free,&fixture_environment));
    assert(runtime.allocation(old_address,0x1E0,0x334A1E));
    Lifecycle history(runtime);assert(history.capture(10,manager));
    assert(runtime.allocation(new_address,0x1E0,0x334A1E));construct(created,old_address,0);
    wind_head=new_address;std::memcpy(original.data()+0x18,&new_address,8);
    assert(history.capture(11,manager));
    release_bridge::route.store(runtime.route());
    const auto references=[](const Image&,const Image&){return true;}; // Fixture has no external references.
    assert(history.restore(0,references) && wind_head==old_address && image_frees==0);
    assert(history.verify(0,100));
    assert(runtime.collect() && image_frees==0); // B remains pinned by frame11.
    assert(history.restore(1,references) && wind_head==new_address && image_frees==0);
    assert(history.verify(1,101));
    // Rewind again, then emulate native replay creating B at old_address fresh address.
    assert(history.restore(0,references));
    assert(runtime.allocation(replay_address,0x1E0,0x334A1E));construct(recreated,old_address,0);
    wind_head=replay_address;std::memcpy(original.data()+0x18,&replay_address,8);
    assert(history.verify(1,102)); // Link addresses differ; payload is identical.
    assert(history.diagnostic.restores==3 && history.diagnostic.verified==3);
    assert(history.diagnostic.removed==2 && history.diagnostic.added==1);
    assert(history.retire() && image_frees==1 && last_image_free==new_address);
    assert(history.retire() && image_frees==1);
    release_bridge::route.store(nullptr);assert(runtime.finish(true));
    // Exact payload differences must not be mistaken for address relocation.
    Image expected,actual;assert(expected.capture(manager,alloc::base));actual=expected;
    fixture_environment(alloc::base,expected.environment);actual.environment=expected.environment;
    for(unsigned i=0;i<expected.count;++i)expected.objects[i].generation=actual.objects[i].generation=i+1;
    actual.objects[0].bytes[0x30]=std::byte{1};unsigned object{},offset{};
    assert(!Lifecycle::equivalent(expected,actual,object,offset) && object==0 && offset==0x30);
    actual=expected;actual.count=5;assert(!Lifecycle::equivalent(expected,actual,object,offset));
    actual=expected;uintptr_t bad_link=123;std::memcpy(actual.objects[0].bytes.data()+0x10,&bad_link,8);
    assert(!Lifecycle::equivalent(expected,actual,object,offset)); // Never mask broken linkage.
    // Exhaustive independent gap oracle: every active byte must still reject.
    const auto gap=[](unsigned j){return (j>=8 && j<16)||(j>=34 && j<40)||
        (j>=52 && j<64)||(j>=244 && j<248)||(j>=284 && j<288)||
        (j>=324 && j<328)||(j>=332 && j<336);};
    for(unsigned j=0;j<0x1E0;++j){
        actual=expected;actual.objects[0].bytes[j]^=std::byte{1};
        assert(Lifecycle::equivalent(expected,actual,object,offset)==gap(j));
        assert(actual.objects[0].bytes[j]==(expected.objects[0].bytes[j]^std::byte{1}));
        assert(Lifecycle::audited_gap(alloc::base,type,0x1E0,j)==gap(j));
        assert(!Lifecycle::audited_gap(alloc::base,type,0x180,j));
        assert(!Lifecycle::audited_gap(alloc::base,type+1,0x1E0,j));
        assert(!Lifecycle::audited_gap(alloc::base+1,type,0x1E0,j));
        assert(!Lifecycle::audited_gap(type+1,type,0x1E0,j));
    }
    actual=expected;
    for(unsigned j=0;j<0x1E0;++j)if(gap(j))actual.objects[0].bytes[j]^=std::byte{0xFF};
    assert(Lifecycle::equivalent(expected,actual,object,offset));
    actual.objects[0].bytes[0x148]^=std::byte{1};
    assert(!Lifecycle::equivalent(expected,actual,object,offset) && offset==0x148);
    for(const auto other: {uintptr_t{0x3E88D18},uintptr_t{0x3E88C88},uintptr_t{0x3E88CB8}})
        for(unsigned j=0;j<0x1E0;++j)assert(!Lifecycle::audited_gap(alloc::base,alloc::base+other,0x1E0,j));
    // Every supported layout: exhaustive payload mutation with independent
    // fixture ranges, including type-specific overlap traps (F4, 110, 144).
    for(const auto& site:sc6::research::effect_allocation_sites){
        Image baseline{};baseline.valid=true;baseline.manager=manager;
        baseline.environment=expected.environment;baseline.count=1;baseline.head=old_address;
        auto& record=baseline.objects[0];record.address=old_address;record.generation=1;
        record.vtable=alloc::base+site.vtable;record.size=static_cast<unsigned>(site.bytes);
        std::memcpy(record.bytes.data(),&record.vtable,8);
        std::memcpy(record.bytes.data()+0x28,&manager,8);
        assert(Lifecycle::equivalent(baseline,baseline,object,offset));
        const auto allowed=[&](unsigned j){
            const bool common=(j>=8 && j<16)||(j>=34 && j<40)||(j>=52 && j<64);
            if(site.type==1)return gap(j);
            if(site.type==2)return common||(j>=228 && j<240)||(j>=272 && j<288)||(j>=300 && j<304);
            return common||(j>=224 && j<288)||(j>=300 && j<304);
        };
        Image all=baseline;
        for(unsigned j=0;j<record.size;++j){
            Image changed=baseline;changed.objects[0].bytes[j]^=std::byte{0xFF};
            assert(Lifecycle::equivalent(baseline,changed,object,offset)==allowed(j));
            assert(changed.objects[0].bytes[j]==std::byte{0xFF} ||
                   changed.objects[0].bytes[j]==(baseline.objects[0].bytes[j]^std::byte{0xFF}));
            if(allowed(j))all.objects[0].bytes[j]^=std::byte{0xFF};
            assert(!Lifecycle::audited_gap(alloc::base,record.vtable,record.size+1,j));
        }
        assert(Lifecycle::equivalent(baseline,all,object,offset));
        all.objects[0].bytes[0x50]^=std::byte{1};
        assert(!Lifecycle::equivalent(baseline,all,object,offset) && offset==0x50);
    }
    // Refusing external ownership must leave both list and payload untouched.
    Lifecycle::Runtime guarded_runtime;wind_head=old_address;construct(original,0,0);
    assert(guarded_runtime.begin(alloc::base,8,1,&image_native_free,&fixture_environment));
    assert(guarded_runtime.allocation(old_address,0x1E0,0x334A1E));
    Lifecycle guarded(guarded_runtime);assert(guarded.capture(1,manager));
    original[0x30]=std::byte{77};
    assert(!guarded.restore(0,[](const Image&,const Image&){return false;}));
    assert(guarded.diagnostic.failure==Lifecycle::Failure::dependencies && !guarded.diagnostic.mutated);
    assert(wind_head==old_address && original[0x30]==std::byte{77});
    assert(guarded.retire() && guarded_runtime.finish(true));
    // A frame reset/rematch cannot silently enter the same lifecycle window.
    Lifecycle::Runtime reset_runtime;
    assert(reset_runtime.begin(alloc::base,9,1,&image_native_free,&fixture_environment));
    assert(reset_runtime.allocation(old_address,0x1E0,0x334A1E));
    Lifecycle reset(reset_runtime);assert(reset.capture(20,manager));
    assert(!reset.capture(1,manager) && reset.diagnostic.failure==Lifecycle::Failure::identity);
    assert(reset.retire() && reset_runtime.finish(true));
    std::puts("PASS: wind creation, rewind, revival, replay relocation, exact payload comparison and retirement");
}
struct ExternalFixture {
    std::array<sc6::research::EffectOwnedRange,3> ranges{};unsigned count{};bool owned=true;
    static bool provide(void* context,std::array<sc6::research::EffectOwnedRange,3>& out,unsigned& size){
        auto& f=*static_cast<ExternalFixture*>(context);out=f.ranges;size=f.count;return f.owned;
    }
};
static void native_wind_binding_tests(){
    auto* memory=static_cast<std::byte*>(VirtualAlloc(nullptr,0x4860000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(memory);
    const auto module=reinterpret_cast<uintptr_t>(memory),query=module+0x4844DD0,manager=module+0x4100B60;
    auto put=[](uintptr_t at,uintptr_t value){std::memcpy(reinterpret_cast<void*>(at),&value,8);};
    std::array<std::byte,64> descriptor{};std::array<std::byte,256> table{};
    const auto desc=reinterpret_cast<uintptr_t>(descriptor.data()),data=reinterpret_cast<uintptr_t>(table.data());
    put(module+0x470E038,manager);put(module+0x40FBC38,module+0x4100C00);put(module+0x40F7688,module+0x4100C10);
    put(query,desc);memory[0x4844DD0+0x410]=std::byte{1};descriptor[0x28]=std::byte{1};
    put(query+8,data);put(data,data+16);table[8]=std::byte{1};
    put(data+16,data+32);table[40]=std::byte{1};put(data+48,data+64);
    ExternalFixture fixture;fixture.count=2;fixture.ranges[0]={desc,descriptor.size(),11};fixture.ranges[1]={data,table.size(),22};
    sc6::research::EffectExternalSource external{&fixture,ExternalFixture::provide};
    sc6::research::EffectExternalReferences references;assert(references.capture(module,external));
    table[100]=std::byte{1};assert(!references.validate());table[100]=std::byte{};assert(references.validate());
    put(query+8,1);assert(!references.validate());put(query+8,data);
    fixture.owned=false;assert(!references.validate());fixture.owned=true;assert(references.validate());
    put(data+48,data+240);assert(!references.capture(module,external)); // Full candidate extent exceeds ownership.
    put(data+48,data+64);assert(references.capture(module,external));
    // Dense shared stage references exceed the former 8192-visit cap.
    std::array<std::byte,4096> dense{};const auto dense_address=reinterpret_cast<uintptr_t>(dense.data());
    const auto old_range=fixture.ranges[1];fixture.ranges[1]={dense_address,dense.size(),22};
    descriptor[0x28]=std::byte{64};
    for(unsigned i=0;i<64;++i)put(query+8+i*8,dense_address);
    put(dense_address,dense_address+16);dense[8]=std::byte{30};
    for(unsigned j=0;j<30;++j)put(dense_address+16+j*8,dense_address+512);
    dense[520]=std::byte{8};
    for(unsigned j=0;j<8;++j)put(dense_address+528+j*8,dense_address+1024);
    assert(references.capture(module,external) && references.diagnostic.visits==17280);
    put(dense_address+528,dense_address+dense.size()-16);
    assert(!references.capture(module,external) && references.diagnostic.check==11);
    put(dense_address+528,dense_address+1024);
    dense[520]=std::byte{40};
    for(unsigned j=0;j<40;++j)put(dense_address+528+j*8,dense_address+1024);
    assert(!references.capture(module,external) && references.diagnostic.visits==65537);
    assert(references.diagnostic.check==7 || references.diagnostic.check==10);
    // The native query explicitly accepts an active root without a descriptor.
    put(query,0);assert(references.capture(module,external));put(query,desc);
    fixture.ranges[1]=old_range;descriptor[0x28]=std::byte{1};
    for(unsigned i=0;i<64;++i)put(query+8+i*8,0);
    put(query+8,data);assert(references.capture(module,external));
    std::array<std::byte,0x1E0> old_node{},new_node{};
    const auto old_address=reinterpret_cast<uintptr_t>(old_node.data()),new_address=reinterpret_cast<uintptr_t>(new_node.data());
    auto node=[&](auto& bytes,uintptr_t next,uintptr_t previous){
        const auto address=reinterpret_cast<uintptr_t>(bytes.data());
        put(address,module+0x3E88CE8);put(address+0x10,next);put(address+0x18,previous);put(address+0x28,manager);
    };
    node(old_node,0,0);put(manager,old_address);
    uintptr_t emitter_sentinel{};emitter_sentinel=reinterpret_cast<uintptr_t>(&emitter_sentinel);
    put(module+0x470F1C0,emitter_sentinel);
    sc6::research::NativeEffectSession session;session.configure(image_native_free);
    assert(session.begin(module,17,2,external));
    sc6::research::NativeEffectSession::Binding before,after;
    assert(session.capture(before));
    // Use the real allocation callback route, with a fixture allocator result.
    const auto old_base=alloc::base;alloc::base=module;
    // A known allocation born and freed before any snapshot is not a dangling
    // retained object and must not stop later captures.
    std::array<std::byte,0x1E0> transient{};
    allocation_result=reinterpret_cast<uintptr_t>(transient.data());
    alloc::observe_allocate(0x1E0,module+0x334A1E,allocate_mock);
    const auto* route=release_bridge::route.load();assert(route);
    route->before_native(route->context,allocation_result);
    allocation_result=new_address;
    assert(alloc::observe_allocate(0x1E0,module+0x334A1E,allocate_mock)==new_address);
    node(new_node,old_address,0);put(old_address+0x18,new_address);put(manager,new_address);
    assert(session.capture(after));assert(session.restore(before));
    assert(session.compare(before) && session.removed==1);
    assert(session.restore(after) && session.compare(after) && session.added==1);
    // The native pool must retire the lifecycle pin before releasing resources.
    sc6::research::NativeSnapshotPool<1> pool;
    auto handle=pool.acquire(17,2,50);assert(handle);
    assert(pool.visit(*handle,[&](sc6::research::NativeBoundarySnapshot& snapshot){
        snapshot.effects=std::make_unique<sc6::research::NativeEffectSession::Binding>();
        return session.capture(*snapshot.effects);
    }));
    assert(pool.release_dependencies(*handle));
    assert(session.release(before) && session.release(after));assert(session.finish());
    assert(!release_bridge::route.load());alloc::base=old_base;
    assert(VirtualFree(memory,0,MEM_RELEASE));
    std::puts("PASS: owned stage graph, native wind snapshot bindings, list restore, replay comparison and pool retirement");
}
#include "../src/ResourceDescriptorObserver.hpp"
namespace ro=sc6::research::resource_observer;
static uintptr_t resource_mock(uintptr_t){assert(GetLastError()==123);SetLastError(456);return 789;}
static uintptr_t resource_factory_mock(uintptr_t,uintptr_t output,uintptr_t){SetLastError(456);return output;}
static void resource_tests(){
    auto memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x470E000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(memory);
    ro::base=reinterpret_cast<uintptr_t>(memory);
    alignas(8) unsigned char object[0xC0]{};
    *reinterpret_cast<uintptr_t*>(object)=ro::base+0x3CEA0F8;
    *reinterpret_cast<uintptr_t*>(object+0x38)=789;
    *reinterpret_cast<int32_t*>(object+0x48)=64;object[0x50]=1;
    *reinterpret_cast<uintptr_t*>(memory+0x470D0D0)=789;
    ro::count=0;ro::epoch=1;SetLastError(123);
    assert(ro::observe(reinterpret_cast<uintptr_t>(object),ro::base+0x89D79C,resource_mock)==789);
    assert(GetLastError()==456 && ro::count==1 && ro::rows[0].valid && ro::rows[0].size==64 && ro::rows[0].descriptor0==789);
    SetLastError(123);ro::observe(0,0,resource_mock);assert(ro::count==1);
    SetLastError(123);ro::observe(0,ro::base+0x89D79C,resource_mock);assert(ro::count==2 && !ro::rows[1].valid);
    ro::count=4096;ro::dropped=0;SetLastError(123);ro::observe(reinterpret_cast<uintptr_t>(object),ro::base+0x89D79C,resource_mock);assert(ro::count==4096 && ro::dropped==1);
    alignas(8) unsigned char control[24]{};
    *reinterpret_cast<uintptr_t*>(control)=ro::base+0x36E9D40;
    *reinterpret_cast<int32_t*>(control+8)=2;
    *reinterpret_cast<int32_t*>(control+12)=1;
    *reinterpret_cast<uintptr_t*>(control+16)=reinterpret_cast<uintptr_t>(object);
    uintptr_t pair[]={reinterpret_cast<uintptr_t>(object),reinterpret_cast<uintptr_t>(control)};
    {
        unsigned char payload[]={1,2,3,4};
        *reinterpret_cast<uintptr_t*>(object+0x38)=reinterpret_cast<uintptr_t>(payload);
        *reinterpret_cast<int32_t*>(object+0x48)=4;
        using Fingerprint=sc6::research::ResourcePayloadFingerprint;
        const auto first=Fingerprint::read(pair[0],ro::base);
        assert(first.valid && first.matches(Fingerprint::read(pair[0],ro::base)));
        payload[2]=9;assert(!first.matches(Fingerprint::read(pair[0],ro::base)));
        *reinterpret_cast<int32_t*>(object+0x48)=1024*1024+1;
        assert(!Fingerprint::read(pair[0],ro::base).valid);
        *reinterpret_cast<int32_t*>(object+0x48)=4;
        *reinterpret_cast<uintptr_t*>(object+0x38)=1;
        assert(!Fingerprint::read(pair[0],ro::base).valid);
        *reinterpret_cast<uintptr_t*>(object+0x38)=789;
        *reinterpret_cast<int32_t*>(object+0x48)=64;
    }
    alignas(8) uintptr_t caller_stack[18]{};
    caller_stack[16]=pair[0];caller_stack[17]=pair[1];
    ro::count=0;
    SetLastError(123);
    ro::observe(pair[0],ro::base+0x89D79C,resource_mock,reinterpret_cast<uintptr_t>(caller_stack));
    assert(GetLastError()==456 && ro::rows[0].owner_valid && ro::rows[0].control==pair[1]);
    assert(ro::rows[0].strong==2 && *reinterpret_cast<int32_t*>(control+8)==2);
    const auto eligible=ro::rows[0];assert(ro::descriptor_hold_eligible(eligible));
    auto rejected=eligible;rejected.owner_valid=0;assert(!ro::descriptor_hold_eligible(rejected));
    rejected=eligible;rejected.ready=0;assert(!ro::descriptor_hold_eligible(rejected));
    rejected=eligible;rejected.result=0;assert(!ro::descriptor_hold_eligible(rejected));
    rejected=eligible;rejected.descriptor0=0;assert(!ro::descriptor_hold_eligible(rejected));
    rejected=eligible;rejected.size=0;assert(!ro::descriptor_hold_eligible(rejected));
    rejected=eligible;rejected.descriptor0=0;rejected.descriptor1=789;assert(ro::descriptor_hold_eligible(rejected));
    caller_stack[16]=0;
    SetLastError(123);
    ro::observe(pair[0],ro::base+0x89D79C,resource_mock,reinterpret_cast<uintptr_t>(caller_stack));
    assert(!ro::rows[1].owner_valid);
    SetLastError(123);
    ro::observe(pair[0],ro::base+0x89D79C,resource_mock,1);
    assert(!ro::rows[2].owner_valid && GetLastError()==456);
    SetLastError(123);
    ro::observe(pair[0],0,resource_mock,1);
    assert(ro::count==3 && GetLastError()==456);
    ro::owner_count=0;ro::retention_requested=true;ro::epoch=1;
    object[0x50]=0; // Loading resource with no published descriptor must be probeable.
    *reinterpret_cast<uintptr_t*>(memory+0x470D0D0)=0;
    assert(ro::observe_factory(0,reinterpret_cast<uintptr_t>(pair),0,resource_factory_mock)==reinterpret_cast<uintptr_t>(pair));
    assert(GetLastError()==456 && ro::owners[0].probe_attempted && ro::owners[0].probe_passed);
    assert(*reinterpret_cast<int32_t*>(control+8)==2);
    ro::retention_requested=false;
    ro::observe_factory(0,reinterpret_cast<uintptr_t>(pair),0,resource_factory_mock);
    assert(!ro::owners[1].probe_attempted);
    ro::retention_requested=true;ro::owner_count=512;
    ro::observe_factory(0,reinterpret_cast<uintptr_t>(pair),0,resource_factory_mock);
    assert(!ro::owners[512].probe_attempted);
    ro::retention_requested=false;
    const auto owner=ro::read_owner(reinterpret_cast<uintptr_t>(pair),ro::base);
    assert(owner.valid && owner.strong==2 && owner.controlled==pair[0]);
    *reinterpret_cast<int32_t*>(control+8)=0;
    assert(!ro::read_owner(reinterpret_cast<uintptr_t>(pair),ro::base).valid);
    *reinterpret_cast<int32_t*>(control+8)=2;
    *reinterpret_cast<uintptr_t*>(control+16)=0;
    assert(!ro::read_owner(reinterpret_cast<uintptr_t>(pair),ro::base).valid);
    assert(!ro::read_owner(0,ro::base).valid);
    ro::epoch=0;VirtualFree(memory,0,MEM_RELEASE);
}

#include "../src/ResourceReferenceLease.hpp"
static unsigned lease_object_calls{},lease_control_calls{};
static void lease_object_dispose(uintptr_t){++lease_object_calls;}
static void lease_control_dispose(uintptr_t){assert(lease_object_calls);++lease_control_calls;}
static void resource_lease_tests(){
    using Lease=sc6::research::ResourceReferenceLease;
    struct Control {uintptr_t vtable{};LONG strong{1},weak{1};} control;
    const auto address=reinterpret_cast<uintptr_t>(&control);
    Lease lease;
    assert(lease.acquire(address,GetCurrentThreadId(),10,lease_object_dispose,lease_control_dispose));
    assert(control.strong==2 && lease.release(12)==Lease::Release::rejected);
    assert(!lease.acquire(address,GetCurrentThreadId(),10,lease_object_dispose,lease_control_dispose));
    assert(lease.release(13)==Lease::Release::shared && control.strong==1);
    assert(lease.release(14)==Lease::Release::rejected);
    assert(lease.acquire(address,GetCurrentThreadId(),20,lease_object_dispose,lease_control_dispose));
    InterlockedDecrement(&control.strong); // Native owner retires during hold.
    assert(lease.release(23)==Lease::Release::control_destroyed);
    assert(control.strong==0 && control.weak==0 && lease_object_calls==1 && lease_control_calls==1);
    control.strong=1;control.weak=2;
    assert(lease.acquire(address,GetCurrentThreadId(),30,lease_object_dispose,lease_control_dispose));
    InterlockedDecrement(&control.strong);
    assert(lease.release(33)==Lease::Release::object_destroyed && control.weak==1);
    control.strong=1;
    assert(lease.acquire(address,GetCurrentThreadId(),40,lease_object_dispose,lease_control_dispose));
    std::thread wrong_thread([&]{assert(lease.release(43)==Lease::Release::rejected);});wrong_thread.join();
    assert(lease.active() && lease.release(40,true)==Lease::Release::shared);
    control.strong=0;assert(!lease.acquire(address,GetCurrentThreadId(),50,lease_object_dispose,lease_control_dispose));
}
#include "../src/ResourceLeaseBoundary.hpp"
static void resource_boundary_tests(){
    struct Control {uintptr_t vtable{};LONG strong{1},weak{1};} control;
    auto address=reinterpret_cast<uintptr_t>(&control);
    sc6::research::ResourceLeaseBoundary<1> boundary;
    assert(boundary.begin());
    assert(!boundary.capture(address,lease_object_dispose,lease_control_dispose));
    assert(boundary.boundary(10));
    assert(boundary.capture(address,lease_object_dispose,lease_control_dispose));
    assert(!boundary.capture(address,lease_object_dispose,lease_control_dispose));
    assert(!boundary.begin());
    assert(boundary.boundary(12) && boundary.pending()==1 && control.strong==2);
    std::thread other([&]{assert(!boundary.cancel());assert(!boundary.boundary(13));});other.join();
    assert(boundary.boundary(13) && boundary.pending()==0 && control.strong==1 && boundary.released()==1);
    assert(boundary.capture(address,lease_object_dispose,lease_control_dispose));
    assert(!boundary.boundary(1)); // Reset cancels on the owning thread.
    assert(boundary.pending()==0 && control.strong==1 && boundary.cancelled()==1);
    assert(!boundary.capture(address,lease_object_dispose,lease_control_dispose));
    assert(boundary.begin() && boundary.boundary(20));
    assert(boundary.capture(address,lease_object_dispose,lease_control_dispose));
    assert(boundary.cancel() && boundary.cancel() && control.strong==1);
}
#include "../src/ResourceLeaseService.hpp"
static bool service_cancel_seen{};
static bool service_mock(uint32_t frame,bool cancel){assert(frame==42);service_cancel_seen=cancel;return true;}
static void lease_service_tests(){
    namespace svc=sc6::research::lease_service;
    svc::owner=0;svc::events=0;svc::wrong_thread=0;svc::handler=nullptr;
    svc::request_cancel();assert(svc::dispatch(42) && svc::cancellation);
    svc::handler=service_mock;
    std::thread wrong([]{assert(!sc6::research::lease_service::dispatch(42));});wrong.join();
    assert(svc::cancellation && svc::wrong_thread==1);
    assert(svc::dispatch(42) && service_cancel_seen && !svc::cancellation);
    svc::handler=nullptr;svc::owner=0;
}
static void lease_runtime_tests(){
    namespace runtime=sc6::research::lease_runtime;
    namespace service=sc6::research::lease_service;
    struct Control {uintptr_t vtable{};LONG strong{1},weak{1};} control;
    service::owner=GetCurrentThreadId();service::cancellation=false;
    assert(runtime::prepare(0));assert(!runtime::prepare(0));
    service::owner=0;assert(!runtime::capture(reinterpret_cast<uintptr_t>(&control)));
    assert(runtime::gate_no_owner==1);service::owner=GetCurrentThreadId();
    std::thread other([&]{assert(runtime::capture(reinterpret_cast<uintptr_t>(&control)));});other.join();
    assert(runtime::wrong_factory_thread==1 && control.strong==2 && runtime::queue.pending()==1);
    assert(service::dispatch(100));assert(runtime::transferred==1 && control.strong==2);
    assert(service::dispatch(102) && runtime::pending==1);
    assert(service::dispatch(103) && runtime::pending==0 && control.strong==1);
    assert(runtime::capture(reinterpret_cast<uintptr_t>(&control)));
    runtime::before_clear();assert(runtime::pending==0 && runtime::cancelled==1 && control.strong==1);
    assert(!runtime::capture(reinterpret_cast<uintptr_t>(&control)));
    assert(runtime::gate_disabled==1 && runtime::first_stop==2);
    runtime::note_stop(3);assert(runtime::first_stop==2);
    service::handler=nullptr;service::owner=0;service::cancellation=false;
    sc6::research::ResourceOwnedQueue<1> bounded;
    assert(bounded.open() && bounded.push(reinterpret_cast<uintptr_t>(&control)));
    assert(!bounded.push(reinterpret_cast<uintptr_t>(&control)) && control.strong==2);
    bounded.close();assert(!bounded.push(reinterpret_cast<uintptr_t>(&control)));
    sc6::research::ResourceReferenceLease owned;
    assert(owned.adopt(bounded.pop(),GetCurrentThreadId(),0,lease_object_dispose,lease_control_dispose));
    assert(owned.release(0,true)==sc6::research::ResourceReferenceLease::Release::shared && control.strong==1);
    assert(!bounded.pop());
}

static void snapshot_hold_cancel_test(){
    using sc6::research::descriptor_range_coverage;
    assert(descriptor_range_coverage(100,16,100,16)==1);
    assert(descriptor_range_coverage(108,8,100,16)==1);
    assert(descriptor_range_coverage(100,1000000,100,16)==2);
    assert(descriptor_range_coverage(116,1,100,16)==0);
    assert(descriptor_range_coverage(99,1,100,16)==0);
    assert(descriptor_range_coverage(UINT64_MAX-1,8,100,16)==0);
    {
        namespace ranges=sc6::research::range_observer;
        using Image=sc6::research::NativeRangeImage<>;
        Image::Entry entries[]={{0,0},{100,16}};
        const auto begin=reinterpret_cast<uintptr_t>(entries);
        Image::Header header{begin,begin+sizeof(entries),begin+sizeof(entries)};
        Image output;
        {
            ranges::ScopedCapture scope(output);assert(scope.active);
            SetLastError(123);
            assert(ranges::observe(0,1,reinterpret_cast<uintptr_t>(&header),ranges::base+0x3844AC,range_native)==789);
            assert(GetLastError()==456 && output.valid && output.count==2);
            SetLastError(123);
            ranges::observe(0,1,reinterpret_cast<uintptr_t>(&header),ranges::base+0x3844AC,range_native);
            assert(!output.valid); // Ambiguous multiple native tables reject.
        }
        assert(!ranges::scoped_image);
    }
    namespace probe=sc6::research::dependency_probe;
    struct Control {uintptr_t table{};LONG strong{1},weak{1};} control;
    probe::pool=std::make_unique<probe::Pool>();
    probe::held_snapshot=probe::pool->acquire(1,1,0);assert(probe::held_snapshot);
    assert(probe::pool->retain_dependency(*probe::held_snapshot,reinterpret_cast<uintptr_t>(&control),lease_object_dispose,lease_control_dispose));
    sc6::research::lease_service::owner=GetCurrentThreadId();
    probe::phase=6;probe::cancelled=false;
    probe::clear();assert(probe::cancelled);
    probe::frame(0,0); // Cancellation must not read invalid native addresses.
    assert(probe::phase==3 && probe::retired && !probe::held_snapshot && control.strong==1);
    assert(!probe::hold_completed);
    probe::frame(0,0);assert(control.strong==1); // No double retirement.
    probe::pool.reset();probe::phase=0;probe::cancelled=false;
}
static void wind_emitter_snapshot_tests(){
    using sc6::research::WindEmitterSnapshot;
    std::array<uintptr_t,3> sentinel{},node{};
    std::array<std::byte,0xA8> object{};
    auto root=reinterpret_cast<uintptr_t>(sentinel.data());
    uintptr_t root_pointer=root;
    auto module=reinterpret_cast<uintptr_t>(&root_pointer)-0x470F1C0;
    sentinel[0]=reinterpret_cast<uintptr_t>(node.data());node[0]=root;node[2]=reinterpret_cast<uintptr_t>(object.data());
    float zero=0.0f,later=10.0f;unsigned pending=1,count=3;
    std::memcpy(object.data()+0x50,&pending,4);std::memcpy(object.data()+0x54,&count,4);
    std::memcpy(object.data()+0x5C,&zero,4);object[0x58]=std::byte{0x42};
    auto saved=std::make_unique<WindEmitterSnapshot>(),live=std::make_unique<WindEmitterSnapshot>();
    assert(saved->capture(module) && saved->count==1);
    std::memcpy(object.data()+0x5C,&later,4);std::memset(object.data()+0x50,0,8);
    assert(live->capture(module) && saved->compatible(*live) && !saved->compatible(*live,true));
    assert(saved->restore(module,*live) && object[0x58]==std::byte{0x42});
    unsigned actual{};std::memcpy(&actual,object.data()+0x54,4);assert(actual==3);
    float timer{};std::memcpy(&timer,object.data()+0x5C,4);assert(timer==0.0f);
    // The restored threshold triggers emission again; the advanced timer would skip it.
    assert(timer<=0.0f && later>0.0f);
    std::memcpy(object.data()+0x5C,&later,4);object[0x58]=std::byte{0x43};
    assert(!saved->restore(module,*live));std::memcpy(&timer,object.data()+0x5C,4);assert(timer==later);
    object[0x58]=std::byte{0x42};node[0]=reinterpret_cast<uintptr_t>(node.data());
    assert(!live->capture(module));node[0]=root;
    node[2]=1;assert(!live->capture(module) && live->exception);node[2]=reinterpret_cast<uintptr_t>(object.data());
    sentinel[0]=root;assert(live->capture(module) && live->count==0 && !saved->compatible(*live));
    assert(live->restore(module,*saved));
}
int main(){
    wind_emitter_snapshot_tests();
    {
        namespace g=sc6::research::generated_observer;
        unsigned char buffer_bytes[]={1,2,3,4};
        const auto buffer_address=reinterpret_cast<uintptr_t>(buffer_bytes);
        using Fingerprint=sc6::research::GeneratedBufferFingerprint;
        const auto initial=Fingerprint::read(buffer_address,4);assert(initial.valid && initial.matches(Fingerprint::read(buffer_address,4)));
        buffer_bytes[3]=5;assert(!initial.matches(Fingerprint::read(buffer_address,4)));
        assert(!Fingerprint::read(0,4).valid && !Fingerprint::read(buffer_address,0).valid && !Fingerprint::read(buffer_address,1024*1024+1).valid);
        assert(!Fingerprint::read((std::numeric_limits<uintptr_t>::max)()-1,4).valid);
        struct FakeControl {uintptr_t table{};LONG strong{1},weak{1};} control;
        auto dispose=+[](uintptr_t){assert(false);};
        sc6::research::GeneratedBufferHold held;
        assert(!held.capture(reinterpret_cast<uintptr_t>(&control),0,dispose,dispose));
        assert(held.capture(reinterpret_cast<uintptr_t>(&control),GetCurrentThreadId(),dispose,dispose));
        assert(control.strong==2);held.before_setup(100,200);assert(held.pending() && held.cancelled==0 && control.strong==2);held.step();held.step();assert(held.pending() && control.strong==2);
        held.step();assert(!held.pending() && held.released==1 && held.depth()==3 && held.cancel_reason==0 && control.strong==1);held.step();assert(control.strong==1);
        sc6::research::GeneratedBufferHold cancelled;
        assert(cancelled.capture(reinterpret_cast<uintptr_t>(&control),GetCurrentThreadId(),dispose,dispose));
        cancelled.before_setup(100,100);assert(cancelled.cancelled==1 && cancelled.cancel_reason==1 && cancelled.depth()==0 && control.strong==1);cancelled.step(true,2);assert(cancelled.cancel_reason==1);
        g::ThreadCorrelation threads;assert(threads.classify(7)==0);threads.observe(7);assert(threads.classify(7)==1 && threads.classify(8)==2);threads.observe(8);assert(threads.classify(7)==0);
        auto mock=+[](uintptr_t,uintptr_t,uintptr_t)->bool{SetLastError(765);return true;};
        g::armed=false;
        assert(g::observe_allocate(0,0,0,mock) && GetLastError()==765);
        assert(g::current_root==0);
        {g::Context outer(100);assert(g::current_root==100);
         {g::Context inner(200);assert(g::current_root==200);}
         assert(g::current_root==100);}
        assert(g::current_root==0);
        g::armed=true;g::count=0;
        {g::Context context(100);assert(g::observe_allocate(0,1,8,mock));}
        assert(g::count==0 && GetLastError()==765);
        g::armed=false;
    }
    resource_lease_tests();
    resource_boundary_tests();
    lease_service_tests();
    lease_runtime_tests();
    snapshot_hold_cancel_test();
    {
        namespace clear=sc6::research::resource_clear;
        sc6::research::lease_service::owner=GetCurrentThreadId();
        clear::events=0;clear::same_thread=0;
        SetLastError(123);assert(clear::observe(0,resource_mock)==789);
        assert(GetLastError()==456 && clear::events==1 && clear::same_thread==1);
        sc6::research::lease_service::owner=0;
    }
    resource_tests();
    {
        ro::OwnerRow owner{};ro::Row resource{};
        assert(ro::eligibility(owner,resource)==1);
        owner.valid=1;assert(ro::eligibility(owner,resource)==2);
        resource.valid=1;assert(ro::eligibility(owner,resource)==3);
        resource.ready=1;assert(ro::eligibility(owner,resource)==4);
        resource.payload=123;assert(ro::eligibility(owner,resource)==5);
        resource.descriptor0=456;assert(ro::eligibility(owner,resource)==6);
        resource.descriptor1=123;assert(ro::eligibility(owner,resource)==0);
    }
    { volatile LONG reference_count=1;LONG before{},held{},released{};
    assert(sc6::research::probe_resource_reference(&reference_count,before,held,released));
    assert(reference_count==1 && before==1 && held==2 && released==1);
    reference_count=7;assert(sc6::research::probe_resource_reference(&reference_count,before,held,released) && reference_count==7);
    reference_count=0;assert(!sc6::research::probe_resource_reference(&reference_count,before,held,released) && reference_count==0);
    reference_count=LONG_MAX;assert(!sc6::research::probe_resource_reference(&reference_count,before,held,released) && reference_count==LONG_MAX); }
    {
        std::array<std::byte,160> memory{};
        int16_t header[]={1,2,2};uint16_t first=3,second=4;
        std::memcpy(memory.data()+0x28,header,6);
        std::memcpy(memory.data()+72,&first,2);std::memcpy(memory.data()+96,&second,2);
        sc6::research::StageRangeDescriptor detail;auto address=reinterpret_cast<uintptr_t>(memory.data());
        assert(detail.capture(address) && detail.computed==288 && detail.read_count==2);
        header[1]=4097;std::memcpy(memory.data()+0x28,header,6);
        assert(!detail.capture(address) && detail.bounded_out && !detail.read_count);
        header[1]=-1;std::memcpy(memory.data()+0x28,header,6);assert(!detail.capture(address));
        assert(!detail.capture(1) && detail.exception);
    }
    {
        namespace ro=sc6::research::range_observer;
        std::array<sc6::research::NativeRangeImage<>::Entry,2> entries{{{}, {0x1000,16}}};
        auto begin=reinterpret_cast<uintptr_t>(entries.data());
        sc6::research::NativeRangeImage<>::Header header{begin,begin+sizeof(entries),begin+sizeof(entries)};
        ro::base=0x100000;ro::serial=1;ro::recording=true;ro::armed=true;ro::completed=false;
        SetLastError(123);assert(ro::observe(0,1,reinterpret_cast<uintptr_t>(&header),0,range_native)==789);
        assert(ro::armed && !ro::completed);
        SetLastError(123);assert(ro::observe(0,1,reinterpret_cast<uintptr_t>(&header),ro::base+0x3844AC,range_native)==789);
        assert(GetLastError()==456 && ro::completed && ro::captured && ro::image.count==2 && !ro::armed);
        ro::recording=false;
    }
    {
        using Image=sc6::research::NativeRangeImage<3>;
        sc6::research::NativeRangeCaptureBridge<3> bridge;
        std::array<Image::Entry,2> entries{{{}, {0x1000,0x100}}};
        auto begin=reinterpret_cast<uintptr_t>(entries.data());
        Image::Header header{};auto address=reinterpret_cast<uintptr_t>(&header);
        unsigned invoked=0;
        auto native=[&](uintptr_t context,uintptr_t stream,uintptr_t vector){
            assert(context==7 && stream==8 && vector==address);++invoked;
            header={begin,begin+sizeof(entries),begin+sizeof(entries)};
            SetLastError(987);return uintptr_t{55};
        };
        Image image;
        assert(bridge.observe(7,8,address,9,native)==55 && !bridge.finish(image));
        assert(bridge.arm(8,9) && !bridge.arm(8,9));header={};
        assert(bridge.observe(7,8,address,9,native)==55 && GetLastError()==987);
        assert(bridge.finish(image) && image.count==2 && invoked==2);
        assert(bridge.arm(8,10));bridge.observe(7,8,address,9,native);assert(!bridge.finish(image));
        assert(bridge.arm(8,9));
        std::thread foreign([&]{bridge.observe(7,8,address,9,native);});foreign.join();
        assert(!bridge.finish(image));
        assert(bridge.arm(8,9));bridge.observe(7,8,address,9,native);bridge.observe(7,8,address,9,native);
        assert(!bridge.finish(image) && bridge.dropped==1);
        assert(bridge.arm(8,9));
        bridge.observe(7,8,address,9,[&](uintptr_t,uintptr_t,uintptr_t){
            assert(!bridge.finish(image));assert(bridge.arm(8,9));return uintptr_t{};
        });
        assert(!bridge.finish(image)); // Old in-flight call cannot populate a new arm.
    }
    {
        using Image=sc6::research::NativeRangeImage<3>;
        std::array<Image::Entry,3> entries{{{}, {0x1000,0x100},{0x2000,0x80}}};
        const auto begin=reinterpret_cast<uintptr_t>(entries.data());
        Image::Header header{begin,begin+sizeof(entries),begin+sizeof(entries)};
        Image image;const auto address=reinterpret_cast<uintptr_t>(&header);
        assert(image.capture(address) && image.count==3);
        std::array<sc6::research::NativeRangeCompatibility::Range,3> ranges{};
        assert(!image.bind(ranges,[](uint64_t,uint64_t){return uint64_t{};}));
        assert(ranges[1].generation==0);
        assert(image.bind(ranges,[](uint64_t base,uint64_t){return base==0x1000?uint64_t{1}:uint64_t{2};}));
        assert(ranges[2].generation==2);
        --header.end;assert(!image.capture(address));++header.end;
        header.capacity=header.end-16;assert(!image.capture(address));header.capacity=header.end;
        entries[0].base=1;assert(!image.capture(address));entries[0].base=0;
        entries[2]={UINT64_MAX-4,8};assert(!image.capture(address));entries[2]={0x2000,0x80};
        sc6::research::NativeRangeImage<2> bounded_image;assert(!bounded_image.capture(address));
        assert(!image.capture(1) && image.exception);
        assert(image.capture(address) && !image.exception);
    }
    {
        using Guard=sc6::research::NativeRangeCompatibility;
        std::array<Guard::Range,3> saved{{{}, {0x1000,0x100,1}, {0x2000,0x80,2}}};
        auto current=saved;
        std::array<Guard::Reference,2> refs{{{1,0x100},{0,0}}};
        auto retained=[](const Guard::Range&){return true;};
        auto absolute=[](uint64_t value){return value==0;};
        auto check=[&]{return Guard::check(saved,current,refs,retained,absolute);};
        assert(check()); // Native encoding allows the range endpoint.
        refs[0].offset=0x101;assert(!check());refs[0].offset=0;
        refs[0].index=3;assert(!check());refs[0].index=1;
        refs[1].offset=0x1234;assert(!check());refs[1].offset=0;
        ++current[1].generation;assert(!check());current=saved;
        std::swap(current[1],current[2]);assert(!check());current=saved;
        --current[1].size;assert(!check());current=saved;
        assert(!Guard::check(saved,current,refs,[](const Guard::Range&){return false;},absolute));
        saved[1].base=UINT64_MAX-4;saved[1].size=8;current=saved;assert(!check());
    }
    allocation_tests();
    release_bridge_tests();
    release_notice_tests();
    memory_gate_tests();
    runtime_tests();
    birth_hook_tests();
    effect_manifest_tests();
    effect_image_tests();
    restore_plan_tests();
    apply_image_tests();
    wind_lifecycle_window_tests();
    native_wind_binding_tests();
    failed_image_write_test();
    {
        for(const auto& site:sc6::research::effect_allocation_sites){
            assert(alloc::allocation_type(alloc::base+site.caller,site.bytes)==site.type);
            assert(alloc::allocation_type(alloc::base+site.caller,site.bytes+1)==0);
        }
        auto saved=std::make_unique<sc6::research::TransformSnapshot>();saved->valid=true;saved->count=5;
        auto& manager=saved->records[0];manager.kind=3;manager.side=2;manager.size=16;
        uintptr_t address=4096;std::memcpy(manager.bytes.data(),&address,8);
        auto& node=saved->records[1];node.kind=4;node.side=2;node.size=24;node.address=address;
        constexpr unsigned offsets[]{0x40,0x70,0x120},sizes[]{32,112,12};
        for(unsigned i=0;i<3;++i){auto& field=saved->records[i+2];field.kind=7+i;field.side=2;field.parent=1;field.address=address+offsets[i];field.size=sizes[i];}
        sc6::research::NativeEffectReferences<> refs;
        for(uintptr_t type:{uintptr_t{0x3E88C88},uintptr_t{0x3E88CB8}}){type+=alloc::base;std::memcpy(node.bytes.data(),&type,8);assert(refs.read(*saved,alloc::base) && refs.count==1);}
        saved->records[4].size++;assert(!refs.read(*saved,alloc::base));
        sc6::research::EffectObjectRegistry<2,2> registry;
        sc6::research::EffectObjectRegistry<2,2>::Window window;assert(window.begin(1,1));
        assert(registry.allocation(4096,0x130,0x334516,alloc::base));
        assert(registry.constructed(4096,alloc::base+0x3E88C88,true,window).generation);
        assert(registry.allocation(8192,0x130,0x3345B4,alloc::base));
        assert(registry.constructed(8192,alloc::base+0x3E88CB8,true,window).generation);
    }
    {
        auto image=std::make_unique<sc6::research::TransformSnapshot>();
        image->valid=true;image->count=3;
        auto& manager=image->records[0];manager.side=2;manager.kind=3;manager.address=0x1000;manager.size=16;manager.parent=UINT32_MAX;
        manager.bytes[8]=std::byte{42};
        auto& wind=image->records[1];wind.side=2;wind.kind=4;wind.parent=0;
        auto& player=image->records[2];player.side=0;player.kind=1;player.parent=UINT32_MAX;player.address=0x2000;
        assert(image->separate_effects() && image->count==2);
        assert(image->records[0].address==0x1008 && image->records[0].size==8 && image->records[0].bytes[0]==std::byte{42});
        assert(image->records[1].address==0x2000 && image->records[1].side==0);
    }
    auto before=o::sample(reinterpret_cast<uintptr_t>(&head));
    assert(before.valid && before.count==1);
    head=reinterpret_cast<uintptr_t>(&b);
    auto after=o::sample(reinterpret_cast<uintptr_t>(&head));auto delta=o::compare(before,after);
    assert(delta.valid && delta.added==1 && delta.removed==1);
    b.next=head;assert(!o::sample(reinterpret_cast<uintptr_t>(&head)).valid);b.next=0;
    assert(!o::sample(1).valid && !o::compare(before,{}).valid);
    o::IdentityTrace<32> trace;
    trace.observe(before,10,1,0,0,0);
    assert(trace.count==2 && trace.events[0].kind==4 && trace.events[1].kind==0);
    trace.observe(before,10,1,0,0,1);assert(trace.count==2);
    trace.observe(after,10,1,0,1,0); // Change between updates, missing from old per-call comparisons.
    assert(trace.count==4 && trace.events[2].kind==2 && trace.events[3].kind==1);
    auto changed=after;changed.nodes[0].vtable=789;
    trace.observe(changed,10,1,0,1,1);
    assert(trace.count==5 && trace.events[4].kind==3 && trace.events[4].old_type==456);
    trace.observe(changed,10,1,1,2,0); // Dropped call invalidates continuity.
    assert(trace.epoch==2 && trace.events[5].kind==4);
    trace.observe({},10,1,1,2,1);trace.observe(before,10,1,1,3,0);
    assert(trace.epoch==3); // Invalid read doesn't manufacture removals.
    trace.observe(before,11,1,1,3,1);assert(trace.epoch==4);
    trace.observe(before,11,2,1,4,0);assert(trace.epoch==5);
    trace.reset();assert(trace.count==0 && trace.epoch==0 && !trace.previous.valid);
    o::IdentityTrace<1> tiny_trace;
    tiny_trace.observe(before,10,1,0,0,0);assert(tiny_trace.count==1 && tiny_trace.dropped==1);
    tiny_trace.observe(after,10,1,0,0,1);assert(tiny_trace.count==1 && tiny_trace.dropped==3);
    const uintptr_t manager_address=reinterpret_cast<uintptr_t>(&head);
    o::base=reinterpret_cast<uintptr_t>(&manager_address)-0x470E038;
    o::active=true;head=reinterpret_cast<uintptr_t>(&a);SetLastError(1234);
    assert(o::observe(42,&invoke)==9876 && calls==1 && GetLastError()==5678);
    assert(o::count==1 && o::rows[0].delta.added==1 && o::rows[0].delta.removed==1);
    o::count=static_cast<unsigned>(o::rows.size());SetLastError(1234);
    assert(o::observe(42,&invoke)==9876 && calls==2 && o::dropped==1);
    AcquireSRWLockExclusive(&o::lock);SetLastError(1234);
    assert(o::observe(42,&invoke)==9876 && calls==3 && o::dropped==2);
    ReleaseSRWLockExclusive(&o::lock);o::active=false;SetLastError(1234);
    assert(o::observe(42,&invoke)==9876 && calls==4);
    std::puts("Effect observer tests passed; no native hook installed.");
}


