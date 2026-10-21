#include "../src/GeneratedSnapshotDependency.hpp"
#include "../src/NativeSnapshotPool.hpp"
#include "../src/ResourceDependencyCapture.hpp"
#include "../src/DescriptorOwnershipBridge.hpp"
#include "../src/NativeRollingHistory.hpp"
#include <cassert>
#include <thread>
#include <iostream>
static bool fixture_preflight(uintptr_t){return true;}
static unsigned disposed_objects{},disposed_controls{};
static void dispose_object(uintptr_t){++disposed_objects;}
static void dispose_control(uintptr_t){++disposed_controls;}
int main(){
    {
        using namespace sc6::research;
        auto source=std::make_unique<FullStateProbe>();
        auto target=std::make_unique<FullStateProbe>();
        std::memset(target.get(),0xA5,sizeof(*target));
        for(unsigned i=0;i<source->transforms.size();++i){
            auto& t=source->transforms[i];t.count=i;t.valid=true;t.failure=i;
            for(unsigned j=0;j<i;++j)std::memset(&t.records[j],0x30+i,sizeof(t.records[j]));
        }
        source->fault=true;source->saved.writes=17;
        assert(target->copy_recorded_from(*source));
        // Published prefixes are exact; unused capacity must not be copied.
        assert(target->fault && target->saved.writes==17);
        for(unsigned i=0;i<source->transforms.size();++i){
            const auto& t=source->transforms[i];
            assert(!std::memcmp(target->transforms[i].records.data(),t.records.data(),t.count*sizeof(TransformSnapshot::Record)));
            assert(target->transforms[i].records.back().bytes.back()==std::byte{0xA5});
        }
        source->saved.stream.write_position=3;source->saved.stream.payload[2]=std::byte{0x37};
        source->selectors[0].entries[0].bytes=5;source->selectors[0].entries[0].table[4]=std::byte{0x52};
        assert(target->copy_recorded_from(*source));
        assert(target->saved.stream.payload[2]==std::byte{0x37} && target->saved.stream.payload[3]==std::byte{0xA5});
        assert(target->selectors[0].entries[0].table[4]==std::byte{0x52} && target->selectors[0].entries[0].table[5]==std::byte{0xA5});
        source->saved.stream.write_position=SaveStream::limit+1;
        assert(!target->copy_recorded_from(*source) && target->saved.stream.write_position==3);
        source->saved.stream.write_position=3;
        source->selectors[0].entries[0].bytes=SelectorSnapshot::table_limit+1;
        assert(!target->copy_recorded_from(*source) && target->selectors[0].entries[0].bytes==5);
        source->selectors[0].entries[0].bytes=5;
        auto copied_expected=std::make_unique<FullStateProbe>(*source);
        for(auto& t:copied_expected->transforms)std::memset(t.records.data()+t.count,0xA5,(t.records.size()-t.count)*sizeof(TransformSnapshot::Record));
        for(auto& selector:copied_expected->selectors)for(auto& e:selector.entries)std::memset(e.table.data()+e.bytes,0xA5,e.table.size()-e.bytes);
        for(auto* stream:{&copied_expected->saved,&copied_expected->after_save,&copied_expected->replay,&copied_expected->restored})std::memset(stream->stream.payload.data()+stream->stream.write_position,0xA5,SaveStream::limit-stream->stream.write_position);
        assert(!std::memcmp(target.get(),copied_expected.get(),sizeof(*target)));
        auto& transform=target->transforms[1];
        transform.records.back().bytes.back()=std::byte{0x5A};
        assert(transform.copy_recorded_from(source->transforms[2]));
        assert(transform.count==2 && transform.valid && transform.failure==2);
        assert(!std::memcmp(transform.records.data(),source->transforms[2].records.data(),2*sizeof(TransformSnapshot::Record)));
        assert(transform.records.back().bytes.back()==std::byte{0x5A});
        assert(transform.copy_recorded_from(transform));
        source->transforms[2].count=unsigned(source->transforms[2].records.size()+1);
        assert(!transform.copy_recorded_from(source->transforms[2]) && transform.count==2);
        source->transforms[2].count=0;
        assert(transform.copy_recorded_from(source->transforms[2]) && transform.count==0);
        assert(target->copy_recorded_from(*target));
        source->transforms[0].count=unsigned(source->transforms[0].records.size()+1);
        target->saved.writes=99;
        assert(!target->copy_recorded_from(*source) && target->saved.writes==99);
        source->transforms[0].count=unsigned(source->transforms[0].records.size());
        source->transforms[0].records.back().bytes.back()=std::byte{0x77};
        assert(target->copy_recorded_from(*source));
        assert(target->transforms[0].records.back().bytes.back()==std::byte{0x77});
    }
    {
        using namespace sc6::research;
        const auto& defaults=FullStateProbe::reset_defaults();
        auto reused=std::make_unique<FullStateProbe>();
        std::memset(reused.get(),0xA5,sizeof(*reused));
        reused->reset_for_reuse();
        auto expected=std::make_unique<FullStateProbe>(defaults);
        for(auto& t:expected->transforms)std::memset(t.records.data(),0xA5,sizeof(t.records));
        for(auto& selector:expected->selectors)for(auto& e:selector.entries)e.table.fill(std::byte{0xA5});
        for(auto* stream:{&expected->saved,&expected->after_save,&expected->replay,&expected->restored})stream->stream.payload.fill(std::byte{0xA5});
        assert(!std::memcmp(reused.get(),expected.get(),sizeof(*reused)));
        std::array<std::byte,24> payload{};payload.fill(std::byte{0x37});
        auto& t=reused->transforms[0];
        assert(t.append(reinterpret_cast<uintptr_t>(payload.data()),1,9,0,0,24));
        assert(t.count==1 && !t.valid && t.records[0].size==24);
        for(unsigned i=0;i<512;++i)assert(t.records[0].bytes[i]==(i<24?std::byte{0x37}:std::byte{}));
        // Repeated reset must also discard stream cursors and diagnostic state.
        reused->saved.writes=17;reused->saved.sealed=true;reused->fault=true;
        reused->reset_for_reuse();
        assert(!reused->fault && !reused->saved.writes && !reused->saved.sealed);
        assert(reused->saved.stream.slots==&FullRestoreStream::table);
        assert(!t.count && !t.valid);
    }

    {
        using namespace sc6::research;
        struct Control {uintptr_t table{};LONG strong{1},weak{1};} control;
        auto history=std::make_unique<NativeRollingHistory<>>();
        RollbackBoundaryKey key{1,2,3,4,5,100,1,GetCurrentThreadId()};
        ReplayDependencySource unavailable;assert(!history->capture_owned(key,0,0,0,unavailable));
        auto capture=[&](auto& pool,const auto& handle,const auto&){
            if(!pool.retain_dependency(handle,reinterpret_cast<uintptr_t>(&control),dispose_object,dispose_control))return false;
            return pool.visit(handle,[](NativeBoundarySnapshot& snapshot){snapshot.valid=true;return snapshot.mode==NativeBoundarySnapshot::Mode::combat_capture_only;});
        };
        for(unsigned frame=100;frame<120;++frame){
            key.frame=frame;
            if(frame>=104){assert(!history->append(key,frame-4,capture));assert(control.strong==5);}
            assert(history->append(key,frame>103?frame-3:100,capture));
            assert(history->count()==(std::min)(4u,frame-99));
            assert(control.strong==static_cast<LONG>(history->count()+1));
        }
        auto pairFirst=key;pairFirst.frame=118;
        NativeBoundarySnapshot::Rejection pairReason{};
        assert(history->assess_pair(pairFirst,key,pairReason) && pairReason==NativeBoundarySnapshot::Rejection::thread);
        auto wrongPair=key;++wrongPair.round;
        assert(!history->assess_pair(pairFirst,wrongPair,pairReason));
        auto rewindKey=key;rewindKey.frame=117;
        assert(history->rewind_for_replay(rewindKey,[&](auto& snapshot){assert(control.strong==5);return snapshot.valid;}));
        assert(history->count()==1 && control.strong==2);
        assert(!history->append(key,116,capture)); // Must recapture the restored boundary first.
        for(unsigned f=117;f<120;++f){key.frame=f;assert(history->append(key,116,capture));}
        assert(history->count()==4 && control.strong==5);
        auto stale=key;stale.frame=115;assert(!history->visit(stale,[](auto&){return true;}));
        assert(history->visit(key,[](auto&){return true;}));
        auto wrong=key;++wrong.round;++wrong.frame;assert(!history->append(wrong,200,capture));
        std::thread foreign([&]{assert(!history->release());});foreign.join();assert(control.strong==5);
        assert(history->release() && control.strong==1 && history->failed());
        assert(!history->visit(key,[](auto&){return true;}));
        assert(history->release() && control.strong==1);
        auto broken=std::make_unique<NativeRollingHistory<>>();
        assert(!broken->append(key,0,[&](auto& pool,const auto& handle,const auto&){
            assert(pool.retain_dependency(handle,reinterpret_cast<uintptr_t>(&control),dispose_object,dispose_control));return false;
        }));
        assert(broken->failed() && control.strong==2);
        assert(broken->release() && control.strong==1);
        auto failedLoad=std::make_unique<NativeRollingHistory<>>();
        assert(failedLoad->append(key,0,capture));
        assert(!failedLoad->rewind_for_replay(key,[](auto&){return false;}));
        assert(failedLoad->failed() && control.strong==2);
        assert(failedLoad->release() && control.strong==1);
        auto partial=std::make_unique<NativeRollingHistory<>>();
        for(unsigned f=200;f<204;++f){key.frame=f;assert(partial->append(key,200,capture));}
        auto poisoned=key;poisoned.frame=202;
        assert(partial->visit(poisoned,[](auto& snapshot){snapshot.fault=true;return true;}));
        auto first=key;first.frame=200;
        assert(!partial->rewind_for_replay(first,[](auto&){return true;}));
        assert(partial->failed() && partial->count()==3 && control.strong==3);
        assert(partial->release() && control.strong==1);
    }
    using Pool=sc6::research::NativeSnapshotPool<2>;
    using B=sc6::research::NativeBoundarySnapshot;
    assert(B::supported_mode(0,0,65535,65535,1,-1,0,false,false)==B::Mode::idle);
    assert(B::supported_mode(1,0,65535,65535,1,-1,0,false,false)==B::Mode::combat_capture_only);
    assert(B::supported_mode(0,0,487,10041,0,-1,0,false,false)==B::Mode::combat_capture_only);
    assert(B::supported_mode(0,0,487,10041,0,-1,0,false,true)==B::Mode::throw_move);
    assert(B::supported_mode(0,0,219,65535,0,0,0,false,true)==B::Mode::combat_capture_only);
    assert(B::supported_mode(0,0,219,65535,0,0,0,true,true)==B::Mode::charge);
    assert(B::supported_mode(0,0,679,65535,0,0,0,false,true)==B::Mode::attack);
    assert(B::supported_mode(0,0,656,65535,0,0,0,false,true)==B::Mode::edge);
    assert(B::supported_mode(0,0,187,193,819.0f/16384.0f,-1,0,false,true)==B::Mode::reversal);
    assert(B::supported_mode(0,0,999,999,0,-1,0,true,true)==B::Mode::combat_capture_only);
    auto routing=std::make_unique<B>(B::Mode::charge);routing->select_mode(B::Mode::edge);
    assert(routing->data->edge_requested && !routing->data->charge_requested);
    routing->select_mode(B::Mode::combat_capture_only);assert(!routing->data->combat_requested());
    {
        struct Control {uintptr_t table{};LONG strong{1},weak{1};} control;
        sc6::research::GeneratedSnapshotDependency dependency;
        Pool generated_pool;auto key=generated_pool.acquire(4,1,1);assert(key);
        const auto address=reinterpret_cast<uintptr_t>(&control);
        assert(dependency.acquire(100,200,address,256,dispose_object,dispose_control));
        assert(control.strong==2);
        assert(!dependency.acquire(100,201,address,256,dispose_object,dispose_control));
        std::thread foreign([&]{assert(!dependency.bind(generated_pool,*key));assert(!dependency.reset());});foreign.join();
        assert(dependency.bind(generated_pool,*key) && control.strong==3);
        assert(dependency.reset() && control.strong==2);
        assert(generated_pool.retire(*key) && control.strong==1);
        assert(dependency.reset() && control.strong==1);
    }
    {
        struct Control {uintptr_t table{};LONG strong{1},weak{1};} control;
        sc6::research::DescriptorOwnershipBridge bridge;
        const auto address=reinterpret_cast<uintptr_t>(&control);
        assert(bridge.add(1,address,123,10,dispose_object,dispose_control));
        assert(bridge.add(1,address,123,10,dispose_object,dispose_control) && control.strong==2);
        assert(!bridge.add(1,address,456,10,dispose_object,dispose_control));
        assert(bridge.fresh(13) && !bridge.fresh(14) && !bridge.fresh(0));
        std::thread other([&]{assert(!bridge.reset());});other.join();
        {
            sc6::research::ResourceDependencyCapture scope(dispose_object,dispose_control);
            assert(!bridge.seed(11,[](uintptr_t,uintptr_t){return false;}));
            assert(bridge.seed(11,[](uintptr_t object,uintptr_t payload){return object==1 && payload==123;}));
            assert(scope.covers(123,0) && control.strong==3);
        }
        assert(control.strong==2 && bridge.reset() && control.strong==1 && !bridge.count());
        assert(bridge.reset() && control.strong==1);
    }
    {
        using Scope=sc6::research::ResourceDependencyCapture;
        struct Control {uintptr_t table{};LONG strong{1},weak{1};} control;
        Pool collected;auto key=collected.acquire(3,1,1);assert(key);
        {
            Scope scope(dispose_object,dispose_control);
            assert(!scope.covers(123,0));
            Scope::observe_owned(reinterpret_cast<uintptr_t>(&control),123);
            Scope::observe_owned(reinterpret_cast<uintptr_t>(&control),123);
            assert(control.strong==2 && scope.covers(123,0) && !scope.covers(123,456));
            std::thread foreign([&]{Scope::observe_owned(reinterpret_cast<uintptr_t>(&control),123);});foreign.join();
            assert(control.strong==2);
            assert(scope.bind(collected,*key) && control.strong==3);
        }
        assert(!Scope::active() && control.strong==2);
        assert(collected.retire(*key) && control.strong==1);
        {
            Scope scope(dispose_object,dispose_control);
            Scope::observe_owned(reinterpret_cast<uintptr_t>(&control),123);
            Scope::observe_owned(reinterpret_cast<uintptr_t>(&control),456);
            assert(!scope.covers(123,0));
        }
        assert(control.strong==1);
    }
    {
        struct Control {uintptr_t table{};LONG strong{1},weak{1};} controls[9];
        Pool owned;auto key=owned.acquire(2,1,0);assert(key);
        for(unsigned i=0;i<8;++i)assert(owned.retain_dependency(*key,reinterpret_cast<uintptr_t>(&controls[i]),dispose_object,dispose_control));
        assert(!owned.retain_dependency(*key,reinterpret_cast<uintptr_t>(&controls[0]),dispose_object,dispose_control));
        assert(!owned.retain_dependency(*key,reinterpret_cast<uintptr_t>(&controls[8]),dispose_object,dispose_control));
        assert(controls[8].strong==1);
        std::thread foreign([&]{assert(!owned.release_dependencies(*key));});foreign.join();
        assert(controls[0].strong==2);
        assert(owned.visit(*key,[&](B&){assert(!owned.release_dependencies(*key));return true;}));
        assert(owned.retire(*key));
        for(const auto& control:controls)assert(control.strong==1);
        assert(!owned.release_dependencies(*key));
        key=owned.acquire(2,1,1);assert(key);
        assert(owned.retain_dependency(*key,reinterpret_cast<uintptr_t>(&controls[0]),dispose_object,dispose_control));
        assert(owned.visit(*key,[](B& snapshot){snapshot.fault=true;return true;}));
        assert(!owned.retire(*key) && controls[0].strong==1);
        auto final=owned.acquire(2,1,2);assert(final);
        assert(owned.retain_dependency(*final,reinterpret_cast<uintptr_t>(&controls[1]),dispose_object,dispose_control));
        --controls[1].strong; // Native owner exits; snapshot is now sole owner.
        assert(owned.release_dependencies(*final));
        assert(!owned.visit(*final,[](B&){return true;}));
        assert(disposed_objects==1 && disposed_controls==1 && controls[1].strong==0);
        assert(owned.release_dependencies(*final) && disposed_objects==1);
    }
    Pool pool;auto a=pool.acquire(1,1,0),b=pool.acquire(1,1,1);
    assert(a && b && !pool.acquire(1,1,2) && !pool.acquire(1,1,0));
    uintptr_t storage{};
    assert(pool.visit(*a,[&](B& snapshot){
        storage=reinterpret_cast<uintptr_t>(snapshot.data.get());
        snapshot.attempted=snapshot.valid=true;snapshot.capture_stage=15;
        auto& saved=snapshot.data->saved;
        unsigned word=42;assert(saved.stream.slots->write(&saved.stream,&word,4)==4);assert(saved.seal());
        assert(!pool.retire(*a));return true;
    }));
    assert(pool.retire(*a));assert(!pool.visit(*a,[](B&){return true;}));
    auto fresh=pool.acquire(1,1,2,B::Mode::charge,2);assert(fresh && fresh->serial!=a->serial);
    assert(pool.visit(*fresh,[&](B& snapshot){
        assert(storage==reinterpret_cast<uintptr_t>(snapshot.data.get()));
        assert(!snapshot.attempted && !snapshot.valid && !snapshot.capture_stage);
        auto& q=*snapshot.data;assert(q.charge_requested && !q.throw_requested);
        assert(!q.saved.sealed && !q.saved.writes && !q.saved.stream.write_position);
        assert(q.saved.stream.slots==&sc6::research::FullRestoreStream::table);
        unsigned word=17;assert(q.saved.stream.slots->write(&q.saved.stream,&word,4)==4);
        return true;
    }));
    auto forged=*fresh;++forged.round;assert(!pool.retire(forged));
    std::thread other([&]{assert(!pool.retire(*fresh));assert(!pool.acquire(1,1,3));});other.join();
    assert(pool.retire(*fresh));
    auto combat=pool.acquire(1,1,2,B::Mode::combat_capture_only);assert(combat);
    assert(pool.visit(*combat,[&](B& source){
        assert(source.mode==B::Mode::combat_capture_only);
        assert(!source.data->throw_requested && !source.data->charge_requested && !source.data->attack_requested);
        return pool.visit(*b,[&](B& other){
            B::Rejection reason{};
            const auto writes=other.data->saved.writes;
            assert(!source.prepare_adjacent(other,*other.data,&reason) && reason==B::Rejection::mode);
            assert(!other.prepare_adjacent(source,*other.data,&reason) && reason==B::Rejection::mode);
            assert(!source.prepare_adjacent(source,*other.data,&reason) && reason==B::Rejection::mode);
            assert(other.data->saved.writes==writes && !other.data->saved.sealed && !other.data->restore_invoked);
            source.mode_values_valid=true;source.mode_values[2]=264;
            return true;
        });
    }));
    assert(pool.retire(*combat));
    for(unsigned frame=3;frame<40;++frame){auto h=pool.acquire(1,1,frame);assert(h);
        assert(pool.visit(*h,[&](B& snapshot){assert(storage==reinterpret_cast<uintptr_t>(snapshot.data.get()));assert(!snapshot.mode_values_valid && snapshot.mode_values[2]==0);return true;}));
        assert(pool.retire(*h));
    }
    assert(pool.visit(*b,[](B& snapshot){snapshot.fault=true;return true;}));
    assert(!pool.retire(*b));assert(!pool.visit(*b,[](B&){return true;}));
    auto last=pool.acquire(1,2,0);assert(last && !pool.acquire(1,2,1));
    assert(pool.visit(*last,[](B& snapshot){snapshot.data->restore_invoked=true;return true;}));
    assert(!pool.retire(*last));
    {
        // A sparse fake image exercises only mode sampling and invalid-envelope
        // rejection. No native serializer or game function is executed.
        auto image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x4900000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        assert(image);
        *reinterpret_cast<uint16_t*>(image+0x4717840)=0x55AA;
        *reinterpret_cast<uint16_t*>(image+0x47AEC30)=0x1122;
        *reinterpret_cast<uint16_t*>(image+0x475A04A)=0x1234;
        *reinterpret_cast<uint16_t*>(image+0x47F143A)=0x2345;
        *reinterpret_cast<float*>(image+0x48462F4)=0.5f;image[0x48462D0]=1;
        auto capture=std::make_unique<B>(B::Mode::combat_capture_only);
        assert(!capture->capture(reinterpret_cast<uintptr_t>(image),0,nullptr,fixture_preflight));
        assert(capture->capture_stage==3 && capture->mode_values_valid && capture->fault);
        assert(capture->mode_values[0]==0x55AA && capture->mode_values[2]==0x1234 && capture->mode_values[5]==1);
        assert(!capture->data->saved.sealed && !capture->data->saved.writes && !capture->data->restore_invoked);
        assert(VirtualFree(image,0,MEM_RELEASE));
    }
    {
        // Reproduce the second-cycle failure using the real pool: terminal
        // release quarantines all four slots even if retire returns success.
        sc6::research::NativeSnapshotPool<4> terminal_pool;
        for(unsigned frame=1;frame<=4;++frame){auto h=terminal_pool.acquire(1,1,frame);assert(h);assert(terminal_pool.release_dependencies(*h));assert(terminal_pool.retire(*h));}
        assert(!terminal_pool.acquire(1,1,5));
        // Normal successful retirement must preserve reusable storage.
        sc6::research::NativeSnapshotPool<4> reusable;
        for(unsigned cycle=0;cycle<3;++cycle){
            std::array<sc6::research::NativeSnapshotPool<4>::Handle,4> handles;
            for(unsigned n=0;n<4;++n){auto h=reusable.acquire(1,1,cycle*4+n+1);assert(h);handles[n]=*h;}
            for(const auto& h:handles)assert(reusable.retire(h));
        }
    }
    {
        sc6::research::NativeSnapshotPool<1> completed;
        auto h=completed.acquire(9,1,1);assert(h);
        assert(!completed.discard_drained_storage()); // a live handle cannot be discarded
        assert(completed.release_dependencies(*h));
        assert(completed.discard_drained_storage());
        assert(completed.discard_drained_storage()); // terminal cleanup is idempotent
        assert(!completed.visit(*h,[](auto&){return true;}));
        assert(!completed.acquire(10,1,1)); // a new match must use a new pool
    }
    std::cout<<"PASS: native snapshot in-place reuse, stale handle/identity/thread rejection, retention, poisoned slots and terminal storage release\n";
}
