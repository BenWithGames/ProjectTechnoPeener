#pragma once
#include "OwnedPredictionCorrection.hpp"
#include <atomic>
namespace sc6::research {
// One session, one agreed three-frame window. Outer Steam packet validation
// authenticates sender/nonces/profile. Game writes local slots once; worker
// writes peer slots once. Release publication prevents partial-record reads.
class StartupCorrectionExchange {
public:
    using Words=std::array<uint32_t,14>;
    std::atomic<unsigned> local_count{},remote_count{};
    std::atomic<bool> configured{},failed{},ready{};
private:
    unsigned target_{},slot_{};
    std::array<Words,3> local_{},remote_{};
    std::array<RollbackBoundaryKey,3> keys_{};
    std::array<bool,3> seen_{};
    Words peer_certificate_{};bool certificate_seen_{};
    bool reject(){failed.store(true);return false;}
    static uint32_t hash(const std::array<Words,3>& rows){
        uint32_t h=2166136261u;
        for(const auto& row:rows)for(auto word:row)for(unsigned shift=0;shift<32;shift+=8)h=(h^((word>>shift)&255u))*16777619u;
        return h;
    }
    bool complete_inputs(){
        if(!configured.load(std::memory_order_acquire) || failed.load() || local_count.load(std::memory_order_acquire)!=3 || remote_count.load()!=3)return false;
        for(unsigned i=0;i<3;++i){
            if(local_[i][10]!=remote_[i][10] || local_[i][11]!=remote_[i][11] || local_[i][12]!=remote_[i][12])return reject();
        }
        return true;
    }
public:
    bool configure(unsigned target,unsigned slot){
        if(configured.load())return target_==target && slot_==slot;
        if(!target || target>UINT32_MAX-3 || slot<1 || slot>2)return false;
        target_=target;slot_=slot;configured.store(true,std::memory_order_release);return true;
    }
    // Game owner only, in increasing order, immediately following each save.
    bool publish(const RollbackBoundaryKey& key,const OwnedPredictionCorrection::Input& input){
        if(!configured.load(std::memory_order_acquire) || failed.load())return false;
        const auto i=local_count.load();
        if(i>=3 || key.frame!=target_+i || key.slot!=slot_ || !key.same_round(key) ||
           (i && !keys_[0].same_round(key)) || !RecordedInput::valid_envelope(input))return reject();
        Words row{};std::memcpy(row.data(),input.data(),40);
        row[10]=key.frame;row[11]=key.round;row[12]=key.event;row[13]=slot_;
        local_[i]=row;keys_[i]=key;local_count.store(i+1,std::memory_order_release);return true;
    }
    // Network worker only. Packets arriving before setup are retried by sender.
    bool receive(unsigned index,const Words& row){
        if(!configured.load(std::memory_order_acquire) || failed.load())return false;
        if(index>=3 || row[10]!=target_+index || !row[11] || row[13]!=3-slot_)return reject();
        OwnedPredictionCorrection::Input bytes{};std::memcpy(bytes.data(),row.data(),40);
        if(!RecordedInput::valid_envelope(bytes))return reject();
        if(seen_[index])return row==remote_[index]?true:reject();
        remote_[index]=row;seen_[index]=true;++remote_count;return true;
    }
    bool local(unsigned i,Words& row)const{
        if(i>=local_count.load(std::memory_order_acquire))return false;row=local_[i];return true;
    }
    bool certificate(Words& out){
        out={};if(!complete_inputs())return false;
        out[0]=hash(local_);out[1]=hash(remote_);
        out[10]=target_;out[11]=local_[0][11];out[12]=local_[0][12];out[13]=slot_;return true;
    }
    bool receive_certificate(const Words& row){
        if(!configured.load(std::memory_order_acquire) || failed.load())return false;
        if(row[10]!=target_ || !row[11] || row[13]!=3-slot_)return reject();
        for(unsigned i=2;i<10;++i)if(row[i])return reject();
        if(certificate_seen_ && peer_certificate_!=row)return reject();
        peer_certificate_=row;certificate_seen_=true;return true;
    }
    void update(){
        if(!certificate_seen_ || !complete_inputs())return;
        const auto& p=peer_certificate_;
        if(p[0]!=hash(remote_) || p[1]!=hash(local_) || p[11]!=local_[0][11] || p[12]!=local_[0][12]){reject();return;}
        ready.store(true,std::memory_order_release);
    }
    // Game owner only after acquiring ready. No partial confirmation escapes.
    bool frames(OwnedPredictionCorrection::Frames& out)const{
        out={};if(!ready.load(std::memory_order_acquire) || failed.load())return false;
        for(unsigned i=0;i<3;++i){out[i].key=keys_[i];out[i].confirmed=true;
            std::memcpy(out[i].local.data(),local_[i].data(),40);std::memcpy(out[i].peer.data(),remote_[i].data(),40);
        }
        return true;
    }
};
}
