#pragma once
#include <Windows.h>
namespace sc6::research {
// Declared before the sender: its event outlives the sender worker. Auto-reset
// coalesces publications; archive cursors, not event counts, own delivery.
struct RawPublicationSignal {
    HANDLE event{CreateEventW(nullptr,FALSE,FALSE,nullptr)};
    ~RawPublicationSignal(){if(event)CloseHandle(event);}
    RawPublicationSignal()=default;
    RawPublicationSignal(const RawPublicationSignal&)=delete;
    RawPublicationSignal& operator=(const RawPublicationSignal&)=delete;
    void notify()const noexcept {if(event)SetEvent(event);}
};
inline RawPublicationSignal raw_publication_signal;
}
