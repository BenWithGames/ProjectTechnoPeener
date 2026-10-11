#pragma once
#include <cstdint>
#include <limits>
namespace sc6::research {
// A caller establishes the clock/battle frame mapping at the same boundary.
// Never infer that the two counters share a numeric origin.
class CombatBoundaryReservation {
    uint64_t session_{},round_{};
    unsigned owner_{},identity_{},clock_{},battle_{};
    bool bound_{},reserved_{},advanced_{},failed_{};
    bool fail(){failed_=true;return false;}
public:
    bool bind(uint64_t session,uint64_t round,unsigned owner,unsigned identity,unsigned clock,unsigned battle){
        if(bound_ || failed_ || !session || !round || !owner)return false;
        session_=session;round_=round;owner_=owner;identity_=identity;clock_=clock;battle_=battle;bound_=true;return true;
    }
    bool reserve(uint64_t session,uint64_t round,unsigned thread,unsigned identity,unsigned clock,bool can_advance){
        if(!bound_ || failed_)return false;
        if(session!=session_ || round!=round_ || thread!=owner_ || identity!=identity_ || clock!=clock_ || reserved_ || advanced_)return fail();
        if(!can_advance)return false;
        if(clock_==UINT32_MAX || battle_==UINT32_MAX)return fail();
        reserved_=true;return true;
    }
    bool clock_completed(bool advanced){
        if(!bound_ || failed_)return false;
        if(!advanced){if(reserved_ || advanced_)return fail();return true;}
        if(!reserved_ || advanced_)return fail();
        advanced_=true;return true;
    }
    template<class Session> bool reserve_from(const Session& controller,uint64_t session,uint64_t round,
        unsigned thread,unsigned identity,unsigned clock){
        const auto admission=controller.admission(session,round);
        if(admission==Session::Admission::restart_required)return fail();
        return reserve(session,round,thread,identity,clock,admission==Session::Admission::ready);
    }
    bool consume(uint64_t session,uint64_t round,unsigned thread,unsigned frame){
        if(!bound_ || failed_)return false;
        if(session!=session_ || round!=round_ || thread!=owner_ || frame!=battle_ || !reserved_ || !advanced_)return fail();
        reserved_=advanced_=false;++clock_;++battle_;return true;
    }
    bool restart_required()const{return failed_;}
    void stop()noexcept{failed_=true;}
};
}
