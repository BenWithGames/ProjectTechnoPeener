#pragma once
#include "RawPredictionTransport.hpp"
#include "SteamInputExchange.hpp"
#include "CombatPeerContext.hpp"
#include <thread>
namespace sc6::research {
// Only immutable, locally authored raw words leave this cursor. Routes have
// process lifetime; a replacement starts its own archive at zero.
struct FreshRawCursor {
    RawPredictionRoute<>* route{};unsigned next{};
    template<class Send> void pump(RawPredictionRoute<>* current,uint64_t local,uint64_t peer,Send send){
        if(current!=route){route=current;next=0;}
        if(!current || current->retired())return;
        const auto count=current->outgoing_count();
        for(unsigned budget=0;budget<4 && next<count;++budget){
            RawInputConfirmation raw;
            if(!current->outgoing(next,raw) || raw.sender!=local || raw.recipient!=peer)return;
            if(!send(raw))return; // Failed sends must not consume the cursor.
            ++next;
        }
    }
};
// Repeat only the two preceding immutable inputs when fresh progress is made.
// This bounds traffic and avoids depending on the slower control-channel retry.
struct FreshRawRedundancy {
    RawPredictionRoute<>* route{};unsigned through{};
    template<class Send> void pump(const FreshRawCursor& fresh,Send send){
        if(route!=fresh.route){route=fresh.route;through=0;}
        if(!route || route->retired() || fresh.next<=through)return;
        through=fresh.next;
        const auto first=through>2?through-3:0;
        for(unsigned n=first;n+1<through;++n){RawInputConfirmation raw;
            if(!route->outgoing(n,raw))break;
            send(raw); // Failed redundancy never changes the fresh cursor.
        }
    }
};
struct FreshRawIngress {
    static RawPredictionRoute<>::Submit receive(const steam_inputs::Packet& packet,uint64_t sender,uint64_t peer,
        uint64_t local,uint64_t remote,RawPredictionRoute<>* route,uint64_t received=0){
        RawInputConfirmation raw;
        if(!route || sender!=peer || packet.magic!=steam_inputs::signature || packet.nonce!=remote || packet.echo!=local ||
           !RawPredictionPacket::decode(packet,raw))return RawPredictionRoute<>::Submit::rejected;
        return route->submit(raw,received);
    }
};
struct PeerLivenessPacket {
    static constexpr uint32_t kind=0x50454552;
    static steam_inputs::Packet make(uint64_t local,uint64_t remote,uint32_t sequence){
        steam_inputs::Packet p;p.nonce=local;p.echo=remote;p.kind=kind;p.index=sequence;return p;
    }
    static bool receive(const steam_inputs::Packet& p,uint64_t sender,uint64_t peer,uint64_t local,
                        uint64_t remote,uint64_t now,CombatPeerContext& context){
        if(sender!=peer || p.magic!=steam_inputs::signature || p.kind!=kind || p.nonce!=remote ||
           p.echo!=local || !p.index || p.own_hash || p.received_hash)return false;
        for(auto word:p.record.data)if(word)return false;
        return context.heartbeat(peer,local,remote,p.index,now);
    }
};
class FreshRawSender {
public:
    using Send=bool(*)(void*,uint64_t,const void*,uint32_t,int,int);
    using Available=bool(*)(void*,uint32_t*,int);
    using Read=bool(*)(void*,void*,uint32_t,uint32_t*,uint64_t*,int);
private:
    HANDLE stop_event_{},timer_{};
    std::thread worker_;
    bool attempted_{};
    std::atomic<uint64_t> starts_{},stops_{};
    std::atomic<uint64_t> heartbeat_sent_{},heartbeat_received_{},heartbeat_rejected_{},heartbeat_failed_{};
    std::atomic<uint64_t> received_{},rejected_{},full_{},receive_faults_{};
    std::atomic<uint64_t> publication_wakes_{},timer_wakes_{};
    std::atomic<uint64_t> redundant_sent_{},redundant_failed_{};
    std::atomic<uint64_t> post_send_receive_passes_{},post_send_received_{};
    std::atomic<uint64_t> sent_{},failed_{},ticks_{},faults_{},first_{},age_total_{},age_max_{};
public:
    ~FreshRawSender(){stop();}
    bool start(void* net,Send send,uint64_t peer,uint64_t local_nonce,uint64_t remote_nonce,int channel,Available available=nullptr,Read read=nullptr,bool liveness=false){
        if(worker_.joinable())return true;
        if(attempted_ || !net || !send || !peer || !local_nonce || !remote_nonce)return false;
        attempted_=true;
        heartbeat_sent_=0;heartbeat_received_=0;heartbeat_rejected_=0;heartbeat_failed_=0;
        received_=0;rejected_=0;full_=0;receive_faults_=0;
        redundant_sent_=0;redundant_failed_=0;post_send_receive_passes_=0;post_send_received_=0;
        publication_wakes_=0;timer_wakes_=0;sent_=0;failed_=0;ticks_=0;faults_=0;first_=0;age_total_=0;age_max_=0;
        stop_event_=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        timer_=CreateWaitableTimerExW(nullptr,nullptr,0x2,TIMER_ALL_ACCESS); // high-resolution, auto-reset
        if(!stop_event_ || !timer_){stop(false);++faults_;return false;}
        try {worker_=std::thread([=,this]{
            FreshRawCursor cursor;FreshRawRedundancy redundancy;bool receive_enabled=available && read;
            uint64_t heartbeat_at{};uint32_t heartbeat_sequence{};
            try {for(;;){
                if(WaitForSingleObject(stop_event_,0)!=WAIT_TIMEOUT)break;
                ++ticks_;
                // Dedicated channel: never steal handshake/stop/ping/control packets.
                const auto receive_batch=[&]{
                for(unsigned budget=0;receive_enabled && budget<32;++budget){
                    uint32_t bytes{};if(!available(net,&bytes,RawPredictionPacket::fast_channel))break;
                    std::array<unsigned char,1200> buffer{};uint32_t size{};uint64_t sender{};
                    if(bytes>buffer.size()){++receive_faults_;receive_enabled=false;break;}
                    if(!read(net,buffer.data(),static_cast<uint32_t>(buffer.size()),&size,&sender,RawPredictionPacket::fast_channel))break;
                    LARGE_INTEGER received{};QueryPerformanceCounter(&received);
                    if(size!=sizeof(steam_inputs::Packet)){++rejected_;continue;}
                    steam_inputs::Packet packet;std::memcpy(&packet,buffer.data(),sizeof(packet));
                    if(liveness && packet.kind==PeerLivenessPacket::kind){
                        if(PeerLivenessPacket::receive(packet,sender,peer,local_nonce,remote_nonce,
                            GetTickCount64(),combat_peer_context))++heartbeat_received_;else ++heartbeat_rejected_;
                        continue;
                    }
                    const auto result=FreshRawIngress::receive(packet,sender,peer,local_nonce,remote_nonce,
                        raw_prediction_transport.load(std::memory_order_acquire),received.QuadPart);
                    using R=RawPredictionRoute<>::Submit;
                    if(result==R::accepted || result==R::retired)++received_;
                    else if(result==R::full)++full_;else ++rejected_;
                }
                };
                receive_batch();
                const auto heartbeat_now=GetTickCount64();
                if(liveness && receive_enabled && (!heartbeat_at || heartbeat_now-heartbeat_at>=50) && heartbeat_sequence!=UINT32_MAX){
                    const auto packet=PeerLivenessPacket::make(local_nonce,remote_nonce,++heartbeat_sequence);
                    heartbeat_at=heartbeat_now;
                    if(send(net,peer,&packet,sizeof(packet),1,RawPredictionPacket::fast_channel))++heartbeat_sent_;
                    else ++heartbeat_failed_;
                }
                const auto sent_before=sent_.load();
                cursor.pump(raw_prediction_transport.load(std::memory_order_acquire),local_nonce,remote_nonce,[&](const auto& raw){
                    steam_inputs::Packet packet;
                    if(!RawPredictionPacket::encode(raw,packet))return false;
                    LARGE_INTEGER send_begin{};QueryPerformanceCounter(&send_begin);
                    if(send(net,peer,&packet,sizeof(packet),1,receive_enabled?RawPredictionPacket::fast_channel:channel)){
                        ++sent_;uint64_t age{};
                        if(cursor.route->mark_first_send(cursor.next,age,send_begin.QuadPart)){++first_;age_total_+=age;if(age>age_max_)age_max_=age;}
                        return true;
                    }
                    ++failed_;return false;
                });
                if(receive_enabled)redundancy.pump(cursor,[&](const auto& raw){
                    steam_inputs::Packet packet;
                    if(!RawPredictionPacket::encode(raw,packet)){++redundant_failed_;return;}
                    if(send(net,peer,&packet,sizeof(packet),1,RawPredictionPacket::fast_channel))++redundant_sent_;
                    else ++redundant_failed_;
                });
                // Catch packets arriving during fresh/redundant sends before sleeping.
                // One bounded pass; application remains on the owner thread.
                if(receive_enabled && sent_.load()!=sent_before){
                    ++post_send_receive_passes_;const auto before=received_.load();
                    receive_batch();post_send_received_+=received_.load()-before;
                }
                LARGE_INTEGER due{};due.QuadPart=-10000; // one millisecond, no busy spin or game-thread wait
                if(!SetWaitableTimer(timer_,&due,0,nullptr,nullptr,FALSE)){++faults_;break;}
                HANDLE waits[]={stop_event_,timer_,raw_publication_signal.event};
                const DWORD count=raw_publication_signal.event?3u:2u;
                const auto result=WaitForMultipleObjects(count,waits,FALSE,INFINITE);
                if(result==WAIT_OBJECT_0)break;
                if(result==WAIT_OBJECT_0+1)++timer_wakes_;
                else if(count==3 && result==WAIT_OBJECT_0+2)++publication_wakes_;
                else {++faults_;break;}
            }}catch(...){++faults_;}
        });}catch(...){stop(false);++faults_;return false;}
        ++starts_;return true;
    }
    // Called by control thread before terminal reports and before mod teardown.
    void stop(bool allow_restart=true){
        if(stop_event_)SetEvent(stop_event_);
        if(worker_.joinable()){worker_.join();++stops_;}
        if(timer_){CloseHandle(timer_);timer_=nullptr;}
        if(stop_event_){CloseHandle(stop_event_);stop_event_=nullptr;}
        if(allow_restart)attempted_=false;
    }
    template<class Stream> void report(Stream& out)const{
        out<<"peer_liveness_schema=1\npeer_liveness_sent="<<heartbeat_sent_.load()
           <<"\npeer_liveness_received="<<heartbeat_received_.load()<<"\npeer_liveness_rejected="<<heartbeat_rejected_.load()
           <<"\npeer_liveness_send_failed="<<heartbeat_failed_.load()<<'\n';
        out<<"fresh_raw_receiver_schema=2\nfresh_raw_receiver_received="<<received_.load()
           <<"\nfresh_raw_receiver_rejected="<<rejected_.load()<<"\nfresh_raw_receiver_full="<<full_.load()
           <<"\nfresh_raw_receiver_faults="<<receive_faults_.load()
           <<"\nfresh_raw_receiver_post_send_passes="<<post_send_receive_passes_.load()
           <<"\nfresh_raw_receiver_post_send_received="<<post_send_received_.load()<<'\n';
        out<<"fresh_raw_sender_schema=3\nfresh_raw_sender_sent="<<sent_.load()
           <<"\nfresh_raw_sender_redundant_sent="<<redundant_sent_.load()
           <<"\nfresh_raw_sender_redundant_failed="<<redundant_failed_.load()
           <<"\nfresh_raw_sender_lifetime_starts="<<starts_.load()<<"\nfresh_raw_sender_lifetime_stops="<<stops_.load()
           <<"\nfresh_raw_sender_receive_before_send=1"
           <<"\nfresh_raw_sender_publication_wake_enabled="<<bool(raw_publication_signal.event)
           <<"\nfresh_raw_sender_publication_wakes="<<publication_wakes_.load()
           <<"\nfresh_raw_sender_timer_wakes="<<timer_wakes_.load()
           <<"\nfresh_raw_sender_failed="<<failed_.load()<<"\nfresh_raw_sender_ticks="<<ticks_.load()
           <<"\nfresh_raw_sender_faults="<<faults_.load()
           <<"\nfresh_raw_sender_first_sends="<<first_.load()<<"\nfresh_raw_sender_publication_age_total_ticks="<<age_total_.load()
           <<"\nfresh_raw_sender_publication_age_max_ticks="<<age_max_.load()<<'\n';
    }
};
inline FreshRawSender fresh_raw_sender;
}
