#pragma once
#include <Windows.h>
#include <climits>
namespace sc6::research {
// Caller must own a strong reference throughout this bounded probe.
// Never uses a previously captured pointer, and never performs last-owner destruction.
inline bool probe_resource_reference(volatile LONG* strong,LONG& before,LONG& held,LONG& released) noexcept {
    before=held=released=0;
    LONG observed=InterlockedCompareExchange(strong,0,0);
    for(unsigned attempt=0;attempt<16;++attempt){
        if(observed<=0 || observed==LONG_MAX)return false;
        const auto previous=InterlockedCompareExchange(strong,observed+1,observed);
        if(previous==observed){before=observed;held=observed+1;break;}
        observed=previous;
    }
    if(!held)return false;
    // The caller's reference plus ours guarantee at least two strong references.
    // InterlockedDecrement balances precisely our increment, even with other owners.
    released=InterlockedDecrement(strong);
    return released>0;
}
}
