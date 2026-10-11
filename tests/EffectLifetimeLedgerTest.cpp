#include "../src/EffectLifetimeLedger.hpp"
#include "../src/EffectRetentionWindow.hpp"
#include <cassert>
#include "../src/EffectObjectRegistry.hpp"
#include <cstdio>
using Ledger=sc6::research::EffectLifetimeLedger<2,2>;
static void retention_tests(){
    using Window=sc6::research::EffectRetentionWindow<2,2>;
    Window window;
    unsigned freed=0;
    auto free_object=[&](Window::Allocation) noexcept {++freed;};
    assert(!window.track({0x1000,1,16}).generation);
    assert(window.begin(1,1) && !window.begin(2,1));
    auto a=window.track({0x1000,1,16});
    auto b=window.track({0x2000,1,16});
    std::array<Window::Token,2> invalid{a,{0x3000,99}};
    assert(!window.capture(1,invalid).generation);
    // Failed capture leaves no partial pin on a.
    assert(window.retire(a) && window.collect(free_object)==1 && freed==1);
    auto reused=window.track({0x1000,1,16});assert(reused.generation!=a.generation);
    std::array<Window::Token,2> pair{reused,b};
    auto first=window.capture(2,pair);auto second=window.capture(3,pair);
    assert(first.generation && second.generation && !window.capture(4,pair).generation);
    assert(!window.capture(2,pair).generation && !window.retained(first,a));
    bool foreign_rejected=false;
    std::thread other([&]{foreign_rejected=!window.retire(reused) && !window.expire(first,free_object);});other.join();assert(foreign_rejected);
    assert(window.retire(reused) && !window.retire(reused));
    assert(window.collect(free_object)==0 && !window.finish(free_object));
    assert(window.expire(first,free_object) && freed==1);
    assert(!window.expire(first,free_object));
    assert(window.restored(second,reused)); // A successfully restored object is game-owned again.
    assert(window.expire(second,free_object) && freed==1);
    auto next=window.capture(4,pair);assert(next.generation && next.generation!=first.generation);
    assert(!window.retained(first,reused));
    auto forged=next;forged.round++;assert(!window.expire(forged,free_object));
    assert(window.retire(reused) && window.expire(next,free_object) && freed==2);
    assert(window.finish(free_object) && freed==2); // b remains game-owned, never freed.
    assert(window.begin(2,1));
    auto new_session=window.track({0x1000,1,16});
    assert(new_session.generation!=reused.generation && !window.retire(reused));
    assert(window.finish(free_object) && freed==2);
}
int main(){
    retention_tests();
    {
        using Window=sc6::research::EffectRetentionWindow<3,3>;
        Window window;assert(window.begin(1,1));
        auto first=window.track({0x1000,1,16});auto other=window.track({0x2000,1,16});
        std::array both{first,other};std::array only_other{other};
        auto shared=window.capture(1,both);auto independent=window.capture(2,only_other);
        assert(shared.generation && independent.generation);
        assert(window.retire(other));
        bool denied=false;
        std::thread foreign([&]{denied=!window.unexpected_release(first);});foreign.join();
        assert(denied && !window.failed() && window.retained(shared,first));
        assert(window.unexpected_release(first) && window.failed());
        assert(!window.retained(shared,first) && !window.restored(shared,first));
        assert(!window.restored(independent,other) && !window.capture(3,only_other).generation);
        assert(!window.track({0x3000,1,16}).generation);
        unsigned frees=0;
        auto release=[&](Window::Allocation allocation) noexcept {assert(allocation.address==other.address);++frees;};
        assert(window.collect(release)==0); // Independent snapshot still pins other.
        assert(!window.expire(shared,release) && !window.finish(release));
        assert(window.expire(independent,release) && frees==1);
        assert(window.finish(release) && frees==1); // Unexpected native free is never repeated.
        assert(window.begin(2,1) && !window.failed());
        auto reused=window.track({first.address,1,16});assert(reused.generation!=first.generation);
        assert(window.finish(release));
    }
    {
        using Registry=sc6::research::EffectObjectRegistry<2,2>;
        Registry registry;Registry::Window window;assert(window.begin(1,1));
        assert(registry.allocation(0x1000,0x180,0x2FC720,0x10000000));
        auto token=registry.constructed(0x1000,0x13E88D18,true,window);
        std::array tokens{token};auto snapshot=window.capture(1,tokens);assert(snapshot.generation);
        assert(registry.unexpected_release(0x1000,window));
        assert(!registry.lookup(0x1000).generation && window.failed());
        assert(!registry.unexpected_release(0x1000,window));
        assert(window.finish([](Registry::Window::Allocation) noexcept {assert(false);}));
    }
    {
        using Registry=sc6::research::EffectObjectRegistry<2,2>;
        Registry registry;Registry::Window window;assert(window.begin(1,1));
        constexpr uintptr_t module=0x10000000;
        assert(!registry.allocation(0x1000,0x180,0x334A1E,module));
        assert(registry.allocation(0x1000,0x1E0,0x334A1E,module));
        assert(!registry.lookup(0x1000).generation);
        assert(!registry.constructed(0x1000,module+0x3E88CE8,false,window).generation);
        assert(!registry.constructed(0x1000,module+0x3E88D18,true,window).generation);
        auto token=registry.constructed(0x1000,module+0x3E88CE8,true,window);
        assert(token.generation && registry.lookup(0x1000).generation==token.generation);
        assert(registry.constructed(0x1000,module+0x3E88CE8,true,window).generation==token.generation);
        assert(!registry.allocation(0x1008,0x180,0x2FC720,module));
        assert(registry.allocation(0x2000,0x180,0x2FC720,module));
        assert(!registry.allocation(0x3000,0x180,0x2FC720,module));
        assert(window.retire(token));
        assert(window.collect([&](Registry::Window::Allocation allocation) noexcept {assert(registry.released(allocation.address));})==1);
        assert(registry.allocation(0x1000,0x180,0x2FC720,module));
        auto reused=registry.constructed(0x1000,module+0x3E88D18,true,window);
        assert(reused.generation && reused.generation!=token.generation && !window.retire(token));
        assert(registry.released(0x2000)); // Pending allocation can be freed before registration.
        assert(!registry.released(0x2000));
    }
    Ledger ledger;
    unsigned releases=0;
    auto release=[&](Ledger::Allocation a) noexcept {assert(a.address && a.bytes && a.vtable);++releases;};
    const auto a=ledger.track({0x10000,0x20000,0x1e0});
    assert(a.generation && ledger.pin(a,0) && ledger.pin(a,1));
    assert(!ledger.track({a.address,0x20000,0x1e0}).generation);
    assert(!ledger.track({a.address+8,0x20000,0x10}).generation);
    assert(!ledger.track({a.address-8,0x20000,0x10}).generation);
    assert(ledger.retire(a) && !ledger.retire(a));
    assert(!ledger.pin(a,0) && ledger.collect(release)==0);
    assert(ledger.release_snapshot(0) && ledger.collect(release)==0);
    assert(!ledger.revive(a,0) && ledger.revive(a,1));
    assert(ledger.release_snapshot(1) && ledger.collect(release)==0); // Live object stays owned by game.
    assert(ledger.retire(a) && ledger.collect(release)==1);
    assert(ledger.collect(release)==0 && releases==1);
    const auto reused=ledger.track({a.address,0x30000,0x180});
    assert(reused.generation!=a.generation && !ledger.pin(a,0) && !ledger.retire(a));
    const auto b=ledger.track({0x40000,0x50000,0x130});
    assert(b.generation && ledger.size()==2);
    assert(!ledger.track({0x60000,0x70000,0x130}).generation); // Bounded exhaustion.
    assert(!ledger.pin(b,2) && !ledger.release_snapshot(2));
    assert(ledger.pin(b,0) && ledger.pin(b,0)); // Same snapshot is idempotent.
    assert(ledger.retire(b) && ledger.collect(release)==0);
    assert(ledger.release_snapshot(0) && ledger.collect(release)==1);
    assert(ledger.retire(reused) && ledger.collect(release)==1 && releases==3);
    assert(!ledger.track({0,1,1}).generation && !ledger.track({1,0,1}).generation);
    assert(!ledger.track({1,1,0}).generation && !ledger.track({UINTPTR_MAX,1,2}).generation);
    assert(!ledger.retained_by({},0) && !ledger.revive(a,0) && ledger.size()==0);
    sc6::research::EffectLifetimeLedger<1,64> wide;
    const auto w=wide.track({1,2,3});
    assert(wide.pin(w,63) && wide.retained_by(w,63) && !wide.pin(w,64));
    Ledger session;
    const auto retired=session.track({0x10000,0x20000,0x100});
    const auto live=session.track({0x10100,0x20000,0x100}); // Adjacent ranges are allowed.
    assert(retired.generation && live.generation && session.retire(retired));
    assert(session.pin(live,0));
    unsigned session_releases=0;
    auto release_session=[&](Ledger::Allocation allocation) noexcept {
        assert(allocation.address==retired.address);++session_releases;
    };
    assert(!session.finish_session(release_session));
    assert(session_releases==0 && session.size()==2); // Late pinned entry prevents earlier release.
    assert(session.release_snapshot(0) && session.finish_session(release_session));
    assert(session_releases==1 && session.size()==0); // Live allocation was not freed.
    assert(session.finish_session(release_session) && session_releases==1);
    const auto next_session=session.track({retired.address,0x20000,0x100});
    assert(next_session.generation!=retired.generation && !session.retire(retired));
    assert(!session.retire(live));
    assert(session.finish_session(release_session) && session_releases==1);
    std::puts("Effect lifetime ledger tests passed (no native hooks).");
}
