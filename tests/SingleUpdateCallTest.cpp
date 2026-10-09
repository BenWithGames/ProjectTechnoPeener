#include "../src/SingleUpdateCall.hpp"
#include <cstdlib>
#include <iostream>
using sc6::research::SingleUpdateCall;
static uint32_t counter{};static unsigned calls{};
static void update(uintptr_t arg){if(arg!=17)std::abort();++calls;++counter;}
static void twice(uintptr_t){++calls;counter+=2;}
static void crash(uintptr_t){RaiseException(0xE0000001,0,0,nullptr);}
static void require(bool ok){if(!ok)std::abort();}
int main(){
    SingleUpdateCall good;require(good.run(update,17,&counter,0,GetCurrentThreadId()));
    require(calls==1 && counter==1 && good.completed);
    require(!good.run(update,17,&counter,1,GetCurrentThreadId()) && calls==1);
    SingleUpdateCall stale;require(!stale.run(update,17,&counter,0,GetCurrentThreadId()) && calls==1);
    SingleUpdateCall thread;require(!thread.run(update,17,&counter,1,GetCurrentThreadId()+1) && calls==1);
    SingleUpdateCall missing;require(!missing.run(nullptr,17,&counter,1,GetCurrentThreadId()));
    SingleUpdateCall wrong;require(!wrong.run(twice,17,&counter,1,GetCurrentThreadId()) && wrong.fault);
    SingleUpdateCall exception;require(!exception.run(crash,17,&counter,counter,GetCurrentThreadId()) && exception.exception==0xE0000001);
    counter=0xffffffffu;SingleUpdateCall wrap;require(wrap.run(update,17,&counter,counter,GetCurrentThreadId()) && counter==0);
    std::cout<<"PASS: one synchronous update, no retry, thread and counter guards, excess advance, exception latch, counter wrap\n";
}
