#pragma once
#include "NativeSnapshotPool.hpp"
#include "ResourceDescriptorObserver.hpp"
#include "DescriptorOwnershipBridge.hpp"
#include "NativeRangeObserver.hpp"
#include "DescriptorRangeCoverage.hpp"
namespace sc6::research::dependency_probe {
using Pool=NativeSnapshotPool<1>;
inline std::unique_ptr<Pool> pool;
// Worker publishes armed; frame thread claims running and publishes done.
inline std::atomic<unsigned> phase{};
inline bool used{},written{};
inline uint64_t deadline{};
inline unsigned capture_stage{},capture_exception{},observed{};
inline bool captured{},identity_match{},covered{},bound{},retired{};
inline DescriptorOwnershipBridge bridge;
inline std::atomic<bool> cancelled{};
inline uintptr_t module{};
inline std::atomic<unsigned> bridge_acquired{},bridge_released{};
inline std::optional<Pool::Handle> held_snapshot;
inline std::array<uintptr_t,2> held_objects{},held_descriptors{};
inline std::array<ResourcePayloadFingerprint,2> held_fingerprints;
inline unsigned held_count{},hold_samples{},hold_changes{},hold_invalid{},hold_depth{};
inline uint32_t hold_start{},hold_previous{};
inline bool hold_completed{};
inline NativeRangeImage<> snapshot_ranges;
inline bool frame_number(uint32_t& frame){
    __try{frame=*reinterpret_cast<uint32_t*>(module+0x470D0C4);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline void clear(){
    if(phase.load()!=1 && phase.load()!=2 && phase.load()!=6)return;
    // Signal only; release on the next owner boundary, never inside clear.
    if(phase.load()==6 || bridge_acquired.load()>bridge_released.load())cancelled=true;
}
inline void owned(uintptr_t object,uintptr_t control,uintptr_t payload){
    if(phase.load()!=1 || cancelled.load() || GetCurrentThreadId()!=lease_service::owner.load())return;
    uint32_t frame{};if(!frame_number(frame))return;
    const auto before=bridge.count();
    if(!bridge.add(object,control,payload,frame,ResourceDependencyCapture::native_object_dispose,
        ResourceDependencyCapture::native_control_dispose)){cancelled=true;return;}
    bridge_acquired+=bridge.count()-before;
}
inline bool ready(uintptr_t base,std::array<uintptr_t,2>& descriptors){
    __try {
        descriptors={*reinterpret_cast<uintptr_t*>(base+0x470D0D0),*reinterpret_cast<uintptr_t*>(base+0x470D0F8)};
        return descriptors[0] && FullStateProbe::preflight(base);
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline void frame(uintptr_t base,uintptr_t argument){
    if(GetCurrentThreadId()!=lease_service::owner.load())return;
    if(phase.load()==6){
        uint32_t current{};std::array<uintptr_t,2> descriptors{};
        bool valid=!cancelled.load() && GetTickCount64()<deadline && frame_number(current) &&
            current>=hold_previous && current-hold_previous<=1 && ready(base,descriptors) && descriptors==held_descriptors;
        if(valid && current>hold_previous){
            ++hold_samples;hold_depth=current-hold_start;
            for(unsigned i=0;i<held_count;++i){
                const auto value=ResourcePayloadFingerprint::read(held_objects[i],base);
                if(!value.valid){++hold_invalid;valid=false;}
                else if(!held_fingerprints[i].matches(value)){++hold_changes;valid=false;}
            }
            hold_previous=current;
        }
        if(!valid || hold_depth>=3){
            hold_completed=valid && hold_depth==3;
            if(!valid)cancelled=true;
            retired=held_snapshot && pool->retire(*held_snapshot);held_snapshot.reset();phase=3;
        }
        return;
    }
    if(phase.load()==5 || (phase.load()==1 && cancelled.load())){
        bridge_released+=bridge.count();bridge.reset();phase=4;return;
    }
    if(phase.load()!=1 || !bridge.count())return;
    uint32_t current{};
    if(!frame_number(current) || !bridge.fresh(current)){cancelled=true;return;}
    std::array<uintptr_t,2> before{},after{};
    if(!ready(base,before))return;
    unsigned armed=1;if(!phase.compare_exchange_strong(armed,2))return;
    if(pool->bind_current_thread()){
        const auto handle=pool->acquire(1,1,0,NativeBoundarySnapshot::Mode::combat_capture_only);
        if(handle){
            {
                ResourceDependencyCapture dependencies(ResourceDependencyCapture::native_object_dispose,
                    ResourceDependencyCapture::native_control_dispose);
                const bool seeded=bridge.seed(current,[&](uintptr_t object,uintptr_t payload){
                    const auto row=resource_observer::read(object,payload,base);
                    if(!row.valid || !row.ready || row.payload!=payload || row.size<=0 || held_count>=held_objects.size())return false;
                    const auto fingerprint=ResourcePayloadFingerprint::read(object,base);
                    if(!fingerprint.valid || fingerprint.payload!=payload)return false;
                    held_objects[held_count]=object;held_fingerprints[held_count++]=fingerprint;return true;
                });
                captured=seeded && pool->visit(*handle,[&](NativeBoundarySnapshot& snapshot){
                    range_observer::ScopedCapture ranges(snapshot_ranges);
                    if(!ranges.active)return false;
                    const bool ok=snapshot.capture(base,argument);
                    capture_stage=snapshot.capture_stage;capture_exception=snapshot.exception;return ok;
                });
                observed=dependencies.count();
                identity_match=ready(base,after) && before==after;
                covered=dependencies.covers(before[0],before[1]);
                if(captured && identity_match && covered && !cancelled.load())bound=dependencies.bind(*pool,*handle);
            } // Collector releases; slot retains its own references if bound.
            if(bound){held_snapshot=*handle;held_descriptors=before;hold_start=hold_previous=current;}
            else retired=pool->retire(*handle); // Same-thread cleanup on capture failure.
        }
    }
    bridge_released+=bridge.count();bridge.reset();phase=held_snapshot?6:3;
}
inline void poll(uintptr_t base,const std::filesystem::path& directory){
    auto value=phase.load();
    if(value==1 && GetTickCount64()>=deadline){unsigned armed=1;phase.compare_exchange_strong(armed,5);value=phase.load();}
    if((value==3 || value==4) && !written){
        dependency_events::owned=nullptr;dependency_events::clear=nullptr;
        std::ofstream out(directory/("snapshot-dependencies-"+std::to_string(GetCurrentProcessId())+".txt"));
        out<<"schema=1\nscope=snapshot_descriptor_dependencies\nrestore=0\ninjection=0\nrollback_active=0\n"
           <<"status="<<(value==4?(cancelled.load()?"bridge_cancelled":"timeout"):captured && identity_match && covered && bound && retired && hold_completed && snapshot_ranges.valid?"PASS":"rejected")
           <<"\ncapture_stage="<<capture_stage<<"\ncapture_exception="<<capture_exception
           <<"\ncaptured="<<captured<<"\nobserved_dependencies="<<observed<<"\ndescriptor_identity_match="<<identity_match
           <<"\ndescriptor_coverage="<<covered<<"\nsnapshot_dependencies_bound="<<bound<<"\nsnapshot_retired="<<retired
           <<"\nbridge_acquired="<<bridge_acquired<<"\nbridge_released="<<bridge_released
           <<"\nbridge_cancelled="<<cancelled.load()<<"\nsnapshot_hold_target_frames=3\nsnapshot_hold_depth="<<hold_depth
           <<"\nsnapshot_hold_samples="<<hold_samples<<"\nsnapshot_hold_changes="<<hold_changes
           <<"\nsnapshot_hold_invalid="<<hold_invalid<<"\nsnapshot_hold_completed="<<hold_completed
           <<"\ncomplete_dependency_coverage=0\n";
        written=true;
        std::ofstream ranges(directory/("snapshot-dependencies-"+std::to_string(GetCurrentProcessId())+"-ranges.csv"));
        ranges<<"index,base,registered_size,descriptor_class,descriptor_index,descriptor_payload,descriptor_bytes\n";
        unsigned contained=0,exceeds=0,unmapped=0;
        if(snapshot_ranges.valid)for(unsigned i=1;i<snapshot_ranges.count;++i){
            const auto& entry=snapshot_ranges.entries[i];unsigned classification=0,match=0;
            for(unsigned j=0;j<held_count;++j){const auto& fingerprint=held_fingerprints[j];
                const auto candidate=descriptor_range_coverage(entry.base,entry.size,fingerprint.payload,static_cast<uint64_t>(fingerprint.size));
                if(candidate && (!classification || candidate<classification)){classification=candidate;match=j;}
            }
            if(classification==1)++contained;else if(classification==2)++exceeds;else ++unmapped;
            ranges<<i<<','<<entry.base<<','<<entry.size<<','<<classification<<','<<(classification?int(match):-1)<<','
                  <<(classification?held_fingerprints[match].payload:0)<<','<<(classification?held_fingerprints[match].size:0)<<'\n';
        }
        ranges.flush();
        out<<"range_inventory_valid="<<snapshot_ranges.valid<<"\nrange_inventory_count="<<snapshot_ranges.count
           <<"\nrange_descriptor_contained="<<contained<<"\nrange_descriptor_extent_exceeds="<<exceeds
           <<"\nrange_descriptor_unmapped="<<unmapped<<"\nrange_inventory_csv_written="<<ranges.good()<<'\n';
    }
    const auto path=directory/"snapshot-dependencies.arm";
    std::ifstream request(path);if(!request)return;
    std::string token,extra;unsigned pid{};request>>token>>pid;
    const bool valid=bool(request) && !(request>>extra) && token=="SNAPSHOT_DEPENDENCIES_ONLY" && pid==GetCurrentProcessId();
    request.close();std::filesystem::remove(path);
    const bool accepted=valid && !used && !resource_observer::recording && !range_observer::recording && !lease_runtime::enabled.load() && resource_observer::install(base) && resource_clear::install(base) && range_observer::install(base);
    if(accepted){pool=std::make_unique<Pool>();used=true;module=base;deadline=GetTickCount64()+120000;
        dependency_events::owned=owned;dependency_events::clear=clear;phase=1;}
    std::ofstream out(directory/("snapshot-dependencies-ready-"+std::to_string(GetCurrentProcessId())+".txt"));
    out<<"schema=1\nstatus="<<(accepted?"armed":"rejected")<<"\nduration_seconds=120\nrestore=0\n";
}
}
