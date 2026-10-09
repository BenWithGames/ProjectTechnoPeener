#pragma once
#include <Windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include "SteamHandshakeProtocol.hpp"
#include "SteamInputExchange.hpp"
#include "SteamPing.hpp"
#include "StatusOverlay.hpp"
#include <chrono>

namespace sc6::steam_handshake {
using Send=bool(*)(void*,uint64_t,const void*,uint32_t,int,int);
using Available=bool(*)(void*,uint32_t*,int);
using Read=bool(*)(void*,void*,uint32_t,uint32_t*,uint64_t*,int);
struct Session {unsigned char active,connecting,error,relay;int bytes,packets;uint32_t ip;uint16_t port;};
static_assert(sizeof(Session)==20);
using State=bool(*)(void*,uint64_t,Session*);
inline bool stopped=false,running=false;
inline SteamPing ping;
inline bool peer_live=false;
inline uint64_t clock_us(){return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
inline OverlayStats overlay_stats(){OverlayStats stats;stats.connected=running&&!stopped&&peer_live;if(stats.connected)stats.ping_ms=ping.value(clock_us());return stats;}
inline void poll() {
    static ULONGLONG next=0,start=0,last_report=0;
    static std::filesystem::path report_path;
    static Protocol protocol;
    static Send send=nullptr;static Available available=nullptr;static Read read=nullptr;static State state=nullptr;
    static void* net=nullptr;
    static unsigned sent=0,received=0,rejected=0,send_failed=0;
    static Session session{};
    static bool input_mode=false;
    static steam_inputs::Capture capture;
    static ULONGLONG completed=0;
    static bool data_done=false;
    static ULONGLONG last_peer=0;
    const auto now=GetTickCount64();
    if(stopped||now<next)return;
    next=now+(running?(input_mode?16:250):1000);
    auto report=[&](const char* status){
        std::ofstream out(report_path);
        out<<"schema=1\nscope="<<(input_mode?"steam_recorded_inputs":"steam_handshake_only")<<"\nrollback_active=0\ninjection=0\nrestore=0\ngame_alignment_verified=0\nstatus="<<status
            <<"\nchannel="<<channel<<"\npeer="<<protocol.expected_peer<<"\nsent="<<sent<<"\nreceived="<<received
            <<"\nrejected="<<rejected<<"\nsend_failed="<<send_failed<<"\nroundtrip_proof="<<protocol.proof
            <<"\nmutual_confirmation="<<protocol.mutual<<"\nsteam_using_relay="<<unsigned(session.relay)<<'\n';
        auto measured=ping.value(clock_us());out<<"ping_refresh_seconds=3\nping_rtt_ms="<<(measured?std::to_string(*measured):"unavailable")<<'\n';
        if(input_mode)out<<"target_records="<<steam_inputs::limit<<"\nslot_mapping_assumed=1\nlocal_records="<<capture.exchange.published<<"\nremote_records="<<capture.exchange.received
            <<"\npeer_checksum_match="<<capture.exchange.peer_match<<"\nlocal_checksum="<<observation::checksum(capture.exchange.local)
            <<"\nremote_checksum="<<observation::checksum(capture.exchange.remote)<<"\nqpc_frequency="<<(capture.shared?capture.shared->frequency:0)<<'\n';
        if(input_mode){const auto& s=capture.shadow;out<<"shadow_enabled=1\nshadow_delay_frames=4\nshadow_limit_frames=3\nshadow_remote_slot="<<s.remote_slot
            <<"\nshadow_scheduled="<<s.scheduled<<"\nshadow_evaluated="<<s.evaluated<<"\nshadow_predicted="<<s.predicted
            <<"\nshadow_errors="<<s.wrong<<"\nshadow_over_limit="<<s.over_limit<<"\nshadow_max_depth="<<s.max_depth
            <<"\nshadow_unseeded="<<s.unseeded<<"\nshadow_censored="<<s.censored<<"\nshadow_truth_mismatches="<<s.truth_mismatches
            <<"\nshadow_invalid="<<s.invalid<<"\nshadow_no_overlap="<<s.no_overlap<<"\nshadow_tail_excluded=4\nshadow_within_observed_bounds="<<s.within_observed_bounds()<<'\n';}
    };
    if(!running) {
        wchar_t exe[32768]{};if(!GetModuleFileNameW(nullptr,exe,32768))return;
        auto dir=std::filesystem::path(exe).parent_path()/L"Mods/SC6Rollback/captures";
        input_mode=std::filesystem::exists(dir/L"steam-inputs.arm");
        auto arm=dir/(input_mode?L"steam-inputs.arm":L"steam-handshake.arm");
        if(!std::filesystem::exists(arm))return;
        report_path=dir/((input_mode?"steam-inputs-":"steam-handshake-")+std::to_string(GetCurrentProcessId())+".txt");
        stopped=true;
        std::ifstream config(arm);std::string mode,extra;uint64_t pid=0,peer=0;
        unsigned local_slot=0;
        bool valid=bool(config>>mode>>pid>>peer);
        if(input_mode)valid=valid && bool(config>>local_slot) && (local_slot==1||local_slot==2);
        bool trailing=bool(config>>extra);config.close();std::filesystem::remove(arm);
        if(!valid||trailing||mode!=(input_mode?"PRIVATE_STEAM_INPUTS":"PRIVATE_STEAM_HANDSHAKE")||pid!=GetCurrentProcessId()||peer<=76561197960265728ULL||peer>76561202255233023ULL){report("invalid_arm");return;}
        if(input_mode){protocol.expected_signature=0x000553433648534bULL;capture.shadow.remote_slot=3-local_slot;}
        protocol.expected_peer=peer;
        HMODULE api=GetModuleHandleW(L"steam_api64.dll");if(!api){report("steam_missing");return;}
        using Handle=int(*)();using Factory=void*(*)(const char*);using Network=void*(*)(void*,int,int,const char*);
        auto user=reinterpret_cast<Handle>(GetProcAddress(api,"SteamAPI_GetHSteamUser"));
        auto pipe=reinterpret_cast<Handle>(GetProcAddress(api,"SteamAPI_GetHSteamPipe"));
        auto factory=reinterpret_cast<Factory>(GetProcAddress(api,"SteamInternal_CreateInterface"));
        auto network=reinterpret_cast<Network>(GetProcAddress(api,"SteamAPI_ISteamClient_GetISteamNetworking"));
        send=reinterpret_cast<Send>(GetProcAddress(api,"SteamAPI_ISteamNetworking_SendP2PPacket"));
        available=reinterpret_cast<Available>(GetProcAddress(api,"SteamAPI_ISteamNetworking_IsP2PPacketAvailable"));
        read=reinterpret_cast<Read>(GetProcAddress(api,"SteamAPI_ISteamNetworking_ReadP2PPacket"));
        state=reinterpret_cast<State>(GetProcAddress(api,"SteamAPI_ISteamNetworking_GetP2PSessionState"));
        if(!user||!pipe||!factory||!network||!send||!available||!read||!state){report("exports_missing");return;}
        int u=user(),p=pipe();if(!u||!p){report("steam_not_ready");return;}
        auto client=factory("SteamClient017");if(!client){report("client_missing");return;}
        net=network(client,u,p,"SteamNetworking005");if(!net){report("network_missing");return;}
        if(!state(net,peer,&session)||!session.active){report("peer_connection_not_active_join_private_lobby_first");return;}
        // The other player may start first. Validate queued messages through the
        // normal size, mode, peer and fresh-nonce checks instead of rejecting them.
        if(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&protocol.local_nonce),sizeof(protocol.local_nonce),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0||!protocol.local_nonce){report("random_failed");return;}
        start=now;running=true;stopped=false;report("running");return;
    }
    if(!data_done && now-start>=(input_mode?180000u:60000u)){running=false;stopped=true;report(!input_mode&&protocol.mutual?"PASS":"timeout");return;}
    auto stop_file=report_path.parent_path()/L"steam-telemetry-stop.arm";
    if(std::filesystem::exists(stop_file)){std::filesystem::remove(stop_file);running=false;stopped=true;if(!data_done)report("stopped_by_user");return;}
    if(!state(net,protocol.expected_peer,&session)||!session.active){running=false;stopped=true;if(!data_done)report("peer_disconnected");return;}
    if(data_done && last_peer && now-last_peer>15000){running=false;stopped=true;return;}
    // Bounded receive work; continuous mode polls at most once per 16 ms.
    for(unsigned i=0;i<(input_mode?128u:8u);++i){
        uint32_t bytes=0;if(!available(net,&bytes,channel))break;
        if(input_mode && bytes==sizeof(steam_inputs::Packet)){
            steam_inputs::Packet packet{};uint64_t sender=0;uint32_t size=0;
            if(!read(net,&packet,sizeof(packet),&size,&sender,channel))break;
            if(!protocol.mutual||sender!=protocol.expected_peer||size!=sizeof(packet)||packet.nonce!=protocol.remote_nonce||packet.echo!=protocol.local_nonce){++rejected;continue;}
            if(packet.magic!=steam_inputs::signature){++rejected;continue;}
            if(packet.kind==3){
                // Echo only an authenticated-session peer's small diagnostic ping.
                packet.kind=4;packet.nonce=protocol.local_nonce;packet.echo=protocol.remote_nonce;
                if(send(net,protocol.expected_peer,&packet,sizeof(packet),1,channel))++sent;else ++send_failed;
                last_peer=now;++received;continue;
            }
            if(packet.kind==4){if(ping.pong(packet.index,clock_us())){last_peer=now;++received;}else ++rejected;continue;}
            if(data_done)continue;
            try {if(capture.accept(packet))++received;else ++rejected;}
            catch(const std::exception& e){running=false;stopped=true;report(e.what());return;}
            continue;
        }
        if(bytes!=sizeof(Packet)){running=false;stopped=true;report("unexpected_channel_packet_size");return;}
        Packet packet{};uint64_t sender=0;uint32_t size=0;
        if(!read(net,&packet,sizeof(packet),&size,&sender,channel))break;
        if(protocol.receive(sender,packet,size)){++received;if(packet.echo==protocol.local_nonce)last_peer=now;}else ++rejected;
    }
    const auto packet=protocol.outgoing();
    // UnreliableNoDelay prevents deferred sends after the bounded test ends.
    if(!data_done){if(send(net,protocol.expected_peer,&packet,sizeof(packet),1,channel))++sent;else ++send_failed;}
    peer_live=protocol.mutual && last_peer && now-last_peer<=9000;
    if(input_mode && protocol.mutual && ping.due(clock_us())){
        steam_inputs::Packet probe;probe.nonce=protocol.local_nonce;probe.echo=protocol.remote_nonce;probe.kind=3;probe.index=ping.begin(clock_us());
        if(send(net,protocol.expected_peer,&probe,sizeof(probe),1,channel))++sent;else ++send_failed;
    }
    if(input_mode && protocol.mutual && !data_done){
        try {
            if(!capture.armed)capture.arm(report_path.parent_path(),protocol.local_nonce);
            capture.update(protocol.local_nonce);
            auto& exchange=capture.exchange;
            capture.shadow.update(exchange.local,exchange.published,exchange.remote,exchange.seen,capture.arrival_qpc,exchange.received==steam_inputs::limit);
            for(unsigned budget=0;budget<12 && exchange.published;++budget){
                steam_inputs::Packet data;data.nonce=protocol.local_nonce;data.echo=protocol.remote_nonce;data.kind=1;
                const bool fresh=budget<8 && exchange.fresh<exchange.published;
                if(budget<8 && !fresh)continue;
                data.index=fresh?exchange.fresh:exchange.cursor++%exchange.published;data.record=exchange.local[data.index];
                if(send(net,protocol.expected_peer,&data,sizeof(data),1,channel)){
                    ++sent;if(fresh)++exchange.fresh;
                    if(!capture.first_send_qpc[data.index]){LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);capture.first_send_qpc[data.index]=stamp.QuadPart;}
                }else ++send_failed;
            }
            if(exchange.published==steam_inputs::limit && exchange.received==steam_inputs::limit){
                steam_inputs::Packet summary;summary.nonce=protocol.local_nonce;summary.echo=protocol.remote_nonce;summary.kind=2;
                summary.own_hash=observation::checksum(exchange.local);summary.received_hash=observation::checksum(exchange.remote);
                if(send(net,protocol.expected_peer,&summary,sizeof(summary),1,channel))++sent;else ++send_failed;
            }
            if(capture.complete()){
                if(!completed)completed=now;
                if(now-completed>=10000){capture.export_csv(report_path.parent_path()/report_path.stem());data_done=true;report("PASS");return;}
            }
        }catch(const std::exception& e){running=false;stopped=true;report(e.what());return;}
    }
    if(!data_done && now-last_report>=1000){last_report=now;report("running");}
}
}
