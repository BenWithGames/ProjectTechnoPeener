#pragma once
#include "SteamObserver.hpp"
#include "SteamHandshakeProtocol.hpp"
#include "SteamPing.hpp"
#include "OverlayPingTransport.hpp"
#include "AutomaticPlayControl.hpp"
#include "StatusOverlay.hpp"
#include <bcrypt.h>
#include <chrono>
namespace sc6::automatic_steam {
inline constexpr int channel=27862;
inline constexpr uint64_t version=0x0001534336415554ULL;
struct Packet {steam_handshake::Packet handshake{};uint32_t kind{},id{};uint64_t handling{};};
static_assert(sizeof(Packet)==48);
using Send=bool(*)(void*,uint64_t,const void*,uint32_t,int,int);
using Available=bool(*)(void*,uint32_t*,int);
using Read=bool(*)(void*,void*,uint32_t,uint32_t*,uint64_t*,int);
struct Session {unsigned char active,connecting,error,relay;int bytes,packets;uint32_t ip;uint16_t port;};
static_assert(sizeof(Session)==20);
using State=bool(*)(void*,uint64_t,Session*);
inline steam_handshake::Protocol protocol;
inline SteamPing ping;
inline uint64_t peer{},last_received{},started{},next_send{},retry_at{},next_init{},next_report{};
inline bool initialized{},disabled{},collision{};
inline Send send=nullptr;inline Read read=nullptr;inline Available available=nullptr;inline State state=nullptr;
inline const char* status="waiting_for_game_peer";
inline unsigned sent{},received{},rejected{},echoes{};
inline uint64_t echo_window{};
inline uint64_t micros(){return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
inline bool compatible(uint64_t now){return protocol.mutual && last_received && now>=last_received && now-last_received<3000;}
inline OverlayStats overlay_stats(){OverlayStats s;s.connected=compatible(GetTickCount64());if(s.connected)s.ping_ms=ping.value(micros());return s;}
inline void reset(uint64_t candidate,uint64_t now){
    peer=candidate;protocol={};protocol.expected_signature=version;protocol.expected_peer=candidate;
    ping={};last_received=next_send=retry_at=next_report=0;started=now;collision=false;echoes=0;echo_window=now;
    if(candidate && (BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&protocol.local_nonce),sizeof(protocol.local_nonce),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0 || !protocol.local_nonce)){
        peer=0;status="random_failed";return;}
    status=candidate?"negotiating_default_networking":"waiting_for_unique_game_peer";
}
inline void report(uint64_t now){
    if(now<next_report)return;next_report=now+3000;
    wchar_t name[32768]{};if(!GetModuleFileNameW(nullptr,name,32768))return;
    const auto dir=std::filesystem::path(name).parent_path()/(automatic_play::enabled?L"Mods/SC6Rollback/logs":L"Mods/SC6Rollback/captures");
    std::filesystem::create_directories(dir);
    std::ofstream out(dir/("steam-auto-"+std::to_string(GetCurrentProcessId())+".txt"));
    out<<"schema=1\nscope=automatic_compatibility_only\nrollback_active=0\ninjection=0\nrestore=0\ndefault_game_networking=1\n"
       <<"status="<<status<<"\npeer="<<peer<<"\nchannel="<<channel<<"\nmutual_confirmation="<<unsigned(compatible(now))
       <<"\nsent="<<sent<<"\nreceived="<<received<<"\nrejected="<<rejected<<"\nping_refresh_seconds=3\nping_rtt_ms=";
    const auto value=compatible(now)?ping.value(micros()):std::nullopt;if(value)out<<*value;else out<<"unavailable";
    out<<"\n";overlay_ping.report(out);
}
inline void poll(){
    const auto now=GetTickCount64();
    if(disabled){report(now);return;}
    if(!initialized){
        if(now<next_init)return;next_init=now+1000;
        std::string error;
        if(!steam_observer::ensure_hooks(error)){
            if(error!="no_steam_module" && error!="steam_not_ready" && error!="no_client")disabled=true;
            status=disabled?"observer_unavailable_default_networking":"waiting_for_steam";report(now);return;
        }
        const auto api=GetModuleHandleW(L"steam_api64.dll");
        send=reinterpret_cast<Send>(GetProcAddress(api,"SteamAPI_ISteamNetworking_SendP2PPacket"));
        read=reinterpret_cast<Read>(GetProcAddress(api,"SteamAPI_ISteamNetworking_ReadP2PPacket"));
        available=reinterpret_cast<Available>(GetProcAddress(api,"SteamAPI_ISteamNetworking_IsP2PPacketAvailable"));
        state=reinterpret_cast<State>(GetProcAddress(api,"SteamAPI_ISteamNetworking_GetP2PSessionState"));
        if(!send||!read||!available||!state){disabled=true;status="exports_missing_default_networking";report(now);return;}
        initialized=true;
    }
    auto candidate=steam_observer::automatic_peer(now);Session session{};
    if(candidate && (!state(steam_observer::observed,candidate,&session)||!session.active))candidate=0;
    if(candidate!=peer)reset(candidate,now);
    if(!peer || collision){report(now);return;}
    // Only this dedicated protocol channel is read. Native packets are untouched.
    for(unsigned budget=0;budget<32;++budget){
        uint32_t bytes=0;if(!available(steam_observer::observed,&bytes,channel))break;
        if(bytes>256){collision=true;status="channel_collision_default_networking";last_received=0;break;}
        std::array<std::byte,256> buffer{};uint32_t size{};uint64_t sender{};
        if(!read(steam_observer::observed,buffer.data(),static_cast<uint32_t>(buffer.size()),&size,&sender,channel))break;
        const auto arrival=micros();
        if(size!=sizeof(Packet)||sender!=peer){++rejected;continue;}
        Packet packet{};std::memcpy(&packet,buffer.data(),sizeof(packet));
        if(packet.kind==0){
            if(packet.id || packet.handling || !protocol.receive(sender,packet.handshake,sizeof(steam_handshake::Packet))){++rejected;continue;}
            ++received;if(packet.handshake.echo==protocol.local_nonce)last_received=now;
        }else{
            const auto& h=packet.handshake;
            if(!compatible(now)||h.magic!=version||h.nonce!=protocol.remote_nonce||h.echo!=protocol.local_nonce||h.confirmed!=1||!packet.id){++rejected;continue;}
            if(packet.kind==1 && packet.handling==0){
                if(now-echo_window>=1000){echo_window=now;echoes=0;}
                if(echoes++>=5){++rejected;continue;}
                packet.kind=2;packet.handshake=protocol.outgoing();packet.handling=micros()-arrival;
                if(send(steam_observer::observed,peer,&packet,sizeof(packet),1,channel))++sent;
                ++received;
            }else if(packet.kind==2 && ping.pong(packet.id,arrival,packet.handling)){++received;}else ++rejected;
        }
    }
    if(collision){report(now);return;}
    if(compatible(now)){
        status="compatible_default_networking";ping.update(micros());
        if(ping.due(micros())){Packet packet{protocol.outgoing(),1,ping.begin(micros()),0};
            if(send(steam_observer::observed,peer,&packet,sizeof(packet),1,channel))++sent;else ping.send_failed(packet.id);}
    }else if(now-started>=10000){
        status="no_compatible_reply_default_networking";
        if(!retry_at)retry_at=now+30000;
        if(now>=retry_at)reset(peer,now);
        report(now);return;
    }
    if(now>=next_send){Packet packet{protocol.outgoing(),0,0,0};
        if(send(steam_observer::observed,peer,&packet,sizeof(packet),1,channel))++sent;
        next_send=now+(protocol.mutual?1000:250);}
    report(now);
}
}
