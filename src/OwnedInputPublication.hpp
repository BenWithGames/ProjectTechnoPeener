#pragma once
#include "InputObservation.hpp"
#include "CombatUpdateAdapter.hpp"
namespace sc6::research {
// Sole producer for an existing observation mapping during exclusive ownership.
// Publishes admitted live frames only; internal replay never enters this writer.
class OwnedInputPublication {
    observation::Shared* shared_{};
    uint64_t session_{},round_{};unsigned event_{},owner_{},next_{};
    bool failed_{};
    bool retired_{},transferred_{};LONG retired_count_{};
public:
    bool complete()const{return shared_ && shared_->status==2;}
    bool bind(observation::Shared& shared,uint64_t session,unsigned round,unsigned event,unsigned first){
        if(shared_ || !session || !round || shared.nonce!=session || shared.signature!=observation::magic ||
            shared.schema!=observation::version || shared.status!=1 || shared.count!=0 || !shared.frequency)return false;
        shared_=&shared;session_=session;round_=round;event_=event;owner_=GetCurrentThreadId();next_=first;return true;
    }
    bool retire(){
        if(!shared_ || failed_ || GetCurrentThreadId()!=owner_ || shared_->nonce!=session_ || shared_->status!=1)return false;
        if(retired_)return shared_->count==retired_count_;
        const auto count=shared_->count;
        const auto limit=shared_->reserved?observation::capacity:observation::limit;
        if(count<0 || static_cast<unsigned>(count)>=limit)return false;
        retired_count_=count;retired_=true;return true;
    }
    // Transfer the sole producer role without erasing records already visible
    // to the network worker. The previous producer must never publish again.
    bool bind_after(OwnedInputPublication& previous,observation::Shared& shared,uint64_t session,unsigned round,unsigned event,unsigned first){
        if(shared_ || !previous.retired_ || previous.transferred_ || previous.failed_ || previous.shared_!=&shared ||
            !round || first==UINT32_MAX || session!=previous.session_ || GetCurrentThreadId()!=previous.owner_ ||
            (round==previous.round_ && event==previous.event_))return false;
        if(shared.nonce!=session || shared.status!=1 || shared.count!=previous.retired_count_)return false;
        previous.transferred_=true;
        shared_=&shared;session_=session;round_=round;event_=event;owner_=GetCurrentThreadId();next_=first;return true;
    }
    bool publish(uint64_t session,uint64_t round,unsigned frame,const CombatFrameInput& input){
        if(!shared_ || failed_ || retired_)return false;
        const auto count=shared_->count;
        const auto limit=shared_->reserved?observation::capacity:observation::limit;
        if(GetCurrentThreadId()!=owner_ || session!=session_ || round!=round_ || frame!=next_ || frame==UINT32_MAX ||
            shared_->nonce!=session_ || shared_->status!=1 || count<0 || static_cast<unsigned>(count)>=limit){
            failed_=true;InterlockedExchange(&shared_->status,3);return false;
        }
        observation::Record record;record.data[0]=frame;
        std::memcpy(record.data.data()+1,input.envelope.data(),40);
        LARGE_INTEGER stamp{};
        if(!QueryPerformanceCounter(&stamp)){failed_=true;InterlockedExchange(&shared_->status,3);return false;}
        record.data[11]=static_cast<uint32_t>(stamp.QuadPart);record.data[12]=static_cast<uint32_t>(stamp.QuadPart>>32);
        shared_->records[count]=record;shared_->rounds[count]={static_cast<unsigned>(round_),event_};
        MemoryBarrier();InterlockedExchange(&shared_->count,count+1);++next_;
        if(static_cast<unsigned>(count+1)==limit)InterlockedExchange(&shared_->status,2);
        return true;
    }
};
}
