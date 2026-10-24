#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "../src/NativeSnapshotPool.hpp"
#include <cassert>
#include <cstdio>
#include <thread>
using namespace sc6::research;
unsigned disposed{};
void dispose(uintptr_t){++disposed;}
__declspec(noinline) void reset_full(FullStateProbe& s){s.reset_for_reuse();}
__declspec(noinline) void reset_compact(CompactBoundaryState& s){s.reset_for_reuse();}
__declspec(noinline) void reset_full_previous(FullStateProbe& s){
    const auto& defaults=FullStateProbe::reset_defaults();
    static const auto spans=[](){
        const auto& d=FullStateProbe::reset_defaults();std::array<FullStateProbe::StorageSpan,22> a{};size_t n=0;
        auto add=[&](const auto& v){a[n++]={size_t(reinterpret_cast<const std::byte*>(v.data())-reinterpret_cast<const std::byte*>(&d)),sizeof(v),0};};
        for(const auto& selector:d.selectors)for(const auto& e:selector.entries)add(e.table);
        for(const auto* stream:{&d.saved,&d.after_save,&d.replay,&d.restored})add(stream->stream.payload);
        for(const auto& t:d.transforms)add(t.records);
        std::sort(a.begin(),a.end(),[](const auto& x,const auto& y){return x.offset<y.offset;});return a;
    }();
    auto* dst=reinterpret_cast<std::byte*>(&s);const auto* src=reinterpret_cast<const std::byte*>(&defaults);size_t at=0;
    for(const auto& span:spans){std::memcpy(dst+at,src+at,span.offset-at);at=span.offset+span.capacity;}
    std::memcpy(dst+at,src+at,sizeof(s)-at);
}
int main(){
    auto state=std::make_unique<CompactBoundaryState>();
    auto expected=std::make_unique<CompactBoundaryState>(CompactBoundaryState::reset_defaults());
    size_t skipped=0;
    for(const auto& span:expected->storage_spans()){
        skipped+=span.capacity;
        std::memset(reinterpret_cast<std::byte*>(expected.get())+span.offset,0xA5,span.capacity);
    }
    std::memset(state.get(),0xA5,sizeof(*state));state->reset_for_reuse();
    assert(!std::memcmp(state.get(),expected.get(),sizeof(*state)));
    assert(!state->saved.sealed && !state->saved.writes && !state->transforms[0].count && !state->transforms[0].valid);
    assert(state->saved.stream.slots==&FullRestoreStream::table && state->saved.stream.copier==SaveProbe::guarded_copy);
    assert(!state->selector_enabled && !state->external_wind_lifecycle && !state->blend_timer_count && !state->fault);
    std::array<std::byte,19> payload{};payload[0]=std::byte{0x5B};
    assert(state->saved.stream.slots->write(&state->saved.stream,payload.data(),payload.size())==payload.size());
    assert(state->saved.traces[0].source==reinterpret_cast<uintptr_t>(payload.data()));
    assert(state->saved.traces[0].writer && !state->saved.traces[0].reader && !state->saved.traces[0].destination);
    assert(state->saved.seal() && state->saved.writes==1 && state->saved.stream.payload[0]==payload[0]);
    assert(state->saved.stream.payload[19]==std::byte{0xA5}); // unused tail is not published
    auto reader=std::make_unique<FullRestoreStream>();assert(reader->prepare_reader_from(state->saved));
    std::array<std::byte,19> result{};reader->stream.slots->read(&reader->stream,result.data(),result.size());
    assert(result==payload && reader->consumed() && !state->saved.reads);
    // A smaller capture after a consumed, poisoned previous image must expose
    // neither old read addresses nor old ring bytes as valid evidence.
    state->saved.traces[0].reader=0x1234;state->saved.traces[0].destination=0x5678;
    state->rings[0].bytes.fill(std::byte{0x6C});state->rings[0].valid=true;
    state->reset_for_reuse();assert(!state->rings[0].valid && !state->rings[0].same(state->rings[0]));
    assert(state->rings[0].bytes.front()==std::byte{0x6C});
    state->saved.stream.slots->write(&state->saved.stream,payload.data(),1);
    assert(state->saved.seal() && state->saved.writes==1 && state->saved.lengths[0]==1);
    assert(!state->saved.traces[0].reader && !state->saved.traces[0].destination);
    auto reader2=std::make_unique<FullRestoreStream>();assert(reader2->prepare_reader_from(state->saved));
    std::byte one{};reader2->stream.slots->read(&reader2->stream,&one,1);assert(one==payload[0] && reader2->consumed());
    using Pool=NativeSnapshotPool<2,CompactNativeBoundarySnapshot>;
    struct Control{uintptr_t table{};LONG strong{1},weak{1};} control;
    Pool pool;
    uintptr_t address{};
    for(unsigned frame=1;frame<=100;++frame){
        auto h=pool.acquire(1,2,frame,NativeBoundarySnapshot::Mode::general_combat);assert(h);
        assert(pool.retain_dependency(*h,reinterpret_cast<uintptr_t>(&control),dispose,dispose));assert(control.strong==2);
        assert(pool.visit(*h,[&](auto& s){
            if(!address)address=reinterpret_cast<uintptr_t>(s.data.get());assert(address==reinterpret_cast<uintptr_t>(s.data.get()));
            assert(!s.data->saved.writes && !s.data->fault && !s.data->blend_timer_count);
            s.data->saved.stream.slots->write(&s.data->saved.stream,payload.data(),payload.size());assert(s.data->saved.seal());
            return !pool.retire(*h); // borrowed storage cannot retire
        }));
        std::thread foreign([&]{assert(!pool.retire(*h));});foreign.join();
        assert(pool.retire(*h) && control.strong==1 && !disposed);
        assert(!pool.visit(*h,[](auto&){return true;}));
    }
    Pool next(&pool);assert(next.storage_reused);
    auto h=next.acquire(2,3,1);assert(h);assert(next.visit(*h,[&](auto& s){return reinterpret_cast<uintptr_t>(s.data.get())==address;}));
    assert(next.retire(*h));
    for(unsigned failure=0;failure<4;++failure){
        Pool broken;auto bad=broken.acquire(3,4,5);assert(bad);
        assert(broken.visit(*bad,[&](auto& s){if(failure==0)s.fault=true;if(failure==1)s.data->fault=true;if(failure==2)s.data->restore_invoked=true;if(failure==3)s.data->saved.stream.failures=1;return true;}));
        assert(!broken.retire(*bad));assert(!broken.reusable_storage());assert(broken.release_dependencies(*bad));
    }
    auto full=std::make_unique<FullStateProbe>();size_t full_skipped=0;for(const auto& span:full->storage_spans())full_skipped+=span.capacity;
    assert(sizeof(*state)-skipped < sizeof(*full)-full_skipped);
    LARGE_INTEGER hz,a,b,c;QueryPerformanceFrequency(&hz);reset_full(*full);reset_compact(*state);
    QueryPerformanceCounter(&a);for(unsigned i=0;i<1000;++i)reset_full(*full);QueryPerformanceCounter(&b);
    for(unsigned i=0;i<1000;++i)reset_compact(*state);QueryPerformanceCounter(&c);
    std::printf("Storage bytes full=%zu compact=%zu; reset bytes full=%zu compact=%zu\n",sizeof(*full),sizeof(*state),sizeof(*full)-full_skipped,sizeof(*state)-skipped);
    std::printf("Mock reset us: full=%.3f compact=%.3f (not live frame time)\n",double(b.QuadPart-a.QuadPart)*1000/hz.QuadPart,double(c.QuadPart-b.QuadPart)*1000/hz.QuadPart);
    QueryPerformanceCounter(&a);for(unsigned i=0;i<1000;++i)reset_full_previous(*full);QueryPerformanceCounter(&b);
    for(unsigned i=0;i<1000;++i)reset_full(*full);QueryPerformanceCounter(&c);
    std::printf("Same-run full workspace reset us: previous=%.3f optimized=%.3f (not total correction time)\n",double(b.QuadPart-a.QuadPart)*1000/hz.QuadPart,double(c.QuadPart-b.QuadPart)*1000/hz.QuadPart);
    std::puts("PASS: compact boundary reset, exact stream readback, unpublished tails, 100 ownership cycles, stale/borrowed/foreign handles, quarantine and round storage transfer");
}
