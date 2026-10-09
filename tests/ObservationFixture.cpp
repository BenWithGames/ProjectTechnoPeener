#include "../src/InputObservation.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
int main(int argc,char** argv){
    if(argc!=3)return 1;using namespace sc6::observation;
    const auto pid=GetCurrentProcessId();const unsigned tag=static_cast<unsigned>(std::stoul(argv[2]));
    HANDLE mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Shared),mapping_name(pid).c_str());
    if(!mapping)return 2;
    auto* shared=static_cast<Shared*>(MapViewOfFile(mapping,FILE_MAP_WRITE,0,0,sizeof(Shared)));if(!shared)return 3;
    *shared={};shared->schema=version;shared->pid=pid;shared->frequency=1000000;shared->signature=magic;
    std::cout<<pid<<'\n'<<std::flush;
    auto start=GetTickCount64();bool produced=false;
    const auto arm=std::filesystem::path(argv[1])/"network-observe.arm";
    while(GetTickCount64()-start<20000){
        if(!produced && std::filesystem::exists(arm)){
            std::ifstream in(arm);std::string token;unsigned requested{};std::uint64_t nonce{};in>>token>>requested>>nonce;in.close();
            if(token!="NETWORK_OBSERVE_ONLY" || requested!=pid || !nonce)return 4;
            std::filesystem::remove(arm);shared->nonce=nonce;shared->status=1;
            for(unsigned f=0;f<limit;++f){auto& r=shared->records[f];r.data[0]=1000+tag*100+f;
                for(unsigned w=1;w<=10;++w)r.data[w]=(tag<<24)|(f<<8)|w;
                r.data[11]=f*1000;MemoryBarrier();InterlockedExchange(&shared->count,static_cast<LONG>(f+1));Sleep(2);}
            InterlockedExchange(&shared->status,2);produced=true;
        }
        Sleep(10);
    }
    UnmapViewOfFile(shared);CloseHandle(mapping);return produced?0:5;
}
