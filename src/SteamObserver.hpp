#pragma once
#include <Windows.h>
#include <MinHook.h>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>

namespace sc6::steam_observer {
using Send=bool(*)(void*,uint64_t,const void*,uint32_t,int,int);
using Read=bool(*)(void*,void*,uint32_t,uint32_t*,uint64_t*,int);
inline Send original_send=nullptr;
inline Read original_read=nullptr;
inline void* observed=nullptr;
inline std::atomic<bool> active=false;
inline SRWLOCK lock=SRWLOCK_INIT;
struct Row {uint64_t peer=0; int channel=0; bool receive=false; uint64_t calls=0,bytes=0,failed=0;};
inline Row rows[128]{};
inline unsigned used=0;
inline std::atomic<unsigned> dropped=0;
inline void record(void* self,uint64_t peer,int channel,bool receive,uint32_t bytes,bool ok) noexcept {
    if(self!=observed || !active.load(std::memory_order_relaxed)) return;
    if(!TryAcquireSRWLockExclusive(&lock)){++dropped;return;}
    unsigned i=0;
    for(;i<used;++i) if(rows[i].peer==peer && rows[i].channel==channel && rows[i].receive==receive) break;
    if(i==used && used<128) {rows[used++]={peer,channel,receive,0,0,0};}
    if(i<used){++rows[i].calls;if(ok)rows[i].bytes+=bytes;else ++rows[i].failed;}
    else ++dropped;
    ReleaseSRWLockExclusive(&lock);
}
inline bool send(void* self,uint64_t peer,const void* data,uint32_t size,int type,int channel) {
    const bool ok=original_send(self,peer,data,size,type,channel);
    record(self,peer,channel,false,size,ok);
    return ok;
}
inline bool read(void* self,void* data,uint32_t capacity,uint32_t* size,uint64_t* peer,int channel) {
    const bool ok=original_read(self,data,capacity,size,peer,channel);
    // Successful Steam reads supply both outputs. No payload bytes are inspected.
    if(ok && size && peer) record(self,*peer,channel,true,*size,true);
    return ok;
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
    HMODULE api=GetModuleHandleW(L"steam_api64.dll");
    if(!api){report<<"status=no_steam_module\n";return;}
    using Handle=int(*)();using Factory=void*(*)(const char*);using Network=void*(*)(void*,int,int,const char*);
    auto user=reinterpret_cast<Handle>(GetProcAddress(api,"SteamAPI_GetHSteamUser"));
    auto pipe=reinterpret_cast<Handle>(GetProcAddress(api,"SteamAPI_GetHSteamPipe"));
    auto factory=reinterpret_cast<Factory>(GetProcAddress(api,"SteamInternal_CreateInterface"));
    auto network=reinterpret_cast<Network>(GetProcAddress(api,"SteamAPI_ISteamClient_GetISteamNetworking"));
    if(!user||!pipe||!factory||!network){report<<"status=missing_exports\n";return;}
    int u=user(),p=pipe();if(!u||!p){report<<"status=steam_not_ready\n";return;}
    auto client=factory("SteamClient017");if(!client){report<<"status=no_client\n";return;}
    observed=network(client,u,p,"SteamNetworking005");
    void* table=nullptr;void* slots[3]{};SIZE_T copied=0;
    if(!observed || !ReadProcessMemory(GetCurrentProcess(),observed,&table,sizeof(table),&copied) || copied!=sizeof(table)
        || !ReadProcessMemory(GetCurrentProcess(),table,slots,sizeof(slots),&copied) || copied!=sizeof(slots)) {report<<"status=invalid_vtable\n";return;}
    HMODULE steamclient=GetModuleHandleW(L"steamclient64.dll");
    for(unsigned i: {0u,2u}) {
        MEMORY_BASIC_INFORMATION info{};
        if(!steamclient || !VirtualQuery(slots[i],&info,sizeof(info)) || info.AllocationBase!=steamclient
            || !(info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))) {report<<"status=unexpected_target_module\n";return;}
    }
    if(slots[0]==slots[2]){report<<"status=aliased_targets\n";return;}
    auto init=MH_Initialize();if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED){report<<"status=hook_init_failed\n";return;}
    if(MH_CreateHook(slots[0],reinterpret_cast<void*>(&send),reinterpret_cast<void**>(&original_send))!=MH_OK){report<<"status=send_hook_failed\n";return;}
    if(MH_CreateHook(slots[2],reinterpret_cast<void*>(&read),reinterpret_cast<void**>(&original_read))!=MH_OK){MH_RemoveHook(slots[0]);report<<"status=read_hook_failed\n";return;}
    // Trampolines remain pass-through after the window. Pin our code for process lifetime.
    HMODULE pin=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&send),&pin)){
        MH_RemoveHook(slots[0]);MH_RemoveHook(slots[2]);report<<"status=pin_failed\n";return;
    }
    if(MH_EnableHook(slots[0])!=MH_OK){MH_RemoveHook(slots[0]);MH_RemoveHook(slots[2]);report<<"status=send_enable_failed\n";return;}
    if(MH_EnableHook(slots[2])!=MH_OK){MH_DisableHook(slots[0]);report<<"status=read_enable_failed\n";return;}
    deadline=now+180000;active=true;report<<"status=recording\n";
}
}
