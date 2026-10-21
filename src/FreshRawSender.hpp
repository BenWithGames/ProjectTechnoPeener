#pragma once
#include "RawPredictionTransport.hpp"
#include "SteamInputExchange.hpp"
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
struct FreshRawIngress {
    static RawPredictionRoute<>::Submit receive(const steam_inputs::Packet& packet,uint64_t sender,uint64_t peer,
        uint64_t local,uint64_t remote,RawPredictionRoute<>* route){
        RawInputConfirmation raw;
        if(!route || sender!=peer || packet.magic!=steam_inputs::signature || packet.nonce!=remote || packet.echo!=local ||
           !RawPredictionPacket::decode(packet,raw))return RawPredictionRoute<>::Submit::rejected;
        return route->submit(raw);
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
    std::atomic<uint64_t> received_{},rejected_{},full_{},receive_faults_{};
    std::atomic<uint64_t> publication_wakes_{},timer_wakes_{};
    std::atomic<uint64_t> sent_{},failed_{},ticks_{},faults_{},first_{},age_total_{},age_max_{};
public:
    ~FreshRawSender(){stop();}
    bool start(void* net,Send send,uint64_t peer,uint64_t local_nonce,uint64_t remote_nonce,int channel,Available available=nullptr,Read read=nullptr){
        if(worker_.joinable())return true;
        if(attempted_ || !net || !send || !peer || !local_nonce || !remote_nonce)return false;
        attempted_=true;
        stop_event_=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        timer_=CreateWaitableTimerExW(nullptr,nullptr,0x2,TIMER_ALL_ACCESS); // high-resolution, auto-reset
        if(!stop_event_ || !timer_){stop();++faults_;return false;}
        try {worker_=std::thread([=,this]{
            FreshRawCursor cursor;bool receive_enabled=available && read;
            try {for(;;){
                if(WaitForSingleObject(stop_event_,0)!=WAIT_TIMEOUT)break;
                ++ticks_;
                cursor.pump(raw_prediction_transport.load(std::memory_order_acquire),local_nonce,remote_nonce,[&](const auto& raw){
                    steam_inputs::Packet packet;
                    if(!RawPredictionPacket::encode(raw,packet))return false;
                    if(send(net,peer,&packet,sizeof(packet),1,receive_enabled?RawPredictionPacket::fast_channel:channel)){
                        ++sent_;uint64_t age{};
                        if(cursor.route->mark_first_send(cursor.next,age)){++first_;age_total_+=age;if(age>age_max_)age_max_=age;}
                        return true;
                    }
                    ++failed_;return false;
                });
                // Dedicated channel: never steal handshake/stop/ping/control packets.
                for(unsigned budget=0;receive_enabled && budget<32;++budget){
                    uint32_t bytes{};if(!available(net,&bytes,RawPredictionPacket::fast_channel))break;
                    std::array<unsigned char,1200> buffer{};uint32_t size{};uint64_t sender{};
                    if(bytes>buffer.size()){++receive_faults_;receive_enabled=false;break;}
                    if(!read(net,buffer.data(),static_cast<uint32_t>(buffer.size()),&size,&sender,RawPredictionPacket::fast_channel))break;
                    if(size!=sizeof(steam_inputs::Packet)){++rejected_;continue;}
                    steam_inputs::Packet packet;std::memcpy(&packet,buffer.data(),sizeof(packet));
                    const auto result=FreshRawIngress::receive(packet,sender,peer,local_nonce,remote_nonce,
                        raw_prediction_transport.load(std::memory_order_acquire));
                    using R=RawPredictionRoute<>::Submit;
                    if(result==R::accepted || result==R::retired)++received_;
                    else if(result==R::full)++full_;else ++rejected_;
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
        });}catch(...){stop();++faults_;return false;}
        return true;
    }
    // Called by control thread before terminal reports and before mod teardown.
    void stop(){
        if(stop_event_)SetEvent(stop_event_);
        if(worker_.joinable())worker_.join();
        if(timer_){CloseHandle(timer_);timer_=nullptr;}
        if(stop_event_){CloseHandle(stop_event_);stop_event_=nullptr;}
    }
    template<class Stream> void report(Stream& out)const{
        out<<"fresh_raw_receiver_schema=1\nfresh_raw_receiver_received="<<received_.load()
           <<"\nfresh_raw_receiver_rejected="<<rejected_.load()<<"\nfresh_raw_receiver_full="<<full_.load()
           <<"\nfresh_raw_receiver_faults="<<receive_faults_.load()<<'\n';
        out<<"fresh_raw_sender_schema=2\nfresh_raw_sender_sent="<<sent_.load()
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
