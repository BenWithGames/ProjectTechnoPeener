#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <ostream>
namespace sc6 {
// Game-thread scheduling only. Choices are recorded before native simulation;
// they are NOT applied to it. A request is evidence, not restoration authority.
struct LateInputScheduler {
    struct Choice {uint32_t frame{},held{},edge{};bool valid{},seeded{},confirmed{};};
    struct Event {std::atomic<unsigned> frame{},at{},depth{},predicted{},predicted_edge{},actual{},actual_edge{};};
    std::array<Choice,64> choices{};
    std::array<Event,512> events{};
    std::atomic<unsigned> decisions{},seeded{},unseeded{},confirmed{},errors{},over_limit{},max_depth{},requests{},resets{},invalid{},expired{},event_count{},event_dropped{};
    std::atomic<unsigned> last_target{},last_depth{},remote_slot{};
    uint64_t generation{};unsigned round{},boundary{},last_frame{},seed_frame{},seed_held{},pending{};
    bool entered{},has_seed{},valid{},has_pending{};
    bool enter(uint64_t gen,unsigned r,unsigned event,unsigned frame,unsigned slot){
        pending=0;has_pending=false;valid=false;
        if(!gen || !r || slot<1 || slot>2){++invalid;has_seed=false;return false;}
        if(!entered || gen!=generation || r!=round || event!=boundary){
            if(entered)++resets;
            choices={};has_seed=false;entered=true;generation=gen;round=r;boundary=event;
        }else if(last_frame==UINT32_MAX || frame!=last_frame+1){++invalid;has_seed=false;return false;}
        last_frame=frame;remote_slot=3-slot;valid=true;return true;
    }
    void receive(unsigned frame,unsigned held,unsigned edge){
        if(!valid || frame>=last_frame)return;
        auto& choice=choices[frame%choices.size()];
        if(!choice.valid || choice.frame!=frame){++expired;}
        else if(!choice.confirmed){
            choice.confirmed=true;++confirmed;
            if(choice.seeded && (choice.held!=held || choice.edge!=edge)){
                ++errors;const unsigned depth=last_frame-frame;
                if(depth>max_depth.load())max_depth=depth;
                if(depth>3)++over_limit;
                else if(!has_pending || frame<pending){pending=frame;has_pending=true;}
                const auto index=event_count.load();
                if(index<events.size()){
                    auto& e=events[index];e.frame=frame;e.at=last_frame;e.depth=depth;
                    e.predicted=choice.held;e.predicted_edge=choice.edge;e.actual=held;e.actual_edge=edge;
                    event_count.store(index+1,std::memory_order_release);
                }else ++event_dropped;
            }
        }
        // Reordered older confirmations must not replace a newer prediction seed.
        if(!has_seed || frame>seed_frame){has_seed=true;seed_frame=frame;seed_held=held;}
    }
    void decide(){
        if(!valid)return;
        if(has_pending){++requests;last_target=pending;last_depth=last_frame-pending;}
        auto& choice=choices[last_frame%choices.size()];
        choice={last_frame,has_seed?seed_held:0,0,true,has_seed,false};
        ++decisions;if(has_seed)++seeded;else ++unseeded;
    }
    void csv(std::ostream& out)const{
        out<<"frame,arrival_boundary,depth,predicted_held,predicted_edge,actual_held,actual_edge\n";
        const unsigned count=event_count.load(std::memory_order_acquire);
        for(unsigned i=0;i<count;++i){const auto& e=events[i];
            out<<e.frame.load()<<','<<e.at.load()<<','<<e.depth.load()<<','<<e.predicted.load()<<','<<e.predicted_edge.load()<<','<<e.actual.load()<<','<<e.actual_edge.load()<<'\n';}
    }
};
}
