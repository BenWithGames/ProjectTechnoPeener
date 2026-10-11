#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <ostream>
namespace sc6::research {
// One simulation-thread producer, one logging-worker consumer. No disk I/O or
// allocation on the simulation thread. Overflow is counted, never concealed.
struct RollbackEvent {
    enum Kind : unsigned {begin=1,arrival,correction_begin,correction_end,wait,advance,failure};
    uint64_t sequence{},session{},round{},frame{},head{};
    unsigned kind{},detail{};
};
template<unsigned Capacity=4096> class RollbackJournal {
    static_assert(Capacity>1);
    std::array<RollbackEvent,Capacity> events_{};
    std::atomic<uint64_t> written_{},read_{},dropped_{};
    uint64_t sequence_{};
public:
    void push(uint64_t session,uint64_t round,uint64_t frame,uint64_t head,unsigned kind,unsigned detail=0){
        const auto sequence=sequence_++;
        const auto w=written_.load(std::memory_order_relaxed),r=read_.load(std::memory_order_acquire);
        if(w-r>=Capacity){dropped_.fetch_add(1,std::memory_order_relaxed);return;}
        events_[w%Capacity]={sequence,session,round,frame,head,kind,detail};
        written_.store(w+1,std::memory_order_release);
    }
    bool pop(RollbackEvent& out){
        const auto r=read_.load(std::memory_order_relaxed);
        if(r==written_.load(std::memory_order_acquire))return false;
        out=events_[r%Capacity];read_.store(r+1,std::memory_order_release);return true;
    }
    uint64_t dropped()const{return dropped_.load(std::memory_order_relaxed);}
    static void header(std::ostream& out){out<<"sequence,session,round,frame,head,kind,detail\n";}
    static void row(std::ostream& out,const RollbackEvent& e){
        out<<e.sequence<<','<<e.session<<','<<e.round<<','<<e.frame<<','<<e.head<<','<<e.kind<<','<<e.detail<<'\n';
    }
};
}
