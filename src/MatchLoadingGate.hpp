#pragma once
#include <cstdint>
namespace sc6::research {
// Policy only; not a native hook. The verified owner-thread loading callback
// must return to its caller on hold, allowing loading/network workers to run.
// A release does not authorize rollback: native battle identity still requires
// independent validation. Never use this to resume modified simulation.
class MatchLoadingGate {
public:
    struct Key {uint64_t local{},peer{},ticket{};bool operator==(const Key&)const=default;
        bool valid()const{return local && peer && ticket && local!=peer;}};
    enum class State {idle,waiting,released,native_fallback,invalid_modified};
    enum class Reason {none,no_compatible_peer,timeout,identity_changed,unsafe_boundary,clock_reversed,peer_cancelled};
    struct Observation {Key key;bool safe{},prepared{},peer_ready{},committed{},peer_cancelled{},modified{};};
    static constexpr uint64_t maximum_wait_ms=5000;
private:
    Key key_{};State state_{};Reason reason_{};
    uint64_t began_{},last_{},waited_{},polls_{};
    bool fallback(Reason r){state_=State::native_fallback;reason_=r;return false;}
public:
    bool begin(Key key,uint64_t now,bool compatible,bool modified){
        if(state_!=State::idle || !key.valid() || modified)return false;
        key_=key;began_=last_=now;
        if(!compatible)return fallback(Reason::no_compatible_peer);
        state_=State::waiting;return true;
    }
    // True withholds only the audited loading transition, never sleeps/spins.
    // Modified state is an integration error: it cannot take this fallback.
    bool hold(const Observation& o,uint64_t now){
        if(state_==State::invalid_modified)return true;
        if(state_!=State::waiting)return false;
        ++polls_;
        if(o.modified){state_=State::invalid_modified;return true;}
        if(now<last_)return fallback(Reason::clock_reversed);
        last_=now;waited_=now-began_;
        if(o.key!=key_)return fallback(Reason::identity_changed);
        if(!o.safe)return fallback(Reason::unsafe_boundary);
        if(o.peer_cancelled)return fallback(Reason::peer_cancelled);
        // Expiry wins over a late commit, and fallback is terminal for this key.
        if(waited_>=maximum_wait_ms)return fallback(Reason::timeout);
        if(o.prepared && o.peer_ready && o.committed){state_=State::released;return false;}
        return true;
    }
    State state()const{return state_;}Reason reason()const{return reason_;}
    template<class Stream>void report(Stream& s)const{
        s<<"loading_gate_policy_schema=1\nloading_gate_state="<<unsigned(state_)
         <<"\nloading_gate_reason="<<unsigned(reason_)<<"\nloading_gate_wait_ms="<<waited_
         <<"\nloading_gate_polls="<<polls_<<'\n';
    }
};
}
