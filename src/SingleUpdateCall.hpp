#pragma once
#include <Windows.h>
#include <cstdint>
namespace sc6::research {
// One synchronous invocation only. The caller must supply the original
// trampoline, never the hooked entry, and a live owned input envelope.
struct SingleUpdateCall {
    using Routine=void(*)(uintptr_t);
    bool attempted{},invoked{},completed{},fault{};
    unsigned exception{};uint32_t before{},after{};
    bool run(Routine routine,uintptr_t argument,volatile uint32_t* counter,
             uint32_t expected,DWORD owner) noexcept {
        if(attempted || fault)return false;
        attempted=true;
        if(!routine || !argument || !counter || owner!=GetCurrentThreadId()){fault=true;return false;}
        __try {
            before=*counter;
            if(before!=expected){fault=true;return false;}
            invoked=true;routine(argument);
            after=*counter;
            completed=after==uint32_t(before+1u);
            fault=!completed;
            return completed;
        }__except(EXCEPTION_EXECUTE_HANDLER){exception=GetExceptionCode();fault=true;return false;}
    }
};
}
