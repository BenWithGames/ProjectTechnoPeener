#pragma once
#include <Windows.h>
#include <filesystem>
#include <fstream>

namespace sc6 {
// Availability only: never read/consume packets, accept peers, or initialize Steam.
inline void steam_diagnostic_poll() {
    static bool done=false;
    if(done) return;
    static ULONGLONG nextPoll=0;
    const auto now=GetTickCount64();
    if(now<nextPoll) return;
    nextPoll=now+1000;
    wchar_t exe[32768]{};
    if(!GetModuleFileNameW(nullptr,exe,32768)) return;
    const auto directory=std::filesystem::path(exe).parent_path()/L"Mods/SC6Rollback/captures";
    const auto arm=directory/L"steam-diagnostic.arm";
    if(!std::filesystem::exists(arm)) return;
    done=true;
    std::filesystem::remove(arm);
    std::ofstream report(directory/("steam-diagnostic-"+std::to_string(GetCurrentProcessId())+".txt"));
    report<<"schema=1\nscope=steam_interface_availability\npackets_sent=0\npackets_consumed=0\npeer_identified=0\nchannel_usage_verified=0\n";
    const auto module=GetModuleHandleW(L"steam_api64.dll");
    if(!module){report<<"status=steam_module_not_loaded\n";return;}
    using Handle=int(*)();
    using Factory=void*(*)(const char*);
    using Network=void*(*)(void*,int,int,const char*);
    auto user=reinterpret_cast<Handle>(GetProcAddress(module,"SteamAPI_GetHSteamUser"));
    auto pipe=reinterpret_cast<Handle>(GetProcAddress(module,"SteamAPI_GetHSteamPipe"));
    auto factory=reinterpret_cast<Factory>(GetProcAddress(module,"SteamInternal_CreateInterface"));
    auto network=reinterpret_cast<Network>(GetProcAddress(module,"SteamAPI_ISteamClient_GetISteamNetworking"));
    if(!user||!pipe||!factory||!network){report<<"status=missing_exports\n";return;}
    const int u=user(),p=pipe();
    if(!u||!p){report<<"status=steam_not_initialized\n";return;}
    void* client=factory("SteamClient017");
    if(!client){report<<"status=client_unavailable\n";return;}
    void* net=network(client,u,p,"SteamNetworking005");
    report<<"interface=SteamNetworking005\nstatus="<<(net?"available":"unavailable")<<'\n';
    // ReadProcessMemory avoids dereferencing an invalid diagnostic pointer.
    void* table=nullptr; SIZE_T copied=0;
    if(net && ReadProcessMemory(GetCurrentProcess(),net,&table,sizeof(table),&copied) && copied==sizeof(table)) {
        void* slots[8]{};
        if(ReadProcessMemory(GetCurrentProcess(),table,slots,sizeof(slots),&copied) && copied==sizeof(slots)) {
            report<<"interface_address="<<net<<"\nvtable="<<table<<'\n';
            for(unsigned i=0;i<8;++i) report<<"slot"<<i<<'='<<slots[i]<<'\n';
        }
    }
}
}
