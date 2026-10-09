#include "../src/OfflineRollbackSession.hpp"
#include <cstdlib>
#include <iostream>
#include <vector>
#include <algorithm>
struct State {unsigned position{},rng{123},edge{},hits{};bool operator==(const State&)const=default;};
struct Adapter {
    State state{};unsigned saves{},loads{},steps{};bool fail_save{},fail_load{},fail_step{};
    bool save(State& out){++saves;if(fail_save)return false;out=state;return true;}
    bool load(const State& in){++loads;if(fail_load)return false;state=in;return true;}
    bool step(unsigned local,unsigned remote){
        ++steps;if(fail_step)return false;
        state.rng=state.rng*1664525u+1013904223u;
        state.position=state.position*3u+local*17u+remote;
        if(remote && !state.edge)state.hits+=state.rng%11u;
        state.edge=remote;return true;
    }
};
using Session=sc6::research::OfflineRollbackSession<unsigned,State>;
void require(bool v){if(!v){std::cerr<<"Snapshot controller test failed\n";std::exit(1);}}
unsigned local(unsigned n){return n%7u;} unsigned remote(unsigned n){return (n/3u)%4u;}
int main(){
    for(unsigned delay:{0u,1u,3u,6u})for(bool jitter:{false,true}){
        Session session;Adapter live,oracle;
        struct Packet{unsigned frame,due;};std::vector<Packet> packets;
        for(unsigned f=0;f<120;++f)packets.push_back({f,f+delay+(jitter?(f%4u)*2u:0u)});
        std::sort(packets.begin(),packets.end(),[](auto a,auto b){return a.due==b.due?a.frame>b.frame:a.due<b.due;});
        std::size_t cursor=0;
        for(unsigned tick=0;tick<134;++tick){
            while(cursor<packets.size() && packets[cursor].due<=tick){auto f=packets[cursor++].frame;
                require(session.receive(f,remote(f))==Session::Arrival::accepted);
                require(session.receive(f,remote(f))==Session::Arrival::duplicate);
            }
            require(session.reconcile(live));
            if(tick<120)require(session.advance(live,local(tick)));
        }
        for(unsigned f=0;f<120;++f)require(oracle.step(local(f),remote(f)));
        require(cursor==120 && live.state==oracle.state && !session.fault());
        require(live.loads==session.corrections() && live.steps==120+session.replayed());
        std::cout<<"PASS: snapshot controller delay="<<delay<<" jitter="<<jitter<<" corrections="<<session.corrections()<<" replayed="<<session.replayed()<<" local inputs preserved, final state exact\n";
    }
    for(unsigned failure=0;failure<3;++failure){
        Session session;Adapter adapter;
        require(session.advance(adapter,7));require(session.receive(0,1)==Session::Arrival::accepted);
        adapter.fail_load=failure==0;adapter.fail_save=failure==1;adapter.fail_step=failure==2;
        require(!session.reconcile(adapter) && session.fault());
        auto steps=adapter.steps,loads=adapter.loads,saves=adapter.saves;
        require(!session.advance(adapter,8) && !session.reconcile(adapter));
        require(adapter.steps==steps && adapter.loads==loads && adapter.saves==saves);
    }
    Session conflict;Adapter a;
    require(conflict.receive(0,1)==Session::Arrival::accepted);
    require(conflict.receive(0,2)==Session::Arrival::conflict && conflict.fault());
    require(!conflict.advance(a,0) && a.steps==0 && a.saves==0);
    Session bounded;for(unsigned f=0;f<256;++f)require(bounded.advance(a,f));
    auto steps=a.steps;require(!bounded.advance(a,0) && a.steps==steps);
    std::cout<<"PASS: save/load/step failure latching, conflicting input rejection, bounded snapshot capacity\n";
}
