#pragma once
#include <array>
#include <algorithm>
#include <chrono>
#include <cstdint>
namespace sc6::research {
// Startup only. Never changes frame numbers, input delay, or the prediction limit.
// Four timestamps measure RTT minus peer handling, and convert a future release
// time into the peer's clock domain. No cross-machine QPC subtraction is used.
struct CombatStartSync {
    struct Wire {uint64_t sent{},echo{},received{},target{};};
    static uint64_t now(){return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());}
    bool leader{},failed{},committed{},commit_sent{};
    unsigned failure{}; // 1 late send, 2 late ack, 3 late commit, 4 expired proposal, 5 clock conversion
    bool fail(unsigned reason){failed=true;if(!failure)failure=reason;return false;}
    uint64_t peer_sent{},peer_received{},local_target{},peer_target{},best_rtt{},samples{},worst_rtt{},release_margin{};
    int64_t peer_offset{};
    std::array<uint64_t,256> probes{};unsigned cursor{};
    // Network-worker-only rolling connection evidence. Scheduling fields above
    // remain frozen once a round has a target; these samples keep refreshing.
    struct Sample {uint64_t at{},rtt{};int64_t offset{};};
    std::array<Sample,32> history{};unsigned history_cursor{};
    uint64_t carried_samples{},carried_age_us{};
    static constexpr uint64_t freshness_us=5000000;
    void refresh(uint64_t time){
        if(local_target)return;
        samples=best_rtt=worst_rtt=0;peer_offset=0;
        for(const auto& s:history){
            if(!s.at || time<s.at || time-s.at>freshness_us)continue;
            worst_rtt=(std::max)(worst_rtt,s.rtt);
            if(!samples || s.rtt<best_rtt){best_rtt=s.rtt;peer_offset=s.offset;}
            ++samples;
        }
    }
    void inherit_timing(const CombatStartSync& previous,uint64_t time){
        // Never inherit tickets, probes, targets, hold state or commitment.
        if(local_target || history_cursor || previous.failed)return;
        history=previous.history;history_cursor=previous.history_cursor;
        refresh(time);carried_samples=samples;
        uint64_t newest=0;for(const auto& s:history)
            if(s.at<=time)newest=(std::max)(newest,s.at);
        carried_age_us=newest?time-newest:0;
    }
    static bool valid(uint64_t t){return t && t<static_cast<uint64_t>(INT64_MAX/4);}
    void sent(const Wire& w,unsigned kind,uint64_t time){
        probes[cursor++%probes.size()]=w.sent;
        if(kind==6 && !commit_sent){
            if(!committed || time>=local_target){fail(1);return;}
            commit_sent=true;
        }
    }
    bool receive(const Wire& w,unsigned kind,uint64_t time,bool ready){
        if(failed || !valid(time) || !valid(w.sent) || bool(w.echo)!=bool(w.received))return false;
        if(w.echo){
            if(!valid(w.echo) || !valid(w.received) || w.received>w.sent)return false;
            auto probe=std::find(probes.begin(),probes.end(),w.echo);
            if(probe!=probes.end() && time>=w.echo){
                const auto elapsed=time-w.echo,handling=w.sent-w.received;
                if(elapsed>=handling && elapsed-handling<=2000000){
                    const auto rtt=elapsed-handling;
                    const auto offset=((int64_t(w.received)-int64_t(w.echo))+(int64_t(w.sent)-int64_t(time)))/2;
                    history[history_cursor++%history.size()]={time,rtt,offset};
                    refresh(time);*probe=0;
                }
            }
        }
        peer_sent=w.sent;peer_received=time;
        if(kind<4)return w.target==0;
        if(!ready || !valid(w.target))return false;
        if(kind==4 && !leader){
            if(local_target)return local_target==w.target;
            if(w.target<=time || w.target-time>5000000)return false;
            local_target=w.target;return true;
        }
        if(kind==5 && leader){
            if(!local_target || w.target!=peer_target)return false;
            if(!committed && time+10000>=local_target)return fail(2);
            committed=true;return true;
        }
        if(kind==6 && !leader){
            if(!local_target || w.target!=local_target)return false;
            if(!committed && time>=local_target)return fail(3);
            committed=true;return true;
        }
        return false;
    }
    unsigned kind(uint64_t time,bool ready,unsigned fallback){
        if(failed || !ready)return fallback;
        if(history_cursor)refresh(time);
        if(leader && !local_target && samples>=3){
            // Proposal, acknowledgment, then commit need three one-way trips.
            // Use the worst fresh RTT, not the minimum clock-offset sample.
            // Keep 50 ms for scheduling/retries and a 100 ms LAN floor.
            release_margin=(std::max)(uint64_t(100000),(std::max)(worst_rtt,best_rtt)*3/2+50000);
            local_target=time+release_margin;
            const auto converted=int64_t(local_target)+peer_offset;
            if(converted<=0){fail(5);return fallback;}peer_target=uint64_t(converted);
        }
        if(!local_target)return fallback;
        if(!committed && time>=local_target){fail(4);return fallback;}
        return leader?(committed?6u:4u):5u;
    }
    Wire wire(uint64_t time,unsigned kind)const{return {time,peer_sent,peer_received,kind>=4?(leader?peer_target:local_target):0};}
    bool can_publish()const{return !failed && committed && (!leader || commit_sent);}
};
}
