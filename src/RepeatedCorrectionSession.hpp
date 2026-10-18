#pragma once
#include "CorrectionCycleBank.hpp"
#include "RetryBoundaryObservation.hpp"
namespace sc6::research {
// Network worker only. Outer Steam transport authenticates session/peer and
// profile. Results remain available for retransmission after cycle advance.
class RepeatedCorrectionSession {
public:
    using Words=StartupCorrectionExchange::Words;
    static constexpr unsigned spacing=180;
    std::array<unsigned,12> local{},peer{};
    unsigned identity{},first_clock{},first_battle{},slot{},active_cycle{};
    bool started{},failed{},complete{},combat{},exhausted{};
    unsigned limit{3};
    bool fresh_retries{};
    std::array<unsigned,12> battles{};
    Words proposal{};unsigned proposal_cycle{},proposal_phase{};
    static bool same_proposal(const Words& a,const Words& b){return a[1]==b[1] && a[3]==b[3] && a[4]==b[4];}
    static bool future_pair(const Words& w,const RetryBoundaryObservation::Sample& sample){
        return sample.stable>=4 && w[4]==sample.identity && w[3]>sample.clock && w[1]>sample.battle &&
            w[3]-sample.clock==w[1]-sample.battle && w[3]-sample.clock>=30 && w[3]-sample.clock<=900;
    }
    template<class Bank> bool begin(Bank& bank,unsigned id,unsigned clock,unsigned battle,unsigned side,bool fresh=false){
        if(started)return (fresh_retries || identity==id) && first_clock==clock && first_battle==battle && slot==side;
        combat=bank.combat;limit=bank.limit;fresh_retries=combat && fresh;
        if(!clock || !battle || clock>UINT32_MAX-(limit-1)*spacing-3 || battle>UINT32_MAX-(limit-1)*spacing-3)return false;
        if(!bank.arm(1,id,clock,battle,side))return false;
        identity=id;first_clock=clock;first_battle=battle;slot=side;active_cycle=1;battles[0]=battle;started=true;return true;
    }
    bool receive(unsigned cycle,const Words& w){
        if(!started || cycle<1 || cycle>limit || w[2]!=3-slot)return false;
        if(fresh_retries && w[0]>=4 && w[0]<=6){
            if(cycle!=active_cycle+1 && !(cycle==active_cycle && proposal_cycle==cycle))return false;
            for(unsigned i=5;i<w.size();++i)if(w[i])return false;
            if(w[0]==4 && slot==2 && cycle==active_cycle+1){
                if(proposal_phase>=6 && !same_proposal(proposal,w))return false;
                proposal=w;proposal_cycle=cycle;proposal_phase=4;return true;
            }
            if(proposal_cycle!=cycle || !same_proposal(proposal,w))return false;
            if(w[0]==5 && slot==1){if(proposal_phase<6)proposal_phase=5;return true;}
            if(w[0]==6 && slot==2){proposal_phase=6;return true;}
            return false;
        }
        if(cycle>active_cycle || w[0]<1 || w[0]>(combat?3u:2u) ||
           w[1]!=(fresh_retries?battles[cycle-1]:first_battle+(cycle-1)*spacing))return false;
        for(unsigned i=3;i<w.size();++i)if(w[i])return false;
        if(peer[cycle-1] && peer[cycle-1]!=w[0])return false;
        peer[cycle-1]=w[0];if(w[0]==2)failed=true;return true;
    }
    bool outgoing(unsigned cycle,Words& w)const{
        w={};if(!started || cycle<1 || cycle>limit)return false;
        if(fresh_retries && cycle==proposal_cycle && !local[cycle-1]){
            if((slot==1 && proposal_phase>=4) || (slot==2 && proposal_phase>=5)){
                w=proposal;w[0]=slot==1?(proposal_phase>=6?6u:4u):5u;w[2]=slot;return true;
            }
        }
        if(!local[cycle-1])return false;
        w[0]=local[cycle-1];w[1]=fresh_retries?battles[cycle-1]:first_battle+(cycle-1)*spacing;w[2]=slot;return true;
    }
    template<class Bank> void update(Bank& bank,unsigned id,unsigned clock,RetryBoundaryObservation::Sample sample={}){
        if(!started || complete)return;
        const auto n=bank.active();if(!n)return;
        const auto& c=bank.at(n-1);const auto& r=c.runner;
        const bool neutral=bank.skipped(n-1);
        // A neutral rejection is pending until the owner has released snapshots
        // and resumed the held frontier. Never advance merely on worker rejection.
        const bool pending_neutral=combat && r.exchange.input_rejection.load()==2 && !r.restart.load();
        const bool clean_boundary=fresh_retries && c.entry.state.load()==4 && !r.stage.load() &&
            !r.restored.load() && !r.injected.load() && !r.restart.load();
        if((!fresh_retries && id!=identity) || r.restart.load() || c.entry.state.load()==5 ||
           (c.entry.state.load()==4 && !neutral && !clean_boundary) || c.confirmation.state.load()==5 ||
           (!pending_neutral && !neutral && !clean_boundary && (r.failed.load() || r.exchange.failed.load() || c.confirmation.state.load()==4)))failed=true;
        if(failed){if(!local[n-1])local[n-1]=2;return;}
        if(neutral)local[n-1]=3;
        else if(r.verified.load() && r.retired.load())local[n-1]=1;
        if(!local[n-1] || !peer[n-1]){if(!fresh_retries && clock>=first_clock+n*spacing)failed=true;return;}
        // Either clean peer can find the prediction ineffective. Both have
        // retired before publishing 1/3; retry together without claiming PASS.
        const bool retry=combat && (local[n-1]==3 || peer[n-1]==3);
        if(local[n-1]!=peer[n-1] && !retry){failed=true;return;}
        if(combat && !retry){
            unsigned bilateral=0;for(unsigned i=0;i<n;++i)if(local[i]==1 && peer[i]==1)++bilateral;
            if(bilateral>=3){complete=true;return;}
        }
        if(n==limit){exhausted=combat;complete=exhausted || bank.all_verified();if(!complete)failed=true;return;}
        if(fresh_retries){
            if(slot==1 && proposal_cycle==n+1 && sample.stable>=4 &&
               (proposal[4]!=sample.identity || proposal[3]<=sample.clock+30 ||
                proposal[1]<=sample.battle || proposal[3]-sample.clock!=proposal[1]-sample.battle)){
                proposal_cycle=0;proposal_phase=0;
            }
            if(proposal_cycle!=n+1){
                if(slot!=1 || sample.stable<4 || sample.clock>UINT32_MAX-603 || sample.battle>UINT32_MAX-603)return;
                proposal={};proposal[0]=4;proposal[1]=sample.battle+600;proposal[2]=slot;
                proposal[3]=sample.clock+600;proposal[4]=sample.identity;proposal_cycle=n+1;proposal_phase=4;
            }
            if(!future_pair(proposal,sample))return; // wait for matching fresh owner observations
            if(slot==2 && proposal_phase==4){proposal_phase=5;return;}
            if((slot==1 && proposal_phase==5) || (slot==2 && proposal_phase==6)){
                if(!bank.arm(n+1,proposal[4],proposal[3],proposal[1],slot)){failed=true;return;}
                identity=proposal[4];battles[n]=proposal[1];active_cycle=n+1;proposal_phase=6;
            }
            return;
        }
        const auto next_clock=first_clock+n*spacing;
        if(clock>=next_clock || !bank.arm(n+1,identity,next_clock,first_battle+n*spacing,slot))failed=true;
        else active_cycle=n+1;
    }
};
}
