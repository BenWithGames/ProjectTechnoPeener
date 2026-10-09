#pragma once
#include "SaveProbe.hpp"
namespace sc6::research {
// Narrow immediate round-trip only. No file loading, delay, player loader, or callbacks.
struct SharedRestoreProbe {
    SaveStream stream;
    struct Range {uintptr_t base; std::uint64_t length;};
    struct Ranges {Range* begin; Range* end; Range* capacity;};
    using Routine=void* (*)(void*,SaveStream*,Ranges*);
    static constexpr unsigned sizes[]={0x58,0x140,0x50,0xEF0,8,16,16,16,16};
    static constexpr unsigned offsets[]={0x48462D0,0x4846330,0x48545A8,0x48545F8,0x48545A0};
    static constexpr unsigned byte_count=0x1120, raw_count=0x10E0;
    static constexpr DWORD rejection=0xE053C601;
    std::array<std::byte,raw_count> before{},after{};
    uintptr_t pointers_before[4]{},pointers_after[4]{};
    unsigned reads{}; bool attempted{},saved{},restore_invoked{},restored{},equal{},fault{};
    bool read_mode{}; DWORD exception{}; LONGLONG elapsed_qpc{};
    std::uint32_t outer_before{},outer_after{};
    SharedRestoreProbe():stream(SaveProbe::guarded_copy){stream.slots=&table;}
    static SharedRestoreProbe* owner(SaveStream* s){return reinterpret_cast<SharedRestoreProbe*>(s);}
    static void reject(SaveStream* s,SaveStream::Error error){s->fail(error);RaiseException(rejection,0,0,nullptr);}
    static std::uint64_t write(SaveStream* s,const void* source,std::uint64_t n){
        if(owner(s)->read_mode || s->transfers>=std::size(sizes) || n!=sizes[s->transfers]){
            reject(s,SaveStream::forbidden_operation);return 0;
        }
        return SaveStream::write(s,source,n);
    }
    static std::uint64_t read(SaveStream* s,void* destination,std::uint64_t n){
        auto* p=owner(s);
        if(!p->read_mode || s->failures || p->reads>=std::size(sizes) || n!=sizes[p->reads] ||
           s->read_position>s->write_position || n>s->write_position-s->read_position){
            reject(s,SaveStream::overflow);return 0;
        }
        if(!destination || !s->copier(destination,s->payload.data()+s->read_position,static_cast<size_t>(n))){
            reject(s,SaveStream::copy_fault);return 0;
        }
        s->read_position+=n; ++p->reads; return n;
    }
    inline static const SaveStreamSlots table={SaveStream::destroy,SaveStream::capacity,
        SaveStream::reset,SaveStream::set_write,SaveStream::set_read,write,read,SaveStream::tell_write,SaveStream::tell_read};
    static bool snapshot(uintptr_t b,std::byte* bytes,uintptr_t* pointers){
        size_t pos=0;
        for(size_t i=0;i<std::size(offsets);++i){
            if(!SaveProbe::guarded_copy(bytes+pos,reinterpret_cast<void*>(b+offsets[i]),sizes[i]))return false;
            pos+=sizes[i];
        }
        return SaveProbe::guarded_copy(pointers,reinterpret_cast<void*>(b+0x4846470),32);
    }
    void run(uintptr_t base,Routine writer=nullptr,Routine reader=nullptr) noexcept {
        if(attempted)return; attempted=true;
        LARGE_INTEGER start{},end{};QueryPerformanceCounter(&start);
        __try {
            Range entries[3]={{0,0},{SaveProbe::ptr(base+0x470DE90),0x973F0},{SaveProbe::ptr(base+0x470DE98),0x973F0}};
            Ranges ranges{entries,entries+3,entries+3};
            if(!entries[1].base || !entries[2].base || entries[1].base!=SaveProbe::ptr(base+0x470F1A0) ||
               entries[2].base!=SaveProbe::ptr(base+0x470F1A8) || !snapshot(base,before.data(),pointers_before)) {
                fault=true;
            } else {
                outer_before=*reinterpret_cast<std::uint32_t*>(base+0x470D0C4);
                if(!writer)writer=reinterpret_cast<Routine>(base+0x31DC00);
                if(!reader)reader=reinterpret_cast<Routine>(base+0x31DE50);
                writer(nullptr,&stream,&ranges);
                saved=!stream.failures && stream.write_position==byte_count && stream.transfers==9;
                if(saved){
                    // Validate every relocation index and decoded pointer before the first load write.
                    for(unsigned i=0;i<4;++i){
                        std::uint32_t index{};std::uint64_t displacement{};
                        std::memcpy(&index,stream.payload.data()+raw_count+i*16,4);
                        std::memcpy(&displacement,stream.payload.data()+raw_count+i*16+8,8);
                        if(index>=3 || (index && displacement>entries[index].length) ||
                           (index<3 && entries[index].base+displacement!=pointers_before[i])) saved=false;
                    }
                    if(saved){
                        read_mode=true;restore_invoked=true;reader(nullptr,&stream,&ranges);read_mode=false;
                        restored=!stream.failures && reads==9 && stream.read_position==byte_count;
                        outer_after=*reinterpret_cast<std::uint32_t*>(base+0x470D0C4);
                        equal=restored && snapshot(base,after.data(),pointers_after) && before==after &&
                            !std::memcmp(pointers_before,pointers_after,sizeof(pointers_before)) && outer_before==outer_after;
                    }
                }
                if(!saved || !restored || !equal)fault=true;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER){exception=GetExceptionCode();fault=true;}
        read_mode=false;QueryPerformanceCounter(&end);elapsed_qpc=end.QuadPart-start.QuadPart;
    }
};
static_assert(std::is_standard_layout_v<SharedRestoreProbe>);
static_assert(offsetof(SharedRestoreProbe,stream)==0);
}
