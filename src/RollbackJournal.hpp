#pragma once
#include <array>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <ostream>
namespace sc6::research {
// One simulation-thread producer, one logging-worker consumer. No disk I/O or
// allocation on the simulation thread. Overflow is counted, never concealed.
struct RollbackEvent {
    enum Kind : unsigned {begin=1,arrival,correction_begin,correction_end,wait,advance,failure,admission,confirmation_delivery};
    uint64_t sequence{},session{},round{},frame{},head{};
    unsigned kind{},detail{};uint64_t qpc{},aux0{},aux1{};
};
template<unsigned Capacity=4096> class RollbackJournal {
    static_assert(Capacity>1);
    std::array<RollbackEvent,Capacity> events_{};
    std::atomic<uint64_t> written_{},read_{},dropped_{};
    uint64_t sequence_{};
public:
    void push(uint64_t session,uint64_t round,uint64_t frame,uint64_t head,unsigned kind,unsigned detail=0,uint64_t aux0=0,uint64_t aux1=0){
        const auto sequence=sequence_++;
        const auto w=written_.load(std::memory_order_relaxed),r=read_.load(std::memory_order_acquire);
        if(w-r>=Capacity){dropped_.fetch_add(1,std::memory_order_relaxed);return;}
        LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);
        events_[w%Capacity]={sequence,session,round,frame,head,kind,detail,static_cast<uint64_t>(stamp.QuadPart),aux0,aux1};
        written_.store(w+1,std::memory_order_release);
    }
    bool pop(RollbackEvent& out){
        const auto r=read_.load(std::memory_order_relaxed);
        if(r==written_.load(std::memory_order_acquire))return false;
        out=events_[r%Capacity];read_.store(r+1,std::memory_order_release);return true;
    }
    uint64_t dropped()const{return dropped_.load(std::memory_order_relaxed);}
    static void header(std::ostream& out){out<<"sequence,session,round,frame,head,kind,detail,qpc,aux0,aux1\n";}
    static void row(std::ostream& out,const RollbackEvent& e){
        out<<e.sequence<<','<<e.session<<','<<e.round<<','<<e.frame<<','<<e.head<<','<<e.kind<<','<<e.detail<<','<<e.qpc<<','<<e.aux0<<','<<e.aux1<<'\n';
    }
};
}
