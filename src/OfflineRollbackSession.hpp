#pragma once
#include "DelayedInputHistory.hpp"
#include <array>
#include <optional>
namespace sc6::research {
// Offline controller. Adapter must implement save(Snapshot&), load(const
// Snapshot&), step(local,remote), all returning bool. No SC6 adapter is implied.
// step must be safe to call synchronously; do not bind to a hooked game update.
template<class Input,class Snapshot,std::size_t Capacity=256>
class OfflineRollbackSession {
    DelayedInputHistory<Input,Capacity> remote_;
    std::array<std::optional<Snapshot>,Capacity> before_;
    std::array<Input,Capacity> local_{};
    bool fault_{};
    std::size_t corrections_{},replayed_{};
public:
    using Arrival=typename DelayedInputHistory<Input,Capacity>::Arrival;
    bool fault()const{return fault_;}
    std::size_t head()const{return remote_.head();}
    std::size_t corrections()const{return corrections_;}
    std::size_t replayed()const{return replayed_;}
    Arrival receive(std::size_t frame,const Input& input){
        if(fault_)return Arrival::conflict;
        auto result=remote_.receive(frame,input);
        if(result==Arrival::conflict || result==Arrival::out_of_range)fault_=true;
        return result;
    }
    template<class Adapter> bool reconcile(Adapter& adapter){
        if(fault_)return false;
        auto first=remote_.correction();if(!first)return true;
        if(!before_[*first] || !adapter.load(*before_[*first])){fault_=true;return false;}
        for(auto f=*first;f<head();++f){
            before_[f].emplace();
            if(!adapter.save(*before_[f]) || !adapter.step(local_[f],remote_.input_for(f))){fault_=true;return false;}
            ++replayed_;
        }
        if(!remote_.finish_correction(*first)){fault_=true;return false;}
        ++corrections_;return true;
    }
    template<class Adapter> bool advance(Adapter& adapter,const Input& local){
        if(fault_ || head()>=Capacity)return false;
        if(!reconcile(adapter))return false;
        const auto f=head();before_[f].emplace();
        if(!adapter.save(*before_[f])){fault_=true;return false;}
        local_[f]=local;Input predicted{};
        if(!remote_.advance(predicted) || !adapter.step(local,predicted)){fault_=true;return false;}
        return true;
    }
};
}
