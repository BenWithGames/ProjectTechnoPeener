#pragma once
#include "NativeInputWords.hpp"
#include <array>
#include <cstdint>
#include <optional>

namespace sc6::research {
// Owned raw-input history. No native ring, cursor, listener or pointer writes.
// The caller supplies resolved native filter decisions (3FCD10); raw input
// must not be reconstructed from a filtered held/pressed envelope.
template<unsigned Capacity=16,unsigned Limit=3> class PredictionInputDelivery {
    static_assert(Capacity>Limit && Limit>0);
public:
    struct Filter { uint32_t disabled{}; bool suppress_edges{},suppress_held{},restrict_buttons{}; };
    struct Frame {
        uint64_t number{};
        std::array<uint32_t,2> raw{};
        std::array<Filter,2> filters{};
        std::array<uint32_t,10> envelope{};
        uint32_t actual{};
        bool confirmed{};
    };
    enum class Arrival {accepted,duplicate,wrong_identity,not_retained,conflict};
private:
    std::array<Frame,Capacity> frames_{};
    std::array<uint32_t,2> seed_{};
    uint64_t session_{},round_{},first_{},head_{},published_{},revision_{},applied_{};
    unsigned count_{},remote_{};
    bool bound_{},failed_{},handoff_{};
    bool fail(){failed_=true;return false;}
    void compose(){
        auto previous=seed_;
        for(unsigned i=0;i<count_;++i){auto& f=frames_[i];
            f.raw[remote_]=f.confirmed?f.actual:previous[remote_];
            for(unsigned slot=0;slot<2;++slot){const auto& p=f.filters[slot];
                const auto words=NativeInputWords::compose(previous[slot],f.raw[slot],p.disabled,
                    p.suppress_edges,p.suppress_held,p.restrict_buttons);
                f.envelope[slot*2]=words.held;f.envelope[slot*2+1]=words.pressed;
            }
            previous=f.raw;
        }
    }
    bool changed(){if(revision_==UINT64_MAX)return fail();++revision_;compose();return true;}
public:
    bool begin(uint64_t session,uint64_t round,unsigned remote_slot,std::array<uint32_t,2> previous_raw){
        if(failed_ || !session || !round || remote_slot<1 || remote_slot>2 ||
            (bound_ && (!handoff_ready() || (session_==session && round_==round))))return false;
        frames_={};seed_=previous_raw;session_=session;round_=round;remote_=remote_slot-1;
        first_=head_=published_=revision_=applied_=0;count_=0;handoff_=false;bound_=true;return true;
    }
    bool append(uint32_t local_raw,std::optional<uint32_t> remote_raw,
        const std::array<Filter,2>& filters,const std::array<uint32_t,10>& auxiliary){
        if(!bound_ || failed_ || handoff_ || head_==UINT64_MAX)return false;
        unsigned unresolved=0;
        for(unsigned i=0;i<count_;++i)if(!frames_[i].confirmed){unresolved=count_-i;break;}
        if(unresolved>=Limit)return false;
        if(count_==Capacity){
            // Preserve the previous raw value when retiring a prefix. No
            // unconsumed correction or unpublished input may be overwritten.
            if(applied_!=revision_ || published_<=first_ || !frames_[0].confirmed)return false;
            seed_=frames_[0].raw;
            for(unsigned i=1;i<count_;++i)frames_[i-1]=frames_[i];
            --count_;++first_;
        }
        auto& f=frames_[count_++];f={};f.number=head_++;f.filters=filters;f.envelope=auxiliary;
        f.raw[remote_^1u]=local_raw;f.confirmed=remote_raw.has_value();f.actual=remote_raw.value_or(0);
        return changed();
    }
    Arrival confirm(uint64_t session,uint64_t round,uint64_t frame,uint32_t raw){
        if(!bound_ || failed_ || session!=session_ || round!=round_)return Arrival::wrong_identity;
        if(frame<first_ || frame>=head_)return Arrival::not_retained;
        auto& f=frames_[static_cast<unsigned>(frame-first_)];
        if(f.confirmed){if(f.actual==raw)return Arrival::duplicate;fail();return Arrival::conflict;}
        f.confirmed=true;f.actual=raw;
        if(!changed())return Arrival::conflict;
        return Arrival::accepted;
    }
    const Frame* frame(uint64_t number)const{
        if(!bound_ || failed_ || number<first_ || number>=head_)return nullptr;
        return &frames_[static_cast<unsigned>(number-first_)];
    }
    uint64_t revision()const{return revision_;}
    uint64_t head()const{return head_;}
    // Token must be captured before applying the complete corrected suffix.
    // Receiving packets alone must never make handoff ready.
    bool reconciled(uint64_t token,uint64_t through){
        if(!bound_ || failed_ || token!=revision_ || through!=head_)return false;
        applied_=token;return true;
    }
    template<class Publish> bool publish_confirmed(Publish publish){
        if(!bound_ || failed_)return false;
        while(published_<head_){const auto& f=frames_[static_cast<unsigned>(published_-first_)];
            if(!f.confirmed)break;
            // No prediction or filtered envelope goes to the recorder.
            // Callback failure is terminal: it may have partially published.
            try {if(!publish(session_,round_,f.number,f.raw))return fail();}
            catch(...){return fail();}
            ++published_;
        }
        return true;
    }
    bool request_handoff(){if(!bound_ || failed_)return false;handoff_=true;return true;}
    bool handoff_ready()const{
        return bound_ && !failed_ && handoff_ && applied_==revision_ && published_==head_;
    }
    bool failed()const{return failed_;}
};
}
