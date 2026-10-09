#pragma once
#include "FullRestoreStream.hpp"
namespace sc6::research {
struct CameraSnapshot {
    struct Record {
        uintptr_t owner{}; std::array<std::byte,0x1C8> bytes{};
        // Packed flags at1E0 (8bytes), then16float history/count/index at25C (72bytes).
        std::array<std::byte,80> smoothing{};
        // Packed target/cache/interpolation [1E8,25C), selection [2A4,2B0).
        std::array<std::byte,128> targets{};
        // Three response factors at1D0..1DB; only1D0/1D8 are restored.
        std::array<std::byte,12> response{};
        bool smoothing_valid{};
    };
    std::array<Record,32> records{};
    unsigned count{},failure{},exception{};
    bool valid{};
    static unsigned word(const std::byte* p){unsigned v{};std::memcpy(&v,p,4);return v;}
    static bool rising_factor(unsigned a,unsigned b) noexcept {
        if(a>0x3F800000u || b>0x3F800000u)return false;
        float value{};std::memcpy(&value,&a,4);
        value=value+0.1f;if(value>1.0f)value=1.0f;
        unsigned expected{};std::memcpy(&expected,&value,4);return b==expected;
    }
    bool adjacent_combat_ready(const CameraSnapshot& next,uintptr_t base,bool charge_rising=false)const noexcept {
        if(!valid || !next.valid || !count || count>records.size() || count!=next.count)return false;
        unsigned selected=0;
        for(unsigned i=0;i<count;++i){auto& a=records[i];auto& b=next.records[i];
            if(a.owner!=b.owner || a.smoothing_valid!=b.smoothing_valid ||
               std::memcmp(a.bytes.data(),b.bytes.data(),8))return false;
            if(!a.smoothing_valid)continue;
            uintptr_t vt{};std::memcpy(&vt,a.bytes.data(),8);
            if(vt!=base+0x3E87EB0 || ++selected!=1)return false;
            for(auto* r:{&a,&b}){
                if(word(r->smoothing.data()) || word(r->smoothing.data()+4) ||
                   word(r->smoothing.data()+72)>0x7FFFFFFFu || word(r->smoothing.data()+76)>=16)return false;
                for(unsigned j=8;j<72;j+=4)if((word(r->smoothing.data()+j)&0x7F800000u)==0x7F800000u)return false;
                if(word(r->response.data()+4)!=0x3EC00000u || word(r->response.data())>0x3F800000u ||
                   word(r->response.data()+8)>0x3F800000u)return false;
                if(!charge_rising && (word(r->targets.data()+0x68)!=0x3F800000u || word(r->targets.data()+0x6C)!=0x3F800000u))return false;
            }
            auto old=word(a.smoothing.data()+72);
            if(word(b.smoothing.data()+72)!=(old==0x7FFFFFFFu?old:old+1) ||
               word(b.smoothing.data()+76)!=(word(a.smoothing.data()+76)+1)%16)return false;
            for(unsigned j=0;j<128;j+=4){
                bool position=(j>=0x48 && j<0x54)||(j>=0x58 && j<0x64);
                if(charge_rising && (j==0x68 || j==0x6C)){
                    if(!rising_factor(word(a.targets.data()+j),word(b.targets.data()+j)))return false;
                }else if(position){if((word(a.targets.data()+j)&0x7F800000u)==0x7F800000u ||
                                (word(b.targets.data()+j)&0x7F800000u)==0x7F800000u)return false;}
                else if(word(a.targets.data()+j)!=word(b.targets.data()+j))return false;
            }
        }
        return selected==1;
    }
    bool restore_smoothing(const CameraSnapshot& next,const CameraSnapshot& current,uintptr_t base,bool immediate_throw=false,bool adjacent_throw=false,bool validate_only=false,bool adjacent_charge=false)const noexcept {
        __try {
            if(immediate_throw && adjacent_throw)return false;
            if(adjacent_charge && (immediate_throw || !adjacent_throw || !adjacent_combat_ready(next,base,true)))return false;
            if(!valid || !next.valid || !current.valid || !count || count>records.size() || count!=next.count || count!=current.count)return false;
            unsigned selected=count;
            for(unsigned i=0;i<count;++i){auto& a=records[i];auto& b=next.records[i];auto& c=current.records[i];
                if(a.owner!=b.owner || a.owner!=c.owner || std::memcmp(a.bytes.data(),b.bytes.data(),8) || std::memcmp(a.bytes.data(),c.bytes.data(),8))return false;
                if(!a.smoothing_valid)continue;
                uintptr_t vt{};std::memcpy(&vt,a.bytes.data(),8);
                if(selected!=count || vt!=base+0x3E87EB0 || !b.smoothing_valid || !c.smoothing_valid)return false;
                selected=i;
                if(immediate_throw && (a.bytes!=b.bytes || a.smoothing!=b.smoothing))return false;
                if(std::memcmp(c.bytes.data(),reinterpret_cast<void*>(a.owner),c.bytes.size()) ||
                   std::memcmp(c.smoothing.data(),reinterpret_cast<void*>(a.owner+0x1E0),8) ||
                   std::memcmp(c.smoothing.data()+8,reinterpret_cast<void*>(a.owner+0x25C),72))return false;
                for(unsigned j=0;j<a.bytes.size();++j)
                    if((j<0x154 || j>=0x158) && a.bytes[j]!=c.bytes[j])return false;
                if(word(c.bytes.data()+0x154)!=0)return false;
                for(unsigned j=0;j<8;j+=4)
                    if(word(a.smoothing.data()+j)!=0 || word(b.smoothing.data()+j)!=0 || word(c.smoothing.data()+j)!=1)return false;
                for(unsigned j=8;j<80;++j)if(c.smoothing[j]!=std::byte{})return false;
                for(auto* source:{&a,&b}){
                    if((word(source->bytes.data()+0x154)&0x7F800000u)==0x7F800000u)return false;
                    for(unsigned j=8;j<72;j+=4)if((word(source->smoothing.data()+j)&0x7F800000u)==0x7F800000u)return false;
                    if(word(source->smoothing.data()+72)>0x7FFFFFFFu || word(source->smoothing.data()+76)>=16)return false;
                }
                // Idle-only target correction: stable A/B, observed load reset, live identity.
                if((!adjacent_throw && a.targets!=b.targets) ||
                   std::memcmp(c.targets.data(),reinterpret_cast<void*>(a.owner+0x1E8),116) ||
                   std::memcmp(c.targets.data()+116,reinterpret_cast<void*>(a.owner+0x2A4),12))return false;
                for(unsigned j=0;j<128;j+=4){
                    const bool position=(j>=0x48 && j<0x54)||(j>=0x58 && j<0x64);
                    const bool factor=j==0x68 || j==0x6C;
                    auto av=word(a.targets.data()+j),bv=word(b.targets.data()+j),cv=word(c.targets.data()+j);
                    if(adjacent_throw && !position && !(adjacent_charge && factor) && av!=bv)return false;
                    if(position){
                        if((av&0x7F800000u)==0x7F800000u || (bv&0x7F800000u)==0x7F800000u || cv!=0)return false;
                    }else if(factor){
                        if((adjacent_charge?!rising_factor(av,bv):av!=((immediate_throw || adjacent_throw)?0x3F800000u:0u)) || cv!=0x3F800000u)return false;
                    }else if(av!=cv)return false;
                }
                if((!adjacent_throw && a.response!=b.response) ||
                   std::memcmp(c.response.data(),reinterpret_cast<void*>(a.owner+0x1D0),12))return false;
                for(unsigned j=0;j<12;j+=4){
                    auto av=word(a.response.data()+j);
                    if(word(c.response.data()+j)!=0x3EC00000u)return false;
                    if(j==4){if(av!=0x3EC00000u || word(b.response.data()+j)!=0x3EC00000u)return false;}
                    else if(immediate_throw || adjacent_throw){if(av>0x3F800000u || word(b.response.data()+j)>0x3F800000u)return false;} // finite [0,1], positive bits
                    else if(av!=0)return false;
                }
                auto old_count=word(a.smoothing.data()+72);
                if(!immediate_throw && (word(b.smoothing.data()+72)!=(old_count==0x7FFFFFFFu?old_count:old_count+1) ||
                   word(b.smoothing.data()+76)!=(word(a.smoothing.data()+76)+1)%16))return false;
            }
            if(selected==count)return false;
            if(validate_only)return true;
            auto& a=records[selected];
            std::memcpy(reinterpret_cast<void*>(a.owner+0x154),a.bytes.data()+0x154,4);
            std::memcpy(reinterpret_cast<void*>(a.owner+0x1E0),a.smoothing.data(),8);
            std::memcpy(reinterpret_cast<void*>(a.owner+0x25C),a.smoothing.data()+8,72);
            std::memcpy(reinterpret_cast<void*>(a.owner+0x230),a.targets.data()+0x48,12);
            std::memcpy(reinterpret_cast<void*>(a.owner+0x240),a.targets.data()+0x58,12);
            std::memcpy(reinterpret_cast<void*>(a.owner+0x250),a.targets.data()+0x68,8);
            std::memcpy(reinterpret_cast<void*>(a.owner+0x1D0),a.response.data(),4);
            std::memcpy(reinterpret_cast<void*>(a.owner+0x1D8),a.response.data()+8,4);
            return !std::memcmp(reinterpret_cast<void*>(a.owner+0x1D0),a.response.data(),12) &&
                !std::memcmp(reinterpret_cast<void*>(a.owner+0x1E8),a.targets.data(),116) &&
                !std::memcmp(reinterpret_cast<void*>(a.owner+0x2A4),a.targets.data()+116,12) &&
                !std::memcmp(reinterpret_cast<void*>(a.owner+0x154),a.bytes.data()+0x154,4) &&
                !std::memcmp(reinterpret_cast<void*>(a.owner+0x1E0),a.smoothing.data(),8) &&
                !std::memcmp(reinterpret_cast<void*>(a.owner+0x25C),a.smoothing.data()+8,72);
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool capture(const FullRestoreStream& stream,uintptr_t base) noexcept {
        count=0;failure=0;exception=0;valid=false;
        __try {
            if(!stream.sealed){failure=1;return false;}
            constexpr unsigned sites[]={0x33FC68,0x33FC7B,0x33FC8E,0x33FE69,0x33FE7F,0x33FE95};
            constexpr unsigned offsets[]={0x50,0x60,0x70,0x110,0x120,0x130};
            constexpr unsigned sizes[]={16,16,4,16,4,16};
            for(unsigned i=0;i<stream.writes;++i){
                if(stream.traces[i].writer!=base+sites[0])continue;
                auto source=stream.traces[i].source;
                if(source<0x10050){failure=2;return false;}
                auto owner=source-0x50;
                if(owner>UINTPTR_MAX-0x1C8){failure=2;return false;}
                for(unsigned k=0;k<6;++k){unsigned matches=0;
                    for(unsigned j=0;j<stream.writes;++j)
                        if(stream.traces[j].writer==base+sites[k] && stream.traces[j].source==owner+offsets[k] && stream.lengths[j]==sizes[k])++matches;
                    if(matches!=1){failure=3;return false;}
                }
                if(count==records.size()){failure=4;return false;}
                auto& r=records[count];r.owner=owner;r.smoothing_valid=false;
                std::memcpy(r.bytes.data(),reinterpret_cast<void*>(owner),r.bytes.size());
                uintptr_t vtable{};std::memcpy(&vtable,r.bytes.data(),8);
                if(vtable==base+0x3E87EB0){
                    if(owner>UINTPTR_MAX-0x2B0){failure=2;return false;}
                    std::memcpy(r.smoothing.data(),reinterpret_cast<void*>(owner+0x1E0),8);
                    std::memcpy(r.smoothing.data()+8,reinterpret_cast<void*>(owner+0x25C),72);
                    std::memcpy(r.targets.data(),reinterpret_cast<void*>(owner+0x1E8),116);
                    std::memcpy(r.targets.data()+116,reinterpret_cast<void*>(owner+0x2A4),12);
                    std::memcpy(r.response.data(),reinterpret_cast<void*>(owner+0x1D0),12);
                    r.smoothing_valid=true;
                }
                ++count;
            }
            if(!count){failure=5;return false;}
            for(unsigned i=0;i<count;++i){auto& r=records[i];
                if(std::memcmp(r.bytes.data(),reinterpret_cast<void*>(r.owner),r.bytes.size())){failure=6;return false;}
                if(r.smoothing_valid && (std::memcmp(r.smoothing.data(),reinterpret_cast<void*>(r.owner+0x1E0),8) ||
                   std::memcmp(r.smoothing.data()+8,reinterpret_cast<void*>(r.owner+0x25C),72) ||
                   std::memcmp(r.targets.data(),reinterpret_cast<void*>(r.owner+0x1E8),116) ||
                   std::memcmp(r.targets.data()+116,reinterpret_cast<void*>(r.owner+0x2A4),12) ||
                   std::memcmp(r.response.data(),reinterpret_cast<void*>(r.owner+0x1D0),12))){failure=6;return false;}
            }
            valid=true;return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){exception=GetExceptionCode();failure=7;return false;}
    }
};
}
