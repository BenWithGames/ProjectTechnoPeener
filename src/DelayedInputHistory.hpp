#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
namespace sc6::research {
// Bounded offline experiment, not a transport or an SC6 restore adapter.
// Values are owned opaque input envelopes; the caller controls player mapping.
template<class Input, std::size_t Capacity=256>
class DelayedInputHistory {
    struct Frame {Input actual{},used{};bool received{},simulated{};};
    std::array<Frame,Capacity> frames_{};
    Input neutral_{};
    std::size_t head_{};
    std::optional<std::size_t> dirty_;
public:
    enum class Arrival {accepted,duplicate,conflict,out_of_range};
    explicit DelayedInputHistory(Input neutral={}):neutral_(neutral){}
    std::size_t head()const{return head_;}
    std::optional<std::size_t> correction()const{return dirty_;}
    Input input_for(std::size_t frame)const {
        if(frame>=Capacity)return neutral_;
        for(std::size_t n=frame+1;n>0;--n)
            if(frames_[n-1].received)return frames_[n-1].actual;
        return neutral_;
    }
    Arrival receive(std::size_t frame,const Input& value){
        if(frame>=Capacity)return Arrival::out_of_range;
        auto& f=frames_[frame];
        if(f.received)return f.actual==value?Arrival::duplicate:Arrival::conflict;
        f.received=true;f.actual=value;
        // A late value may change predictions after its own frame, until the
        // next authoritative value. Future packets never predict the past.
        for(std::size_t n=frame;n<head_;++n){
            if(n>frame && frames_[n].received)break;
            if(frames_[n].simulated && frames_[n].used!=input_for(n)){
                if(!dirty_ || n<*dirty_)dirty_=n;
                break;
            }
        }
        return Arrival::accepted;
    }
    bool advance(Input& input){
        if(dirty_ || head_>=Capacity)return false;
        input=input_for(head_);auto& f=frames_[head_++];
        f.used=input;f.simulated=true;return true;
    }
    // Caller restores its snapshot before correction(), then replays every
    // affected frame through head()-1 before acknowledging success here.
    bool finish_correction(std::size_t first){
        if(!dirty_ || first!=*dirty_)return false;
        for(std::size_t n=first;n<head_;++n)frames_[n].used=input_for(n);
        dirty_.reset();return true;
    }
};
}
