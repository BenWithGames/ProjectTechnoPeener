#pragma once
#include <array>
#include <cstdint>
#include <cstring>
namespace sc6::research {
// Separate from idle preservation: validates adjacent combat states before load.
struct CombatBlend {
    using Bytes=std::array<std::byte,1380>;
    static unsigned word(const std::byte* p){unsigned v{};std::memcpy(&v,p,4);return v;}
    static std::uint64_t wide(const std::byte* p){std::uint64_t v{};std::memcpy(&v,p,8);return v;}
    static bool quartet_byte(unsigned k) noexcept {
        return (k>=0x3C && k<0x44) || (k>=0x6C && k<0x74);
    }
    // 2F90D0 writes these four block0 weights together. The zero-weight
    // reset at 2F7570 clears mask bits without overwriting their floats.
    static bool quartet_transition(const std::byte* a,const std::byte* b) noexcept {
        auto old=word(a+0x3C);
        // Positive IEEE floats below one; rejects NaNs, negatives and infinity.
        if(!old || old>=0x3F800000u)return false;
        for(unsigned off:{0x3Cu,0x40u,0x6Cu,0x70u})
            if(word(a+off)!=old || word(b+off)!=0x3F800000u)return false;
        return true;
    }
    struct Diagnostic {unsigned reason{},offset{UINT32_MAX},saved{},next{};};
    static bool adjacent(const Bytes& a,const Bytes& b,bool throw_p1=false,Diagnostic* diagnostic=nullptr,bool general=false) noexcept {
        if(diagnostic)*diagnostic={};
        const auto reject=[&](unsigned reason,unsigned offset,unsigned saved,unsigned next){if(diagnostic)*diagnostic={reason,offset,saved,next};return false;};
        for(unsigned i=0;i<35;++i){
            auto x=word(a.data()+i*4),y=word(b.data()+i*4);
            auto known=i<24?0x3F59999Au:0x3F400000u;
            if(general?((x&0x7F800000u)==0x7F800000u || (y&0x7F800000u)==0x7F800000u):((x && x!=known) || (y && y!=known)))return reject(5,i*4,x,y);
        }
        for(unsigned j=0;j<7;++j){auto x=a.data()+140+j*176;auto y=b.data()+140+j*176;
            bool quartet=throw_p1 && j==0 && quartet_transition(x,y);
            // Keep every other weight and all pointers stable.
            for(unsigned k=0;k<176;++k)
                if(!(k>=16 && k<20) && !(k>=24 && k<32) &&
                   !(quartet && quartet_byte(k)) && !(general && k>=32 && k<172) && x[k]!=y[k])return reject(6,140+j*176+k,std::to_integer<unsigned>(x[k]),std::to_integer<unsigned>(y[k]));
            if(general)for(unsigned k=32;k<172;k+=4)
                if((word(x+k)&0x7F800000u)==0x7F800000u || (word(y+k)&0x7F800000u)==0x7F800000u)return reject(9,140+j*176+k,word(x+k),word(y+k));
            if((wide(x+24)>>35) || (wide(y+24)>>35))return reject(7,140+j*176+28,word(x+28),word(y+28));
        }
        // The rotating history pointer is handled by the ring restorer, never copied.
        return true;
    }
    // Read-only charge readiness. Never used by ready()/restore().
    static bool charge_adjacent(const Bytes& a,const Bytes& b,const std::byte* wrapper) noexcept {
        if(adjacent(a,b))return true;
        if(!wrapper || word(wrapper+0x34) || word(wrapper+0x38) ||
           word(wrapper+0x54)!=0x3F800000u)return false;
        auto parameter=word(wrapper+0x20);
        if(!parameter || parameter>=0x3F800000u)return false;
        float x{};std::memcpy(&x,&parameter,4);float complement=1.0f-x;
        unsigned complement_bits{};std::memcpy(&complement_bits,&complement,4);
        // Require the captured old quartets to be complementary too. The old
        // parameter is not the current wrapper value (weights lag one update).
        unsigned old_x=word(a.data()+140+2*176+0x3C);
        if(!old_x || old_x>=0x3F800000u)return false;
        float old{};std::memcpy(&old,&old_x,4);old=1.0f-old;
        unsigned old_complement{};std::memcpy(&old_complement,&old,4);
        Bytes checked=b;
        constexpr std::uint64_t mask=(1ull<<7)|(1ull<<8)|(1ull<<19)|(1ull<<20);
        for(unsigned block:{0u,2u}){
            auto start=140+block*176;
            if((wide(a.data()+start+24)&mask)!=mask || (wide(b.data()+start+24)&mask)!=mask)return false;
            for(unsigned off:{0x3Cu,0x40u,0x6Cu,0x70u}){
                if(word(a.data()+start+off)!=(block?old_x:old_complement) ||
                   word(b.data()+start+off)!=(block?parameter:complement_bits))return false;
                std::memcpy(checked.data()+start+off,a.data()+start+off,4);
            }
        }
        // All other bytes still use the original strict adjacency checks.
        return adjacent(a,checked);
    }
    struct LoadedDiagnostic {unsigned reason{},side{},block{},offset{};uint64_t saved{},next{},loaded{};unsigned pointer_valid{};uint64_t pointer_saved{},pointer_next{},pointer_loaded{};};
    static bool fail_loaded(LoadedDiagnostic* d,unsigned reason,unsigned side,unsigned block,unsigned offset,
                            uint64_t saved=0,uint64_t next=0,uint64_t loaded=0) noexcept {
        if(d)*d={reason,side,block,offset,saved,next,loaded};return false;
    }
    static bool fail_block(LoadedDiagnostic* d,unsigned side,unsigned block,unsigned k,
                           const std::byte* x,const std::byte* y,const std::byte* z) noexcept {
        fail_loaded(d,5,side,block,k,std::to_integer<unsigned>(x[k]),std::to_integer<unsigned>(y[k]),std::to_integer<unsigned>(z[k]));
        if(d && k<16){const auto start=k<8?0:8;d->pointer_valid=1;
            d->pointer_saved=wide(x+start);d->pointer_next=wide(y+start);d->pointer_loaded=wide(z+start);}
        return false;
    }
    // Native 2F8FE0..2F8FE3 assigns the wrapper table without ownership changes.
    // Only the observed P2 block5 null/inactive reset is reversible here.
    static bool rebuilt_inactive(unsigned side,unsigned block,const std::byte* a,const std::byte* b,
                                 const std::byte* c,const uintptr_t* verified_tables) noexcept {
        return side==1 && block==5 && verified_tables && verified_tables[side]>=0x10000 &&
            !wide(a+8) && !wide(b+8) && wide(c+8)==verified_tables[side] &&
            !word(a+16) && !word(b+16) && !word(c+16) &&
            !word(a+20) && !word(b+20) && !word(c+20) &&
            !wide(a+24) && !wide(b+24) && !wide(c+24);
    }
    static bool ready(const std::array<Bytes,2>& a,const std::array<Bytes,2>& b,
                      const std::array<Bytes,2>& loaded,const uintptr_t* owners,bool throw_p1=false,bool general=false,LoadedDiagnostic* diagnostic=nullptr,const uintptr_t* verified_tables=nullptr,const uintptr_t* verified_resources=nullptr) noexcept {
        if(diagnostic)*diagnostic={};
        __try {
            for(unsigned side=0;side<2;++side){
                auto owner=owners[side];
                if(owner<0x10000 || owner>UINTPTR_MAX-0x22B0 || !adjacent(a[side],b[side],throw_p1 && side==0,nullptr,general))return fail_loaded(diagnostic,1,side,0,0,owner);
                if(side && owner<owners[0]+0x22B0 && owners[0]<owner+0x22B0)return fail_loaded(diagnostic,2,side,0,0);
                auto c=loaded[side].data();
                for(unsigned k=0;k<1380;++k){
                    auto address=k<140?owner+0x4BC+k:k<1372?owner+0x1DD8+k-140:owner+0x22A8+k-1372;
                    const auto live=*reinterpret_cast<const unsigned char*>(address);
                    if(live!=std::to_integer<unsigned>(c[k]))return fail_loaded(diagnostic,3,side,0,k,std::to_integer<unsigned>(c[k]),0,live);
                }
                for(unsigned i=0;i<35;++i)if(word(c+i*4))return fail_loaded(diagnostic,4,side,0,i*4,word(a[side].data()+i*4),word(b[side].data()+i*4),word(c+i*4));
                for(unsigned j=0;j<7;++j){auto x=a[side].data()+140+j*176;auto y=b[side].data()+140+j*176;auto z=c+140+j*176;
                    bool quartet=throw_p1 && side==0 && j==0 && quartet_transition(x,y);
                    const bool rebuilt=general && rebuilt_inactive(side,j,x,y,z,verified_tables);
                    for(unsigned k=0;k<176;++k)
                        if(!(k>=16 && k<20) && !(k>=24 && k<32) &&
                           !(general && k>=32 && k<172) && !(rebuilt && k>=8 && k<16) && z[k]!=(quartet && quartet_byte(k)?y[k]:x[k]))return fail_block(diagnostic,side,j,k,x,y,z);
                    if(general)for(unsigned k=32;k<172;k+=4)
                        if(word(z+k)!=word(x+k) && word(z+k)!=word(y+k))return fail_loaded(diagnostic,6,side,j,k,word(x+k),word(y+k),word(z+k));
                    auto index=word(x+16),current=word(z+16);auto mask=wide(x+24),newmask=wide(z+24);
                    bool throw_selection=!general && side==1 && j==2 && index==12 && current==3 && !mask && !newmask;
                    // 2F8FA0 calls 2FA080(mode1): resource byte3 selects block2.
                    // The caller verifies archived and live wrapper identities first.
                    bool reset_selection=false;
                    if(general && j==2 && !newmask && verified_tables && verified_resources &&
                       verified_tables[side]>=0x10000 && wide(x+8)==verified_tables[side] &&
                       verified_resources[side]>=0x10000 && verified_resources[side]<=UINTPTR_MAX-3){
                        const auto selected=*reinterpret_cast<const unsigned char*>(verified_resources[side]+3)&127u;
                        reset_selection=current==selected;
                    }
                    if(current && current!=index && !throw_selection && !reset_selection)return fail_loaded(diagnostic,7,side,j,16,index,word(y+16),current);
                    if(newmask && newmask!=mask)return fail_loaded(diagnostic,8,side,j,24,mask,wide(y+24),newmask);
                    auto table=wide(x+8);
                    if(table){
                        if(table<0x10000 || table>UINTPTR_MAX-4)return fail_loaded(diagnostic,9,side,j,8,table);
                        auto count=*reinterpret_cast<const int*>(table);
                        if(count<=0 || index>=unsigned(count) || current>=unsigned(count) || word(y+16)>=unsigned(count))return fail_loaded(diagnostic,10,side,j,16,index,word(y+16),current);
                    }else if(index || current || word(y+16) || mask || newmask || wide(y+24))return fail_loaded(diagnostic,11,side,j,16,index,word(y+16),current);
                }
            }
            return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){return fail_loaded(diagnostic,12,0,0,0);}
    }
    static bool restore(const std::array<Bytes,2>& a,const std::array<Bytes,2>& b,
                        const std::array<Bytes,2>& loaded,const uintptr_t* owners,bool throw_p1=false,bool general=false,const uintptr_t* verified_tables=nullptr,const uintptr_t* verified_resources=nullptr) noexcept {
        __try {
            if(!ready(a,b,loaded,owners,throw_p1,general,nullptr,verified_tables,verified_resources))return false;
            for(unsigned side=0;side<2;++side){
                std::memcpy(reinterpret_cast<void*>(owners[side]+0x4BC),a[side].data(),140);
                for(unsigned j=0;j<7;++j){auto x=a[side].data()+140+j*176;auto target=owners[side]+0x1DD8+j*176;
                    if(general && rebuilt_inactive(side,j,x,b[side].data()+140+j*176,loaded[side].data()+140+j*176,verified_tables))
                        std::memcpy(reinterpret_cast<void*>(target+8),x+8,8);
                    std::memcpy(reinterpret_cast<void*>(target+16),x+16,4);
                    std::memcpy(reinterpret_cast<void*>(target+24),x+24,8);
                    if(general)std::memcpy(reinterpret_cast<void*>(target+32),x+32,140);
                    if(throw_p1 && side==0 && j==0 && quartet_transition(x,b[side].data()+140))
                        for(unsigned off:{0x3Cu,0x40u,0x6Cu,0x70u})
                            std::memcpy(reinterpret_cast<void*>(target+off),x+off,4);
                }
            }
            for(unsigned side=0;side<2;++side)
                if(std::memcmp(reinterpret_cast<void*>(owners[side]+0x4BC),a[side].data(),140) ||
                   std::memcmp(reinterpret_cast<void*>(owners[side]+0x1DD8),a[side].data()+140,1232) ||
                   std::memcmp(reinterpret_cast<void*>(owners[side]+0x22A8),loaded[side].data()+1372,8))return false;
            return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
};
}
