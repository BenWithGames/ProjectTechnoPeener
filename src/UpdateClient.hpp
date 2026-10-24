#pragma once
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include "UpdateBuild.hpp"
namespace sc6::updater {
// Used only by the overlay thread; no network or updater waits on the game thread.
class Client {
    std::filesystem::path directory;
    HANDLE helper{};
    ULONGLONG last_poll{};
    ULONGLONG last_request{};
    std::wstring status_=L"Updater starting...";
public:
    ~Client(){if(helper)CloseHandle(helper);} // Helper deliberately survives game exit.
    void start() noexcept {
        try {
            wchar_t exe[32768]{};
            if(!GetModuleFileNameW(nullptr,exe,32768))return;
            const auto mod=std::filesystem::path(exe).parent_path()/L"Mods/SC6Rollback";
            directory=mod/L"updates";
            std::filesystem::create_directories(directory);
            const auto script=mod/L"SC6Astral-Updater.ps1";
            if(!std::filesystem::exists(script)){status_=L"Updater not installed";return;}
            wchar_t system[32768]{};GetSystemDirectoryW(system,32768);
            const auto powershell=std::filesystem::path(system)/L"WindowsPowerShell/v1.0/powershell.exe";
            auto command=L"\""+powershell.wstring()+L"\" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \""+
                script.wstring()+L"\" -ModDirectory \""+mod.wstring()+L"\" -GameProcessId "+
                std::to_wstring(GetCurrentProcessId())+L" -CurrentBuild "+build;
            STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
            if(CreateProcessW(powershell.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|BELOW_NORMAL_PRIORITY_CLASS,
                              nullptr,mod.c_str(),&startup,&process)){
                helper=process.hProcess;CloseHandle(process.hThread);
            } else status_=L"Updater unavailable";
        } catch (...) {status_=L"Updater unavailable";}
    }
    unsigned cooldown_seconds() const noexcept {
        if(!last_request)return 0;
        const auto elapsed=GetTickCount64()-last_request;
        return elapsed>=30000?0:static_cast<unsigned>((30000-elapsed+999)/1000);
    }
    bool can_check() noexcept {
        const auto& current=status();
        return cooldown_seconds()==0 && current!=L"Checking for updates..." && current!=L"Downloading verified update..." &&
               current!=L"Update ready - installs after exit";
    }
    void check() noexcept {
        try {
            const auto now=GetTickCount64();
            if(!can_check())return;
            if(!helper || WaitForSingleObject(helper,0)==WAIT_OBJECT_0){
                if(helper){CloseHandle(helper);helper=nullptr;}start();
            }
            if(!directory.empty()){
                std::ofstream request(directory/L"check.request");request<<"check";request.close();
                if(!request){status_=L"Update check unavailable";return;}
                last_request=now;last_poll=now;status_=L"Checking for updates...";
            }
        } catch (...) {status_=L"Update check unavailable";}
    }
    const std::wstring& status() noexcept {
        const auto now=GetTickCount64();if(now-last_poll<1000 || (last_request && now-last_request<2000))return status_;last_poll=now;
        try {
            std::ifstream file(directory/L"status.txt");std::string text;
            for(char c; text.size()<100 && file.get(c);){if(c>=32 && c<=126)text+=c;}
            if(!text.empty())status_.assign(text.begin(),text.end());
            else if(helper && WaitForSingleObject(helper,0)==WAIT_OBJECT_0)status_=L"Updater unavailable";
        } catch (...) {}
        return status_;
    }
};
}
