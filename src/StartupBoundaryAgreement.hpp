#pragma once
#include "StartupBoundaryPairs.hpp"
namespace sc6::research {
// Network-thread rehearsal only. Session/peer authentication is performed by
// the enclosing transport. No held-clock claim or activation token is issued.
class StartupBoundaryAgreement {
public:
    // Agree on intent in intros, not a guessed future combat frame. The owner
    // later captures the first safe boundary; CombatStartAgreement still checks
    // both exact native frame offers before synchronized release.
    bool begin_first_fight(unsigned identity,unsigned round,unsigned event,unsigned slot){
        if(bound_ || !round || slot<1 || slot>2)return false;
        local_={3u,identity,round,event,0,0,slot,0,0,0};bound_=true;return true;
    }
    bool first_fight()const{return bound_ && local_[0]==3;}
    bool fallback_requested()const{return fallback_;}
    bool fallback(const StartupBoundaryPairs& pairs,unsigned slot){
        if(!first_fight() || !fallback_ || failed_)return false;
        StartupBoundaryAgreement next;if(!next.begin(pairs,slot,true))return false;
        *this=next;return true;
    }
    using Words=std::array<uint32_t,10>;
private:
    Words local_{};bool bound_{},received_{},ack_{},failed_{},fallback_{};
public:
    bool begin(const StartupBoundaryPairs& pairs,unsigned slot,bool early=false){
        const unsigned required=early?2u:120u,target=early?60u:600u;
        if(bound_ || slot<1 || slot>2 || pairs.count.load(std::memory_order_acquire)<required ||
            pairs.overwritten.load() || pairs.unpaired.load() || pairs.discontinuities.load())return false;
        const auto& first=pairs.rows[0];
        if(first.clock>UINT32_MAX-target || first.battle>UINT32_MAX-target)return false;
        local_={early?2u:1u,first.identity,first.clock,first.battle,first.clock+target,first.battle+target,slot,0,required,0};
        bound_=true;return true;
    }
    // Revision 2 can renegotiate only while the caller has withdrawn any
    // unstarted request. Targets retain the verified clock/battle mapping.
    bool refresh(unsigned frame){
        if(!bound_ || failed_ || local_[0]!=2 || frame<local_[2])return false;
        const uint64_t delta=((uint64_t(frame)-local_[2])/60+2)*60;
        if(delta>UINT32_MAX-local_[2] || delta>UINT32_MAX-local_[3])return false;
        if(local_[2]+delta<=local_[4])return true;
        local_[4]=local_[2]+unsigned(delta);local_[5]=local_[3]+unsigned(delta);
        local_[9]=unsigned(delta/60-1);received_=ack_=false;return true;
    }
    unsigned revision()const{return local_[9];}
    bool newer(const Words& peer)const{return bound_ && local_[0]==2 && peer[0]==2 && peer[9]>local_[9];}
    bool bound()const{return bound_;}
    bool failed()const{return failed_;}
    bool ready()const{return bound_ && received_ && ack_ && !failed_ && !fallback_;}
    // The initial clock identity is an admission check. Profile 15 transfers
    // round identity validation to RetainedObjectReplay after the agreed hold
    // has released. Failed agreement and missed initial targets remain fatal.
    unsigned runtime_rejection(unsigned profile,unsigned hold,bool repeated,bool continuous,
        unsigned live_identity,unsigned startup_identity,unsigned live_frame)const{
        const bool round_runner=(profile==15 || profile==16) && hold==3 && ready();
        return (failed()?1u:0u) |
            (!repeated && !round_runner && live_identity!=startup_identity?2u:0u) |
            (!first_fight() && !continuous && !repeated && hold==0 && live_frame>=target_clock()?4u:0u);
    }
    unsigned target_clock()const{return local_[4];}
    unsigned target_battle()const{return local_[5];}
    Words outgoing()const{auto out=local_;out[7]=received_?1u:0u;return out;}
    bool receive(const Words& peer){
        if(!bound_ || failed_)return false;
        // A peer that could not prepare in intros chooses the existing paired
        // target path. Neither side may silently activate at different frames.
        if(first_fight() && peer[0]==2){
            if(peer[1]!=local_[1] || peer[6]!=3-local_[6] || peer[7]>1 || peer[8]!=2){failed_=true;return false;}
            fallback_=true;return true;
        }
        if(local_[0]==2 && peer[0]==3){
            // Delayed intro intent cannot overwrite an agreed fallback target.
            return peer[1]==local_[1] && peer[6]==3-local_[6] && peer[7]<=1;
        }
        if(local_[0]==2){
            const uint64_t delta=(uint64_t(peer[9])+1)*60;
            for(unsigned i:{0u,1u,2u,3u,8u})if(peer[i]!=local_[i]){failed_=true;return false;}
            if(peer[6]!=3-local_[6] || peer[7]>1 || delta>UINT32_MAX-local_[2] ||
               delta>UINT32_MAX-local_[3] || peer[4]!=local_[2]+delta || peer[5]!=local_[3]+delta){failed_=true;return false;}
            if(peer[9]<local_[9])return true; // old acknowledgements never authorize a new target
            if(peer[9]>local_[9]){local_[4]=peer[4];local_[5]=peer[5];local_[9]=peer[9];received_=ack_=false;}
        }
        for(unsigned i=0;i<peer.size();++i){
            const bool valid=i==6?peer[i]==3-local_[6]:i==7?peer[i]<=1:peer[i]==local_[i];
            if(!valid){failed_=true;return false;}
        }
        received_=true;if(peer[7])ack_=true;return true;
    }
};
}
