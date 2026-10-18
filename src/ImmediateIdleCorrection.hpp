#pragma once
#include "FullStateProbe.hpp"
namespace sc6::research {
// Idle boundary completion only. Build and validate the entire patch list before
// writing; reject unknown differences, changed identities, and stale live data.
struct ImmediateIdleCorrection {
    struct Patch { uintptr_t address; const std::byte* bytes; unsigned size; };
    std::array<Patch,8192> patches{};unsigned count{};bool verified{};
    bool add(uintptr_t address,const std::byte* bytes,unsigned size){
        if(!size || size>64 || address<0x10000 || address>UINTPTR_MAX-size || count>=patches.size())return false;
        for(unsigned i=0;i<count;++i){const auto& p=patches[i];
            if(address<p.address+p.size && p.address<address+size){
                return address==p.address && size==p.size && !std::memcmp(bytes,p.bytes,size);
            }
        }
        patches[count++]={address,bytes,size};return true;
    }
    static bool finite(const std::byte* p,unsigned n){
        for(unsigned i=0;i<n;i+=4){unsigned v{};std::memcpy(&v,p+i,4);if((v&0x7F800000u)==0x7F800000u)return false;}return true;
    }
    unsigned failure_line{},section{},record{},kind{},difference_offset{UINT32_MAX},saved_word{},live_word{};
    bool reject(unsigned line) noexcept {failure_line=line;return false;}
    void difference(const std::byte* a,const std::byte* b,unsigned size) noexcept {
        difference_offset=UINT32_MAX;saved_word=live_word=0;
        for(unsigned j=0;j<size;++j)if(a[j]!=b[j]){difference_offset=j;const auto n=(size-j<4?size-j:4);std::memcpy(&saved_word,a+j,n);std::memcpy(&live_word,b+j,n);break;}
    }
    bool apply(const FullStateProbe& a,const FullStateProbe& b,const FullStateProbe& c,uintptr_t base) noexcept {
        count=0;verified=false;failure_line=0;section=1;record=kind=0;difference_offset=UINT32_MAX;saved_word=live_word=0;
        __try {
            const auto& x=a.transforms[0];const auto& y=b.transforms[0];const auto& z=c.transforms[3];
            if(!x.same_topology(y) || !x.same_topology(z) || x.count>x.records.size())return reject(__LINE__);
            for(unsigned i=0;i<x.count;++i){const auto& r=x.records[i];const auto& next=y.records[i];const auto& live=z.records[i];
                section=2;record=i;kind=r.kind;
                if(r.size<=512)difference(r.bytes.data(),live.bytes.data(),r.size);
                if(r.size>512 || std::memcmp(reinterpret_cast<void*>(r.address),live.bytes.data(),r.size))return reject(__LINE__);
                if(!std::memcmp(r.bytes.data(),live.bytes.data(),r.size))continue;
                if(r.side>1)return reject(__LINE__);
                unsigned offset=0,length=0;
                if(r.kind==0 && r.size==512){offset=0x1F0;length=16;}
                else if(r.kind==1 && r.size==88 && r.parent<x.count && x.records[r.parent].kind==0 && x.records[r.parent].side==r.side){length=64;}
                else if((r.kind==18 || r.kind==19) && r.size==64 && r.parent<x.count){
                    const auto& link=x.records[r.parent];
                    if(link.kind!=16 || link.size!=8 || link.side!=r.side)return reject(__LINE__);
                    auto target=TransformSnapshot::pointer(link.bytes.data());
                    if(target<0x10000 || target>UINTPTR_MAX-0xC0 || r.address!=target+(r.kind==18?0x80:0x40) ||
                       *reinterpret_cast<uintptr_t*>(target)!=base+0x3E88E38 ||
                       std::memcmp(link.bytes.data(),y.records[r.parent].bytes.data(),8) ||
                       std::memcmp(link.bytes.data(),reinterpret_cast<void*>(link.address),8))return reject(__LINE__);
                    length=64;
                }else return reject(__LINE__);
                if(std::memcmp(r.bytes.data(),live.bytes.data(),offset) ||
                   std::memcmp(r.bytes.data()+offset+length,live.bytes.data()+offset+length,r.size-offset-length) ||
                   !finite(r.bytes.data()+offset,length) || !finite(live.bytes.data()+offset,length))return reject(__LINE__);
                // Native loading may already restore some float words while
                // leaving others at the later captured values. Accept only
                // those two exact provenances, never a third unexplained value.
                for(unsigned k=offset;k<offset+length;k+=4)
                    if(std::memcmp(live.bytes.data()+k,r.bytes.data()+k,4) &&
                       std::memcmp(live.bytes.data()+k,next.bytes.data()+k,4))return reject(__LINE__);
                if(!add(r.address+offset,r.bytes.data()+offset,length))return reject(__LINE__);
            }
            section=3;
            if(!a.rings[0].same(c.restored_rings) || !b.rings[0].rotated_from(a.rings[0]))return reject(__LINE__);
            section=4;
            for(unsigned side=0;side<2;++side){
                record=side;
                if(!a.blends[0].valid || !b.blends[0].valid || !c.blends[3].valid)return reject(__LINE__);
                const auto owner=a.blends[0].owners[side];
                if(owner!=b.blends[0].owners[side] || owner!=c.blends[3].owners[side])return reject(__LINE__);
                const auto& av=a.blends[0].bytes[side];const auto& bv=b.blends[0].bytes[side];const auto& cv=c.blends[3].bytes[side];
                if(std::memcmp(av.data(),cv.data(),1372) || std::memcmp(reinterpret_cast<void*>(owner+0x22A8),cv.data()+1372,8))return reject(__LINE__);
                if(std::memcmp(av.data()+1372,cv.data()+1372,8)){
                    // Observed history aliases select the prior matrix ring slot.
                    if(TransformSnapshot::pointer(av.data()+1372)!=a.rings[0].headers[side*2].previous ||
                       TransformSnapshot::pointer(bv.data()+1372)!=b.rings[0].headers[side*2].previous ||
                       std::memcmp(bv.data()+1372,cv.data()+1372,8) || !add(owner+0x22A8,av.data()+1372,8))return reject(__LINE__);
                }
            }
            section=5;
            const auto& ca=a.cameras[0];const auto& cb=b.cameras[0];const auto& cc=c.cameras[4];
            if(!ca.valid || !cb.valid || !cc.valid || ca.count>ca.records.size() || ca.count!=cb.count || ca.count!=cc.count)return reject(__LINE__);
            for(unsigned i=0;i<ca.count;++i){const auto& r=ca.records[i];const auto& next=cb.records[i];const auto& live=cc.records[i];
                record=i;difference(r.bytes.data(),live.bytes.data(),static_cast<unsigned>(r.bytes.size()));
                if(r.owner!=next.owner || r.owner!=live.owner || r.smoothing_valid!=live.smoothing_valid ||
                   std::memcmp(reinterpret_cast<void*>(r.owner),live.bytes.data(),r.bytes.size()))return reject(__LINE__);
                if(r.smoothing_valid && (r.smoothing!=live.smoothing || r.targets!=live.targets || r.response!=live.response))return reject(__LINE__);
                if(r.bytes==live.bytes)continue;
                // 33FF40/340F70 omit these active limits. 33E640/3402C0
                // initialize the exact defaults below; 340C00 uses the limits.
                // Restore the archived scalars, never exclude them from equality.
                if(!r.smoothing_valid && !next.smoothing_valid &&
                   TransformSnapshot::pointer(r.bytes.data())==base+0x3E885E0 &&
                   TransformSnapshot::pointer(next.bytes.data())==base+0x3E885E0 &&
                   TransformSnapshot::pointer(live.bytes.data())==base+0x3E885E0){
                    constexpr unsigned offsets[]={0x188,0x18C,0x1B8,0x1BC};
                    constexpr unsigned defaults[]={0xBE7FFFBC,0x3E7FFD60,0x3F000000,0x437A0000};
                    for(unsigned j=0;j<r.bytes.size();++j){
                        bool limit=false;for(auto off:offsets)if(j>=off && j<off+4)limit=true;
                        if(!limit && r.bytes[j]!=live.bytes[j])return reject(__LINE__);
                    }
                    for(unsigned j=0;j<4;++j){const auto off=offsets[j];
                        if(!std::memcmp(r.bytes.data()+off,live.bytes.data()+off,4))continue;
                        if(!finite(r.bytes.data()+off,4) || CameraSnapshot::word(live.bytes.data()+off)!=defaults[j] ||
                           !add(r.owner+off,r.bytes.data()+off,4))return reject(__LINE__);
                    }
                    continue;
                }
                // Separate observed base-camera object; smoothing camera has
                // already been corrected by its existing dedicated restorer.
                // Later-frame motion is irrelevant: copy only the saved finite
                // reset scalars after all other saved/loaded bytes match exactly.
                if(r.smoothing_valid || next.smoothing_valid || TransformSnapshot::pointer(r.bytes.data())!=base+0x3E881C8 ||
                   TransformSnapshot::pointer(next.bytes.data())!=base+0x3E881C8 || CameraSnapshot::word(live.bytes.data()+0x154)!=0 ||
                   !finite(r.bytes.data()+0x144,4) || !finite(r.bytes.data()+0x154,4) ||
                   std::memcmp(r.bytes.data(),live.bytes.data(),0x144) ||
                   std::memcmp(r.bytes.data()+0x148,live.bytes.data()+0x148,0xC) ||
                   std::memcmp(r.bytes.data()+0x158,live.bytes.data()+0x158,r.bytes.size()-0x158) ||
                   !add(r.owner+0x154,r.bytes.data()+0x154,4))return reject(__LINE__);
                // Trust the archived finite smoothing offset, not a guessed range.
                // Native reset may clear it; arbitrary loaded changes remain rejected.
                if(std::memcmp(r.bytes.data()+0x144,live.bytes.data()+0x144,4) &&
                   (CameraSnapshot::word(live.bytes.data()+0x144)!=0 ||
                    !add(r.owner+0x144,r.bytes.data()+0x144,4)))return reject(__LINE__);
            }
            section=6;
            for(unsigned i=0;i<count;++i){const auto& p=patches[i];std::memcpy(reinterpret_cast<void*>(p.address),p.bytes,p.size);}
            for(unsigned i=0;i<count;++i){const auto& p=patches[i];if(std::memcmp(reinterpret_cast<void*>(p.address),p.bytes,p.size))return reject(__LINE__);}
            verified=true;return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){return reject(__LINE__);}
    }
};
}
