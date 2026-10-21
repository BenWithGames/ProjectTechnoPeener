#pragma once
#include "SteamPing.hpp"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <ostream>
namespace sc6 {
// Dedicated mod-only channel. Never consumes native gameplay packets.
class OverlayPingTransport {
public:
    struct Packet {uint64_t magic{},nonce{},echo{},handling{};uint32_t kind{},id{};};
    static constexpr uint64_t magic=0x000153433650494eULL;
    static constexpr int channel=27864;
    static bool authenticated(const Packet& p,uint32_t size,uint64_t sender,uint64_t peer,uint64_t local,uint64_t remote){
        return size==sizeof(Packet)&&peer&&local&&remote&&sender==peer&&p.magic==magic&&p.nonce==remote&&p.echo==local&&p.id&&(p.kind==1||p.kind==2)&&(p.kind==2||p.handling==0);
    }
private:
    using Send=bool(*)(void*,uint64_t,const void*,uint32_t,int,int);
    using Available=bool(*)(void*,uint32_t*,int);
    using Read=bool(*)(void*,void*,uint32_t,uint32_t*,uint64_t*,int);
public:
    struct Api {Send send{};Available available{};Read read{};};
private:
    Api api_{};
    struct Config {void* net{};uint64_t peer{},local{},remote{},refreshed{};};
    static_assert(sizeof(Packet)==40);
    std::mutex mutex_;
    Config config_{};
    SteamPing published_{};
    uint64_t max_poll_gap_us_{};
    std::thread worker_;
    std::atomic<bool> stopping_{false};
    static uint64_t now(){return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
    void run(){
        auto api=GetModuleHandleW(L"steam_api64.dll");
        auto send=api_.send?api_.send:reinterpret_cast<Send>(GetProcAddress(api,"SteamAPI_ISteamNetworking_SendP2PPacket"));
        auto available=api_.available?api_.available:reinterpret_cast<Available>(GetProcAddress(api,"SteamAPI_ISteamNetworking_IsP2PPacketAvailable"));
        auto read=api_.read?api_.read:reinterpret_cast<Read>(GetProcAddress(api,"SteamAPI_ISteamNetworking_ReadP2PPacket"));
        if(!send||!available||!read)return;
        // Local high-resolution wait, without changing the system timer period.
        HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,0x2,TIMER_ALL_ACCESS);
        Config active{};SteamPing ping;uint64_t previous=0,window=0;unsigned echoes=0;
        while(!stopping_.load()){
            const auto tick=now();Config c;{std::lock_guard lock(mutex_);c=config_;}
            if(c.net!=active.net || c.peer!=active.peer || c.local!=active.local || c.remote!=active.remote){
                active=c;ping={};previous=0;window=tick;echoes=0;
                std::lock_guard lock(mutex_);published_={};max_poll_gap_us_=0;
            }
            if(c.net && c.peer && tick>=c.refreshed && tick-c.refreshed<3000000){
                if(previous){std::lock_guard lock(mutex_);max_poll_gap_us_=(std::max)(max_poll_gap_us_,tick-previous);}previous=tick;
                for(unsigned budget=0;budget<32;++budget){
                    uint32_t bytes=0;if(!available(c.net,&bytes,channel))break;
                    if(bytes>256)break;
                    std::array<std::byte,256> data{};uint32_t size=0;uint64_t sender=0;
                    if(!read(c.net,data.data(),static_cast<uint32_t>(data.size()),&size,&sender,channel))break;
                    const auto arrival=now();Packet p{};
                    if(size!=sizeof(p)||sender!=c.peer)continue;
                    std::memcpy(&p,data.data(),sizeof(p));
                    if(!authenticated(p,size,sender,c.peer,c.local,c.remote))continue;
                    if(p.kind==1 && p.handling==0){
                        if(arrival-window>=1000000){window=arrival;echoes=0;}
                        if(echoes++>=10)continue;
                        p.kind=2;p.nonce=c.local;p.echo=c.remote;p.handling=now()-arrival;
                        send(c.net,c.peer,&p,sizeof(p),1,channel);
                    }else if(p.kind==2)ping.pong(p.id,arrival,p.handling);
                }
                ping.update(now());
                if(ping.due(now())){
                    Packet p{magic,c.local,c.remote,0,1,0};p.id=ping.begin(now());
                    if(!send(c.net,c.peer,&p,sizeof(p),1,channel))ping.send_failed(p.id);
                }
                {std::lock_guard lock(mutex_);
                    if(config_.peer==c.peer && config_.local==c.local && config_.remote==c.remote)published_=ping;}
            }else {previous=0;std::lock_guard lock(mutex_);published_={};}
            if(timer){LARGE_INTEGER due{};due.QuadPart=-10000;
                if(SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE))WaitForSingleObject(timer,10);else Sleep(1);
            }else Sleep(1);
        }
        if(timer)CloseHandle(timer);
    }
public:
    OverlayPingTransport()=default;
    explicit OverlayPingTransport(Api api):api_(api){}
    ~OverlayPingTransport(){stop();}
    void configure(void* net,uint64_t peer,uint64_t local,uint64_t remote){
        {std::lock_guard lock(mutex_);
            if(config_.peer!=peer||config_.local!=local||config_.remote!=remote)published_={};
            config_={net,peer,local,remote,now()};
            if(!net||!peer||!local||!remote){config_={};published_={};}}
        if(net&&peer&&local&&remote&&!worker_.joinable())worker_=std::thread([this]{run();});
    }
    void stop(){stopping_=true;if(worker_.joinable())worker_.join();}
    std::optional<unsigned> value(){std::lock_guard lock(mutex_);const auto tick=now();
        return config_.peer&&tick>=config_.refreshed&&tick-config_.refreshed<3000000?published_.value(tick):std::nullopt;}
    void report(std::ostream& out){SteamPing snapshot;uint64_t gap{};
        {std::lock_guard lock(mutex_);snapshot=published_;gap=max_poll_gap_us_;}
        out<<"overlay_ping_method=dedicated_worker_minimum_of_5_minus_reply_handling\noverlay_ping_network_only=0\noverlay_ping_refresh_seconds=3\noverlay_ping_samples="<<snapshot.latest_samples
           <<"\noverlay_ping_raw_us="<<snapshot.latest_raw_us<<"\noverlay_ping_handling_us="<<snapshot.latest_handling_us<<"\noverlay_ping_max_poll_gap_us="<<gap<<"\noverlay_ping_rtt_ms=";
        auto v=snapshot.value(now());if(v)out<<*v;else out<<"unavailable";out<<'\n';
    }
};
inline OverlayPingTransport overlay_ping;
}
