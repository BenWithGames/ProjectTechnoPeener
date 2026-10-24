#pragma once
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include "ReportMatchId.hpp"
namespace sc6::automatic_report {
inline bool identify(const std::filesystem::path& directory,uint64_t local,uint64_t remote){
    const auto id=match_id(local,remote);if(id.empty())return false;
    const auto temporary=directory/L"match-id.tmp";
    {std::ofstream out(temporary);out<<id;out.flush();if(!out.good())return false;}
    return MoveFileExW(temporary.c_str(),(directory/L"match-id.txt").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
}
inline bool launch(const std::filesystem::path& directory){
    const auto script=directory.parent_path().parent_path()/L"Archive-AutomaticReport.ps1";
    wchar_t system[MAX_PATH]{};if(!GetSystemDirectoryW(system,MAX_PATH))return false;
    const auto exe=std::filesystem::path(system)/L"WindowsPowerShell/v1.0/powershell.exe";
    auto command=L"\""+exe.wstring()+L"\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \""+script.wstring()+
        L"\" -ReportFolder \""+directory.wstring()+L"\" -ParentId "+std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION process{};
    if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)){
        std::ofstream(directory/L"archive-error.txt")<<"Could not launch ZIP helper. Windows error "<<GetLastError();return false;
    }
    CloseHandle(process.hThread);CloseHandle(process.hProcess);return true;
}
inline bool finish(const std::filesystem::path& directory,bool passed){
    const auto temporary=directory/L"archive.ready.tmp";
    {std::ofstream marker(temporary,std::ios::trunc);marker<<(passed?"pass":"fails");marker.flush();if(!marker.good())return false;}
    return MoveFileExW(temporary.c_str(),(directory/L"archive.ready").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
}
}
