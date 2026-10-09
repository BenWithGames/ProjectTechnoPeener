#include "../src/DelayedInputHistory.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>
using Input=std::array<std::byte,40>;
using History=sc6::research::DelayedInputHistory<Input>;
void require(bool ok){if(!ok){std::cerr<<"Delayed-input test failed\n";std::exit(1);}}
struct State {
    std::uint32_t position{},velocity{},rng{123},hits{},previous{};
    bool operator==(const State&)const=default;
};
void update(State& s,const Input& input){
    const auto buttons=std::to_integer<unsigned>(input[0]);
    s.velocity=s.velocity*3u+(buttons&3u);
    s.position+=s.velocity;
    s.rng=s.rng*1664525u+1013904223u;
    if((buttons&4u) && !(s.previous&4u))s.hits+=(s.rng%7u)+1u;
    s.previous=buttons;
}
Input value(unsigned n){Input i{};i[0]=std::byte((n/5u)%8u);return i;}
struct Packet {unsigned frame,due;Input input;};
void scenario(unsigned delay,bool jitter){
    constexpr unsigned count=120;
    History h;State actual{},reference{};
    std::array<State,count+1> snapshots{};
    std::vector<Packet> packets;
    for(unsigned f=0;f<count;++f)packets.push_back({f,f+delay+(jitter?(f%4u)*2u:0u),value(f)});
    // Reverse tie order deliberately, exercising packet reordering.
    std::sort(packets.begin(),packets.end(),[](const Packet&a,const Packet&b){return a.due==b.due?a.frame>b.frame:a.due<b.due;});
    std::size_t cursor=0;unsigned corrections=0,replayed=0;
    for(unsigned tick=0;tick<count+delay+8;++tick){
        while(cursor<packets.size() && packets[cursor].due<=tick){
            const auto& p=packets[cursor++];
            require(h.receive(p.frame,p.input)==History::Arrival::accepted);
            require(h.receive(p.frame,p.input)==History::Arrival::duplicate);
        }
        if(auto first=h.correction()){
            Input blocked{};require(!h.advance(blocked));
            actual=snapshots[*first];++corrections;
            for(auto f=*first;f<h.head();++f){snapshots[f]=actual;update(actual,h.input_for(f));++replayed;}
            snapshots[h.head()]=actual;
            require(h.finish_correction(*first));
        }
        if(tick<count){
            snapshots[tick]=actual;Input predicted{};require(h.advance(predicted));
            update(actual,predicted);snapshots[tick+1]=actual;
        }
        // Independent zero-delay oracle for the fully confirmed prefix.
        unsigned prefix=0;
        for(;prefix<h.head();++prefix){
            bool arrived=false;for(std::size_t k=0;k<cursor;++k)if(packets[k].frame==prefix)arrived=true;
            if(!arrived)break;
        }
        State oracle{};for(unsigned f=0;f<prefix;++f)update(oracle,value(f));
        require(snapshots[prefix]==oracle);
    }
    for(unsigned f=0;f<count;++f)update(reference,value(f));
    require(cursor==packets.size() && actual==reference && !h.correction());
    require(delay || jitter?corrections>0:corrections==0);
    std::cout<<"PASS: delay="<<delay<<" jitter="<<jitter<<" corrections="<<corrections<<" replayed="<<replayed<<" final state matches zero-delay oracle\n";
}
int main(){
    for(unsigned delay:{0u,1u,3u,6u}){scenario(delay,false);scenario(delay,true);}
    History h;Input zero{},one{};one[0]=std::byte{1};Input out{};
    require(h.receive(2,one)==History::Arrival::accepted);
    require(h.advance(out) && out==zero); // Future data cannot predict frame0.
    require(h.receive(0,zero)==History::Arrival::accepted && !h.correction());
    require(h.receive(0,one)==History::Arrival::conflict && !h.correction());
    require(h.receive(256,one)==History::Arrival::out_of_range);
    require(!h.finish_correction(0));
    require(h.advance(out) && out==zero);require(h.advance(out) && out==one);
    require(h.receive(1,one)==History::Arrival::accepted && h.correction()==1);
    require(!h.finish_correction(0));require(h.finish_correction(1));
    while(h.head()<256)require(h.advance(out));require(!h.advance(out));
    std::cout<<"PASS: duplicate/conflict/bounds, future-input isolation, matching prediction, correction acknowledgement and capacity exhaustion\n";
}
