#pragma once
#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <ostream>
namespace sc6::research {
// Optional owner-thread grace at the prediction limit only. No polling loop,
// native calls, input sampling, or mutation of the prediction budget here.
class ConfirmationReceiptGrace {
    HANDLE receipt_{},timer_{};
    std::atomic<bool> armed_{};
    std::atomic<uint64_t> waits_{},wakes_{},timeouts_{},faults_{},ticks_{},maximum_{};
public:
    // Paired traces show needed truth arriving 3.1--6.9 ms after a blocked
    // admission. Cover that observed tail without a full-frame wait.
    static constexpr unsigned budget_us=8000;
    static constexpr unsigned backstop_ms=9;
    ConfirmationReceiptGrace()=default;
    ConfirmationReceiptGrace(const ConfirmationReceiptGrace&)=delete;
    ~ConfirmationReceiptGrace(){if(receipt_)CloseHandle(receipt_);if(timer_)CloseHandle(timer_);}
    bool enable(){
        if(receipt_ && timer_)return true;
        receipt_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        timer_=CreateWaitableTimerExW(nullptr,nullptr,0x2,TIMER_ALL_ACCESS);
        if(receipt_ && timer_)return true;
        if(receipt_){CloseHandle(receipt_);receipt_=nullptr;}if(timer_){CloseHandle(timer_);timer_=nullptr;}
        ++faults_;return false; // Optional optimization; original withholding remains safe.
    }
    void notify(){if(armed_.load(std::memory_order_acquire))SetEvent(receipt_);}
    template<class Pending> void wait(Pending pending){
        if(!receipt_ || !timer_ || pending())return;
        ResetEvent(receipt_);armed_.store(true,std::memory_order_release);
        if(pending()){armed_.store(false,std::memory_order_release);return;}
        LARGE_INTEGER begin{},end{},due{};QueryPerformanceCounter(&begin);due.QuadPart=-static_cast<LONGLONG>(budget_us)*10;
        if(!SetWaitableTimer(timer_,&due,0,nullptr,nullptr,FALSE)){++faults_;armed_=false;return;}
        ++waits_;HANDLE handles[]={receipt_,timer_};
        const auto result=WaitForMultipleObjects(2,handles,FALSE,backstop_ms);
        armed_.store(false,std::memory_order_release);CancelWaitableTimer(timer_);QueryPerformanceCounter(&end);
        if(result==WAIT_OBJECT_0)++wakes_;else if(result==WAIT_OBJECT_0+1 || result==WAIT_TIMEOUT)++timeouts_;else ++faults_;
        if(end.QuadPart>=begin.QuadPart){const auto elapsed=static_cast<uint64_t>(end.QuadPart-begin.QuadPart);
            ticks_+=elapsed;auto old=maximum_.load();while(old<elapsed && !maximum_.compare_exchange_weak(old,elapsed)){} }
    }
    void write(std::ostream& out)const{
        LARGE_INTEGER f{};QueryPerformanceFrequency(&f);const auto scale=f.QuadPart>0?1e6/f.QuadPart:0.;
        out<<"receipt_grace_schema=2\nreceipt_grace_budget_us="<<(timer_?budget_us:0)<<"\nreceipt_grace_backstop_ms="<<(timer_?backstop_ms:0)<<"\nreceipt_grace_waits="<<waits_.load()
           <<"\nreceipt_grace_wakes="<<wakes_.load()<<"\nreceipt_grace_timeouts="<<timeouts_.load()
           <<"\nreceipt_grace_faults="<<faults_.load()<<"\nreceipt_grace_total_us="<<ticks_.load()*scale
           <<"\nreceipt_grace_max_us="<<maximum_.load()*scale<<'\n';
    }
};
}
