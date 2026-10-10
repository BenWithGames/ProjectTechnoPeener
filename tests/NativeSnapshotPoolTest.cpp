#include "../src/NativeSnapshotPool.hpp"
#include <cassert>
#include <thread>
#include <iostream>
int main(){
    using Pool=sc6::research::NativeSnapshotPool<2>;
    using B=sc6::research::NativeBoundarySnapshot;
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
    for(unsigned frame=3;frame<40;++frame){auto h=pool.acquire(1,1,frame);assert(h);
        assert(pool.visit(*h,[&](B& snapshot){assert(storage==reinterpret_cast<uintptr_t>(snapshot.data.get()));return true;}));
        assert(pool.retire(*h));
    }
    assert(pool.visit(*b,[](B& snapshot){snapshot.fault=true;return true;}));
    assert(!pool.retire(*b));assert(!pool.visit(*b,[](B&){return true;}));
    auto last=pool.acquire(1,2,0);assert(last && !pool.acquire(1,2,1));
    assert(pool.visit(*last,[](B& snapshot){snapshot.data->restore_invoked=true;return true;}));
    assert(!pool.retire(*last));
    std::cout<<"PASS: native snapshot in-place reuse, stale handle/identity/thread rejection, retention and poisoned slots\n";
}
