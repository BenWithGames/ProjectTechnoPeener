#pragma once
#include "PredictionInputDelivery.hpp"
#include <Windows.h>
namespace sc6::research {
// A single owner-thread history view used by both consumers. No pointers to
// native input rings are retained. Its identity cannot be rebound across rounds.
class RawInputHistoryView {
public:
    using Delivery=PredictionInputDelivery<>;
    using Envelope=std::array<uint32_t,10>;
private:
    const Delivery* history_{};
    uint64_t session_{},round_{},event_{};
    uint32_t origin_{},owner_{};
    bool retired_{};
public:
    bool bind(const Delivery& history,uint64_t session,uint64_t round,uint64_t event,
              uint32_t origin,uint32_t owner){
        if(history_ || retired_ || !history.identity(session,round) || !session || !round || !event ||
           !owner || owner!=GetCurrentThreadId())return false;
        history_=&history;session_=session;round_=round;event_=event;origin_=origin;owner_=owner;return true;
    }
    bool identity(uint64_t session,uint64_t round,uint64_t event)const{
        return owner_==GetCurrentThreadId() && history_ && !retired_ && history_->identity(session_,round_) &&
            session==session_ && round==round_ && event==event_;
    }
    bool envelope(uint64_t session,uint64_t round,uint64_t event,uint32_t frame,Envelope& out)const{
        out={};if(!identity(session,round,event) || frame<origin_)return false;
        const auto* f=history_->frame(uint64_t(frame-origin_));if(!f || !f->complete)return false;
        out=f->envelope;return true;
    }
    bool raw(uint64_t session,uint64_t round,uint64_t event,uint32_t frame,unsigned slot,uint32_t& out)const{
        out=0;if(slot>1 || !identity(session,round,event) || frame<origin_)return false;
        const auto* f=history_->frame(uint64_t(frame-origin_));if(!f)return false;
        out=f->raw[slot];return true;
    }
    bool retire(){if(!history_ || owner_!=GetCurrentThreadId() || !history_->handoff_ready())return false;retired_=true;return true;}
};
}
