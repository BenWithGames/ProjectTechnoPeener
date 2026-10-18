#pragma once
#include <cstdint>
#include "PostBattleSettlement.hpp"
namespace sc6::research {
// Control protocol only. A native adapter must OWN THE ENTIRE outer continuation
// (including repeat and remaining-step loops). A delegate hook is not custody.
// Not published to the game until that adapter has been implemented and audited.
class PostBattleContinuation {
public:
    struct Identity {
        uint64_t session{},peer{},generation{},event{};
        unsigned round{},owner{};
        bool valid()const{return session && peer && session!=peer && generation && event && round && owner;}
        bool operator==(const Identity&)const=default;
    };
    enum class Step {waiting,complete,failed};
    enum class Phase {idle,settling,retiring,committing,awaiting_fight,rearming,active,failed};
    enum class Failure {none,custody,identity,reentry,settlement,retirement,commit,rearm,exception};
    struct Diagnostics {
        uint64_t polls{},settlement_waits{},retirement_waits{},commit_calls{},fight_waits{},rearm_waits{},rearms{};
    } diagnostics;
private:
    Identity old_{},next_{};
    uint64_t token_{};
    Phase phase_{Phase::idle};
    Failure failure_{};
    bool entered_{};
    bool fail(Failure reason){if(phase_!=Phase::failed)failure_=reason;phase_=Phase::failed;return false;}
public:
    Phase phase()const{return phase_;}
    Failure failure()const{return failure_;}
    bool committed(const Identity& identity)const{
        return identity==old_ && (phase_==Phase::awaiting_fight || phase_==Phase::rearming || phase_==Phase::active);
    }
    bool retain(Identity identity,uint64_t token,bool entire_continuation_owned){
        if(phase_!=Phase::idle || !entire_continuation_owned || !token)return fail(Failure::custody);
        if(!identity.valid())return fail(Failure::identity);
        old_=identity;token_=token;phase_=Phase::settling;return true;
    }
    // Observe/arm are invoked only after native commit returns. Waiting for a
    // fresh fight is clean inactivity; it is not permission to retry a commit.
    // Every callback reports failure explicitly. Exceptions never enable stock
    // fallback after a possibly partial restore or callback side effect.
    template<class Settle,class Retire,class Commit,class Observe,class Arm>
    bool poll(unsigned owner,uint64_t token,Settle settle,Retire retire,Commit commit,Observe observe,Arm arm){
        if(phase_==Phase::failed)return false;
        if(entered_)return fail(Failure::reentry);
        if(phase_==Phase::idle || owner!=old_.owner || token!=token_)return fail(Failure::identity);
        if(phase_==Phase::active)return true;
        entered_=true;
        struct Exit {bool& entered;~Exit(){entered=false;}} exit{entered_};
        ++diagnostics.polls;
        const bool recheck_fight=phase_==Phase::rearming;
        try {
            if(phase_==Phase::settling){
                auto result=settle();
                if(phase_==Phase::failed)return false;
                if(result==PostBattleSettlement::Result::restart_required)return fail(Failure::settlement);
                if(result==PostBattleSettlement::Result::waiting){++diagnostics.settlement_waits;return true;}
                phase_=Phase::retiring;
            }
            if(phase_==Phase::retiring){
                auto result=retire();
                if(phase_==Phase::failed)return false;
                if(result==Step::failed)return fail(Failure::retirement);
                if(result==Step::waiting){++diagnostics.retirement_waits;return true;}
                phase_=Phase::committing;
                ++diagnostics.commit_calls;
                // Mark before invoking: an exception/reentry can never retry it.
                bool committed=commit();
                if(phase_==Phase::failed)return false;
                if(!committed)return fail(Failure::commit);
                phase_=Phase::awaiting_fight;
            }
            if(phase_==Phase::awaiting_fight){
                Identity candidate{};
                auto result=observe(candidate);
                if(phase_==Phase::failed)return false;
                if(result==Step::failed)return fail(Failure::identity);
                if(result==Step::waiting){++diagnostics.fight_waits;return true;}
                // Generation may stay the same across rounds. Event must be
                // fresh even when the displayed round number is reused.
                if(!candidate.valid() || candidate.session!=old_.session || candidate.peer!=old_.peer ||
                    candidate.owner!=old_.owner || candidate.generation<old_.generation || candidate.event<=old_.event)
                    return fail(Failure::identity);
                next_=candidate;phase_=Phase::rearming;
            }
            if(phase_==Phase::rearming){
                // A bilateral startup wait may outlive the observed fight.
                // Never arm the cached identity after playback or another round.
                if(recheck_fight){
                    Identity current{};auto observed=observe(current);
                    if(phase_==Phase::failed)return false;
                    if(observed==Step::failed)return fail(Failure::identity);
                    if(observed==Step::waiting){++diagnostics.fight_waits;return true;}
                    if(!(current==next_))return fail(Failure::identity);
                }
                auto result=arm(next_);
                if(phase_==Phase::failed)return false;
                if(result==Step::failed)return fail(Failure::rearm);
                if(result==Step::waiting){++diagnostics.rearm_waits;return true;}
                ++diagnostics.rearms;phase_=Phase::active;
            }
            return true;
        }catch(...){return fail(Failure::exception);}
    }
};
}
