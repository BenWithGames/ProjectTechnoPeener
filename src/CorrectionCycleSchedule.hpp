#pragma once
#include <cstdint>
#include <limits>

namespace sc6::research {
// Worker-owned policy only. Native capture/load and network authentication
// remain external. No packet can authorize a cycle without local retirement.
class CorrectionCycleSchedule {
public:
    struct Ticket {
        uint64_t session{};
        uint32_t round{},cycle{},first{};
        bool operator==(const Ticket&)const=default;
    };
    enum class State {unused,waiting,admitted,draining,complete,failed};
private:
    Ticket ticket_{};
    State state_{State::unused};
    uint32_t limit_{},spacing_{};
    bool local_{},peer_{};
public:
    State state()const{return state_;}
    Ticket ticket()const{return ticket_;}
    bool start(uint64_t session,uint32_t round,uint32_t first,uint32_t cycles,uint32_t spacing){
        if(state_!=State::unused || !session || !round || !first || !cycles || cycles>16 || spacing<4 ||
           uint64_t(first)+uint64_t(cycles-1)*spacing+3>(std::numeric_limits<uint32_t>::max)())return false;
        ticket_={session,round,1,first};limit_=cycles;spacing_=spacing;state_=State::waiting;return true;
    }
    bool observe(uint32_t round,uint32_t frame){
        if(state_==State::unused || state_==State::complete || state_==State::failed)return false;
        if(round!=ticket_.round || (state_==State::waiting && frame>ticket_.first)){
            state_=State::failed;return false;
        }
        return true;
    }
    bool admit(const Ticket& t,uint32_t round,uint32_t frame,bool fresh_inputs_confirmed){
        if(state_!=State::waiting || t!=ticket_ || !observe(round,frame))return false;
        if(frame!=ticket_.first || !fresh_inputs_confirmed)return false;
        state_=State::admitted;return true;
    }
    bool local_result(const Ticket& t,bool verified,bool retired){
        if(t!=ticket_ || (state_!=State::admitted && state_!=State::draining))return false;
        if(!verified || !retired){state_=State::failed;return false;}
        local_=true;state_=State::draining;return true;
    }
    // Call only after outer transport has authenticated the peer and session.
    // Stale/out-of-order results are ignored; a current failure is terminal.
    bool peer_result(const Ticket& t,bool verified,bool retired){
        if(t!=ticket_ || state_==State::unused || state_==State::complete || state_==State::failed)return false;
        if(!verified || !retired){state_=State::failed;return false;}
        peer_=true;return true;
    }
    bool advance(uint32_t round,uint32_t frame){
        if(state_!=State::draining || !local_ || !peer_ || !observe(round,frame))return false;
        if(ticket_.cycle==limit_){state_=State::complete;return true;}
        const auto next=ticket_.first+spacing_;
        if(frame>=next){state_=State::failed;return false;}
        ++ticket_.cycle;ticket_.first=next;local_=peer_=false;state_=State::waiting;return true;
    }
    void cancel(){if(state_!=State::unused && state_!=State::complete)state_=State::failed;}
};
}
