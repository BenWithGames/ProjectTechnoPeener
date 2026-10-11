#pragma once
#include <atomic>
#include "NativeRangeImage.hpp"
namespace sc6::research {
// Adapter for the verified 301820 save helper (RCX, stream RDX, vector R8).
// Capture after its return to 3844AC, before the parent frees the vector.
// No hook is installed here. Keep this object alive until all callers exit.
template<unsigned Capacity=512> class NativeRangeCaptureBridge {
    SRWLOCK lock_=SRWLOCK_INIT;
    std::atomic<uint64_t> epoch_{};
    uint64_t serial_{};
    DWORD owner_{};uintptr_t stream_{},caller_{};
    NativeRangeImage<Capacity> image_;
    unsigned captured_{},failed_{};
public:
    std::atomic<unsigned> dropped{};
    bool arm(uintptr_t stream,uintptr_t caller){
        if(!stream || !caller)return false;
        AcquireSRWLockExclusive(&lock_);
        if(epoch_.load()){ReleaseSRWLockExclusive(&lock_);return false;}
        owner_=GetCurrentThreadId();stream_=stream;caller_=caller;
        image_={};captured_=failed_=0;dropped=0;
        if(!++serial_)++serial_;epoch_=serial_;
        ReleaseSRWLockExclusive(&lock_);return true;
    }
    template<class Invoke> uintptr_t observe(uintptr_t context,uintptr_t stream,uintptr_t vector,uintptr_t caller,Invoke&& invoke){
        const auto epoch=epoch_.load();
        const auto result=invoke(context,stream,vector);
        const auto error=GetLastError();
        if(epoch){
            if(TryAcquireSRWLockExclusive(&lock_)){
                if(epoch_.load()==epoch && owner_==GetCurrentThreadId() && stream_==stream && caller_==caller){
                    if(captured_ || failed_)++dropped;
                    else if(image_.capture(vector))++captured_;else ++failed_;
                }
                ReleaseSRWLockExclusive(&lock_);
            }else ++dropped;
        }
        SetLastError(error);return result;
    }
    bool finish(NativeRangeImage<Capacity>& output){
        AcquireSRWLockExclusive(&lock_);
        epoch_=0;
        const bool ok=captured_==1 && !failed_ && !dropped.load();
        output={};if(ok)output=image_;
        ReleaseSRWLockExclusive(&lock_);return ok;
    }
};
}
