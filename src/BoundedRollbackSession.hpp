#pragma once
#include <array>
#include <cstdint>
#include <optional>
namespace sc6::research {
// Adapter-owned reusable snapshots; deliberately not wired to the SC6 hook.
// All calls belong to one simulation thread. Adapter failures are terminal:
// switching to stock simulation after a partially restored state is not valid.
template<class Input,class Snapshot,unsigned Limit=3,unsigned Capacity=16>
class BoundedRollbackSession {
    static_assert(Limit>0 && Capacity>Limit+1);
    struct Frame {
        uint64_t number{}; bool valid{},simulated{},confirmed{};
        Input local{},used{},actual{}; Snapshot before{};
    };
    std::array<Frame,Capacity> frames_{};
    uint64_t session_{},round_{},head_{},settled_{};
    Input seed_{};
    std::optional<uint64_t> dirty_;
public:
    enum class State {idle,running,waiting,restart_required};
    enum class Arrival {accepted,duplicate,wrong_identity,too_far,conflict,expired};
    State state{State::idle};
    uint64_t corrections{},replayed{},stalls{};
    uint64_t head()const{return head_;}
    // Only at an independently established clean round boundary.
    bool begin(uint64_t session,uint64_t round,const Input& neutral={}){
        if(!session || !round || state==State::restart_required)return false;
        frames_={};session_=session;round_=round;head_=settled_=0;seed_=neutral;dirty_.reset();
        state=State::running;return true;
    }
    Arrival receive(uint64_t session,uint64_t round,uint64_t frame,const Input& input){
        if(state==State::idle || state==State::restart_required || session!=session_ || round!=round_)return Arrival::wrong_identity;
        if(frame<head_ && head_-frame>Limit){
            // Never silently discard a correction to already simulated state.
            state=State::restart_required;return Arrival::expired;
        }
        if(frame>=head_ && frame-head_>=Capacity-Limit)return Arrival::too_far;
        auto& f=frames_[frame%Capacity];
        if(!f.valid || f.number!=frame){f={};f.valid=true;f.number=frame;}
        if(f.confirmed){
            if(f.actual==input)return Arrival::duplicate;
            state=State::restart_required;return Arrival::conflict;
        }
        f.confirmed=true;f.actual=input;
        if(f.simulated && !(f.used==input) && (!dirty_ || frame<*dirty_))dirty_=frame;
        return Arrival::accepted;
    }
    template<class Adapter> bool reconcile(Adapter& adapter){
        if(state==State::idle || state==State::restart_required)return false;
        if(dirty_){
            const auto first=*dirty_;
            if(first>=head_ || head_-first>Limit){state=State::restart_required;return false;}
            auto& start=frames_[first%Capacity];
            if(!start.valid || start.number!=first || !start.simulated || !adapter.load(start.before)){
                state=State::restart_required;return false;
            }
            Input held=seed_;
            if(first>settled_){const auto& prior=frames_[(first-1)%Capacity];
                if(!prior.valid || prior.number!=first-1){state=State::restart_required;return false;}
                held=prior.used;
            }
            for(auto n=first;n<head_;++n){auto& f=frames_[n%Capacity];
                if(!f.valid || f.number!=n || !f.simulated){state=State::restart_required;return false;}
                f.used=f.confirmed?f.actual:adapter.predict(held);held=f.used;
                if(!adapter.save(f.before) || !adapter.step(f.local,f.used)){state=State::restart_required;return false;}
                ++replayed;
            }
            ++corrections;dirty_.reset();
        }
        // Retire only a contiguous, corrected, confirmed prefix.
        while(settled_<head_){auto& f=frames_[settled_%Capacity];
            if(!f.valid || f.number!=settled_ || !f.confirmed)break;
            seed_=f.actual;++settled_;
        }
        state=State::running;return true;
    }
    template<class Adapter> bool advance(Adapter& adapter,const Input& local){
        if(!reconcile(adapter))return false;
        // Stall BEFORE an unconfirmed frame could become four frames old.
        // A real adapter must coordinate both simulation clocks; do not sleep
        // or skip the original SC6 update based on this controller alone.
        if(head_-settled_>=Limit){state=State::waiting;++stalls;return false;}
        if(head_==UINT64_MAX){state=State::restart_required;return false;}
        auto& f=frames_[head_%Capacity];
        if(!f.valid || f.number!=head_){f={};f.valid=true;f.number=head_;}
        Input held=seed_;
        if(head_>settled_)held=frames_[(head_-1)%Capacity].used;
        f.local=local;f.used=f.confirmed?f.actual:adapter.predict(held);
        if(!adapter.save(f.before) || !adapter.step(f.local,f.used)){state=State::restart_required;return false;}
        f.simulated=true;++head_;return true;
    }
};
}
