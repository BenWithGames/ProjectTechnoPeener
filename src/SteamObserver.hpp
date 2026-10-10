#pragma once
#include <Windows.h>
#include <MinHook.h>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include "AutomaticPeer.hpp"

namespace sc6::steam_observer {
using Send=bool(*)(void*,uint64_t,const void*,uint32_t,int,int);
using Read=bool(*)(void*,void*,uint32_t,uint32_t*,uint64_t*,int);
inline Send original_send=nullptr;
inline Read original_read=nullptr;
inline void* observed=nullptr;
inline std::atomic<bool> active=false;
inline AutomaticPeer automatic_peers;
inline SRWLOCK peer_lock=SRWLOCK_INIT;
inline bool hooks_ready=false;
inline uint64_t automatic_peer(uint64_t now){AcquireSRWLockShared(&peer_lock);auto peer=automatic_peers.candidate(now);ReleaseSRWLockShared(&peer_lock);return peer;}
inline SRWLOCK lock=SRWLOCK_INIT;
struct Row {uint64_t peer=0; int channel=0; bool receive=false; uint64_t calls=0,bytes=0,failed=0;};
inline Row rows[128]{};
inline unsigned used=0;
inline std::atomic<unsigned> dropped=0;
inline void record(void* self,uint64_t peer,int channel,bool receive,uint32_t bytes,bool ok) noexcept {
    if(self!=observed)return;
    if(TryAcquireSRWLockExclusive(&peer_lock)){automatic_peers.observe(peer,channel,receive,ok,GetTickCount64());ReleaseSRWLockExclusive(&peer_lock);}
    if(!active.load(std::memory_order_relaxed))return;
    if(!TryAcquireSRWLockExclusive(&lock)){++dropped;return;}
    unsigned i=0;
    for(;i<used;++i) if(rows[i].peer==peer && rows[i].channel==channel && rows[i].receive==receive) break;
    if(i==used && used<128) {rows[used++]={peer,channel,receive,0,0,0};}
    if(i<used){++rows[i].calls;if(ok)rows[i].bytes+=bytes;else ++rows[i].failed;}
    else ++dropped;
    ReleaseSRWLockExclusive(&lock);
}
inline bool send(void* self,uint64_t peer,const void* data,uint32_t size,int type,int channel) {
    const bool ok=original_send(self,peer,data,size,type,channel);const auto error=GetLastError();
    record(self,peer,channel,false,size,ok);
    SetLastError(error);return ok;
}
inline bool read(void* self,void* data,uint32_t capacity,uint32_t* size,uint64_t* peer,int channel) {
    const bool ok=original_read(self,data,capacity,size,peer,channel);const auto error=GetLastError();
    // Successful Steam reads supply both outputs. No payload bytes are inspected.
    if(ok && size && peer) record(self,*peer,channel,true,*size,true);
    SetLastError(error);return ok;
}
inline bool ensure_hooks(std::string& error) {
    if(hooks_ready)return true;
    HMODULE api=GetModuleHandleW(L"steam_api64.dll");
    if(!api){error="no_steam_module";return false;}
    using Handle=int(*)();using Factory=void*(*)(const char*);using Network=void*(*)(void*,int,int,const char*);
    auto user=reinterpret_cast<Handle>(GetProcAddress(api,"SteamAPI_GetHSteamUser"));
    auto pipe=reinterpret_cast<Handle>(GetProcAddress(api,"SteamAPI_GetHSteamPipe"));
    auto factory=reinterpret_cast<Factory>(GetProcAddress(api,"SteamInternal_CreateInterface"));
    auto network=reinterpret_cast<Network>(GetProcAddress(api,"SteamAPI_ISteamClient_GetISteamNetworking"));
    if(!user||!pipe||!factory||!network){error="missing_exports";return false;}
    int u=user(),p=pipe();if(!u||!p){error="steam_not_ready";return false;}
    auto client=factory("SteamClient017");if(!client){error="no_client";return false;}
    observed=network(client,u,p,"SteamNetworking005");
    void* table=nullptr;void* slots[3]{};SIZE_T copied=0;
    if(!observed || !ReadProcessMemory(GetCurrentProcess(),observed,&table,sizeof(table),&copied) || copied!=sizeof(table)
        || !ReadProcessMemory(GetCurrentProcess(),table,slots,sizeof(slots),&copied) || copied!=sizeof(slots)) {error="invalid_vtable";return false;}
    HMODULE steamclient=GetModuleHandleW(L"steamclient64.dll");
    for(unsigned i: {0u,2u}) {
        MEMORY_BASIC_INFORMATION info{};
        if(!steamclient || !VirtualQuery(slots[i],&info,sizeof(info)) || info.AllocationBase!=steamclient
            || !(info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))) {error="unexpected_target_module";return false;}
    }
    if(slots[0]==slots[2]){error="aliased_targets";return false;}
    auto init=MH_Initialize();if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED){error="hook_init_failed";return false;}
    if(MH_CreateHook(slots[0],reinterpret_cast<void*>(&send),reinterpret_cast<void**>(&original_send))!=MH_OK){error="send_hook_failed";return false;}
    if(MH_CreateHook(slots[2],reinterpret_cast<void*>(&read),reinterpret_cast<void**>(&original_read))!=MH_OK){MH_RemoveHook(slots[0]);error="read_hook_failed";return false;}
    // Trampolines remain pass-through after the window. Pin our code for process lifetime.
    HMODULE pin=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&send),&pin)){
        MH_RemoveHook(slots[0]);MH_RemoveHook(slots[2]);error="pin_failed";return false;
    }
    if(MH_EnableHook(slots[0])!=MH_OK){MH_RemoveHook(slots[0]);MH_RemoveHook(slots[2]);error="send_enable_failed";return false;}
    if(MH_EnableHook(slots[2])!=MH_OK){MH_DisableHook(slots[0]);error="read_enable_failed";return false;}
    hooks_ready=true;return true;
}
inline void poll() {
    static bool attempted=false;
    static ULONGLONG next=0,deadline=0;
    static std::filesystem::path output;
    const auto now=GetTickCount64();
    if(now<next) return;
    next=now+1000;
    if(active.load()) {
        if(now>=deadline) active=false;
        Row copy[128]{}; unsigned count;
        AcquireSRWLockExclusive(&lock);count=used;
        for(unsigned i=0;i<count;++i)copy[i]=rows[i];
        ReleaseSRWLockExclusive(&lock);
        std::ofstream file(output);
        file<<"scope=passive_steam_metadata\nstatus="<<(active.load()?"recording":"complete")
            <<"\nmod_packets_sent=0\nextra_packet_reads=0\npayload_recorded=0\nranked_detection=0\ndropped_metadata="<<dropped.load()
            <<"\npeer,channel,direction,calls,bytes,failed\n";
        for(unsigned i=0;i<count;++i){const auto& r=copy[i];file<<r.peer<<','<<r.channel<<','<<(r.receive?"receive":"send")<<','<<r.calls<<','<<r.bytes<<','<<r.failed<<'\n';}
        return;
    }
    if(attempted)return;
    wchar_t name[32768]{};if(!GetModuleFileNameW(nullptr,name,32768))return;
    auto dir=std::filesystem::path(name).parent_path()/L"Mods/SC6Rollback/captures";
    auto arm=dir/L"steam-observe.arm";
    if(!std::filesystem::exists(arm))return;
    attempted=true;
    std::ifstream request(arm);std::string confirmation;std::getline(request,confirmation);request.close();
    std::filesystem::remove(arm);
    output=dir/("steam-observe-"+std::to_string(GetCurrentProcessId())+".txt");
    std::ofstream report(output);
    if(confirmation!="PRIVATE_MATCH_METADATA_ONLY"){report<<"status=invalid_arm\n";return;}
    std::string error;if(!ensure_hooks(error)){report<<"status="<<error<<"\n";return;}
    deadline=now+180000;active=true;report<<"status=recording\n";
}
}
