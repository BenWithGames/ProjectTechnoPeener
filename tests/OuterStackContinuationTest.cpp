#include "../src/OuterStackContinuation.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <xmmintrin.h>
using Continuation=sc6::research::OuterStackContinuation;
struct Fixture {
    Continuation continuation;
    unsigned calls{},completed{},destroyed{},sum{};
    bool throw_after{},park{};
    static void routine(void* context,uintptr_t argument){
        auto& self=*static_cast<Fixture*>(context);++self.calls;
        assert(_MM_GET_ROUNDING_MODE()==_MM_ROUND_UP);
        struct Guard {unsigned& count;~Guard(){++count;}} guard{self.destroyed};
        volatile unsigned locals[4]={unsigned(argument),2,3,4};
        for(unsigned step=0;step<3;++step){
            locals[0]=locals[0]+step;
            SetLastError(1234+step);
            _MM_SET_ROUNDING_MODE(_MM_ROUND_DOWN);
            assert(self.continuation.suspend());
            assert(GetLastError()==1234+step);
            assert(_MM_GET_ROUNDING_MODE()==_MM_ROUND_DOWN);
            assert(locals[1]==2 && locals[2]==3 && locals[3]==4);
            self.sum+=locals[0];
        }
        if(self.park)self.continuation.park_failure();
        if(self.throw_after)throw std::runtime_error("unwind native-shaped stack");
        ++self.completed;
    }
};
int main(){
    const bool converted=!IsThreadAFiber();
    if(converted)assert(ConvertThreadToFiberEx(nullptr,FIBER_FLAG_FLOAT_SWITCH));
    Fixture f;assert(f.continuation.bind(GetCurrentThreadId(),&f,Fixture::routine));
    for(unsigned cycle=0;cycle<2;++cycle){
        for(unsigned poll=0;poll<4;++poll){
            _MM_SET_ROUNDING_MODE(_MM_ROUND_UP);SetLastError(700+poll);
            assert(f.continuation.run(10));
            assert(GetLastError()==700+poll && _MM_GET_ROUNDING_MODE()==_MM_ROUND_UP);
            if(poll<3){assert(f.continuation.has_native_frames());assert(!f.continuation.release());}
        }
        assert(f.continuation.state()==Continuation::State::complete);
        assert(f.completed==cycle+1 && f.destroyed==cycle+1);
    }
    assert(f.calls==2 && f.sum==68 && f.continuation.starts()==2 && f.continuation.resumes()==6 && f.continuation.suspensions()==6);
    assert(f.continuation.release());
    Fixture exception;exception.throw_after=true;
    assert(exception.continuation.bind(GetCurrentThreadId(),&exception,Fixture::routine));
    for(unsigned i=0;i<3;++i)assert(exception.continuation.run(20));
    assert(!exception.continuation.run(20) && exception.destroyed==1 && !exception.completed);
    assert(exception.continuation.failure()==Continuation::Failure::exception && !exception.continuation.has_native_frames());
    assert(exception.continuation.release());
    // These two instances intentionally retain their native-shaped stacks for
    // process lifetime. release must refuse to abandon either stack's Guard.
    auto* parked=new Fixture;parked->park=true;
    assert(parked->continuation.bind(GetCurrentThreadId(),parked,Fixture::routine));
    for(unsigned i=0;i<3;++i)assert(parked->continuation.run(30));
    assert(!parked->continuation.run(30) && parked->destroyed==0 && !parked->continuation.release());
    assert(!parked->continuation.run(30) && parked->continuation.failure()==Continuation::Failure::parked);
    auto* wrong=new Fixture;
    assert(wrong->continuation.bind(GetCurrentThreadId(),wrong,Fixture::routine));
    assert(wrong->continuation.run(40));
    std::thread other([&]{assert(!wrong->continuation.run(40));});other.join();
    assert(wrong->continuation.state()==Continuation::State::suspended);
    assert(!wrong->continuation.run(41) && wrong->continuation.failure()==Continuation::Failure::argument);
    assert(!wrong->continuation.release() && wrong->destroyed==0);
    if(converted)assert(ConvertFiberToThread());
    std::cout<<"PASS: real same-thread stack retention, loops, input locals, FP controls, LastError, reuse, unwind, identity, thread and failed-stack quarantine\n";
}
