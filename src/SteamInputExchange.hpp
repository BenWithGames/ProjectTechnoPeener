#pragma once
#include "InputObservation.hpp"
#include "LiveShadowPrediction.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>
namespace sc6::steam_inputs {
constexpr unsigned limit=observation::capacity;
constexpr uint64_t signature=0x0002534336494e50ULL;
struct Packet {
    uint64_t magic=signature,nonce=0,echo=0;
    uint32_t kind=0,index=0;
    observation::Record record{};
    uint32_t own_hash=0,received_hash=0;
};
static_assert(sizeof(Packet)==96);
struct Exchange {
    std::array<observation::Record,limit> local{},remote{};
    std::array<bool,limit> seen{};
    unsigned published=0,received=0,cursor=0,fresh=0;
    bool peer_match=false;
    bool accept(const Packet& p) {
        if(p.magic!=signature)return false;
        if(p.kind==1){
            if(p.index>=limit||p.record.data[13]!=0)return false;
            if(seen[p.index]){if(remote[p.index].data!=p.record.data)throw std::runtime_error("conflicting_input_record");}
            else {remote[p.index]=p.record;seen[p.index]=true;++received;}
            return true;
        }
        if(p.kind==2 && published==limit && received==limit){
            if(p.own_hash!=observation::checksum(remote)||p.received_hash!=observation::checksum(local))throw std::runtime_error("peer_checksum_mismatch");
            peer_match=true;return true;
        }
        return false;
    }
};
struct Capture {
    HANDLE mapping=nullptr;const observation::Shared* shared=nullptr;
    bool armed=false;uint64_t armed_at=0;
    Exchange exchange;
    LiveShadowPrediction shadow;
    std::array<uint64_t,limit> arrival_qpc{},first_send_qpc{};
    bool accept(const Packet& packet) {
        const bool first=packet.kind==1 && packet.index<limit && !exchange.seen[packet.index];
        const bool accepted=exchange.accept(packet);
        if(accepted && first){LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);arrival_qpc[packet.index]=stamp.QuadPart;}
        return accepted;
    }
    ~Capture(){if(shared)UnmapViewOfFile(shared);if(mapping)CloseHandle(mapping);}
    void arm(const std::filesystem::path& directory,uint64_t nonce) {
        mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,observation::mapping_name(GetCurrentProcessId()).c_str());
        if(!mapping)throw std::runtime_error("recorder_mapping_missing");
        shared=static_cast<const observation::Shared*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,sizeof(observation::Shared)));
        if(!shared||shared->signature!=observation::magic||shared->schema!=observation::version||shared->pid!=GetCurrentProcessId()||shared->status!=0)throw std::runtime_error("recorder_unavailable_restart_game");
        for(const auto& entry:std::filesystem::directory_iterator(directory))if(entry.path().extension()==L".arm")throw std::runtime_error("conflicting_capture_arm");
        auto path=directory/L"network-observe.arm";
        HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("cannot_arm_recorder");
        const auto text="NETWORK_OBSERVE_LONG "+std::to_string(GetCurrentProcessId())+" "+std::to_string(nonce)+"\n";
        DWORD written=0;bool ok=WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)!=FALSE;CloseHandle(file);
        if(!ok||written!=text.size())throw std::runtime_error("arm_write_failed");
        armed=true;armed_at=GetTickCount64();
    }
    void update(uint64_t nonce) {
        if(!armed)return;
        if(shared->nonce!=nonce){if(GetTickCount64()-armed_at>5000)throw std::runtime_error("recorder_did_not_start");return;}
        LONG count=shared->count;MemoryBarrier();
        if(shared->status==3)throw std::runtime_error("recorder_failed");
        if(count<0||count>static_cast<LONG>(limit))throw std::runtime_error("invalid_record_count");
        while(exchange.published<static_cast<unsigned>(count)){exchange.local[exchange.published]=shared->records[exchange.published];++exchange.published;}
    }
    bool complete()const{return shared && shared->status==2 && exchange.published==limit && exchange.received==limit && exchange.peer_match;}
    void export_csv(const std::filesystem::path& base)const {
        for(unsigned side=0;side<2;++side){
            std::ofstream out(base.string()+(side?"-remote.csv":"-local.csv"));
            out<<"sample";for(unsigned w=0;w<14;++w)out<<",word"<<w;out<<'\n';
            const auto& records=side?exchange.remote:exchange.local;
            for(unsigned i=0;i<limit;++i){out<<i;for(auto word:records[i].data)out<<','<<word;out<<'\n';}
            out.flush();if(!out)throw std::runtime_error("csv_write_failed");
        }
        std::ofstream timing(base.string()+"-timing.csv");
        timing<<"sample,remote_outer,arrival_qpc,local_outer,first_send_qpc\n";
        for(unsigned i=0;i<limit;++i)timing<<i<<','<<exchange.remote[i].data[0]<<','<<arrival_qpc[i]<<','<<exchange.local[i].data[0]<<','<<first_send_qpc[i]<<'\n';
        timing.flush();if(!timing)throw std::runtime_error("timing_write_failed");
        std::ofstream decisions(base.string()+"-shadow.csv");
        decisions<<"sample,outer,scheduled_qpc,predicted,seeded,settled,held,edge\n";
        for(unsigned i=0;i<shadow.scheduled;++i){const auto& c=shadow.choices[i];decisions<<i<<','<<exchange.local[i].data[0]<<','<<LiveShadowPrediction::tick(exchange.local[i+LiveShadowPrediction::delay])<<','<<c.predicted<<','<<c.seeded<<','<<c.settled<<','<<c.held<<','<<c.edge<<'\n';}
        decisions.flush();if(!decisions)throw std::runtime_error("shadow_write_failed");
    }
};
}
