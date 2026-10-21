#pragma once
#include "FullRestoreStream.hpp"
#include "CameraSlots.hpp"
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
    // Pinned 31AADC..31AAFA subtracts 0.0765 and clamps to zero;
    // 31AB34..31AB4E also forces zero in mode 1. Thus zero may remain
    // zero while history advances. Do not admit arbitrary stable factors.
    static bool replay_factor(unsigned a,unsigned b) noexcept {
        return (a==0 && b==0) || rising_factor(a,b);
    }
    // Pinned 31A890 updates BOTH +250/+254 on the same branch: +0.1
    // capped at one, -float(0x3D9CAC08) clamped at zero, or mode-1
    // subtraction of one after clamping (therefore both zero). General
    // combat must admit the falling branch; fixture rising policy stays strict.
    static bool falling_factor(unsigned a,unsigned b) noexcept {
        if(a>0x3F800000u || b>0x3F800000u)return false;
        float value{},delta{};const unsigned bits=0x3D9CAC08u;
        std::memcpy(&value,&a,4);std::memcpy(&delta,&bits,4);
        value-=delta;if(value<0.0f)value=0.0f;
        unsigned expected{};std::memcpy(&expected,&value,4);return b==expected;
    }
    static bool general_factor(unsigned a,unsigned b) noexcept {
        return a<=0x3F800000u && b<=0x3F800000u &&
            (b==0 || rising_factor(a,b) || falling_factor(a,b));
    }
    static bool general_factors(unsigned a0,unsigned a1,unsigned b0,unsigned b1) noexcept {
        if(a0>0x3F800000u || a1>0x3F800000u || b0>0x3F800000u || b1>0x3F800000u)return false;
        return (!b0 && !b1) || (rising_factor(a0,b0) && rising_factor(a1,b1)) ||
            (falling_factor(a0,b0) && falling_factor(a1,b1));
    }
    // Pinned 31AC60 resets decision counters to -1 or increments them, including
    // -1 -> 0. Keep other negative values and signed overflow out of the policy.
    // Event nibble 391088/39108D admits 0..14; 15 means no update.
    static bool camera_mode_pair(unsigned a,unsigned b) noexcept {return a<=14 && b<=14;}
    static bool camera_mode_loaded(unsigned a,unsigned b,unsigned c) noexcept {
        return camera_mode_pair(a,b) && (c==0 || c==a);
    }
    static bool decision_counter_pair(unsigned a,unsigned b) noexcept {
        const auto valid=[](unsigned v){return v<=0x7FFFFFFFu || v==0xFFFFFFFFu;};
        return valid(a) && valid(b) && (a==b || b==0xFFFFFFFFu ||
            (a==0xFFFFFFFFu && b==0) || (a<0x7FFFFFFFu && b==a+1));
    }
    static bool decision_counter_loaded(unsigned a,unsigned b,unsigned c) noexcept {
        // Native decision helper 31AC60 resets +2A4/+2A8 to -1 when
        // the relevant player flag is absent. Both captured post-load failures
        // show this sentinel. Source adjacency and live identity are still
        // checked before restoring the exact saved counters below.
        return decision_counter_pair(a,b) && (c==a || c==b || c==0xFFFFFFFFu);
    }
    // Ghidra 31A450 resets +2AC to 0; 31A5E2 reads it as a two-player
    // table index and 31A601 stores selected player +23C. This is active
    // previous-selection state, not a frame counter or a pointer.
    // Camera virtual methods 31BB90/31BBA0/31BBD0/31BC10 assign
    // 0 (automatic), -1 or +1 (forced player ordering). Native init clears it.
    static bool forced_selection_value(unsigned v) noexcept {return v==0 || v==1 || v==0xFFFFFFFFu;}
    static bool forced_selection_pair(unsigned a,unsigned b) noexcept {return forced_selection_value(a) && forced_selection_value(b);}
    static bool forced_selection_loaded(unsigned a,unsigned b,unsigned c) noexcept {
        return forced_selection_pair(a,b) && (c==0 || c==a);
    }
    static bool player_selection_pair(unsigned a,unsigned b) noexcept {return a<=1 && b<=1;}
    static bool player_selection_loaded(unsigned a,unsigned b,unsigned c) noexcept {
        return player_selection_pair(a,b) && (c==0 || c==a);
    }
    struct ReadinessDiagnostic {unsigned reason{},record{},section{},offset{},side{};uint64_t saved{},next{};};
    bool adjacent_combat_ready(const CameraSnapshot& next,uintptr_t base,bool charge_rising=false,ReadinessDiagnostic* detail=nullptr,bool idle_camera=false,bool decision_counters=false,unsigned span=1)const noexcept {
        if(detail)*detail={};
        if(!span || span>3)return false;
        unsigned index=0,side=0;
        auto reject=[&](unsigned reason,unsigned section,unsigned offset,uint64_t a,uint64_t b){
            if(detail)*detail={reason,index,section,offset,side,a,b};return false;
        };
        if(!valid || !next.valid || !count || count>records.size() || count!=next.count)return reject(1,0,0,count,next.count);
        unsigned selected=0;
        for(unsigned i=0;i<count;++i){index=i;side=0;auto& a=records[i];auto& b=next.records[i];
            if(a.owner!=b.owner)return reject(2,0,0,a.owner,b.owner);
            if(a.smoothing_valid!=b.smoothing_valid)return reject(3,0,0,a.smoothing_valid,b.smoothing_valid);
            if(std::memcmp(a.bytes.data(),b.bytes.data(),8)){uint64_t av{},bv{};std::memcpy(&av,a.bytes.data(),8);std::memcpy(&bv,b.bytes.data(),8);return reject(4,0,0,av,bv);}
            if(!a.smoothing_valid)continue;
            uintptr_t vt{};std::memcpy(&vt,a.bytes.data(),8);
            if(vt!=base+0x3E87EB0 || ++selected!=1)return reject(5,0,0,vt,selected);
            for(auto* r:{&a,&b}){side=r==&a?0:1;
                for(unsigned j:{0u,4u,72u,76u}){auto v=word(r->smoothing.data()+j);
                    if((j<8 && v) || (j==72 && v>0x7FFFFFFFu) || (j==76 && v>=16))return reject(6,1,j,v,0);
                }
                for(unsigned j=8;j<72;j+=4)if((word(r->smoothing.data()+j)&0x7F800000u)==0x7F800000u)return reject(7,1,j,word(r->smoothing.data()+j),0);
                for(unsigned j:{4u,0u,8u}){auto v=word(r->response.data()+j);
                    if(j==4?v!=0x3EC00000u:v>0x3F800000u)return reject(8,2,j,v,j==4?0x3EC00000u:0x3F800000u);
                }
                if(!charge_rising)for(unsigned j:{0x68u,0x6Cu})if(word(r->targets.data()+j)!=(idle_camera?0u:0x3F800000u))return reject(9,3,j,word(r->targets.data()+j),idle_camera?0u:0x3F800000u);
            }
            side=0;auto old=word(a.smoothing.data()+72);
            if(word(b.smoothing.data()+72)!=(old>0x7FFFFFFFu-span?0x7FFFFFFFu:old+span))return reject(10,1,72,old,word(b.smoothing.data()+72));
            if(word(b.smoothing.data()+76)!=(word(a.smoothing.data()+76)+span)%16)return reject(11,1,76,word(a.smoothing.data()+76),word(b.smoothing.data()+76));
            if(charge_rising && decision_counters && !general_factors(word(a.targets.data()+0x68),word(a.targets.data()+0x6C),
                word(b.targets.data()+0x68),word(b.targets.data()+0x6C)))
                return reject(20,3,0x68,word(a.targets.data()+0x68),word(b.targets.data()+0x68));
            for(unsigned j=0;j<128;j+=4){
                bool position=(j>=0x48 && j<0x54)||(j>=0x58 && j<0x64);
                const auto av=word(a.targets.data()+j),bv=word(b.targets.data()+j);
                if(charge_rising && (j==0x68 || j==0x6C)){
                    if(!(decision_counters?general_factor(av,bv):replay_factor(av,bv)))return reject(12,3,j,av,bv);
                }else if(position){if((av&0x7F800000u)==0x7F800000u || (bv&0x7F800000u)==0x7F800000u)return reject(13,3,j,av,bv);}
                else if(decision_counters && j==112){if(!camera_mode_pair(av,bv))return reject(17,3,j,av,bv);}
                else if(decision_counters && j==124){if(!player_selection_pair(av,bv))return reject(18,3,j,av,bv);}
                else if(decision_counters && j==0){if(!forced_selection_pair(av,bv))return reject(19,3,j,av,bv);}
                else if(decision_counters && (j==116 || j==120)){if(!decision_counter_pair(av,bv))return reject(16,3,j,av,bv);}
                else if(av!=bv)return reject(14,3,j,av,bv);
            }
        }
        return selected==1?true:reject(15,0,0,selected,1);
    }
    // Reuse the existing strict idle restoration policy only when every
    // target is unchanged and the response/factor values match that policy.
    bool adjacent_idle_ready(const CameraSnapshot& next,uintptr_t base,unsigned span=1)const noexcept {
        if(!adjacent_combat_ready(next,base,false,nullptr,true,false,span))return false;
        for(unsigned i=0;i<count;++i){const auto& a=records[i];const auto& b=next.records[i];
            if(!a.smoothing_valid)continue;
            if(a.targets!=b.targets || a.response!=b.response ||
               word(a.response.data()) || word(a.response.data()+8))return false;
        }
        return true;
    }
    struct RestoreDiagnostic {unsigned failure_line{},record{},section{},offset{},a{},b{},c{},live{};};
    static bool restore_reject(RestoreDiagnostic* diagnostic,unsigned line) noexcept {
        if(diagnostic)diagnostic->failure_line=line;return false;
    }
    bool restore_smoothing(const CameraSnapshot& next,const CameraSnapshot& current,uintptr_t base,bool immediate_throw=false,bool adjacent_throw=false,bool validate_only=false,bool adjacent_charge=false,RestoreDiagnostic* diagnostic=nullptr,bool decision_counters=false,unsigned span=1)const noexcept {
        if(diagnostic)*diagnostic={};
        if(!span || span>3)return false;
        __try {
            if(immediate_throw && adjacent_throw)return restore_reject(diagnostic,__LINE__);
            if(adjacent_charge && (immediate_throw || !adjacent_throw || !adjacent_combat_ready(next,base,true,nullptr,false,decision_counters,span)))return restore_reject(diagnostic,__LINE__);
            if(!valid || !next.valid || !current.valid || !count || count>records.size() || count!=next.count || count!=current.count)return restore_reject(diagnostic,__LINE__);
            unsigned selected=count;
            for(unsigned i=0;i<count;++i){auto& a=records[i];auto& b=next.records[i];auto& c=current.records[i];
                if(a.owner!=b.owner || a.owner!=c.owner || std::memcmp(a.bytes.data(),b.bytes.data(),8) || std::memcmp(a.bytes.data(),c.bytes.data(),8))return restore_reject(diagnostic,__LINE__);
                if(diagnostic)diagnostic->record=i;
                if(!a.smoothing_valid)continue;
                uintptr_t vt{};std::memcpy(&vt,a.bytes.data(),8);
                if(selected!=count || vt!=base+0x3E87EB0 || !b.smoothing_valid || !c.smoothing_valid)return restore_reject(diagnostic,__LINE__);
                selected=i;
                if(immediate_throw && (a.bytes!=b.bytes || a.smoothing!=b.smoothing))return restore_reject(diagnostic,__LINE__);
                if(std::memcmp(c.bytes.data(),reinterpret_cast<void*>(a.owner),c.bytes.size()) ||
                   std::memcmp(c.smoothing.data(),reinterpret_cast<void*>(a.owner+0x1E0),8) ||
                   std::memcmp(c.smoothing.data()+8,reinterpret_cast<void*>(a.owner+0x25C),72))return restore_reject(diagnostic,__LINE__);
                for(unsigned j=0;j<a.bytes.size();++j){
                    if(diagnostic){diagnostic->section=1;diagnostic->offset=j;diagnostic->a=std::to_integer<unsigned>(a.bytes[j]);diagnostic->b=std::to_integer<unsigned>(b.bytes[j]);diagnostic->c=std::to_integer<unsigned>(c.bytes[j]);}
                    if((j<0x154 || j>=0x158) && a.bytes[j]!=c.bytes[j])return restore_reject(diagnostic,__LINE__);
                }
                if(word(c.bytes.data()+0x154)!=0)return restore_reject(diagnostic,__LINE__);
                for(unsigned j=0;j<8;j+=4){
                    if(diagnostic){diagnostic->section=8;diagnostic->offset=j;diagnostic->a=word(a.smoothing.data()+j);diagnostic->b=word(b.smoothing.data()+j);diagnostic->c=word(c.smoothing.data()+j);}
                    if(word(a.smoothing.data()+j)!=0 || word(b.smoothing.data()+j)!=0 || word(c.smoothing.data()+j)!=1)return restore_reject(diagnostic,__LINE__);
                }
                for(unsigned j=8;j<80;++j){
                    if(diagnostic){diagnostic->section=9;diagnostic->offset=j;diagnostic->a=std::to_integer<unsigned>(a.smoothing[j]);diagnostic->b=std::to_integer<unsigned>(b.smoothing[j]);diagnostic->c=std::to_integer<unsigned>(c.smoothing[j]);}
                    if(c.smoothing[j]!=std::byte{})return restore_reject(diagnostic,__LINE__);
                }
                for(auto* source:{&a,&b}){
                    if((word(source->bytes.data()+0x154)&0x7F800000u)==0x7F800000u)return restore_reject(diagnostic,__LINE__);
                    for(unsigned j=8;j<72;j+=4)if((word(source->smoothing.data()+j)&0x7F800000u)==0x7F800000u)return restore_reject(diagnostic,__LINE__);
                    if(word(source->smoothing.data()+72)>0x7FFFFFFFu || word(source->smoothing.data()+76)>=16)return restore_reject(diagnostic,__LINE__);
                }
                // Idle-only target correction: stable A/B, observed load reset, live identity.
                for(unsigned j=0;!adjacent_throw && j<128;j+=4){
                    const auto av=word(a.targets.data()+j),bv=word(b.targets.data()+j);
                    if(av!=bv && !(decision_counters && ((j==112 && camera_mode_pair(av,bv)) || ((j==116 || j==120) && decision_counter_pair(av,bv)) || (j==124 && player_selection_pair(av,bv)) || (j==0 && forced_selection_pair(av,bv))))){
                        if(diagnostic){diagnostic->section=4;diagnostic->offset=j;diagnostic->a=av;diagnostic->b=bv;diagnostic->c=word(c.targets.data()+j);diagnostic->live=0;}
                        return restore_reject(diagnostic,__LINE__);
                    }
                }
                for(unsigned j=0;j<128;j+=4){
                    const auto address=a.owner+(j<116?0x1E8+j:0x2A4+j-116);
                    const auto cv=word(c.targets.data()+j),lv=word(reinterpret_cast<const std::byte*>(address));
                    if(cv!=lv){
                        if(diagnostic){diagnostic->section=5;diagnostic->offset=j;diagnostic->a=word(a.targets.data()+j);diagnostic->b=word(b.targets.data()+j);diagnostic->c=cv;diagnostic->live=lv;}
                        return restore_reject(diagnostic,__LINE__);
                    }
                }
                for(unsigned j=0;j<128;j+=4){
                    const bool position=(j>=0x48 && j<0x54)||(j>=0x58 && j<0x64);
                    const bool factor=j==0x68 || j==0x6C;
                    auto av=word(a.targets.data()+j),bv=word(b.targets.data()+j),cv=word(c.targets.data()+j);
                    if(diagnostic){diagnostic->section=2;diagnostic->offset=j;diagnostic->a=av;diagnostic->b=bv;diagnostic->c=cv;}
                    if(decision_counters && j==0){
                        if(!forced_selection_loaded(av,bv,cv))return restore_reject(diagnostic,__LINE__);
                        continue;
                    }
                    if(decision_counters && j==124){
                        if(!player_selection_loaded(av,bv,cv))return restore_reject(diagnostic,__LINE__);
                        continue;
                    }
                    if(decision_counters && j==112){
                        if(!camera_mode_loaded(av,bv,cv))return restore_reject(diagnostic,__LINE__);
                        continue;
                    }
                    if(decision_counters && (j==116 || j==120)){
                        if(!decision_counter_loaded(av,bv,cv))return restore_reject(diagnostic,__LINE__);
                        continue;
                    }
                    if(adjacent_throw && !position && !(adjacent_charge && factor) && av!=bv)return restore_reject(diagnostic,__LINE__);
                    if(position){
                        if((av&0x7F800000u)==0x7F800000u || (bv&0x7F800000u)==0x7F800000u || cv!=0)return restore_reject(diagnostic,__LINE__);
                    }else if(factor){
                        if((adjacent_charge?!(decision_counters?general_factor(av,bv):replay_factor(av,bv)):av!=((immediate_throw || adjacent_throw)?0x3F800000u:0u)) || cv!=0x3F800000u)return restore_reject(diagnostic,__LINE__);
                    }else if(j==0 && !immediate_throw && !adjacent_throw && av==0xFFFFFFFFu && bv==av && cv==0){
                        // Pinned camera selector:31A588/31AB88 interpret -1.
                        // Observed native load resets it; restore only this exact idle pattern.
                    }else if(av!=cv)return restore_reject(diagnostic,__LINE__);
                }
                for(unsigned j=0;!adjacent_throw && j<12;j+=4){
                    const auto av=word(a.response.data()+j),bv=word(b.response.data()+j);
                    if(av!=bv){
                        if(diagnostic){diagnostic->section=6;diagnostic->offset=j;diagnostic->a=av;diagnostic->b=bv;diagnostic->c=word(c.response.data()+j);diagnostic->live=0;}
                        return restore_reject(diagnostic,__LINE__);
                    }
                }
                for(unsigned j=0;j<12;j+=4){
                    const auto cv=word(c.response.data()+j),lv=word(reinterpret_cast<const std::byte*>(a.owner+0x1D0+j));
                    if(cv!=lv){
                        if(diagnostic){diagnostic->section=7;diagnostic->offset=j;diagnostic->a=word(a.response.data()+j);diagnostic->b=word(b.response.data()+j);diagnostic->c=cv;diagnostic->live=lv;}
                        return restore_reject(diagnostic,__LINE__);
                    }
                }
                for(unsigned j=0;j<12;j+=4){
                    auto av=word(a.response.data()+j);
                    if(diagnostic){diagnostic->section=3;diagnostic->offset=j;diagnostic->a=av;diagnostic->b=word(b.response.data()+j);diagnostic->c=word(c.response.data()+j);}
                    if(word(c.response.data()+j)!=0x3EC00000u)return restore_reject(diagnostic,__LINE__);
                    if(j==4){if(av!=0x3EC00000u || word(b.response.data()+j)!=0x3EC00000u)return restore_reject(diagnostic,__LINE__);}
                    else if(immediate_throw || adjacent_throw){if(av>0x3F800000u || word(b.response.data()+j)>0x3F800000u)return restore_reject(diagnostic,__LINE__);} // finite [0,1], positive bits
                    else if(av!=0)return restore_reject(diagnostic,__LINE__);
                }
                auto old_count=word(a.smoothing.data()+72);
                if(!immediate_throw)for(unsigned j:{72u,76u}){
                    if(diagnostic){diagnostic->section=10;diagnostic->offset=j;diagnostic->a=word(a.smoothing.data()+j);diagnostic->b=word(b.smoothing.data()+j);diagnostic->c=word(c.smoothing.data()+j);}
                    const auto expected=j==72?(old_count>0x7FFFFFFFu-span?0x7FFFFFFFu:old_count+span):(word(a.smoothing.data()+76)+span)%16;
                    if(word(b.smoothing.data()+j)!=expected)return restore_reject(diagnostic,__LINE__);
                }
            }
            if(selected==count)return restore_reject(diagnostic,__LINE__);
            if(validate_only)return true;
            auto& a=records[selected];
            // All guards above passed; preserve the captured selector as well.
            if(decision_counters){
                std::memcpy(reinterpret_cast<void*>(a.owner+0x2A4),a.targets.data()+116,8);
                std::memcpy(reinterpret_cast<void*>(a.owner+0x2AC),a.targets.data()+124,4);
                std::memcpy(reinterpret_cast<void*>(a.owner+0x258),a.targets.data()+112,4);
            }
            std::memcpy(reinterpret_cast<void*>(a.owner+0x1E8),a.targets.data(),4);
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
        }__except(EXCEPTION_EXECUTE_HANDLER){return restore_reject(diagnostic,__LINE__);}
    }
    // Align only supplemental camera observations; serialized streams are never
    // rewritten. Missing/retyped pool cameras use target identity for the existing
    // non-smoothing post-load guards. Main smoothing camera must remain present.
    bool align_next(const CameraSnapshot& next,const CameraSlots& a,const CameraSlots& b,CameraSnapshot& out)const {
        if(!valid || !next.valid || count>records.size() || next.count>next.records.size() || !a.compatible(b))return false;
        out=*this;
        for(unsigned i=0;i<count;++i){const auto& r=records[i];bool found=false;
            for(unsigned j=0;j<next.count;++j){const auto& n=next.records[j];
                if(r.owner==n.owner && !std::memcmp(r.bytes.data(),n.bytes.data(),8)){out.records[i]=n;found=true;break;}}
            if(!found && (r.smoothing_valid || !a.pool_owner(r.owner)))return false;
        }
        for(unsigned j=0;j<next.count;++j){bool found=false;for(unsigned i=0;i<count;++i)if(next.records[j].owner==records[i].owner)found=true;
            if(!found && (next.records[j].smoothing_valid || !b.pool_owner(next.records[j].owner)))return false;}
        return true;
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
