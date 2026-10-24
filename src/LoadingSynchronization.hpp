#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>
namespace sc6::research::loading_sync {
// Loading agreement only: never authorizes input replacement or restoration.
// Storage remains valid for hook readers until process exit (bounded 128 matches).
inline constexpr uint64_t magic=0x3144414F4C364353ULL;
struct Session {
    uint64_t local{},remote{},peer{},build{};
    std::atomic<uint64_t> entered{},peer_started{},object{},last_native{},polls{},withheld{};
    std::atomic<unsigned> owner{},stage{},peer_stage{},reason{};
    std::atomic<bool> sealed{};
    uint64_t sealed_at{},sent_at{};unsigned last_sent=99;
    enum Reason:unsigned {none,deadline,peer_cancel,identity,wrong_thread,missed_loading,disconnected,clock_reversed};
    void cancel(unsigned why){unsigned zero=0;reason.compare_exchange_strong(zero,why);}
    bool cancelled()const{return reason.load(std::memory_order_acquire)!=0;}
    void advance(uint64_t now){
        if(cancelled() || sealed.load())return;
        const auto began=entered.load(),other=peer_started.load();
        const auto first=began && other?(began<other?began:other):began?began:other;
        // A concurrent callback may publish a timestamp newer than this poll.
        if(first && now<first)return;
        if(first && now-first>=5000 && !admitted()){cancel(deadline);return;}
        if(!began)return;
        unsigned desired=1;const auto ps=peer_stage.load();
        if(ps>=1)desired=2;
        if(ps>=2)desired=3;
        auto old=stage.load();while(old<desired && !stage.compare_exchange_weak(old,desired)){}
    }
    // Owner callback; caller and native state are checked by the hook adapter.
    bool hold(uintptr_t obj,unsigned thread,uint64_t now){
        if(cancelled() || sealed.load())return false;
        ++polls;const auto prior=last_native.exchange(now);
        if(prior && now<prior){cancel(clock_reversed);return false;}
        auto old=object.load();
        if(!old){uint64_t empty=0;if(object.compare_exchange_strong(empty,obj)){
            owner.store(thread);entered.store(now,std::memory_order_release);
        }else old=empty;}
        if((old && old!=obj) || !obj){cancel(identity);return false;}
        if(owner.load()!=thread){cancel(wrong_thread);return false;}
        advance(now);if(cancelled())return false;
        if(stage.load()>=3 && peer_stage.load()>=3){stage.store(4,std::memory_order_release);return false;}
        ++withheld;return true;
    }
    bool admitted()const{return !cancelled() && stage.load(std::memory_order_acquire)==4 && peer_stage.load(std::memory_order_acquire)==4;}
    using Bytes=std::array<unsigned char,48>;
    static void put(Bytes& b,unsigned at,uint64_t v){for(unsigned i=0;i<8;++i)b[at+i]=static_cast<unsigned char>(v>>(8*i));}
    static uint64_t get(const Bytes& b,unsigned at){uint64_t v=0;for(unsigned i=0;i<8;++i)v|=uint64_t(b[at+i])<<(8*i);return v;}
    Bytes outgoing()const{Bytes b{};put(b,0,magic);put(b,8,local);put(b,16,remote);put(b,24,build);put(b,32,cancelled()?9:stage.load());return b;}
    bool receive(uint64_t sender,const Bytes& b,size_t size,uint64_t now){
        const auto s=get(b,32);
        if(size!=b.size() || sender!=peer || get(b,0)!=magic || get(b,8)!=remote || get(b,16)!=local || get(b,24)!=build || get(b,40) || (s>4 && s!=9))return false;
        if(s==9){cancel(peer_cancel);return true;}
        if(sealed.load())return true; // Activation uses its own stop/liveness protocol.
        if(s){uint64_t zero=0;peer_started.compare_exchange_strong(zero,now);}
        auto old=peer_stage.load();while(old<s && !peer_stage.compare_exchange_weak(old,static_cast<unsigned>(s))){}
        advance(now);return true;
    }
    bool due(uint64_t now){const auto s=cancelled()?9:stage.load();if(last_sent==s && now>=sent_at && now-sent_at<20)return false;last_sent=s;sent_at=now;return true;}
    template<class Stream>void report(Stream& out)const{
        out<<"loading_sync_schema=1\nloading_sync_local="<<local<<"\nloading_sync_stage="<<stage.load()
           <<"\nloading_sync_peer_stage="<<peer_stage.load()<<"\nloading_sync_reason="<<reason.load()
           <<"\nloading_sync_entered_ms="<<entered.load()<<"\nloading_sync_last_native_ms="<<last_native.load()
           <<"\nloading_sync_polls="<<polls.load()<<"\nloading_sync_withheld="<<withheld.load()
           <<"\nloading_sync_owner="<<owner.load()<<"\nloading_sync_admitted="<<admitted()<<"\nloading_sync_sealed="<<sealed.load()<<'\n';
    }
};
inline std::array<Session,128> sessions;
inline size_t used{}; // sole control-thread writer
inline std::atomic<Session*> current{};
inline Session* bind(uint64_t local,uint64_t remote,uint64_t peer,uint64_t build){
    if(auto* s=current.load();s && s->local==local)return s->remote==remote && s->peer==peer && s->build==build?s:nullptr;
    if(!local || !remote || local==remote || !peer || !build || used==sessions.size())return nullptr;
    auto* s=&sessions[used++];s->local=local;s->remote=remote;s->peer=peer;s->build=build;
    current.store(s,std::memory_order_release);return s;
}
inline void clear(uint64_t local){auto* s=current.load();if(s && s->local==local)current.compare_exchange_strong(s,nullptr);}
}
