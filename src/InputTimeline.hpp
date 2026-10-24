#pragma once
#include "RollbackJournal.hpp"
#include <memory>
namespace sc6::research {
// Multiple producers, one report worker. Never spin or perform I/O while locked.
// Diagnostic loss is preferable to delaying an input or simulation update.
template<unsigned Capacity=8192> class InputTimeline {
    std::unique_ptr<std::array<RollbackEvent,Capacity>> rows_=std::make_unique<std::array<RollbackEvent,Capacity>>();
    std::atomic_flag gate_=ATOMIC_FLAG_INIT;
    uint64_t written_{},read_{};
    std::atomic<uint64_t> busy_{},full_{};
public:
    enum Kind : unsigned {sample_begin=100,sample_end,published,send_begin,send_end,received,enqueued,drained,selected,consumed,admitted,deferred};
    void push(uint64_t session,uint64_t round,uint64_t frame,uint64_t event,unsigned kind,unsigned slot,uint64_t qpc,uint64_t raw,uint64_t context=0){
        if(gate_.test_and_set(std::memory_order_acquire)){++busy_;return;}
        if(written_-read_==Capacity)++full_;
        else {(*rows_)[written_%Capacity]={written_,session,round,frame,event,kind,slot,qpc,raw,context};++written_;}
        gate_.clear(std::memory_order_release);
    }
    void drain(std::ostream& events){
        std::array<RollbackEvent,128> batch;
        // Bounded worker pass; release the buffer before formatting or disk I/O.
        for(unsigned pass=0;pass<(Capacity+127)/128;++pass){
            if(gate_.test_and_set(std::memory_order_acquire))return;
            const auto n=(std::min)(uint64_t(batch.size()),written_-read_);
            for(uint64_t i=0;i<n;++i)batch[i]=(*rows_)[(read_+i)%Capacity];
            read_+=n;gate_.clear(std::memory_order_release);
            for(uint64_t i=0;i<n;++i)RollbackJournal<>::row(events,batch[i]);
            if(n<batch.size())return;
        }
    }
    void report(std::ostream& out)const{
        out<<"input_timeline_schema=1\ninput_timeline_capacity="<<Capacity
           <<"\ninput_timeline_contention_drops="<<busy_.load()<<"\ninput_timeline_full_drops="<<full_.load()<<'\n';
    }
};
}
