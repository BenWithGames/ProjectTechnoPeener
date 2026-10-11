#include "../src/NativeSnapshotPool.hpp"
#include "../src/NativeAdjacentRestore.hpp"
#include <cassert>
#include <iostream>
using Restore=sc6::research::NativeAdjacentRestore;
// Model completed diagnostics only; this test does not call native game code.
static void completed(Restore& r){
    r.attempted=r.prepared=r.verified=true;
    r.replay_delivered=r.replay_checked=r.replay_verified=r.replay_scalars_verified=true;
    r.native_payload_verified=r.completion.verified=true;
    r.probe->restore_invoked=r.probe->restore_completed=r.probe->delayed_loaded=r.probe->camera_restore_verified=true;
}
int main(){
    auto workspace=std::make_unique<Restore>();auto& r=*workspace;
    auto* snapshot=r.current.data.get();auto* scratch=r.probe.get();
    assert(!r.recycle_after_replay());
    for(unsigned test=0;test<8;++test){
        completed(r);
        switch(test){
            case 0:r.replay_checked=false;break;
            case 1:r.replay_verified=false;break;
            case 2:r.fault=true;break;
            case 3:r.probe->exception=1;break;
            case 4:r.probe->saved.stream.failures=1;break;
            case 5:r.completion.verified=false;break;
            case 6:r.current.fault=true;break;
            case 7:r.probe->restore_completed=false;break;
        }
        assert(!r.recycle_after_replay());assert(r.current.data.get()==snapshot && r.probe.get()==scratch);
        // Clear synthetic test faults; production callers must not do this.
        r.fault=r.current.fault=false;r.probe->exception=0;r.probe->saved.stream.failures=0;
    }
    for(unsigned n=0;n<20;++n){
        completed(r);r.completion.count=1;r.completion.patches[0]={123,nullptr,4};
        r.replay_stage=5;r.diagnostic_stage=10;
        std::array<std::byte,40> input{};r.replay_delivery.assign(input);r.next_argument=123;
        assert(r.recycle_after_replay());
        assert(r.current.data.get()==snapshot && r.probe.get()==scratch);
        assert(!r.attempted && !r.prepared && !r.verified && !r.replay_checked && !r.replay_verified);
        assert(!r.completion.count && !r.completion.patches[0].address && !r.completion.verified);
        assert(!r.replay_stage && !r.diagnostic_stage && !r.next_argument);
        assert(!r.replay_delivery.pointers[0] && !r.probe->restore_invoked);
        assert(r.replay_first_offset[0]==UINT32_MAX);
        assert(r.probe->saved.stream.slots==&sc6::research::FullRestoreStream::table);
        assert(r.probe->saved.stream.copier==sc6::research::SaveProbe::guarded_copy);
    }
    std::cout<<"PASS: restoration workspace reuse, retained allocations, reset owned pointers, rejection of incomplete/failed state\n";
}
