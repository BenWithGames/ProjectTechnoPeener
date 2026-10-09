#pragma once
#include "SaveStream.hpp"
// Included only by FrameRecorder.cpp, after Windows declarations.
namespace sc6::research {
struct SaveProbe {
    SaveStream stream;
    bool attempted{}, invoked{}, fault{}, preflight_ok{};
    std::uint64_t bookkeeping_before{}, bookkeeping_after{};
    LONGLONG elapsed_qpc{};
    static bool guarded_copy(void* d,const void* s,std::size_t n) {
        __try { std::memcpy(d,s,n); return true; }
        __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    SaveProbe():stream(guarded_copy) {}
    static uintptr_t ptr(uintptr_t a) { return *reinterpret_cast<const uintptr_t*>(a); }
    static bool method(uintptr_t object,unsigned slot,uintptr_t base,unsigned rva) {
        return object>=0x10000 && ptr(ptr(object)+slot)==base+rva;
    }
    static bool preflight(uintptr_t b) {
        for(unsigned p=0;p<2;++p) {
            auto player=ptr(b+0x470DE90+p*8);
            if(!player || player!=ptr(b+0x470F1A0+p*8)) return false;
            if(!method(player+0x35A0,0x28,b,0x30B7B0) || !method(player+0x27760,0x28,b,0x30B680)) return false;
            for(unsigned off: {0x400u,0x420u,0x440u}) {
                auto node=ptr(player+0x44078+off); unsigned count=0;
                while(node) {
                    if(++count>64) return false;
                    if(!method(node,0x30,b,0x30EA70) && !method(node,0x30,b,0x30EB50)) return false;
                    node=ptr(node+0x18);
                }
            }
        }
        auto manager=ptr(b+0x470DF28);
        if(!method(manager,8,b,0x31F480)) return false;
        if(ptr(manager+8)) {
            auto table=ptr(manager+0x90); if(!table) return false;
            for(unsigned i=0;i<16;++i) {
                auto object=ptr(table+0x270+8*i); if(!object) continue;
                // Only the paired type getter/writer combinations inspected for this build.
                bool ok=false;
                constexpr unsigned pairs[][2]={{0x301450,0x318860},{0x301470,0x33FBC0},
                    {0x31F420,0x31C190},{0x2D9BF0,0x340ED0},{0x301490,0x317BD0},{0x2D9C30,0x341110}};
                for(auto& pair:pairs) if(method(object,0xE0,b,pair[0]) && method(object,0x100,b,pair[1])) ok=true;
                if(!ok) return false;
            }
        }
        auto aux=ptr(manager+0xA0);
        if(aux) for(unsigned i=0;i<17;++i) {
            auto object=ptr(aux+0x10+8*i);
            if(object && !method(object,0x20,b,0x2D2BB0) && !method(object,0x20,b,0x32AC00)) return false;
        }
        return true;
    }
    // No restore routine is exposed. A fault invalidates the capture; it does not
    // prove the game can recover from an arbitrary exception inside its allocator.
    void run(uintptr_t base) noexcept {
        if(attempted) return;
        attempted=true;
        LARGE_INTEGER start{},end{}; QueryPerformanceCounter(&start);
        bool bookkeeping_read=false;
        __try {
            preflight_ok=preflight(base);
            if(preflight_ok) {
                bookkeeping_before=ptr(base+0x470E870); bookkeeping_read=true;
                invoked=true;
                using Writer=void* (*)(SaveStream*);
                reinterpret_cast<Writer>(base+0x3841E0)(&stream);
                bookkeeping_after=ptr(base+0x470E870);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) { fault=true; }
        // Preserve the known byte-count bookkeeping field; never load battle state.
        if(bookkeeping_read) {
            __try { *reinterpret_cast<std::uint64_t*>(base+0x470E870)=bookkeeping_before; }
            __except(EXCEPTION_EXECUTE_HANDLER) { fault=true; }
        }
        QueryPerformanceCounter(&end); elapsed_qpc=end.QuadPart-start.QuadPart;
    }
};
}
