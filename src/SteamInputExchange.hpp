#pragma once
#include "InputObservation.hpp"
#include "OwnedTransportAttachment.hpp"
#include "LiveShadowPrediction.hpp"
#include "LiveInputComposition.hpp"
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
    std::array<observation::Round,limit> local_rounds{},remote_rounds{};
    unsigned published=0,received=0,cursor=0,fresh=0;
    bool peer_match=false;
    static unsigned checksum(const std::array<observation::Record,limit>& records,
                             const std::array<observation::Round,limit>& rounds){
        unsigned h=observation::checksum(records);
        for(const auto& r:rounds)for(auto word:{r.number,r.event_counter})
            for(unsigned shift=0;shift<32;shift+=8)h=(h^((word>>shift)&255u))*16777619u;
        return h;
    }
    bool accept(const Packet& p) {
        if(p.magic!=signature)return false;
        if(p.kind==1){
            if(p.index>=limit||p.record.data[13]!=0)return false;
            if(seen[p.index]){if(remote[p.index].data!=p.record.data || remote_rounds[p.index].number!=p.own_hash || remote_rounds[p.index].event_counter!=p.received_hash)throw std::runtime_error("conflicting_input_record");}
            else {remote[p.index]=p.record;remote_rounds[p.index]={p.own_hash,p.received_hash};seen[p.index]=true;++received;}
            return true;
        }
        if(p.kind==2 && published==limit && received==limit){
            if(p.own_hash!=checksum(remote,remote_rounds)||p.received_hash!=checksum(local,local_rounds))throw std::runtime_error("peer_checksum_mismatch");
            peer_match=true;return true;
        }
        return false;
    }
};
struct Capture {
    HANDLE mapping=nullptr;const observation::Shared* shared=nullptr;
    bool armed=false;uint64_t armed_at=0;
    uint64_t owned_nonce{};
    Exchange exchange;
    LiveShadowPrediction shadow;
    LiveInputComposition composition;
    std::array<uint64_t,limit> arrival_qpc{},first_send_qpc{},import_qpc{},first_send_begin_qpc{};
    struct Pump {uint64_t start{},receive_end{},send_begin{},send_end{};unsigned published{},received{};};
    std::array<Pump,65536> pumps{};unsigned pump_count{},pump_dropped{};
    static uint64_t timestamp(){LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);return stamp.QuadPart;}
    void record_pump(uint64_t start,uint64_t receive_end,uint64_t send_begin){
        if(pump_count==pumps.size()){++pump_dropped;return;}
        pumps[pump_count++]={start,receive_end,send_begin,timestamp(),exchange.published,exchange.received};
    }
    bool accept(const Packet& packet) {
        const bool first=packet.kind==1 && packet.index<limit && !exchange.seen[packet.index];
        const bool accepted=exchange.accept(packet);
        if(accepted && first){LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);arrival_qpc[packet.index]=stamp.QuadPart;}
        return accepted;
    }
    ~Capture(){research::owned_transport_attachment.detach(owned_nonce);if(shared)UnmapViewOfFile(shared);if(mapping)CloseHandle(mapping);}
    // Attach to a mapping initialized by the game-thread combat controller.
    // Unlike arm(), this never creates network-observe.arm or starts a writer.
    bool attach_owned(uint64_t nonce){
        if(armed || mapping || shared || !nonce)return false;
        const auto handle=OpenFileMappingW(FILE_MAP_READ,FALSE,observation::mapping_name(GetCurrentProcessId()).c_str());
        if(!handle)return false;
        const auto view=static_cast<const observation::Shared*>(MapViewOfFile(handle,FILE_MAP_READ,0,0,sizeof(observation::Shared)));
        const bool valid=view && view->signature==observation::magic && view->schema==observation::version &&
            view->pid==GetCurrentProcessId() && view->reserved==observation::owned_mode && view->nonce==nonce &&
            view->frequency && view->status==1 && view->count==0;
        if(!valid){if(view)UnmapViewOfFile(view);CloseHandle(handle);return false;}
        if(!research::owned_transport_attachment.attach(nonce)){UnmapViewOfFile(view);CloseHandle(handle);return false;}
        owned_nonce=nonce;
        mapping=handle;shared=view;armed=true;armed_at=GetTickCount64();return true;
    }
    void arm(const std::filesystem::path& directory,uint64_t nonce,bool checkpoints=false,bool replay=false,bool late=false,bool reuse=false) {
        if(armed || mapping || shared)throw std::runtime_error("capture_already_attached");
        mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,observation::mapping_name(GetCurrentProcessId()).c_str());
        if(!mapping)throw std::runtime_error("recorder_mapping_missing");
        shared=static_cast<const observation::Shared*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,sizeof(observation::Shared)));
        if(!shared||shared->signature!=observation::magic||shared->schema!=observation::version||shared->pid!=GetCurrentProcessId()||shared->status!=0)throw std::runtime_error("recorder_unavailable_restart_game");
        for(const auto& entry:std::filesystem::directory_iterator(directory))if(entry.path().extension()==L".arm")throw std::runtime_error("conflicting_capture_arm");
        auto path=directory/L"network-observe.arm";
        HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("cannot_arm_recorder");
        const auto text=std::string(reuse?"NETWORK_REUSE_LONG ":late?"NETWORK_LATE_LONG ":replay?"NETWORK_REPLAY_LONG ":checkpoints?"NETWORK_CHECKPOINT_LONG ":"NETWORK_OBSERVE_LONG ")+std::to_string(GetCurrentProcessId())+" "+std::to_string(nonce)+"\n";
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
        while(exchange.published<static_cast<unsigned>(count)){exchange.local[exchange.published]=shared->records[exchange.published];exchange.local_rounds[exchange.published]=shared->rounds[exchange.published];import_qpc[exchange.published]=timestamp();++exchange.published;}
    }
    bool complete()const{return shared && shared->status==2 && exchange.published==limit && exchange.received==limit && exchange.peer_match;}
    void export_csv(const std::filesystem::path& base)const {
        std::ofstream rounds(base.string()+"-rounds.csv");
        rounds<<"sample,local_outer,local_round,local_event_counter,remote_outer,remote_round,remote_event_counter\n";
        for(unsigned i=0;i<limit;++i)rounds<<i<<','<<exchange.local[i].data[0]<<','<<exchange.local_rounds[i].number<<','<<exchange.local_rounds[i].event_counter<<','<<exchange.remote[i].data[0]<<','<<exchange.remote_rounds[i].number<<','<<exchange.remote_rounds[i].event_counter<<'\n';
        rounds.flush();if(!rounds)throw std::runtime_error("round_csv_write_failed");
        std::ofstream composed(base.string()+"-composed.csv");
        composed<<"sample,outer,remote_slot";for(unsigned w=0;w<10;++w)composed<<",word"<<w+1;composed<<'\n';
        for(unsigned i=0;i<limit;++i)if(composition.staged[i]){
            composed<<i<<','<<exchange.local[i].data[0]<<','<<composition.peer_slot;
            for(auto word:composition.envelopes[i])composed<<','<<word;composed<<'\n';
        }
        composed.flush();if(!composed)throw std::runtime_error("composed_csv_write_failed");
        for(unsigned side=0;side<2;++side){
            std::ofstream out(base.string()+(side?"-remote.csv":"-local.csv"));
            out<<"sample";for(unsigned w=0;w<14;++w)out<<",word"<<w;out<<'\n';
            const auto& records=side?exchange.remote:exchange.local;
            for(unsigned i=0;i<limit;++i){out<<i;for(auto word:records[i].data)out<<','<<word;out<<'\n';}
            out.flush();if(!out)throw std::runtime_error("csv_write_failed");
        }
        std::ofstream timing(base.string()+"-timing.csv");
        timing<<"sample,remote_outer,arrival_qpc,local_outer,first_send_qpc,import_qpc,first_send_begin_qpc\n";
        for(unsigned i=0;i<limit;++i)timing<<i<<','<<exchange.remote[i].data[0]<<','<<arrival_qpc[i]<<','<<exchange.local[i].data[0]<<','<<first_send_qpc[i]<<','<<import_qpc[i]<<','<<first_send_begin_qpc[i]<<'\n';
        timing.flush();if(!timing)throw std::runtime_error("timing_write_failed");
        std::ofstream pump_file(base.string()+"-pipeline.csv");
        pump_file<<"start_qpc,receive_end_qpc,send_begin_qpc,send_end_qpc,published,received\n";
        for(unsigned i=0;i<pump_count;++i){const auto& p=pumps[i];pump_file<<p.start<<','<<p.receive_end<<','<<p.send_begin<<','<<p.send_end<<','<<p.published<<','<<p.received<<'\n';}
        pump_file.flush();if(!pump_file)throw std::runtime_error("pipeline_write_failed");
        std::ofstream decisions(base.string()+"-shadow.csv");
        decisions<<"sample,outer,scheduled_qpc,predicted,seeded,settled,held,edge\n";
        for(unsigned i=0;i<shadow.scheduled;++i){const auto& c=shadow.choices[i];decisions<<i<<','<<exchange.local[i].data[0]<<','<<LiveShadowPrediction::tick(exchange.local[i+LiveShadowPrediction::delay])<<','<<c.predicted<<','<<c.seeded<<','<<c.settled<<','<<c.held<<','<<c.edge<<'\n';}
        decisions.flush();if(!decisions)throw std::runtime_error("shadow_write_failed");
    }
};
}

