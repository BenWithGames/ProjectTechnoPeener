#pragma once
#include "CorrectionCycleBank.hpp"
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
    template<class Bank> bool begin(Bank& bank,unsigned id,unsigned clock,unsigned battle,unsigned side){
        if(started)return identity==id && first_clock==clock && first_battle==battle && slot==side;
        combat=bank.combat;limit=bank.limit;
        if(!clock || !battle || clock>UINT32_MAX-(limit-1)*spacing-3 || battle>UINT32_MAX-(limit-1)*spacing-3)return false;
        if(!bank.arm(1,id,clock,battle,side))return false;
        identity=id;first_clock=clock;first_battle=battle;slot=side;active_cycle=1;started=true;return true;
    }
    bool receive(unsigned cycle,const Words& w){
        if(!started || cycle<1 || cycle>active_cycle || w[0]<1 || w[0]>(combat?3u:2u) || w[1]!=first_battle+(cycle-1)*spacing || w[2]!=3-slot)return false;
        for(unsigned i=3;i<w.size();++i)if(w[i])return false;
        if(peer[cycle-1] && peer[cycle-1]!=w[0])return false;
        peer[cycle-1]=w[0];if(w[0]==2)failed=true;return true;
    }
    bool outgoing(unsigned cycle,Words& w)const{
        w={};if(!started || cycle<1 || cycle>limit || !local[cycle-1])return false;
        w[0]=local[cycle-1];w[1]=first_battle+(cycle-1)*spacing;w[2]=slot;return true;
    }
    template<class Bank> void update(Bank& bank,unsigned id,unsigned clock){
        if(!started || complete)return;
        const auto n=bank.active();if(!n)return;
        const auto& c=bank.at(n-1);const auto& r=c.runner;
        const bool neutral=bank.skipped(n-1);
        // A neutral rejection is pending until the owner has released snapshots
        // and resumed the held frontier. Never advance merely on worker rejection.
        const bool pending_neutral=combat && r.exchange.input_rejection.load()==2 && !r.restart.load();
        if(id!=identity || r.restart.load() || c.entry.state.load()>=4 || c.confirmation.state.load()==5 ||
           (!pending_neutral && (r.failed.load() || r.exchange.failed.load() || c.confirmation.state.load()==4)))failed=true;
        if(failed){if(!local[n-1])local[n-1]=2;return;}
        if(neutral)local[n-1]=3;
        else if(r.verified.load() && r.retired.load())local[n-1]=1;
        if(!local[n-1] || !peer[n-1]){if(clock>=first_clock+n*spacing)failed=true;return;}
        // Either clean peer can find the prediction ineffective. Both have
        // retired before publishing 1/3; retry together without claiming PASS.
        const bool retry=combat && (local[n-1]==3 || peer[n-1]==3);
        if(local[n-1]!=peer[n-1] && !retry){failed=true;return;}
        if(combat && !retry){
            unsigned bilateral=0;for(unsigned i=0;i<n;++i)if(local[i]==1 && peer[i]==1)++bilateral;
            if(bilateral>=3){complete=true;return;}
        }
        if(n==limit){exhausted=combat;complete=exhausted || bank.all_verified();if(!complete)failed=true;return;}
        const auto next_clock=first_clock+n*spacing;
        if(clock>=next_clock || !bank.arm(n+1,identity,next_clock,first_battle+n*spacing,slot))failed=true;
        else active_cycle=n+1;
    }
};
}
