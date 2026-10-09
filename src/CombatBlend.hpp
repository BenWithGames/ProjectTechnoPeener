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
    static bool adjacent(const Bytes& a,const Bytes& b,bool throw_p1=false) noexcept {
        for(unsigned i=0;i<35;++i){
            auto x=word(a.data()+i*4),y=word(b.data()+i*4);
            auto known=i<24?0x3F59999Au:0x3F400000u;
            if((x && x!=known) || (y && y!=known))return false;
        }
        for(unsigned j=0;j<7;++j){auto x=a.data()+140+j*176;auto y=b.data()+140+j*176;
            bool quartet=throw_p1 && j==0 && quartet_transition(x,y);
            // Keep every other weight and all pointers stable.
            for(unsigned k=0;k<176;++k)
                if(!(k>=16 && k<20) && !(k>=24 && k<32) &&
                   !(quartet && quartet_byte(k)) && x[k]!=y[k])return false;
            if((wide(x+24)>>35) || (wide(y+24)>>35))return false;
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
    static bool ready(const std::array<Bytes,2>& a,const std::array<Bytes,2>& b,
                      const std::array<Bytes,2>& loaded,const uintptr_t* owners,bool throw_p1=false) noexcept {
        __try {
            for(unsigned side=0;side<2;++side){
                auto owner=owners[side];
                if(owner<0x10000 || owner>UINTPTR_MAX-0x22B0 || !adjacent(a[side],b[side],throw_p1 && side==0))return false;
                if(side && owner<owners[0]+0x22B0 && owners[0]<owner+0x22B0)return false;
                auto c=loaded[side].data();
                if(std::memcmp(c,reinterpret_cast<void*>(owner+0x4BC),140) ||
                   std::memcmp(c+140,reinterpret_cast<void*>(owner+0x1DD8),1232) ||
                   std::memcmp(c+1372,reinterpret_cast<void*>(owner+0x22A8),8))return false;
                for(unsigned i=0;i<35;++i)if(word(c+i*4))return false;
                for(unsigned j=0;j<7;++j){auto x=a[side].data()+140+j*176;auto y=b[side].data()+140+j*176;auto z=c+140+j*176;
                    bool quartet=throw_p1 && side==0 && j==0 && quartet_transition(x,y);
                    for(unsigned k=0;k<176;++k)
                        if(!(k>=16 && k<20) && !(k>=24 && k<32) &&
                           z[k]!=(quartet && quartet_byte(k)?y[k]:x[k]))return false;
                    auto index=word(x+16),current=word(z+16);auto mask=wide(x+24),newmask=wide(z+24);
                    bool throw_selection=side==1 && j==2 && index==12 && current==3 && !mask && !newmask;
                    if((current && current!=index && !throw_selection) || (newmask && newmask!=mask))return false;
                    auto table=wide(x+8);
                    if(table){
                        if(table<0x10000 || table>UINTPTR_MAX-4)return false;
                        auto count=*reinterpret_cast<const int*>(table);
                        if(count<=0 || index>=unsigned(count) || current>=unsigned(count) || word(y+16)>=unsigned(count))return false;
                    }else if(index || current || word(y+16) || mask || newmask || wide(y+24))return false;
                }
            }
            return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool restore(const std::array<Bytes,2>& a,const std::array<Bytes,2>& b,
                        const std::array<Bytes,2>& loaded,const uintptr_t* owners,bool throw_p1=false) noexcept {
        __try {
            if(!ready(a,b,loaded,owners,throw_p1))return false;
            for(unsigned side=0;side<2;++side){
                std::memcpy(reinterpret_cast<void*>(owners[side]+0x4BC),a[side].data(),140);
                for(unsigned j=0;j<7;++j){auto x=a[side].data()+140+j*176;auto target=owners[side]+0x1DD8+j*176;
                    std::memcpy(reinterpret_cast<void*>(target+16),x+16,4);
                    std::memcpy(reinterpret_cast<void*>(target+24),x+24,8);
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
